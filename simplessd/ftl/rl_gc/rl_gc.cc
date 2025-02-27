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
      debugEnabled(false),
      debugFilePath("output/rl_gc_debug.log") {
  
  // Initialize statistics
  stats.gcInvocations = 0;
  stats.totalPageCopies = 0;
  stats.intensiveGCCount = 0;
  stats.avgReward = 0.0f;
  stats.rewardCount = 0;
  
  // Initialize current state
  currentState = State(0, 0, 0);
  
  // Create/clear the debug file if debug is enabled
  if (debugEnabled) {
    std::ofstream debugFile(debugFilePath, std::ios::trunc);
    if (debugFile.is_open()) {
      debugFile.close();
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
  // Critical situation - force intensive GC
  if (freeBlocks <= tigcThreshold) {
    // Return maximum action (most aggressive GC)
    stats.intensiveGCCount++;
    RL_DEBUG_LOG("[RL-GC ACTION] INTENSIVE GC: Using maximum action " 
              << maxPageCopies << " due to critical free blocks (" 
              << freeBlocks << " <= " << tigcThreshold << ")");
    return maxPageCopies;
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
  // Safety check - ignore unreasonable response times
  if (responseTime > UINT64_MAX / 2) {
    RL_DEBUG_LOG("[RL-GC RESPONSE] Ignoring unreasonable response time: " 
              << responseTime << "ns");
    return;
  }
  
  // Add to response time history
  responseTimes.push_back(responseTime);
  
  // Keep history size limited
  while (responseTimes.size() > maxResponseTimes) {
    responseTimes.pop_front();
  }
  
  RL_DEBUG_LOG("[RL-GC RESPONSE] Recorded response time: " << responseTime 
            << "ns, history size: " << responseTimes.size() << "/" 
            << maxResponseTimes);
  
  // Update percentile thresholds if we have enough data
  if (responseTimes.size() >= 100) {
    updatePercentileThresholds();
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

bool RLGarbageCollector::shouldPerformIntensiveGC(uint32_t freeBlocks) {
  return freeBlocks <= tigcThreshold;
}

uint32_t RLGarbageCollector::discretizePrevInterval(uint64_t interval) {
  // Simple discretization - can be improved
  if (interval == 0) return 0;
  if (interval < 1000000) return 1;  // < 1ms
  if (interval < 10000000) return 2; // < 10ms
  return 3; // >= 10ms
}

uint32_t RLGarbageCollector::discretizeCurrInterval(uint64_t interval) {
  // Same as prev interval for now
  return discretizePrevInterval(interval);
}

uint32_t RLGarbageCollector::discretizeAction(uint32_t action) {
  // Just return action directly if within range
  return action < maxPageCopies ? action : 0;
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
  stats.intensiveGCCount++;
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
    reward = responseTime < 1000000 ? 1.0f : 0.0f;
    RL_DEBUG_LOG("[RL-GC REWARD] Simple reward calculation (not enough samples): " 
              << "responseTime=" << responseTime << "ns" 
              << ", threshold=1000000ns"
              << ", reward=" << reward);
    return reward;
  }
  
  // Calculate reward based on percentile thresholds
  if (responseTime <= t1Threshold) {
    // Response time is below 70th percentile - good
    reward = 1.0f;
    RL_DEBUG_LOG("[RL-GC REWARD] GOOD response time: " << responseTime 
              << "ns <= t1(" << t1Threshold << "ns), reward=" << reward);
  }
  else if (responseTime <= t2Threshold) {
    // Response time is between 70th and 90th percentile - neutral
    reward = 0.5f;
    RL_DEBUG_LOG("[RL-GC REWARD] NEUTRAL response time: " << responseTime 
              << "ns <= t2(" << t2Threshold << "ns), reward=" << reward);
  }
  else if (responseTime <= t3Threshold) {
    // Response time is between 90th and 99th percentile - bad
    reward = -0.5f;
    RL_DEBUG_LOG("[RL-GC REWARD] BAD response time: " << responseTime 
              << "ns <= t3(" << t3Threshold << "ns), reward=" << reward);
  }
  else {
    // Response time is above 99th percentile - very bad
    reward = -1.0f;
    RL_DEBUG_LOG("[RL-GC REWARD] VERY BAD response time: " << responseTime 
              << "ns > t3(" << t3Threshold << "ns), reward=" << reward);
  }
  
  return reward;
}

}  // namespace FTL

}  // namespace SimpleSSD
