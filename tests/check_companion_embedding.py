"""Windows integration check: real embedded SDL child and native mouse messages.

Uses an off-screen host and isolated simulator settings, never serial hardware.
"""
from pathlib import Path
import ctypes
from ctypes import wintypes as wt
import hashlib
import json
import shutil
import sys
import tempfile
import time
import tkinter as tk

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'launcher'))
from app import App


def main():
    if sys.platform != 'win32':
        return
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
    with tempfile.TemporaryDirectory(prefix='companion-embedding-', dir=ROOT / 'build') as temporary:
        folder = Path(temporary)
        payload = folder / 'payload'
        shutil.copytree(ROOT / 'build/companion/payload', payload)
        shutil.copyfile(ROOT / 'build/Release/SmartMidiSimulator.exe', payload / 'simulator/SmartMidiSimulator.exe')
        manifest = json.loads((payload / 'manifest.json').read_text())
        for item in manifest['simulator']:
            item['sha256'] = hashlib.sha256((payload / item['file']).read_bytes()).hexdigest()
        (payload / 'manifest.json').write_text(json.dumps(manifest))
        root = tk.Tk()
        root.withdraw()
        app = App(root, payload, folder / 'data')
        root.geometry('720x720+20000+20000')
        root.deiconify()
        root.update()
        user = ctypes.windll.user32
        user.GetParent.argtypes = [wt.HWND]
        user.GetParent.restype = wt.HWND
        user.GetWindowLongPtrW.argtypes = [wt.HWND, ctypes.c_int]
        user.GetWindowLongPtrW.restype = ctypes.c_ssize_t
        user.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
        user.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
        callback = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        user.EnumChildWindows.argtypes = [wt.HWND, callback, wt.LPARAM]

        def pump(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                root.update()
                time.sleep(.01)

        try:
            # Seed a modified chord using a supported legacy config. This tests
            # native RESET without relying on off-screen mouse focus for LVGL notes.
            config=folder/'data/simulator/settings.cfg';config.parent.mkdir(parents=True,exist_ok=True)
            config.write_text('10 0 0 1 3074023 1 1 50 400 0 0 0 3 0 0 0 0 0 0\n')
            app.launch_simulator()
            pump(4.5)
            assert app.simulator and app.simulator.poll() is None, 'simulator did not start'
            host = app.simulator_host.winfo_id()
            children = []

            @callback
            def child(hwnd, unused):
                name = ctypes.create_unicode_buffer(100)
                user.GetClassNameW(hwnd, name, 100)
                if name.value == 'SDL_app':
                    children.append(hwnd)
                return True

            user.EnumChildWindows(host, child, 0)
            assert len(children) == 1, children
            hwnd = children[0]
            assert user.GetParent(hwnd) == host
            style = user.GetWindowLongPtrW(hwnd, -16)
            assert style & 0x40000000 and not style & 0x00c00000, hex(style)
            root.geometry('780x660')
            pump(.5)
            assert root.winfo_width() == root.winfo_height()
            bounds = wt.RECT()
            user.GetClientRect(hwnd, ctypes.byref(bounds))
            assert (bounds.right, bounds.bottom) == (root.winfo_width(), root.winfo_height()), (bounds.right, bounds.bottom)

            def click(x, y, right=False):
                px, py = int(bounds.right*x), int(bounds.bottom*y)
                xy = px | (py << 16)
                user.PostMessageW(hwnd, 0x200, 0, xy)
                user.PostMessageW(hwnd, 0x204 if right else 0x201, 2 if right else 1, xy)
                if not right:pump(.12)  # Let LVGL sample the held contact before release.
                # SDL button messages use the last mouse position, not LPARAM.
                # Restore it immediately before release in the off-screen host.
                user.PostMessageW(hwnd, 0x200, 2 if right else 1, xy)
                user.PostMessageW(hwnd, 0x205 if right else 0x202, 0, xy)
                pump(.15)

            click(.085, .085)  # Reset the seeded chord alteration.
            click(.085, .085)  # Empty state: hidden RESET must be inert.
            for _ in range(2):  # PWR: Chords -> Arpeggiator -> Kinetic.
                user.PostMessageW(hwnd, 0x100, ord('P'), 1 | (0x19 << 16))
                pump(.12)
                user.PostMessageW(hwnd, 0x101, ord('P'), 0xc0000001 | (0x19 << 16))
                pump(.15)
            click(.5, .5, right=True)  # Right click opens Kinetic settings.
            click(.5, .5, right=True)  # And closes them again.
            process = app.simulator
            # Normal device power-off is now the route back to Companion home.
            user.PostMessageW(hwnd, 0x100, ord('B'), 1 | (0x30 << 16))
            pump(3.4)
            pump(.5)
            assert process.poll() == 0, process.poll()
            assert app.simulator_host is None, 'Power-off did not restore companion home'
            log = (folder / 'data/logs/simulator.txt').read_text(encoding='utf-8')
            (ROOT / 'build/companion/embedding-test.log').write_text(log, encoding='utf-8')
            assert log.count('Companion action: reset')==1, log
            for action in ['reset', 'kinetic menu', 'shutdown']:
                assert f'Companion action: {action}' in log, log[-1200:]
            print('Embedded SDL, square resize, reset top-left, Kinetic right-click and power-off to home: PASS')
        finally:
            log_file = folder / 'data/logs/simulator.txt'
            if log_file.exists():
                (ROOT / 'build/companion/embedding-test.log').write_text(log_file.read_text(encoding='utf-8'), encoding='utf-8')
            if app.simulator and app.simulator.poll() is None:
                app.simulator.terminate()
                app.simulator.wait(timeout=5)
            app.closed = True
            for timer in root.tk.call('after', 'info'):
                root.after_cancel(timer)
            root.destroy()


if __name__ == '__main__':
    main()
