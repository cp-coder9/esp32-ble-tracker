@echo off
setlocal EnableExtensions

echo.
echo Hard-fixing PowerShell PATH for coding agents...
echo.

set "PS_DIR=%SystemRoot%\System32\WindowsPowerShell\v1.0"
set "PS_EXE=%PS_DIR%\powershell.exe"

if not exist "%PS_EXE%" (
    echo ERROR: PowerShell was not found at:
    echo %PS_EXE%
    pause
    exit /b 1
)

echo Found PowerShell:
echo %PS_EXE%
echo.

"%PS_EXE%" -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$psDir = [System.Environment]::ExpandEnvironmentVariables('%PS_DIR%');" ^
  "$userPath = [System.Environment]::GetEnvironmentVariable('Path','User');" ^
  "if ([string]::IsNullOrWhiteSpace($userPath)) { $userPath = '' }" ^
  "$parts = $userPath -split ';' | Where-Object { $_ -and $_.Trim() -ne '' };" ^
  "$already = $parts | Where-Object { $_.TrimEnd('\') -ieq $psDir.TrimEnd('\') };" ^
  "if (-not $already) { $newPath = (($parts + $psDir) -join ';'); [System.Environment]::SetEnvironmentVariable('Path',$newPath,'User'); Write-Host 'Added PowerShell to USER PATH.' } else { Write-Host 'PowerShell is already in USER PATH.' }" ^
  "$signature = '[DllImport(\"user32.dll\", SetLastError=true, CharSet=CharSet.Auto)] public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);';" ^
  "Add-Type -MemberDefinition $signature -Name NativeMethods -Namespace Win32;" ^
  "$result = [UIntPtr]::Zero;" ^
  "[Win32.NativeMethods]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result) | Out-Null;" ^
  "Write-Host 'Environment refresh broadcast sent.'"

if not %errorlevel%==0 (
    echo.
    echo ERROR: PowerShell PATH update failed.
    echo Trying fallback shim fix...
    echo.

    set "SHIM_DIR=%USERPROFILE%\bin"

    if not exist "%SHIM_DIR%" mkdir "%SHIM_DIR%"

    echo @echo off> "%SHIM_DIR%\powershell.bat"
    echo "%PS_EXE%" %%*>> "%SHIM_DIR%\powershell.bat"

    "%PS_EXE%" -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ErrorActionPreference='Stop';" ^
      "$shimDir = [System.Environment]::ExpandEnvironmentVariables('%SHIM_DIR%');" ^
      "$userPath = [System.Environment]::GetEnvironmentVariable('Path','User');" ^
      "if ([string]::IsNullOrWhiteSpace($userPath)) { $userPath = '' }" ^
      "$parts = $userPath -split ';' | Where-Object { $_ -and $_.Trim() -ne '' };" ^
      "$already = $parts | Where-Object { $_.TrimEnd('\') -ieq $shimDir.TrimEnd('\') };" ^
      "if (-not $already) { $newPath = (($parts + $shimDir) -join ';'); [System.Environment]::SetEnvironmentVariable('Path',$newPath,'User'); Write-Host 'Added shim folder to USER PATH.' } else { Write-Host 'Shim folder already in USER PATH.' }"

    if not %errorlevel%==0 (
        echo.
        echo FATAL: Could not update user PATH even with fallback.
        echo Your Windows user environment may be locked by policy.
        echo.
        echo Manual workaround for your coding agent:
        echo Set shell path to:
        echo %PS_EXE%
        echo.
        pause
        exit /b 1
    )
)

echo.
echo Verifying from registry-backed USER environment...
echo.

"%PS_EXE%" -NoProfile -ExecutionPolicy Bypass -Command ^
  "$userPath = [System.Environment]::GetEnvironmentVariable('Path','User');" ^
  "Write-Host $userPath;" ^
  "if ($userPath -match [regex]::Escape('%PS_DIR%')) { exit 0 } else { exit 2 }"

echo.
echo Done.
echo.
echo IMPORTANT:
echo Fully close and reopen your coding agent, VS Code, Cursor, Windsurf, terminal, or IDE.
echo If it still does not work, restart Windows once.
echo.
echo Test after reopening:
echo powershell -NoProfile -Command "$PSVersionTable.PSVersion"
echo.

pause
exit /b 0