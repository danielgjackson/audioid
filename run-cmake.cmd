@ECHO OFF
SETLOCAL EnableDelayedExpansion
CD /D %~dp0

rem If launched from anything other than cmd.exe, will have "%WINDIR%\system32\cmd.exe" in the command line
set INTERACTIVE_BUILD=
IF "%1"=="/NONINTERACTIVE" GOTO NONINTERACTIVE
ECHO.%CMDCMDLINE% | FINDSTR /C:"%COMSPEC% /c" >NUL
IF ERRORLEVEL 1 GOTO NONINTERACTIVE
rem Preserve this as it seems to be corrupted below otherwise?!
SET CMDLINE=%CMDCMDLINE%
rem If launched from anything other than cmd.exe, last character of command line will always be a double quote
IF NOT ^!CMDCMDLINE:~-1!==^" GOTO NONINTERACTIVE
rem If run from Explorer, last-but-one character of command line will be a space
IF NOT "!CMDLINE:~-2,1!"==" " GOTO NONINTERACTIVE
SET INTERACTIVE_BUILD=1
:NONINTERACTIVE

SET FIND_CMAKE=
FOR %%p IN (cmake.exe) DO SET "FIND_CMAKE=%%~$PATH:p"
IF DEFINED FIND_CMAKE (
  ECHO Build tools already on path.
  GOTO PREBUILD
)

ECHO Build tools not on path, looking for 'vcvarsall.bat'...
SET ARCH=x86
SET VCVARSALL=
FOR %%f IN (70 71 80 90 100 110 120 130 140) DO IF EXIST "!VS%%fCOMNTOOLS!\..\..\VC\vcvarsall.bat" SET VCVARSALL=!VS%%fCOMNTOOLS!\..\..\VC\vcvarsall.bat
FOR /F "usebackq tokens=*" %%f IN (`DIR /B /ON "%ProgramFiles(x86)%\Microsoft Visual Studio\????"`) DO FOR %%g IN (Community Professional Enterprise) DO IF EXIST "%ProgramFiles(x86)%\Microsoft Visual Studio\%%f\%%g\VC\Auxiliary\Build\vcvarsall.bat" SET "VCVARSALL=%ProgramFiles(x86)%\Microsoft Visual Studio\%%f\%%g\VC\Auxiliary\Build\vcvarsall.bat"
FOR /F "usebackq tokens=*" %%f IN (`DIR /B /ON "%ProgramFiles%\Microsoft Visual Studio\????"`) DO FOR %%g IN (Community Professional Enterprise) DO IF EXIST "%ProgramFiles%\Microsoft Visual Studio\%%f\%%g\VC\Auxiliary\Build\vcvarsall.bat" SET "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\%%f\%%g\VC\Auxiliary\Build\vcvarsall.bat"
IF "%VCVARSALL%"=="" ECHO Cannot find VC environment for 'vcvarsall.bat'. & GOTO ERROR
ECHO Setting environment variables for VC... %VCVARSALL%
CALL "%VCVARSALL%" %ARCH%

:PREBUILD
IF EXIST "build\CMakeCache.txt" GOTO BUILD
cmake -Bbuild
IF ERRORLEVEL 1 GOTO ERROR

:BUILD
ECHO Compiling...
cmake --build build
IF ERRORLEVEL 1 GOTO ERROR
ECHO Done.

rem UTF-8
chcp 65001

echo Executable:  build\src\Debug\audioid.exe
build\src\Debug\audioid.exe %*

IF DEFINED INTERACTIVE_BUILD COLOR 2F & PAUSE & COLOR
GOTO :EOF

:ERROR
ECHO ERROR: An error occured.
IF DEFINED INTERACTIVE_BUILD COLOR 4F & PAUSE & COLOR
EXIT /B 1
GOTO :EOF