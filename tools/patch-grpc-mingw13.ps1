param(
    [Parameter(Mandatory = $true)]
    [string]$GrpcHeader
)

$path = (Resolve-Path -LiteralPath $GrpcHeader -ErrorAction Stop).Path
$content = [IO.File]::ReadAllText($path)
$oldConstructor = 'explicit PerCpu(PerCpuOptions options) : shards_(options.Shards()) {}'
$oldMember = 'std::unique_ptr<T[]> data_{new T[shards_]};'
$newConstructor = 'explicit PerCpu(PerCpuOptions options) : shards_(options.Shards()), data_(new T[shards_]) {}'
$newMember = 'std::unique_ptr<T[]> data_;'

if ($content.Contains('data_(new T[shards_])') -and $content.Contains($newMember)) {
    Write-Output "gRPC workaround already applied: $path"
    return
}
if (-not $content.Contains($oldConstructor) -or -not $content.Contains($oldMember)) {
    throw "Unexpected gRPC PerCpu implementation: $path"
}

$content = $content.Replace($oldConstructor, $newConstructor).Replace($oldMember, $newMember)
[IO.File]::WriteAllText($path, $content, [Text.UTF8Encoding]::new($false))
Write-Output "Applied MinGW 13 workaround to gRPC cache: $path"
