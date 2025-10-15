// We partition the pipeline into two parts:
// - reader: XEM7310 (Opal Kelly) for Intan RHX device
// - interface: XEM6310 (Opal Kelly) for seizure detection SDK
// This is essential since both drivers cannot run on the same 
// virtual environment. Their object files have symbol linking 
// conflicts.
// - log: data logger interface;
// we sample the detections given seizure outcomes.
// - Intan Visualizer: we modify the intan visualizer to 
// allow (advanced window) pipelined mode to handle the data
// supplied from external sources like this pipeline. Thus,
// one can enable/disable the visualizer based on the needs.

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <algorithm>

#include "intan-reader/intan_reader.h"
#include "intan-reader/shared_memory_reader.h"
#include "asic-sender/asic_sender.h"
#include "data-analyser/src/core/fpga_logger.h"
#include "data-analyser/src/core/halo_response_decoder.h"

int main(int /* argc */, char* /* argv */[]) {
    std::cout << "Testing Pipeline - Main Entry Point" << std::endl;
    std::cout << "Starting Intan RHX Device Reader..." << std::endl;
    
    try {
        // Create and initialize the reader
        IntanReader reader;
        if (!reader.initialize()) {
            std::cerr << "Failed to initialize Intan Reader." << std::endl;
            return -1;
        }
        
        // Create and initialize the ASIC sender (optional)
        AsicSender asicSender;
        bool asicInitialized = asicSender.initialize("2437001CWG", "bitstreams/seizure_pipe_test0.bit");
        if (!asicInitialized) {
            std::cerr << "Warning: ASIC Sender not available, continuing without FPGA processing." << std::endl;
        }
        
        // Create shared memory reader for real Intan data
        SharedMemoryReader sharedMemoryReader;
        if (!sharedMemoryReader.initialize()) {
            std::cerr << "ERROR: Failed to initialize shared memory reader for real Intan data!" << std::endl;
            std::cerr << "Pipeline cannot proceed without real neural data. Exiting." << std::endl;
            return -1;
        }
        
        // Create and initialize the FPGA logger only if ASIC is available
        std::unique_ptr<FpgaLogger> fpgaLogger;
        if (asicInitialized) {
            fpgaLogger = std::make_unique<FpgaLogger>();
            // Connect the FPGA logger to the ASIC sender
            asicSender.setDataAnalyzer(fpgaLogger.get());
            
            // Configure FPGA for real analysis (instead of test)
            std::cout << "Configuring FPGA for seizure detection analysis..." << std::endl;
            
            // Configure pipeline 6 (NEO -> THR -> GATE) for seizure detection
            if (!asicSender.configurePipeline(6)) {
                std::cerr << "Warning: Failed to configure FPGA pipeline" << std::endl;
            }
            
            // Enable analysis mode
            if (!asicSender.enableAnalysisMode()) {
                std::cerr << "Warning: Failed to enable FPGA analysis mode" << std::endl;
            }
            
            // Disable test pattern generation
            if (!asicSender.disableTestPattern()) {
                std::cerr << "Warning: Failed to disable FPGA test pattern" << std::endl;
            }
            
            // Set seizure detection thresholds
            if (!asicSender.setThresholds(0.3, 0.7)) {
                std::cerr << "Warning: Failed to set FPGA thresholds" << std::endl;
            }
            
            // Configure the decoder to match FPGA pipeline
            fpgaLogger->setHaloPipeline(HaloPipeline::PIPELINE_6);
            fpgaLogger->setThresholds(0.3, 0.7);
            
            std::cout << "FPGA configured for real-time seizure detection analysis" << std::endl;
        }
        
        // Start data acquisition
        if (!reader.start()) {
            std::cerr << "Failed to start data acquisition." << std::endl;
            return -1;
        }
        
        // Start ASIC sender only if initialized
        std::thread asicThread;
        if (asicInitialized) {
            asicSender.startSending();
            
            // Create ASIC sender thread
            asicThread = std::thread([&asicSender, &sharedMemoryReader]() {
                std::vector<uint8_t> waveformData;
                bool hasReceivedData = false;
                int noDataCount = 0;
                const int MAX_NO_DATA_COUNT = 50; // 5 seconds at 100ms intervals
                
                while (asicSender.isRunning()) {
                    // Try to read real data from Intan device
                    if (sharedMemoryReader.readLatestData(waveformData)) {
                        if (!hasReceivedData) {
                            std::cout << "Now sending REAL neural data from Intan device to ASIC!" << std::endl;
                            hasReceivedData = true;
                        }
                        asicSender.sendWaveformData(waveformData);
                        noDataCount = 0; // Reset counter
                    } else {
                        noDataCount++;
                        
                        // If we've been waiting too long for data, halt the pipeline
                        if (noDataCount >= MAX_NO_DATA_COUNT) {
                            std::cerr << "ERROR: No real neural data received from Intan device for 5 seconds!" << std::endl;
                            std::cerr << "Pipeline cannot proceed without real data. Halting." << std::endl;
                            asicSender.stopSending();
                            return;
                        }
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Check every 100ms
                }
            });
        }
        
        // Main loop - wait for reader to finish
        while (reader.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Stop ASIC sender and wait for thread
        if (asicInitialized) {
            asicSender.stopSending();
        }
        if (asicThread.joinable()) {
            asicThread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return -1;
    }
    
    return 0;
}
