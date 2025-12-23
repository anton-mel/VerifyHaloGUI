#include "raw_log_format.h"

#include <filesystem>
#include <type_traits>

namespace {
// Ensure we always write little-endian regardless of host.
template <typename T>
void write_le(std::ofstream& out, T value) {
    static_assert(std::is_integral_v<T>, "write_le requires integral type");
    for (size_t i = 0; i < sizeof(T); ++i) {
        uint8_t byte = static_cast<uint8_t>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF);
        out.put(static_cast<char>(byte));
    }
}
} // namespace

RawLogWriter::RawLogWriter() = default;
RawLogWriter::~RawLogWriter() { close(); }

bool RawLogWriter::open(const std::string& path) {
    close();

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        return false;
    }

    // Write header in a stable, explicit way.
    file_.write(header_.magic, sizeof(header_.magic));
    write_le<uint16_t>(file_, header_.version);
    write_le<uint16_t>(file_, header_.reserved);
    write_le<uint32_t>(file_, header_.channel_count);
    write_le<uint32_t>(file_, header_.samples_per_record);
    write_le<uint32_t>(file_, header_.sample_bits);
    write_le<uint32_t>(file_, header_.timestamp_bits);

    sequence_ = 0;
    return file_.good();
}

bool RawLogWriter::append(uint64_t unix_time_ns,
                          const std::vector<uint32_t>& timestamps,
                          const std::vector<uint16_t>& waveform) {
    if (!file_.is_open()) return false;
    if (timestamps.size() != 128) return false;
    if (waveform.size() != 32 * 128) return false;

    RawLogRecordHeader rec{};
    rec.unix_time_ns = unix_time_ns;
    rec.sequence_index = sequence_++;

    // Record header
    write_le<uint64_t>(file_, rec.unix_time_ns);
    write_le<uint32_t>(file_, rec.sequence_index);
    write_le<uint32_t>(file_, rec.payload_bytes);

    // Timestamps
    for (uint32_t ts : timestamps) {
        write_le<uint32_t>(file_, ts);
    }

    // Waveform samples (channel-major)
    for (uint16_t sample : waveform) {
        write_le<uint16_t>(file_, sample);
    }

    file_.flush();
    return file_.good();
}

void RawLogWriter::close() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

