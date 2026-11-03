from pathlib import Path
import sys

import numpy as np
from scipy.fft import dct
from scipy.signal import sosfilt_zi

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'mfcc'))
import mfcc_processor as reference


# Export the existing reference constants as reproducible flash-resident C++ tables.
def main():
    sos, window, mel = reference.configuration()
    # Each row maps 26 log Mel energies to one orthonormal MFCC coefficient.
    dct_matrix = dct(np.eye(26), type=2, norm='ortho', axis=0)[:13]
    tables = {'SOS': sos, 'ZI': sosfilt_zi(sos), 'WINDOW': window,
              'MEL': mel, 'DCT': dct_matrix,
              'COS': np.cos(2 * np.pi * np.arange(128) / 256),
              'SIN': -np.sin(2 * np.pi * np.arange(128) / 256)}
    lines = ['#pragma once', '// Generated from the existing Python reference; values stay in flash.']
    for name, array in tables.items():
        shape = ''.join(f'[{n}]' for n in array.shape)
        rows = array if array.ndim == 2 else array[None]
        values = ['{' + ','.join(format(float(x), '.17g') for x in row) + '}' for row in rows]
        body = ',\n'.join(values) if array.ndim == 2 else values[0][1:-1]
        lines.append(f'static const double {name}{shape} = {{\n{body}\n}};')
    output = Path(__file__).resolve().parents[1] / 'include/dsp_tables.h'
    output.write_text('\n'.join(lines) + '\n')
    print(output)


if __name__ == '__main__':
    main()
