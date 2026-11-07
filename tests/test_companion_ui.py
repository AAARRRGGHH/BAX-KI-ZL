"""Exercise the real Tk screens with fake devices; never open a serial port."""
from pathlib import Path
import sys
import tempfile
import time
import tkinter as tk
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
import app
import backend


class InterfaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = app.App(self.root, payload=Path(__file__).resolve().parents[1] / 'build/companion/payload',
                           data=Path(self.temp.name))
        self.root.update_idletasks()

    def tearDown(self):
        self.app.closed = True
        for timer in self.root.tk.call('after', 'info'):
            self.root.after_cancel(timer)
        self.root.destroy()
        self.temp.cleanup()

    def texts(self):
        result = []
        def walk(widget):
            if isinstance(widget, (tk.Label, tk.Button)):
                result.append(widget.cget('text'))
            if isinstance(widget, app.RoundedButton):
                result.append(widget.text)
            for child in widget.winfo_children():
                walk(child)
        walk(self.app.body)
        return result

    def finish_work(self):
        end = time.monotonic() + 2
        while self.app.busy and time.monotonic() < end:
            self.root.update()
            time.sleep(.01)
        self.assertFalse(self.app.busy)

    def test_home_two_requested_actions(self):
        self.assertIn('Flash Firmware', self.texts())
        self.assertIn('Run Simulator', self.texts())
        self.assertEqual(self.root.cget('bg'), '#000000')
        self.assertEqual([text for text in self.texts() if text], ['Flash Firmware', 'Run Simulator'])

    def test_home_proportions_scale_together(self):
        self.root.deiconify()
        def measure(side):
            self.root.geometry(f'{side}x{side}+20000+20000')
            self.root.update()
            self.app.layout()
            self.root.update_idletasks()
            left, right = self.app.home_buttons
            return [self.app.logo.width()/side, self.app.logo.height()/side,
                    left.winfo_width()/side, left.winfo_height()/side,
                    (right.winfo_x()-left.winfo_width())/side,
                    abs(left.label_font.cget('size'))/side]
        small, large = measure(640), measure(800)
        for x, y in zip(small, large):
            self.assertAlmostEqual(x, y, delta=.002)
        self.assertEqual(self.app.home_buttons[0].shape, 'left')
        self.assertEqual(self.app.home_buttons[1].shape, 'right')

    @unittest.skipUnless(sys.platform == 'win32', 'Windows native sizing')
    def test_native_resize_stays_square_before_apply(self):
        import ctypes
        from ctypes import wintypes as wt
        hook = self.app.square_window
        user = ctypes.windll.user32
        user.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
        user.SendMessageW.restype = ctypes.c_ssize_t
        outer, client = wt.RECT(), wt.RECT()
        user.GetWindowRect(hook.hwnd, ctypes.byref(outer))
        user.GetClientRect(hook.hwnd, ctypes.byref(client))
        fw, fh = outer.right-outer.left-client.right, outer.bottom-outer.top-client.bottom
        for edge in range(1, 9):
            bounds = wt.RECT(40, 60, 40+780+fw, 60+650+fh)
            result = user.SendMessageW(hook.hwnd, 0x214, edge, ctypes.addressof(bounds))
            self.assertEqual(result, 1)
            self.assertEqual(bounds.right-bounds.left-fw, bounds.bottom-bounds.top-fh)

    def test_simulator_embedded_then_return_home(self):
        from unittest.mock import Mock
        process = Mock()
        process.poll.return_value = None
        with patch.object(backend, 'run_simulator', return_value=process) as mocked:
            self.app.launch_simulator()
            self.finish_work()
        self.assertIsNotNone(self.app.simulator_host)
        self.assertEqual(mocked.call_args.args[2], self.app.simulator_host.winfo_id())
        self.assertEqual(self.app.header.winfo_manager(), '')
        process.poll.return_value = 0
        process.returncode = 0
        self.app.poll()
        self.assertIsNone(self.app.simulator_host)
        self.assertIn('Run Simulator', self.texts())

    def test_no_port_guidance_without_flash_action(self):
        self.app.show_ports([])
        self.assertIn('Scan Again', self.texts())
        self.assertNotIn('Start Flash', self.texts())

    def test_multiple_ports_require_explicit_choice(self):
        candidate = backend.Port('COM3', backend.VID, backend.APP_PID, 'id', '1-2', 'BAX:KI:ZL')
        self.app.show_ports([candidate, candidate])
        self.assertEqual(self.app.port_choice.current(), -1)
        with patch.object(backend, 'flash', side_effect=AssertionError('must not flash')):
            self.app.start_flash()
        self.assertFalse(self.app.busy)

    def test_confirmation_progress_then_success(self):
        target = backend.Port('COM3', backend.VID, backend.APP_PID, 'id', '1-2', 'BAX:KI:ZL')
        self.app.show_ports([target])
        self.assertEqual(self.app.port_choice.current(), 0)
        def flash(selected, payload, data, emit):
            self.assertEqual(selected, target)
            emit('status', 'Writing')
            emit('progress', 75)
            emit('log', 'Hash of data verified.')
            emit('success', {'log': 'flash.txt', 'port': None})
        with patch.object(backend, 'flash', side_effect=flash) as mocked:
            self.app.start_flash()
            self.finish_work()
            mocked.assert_called_once()
        self.assertIn('Firmware flashed successfully', self.texts())
        self.assertIn('Done', self.texts())

    def test_flash_error_shows_recovery_and_navigation(self):
        self.app.show_ports([backend.Port('COM3', backend.VID, backend.APP_PID, 'id', '1-2', 'BAX')])
        with patch.object(backend, 'flash', side_effect=OSError('COM3 busy')):
            self.app.start_flash()
            self.finish_work()
        self.assertIn('Operation unsuccessful', self.texts())
        self.assertIn('Back', self.texts())
        self.assertIn('Open Logs', self.texts())

    def test_close_during_flash_does_not_terminate_worker(self):
        self.app.busy = True
        self.app.close()
        self.assertFalse(self.app.closed)
        self.assertTrue(self.root.winfo_exists())
        self.app.busy = False


if __name__ == '__main__':
    unittest.main()
