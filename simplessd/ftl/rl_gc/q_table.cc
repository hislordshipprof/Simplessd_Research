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
#include <algorithm>
#include <chrono>
#include <iostream>  // For debug printing
#include <fstream>   // For file output
#include <sstream>   // For string stream operations

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
      numActions(actionCount) {
  // Seed the random number generator
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  rng.seed(seed);
  
  std::stringstream ss;
  ss << "[RL-DEBUG] QTable initialized with alpha=" << alpha 
     << ", gamma=" << gamma << ", epsilon=" << epsilon 
     << ", actions=" << numActions;
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
  if (epsilon > 0.01f) {
    epsilon = std::max(0.01f, epsilon * 0.99f);
    
    std::stringstream ss;
    ss << "[RL-DEBUG] Epsilon decayed: " << oldEpsilon << " -> " << epsilon;
    writeQTableDebug(ss.str());
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

}  // namespace FTL

}  // namespace SimpleSSD
