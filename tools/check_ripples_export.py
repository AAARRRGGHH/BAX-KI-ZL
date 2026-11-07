"""Independent stdlib SMF parser: compare physical MIDI bytes with the CSV log."""
import csv
import json
import struct
import sys
from pathlib import Path

def parse(path):
    data = path.read_bytes()
    assert data[:4] == b'MThd' and struct.unpack('>IHHH', data[4:14]) == (6, 0, 1, 960)
    assert data[14:18] == b'MTrk'
    length = int.from_bytes(data[18:22], 'big')
    assert len(data) == 22 + length
    i, tick, tempo, held, events = 22, 0, None, set(), []
    def vlq():
        nonlocal i
        result = 0
        for _ in range(4):
            x = data[i]; i += 1; result = (result << 7) | (x & 127)
            if not x & 128: return result
        raise AssertionError('invalid VLQ')
    ended = False
    while i < len(data):
        tick += vlq(); status = data[i]; i += 1
        if status == 255:
            kind = data[i]; i += 1; size = vlq(); payload = data[i:i+size]; i += size
            if kind == 81:
                assert size == 3; tempo = int.from_bytes(payload, 'big')
            if kind == 47:
                assert size == 0 and i == len(data); ended = True
        else:
            assert status in (128, 144, 176)
            note, velocity = data[i:i+2]; i += 2
            assert note <= 127 and velocity <= 127
            if status == 144:
                assert velocity > 0 and note not in held; held.add(note)
            elif status == 128:
                assert note in held; held.remove(note)
            else:
                assert note == 123; held.clear()
            events.append((tick, status, note, velocity))
    assert ended and not held and tempo is not None
    return events, tick, tempo

def main():
    folder = Path(sys.argv[1])
    for stem in ('A-ohne-kopplung', 'B-mit-kopplung'):
        report = json.loads((folder / (stem+'.json')).read_text())
        actual, end, tempo = parse(folder / (stem+'.mid'))
        assert tempo == 60000000 // report['bpm']
        expected = []
        for row in csv.DictReader((folder / (stem+'-events.csv')).open()):
            status = {'on': 144, 'off': 128, 'panic': 176}[row['event']]
            tick = int(int(row['time_us']) * report['bpm'] * 960 / 60000000 + .5)
            expected.append((tick, status, 123 if status == 176 else int(row['note']), int(row['velocity'])))
        assert sorted(actual) == sorted(expected)
        assert end >= round(report['duration_seconds'] * report['bpm'] * 16)
        assert report['metrics']['notes'] == report['metrics']['offs']
        for note in report['notes']:
            assert 0 <= note['on_us'] < note['off_us'] <= report['duration_seconds'] * 1000000
    print('RIPPLES SMF/CSV: PASS')

if __name__ == '__main__':
    main()
