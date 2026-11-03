import argparse
from collections import Counter
import ctypes as ct
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

import numpy as np
import soundfile as sf

from decoder import BANK, decode_payload, encode_payload

SENSING = Path(__file__).resolve().parents[1]
ROOT = SENSING.parent


# Compile the actual firmware DSP and encoder for direct native testing through ctypes.
def build_native():
    output = SENSING / 'results/libpipeline.so'
    subprocess.run(['g++', '-std=c++11', '-O2', '-fno-fast-math', '-ffp-contract=off', '-shared', '-fPIC',
                    '-I'+str(SENSING/'include'), str(SENSING/'src/dsp.cpp'), str(SENSING/'src/payload.cpp'), '-o', str(output)], check=True)
    lib = ct.CDLL(str(output))
    lib.make_payload.argtypes = [ct.c_void_p, ct.c_float, ct.c_uint32, ct.c_uint32, ct.c_uint32, ct.c_void_p, ct.c_size_t, ct.POINTER(ct.c_size_t), ct.POINTER(ct.c_uint8)]
    lib.make_payload.restype = ct.c_int
    lib.quantize_features.argtypes = [ct.c_void_p, ct.c_void_p, ct.POINTER(ct.c_float)]
    lib.dsp_workspace_bytes.restype = ct.c_size_t
    lib.compute_mfcc.argtypes = [ct.c_void_p, ct.c_double, ct.c_void_p, ct.c_void_p, ct.c_void_p, ct.POINTER(ct.c_float), ct.c_void_p, ct.c_void_p]
    return lib


# Call the C++ encoder and return its status, bytes, required size, and schedule.
def native_packet(lib, matrix, scale, codes=(42,11,7), capacity=255):
    matrix = np.ascontiguousarray(matrix, dtype=np.int8)
    storage = (ct.c_uint8*255)()
    needed, schedule = ct.c_size_t(), ct.c_uint8()
    status = lib.make_payload(matrix.ctypes.data, scale, *codes, storage, capacity, ct.byref(needed), ct.byref(schedule))
    return status, bytes(storage[:min(needed.value,255)]), needed.value, schedule.value


# Make malformed-packet tests fail if the decoder accidentally accepts their input.
def expect_rejection(payload):
    try:
        decode_payload(payload)
    except (ValueError, OverflowError):
        return
    raise AssertionError('Malformed payload accepted')


# Exercise packet layout, signed boundaries, rounding, errors, and all eight schedules.
def test_generated(lib):
    zero = np.zeros((15,13), dtype=np.int8)
    # Hand-derived: header, 107 zero bits, 182 unary one bits, seven zero pad bits.
    golden = bytes.fromhex('2a5f3f800000') + bytes(13) + b'\x1f' + bytes([255])*22 + b'\x80'
    assert len(golden) == 43
    status, packet, _, selected = native_packet(lib, zero, 1)
    assert status == 0 and packet == golden == encode_payload(zero, 1, 42,11,7) and selected == 0
    assert decode_payload(golden)['padding_bits'] == 7
    for end in range(len(golden)):
        expect_rejection(golden[:end])
    for bad in (golden+b'\x00', golden[:-1]+b'\x81', golden[:6]+bytes(len(golden)-6)):
        expect_rejection(bad)
    for scale in (0., -1., float('nan'), float('inf')):
        expect_rejection(golden[:2]+struct.pack('>f',scale)+golden[6:])
        assert native_packet(lib, zero, scale)[0] == 2
    # Exercise unsigned field limits and values whose old four-bit decoding would differ.
    header_cases = [(0,0,0), (255,31,7), (42,31,0), (42,0,7), (42,16,0), (42,15,7), (128,30,6)]
    for codes in header_cases:
        status, packed, _, _ = native_packet(lib,zero,1,codes)
        assert status == 0 and packed[:2] == bytes([codes[0],(codes[1]<<3)|codes[2]])
        assert packed == encode_payload(zero,1,*codes)
        fields = decode_payload(packed)
        assert (fields['node_id'],fields['date_code'],fields['battery_code']) == codes
    invalid_codes = [(-1,0,0),(256,0,0),(0,-1,0),(0,32,0),(0,0,-1),(0,0,8)]
    for codes in invalid_codes:
        assert native_packet(lib,zero,1,codes)[0] == 3
        try:
            encode_payload(zero,1,*codes)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Out-of-range unsigned header accepted: {codes}')
    for codes in [(1.5,0,0),(0,1.5,0),(0,0,1.5),('1',0,0),(2**32,0,0)]:
        try:
            encode_payload(zero,1,*codes)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Invalid integer header accepted: {codes}')
    assert native_packet(lib, zero, 1, capacity=1)[0] == 4
    # A single changing column keeps extreme temporal deltas within the radio budget.
    for minimum in (-127, -128):
        single = np.zeros((15,13),dtype=np.int8)
        single[:,0] = np.where(np.arange(15)%2,127,minimum)
        status, packet, _, _ = native_packet(lib,single,1)
        assert status == 0
        np.testing.assert_array_equal(decode_payload(packet)['mfcc_q'],single)
    scale = ct.c_float()
    q = np.empty((15,13), dtype=np.int8)
    features = np.zeros((15,13), dtype=np.float64)
    features[0,:7] = [-127, -2.5, -1.5, .5, 1.5, 2.5, 127]
    assert lib.quantize_features(features.ctypes.data, q.ctypes.data, ct.byref(scale)) == 0
    np.testing.assert_array_equal(q[0,:7], [-127,-2,-2,0,2,2,127])
    assert scale.value == 1
    features.fill(0)
    assert lib.quantize_features(features.ctypes.data, q.ctypes.data, ct.byref(scale)) == 0
    assert scale.value == 1 and not q.any()
    features[0,0] = np.nan
    assert lib.quantize_features(features.ctypes.data, q.ctypes.data, ct.byref(scale)) == 1
    extreme = np.full((15,13), -127, dtype=np.int8)
    extreme[1::2] = 127
    status, _, required, _ = native_packet(lib, extreme, 1)
    assert status == 5 and required > 255
    try:
        encode_payload(extreme, 1, 42,11,7)
    except ValueError:
        pass
    else:
        raise AssertionError('Oversize packet not rejected')
    vectors = [{'name': 'hand_checked_zero', 'hex': golden.hex(), 'scale_hex': '3f800000', 'schedule': 0, 'mfcc_q': zero.tolist()}]
    rng = np.random.default_rng(20261102)
    found = {0}
    # Seeded walks target different coefficient ranges to exercise every selected index.
    for target, bank in enumerate(BANK):
        for attempt in range(3000):
            means = np.asarray([.12 if k == 0 else .8*(2**(k-1)) for k in bank])
            deltas = (rng.geometric(1/(means+1), size=(14,13))-1) * rng.choice([-1,1], size=(14,13))
            rows = np.vstack([np.zeros(13,dtype=int), np.clip(np.cumsum(deltas,axis=0),-127,127)]).astype(np.int8)
            status, packet, _, chosen = native_packet(lib, rows, np.float32(.12345))
            if status != 0:
                continue
            expected = encode_payload(rows, np.float32(.12345),42,11,7)
            assert packet == expected
            decoded = decode_payload(packet)
            np.testing.assert_array_equal(decoded['mfcc_q'], rows)
            if chosen not in found:
                found.add(chosen)
                vectors.append({'name': f'generated_schedule_{chosen}', 'hex': packet.hex(), 'scale_hex': packet[2:6].hex(), 'schedule': chosen, 'mfcc_q': rows.tolist()})
            if target in found: break
    assert found == set(range(8)), f'Generated patterns did not exercise schedules {set(range(8))-found}'
    constant = np.tile(np.asarray([-128,-127,-64,-1,0,1,64,127,-2,2,-3,3,4],dtype=np.int8), (15,1))
    status, packet, _, _ = native_packet(lib, constant, 1)
    assert status == 0
    np.testing.assert_array_equal(decode_payload(packet)['mfcc_q'], constant)
    vectors.append({'name': 'signed_first_frame', 'hex': packet.hex(), 'scale_hex': packet[2:6].hex(), 'mfcc_q': constant.tolist()})
    (SENSING/'results/golden_vectors.json').write_text(json.dumps(vectors,indent=2)+'\n')
    return {'generated_schedule_indices': sorted(found), 'oversize_alternating_127_required_bytes': required,
            'zero_payload_bytes': len(golden), 'golden_vectors': len(vectors), 'truncation_cuts_checked': len(golden),
            'unsigned_header_valid_cases':len(header_cases), 'unsigned_header_invalid_cases':len(invalid_codes)+5}


# Compare identical PCM through the shared C++ DSP and the existing Python reference.
def test_dsp(lib):
    sys.path.insert(0, str(ROOT/'mfcc'))
    import mfcc_processor as reference
    rng = np.random.default_rng(62)
    samples = np.arange(80000)
    signals = [
        ('silence', np.zeros(80000,dtype=np.int16),32768.),
        ('500Hz', np.rint(6553*np.sin(2*np.pi*500*samples/8000)).astype(np.int16),32768.),
        ('biased_adc_730Hz', np.rint(2048+500*np.sin(2*np.pi*730*samples/8000)).astype(np.int16),2048.),
        ('seeded_noise', rng.integers(-12000,12001,size=80000,dtype=np.int16),32768.),
    ]
    impulse = np.zeros(80000,dtype=np.int16)
    impulse[[0,250,40000,79999]] = [15000,-15000,12000,-10000]
    signals.append(('edge_and_center_impulses',impulse,32768.))
    for source in ('lab','field'):
        with np.load(ROOT/f'mfcc/outputs/{source}_mfcc_quantized.npz', allow_pickle=False) as data:
            paths = data['path']
            for index in (0,len(paths)//2):
                audio, rate = sf.read(ROOT/str(paths[index]),dtype='float64',always_2d=True)
                clip = reference.crop_and_resample(audio,rate)
                if len(clip) != 80000:
                    continue
                pcm = np.clip(np.rint(clip*32768),-32768,32767).astype(np.int16)
                signals.append((str(paths[index]),pcm,32768.))
    # Double alignment and the exported size keep the native workspace ABI correct.
    workspace = (ct.c_double*((lib.dsp_workspace_bytes()+7)//8))()
    rows = []
    for name, pcm, divisor in signals:
        pointers = (ct.c_void_p*4)(*[pcm[i*20000:(i+1)*20000].ctypes.data for i in range(4)])
        features = np.empty((15,13),dtype=np.float64)
        quantized = np.empty((15,13),dtype=np.int8)
        filtered = np.empty(80000,dtype=np.float64)
        scale = ct.c_float()
        start = time.perf_counter()
        status = lib.compute_mfcc(pointers,divisor,workspace,features.ctypes.data,quantized.ctypes.data,ct.byref(scale),filtered.ctypes.data,None)
        elapsed = time.perf_counter()-start
        assert status == 0, (name,status)
        normalized = (pcm.astype(np.float64)-pcm.mean())/divisor
        ref_filtered = reference.bandpass(normalized)
        ref_features = reference.extract_mfcc(ref_filtered)
        ref_q, ref_scale = reference.quantize(ref_features)
        wave_error = float(np.max(np.abs(filtered-ref_filtered)))
        mfcc_error = float(np.max(np.abs(features-ref_features)))
        difference_count = int(np.count_nonzero(quantized != ref_q))
        assert wave_error <= 1e-10 and mfcc_error <= 1e-7, (name,wave_error,mfcc_error)
        assert difference_count == 0 and struct.pack('>f',scale.value) == struct.pack('>f',ref_scale), (name,difference_count)
        status, packet, _, _ = native_packet(lib,quantized,scale.value)
        assert status == 0 and packet == encode_payload(ref_q,ref_scale,42,11,7)
        rows.append({'name':name,'filtered_max_abs_error':wave_error,'mfcc_max_abs_error':mfcc_error,
                     'quantized_mismatches':difference_count,'scale_abs_error':abs(scale.value-float(ref_scale)),
                     'payload_equal':True,'native_dsp_seconds':elapsed})
        print('DSP', rows[-1],flush=True)
    return {'tolerances':{'filtered_max_abs':1e-10,'mfcc_max_abs':1e-7,'quantized_mismatches':0,'scale_bits_equal':True,'packet_bits_equal':True},
            'workspace_bytes':int(lib.dsp_workspace_bytes()), 'vectors':rows,
            'scope':'Native build of identical C++ firmware DSP, not execution on an ESP32. Embedded libm and physical ADC still require bench verification.'}


# Round-trip stored recordings and summarize actual complete application payload sizes.
def test_datasets(lib, limit):
    reports = {}
    example = None
    for source in ('lab','field'):
        file = ROOT/f'mfcc/outputs/{source}_mfcc_quantized.npz'
        # Input hashes confirm that verification never alters the original NPZ archive.
        before = hashlib.sha256(file.read_bytes()).hexdigest()
        with np.load(file,allow_pickle=False) as data:
            q,scales,paths = data['mfcc_q'],data['scale'],data['path']
        assert q.dtype == np.int8 and q.shape[1:] == (15,13) and scales.dtype == np.float32 and paths.dtype.kind == 'U'
        sizes, failures, counts = [], [], Counter()
        count = len(q) if limit is None else min(limit,len(q))
        for i in range(count):
            status,packet,needed,chosen = native_packet(lib,q[i],scales[i])
            if status == 5:
                failures.append({'path':str(paths[i]),'required_bytes':needed,'reason':'exceeds 255-byte radio limit'})
                continue
            assert status == 0, (paths[i],status)
            decoded = decode_payload(packet)
            np.testing.assert_array_equal(decoded['mfcc_q'],q[i])
            assert packet[2:6] == struct.pack('>f',scales[i])
            assert decoded['node_id']==42 and decoded['date_code']==11 and decoded['battery_code']==7 and decoded['schedule']==chosen
            assert packet == encode_payload(q[i],scales[i],42,11,7)
            sizes.append(needed); counts[chosen] += 1
            if example is None and source == 'field':
                example = {'source_path':str(paths[i]),'synthetic_header_codes':True,'hex':packet.hex(),'decoded':decoded}
        assert hashlib.sha256(file.read_bytes()).hexdigest() == before
        reports[source] = {'available':len(q),'tested':count,'success':len(sizes),'failures':failures,
                           'mean_bytes':float(np.mean(sizes)),'median_bytes':float(np.quantile(sizes,.5,method='inverted_cdf')),
                           'p95_bytes':int(np.quantile(sizes,.95,method='inverted_cdf')),'p99_bytes':int(np.quantile(sizes,.99,method='inverted_cdf')),
                           'max_bytes':max(sizes),'schedule_counts':dict(sorted(counts.items())),'input_sha256':before}
        print('DATASET',source,reports[source],flush=True)
    (SENSING/'results/example_payload.json').write_text(json.dumps(example,indent=2)+'\n')
    (SENSING/'results/example_payload.bin').write_bytes(bytes.fromhex(example['hex']))
    return reports


# Run the checks and save their measured results separately from physical bench claims.
def main():
    parser = argparse.ArgumentParser(description='Build/test shared firmware core, decode all original MFCC rows, and compare DSP to Python.')
    parser.add_argument('--limit',type=int,help='Optional per-source subset; default: all 58,797 matrices')
    args = parser.parse_args()
    (SENSING/'results').mkdir(exist_ok=True)
    lib = build_native()
    generated = test_generated(lib)
    dsp = test_dsp(lib)
    datasets = test_datasets(lib,args.limit)
    summary = {'generated':generated,'dsp':dsp,'datasets':datasets,'physical_hardware_tested':False,'rf_transmitted':False,
               'format':'6-byte fixed header (unsigned node8/date5/battery3, float32 scale) + 3-bit index + 104-bit first frame + 182 Rice deltas + terminal zero padding; no fallback'}
    (SENSING/'results/verification.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('PASS: generated boundary/layout tests, DSP comparisons and complete packet roundtrips.',flush=True)


if __name__ == '__main__':
    main()
