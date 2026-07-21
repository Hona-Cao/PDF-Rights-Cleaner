param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$sdkRc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter rc.exe |
  Where-Object { $_.FullName -match '\\x64\\rc\.exe$' } | Sort-Object FullName -Descending | Select-Object -First 1
$engine = Join-Path $root 'third_party\qpdf-12.3.2\qpdf-12.3.2-msvc64\bin'
$build = Join-Path $root 'build'
$packed = Join-Path $build 'packed'
$dist = Join-Path $root 'dist'

if ($Clean -and (Test-Path $build)) { Remove-Item -LiteralPath $build -Recurse -Force }
New-Item -ItemType Directory -Force -Path $build,$packed,$dist | Out-Null
if (!(Test-Path $vcvars)) { throw 'Visual Studio C++ toolchain not found.' }
if (!$sdkRc) { throw 'Windows SDK resource compiler not found.' }
if (!(Test-Path (Join-Path $engine 'qpdf.exe'))) { throw 'qpdf 12.3.2 engine not found.' }

function Invoke-Vc([string]$Command) {
  $cmd = '"' + $vcvars + '" >nul && ' + $Command
  & cmd.exe /d /s /c $cmd
  if ($LASTEXITCODE -ne 0) { throw "Build command failed ($LASTEXITCODE): $Command" }
}

Push-Location $root
try {
  Invoke-Vc "cl.exe /nologo /std:c++20 /O2 /MT /EHsc packer.cpp /Fe:`"$build\packer.exe`" cabinet.lib"
  $files = @('qpdf.exe','qpdf30.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')
  foreach ($file in $files) {
    & "$build\packer.exe" (Join-Path $engine $file) (Join-Path $packed "$file.xpress")
    if ($LASTEXITCODE -ne 0) { throw "Packing failed: $file" }
  }
  & $sdkRc.FullName /nologo /c65001 /fo "$build\app.res" app.rc
  if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
  $asciiExe = Join-Path $dist 'PdfRightsCleaner.exe'
  $finalName = 'PDF-Rights-Cleaner.exe'
  $finalExe = Join-Path $dist $finalName
  Invoke-Vc "cl.exe /nologo /std:c++20 /utf-8 /O2 /GL /MT /EHsc /DUNICODE /D_UNICODE app.cpp `"$build\app.res`" /Fe:`"$asciiExe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib shlwapi.lib cabinet.lib dwmapi.lib uxtheme.lib"
  Move-Item -LiteralPath $asciiExe -Destination $finalExe -Force
  Get-ChildItem -LiteralPath $dist -Filter '*.exe' | Where-Object { $_.FullName -ne $finalExe } | Remove-Item -Force
  $exe = Get-Item -LiteralPath $finalExe
  Write-Host "Built: $($exe.FullName)"
  Write-Host ("Size: {0:N2} MB ({1} bytes)" -f ($exe.Length / 1MB), $exe.Length)
  if ($exe.Length -gt 10MB) { throw 'Executable exceeds the 10 MB requirement.' }
} finally {
  Pop-Location
}
