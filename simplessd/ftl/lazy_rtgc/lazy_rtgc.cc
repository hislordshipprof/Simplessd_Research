/*
 * Copyright (C) 2023 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ftl/lazy_rtgc/lazy_rtgc.hh"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>  // For formatting output
#include <fstream>  // For file output
#include <sstream>  // For string stream operations
#include <sys/stat.h>  // For directory creation

namespace SimpleSSD {

namespace FTL {

// Constructor
LazyRTGC::LazyRTGC(uint32_t gcThresh, uint32_t maxCopies)
    : gcThreshold(gcThresh),
      maxPageCopiesPerGC(maxCopies),
      lastRequestTime(0),
      currentRequestTime(0),
      maxResponseTimes(1000),  // Track up to 1000 response times for consistency with other policies
      metricsEnabled(false),
      metricsFilePath("output/lazy_rtgc_metrics.txt") {
  
  // Initialize statistics
  resetStats();
  
  // Create output directory if metrics are enabled
  if (metricsEnabled) {
    // Ensure the output directory exists
    #ifdef _WIN32
    // Windows
    if (system("if not exist output mkdir output") != 0) {
      std::cerr << "Warning: Failed to create output directory for Lazy-RTGC" << std::endl;
    }
    #else
    // Linux/Unix/MacOS
    if (system("mkdir -p output") != 0) {
      std::cerr << "Warning: Failed to create output directory for Lazy-RTGC" << std::endl;
    }
    #endif
    
    // Initialize metrics file
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# Lazy-RTGC Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <valid_copies> <erases> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
    else {
      std::cerr << "Warning: Failed to initialize Lazy-RTGC metrics file" << std::endl;
    }
  }
  
  std::cout << "Lazy-RTGC initialized with parameters:" << std::endl
            << "  GC Threshold: " << gcThresh << std::endl
            << "  Max Page Copies per GC: " << maxCopies << std::endl;
}

// Destructor
LazyRTGC::~LazyRTGC() {
  // Output final metrics
  if (metricsEnabled) {
    outputMetricsToFile();
  }
  
  // Print final statistics
  printStats();
}

// Check if GC should be triggered
bool LazyRTGC::shouldTriggerGC(uint32_t freeBlocks) {
  // According to Lazy-RTGC paper as described in our research work,
  // GC is only triggered when free blocks are below threshold
  // Note: The write-operation check is handled in PageMapping
  
  // Check if free blocks are below threshold
  if (freeBlocks <= gcThreshold) {
    return true;
  }
  
  return false;
}

// Get the number of pages to copy in this GC operation
uint32_t LazyRTGC::getMaxPageCopies() const {
  // According to our research paper on Lazy-RTGC, we use a fixed
  // number of page copies per GC operation to provide bounded
  // response time, which helps to reduce long-tail latency
  return maxPageCopiesPerGC;
}

// Update read latency statistics
void LazyRTGC::updateReadLatencyStats(uint64_t responseTime) {
  // Safety check - ignore unreasonably large values
  if (responseTime > UINT64_MAX / 2) {
    return;
  }
  
  // Add to response time history
  responseTimes.push_back(responseTime);
  
  // Keep history size limited
  while (responseTimes.size() > maxResponseTimes) {
    responseTimes.pop_front();
  }
  
  // Calculate average response time safely
  double sum = 0.0;
  for (uint64_t time : responseTimes) {
    sum += static_cast<double>(time);
  }
  stats.avgResponseTime = sum / responseTimes.size();
  
  // Safety check - if the average is unreasonably large, cap it
  if (stats.avgResponseTime > 1e16) {
    stats.avgResponseTime = std::accumulate(responseTimes.begin(), 
                                          responseTimes.begin() + std::min(static_cast<size_t>(100), responseTimes.size()), 
                                          0.0) / std::min(static_cast<size_t>(100), responseTimes.size());
  }
  stats.responseTimeCount++;
  
  // If metrics are enabled and we've collected enough samples, output metrics periodically
  if (metricsEnabled && (stats.responseTimeCount % 1000 == 0)) {
    outputMetricsToFile();
  }
}

// Update write latency statistics
void LazyRTGC::updateWriteLatencyStats(uint64_t responseTime) {
  // Safety check - ignore unreasonably large values
  if (responseTime > UINT64_MAX / 2) {
    return;
  }
  
  // Add to response time history
  responseTimes.push_back(responseTime);
  
  // Keep history size limited
  while (responseTimes.size() > maxResponseTimes) {
    responseTimes.pop_front();
  }
  
  // Calculate average response time safely
  double sum = 0.0;
  for (uint64_t time : responseTimes) {
    sum += static_cast<double>(time);
  }
  stats.avgResponseTime = sum / responseTimes.size();
  
  // Safety check - if the average is unreasonably large, cap it
  if (stats.avgResponseTime > 1e16) {
    stats.avgResponseTime = std::accumulate(responseTimes.begin(), 
                                          responseTimes.begin() + std::min(static_cast<size_t>(100), responseTimes.size()), 
                                          0.0) / std::min(static_cast<size_t>(100), responseTimes.size());
  }
  stats.responseTimeCount++;
}

// Record GC invocation statistics
void LazyRTGC::recordGCInvocation(uint32_t copiedPages) {
  stats.gcInvocations++;
  stats.totalPageCopies += copiedPages;
}

// Record block erase
void LazyRTGC::recordBlockErase() {
  stats.eraseCount++;
}

// Get GC threshold
uint32_t LazyRTGC::getGCThreshold() const {
  return gcThreshold;
}

// Get maximum page copies per GC operation
uint32_t LazyRTGC::getMaxPageCopiesPerGC() const {
  return maxPageCopiesPerGC;
}

// Get statistics
void LazyRTGC::getStats(uint64_t &invocations, uint64_t &pageCopies, 
                      uint64_t &validCopies, uint64_t &erases,
                      float &avgResponse) {
  invocations = stats.gcInvocations;
  pageCopies = stats.totalPageCopies;
  validCopies = stats.validPageCopies;
  erases = stats.eraseCount;
  
  // Calculate average response time on-demand
  avgResponse = 0.0f;
  if (!responseTimes.empty()) {
    double sum = 0.0;
    for (uint64_t time : responseTimes) {
      sum += static_cast<double>(time);
    }
    avgResponse = static_cast<float>(sum / responseTimes.size());
    
    // Safety check for unreasonable values
    if (avgResponse > 1e16f) {
      avgResponse = static_cast<float>(std::accumulate(responseTimes.begin(), 
                                                    responseTimes.begin() + std::min(static_cast<size_t>(100), responseTimes.size()), 
                                                    0.0) / std::min(static_cast<size_t>(100), responseTimes.size()));
    }
  }
}

// Reset statistics
void LazyRTGC::resetStats() {
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.validPageCopies = 0;
  stats.eraseCount = 0;
  stats.responseTimeCount = 0;
  
  // Clear response times
  responseTimes.clear();
}

// Print statistics
void LazyRTGC::printStats() const {
  std::cout << "=== Lazy-RTGC Statistics ===" << std::endl;
  std::cout << "GC Invocations: " << stats.gcInvocations << std::endl;
  std::cout << "Total Page Copies: " << stats.totalPageCopies << std::endl;
  std::cout << "Valid Page Copies: " << stats.validPageCopies << std::endl;
  std::cout << "Block Erases: " << stats.eraseCount << std::endl;
  
  double avgResponseTime = 0.0;
  
  // Calculate average response time if we have data
  if (!responseTimes.empty()) {
    double sum = 0.0;
    for (uint64_t time : responseTimes) {
      sum += static_cast<double>(time);
    }
    avgResponseTime = sum / responseTimes.size();
    
    // Safety check for unreasonable values
    if (avgResponseTime > 1e16) {
      avgResponseTime = std::accumulate(responseTimes.begin(), 
                                      responseTimes.begin() + std::min(static_cast<size_t>(100), responseTimes.size()), 
                                      0.0) / std::min(static_cast<size_t>(100), responseTimes.size());
    }
  }
  
  std::cout << "Average Response Time: " << std::fixed << std::setprecision(2) 
            << avgResponseTime << " ns" << std::endl;
  
  // Calculate and print percentiles if we have enough data
  if (responseTimes.size() >= 100) {
    std::cout << "P99 Latency: " << getLatencyPercentile(99.0f) << " ns" << std::endl;
    std::cout << "P99.9 Latency: " << getLatencyPercentile(99.9f) << " ns" << std::endl;
    std::cout << "P99.99 Latency: " << getLatencyPercentile(99.99f) << " ns" << std::endl;
  }
  std::cout << "===========================" << std::endl;
}

// Output metrics to file
void LazyRTGC::outputMetricsToFile() {
  if (!metricsEnabled) {
    return;
  }
  
  std::ofstream metricsFile(metricsFilePath, std::ios::app);
  if (metricsFile.is_open()) {
    // Calculate percentiles
    uint64_t p99 = 0;
    uint64_t p999 = 0;
    uint64_t p9999 = 0;
    double avgResponseTime = 0.0;
    
    if (responseTimes.size() >= 100) {
      p99 = getLatencyPercentile(99.0f);
      p999 = getLatencyPercentile(99.9f);
      p9999 = getLatencyPercentile(99.99f);
      
      // Calculate average response time safely
      double sum = 0.0;
      for (uint64_t time : responseTimes) {
        // Convert to double to avoid uint64_t overflow
        sum += static_cast<double>(time);
      }
      avgResponseTime = sum / responseTimes.size();
      
      // Safety check - if the average is unreasonably large, cap it
      if (avgResponseTime > 1e16) {
        avgResponseTime = std::accumulate(responseTimes.begin(), 
                                       responseTimes.begin() + std::min(static_cast<size_t>(100), responseTimes.size()), 
                                       0.0) / std::min(static_cast<size_t>(100), responseTimes.size());
      }
    }
    
    // Write metrics line
    metricsFile << currentRequestTime << " "
                << stats.gcInvocations << " "
                << stats.totalPageCopies << " "
                << 0 << " " // Placeholder for valid_copies
                << stats.eraseCount << " "
                << std::fixed << std::setprecision(2) << avgResponseTime << " "
                << p99 << " "
                << p999 << " "
                << p9999 << std::endl;
    
    metricsFile.close();
  }
}

// Finalize metrics - creates a comprehensive summary report
void LazyRTGC::finalizeMetrics() {
  if (!metricsEnabled) {
    return;
  }
  
  // First, make sure to output the latest metrics
  outputMetricsToFile();
  
  // Create a summary file
  std::string summaryPath = metricsFilePath.substr(0, metricsFilePath.find("_metrics.txt")) + "_summary.txt";
  std::ofstream summaryFile(summaryPath, std::ios::trunc);
  
  if (summaryFile.is_open()) {
    // Calculate percentiles for final reporting
    uint64_t p99 = 0;
    uint64_t p999 = 0;
    uint64_t p9999 = 0;
    double avgResponseTime = 0.0;
    
    if (responseTimes.size() >= 100) {
      p99 = getLatencyPercentile(99.0f);
      p999 = getLatencyPercentile(99.9f);
      p9999 = getLatencyPercentile(99.99f);
      
      // Calculate average response time safely
      double sum = 0.0;
      for (uint64_t time : responseTimes) {
        // Convert to double to avoid uint64_t overflow
        sum += static_cast<double>(time);
      }
      avgResponseTime = sum / responseTimes.size();
      
      // Safety check - if the average is unreasonably large, cap it
      if (avgResponseTime > 1e16) {
        avgResponseTime = std::accumulate(responseTimes.begin(), 
                                       responseTimes.begin() + std::min(static_cast<size_t>(100), responseTimes.size()), 
                                       0.0) / std::min(static_cast<size_t>(100), responseTimes.size());
      }
    }
    
    // Write summary header
    summaryFile << "Lazy-RTGC Policy Summary Report" << std::endl;
    summaryFile << "===========================" << std::endl;
    summaryFile << std::endl;
    
    // Write simulation parameters
    summaryFile << "Simulation Parameters:" << std::endl;
    summaryFile << "---------------------" << std::endl;
    summaryFile << "GC Threshold: " << gcThreshold << " free blocks" << std::endl;
    summaryFile << "Max Page Copies per GC: " << maxPageCopiesPerGC << " pages" << std::endl;
    summaryFile << std::endl;
    
    // Write GC statistics
    summaryFile << "GC Statistics:" << std::endl;
    summaryFile << "-------------" << std::endl;
    summaryFile << "Total GC Invocations: " << stats.gcInvocations << std::endl;
    summaryFile << "Total Pages Copied: " << stats.totalPageCopies << std::endl;
    summaryFile << "Valid Pages Copied: " << stats.validPageCopies 
                << " (Note: May not be accurately tracked by LazyRTGC)" << std::endl; 
    summaryFile << "Block Erasures: " << stats.eraseCount << std::endl;
    summaryFile << std::endl;
    
    // Write performance statistics
    summaryFile << "Performance Metrics:" << std::endl;
    summaryFile << "-------------------" << std::endl;
    summaryFile << "Average Response Time: " << std::fixed << std::setprecision(2) << avgResponseTime << " ns" << std::endl;
    summaryFile << "P99 Latency: " << p99 << " ns" << std::endl;
    summaryFile << "P99.9 Latency: " << p999 << " ns" << std::endl;
    summaryFile << "P99.99 Latency: " << p9999 << " ns" << std::endl;
    summaryFile << std::endl;
    
    // Additional metrics that are relevant according to the research paper
    summaryFile << "Efficiency Metrics:" << std::endl;
    summaryFile << "------------------" << std::endl;
    float avgPagesPerGC = stats.gcInvocations > 0 ? (float)stats.totalPageCopies / stats.gcInvocations : 0;
    summaryFile << "Average Pages Copied per GC: " << std::fixed << std::setprecision(2) << avgPagesPerGC << std::endl;
    summaryFile << "Valid Page Copy Ratio: N/A (See Valid Pages Copied note)" << std::endl; 
    
    summaryFile.close();
    
    std::cout << "Lazy-RTGC summary metrics saved to: " << summaryPath << std::endl;
  }
  else {
    std::cerr << "Warning: Failed to open Lazy-RTGC summary file for writing" << std::endl;
  }
}

// Set metrics file path with prefix
void LazyRTGC::setMetricsFilePath(const std::string &basePath) {
  metricsFilePath = basePath + "_metrics.txt";
  
  // Initialize metrics file
  if (metricsEnabled) {
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# Lazy-RTGC Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <valid_copies(placeholder)> <erases> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
  }
}

// Calculate latency percentile
uint64_t LazyRTGC::getLatencyPercentile(float percentile) const {
  if (responseTimes.empty()) {
    return 0;
  }
  
  // Make a copy and sort
  std::vector<uint64_t> sortedTimes(responseTimes.begin(), responseTimes.end());
  std::sort(sortedTimes.begin(), sortedTimes.end());
  
  // Normalize percentile to 0.0-1.0 range
  float normalizedPercentile = percentile / 100.0f;
  
  // Calculate the exact position for precise interpolation
  float position = (sortedTimes.size() - 1) * normalizedPercentile;
  size_t idx = static_cast<size_t>(position);
  
  // Ensure we don't go out of bounds
  if (idx >= sortedTimes.size() - 1) {
    return sortedTimes.back(); // Return the maximum value
  }
  
  // Use linear interpolation for fractional positions
  float fraction = position - idx;
  if (fraction > 0 && idx < sortedTimes.size() - 1) {
    return static_cast<uint64_t>(sortedTimes[idx] * (1 - fraction) + 
                               sortedTimes[idx + 1] * fraction);
  }
  
  return sortedTimes[idx];
}

}  // namespace FTL

}  // namespace SimpleSSD
