#!/usr/bin/env python3
"""Trusted-LAN RPW v5 binary receiver. Python standard library only."""
import argparse
from contextlib import closing
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
import math
import socket
import sqlite3
import struct
import zlib

PACKET_BYTES = 34
HEADER = struct.Struct('<BBBBIIIIBBBB')


def decode(packet):
    if len(packet) != PACKET_BYTES:
        raise ValueError('expected exactly 34 bytes')
    magic, version, kind, node, run, token, batch, measurement, index, count, attempt, length = HEADER.unpack_from(packet)
    if (magic, version, kind, length) != (0xA7, 5, 3, 6):
        raise ValueError('expected RPW v5 DATA with six application bytes')
    if node not in (1, 2) or not run or not token or not batch or not measurement:
        raise ValueError('invalid transport identity')
    if not 1 <= count <= 8 or index >= count or attempt >= 5:
        raise ValueError('invalid batch index/count/attempt')
    if struct.unpack_from('<I', packet, 30)[0] != zlib.crc32(packet[:30]):
        raise ValueError('CRC32 mismatch')
    app = packet[24:30]
    day, battery = app[1] >> 3, app[1] & 7
    probability = struct.unpack_from('<f', app, 2)[0]
    bits = struct.unpack_from('<I', app, 2)[0]
    if app[0] != node or not 1 <= day <= 31:
        raise ValueError('invalid application node/day')
    if not math.isfinite(probability) or not 0 <= probability <= 1:
        raise ValueError('invalid probability')
    return node, token, measurement, day, battery, probability, bits, app


def initialize_database(path):
    with closing(sqlite3.connect(path, timeout=2)) as db:
        db.execute('''CREATE TABLE IF NOT EXISTS results (
            id INTEGER PRIMARY KEY,
            node_id INTEGER NOT NULL,
            sender_boot_token INTEGER NOT NULL,
            measurement_id INTEGER NOT NULL,
            day INTEGER NOT NULL,
            battery_code INTEGER NOT NULL,
            probability REAL NOT NULL,
            probability_bits INTEGER NOT NULL,
            application BLOB NOT NULL,
            raw_packet BLOB NOT NULL,
            ingested_at TEXT NOT NULL,
            UNIQUE(node_id, sender_boot_token, measurement_id)
        )''')
        db.commit()


def store(path, packet, values):
    node, token, measurement, day, battery, probability, bits, app = values
    now = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
    with closing(sqlite3.connect(path, timeout=2)) as db:
        cursor = db.execute('''INSERT INTO results
            (node_id,sender_boot_token,measurement_id,day,battery_code,probability,
             probability_bits,application,raw_packet,ingested_at)
            VALUES (?,?,?,?,?,?,?,?,?,?)
            ON CONFLICT(node_id,sender_boot_token,measurement_id) DO NOTHING''',
            (node, token, measurement, day, battery, probability, bits, app, packet, now))
        if cursor.rowcount == 1:
            status = 201
        else:
            original = db.execute('''SELECT application FROM results WHERE
                node_id=? AND sender_boot_token=? AND measurement_id=?''',
                (node, token, measurement)).fetchone()
            status = 200 if original and original[0] == app else 409
        # A successful echo may only be sent after commit returns successfully.
        db.commit()
    return status


class Receiver(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    timeout = 3

    def setup(self):
        super().setup()
        self.connection.settimeout(self.timeout)

    def reply(self, status, body, binary=False):
        self.send_response(status)
        self.send_header('Content-Type', 'application/octet-stream' if binary else 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        self.close_connection = True
        try:
            self.end_headers()
            self.wfile.write(body)
        except (OSError, socket.timeout):
            pass  # A committed request can safely be retried.

    def send_error(self, code, message=None, explain=None):
        self.reply(code, f'HTTP {code}\n'.encode())

    def do_POST(self):
        if self.path != '/results':
            self.reply(404, b'POST /results required\n')
            return
        if self.headers.get_all('Transfer-Encoding'):
            self.reply(400, b'Transfer-Encoding is not supported\n')
            return
        if self.headers.get_all('Content-Type') != ['application/octet-stream']:
            self.reply(415, b'Content-Type must be application/octet-stream\n')
            return
        if self.headers.get_all('Content-Length') != ['34']:
            self.reply(400, b'Content-Length must be exactly 34\n')
            return
        try:
            packet = self.rfile.read(PACKET_BYTES)
        except (OSError, socket.timeout):
            self.reply(408, b'packet read timed out\n')
            return
        try:
            values = decode(packet)
        except ValueError as error:
            self.reply(400, (str(error) + '\n').encode())
            return
        try:
            status = store(self.server.database, packet, values)
        except sqlite3.Error as error:
            print(f'STORAGE FAILURE: {error}', flush=True)
            self.reply(503, b'SQLite storage failed; retry later\n')
            return
        node, token, measurement, day, battery, probability, bits, _ = values
        outcome = {201: 'STORED', 200: 'DUPLICATE', 409: 'IDENTITY CONFLICT'}[status]
        print(f'{outcome} node={node} event=({node},{token:08x},{measurement}) '
              f'day={day} bench_battery_code={battery}/7 probability={probability:.9f} bits={bits:08x}', flush=True)
        self.reply(status, packet if status in (200, 201) else b'identity already carries different application bytes\n',
                   binary=status in (200, 201))

    def log_message(self, format, *args):
        # Results and errors above carry the useful diagnostics.
        pass


def make_server(host, port, database):
    initialize_database(database)
    server = HTTPServer((host, port), Receiver)
    server.database = str(database)
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--db', default='results.sqlite3')
    args = parser.parse_args()
    with make_server(args.bind, args.port, args.db) as server:
        print(f'Listening http://{args.bind}:{server.server_port}/results db={args.db}', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
