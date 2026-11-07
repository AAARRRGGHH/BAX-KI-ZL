"""Read-only/mock tests: never opens a physical COM port."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('usb_flash_port', Path(__file__).resolve().parents[1]/'tools/usb_flash_port.py')
usb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(usb)

class Ports(unittest.TestCase):
    def test_identity(self):
        self.assertEqual(usb.identity('AA:BB:01:02:03:04'), 'aabb01020304')
        p = SimpleNamespace(serial_number='AA:BB:01:02:03:04', location='1-2')
        self.assertTrue(usb.matches(p, dict(serial='aabb01020304', location=None)))
        self.assertFalse(usb.matches(p, dict(serial='ffffffffffff', location='1-2')))
        p.serial_number = None
        self.assertTrue(usb.matches(p, dict(serial=None, location='1-2')))
        self.assertFalse(usb.matches(p, dict(serial=None, location=None)))

    def test_only_matching_rom(self):
        target = dict(serial='aabb01020304', location=None)
        def port(name, sn, pid):
            return SimpleNamespace(device=name, vid=usb.VID, pid=pid, serial_number=sn, location=None)
        wrong = port('COM8','000000000000',usb.ROM_PID)
        right = port('COM9','AA:BB:01:02:03:04',usb.ROM_PID)
        with patch.object(usb.list_ports,'comports',return_value=[wrong,right]):
            self.assertEqual(usb.wait_for(target,usb.ROM_PID), 'COM9')
        with patch.object(usb.list_ports,'comports',return_value=[wrong]), patch.object(usb.time,'monotonic',side_effect=[0,100]):
            with self.assertRaises(RuntimeError):
                usb.wait_for(target,usb.ROM_PID)

if __name__ == '__main__':
    unittest.main()
