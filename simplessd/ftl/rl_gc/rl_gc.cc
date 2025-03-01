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

#include "ftl/rl_gc/rl_gc.hh"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>  // For formatting output
#include <fstream>  // For file output
#include <sstream>  // For string stream operations
#include <sys/stat.h>  // For file system operations

namespace SimpleSSD {

namespace FTL {

// More efficient debug logging that keeps file open
class DebugLogger {
private:
  bool enabled;
  std::string filepath;
  std::ofstream file;
  
public:
  DebugLogger(bool enabled, const std::string &filepath) 
    : enabled(enabled), filepath(filepath) {
    if (enabled) {
      // Open file in truncate mode initially
      file.open(filepath, std::ios::trunc);
      if (file.is_open()) {
        file.close();
      }
      
      // Reopen in append mode for ongoing logging
      file.open(filepath, std::ios::app);
      if (!file.is_open()) {
        std::cerr << "Failed to open debug log file: " << filepath << std::endl;
      }
    }
  }
  
  ~DebugLogger() {
    if (enabled && file.is_open()) {
      file.close();
    }
  }
  
  template<typename T>
  void log(const T &message) {
    if (enabled && file.is_open()) {
      file << message << std::endl;
      file.flush();  // Ensure data is written to file
    }
  }
  
  // Add getter for filepath
  const std::string& getFilepath() const {
    return filepath;
  }
};

RLGarbageCollector::RLGarbageCollector(uint32_t tgc, uint32_t tigc, uint32_t maxCopies,
                                     float alpha, float gamma, float epsilon, uint32_t actions)
    : qTable(alpha, gamma, epsilon, actions),
      lastAction(0),
      lastRequestTime(0),
      currentRequestTime(0),
      prevInterRequestTime(0),
      currInterRequestTime(0),
      maxResponseTimes(1000),  // Track up to 1000 response times
      t1Threshold(0),          // Will be calculated dynamically
      t2Threshold(0),
      t3Threshold(0),
      tgcThreshold(tgc),
      tigcThreshold(tigc),
      maxPageCopies(maxCopies),
      hasPendingUpdate(false),
      pendingState(0, 0, 0),
      pendingAction(0),
      debugEnabled(false),
      debugFilePath("output/rl_gc_debug.log"),
      convergenceCounter(0),
      qTableConverged(false),
      responseCounter(0),
      enableRewardLogging(true),  // Enable reward logging by default
      rewardLogFilePath("output/rl_gc_reward_log.csv") {  // Add reward logging file path
    
    // Initialize the debug logger
    logger = new DebugLogger(debugEnabled, debugFilePath);
    
    // Initialize statistics
    stats.gcInvocations = 0;
    stats.totalPageCopies = 0;
    stats.intensiveGCCount = 0;
    stats.avgReward = 0.0f;
    stats.rewardCount = 0;
    
    // Initialize current state
    currentState = State(0, 0, 0);
    
    // Initialize reward tracking for each action
    actionRewards.resize(actions);
    
    if (logger) {
      logger->log("[RL-GC INIT] Initialized RL-GC with parameters:" 
                 "\n  Learning rate (alpha): " + std::to_string(alpha) +
                 "\n  Discount factor (gamma): " + std::to_string(gamma) +
                 "\n  Initial epsilon: " + std::to_string(epsilon) +
                 "\n  Action count: " + std::to_string(actions) +
                 "\n  TGC threshold: " + std::to_string(tgc) +
                 "\n  TIGC threshold: " + std::to_string(tigc) +
                 "\n  Max page copies: " + std::to_string(maxCopies) +
                 "\n  Reward logging: enabled, file=" + rewardLogFilePath);
    }
}

RLGarbageCollector::~RLGarbageCollector() {
  if (logger) {
    logger->log("[RL-GC SUMMARY] Final statistics:" 
               "\n  GC invocations: " + std::to_string(stats.gcInvocations) +
               "\n  Total page copies: " + std::to_string(stats.totalPageCopies) +
               "\n  Intensive GC count: " + std::to_string(stats.intensiveGCCount));
    
    // Save any pending reward history
    if (enableRewardLogging) {
      saveActionRewardHistory();
    }
    
    delete logger;
    logger = nullptr;
  }
}

bool RLGarbageCollector::shouldTriggerGC(uint32_t freeBlocks, uint64_t currentTime) {
  // Safety check - don't trigger if we have enough free blocks
  if (freeBlocks > tgcThreshold) {
    // Only log occasionally to reduce log size
    if (logger && responseCounter % 20 == 0) {
      logger->log("[RL-GC DECISION] Not triggering GC: free blocks (" 
                + std::to_string(freeBlocks) + ") > TGC threshold (" + std::to_string(tgcThreshold) + ")");
    }
    return false;
  }
  
  // Update current time
  currentRequestTime = currentTime;
  
  // Calculate inter-request time
  if (lastRequestTime > 0) {
    prevInterRequestTime = currInterRequestTime;
    currInterRequestTime = currentRequestTime - lastRequestTime;
    
    // Only log occasionally to reduce file size
    if (logger && responseCounter % 20 == 0) {
      logger->log("[RL-GC TIME] Inter-request times updated: previous=" 
                + std::to_string(prevInterRequestTime) + "ns, current=" 
                + std::to_string(currInterRequestTime) + "ns");
    }
  }
  else {
    // First request, no inter-request interval
    prevInterRequestTime = 0;
    currInterRequestTime = 0;
    if (logger) {
      logger->log("[RL-GC TIME] First request detected, no inter-request times yet");
    }
  }
  
  // Update last request time for next calculation
  lastRequestTime = currentTime;
  
  // Skip if there's no idle time (consecutive requests)
  if (currInterRequestTime == 0) {
    if (logger && responseCounter % 20 == 0) {
      logger->log("[RL-GC DECISION] Not triggering GC: no idle time between requests");
    }
    return false;
  }
  
  // Critical situation - force GC if free blocks are too low
  if (freeBlocks <= tigcThreshold) {
    if (logger) {
      logger->log("[RL-GC DECISION] CRITICAL: Free blocks (" 
                + std::to_string(freeBlocks) + ") below TIGC threshold (" + std::to_string(tigcThreshold) 
                + "). Forcing intensive GC.");
    }
    return true;
  }
  
  // Update state based on current intervals
  updateState(currentTime);
  
  if (logger) {
    logger->log("[RL-GC DECISION] Triggering GC with state: prevInterval=" 
              + std::to_string(currentState.getPrevIntervalBin()) 
              + ", currInterval=" + std::to_string(currentState.getCurrIntervalBin()) 
              + ", prevAction=" + std::to_string(currentState.getPrevActionBin()) 
              + ", freeBlocks=" + std::to_string(freeBlocks));
  }
  
  // Always return true for normal GC triggering
  // The action selection will determine how many pages to copy
  return true;
}

uint32_t RLGarbageCollector::getGCAction(uint32_t freeBlocks) {
  // Critical situation - force intensive GC
  if (freeBlocks <= tigcThreshold) {
    // Return maximum action (most aggressive GC)
    stats.intensiveGCCount++;
    if (logger) {
      logger->log("[RL-GC ACTION] INTENSIVE GC: Using maximum action " 
                + std::to_string(maxPageCopies) + " due to critical free blocks (" 
                + std::to_string(freeBlocks) + " <= " + std::to_string(tigcThreshold) + ")");
    }
    
    // Store last action for next state
    lastAction = maxPageCopies;
    
    // Schedule pending update with current state and action
    schedulePendingUpdate(currentState, lastAction);
    
    return maxPageCopies;
  }
  
  // Get action from Q-table
  uint32_t action = qTable.selectAction(currentState);
  
  // Ensure action is within bounds
  if (action > maxPageCopies) {
    if (logger) {
      logger->log("[RL-GC ACTION] Action " + std::to_string(action) + " exceeds maximum, capping to " 
                + std::to_string(maxPageCopies));
    }
    action = maxPageCopies;
  }
  
  // Only log occasionally to reduce file size (but always log epsilon changes and unusual conditions)
  static float lastEpsilon = -1.0f;
  float currentEpsilon = qTable.getEpsilon();
  bool epsilonChanged = std::abs(currentEpsilon - lastEpsilon) > 0.01f;
  
  if (logger && (responseCounter % 15 == 0 || epsilonChanged)) {
    logger->log("[RL-GC ACTION] Selected action: " + std::to_string(action) 
              + " (copy " + std::to_string(action) + " pages), epsilon=" 
              + std::to_string(currentEpsilon) + ", free blocks=" + std::to_string(freeBlocks));
    lastEpsilon = currentEpsilon;
  }
  
  // Update statistics
  stats.gcInvocations++;
  stats.totalPageCopies += action;
  
  // Store last action for next state
  lastAction = action;
  
  // Schedule pending update with current state and action
  schedulePendingUpdate(currentState, lastAction);
  
  return action;
}

void RLGarbageCollector::updateState(uint64_t currentTime) {
  // Store previous state for Q-learning
  previousState = currentState;
  
  // Update current time
  currentRequestTime = currentTime;
  
  // Calculate inter-request time
  if (lastRequestTime > 0) {
    prevInterRequestTime = currInterRequestTime;
    currInterRequestTime = currentRequestTime - lastRequestTime;
  }
  else {
    prevInterRequestTime = 0;
    currInterRequestTime = 0;
  }
  
  // Update last request time
  lastRequestTime = currentTime;
  
  // Discretize inter-request times for state
  uint32_t prevBin = discretizePrevInterval(prevInterRequestTime);
  uint32_t currBin = discretizeCurrInterval(currInterRequestTime);
  uint32_t actionBin = discretizeAction(lastAction);
  
  // Update current state
  currentState = State(prevBin, currBin, actionBin);
  
  // Only log occasionally to reduce file size
  if (logger && responseCounter % 15 == 0) {
    logger->log("[RL-GC STATE] State updated: previous=(" 
              + std::to_string(previousState.getPrevIntervalBin()) + "," 
              + std::to_string(previousState.getCurrIntervalBin()) + "," 
              + std::to_string(previousState.getPrevActionBin()) + "), current=(" 
              + std::to_string(currentState.getPrevIntervalBin()) + "," 
              + std::to_string(currentState.getCurrIntervalBin()) + "," 
              + std::to_string(currentState.getPrevActionBin()) + ")" 
              + "\n[RL-GC STATE] Raw intervals: prevInterval=" + std::to_string(prevInterRequestTime) 
              + "ns, currInterval=" + std::to_string(currInterRequestTime) 
              + "ns, lastAction=" + std::to_string(lastAction));
  }
}

void RLGarbageCollector::recordResponseTime(uint64_t responseTime) {
  // Safety check - ignore unreasonable response times
  if (responseTime > UINT64_MAX / 2) {
    if (logger) {
      logger->log("[RL-GC RESPONSE] Ignoring unreasonable response time: " 
                + std::to_string(responseTime) + "ns");
    }
    return;
  }
  
  // Log the response time to file
  logResponseTime(responseTime);
  
  // Add to response time history
  responseTimes.push_back(responseTime);
  
  // Keep history size limited
  while (responseTimes.size() > maxResponseTimes) {
    responseTimes.pop_front();
  }
  
  // Only log every 10th response to reduce file size
  responseCounter++;
  if (responseCounter % 10 == 0) {
    if (logger) {
      logger->log("[RL-GC RESPONSE] Recorded response time: " + std::to_string(responseTime) 
                + "ns, history size: " + std::to_string(responseTimes.size()) + "/" 
                + std::to_string(maxResponseTimes));
    }
  }
  
  // Update percentile thresholds if we have enough data
  // Only update every 10 responses to reduce overhead
  if (responseTimes.size() >= 100 && responseCounter % 10 == 0) {
    updatePercentileThresholds();
  }
}

// Add a new function to log response times
void RLGarbageCollector::logResponseTime(uint64_t responseTime) {
  static bool headerWritten = false;
  static uint64_t ioCount = 0;
  
  // Define log file path - use a different file than the regular GC log
  static std::string logPath = "output/rl_gc_response.csv";
  
  // Create output directory if it doesn't exist
  struct stat st;
  if (::stat("output", &st) != 0) {
    int ret = std::system("mkdir -p output");
    if (ret != 0) {
      std::cerr << "Error: Failed to create output directory" << std::endl;
      exit(1);
    }
  }
  
  std::ofstream logFile;
  
  // If this is the first write, create the file with header
  if (!headerWritten) {
    logFile.open(logPath, std::ios::trunc);
    if (logFile.is_open()) {
      logFile << "IO_Count,Response_Time_ns" << std::endl;
      headerWritten = true;
      logFile.close();
    }
  }
  
  // Append the response time data
  logFile.open(logPath, std::ios::app);
  if (logFile.is_open()) {
    logFile << ioCount << "," << responseTime << std::endl;
    ioCount++;
    logFile.close();
  }
}

float RLGarbageCollector::updateQValue(uint64_t responseTime) {
  // Calculate reward based on response time
  float reward = calculateReward(responseTime);
  
  // Log reward for the action if logging is enabled
  logActionReward(lastAction, reward);
  
  // Create next state based on current intervals and last action
  State nextState(discretizePrevInterval(prevInterRequestTime),
                 discretizeCurrInterval(currInterRequestTime),
                 discretizeAction(lastAction));
  
  // Only log occasionally or for significant rewards to reduce file size
  bool significantReward = std::abs(reward) > 0.7f; // Log high rewards/penalties
  
  if (logger && (responseCounter % 15 == 0 || significantReward)) {
    logger->log("[RL-GC Q-UPDATE] Updating Q-value:"
              "\n  Response time: " + std::to_string(responseTime) + "ns"
              + "\n  Reward: " + std::to_string(reward)
              + "\n  Current state: (" + std::to_string(currentState.getPrevIntervalBin())
              + "," + std::to_string(currentState.getCurrIntervalBin())
              + "," + std::to_string(currentState.getPrevActionBin()) + ")"
              + "\n  Action: " + std::to_string(lastAction)
              + "\n  Next state: (" + std::to_string(nextState.getPrevIntervalBin())
              + "," + std::to_string(nextState.getCurrIntervalBin())
              + "," + std::to_string(nextState.getPrevActionBin()) + ")");
  }
  
  // Update Q-table
  qTable.updateQ(currentState, lastAction, reward, nextState);
  
  // Update statistics
  stats.avgReward = (stats.avgReward * stats.rewardCount + reward) / (stats.rewardCount + 1);
  stats.rewardCount++;
  
  if (logger && (responseCounter % 20 == 0 || significantReward)) {
    logger->log("[RL-GC STATS] Updated average reward: " + std::to_string(stats.avgReward) 
              + " (total rewards: " + std::to_string(stats.rewardCount) + ")");
  }
  
  // Track reward trends for convergence monitoring
  trackRewardTrends();
  
  // Update current state for next decision
  currentState = nextState;
  
  // Decay exploration rate
  qTable.decayEpsilon();
  
  return reward;
}

void RLGarbageCollector::schedulePendingUpdate(State state, uint32_t action) {
  // Store the state and action for later update
  hasPendingUpdate = true;
  pendingState = state;
  pendingAction = action;
  
  // Only log occasionally to reduce file size
  if (logger && responseCounter % 15 == 0) {
    logger->log("[RL-GC PENDING] Scheduled pending Q-value update:" 
               "\n  State: (" + std::to_string(state.getPrevIntervalBin()) 
               + "," + std::to_string(state.getCurrIntervalBin()) 
               + "," + std::to_string(state.getPrevActionBin()) + ")" 
               "\n  Action: " + std::to_string(action));
  }
}

float RLGarbageCollector::processPendingUpdate(uint64_t responseTime) {
  // Check if there's a pending update
  if (!hasPendingUpdate) {
    if (logger && responseCounter % 20 == 0) {
      logger->log("[RL-GC PENDING] No pending update to process");
    }
    return 0.0f;
  }
  
  // Calculate reward based on response time
  float reward = calculateReward(responseTime);
  
  // Log reward for the action if logging is enabled
  logActionReward(pendingAction, reward);
  
  // Create next state based on current intervals
  State nextState(
    discretizePrevInterval(prevInterRequestTime),
    discretizeCurrInterval(currInterRequestTime),
    discretizeAction(pendingAction)
  );
  
  // Only log occasionally or for significant rewards to reduce file size
  bool significantReward = std::abs(reward) > 0.7f; // Log high rewards/penalties
  
  if (logger && (responseCounter % 15 == 0 || significantReward)) {
    logger->log("[RL-GC PENDING] Processing pending Q-value update:"
               "\n  Response time: " + std::to_string(responseTime) + "ns"
               + "\n  Reward: " + std::to_string(reward)
               + "\n  Pending state: (" + std::to_string(pendingState.getPrevIntervalBin())
               + "," + std::to_string(pendingState.getCurrIntervalBin())
               + "," + std::to_string(pendingState.getPrevActionBin()) + ")"
               + "\n  Action: " + std::to_string(pendingAction)
               + "\n  Next state: (" + std::to_string(nextState.getPrevIntervalBin())
               + "," + std::to_string(nextState.getCurrIntervalBin())
               + "," + std::to_string(nextState.getPrevActionBin()) + ")");
  }
  
  // Update Q-table using the pending state and action
  qTable.updateQ(pendingState, pendingAction, reward, nextState);
  
  // Update statistics
  stats.avgReward = (stats.avgReward * stats.rewardCount + reward) / (stats.rewardCount + 1);
  stats.rewardCount++;
  
  // Track reward trends for convergence monitoring
  trackRewardTrends();
  
  // Clear pending update
  hasPendingUpdate = false;
  
  // Decay exploration rate
  qTable.decayEpsilon();
  
  return reward;
}

bool RLGarbageCollector::shouldPerformIntensiveGC(uint32_t freeBlocks) {
  return freeBlocks <= tigcThreshold;
}

uint32_t RLGarbageCollector::discretizePrevInterval(uint64_t interval) {
  // 2 bins as per the paper: "short" if less than 100μs, or "long" otherwise
  if (interval < 100000) return 0;  // < 100μs (short)
  return 1;                         // >= 100μs (long)
}

uint32_t RLGarbageCollector::discretizeCurrInterval(uint64_t interval) {
  // 17 bins as per the paper for finer granularity
  if (interval == 0) return 0;
  
  // Define thresholds for the 17 bins (in nanoseconds)
  // These thresholds can be adjusted based on workload characteristics
  static const uint64_t thresholds[] = {
    10000,      // 10μs
    20000,      // 20μs
    50000,      // 50μs
    100000,     // 100μs
    200000,     // 200μs
    500000,     // 500μs
    1000000,    // 1ms
    2000000,    // 2ms
    5000000,    // 5ms
    10000000,   // 10ms
    20000000,   // 20ms
    50000000,   // 50ms
    100000000,  // 100ms
    200000000,  // 200ms
    500000000,  // 500ms
    1000000000  // 1s
  };
  
  // Find the appropriate bin
  for (uint32_t i = 0; i < 16; i++) {
    if (interval < thresholds[i]) {
      return i + 1;  // +1 because bin 0 is for interval == 0
    }
  }
  
  return 17;  // For intervals >= 1s
}

uint32_t RLGarbageCollector::discretizeAction(uint32_t action) {
  // 2 bins as per the paper: actions less than or equal to half of max, or above
  if (action <= maxPageCopies / 2) return 0;  // <= half of max
  return 1;                                   // > half of max
}

uint32_t RLGarbageCollector::getTGCThreshold() const {
  return tgcThreshold;
}

uint32_t RLGarbageCollector::getTIGCThreshold() const {
  return tigcThreshold;
}

uint32_t RLGarbageCollector::getMaxPageCopies() const {
  return maxPageCopies;
}

const State& RLGarbageCollector::getCurrentState() const {
  return currentState;
}

void RLGarbageCollector::getStats(uint64_t &invocations, uint64_t &pageCopies,
                                 uint64_t &intensiveGCs, float &avgReward) {
  // Use a mutex or atomic operations if this is accessed from multiple threads
  invocations = stats.gcInvocations;
  pageCopies = stats.totalPageCopies;
  intensiveGCs = stats.intensiveGCCount;
  avgReward = stats.avgReward;
}

void RLGarbageCollector::resetStats() {
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.intensiveGCCount = 0;
  stats.avgReward = 0.0f;
  stats.rewardCount = 0;
}

void RLGarbageCollector::printDebugInfo() const {
  std::stringstream ss;
  ss << "=== RL-GC Debug Information ===" << std::endl;
  ss << "Current state: " << 
    "prevIntervalBin=" << currentState.getPrevIntervalBin() << 
    ", currIntervalBin=" << currentState.getCurrIntervalBin() << 
    ", prevActionBin=" << currentState.getPrevActionBin() << std::endl;
  
  ss << "Last action taken: " << lastAction << std::endl;
  ss << "Free blocks thresholds: tgc=" << tgcThreshold << ", tigc=" << tigcThreshold << std::endl;
  ss << "Statistics: GC invocations=" << stats.gcInvocations
            << ", page copies=" << stats.totalPageCopies
            << ", intensive GCs=" << stats.intensiveGCCount
            << ", avg reward=" << stats.avgReward << std::endl;
  
  // Since we can't access the Q-table entries directly, print summary info
  ss << "Q-table summary: epsilon=" << qTable.getEpsilon() 
            << ", GC count=" << qTable.getGCCount() << std::endl;
  
  ss << "===============================" << std::endl;
  
  // Print to console and debug file if enabled
  std::cout << ss.str();
  
  if (logger) {
    std::ofstream debugFile;
    debugFile.open(logger->getFilepath(), std::ios_base::app);
    if (debugFile.is_open()) {
      debugFile << ss.str() << std::endl;
      debugFile.close();
    }
  }
}

void RLGarbageCollector::recordGCInvocation(uint32_t copiedPages) {
  stats.gcInvocations++;
  stats.totalPageCopies += copiedPages;
  
  // Every 100 GC operations, check for convergence
  if (stats.gcInvocations % 100 == 0) {
    // Check Q-table convergence
    bool tableConverged = qTable.checkConvergence();
    if (tableConverged && !qTableConverged) {
      qTableConverged = true;
      if (logger) {
        logger->log("[RL-GC CONVERGENCE] Q-table has converged after " 
                  + std::to_string(stats.gcInvocations) + " GC operations");
      }
    }
    
    // Every 1000 GC operations, export data for visualization
    if (stats.gcInvocations % 1000 == 0) {
      std::string timestamp = std::to_string(currentRequestTime);
      qTable.exportQTableToCSV("output/q_table_" + timestamp + ".csv");
      exportConvergenceData("output/convergence_" + timestamp + ".csv");
      
      // Log the export
      if (logger) {
        logger->log("[RL-GC DATA] Exported Q-table and convergence data at iteration " 
                  + std::to_string(stats.gcInvocations));
      }
    }
  }
}

void RLGarbageCollector::recordIntensiveGC() {
  stats.intensiveGCCount++;
}

void RLGarbageCollector::updatePercentileThresholds() {
  // Need at least 100 samples to calculate percentiles
  if (responseTimes.size() < 100) {
    if (logger) {
      logger->log("[RL-GC PERCENTILE] Not enough samples to update thresholds: " 
                + std::to_string(responseTimes.size()) + " < 100");
    }
    return;
  }
  
  // Create a copy of response times for sorting
  std::vector<uint64_t> sortedTimes(responseTimes.begin(), responseTimes.end());
  std::sort(sortedTimes.begin(), sortedTimes.end());
  
  // Calculate percentiles
  size_t size = sortedTimes.size();
  uint64_t oldT1 = t1Threshold;
  uint64_t oldT2 = t2Threshold;
  uint64_t oldT3 = t3Threshold;
  
  t1Threshold = sortedTimes[size * 70 / 100];  // 70th percentile
  t2Threshold = sortedTimes[size * 90 / 100];  // 90th percentile
  t3Threshold = sortedTimes[size * 99 / 100];  // 99th percentile
  
  // Only log if thresholds have changed significantly
  if (oldT1 != t1Threshold || oldT2 != t2Threshold || oldT3 != t3Threshold) {
    if (logger) {
      logger->log("[RL-GC PERCENTILE] Updated thresholds:"
                "\n  t1 (70%): " + std::to_string(oldT1) + " -> " + std::to_string(t1Threshold) + "ns"
                + "\n  t2 (90%): " + std::to_string(oldT2) + " -> " + std::to_string(t2Threshold) + "ns"
                + "\n  t3 (99%): " + std::to_string(oldT3) + " -> " + std::to_string(t3Threshold) + "ns"
                + "\n  Sample size: " + std::to_string(size)
                + "\n  Min response time: " + std::to_string(sortedTimes.front()) + "ns"
                + "\n  Max response time: " + std::to_string(sortedTimes.back()) + "ns");
    }
  }
}

float RLGarbageCollector::calculateReward(uint64_t responseTime) {
  float reward = 0.0f;
  
  // If we don't have enough data to calculate thresholds, use a simple reward
  if (responseTimes.size() < 100) {
    // Simple reward: lower response time is better
    // Use a more nuanced approach even with limited data
    if (responseTime < 100000) {  // < 100μs
      reward = 1.0f;              // Excellent response time
    }
    else if (responseTime < 1000000) {  // < 1ms
      reward = 0.5f;                    // Good response time
    }
    else if (responseTime < 10000000) {  // < 10ms
      reward = 0.0f;                     // Acceptable response time
    }
    else {
      reward = -0.5f;                    // Poor response time
    }
    
    if (logger) {
      std::string logMessage = "[RL-GC REWARD] Simple reward calculation (not enough samples): ";
      logMessage += "responseTime=" + std::to_string(responseTime) + "ns";
      logMessage += ", reward=" + std::to_string(reward);
      logger->log(logMessage);
    }
    return reward;
  }
  
  // Calculate reward based on percentile thresholds
  if (responseTime <= t1Threshold) {
    // Response time is below 70th percentile - excellent
    reward = 1.0f;
    if (logger) {
      logger->log("[RL-GC REWARD] EXCELLENT response time: " + std::to_string(responseTime) 
                + "ns <= t1(" + std::to_string(t1Threshold) + "ns), reward=" + std::to_string(reward));
    }
  }
  else if (responseTime <= t2Threshold) {
    // Response time is between 70th and 90th percentile - good
    reward = 0.5f;
    if (logger) {
      logger->log("[RL-GC REWARD] GOOD response time: " + std::to_string(responseTime) 
                + "ns <= t2(" + std::to_string(t2Threshold) + "ns), reward=" + std::to_string(reward));
    }
  }
  else if (responseTime <= t3Threshold) {
    // Response time is between 90th and 99th percentile - poor
    reward = -0.5f;
    if (logger) {
      logger->log("[RL-GC REWARD] POOR response time: " + std::to_string(responseTime) 
                + "ns <= t3(" + std::to_string(t3Threshold) + "ns), reward=" + std::to_string(reward));
    }
  }
  else {
    // Response time is above 99th percentile - very poor
    reward = -1.0f;
    if (logger) {
      logger->log("[RL-GC REWARD] VERY POOR response time: " + std::to_string(responseTime) 
                + "ns > t3(" + std::to_string(t3Threshold) + "ns), reward=" + std::to_string(reward));
    }
  }
  
  return reward;
}

/**
 * Track reward trends for convergence monitoring
 */
void RLGarbageCollector::trackRewardTrends() {
  // Only process if we have rewards
  if (stats.rewardCount == 0) {
    return;
  }
  
  // Add current reward to history
  rewardHistory.push_back(stats.avgReward);
  
  // Keep a fixed-size window
  if (rewardHistory.size() > 1000) {
    rewardHistory.erase(rewardHistory.begin());
  }
  
  // Calculate moving average if we have enough data
  if (rewardHistory.size() >= 100) {
    float sum = 0.0f;
    for (size_t i = rewardHistory.size() - 100; i < rewardHistory.size(); i++) {
      sum += rewardHistory[i];
    }
    float movingAvg = sum / 100.0f;
    
    // Track moving averages for trend analysis
    movingAverageRewards.push_back(movingAvg);
    
    // Keep a fixed-size window for moving averages
    if (movingAverageRewards.size() > 100) {
      movingAverageRewards.erase(movingAverageRewards.begin());
    }
    
    // Calculate slope of recent moving averages to detect flattening
    if (movingAverageRewards.size() >= 10) {
      float recentSlope = (movingAverageRewards.back() - movingAverageRewards[movingAverageRewards.size() - 10]) / 10.0f;
      
      // If slope is near zero, we might be converging
      if (std::abs(recentSlope) < 0.01f) {
        convergenceCounter++;
        if (convergenceCounter >= 5 && !qTableConverged) {
          qTableConverged = true;
          if (logger) {
            logger->log("[RL-GC CONVERGENCE] Q-learning appears to be converging based on reward trends");
          }
        }
      }
      else {
        convergenceCounter = 0;
      }
    }
  }
}

/**
 * Export convergence data to CSV file for visualization
 */
void RLGarbageCollector::exportConvergenceData(const std::string &filename) {
  std::ofstream outFile(filename);
  if (!outFile.is_open()) {
    if (logger) {
      logger->log("[RL-GC ERROR] Failed to open file for exporting convergence data: " + filename);
    }
    return;
  }
  
  // CSV header
  outFile << "Iteration,MaxQDelta,AvgReward,NumStates,ConvMetric" << std::endl;
  
  // Get current Q-value delta
  float maxDelta = qTable.getMaxQValueDelta();
  float convMetric = qTable.getConvergenceMetric();
  
  // Get the actual number of unique states in the Q-table
  // We'll need to add a method to the QTable class to access this information
  uint32_t numStates = qTable.getNumStates();
  
  // Write current data
  outFile << stats.gcInvocations << ","
          << maxDelta << ","
          << stats.avgReward << ","
          << numStates << ","  // Now using the actual number of states
          << convMetric << std::endl;
  
  outFile.close();
  
  if (logger) {
    logger->log("[RL-GC DATA] Exported convergence data to " + filename + " with " + std::to_string(numStates) + " states");
  }
}

void RLGarbageCollector::enableActionRewardLogging(const std::string& logFilePath) {
  enableRewardLogging = true;
  rewardLogFilePath = logFilePath;
  
  // Clear any existing reward history
  for (auto& actionReward : actionRewards) {
    actionReward.rewards.clear();
    actionReward.cumulativeReward = 0.0f;
    actionReward.count = 0;
  }
  
  if (logger) {
    logger->log("[RL-GC REWARD LOGGING] Enabled action reward logging to file: " + logFilePath);
  }
}

void RLGarbageCollector::disableActionRewardLogging() {
  enableRewardLogging = false;
  
  if (logger) {
    logger->log("[RL-GC REWARD LOGGING] Disabled action reward logging");
  }
}

void RLGarbageCollector::logActionReward(uint32_t action, float reward) {
  // Only log if enabled and action is within bounds
  if (!enableRewardLogging || action >= actionRewards.size()) {
    return;
  }
  
  // Store the reward with the current GC invocation count
  actionRewards[action].rewards.push_back(std::make_pair(stats.gcInvocations, reward));
  actionRewards[action].cumulativeReward += reward;
  actionRewards[action].count++;
  
  // Log periodically to reduce file I/O
  if (stats.gcInvocations % 1000 == 0) {
    saveActionRewardHistory();
  }
}

void RLGarbageCollector::saveActionRewardHistory() {
  if (!enableRewardLogging) {
    return;
  }
  
  // Open the log file
  std::ofstream outFile(rewardLogFilePath);
  if (!outFile) {
    if (logger) {
      logger->log("[RL-GC REWARD LOGGING] ERROR: Failed to open file: " + rewardLogFilePath);
    }
    return;
  }
  
  // Write CSV header
  outFile << "Iteration,Action,Reward,CumulativeReward,AverageReward,ThresholdT1,ThresholdT2,ThresholdT3" << std::endl;
  
  // Write data for each action
  for (size_t action = 0; action < actionRewards.size(); action++) {
    const auto& history = actionRewards[action];
    for (const auto& entry : history.rewards) {
      outFile << entry.first << ","                    // Iteration
              << action << ","                         // Action
              << entry.second << ","                   // Reward
              << history.cumulativeReward << ",";      // Cumulative reward
      
      // Calculate average reward
      float avgReward = history.count > 0 ? history.cumulativeReward / history.count : 0.0f;
      outFile << avgReward << ","                      // Average reward
              << t1Threshold << ","                    // Current t1 threshold
              << t2Threshold << ","                    // Current t2 threshold 
              << t3Threshold;                          // Current t3 threshold
      
      outFile << std::endl;
    }
  }
  
  outFile.close();
  
  if (logger) {
    logger->log("[RL-GC REWARD LOGGING] Saved action reward history to: " + rewardLogFilePath);
  }
}

}  // namespace FTL

}  // namespace SimpleSSD
