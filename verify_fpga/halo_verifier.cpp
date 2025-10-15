#include "halo_verifier.h"

// Static member definitions
const size_t HaloVerifier::BUF_LEN = 16384;

HaloVerifier::HaloVerifier() : device_(nullptr), initialized_(false) {
    device_ = new okCFrontPanel();
}

HaloVerifier::~HaloVerifier() {
    cleanup();
    if (device_) {
        delete device_;
        device_ = nullptr;
    }
}

bool HaloVerifier::initialize(const std::string& deviceSerial, const std::string& bitfilePath) {
    std::cout << "Initializing Halo Verifier..." << std::endl;
    
    // Open device by serial number
    int error = device_->OpenBySerial(deviceSerial.c_str());
    std::cout << "OpenBySerial ret value: " << error << std::endl;
    
    if (error != okCFrontPanel::NoError) {
        std::cerr << "Failed to open device with serial: " << deviceSerial << std::endl;
        return false;
    }
    
    // Configure FPGA with bitfile
    if (!configureFpga(bitfilePath)) {
        std::cerr << "Failed to configure FPGA" << std::endl;
        return false;
    }
    
    // Reset FIFO
    resetFifo();
    
    initialized_ = true;
    std::cout << "Halo Verifier initialized successfully" << std::endl;
    return true;
}

bool HaloVerifier::configureFpga(const std::string& bitfilePath) {
    std::cout << "Configuring FPGA with bitfile: " << bitfilePath << std::endl;
    
    int error = device_->ConfigureFPGA(bitfilePath.c_str());
    std::cout << "ConfigureFPGA ret value: " << error << std::endl;
    
    return (error == okCFrontPanel::NoError);
}

void HaloVerifier::resetFifo() {
    std::cout << "Resetting FIFO..." << std::endl;
    
    // Send reset signal to FIFO
    device_->SetWireInValue(0x10, 0xff, 0x01);
    device_->UpdateWireIns();
    
    device_->SetWireInValue(0x10, 0x00, 0x01);
    device_->UpdateWireIns();
    
    std::cout << "FIFO reset complete" << std::endl;
}

bool HaloVerifier::writeToFpga(const std::vector<uint8_t>& data) {
    int writeRet = device_->WriteToPipeIn(0x80, static_cast<long>(data.size()), const_cast<unsigned char*>(data.data()));
    
    return (writeRet > 0);
}

bool HaloVerifier::readFromFpga(std::vector<uint8_t>& data) {
    data.resize(BUF_LEN);
    
    int readRet = device_->ReadFromPipeOut(0xA0, static_cast<long>(data.size()), data.data());
    
    if (readRet > 0) {
        data.resize(readRet);
        return true;
    }
    
    return false;
}

bool HaloVerifier::sendReceiveData(const std::vector<uint8_t>& inputData, std::vector<uint8_t>& outputData) {
    if (!initialized_) {
        std::cerr << "Halo Verifier not initialized" << std::endl;
        return false;
    }
    
    // Ensure data length is multiple of 16 for USB 3.0
    std::vector<uint8_t> paddedData = inputData;
    while (paddedData.size() % 16 != 0) {
        paddedData.push_back(0);
    }
    
    // Limit to BUF_LEN
    if (paddedData.size() > BUF_LEN) {
        paddedData.resize(BUF_LEN);
    }
    
    std::cout << "Sending data to FPGA..." << std::endl;
    
    // Send data to FPGA
    if (!writeToFpga(paddedData)) {
        std::cerr << "Failed to write data to FPGA" << std::endl;
        return false;
    }
    
    // Read response from FPGA
    if (!readFromFpga(outputData)) {
        std::cerr << "Failed to read response from FPGA" << std::endl;
        return false;
    }
    
    std::cout << "Successfully received response from FPGA" << std::endl;
    return true;
}

void HaloVerifier::cleanup() {
    if (initialized_) {
        initialized_ = false;
        std::cout << "Halo Verifier cleaned up" << std::endl;
    }
}
