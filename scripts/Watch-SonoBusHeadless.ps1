$ErrorActionPreference = 'Stop'
$base = Split-Path -Parent $PSCommandPath
$configPath = Join-Path $base 'startup-config.json'
$historyPath = Join-Path $base 'startup-history.log'
$logs = Join-Path $base 'logs'
$monitorLog = Join-Path $logs 'headless-watchdog.log'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$exe = Join-Path $base $config.Executable
$setup = Join-Path $base $config.SetupFile
$lastState = ''
$lastAttempt = [datetime]::MinValue
New-Item -ItemType Directory -Path $logs -Force | Out-Null

function Write-WatchLog([string]$Message) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'), $Message
    Add-Content -LiteralPath $monitorLog -Value $line -Encoding UTF8
    Add-Content -LiteralPath $historyPath -Value $line -Encoding UTF8
}

function Get-HeadlessProcesses {
    @(Get-CimInstance Win32_Process -Filter "Name='SonoBus.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -ieq $exe })
}

function Move-ToFreeUdpPort {
    # Only move our own service; never stop the application holding the old port.
    $candidate = $null
    for ($port = 51441; $port -le 51459; $port++) {
        if ($port -ne [int]$config.LocalUdpPort -and
            @(Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue).Count -eq 0) {
            $candidate = $port
            break
        }
    }
    if ($null -eq $candidate) { throw 'No free SonoBus port in 51441-51459' }
    $oldPort = [int]$config.LocalUdpPort
    $originalConfig = [IO.File]::ReadAllText($configPath)
    $originalSetup = [IO.File]::ReadAllText($setup)
    $expected = 'UseUdpPort="' + $oldPort + '"'
    if (-not $originalSetup.Contains($expected)) { throw 'Setup port does not match watchdog configuration' }
    $config.LocalUdpPort = $candidate
    $nextConfig = $config | ConvertTo-Json -Depth 10
    $nextSetup = $originalSetup.Replace($expected, ('UseUdpPort="' + $candidate + '"'))
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    try {
        [IO.File]::WriteAllText($setup + '.port-tmp', $nextSetup, $utf8)
        [IO.File]::WriteAllText($configPath + '.port-tmp', $nextConfig, $utf8)
        Move-Item -LiteralPath ($setup + '.port-tmp') -Destination $setup -Force
        Move-Item -LiteralPath ($configPath + '.port-tmp') -Destination $configPath -Force
    } catch {
        $config.LocalUdpPort = $oldPort
        [IO.File]::WriteAllText($setup, $originalSetup, $utf8)
        [IO.File]::WriteAllText($configPath, $originalConfig, $utf8)
        throw
    }
    Write-WatchLog "WATCHDOG_PORT_MOVED old=$oldPort new=$candidate reason=port_in_use"
}

function Start-ConfiguredHeadless {
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Executable missing: $exe" }
    if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "Setup missing: $setup" }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $stdout = Join-Path $logs "sonobus-$stamp.stdout.log"
    $stderr = Join-Path $logs "sonobus-$stamp.stderr.log"
    $arguments = '-q -d "{0}" -l "{1}"' -f $config.DirectPeer, $setup
    Write-WatchLog "WATCHDOG_START_ATTEMPT target=$($config.DirectPeer); UDP=$($config.LocalUdpPort)"
    $child = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $base -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    Start-Sleep -Seconds 3
    $alive = Get-Process -Id $child.Id -ErrorAction SilentlyContinue
    $bound = @(Get-NetUDPEndpoint -LocalPort ([int]$config.LocalUdpPort) -ErrorAction SilentlyContinue |
        Where-Object { $_.OwningProcess -eq $child.Id })
    if ($alive -and $bound.Count -gt 0) {
        Write-WatchLog "WATCHDOG_STARTED PID=$($child.Id); UDP=$($config.LocalUdpPort); target=$($config.DirectPeer); stdout=$stdout; stderr=$stderr"
    } elseif ($alive) {
        Write-WatchLog "WATCHDOG_STARTED_WITHOUT_UDP_BIND PID=$($child.Id); target=$($config.DirectPeer); stdout=$stdout; stderr=$stderr"
    } else {
        Write-WatchLog "WATCHDOG_EXITED_EARLY PID=$($child.Id); target=$($config.DirectPeer); stdout=$stdout; stderr=$stderr"
    }
}

Write-WatchLog "WATCHDOG_ACTIVE monitorPID=$PID; target=$($config.DirectPeer); UDP=$($config.LocalUdpPort)"
Start-Sleep -Seconds ([int]$config.StartupDelaySeconds)
while ($true) {
    try {
        $processes = Get-HeadlessProcesses
        $endpoints = @(Get-NetUDPEndpoint -LocalPort ([int]$config.LocalUdpPort) -ErrorAction SilentlyContinue)
        if ($endpoints.Count -gt 0) {
            $owners = @($endpoints.OwningProcess | Sort-Object -Unique)
            if ($owners.Count -eq 1) {
                $ownerPid = [int]$owners[0]
                $owner = @($processes | Where-Object { [int]$_.ProcessId -eq $ownerPid })
                if ($owner.Count -eq 1 -and $owner[0].CommandLine -match ' -d\s+' -and $owner[0].CommandLine.IndexOf([string]$config.DirectPeer, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    $state = "ONLINE PID=$ownerPid UDP=$($config.LocalUdpPort)"
                } elseif ($owner.Count -eq 1) {
                    $state = "SONOBUS_PORT_WRONG_TARGET PID=$ownerPid UDP=$($config.LocalUdpPort)"
                } else {
                    $state = "PORT_OWNED_BY_OTHER PID=$ownerPid UDP=$($config.LocalUdpPort)"
                    if ($processes.Count -eq 0) {
                        Move-ToFreeUdpPort
                        $state += " moved_to=$($config.LocalUdpPort)"
                        $lastAttempt = [datetime]::MinValue
                    }
                }
            } else {
                $state = "PORT_HAS_MULTIPLE_OWNERS UDP=$($config.LocalUdpPort)"
                if ($processes.Count -eq 0) {
                    Move-ToFreeUdpPort
                    $state += " moved_to=$($config.LocalUdpPort)"
                    $lastAttempt = [datetime]::MinValue
                }
            }
        } else {
            $matching = @($processes | Where-Object { $_.CommandLine -match ' -d\s+' -and $_.CommandLine.IndexOf([string]$config.DirectPeer, [StringComparison]::OrdinalIgnoreCase) -ge 0 })
            if ($matching.Count -gt 0) {
                $state = "PROCESS_WITHOUT_UDP PID=$($matching[0].ProcessId)"
            } else {
                $state = 'OFFLINE_NO_PROCESS'
                if (((Get-Date) - $lastAttempt).TotalSeconds -ge 15) {
                    $lastAttempt = Get-Date
                    Start-ConfiguredHeadless
                }
            }
        }
        if ($state -ne $lastState) {
            Write-WatchLog "STATE $state"
            $lastState = $state
        }
    } catch {
        Write-WatchLog "WATCHDOG_ERROR $($_.Exception.Message)"
    }
    Start-Sleep -Seconds 5
}
