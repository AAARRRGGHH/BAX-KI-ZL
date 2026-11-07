"""Resolve application CDC <-> ROM USB ports without touching unrelated devices.

stdout contains only the selected COM port; status/errors go to stderr.
The 1200-baud touch is sent only to this prototype's 303A:4005 interface.
"""
from pathlib import Path
import json
import re
import sys
import time
import serial
from serial.tools import list_ports

STATE = Path(__file__).resolve().parents[1] / 'build/usb-flash-target.json'
VID, APP_PID, ROM_PID = 0x303A, 0x4005, 0x1001

def identity(value):
    return re.sub('[^a-z0-9]', '', (value or '').lower())

def matches(port, target):
    sn = identity(target.get('serial'))
    if sn and identity(port.serial_number):
        return identity(port.serial_number) == sn
    return bool(target.get('location') and port.location == target['location'])

def remember(port):
    target = dict(serial=port.serial_number, location=port.location)
    STATE.parent.mkdir(exist_ok=True)
    STATE.write_text(json.dumps(target), encoding='utf-8')
    return target

def wait_for(target, pid, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        found = [p for p in list_ports.comports() if p.vid == VID and p.pid == pid and matches(p, target)]
        if len(found) == 1:
            return found[0].device
        time.sleep(.2)
    raise RuntimeError('Kein eindeutig passender USB-Port erschienen. BOOT-Recovery verwenden; es wurde kein anderer Port ausgewaehlt.')

def main():
    action, name = sys.argv[1:]
    selected = next((p for p in list_ports.comports() if p.device.upper() == name.upper()), None)
    if action == 'prepare':
        if selected is None:
            raise RuntimeError(f'{name} ist nicht vorhanden. Seriellen Monitor schliessen und den aktuellen COM-Port pruefen.')
        target = remember(selected)
        if selected.vid == VID and selected.pid == APP_PID:
            print('BAX:KI:ZL CDC: fordere ROM-Bootloader an (1200 Baud).', file=sys.stderr)
            with serial.Serial(port=None, baudrate=1200, timeout=.2, write_timeout=.2) as connection:
                connection.port = selected.device
                connection.dtr = True
                connection.rts = False
                connection.open()
                time.sleep(.15)
                connection.dtr = False
                time.sleep(.1)
            print(wait_for(target, ROM_PID))
        else:
            print(selected.device)  # Legacy serial/JTAG or ROM: esptool handles reset.
    elif action == 'monitor':
        if selected and selected.vid == VID and selected.pid == APP_PID:
            print(selected.device)
        elif STATE.exists():
            print(wait_for(json.loads(STATE.read_text(encoding='utf-8')), APP_PID))
        else:
            raise RuntimeError('Aktuellen BAX:KI:ZL-CDC-Port im Geraetemanager nachsehen und Monitor erneut starten.')
    else:
        raise ValueError('Expected prepare or monitor')

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, serial.SerialException) as error:
        print(f'USB-Port: {error}', file=sys.stderr)
        sys.exit(1)
