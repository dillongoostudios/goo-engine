if NOT EXIST %PYTHON% (
    echo python not found, required for this operation
    exit /b 1
)
:detect_python_done

REM Use -B to avoid writing __pycache__ in lib directory and causing update conflicts.
echo ---- DEBUG update_sources.cmd: Arguments being passed to make_update.py ----
echo Python exe: %PYTHON%
echo Script: %BLENDER_DIR%\build_files\utils\make_update.py
echo Fixed Args: --git-command "%GIT%" --svn-command "%SVN%"
echo BUILD_UPDATE_ARGS: %BUILD_UPDATE_ARGS%
echo Star Args (from make.bat call to this script): %*
echo Full command to python: %PYTHON% -B %BLENDER_DIR%\build_files\utils\make_update.py --git-command "%GIT%" --svn-command "%SVN%" %BUILD_UPDATE_ARGS% %*
echo ---- /DEBUG ----
%PYTHON% -B %BLENDER_DIR%\build_files\utils\make_update.py --git-command "%GIT%" --svn-command "%SVN%" %BUILD_UPDATE_ARGS% %*

:EOF
