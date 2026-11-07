import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("read_power_trace", Path(__file__).resolve().parents[1] / "tools/read_power_trace.py")
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


class Port:
    def __init__(self, lines):
        self.lines = iter(lines)

    def read(self, *args, **kwargs):
        return next(self.lines, b"")


class PowerTraceReaderTests(unittest.TestCase):
    def read(self, lines):
        output = io.BytesIO()
        return reader.read_dump(Port(lines), output, clock=lambda: 0), output.getvalue()

    def test_complete_with_interleaved_logs(self):
        lines = [b"I (50) other log\n", b"PWRTRACE BEGIN v=1 first=3 end=5 retained=2 overwritten=3 snapshot_skipped=0\n",
                 b"PWRTRACE FIELDS ms,kind,detail,flags,value,extra\n",
                 b"PWRTRACE 4294967295,RAW,1,0x0001,0,0\n", b"other log\n",
                 b"PWRTRACE 0,PMIC,0,0x000f,0,0\n", b"PWRTRACE END count=2\n"]
        count, output = self.read(lines)
        self.assertEqual(count, 2)
        self.assertEqual(output, b"".join(lines))

    def test_empty_trace(self):
        self.assertEqual(self.read([b"PWRTRACE BEGIN v=1 retained=0\n", b"PWRTRACE END count=0\n"])[0], 0)

    def test_missing_entry_rejected(self):
        with self.assertRaises(RuntimeError):
            self.read([b"PWRTRACE BEGIN v=1 retained=2\n", b"PWRTRACE 1,RAW,1,0,0,0\n", b"PWRTRACE END count=1\n"])

    def test_previous_dump_tail_ignored_before_new_header(self):
        self.assertEqual(self.read([b"PWRTRACE END count=10\n", b"PWRTRACE BEGIN v=1 retained=0\n", b"PWRTRACE END count=0\n"])[0], 0)

    def test_arbitrarily_split_usb_reads(self):
        data = b"PWRTRACE BEGIN v=1 retained=1\r\nPWRTRACE 5,RAW,1,0,0,0\r\nPWRTRACE END count=1\r\n"
        for size in [1, 2, 7, 64, 4096]:
            count, output = self.read([data[i:i+size] for i in range(0, len(data), size)])
            self.assertEqual(count, 1)
            self.assertEqual(output, data)

    def test_full_2048_entry_trace_in_blocks(self):
        data = b"PWRTRACE BEGIN v=1 retained=2048\n" + b"".join(
            f"PWRTRACE {i},PMIC,0,0x000f,0x12345678,0x00000004\n".encode() for i in range(2048)) + b"PWRTRACE END count=2048\n"
        count, output = self.read([data[i:i+4096] for i in range(0, len(data), 4096)])
        self.assertEqual(count, 2048)
        self.assertEqual(output, data)

    def test_device_error(self):
        with self.assertRaisesRegex(RuntimeError, "allocation failed"):
            self.read([b"PWRTRACE ERROR snapshot allocation failed\n"])

    def test_timeout_keeps_partial_data(self):
        output = io.BytesIO()
        ticks = iter([0, 0, 91])
        with self.assertRaises(TimeoutError):
            reader.read_dump(Port([b"PWRTRACE BEGIN v=1 retained=1\n"]), output, clock=lambda: next(ticks))
        self.assertIn(b"retained=1", output.getvalue())

    def test_cli_sets_control_lines_before_open_and_saves_file(self):
        actions = []

        class Serial(Port):
            def __init__(self, **kwargs):
                self.asserted_port = kwargs["port"]
                super().__init__([b"PWRTRACE BEGIN v=1 retained=0\n", b"PWRTRACE END count=0\n"])

            def __setattr__(self, key, value):
                actions.append((key, value))
                object.__setattr__(self, key, value)

            def __enter__(self):
                return self

            def __exit__(self, *args):
                actions.append(("close", None))

            def open(self):
                actions.append(("open", self.port))

            def write(self, data):
                actions.append(("write", data))

        module = types.SimpleNamespace(Serial=Serial, SerialException=OSError)
        with tempfile.TemporaryDirectory() as root:
            fake_file = str(Path(root) / "tools" / "read_power_trace.py")
            with patch.dict(sys.modules, {"serial": module}), patch.object(reader, "__file__", fake_file), \
                 patch.object(sys, "argv", ["read_power_trace.py", "--port", "COM3"]), \
                 patch.object(reader.time, "sleep"), patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(reader.main(), 0)
            files = list((Path(root) / "logs" / "pwr").glob("*.txt"))
            self.assertEqual(len(files), 1)
            self.assertIn(b"PWRTRACE END count=0", files[0].read_bytes())
        opened = actions.index(("open", "COM3"))
        self.assertLess(actions.index(("dtr", False)), opened)
        self.assertLess(actions.index(("rts", False)), opened)
        self.assertIn(("asserted_port", None), actions)
        self.assertEqual([a for a in actions if a[0] == "write"], [("write", b"\npwr-trace\n")])


if __name__ == "__main__":
    unittest.main()
