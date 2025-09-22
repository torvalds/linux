@echo off
setlocal enabledelayedexpansion

goto begin

:usage
echo Script for building the LLVM installer on Windows,
echo used for the releases at https://github.com/llvm/llvm-project/releases
echo.
echo Usage: build_llvm_release.bat --version ^<version^> [--x86,--x64, --arm64] [--skip-checkout] [--local-python]
echo.
echo Options:
echo --version: [required] version to build
echo --help: display this help
echo --x86: build and test x86 variant
echo --x64: build and test x64 variant
echo --arm64: build and test arm64 variant
echo --skip-checkout: use local git checkout instead of downloading src.zip
echo --local-python: use installed Python and does not try to use a specific version (3.10)
echo.
echo Note: At least one variant to build is required.
echo.
echo Example: build_llvm_release.bat --version 15.0.0 --x86 --x64
exit /b 1

:begin

::==============================================================================
:: parse args
set version=
set help=
set x86=
set x64=
set arm64=
set skip-checkout=
set local-python=
call :parse_args %*

if "%help%" NEQ "" goto usage

if "%version%" == "" (
    echo --version option is required
    echo =============================
    goto usage
)

if "%arm64%" == "" if "%x64%" == "" if "%x86%" == "" (
    echo nothing to build!
    echo choose one or several variants from: --x86 --x64 --arm64
    exit /b 1
)

::==============================================================================
:: check prerequisites
REM Note:
REM   7zip versions 21.x and higher will try to extract the symlinks in
REM   llvm's git archive, which requires running as administrator.

REM Check 7-zip version and/or administrator permissions.
for /f "delims=" %%i in ('7z.exe ^| findstr /r "2[1-9].[0-9][0-9]"') do set version_7z=%%i
if not "%version_7z%"=="" (
  REM Unique temporary filename to use by the 'mklink' command.
  set "link_name=%temp%\%username%_%random%_%random%.tmp"

  REM As the 'mklink' requires elevated permissions, the symbolic link
  REM creation will fail if the script is not running as administrator.
  mklink /d "!link_name!" . 1>nul 2>nul
  if errorlevel 1 (
    echo.
    echo Script requires administrator permissions, or a 7-zip version 20.x or older.
    echo Current version is "%version_7z%"
    exit /b 1
  ) else (
    REM Remove the temporary symbolic link.
    rd "!link_name!"
  )
)

REM Prerequisites:
REM
REM   Visual Studio 2019, CMake, Ninja, GNUWin32, SWIG, Python 3,
REM   NSIS with the strlen_8192 patch,
REM   Visual Studio 2019 SDK and Nuget (for the clang-format plugin),
REM   Perl (for the OpenMP run-time).
REM
REM
REM   For LLDB, SWIG version 4.1.1 should be used.
REM

:: Detect Visual Studio
set vsinstall=
set vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe

if "%VSINSTALLDIR%" NEQ "" (
  echo using enabled Visual Studio installation
  set "vsinstall=%VSINSTALLDIR%"
) else (
  echo using vswhere to detect Visual Studio installation
  FOR /F "delims=" %%r IN ('^""%vswhere%" -nologo -latest -products "*" -all -property installationPath^"') DO set vsinstall=%%r
)
set "vsdevcmd=%vsinstall%\Common7\Tools\VsDevCmd.bat"

if not exist "%vsdevcmd%" (
  echo Can't find any installation of Visual Studio
  exit /b 1
)
echo Using VS devcmd: %vsdevcmd%

::==============================================================================
:: start echoing what we do
@echo on

set python32_dir=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python310-32
set python64_dir=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python310
set pythonarm64_dir=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python311-arm64

set revision=llvmorg-%version%
set package_version=%version%
set build_dir=%cd%\llvm_package_%package_version%

echo Revision: %revision%
echo Package version: %package_version%
echo Build dir: %build_dir%
echo.

if exist %build_dir% (
  echo Build directory already exists: %build_dir%
  exit /b 1
)
mkdir %build_dir%
cd %build_dir% || exit /b 1

if "%skip-checkout%" == "true" (
  echo Using local source
  set llvm_src=%~dp0..\..\..
) else (
  echo Checking out %revision%
  curl -L https://github.com/llvm/llvm-project/archive/%revision%.zip -o src.zip || exit /b 1
  7z x src.zip || exit /b 1
  mv llvm-project-* llvm-project || exit /b 1
  set llvm_src=%build_dir%\llvm-project
)

curl -O https://gitlab.gnome.org/GNOME/libxml2/-/archive/v2.9.12/libxml2-v2.9.12.tar.gz || exit /b 1
tar zxf libxml2-v2.9.12.tar.gz

REM Setting CMAKE_CL_SHOWINCLUDES_PREFIX to work around PR27226.
REM Common flags for all builds.
set common_compiler_flags=-DLIBXML_STATIC
set common_cmake_flags=^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLLVM_ENABLE_ASSERTIONS=OFF ^
  -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON ^
  -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86" ^
  -DLLVM_BUILD_LLVM_C_DYLIB=ON ^
  -DCMAKE_INSTALL_UCRT_LIBRARIES=ON ^
  -DPython3_FIND_REGISTRY=NEVER ^
  -DPACKAGE_VERSION=%package_version% ^
  -DLLDB_RELOCATABLE_PYTHON=1 ^
  -DLLDB_EMBED_PYTHON_HOME=OFF ^
  -DCMAKE_CL_SHOWINCLUDES_PREFIX="Note: including file: " ^
  -DLLVM_ENABLE_LIBXML2=FORCE_ON ^
  -DLLDB_ENABLE_LIBXML2=OFF ^
  -DCLANG_ENABLE_LIBXML2=OFF ^
  -DCMAKE_C_FLAGS="%common_compiler_flags%" ^
  -DCMAKE_CXX_FLAGS="%common_compiler_flags%" ^
  -DLLVM_ENABLE_RPMALLOC=ON ^
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;compiler-rt;lldb;openmp"

set cmake_profile_flags=""

REM Preserve original path
set OLDPATH=%PATH%

REM Build the 32-bits and/or 64-bits binaries.
if "%x86%" == "true" call :do_build_32 || exit /b 1
if "%x64%" == "true" call :do_build_64 || exit /b 1
if "%arm64%" == "true" call :do_build_arm64 || exit /b 1
exit /b 0

::==============================================================================
:: Build 32-bits binaries.
::==============================================================================
:do_build_32
call :set_environment %python32_dir% || exit /b 1
call "%vsdevcmd%" -arch=x86 || exit /b 1
@echo on
mkdir build32_stage0
cd build32_stage0
call :do_build_libxml || exit /b 1

REM Stage0 binaries directory; used in stage1.
set "stage0_bin_dir=%build_dir%/build32_stage0/bin"
set cmake_flags=^
  %common_cmake_flags% ^
  -DLLVM_ENABLE_RPMALLOC=OFF ^
  -DLLDB_TEST_COMPILER=%stage0_bin_dir%/clang.exe ^
  -DPYTHON_HOME=%PYTHONHOME% ^
  -DPython3_ROOT_DIR=%PYTHONHOME% ^
  -DLIBXML2_INCLUDE_DIR=%libxmldir%/include/libxml2 ^
  -DLIBXML2_LIBRARIES=%libxmldir%/lib/libxml2s.lib

cmake -GNinja %cmake_flags% %llvm_src%\llvm || exit /b 1
ninja || ninja || ninja || exit /b 1
REM ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
REM ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
ninja check-sanitizer || ninja check-sanitizer || ninja check-sanitizer || exit /b 1
REM ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
cd..

REM CMake expects the paths that specifies the compiler and linker to be
REM with forward slash.
set all_cmake_flags=^
  %cmake_flags% ^
  -DCMAKE_C_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_CXX_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_LINKER=%stage0_bin_dir%/lld-link.exe ^
  -DCMAKE_AR=%stage0_bin_dir%/llvm-lib.exe ^
  -DCMAKE_RC=%stage0_bin_dir%/llvm-windres.exe
set cmake_flags=%all_cmake_flags:\=/%

mkdir build32
cd build32
cmake -GNinja %cmake_flags% %llvm_src%\llvm || exit /b 1
ninja || ninja || ninja || exit /b 1
REM ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
REM ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
ninja check-sanitizer || ninja check-sanitizer || ninja check-sanitizer || exit /b 1
REM ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
ninja package || exit /b 1
cd ..

exit /b 0
::==============================================================================

::==============================================================================
:: Build 64-bits binaries.
::==============================================================================
:do_build_64
call :set_environment %python64_dir% || exit /b 1
call "%vsdevcmd%" -arch=amd64 || exit /b 1
@echo on
mkdir build64_stage0
cd build64_stage0
call :do_build_libxml || exit /b 1

REM Stage0 binaries directory; used in stage1.
set "stage0_bin_dir=%build_dir%/build64_stage0/bin"
set cmake_flags=^
  %common_cmake_flags% ^
  -DLLDB_TEST_COMPILER=%stage0_bin_dir%/clang.exe ^
  -DPYTHON_HOME=%PYTHONHOME% ^
  -DPython3_ROOT_DIR=%PYTHONHOME% ^
  -DLIBXML2_INCLUDE_DIR=%libxmldir%/include/libxml2 ^
  -DLIBXML2_LIBRARIES=%libxmldir%/lib/libxml2s.lib

cmake -GNinja %cmake_flags% %llvm_src%\llvm || exit /b 1
ninja || ninja || ninja || exit /b 1
ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
ninja check-sanitizer || ninja check-sanitizer || ninja check-sanitizer || exit /b 1
ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
ninja check-clangd || ninja check-clangd || ninja check-clangd || exit /b 1
cd..

REM CMake expects the paths that specifies the compiler and linker to be
REM with forward slash.
set all_cmake_flags=^
  %cmake_flags% ^
  -DCMAKE_C_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_CXX_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_LINKER=%stage0_bin_dir%/lld-link.exe ^
  -DCMAKE_AR=%stage0_bin_dir%/llvm-lib.exe ^
  -DCMAKE_RC=%stage0_bin_dir%/llvm-windres.exe
set cmake_flags=%all_cmake_flags:\=/%


mkdir build64
cd build64
call :do_generate_profile || exit /b 1
cmake -GNinja %cmake_flags% %cmake_profile_flags% %llvm_src%\llvm || exit /b 1
ninja || ninja || ninja || exit /b 1
ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
ninja check-sanitizer || ninja check-sanitizer || ninja check-sanitizer || exit /b 1
ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
ninja check-clangd || ninja check-clangd || ninja check-clangd || exit /b 1
ninja package || exit /b 1

:: generate tarball with install toolchain only off
set filename=clang+llvm-%version%-x86_64-pc-windows-msvc
cmake -GNinja %cmake_flags% %cmake_profile_flags% -DLLVM_INSTALL_TOOLCHAIN_ONLY=OFF ^
  -DCMAKE_INSTALL_PREFIX=%build_dir%/%filename% ..\llvm-project\llvm || exit /b 1
ninja install || exit /b 1
:: check llvm_config is present & returns something
%build_dir%/%filename%/bin/llvm-config.exe --bindir || exit /b 1
cd ..
7z a -ttar -so %filename%.tar %filename% | 7z a -txz -si %filename%.tar.xz

exit /b 0
::==============================================================================

::==============================================================================
:: Build arm64 binaries.
::==============================================================================
:do_build_arm64
call :set_environment %pythonarm64_dir% || exit /b 1
call "%vsdevcmd%" -host_arch=x64 -arch=arm64 || exit /b 1
@echo on
mkdir build_arm64_stage0
cd build_arm64_stage0
call :do_build_libxml || exit /b 1

REM Stage0 binaries directory; used in stage1.
set "stage0_bin_dir=%build_dir%/build_arm64_stage0/bin"
set cmake_flags=^
  %common_cmake_flags% ^
  -DCLANG_DEFAULT_LINKER=lld ^
  -DLIBXML2_INCLUDE_DIR=%libxmldir%/include/libxml2 ^
  -DLIBXML2_LIBRARIES=%libxmldir%/lib/libxml2s.lib ^
  -DPython3_ROOT_DIR=%PYTHONHOME% ^
  -DCOMPILER_RT_BUILD_PROFILE=OFF ^
  -DCOMPILER_RT_BUILD_SANITIZERS=OFF

REM We need to build stage0 compiler-rt with clang-cl (msvc lacks some builtins).
cmake -GNinja %cmake_flags% ^
  -DCMAKE_C_COMPILER=clang-cl.exe ^
  -DCMAKE_CXX_COMPILER=clang-cl.exe ^
  %llvm_src%\llvm || exit /b 1
ninja || exit /b 1
::ninja check-llvm || exit /b 1
::ninja check-clang || exit /b 1
::ninja check-lld || exit /b 1
::ninja check-sanitizer || exit /b 1
::ninja check-clang-tools || exit /b 1
::ninja check-clangd || exit /b 1
cd..

REM CMake expects the paths that specifies the compiler and linker to be
REM with forward slash.
REM CPACK_SYSTEM_NAME is set to have a correct name for installer generated.
set all_cmake_flags=^
  %cmake_flags% ^
  -DCMAKE_C_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_CXX_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_LINKER=%stage0_bin_dir%/lld-link.exe ^
  -DCMAKE_AR=%stage0_bin_dir%/llvm-lib.exe ^
  -DCMAKE_RC=%stage0_bin_dir%/llvm-windres.exe ^
  -DCPACK_SYSTEM_NAME=woa64
set cmake_flags=%all_cmake_flags:\=/%

mkdir build_arm64
cd build_arm64
cmake -GNinja %cmake_flags% %llvm_src%\llvm || exit /b 1
ninja || exit /b 1
REM Check but do not fail on errors.
ninja check-lldb
::ninja check-llvm || exit /b 1
::ninja check-clang || exit /b 1
::ninja check-lld || exit /b 1
::ninja check-sanitizer || exit /b 1
::ninja check-clang-tools || exit /b 1
::ninja check-clangd || exit /b 1
ninja package || exit /b 1
cd ..

exit /b 0
::==============================================================================
::
::==============================================================================
:: Set PATH and some environment variables.
::==============================================================================
:set_environment
REM Restore original path
set PATH=%OLDPATH%

set python_dir=%1

REM Set Python environment
if "%local-python%" == "true" (
  FOR /F "delims=" %%i IN ('where python.exe ^| head -1') DO set python_exe=%%i
  set PYTHONHOME=!python_exe:~0,-11!
) else (
  %python_dir%/python.exe --version || exit /b 1
  set PYTHONHOME=%python_dir%
)
set PATH=%PYTHONHOME%;%PATH%

set "VSCMD_START_DIR=%build_dir%"

exit /b 0

::=============================================================================

::==============================================================================
:: Build libxml.
::==============================================================================
:do_build_libxml
mkdir libxmlbuild
cd libxmlbuild
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install ^
  -DBUILD_SHARED_LIBS=OFF -DLIBXML2_WITH_C14N=OFF -DLIBXML2_WITH_CATALOG=OFF ^
  -DLIBXML2_WITH_DEBUG=OFF -DLIBXML2_WITH_DOCB=OFF -DLIBXML2_WITH_FTP=OFF ^
  -DLIBXML2_WITH_HTML=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_ICONV=OFF ^
  -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_ISO8859X=OFF -DLIBXML2_WITH_LEGACY=OFF ^
  -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_MEM_DEBUG=OFF -DLIBXML2_WITH_MODULES=OFF ^
  -DLIBXML2_WITH_OUTPUT=ON -DLIBXML2_WITH_PATTERN=OFF -DLIBXML2_WITH_PROGRAMS=OFF ^
  -DLIBXML2_WITH_PUSH=OFF -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_READER=OFF ^
  -DLIBXML2_WITH_REGEXPS=OFF -DLIBXML2_WITH_RUN_DEBUG=OFF -DLIBXML2_WITH_SAX1=OFF ^
  -DLIBXML2_WITH_SCHEMAS=OFF -DLIBXML2_WITH_SCHEMATRON=OFF -DLIBXML2_WITH_TESTS=OFF ^
  -DLIBXML2_WITH_THREADS=ON -DLIBXML2_WITH_THREAD_ALLOC=OFF -DLIBXML2_WITH_TREE=ON ^
  -DLIBXML2_WITH_VALID=OFF -DLIBXML2_WITH_WRITER=OFF -DLIBXML2_WITH_XINCLUDE=OFF ^
  -DLIBXML2_WITH_XPATH=OFF -DLIBXML2_WITH_XPTR=OFF -DLIBXML2_WITH_ZLIB=OFF ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  ../../libxml2-v2.9.12 || exit /b 1
ninja install || exit /b 1
set libxmldir=%cd%\install
set "libxmldir=%libxmldir:\=/%"
cd ..
exit /b 0

::==============================================================================
:: Generate a PGO profile.
::==============================================================================
:do_generate_profile
REM Build Clang with instrumentation.
mkdir instrument
cd instrument
cmake -GNinja %cmake_flags% -DLLVM_TARGETS_TO_BUILD=Native ^
  -DLLVM_BUILD_INSTRUMENTED=IR %llvm_src%\llvm || exit /b 1
ninja clang || ninja clang || ninja clang || exit /b 1
set instrumented_clang=%cd:\=/%/bin/clang-cl.exe
cd ..
REM Use that to build part of llvm to generate a profile.
mkdir train
cd train
cmake -GNinja %cmake_flags% ^
  -DCMAKE_C_COMPILER=%instrumented_clang% ^
  -DCMAKE_CXX_COMPILER=%instrumented_clang% ^
  -DLLVM_ENABLE_PROJECTS=clang ^
  -DLLVM_TARGETS_TO_BUILD=Native ^
  %llvm_src%\llvm || exit /b 1
REM Drop profiles generated from running cmake; those are not representative.
del ..\instrument\profiles\*.profraw
ninja tools/clang/lib/Sema/CMakeFiles/obj.clangSema.dir/Sema.cpp.obj
cd ..
set profile=%cd:\=/%/profile.profdata
%stage0_bin_dir%\llvm-profdata merge -output=%profile% instrument\profiles\*.profraw || exit /b 1
set common_compiler_flags=%common_compiler_flags% -Wno-backend-plugin
set cmake_profile_flags=-DLLVM_PROFDATA_FILE=%profile% ^
  -DCMAKE_C_FLAGS="%common_compiler_flags%" ^
  -DCMAKE_CXX_FLAGS="%common_compiler_flags%"
exit /b 0

::=============================================================================
:: Parse command line arguments.
:: The format for the arguments is:
::   Boolean: --option
::   Value:   --option<separator>value
::     with <separator> being: space, colon, semicolon or equal sign
::
:: Command line usage example:
::   my-batch-file.bat --build --type=release --version 123
:: It will create 3 variables:
::   'build' with the value 'true'
::   'type' with the value 'release'
::   'version' with the value '123'
::
:: Usage:
::   set "build="
::   set "type="
::   set "version="
::
::   REM Parse arguments.
::   call :parse_args %*
::
::   if defined build (
::     ...
::   )
::   if %type%=='release' (
::     ...
::   )
::   if %version%=='123' (
::     ...
::   )
::=============================================================================
:parse_args
  set "arg_name="
  :parse_args_start
  if "%1" == "" (
    :: Set a seen boolean argument.
    if "%arg_name%" neq "" (
      set "%arg_name%=true"
    )
    goto :parse_args_done
  )
  set aux=%1
  if "%aux:~0,2%" == "--" (
    :: Set a seen boolean argument.
    if "%arg_name%" neq "" (
      set "%arg_name%=true"
    )
    set "arg_name=%aux:~2,250%"
  ) else (
    set "%arg_name%=%1"
    set "arg_name="
  )
  shift
  goto :parse_args_start

:parse_args_done
exit /b 0
