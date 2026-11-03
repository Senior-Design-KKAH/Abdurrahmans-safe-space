"""Small checks using the firmware codec and a real local HTTP/SQLite receiver."""
from pathlib import Path
import hashlib
import http.client
import os
import re
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import receiver


def packet(bits=0x3f5f9db2, measurement=41, attempt=0, batch=1):
    header = struct.pack('<BBBBIIIIBBBB', 0xa7, 5, 3, 1, 0x12345678,
                         0xaabbccdd, batch, measurement, 0, 1, attempt, 6)
    raw = header + bytes([1, 255]) + struct.pack('<I', bits)
    return raw + struct.pack('<I', zlib.crc32(raw))


def repaired(raw):
    return bytes(raw[:-4]) + struct.pack('<I', zlib.crc32(raw[:-4]))


class Checks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.db = Path(cls.tmp.name) / 'results.sqlite3'
        cls.server = receiver.make_server('127.0.0.1', 0, cls.db)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join()
        cls.server.server_close()
        cls.tmp.cleanup()

    def post(self, raw, path='/results', content_type='application/octet-stream'):
        conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=5)
        conn.request('POST', path, raw, {'Content-Type': content_type})
        response = conn.getresponse()
        status, body = response.status, response.read(256)
        conn.close()
        return status, body

    def test_cpp_codec_and_model(self):
        executable = str(Path(self.tmp.name) / 'packet_check')
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra',
                        '-I', str(ROOT / 'main'), str(ROOT / 'test/packet_check.cpp'), '-o', executable], check=True)
        output = subprocess.check_output([executable], text=True)
        self.assertEqual(bytes.fromhex(output.splitlines()[0]), packet())
        print(output.splitlines()[1], flush=True)
        for node in (0, 1):
            api = str(Path(self.tmp.name) / f'api_check_{node}')
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-Wall', '-Wextra',
                            '-pthread', '-DRPW_HOST_TEST', f'-DNODE_ID={node}',
                            '-I', str(ROOT / 'main'), str(ROOT / 'test/api_check.cpp'),
                            str(ROOT / 'main/communication_api.cpp'), '-o', api], check=True)
            subprocess.run([api], check=True)
        model = (ROOT / 'main/generated/model_data.cc').read_text()
        blob = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-fA-F]{2})', model))
        self.assertEqual(len(blob), 35344)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), '22c373cfc93af09fd3fc5c84f384ea273f7c815dd08ab7100b74be5e238a5a46')
        clip = (ROOT / 'main/generated/clip.cc').read_text().split('{', 1)[1].split('}', 1)[0]
        self.assertEqual(len(re.findall(r'-?\d+', clip)), 80000)

    def test_http_commit_duplicate_conflict_and_bits(self):
        raw = packet(measurement=100)
        self.assertEqual(self.post(raw), (201, raw))
        # Simulates commit followed by lost reply, then changed batch/retry metadata.
        retry = packet(measurement=100, attempt=2, batch=17)
        self.assertEqual(self.post(retry), (200, retry))
        self.assertEqual(self.post(packet(bits=0x3e800000, measurement=100))[0], 409)
        with sqlite3.connect(self.db) as db:
            rows = db.execute('SELECT probability_bits,application,raw_packet,ingested_at FROM results WHERE measurement_id=100').fetchall()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][:3], (0x3f5f9db2, raw[24:30], raw))
        self.assertTrue(rows[0][3].endswith('+00:00'))
        for i, bits in enumerate((0, 0x80000000, 1, 0x3f800000)):
            raw = packet(bits=bits, measurement=200+i)
            self.assertEqual(self.post(raw), (201, raw))
            self.assertEqual(receiver.decode(raw)[6], bits)

    def test_invalid_input(self):
        raw = packet()
        self.assertEqual(self.post(raw[:-1])[0], 400)
        self.assertEqual(self.post(raw+b'\0')[0], 400)
        self.assertEqual(self.post(raw, path='/bad')[0], 404)
        self.assertEqual(self.post(raw, content_type='application/json')[0], 415)
        broken = bytearray(raw); broken[27] ^= 1
        self.assertEqual(self.post(broken)[0], 400)
        for offset, value in ((0, 0), (1, 4), (2, 4), (3, 0), (20, 1), (21, 0), (21, 9), (22, 5), (23, 7), (24, 2), (25, 7)):
            broken = bytearray(raw); broken[offset] = value
            self.assertEqual(self.post(repaired(broken))[0], 400, (offset, value))
        for offset in (4, 8, 12, 16):
            broken = bytearray(raw); broken[offset:offset+4] = b'\0'*4
            self.assertEqual(self.post(repaired(broken))[0], 400, offset)
        for bits in (0x7fc00000, 0x7f800000, 0xbf800000, 0x3f800001):
            self.assertEqual(self.post(packet(bits=bits))[0], 400)

    def test_storage_failure(self):
        path = self.server.database
        self.server.database = str(Path(self.tmp.name) / 'missing' / 'db.sqlite3')
        try:
            self.assertEqual(self.post(packet(measurement=500))[0], 503)
        finally:
            self.server.database = path


if __name__ == '__main__':
    unittest.main(verbosity=2)
