param(
    [string]$SourceRoot,
    [string]$TargetRoot,
    [string]$TargetJavaRoot
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = (Resolve-Path (Join-Path $scriptDir '..')).Path
}
if ([string]::IsNullOrWhiteSpace($TargetRoot)) {
    $TargetRoot = $env:DOOMXR_SYNC_TARGET_ROOT
}
if ([string]::IsNullOrWhiteSpace($TargetJavaRoot)) {
    $TargetJavaRoot = $env:DOOMXR_SYNC_TARGET_JAVA_ROOT
}

if (-not (Test-Path -LiteralPath $SourceRoot)) {
    throw "Source root not found: $SourceRoot"
}

if ([string]::IsNullOrWhiteSpace($TargetRoot)) {
    throw 'Target root not provided. Pass -TargetRoot or set DOOMXR_SYNC_TARGET_ROOT.'
}

if (-not (Test-Path -LiteralPath $TargetRoot)) {
    throw "Target root not found: $TargetRoot"
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')

if ($source -eq $target) {
    throw 'Source and target roots must be different.'
}

Write-Host "Syncing DoomXR source tree"
Write-Host "  From: $source"
Write-Host "  To:   $target"

$excludeDirs = @(
    '.git',
    '.gradle',
    '.vs',
    'build',
    'obj'
)

$robocopyArgs = @(
    $source,
    $target,
    '/MIR',
    '/NFL',
    '/NDL',
    '/NJH',
    '/NJS',
    '/NP',
    '/R:1',
    '/W:1'
)

foreach ($dir in $excludeDirs) {
    $robocopyArgs += '/XD'
    $robocopyArgs += (Join-Path $source $dir)
}

$process = Start-Process -FilePath 'robocopy.exe' -ArgumentList $robocopyArgs -NoNewWindow -Wait -PassThru
$exitCode = $process.ExitCode

if ($exitCode -ge 8) {
    throw "robocopy failed with exit code $exitCode"
}

Write-Host "Sync complete with robocopy exit code $exitCode"

$sourceJavaRoot = Join-Path $source 'java\com\ermac\doomxr'
if ((-not [string]::IsNullOrWhiteSpace($TargetJavaRoot)) -and (Test-Path -LiteralPath $sourceJavaRoot)) {
    if (-not (Test-Path -LiteralPath $TargetJavaRoot)) {
        throw "Target Java root not found: $TargetJavaRoot"
    }

    $targetJava = (Resolve-Path -LiteralPath $TargetJavaRoot).Path.TrimEnd('\')
    Write-Host "Syncing DoomXR Java overlay"
    Write-Host "  From: $sourceJavaRoot"
    Write-Host "  To:   $targetJava"

    $javaArgs = @(
        $sourceJavaRoot,
        $targetJava,
        '/MIR',
        '/NFL',
        '/NDL',
        '/NJH',
        '/NJS',
        '/NP',
        '/R:1',
        '/W:1'
    )

    $javaProcess = Start-Process -FilePath 'robocopy.exe' -ArgumentList $javaArgs -NoNewWindow -Wait -PassThru
    $javaExitCode = $javaProcess.ExitCode
    if ($javaExitCode -ge 8) {
        throw "java robocopy failed with exit code $javaExitCode"
    }

    Write-Host "Java sync complete with robocopy exit code $javaExitCode"
}
