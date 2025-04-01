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

#include "ftl/rl_aggressive_gc/rl_aggressive.hh"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>  // For formatting output
#include <fstream>  // For file output
#include <sstream>  // For string stream operations
#include <sys/stat.h>  // For directory creation
#include <cstdlib>  // For srand and rand
#include <ctime>  // For time

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
      std::cerr << "Failed to open RL-Aggressive-GC debug file: " << debugFilePath << std::endl; \
    } \
  }

RLAggressiveGarbageCollector::RLAggressiveGarbageCollector(uint32_t tgc, uint32_t tigc, uint32_t maxCopies,
                                     float alpha, float gamma, float epsilon, uint32_t actions)
    : qTable(alpha, gamma, epsilon, actions),
      lastAction(0),
      lastRequestTime(0),
      currentRequestTime(0),
      prevInterRequestTime(0),
      currInterRequestTime(0),
      maxResponseTimes(1000),  // Changed back to 1000 to match other policies
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
      intensiveGCMaxPageCopies(maxCopies),  // Use maximum for aggressive policy
      tagcThreshold(100),     // Default TAGC threshold from paper: 100
      maxGCOps(2),            // Default max GC operations when between TAGC and TGC: 2
      readTriggeredGCEnabled(true),   // Enable by default
      earlyGCInvalidThreshold(0.6), // 60% invalid pages threshold from paper
      debugEnabled(false),
      debugFilePath("output/rl_aggressive_debug.log"),
      metricsEnabled(false),
      metricsFilePath("output/rl_aggressive_metrics.txt") {
  
  // Initialize random seed for jitter in response times
  srand(static_cast<unsigned int>(time(nullptr)));
  
  // Initialize statistics
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.intensiveGCCount = 0;
  stats.readTriggeredGCCount = 0;
  stats.earlyGCCount = 0;
  stats.avgReward = 0.0f;
  stats.rewardCount = 0;
  stats.eraseCount = 0;
  
  // Initialize current state
  currentState = State(0, 0, 0);
  
  // Create output directory if metrics or debug are enabled
  #ifdef _WIN32
  // Windows
  if (system("if not exist output mkdir output") != 0) {
    std::cerr << "Warning: Failed to create output directory for RL-Aggressive-GC" << std::endl;
  }
  #else
  // Linux/Unix/MacOS
  if (system("mkdir -p output") != 0) {
    std::cerr << "Warning: Failed to create output directory for RL-Aggressive-GC" << std::endl;
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
      metricsFile << "# RL-Aggressive Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <intensive_gc_count> <read_triggered_count> <early_gc_count> <erases> <avg_reward> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
    else {
      std::cerr << "Warning: Failed to initialize RL-Aggressive metrics file" << std::endl;
    }
  }
  
  // Initialize running average tracking
  stats.avgResponseTime = 0.0;
  stats.responseTimeCount = 0;
  
  RL_DEBUG_LOG("[RL-AGG INIT] Initialized RL-Aggressive GC with parameters:" << std::endl
            << "  Learning rate (alpha): " << alpha << std::endl
            << "  Discount factor (gamma): " << gamma << std::endl
            << "  Initial epsilon: " << epsilon << std::endl
            << "  Action count: " << actions << std::endl
            << "  TGC threshold: " << tgc << std::endl
            << "  TIGC threshold: " << tigc << std::endl
            << "  Max page copies: " << maxCopies << std::endl
            << "  TAGC threshold: " << tagcThreshold << std::endl
            << "  Max GC ops: " << maxGCOps << std::endl
            << "  Read-triggered GC: " << (readTriggeredGCEnabled ? "Enabled" : "Disabled") << std::endl
            << "  Early GC Invalid Threshold: " << earlyGCInvalidThreshold << std::endl);
}

RLAggressiveGarbageCollector::~RLAggressiveGarbageCollector() {
  // Finalize metrics if enabled
  if (metricsEnabled) {
    finalizeMetrics();
  }
  
  RL_DEBUG_LOG("[RL-AGG SUMMARY] Final statistics:" << std::endl
            << "  GC invocations: " << stats.gcInvocations << std::endl
            << "  Total page copies: " << stats.totalPageCopies << std::endl
            << "  Intensive GC count: " << stats.intensiveGCCount << std::endl
            << "  Read-triggered GC count: " << stats.readTriggeredGCCount << std::endl
            << "  Early GC count: " << stats.earlyGCCount << std::endl
            << "  Block erases: " << stats.eraseCount << std::endl
            << "  Average reward: " << stats.avgReward);
}

bool RLAggressiveGarbageCollector::shouldTriggerGC(uint32_t freeBlocks, uint64_t currentTime) {
  // Update current time
  currentRequestTime = currentTime;
  
  // Calculate inter-request time
  if (lastRequestTime > 0) {
    prevInterRequestTime = currInterRequestTime;
    currInterRequestTime = currentRequestTime - lastRequestTime;
    
    RL_DEBUG_LOG("[RL-AGG TIME] Inter-request times updated: previous=" 
              << prevInterRequestTime << "ns, current=" 
              << currInterRequestTime << "ns");
  }
  else {
    // First request, no inter-request interval
    prevInterRequestTime = 0;
    currInterRequestTime = 0;
    RL_DEBUG_LOG("[RL-AGG TIME] First request detected, no inter-request times yet");
  }
  
  // Update last request time for next calculation
  lastRequestTime = currentTime;
  
  // Skip if there's no idle time (consecutive requests)
  if (currInterRequestTime == 0) {
    RL_DEBUG_LOG("[RL-AGG DECISION] Not triggering GC: no idle time between requests");
    return false;
  }
  
  // Critical situation - force GC if free blocks are too low
  if (freeBlocks <= tigcThreshold) {
    RL_DEBUG_LOG("[RL-AGG DECISION] CRITICAL: Free blocks (" << freeBlocks 
              << ") below TIGC threshold (" << tigcThreshold 
              << "). Forcing intensive GC.");
    return true;
  }
  
  // ---------- Aggressive Policy: Early GC with TAGC threshold ----------
  // If free blocks fall below the TAGC threshold (100), trigger early GC
  if (freeBlocks <= tagcThreshold) {
    RL_DEBUG_LOG("[RL-AGG DECISION] Aggressive Early GC: Free blocks (" << freeBlocks 
              << ") below TAGC threshold (" << tagcThreshold 
              << "). Triggering early GC.");
    
    // Record early GC stat if it's between TAGC and TGC
    if (freeBlocks > tgcThreshold) {
      stats.earlyGCCount++;
    }
    
    return true;
  }
  
  // Regular GC trigger condition (from baseline policy)
  if (freeBlocks <= tgcThreshold) {
    // Update state based on current intervals
    updateState(currentTime);
    
    RL_DEBUG_LOG("[RL-AGG DECISION] Normal GC trigger: Free blocks (" << freeBlocks 
              << ") below TGC threshold (" << tgcThreshold << ").");
    
    return true;
  }
  
  return false;
}

bool RLAggressiveGarbageCollector::shouldTriggerGCForRead(uint32_t freeBlocks, uint64_t currentTime) {
  // Check if read-triggered GC is disabled
  if (!readTriggeredGCEnabled) {
    return false;
  }
  
  // Calculate inter-request time to check for idle period
  uint64_t interRequestTime = 0;
  if (lastRequestTime > 0) {
    interRequestTime = currentTime - lastRequestTime;
  }
  
  // Only trigger read-based GC if we have sufficient idle time
  // Use similar idle time threshold as in regular shouldTriggerGC
  bool isIdlePeriod = interRequestTime > 0 && 
                      discretizeCurrInterval(interRequestTime) > 2; // Threshold for idle period
  
  // Trigger GC for read operations if free blocks are below threshold
  // AND we're in an idle period (to avoid affecting read latency)
  if (freeBlocks <= tgcThreshold * 1.5 && isIdlePeriod) {
    RL_DEBUG_LOG("[RL-AGG DECISION] Read-Triggered GC: Free blocks (" << freeBlocks 
              << ") below read threshold (" << (tgcThreshold * 1.5) 
              << "), idle time (" << interRequestTime
              << "). Triggering GC for read operation.");
    
    // Record read-triggered GC stat
    stats.readTriggeredGCCount++;
    
    return true;
  }
  
  return false;
}

uint32_t RLAggressiveGarbageCollector::getGCAction(uint32_t freeBlocks) {
  // Check if we're in intensive mode
  if (inIntensiveMode) {
    // Increment intensive GC counter
    stats.intensiveGCCount++;
    
    // For aggressive policy, always return maximum page copies in intensive mode
    RL_DEBUG_LOG("[RL-AGG ACTION] INTENSIVE GC: Using maximum action " 
              << maxPageCopies << " due to being in intensive mode, "
              << freeBlocks << " free blocks, threshold: " << tigcThreshold
              << ", total intensive GCs: " << stats.intensiveGCCount);
    
    // Store last action for next state
    lastAction = maxPageCopies;
    
    // Schedule pending update with current state and action
    schedulePendingUpdate(currentState, lastAction);
    
    return maxPageCopies;
  }
  
  // If free blocks are critically low, use maximum action regardless of Q-table
  if (freeBlocks <= tigcThreshold + 2) {
    uint32_t action = maxPageCopies;
    
    RL_DEBUG_LOG("[RL-AGG ACTION] CRITICAL: Free blocks (" << freeBlocks 
              << ") near TIGC threshold. Using maximum action " << action);
    
    // Update statistics
    stats.gcInvocations++;
    stats.totalPageCopies += action;
    
    // Store last action for next state
    lastAction = action;
    
    // Schedule pending update with current state and action
    schedulePendingUpdate(currentState, lastAction);
    
    return action;
  }
  
  // From paper section 5.3: Max-limited early GC triggering
  // When free blocks are between TAGC and TGC thresholds, limit max GC operations to 2
  if (freeBlocks > tgcThreshold && freeBlocks <= tagcThreshold) {
    // Get action from Q-table
    uint32_t action = qTable.selectAction(currentState);
    
    // Limit to max 2 operations as per paper (maxGCOps parameter)
    if (action > maxGCOps) {
      action = maxGCOps;
      
      RL_DEBUG_LOG("[RL-AGG ACTION] Limiting action to " << maxGCOps 
                << " operations (max-limited early GC)");
    }
    
    RL_DEBUG_LOG("[RL-AGG ACTION] Early GC: Selected action: " << action 
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
  
  // Normal case - use Q-table with bias toward more aggressive actions
  uint32_t action = qTable.selectAction(currentState);
  
  // Biased toward higher values for regular GC (not early GC)
  action = std::max(action, maxPageCopies / 2); // At least half of max copies
  
  // Ensure action is within bounds
  if (action > maxPageCopies) {
    action = maxPageCopies;
  }
  
  RL_DEBUG_LOG("[RL-AGG ACTION] Selected action: " << action 
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

uint32_t RLAggressiveGarbageCollector::getMaxGCAction() {
  // For aggressive policy, always return the maximum action value
  // This follows our research paper approach for the RL aggressive policy
  // which maximizes free blocks by always performing maximum page copies
  
  RL_DEBUG_LOG("[RL-AGG ACTION] Aggressive policy using maximum action " 
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

void RLAggressiveGarbageCollector::updateState(uint64_t currentTime) {
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
  
  RL_DEBUG_LOG("[RL-AGG STATE] State updated: previous=(" 
            << previousState.getPrevIntervalBin() << "," 
            << previousState.getCurrIntervalBin() << "," 
            << previousState.getPrevActionBin() << "), current=(" 
            << currentState.getPrevIntervalBin() << "," 
            << currentState.getCurrIntervalBin() << "," 
            << currentState.getPrevActionBin() << ")" << std::endl
            << "[RL-AGG STATE] Raw intervals: prevInterval=" << prevInterRequestTime 
            << "ns, currInterval=" << currInterRequestTime 
            << "ns, lastAction=" << lastAction);
}

void RLAggressiveGarbageCollector::recordResponseTime(uint64_t responseTime) {
  // Safety check - ignore unreasonably large values
  if (responseTime > UINT64_MAX / 2) {
    RL_DEBUG_LOG("[RL-AGG RESPONSE] Ignoring unreasonable response time: " 
              << responseTime << "ns");
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

float RLAggressiveGarbageCollector::updateQValue(uint64_t responseTime) {
  // Calculate reward
  float reward = calculateReward(responseTime);
  
  // Safety check - make sure we have a valid previous action
  if (lastAction > maxPageCopies) {
    RL_DEBUG_LOG("[RL-AGG Q-UPDATE] Capping last action from " << lastAction 
              << " to " << maxPageCopies);
    lastAction = maxPageCopies;
  }
  
  // Create next state based on current intervals
  State nextState(
    discretizePrevInterval(prevInterRequestTime),
    discretizeCurrInterval(currInterRequestTime),
    discretizeAction(lastAction)
  );
  
  RL_DEBUG_LOG("[RL-AGG Q-UPDATE] Updating Q-value:" << std::endl
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
  
  RL_DEBUG_LOG("[RL-AGG STATS] Updated average reward: " << std::fixed << std::setprecision(4) 
            << stats.avgReward << " (total rewards: " << stats.rewardCount << ")");
  
  // Output metrics periodically if enabled
  if (metricsEnabled && stats.rewardCount % 1000 == 0 && stats.rewardCount > 0) {
    outputMetricsToFile();
  }
  
  // Update current state for next decision
  currentState = nextState;
  
  // Decay exploration rate
  qTable.decayEpsilon();
  
  return reward;
}

void RLAggressiveGarbageCollector::schedulePendingUpdate(State state, uint32_t action) {
  // Store the state and action for later update
  hasPendingUpdate = true;
  pendingState = state;
  pendingAction = action;
  
  RL_DEBUG_LOG("[RL-AGG PENDING] Scheduled pending Q-value update:" << std::endl
            << "  State: (" << state.getPrevIntervalBin() 
            << "," << state.getCurrIntervalBin() 
            << "," << state.getPrevActionBin() << ")" << std::endl
            << "  Action: " << action);
}

float RLAggressiveGarbageCollector::processPendingUpdate(uint64_t responseTime) {
  if (!hasPendingUpdate) {
    RL_DEBUG_LOG("[RL-AGG PENDING] No pending update to process");
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
  
  RL_DEBUG_LOG("[RL-AGG PENDING] Processing pending Q-value update:" << std::endl
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
  
  // Output metrics periodically if enabled
  if (metricsEnabled && stats.rewardCount % 1000 == 0 && stats.rewardCount > 0) {
    outputMetricsToFile();
  }
  
  // Clear pending update
  hasPendingUpdate = false;
  
  // Decay exploration rate
  qTable.decayEpsilon();
  
  return reward;
}

bool RLAggressiveGarbageCollector::shouldPerformIntensiveGC(uint32_t freeBlocks) {
  return freeBlocks <= tigcThreshold;
}

bool RLAggressiveGarbageCollector::shouldExitIntensiveMode(uint32_t freeBlocks) {
  // For aggressive policy, only exit intensive mode when we have significantly 
  // more free blocks than the threshold
  return freeBlocks > (tigcThreshold + 2);
}

void RLAggressiveGarbageCollector::setIntensiveMode(bool enable) {
  if (enable && !inIntensiveMode) {
    RL_DEBUG_LOG("[RL-AGG MODE] Entering INTENSIVE GC mode with free blocks <= " << tigcThreshold);
    inIntensiveMode = true;
  }
  else if (!enable && inIntensiveMode) {
    RL_DEBUG_LOG("[RL-AGG MODE] Exiting INTENSIVE GC mode with free blocks > " << tigcThreshold);
    inIntensiveMode = false;
  }
}

bool RLAggressiveGarbageCollector::isInIntensiveMode() const {
  return inIntensiveMode;
}

uint32_t RLAggressiveGarbageCollector::discretizePrevInterval(uint64_t interval) {
  // 2 bins as per the paper: "short" if less than 100μs, or "long" otherwise
  if (interval < 100000) return 0;  // < 100μs (short)
  return 1;                         // >= 100μs (long)
}

uint32_t RLAggressiveGarbageCollector::discretizeCurrInterval(uint64_t interval) {
  // 17 bins as per the paper for finer granularity
  if (interval == 0) return 0;
  
  // Define thresholds for the 17 bins (in nanoseconds)
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

uint32_t RLAggressiveGarbageCollector::discretizeAction(uint32_t action) {
  // 2 bins as per the paper: actions less than or equal to half of max, or above
  if (action <= maxPageCopies / 2) return 0;  // <= half of max
  return 1;                                   // > half of max
}

uint32_t RLAggressiveGarbageCollector::getTGCThreshold() const {
  return tgcThreshold;
}

uint32_t RLAggressiveGarbageCollector::getTIGCThreshold() const {
  return tigcThreshold;
}

uint32_t RLAggressiveGarbageCollector::getMaxPageCopies() const {
  return maxPageCopies;
}

uint32_t RLAggressiveGarbageCollector::getTAGCThreshold() const {
  return tagcThreshold;
}

uint32_t RLAggressiveGarbageCollector::getMaxGCOps() const {
  return maxGCOps;
}

const State& RLAggressiveGarbageCollector::getCurrentState() const {
  return currentState;
}

void RLAggressiveGarbageCollector::getStats(uint64_t &invocations, uint64_t &pageCopies, 
                                         uint64_t &intensiveGCs, uint64_t &readTriggeredGCs, 
                                         uint64_t &earlyGCs, float &avgReward, uint64_t &erases) {
  // Use a mutex or atomic operations if this is accessed from multiple threads
  invocations = stats.gcInvocations;
  pageCopies = stats.totalPageCopies;
  intensiveGCs = stats.intensiveGCCount;
  readTriggeredGCs = stats.readTriggeredGCCount;
  earlyGCs = stats.earlyGCCount;
  avgReward = stats.avgReward;
  erases = stats.eraseCount;
}

void RLAggressiveGarbageCollector::resetStats() {
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.intensiveGCCount = 0;
  stats.readTriggeredGCCount = 0;
  stats.earlyGCCount = 0;
  stats.avgReward = 0.0f;
  stats.rewardCount = 0;
  stats.eraseCount = 0;
  
  // Reset intensive mode and consecutive GC counter
  inIntensiveMode = false;
}

void RLAggressiveGarbageCollector::recordGCInvocation(uint32_t copiedPages) {
  stats.gcInvocations++;
  stats.totalPageCopies += copiedPages;
}

void RLAggressiveGarbageCollector::recordIntensiveGC() {
  // Ensure we're in intensive mode
  if (!inIntensiveMode) {
    setIntensiveMode(true);
  }
  
  stats.intensiveGCCount++;
  
  RL_DEBUG_LOG("[RL-AGG STATS] Recorded intensive GC operation. Total intensive GCs: " 
            << stats.intensiveGCCount << ", Intensive mode: " << (inIntensiveMode ? "ON" : "OFF"));
}

void RLAggressiveGarbageCollector::recordReadTriggeredGC() {
  stats.readTriggeredGCCount++;
  
  RL_DEBUG_LOG("[RL-AGG STATS] Recorded read-triggered GC. Total read-triggered GCs: " 
            << stats.readTriggeredGCCount);
}

void RLAggressiveGarbageCollector::recordEarlyGC() {
  stats.earlyGCCount++;
  
  RL_DEBUG_LOG("[RL-AGG STATS] Recorded early GC. Total early GCs: " 
            << stats.earlyGCCount);
}

void RLAggressiveGarbageCollector::recordBlockErase() {
  stats.eraseCount++;
  
  RL_DEBUG_LOG("[RL-AGG STATS] Recorded block erase. Total erases: " 
            << stats.eraseCount);
}

void RLAggressiveGarbageCollector::printDebugInfo() const {
  std::stringstream ss;
  ss << "=== RL-Aggressive-GC Debug Information ===" << std::endl;
  ss << "Current state: " << 
    "prevIntervalBin=" << currentState.getPrevIntervalBin() << 
    ", currIntervalBin=" << currentState.getCurrIntervalBin() << 
    ", prevActionBin=" << currentState.getPrevActionBin() << std::endl;
  
  ss << "Last action taken: " << lastAction << std::endl;
  ss << "Free blocks thresholds: tgc=" << tgcThreshold << ", tigc=" << tigcThreshold
     << ", tagc=" << tagcThreshold << std::endl;
  ss << "Statistics: GC invocations=" << stats.gcInvocations
     << ", page copies=" << stats.totalPageCopies
     << ", intensive GCs=" << stats.intensiveGCCount
     << ", read-triggered GCs=" << stats.readTriggeredGCCount
     << ", early GCs=" << stats.earlyGCCount
     << ", avg reward=" << stats.avgReward << std::endl;
  
  // Since we can't access the Q-table entries directly, print summary info
  ss << "Q-table summary: epsilon=" << qTable.getEpsilon() 
     << ", GC count=" << qTable.getGCCount() << std::endl;
  
  ss << "========================================" << std::endl;
  
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

void RLAggressiveGarbageCollector::updatePercentileThresholds() {
  // Need at least 100 samples to calculate percentiles
  if (responseTimes.size() < 100) {
    RL_DEBUG_LOG("[RL-AGG PERCENTILE] Not enough samples to update thresholds: " 
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
  
  RL_DEBUG_LOG("[RL-AGG PERCENTILE] Updated thresholds:" << std::endl
            << "  t1 (70%): " << oldT1 << " -> " << t1Threshold << "ns" << std::endl
            << "  t2 (90%): " << oldT2 << " -> " << t2Threshold << "ns" << std::endl
            << "  t3 (99%): " << oldT3 << " -> " << t3Threshold << "ns" << std::endl
            << "  Sample size: " << size << std::endl
            << "  Min response time: " << sortedTimes.front() << "ns" << std::endl
            << "  Max response time: " << sortedTimes.back() << "ns");
}

float RLAggressiveGarbageCollector::calculateReward(uint64_t responseTime) {
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
    
    RL_DEBUG_LOG("[RL-AGG REWARD] Simple reward calculation (not enough samples): " 
              << "responseTime=" << responseTime << "ns" 
              << ", reward=" << reward);
    return reward;
  }
  
  // Calculate reward based on percentile thresholds
  if (responseTime <= t1Threshold) {
    // Response time is below 70th percentile - excellent
    reward = 1.0f;
    RL_DEBUG_LOG("[RL-AGG REWARD] EXCELLENT response time: " << responseTime 
              << "ns <= t1(" << t1Threshold << "ns), reward=" << reward);
  }
  else if (responseTime <= t2Threshold) {
    // Response time is between 70th and 90th percentile - good
    reward = 0.5f;
    RL_DEBUG_LOG("[RL-AGG REWARD] GOOD response time: " << responseTime 
              << "ns <= t2(" << t2Threshold << "ns), reward=" << reward);
  }
  else if (responseTime <= t3Threshold) {
    // Response time is between 90th and 99th percentile - poor
    reward = -0.5f;
    RL_DEBUG_LOG("[RL-AGG REWARD] POOR response time: " << responseTime 
              << "ns <= t3(" << t3Threshold << "ns), reward=" << reward);
  }
  else {
    // Response time is above 99th percentile - very poor
    reward = -0.5f;
    RL_DEBUG_LOG("[RL-AGG REWARD] VERY POOR response time: " << responseTime 
              << "ns > t3(" << t3Threshold << "ns), reward=" << reward);
  }
  
  return reward;
}

uint64_t RLAggressiveGarbageCollector::getLatencyPercentile(float percentile) const {
  if (responseTimes.size() < 10) {
    return 0;  // Not enough data
  }
  
  std::vector<uint64_t> sortedTimes(responseTimes.begin(), responseTimes.end());
  std::sort(sortedTimes.begin(), sortedTimes.end());
  
  // Calculate the correct index for the desired percentile
  // percentile should be between 0.0 and 1.0, not 0-100
  if (percentile > 1.0) {
    percentile /= 100.0; // Convert from 99.0 to 0.99 format if necessary
  }
  
  // For very high percentiles, we need to be more precise with interpolation
  float position = (sortedTimes.size() - 1) * percentile;
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

void RLAggressiveGarbageCollector::setMetricsFilePath(const std::string &basePath) {
  metricsFilePath = basePath + "_metrics.txt";
  
  // Initialize metrics file
  if (metricsEnabled) {
    std::ofstream metricsFile(metricsFilePath, std::ios::trunc);
    if (metricsFile.is_open()) {
      metricsFile << "# RL-Aggressive Metrics" << std::endl;
      metricsFile << "# Format: <timestamp> <gc_invocations> <page_copies> <intensive_gc_count> <read_triggered_count> <early_gc_count> <erases> <avg_reward> <avg_response_time> <p99_latency> <p99.9_latency> <p99.99_latency>" << std::endl;
      metricsFile.close();
    }
    else {
      std::cerr << "Warning: Failed to initialize RL-Aggressive metrics file" << std::endl;
    }
  }
}

void RLAggressiveGarbageCollector::outputMetricsToFile() {
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
      p99 = getLatencyPercentile(0.99);
      p999 = getLatencyPercentile(0.999);
      p9999 = getLatencyPercentile(0.9999);
      
      // Calculate average response time safely
      double sum = 0.0;
      for (uint64_t time : responseTimes) {
        // Convert to double to avoid uint64_t overflow
        sum += static_cast<double>(time);
      }
      avgResponseTime = sum / responseTimes.size();
      
      // Safety check - if the average is unreasonably large, cap it
      if (avgResponseTime > 1e16) {
        avgResponseTime = std::accumulate(responseTimes.begin(), responseTimes.begin() + 100, 0.0) / 100.0;
        RL_DEBUG_LOG("[RL-AGG METRICS] Average response time capped due to potential overflow");
      }
    }
    
    // Write metrics line
    metricsFile << currentRequestTime << " "
                << stats.gcInvocations << " "
                << stats.totalPageCopies << " "
                << stats.intensiveGCCount << " "
                << stats.readTriggeredGCCount << " "
                << stats.earlyGCCount << " "
                << stats.eraseCount << " "
                << std::fixed << std::setprecision(4) << stats.avgReward << " "
                << std::fixed << std::setprecision(2) << avgResponseTime << " "
                << p99 << " "
                << p999 << " "
                << p9999 << std::endl;
    
    metricsFile.close();
    
    // // Print to stdout for easier tracking
    // std::cout << "RL-Aggressive metrics written to file: " << metricsFilePath
    //           << " (GC invocations: " << stats.gcInvocations 
    //           << ", erases: " << stats.eraseCount << ")" << std::endl;
    
    // Print debug information about percentile values
    RL_DEBUG_LOG("[RL-AGG METRICS] Wrote metrics: p99=" << p99 
              << ", p99.9=" << p999 
              << ", p99.99=" << p9999
              << ", avgResponse=" << avgResponseTime);
  }
  else {
    std::cerr << "Warning: Failed to open RL-Aggressive metrics file for writing" << std::endl;
  }
}

void RLAggressiveGarbageCollector::finalizeMetrics() {
  if (!metricsEnabled) {
    return;
  }
  
  // Make sure to output the latest metrics
  outputMetricsToFile();
  
  // Create a unified summary file
  std::string summaryPath = metricsFilePath;
  // Remove the "_metrics.txt" suffix if present
  size_t metricsSuffix = summaryPath.find("_metrics.txt");
  if (metricsSuffix != std::string::npos) {
    summaryPath = summaryPath.substr(0, metricsSuffix);
  }
  summaryPath += "_summary.txt";
  
  std::ofstream summaryFile(summaryPath, std::ios::trunc);
  if (summaryFile.is_open()) {
    // Calculate percentiles for final reporting
    uint64_t p99 = 0;
    uint64_t p999 = 0;
    uint64_t p9999 = 0;
    double avgResponseTime = 0.0;
    
    if (responseTimes.size() >= 100) {
      p99 = getLatencyPercentile(0.99);
      p999 = getLatencyPercentile(0.999);
      p9999 = getLatencyPercentile(0.9999);
      
      // Calculate average response time safely
      double sum = 0.0;
      for (uint64_t time : responseTimes) {
        // Convert to double to avoid uint64_t overflow
        sum += static_cast<double>(time);
      }
      avgResponseTime = sum / responseTimes.size();
      
      // Safety check - if the average is unreasonably large, cap it
      if (avgResponseTime > 1e16) {
        avgResponseTime = std::accumulate(responseTimes.begin(), responseTimes.begin() + 100, 0.0) / 100.0;
        RL_DEBUG_LOG("[RL-AGG METRICS] Average response time capped due to potential overflow");
      }
    }
    
    // Write summary header
    summaryFile << "RL-Aggressive GC Policy Summary Report" << std::endl;
    summaryFile << "=====================================" << std::endl;
    
    if (inIntensiveMode) {
      summaryFile << "Final Mode: Intensive GC mode (ended in intensive mode)" << std::endl;
    } else {
      summaryFile << "Final Mode: Normal mode (intensive mode was exited)" << std::endl;
    }
    summaryFile << std::endl;
    
    // Write simulation parameters
    summaryFile << "Simulation Parameters:" << std::endl;
    summaryFile << "---------------------" << std::endl;
    summaryFile << "Regular GC Threshold: " << tgcThreshold << " free blocks" << std::endl;
    summaryFile << "Intensive GC Threshold: " << tigcThreshold << " free blocks" << std::endl;
    summaryFile << "TAGC Threshold: " << tagcThreshold << " free blocks" << std::endl;
    summaryFile << "Max Page Copies per GC: " << maxPageCopies << " pages" << std::endl;
    summaryFile << "Max GC Operations: " << maxGCOps << std::endl;
    summaryFile << "Read-Triggered GC: " << (readTriggeredGCEnabled ? "Enabled" : "Disabled") << std::endl;
    summaryFile << std::endl;
    
    // Write GC statistics
    summaryFile << "GC Statistics:" << std::endl;
    summaryFile << "-------------" << std::endl;
    summaryFile << "Total GC Invocations: " << stats.gcInvocations << std::endl;
    summaryFile << "Total Pages Copied: " << stats.totalPageCopies << std::endl;
    summaryFile << "Intensive GC Count: " << stats.intensiveGCCount << std::endl;
    summaryFile << "Read-Triggered GC Count: " << stats.readTriggeredGCCount << std::endl;
    summaryFile << "Early GC Count: " << stats.earlyGCCount << std::endl;
    summaryFile << "Block Erasures: " << stats.eraseCount << std::endl;
    summaryFile << std::endl;
    
    // Write performance metrics
    summaryFile << "Performance Metrics:" << std::endl;
    summaryFile << "-------------------" << std::endl;
    summaryFile << "Average Response Time: " << std::fixed << std::setprecision(2) << avgResponseTime << " ns" << std::endl;
    summaryFile << "P99 Latency: " << p99 << " ns (" << (p99 / 1000000.0) << " ms)" << std::endl;
    summaryFile << "P99.9 Latency: " << p999 << " ns (" << (p999 / 1000000.0) << " ms)" << std::endl;
    summaryFile << "P99.99 Latency: " << p9999 << " ns (" << (p9999 / 1000000.0) << " ms)" << std::endl;
    summaryFile << std::endl;
    
    // Write efficiency metrics
    summaryFile << "Efficiency Metrics:" << std::endl;
    summaryFile << "------------------" << std::endl;
    summaryFile << "Average Pages Copied per GC: " << std::fixed << std::setprecision(2) 
                << (stats.gcInvocations > 0 ? (float)stats.totalPageCopies / stats.gcInvocations : 0) << std::endl;
    summaryFile << "Average Reward: " << std::fixed << std::setprecision(4) << stats.avgReward << std::endl;
    summaryFile << std::endl;
    
    // Write summary footer
    summaryFile << "Note: The RL-Aggressive policy combines TAGC early GC, read-triggered GC," << std::endl;
    summaryFile << "and other techniques to minimize long-tail latency at the expense of slightly higher GC frequency." << std::endl;
    
    summaryFile.close();
    
    std::cout << "RL-Aggressive summary metrics saved to: " << summaryPath << std::endl;
  }
  else {
    std::cerr << "Warning: Failed to create RL-Aggressive summary file" << std::endl;
  }
}

void RLAggressiveGarbageCollector::setup(ConfigReader &cfg) {
  // Get RL-Aggressive specific configurations
  uint32_t tagcThresholdCfg = (uint32_t)cfg.readUint(CONFIG_FTL, FTL_RL_AGG_TAGC_THRESHOLD);
  uint32_t maxGCOpsCfg = (uint32_t)cfg.readUint(CONFIG_FTL, FTL_RL_AGG_MAX_GC_OPS);
  bool readTriggeredGCEnabledCfg = cfg.readBoolean(CONFIG_FTL, FTL_RL_AGG_READ_TRIGGERED_GC);
  bool debugEnabledCfg = cfg.readBoolean(CONFIG_FTL, FTL_RL_AGG_DEBUG_ENABLE);
  bool metricsEnabledCfg = cfg.readBoolean(CONFIG_FTL, FTL_RL_AGG_METRICS_ENABLE);
  
  // Update parameters if configuration values are valid
  if (tagcThresholdCfg > 0) {
    tagcThreshold = tagcThresholdCfg;
  }
  
  if (maxGCOpsCfg > 0) {
    maxGCOps = maxGCOpsCfg;
  }
  
  // Update boolean flags
  readTriggeredGCEnabled = readTriggeredGCEnabledCfg;
  debugEnabled = debugEnabledCfg;
  metricsEnabled = metricsEnabledCfg;
  
  // Log setup completion
  RL_DEBUG_LOG("[RL-AGG SETUP] Configuration updated:" << std::endl
            << "  TAGC threshold: " << tagcThreshold << std::endl
            << "  Max GC ops: " << maxGCOps << std::endl
            << "  Read-triggered GC: " << (readTriggeredGCEnabled ? "Enabled" : "Disabled") << std::endl
            << "  Debug: " << (debugEnabled ? "Enabled" : "Disabled") << std::endl
            << "  Metrics: " << (metricsEnabled ? "Enabled" : "Disabled"));
}

}  // namespace FTL

}  // namespace SimpleSSD 