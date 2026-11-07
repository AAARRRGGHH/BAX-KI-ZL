from pathlib import Path
import hashlib
import io
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
import backend as b


def port(device='COM3', pid=b.APP_PID, serial='ab:cd', location='1-2'):
    return b.Port(device, b.VID, pid, serial, location, 'BAX:KI:ZL')


class CompanionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'payload'
        self.root.mkdir()
        self.data = Path(self.temp.name) / 'data'
        self.events = []
        self.manifest = {'built': 'test', 'firmware': [], 'simulator': []}
        for offset, name in [(0, 'boot.bin'), (0x8000, 'table.bin'), (0x10000, 'app.bin')]:
            contents = (name + ' bytes').encode()
            (self.root / name).write_bytes(contents)
            self.manifest['firmware'].append(dict(offset=offset, file=name,
                sha256=hashlib.sha256(contents).hexdigest()))
        for name in ['simulator/SmartMidiSimulator.exe', 'simulator/assets/simulator_mask.png']:
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(name.encode())
            self.manifest['simulator'].append(dict(file=name, sha256=hashlib.sha256(name.encode()).hexdigest()))
        self.save_manifest()

    def save_manifest(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest), encoding='utf-8')

    def emit(self, kind, value):
        self.events.append((kind, value))

    def test_payload_hash_checked_before_any_device_access(self):
        (self.root / 'app.bin').write_bytes(b'broken')
        with self.assertRaisesRegex(RuntimeError, 'integrity check'):
            b.flash(port(), self.root, self.data, self.emit,
                    prepare_device=lambda *_: self.fail('opened a device'))

    def test_reject_unexpected_flash_offsets(self):
        self.manifest['firmware'][1]['offset'] = 0x9000
        self.save_manifest()
        with self.assertRaisesRegex(RuntimeError, 'flash regions'):
            b.validate_payload(self.root)

    def test_reject_path_traversal(self):
        self.manifest['firmware'][0]['file'] = '../escape.bin'
        self.save_manifest()
        with self.assertRaisesRegex(RuntimeError, 'outside'):
            b.validate_payload(self.root)

    def test_no_unrelated_ports_or_bluetooth_ports(self):
        from types import SimpleNamespace
        def device(vid, pid):
            return SimpleNamespace(device='COM3', vid=vid, pid=pid, serial_number='ab',
                                   location='1-2', product='device', description='device')
        with patch('serial.tools.list_ports.comports', return_value=[
                device(b.VID, b.APP_PID), device(b.VID, b.ROM_PID),
                device(0x1234, b.ROM_PID), device(b.VID, 99), device(None, None)]):
            self.assertEqual([p.pid for p in b.ports()], [b.APP_PID, b.ROM_PID])

    def test_reenumeration_identity(self):
        self.assertTrue(b.same_device(port('COM4', b.ROM_PID), port()))
        self.assertTrue(b.same_device(port('COM4', serial=None), port()))
        self.assertTrue(b.same_device(port('COM4', serial='ABCD', location=None), port()))
        self.assertFalse(b.same_device(port('COM4', serial='other', location='1-3'), port()))
        self.assertFalse(b.same_device(port(serial=None, location=None), port(serial=None, location=None)))

    def test_localized_windows_errors_have_english_ui_messages(self):
        self.assertIn('Access denied', b.error_message(PermissionError(13, 'localized system message')))
        self.assertIn('could not be found', b.error_message(FileNotFoundError('localized system message')))
        self.assertIn('USB/serial', b.error_message(OSError('localized system message')))
        self.assertEqual(b.error_message(RuntimeError('USB bootloader not found.')), 'USB bootloader not found.')

    def test_changed_port_must_be_reconfirmed(self):
        with self.assertRaisesRegex(RuntimeError, 'scan again'):
            b.prepare(port(), self.emit, enumerate_ports=lambda: [port('COM4')])

    def test_existing_rom_needs_no_1200_baud_touch(self):
        target = port(pid=b.ROM_PID)
        self.assertEqual(b.prepare(target, self.emit, enumerate_ports=lambda: [target],
                                  serial_factory=lambda **_: self.fail('unexpected touch')), target)

    def test_app_to_rom_tracks_correct_device_only(self):
        snapshots = iter([[port()], [port('COM7', b.ROM_PID, 'foreign', '1-4')],
                          [port('COM4', b.ROM_PID)]])
        class Connection:
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def open(self): pass
        result = b.prepare(port(), self.emit, enumerate_ports=lambda: next(snapshots),
                           serial_factory=lambda **_: Connection(), sleep=lambda _: None)
        self.assertEqual(result.device, 'COM4')

    def test_ambiguous_rom_is_not_selected(self):
        snapshots = iter([[port()], [port('COM4', b.ROM_PID), port('COM5', b.ROM_PID)]])
        class Connection:
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def open(self): pass
        with self.assertRaisesRegex(RuntimeError, 'Multiple'):
            b.prepare(port(), self.emit, enumerate_ports=lambda: next(snapshots),
                      serial_factory=lambda **_: Connection(), sleep=lambda _: None)

    def flash(self, tool, enumerate_ports=lambda: []):
        b.flash(port(), self.root, self.data, self.emit, tool=tool,
                prepare_device=lambda *_: port('COM4', b.ROM_PID), enumerate_ports=enumerate_ports)

    def test_bootloader_failure_never_writes(self):
        calls = []
        def fail(*_):
            raise OSError('port busy')
        with self.assertRaisesRegex(OSError, 'busy'):
            b.flash(port(), self.root, self.data, self.emit,
                    tool=lambda *args: calls.append(args), prepare_device=fail)
        self.assertEqual(calls, [])
        self.assertNotIn('success', [e[0] for e in self.events])

    def test_existing_backups_are_not_deleted(self):
        old = self.data / 'backups/old.bin'
        old.parent.mkdir(parents=True)
        old.write_bytes(b'previous backup')
        self.flash(lambda *_: None)
        self.assertEqual(old.read_bytes(), b'previous backup')
        self.assertEqual(list(old.parent.iterdir()), [old])

    def test_direct_write_three_regions_no_backup_or_nvs_erase(self):
        calls = []
        def tool(args, *_):
            calls.append(args)
        self.flash(tool)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][calls[0].index('--port') + 1], 'COM4')
        self.assertIn('write_flash', calls[0])
        self.assertNotIn('read_flash', calls[0])
        self.assertNotIn('erase_flash', calls[0])
        self.assertNotIn('0x9000', calls[0])
        self.assertEqual(calls[0][-6::2], ['0x0', '0x8000', '0x10000'])
        self.assertEqual(self.events[-1][0], 'success')
        # Returning CDC is not required for a verified flash to succeed.
        self.assertIsNone(self.events[-1][1]['port'])
        self.assertFalse((self.data / 'backups').exists())
        self.assertNotIn('backup', self.events[-1][1])

    def test_write_failure_never_reports_success(self):
        def tool(args, *_):
            raise RuntimeError('write interrupted')
        with self.assertRaisesRegex(RuntimeError, 'interrupted'):
            self.flash(tool)
        self.assertNotIn('success', [e[0] for e in self.events])
        self.assertIn('write interrupted', next((self.data / 'logs').glob('*.txt')).read_text())

    def test_progress_handles_backspaces_and_three_images(self):
        output = b.Output(self.emit, io.StringIO(), 'inspect')
        output.write('123 (25 %)\b\b\b')
        self.assertIn(('progress', 25), self.events)
        self.events.clear()
        output = b.Output(self.emit, io.StringIO(), 'write')
        for image in range(3):
            output.write('Writing at 0x0000 (0 %)\r')
            output.write('Writing at 0x0000 (100 %)\r')
            output.write('Hash of data verified.\n')
        progress = [value for kind, value in self.events if kind == 'progress']
        self.assertEqual(progress, sorted(progress))
        self.assertEqual(progress[-1], 99)  # 100 only after command verification.

    def test_simulator_update_preserves_user_settings(self):
        exe = b.install_simulator(self.root, self.data)
        settings = exe.parent / 'settings.cfg'
        settings.write_bytes(b'user choices')
        exe.write_bytes(b'outdated exe')
        b.install_simulator(self.root, self.data)
        self.assertEqual(exe.read_bytes(), b'simulator/SmartMidiSimulator.exe')
        self.assertEqual(settings.read_bytes(), b'user choices')

    def test_corrupt_simulator_not_started(self):
        (self.root / 'simulator/SmartMidiSimulator.exe').write_bytes(b'corrupt')
        with self.assertRaisesRegex(RuntimeError, 'Simulator file is corrupted'):
            b.install_simulator(self.root, self.data)


if __name__ == '__main__':
    unittest.main()
