@echo off
setlocal enabledelayedexpansion

::路径
set SOURCE_x86_LIB=D:\Project\GitHome\vcpkg\vcpkg\installed\x86-windows-static
set SOURCE_x64_LIB=D:\Project\GitHome\vcpkg\vcpkg\installed\x64-windows-static

set TARGET_x86_LIB=%cd%\lib32
set TARGET_x64_LIB=%cd%\lib64

::定义要拷贝的库文件列表
set LIB= ^
jsoncpp.lib ^
fmt.lib ^
spdlog.lib ^
zlib.lib ^
minizip-ng.lib


set HEADER= ^
json ^
fmt ^
spdlog ^
zlib.h ^
zconf.h ^
minizip-ng

::echo %LIB%

echo ============== clean header files ==============
call :removeHeaderFiles %TARGET_x86_LIB% "%HEADER%"
call :removeHeaderFiles %TARGET_x64_LIB% "%HEADER%"
echo ============== copy header files ==============
call :copyHeaderFiles %SOURCE_x86_LIB% %TARGET_x86_LIB% "%HEADER%"
call :copyHeaderFiles %SOURCE_x64_LIB% %TARGET_x64_LIB% "%HEADER%"

echo ============== clean files ==============
call :removeFiles %TARGET_x86_LIB% "%LIB%"
call :removeFiles %TARGET_x64_LIB% "%LIB%"
echo ============== copy files ==============
call :copyFiles %SOURCE_x86_LIB% %TARGET_x86_LIB% "%LIB%"
call :copyFiles %SOURCE_x64_LIB% %TARGET_x64_LIB% "%LIB%"

echo. & echo Finished. & pause>nul
endlocal
goto :EOF

::清除旧文件
:removeFiles
setlocal
set LIST=%~2

set TARGET_DIR=%~1\lib

for %%F in (%LIST%) do (
    del "%TARGET_DIR%\%%F"
)

set TARGET_DIR=%~1\libd

for %%F in (%LIST%) do (
    if exist "%TARGET_DIR%\%%F" (
        del "%TARGET_DIR%\%%F"
    ) else if exist "%TARGET_DIR%\%%~nFd.lib" (
        del "%TARGET_DIR%\%%~nFd.lib"
    )
)

endlocal
goto :EOF

::拷贝文件
:copyFiles
setlocal
set LIST=%~3

set SOURCE_DIR=%~1\lib
set TARGET_DIR=%~2\lib

for %%F in (%LIST%) do (
    copy /Y "%SOURCE_DIR%\%%F" "%TARGET_DIR%" || echo %%F
)

set SOURCE_DIR=%~1\debug\lib
set TARGET_DIR=%~2\libd

for %%F in (%LIST%) do (    
    if exist "%SOURCE_DIR%\%%F" (
        copy /Y "%SOURCE_DIR%\%%F" "%TARGET_DIR%" || echo %%F
    ) else if exist "%SOURCE_DIR%\%%~nFd.lib" (
        copy "%SOURCE_DIR%\%%~nFd.lib" "%TARGET_DIR%" || echo %%~nFd.lib
    ) else (
        echo copy fiald not found %%~nFd.lib
    )
)

endlocal
goto :EOF


:removeHeaderFiles
setlocal
set TARGET_DIR=%~1\include
set LIST=%~2

for %%F in (%LIST%) do (
    if exist "%TARGET_DIR%\%%F\.\" (
        rmdir /S /Q "%TARGET_DIR%\%%F"
    ) else if exist "%TARGET_DIR%\%%F" (
        del /Q "%TARGET_DIR%\%%F"
    )
)

endlocal
goto :EOF


:copyHeaderFiles
setlocal
set SOURCE_DIR=%~1\include
set TARGET_DIR=%~2\include
set LIST=%~3

for %%F in (%LIST%) do (
    if exist "%SOURCE_DIR%\%%F\.\" (
        xcopy "%SOURCE_DIR%\%%F" "%TARGET_DIR%\%%~nxF" /s /e /i /y
    ) else (
        copy /y "%SOURCE_DIR%\%%F" "%TARGET_DIR%"  || echo %%F
    )
)

endlocal
goto :EOF
