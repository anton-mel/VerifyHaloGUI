// Simple binary log format for raw Intan blocks (timestamps + waveform data).
// The format is intentionally flat and fixed-size to be easy to stream and read
// from the GUI without HDF5 or other heavy dependencies.
//
// Layout (all little-endian):
// FileHeader {
//   char     magic[8]      = "HALOLOG";
//   uint16_t version        = 1;
//   uint16_t reserved       = 0;
//   uint32_t channel_count  = 32;
//   uint32_t samples_per_record = 128;
//   uint32_t sample_bits    = 16;   // waveform sample width
//   uint32_t timestamp_bits = 32;   // timestamp width per sample
// }
// Repeated Record {
//   uint64_t unix_time_ns;          // capture start time for this record
//   uint32_t sequence_index;        // increments per record for sanity
//   uint32_t payload_bytes;         // fixed at 512 (timestamps) + 8192 (waveform)
//   uint32_t timestamps[128];       // 512 bytes, one per sample
//   uint16_t waveform[32*128];      // 8192 bytes, channel-major:
//                                   // waveform[channel * 128 + sample]
// }
//
// Record size is fixed: 16 bytes header + 512 + 8192 = 8720 bytes.
// Files can be memory-mapped or sequentially read with simple pointer math.

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

struct RawLogFileHeader {
    char magic[8] = {'H','A','L','O','L','O','G',0};
    uint16_t version = 1;
    uint16_t reserved = 0;
    uint32_t channel_count = 32;
    uint32_t samples_per_record = 128;
    uint32_t sample_bits = 16;
    uint32_t timestamp_bits = 32;
};

struct RawLogRecordHeader {
    uint64_t unix_time_ns = 0;
    uint32_t sequence_index = 0;
    uint32_t payload_bytes = 512 + 8192; // 128 * 4 + 32 * 128 * 2
};

// Minimal writer for the raw log format.
class RawLogWriter {
public:
    RawLogWriter();
    ~RawLogWriter();

    // Open/prepare a log file. Creates directories if needed and writes header.
    bool open(const std::string& path);

    // Append a record with 128 timestamps (uint32) and 32x128 waveform samples (uint16).
    bool append(uint64_t unix_time_ns,
                const std::vector<uint32_t>& timestamps,
                const std::vector<uint16_t>& waveform);

    void close();
    bool isOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
    RawLogFileHeader header_{};
    uint32_t sequence_{0};
};

