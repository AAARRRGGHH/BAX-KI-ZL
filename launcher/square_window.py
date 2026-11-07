"""Keep the client area square during the native Windows sizing gesture."""
import ctypes
import sys
from ctypes import wintypes as wt


def constrain(rect, edge, frame, current, minimum, maximum):
    left, top, right, bottom = rect
    fw, fh = frame
    width, height = right-left-fw, bottom-top-fh
    if edge in (1, 2):
        side = width
    elif edge in (3, 6):
        side = height
    else:
        side = width if abs(width-current) >= abs(height-current) else height
    side = max(minimum, min(maximum, side))
    if edge in (1, 4, 7):
        left = right-side-fw
    else:
        right = left+side+fw
    if edge in (3, 4, 5):
        top = bottom-side-fh
    else:
        bottom = top+side+fh
    return left, top, right, bottom


class SquareWindow:
    def __init__(self, root, minimum, maximum):
        self.hwnd = None
        if sys.platform != 'win32':
            return
        root.update_idletasks()
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.user.GetParent.argtypes = [wt.HWND]
        self.user.GetParent.restype = wt.HWND
        self.user.GetWindowLongPtrW.argtypes = [wt.HWND, ctypes.c_int]
        self.user.GetWindowLongPtrW.restype = ctypes.c_ssize_t
        self.user.SetWindowLongPtrW.argtypes = [wt.HWND, ctypes.c_int, ctypes.c_ssize_t]
        self.user.SetWindowLongPtrW.restype = ctypes.c_ssize_t
        self.user.CallWindowProcW.argtypes = [ctypes.c_void_p, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
        self.user.CallWindowProcW.restype = ctypes.c_ssize_t
        self.user.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
        self.user.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
        self.hwnd = self.user.GetParent(root.winfo_id())
        self.old = self.user.GetWindowLongPtrW(self.hwnd, -4)
        procedure = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM)

        @procedure
        def callback(hwnd, message, wparam, lparam):
            if message == 0x0214 and lparam:  # WM_SIZING: before Windows applies the rectangle.
                proposed = ctypes.cast(lparam, ctypes.POINTER(wt.RECT)).contents
                outer, inner = wt.RECT(), wt.RECT()
                self.user.GetWindowRect(hwnd, ctypes.byref(outer))
                self.user.GetClientRect(hwnd, ctypes.byref(inner))
                frame = (outer.right-outer.left-inner.right, outer.bottom-outer.top-inner.bottom)
                proposed.left, proposed.top, proposed.right, proposed.bottom = constrain(
                    (proposed.left, proposed.top, proposed.right, proposed.bottom),
                    wparam, frame, inner.right, minimum, maximum)
                return 1
            # Maximization would break the intended square canvas.
            if message == 0x00A3 and wparam == 2:
                return 0
            return self.user.CallWindowProcW(self.old, hwnd, message, wparam, lparam)

        self.callback = callback  # Keep the C callback alive for the HWND's lifetime.
        self.user.SetWindowLongPtrW(self.hwnd, -4, ctypes.cast(callback, ctypes.c_void_p).value)
        style = self.user.GetWindowLongPtrW(self.hwnd, -16)
        self.user.SetWindowLongPtrW(self.hwnd, -16, style & ~0x00010000)  # WS_MAXIMIZEBOX
        root.bind('<Destroy>', lambda event: self.close() if event.widget == root else None, add='+')

    def close(self):
        if self.hwnd:
            self.user.SetWindowLongPtrW(self.hwnd, -4, self.old)
            self.hwnd = None
