#!/usr/bin/env python3
"""Follow the known Windows network adapter when its IPv4 address changes."""
import base64
import concurrent.futures
import ipaddress
import os
import plistlib
import re
import subprocess
import sys
import time
from pathlib import Path

MAC = '04:42:1a:02:b6:7b'
AGENT = Path.home() / 'Library/LaunchAgents/com.jiajia.sonobusheadless.plist'
LABEL = f'gui/{os.getuid()}/com.jiajia.sonobusheadless'

def command(*args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=4).stdout

def addresses(arp, mac=MAC):
    wanted = tuple(int(x, 16) for x in mac.split(':'))
    found = set()
    for ip, hw in re.findall(r'\((\d+\.\d+\.\d+\.\d+)\) at ([0-9a-fA-F:]+)', arp):
        try:
            if tuple(int(x, 16) for x in hw.split(':')) == wanted:
                found.add(ip)
        except ValueError:
            pass
    return found

def probe(ip):
    try:
        return subprocess.run(['/sbin/ping', '-c', '1', '-W', '350', str(ip)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2).returncode == 0
    except (subprocess.TimeoutExpired, OSError):
        return False

def discover(current):
    # Verify live reachability before trusting potentially stale ARP entries.
    if probe(current) and current in addresses(command('/usr/sbin/arp', '-an')):
        return current
    matches = addresses(command('/usr/sbin/arp', '-an'))
    for ip in matches:
        if probe(ip) and ip in addresses(command('/usr/sbin/arp', '-an')):
            return ip
    # Scan the current Wi-Fi subnet only; never accept an IP without the known MAC.
    interface = command('/sbin/route', '-n', 'get', 'default')
    match = re.search(r'interface:\s*(\S+)', interface)
    if not match:
        return None
    info = command('/sbin/ifconfig', match.group(1))
    match = re.search(r'inet (\d+\.\d+\.\d+\.\d+) netmask (0x[0-9a-f]+)', info)
    if not match:
        return None
    network = ipaddress.ip_network(f'{match.group(1)}/{ipaddress.IPv4Address(int(match.group(2), 16))}', strict=False)
    if network.num_addresses > 1024:
        return None
    with concurrent.futures.ThreadPoolExecutor(max_workers=48) as pool:
        list(pool.map(probe, network.hosts()))
    valid = [ip for ip in addresses(command('/usr/sbin/arp', '-an')) if ipaddress.ip_address(ip) in network and probe(ip)]
    return valid[0] if len(valid) == 1 else None

def windows_port_and_watchdog(ip):
    # Use the existing pinned SSH host key, even after a DHCP address change.
    script = r"""$ProgressPreference='SilentlyContinue'; $ErrorActionPreference='Stop'; $task=Get-ScheduledTask -TaskName 'SonoBus Headless Direct (Windows)'; if ($task.State -ne 'Running') { Start-ScheduledTask -TaskName $task.TaskName }; $cfg=Get-Content -LiteralPath (Join-Path $task.Actions[0].WorkingDirectory 'startup-config.json') -Raw | ConvertFrom-Json; Write-Output ('SONOBUS_PORT=' + [int]$cfg.LocalUdpPort)"""
    encoded = base64.b64encode(script.encode('utf-16le')).decode('ascii')
    try:
        result = subprocess.run(['/usr/bin/ssh', '-o', 'BindAddress=0.0.0.0', '-o', 'HostKeyAlias=192.168.1.8',
                                 '-o', f'HostName={ip}', '-o', 'ConnectTimeout=4', 'lan-mouse-windows',
                                 f'powershell -NoProfile -EncodedCommand {encoded}'],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=12)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    match = re.search(r'(?m)^SONOBUS_PORT=(\d+)\s*$', result.stdout)
    if not match:
        return None
    port = int(match.group(1))
    return str(port) if 51440 <= port <= 51459 else None


def run():
    with AGENT.open('rb') as f:
        config = plistlib.load(f)
    args = config['ProgramArguments']
    if len(args) < 4 or args[2] != '-d' or not args[0].endswith('/SonoBusHeadless.app/Contents/MacOS/SonoBus'):
        raise ValueError('unexpected SonoBus launch configuration')
    current, port = args[3].rsplit(':', 1)
    ipaddress.IPv4Address(current)
    if not port.isdigit() or not 51440 <= int(port) <= 51459:
        raise ValueError('unexpected SonoBus port')
    target = discover(current)
    if target is None:
        return
    next_port = windows_port_and_watchdog(target)
    if next_port is None:
        print('Windows port unavailable; preserving existing connection', file=sys.stderr, flush=True)
        return
    if target == current and next_port == port:
        return
    args[3] = f'{target}:{next_port}'
    temp = AGENT.with_suffix('.plist.tmp')
    with temp.open('wb') as f:
        plistlib.dump(config, f)
    os.replace(temp, AGENT)
    try:
        subprocess.run(['/bin/launchctl', 'bootout', LABEL], check=True, timeout=15)
        # launchd can briefly retain the previous service after bootout.
        for attempt in range(5):
            result = subprocess.run(['/bin/launchctl', 'bootstrap', f'gui/{os.getuid()}', str(AGENT)], timeout=15, capture_output=True)
            if result.returncode == 0:
                break
            if attempt == 4:
                raise subprocess.CalledProcessError(result.returncode, result.args)
            time.sleep(0.5)
    except (subprocess.SubprocessError, OSError):
        args[3] = f'{current}:{port}'
        with temp.open('wb') as f:
            plistlib.dump(config, f)
        os.replace(temp, AGENT)
        subprocess.run(['/bin/launchctl', 'bootstrap', f'gui/{os.getuid()}', str(AGENT)], timeout=15, check=False)
        raise
    print(f'SonoBus peer updated {current}:{port} -> {target}:{next_port}', flush=True)

if __name__ == '__main__':
    try:
        run()
    except Exception as exc:
        print(f'SonoBus peer discovery failed: {exc}', file=sys.stderr, flush=True)
        sys.exit(1)
