"""Real TCP contract tests against the stdlib mock, using the same Qt fixture."""
import argparse
import asyncio
import copy
import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mock_planning', ROOT / 'tools/mock_planning_server.py')
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)


class ContractTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.args = argparse.Namespace(coalesce=True, fragment=True, delay=.09, ack_delay=.03, mode='normal')
        self.backend = mock.MockServer(self.args)
        self.server = await asyncio.start_server(self.backend.client, '127.0.0.1', 0, limit=mock.LIMIT + 1)
        self.port = self.server.sockets[0].getsockname()[1]
        self.reader, self.writer = await asyncio.open_connection('127.0.0.1', self.port)
        self.counter = 0

    async def asyncTearDown(self):
        if self.backend.active:
            self.backend.tasks[self.backend.active]['cancel'] = True
            await asyncio.sleep(.25)
        self.writer.close()
        await self.writer.wait_closed()
        self.server.close()
        await self.server.wait_closed()

    async def send(self, command, p=None):
        self.counter += 1
        rid = 'python-contract-' + str(self.counter)
        request = dict(protocol_version=1, type='request', request_id=rid, command=command, payload=p or {})
        data = json.dumps(request).encode() + b'\n'
        # Fragment requests independently of fragmented/coalesced server replies.
        self.writer.write(data[:7]); await self.writer.drain()
        self.writer.write(data[7:]); await self.writer.drain()
        return rid

    async def read(self):
        return json.loads(await asyncio.wait_for(self.reader.readline(), 3))

    async def response(self, rid):
        while True:
            e = await self.read()
            if e['request_id'] == rid and e['type'] == 'response':
                return e

    async def terminal(self, rid):
        events = []
        while True:
            e = await self.read()
            if e['request_id'] == rid and e['type'] == 'event':
                events.append(e)
                if e['status'] != 'planning':
                    self.assertEqual([x['event_seq'] for x in events], sorted(set(x['event_seq'] for x in events)))
                    return e

    def plan_payload(self):
        return copy.deepcopy(self.backend.fixture['requests']['plan_pick_place']['payload'])

    async def test_capabilities_scene_preview_invariance_and_stale(self):
        rid = await self.send('get_capabilities'); caps = (await self.response(rid))['payload']
        self.assertFalse(caps['execution_enabled']); self.assertEqual(caps['backend_mode'], 'mock')
        before = copy.deepcopy(self.backend.scene)
        rid = await self.send('plan_pick_place', self.plan_payload())
        self.assertTrue((await self.response(rid))['ok'])
        terminal = await self.terminal(rid); self.assertEqual(terminal['status'], 'succeeded')
        self.assertTrue(terminal['payload']['validation']['simulated'])
        self.assertEqual(terminal['payload']['validation']['collision'], 'not_checked')
        plan = terminal['payload']['plan_id']
        preview = await self.send('preview_plan', dict(plan_id=plan)); result = await self.response(preview)
        self.assertFalse(result['payload']['rviz_displayed']); self.assertEqual(self.backend.scene, before)
        load = await self.send('load_test_scene', dict(scenario_id='baseline')); self.assertTrue((await self.response(load))['ok'])
        preview = await self.send('preview_plan', dict(plan_id=plan)); self.assertEqual((await self.response(preview))['error']['code'], 'STALE_PLAN')

    async def test_busy_cancel_and_reconnect(self):
        self.args.delay = .6
        rid = await self.send('plan_pick_place', self.plan_payload()); await self.response(rid)
        busy = await self.send('load_test_scene', dict(scenario_id='baseline'))
        self.assertEqual((await self.response(busy))['error']['code'], 'BUSY')
        busy = await self.send('plan_pick_place', self.plan_payload())
        self.assertEqual((await self.response(busy))['error']['code'], 'BUSY')
        self.writer.close(); await self.writer.wait_closed()
        self.reader, self.writer = await asyncio.open_connection('127.0.0.1', self.port)
        status = await self.send('get_task_status', dict(target_request_id=rid)); snap = (await self.response(status))['payload']
        self.assertEqual(snap['request_id'], rid); self.assertEqual(snap['status'], 'planning')
        cancel = await self.send('cancel_planning', dict(target_request_id=rid)); self.assertTrue((await self.response(cancel))['ok'])
        terminal = await self.terminal(rid); self.assertEqual(terminal['status'], 'cancelled')
        self.assertNotIn('plan_id', terminal['payload'])
        status = await self.send('get_task_status', dict(target_request_id=rid)); self.assertEqual((await self.response(status))['payload']['status'], 'cancelled')
        self.assertEqual(len(self.backend.plans), 0)

    async def test_failures_deadline_and_terminal_before_ack(self):
        for mode, status in [('failure', 'failed'), ('timeout', 'failed'), ('terminal-first', 'succeeded')]:
            self.args.mode = mode
            p = self.plan_payload(); p['planning_timeout_s'] = .04 if mode == 'timeout' else 5
            rid = await self.send('plan_pick_place', p)
            if mode != 'terminal-first':
                self.assertTrue((await self.response(rid))['ok'])
            terminal = await self.terminal(rid); self.assertEqual(terminal['status'], status)
            if mode == 'timeout': self.assertEqual(terminal['error']['code'], 'TIMEOUT')
            if mode == 'terminal-first': self.assertTrue((await self.response(rid))['ok'])
            # No second terminal event is stored or scheduled.
            self.assertEqual(self.backend.tasks[rid]['snapshot'], terminal)

    async def test_invalid_input_and_unknown_commands(self):
        for key, value in [('velocity_scale', 0), ('acceleration_scale', 1.1), ('planning_timeout_s', -1), ('lift_distance_m', -1)]:
            p = self.plan_payload(); p[key] = value
            rid = await self.send('plan_pick_place', p); self.assertEqual((await self.response(rid))['error']['code'], 'INVALID_REQUEST')
        p = self.plan_payload(); p['pick_tcp_pose']['orientation_xyzw'] = [0, 0, 0, 0]
        rid = await self.send('plan_pick_place', p); self.assertEqual((await self.response(rid))['error']['code'], 'INVALID_REQUEST')
        rid = await self.send('execute_plan'); self.assertEqual((await self.response(rid))['error']['code'], 'UNSUPPORTED_COMMAND')
        rid = await self.send('load_test_scene', dict(scenario_id='../../file.yaml'))
        self.assertEqual((await self.response(rid))['error']['code'], 'INVALID_REQUEST')
        rid = await self.send('get_task_status', dict(target_request_id='missing'))
        self.assertEqual((await self.response(rid))['error']['code'], 'STATE_UNAVAILABLE')

    async def test_ttl_and_frame_limit(self):
        rid = await self.send('plan_pick_place', self.plan_payload()); await self.response(rid)
        e = await self.terminal(rid); plan = e['payload']['plan_id']
        self.backend.plans[plan]['expires'] = 0
        preview = await self.send('preview_plan', dict(plan_id=plan))
        self.assertEqual((await self.response(preview))['error']['code'], 'STALE_PLAN')
        self.backend.tasks[rid]['terminal_at'] = 0
        status = await self.send('get_task_status', dict(target_request_id=rid))
        self.assertEqual((await self.response(status))['error']['code'], 'STATE_UNAVAILABLE')
        self.writer.write(b'x' * (mock.LIMIT + 2) + b'\n'); await self.writer.drain()
        try:
            self.assertEqual(await asyncio.wait_for(self.reader.read(1), 3), b'')
        except ConnectionResetError:
            pass

    def test_nonfinite_validation(self):
        for v in [float('nan'), float('inf'), -float('inf')]:
            p = self.plan_payload(); p['velocity_scale'] = v
            with self.assertRaises(ValueError): mock.validate_plan('plan_pick_place', p, self.backend.scene, self.backend.caps)
            pose = copy.deepcopy(self.backend.scene['pick_tcp_pose']); pose['position'][0] = v
            with self.assertRaises(ValueError): mock.validate_pose(pose)


if __name__ == '__main__':
    unittest.main(verbosity=2)
