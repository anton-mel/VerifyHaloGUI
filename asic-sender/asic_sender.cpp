#include "../data-analyser/src/core/fpga_logger.h"
#include "asic_sender.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <cstring>
#include <chrono>
#include <iomanip>

// Static member definitions
const size_t AsicSender::BUF_LEN = 16384; // Must be multiple of 16 for USB 3.0

AsicSender::AsicSender() : device_(nullptr), running_(false), initialized_(false), data_analyzer_(nullptr) {
    device_ = new OpalKellyLegacy::okCFrontPanel();
}

AsicSender::~AsicSender() {
    stopSending();
    if (device_) {
        delete device_;
        device_ = nullptr;
    }
}

bool AsicSender::initialize(const std::string& deviceSerial, const std::string& bitfilePath) {
    std::cout << "Initializing ASIC Sender..." << std::endl;
    
    // Open device by serial number
    int error = device_->OpenBySerial(deviceSerial.c_str());
    std::cout << "OpenBySerial ret value: " << error << std::endl;
    
    if (error != OpalKellyLegacy::okCFrontPanel::NoError) {
        std::cerr << "Failed to open ASIC device with serial: " << deviceSerial << std::endl;
        return false;
    }
    
    // Configure FPGA with bitfile
    if (!configureFpga(bitfilePath)) {
        std::cerr << "Failed to configure ASIC FPGA" << std::endl;
        return false;
    }
    
    // Reset FIFO
    resetFifo();
    
    initialized_ = true;
    std::cout << "ASIC Sender initialized successfully" << std::endl;
    return true;
}

bool AsicSender::configureFpga(const std::string& bitfilePath) {
    std::cout << "Configuring ASIC FPGA with bitfile: " << bitfilePath << std::endl;
    
    int error = device_->ConfigureFPGA(bitfilePath.c_str());
    std::cout << "ConfigureFPGA ret value: " << error << std::endl;
    
    return (error == OpalKellyLegacy::okCFrontPanel::NoError);
}

void AsicSender::resetFifo() {
    std::cout << "Resetting ASIC FIFO..." << std::endl;
    
    // Send reset signal to FIFO
    device_->SetWireInValue(0x10, 0xff, 0x01);
    device_->UpdateWireIns();
    
    device_->SetWireInValue(0x10, 0x00, 0x01);
    device_->UpdateWireIns();
    
    std::cout << "ASIC FIFO reset complete" << std::endl;
}

bool AsicSender::writeToFpga(const std::vector<uint8_t>& data) {
    int writeRet = device_->WriteToPipeIn(0x80, data.size(), data.data());
    
    // Return value is the number of bytes written, not an error code
    return (writeRet > 0);
}

bool AsicSender::readFromFpga(std::vector<uint8_t>& data) {
    data.resize(BUF_LEN);
    
    int readRet = device_->ReadFromPipeOut(0xA0, data.size(), data.data());
    
    // Return value is the number of bytes read, not an error code
    if (readRet > 0) {
        data.resize(readRet); // Resize to actual bytes read
        return true;
    }
    
    return false;
}

void AsicSender::startSending() {
    if (!initialized_) {
        std::cerr << "ASIC Sender not initialized" << std::endl;
        return;
    }
    
    std::cout << "Starting ASIC data sending..." << std::endl;
    running_ = true;
}

void AsicSender::stopSending() {
    if (running_) {
        std::cout << "Stopping ASIC data sending..." << std::endl;
        running_ = false;
    }
}

void AsicSender::setDataAnalyzer(FpgaLogger* analyzer) {
    data_analyzer_ = analyzer;
}

bool AsicSender::configurePipeline(int pipelineId) {
    if (!initialized_) {
        std::cerr << "ASIC Sender not initialized" << std::endl;
        return false;
    }
    
    std::cout << "Configuring FPGA pipeline: " << pipelineId << std::endl;
    
    // Set pipeline configuration via WireIn
    // Address 0x01: Pipeline selection (0-9)
    device_->SetWireInValue(0x01, pipelineId, 0x0F); // Use lower 4 bits for pipeline ID
    device_->UpdateWireIns();
    
    // Trigger configuration update
    device_->ActivateTriggerIn(0x40, 0); // Trigger bit 0 for pipeline config
    device_->UpdateWireIns();
    
    std::cout << "Pipeline " << pipelineId << " configured successfully" << std::endl;
    return true;
}

bool AsicSender::enableAnalysisMode() {
    if (!initialized_) {
        std::cerr << "ASIC Sender not initialized" << std::endl;
        return false;
    }
    
    std::cout << "Enabling FPGA analysis mode..." << std::endl;
    
    // Set analysis mode via WireIn
    // Address 0x02: Mode control (bit 0: analysis mode, bit 1: test mode)
    device_->SetWireInValue(0x02, 0x01, 0x03); // Enable analysis mode, disable test mode
    device_->UpdateWireIns();
    
    // Trigger mode change
    device_->ActivateTriggerIn(0x40, 1); // Trigger bit 1 for mode change
    device_->UpdateWireIns();
    
    std::cout << "Analysis mode enabled successfully" << std::endl;
    return true;
}

bool AsicSender::disableTestPattern() {
    if (!initialized_) {
        std::cerr << "ASIC Sender not initialized" << std::endl;
        return false;
    }
    
    std::cout << "Disabling FPGA test pattern mode..." << std::endl;
    
    // Disable test pattern generation
    // Address 0x03: Test pattern control (bit 0: enable/disable test pattern)
    device_->SetWireInValue(0x03, 0x00, 0x01); // Disable test pattern
    device_->UpdateWireIns();
    
    // Trigger test pattern disable
    device_->ActivateTriggerIn(0x40, 2); // Trigger bit 2 for test pattern control
    device_->UpdateWireIns();
    
    std::cout << "Test pattern mode disabled successfully" << std::endl;
    return true;
}

bool AsicSender::setThresholds(double lowThreshold, double highThreshold) {
    if (!initialized_) {
        std::cerr << "ASIC Sender not initialized" << std::endl;
        return false;
    }
    
    std::cout << "Setting FPGA thresholds - NEO: " << lowThreshold << ", Seizure: " << highThreshold << std::endl;
    
    // Convert thresholds to appropriate bit widths
    uint16_t neoThresh = static_cast<uint16_t>(lowThreshold * 65535);      // 16-bit NEO threshold
    uint8_t seizureThresh = static_cast<uint8_t>(highThreshold * 32);      // 8-bit seizure threshold (channels)
    
    // Configure FPGA according to corrected implementation:
    // ep00wire[0] = pipeline enable
    // ep00wire[15:8] = seizure threshold (channels)
    // ep01wire[31:0] = input timestamp (set by sendWaveformData)
    // ep02wire[15:0] = NEO threshold
    // ep02wire[23:16] = amplitude threshold (reserved)
    // ep02wire[31:24] = frequency threshold (reserved)
    
    // Set ep00wire: pipeline enable + seizure threshold
    uint32_t ep00wire = 0x0001 | (seizureThresh << 8);  // Enable + seizure threshold
    device_->SetWireInValue(0x00, ep00wire, 0xFFFFFFFF);
    device_->UpdateWireIns();
    
    // Set ep02wire: NEO threshold + reserved fields
    uint32_t ep02wire = neoThresh;  // NEO threshold in lower 16 bits
    device_->SetWireInValue(0x02, ep02wire, 0xFFFFFFFF);
    device_->UpdateWireIns();
    
    std::cout << "Thresholds set successfully - NEO: " << neoThresh << ", Seizure channels: " << (int)seizureThresh << std::endl;
    return true;
}

void AsicSender::sendWaveformData(const std::vector<uint8_t>& waveformData) {
    if (!running_ || !initialized_) {
        return;
    }
    
    // Ensure data length is multiple of 16 for USB 3.0
    std::vector<uint8_t> paddedData = waveformData;
    while (paddedData.size() % 16 != 0) {
        paddedData.push_back(0);
    }
    
    // Limit to BUF_LEN
    if (paddedData.size() > BUF_LEN) {
        paddedData.resize(BUF_LEN);
    }
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::cout << "[";
    std::cout << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    std::cout << "] Sending waveform data to the FPGA..." << std::endl;
    
    // Send timestamp to FPGA via ep01wire (input timestamp)
    uint32_t timestamp = static_cast<uint32_t>(time_t);  // Unix timestamp
    device_->SetWireInValue(0x01, timestamp, 0xFFFFFFFF);
    device_->UpdateWireIns();
    
    // Send data to FPGA
    if (!writeToFpga(paddedData)) {
        std::cerr << "Failed to write waveform data to ASIC FPGA" << std::endl;
        return;
    }
    
    // Read processed data from FPGA
    std::vector<uint8_t> processedData;
    if (!readFromFpga(processedData)) {
        std::cerr << "Failed to read processed data from ASIC FPGA" << std::endl;
        return;
    } else {
        // Get current timestamp for success message
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::cout << "[";
        std::cout << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        std::cout << "] Data successfully read from ASIC FPGA!" << std::endl;
    }
    
    // Read seizure detection results from WireOut
    device_->UpdateWireOuts();
    uint32_t seizureResults = device_->GetWireOutValue(0x30);  // ep30wire contains HALO_outs
    
    // Extract seizure detection results according to corrected FPGA format:
    // HALO_outs[31:2] = seizure_timestamp[29:0]
    // HALO_outs[1] = seizure_result_valid
    // HALO_outs[0] = seizure_detected
    bool seizureDetected = (seizureResults & 0x01) != 0;
    bool resultValid = (seizureResults & 0x02) != 0;
    uint32_t seizureTimestamp = (seizureResults >> 2) & 0x3FFFFFFF;  // 30-bit timestamp
    
    std::cout << "Seizure Detection Results:" << std::endl;
    std::cout << "  Detected: " << (seizureDetected ? "YES" : "NO") << std::endl;
    std::cout << "  Valid: " << (resultValid ? "YES" : "NO") << std::endl;
    std::cout << "  Timestamp: " << seizureTimestamp << std::endl;
    
    // Analyze FPGA response data with original neural data
    if (data_analyzer_) {
        data_analyzer_->analyzeFpgaData(processedData, waveformData);
    }
}
