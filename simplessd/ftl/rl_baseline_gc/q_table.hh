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

#ifndef __FTL_RL_GC_Q_TABLE__
#define __FTL_RL_GC_Q_TABLE__

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <random>

namespace SimpleSSD {

namespace FTL {

/**
 * State class representing the discretized state for Q-learning
 */
class State {
 private:
  uint32_t prevIntervalBin;   // Previous inter-request interval bin (0-1)
  uint32_t currIntervalBin;   // Current inter-request interval bin (0-16)
  uint32_t prevActionBin;     // Previous action bin (0-1)

 public:
  State();
  State(uint32_t prev, uint32_t curr, uint32_t action);

  // For use as a key in unordered_map
  bool operator==(const State &other) const;

  // Hash function for unordered_map
  struct StateHash {
    std::size_t operator()(const State &state) const;
  };

  // Getters
  uint32_t getPrevIntervalBin() const;
  uint32_t getCurrIntervalBin() const;
  uint32_t getPrevActionBin() const;
};

/**
 * Q-table for Reinforcement Learning based GC
 */
class QTable {
 private:
  // Q-table structure: state -> (action -> value)
  std::unordered_map<State, std::vector<float>, State::StateHash> table;
  
  // RL parameters
  float alpha;        // Learning rate
  float gamma;        // Discount factor
  float epsilon;      // Exploration rate
  uint64_t gcCount;   // Count of GC operations for epsilon decay
  
  // Action space size
  uint32_t numActions;
  
  // Random number generator
  std::mt19937 rng;

 public:
  QTable(float learningRate, float discountFactor, float initialEpsilon, uint32_t actionCount);
  ~QTable();

  // Core Q-learning functions
  uint32_t selectAction(const State &state);  // Using epsilon-greedy policy
  void updateQ(const State &state, uint32_t action, float reward, const State &nextState);

  // Helper functions
  float getQValue(const State &state, uint32_t action);
  void decayEpsilon();  // Reduce epsilon over time
  float getEpsilon() const;
  void setEpsilon(float newEpsilon);
  uint64_t getGCCount() const;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif  // __FTL_RL_GC_Q_TABLE__
