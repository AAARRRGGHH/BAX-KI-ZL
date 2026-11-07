"""Portable launcher backend. No device is opened until the user confirms it."""
from __future__ import annotations

from dataclasses import dataclass, asdict
from pathlib import Path
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid

VID, APP_PID, ROM_PID = 0x303A, 0x4005, 0x1001


def resources() -> Path:
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent)) / 'payload'


def user_data() -> Path:
    root = Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'BAX-KI-ZL'
    root.mkdir(parents=True, exist_ok=True)
    return root


@dataclass(frozen=True)
class Port:
    device: str
    vid: int | None
    pid: int | None
    serial: str | None
    location: str | None
    product: str

    @property
    def label(self):
        kind = 'BAX:KI:ZL' if self.pid == APP_PID else 'ESP32 USB bootloader'
        return f'{self.device}  ·  {kind}  ·  {self.product}'


def ports() -> list[Port]:
    from serial.tools import list_ports
    return [Port(p.device, p.vid, p.pid, p.serial_number, p.location,
                 p.product or p.description or '') for p in list_ports.comports()
            if p.vid == VID and p.pid in (APP_PID, ROM_PID)]


def identity(value):
    return re.sub('[^a-z0-9]', '', (value or '').lower())


def same_device(port: Port, target: Port) -> bool:
    return bool((target.location and port.location == target.location) or
                (identity(target.serial) and identity(port.serial) == identity(target.serial)))


def validate_payload(root: Path) -> dict:
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    files = manifest['firmware']
    if {item['offset'] for item in files} != {0, 0x8000, 0x10000} or len(files) != 3:
        raise RuntimeError('Invalid firmware package: unexpected flash regions.')
    limits = {0: 0x8000, 0x8000: 0x1000, 0x10000: 0x800000}
    base = root.resolve()
    for item in files:
        file = (base / item['file']).resolve()
        if not file.is_relative_to(base) or not file.is_file():
            raise RuntimeError('Firmware file missing or outside the package directory.')
        size = file.stat().st_size
        if size <= 0 or size > limits[item['offset']]:
            raise RuntimeError(f'Invalid firmware size: {file.name}')
        if hashlib.sha256(file.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError(f'File integrity check failed: {file.name}. Download the package again.')
    return manifest


class Output(io.TextIOBase):
    """Consume esptool's newline, carriage-return and backspace progress output."""
    def __init__(self, emit, log, phase):
        self.emit, self.log, self.phase = emit, log, phase
        self.pending = ''
        self.last_percent = -1
        self.part = 0

    def writable(self):
        return True

    def isatty(self):
        return False

    def write(self, value):
        self.pending += value
        self.log.write(value.replace('\b', ''))
        self.log.flush()
        chunks = re.split(r'[\r\n\b]+', self.pending)
        self.pending = chunks.pop()
        for line in chunks:
            if line.strip():
                self.emit('log', line.strip())
                for match in re.finditer(r'(\d+)\s*%', line):
                    percent = min(100, int(match[1]))
                    if self.phase == 'write':
                        percent = min(99, (min(self.part, 2) * 100 + percent) // 3)
                    if percent > self.last_percent:
                        self.emit('progress', percent)
                        self.last_percent = percent
                if self.phase == 'write' and 'Hash of data verified.' in line:
                    self.part += 1
        return len(value)

    def flush(self):
        self.log.flush()


def invoke_esptool(args, emit, log, phase):
    import esptool
    output = Output(emit, log, phase)
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
        try:
            result = esptool.main(args)
        except SystemExit as error:
            if error.code not in (0, None):
                raise RuntimeError(f'esptool exited with error {error.code}.') from error
            result = None
    if output.pending.strip():
        emit('log', output.pending.strip())
    if result not in (None, 0):
        raise RuntimeError(f'esptool exited with error {result}.')


def prepare(target: Port, emit, enumerate_ports=ports, sleep=time.sleep,
            monotonic=time.monotonic, serial_factory=None) -> Port:
    current = [p for p in enumerate_ports() if p.device == target.device and
               p.vid == target.vid and p.pid == target.pid and same_device(p, target)]
    if len(current) != 1:
        raise RuntimeError('The confirmed port has changed. Please scan again and confirm the new port.')
    if target.pid == ROM_PID:
        return current[0]
    if serial_factory is None:
        from serial import Serial
        serial_factory = Serial
    emit('status', 'Starting USB bootloader …')
    with serial_factory(port=None, baudrate=1200, timeout=.2, write_timeout=.2) as connection:
        connection.port = target.device
        connection.dtr, connection.rts = True, False
        connection.open()
        sleep(.15)
        connection.dtr = False
        sleep(.1)
    end = monotonic() + 20
    while monotonic() < end:
        found = [p for p in enumerate_ports() if p.pid == ROM_PID and same_device(p, target)]
        if len(found) == 1:
            emit('log', f'Bootloader found: {found[0].device}')
            return found[0]
        if len(found) > 1:
            raise RuntimeError('Multiple matching bootloaders found. Connect only one device.')
        sleep(.2)
    raise RuntimeError('USB bootloader not found. Please use BOOT recovery.')


def flash(target: Port, root: Path, data: Path, emit, tool=invoke_esptool,
          prepare_device=prepare, enumerate_ports=ports):
    """Write only the three release regions; never erase NVS."""
    manifest = validate_payload(root)  # Must succeed before touching any device.
    logs = data / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
    log_path = logs / f'flash-{stamp}.txt'
    emit('log_path', str(log_path))
    with log_path.open('w', encoding='utf-8') as log:
        log.write(json.dumps({'target': asdict(target), 'build': manifest['built']}) + '\n')
        try:
            boot = prepare_device(target, emit)
            common = ['--chip', 'esp32s3', '--port', boot.device, '--baud', '921600']
            emit('status', 'Writing and verifying firmware …')
            emit('progress', 0)
            args = common + ['--before', 'no_reset', '--after', 'hard_reset', 'write_flash',
                             '--flash_mode', 'dio', '--flash_size', '32MB', '--flash_freq', '80m']
            for item in sorted(manifest['firmware'], key=lambda entry: entry['offset']):
                args.extend([hex(item['offset']), str(root / item['file'])])
            tool(args, emit, log, 'write')
            emit('progress', 100)
            # A changed or missing CDC port does not undo a verified write.
            found = [p for p in enumerate_ports() if p.pid == APP_PID and same_device(p, target)]
            emit('success', {'log': str(log_path),
                             'port': found[0].device if len(found) == 1 else None})
        except Exception as error:
            log.write(f'\nERROR: {error}\n')
            raise


def error_message(error: Exception) -> str:
    """Keep UI diagnostics English even when Windows supplies localized errors."""
    if isinstance(error, PermissionError) or 'permissionerror' in str(error).lower():
        return 'Access denied. Close programs using the COM port and check folder permissions.'
    if isinstance(error, FileNotFoundError):
        return 'A required file or USB port could not be found.'
    if isinstance(error, OSError):
        code = getattr(error, 'winerror', None) or error.errno
        return 'USB/serial or file access failed.' + (f' Operating system error: {code}.' if code else '')
    return str(error)


def troubleshooting(error: str) -> str:
    steps = [
        'Close serial monitors, other flash windows and any programs using the COM port.',
        'Use a USB data cable connected directly to the PC. Keep the device powered on.',
        'If needed, hold BOOT, connect USB, then release BOOT. '
        'Scan again for the port. Its COM number may change.',
        'If a write was interrupted, flash again in BOOT mode. '
        'The device can start normally once the complete firmware has been written.',
    ]
    return str(error) + '\n\n' + '\n\n'.join(f'{i + 1}. {s}' for i, s in enumerate(steps))


def install_simulator(root: Path, data: Path) -> Path:
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    destination = data / 'simulator'
    destination.mkdir(parents=True, exist_ok=True)
    for item in manifest['simulator']:
        relative = Path(item['file']).relative_to('simulator')
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('Invalid simulator file path.')
        source = root / item['file']
        if hashlib.sha256(source.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError(f'Simulator file is corrupted: {source.name}')
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != item['sha256']:
            temporary = target.with_name(target.name + '.new')
            shutil.copyfile(source, temporary)
            temporary.replace(target)
    return destination / 'SmartMidiSimulator.exe'


def run_simulator(root: Path, data: Path, parent_window: int | None = None):
    executable = install_simulator(root, data)
    log_dir = data / 'logs'
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / 'simulator.txt'
    with log_path.open('w', encoding='utf-8') as log:
        # PyInstaller changes the Windows DLL search path for its own runtime.
        # The separate native simulator must load only its own bundled CRT.
        if sys.platform == 'win32':
            import ctypes
            ctypes.windll.kernel32.SetDllDirectoryW(None)
        try:
            args = [str(executable)]
            if parent_window is not None:
                if sys.platform != 'win32' or not isinstance(parent_window, int) or parent_window <= 0:
                    raise RuntimeError('Invalid simulator host window.')
                args.extend(['--parent-window', str(parent_window)])
            return subprocess.Popen(args, cwd=executable.parent, stdout=log, stderr=log,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        finally:
            if sys.platform == 'win32' and hasattr(sys, '_MEIPASS'):
                ctypes.windll.kernel32.SetDllDirectoryW(str(sys._MEIPASS))
