param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Knights of the Old Republic II",
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$SupportedShas = @(
    "6A522E71631DCEE93467BD2010F3B23D9145326E1E2E89305F13AB104DBBFFEF",
    "4AB72FC1AB082F427E008CDA32FC5602D27B4E12FEF48C4A1A2C6F7B2F36FB5A"
)
$GameExe = Join-Path $GameDir "swkotor2.exe"

if (Get-Process -Name "swkotor2" -ErrorAction SilentlyContinue) {
    throw "Close KOTOR 2 before staging the animation probe."
}
if (-not (Test-Path -LiteralPath $GameExe -PathType Leaf)) {
    throw "KOTOR 2 executable not found: $GameExe"
}

$ActualSha = (Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash
if ($SupportedShas -notcontains $ActualSha) {
    throw "Unsupported swkotor2.exe SHA-256: $ActualSha"
}

$CoreDll = Join-Path $RepoRoot "Patches\CustomAnimationCore\windows_x86.dll"
$ProbeDll = Join-Path $RepoRoot "Patches\CustomAnimationK2DrexlTest\windows_x86.dll"
$PatcherDll = Join-Path $RepoRoot "bin\Release\KotorPatcher.dll"
$SqliteDll = Join-Path $RepoRoot "bin\Release\sqlite3.dll"
$AddressDb = Join-Path $RepoRoot "AddressDatabases\kotor2_steam_aspyr.db"
$ProbeAdditional = Join-Path $RepoRoot "Patches\CustomAnimationK2DrexlTest\additional"

$RequiredInputs = @(
    $CoreDll,
    $ProbeDll,
    $PatcherDll,
    $SqliteDll,
    $AddressDb,
    (Join-Path $ProbeAdditional "c_drexlf.mdl"),
    (Join-Path $ProbeAdditional "c_drexlf.mdx"),
    (Join-Path $ProbeAdditional "kpm98_drx.utc")
)
foreach ($Path in $RequiredInputs) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required build artifact not found: $Path"
    }
}

$Stamp = Get-Date -Format "yyyyMMddTHHmmss"
$BackupRoot = Join-Path $RepoRoot "logs\k2-drexl-staging\$Stamp"
New-Item -ItemType Directory -Path $BackupRoot -Force | Out-Null

function Backup-IfPresent {
    param([string]$RelativePath)
    $Source = Join-Path $GameDir $RelativePath
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        return
    }
    $Destination = Join-Path $BackupRoot $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$TouchedRelativePaths = @(
    "KotorPatcher.dll",
    "sqlite3.dll",
    "addresses.db",
    "patch_config.toml",
    "patches\custom-animation-core.dll",
    "patches\custom-animation-k2-drexl-test.dll",
    "Override\c_drexlf.mdl",
    "Override\c_drexlf.mdx",
    "Override\kpm98_drx.utc"
)
foreach ($RelativePath in $TouchedRelativePaths) {
    Backup-IfPresent $RelativePath
}

$PatchesDir = Join-Path $GameDir "patches"
$OverrideDir = Join-Path $GameDir "Override"
New-Item -ItemType Directory -Path $PatchesDir -Force | Out-Null
New-Item -ItemType Directory -Path $OverrideDir -Force | Out-Null

Copy-Item -LiteralPath $PatcherDll -Destination (Join-Path $GameDir "KotorPatcher.dll") -Force
Copy-Item -LiteralPath $SqliteDll -Destination (Join-Path $GameDir "sqlite3.dll") -Force
Copy-Item -LiteralPath $AddressDb -Destination (Join-Path $GameDir "addresses.db") -Force
Copy-Item -LiteralPath $CoreDll -Destination (Join-Path $PatchesDir "custom-animation-core.dll") -Force
Copy-Item -LiteralPath $ProbeDll -Destination (Join-Path $PatchesDir "custom-animation-k2-drexl-test.dll") -Force
Copy-Item -LiteralPath (Join-Path $ProbeAdditional "c_drexlf.mdl") -Destination (Join-Path $OverrideDir "c_drexlf.mdl") -Force
Copy-Item -LiteralPath (Join-Path $ProbeAdditional "c_drexlf.mdx") -Destination (Join-Path $OverrideDir "c_drexlf.mdx") -Force
Copy-Item -LiteralPath (Join-Path $ProbeAdditional "kpm98_drx.utc") -Destination (Join-Path $OverrideDir "kpm98_drx.utc") -Force

$Config = @"
target_version_sha = "$ActualSha"

[[patches]]
id = "custom-animation-core"
dll = "patches/custom-animation-core.dll"

[[patches.hooks]]
address = 5124720
original_bytes = [85, 139, 236, 106, 255]
type = "detour"
function = "OverridePlayAnimationRequest"

[[patches.hooks.parameters]]
source = "ecx"
type = "pointer"

[[patches.hooks.parameters]]
source = "esp+4"
type = "pointer"

[[patches.hooks]]
address = 5098960
original_bytes = [85, 139, 236, 81, 137, 77, 252]
type = "detour"
function = "LogAnimRunConstruct"

[[patches.hooks.parameters]]
source = "ecx"
type = "pointer"

[[patches.hooks.parameters]]
source = "esp+4"
type = "pointer"

[[patches]]
id = "custom-animation-k2-drexl-test"
dll = "patches/custom-animation-k2-drexl-test.dll"
"@
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $GameDir "patch_config.toml"), $Config, $Utf8NoBom)

$Result = [ordered]@{
    staged_at = (Get-Date).ToString("o")
    game_dir = $GameDir
    game_sha256 = $ActualSha
    backup_root = $BackupRoot
    core_dll_sha256 = (Get-FileHash -LiteralPath (Join-Path $PatchesDir "custom-animation-core.dll") -Algorithm SHA256).Hash
    probe_dll_sha256 = (Get-FileHash -LiteralPath (Join-Path $PatchesDir "custom-animation-k2-drexl-test.dll") -Algorithm SHA256).Hash
    drexl_mdl_sha256 = (Get-FileHash -LiteralPath (Join-Path $OverrideDir "c_drexlf.mdl") -Algorithm SHA256).Hash
    drexl_mdx_sha256 = (Get-FileHash -LiteralPath (Join-Path $OverrideDir "c_drexlf.mdx") -Algorithm SHA256).Hash
    test_utc_sha256 = (Get-FileHash -LiteralPath (Join-Path $OverrideDir "kpm98_drx.utc") -Algorithm SHA256).Hash
}
$Result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BackupRoot "stage-manifest.json") -Encoding utf8
$Result | ConvertTo-Json
