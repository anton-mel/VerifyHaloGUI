#ifndef HALO_VERIFIER_H
#define HALO_VERIFIER_H

#include <iostream>
#include <vector>
#include <string>
#include "../asic-sender/okFrontPanel.h"

class HaloVerifier {
public:
    HaloVerifier();
    ~HaloVerifier();
    
    // Initialize FPGA connection
    bool initialize(const std::string& deviceSerial, const std::string& bitfilePath);
    
    // Send data and receive response
    bool sendReceiveData(const std::vector<uint8_t>& inputData, std::vector<uint8_t>& outputData);
    
    // Cleanup
    void cleanup();

private:
    okCFrontPanel* device_;
    bool initialized_;
    static const size_t BUF_LEN;
    
    // Helper functions
    bool configureFpga(const std::string& bitfilePath);
    void resetFifo();
    bool writeToFpga(const std::vector<uint8_t>& data);
    bool readFromFpga(std::vector<uint8_t>& data);
};

#endif // HALO_VERIFIER_H
