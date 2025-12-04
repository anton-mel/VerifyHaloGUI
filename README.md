# Documentation

*Modified by: Anton Melnychuk*  
*Email: <anton.melnychuk@yale.edu>*  
*Date: Sep 19th, 2025*

## Table of Contents

This project can be split into the following sections:

- [Introduction and Getting Started](#introduction)
- [main.cpp](#maincpp)
    - [What if connection fails on restart?](#how-to-run)
    - [How to update the data sample rate?](#sample-rate-update)
    - [How to set-up seizure threshold?](#configurations)
- [intan-reader/](#intan-reader)
    - [Pipeline fails over long time](#pipeline-failed-over-time)
    - [Waveform ADC and DAC Assumptions](#waveform-scaling)
    - [What if Intan Device is Missing?](#intan-device-missing)
- [asic-sender/](#asic-sender)
    - [TODO: Why decoder might be wrong...](#raw-asic-response-structure)
    - [Memory and logging constraints](#logging)
- [data-analyser/](#data-analyser)
- [modified-intan-rhx/](#modified-intan-rhx)

## Introduction

This work is part of the HALO Brain Computer Interface workload testing. It interconnects the devices end-to-end with GUI interface for convenient laboratory neural analysis. Please find more documentation on the project below:

- https://www.cs.yale.edu/homes/abhishek/ksriram-isca20.pdf
- https://www.cs.yale.edu/homes/abhishek/ksriram-isca23.pdf

### Pipeline Overview

Mainly note, this solution provides a PC-in-the-loop approach for temporal testing of the processing part of the chip. The next iteration will involve leveraging data-acquisition network-on-chip for fully implanted use.

```
                Logging (H5) ──→ Data Dashboard
                    ↑ 
    Intan RHX ──→ PC ⟷ ASIC
                    ↓
                Waveform GUI
```

### Device Models Used

- Intan Device: XEM7310 Opal Kelly FPGA
- FPGA for BCI: XEM6310 Opal Kelly FPGA
- https://intantech.com/files/Intan_RHD2000_USB3_FPGA_interface.pdf 
- https://intantech.com/downloads.html  
- https://pins.opalkelly.com/downloads  

### How to Run

Connect both devices to the host PC via USB3. First, launch the pipeline for logging, then start the required GUI.

> [!NOTE]
> If the pipeline cannot establish a connection to the device, try power-cycling (restarting) the devices. It should flush the corrupted drivers.

## main.cpp

The pipeline runs asynchronously for reading neural data and sending it for processing on the FPGA. We decouple the Intan reader and the ASIC sender because the Opal Kelly SDKs/drivers conflict when linked together.

> [!NOTE]
> By separating these workloads, we give up certain flexibility in configuring the device as the headers should be sent over from one process to another with correct synchronization.

FPGA is configured with BCI bitstream `asic-sender/First.bit`. You can regenerate the BCI bitstream using the Xilinx Vivado project. See HALO documentation in `data-analyser/docs` for more details on the HALO APIs.

Overall, `main.cpp` initializes and interconnects:
- (1) the Intan RHX device reader thread
- (2) the POSIX shared-memory hub for the waveform GUI + ASIC communication
- (3) the ASIC/FPGA hub for seizure-processing workloads
- (4) the FPGA-response data analyzer/logging functionality for a health monitoring

> [!NOTE]
> We use shared memory to deliver high-throughput, low-latency frames to multiple local consumers without extra copies, since we do not need cross-host distribution.

### Configurations

- **Configured pipeline**: Pipeline 6 = NEO → THR → GATE (nonlinear energy operator, thresholding, gating) for seizure detection (see HALO docs).

- Threshold must be modified manually in `main.cpp`:
  - `asicSender.setThresholds(<low>, <high>)`
  - `fpgaLogger->setThresholds(<low>, <high>)`
  - **Current values**: low=0.3, high=0.7

> [!NOTE]
> When updating, keep FPGA and decoder thresholds in sync.

### Sample Rate

Current pipeline runs at `1kHz` sample rate. This provides the longest possible execution time by the Intan device.

**Execution Overflow-Risk Analysis:**
- **Timestamp overflow**: `uint32_t timestamp` increments by 128 samples per block at 1kHz
- **Overflow time**: 2^32 / (1000 Hz / 128 samples) = **~49.7 days**
- **Data block size**: 128 samples × 32 channels × 1 stream = 4,096 samples per block
- **Block frequency**: 1000 Hz / 128 samples = **7.8125 Hz** (every 128ms)
- **Shared memory**: Fixed size, no overflow risk (circular overwrite) 

> [!NOTE]
> Sample rate configuration is handled manually by the maintainer due to the separated workload architecture. Contact the maintainer if you need to update the sample rate, as the Intan GUI is no longer responsible for it.

### Pipeline failed over time?

> [!WARNING]
> The pipeline has not been rigorously tested in the development stage for buffer overflow, but we provide some approximate risk analysis above. Contact the maintainer, if the pipeline failed.

## intan-reader

This folder connects to the Intan RHX device (Opal Kelly XEM7310), acquires amplifier data, and publishes frames to shared memory for consumption by the waveform GUI and the HALO ASIC/FPGA path. Thus, multiple readers can map the segment read-only without copies.

### Shared memory interface
  - Segment name: `/intan_rhx_shm_v1` under POSIX `shm_open()`
  - Writer initializes with stream count, channel count, and sample rate, then writes a header followed by contiguous data blocks each frame.
  - Header fields: magic `0x494E5441` ("INTA"), `streamCount`, `channelCount`, `sampleRate`, `dataSize`, `timestamp`.
  - Data blocks: array of `{streamIndex, channelIndex, valueMicrovolts}` for `samplesPerBlock` per channel (internal default is 128 samples per block).

> [!NOTE]
> Since channels arrive in parallel, the shared memory is interleaved in memory as `[t0_ch0, t0_ch1, ..., t0_ch31, t1_ch0, t1_ch1, ...]`.

### Waveform Scaling
  - Raw ADC code → microvolts: `uV = (code - 32768) * 0.195f` before writing into shared memory (Waveform ADC and Display).
  - ASIC path reader converts microvolts → `uint8_t` for transmission: `(uV + 1000.0f) / 8.0f` (clamped to 0–255). See HALO documentation in `data-analyser/docs`.

### Intan Device Missing

> [!NOTE]
> If the Intan RHX Device cannot be found on pipeline start-up, this could potentially be caused if you are already running the GUI for waveform visualization without enabling pipelined mode (see `modified-intan-rhx` section). The GUI will then occupy the cable and block any other readers. To fix, try closing the GUI.

## asic-sender

This folder handles communication with the HALO ASIC/FPGA (XEM6310) for real-time seizure detection processing. It reads neural data from shared memory, sends it to the FPGA, containing the chip, and processes the responses from it.

### Design & Constraints

- **FIFO Buffer**: 16,384 bytes (`BUF_LEN`) - can hold ~4 data blocks. Buffer overflow risk is minimal due to the 5x ASIC processing speed advantage.
- **Input**: All 32 channels sent as single block to FPGA (32 channels × 128 samples = 4,096 bytes per block).
- **Response**: The NEO (Nonlinear Energy Operator) analyzes energy patterns across the entire channel array and ASIC returns a single response for all the channels.

### Raw ASIC Response Structure

```
Input:  32 channels × 128 samples = 4,096 bytes (uint8_t)
         ↓
         [Pad to 16,384 bytes for USB 3.0 compliance]
         ↓
FPGA Processing (Pipeline 6: NEO → THR → GATE)
         ↓
Output: 16,384 raw bytes (uint8_t)
```

### [TODO] ASIC Response & RAW Data Decoding

> [!WARNING]
> The current decoding is likely INVALID because it makes assumptions without understanding the actual FPGA pipeline implementation. Without proper documentation of the SCALO architecture's output format, we are essentially right now guessing what the FPGA computed.

### [TODO] Confidence & Detection Logic

This part is computed as normalized variance of all 16,384 bytes in the FPGA response (a 0–1 activity metric).

### [TODO] Timestamp Matching Issue

- **No explicit matching**: Timestamps are **not synchronized** between input and response
- **Response timestamp**: Generated when FPGA response is received
- **Potential issue**: Cannot correlate specific input samples with seizure detections

### Logging

`data-analyser/logs/YYYY-MM-DD/hour_HH.h5` (on hourly bases using HDF5 files)

> [!NOTE]
> For now, original neural data is preserved alongside FPGA analysis results. If full raw blocks are stored, approximately 87–102 GB will be required for data acquisition over 30 days.

## data-analyser

This program analyzes HDF5 logs and updates every 5 seconds:

- Last 20 detections: table of recent events (timestamp, type, confidence).
- Daily counts: seizures per day over the last month.

![Seizure Detection Logs](DEMO/logging.png)
*Real-time seizure detection analysis with decoded FPGA responses showing normal activity, threshold exceeded events, and seizure detections*

## modified-intan-rhx
 
We modified the original Intan RHX GUI to add an Advanced setting: a "Pipelined mode" checkbox. When enabled, the application does not connect to the device; it reads data from shared memory (similar to `asic-sender`) instead. This decouples visualization from the pipeline, so the GUI can be toggled on/off as needed.
 
![Intan RHX GUI](DEMO/GUI.png)
*Modified Intan RHX visualization interface showing real-time neural data acquisition*

## TODO

- ~~Write the pipeline SPEC~~
- ~~Verify pipeline logic end-to-end~~
- ~~**IMPORTANT:** Correct the ASIC response decoder~~
- ~~Decode ASIC response~~
- ~~Fix the logging format in HDF5 files~~
- ~~Run the pipeline at 1 kHz acquisition frequency~~
- Ensure the device is properly flushed
- ~~Include additional data analysis required for animal testing~~

## How to Contribute

I prefer building on Linux/macOS first (POSIX-friendly, easy terminal builds), then adapting to Windows workloads. On macOS, use the provided `Makefile`:

```bash
make run
```

The GUIs (waveform display and data dashboard) should be built separately in `modified-intan-rhx/` and `data-analyser/` in a similar way.

For Windows, ensure required toolchains are installed. Primarily, have `mingw32-make`, `mingw64`, or MSVC in your PATH. The GUIs use Qt. Download the Qt installer from the [official website](https://www.qt.io/download-qt-installer).

> [!NOTE]
> For chip documentation, please request access.
