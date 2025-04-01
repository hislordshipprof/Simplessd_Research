/*
 * Copyright (C) 2017 CAMELab
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

#ifndef __FTL_CONFIG__
#define __FTL_CONFIG__

#include "sim/base_config.hh"

namespace SimpleSSD {

namespace FTL {

typedef enum {
  /* Common FTL configuration */
  FTL_MAPPING_MODE,
  FTL_OVERPROVISION_RATIO,
  FTL_GC_THRESHOLD_RATIO,
  FTL_BAD_BLOCK_THRESHOLD,
  FTL_FILLING_MODE,
  FTL_FILL_RATIO,
  FTL_INVALID_PAGE_RATIO,
  FTL_GC_MODE,
  FTL_GC_RECLAIM_BLOCK,
  FTL_GC_RECLAIM_THRESHOLD,
  FTL_GC_EVICT_POLICY,
  FTL_GC_D_CHOICE_PARAM,
  FTL_USE_RANDOM_IO_TWEAK,

  /* N+K Mapping configuration*/
  FTL_NKMAP_N,
  FTL_NKMAP_K,

  /* RL GC parameters */
  FTL_RL_GC_ENABLE,            // Whether to enable RL-based GC
  FTL_RL_GC_TGC_THRESHOLD,     // Free block threshold for GC triggering
  FTL_RL_GC_TIGC_THRESHOLD,    // Free block threshold for intensive GC
  FTL_RL_GC_MAX_PAGE_COPIES,   // Maximum page copies per action
  FTL_RL_GC_LEARNING_RATE,     // Learning rate (alpha)
  FTL_RL_GC_DISCOUNT_FACTOR,   // Discount factor (gamma)
  FTL_RL_GC_INIT_EPSILON,      // Initial exploration rate (epsilon)
  FTL_RL_GC_NUM_ACTIONS,       // Number of actions in action space
  FTL_RL_GC_DEBUG_ENABLE,      // Enable debug output for RL-GC

  /* GC Policy Selection */
  FTL_GC_POLICY,               // GC policy selection
  
  /* Lazy-RTGC Configuration */
  FTL_LAZY_RTGC_THRESHOLD,       // Free block threshold for Lazy-RTGC
  FTL_LAZY_RTGC_MAX_PAGE_COPIES, // Maximum page copies per GC operation
  FTL_LAZY_RTGC_METRICS_ENABLE,  // Enable metrics output for Lazy-RTGC
  
  /* RL-Aggressive GC Configuration */
  FTL_RL_AGG_TAGC_THRESHOLD,       // Triggering aggressive GC threshold (100)
  FTL_RL_AGG_MAX_GC_OPS,           // Maximum GC operations when between TAGC and TGC (2)
  FTL_RL_AGG_READ_TRIGGERED_GC,     // Enable read-triggered GC
  FTL_RL_AGG_DEBUG_ENABLE,          // Enable debug output
  FTL_RL_AGG_METRICS_ENABLE,        // Enable metrics collection
} FTL_CONFIG;

typedef enum {
  PAGE_MAPPING,
} MAPPING;

typedef enum {
  GC_MODE_0,  // Reclaim fixed number of blocks
  GC_MODE_1,  // Reclaim blocks until threshold
} GC_MODE;

typedef enum {
  FILLING_MODE_0,
  FILLING_MODE_1,
  FILLING_MODE_2,
} FILLING_MODE;

typedef enum {
  POLICY_GREEDY,  // Select the block with the least valid pages
  POLICY_COST_BENEFIT,
  POLICY_RANDOM,  // Select the block randomly
  POLICY_DCHOICE,
} EVICT_POLICY;

typedef enum {
  GC_POLICY_DEFAULT,   // Default garbage collection
  GC_POLICY_LAZY_RTGC, // Lazy-RTGC implementation
  GC_POLICY_RL_BASELINE, // RL-based GC baseline
  GC_POLICY_RL_INTENSIVE, // RL-based GC with intensive mode
  GC_POLICY_RL_AGGRESSIVE, // RL-based aggressive GC
} GC_POLICY;

class Config : public BaseConfig {
 private:
  MAPPING mapping;             //!< Default: PAGE_MAPPING
  float overProvision;         //!< Default: 0.25 (25%)
  float gcThreshold;           //!< Default: 0.05 (5%)
  uint64_t badBlockThreshold;  //!< Default: 100000
  FILLING_MODE fillingMode;    //!< Default: FILLING_MODE_0
  float fillingRatio;          //!< Default: 0.0 (0%)
  float invalidRatio;          //!< Default: 0.0 (0%)
  uint64_t reclaimBlock;       //!< Default: 1
  float reclaimThreshold;      //!< Default: 0.1 (10%)
  GC_MODE gcMode;              //!< Default: FTL_GC_MODE_0
  EVICT_POLICY evictPolicy;    //!< Default: POLICY_GREEDY
  uint64_t dChoiceParam;       //!< Default: 3
  bool randomIOTweak;          //!< Default: true

  // Add these new variables
  bool enableRLGC;              //!< Default: false
  uint32_t rlGCTgcThreshold;    //!< Default: 10
  uint32_t rlGCTigcThreshold;   //!< Default: 3
  uint32_t rlGCMaxPageCopies;   //!< Default: 2
  float rlGCLearningRate;       //!< Default: 0.3
  float rlGCDiscountFactor;     //!< Default: 0.8
  float rlGCInitEpsilon;        //!< Default: 0.8
  uint32_t rlGCNumActions;      //!< Default: 7
  bool rlGCDebugEnable;         //!< Default: false
  
  // GC policy selection
  GC_POLICY gcPolicy;           //!< Default: GC_POLICY_DEFAULT
  
  // Lazy-RTGC parameters
  uint32_t lazyRTGCThreshold;       //!< Default: 10
  uint32_t lazyRTGCMaxPageCopies; //!< Default: 3
  bool lazyRTGCMetricsEnable;     //!< Default: true
  
  // RL-Aggressive GC parameters
  uint32_t rlAggTAGCThreshold;      //!< Default: 100 (TAGC value)
  uint32_t rlAggMaxGCOps;           //!< Default: 2 (max GC operations)
  bool rlAggReadTriggeredGC;        //!< Default: true
  bool rlAggDebugEnable;            //!< Default: false
  bool rlAggMetricsEnable;          //!< Default: true

 public:
  Config();

  bool setConfig(const char *, const char *) override;
  void update() override;

  int64_t readInt(uint32_t) override;
  uint64_t readUint(uint32_t) override;
  float readFloat(uint32_t) override;
  bool readBoolean(uint32_t) override;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
