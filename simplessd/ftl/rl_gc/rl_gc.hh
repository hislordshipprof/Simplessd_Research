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

#ifndef __SIMPLESSD_FTL_RL_GC_HH__
#define __SIMPLESSD_FTL_RL_GC_HH__

#include <vector>
#include <deque>
#include <cstdint>
#include "ftl/rl_gc/q_table.hh"

namespace SimpleSSD {

namespace FTL {

/**
 * Reinforcement Learning-based Garbage Collection Controller
 */
class RLGarbageCollector {
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
  
  // Stats
  struct {
    uint64_t gcInvocations;       // Number of GC invocations
    uint64_t totalPageCopies;     // Total pages copied during GC
    uint64_t intensiveGCCount;    // Number of intensive GCs triggered
    float avgReward;              // Average reward
    uint64_t rewardCount;         // Number of rewards received
  } stats;
  
  // Debug output
  bool debugEnabled;
  std::string debugFilePath;
  
  // Helper functions
  uint32_t discretizePrevInterval(uint64_t interval);
  uint32_t discretizeCurrInterval(uint64_t interval);
  uint32_t discretizeAction(uint32_t action);
  void updatePercentileThresholds();
  float calculateReward(uint64_t responseTime);
  
 public:
  RLGarbageCollector(uint32_t tgc, uint32_t tigc, uint32_t maxCopies,
                    float alpha, float gamma, float epsilon, uint32_t actions);
  ~RLGarbageCollector();
  
  // Core methods
  bool shouldTriggerGC(uint32_t freeBlocks, uint64_t currentTime);
  uint32_t getGCAction(uint32_t freeBlocks);
  uint32_t getMaxGCAction(); // Return maximum action for aggressive policy
  void updateState(uint64_t currentTime);
  void recordResponseTime(uint64_t responseTime);
  float updateQValue(uint64_t responseTime);
  bool shouldPerformIntensiveGC(uint32_t freeBlocks);
  void recordGCInvocation(uint32_t copiedPages);
  void recordIntensiveGC();  // New method to track intensive GC operations
  
  // Getters
  uint32_t getTGCThreshold() const;
  uint32_t getTIGCThreshold() const;
  uint32_t getMaxPageCopies() const;
  const State& getCurrentState() const;
  
  // Pending update methods
  bool hasPendingQValueUpdate() const { return hasPendingUpdate; }
  void schedulePendingUpdate(State state, uint32_t action);
  float processPendingUpdate(uint64_t responseTime);
  
  // Statistics
  void getStats(uint64_t &invocations, uint64_t &pageCopies,
               uint64_t &intensiveGCs, float &avgReward);
  void resetStats();

  // Debug helper
  void printDebugInfo() const;

  // Debug control
  void enableDebug(bool enable) { debugEnabled = enable; }
  void setDebugFilePath(const std::string &path) { debugFilePath = path; }
  bool isDebugEnabled() const { return debugEnabled; }
  const std::string& getDebugFilePath() const { return debugFilePath; }
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif  // __SIMPLESSD_FTL_RL_GC_HH__
