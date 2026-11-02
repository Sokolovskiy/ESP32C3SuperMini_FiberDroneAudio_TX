param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$compilerPath = (Get-Command $Compiler -CommandType Application -ErrorAction Stop).Source
$env:PATH = (Split-Path $compilerPath -Parent) + ';' + $env:PATH
$projectRoot = Split-Path $PSScriptRoot -Parent
$output = Join-Path $projectRoot 'build\test_audio.exe'
New-Item -ItemType Directory -Force (Split-Path $output -Parent) | Out-Null
& $compilerPath -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $projectRoot 'main') `
    (Join-Path $PSScriptRoot 'test_audio.c') `
    (Join-Path $projectRoot 'main\audio_processing.c') `
    (Join-Path $projectRoot 'main\audio_protocol.c') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
