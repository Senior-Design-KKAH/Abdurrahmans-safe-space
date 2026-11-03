"""Crop one manifest-approved development PCM WAV without changing its gain."""
import argparse
import csv
from pathlib import Path
import struct
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav', type=Path)
    parser.add_argument('--splits', type=Path, required=True)
    parser.add_argument('--manifest-path', required=True)
    args = parser.parse_args()
    with args.splits.open(newline='') as f:
        rows = [r for r in csv.DictReader(f) if r['path'] == args.manifest_path]
    if len(rows) != 1 or rows[0]['split'] not in ('train', 'val'):
        parser.error('Source must be uniquely assigned train or val; audio was not opened')
    # Check identity before opening audio, including paths containing a folder named test.
    if not args.wav.as_posix().endswith('/' + args.manifest_path):
        parser.error('WAV path must end with the exact manifest path')
    with wave.open(str(args.wav), 'rb') as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) != (8000, 1, 2, 'NONE'):
            parser.error('Requires uncompressed 8 kHz mono signed-int16 PCM')
        pcm = wav.readframes(80000)
    if len(pcm) != 160000:
        parser.error('Requires at least ten seconds')
    out = Path(__file__).resolve().parent / 'main/generated'
    (out / 'clip.h').write_text('#pragma once\n#include <stdint.h>\nextern const int16_t kClipPcm[80000];\n')
    values = struct.unpack('<80000h', pcm)
    with (out / 'clip.cc').open('w') as f:
        f.write(f'// {rows[0]["split"]}: {args.manifest_path}; samples [0,80000), gain unchanged.\n')
        f.write('#include "clip.h"\nalignas(4) const int16_t kClipPcm[80000] = {\n')
        for i in range(0, len(values), 20):
            f.write('    ' + ','.join(map(str, values[i:i+20])) + ',\n')
        f.write('};\nstatic_assert(sizeof(kClipPcm) == 160000, "Ten seconds in flash");\n')
    print(f'Embedded {len(values)} samples from {rows[0]["split"]}, first ten seconds, unchanged PCM')


if __name__ == '__main__':
    main()
