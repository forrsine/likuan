param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild,
    [switch]$RunTests,
    [int]$Iterations = 10000
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build-msvc.ps1"
$BuildDir = Join-Path $Root "build-manual"
$OutDir = Join-Path $Root "out\demo"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Require-File {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required file not found: $Path"
    }
}

function Invoke-DemoStep {
    param(
        [string]$Title,
        [string]$Exe,
        [string[]]$Arguments
    )

    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Demo step failed: $Title"
    }
}

if (-not $SkipBuild) {
    if ($RunTests) {
        & $BuildScript -Configuration $Configuration -Test
    } else {
        & $BuildScript -Configuration $Configuration
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Build script failed."
    }
}

$RxCli = Join-Path $BuildDir "rx_cli.exe"
$DumpTokens = Join-Path $BuildDir "rx_dump_tokens.exe"
$DumpAst = Join-Path $BuildDir "rx_dump_ast.exe"
$DumpNfa = Join-Path $BuildDir "rx_dump_nfa.exe"
$DumpDfa = Join-Path $BuildDir "rx_dump_dfa.exe"
$DumpDot = Join-Path $BuildDir "rx_dump_dot.exe"
$Bench = Join-Path $BuildDir "rx_bench.exe"

@($RxCli, $DumpTokens, $DumpAst, $DumpNfa, $DumpDfa, $DumpDot, $Bench) |
    ForEach-Object { Require-File $_ }

$CapturePattern = "([a-z]+)(\d+)"
$PipelinePattern = "([a-z]+)\d{2,4}"
$SimplePattern = "a|b"
$NfaDot = Join-Path $OutDir "letters-digits.nfa.dot"
$DfaDot = Join-Path $OutDir "letters-digits.mindfa.dot"
$Csv = Join-Path $OutDir "performance.csv"

Invoke-DemoStep "1. DFA search with capture groups" `
    $RxCli @("--dfa", $CapturePattern, "--abc123!")

Invoke-DemoStep "2. Lexer token stream" `
    $DumpTokens @($PipelinePattern)

Invoke-DemoStep "3. Parser AST" `
    $DumpAst @($PipelinePattern)

Invoke-DemoStep "4. NFA transition table" `
    $DumpNfa @($SimplePattern)

Invoke-DemoStep "5. MinDFA transition table" `
    $DumpDfa @($SimplePattern)

Invoke-DemoStep "6. Graphviz DOT export (NFA)" `
    $DumpDot @("--nfa", "--output", $NfaDot, $PipelinePattern)

Invoke-DemoStep "7. Graphviz DOT export (MinDFA)" `
    $DumpDot @("--dfa", "--output", $DfaDot, $PipelinePattern)

Invoke-DemoStep "8. Benchmark CSV" `
    $Bench @("--iterations", [string]$Iterations, "--csv", $Csv)

$DotCommand = Get-Command dot -ErrorAction SilentlyContinue
if ($DotCommand -ne $null) {
    $NfaPng = Join-Path $OutDir "letters-digits.nfa.png"
    $DfaPng = Join-Path $OutDir "letters-digits.mindfa.png"
    Invoke-DemoStep "9. Render NFA PNG" `
        $DotCommand.Source @("-Tpng", $NfaDot, "-o", $NfaPng)
    Invoke-DemoStep "10. Render MinDFA PNG" `
        $DotCommand.Source @("-Tpng", $DfaDot, "-o", $DfaPng)
} else {
    Write-Host ""
    Write-Host "Graphviz dot was not found; DOT files were generated but PNG rendering was skipped." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Demo complete." -ForegroundColor Green
Write-Host "Outputs: $OutDir"
