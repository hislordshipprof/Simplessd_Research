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

#ifndef __SIMPLESSD_FTL_RL_AGGRESSIVE_GC_HH__
#define __SIMPLESSD_FTL_RL_AGGRESSIVE_GC_HH__

#include <vector>
#include <deque>
#include <cstdint>
#include "ftl/rl_baseline_gc/q_table.hh"
#include "util/simplessd.hh"  // For ConfigReader

namespace SimpleSSD {

namespace FTL {

/**
 * Reinforcement Learning-based Aggressive Garbage Collection Controller
 * Extends the basic RL-GC with aggressive policies to reduce long-tail latency
 */
class RLAggressiveGarbageCollector {
 private:
  // Q-learning components
  QTable qTable;
  
  // Current state tracking
  State currentState;
  State previousState;
  uint32_t lastAction;
  
  // Time tracking
  uint64_t lastRequestTime;       // Time of previous request
  uint64_t currentRequestTime;    // Time of current request
  uint64_t prevInterRequestTime;  // Previous inter-request interval
  uint64_t currInterRequestTime;  // Current inter-request interval
  
  // Response time tracking for reward calculation
  std::deque<uint64_t> responseTimes;  // Recent response times for percentile calculation
  uint32_t maxResponseTimes;           // Maximum number of response times to track
  
  // Thresholds for reward calculation (percentiles)
  uint64_t t1Threshold;  // 70th percentile
  uint64_t t2Threshold;  // 90th percentile
  uint64_t t3Threshold;  // 99th percentile
  
  // GC parameters
  uint32_t tgcThreshold;     // Free block threshold for GC triggering
  uint32_t tigcThreshold;    // Free block threshold for intensive GC
  uint32_t maxPageCopies;    // Maximum page copies per action
  
  // Pending Q-value update tracking
  bool hasPendingUpdate;     // Flag to indicate if there's a pending Q-value update
  State pendingState;        // State for which the update is pending
  uint32_t pendingAction;    // Action for which the update is pending
  
  // Intensive GC mode tracking
  bool inIntensiveMode;          // Flag indicating if we're in intensive GC mode
  uint32_t intensiveGCMaxPageCopies; // Number of pages to copy during intensive GC

  // Aggressive GC specific parameters
  uint32_t tagcThreshold;        // TAGC threshold (100) for aggressive early GC triggering
  uint32_t maxGCOps;             // Maximum GC operations (2) when between TAGC and TGC
  bool readTriggeredGCEnabled;   // Flag to enable read-triggered GC
  float earlyGCInvalidThreshold; // Threshold for invalid pages percentage in early GC (0.6)

  // Stats
  struct Stats {
    uint64_t gcInvocations;
    uint64_t totalPageCopies;
    uint64_t intensiveGCCount;
    uint64_t readTriggeredGCCount;
    uint64_t earlyGCCount;
    uint64_t eraseCount;
    float avgReward;
    uint64_t rewardCount;
    double avgResponseTime;  // Changed from float to double for better precision
    uint64_t responseTimeCount;
  } stats;
  
  // Debug output
  bool debugEnabled;
  std::string debugFilePath;
  
  // Metrics output
  bool metricsEnabled;
  std::string metricsFilePath;
  
  // Helper functions
  uint32_t discretizePrevInterval(uint64_t interval);
  uint32_t discretizeCurrInterval(uint64_t interval);
  uint32_t discretizeAction(uint32_t action);
  void updatePercentileThresholds();
  float calculateReward(uint64_t responseTime);
  uint64_t getLatencyPercentile(float percentile) const;
  
 public:
  RLAggressiveGarbageCollector(uint32_t tgc, uint32_t tigc, uint32_t maxCopies,
                    float alpha, float gamma, float epsilon, uint32_t actions);
  ~RLAggressiveGarbageCollector();
  
  // Core methods
  bool shouldTriggerGC(uint32_t freeBlocks, uint64_t currentTime);
  bool shouldTriggerGCForRead(uint32_t freeBlocks, uint64_t currentTime);  // New method for read-triggered GC
  uint32_t getGCAction(uint32_t freeBlocks);
  uint32_t getMaxGCAction();  // Return maximum action
  void updateState(uint64_t currentTime);
  void recordResponseTime(uint64_t responseTime);
  float updateQValue(uint64_t responseTime);
  bool shouldPerformIntensiveGC(uint32_t freeBlocks);
  bool shouldExitIntensiveMode(uint32_t freeBlocks);
  void setIntensiveMode(bool enable);
  bool isInIntensiveMode() const;
  void recordGCInvocation(uint32_t copiedPages);
  void recordIntensiveGC();
  void recordReadTriggeredGC();  // Track read-triggered GC operations
  void recordEarlyGC();          // Track early GC operations
  void recordBlockErase();       // Track block erasures
  
  // Getters
  uint32_t getTGCThreshold() const;
  uint32_t getTIGCThreshold() const;
  uint32_t getMaxPageCopies() const;
  uint32_t getTAGCThreshold() const;  // Get TAGC threshold (100)
  uint32_t getMaxGCOps() const;       // Get max GC operations (2)
  uint32_t getMaxLimitedGCThreshold() const { return tagcThreshold; } // Backward compatibility
  const State& getCurrentState() const;
  
  // Pending update methods
  bool hasPendingQValueUpdate() const { return hasPendingUpdate; }
  void schedulePendingUpdate(State state, uint32_t action);
  float processPendingUpdate(uint64_t responseTime);
  
  // Aggressive GC specific methods
  void enableReadTriggeredGC(bool enable) { readTriggeredGCEnabled = enable; }
  bool isReadTriggeredGCEnabled() const { return readTriggeredGCEnabled; }
  void setTAGCThreshold(uint32_t threshold) { tagcThreshold = threshold; }
  void setMaxGCOps(uint32_t ops) { maxGCOps = ops; }
  bool isEarlyGC(uint32_t freeBlocks) const { return freeBlocks > tgcThreshold && freeBlocks <= tagcThreshold; }
  float getEarlyGCInvalidThreshold() const { return earlyGCInvalidThreshold; }
  void setEarlyGCInvalidThreshold(float threshold) { earlyGCInvalidThreshold = threshold; }
  
  // Configuration setup
  void setup(ConfigReader &cfg);
  
  // Statistics
  void getStats(uint64_t &invocations, uint64_t &pageCopies, uint64_t &intensiveGCs, 
               uint64_t &readTriggeredGCs, uint64_t &earlyGCs, float &avgReward, uint64_t &erases);
  void resetStats();

  // Debug helper
  void printDebugInfo() const;

  // Debug control
  void enableDebug(bool enable) { debugEnabled = enable; }
  void setDebugFilePath(const std::string &path) { debugFilePath = path; }
  bool isDebugEnabled() const { return debugEnabled; }
  const std::string& getDebugFilePath() const { return debugFilePath; }
  
  // Metrics control
  void enableMetrics(bool enable) { metricsEnabled = enable; }
  bool isMetricsEnabled() const { return metricsEnabled; }
  void setMetricsFilePath(const std::string &basePath);
  void outputMetricsToFile();
  void finalizeMetrics();
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif  // __SIMPLESSD_FTL_RL_AGGRESSIVE_GC_HH__ 