import argparse
import random
from datetime import datetime, timedelta, timezone
from pathlib import Path
import struct
import sys
import math

# Reuse Intan-style synthetic generator from fpga/halo_seizure/shared/synthetic.py
ROOT = Path(__file__).resolve().parents[2]
SYNTH_PATH = ROOT / "fpga" / "halo_seizure" / "shared"
if str(SYNTH_PATH) not in sys.path:
    sys.path.append(str(SYNTH_PATH))

from synthetic import NeuralSynthSource, NUM_CHANNELS  # type: ignore

# Simple synthetic generator: 32 channels, 1 kHz, configurable duration.
# Writes:
#   - hour_##_raw.log in HALO raw binary format (matches raw_log_format.{h,cpp})
#   - hour_##_detections.bin with bit-packed start/end events (2-bit type, 5-bit channel, 25-bit timestamp ms)

MAGIC = b"HALOLOG\x00"
VERSION = 1
CHANNELS = NUM_CHANNELS
SAMPLES_PER_RECORD = 128
SAMPLE_BITS = 16
TS_BITS = 32
RECORD_PAYLOAD_BYTES = 512 + 8192  # ts + waveform

TYPE_START = 0b10
TYPE_END = 0b01


def write_header(f):
    f.write(
        struct.pack(
            "<8sHHIIII",
            MAGIC,
            VERSION,
            0,
            CHANNELS,
            SAMPLES_PER_RECORD,
            SAMPLE_BITS,
            TS_BITS,
        )
    )


def generate_hour(raw_path: Path, det_path: Path, duration_sec: int, seed_offset: int):
    """Generate one hour's worth of raw data and seizure detections using NeuralSynthSource."""
    raw_path.parent.mkdir(parents=True, exist_ok=True)
    det_path.parent.mkdir(parents=True, exist_ok=True)

    total_samples = duration_sec * 1000  # 1 kHz
    records = (total_samples + SAMPLES_PER_RECORD - 1) // SAMPLES_PER_RECORD

    # One NeuralSynthSource per channel, with seizures enabled
    sources = []
    in_seizure_prev = []
    for ch in range(CHANNELS):
        src = NeuralSynthSource(sample_rate=1000.0, n_units=2, seed=seed_offset + ch, enable_seizures=True)
        # Make seizures rare and ~5 seconds long
        src.seizure_probability = 0.001  # much lower than default 0.01
        src.seizure_duration_ms = 5000.0
        sources.append(src)
        in_seizure_prev.append(False)

    # Track seizures as (start_ms, end_ms, channel)
    seizures = []

    def update_seizures():
        for ch, src in enumerate(sources):
            ss = src.seizure_start_time_ms
            if ss is not None:
                elapsed = src.t_ms - ss
                in_seizure = (0.0 <= elapsed < src.seizure_duration_ms)
            else:
                in_seizure = False

            if in_seizure and not in_seizure_prev[ch]:
                # New seizure starting now
                start_ms = int(ss)
                end_ms = int(ss + src.seizure_duration_ms)
                seizures.append((start_ms, end_ms, ch))
            in_seizure_prev[ch] = in_seizure

    with raw_path.open("wb") as f:
        write_header(f)
        ts_counter = 0
        for rec in range(records):
            ts_block = [ts_counter + i for i in range(SAMPLES_PER_RECORD)]
            ts_counter += SAMPLES_PER_RECORD

            wave = []
            # For each sample in this record
            for i in range(SAMPLES_PER_RECORD):
                # Advance each channel once (1 ms step)
                for ch, src in enumerate(sources):
                    uv = src.next_sample()
                    # Intan-style ADC code conversion
                    code16 = int(round(uv / 0.195)) + 32768
                    code16 = max(0, min(65535, code16))
                    wave.append(code16)
                update_seizures()

            f.write(struct.pack("<QII", 0, rec, RECORD_PAYLOAD_BYTES))
            f.write(struct.pack("<" + "I" * SAMPLES_PER_RECORD, *ts_block))
            f.write(struct.pack("<" + "H" * (CHANNELS * SAMPLES_PER_RECORD), *wave))

    # Convert seizures to packed events (timestamps are local to this hour)
    events = []
    for start_ms, end_ms, ch in seizures:
        if 0 <= start_ms < duration_sec * 1000:
            events.append(((start_ms & 0x1FFFFFF) << 7) | ((ch & 0x1F) << 2) | TYPE_START)
        if 0 <= end_ms < duration_sec * 1000:
            events.append(((end_ms & 0x1FFFFFF) << 7) | ((ch & 0x1F) << 2) | TYPE_END)

    with det_path.open("wb") as f:
        for ev in events:
            f.write(struct.pack("<I", ev))


def main():
    parser = argparse.ArgumentParser(description="Generate synthetic raw Intan logs plus detection bins.")
    parser.add_argument("--days", type=int, default=4, help="Number of days back from today (including today)")
    parser.add_argument("--hours", type=str, default="00,06,12,18", help="Comma list of hours to generate")
    parser.add_argument("--duration-sec", type=int, default=60, help="Duration per hour file (seconds)")
    parser.add_argument("--seed", type=int, default=123, help="RNG seed")
    args = parser.parse_args()

    random.seed(args.seed)
    hours = [h.strip() for h in args.hours.split(",") if h.strip()]

    base_logs = Path(__file__).resolve().parent.parent / "logs"
    today = datetime.now(timezone.utc).date()

    for day_idx in range(args.days):
        day = today - timedelta(days=day_idx)
        date_str = day.strftime("%Y-%m-%d")
        day_dir = base_logs / date_str

        for hr in hours:
            raw_path = day_dir / f"hour_{int(hr):02d}_raw.log"
            det_path = day_dir / f"hour_{int(hr):02d}_detections.bin"
            seed_offset = args.seed + day_idx * 100 + int(hr)
            generate_hour(raw_path, det_path, args.duration_sec, seed_offset)
            print(f"Generated {raw_path} and {det_path}")


if __name__ == "__main__":
    main()

