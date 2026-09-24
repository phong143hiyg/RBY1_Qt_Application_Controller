$ErrorActionPreference = "Stop"

$dockerDesktop = "C:\Program Files\Docker\Docker\Docker Desktop.exe"
$pythonw = "C:\Python314\pythonw.exe"
$proxyScript = Join-Path $PSScriptRoot "rby1_docker_proxy.py"
$composeDirectory = "/home/phongday/MyFolder-Linux/mynameisrobot/rby1-docker"

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
    if (!(Test-Path -LiteralPath $dockerDesktop)) {
        throw "Không tìm thấy Docker Desktop: $dockerDesktop"
    }
    Start-Process -FilePath $dockerDesktop -WindowStyle Hidden

    $dockerReady = $false
    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        Start-Sleep -Seconds 2
        docker info *> $null
        if ($LASTEXITCODE -eq 0) {
            $dockerReady = $true
            break
        }
    }
    if (!$dockerReady) {
        throw "Docker Desktop chưa sẵn sàng sau 60 giây."
    }
}

wsl -d Ubuntu-20.04 -- bash -lc "cd '$composeDirectory' && docker compose up -d rby1-sim rby1-ros2"
if ($LASTEXITCODE -ne 0) {
    throw "Không thể khởi động RBY1 simulator/model M."
}

if (!(Test-LocalPort 55051)) {
    if (!(Test-Path -LiteralPath $pythonw)) {
        throw "Không tìm thấy Python: $pythonw"
    }
    Start-Process -FilePath $pythonw -ArgumentList @($proxyScript) -WindowStyle Hidden
}

for ($attempt = 0; $attempt -lt 15; ++$attempt) {
    if (Test-LocalPort 55051) {
        Write-Host "RBY1-M simulator is ready at 127.0.0.1:55051" -ForegroundColor Green
        exit 0
    }
    Start-Sleep -Seconds 1
}

throw "Proxy không mở được 127.0.0.1:55051. Kiểm tra docker logs rby1-sim."
