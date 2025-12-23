import argparse
import os
import struct
import time
from datetime import datetime, timezone
from pathlib import Path
from math import sin, pi


# Binary layout mirrors src/core/raw_log_format.{h,cpp}
FILE_MAGIC = b"HALOLOG\x00"
VERSION = 1
CHANNELS = 32
SAMPLES_PER_RECORD = 128
SAMPLE_BITS = 16
TIMESTAMP_BITS = 32
RECORD_PAYLOAD_BYTES = 512 + 8192  # timestamps + waveform
RECORD_SIZE = 16 + RECORD_PAYLOAD_BYTES


def write_header(f):
    header = struct.pack(
        "<8sHHIIII",
        FILE_MAGIC,
        VERSION,
        0,  # reserved
        CHANNELS,
        SAMPLES_PER_RECORD,
        SAMPLE_BITS,
        TIMESTAMP_BITS,
    )
    f.write(header)


def make_waveform(record_idx: int):
    """
    Generate a deterministic waveform block:
    - timestamps: incremental sample ticks
    - waveform: channel-major, low-amplitude sine plus channel offset
    """
    timestamps = [record_idx * SAMPLES_PER_RECORD + i for i in range(SAMPLES_PER_RECORD)]

    waveform = []
    for ch in range(CHANNELS):
        for i in range(SAMPLES_PER_RECORD):
            # 10-bit range centered around 1024 with slight per-channel offset
            val = 1024 + int(200 * sin(2 * pi * i / SAMPLES_PER_RECORD) + ch * 4)
            val = max(0, min(4095, val))
            waveform.append(val)
    return timestamps, waveform


def append_record(f, seq_idx: int):
    unix_time_ns = time.time_ns()
    timestamps, waveform = make_waveform(seq_idx)

    f.write(struct.pack("<QII", unix_time_ns, seq_idx, RECORD_PAYLOAD_BYTES))
    f.write(struct.pack("<" + "I" * SAMPLES_PER_RECORD, *timestamps))
    f.write(struct.pack("<" + "H" * (CHANNELS * SAMPLES_PER_RECORD), *waveform))


def main():
    parser = argparse.ArgumentParser(description="Generate synthetic raw Intan logs for GUI testing.")
    parser.add_argument("--records", type=int, default=50, help="Number of records to write (default: 50)")
    parser.add_argument(
        "--out",
        type=str,
        default=None,
        help="Optional output file. Defaults to data-analyser/logs/YYYY-MM-DD/intan_fake.log",
    )
    args = parser.parse_args()

    # Default output under data-analyser/logs/<date>/
    if args.out is None:
        today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        base = Path(__file__).resolve().parent.parent / "logs" / today
        base.mkdir(parents=True, exist_ok=True)
        out_path = base / "intan_fake.log"
    else:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)

    with open(out_path, "wb") as f:
        write_header(f)
        for i in range(args.records):
            append_record(f, i)
            # small sleep to make timestamps obviously increasing
            time.sleep(0.005)

    print(f"Wrote {args.records} records ({RECORD_SIZE * args.records} bytes) to {out_path}")


if __name__ == "__main__":
    main()

