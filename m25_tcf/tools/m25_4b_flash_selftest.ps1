# m25_4b_flash_selftest.ps1 - M25-4b2 flash self-test runbook
#
# Runs the full ELF flash cycle against the live target through the TCF agent:
#   save -> load(loader) -> flash(ELF) -> flashrestore(backup) -> stop
# Each step must exit 0; the flash step verifies the written sector itself.
#
# Preconditions:
#   - tas_server (24817) + TCF agent (1534) running, target attached
#     (check the agent log: "AURIX: attached to 'DAS JTAG AURDUINO 46YPEADW'")
#   - the target must be in the normal debug state (no reset+halt latch):
#     if DEVICE_CONNECT fails, power-cycle the board and retry
param(
    [string]$Elf = 'C:\work\tcf_agent\m25_flashtest.elf',
    [string]$Driver = 'C:\work\tcf_agent\m25_4b_flash.js'
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $Driver)
try {
    function Step([string]$name, [string[]]$argv) {
        Write-Host "=== $name ===" -ForegroundColor Cyan
        $p = Start-Process -FilePath node -ArgumentList (@($Driver) + $argv) `
             -RedirectStandardOutput '_selftest.log' -RedirectStandardError '_selftest.err' -PassThru -NoNewWindow
        $null = $p.WaitForExit(240000)
        if (-not $p.HasExited) { $p.Kill(); throw "$name : TIMEOUT" }
        Get-Content '_selftest.log' -ErrorAction SilentlyContinue | Select-Object -Last 4 | ForEach-Object { "   $_" }
        $err = Get-Content '_selftest.err' -ErrorAction SilentlyContinue | Select-Object -First 2
        if ($err) { $err | ForEach-Object { "   ERR: $_" } }
        if ($p.ExitCode -ne 0) { throw "$name failed (exit $($p.ExitCode))" }
    }

    Step '1. save (state snapshot)'            @('save')
    Step '2. load (flash loader)'              @('load')
    Step '3. flash (ELF -> PFLASH, RMW+verify)' @('flash', $Elf)
    Step '4. restore (sector backups)'         @('flashrestore', $Elf)
    Step '5. stop (restore state, resume)'     @('stop')
    Write-Host 'SELF-TEST PASSED' -ForegroundColor Green
}
finally {
    Pop-Location
}
