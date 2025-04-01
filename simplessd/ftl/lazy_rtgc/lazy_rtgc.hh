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

#ifndef __SIMPLESSD_FTL_LAZY_RTGC_HH__
#define __SIMPLESSD_FTL_LAZY_RTGC_HH__

#include <deque>
#include <vector>
#include <cstdint>
#include <string>
#include <fstream>

namespace SimpleSSD {

namespace FTL {

/**
 * Lazy-RTGC (Real-Time Garbage Collection) Controller
 * Based on the approach described in the Lazy-RTGC paper
 */
class LazyRTGC {
 private:
  // GC parameters
  uint32_t gcThreshold;         // Free block threshold for GC triggering
  uint32_t maxPageCopiesPerGC;  // Maximum page copies per GC operation
  
  // Time tracking
  uint64_t lastRequestTime;     // Time of the last request
  uint64_t currentRequestTime;  // Time of the current request
  
  // Response time tracking for tail latency calculation
  std::deque<uint64_t> responseTimes;  // Recent response times for percentile calculation
  uint32_t maxResponseTimes;           // Maximum number of response times to track
  
  // Statistics
  struct {
    uint64_t gcInvocations;       // Number of GC invocations
    uint64_t totalPageCopies;     // Total pages copied during GC
    uint64_t validPageCopies;     // Valid pages copied during GC
    uint64_t eraseCount;          // Number of block erasures
    uint64_t responseTimeCount;   // Number of response times recorded
    double avgResponseTime;      // Running average response time using EMA
  } stats;
  
  // Metrics output
  bool metricsEnabled;
  std::string metricsFilePath;
  
  // Helper functions
  void updatePercentileThresholds();
  uint64_t getLatencyPercentile(float percentile) const;
  
 public:
  LazyRTGC(uint32_t gcThresh, uint32_t maxCopies);
  ~LazyRTGC();
  
  // Core methods
  bool shouldTriggerGC(uint32_t freeBlocks);
  uint32_t getMaxPageCopies() const;
  void updateReadLatencyStats(uint64_t responseTime);
  void updateWriteLatencyStats(uint64_t responseTime);
  void recordGCInvocation(uint32_t copiedPages);
  void recordBlockErase();
  
  // Getters
  uint32_t getGCThreshold() const;
  uint32_t getMaxPageCopiesPerGC() const;
  
  // Statistics
  void getStats(uint64_t &invocations, uint64_t &pageCopies, 
                uint64_t &validCopies, uint64_t &erases,
                float &avgResponse);
  void resetStats();
  void printStats() const;
  void outputMetricsToFile();
  
  // Metrics control
  void enableMetrics(bool enable) { metricsEnabled = enable; }
  void setMetricsFilePath(const std::string &basePath);
  bool isMetricsEnabled() const { return metricsEnabled; }
  
  // Metrics finalization
  void finalizeMetrics();
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif  // __SIMPLESSD_FTL_LAZY_RTGC_HH__
