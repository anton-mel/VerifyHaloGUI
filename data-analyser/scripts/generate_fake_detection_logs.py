import argparse
import struct
import time
import random
from datetime import datetime, timezone, timedelta
from pathlib import Path

# Bit layout (little-endian uint32 per event):
# bits 0-1 : type (0b10 start, 0b01 end, 0b00 idle-not-logged)
# bits 2-6 : channel id (1-32)
# bits 7-31: truncated timestamp ticks (25 bits)
# Timestamp ticks here are milliseconds since file start (mod 2^25).


TYPE_START = 0b10
TYPE_END = 0b01


def pack_event(ts_ticks: int, channel: int, event_type: int) -> int:
    return ((ts_ticks & ((1 << 25) - 1)) << 7) | ((channel & 0x1F) << 2) | (event_type & 0x3)


def generate_events(pairs: int, rng: random.Random, start_ticks: int = 0):
    events = []
    ts = start_ticks
    for _ in range(pairs):
        channel = rng.randint(1, 32)
        duration = 1200 + rng.randint(0, 800)  # ms
        events.append(pack_event(ts, channel, TYPE_START))
        ts += duration
        events.append(pack_event(ts, channel, TYPE_END))
        ts += 300 + rng.randint(0, 600)  # gap
    return events


def write_events(path: Path, events):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        for ev in events:
            f.write(struct.pack("<I", ev))
    print(f"Wrote {len(events)} events to {path}")


def main():
    parser = argparse.ArgumentParser(description="Generate fake FPGA detection logs (bit-packed) for GUI testing.")
    parser.add_argument("--pairs", type=int, default=10, help="Seizure start/end pairs per file")
    parser.add_argument("--days", type=int, default=3, help="How many past days (including today) to generate")
    parser.add_argument("--files-per-day", type=int, default=6, help="How many hourly files per day")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for reproducibility")
    parser.add_argument("--out", type=str, default=None, help="If set, write a single file here instead of multiple")
    args = parser.parse_args()

    rng = random.Random(args.seed)

    # Single-file compatibility mode
    if args.out is not None:
        out_path = Path(args.out)
        events = generate_events(args.pairs, rng)
        write_events(out_path, events)
        return

    base_logs = Path(__file__).resolve().parent.parent / "logs"
    today = datetime.now(timezone.utc).date()

    for day_offset in range(args.days):
        day = today - timedelta(days=day_offset)
        date_str = day.strftime("%Y-%m-%d")
        day_dir = base_logs / date_str

        # Pick distinct hours for this day
        hours = list(range(24))
        rng.shuffle(hours)
        hours = sorted(hours[: max(1, min(args.files_per_day, 24))])

        for hour in hours:
            out_path = day_dir / f"hour_{hour:02d}_detections.bin"
            events = generate_events(args.pairs, rng)
            write_events(out_path, events)


if __name__ == "__main__":
    main()

