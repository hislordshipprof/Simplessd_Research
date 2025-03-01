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

#include "ftl/rl_gc/q_table.hh"
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iostream>  // For debug printing
#include <sstream>   // For string stream operations
#include <iomanip>   // For std::setprecision

namespace SimpleSSD {

namespace FTL {

// Helper function to write debug messages
void writeQTableDebug(const std::string &message) {
  std::ofstream debugFile;
  debugFile.open("output/rl_gc_debug.log", std::ios_base::app);
  if (debugFile.is_open()) {
    debugFile << message << std::endl;
    debugFile.close();
  } else {
    std::cerr << "Failed to open RL-GC debug file for Q-table" << std::endl;
  }
}

// State Implementation
State::State() : prevIntervalBin(0), currIntervalBin(0), prevActionBin(0) {}

State::State(uint32_t prev, uint32_t curr, uint32_t action)
    : prevIntervalBin(prev), currIntervalBin(curr), prevActionBin(action) {}

bool State::operator==(const State &other) const {
  return prevIntervalBin == other.prevIntervalBin &&
         currIntervalBin == other.currIntervalBin &&
         prevActionBin == other.prevActionBin;
}

std::size_t State::StateHash::operator()(const State &state) const {
  // Simple hash function for the state
  return (state.getPrevIntervalBin() << 16) | 
         (state.getCurrIntervalBin() << 4) | 
         state.getPrevActionBin();
}

uint32_t State::getPrevIntervalBin() const {
  return prevIntervalBin;
}

uint32_t State::getCurrIntervalBin() const {
  return currIntervalBin;
}

uint32_t State::getPrevActionBin() const {
  return prevActionBin;
}

// QTable Implementation
QTable::QTable(float learningRate, float discountFactor, float initialEpsilon, uint32_t actionCount)
    : alpha(learningRate),
      gamma(discountFactor),
      epsilon(initialEpsilon),
      gcCount(0),
      numActions(actionCount),
      convergenceThreshold(0.01f),  // 0.01 is a reasonable threshold for Q-value changes
      hasConverged(false) {
  // Seed the random number generator
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  rng.seed(seed);
  
  // Ensure epsilon starts at 0.8 (80%) if not explicitly set otherwise
  // This matches the paper's recommendation for high initial exploration
  if (epsilon <= 0.0f || epsilon > 1.0f) {
    epsilon = 0.8f;
    std::stringstream ss;
    ss << "[RL-DEBUG] Invalid epsilon value provided, defaulting to 0.8 (80%)";
    writeQTableDebug(ss.str());
  }
  
  std::stringstream ss;
  ss << "[RL-DEBUG] QTable initialized with alpha=" << alpha 
     << ", gamma=" << gamma << ", epsilon=" << epsilon 
     << ", actions=" << numActions
     << ", convergenceThreshold=" << convergenceThreshold;
  writeQTableDebug(ss.str());
}

QTable::~QTable() {}

uint32_t QTable::selectAction(const State &state) {
  // Increment GC operation count
  gcCount++;
  
  // Decay epsilon after 1000 GC operations
  if (gcCount >= 1000 && epsilon > 0.01f) {
    epsilon = 0.01f;
    std::stringstream ss;
    ss << "[RL-DEBUG] Epsilon decayed to " << epsilon << " after " << gcCount << " GC operations";
    writeQTableDebug(ss.str());
  }
  
  // Epsilon-greedy action selection
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float randomValue = dist(rng);
  if (randomValue < epsilon) {
    // Exploration: choose a random action
    std::uniform_int_distribution<uint32_t> actionDist(0, numActions - 1);
    uint32_t randomAction = actionDist(rng);
    
    std::stringstream ss;
    ss << "[RL-DEBUG] EXPLORE: State(" << state.getPrevIntervalBin() << "," 
       << state.getCurrIntervalBin() << "," << state.getPrevActionBin() 
       << ") - Random action " << randomAction << " (epsilon=" << epsilon 
       << ", random=" << randomValue << ")";
    writeQTableDebug(ss.str());
    
    return randomAction;
  }
  else {
    // Exploitation: choose the best action for this state
    auto it = table.find(state);
    if (it == table.end()) {
      // State not found in Q-table, initialize it with zeros
      table[state] = std::vector<float>(numActions, 0.0f);
      
      // Return a random action for new state
      std::uniform_int_distribution<uint32_t> actionDist(0, numActions - 1);
      uint32_t randomAction = actionDist(rng);
      
      std::stringstream ss;
      ss << "[RL-DEBUG] NEW STATE: State(" << state.getPrevIntervalBin() << "," 
         << state.getCurrIntervalBin() << "," << state.getPrevActionBin() 
         << ") - Using random action " << randomAction;
      writeQTableDebug(ss.str());
      
      return randomAction;
    }
    
    // Find action with maximum Q-value
    auto& values = it->second;
    uint32_t bestAction = std::distance(values.begin(), 
                         std::max_element(values.begin(), values.end()));
    
    std::stringstream ss;
    ss << "[RL-DEBUG] EXPLOIT: State(" << state.getPrevIntervalBin() << "," 
       << state.getCurrIntervalBin() << "," << state.getPrevActionBin() 
       << ") - Best action " << bestAction << " with Q-value " << values[bestAction] 
       << " (epsilon=" << epsilon << ", random=" << randomValue << ")";
    writeQTableDebug(ss.str());
    
    // Print all Q-values for this state
    ss.str("");
    ss << "[RL-DEBUG] Q-values for state: ";
    for (uint32_t i = 0; i < values.size(); i++) {
      ss << i << ":" << values[i] << " ";
    }
    writeQTableDebug(ss.str());
    
    return bestAction;
  }
}

void QTable::updateQ(const State &state, uint32_t action, float reward, const State &nextState) {
  // Ensure state exists in the Q-table
  if (table.find(state) == table.end()) {
    table[state] = std::vector<float>(numActions, 0.0f);
    
    std::stringstream ss;
    ss << "[RL-DEBUG] Created new state entry in Q-table for State(" 
       << state.getPrevIntervalBin() << "," << state.getCurrIntervalBin() 
       << "," << state.getPrevActionBin() << ")";
    writeQTableDebug(ss.str());
  }
  
  // Ensure next state exists in the Q-table
  if (table.find(nextState) == table.end()) {
    table[nextState] = std::vector<float>(numActions, 0.0f);
    
    std::stringstream ss;
    ss << "[RL-DEBUG] Created new next state entry in Q-table for State(" 
       << nextState.getPrevIntervalBin() << "," << nextState.getCurrIntervalBin() 
       << "," << nextState.getPrevActionBin() << ")";
    writeQTableDebug(ss.str());
  }
  
  // Get current Q-value
  float currentQ = table[state][action];
  
  // Find maximum Q-value for next state
  float maxNextQ = *std::max_element(table[nextState].begin(), table[nextState].end());
  
  // Q-learning update rule
  float newQ = currentQ + alpha * (reward + gamma * maxNextQ - currentQ);
  
  std::stringstream ss;
  ss << "[RL-DEBUG] Q-UPDATE: State(" << state.getPrevIntervalBin() << "," 
     << state.getCurrIntervalBin() << "," << state.getPrevActionBin() 
     << "), Action=" << action << ", Reward=" << reward 
     << " | Q-value: " << currentQ << " -> " << newQ 
     << " (maxNextQ=" << maxNextQ << ")";
  writeQTableDebug(ss.str());
  
  // Update the Q-value
  table[state][action] = newQ;
}

float QTable::getQValue(const State &state, uint32_t action) {
  auto it = table.find(state);
  if (it == table.end() || action >= numActions) {
    return 0.0f;  // Default value for unknown state/action
  }
  return it->second[action];
}

void QTable::decayEpsilon() {
  // Gradually reduce epsilon over time
  float oldEpsilon = epsilon;
  
  // After 1000 GC operations, reduce epsilon to 0.01 (1%)
  if (gcCount >= 1000 && epsilon > 0.01f) {
    epsilon = 0.01f;
    
    std::stringstream ss;
    ss << "[RL-DEBUG] Epsilon reduced to 1% after " << gcCount << " GC operations";
    writeQTableDebug(ss.str());
  }
  // Otherwise, apply a gradual decay that will reach ~0.1 by 1000 operations
  else if (epsilon > 0.01f) {
    // Use a slower decay rate to maintain higher exploration early on
    epsilon = std::max(0.01f, epsilon * 0.998f);
    
    // Only log significant changes to avoid excessive logging
    if (std::abs(oldEpsilon - epsilon) > 0.01f) {
      std::stringstream ss;
      ss << "[RL-DEBUG] Epsilon decayed: " << std::fixed << std::setprecision(4) 
         << oldEpsilon << " -> " << epsilon;
      writeQTableDebug(ss.str());
    }
  }
}

float QTable::getEpsilon() const {
  return epsilon;
}

void QTable::setEpsilon(float newEpsilon) {
  epsilon = newEpsilon;
}

uint64_t QTable::getGCCount() const {
  return gcCount;
}

/**
 * Export Q-table to CSV file for visualization
 */
void QTable::exportQTableToCSV(const std::string &filename) {
  std::ofstream outFile(filename);
  if (!outFile.is_open()) {
    return;
  }
  
  // CSV header
  outFile << "PrevInterval,CurrInterval,PrevAction";
  for (uint32_t a = 0; a < numActions; a++) {
    outFile << ",Action" << a;
  }
  outFile << ",BestAction" << std::endl;
  
  // Export each state-action value
  for (const auto &entry : table) {
    const State &state = entry.first;
    const std::vector<float> &values = entry.second;
    
    outFile << state.getPrevIntervalBin() << "," 
            << state.getCurrIntervalBin() << "," 
            << state.getPrevActionBin();
    
    // Export Q-values for each action
    for (uint32_t a = 0; a < numActions; a++) {
      outFile << "," << values[a];
    }
    
    // Find and export the best action
    uint32_t bestAction = std::distance(values.begin(), 
                     std::max_element(values.begin(), values.end()));
    outFile << "," << bestAction << std::endl;
  }
  
  outFile.close();
}

/**
 * Get the maximum change in Q-values since the last check
 */
float QTable::getMaxQValueDelta() {
  float maxDelta = 0.0f;
  
  // Debug output
  std::stringstream debug;
  debug << "[RL-DEBUG] Calculating max Q-value delta across " << table.size() << " states";
  writeQTableDebug(debug.str());
  
  // Store if we've just been called for export without any Q-table updates in between
  static float lastMaxDelta = 0.0f;
  static uint64_t lastGCCount = 0;
  
  // If there've been no new GC operations (no Q-table updates), return the previous result
  if (gcCount == lastGCCount && gcCount > 0) {
    debug.str("");
    debug << "[RL-DEBUG] Returning cached max delta: " << lastMaxDelta << " (no Q-table updates since last check)";
    writeQTableDebug(debug.str());
    return lastMaxDelta;
  }
  
  for (const auto &entry : table) {
    const State &state = entry.first;
    const std::vector<float> &values = entry.second;
    
    for (uint32_t a = 0; a < numActions; a++) {
      float currentValue = values[a];
      float oldValue = 0.0f;
      
      // Check if we have previous values for this state-action pair
      auto it = previousQValues.find(state);
      if (it != previousQValues.end() && a < it->second.size()) {
        oldValue = it->second[a];
      }
      
      float delta = std::abs(currentValue - oldValue);
      if (delta > maxDelta) {
        maxDelta = delta;
        debug.str("");
        debug << "[RL-DEBUG] New max delta: " << delta << " at state (" 
              << state.getPrevIntervalBin() << "," << state.getCurrIntervalBin() 
              << "," << state.getPrevActionBin() << "), action " << a
              << " (old: " << oldValue << ", new: " << currentValue << ")";
        writeQTableDebug(debug.str());
      }
    }
    
    // Create or update previousQValues for this state
    previousQValues[state] = values;
  }
  
  // Store this delta for trend analysis
  qValueDeltas.push_back(maxDelta);
  if (qValueDeltas.size() > 100) {  // Keep the most recent 100 deltas
    qValueDeltas.erase(qValueDeltas.begin());
  }
  
  debug.str("");
  debug << "[RL-DEBUG] Max Q-value delta: " << maxDelta;
  writeQTableDebug(debug.str());
  
  // Update the last max delta and GC count
  lastMaxDelta = maxDelta;
  lastGCCount = gcCount;
  
  return maxDelta;
}

/**
 * Check if Q-values have converged based on stability criteria
 */
bool QTable::checkConvergence() {
  // Get the latest delta
  float currentDelta = getMaxQValueDelta();
  
  // Check if delta is below threshold
  if (currentDelta < convergenceThreshold) {
    // Also check policy stability (actions chosen for each state)
    uint32_t stableStates = 0;
    uint32_t totalStates = 0;
    
    for (const auto &entry : table) {
      const State &state = entry.first;
      const std::vector<float> &values = entry.second;
      
      // Find best action
      uint32_t bestAction = std::distance(values.begin(), 
                         std::max_element(values.begin(), values.end()));
      
      // Check if policy for this state has changed
      if (currentPolicy.find(state) != currentPolicy.end()) {
        if (currentPolicy[state] == bestAction) {
          stableStates++;
        }
      }
      
      // Update policy
      currentPolicy[state] = bestAction;
      totalStates++;
    }
    
    // Calculate stability ratio
    float stabilityRatio = totalStates > 0 ? (float)stableStates / totalStates : 0.0f;
    
    // Check if policy is stable enough
    if (stabilityRatio > 0.95f) {  // 95% of states have stable policies
      hasConverged = true;
      return true;
    }
  }
  
  return false;
}

/**
 * Get current convergence metric for monitoring
 */
float QTable::getConvergenceMetric() const {
  if (qValueDeltas.empty()) {
    return 1.0f;  // Not enough data to determine convergence
  }
  
  // Calculate average of recent deltas
  float sum = 0.0f;
  for (float delta : qValueDeltas) {
    sum += delta;
  }
  
  return sum / qValueDeltas.size();
}

/**
 * Get the number of unique states in the Q-table
 */
uint32_t QTable::getNumStates() const {
  return table.size();
}

}  // namespace FTL

}  // namespace SimpleSSD
