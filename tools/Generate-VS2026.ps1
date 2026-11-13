[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$SDL2Root,
    [switch]$Build,
    [switch]$Fresh,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$depsRoot = Join-Path $repoRoot '.deps'
$buildRoot = Join-Path $repoRoot 'build/vs2026'
$downloadRoot = Join-Path $depsRoot 'downloads'

# Use the selected VS installation's CMake even outside a developer prompt.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio 2026 with Desktop development with C++.'
}
$vsRoot = & $vswhere -latest -products '*' -version '[18.0,19.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio 2026 C++ build tools were not found.' }
$cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'Install the C++ CMake tools for Windows component in Visual Studio 2026.'
}
$cmakeVersion = & $cmake --version
if ($cmakeVersion[0] -notmatch 'cmake version (\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'4.2.0') {
    throw 'CMake 4.2 or newer is required. Update the Visual Studio CMake component.'
}

function Get-VerifiedArchive([string]$Url, [string]$FileName, [string]$Sha256) {
    New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
    $archivePath = Join-Path $downloadRoot $FileName
    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading $FileName"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $archivePath
    }
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $Sha256) {
        throw "SHA256 mismatch: $archivePath. Remove this archive and run again."
    }
    return $archivePath
}

if (-not $QtRoot) {
    $QtRoot = Join-Path $depsRoot 'Qt/5.15.2/msvc2019_64'
    $qtPackages = @(
        @{ Name = 'qtbase'; Check = 'lib/cmake/Qt5Widgets/Qt5WidgetsConfig.cmake'; Hash = 'E563DE40230295841C2EECE26E347DE709EDC4BF035515CC239CAE3C994C9AF6' },
        @{ Name = 'qtmultimedia'; Check = 'lib/cmake/Qt5Multimedia/Qt5MultimediaConfig.cmake'; Hash = 'F07053981AFE77D4BFBA8841470A685B1ABD4545C18168D14656FBB4DB1AEFFE' },
        @{ Name = 'qttools'; Check = 'bin/windeployqt.exe'; Hash = 'BABB583BBBAC0E946AD25A2A687B4A3349779D750B1F3DE1C4DADBF1C343C3EA' }
    )
    foreach ($package in $qtPackages) {
        if (Test-Path -LiteralPath (Join-Path $QtRoot $package.Check)) { continue }
        $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
        if (-not (Test-Path -LiteralPath $sevenZip)) {
            $sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
            if (-not $sevenZipCommand) { throw 'Install 7-Zip or supply -QtRoot with an existing Qt 5 MSVC x64 installation.' }
            $sevenZip = $sevenZipCommand.Source
        }
        $fileName = "5.15.2-0-202011130602$($package.Name)-Windows-Windows_10-MSVC2019-Windows-Windows_10-X86_64.7z"
        $url = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/qt.qt5.5152.win64_msvc2019_64/$fileName"
        $archive = Get-VerifiedArchive $url $fileName $package.Hash
        & $sevenZip x $archive "-o$(Join-Path $depsRoot 'Qt')" -y -bso0 -bsp0
        if ($LASTEXITCODE -ne 0) { throw "Could not extract $fileName" }
    }
}
if (-not $SDL2Root) {
    $SDL2Root = Join-Path $depsRoot 'SDL2-2.32.10'
    if (-not (Test-Path -LiteralPath (Join-Path $SDL2Root 'lib/x64/SDL2.lib'))) {
        $archive = Get-VerifiedArchive 'https://www.libsdl.org/release/SDL2-devel-2.32.10-VC.zip' `
            'SDL2-devel-2.32.10-VC.zip' 'AF347939395A58B365846AAEA27391E69F9EC9D4DD650D6AC40802159B418A6E'
        Expand-Archive -LiteralPath $archive -DestinationPath $depsRoot -Force
    }
}

$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$SDL2Root = (Resolve-Path -LiteralPath $SDL2Root).Path
foreach ($requiredFile in @(
    (Join-Path $QtRoot 'lib/cmake/Qt5Widgets/Qt5WidgetsConfig.cmake'),
    (Join-Path $QtRoot 'lib/cmake/Qt5OpenGL/Qt5OpenGLConfig.cmake'),
    (Join-Path $QtRoot 'lib/cmake/Qt5Multimedia/Qt5MultimediaConfig.cmake'),
    (Join-Path $QtRoot 'bin/windeployqt.exe'),
    (Join-Path $SDL2Root 'include/SDL.h'),
    (Join-Path $SDL2Root 'lib/x64/SDL2.lib'),
    (Join-Path $SDL2Root 'lib/x64/SDL2.dll')
)) {
    if (-not (Test-Path -LiteralPath $requiredFile)) { throw "Required dependency not found: $requiredFile" }
}

$cmakeArgs = @(
    '-S', (Join-Path $repoRoot 'yabause'), '-B', $buildRoot,
    '-G', 'Visual Studio 18 2026', '-A', 'x64', '-T', 'v145',
    "-DCMAKE_GENERATOR_INSTANCE=$vsRoot", "-DCMAKE_PREFIX_PATH=$QtRoot",
    '-DYAB_PORTS=qt', '-DYAB_USE_QT5=ON', '-DYAB_DEPLOY_QT=ON',
    '-DYAB_WANT_OPENGL=ON', '-DYAB_WANT_SDL=ON', '-DSDL2_BUILDING_LIBRARY=ON',
    "-DSDL2_INCLUDE_DIR=$SDL2Root/include", "-DSDL2_LIBRARY=$SDL2Root/lib/x64/SDL2.lib",
    "-DSDL2_LIBRARY_TEMP=$SDL2Root/lib/x64/SDL2.lib", "-DSDL2_RUNTIME_LIBRARY=$SDL2Root/lib/x64/SDL2.dll",
    '-DYAB_WANT_MUSASHI=ON', '-DYAB_WANT_C68K=OFF', '-DYAB_USE_PLAY_JIT=ON',
    '-DYAB_WANT_OPENAL=OFF', '-DYAB_WANT_GLUT=OFF', '-DYAB_WANT_DIRECTSOUND=OFF',
    '-DYAB_WANT_DIRECTINPUT=OFF', '-DYAB_USE_SSF=OFF', '-DYAB_WANT_MPEG=OFF'
)
if ($Fresh) { $cmakeArgs += '--fresh' }
& $cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

Write-Host "Solution: $buildRoot/yabause.slnx"
if ($Build) {
    & $cmake --build $buildRoot --config $Configuration --parallel -- /nr:false
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    Write-Host "Executable: $buildRoot/src/qt/$Configuration/yabause.exe"
}
