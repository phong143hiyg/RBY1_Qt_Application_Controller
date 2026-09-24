"""Standalone asyncio/stdlib protocol simulator. NEVER invokes ROS, IK or a robot."""
import argparse
import asyncio
import copy
import json
import math
import time
from pathlib import Path

FIXTURE = Path(__file__).resolve().parents[1] / 'planning_protocol/fixtures/contract-v1.json'
LIMIT = 1024 * 1024


def validate_pose(p):
    if not isinstance(p, dict) or not isinstance(p.get('frame_id'), str) or not p['frame_id'].strip():
        raise ValueError('Pose requires frame_id')
    for key, size in [('position', 3), ('orientation_xyzw', 4)]:
        v = p.get(key)
        if not isinstance(v, list) or len(v) != size or any(
                isinstance(n, bool) or not isinstance(n, (int, float)) or not math.isfinite(n) for n in v):
            raise ValueError('Pose requires finite position[3], orientation_xyzw[4]')
    if abs(sum(n * n for n in p['orientation_xyzw']) - 1) > .002001:
        raise ValueError('Quaternion norm must be 1; tolerance .001')


def validate_plan(command, p, scene, caps):
    for key in ['scene_revision', 'group', 'tcp_frame']:
        if p.get(key) != scene[key]:
            raise ValueError('Scene revision/group/TCP mismatch')
    for key, maximum in [('velocity_scale', 1), ('acceleration_scale', 1),
                         ('planning_timeout_s', caps['max_planning_timeout_s'])]:
        n = p.get(key)
        if isinstance(n, bool) or not isinstance(n, (int, float)) or not math.isfinite(n) or not 0 < n <= maximum:
            raise ValueError(key + ' outside finite permitted range')
    if command == 'plan_to_pose':
        validate_pose(p.get('goal_tcp_pose'))
    else:
        if not any(o['id'] == p.get('object_id') and o['role'] == 'target' for o in scene['objects']):
            raise ValueError('Unknown target object')
        validate_pose(p.get('pick_tcp_pose'))
        validate_pose(p.get('place_object_pose'))
        for key in ['approach_distance_m', 'lift_distance_m', 'retreat_distance_m']:
            n = p.get(key)
            if isinstance(n, bool) or not isinstance(n, (int, float)) or not math.isfinite(n) or n < 0:
                raise ValueError(key + ' must be finite and nonnegative')


class Connection:
    def __init__(self, writer, args):
        self.writer, self.args = writer, args
        self.lock = asyncio.Lock()

    async def send(self, *frames):
        async with self.lock:
            if self.writer.is_closing():
                return
            try:
                chunks = [json.dumps(f, allow_nan=False, separators=(',', ':')).encode() + b'\n' for f in frames]
                if self.args.coalesce:
                    chunks = [b''.join(chunks)]
                for chunk in chunks:
                    if self.args.fragment:
                        for i in range(0, len(chunk), 17):
                            self.writer.write(chunk[i:i + 17])
                            await self.writer.drain()
                            await asyncio.sleep(.001)
                    else:
                        self.writer.write(chunk)
                        await self.writer.drain()
            except (ConnectionError, OSError):
                pass


class MockServer:
    def __init__(self, args):
        self.args = args
        self.fixture = json.loads(FIXTURE.read_text(encoding='utf-8'))
        self.caps = copy.deepcopy(self.fixture['capabilities'])
        self.scene = copy.deepcopy(self.fixture['scene'])
        self.revision = 1
        self.tasks, self.plans, self.seen = {}, {}, {}
        self.active = None

    def cleanup(self):
        now = time.monotonic()
        self.tasks = {k: v for k, v in self.tasks.items()
                      if v['terminal_at'] is None or now - v['terminal_at'] < self.caps['task_status_ttl_s']}
        self.plans = {k: v for k, v in self.plans.items() if now < v['expires']}
        self.seen = {k: t for k, t in self.seen.items() if now - t < self.caps['task_status_ttl_s']}

    @staticmethod
    def envelope(request, kind='response', payload=None, **kw):
        return dict(protocol_version=1, type=kind, request_id=request['request_id'],
                    command=request['command'], payload=payload or {}, **kw)

    async def error(self, connection, request, code, message, stage='request'):
        await connection.send(self.envelope(request, ok=False,
            error=dict(code=code, message=message, stage=stage, details=dict(simulated=True))))

    def event(self, task, status, stage, payload=None, error=None):
        task['seq'] += 1
        e = self.envelope(task['request'], 'event', payload, event_seq=task['seq'], status=status, stage=stage)
        if error:
            e['error'] = error
        task['snapshot'] = e
        return e

    async def worker(self, task):
        timeout = task['request']['payload']['planning_timeout_s']
        delay = self.args.delay
        if self.args.mode == 'timeout':
            delay = timeout + .1
        started = time.monotonic()
        # Bounded worker; cancellation is only confirmed once this worker stops.
        for stage in ['approach', 'lift', 'transfer']:
            await asyncio.sleep(min(delay, timeout) / 3)
            if task['cancel'] or time.monotonic() - started >= timeout:
                break
            await task['connection'].send(self.event(task, 'planning', stage, dict(progress=task['seq'] / 5)))
        if task['cancel']:
            terminal = self.event(task, 'cancelled', 'worker_stopped', dict(simulated=True))
        elif time.monotonic() - started >= timeout:
            terminal = self.event(task, 'failed', 'deadline', error=dict(code='TIMEOUT',
                message='Simulated worker deadline', stage='deadline', details=dict(simulated=True)))
        elif self.args.mode == 'failure' or task['scene']['scenario_id'] in [
                'goal_in_collision', 'unreachable_goal', 'start_in_collision', 'attached_object_clearance']:
            code = {'goal_in_collision': 'GOAL_IN_COLLISION', 'start_in_collision': 'START_IN_COLLISION'}.get(
                task['scene']['scenario_id'], 'PLANNING_FAILED')
            terminal = self.event(task, 'failed', 'transfer', error=dict(code=code,
                message='SIMULATED scenario failure; no IK/collision evidence from mock',
                stage='transfer', details=dict(simulated=True)))
        else:
            p = copy.deepcopy(self.fixture['result'])
            p.update(plan_id='mock-plan-' + task['request']['request_id'],
                     scene_revision=task['scene']['scene_revision'], planning_time_s=time.monotonic() - started)
            self.plans[p['plan_id']] = dict(result=p, scene=task['scene'], start_state='simulated-only',
                                           expires=time.monotonic() + self.caps['plan_ttl_s'])
            terminal = self.event(task, 'succeeded', 'complete', p)
            if self.args.mode == 'stale-revision':
                self.revision += 1
                self.scene['scene_revision'] = 'mock-scene-' + str(self.revision)
        task['terminal_at'] = time.monotonic()
        self.active = None
        await task['connection'].send(terminal)
        if self.args.mode == 'terminal-first':
            await asyncio.sleep(self.args.ack_delay)
            await task['connection'].send(self.envelope(task['request'], ok=True))

    async def handle(self, connection, r):
        self.cleanup()
        if not isinstance(r, dict):
            raise ValueError('Envelope must be object')
        if not isinstance(r.get('request_id'), str) or not r['request_id'] or not isinstance(r.get('command'), str):
            raise ValueError('String request_id and command required')
        if r.get('protocol_version') != 1 or r.get('type') != 'request' or not isinstance(r.get('payload'), dict):
            await self.error(connection, r, 'INVALID_REQUEST', 'Invalid v1 request envelope')
            return
        if r['request_id'] in self.seen:
            await self.error(connection, r, 'INVALID_REQUEST', 'Duplicate request_id; planning is never resubmitted')
            return
        self.seen[r['request_id']] = time.monotonic()
        cmd, p = r['command'], r['payload']
        if cmd not in self.caps['supported_commands']:
            await self.error(connection, r, 'UNSUPPORTED_COMMAND', 'No execution or robot commands')
        elif cmd == 'get_capabilities':
            await connection.send(self.envelope(r, payload=self.caps, ok=True))
        elif cmd == 'get_scene':
            await connection.send(self.envelope(r, payload=self.scene, ok=True))
        elif cmd == 'load_test_scene':
            if self.active:
                await self.error(connection, r, 'BUSY', 'One planning worker is active')
            elif p.get('scenario_id') not in self.caps['scenarios']:
                await self.error(connection, r, 'INVALID_REQUEST', 'Unknown scenario ID; paths are not accepted')
            else:
                self.revision += 1
                self.scene = copy.deepcopy(self.fixture['scene'])
                self.scene.update(scenario_id=p['scenario_id'], scene_revision='mock-scene-' + str(self.revision))
                if p['scenario_id'] == 'baseline':
                    self.scene['objects'] = [o for o in self.scene['objects'] if o['role'] != 'obstacle']
                await connection.send(self.envelope(r, payload=self.scene, ok=True))
        elif cmd in ['plan_to_pose', 'plan_pick_place']:
            if self.active:
                await self.error(connection, r, 'BUSY', 'One planning worker is active')
                return
            if p.get('scene_revision') != self.scene['scene_revision']:
                await self.error(connection, r, 'STALE_PLAN', 'Scene revision mismatch')
                return
            try:
                validate_plan(cmd, p, self.scene, self.caps)
            except (ValueError, TypeError, OverflowError) as e:
                await self.error(connection, r, 'INVALID_REQUEST', str(e))
                return
            task = dict(request=r, scene=copy.deepcopy(self.scene), cancel=False, seq=0,
                        connection=connection, terminal_at=None)
            task['snapshot'] = self.envelope(r, 'event', event_seq=0, status='planning', stage='accepted')
            self.tasks[r['request_id']] = task
            self.active = r['request_id']
            if self.args.mode != 'terminal-first':
                await connection.send(self.envelope(r, ok=True), self.event(task, 'planning', 'accepted', dict(progress=0)))
            asyncio.create_task(self.worker(task))
            if self.args.mode == 'disconnect':
                connection.writer.close()  # Worker survives; status remains available after reconnect.
        elif cmd == 'get_task_status':
            task = self.tasks.get(p.get('target_request_id'))
            if not task:
                await self.error(connection, r, 'STATE_UNAVAILABLE', 'Unknown or expired task; cannot confirm cancellation')
            else:
                task['connection'] = connection
                await connection.send(self.envelope(r, payload=task['snapshot'], ok=True))
        elif cmd == 'cancel_planning':
            task = self.tasks.get(p.get('target_request_id'))
            if not task:
                await self.error(connection, r, 'STATE_UNAVAILABLE', 'Unknown or expired task')
            elif task['terminal_at'] is not None:
                await connection.send(self.envelope(r, payload=dict(already_terminal=True), ok=True))
            else:
                task['cancel'] = True
                task['connection'] = connection
                await connection.send(self.envelope(r, payload=dict(cancel_requested=True), ok=True))
        elif cmd == 'preview_plan':
            plan = self.plans.get(p.get('plan_id'))
            if not plan or plan['result']['scene_revision'] != self.scene['scene_revision'] or plan['result']['robot_model_id'] != self.scene['robot_model_id']:
                await self.error(connection, r, 'STALE_PLAN', 'Expired plan or scene/model mismatch')
            else:
                await connection.send(self.envelope(r, payload=dict(plan_id=p['plan_id'], simulated=True,
                    rviz_displayed=False, message='MOCK preview metadata only; no RViz/MoveIt trajectory'), ok=True))

    async def client(self, reader, writer):
        connection = Connection(writer, self.args)
        try:
            while True:
                line = await reader.readuntil(b'\n')
                if len(line) - 1 > LIMIT:
                    break
                r = json.loads(line.decode('utf-8'), parse_constant=lambda n: (_ for _ in ()).throw(ValueError(n)))
                await self.handle(connection, r)
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, ValueError, UnicodeError, ConnectionError, OSError):
            pass
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, OSError):
                pass


async def main(args):
    mock = MockServer(args)
    server = await asyncio.start_server(mock.client, args.host, args.port, limit=LIMIT + 1)
    print('MOCK planning protocol v1 listening on ' + str(server.sockets[0].getsockname()) +
          ' — NO IK, collision checking, MoveIt, RViz or robot control', flush=True)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8082)
    parser.add_argument('--mode', choices=['normal', 'failure', 'timeout', 'disconnect', 'terminal-first', 'stale-revision'], default='normal')
    parser.add_argument('--delay', type=float, default=.6)
    parser.add_argument('--ack-delay', type=float, default=.1)
    parser.add_argument('--fragment', action='store_true')
    parser.add_argument('--coalesce', action='store_true')
    options = parser.parse_args()
    if not math.isfinite(options.delay) or options.delay < 0 or not math.isfinite(options.ack_delay) or options.ack_delay < 0:
        parser.error('Delays must be finite and nonnegative')
    try:
        asyncio.run(main(options))
    except KeyboardInterrupt:
        pass
