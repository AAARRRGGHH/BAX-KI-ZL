"""Read the device's RAM trace without requesting a reset or a firmware build."""
import argparse
from datetime import datetime
from pathlib import Path
import re
import sys
import time


def read_dump(port, output, timeout=90, clock=time.monotonic):
    deadline = clock() + timeout
    expected = None
    received = 0
    pending = bytearray()
    while clock() < deadline:
        # pyserial.read_until reads one byte at a time on Windows. Large traces
        # need block reads; partial lines must survive USB read timeouts.
        chunk = port.read(4096)
        if not chunk:
            continue
        output.write(chunk)
        output.flush()  # Preserve partial results if USB drops again.
        pending.extend(chunk)
        while b"\n" in pending:
            line, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            if b"PWRTRACE ERROR" in line:
                raise RuntimeError(line.decode("utf-8", errors="replace").strip())
            begin = re.search(rb"PWRTRACE BEGIN v=1 .*?retained=(\d+)\b", line)
            if begin:
                if expected is not None:
                    raise RuntimeError("Mehrere Aufzeichnungen vermischt; bitte erneut auslesen.")
                expected = int(begin[1])
            elif re.search(rb"PWRTRACE \d+,[A-Z_]+,", line) and expected is not None:
                received += 1
            elif b"PWRTRACE END" in line and expected is not None:
                end = re.search(rb"PWRTRACE END count=(\d+)\b", line)
                if not end or received != int(end[1]) or received != expected:
                    raise RuntimeError(f"Unvollstaendige Aufzeichnung: {received} von {expected} Eintraegen; Teildaten gespeichert.")
                return received
        if len(pending) > 65536:
            raise RuntimeError("Unerwartet lange Ausgabe ohne Zeilenende; Teildaten gespeichert.")
    detail = "kein Anfang empfangen" if expected is None else f"{received} von {expected} Eintraegen empfangen"
    raise TimeoutError(f"PWRTRACE-Zeitlimit erreicht ({detail}). Teildaten gespeichert.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()
    if not re.fullmatch(r"COM[0-9]+", args.port, flags=re.IGNORECASE) or args.timeout <= 0:
        parser.error("COM-Port (z.B. COM3) und positive Wartezeit erforderlich.")
    import serial  # Use the already installed project-local pyserial.
    folder = Path(__file__).resolve().parents[1] / "logs" / "pwr"
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / ("pwr-trace-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".txt")
    print("RAM-Aufzeichnung auslesen. Kein Flashen, kein angeforderter Reset.")
    print("Datei:", target, flush=True)
    try:
        with target.open("xb") as output:
            with serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2) as port:
                # Set line states before opening. Do not use any reset sequence.
                port.dtr = False
                port.rts = False
                port.port = args.port
                port.open()
                time.sleep(0.3)
                port.write(b"\npwr-trace\n")
                count = read_dump(port, output, timeout=args.timeout)
        print("Fertig:", count, "Eintraege. Diese Datei bitte zur Auswertung bereitstellen.")
        return 0
    except (OSError, RuntimeError, TimeoutError, serial.SerialException) as error:
        print("Auslesen fehlgeschlagen:", error, file=sys.stderr)
        print("Etwaige Teildaten:", target, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
