import argparse
import json
import math
import operator
from pathlib import Path
import struct

BANK = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0], [1,0,0,0,0,0,0,0,0,0,0,0,0],
    [1,1,0,0,0,0,0,0,0,0,0,0,0], [2,1,0,0,0,0,0,0,0,0,0,0,0],
    [2,1,1,1,0,0,0,0,0,0,0,0,0], [2,2,1,1,1,1,1,1,0,0,0,0,0],
    [3,2,2,1,1,1,1,1,1,1,1,0,0], [4,1,1,1,1,1,0,0,0,0,0,0,0]
]


# Build an independent reference packet; a forced schedule is useful for test vectors.
def encode_payload(matrix, scale, node, date, battery, schedule=None):
    rows = [[int(v) for v in row] for row in matrix]
    if len(rows) != 15 or any(len(row) != 13 for row in rows) or any(not -128 <= v <= 127 for row in rows for v in row):
        raise ValueError('Expected 15x13 signed int8 matrix')
    # Python has no unsigned int type, so require integer inputs and validate their ranges.
    try:
        node, date, battery = (operator.index(v) for v in (node, date, battery))
    except TypeError:
        raise ValueError('Header codes must be unsigned integers') from None
    if not (0 <= node <= 255 and 0 <= date <= 31 and 0 <= battery <= 7):
        raise ValueError('Invalid header codes')
    scale_bytes = struct.pack('>f', scale)
    stored_scale = struct.unpack('>f', scale_bytes)[0]
    if not math.isfinite(stored_scale) or stored_scale <= 0:
        raise ValueError('Scale must be positive finite float32')
    values = [[2*d if d >= 0 else -2*d-1 for d in [rows[t][c]-rows[t-1][c] for c in range(13)]] for t in range(1,15)]
    costs = [sum((u >> bank[c])+1+bank[c] for row in values for c,u in enumerate(row)) for bank in BANK]
    if schedule is None:
        schedule = min(range(8), key=lambda b: ((107+costs[b]+7)//8, 107+costs[b], b))
    if not 0 <= schedule < 8:
        raise ValueError('Invalid schedule')
    # The stream starts with three index bits and 104 raw first-frame bits.
    bits = 107 + costs[schedule]
    length = 6 + (bits+7)//8
    if length > 255:
        raise ValueError(f'Payload requires {length} bytes, exceeds 255; no raw fallback')
    stream = schedule
    for value in rows[0]: stream = (stream << 8) | (value & 255)
    for row in values:
        for c,u in enumerate(row):
            k = BANK[schedule][c]
            stream = (stream << ((u >> k)+1+k)) | (1 << k) | (u & ((1<<k)-1))
    return bytes([node, (date<<3)|battery]) + scale_bytes + (stream << ((-bits)%8)).to_bytes(length-6, 'big')


# Recover the header and all 195 signed coefficients, rejecting malformed streams.
def decode_payload(packet):
    if not 7 <= len(packet) <= 255:
        raise ValueError('Invalid application payload length')
    scale = struct.unpack('>f', packet[2:6])[0]
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError('Invalid scale')
    value = int.from_bytes(packet[6:], 'big')
    remaining = 8*(len(packet)-6)

    # Consume one MSB-first field while bounding every access to the packet.
    def read(count):
        nonlocal value, remaining
        if count < 0 or count > remaining:
            raise ValueError('Truncated bitstream')
        remaining -= count
        result = value >> remaining
        value &= (1<<remaining)-1
        return result

    schedule = read(3)
    first = [read(8) for _ in range(13)]
    matrix = [[v if v < 128 else v-256 for v in first]]
    for t in range(1,15):
        row = []
        for c,k in enumerate(BANK[schedule]):
            # Leading zero count gives the unary quotient without an unbounded scan.
            zeros = remaining-value.bit_length()
            if zeros >= remaining or zeros > 510:
                raise ValueError('Invalid unary code')
            read(zeros+1)
            u = (zeros << k) | read(k)
            if u > 510:
                raise ValueError('Temporal delta exceeds int8 range')
            d = u//2 if u%2 == 0 else -(u//2)-1
            current = matrix[t-1][c]+d
            if not -128 <= current <= 127:
                raise ValueError('Decoded value outside int8 range')
            row.append(current)
        matrix.append(row)
    # Only a partial final byte of zero padding may remain after all deltas.
    if remaining >= 8 or value != 0:
        raise ValueError('Unexpected trailing bytes or nonzero terminal padding')
    return {'node_id': packet[0], 'date_code': packet[1]>>3, 'battery_code': packet[1]&7,
            'scale': scale, 'scale_hex': packet[2:6].hex(), 'schedule': schedule, 'mfcc_q': matrix,
            'payload_bytes': len(packet), 'padding_bits': remaining}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Decode the fixed 15x13 sensing application payload.')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--hex')
    group.add_argument('--file', type=Path, help='Binary payload, without a radio/serial envelope')
    args = parser.parse_args()
    packet = bytes.fromhex(args.hex) if args.hex else args.file.read_bytes()
    print(json.dumps(decode_payload(packet), indent=2))
