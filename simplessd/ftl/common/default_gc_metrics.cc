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

#include "ftl/common/default_gc_metrics.hh"

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
DefaultGCMetrics::DefaultGCMetrics()
    : lastRequestTime(0),
      currentRequestTime(0),
      maxResponseTimes(10000),  // Track up to 10000 response times for accurate percentiles
      metricsEnabled(false),
      metricsFilePath("output/default_page_level_metrics.txt") {
  
  // Initialize statistics
  resetStats();
  
  // Create output directory if metrics are enabled
  #ifdef _WIN32
  // Windows
  if (system("if not exist output mkdir output") != 0) {
    std::cerr << "Warning: Failed to create output directory for Default GC Metrics" << std::endl;
  }
  #else
  // Linux/Unix/MacOS
  if (system("mkdir -p output") != 0) {
    std::cerr << "Warning: Failed to create output directory for Default GC Metrics" << std::endl;
  }
  #endif
  
  // Initialize metrics file
  if (metricsEnabled) {
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# Default Page-Level Mapping Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <valid_copies> <erases> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
    else {
      std::cerr << "Warning: Failed to initialize Default GC metrics file" << std::endl;
    }
  }
}

// Destructor
DefaultGCMetrics::~DefaultGCMetrics() {
  // Output final metrics
  if (metricsEnabled) {
    finalizeMetrics();
  }
  
  // Print final statistics
  printStats();
}

// Record response time for latency calculations
void DefaultGCMetrics::recordResponseTime(uint64_t responseTime) {
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
  
  // Calculate average response time using direct calculation
  // instead of running average formula
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

// Record GC invocation statistics
void DefaultGCMetrics::recordGCInvocation(uint32_t copiedPages, uint32_t validCopies) {
  stats.gcInvocations++;
  stats.totalPageCopies += copiedPages;
  stats.validPageCopies += validCopies;
  
  // Optional: Output metrics on GC invocation
  if (metricsEnabled && (stats.gcInvocations % 10 == 0)) {
    outputMetricsToFile();
  }
}

// Record block erase
void DefaultGCMetrics::recordBlockErase() {
  stats.eraseCount++;
}

// Get statistics
void DefaultGCMetrics::getStats(uint64_t &invocations, uint64_t &pageCopies, 
                              uint64_t &validCopies, uint64_t &erases,
                              float &avgResponse) {
  invocations = stats.gcInvocations;
  pageCopies = stats.totalPageCopies;
  validCopies = stats.validPageCopies;
  erases = stats.eraseCount;
  avgResponse = stats.avgResponseTime;
}

// Reset statistics
void DefaultGCMetrics::resetStats() {
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.validPageCopies = 0;
  stats.eraseCount = 0;
  stats.avgResponseTime = 0.0f;
  stats.responseTimeCount = 0;
  
  // Clear response times
  responseTimes.clear();
}

// Print statistics
void DefaultGCMetrics::printStats() const {
  if (stats.responseTimeCount > 0) {
    std::cout << "Default Page-Level GC Metrics Summary:" << std::endl
              << "  GC Invocations: " << stats.gcInvocations << std::endl
              << "  Total Page Copies: " << stats.totalPageCopies << std::endl
              << "  Valid Page Copies: " << stats.validPageCopies << std::endl
              << "  Block Erases: " << stats.eraseCount << std::endl
              << "  Average Response Time: " << stats.avgResponseTime << " ns" << std::endl;
              
    // Only calculate percentiles if we have enough data
    if (responseTimes.size() >= 100) {
      std::cout << "  P99 Latency: " << getLatencyPercentile(0.99) << " ns" << std::endl
                << "  P99.9 Latency: " << getLatencyPercentile(0.999) << " ns" << std::endl
                << "  P99.99 Latency: " << getLatencyPercentile(0.9999) << " ns" << std::endl;
    }
    else {
      std::cout << "  Not enough samples for tail latency calculation" << std::endl;
    }
  }
  else {
    std::cout << "Default Page-Level GC Metrics: No data collected" << std::endl;
  }
}

// Output metrics to file
void DefaultGCMetrics::outputMetricsToFile() {
  if (!metricsEnabled || stats.responseTimeCount == 0) {
    return;
  }
  
  std::ofstream metricsFile(metricsFilePath, std::ios::app);
  if (metricsFile.is_open()) {
    // Get current timestamp
    uint64_t timestamp = currentRequestTime > 0 ? currentRequestTime : lastRequestTime;
    
    // Calculate percentiles if we have enough data
    uint64_t p99 = responseTimes.size() >= 100 ? getLatencyPercentile(0.99) : 0;
    uint64_t p999 = responseTimes.size() >= 1000 ? getLatencyPercentile(0.999) : 0;
    uint64_t p9999 = responseTimes.size() >= 10000 ? getLatencyPercentile(0.9999) : 0;
    
    // Write metrics line
    metricsFile << timestamp << " "
                << stats.gcInvocations << " "
                << stats.totalPageCopies << " "
                << stats.validPageCopies << " "
                << stats.eraseCount << " "
                << stats.avgResponseTime << " "
                << p99 << " "
                << p999 << " "
                << p9999 << std::endl;
    
    metricsFile.close();
  }
  else {
    std::cerr << "Warning: Failed to open Default GC metrics file for writing" << std::endl;
  }
}

// Finalize metrics - creates a comprehensive summary report
void DefaultGCMetrics::finalizeMetrics() {
  if (!metricsEnabled) {
    return;
  }
  
  // First output the current metrics to the regular file
  outputMetricsToFile();
  
  // Now create a summary file - derive from the metrics file path
  // Extract directory from metrics file path
  size_t lastSlash = metricsFilePath.find_last_of("/\\");
  std::string directory;
  
  if (lastSlash != std::string::npos) {
    directory = metricsFilePath.substr(0, lastSlash + 1);
  }
  else {
    directory = "";
  }
  
  // Get the base filename without extension from metrics path
  std::string baseFilename = metricsFilePath;
  size_t lastDot = baseFilename.find_last_of(".");
  
  if (lastDot != std::string::npos && lastDot > lastSlash) {
    baseFilename = baseFilename.substr(0, lastDot);
  }
  
  // Create summary path by replacing "metrics" with "summary" if present
  std::string summaryPath;
  size_t metricsPos = baseFilename.find("metrics");
  
  if (metricsPos != std::string::npos) {
    summaryPath = baseFilename.substr(0, metricsPos) + "summary.txt";
  }
  else {
    // If "metrics" not found, just append "_summary" to the base filename
    summaryPath = baseFilename + "_summary.txt";
  }
  
  std::ofstream summaryFile(summaryPath, std::ios::trunc);
  
  if (summaryFile.is_open()) {
    summaryFile << "# Default Page-Level GC Summary" << std::endl;
    summaryFile << "# Generated at: " << currentRequestTime << " ns" << std::endl;
    summaryFile << std::endl;
    
    // Basic statistics
    summaryFile << "## Basic Statistics" << std::endl;
    summaryFile << "GC Invocations: " << stats.gcInvocations << std::endl;
    summaryFile << "Total Page Copies: " << stats.totalPageCopies << std::endl;
    summaryFile << "Valid Page Copies: " << stats.validPageCopies << std::endl;
    summaryFile << "Block Erases: " << stats.eraseCount << std::endl;
    summaryFile << "Total I/O Operations: " << stats.responseTimeCount << std::endl;
    summaryFile << std::endl;
    
    // Response time statistics
    summaryFile << "## Response Time Statistics" << std::endl;
    summaryFile << "Average Response Time: " << stats.avgResponseTime << " ns" << std::endl;
    
    // Only calculate percentiles if we have enough data
    if (responseTimes.size() >= 100) {
      summaryFile << "Minimum Response Time: " << *std::min_element(responseTimes.begin(), responseTimes.end()) << " ns" << std::endl;
      summaryFile << "Maximum Response Time: " << *std::max_element(responseTimes.begin(), responseTimes.end()) << " ns" << std::endl;
      // summaryFile << "P50 (Median) Latency: " << getLatencyPercentile(0.5) << " ns" << std::endl;
      // summaryFile << "P90 Latency: " << getLatencyPercentile(0.9) << " ns" << std::endl;
      // summaryFile << "P95 Latency: " << getLatencyPercentile(0.95) << " ns" << std::endl;
      summaryFile << "P99 Latency: " << getLatencyPercentile(0.99) << " ns" << std::endl;
      summaryFile << "P99.9 Latency: " << getLatencyPercentile(0.999) << " ns" << std::endl;
      summaryFile << "P99.99 Latency: " << getLatencyPercentile(0.9999) << " ns" << std::endl;
    }
    else {
      summaryFile << "Not enough samples for latency percentile calculation" << std::endl;
    }
    
    summaryFile << std::endl;
    
    // GC efficiency
    summaryFile << "## GC Efficiency" << std::endl;
    if (stats.gcInvocations > 0) {
      summaryFile << "Average Pages Copied per GC: " 
                  << (float)stats.totalPageCopies / stats.gcInvocations << std::endl;
      summaryFile << "Average Valid Pages Copied per GC: " 
                  << (float)stats.validPageCopies / stats.gcInvocations << std::endl;
      summaryFile << "Average Blocks Erased per GC: " 
                  << (float)stats.eraseCount / stats.gcInvocations << std::endl;
    }
    else {
      summaryFile << "No GC operations performed" << std::endl;
    }
    
    summaryFile.close();
  }
  else {
    std::cerr << "Warning: Failed to create Default GC summary file" << std::endl;
  }
}

// Set metrics file path - use as is without adding prefix
void DefaultGCMetrics::setMetricsFilePath(const std::string &basePath) {
  // Just use the path directly, since the prefix is already added by the caller
  metricsFilePath = basePath;
  
  // Extract directory for creating summary file later
  size_t lastSlash = basePath.find_last_of("/\\");
  std::string directory;
  
  if (lastSlash != std::string::npos) {
    directory = basePath.substr(0, lastSlash + 1);
  }
  else {
    directory = "";
  }
  
  // If metrics are already enabled, initialize the file
  if (metricsEnabled) {
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# Default Page-Level Mapping Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <valid_copies> <erases> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
  }
}

// Calculate latency percentile
uint64_t DefaultGCMetrics::getLatencyPercentile(float percentile) const {
  if (responseTimes.empty()) {
    return 0;
  }
  
  // Copy the response times to a vector for sorting
  std::vector<uint64_t> sortedTimes(responseTimes.begin(), responseTimes.end());
  
  // Sort the vector
  std::sort(sortedTimes.begin(), sortedTimes.end());
  
  // Convert percentile to 0-1 range if necessary
  float normalizedPercentile = percentile;
  if (percentile > 1.0) {
    normalizedPercentile = percentile / 100.0f;
  }
  
  // For very high percentiles, use linear interpolation for more precise values
  float position = (sortedTimes.size() - 1) * normalizedPercentile;
  size_t idx = static_cast<size_t>(position);
  
  // Ensure we don't go out of bounds
  if (idx >= sortedTimes.size() - 1) {
    return sortedTimes.back();
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
