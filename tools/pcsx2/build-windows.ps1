<#
.SYNOPSIS
    Build PCSX2 on Windows with the kernelreloaded patches applied.

.DESCRIPTION
    Everything in tools/pcsx2/ was developed and tested on Linux under WSL2.
    That leaves two fair objections to the work: it might be a Linux-only
    artefact, and WSL2 puts a layer of virtualisation between the emulator and
    the host. This script answers both by reproducing the build, and the PS2
    Linux boot, on native Windows with MSVC.

    It is deliberately verbose and fails fast. Every step says what it is about
    to do and why, because the interesting output here is not the binary -- it
    is whether anything behaves differently from the Linux build.

.PARAMETER WorkDir
    Where to clone and build. Needs ~25GB free: the Qt/FFmpeg dependency tree is
    the bulk of it, not PCSX2 itself. Defaults to D:\ps2dev.

    Only the build tree moves off C:. build-dependencies.bat hardcodes
    "C:\Program Files\7-Zip\7z.exe" and "C:\Program Files\Git\usr\bin\", with no
    search, so those two must sit in their default locations whatever drive this
    runs on. The prerequisite check below enforces that rather than letting the
    dependency build fail an hour in.

.PARAMETER Commit
    PCSX2 commit to build. Pinned by default to the one the patches were cut
    against and verified to apply to.

.PARAMETER PatchSet
    'all'     every patch, including ata-pio-write -- needed to test HDD writes.
    'minimal' only what it takes to run Linux: ee-tlb-fixes + syscall-guard.

.PARAMETER InstallPrereqs
    Install missing prerequisites with winget. Requires elevation, so Windows
    will raise a UAC prompt that a human has to accept.

.PARAMETER SkipDeps
    Skip build-dependencies.bat. Only correct on a re-run where deps\ already
    exists -- it is the multi-hour step, so re-running it by accident is the
    main way to waste an afternoon.

.EXAMPLE
    .\build-windows.ps1 -InstallPrereqs
    .\build-windows.ps1 -SkipDeps                 # second run, deps already built
    .\build-windows.ps1 -WorkDir E:\ps2 -PatchSet minimal
#>
[CmdletBinding()]
param(
    [string]$WorkDir = "D:\ps2dev",
    [string]$Commit = "337daf7ed",
    [ValidateSet('all', 'minimal')]
    [string]$PatchSet = 'all',
    [switch]$InstallPrereqs,
    [switch]$SkipDeps
)

$ErrorActionPreference = 'Stop'
$PatchDir = $PSScriptRoot

function Say($msg) { Write-Host "`n=== $msg" -ForegroundColor Cyan }
function Note($msg) { Write-Host "    $msg" -ForegroundColor DarkGray }
function Die($msg) { Write-Host "`n*** $msg" -ForegroundColor Red; exit 1 }

# The patches must be applied in this order. syscall-guard, dev9-trace-gating
# and branch-delay-ratelimit all touch files ee-tlb-fixes.patch has already
# modified, so they are deltas on top of it rather than against pristine
# upstream. The other two are independent but are kept in sequence for a
# predictable log.
$AllPatches = @(
    'ee-tlb-fixes.patch',
    'syscall-guard.patch',
    'ata-pio-write.patch',
    'dev9-trace-gating.patch',
    'branch-delay-ratelimit.patch',
    'cache-mode-ratelimit.patch'
)
$MinimalPatches = @('ee-tlb-fixes.patch', 'syscall-guard.patch')
$Patches = if ($PatchSet -eq 'minimal') { $MinimalPatches } else { $AllPatches }

# ---------------------------------------------------------------------------
Say "checking prerequisites"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
}

$missing = @()
if (-not $vsPath) { $missing += 'Microsoft.VisualStudio.2022.BuildTools' }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += 'Git.Git' }
# build-dependencies.bat hardcodes these two paths; it does not search.
if (-not (Test-Path "C:\Program Files\7-Zip\7z.exe")) { $missing += '7zip.7zip' }
if (-not (Test-Path "C:\Program Files\Git\usr\bin\patch.exe")) {
    Note "Git for Windows is present but without usr\bin\patch.exe."
    Note "build-dependencies.bat needs the full Git for Windows, not MinGit."
}

if ($missing.Count -gt 0) {
    if (-not $InstallPrereqs) {
        Write-Host "`nMissing:" -ForegroundColor Yellow
        $missing | ForEach-Object { Write-Host "  $_" }
        Die "Re-run with -InstallPrereqs, or install them by hand. Installing needs elevation (UAC)."
    }
    foreach ($pkg in $missing) {
        Say "winget install $pkg"
        if ($pkg -eq 'Microsoft.VisualStudio.2022.BuildTools') {
            # VCTools brings MSVC, the Windows SDK, CMake and Ninja. Without
            # --includeRecommended you get a toolset with no SDK, which fails
            # much later and confusingly.
            winget install --id $pkg --accept-source-agreements --accept-package-agreements `
                --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        }
        else {
            winget install --id $pkg --accept-source-agreements --accept-package-agreements
        }
        if ($LASTEXITCODE -ne 0) { Die "winget failed for $pkg (exit $LASTEXITCODE)" }
    }
    Say "prerequisites installed -- RE-RUN this script in a NEW shell"
    Note "A new shell is needed to pick up the PATH the installers set."
    exit 0
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Die "vcvars64.bat not found under $vsPath" }
Note "Visual Studio: $vsPath"
Note "git:           $((Get-Command git).Source)"

foreach ($p in $Patches) {
    if (-not (Test-Path (Join-Path $PatchDir $p))) { Die "patch missing: $p (expected beside this script)" }
}
Note "patches:       $($Patches.Count) from $PatchDir"

# ---------------------------------------------------------------------------
Say "cloning PCSX2 at $Commit"

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$src = Join-Path $WorkDir 'pcsx2'

if (-not (Test-Path $src)) {
    # core.autocrlf=false matters more than it looks. Git for Windows defaults
    # to converting LF to CRLF in the working tree; the patches are LF, so with
    # conversion on, every hunk fails to apply on line endings alone. This is
    # the same class of problem that stops gcc 2.95 compiling CRLF sources --
    # worth remembering, since the symptom looks like a corrupt patch.
    git -c core.autocrlf=false -c core.eol=lf clone https://github.com/PCSX2/pcsx2.git $src
    if ($LASTEXITCODE -ne 0) { Die "clone failed" }
    Push-Location $src
    git -c advice.detachedHead=false checkout $Commit
    if ($LASTEXITCODE -ne 0) { Die "checkout of $Commit failed" }
    Pop-Location
}
else {
    Note "already present, reusing: $src"
    Push-Location $src
    $head = (git rev-parse --short HEAD).Trim()
    Note "HEAD is $head"
    Pop-Location
}

# ---------------------------------------------------------------------------
Say "applying patches"

Push-Location $src
foreach ($p in $Patches) {
    $full = Join-Path $PatchDir $p
    # --check first so a failure names the patch rather than leaving a
    # half-applied tree behind.
    git apply --check --whitespace=nowarn $full 2>$null
    if ($LASTEXITCODE -ne 0) {
        git apply --reverse --check --whitespace=nowarn $full 2>$null
        if ($LASTEXITCODE -eq 0) { Note "$p -- already applied, skipping"; continue }
        Die "$p does not apply. Tree is at $Commit; the patch may need rebasing."
    }
    git apply --whitespace=nowarn $full
    if ($LASTEXITCODE -ne 0) { Die "$p failed to apply after passing --check" }
    Note "$p -- applied"
}
Pop-Location

# ---------------------------------------------------------------------------
# Everything from here needs the MSVC environment, which is a batch-file affair.
# Rather than reimplement vcvars64 in PowerShell, each stage is handed to cmd
# with vcvars64 called first.
function Invoke-VsBatch([string]$Body, [string]$What) {
    $bat = Join-Path $env:TEMP ("ps2dev-" + [guid]::NewGuid().ToString('N') + ".bat")
    @"
@echo off
call "$vcvars" >nul || exit /b 1
$Body
"@ | Set-Content -Encoding ASCII $bat
    & cmd.exe /c $bat
    $rc = $LASTEXITCODE
    Remove-Item $bat -Force -ErrorAction SilentlyContinue
    if ($rc -ne 0) { Die "$What failed (exit $rc)" }
}

if (-not $SkipDeps) {
    Say "building dependencies (Qt, FFmpeg, SDL3, shaderc) -- hours, not minutes"
    Note "Re-run with -SkipDeps to skip this once deps\ exists."
    Invoke-VsBatch @"
cd /d "$src"
call .github\workflows\scripts\windows\build-dependencies.bat || exit /b 1
"@ "build-dependencies.bat"
}
else {
    Say "skipping dependency build (-SkipDeps)"
    if (-not (Test-Path (Join-Path $src 'deps'))) { Die "-SkipDeps given but deps\ does not exist" }
}

# ---------------------------------------------------------------------------
Say "configuring and building PCSX2"
# Flags mirror .github/workflows/windows_build_qt.yml so this matches what CI
# produces, rather than some configuration only we use.
Invoke-VsBatch @"
cd /d "$src"
cmake . -B build -DCMAKE_PREFIX_PATH="$src\deps" -DQT_BUILD=ON -DCMAKE_BUILD_TYPE=Release -DDISABLE_ADVANCE_SIMD=ON -G Ninja || exit /b 1
cmake --build build --config Release || exit /b 1
"@ "cmake build"

# ---------------------------------------------------------------------------
Say "done"
$exe = Get-ChildItem -Path (Join-Path $src 'build') -Filter 'pcsx2-qt*.exe' -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($exe) {
    Write-Host "`nBuilt: $($exe.FullName)" -ForegroundColor Green
}
else {
    Note "Built, but pcsx2-qt.exe was not found by the glob -- look under $src\build"
}

Write-Host @"

To reproduce the PS2 Linux boot this was all for, you need four things next to
each other, all of which come from the kernelreloaded tree:

    kloader.elf        bin/kloader.elf
    vmlinux_net.gz     the phase1 kernel, with IP_PNP and ROOT_NFS
    DISC.BIN           the BlackRhino rootfs, a bare ext2 image
    a PS2 BIOS         whatever you already use

Then in PCSX2's settings, before booting:

    DEV9 -> HDD    enable, pointing at DISC.BIN
                   NOTE the settings live in [DEV9/Hdd] in PCSX2.ini. A flat
                   [DEV9] section is an older layout and is silently ignored,
                   which cost a long time to discover.
    CPU            EE recompiler off if you hit the recompiler crash; the
                   interpreter is slow but known to work.

and boot kloader.elf with a config.txt beside it:

    KernelFileName=host:vmlinux_net.gz
    KernelParameter=root=/dev/hda rw romcons console=romcons console=tty0
    AutoBootTime=1

What to compare against the Linux build: whether the HDD mounts read-write with
no "no DRQ after issuing WRITE", whether init reaches runlevel 2 without SIGSEGV
in rcS or grep, and whether the log stays near 15,000 lines rather than 268,000.

"@ -ForegroundColor Gray
