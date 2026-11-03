# ESP32 recording comparison — 3 November 2026

One ten-second crop was physically evaluated on the connected ESP32 for each of seven development folders. `difficult_cases` was excluded. Folders 29 and 32 remain unopened because they are sealed test data and permission to expose their scores is pending.

Selection: alphabetically first available recording listed in the split manifest with at least ten seconds. Crop `[0,80000)`, 8 kHz mono signed-int16 PCM, unchanged gain. One clip was embedded/flashed at a time; ten distinct one-second windows were inferred and averaged with float32 accumulation. The model/frontend and weights were fixed throughout.

Model: `stream1s_w8_d4`, SHA-256 `22c373cfc93af09fd3fc5c84f384ea273f7c815dd08ab7100b74be5e238a5a46`. Explicit ML-only firmware bypassed the nonresponding SX1278. No LoRa/head/Wi-Fi test was performed.

| Folder | Split | Mean score | One-second score range | Window SD (percentage points) | Workbook AUDIO | CONFIRMATION | VISUAL |
|---|---|---:|---:|---:|---:|---|---|
| folder_26 | val | 98.91% | 98.44–99.22% | 0.23 | 1 | YES | B |
| folder_27 | val | 91.13% | 87.50–95.70% | 2.23 | 1 | YES | B |
| folder_28 | train | 95.27% | 83.98–98.44% | 3.99 | 1 | YES | B |
| folder_29 | sealed test | Not evaluated | — | — | 1 | YES | B |
| folder_30 | val | 91.05% | 84.38–97.27% | 4.47 | 1 | YES | B |
| folder_31 | train | 95.20% | 93.75–96.88% | 0.99 | 1 | YES | B |
| folder_32 | sealed test | Not evaluated | — | — | 1 | YES | B |
| folder_33 | train | 96.64% | 94.53–98.44% | 1.32 | 1 | YES | B |
| folder_34 | val | 97.77% | 96.88–98.83% | 0.76 | 1 | YES | B |

Across the 7 selected clips: mean 95.14%, median 95.27%, range 91.05–98.91%, sample SD 3.06 percentage points. Within-clip SD above uses the ten one-second outputs and population denominator 10.

All 70 ESP window outputs and seven float32 means match the host reference bit for bit. The reference uses the unchanged C frontend with `LOGMEL_USE_LOGF` and ai-edge-litert 2.3.0 `BUILTIN_REF` on the identical model and PCM crops. Inference runs took about 4.91 seconds per ten-second clip, including frontend processing, task yields and serial reporting.

Workbook comparison: [TreeVibes.xlsx](../TreeVibes.xlsx), `Sheet1!A27:H35`. Every folder has AUDIO=1 (positive acoustic label), CONFIRMATION=YES and VISUAL=B. The visual legend in `J4:K4` defines B/Β as “exit tunnels, low infestation.” Comments describe urban trees near an alley; folder 26 additionally mentions a waveguide. High model scores are consistent with the positive folder labels. The workbook confirmation is folder-level evidence; it does not make the decimal model score a calibrated per-recording probability or an infestation-severity measurement.

The selection contains no negative-labelled folders, uses one crop per folder, and mixes the original train/validation assignments. It cannot establish overall diagnostic accuracy, specificity, false-positive performance or calibration. No decision threshold was fitted or changed, and the model was not retrained.

| Folder | Recording actually run | Workbook row |
|---|---|---:|
| folder_26 | `F_20200529180809_1_T26.4.wav` | 27 |
| folder_27 | `F_20200529181109_1.wav` | 28 |
| folder_28 | `F_20200604165844_1.wav` | 29 |
| folder_30 | `F_20200602171929_1.wav` | 31 |
| folder_31 | `F_20200602172249_1.wav` | 32 |
| folder_33 | `F_20200610171324_1.wav` | 34 |
| folder_34 | `F_20200529190947_13_T21.2.wav` | 35 |

Pending, metadata only:
- `folder_29/F_20200602171716_1.wav` — sealed `test`.
- `folder_32/F_20200610171753_1.wav` — sealed `test`.

The original split/manifest, workbook, dataset and model are unchanged. The original folder-26 demo fixture and ML-only firmware were restored after this comparison; a physical rerun confirmed mean `0.989062488`, bits `3f7d3333`.

Raw measurements, window scores, exact float32 bits and workbook joins: [folder-comparison.json](folder-comparison.json). Flat per-clip measurements: [folder-comparison.csv](folder-comparison.csv). Serial evidence: [folder-comparison.log](folder-comparison.log).
