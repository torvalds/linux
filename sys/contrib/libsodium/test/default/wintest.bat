@ECHO OFF

if "%1" == "" (
  echo "Usage: wintest.bat <Release | ReleaseDLL | Debug | DebugDLL"
	goto :END
)

if not exist sodium_version.c (
	CD test\default
	if not exist sodium_version.c (
		echo "Are you on the right path?" %CD%
		goto :END
	)
)

if "%2" == "x64" (SET ARCH=x64) else (SET ARCH=Win32)
SET CFLAGS=/nologo /DTEST_SRCDIR=\".\" /I..\..\src\libsodium\include\sodium /I..\..\src\libsodium\include /I..\quirks
SET LDFLAGS=/link /LTCG advapi32.lib ..\..\Build\%1\%ARCH%\libsodium.lib
if "%1" == "ReleaseDLL" ( goto :ReleaseDLL )
if "%1" == "DebugDLL"   ( goto :DebugDLL )
if "%1" == "Release"   ( goto :Release )
if "%1" == "Debug"   ( goto :Debug )
echo "Invalid build type"
goto :END
:ReleaseDLL
	SET CFLAGS=%CFLAGS% /MD /Ox 
	SET PATH=..\..\Build\%1\%ARCH%;%PATH% 
	goto :COMPILE
:Release
	SET CFLAGS=%CFLAGS% /MT /Ox /DSODIUM_STATIC /DSODIUM_EXPORT=
	goto :COMPILE
:DebugDLL
	SET CFLAGS=%CFLAGS% /GS /MDd /Od
	SET PATH=..\..\Build\%1\%ARCH%;%PATH%
	goto :COMPILE
:Debug
	SET CFLAGS=%CFLAGS% /GS /MTd /Od /DSODIUM_STATIC /DSODIUM_EXPORT=
	goto :COMPILE
:COMPILE
echo Running the test suite:
FOR %%f in (*.c) DO (
	cl %CFLAGS% %%f %LDFLAGS% /OUT:%%f.exe > NUL 2>&1
	if not exist %%f.exe (
		echo %%f compile failed
		goto :END
	)
	%%f.exe
	if errorlevel 1 ( 
		echo %%f failed
	) else (
		echo %%f ok
	)
)
REM Remove temporary files
del *.exe *.obj *.res 
:END
