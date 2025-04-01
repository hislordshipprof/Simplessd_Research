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

#include "ftl/config.hh"

#include "util/simplessd.hh"

namespace SimpleSSD {

namespace FTL {

const char NAME_MAPPING_MODE[] = "MappingMode";
const char NAME_OVERPROVISION_RATIO[] = "OverProvisioningRatio";
const char NAME_GC_THRESHOLD[] = "GCThreshold";
const char NAME_BAD_BLOCK_THRESHOLD[] = "EraseThreshold";
const char NAME_FILLING_MODE[] = "FillingMode";
const char NAME_FILL_RATIO[] = "FillRatio";
const char NAME_INVALID_PAGE_RATIO[] = "InvalidPageRatio";
const char NAME_GC_MODE[] = "GCMode";
const char NAME_GC_RECLAIM_BLOCK[] = "GCReclaimBlocks";
const char NAME_GC_RECLAIM_THRESHOLD[] = "GCReclaimThreshold";
const char NAME_GC_EVICT_POLICY[] = "EvictPolicy";
const char NAME_GC_D_CHOICE_PARAM[] = "DChoiceParam";
const char NAME_USE_RANDOM_IO_TWEAK[] = "EnableRandomIOTweak";
const char NAME_ENABLE_RL_GC[] = "EnableRLGC";
const char NAME_RL_GC_TGC_THRESHOLD[] = "RLGCTgcThreshold";
const char NAME_RL_GC_TIGC_THRESHOLD[] = "RLGCTigcThreshold";
const char NAME_RL_GC_MAX_PAGE_COPIES[] = "RLGCMaxPageCopies";
const char NAME_RL_GC_LEARNING_RATE[] = "RLGCLearningRate";
const char NAME_RL_GC_DISCOUNT_FACTOR[] = "RLGCDiscountFactor";
const char NAME_RL_GC_INIT_EPSILON[] = "RLGCInitEpsilon";
const char NAME_RL_GC_NUM_ACTIONS[] = "RLGCNumActions";
const char NAME_RL_GC_DEBUG_ENABLE[] = "RLGCDebugEnable";

// GC Policy selection
const char NAME_GC_POLICY[] = "GCPolicy";

// Lazy-RTGC configuration
const char NAME_LAZY_RTGC_THRESHOLD[] = "LazyRTGCThreshold";
const char NAME_LAZY_RTGC_MAX_PAGE_COPIES[] = "LazyRTGCMaxPageCopies";
const char NAME_LAZY_RTGC_METRICS_ENABLE[] = "LazyRTGCMetricsEnable";

// RL-Aggressive GC configuration parameters
const char NAME_RL_AGG_TAGC_THRESHOLD[] = "RLAggressiveTAGCThreshold";
const char NAME_RL_AGG_MAX_GC_OPS[] = "RLAggressiveMaxGCOps";
const char NAME_RL_AGG_READ_TRIGGERED_GC[] = "RLAggressiveReadTriggeredGC";
const char NAME_RL_AGG_DEBUG_ENABLE[] = "RLAggressiveDebugEnable";
const char NAME_RL_AGG_METRICS_ENABLE[] = "RLAggressiveMetricsEnable";

Config::Config() {
  mapping = PAGE_MAPPING;
  overProvision = 0.25f;
  gcThreshold = 0.05f;
  badBlockThreshold = 100000;
  fillingMode = FILLING_MODE_0;
  fillingRatio = 0.f;
  invalidRatio = 0.f;
  reclaimBlock = 1;
  reclaimThreshold = 0.1f;
  gcMode = GC_MODE_0;
  evictPolicy = POLICY_GREEDY;
  dChoiceParam = 3;
  randomIOTweak = true;
  
  // Update RL-GC default parameters to match paper recommendations
  enableRLGC = false;
  rlGCTgcThreshold = 10;      // Regular GC threshold
  rlGCTigcThreshold = 5;      // Intensive GC threshold
  rlGCMaxPageCopies = 10;     // Maximum pages to copy per action
  rlGCLearningRate = 0.3f;    // Alpha parameter
  rlGCDiscountFactor = 0.8f;  // Gamma parameter
  rlGCInitEpsilon = 0.8f;     // Initial exploration rate (80%)
  rlGCNumActions = 10;        // Number of discrete actions
  rlGCDebugEnable = false;
  
  // GC policy selection (default to traditional GC)
  gcPolicy = GC_POLICY_DEFAULT;
  
  // Lazy-RTGC parameters
  lazyRTGCThreshold = 10;      // Free block threshold for GC in Lazy-RTGC
  lazyRTGCMaxPageCopies = 3;   // Max pages to copy per GC op in Lazy-RTGC
  lazyRTGCMetricsEnable = true;  // Enable metrics collection by default
  
  // RL-Aggressive GC parameters
  rlAggTAGCThreshold = 100;         // TAGC threshold from paper (100)
  rlAggMaxGCOps = 2;                // Maximum GC operations when between TAGC and TGC (2)
  rlAggReadTriggeredGC = true;      // Enable read-triggered GC
  rlAggDebugEnable = false;         // Disable debug output by default
  rlAggMetricsEnable = true;        // Enable metrics collection by default
}

bool Config::setConfig(const char *name, const char *value) {
  bool ret = true;

  if (MATCH_NAME(NAME_MAPPING_MODE)) {
    mapping = (MAPPING)strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_OVERPROVISION_RATIO)) {
    overProvision = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_GC_THRESHOLD)) {
    gcThreshold = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_BAD_BLOCK_THRESHOLD)) {
    badBlockThreshold = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_FILLING_MODE)) {
    fillingMode = (FILLING_MODE)strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_FILL_RATIO)) {
    fillingRatio = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_INVALID_PAGE_RATIO)) {
    invalidRatio = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_GC_MODE)) {
    gcMode = (GC_MODE)strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_GC_RECLAIM_BLOCK)) {
    reclaimBlock = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_GC_RECLAIM_THRESHOLD)) {
    reclaimThreshold = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_GC_EVICT_POLICY)) {
    evictPolicy = (EVICT_POLICY)strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_GC_D_CHOICE_PARAM)) {
    dChoiceParam = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_USE_RANDOM_IO_TWEAK)) {
    randomIOTweak = convertBool(value);
  }
  else if (MATCH_NAME(NAME_ENABLE_RL_GC)) {
    enableRLGC = convertBool(value);
  }
  else if (MATCH_NAME(NAME_RL_GC_TGC_THRESHOLD)) {
    rlGCTgcThreshold = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_RL_GC_TIGC_THRESHOLD)) {
    rlGCTigcThreshold = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_RL_GC_MAX_PAGE_COPIES)) {
    rlGCMaxPageCopies = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_RL_GC_LEARNING_RATE)) {
    rlGCLearningRate = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_RL_GC_DISCOUNT_FACTOR)) {
    rlGCDiscountFactor = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_RL_GC_INIT_EPSILON)) {
    rlGCInitEpsilon = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_RL_GC_NUM_ACTIONS)) {
    rlGCNumActions = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_RL_GC_DEBUG_ENABLE)) {
    rlGCDebugEnable = convertBool(value);
  }
  else if (MATCH_NAME(NAME_GC_POLICY)) {
    gcPolicy = (GC_POLICY)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_LAZY_RTGC_THRESHOLD)) {
    lazyRTGCThreshold = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_LAZY_RTGC_MAX_PAGE_COPIES)) {
    lazyRTGCMaxPageCopies = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_LAZY_RTGC_METRICS_ENABLE)) {
    lazyRTGCMetricsEnable = convertBool(value);
  }
  else if (MATCH_NAME(NAME_RL_AGG_TAGC_THRESHOLD)) {
    rlAggTAGCThreshold = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_RL_AGG_MAX_GC_OPS)) {
    rlAggMaxGCOps = (uint32_t)strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_RL_AGG_READ_TRIGGERED_GC)) {
    rlAggReadTriggeredGC = convertBool(value);
  }
  else if (MATCH_NAME(NAME_RL_AGG_DEBUG_ENABLE)) {
    rlAggDebugEnable = convertBool(value);
  }
  else if (MATCH_NAME(NAME_RL_AGG_METRICS_ENABLE)) {
    rlAggMetricsEnable = convertBool(value);
  }
  else {
    ret = false;
  }

  return ret;
}

void Config::update() {
  if (gcMode == GC_MODE_0 && reclaimBlock == 0) {
    panic("Invalid GCReclaimBlocks");
  }

  if (gcMode == GC_MODE_1 && reclaimThreshold < gcThreshold) {
    panic("Invalid GCReclaimThreshold");
  }

  if (fillingRatio < 0.f || fillingRatio > 1.f) {
    panic("Invalid FillingRatio");
  }

  if (invalidRatio < 0.f || invalidRatio > 1.f) {
    panic("Invalid InvalidPageRatio");
  }
}

int64_t Config::readInt(uint32_t idx) {
  int64_t ret = 0;

  switch (idx) {
    case FTL_MAPPING_MODE:
      ret = mapping;
      break;
    case FTL_GC_MODE:
      ret = gcMode;
      break;
    case FTL_GC_EVICT_POLICY:
      ret = evictPolicy;
      break;
  }

  return ret;
}

uint64_t Config::readUint(uint32_t idx) {
  uint64_t ret = 0;

  switch (idx) {
    case FTL_FILLING_MODE:
      ret = fillingMode;
      break;
    case FTL_BAD_BLOCK_THRESHOLD:
      ret = badBlockThreshold;
      break;
    case FTL_GC_RECLAIM_BLOCK:
      ret = reclaimBlock;
      break;
    case FTL_GC_D_CHOICE_PARAM:
      ret = dChoiceParam;
      break;
    case FTL_RL_GC_TGC_THRESHOLD:
      ret = rlGCTgcThreshold;
      break;
    case FTL_RL_GC_TIGC_THRESHOLD:
      ret = rlGCTigcThreshold;
      break;
    case FTL_RL_GC_MAX_PAGE_COPIES:
      ret = rlGCMaxPageCopies;
      break;
    case FTL_RL_GC_NUM_ACTIONS:
      ret = rlGCNumActions;
      break;
    case FTL_GC_POLICY:
      ret = gcPolicy;
      break;
    case FTL_LAZY_RTGC_THRESHOLD:
      ret = lazyRTGCThreshold;
      break;
    case FTL_LAZY_RTGC_MAX_PAGE_COPIES:
      ret = lazyRTGCMaxPageCopies;
      break;
    case FTL_RL_AGG_TAGC_THRESHOLD:
      ret = rlAggTAGCThreshold;
      break;
    case FTL_RL_AGG_MAX_GC_OPS:
      ret = rlAggMaxGCOps;
      break;
  }

  return ret;
}

float Config::readFloat(uint32_t idx) {
  float ret = 0.f;

  switch (idx) {
    case FTL_OVERPROVISION_RATIO:
      ret = overProvision;
      break;
    case FTL_GC_THRESHOLD_RATIO:
      ret = gcThreshold;
      break;
    case FTL_FILL_RATIO:
      ret = fillingRatio;
      break;
    case FTL_INVALID_PAGE_RATIO:
      ret = invalidRatio;
      break;
    case FTL_GC_RECLAIM_THRESHOLD:
      ret = reclaimThreshold;
      break;
    case FTL_RL_GC_LEARNING_RATE:
      ret = rlGCLearningRate;
      break;
    case FTL_RL_GC_DISCOUNT_FACTOR:
      ret = rlGCDiscountFactor;
      break;
    case FTL_RL_GC_INIT_EPSILON:
      ret = rlGCInitEpsilon;
      break;
  }

  return ret;
}

bool Config::readBoolean(uint32_t idx) {
  bool ret = false;

  switch (idx) {
    case FTL_USE_RANDOM_IO_TWEAK:
      ret = randomIOTweak;
      break;
    case FTL_RL_GC_ENABLE:
      ret = enableRLGC;
      break;
    case FTL_RL_GC_DEBUG_ENABLE:
      ret = rlGCDebugEnable;
      break;
    case FTL_LAZY_RTGC_METRICS_ENABLE:
      ret = lazyRTGCMetricsEnable;
      break;
    case FTL_RL_AGG_READ_TRIGGERED_GC:
      ret = rlAggReadTriggeredGC;
      break;
    case FTL_RL_AGG_DEBUG_ENABLE:
      ret = rlAggDebugEnable;
      break;
    case FTL_RL_AGG_METRICS_ENABLE:
      ret = rlAggMetricsEnable;
      break;
  }

  return ret;
}

}  // namespace FTL

}  // namespace SimpleSSD
