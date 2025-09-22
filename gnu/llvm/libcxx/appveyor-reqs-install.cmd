@echo on

if NOT EXIST C:\projects\deps (
  mkdir C:\projects\deps
)
cd C:\projects\deps

::###########################################################################
:: Setup Compiler
::###########################################################################
if NOT EXIST llvm-installer.exe (
  appveyor DownloadFile https://prereleases.llvm.org/win-snapshots/LLVM-9.0.0-r357435-win32.exe -FileName llvm-installer.exe
)
if "%CLANG_VERSION%"=="ToT" (
    START /WAIT llvm-installer.exe /S /D=C:\"Program Files\LLVM"
)
if DEFINED CLANG_VERSION  @set PATH="C:\Program Files\LLVM\bin";%PATH%
if DEFINED CLANG_VERSION  clang-cl -v

if DEFINED MINGW_PATH rename "C:\Program Files\Git\usr\bin\sh.exe" "sh-ignored.exe"
if DEFINED MINGW_PATH @set "PATH=%PATH:C:\Program Files (x86)\Git\bin=%"
if DEFINED MINGW_PATH @set "PATH=%PATH%;%MINGW_PATH%"
if DEFINED MINGW_PATH g++ -v

::###########################################################################
:: Install a recent CMake
::###########################################################################
if NOT EXIST cmake (
  appveyor DownloadFile https://cmake.org/files/v3.7/cmake-3.7.2-win64-x64.zip -FileName cmake.zip
  7z x cmake.zip -oC:\projects\deps > nul
  move C:\projects\deps\cmake-* C:\projects\deps\cmake
  rm cmake.zip
)
@set PATH=C:\projects\deps\cmake\bin;%PATH%
cmake --version

::###########################################################################
:: Install Ninja
::###########################################################################
if NOT EXIST ninja (
  appveyor DownloadFile https://github.com/ninja-build/ninja/releases/download/v1.6.0/ninja-win.zip -FileName ninja.zip
  7z x ninja.zip -oC:\projects\deps\ninja > nul
  rm ninja.zip
)
@set PATH=C:\projects\deps\ninja;%PATH%
ninja --version

::###########################################################################
:: Setup the cached copy of LLVM
::###########################################################################
git clone --depth=1 http://llvm.org/git/llvm.git

@echo off
