"""BAX:KI:ZL companion: standalone Windows launcher, no development tools needed."""
from pathlib import Path
import ctypes
import json
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import ttk
import tkinter.font as tkfont
from PIL import Image, ImageTk

import backend
from brand import ACCENT, dark_accent, simulator_accent, tint_logo
from square_window import SquareWindow
from button_art import render as render_button

BLACK, WHITE, MUTED = '#000000', '#ffffff', '#9ca8aa'


class RoundedButton(tk.Canvas):
    def __init__(self, parent, text, command, font, accent=True, shape='pill', color=ACCENT):
        self.text, self.command = text, command
        self.shape = shape
        self.scale = 1.
        self.edge = color if accent else '#647174'
        self.dark = dark_accent(color)
        self.hover = self.pressed = False
        self.art_key = None
        label_font = tkfont.Font(family=font, size=17)
        super().__init__(parent, width=label_font.measure(text) + 48, height=56,
                         bg=BLACK, highlightthickness=0, cursor='hand2', takefocus=True)
        self.label_font = label_font
        self.bind('<Configure>', lambda _: self.paint())
        self.bind('<Enter>', lambda _: self.state(hover=True))
        self.bind('<Leave>', lambda _: self.state(hover=False))
        self.bind('<ButtonPress-1>', lambda _: self.state(pressed=True))
        self.bind('<ButtonRelease-1>', self.release)
        self.bind('<Return>', lambda _: self.command())
        self.bind('<space>', lambda _: self.command())
        self.bind('<FocusIn>', lambda _: self.paint())
        self.bind('<FocusOut>', lambda _: self.paint())

    def state(self, **values):
        for key, value in values.items():
            setattr(self, key, value)
        self.paint()

    def paint(self):
        w, h = self.winfo_width(), self.winfo_height()
        if w < 12 or h < 12:
            return
        edge = WHITE if self.focus_get() == self else self.edge
        fill = self.dark if self.hover or self.pressed else BLACK
        key = (w, h, self.scale, self.shape, edge, fill)
        if key != self.art_key:
            self.art = ImageTk.PhotoImage(render_button(*key))
            self.art_key = key
        self.delete('all')
        self.create_image(0, 0, anchor='nw', image=self.art)
        offset = 4*self.scale if self.shape == 'left' else -4*self.scale if self.shape == 'right' else 0
        self.create_text(w/2+offset, h/2-1, text=self.text, font=self.label_font, fill=WHITE)

    def resize(self, width, height, scale):
        self.scale = scale
        # Negative font sizes are physical pixels, independent of the desktop DPI.
        self.label_font.configure(size=-max(12, round(25*scale)))
        self.configure(width=width, height=height)
        self.paint()

    def release(self, event):
        pressed = self.pressed
        self.state(pressed=False)
        if pressed and 0 <= event.x <= self.winfo_width() and 0 <= event.y <= self.winfo_height():
            self.command()


class App:
    def __init__(self, root, payload=None, data=None):
        self.root = root
        self.payload = payload or backend.resources()
        self.data = data or backend.user_data()
        self.accent = simulator_accent(self.data)
        self.events = queue.Queue()
        self.busy = False
        self.candidates = []
        self.simulator = None
        self.log_path = None
        self.closed = False
        self.simulator_host = None
        self.last_size = (720, 720)
        self.resize_timer = None
        self.home_buttons = None
        self.logo_size = None
        root.title('BAX:KI:ZL · Companion')
        icon = self.payload / 'companion.ico'
        if icon.is_file() and sys.platform == 'win32':
            root.iconbitmap(default=str(icon))
            root.iconbitmap(str(icon))
        elif (self.payload / 'companion-icon.png').is_file():
            self.window_icon = ImageTk.PhotoImage(Image.open(self.payload / 'companion-icon.png'))
            root.iconphoto(True, self.window_icon)
        root.configure(bg=BLACK)
        root.geometry('720x720')
        root.minsize(620, 620)
        largest = max(620, root.winfo_screenheight() - 100)
        root.maxsize(largest, largest)
        root.aspect(1, 1, 1, 1)
        root.bind('<Configure>', self.square_resize, add='+')
        font_path = self.payload / 'Barlow-Regular.ttf'
        if sys.platform == 'win32':
            ctypes.windll.gdi32.AddFontResourceExW(str(font_path), 0x10, 0)
        self.font = 'Barlow'
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('Accent.Horizontal.TProgressbar', troughcolor=dark_accent(self.accent),
                        background=self.accent, borderwidth=0, lightcolor=self.accent, darkcolor=self.accent)
        style.configure('Dark.TCombobox', fieldbackground='#101819', background='#101819',
                        foreground=WHITE, arrowcolor=self.accent, padding=8)
        style.map('Dark.TCombobox', fieldbackground=[('readonly', '#101819')],
                  foreground=[('readonly', WHITE)], selectbackground=[('readonly', '#101819')])
        self.logo_template = Image.open(self.payload / 'logo.png').convert('RGB')
        self.logo_source = tint_logo(self.logo_template, self.accent)
        self.logo = ImageTk.PhotoImage(self.logo_source)
        self.header = tk.Label(root, image=self.logo, bg=BLACK, bd=0, padx=0, pady=0, highlightthickness=0)
        self.body = tk.Frame(root, bg=BLACK)
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.home()
        self.square_window = SquareWindow(root, 620, largest)
        self.root.after(50, self.poll)

    def label(self, parent, text, size=15, color=WHITE, **kwargs):
        return tk.Label(parent, text=text, font=(self.font, size), fg=color,
                        bg=BLACK, **kwargs)

    def button(self, parent, text, command, accent=True):
        return RoundedButton(parent, text, command, self.font, accent, color=self.accent)

    def square_resize(self, event):
        if event.widget != self.root or self.closed:
            return
        width, height = event.width, event.height
        if width == height:
            self.last_size = (width, height)
            self.layout()
            return
        if self.resize_timer:
            self.root.after_cancel(self.resize_timer)
        side = width if abs(width-self.last_size[0]) >= abs(height-self.last_size[1]) else height
        side = max(620, side)
        self.resize_timer = self.root.after(30, lambda: self.root.geometry(f'{side}x{side}'))

    def layout(self):
        side = min(self.root.winfo_width(), self.root.winfo_height())
        if side < 620:
            side = self.last_size[0]
        # A 720px design canvas: every dimension derives from the same scale.
        scale = side / 720
        home = self.home_buttons is not None
        width = round(side*(.74 if home else .55))
        if width != self.logo_size:
            self.logo_size = width
            image = self.logo_source.resize((width, round(width*self.logo_source.height/self.logo_source.width)),
                                            Image.Resampling.LANCZOS)
            self.logo = ImageTk.PhotoImage(image)
            self.header.configure(image=self.logo)
        if home:
            bw, bh, gap = round(225*scale), round(62*scale), round(5*scale)
            # Match the actual outline edges, not the image rectangle: the top
            # capsule stroke starts at SVG y=43 in the 250px logo canvas.
            body_top = int(.60*side+.5)
            body_height = int(.22*side+.5)
            row_top = int(.5*body_height+.5) - bh//2
            button_bottom = body_top + row_top + bh - max(2., 2.*scale)
            top_gap = side-button_bottom
            image_top = round(top_gap - self.logo.height()*43/250)
            self.header.place(relx=.5, rely=0, y=image_top, anchor='n')
            self.home_row.configure(width=2*bw+gap, height=bh)
            for index, button in enumerate(self.home_buttons):
                button.resize(bw, bh, scale)
                button.place(x=index*(bw+gap), y=0, width=bw, height=bh)

    def clear(self):
        self.home_buttons = None
        if self.simulator_host is not None:
            self.simulator_host.destroy()
            self.simulator_host = None
        for child in self.body.winfo_children():
            child.destroy()
        self.header.place(relx=.5, rely=.16, y=0, anchor='center')
        self.body.place(relx=.07, rely=.30, relwidth=.86, relheight=.65)
        self.layout()

    def refresh_accent(self):
        accent = simulator_accent(self.data)
        if accent == self.accent:
            return
        self.accent = accent
        self.logo_source = tint_logo(self.logo_template, accent)
        self.logo_size = None
        style = ttk.Style(self.root)
        style.configure('Accent.Horizontal.TProgressbar', troughcolor=dark_accent(accent),
                        background=accent, lightcolor=accent, darkcolor=accent)
        style.configure('Dark.TCombobox', arrowcolor=accent)

    def home(self):
        self.refresh_accent()
        self.clear()
        self.body.place(relx=.04, rely=.60, relwidth=.92, relheight=.22)
        self.home_row = tk.Frame(self.body, bg=BLACK)
        self.home_row.place(relx=.5, rely=.5, anchor='center')
        self.home_buttons = (
            RoundedButton(self.home_row, 'Flash Firmware', self.search, self.font, shape='left', color=self.accent),
            RoundedButton(self.home_row, 'Run Simulator', self.launch_simulator, self.font, shape='right', color=self.accent))
        self.layout()

    def background(self, work):
        if self.busy:
            return
        self.busy = True
        def run():
            try:
                work()
            except Exception as error:
                self.events.put(('error', backend.error_message(error)))
        threading.Thread(target=run, daemon=True).start()

    def emit(self, kind, value):
        self.events.put((kind, value))

    def search(self):
        if self.busy:
            return
        self.clear()
        self.label(self.body, 'Looking for your Waveshare …', 20, self.accent).pack(pady=20)
        self.background(lambda: self.emit('ports', backend.ports()))

    def show_ports(self, candidates):
        self.busy = False
        self.candidates = candidates
        self.clear()
        if not candidates:
            self.label(self.body, 'No compatible USB port found', 20, self.accent).pack(pady=8)
            self.label(self.body, 'Connect the Waveshare using a USB data cable.\n'
                       'If needed, hold BOOT, connect USB, then release BOOT.\n'
                       'Close any serial monitors before trying again.', 15, MUTED,
                       justify='center').pack(pady=14)
        else:
            self.label(self.body, 'Confirm flash port', 20, self.accent).pack(pady=(5, 12))
            self.port_choice = ttk.Combobox(self.body, values=[p.label for p in candidates],
                                           state='readonly', style='Dark.TCombobox',
                                           font=(self.font, 13), width=60)
            self.port_choice.pack(fill='x', pady=4)
            if len(candidates) == 1:
                self.port_choice.current(0)
            self.label(self.body, 'Confirm only the Waveshare 1.75C (32 MB). A USB bootloader\n'
                       'could belong to another ESP32. If multiple devices are connected,\n'
                       'select the correct port or disconnect the other devices.\n'
                       'Your saved settings will be preserved.', 13, MUTED,
                       justify='center').pack(pady=10)
        row = tk.Frame(self.body, bg=BLACK)
        row.pack(pady=10)
        self.button(row, 'Back', self.home, False).pack(side='left', padx=6)
        self.button(row, 'Scan Again', self.search, False).pack(side='left', padx=6)
        if candidates:
            self.button(row, 'Start Flash', self.start_flash).pack(side='left', padx=6)

    def start_flash(self):
        selected = self.port_choice.current()
        if selected < 0:
            return
        target = self.candidates[selected]
        self.clear()
        self.log_path = None
        self.status = self.label(self.body, 'Checking firmware package …', 18, self.accent)
        self.status.pack(pady=(2, 12))
        self.progress = ttk.Progressbar(self.body, style='Accent.Horizontal.TProgressbar',
                                        maximum=100, mode='determinate')
        self.progress.pack(fill='x', pady=5)
        self.percent = self.label(self.body, '0 %', 14)
        self.percent.pack()
        self.detail = tk.Text(self.body, height=6, bg='#090e0f', fg=MUTED, relief='flat',
                              font=('Consolas', 10), wrap='word', state='disabled')
        self.detail.pack(fill='both', expand=True, pady=(10, 4))
        self.label(self.body, 'Keep USB connected while flashing.',
                   12, MUTED).pack()
        self.background(lambda: backend.flash(target, self.payload, self.data, self.emit))

    def launch_simulator(self):
        if self.busy or (self.simulator and self.simulator.poll() is None):
            return
        self.clear()
        self.header.place_forget()
        self.body.place_forget()
        self.simulator_host = tk.Frame(self.root, bg=BLACK)
        self.simulator_host.place(x=0, y=0, relwidth=1, relheight=1)
        self.root.update_idletasks()
        parent_window = self.simulator_host.winfo_id()
        self.background(lambda: self.emit('simulator', backend.run_simulator(self.payload, self.data, parent_window)))

    def result(self, success, value):
        self.busy = False
        self.clear()
        self.label(self.body, 'Firmware flashed successfully' if success else 'Operation unsuccessful',
                   22, self.accent).pack(pady=(2, 12))
        if success:
            text = ('The firmware has been written and verified.\n'
                    'Your device is restarting. Its COM port may change.\n'
                    'If it stays in the bootloader, release BOOT and restart the device.\n\n'
                    'Log: ' + value['log'])
        else:
            text = backend.troubleshooting(value)
        text_area = tk.Frame(self.body, bg=BLACK)
        text_area.pack(fill='both', expand=True)
        panel = tk.Text(text_area, height=8, bg=BLACK, fg=WHITE, relief='flat',
                        font=(self.font, 13), wrap='word')
        scrollbar = tk.Scrollbar(text_area, command=panel.yview, bg=BLACK,
                                 troughcolor=BLACK, activebackground=self.accent)
        panel.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side='right', fill='y')
        panel.insert('1.0', text)
        panel.configure(state='disabled')
        panel.pack(side='left', fill='both', expand=True)
        row = tk.Frame(self.body, bg=BLACK)
        row.pack(pady=(10, 0))
        self.button(row, 'Done' if success else 'Back', self.home).pack(side='left', padx=8)
        self.button(row, 'Open Logs', lambda: os.startfile(str(self.data)), False).pack(side='left', padx=8)

    def poll(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == 'ports':
                    self.show_ports(value)
                elif kind == 'status':
                    self.status.configure(text=value)
                elif kind == 'progress':
                    self.progress['value'] = value
                    self.percent.configure(text=f'{value} %')
                elif kind == 'log':
                    if hasattr(self, 'detail') and self.detail.winfo_exists():
                        self.detail.configure(state='normal')
                        self.detail.insert('end', value + '\n')
                        if int(self.detail.index('end-1c').split('.')[0]) > 150:
                            self.detail.delete('1.0', '40.0')
                        self.detail.see('end')
                        self.detail.configure(state='disabled')
                elif kind == 'log_path':
                    self.log_path = value
                elif kind == 'success':
                    self.result(True, value)
                elif kind == 'error':
                    self.result(False, value)
                elif kind == 'simulator':
                    self.busy = False
                    self.simulator = value
        except queue.Empty:
            pass
        if self.simulator and self.simulator.poll() is not None:
            code = self.simulator.returncode
            self.simulator = None
            if code:
                self.result(False, f'The simulator exited with error {code}. '
                            'Choose Open Logs and check logs/simulator.txt for details.')
            else:
                self.home()
        if not self.closed:
            self.root.after(50, self.poll)

    def close(self):
        if self.busy:
            # Never terminate the flash worker halfway through a write.
            self.root.bell()
            return
        # Simulator EXE, mask and all CRT DLLs live in the persistent user folder;
        # it remains independent of the launcher's temporary one-file runtime.
        self.closed = True
        self.root.destroy()


def main():
    if sys.platform == 'win32':
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('AAARRRGGHH.BAX-KI-ZL.Companion')
    root = tk.Tk()
    # Avoid two companion instances racing for the same serial port or simulator
    # installation. The Windows mutex handle stays alive until process exit.
    if sys.platform == 'win32' and '--ui-smoke' not in sys.argv:
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateMutexW.restype = ctypes.c_void_p
        mutex = kernel.CreateMutexW(None, False, 'Local\\BAX-KI-ZL-Companion')
        if not mutex or ctypes.get_last_error() == 183:
            from tkinter import messagebox
            root.withdraw()
            messagebox.showinfo('BAX:KI:ZL', 'The Companion application is already running.')
            root.destroy()
            return
    smoke_output = (Path(sys.argv[sys.argv.index('--ui-smoke') + 1])
                    if '--ui-smoke' in sys.argv else None)
    app = App(root, data=smoke_output.parent / 'smoke-data' if smoke_output else None)
    if smoke_output:
        # Package check only: no serial access and no flash.
        output = smoke_output
        def report_error(kind, value, traceback):
            output.write_text(json.dumps({'ok': False, 'error': str(value)}), encoding='utf-8')
            app.busy = False
            app.close()
        root.report_callback_exception = report_error
        def check():
            import esptool
            from serial.tools import list_ports
            backend.validate_payload(app.payload)
            with (output.parent / 'image-info.txt').open('w', encoding='utf-8') as log:
                image = next(item for item in json.loads((app.payload / 'manifest.json').read_text())['firmware']
                             if item['offset'] == 0x10000)
                backend.invoke_esptool(['--chip', 'esp32s3', 'image_info',
                    str(app.payload / image['file'])], lambda *_: None, log, 'inspect')
            backend.install_simulator(app.payload, output.parent / 'smoke-data')
            root.update_idletasks()
            output.write_text(json.dumps({'ok': True, 'esptool': esptool.__version__,
                                          'title': root.title(), 'size': [root.winfo_width(), root.winfo_height()]}),
                              encoding='utf-8')
            app.close()
        root.after(1000, check)
    root.mainloop()


if __name__ == '__main__':
    main()
