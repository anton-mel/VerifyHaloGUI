#include "halo_verifier.h"
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    
    std::string deviceSerial = "2437001CWG";  // Default from Python code
    std::string bitfilePath = "halo_seizure.bit";   // Use the bitfile in interface folder
    
    // Parse command line arguments
    if (argc >= 2) {
        deviceSerial = argv[1];
    }
    if (argc >= 3) {
        bitfilePath = argv[2];
    }
    
    try {
        // Create Halo Verifier
        HaloVerifier verifier;
        
        // Initialize FPGA connection
        if (!verifier.initialize(deviceSerial, bitfilePath)) {
            std::cerr << "Failed to initialize Halo Verifier" << std::endl;
            return -1;
        }
        
        // Test data - simple zeros as requested
        std::vector<uint8_t> testData(64, 0); // 64 bytes of zeros
        std::vector<uint8_t> responseData;
        
        std::cout << "Sending test data (zeros)..." << std::endl;
        
        // Send and receive data
        if (verifier.sendReceiveData(testData, responseData)) {
            std::cout << "Communication successful!" << std::endl;
            std::cout << "Response size: " << responseData.size() << " bytes" << std::endl;
            
            // Print first few bytes of response
            std::cout << "Response data: ";
            for (size_t i = 0; i < std::min(responseData.size(), size_t(16)); ++i) {
                std::cout << static_cast<int>(responseData[i]) << " ";
            }
            std::cout << std::endl;
        } else {
            std::cerr << "Communication failed" << std::endl;
            return -1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    
    return 0;
}
