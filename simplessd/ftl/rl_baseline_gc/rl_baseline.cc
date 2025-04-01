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

#include "ftl/rl_baseline_gc/rl_baseline.hh"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>  // For formatting output
#include <fstream>  // For file output
#include <sstream>  // For string stream operations
#include <sys/stat.h>  // For directory creation

namespace SimpleSSD {

namespace FTL {

// Helper function to write debug messages
#define RL_DEBUG_LOG(stream) \
  if (debugEnabled) { \
    std::ofstream debugFile; \
    debugFile.open(debugFilePath, std::ios_base::app); \
    if (debugFile.is_open()) { \
      debugFile << stream << std::endl; \
      debugFile.close(); \
    } else { \
      std::cerr << "Failed to open RL-GC debug file: " << debugFilePath << std::endl; \
    } \
  }

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
      inIntensiveMode(false),
      intensiveGCMaxPageCopies(7),  // Based on paper: typically 5-7 page copies in intensive mode
      debugEnabled(false),
      debugFilePath("output/rl_baseline_debug.log"),
      metricsEnabled(false),
      metricsFilePath("output/rl_baseline_metrics.txt") {
  
  // Initialize statistics
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.intensiveGCCount = 0;
  stats.avgReward = 0.0f;
  stats.rewardCount = 0;
  stats.eraseCount = 0;
  
  // Initialize current state
  currentState = State(0, 0, 0);
  
  // Create output directory if metrics or debug are enabled
  #ifdef _WIN32
  // Windows
  if (system("if not exist output mkdir output") != 0) {
    std::cerr << "Warning: Failed to create output directory for RL-GC" << std::endl;
  }
  #else
  // Linux/Unix/MacOS
  if (system("mkdir -p output") != 0) {
    std::cerr << "Warning: Failed to create output directory for RL-GC" << std::endl;
  }
  #endif
  
  // Create/clear the debug file if debug is enabled
  if (debugEnabled) {
    std::ofstream debugFile(debugFilePath, std::ios::trunc);
    if (debugFile.is_open()) {
      debugFile.close();
    }
  }
  
  // Initialize metrics file if metrics are enabled
  if (metricsEnabled) {
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# RL-Baseline Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <intensive_gc_count> <erases> <avg_reward> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
    else {
      std::cerr << "Warning: Failed to initialize RL-Baseline metrics file" << std::endl;
    }
  }
  
  RL_DEBUG_LOG("[RL-GC INIT] Initialized RL-GC with parameters:" << std::endl
            << "  Learning rate (alpha): " << alpha << std::endl
            << "  Discount factor (gamma): " << gamma << std::endl
            << "  Initial epsilon: " << epsilon << std::endl
            << "  Action count: " << actions << std::endl
            << "  TGC threshold: " << tgc << std::endl
            << "  TIGC threshold: " << tigc << std::endl
            << "  Max page copies: " << maxCopies);
}

RLGarbageCollector::~RLGarbageCollector() {
  // Finalize metrics if enabled
  if (metricsEnabled) {
    finalizeMetrics();
  }
  
  RL_DEBUG_LOG("[RL-GC SUMMARY] Final statistics:" << std::endl
            << "  GC invocations: " << stats.gcInvocations << std::endl
            << "  Total page copies: " << stats.totalPageCopies << std::endl
            << "  Intensive GC count: " << stats.intensiveGCCount << std::endl
            << "  Average reward: " << stats.avgReward);
}

bool RLGarbageCollector::shouldTriggerGC(uint32_t freeBlocks, uint64_t currentTime) {
  // Safety check - don't trigger if we have enough free blocks
  if (freeBlocks > tgcThreshold) {
    RL_DEBUG_LOG("[RL-GC DECISION] Not triggering GC: free blocks (" << freeBlocks 
              << ") > TGC threshold (" << tgcThreshold << ")");
    return false;
  }
  
  // Update current time
  currentRequestTime = currentTime;
  
  // Calculate inter-request time
  if (lastRequestTime > 0) {
    prevInterRequestTime = currInterRequestTime;
    currInterRequestTime = currentRequestTime - lastRequestTime;
    
    RL_DEBUG_LOG("[RL-GC TIME] Inter-request times updated: previous=" 
              << prevInterRequestTime << "ns, current=" 
              << currInterRequestTime << "ns");
  }
  else {
    // First request, no inter-request interval
    prevInterRequestTime = 0;
    currInterRequestTime = 0;
    RL_DEBUG_LOG("[RL-GC TIME] First request detected, no inter-request times yet");
  }
  
  // Update last request time for next calculation
  lastRequestTime = currentTime;
  
  // Skip if there's no idle time (consecutive requests)
  if (currInterRequestTime == 0) {
    RL_DEBUG_LOG("[RL-GC DECISION] Not triggering GC: no idle time between requests");
    return false;
  }
  
  // Critical situation - force GC if free blocks are too low
  if (freeBlocks <= tigcThreshold) {
    RL_DEBUG_LOG("[RL-GC DECISION] CRITICAL: Free blocks (" << freeBlocks 
              << ") below TIGC threshold (" << tigcThreshold 
              << "). Forcing intensive GC.");
    return true;
  }
  
  // Update state based on current intervals
  updateState(currentTime);
  
  RL_DEBUG_LOG("[RL-GC DECISION] Triggering GC with state: prevInterval=" 
            << currentState.getPrevIntervalBin() 
            << ", currInterval=" << currentState.getCurrIntervalBin() 
            << ", prevAction=" << currentState.getPrevActionBin() 
            << ", freeBlocks=" << freeBlocks);
  
  // Always return true for normal GC triggering
  // The action selection will determine how many pages to copy
  return true;
}

uint32_t RLGarbageCollector::getGCAction(uint32_t freeBlocks) {
  // Check if we're in intensive mode
  if (inIntensiveMode) {
    // Increment intensive GC counter
    stats.intensiveGCCount++;
    
    // Return intensive action (more aggressive GC)
    RL_DEBUG_LOG("[RL-GC ACTION] INTENSIVE GC: Using intensive action " 
              << intensiveGCMaxPageCopies << " due to being in intensive mode, "
              << freeBlocks << " free blocks, threshold: " << tigcThreshold
              << ", total intensive GCs: " << stats.intensiveGCCount);
    
    // Store last action for next state
    lastAction = intensiveGCMaxPageCopies;
    
    // Schedule pending update with current state and action
    schedulePendingUpdate(currentState, lastAction);
    
    return intensiveGCMaxPageCopies;
  }
  
  // Get action from Q-table
  uint32_t action = qTable.selectAction(currentState);
  
  // Ensure action is within bounds
  if (action > maxPageCopies) {
    RL_DEBUG_LOG("[RL-GC ACTION] Action " << action << " exceeds maximum, capping to " 
              << maxPageCopies);
    action = maxPageCopies;
  }
  
  RL_DEBUG_LOG("[RL-GC ACTION] Selected action: " << action 
            << " (copy " << action << " pages), epsilon=" 
            << qTable.getEpsilon() << ", free blocks=" << freeBlocks);
  
  // Update statistics
  stats.gcInvocations++;
  stats.totalPageCopies += action;
  
  // Store last action for next state
  lastAction = action;
  
  // Schedule pending update with current state and action
  schedulePendingUpdate(currentState, lastAction);
  
  return action;
}

uint32_t RLGarbageCollector::getMaxGCAction() {
  // For aggressive policy, always return the maximum action value
  // This follows our research paper approach for the RL aggressive policy
  // which maximizes free blocks by always performing maximum page copies
  
  RL_DEBUG_LOG("[RL-GC ACTION] Aggressive policy using maximum action " 
            << maxPageCopies);
  
  // Update statistics
  stats.gcInvocations++;
  stats.totalPageCopies += maxPageCopies;
  
  // Store last action for next state
  lastAction = maxPageCopies;
  
  // Schedule pending update with current state and action
  schedulePendingUpdate(currentState, lastAction);
  
  return maxPageCopies;
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
  
  RL_DEBUG_LOG("[RL-GC STATE] State updated: previous=(" 
            << previousState.getPrevIntervalBin() << "," 
            << previousState.getCurrIntervalBin() << "," 
            << previousState.getPrevActionBin() << "), current=(" 
            << currentState.getPrevIntervalBin() << "," 
            << currentState.getCurrIntervalBin() << "," 
            << currentState.getPrevActionBin() << ")" << std::endl
            << "[RL-GC STATE] Raw intervals: prevInterval=" << prevInterRequestTime 
            << "ns, currInterval=" << currInterRequestTime 
            << "ns, lastAction=" << lastAction);
}

void RLGarbageCollector::recordResponseTime(uint64_t responseTime) {
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
  
  // Update percentile thresholds if we have enough data
  if (responseTimes.size() >= 100) {
    updatePercentileThresholds();
  }
  
  // Output metrics periodically if enabled
  if (metricsEnabled && stats.rewardCount % 1000 == 0 && stats.rewardCount > 0) {
    outputMetricsToFile();
  }
}

float RLGarbageCollector::updateQValue(uint64_t responseTime) {
  // Calculate reward
  float reward = calculateReward(responseTime);
  
  // Safety check - make sure we have a valid previous action
  if (lastAction > maxPageCopies) {
    RL_DEBUG_LOG("[RL-GC Q-UPDATE] Capping last action from " << lastAction 
              << " to " << maxPageCopies);
    lastAction = maxPageCopies;
  }
  
  // Create next state based on current intervals
  State nextState(
    discretizePrevInterval(prevInterRequestTime),
    discretizeCurrInterval(currInterRequestTime),
    discretizeAction(lastAction)
  );
  
  RL_DEBUG_LOG("[RL-GC Q-UPDATE] Updating Q-value:" << std::endl
            << "  Response time: " << responseTime << "ns" << std::endl
            << "  Reward: " << std::fixed << std::setprecision(4) << reward << std::endl
            << "  Current state: (" << currentState.getPrevIntervalBin() 
            << "," << currentState.getCurrIntervalBin() 
            << "," << currentState.getPrevActionBin() << ")" << std::endl
            << "  Action: " << lastAction << std::endl
            << "  Next state: (" << nextState.getPrevIntervalBin() 
            << "," << nextState.getCurrIntervalBin() 
            << "," << nextState.getPrevActionBin() << ")");
  
  // Update Q-table
  qTable.updateQ(currentState, lastAction, reward, nextState);
  
  // Update statistics
  stats.avgReward = (stats.avgReward * stats.rewardCount + reward) / (stats.rewardCount + 1);
  stats.rewardCount++;
  
  RL_DEBUG_LOG("[RL-GC STATS] Updated average reward: " << std::fixed << std::setprecision(4) 
            << stats.avgReward << " (total rewards: " << stats.rewardCount << ")");
  
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
  
  RL_DEBUG_LOG("[RL-GC PENDING] Scheduled pending Q-value update:" << std::endl
            << "  State: (" << state.getPrevIntervalBin() 
            << "," << state.getCurrIntervalBin() 
            << "," << state.getPrevActionBin() << ")" << std::endl
            << "  Action: " << action);
}

float RLGarbageCollector::processPendingUpdate(uint64_t responseTime) {
  // Check if there's a pending update
  if (!hasPendingUpdate) {
    RL_DEBUG_LOG("[RL-GC PENDING] No pending update to process");
    return 0.0f;
  }
  
  // Calculate reward based on response time
  float reward = calculateReward(responseTime);
  
  // Create next state based on current intervals
  State nextState(
    discretizePrevInterval(prevInterRequestTime),
    discretizeCurrInterval(currInterRequestTime),
    discretizeAction(pendingAction)
  );
  
  RL_DEBUG_LOG("[RL-GC PENDING] Processing pending Q-value update:" << std::endl
            << "  Response time: " << responseTime << "ns" << std::endl
            << "  Reward: " << std::fixed << std::setprecision(4) << reward << std::endl
            << "  Pending state: (" << pendingState.getPrevIntervalBin() 
            << "," << pendingState.getCurrIntervalBin() 
            << "," << pendingState.getPrevActionBin() << ")" << std::endl
            << "  Action: " << pendingAction << std::endl
            << "  Next state: (" << nextState.getPrevIntervalBin() 
            << "," << nextState.getCurrIntervalBin() 
            << "," << nextState.getPrevActionBin() << ")");
  
  // Update Q-table using the pending state and action
  qTable.updateQ(pendingState, pendingAction, reward, nextState);
  
  // Update statistics
  stats.avgReward = (stats.avgReward * stats.rewardCount + reward) / (stats.rewardCount + 1);
  stats.rewardCount++;
  
  // Clear pending update
  hasPendingUpdate = false;
  
  // Decay exploration rate
  qTable.decayEpsilon();
  
  return reward;
}

bool RLGarbageCollector::shouldPerformIntensiveGC(uint32_t freeBlocks) {
  return freeBlocks <= tigcThreshold;
}

bool RLGarbageCollector::shouldExitIntensiveMode(uint32_t freeBlocks) {
  // Exit intensive mode when free blocks > tigcThreshold (default: 3)
  return freeBlocks > tigcThreshold;
}

void RLGarbageCollector::setIntensiveMode(bool enable) {
  if (enable && !inIntensiveMode) {
    RL_DEBUG_LOG("Entering INTENSIVE GC mode with free blocks <= " << tigcThreshold);
    inIntensiveMode = true;
  }
  else if (!enable && inIntensiveMode) {
    RL_DEBUG_LOG("Exiting INTENSIVE GC mode with free blocks > " << tigcThreshold);
    inIntensiveMode = false;
  }
}

bool RLGarbageCollector::isInIntensiveMode() const {
  return inIntensiveMode;
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
                                 uint64_t &intensiveGCs, float &avgReward, uint64_t &erases) {
  // Use a mutex or atomic operations if this is accessed from multiple threads
  invocations = stats.gcInvocations;
  pageCopies = stats.totalPageCopies;
  intensiveGCs = stats.intensiveGCCount;
  avgReward = stats.avgReward;
  erases = stats.eraseCount;
}

void RLGarbageCollector::resetStats() {
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.intensiveGCCount = 0;
  stats.avgReward = 0.0f;
  stats.rewardCount = 0;
  stats.eraseCount = 0;
  
  // Reset intensive mode
  inIntensiveMode = false;
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
  
  if (debugEnabled) {
    std::ofstream debugFile;
    debugFile.open(debugFilePath, std::ios_base::app);
    if (debugFile.is_open()) {
      debugFile << ss.str() << std::endl;
      debugFile.close();
    }
  }
}

void RLGarbageCollector::recordGCInvocation(uint32_t copiedPages) {
  stats.gcInvocations++;
  stats.totalPageCopies += copiedPages;
}

void RLGarbageCollector::recordIntensiveGC() {
  // Note: We don't increment the counter here anymore because we increment it in getGCAction
  // This method is now used primarily to ensure we're in intensive mode and for logging
  
  // Ensure we're in intensive mode
  if (!inIntensiveMode) {
    setIntensiveMode(true);
  }
  
  RL_DEBUG_LOG("[RL-GC STATS] Recorded intensive GC operation. Total intensive GCs: " 
            << stats.intensiveGCCount << ", Intensive mode: " << (inIntensiveMode ? "ON" : "OFF"));
}

void RLGarbageCollector::recordBlockErase() {
  stats.eraseCount++;
  
  RL_DEBUG_LOG("[RL-GC STATS] Recorded block erase. Total erases: " 
            << stats.eraseCount);
}

void RLGarbageCollector::updatePercentileThresholds() {
  // Need at least 100 samples to calculate percentiles
  if (responseTimes.size() < 100) {
    RL_DEBUG_LOG("[RL-GC PERCENTILE] Not enough samples to update thresholds: " 
              << responseTimes.size() << " < 100");
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
  
  RL_DEBUG_LOG("[RL-GC PERCENTILE] Updated thresholds:" << std::endl
            << "  t1 (70%): " << oldT1 << " -> " << t1Threshold << "ns" << std::endl
            << "  t2 (90%): " << oldT2 << " -> " << t2Threshold << "ns" << std::endl
            << "  t3 (99%): " << oldT3 << " -> " << t3Threshold << "ns" << std::endl
            << "  Sample size: " << size << std::endl
            << "  Min response time: " << sortedTimes.front() << "ns" << std::endl
            << "  Max response time: " << sortedTimes.back() << "ns");
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
    
    RL_DEBUG_LOG("[RL-GC REWARD] Simple reward calculation (not enough samples): " 
              << "responseTime=" << responseTime << "ns" 
              << ", reward=" << reward);
    return reward;
  }
  
  // Calculate reward based on percentile thresholds
  if (responseTime <= t1Threshold) {
    // Response time is below 70th percentile - excellent
    reward = 1.0f;
    RL_DEBUG_LOG("[RL-GC REWARD] EXCELLENT response time: " << responseTime 
              << "ns <= t1(" << t1Threshold << "ns), reward=" << reward);
  }
  else if (responseTime <= t2Threshold) {
    // Response time is between 70th and 90th percentile - good
    reward = 0.5f;
    RL_DEBUG_LOG("[RL-GC REWARD] GOOD response time: " << responseTime 
              << "ns <= t2(" << t2Threshold << "ns), reward=" << reward);
  }
  else if (responseTime <= t3Threshold) {
    // Response time is between 90th and 99th percentile - poor
    reward = -0.5f;
    RL_DEBUG_LOG("[RL-GC REWARD] POOR response time: " << responseTime 
              << "ns <= t3(" << t3Threshold << "ns), reward=" << reward);
  }
  else {
    // Response time is above 99th percentile - very poor
    reward = -0.5f;
    RL_DEBUG_LOG("[RL-GC REWARD] VERY POOR response time: " << responseTime 
              << "ns > t3(" << t3Threshold << "ns), reward=" << reward);
  }
  
  return reward;
}

void RLGarbageCollector::setMetricsFilePath(const std::string &basePath) {
  metricsFilePath = basePath + "_metrics.txt";
  
  // Initialize metrics file
  if (metricsEnabled) {
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# RL-Baseline Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <intensive_gc_count> <erases> <avg_reward> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
    else {
      std::cerr << "Warning: Failed to initialize RL-Baseline metrics file" << std::endl;
    }
  }
}

void RLGarbageCollector::outputMetricsToFile() {
  if (!metricsEnabled) {
    return;
  }
  
  std::ofstream metricsFile(metricsFilePath, std::ios::app);
  if (metricsFile.is_open()) {
    // Calculate percentiles for latency reporting
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
                << stats.intensiveGCCount << " "
                << stats.eraseCount << " "
                << std::fixed << std::setprecision(4) << stats.avgReward << " "
                << std::fixed << std::setprecision(2) << avgResponseTime << " "
                << p99 << " "
                << p999 << " "
                << p9999 << std::endl;
    
    metricsFile.close();
  }
  else {
    std::cerr << "Warning: Failed to open RL-Baseline metrics file for writing" << std::endl;
  }
}

void RLGarbageCollector::finalizeMetrics() {
  if (!metricsEnabled) {
    return;
  }
  
  // Make sure to output the latest metrics
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
    if (metricsFilePath.find("intensive") != std::string::npos) {
      summaryFile << "RL-Intensive GC Policy Summary Report" << std::endl;
      summaryFile << "====================================" << std::endl;
      if (inIntensiveMode) {
        summaryFile << "Final Mode: Intensive GC mode (ended in intensive mode)" << std::endl;
      } else {
        summaryFile << "Final Mode: Normal mode (intensive mode was exited)" << std::endl;
      }
    } else {
      summaryFile << "RL-Baseline Policy Summary Report" << std::endl;
      summaryFile << "===========================" << std::endl;
    }
    summaryFile << std::endl;
    
    // Write simulation parameters
    summaryFile << "Simulation Parameters:" << std::endl;
    summaryFile << "---------------------" << std::endl;
    summaryFile << "GC Threshold (TGC): " << tgcThreshold << " free blocks" << std::endl;
    summaryFile << "Intensive GC Threshold (TIGC): " << tigcThreshold << " free blocks" << std::endl;
    summaryFile << "Max Page Copies per GC: " << maxPageCopies << " pages" << std::endl;
    summaryFile << "Q-learning Epsilon: " << qTable.getEpsilon() << std::endl;
    summaryFile << std::endl;
    
    // Write GC statistics
    summaryFile << "GC Statistics:" << std::endl;
    summaryFile << "-------------" << std::endl;
    summaryFile << "Total GC Invocations: " << stats.gcInvocations << std::endl;
    summaryFile << "Total Pages Copied: " << stats.totalPageCopies << std::endl;
    summaryFile << "Intensive GC Operations: " << stats.intensiveGCCount << std::endl;
    
    // Calculate percentage of intensive operations
    if (stats.gcInvocations > 0) {
      float intensivePercentage = (float)stats.intensiveGCCount * 100.0f / stats.gcInvocations;
      summaryFile << "Intensive GC %: " << std::fixed << std::setprecision(2) << intensivePercentage << "%" << std::endl;
    }
    
    summaryFile << "Average Pages per GC: " << (stats.gcInvocations > 0 ? 
      ((float)stats.totalPageCopies / stats.gcInvocations) : 0.0f) << std::endl;
    summaryFile << "Block Erasures: " << stats.eraseCount << std::endl;
    summaryFile << std::endl;
    
    // Write RL-specific statistics
    summaryFile << "RL Statistics:" << std::endl;
    summaryFile << "-------------" << std::endl;
    summaryFile << "Average Reward: " << std::fixed << std::setprecision(4) << stats.avgReward << std::endl;
    summaryFile << "Total Reward Count: " << stats.rewardCount << std::endl;
    summaryFile << std::endl;
    
    // Write performance statistics
    summaryFile << "Performance Metrics:" << std::endl;
    summaryFile << "-------------------" << std::endl;
    summaryFile << "Average Response Time: " << std::fixed << std::setprecision(2) << avgResponseTime << " ns" << std::endl;
    summaryFile << "P99 Latency: " << p99 << " ns" << std::endl;
    summaryFile << "P99.9 Latency: " << p999 << " ns" << std::endl;
    summaryFile << "P99.99 Latency: " << p9999 << " ns" << std::endl;
    
    // Add intensive GC-specific explanation if this is the intensive GC policy
    if (metricsFilePath.find("intensive") != std::string::npos) {
      summaryFile << std::endl;
      summaryFile << "RL Intensive GC Policy Details:" << std::endl;
      summaryFile << "----------------------------" << std::endl;
      summaryFile << "The RL-Intensive GC policy aims to reduce long-tail latency by" << std::endl;
      summaryFile << "performing more aggressive garbage collection when free blocks are critically low." << std::endl;
      summaryFile << "Intensive mode activates when free blocks <= " << tigcThreshold << "." << std::endl;
      summaryFile << "In intensive mode, GC operations copy " << intensiveGCMaxPageCopies << " pages per operation" << std::endl;
      summaryFile << "instead of the 1-2 pages in normal mode, enabling faster reclamation of free blocks." << std::endl;
    }
    summaryFile << std::endl;
    
    // Additional metrics that are relevant according to the research paper
    summaryFile << "Efficiency Metrics:" << std::endl;
    summaryFile << "------------------" << std::endl;
    float avgPagesPerGC = stats.gcInvocations > 0 ? (float)stats.totalPageCopies / stats.gcInvocations : 0;
    summaryFile << "Average Pages Copied per GC: " << std::fixed << std::setprecision(2) << avgPagesPerGC << std::endl;
    
    summaryFile.close();
    
    std::cout << "RL-Baseline summary metrics saved to: " << summaryPath << std::endl;
  }
  else {
    std::cerr << "Warning: Failed to open RL-Baseline summary file for writing" << std::endl;
  }
}

uint64_t RLGarbageCollector::getLatencyPercentile(float percentile) const {
  if (responseTimes.empty()) {
    return 0;
  }
  
  // Make a copy and sort
  std::vector<uint64_t> sortedTimes(responseTimes.begin(), responseTimes.end());
  std::sort(sortedTimes.begin(), sortedTimes.end());
  
  // For very high percentiles, we need to be more precise
  // Calculate the exact position
  float position = (sortedTimes.size() - 1) * percentile / 100.0f;
  size_t idx = static_cast<size_t>(position);
  
  // Ensure we don't go out of bounds
  if (idx >= sortedTimes.size() - 1) {
    return sortedTimes.back(); // Return the maximum value
  }
  
  // For very high percentiles, use linear interpolation between points
  float fraction = position - idx;
  if (fraction > 0 && idx < sortedTimes.size() - 1) {
    return static_cast<uint64_t>(sortedTimes[idx] * (1 - fraction) + 
                                 sortedTimes[idx + 1] * fraction);
  }
  
  return sortedTimes[idx];
}

}  // namespace FTL

}  // namespace SimpleSSD
