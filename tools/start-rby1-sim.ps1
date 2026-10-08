param(
    [string]$ComposeDirectory = $env:RBY1_SIM_COMPOSE_DIR,
    [string]$WslDistribution = $(if ($env:RBY1_SIM_WSL_DISTRO) { $env:RBY1_SIM_WSL_DISTRO } else { "Ubuntu-20.04" })
)

$ErrorActionPreference = "Stop"

function Test-LocalPort([int]$Port) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $attempt = $client.ConnectAsync("127.0.0.1", $Port)
        return $attempt.Wait(800) -and $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

docker info *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Docker is not ready. Start Docker Desktop and try again."
}

if ([string]::IsNullOrWhiteSpace($ComposeDirectory)) {
    throw "Set RBY1_SIM_COMPOSE_DIR or pass -ComposeDirectory for the WSL compose directory."
}

wsl -d $WslDistribution -- bash -lc "cd '$ComposeDirectory' && docker compose up -d rby1-sim"
if ($LASTEXITCODE -ne 0) {
    throw "Could not start the RBY1 simulator/model M."
}

for ($attempt = 0; $attempt -lt 15; ++$attempt) {
    if (Test-LocalPort 50051) {
        Write-Host "RBY1-M simulator SDK is ready at 127.0.0.1:50051" -ForegroundColor Green
        exit 0
    }
    Start-Sleep -Seconds 1
}

throw "RBY1 SDK endpoint did not open 127.0.0.1:50051. Check port mapping and rby1-sim logs."
