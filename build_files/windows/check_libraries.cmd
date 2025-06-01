REM Goo Engine is based on Blender 4.1, which uses vc15 libraries for Windows.
REM This should be set regardless of the BUILD_VS_YEAR (host compiler).
echo Note: Forcing library type to vc15 for Goo Engine (Blender 4.1 base).
set BUILD_VS_LIBDIRPOST=vc15

set BUILD_VS_SVNDIR=win64_%BUILD_VS_LIBDIRPOST%
set BUILD_VS_LIBDIR="%BLENDER_DIR%..\lib\%BUILD_VS_SVNDIR%"

if NOT "%verbose%" == "" (
	echo Library Directory = "%BUILD_VS_LIBDIR%"
)
if NOT EXIST %BUILD_VS_LIBDIR% (
	rem libs not found, but svn is on the system
	if not "%SVN%"=="" (
		if "%BUILD_UPDATE%" == "1" (
			echo.
			echo The required external libraries in %BUILD_VS_LIBDIR% are missing.
			echo The 'make update' process will attempt to download/update them shortly.
			echo.
			REM Do not exit with an error; let make_update.py handle it by exiting this script successfully.
			goto :EOF
		) else (
			echo.
			echo The required external libraries in %BUILD_VS_LIBDIR% are missing.
			echo.
			set /p GetLibs= "Would you like to download them now? (y/n) "
			if /I "!GetLibs!"=="Y" (
				echo.
				echo Downloading %BUILD_VS_SVNDIR% libraries from SVN TRUNK, please wait.
				echo Note: If your current Blender source is a release version, this might fetch libraries
				echo from an incorrect location. For robust library fetching, especially for
				echo release versions, prefer using 'make update'.
				echo.
	:RETRY
				REM Ensure the parent directory for libs exists
				if NOT EXIST "%BLENDER_DIR%..\lib" mkdir "%BLENDER_DIR%..\lib"
				"%SVN%" checkout https://svn.blender.org/svnroot/bf-blender/trunk/lib/%BUILD_VS_SVNDIR% %BUILD_VS_LIBDIR%
				if errorlevel 1 (
					set /p LibRetry= "Error during download, retry? (y/n) "
					if /I "!LibRetry!"=="Y" (
						if EXIST %BUILD_VS_LIBDIR% (
							echo Trying to cleanup %BUILD_VS_LIBDIR% ...
							pushd %BUILD_VS_LIBDIR%
							"%SVN%" cleanup
							popd
							echo Removing potentially incomplete directory %BUILD_VS_LIBDIR% for fresh checkout attempt.
							rd /s /q %BUILD_VS_LIBDIR%
						) else (
							echo Target directory %BUILD_VS_LIBDIR% was not created.
						)
						goto RETRY
					)
					echo.
					echo Error: Download of external libraries failed.
					echo This is needed for building. Please ensure SVN is configured correctly
					echo and you have network access to the Blender SVN repository.
					echo.
					exit /b 1
				)
			)
		)
	)
) else (
	if NOT EXIST "%PYTHON%" (
		if not "%SVN%"=="" (
			echo.
			echo Python not found in external libraries, updating to latest version
			echo.
			"%SVN%" update %BUILD_VS_LIBDIR%
		)
	)
)

if NOT EXIST %BUILD_VS_LIBDIR% (
	echo.
	echo Error: Required libraries not found at "%BUILD_VS_LIBDIR%"
	echo This is needed for building, aborting!
	echo.
	if "%SVN%"=="" (
		echo This is most likely caused by svn.exe not being available.
	)
	exit /b 1
)