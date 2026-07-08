param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Test
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build-manual"
$Sdk = "C:\Program Files\Microsoft Visual Studio\2022\Community\SDK\ScopeCppSDK\vc15"
$Cl = Join-Path $Sdk "VC\bin\cl.exe"

if (-not (Test-Path -LiteralPath $Cl)) {
    throw "Scope MSVC compiler not found: $Cl"
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$CommonSources = @(
    "src\charset.c",
    "src\ast.c",
    "src\lexer.c",
    "src\parser.c",
    "src\nfa.c",
    "src\matcher.c",
    "src\capture.c",
    "src\dfa.c",
    "src\dfa_minimize.c",
    "src\dot.c",
    "src\regex_engine.c"
) | ForEach-Object { Join-Path $Root $_ }

$Tests = @(
    "test_lexer",
    "test_parser",
    "test_nfa",
    "test_dfa",
    "test_dot",
    "test_api",
    "test_stress",
    "test_conformance"
)

$Tools = @(
    "rx_cli",
    "rx_dump_tokens",
    "rx_dump_ast",
    "rx_dump_nfa",
    "rx_dump_dfa",
    "rx_dump_dot",
    "rx_bench"
)

function Build-Target {
    param(
        [string]$Name,
        [string]$Source
    )

    $Output = Join-Path $BuildDir "$Name.exe"
    $Arguments = @(
        "/nologo",
        "/W4",
        "/DRX_HAVE_POSIX_REGEX=0",
        "/I$(Join-Path $Root 'include')",
        "/I$(Join-Path $Root 'src')",
        "/I$(Join-Path $Sdk 'SDK\include\ucrt')",
        "/I$(Join-Path $Sdk 'VC\include')"
    )
    if ($Configuration -eq "Release") {
        $Arguments += "/O2"
    } else {
        $Arguments += @("/Od", "/Zi", "/Fd:$(Join-Path $BuildDir "$Name.pdb")")
    }
    $Arguments += $CommonSources
    $Arguments += $Source
    $Arguments += @(
        "/Fe:$Output",
        "/link",
        "/LIBPATH:$(Join-Path $Sdk 'VC\lib')",
        "/LIBPATH:$(Join-Path $Sdk 'SDK\lib')",
        "libcmt.lib",
        "libucrt.lib",
        "vcruntime.lib",
        "kernel32.lib"
    )
    if ($Configuration -eq "Debug") {
        $Arguments += "/DEBUG"
    }

    Write-Host "Building $Name ($Configuration)..."
    & $Cl @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $Name"
    }
}

Push-Location $BuildDir
try {
    foreach ($Name in $Tests) {
        Build-Target -Name $Name -Source (Join-Path $Root "tests\$Name.c")
    }
    foreach ($Name in $Tools) {
        Build-Target -Name $Name -Source (Join-Path $Root "tools\$Name.c")
    }

    if ($Test) {
        foreach ($Name in $Tests) {
            Write-Host "Running $Name..."
            & (Join-Path $BuildDir "$Name.exe")
            if ($LASTEXITCODE -ne 0) {
                throw "Test failed: $Name"
            }
        }
    }
} finally {
    Pop-Location
}

Write-Host "Build complete: $BuildDir"
