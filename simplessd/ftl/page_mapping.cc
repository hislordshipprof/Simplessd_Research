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

#include "ftl/page_mapping.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <random>

#include "util/algorithm.hh"
#include "util/bitset.hh"

namespace SimpleSSD {

namespace FTL {

PageMapping::PageMapping(ConfigReader &c, Parameter &p, PAL::PAL *l,
                         DRAM::AbstractDRAM *d)
    : AbstractFTL(p, l, d),
      pPAL(l),
      conf(c),
      lastFreeBlock(param.pageCountToMaxPerf),
      lastFreeBlockIOMap(param.ioUnitInPage),
      bReclaimMore(false) {
  blocks.reserve(param.totalPhysicalBlocks);
  table.reserve(param.totalLogicalBlocks * param.pagesInBlock);

  for (uint32_t i = 0; i < param.totalPhysicalBlocks; i++) {
    freeBlocks.emplace_back(Block(i, param.pagesInBlock, param.ioUnitInPage));
  }

  nFreeBlocks = param.totalPhysicalBlocks;

  status.totalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;

  // Allocate free blocks
  for (uint32_t i = 0; i < param.pageCountToMaxPerf; i++) {
    lastFreeBlock.at(i) = getFreeBlock(i);
  }

  lastFreeBlockIndex = 0;

  memset(&stat, 0, sizeof(stat));

  bRandomTweak = conf.readBoolean(CONFIG_FTL, FTL_USE_RANDOM_IO_TWEAK);
  bitsetSize = bRandomTweak ? param.ioUnitInPage : 1;

  // Initialize RL-GC
  bEnableRLGC = conf.readBoolean(CONFIG_FTL, FTL_RL_GC_ENABLE);
  
  if (bEnableRLGC) {
    uint32_t tgcThreshold = conf.readUint(CONFIG_FTL, FTL_RL_GC_TGC_THRESHOLD);
    uint32_t tigcThreshold = conf.readUint(CONFIG_FTL, FTL_RL_GC_TIGC_THRESHOLD);
    uint32_t maxPageCopies = conf.readUint(CONFIG_FTL, FTL_RL_GC_MAX_PAGE_COPIES);
    float learningRate = conf.readFloat(CONFIG_FTL, FTL_RL_GC_LEARNING_RATE);
    float discountFactor = conf.readFloat(CONFIG_FTL, FTL_RL_GC_DISCOUNT_FACTOR);
    float initEpsilon = conf.readFloat(CONFIG_FTL, FTL_RL_GC_INIT_EPSILON);
    uint32_t numActions = conf.readUint(CONFIG_FTL, FTL_RL_GC_NUM_ACTIONS);
    
    pRLGC = new RLGarbageCollector(tgcThreshold, tigcThreshold, maxPageCopies,
                                  learningRate, discountFactor, initEpsilon, numActions);
    
    // Enable debug output for RL-GC
    bool enableDebug = conf.readBoolean(CONFIG_FTL, FTL_RL_GC_DEBUG_ENABLE);
    if (enableDebug) {
      std::string debugPath = "output/rl_gc_debug.log";
      
      // Create output directory if it doesn't exist
      #ifdef _WIN32
      // Windows
      if (system("if not exist output mkdir output") != 0) {
        std::cerr << "Warning: Failed to create output directory" << std::endl;
      }
      #else
      // Linux/Unix/MacOS
      if (system("mkdir -p output") != 0) {
        std::cerr << "Warning: Failed to create output directory" << std::endl;
      }
      #endif
      
      // Clear existing log file
      std::ofstream logFile(debugPath, std::ios::trunc);
      if (logFile.is_open()) {
        logFile.close();
      }
      
      // Enable debug in RL-GC
      pRLGC->enableDebug(true);
      pRLGC->setDebugFilePath(debugPath);
      
      // Print initial debug info
      pRLGC->printDebugInfo();
    }
  }
  else {
    pRLGC = nullptr;
  }
  
  lastIOStartTime = 0;
  lastIOEndTime = 0;
}

PageMapping::~PageMapping() {
  // Clean up RL-GC
  if (pRLGC) {
    delete pRLGC;
  }
}

bool PageMapping::initialize() {
  uint64_t nPagesToWarmup;
  uint64_t nPagesToInvalidate;
  uint64_t nTotalLogicalPages;
  uint64_t maxPagesBeforeGC;
  uint64_t tick;
  uint64_t valid;
  uint64_t invalid;
  FILLING_MODE mode;

  Request req(param.ioUnitInPage);

  debugprint(LOG_FTL_PAGE_MAPPING, "Initialization started");

  nTotalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;
  nPagesToWarmup =
      nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_FILL_RATIO);
  nPagesToInvalidate =
      nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_INVALID_PAGE_RATIO);
  mode = (FILLING_MODE)conf.readUint(CONFIG_FTL, FTL_FILLING_MODE);
  maxPagesBeforeGC =
      param.pagesInBlock *
      (param.totalPhysicalBlocks *
           (1 - conf.readFloat(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO)) -
       param.pageCountToMaxPerf);  // # free blocks to maintain

  if (nPagesToWarmup + nPagesToInvalidate > maxPagesBeforeGC) {
    warn("ftl: Too high filling ratio. Adjusting invalidPageRatio.");
    nPagesToInvalidate = maxPagesBeforeGC - nPagesToWarmup;
  }

  debugprint(LOG_FTL_PAGE_MAPPING, "Total logical pages: %" PRIu64,
             nTotalLogicalPages);
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Total logical pages to fill: %" PRIu64 " (%.2f %%)",
             nPagesToWarmup, nPagesToWarmup * 100.f / nTotalLogicalPages);
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Total invalidated pages to create: %" PRIu64 " (%.2f %%)",
             nPagesToInvalidate,
             nPagesToInvalidate * 100.f / nTotalLogicalPages);

  req.ioFlag.set();

  // Step 1. Filling
  if (mode == FILLING_MODE_0 || mode == FILLING_MODE_1) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = i;
      writeInternal(req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }

  // Step 2. Invalidating
  if (mode == FILLING_MODE_0) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = i;
      writeInternal(req, tick, false);
    }
  }
  else if (mode == FILLING_MODE_1) {
    // Random
    // We can successfully restrict range of LPN to create exact number of
    // invalid pages because we wrote in sequential mannor in step 1.
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nPagesToWarmup - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }

  // Report
  calculateTotalPages(valid, invalid);
  debugprint(LOG_FTL_PAGE_MAPPING, "Filling finished. Page status:");
  debugprint(LOG_FTL_PAGE_MAPPING,
             "  Total valid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             valid, valid * 100.f / nTotalLogicalPages, nPagesToWarmup,
             (int64_t)(valid - nPagesToWarmup));
  debugprint(LOG_FTL_PAGE_MAPPING,
             "  Total invalid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             invalid, invalid * 100.f / nTotalLogicalPages, nPagesToInvalidate,
             (int64_t)(invalid - nPagesToInvalidate));
  debugprint(LOG_FTL_PAGE_MAPPING, "Initialization finished");

  return true;
}

void PageMapping::read(Request &req, uint64_t &tick) {
  lastIOStartTime = tick;
  
  // Call original read logic
  uint64_t beginAt = tick;
  
  if (req.ioFlag.count() > 0) {
    readInternal(req, tick);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "READ  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.lpn, beginAt, tick, tick - beginAt);
  }
  else {
    warn("FTL got empty request");
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ);
  
  // Record IO completion time
  lastIOEndTime = tick;
  
  // If RL-GC is enabled, record response time for reward calculation
  if (bEnableRLGC && pRLGC) {
    uint64_t responseTime = lastIOEndTime - lastIOStartTime;
    pRLGC->recordResponseTime(responseTime);
    
    // Process any pending Q-value updates
    if (pRLGC->hasPendingQValueUpdate()) {
      pRLGC->processPendingUpdate(responseTime);
    }
    
    // Only check for GC if we have a valid RL-GC controller and free blocks are below threshold
    if (nFreeBlocks <= pRLGC->getTGCThreshold()) {
      // Check if we should trigger GC based on inter-request interval
      if (pRLGC->shouldTriggerGC(nFreeBlocks, tick)) {
        // Get the action to take
        uint32_t action = pRLGC->getGCAction(nFreeBlocks);
        
        // Perform partial GC based on the action
        std::vector<uint32_t> victimBlocks;
        performPartialGC(action, victimBlocks, tick);
      }
    }
  }
}

void PageMapping::write(Request &req, uint64_t &tick) {
  lastIOStartTime = tick;
  
  if (req.ioFlag.count() > 0) {
    writeInternal(req, tick);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "WRITE | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.lpn, lastIOStartTime, tick, tick - lastIOStartTime);
  }
  else {
    warn("FTL got empty request");
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE);
  
  // Record IO completion time
  lastIOEndTime = tick;
  
  // If RL-GC is enabled, record response time for reward calculation
  if (bEnableRLGC && pRLGC) {
    uint64_t responseTime = lastIOEndTime - lastIOStartTime;
    pRLGC->recordResponseTime(responseTime);
    
    // Process any pending Q-value updates
    if (pRLGC->hasPendingQValueUpdate()) {
      pRLGC->processPendingUpdate(responseTime);
    }
  }
  
  // Check if we need to do garbage collection
  bool needGC = bReclaimMore || nFreeBlocks <= conf.readUint(CONFIG_FTL, FTL_RL_GC_TGC_THRESHOLD);
  
  if (needGC) {
    if (bEnableRLGC && pRLGC) {
      // Check if we should trigger GC based on inter-request interval
      if (pRLGC->shouldTriggerGC(nFreeBlocks, tick)) {
        // Get the action to take
        uint32_t action = pRLGC->getGCAction(nFreeBlocks);
        
        // Perform partial GC based on the action
        std::vector<uint32_t> victimBlocks;
        uint32_t copiedPages = performPartialGC(action, victimBlocks, tick);
        
        // Record GC invocation
        pRLGC->recordGCInvocation(copiedPages);
      }
      else if (nFreeBlocks <= pRLGC->getTIGCThreshold()) {
        // Critical situation - perform intensive GC
        std::vector<uint32_t> victimBlocks;
        doGarbageCollection(victimBlocks, tick);
        
        // Record intensive GC
        pRLGC->recordIntensiveGC();
      }
    }
    else {
      // Use original GC implementation
      std::vector<uint32_t> victimBlocks;
      doGarbageCollection(victimBlocks, tick);
    }
  }
}

void PageMapping::trim(Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  trimInternal(req, tick);

  debugprint(LOG_FTL_PAGE_MAPPING,
             "TRIM  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
             ")",
             req.lpn, begin, tick, tick - begin);

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::TRIM);
}

void PageMapping::format(LPNRange &range, uint64_t &tick) {
  PAL::Request req(param.ioUnitInPage);
  std::vector<uint32_t> list;

  req.ioFlag.set();

  for (auto iter = table.begin(); iter != table.end();) {
    if (iter->first >= range.slpn && iter->first < range.slpn + range.nlp) {
      auto &mappingList = iter->second;

      // Do trim
      for (uint32_t idx = 0; idx < bitsetSize; idx++) {
        auto &mapping = mappingList.at(idx);
        
        // Check if the mapping is valid
        if (mapping.first >= param.totalPhysicalBlocks ||
            mapping.second >= param.pagesInBlock) {
          continue;
        }
        
        auto block = blocks.find(mapping.first);

        if (block == blocks.end()) {
          // Skip if block doesn't exist
          continue;
        }

        block->second.invalidate(mapping.second, idx);

        // Collect block indices
        list.push_back(mapping.first);
      }

      iter = table.erase(iter);
    }
    else {
      iter++;
    }
  }

  // Get blocks to erase
  std::sort(list.begin(), list.end());
  auto last = std::unique(list.begin(), list.end());
  list.erase(last, list.end());

  // Do GC only in specified blocks
  doGarbageCollection(list, tick);

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::FORMAT);
}

Status *PageMapping::getStatus(uint64_t lpnBegin, uint64_t lpnEnd) {
  status.freePhysicalBlocks = nFreeBlocks;

  if (lpnBegin == 0 && lpnEnd >= status.totalLogicalPages) {
    status.mappedLogicalPages = table.size();
  }
  else {
    status.mappedLogicalPages = 0;

    for (uint64_t lpn = lpnBegin; lpn < lpnEnd; lpn++) {
      if (table.count(lpn) > 0) {
        status.mappedLogicalPages++;
      }
    }
  }

  return &status;
}

float PageMapping::freeBlockRatio() {
  return (float)nFreeBlocks / param.totalPhysicalBlocks;
}

uint32_t PageMapping::convertBlockIdx(uint32_t blockIdx) {
  return blockIdx % param.pageCountToMaxPerf;
}

uint32_t PageMapping::getFreeBlock(uint32_t idx) {
  uint32_t blockIndex = 0;

  if (idx >= param.pageCountToMaxPerf) {
    panic("Index out of range");
  }

  if (nFreeBlocks > 0) {
    // Search block which is blockIdx % param.pageCountToMaxPerf == idx
    auto iter = freeBlocks.begin();

    for (; iter != freeBlocks.end(); iter++) {
      blockIndex = iter->getBlockIndex();

      if (blockIndex % param.pageCountToMaxPerf == idx) {
        break;
      }
    }

    // Sanity check
    if (iter == freeBlocks.end()) {
      // Just use first one
      iter = freeBlocks.begin();
      blockIndex = iter->getBlockIndex();
    }

    // Insert found block to block list
    if (blocks.find(blockIndex) != blocks.end()) {
      panic("Corrupted");
    }

    blocks.emplace(blockIndex, std::move(*iter));

    // Remove found block from free block list
    freeBlocks.erase(iter);
    nFreeBlocks--;
  }
  else {
    panic("No free block left");
  }

  return blockIndex;
}

uint32_t PageMapping::getLastFreeBlock(Bitset &iomap) {
  if (!bRandomTweak || (lastFreeBlockIOMap & iomap).any()) {
    // Update lastFreeBlockIndex
    lastFreeBlockIndex++;

    if (lastFreeBlockIndex == param.pageCountToMaxPerf) {
      lastFreeBlockIndex = 0;
    }

    lastFreeBlockIOMap = iomap;
  }
  else {
    lastFreeBlockIOMap |= iomap;
  }

  auto freeBlock = blocks.find(lastFreeBlock.at(lastFreeBlockIndex));

  // Sanity check
  if (freeBlock == blocks.end()) {
    panic("Corrupted");
  }

  // If current free block is full, get next block
  if (freeBlock->second.getNextWritePageIndex() == param.pagesInBlock) {
    lastFreeBlock.at(lastFreeBlockIndex) = getFreeBlock(lastFreeBlockIndex);

    bReclaimMore = true;
  }

  return lastFreeBlock.at(lastFreeBlockIndex);
}

// calculate weight of each block regarding victim selection policy
void PageMapping::calculateVictimWeight(
    std::vector<std::pair<uint32_t, float>> &weight, const EVICT_POLICY policy,
    uint64_t tick) {
  float temp;

  weight.reserve(blocks.size());

  switch (policy) {
    case POLICY_GREEDY:
    case POLICY_RANDOM:
    case POLICY_DCHOICE:
      for (auto &iter : blocks) {
        if (iter.second.getNextWritePageIndex() != param.pagesInBlock) {
          continue;
        }

        weight.push_back({iter.first, iter.second.getValidPageCountRaw()});
      }

      break;
    case POLICY_COST_BENEFIT:
      for (auto &iter : blocks) {
        if (iter.second.getNextWritePageIndex() != param.pagesInBlock) {
          continue;
        }

        temp = (float)(iter.second.getValidPageCountRaw()) / param.pagesInBlock;

        weight.push_back(
            {iter.first,
             temp / ((1 - temp) * (tick - iter.second.getLastAccessedTime()))});
      }

      break;
    default:
      panic("Invalid evict policy");
  }
}

void PageMapping::selectVictimBlock(std::vector<uint32_t> &list,
                                    uint64_t &tick) {
  static const GC_MODE mode = (GC_MODE)conf.readInt(CONFIG_FTL, FTL_GC_MODE);
  static const EVICT_POLICY policy =
      (EVICT_POLICY)conf.readInt(CONFIG_FTL, FTL_GC_EVICT_POLICY);
  static uint32_t dChoiceParam =
      conf.readUint(CONFIG_FTL, FTL_GC_D_CHOICE_PARAM);
  uint64_t nBlocks = conf.readUint(CONFIG_FTL, FTL_GC_RECLAIM_BLOCK);
  std::vector<std::pair<uint32_t, float>> weight;

  list.clear();

  // Calculate number of blocks to reclaim
  if (mode == GC_MODE_0) {
    // DO NOTHING
  }
  else if (mode == GC_MODE_1) {
    static const float t = conf.readFloat(CONFIG_FTL, FTL_GC_RECLAIM_THRESHOLD);

    nBlocks = param.totalPhysicalBlocks * t - nFreeBlocks;
  }
  else {
    panic("Invalid GC mode");
  }

  // reclaim one more if last free block fully used
  if (bReclaimMore) {
    nBlocks += param.pageCountToMaxPerf;

    bReclaimMore = false;
  }

  // Calculate weights of all blocks
  calculateVictimWeight(weight, policy, tick);

  if (policy == POLICY_RANDOM || policy == POLICY_DCHOICE) {
    uint64_t randomRange =
        policy == POLICY_RANDOM ? nBlocks : dChoiceParam * nBlocks;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, weight.size() - 1);
    std::vector<std::pair<uint32_t, float>> selected;

    while (selected.size() < randomRange) {
      uint64_t idx = dist(gen);

      if (weight.at(idx).first < std::numeric_limits<uint32_t>::max()) {
        selected.push_back(weight.at(idx));
        weight.at(idx).first = std::numeric_limits<uint32_t>::max();
      }
    }

    weight = std::move(selected);
  }

  // Sort weights
  std::sort(
      weight.begin(), weight.end(),
      [](std::pair<uint32_t, float> a, std::pair<uint32_t, float> b) -> bool {
        return a.second < b.second;
      });

  // Select victims from the blocks with the lowest weight
  nBlocks = MIN(nBlocks, weight.size());

  for (uint64_t i = 0; i < nBlocks; i++) {
    list.push_back(weight.at(i).first);
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::SELECT_VICTIM_BLOCK);
}

void PageMapping::doGarbageCollection(std::vector<uint32_t> &victimBlocks, uint64_t &tick) {
  // Use original GC implementation
  
  // Select victim blocks if not provided
  if (victimBlocks.size() == 0) {
    uint64_t tickLocal = tick;
    selectVictimBlock(victimBlocks, tickLocal);
  }
  
  // Process each victim block
  for (auto &victimBlockID : victimBlocks) {
    auto blockIter = blocks.find(victimBlockID);
    
    // Skip if block doesn't exist
    if (blockIter == blocks.end()) {
      continue;
    }
    
    Block &victimBlock = blockIter->second;
    
    // Skip blocks with no valid pages
    if (victimBlock.getValidPageCount() == 0) {
      // Just erase the block
      PAL::Request req(param.ioUnitInPage);
      req.blockIndex = victimBlockID;
      eraseInternal(req, tick);
      continue;
    }
    
    for (uint32_t pageIndex = 0; pageIndex < param.pagesInBlock; pageIndex++) {
      // Initialize vectors and bitset with proper sizes before calling getPageInfo
      std::vector<uint64_t> lpns(param.ioUnitInPage);
      Bitset validBits(param.ioUnitInPage);
      
      // Get page info safely
      bool hasValidData = victimBlock.getPageInfo(pageIndex, lpns, validBits);
      
      // Skip if no valid data or no valid bits
      if (!hasValidData || !validBits.any()) {
        continue;
      }
      
      // Get logical page number from the first valid bit
      uint64_t lpn = UINT64_MAX;
      for (uint32_t i = 0; i < validBits.size(); i++) {
        if (validBits.test(i)) {
          lpn = lpns[i];
          break;
        }
      }
      
      if (lpn == UINT64_MAX) {
        continue;
      }
      
      // Allocate a new page
      uint32_t newBlockID = getLastFreeBlock(validBits);
      auto newBlockIter = blocks.find(newBlockID);
      if (newBlockIter == blocks.end()) {
        panic("New block not found");
      }
      uint32_t newPageID = newBlockIter->second.getNextWritePageIndex();
      
      // Copy the page data (read from old, write to new)
      PAL::Request palRequest(param.ioUnitInPage);
      palRequest.blockIndex = victimBlockID;
      palRequest.pageIndex = pageIndex;
      palRequest.ioFlag = validBits;
      
      // Read from old location
      pPAL->read(palRequest, tick);
      
      // Write to new location
      palRequest.blockIndex = newBlockID;
      palRequest.pageIndex = newPageID;
      pPAL->write(palRequest, tick);
      
      // Update mapping table
      auto mappingList = table.find(lpn);
      if (mappingList != table.end()) {
        for (uint32_t idx = 0; idx < bitsetSize; idx++) {
          if (validBits.test(idx)) {
            mappingList->second.at(idx).first = newBlockID;
            mappingList->second.at(idx).second = newPageID;
          }
        }
      }
      
      // Invalidate the old page
      for (uint32_t idx = 0; idx < bitsetSize; idx++) {
        if (validBits.test(idx)) {
          victimBlock.invalidate(pageIndex, idx);
        }
      }
      
      stat.validPageCopies++;
    }
    
    // Erase the block if all valid pages were copied
    if (victimBlock.getValidPageCount() == 0) {
      PAL::Request req(param.ioUnitInPage);
      req.blockIndex = victimBlockID;
      eraseInternal(req, tick);
    }
  }
  
  // Update statistics
  stat.gcCount++;
  stat.reclaimedBlocks += victimBlocks.size();
  
  // If RL-GC is enabled, track state
  if (bEnableRLGC && pRLGC) {
    pRLGC->updateState(tick);
  }
}

void PageMapping::readInternal(Request &req, uint64_t &tick) {
  PAL::Request palRequest(req);
  uint64_t beginAt = tick;  // Initialize beginAt to the current tick

  auto mappingList = table.find(req.lpn);

  if (mappingList != table.end()) {
    if (bRandomTweak) {
      pDRAM->read(&(*mappingList), 8 * req.ioFlag.count(), tick);
    }
    else {
      pDRAM->read(&(*mappingList), 8, tick);
    }

    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto &mapping = mappingList->second.at(idx);

        // Check if the mapping is valid before proceeding
        if (mapping.first < param.totalPhysicalBlocks &&
            mapping.second < param.pagesInBlock) {
          
          // Check if the block exists in the blocks map
          auto block = blocks.find(mapping.first);
          if (block == blocks.end()) {
            // Skip this mapping if the block doesn't exist
            continue;
          }

          palRequest.blockIndex = mapping.first;
          palRequest.pageIndex = mapping.second;

          if (bRandomTweak) {
            palRequest.ioFlag.reset();
            palRequest.ioFlag.set(idx);
          }
          else {
            palRequest.ioFlag.set();
          }

          beginAt = tick;  // Update beginAt before each read operation

          block->second.read(palRequest.pageIndex, idx, beginAt);
          pPAL->read(palRequest, beginAt);
        }
      }
    }

    tick = beginAt;  // Update tick with the final value of beginAt
    tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ_INTERNAL);
  }
}

void PageMapping::writeInternal(Request &req, uint64_t &tick, bool sendToPAL) {
  PAL::Request palRequest(req);
  std::unordered_map<uint32_t, Block>::iterator block;
  auto mappingList = table.find(req.lpn);
  bool readBeforeWrite = false;
  uint64_t beginAt = tick;  // Declare beginAt at this scope level

  if (mappingList != table.end()) {
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto &mapping = mappingList->second.at(idx);

        if (mapping.first < param.totalPhysicalBlocks &&
            mapping.second < param.pagesInBlock) {
          // Check if the block exists before trying to invalidate
          block = blocks.find(mapping.first);
          
          // Skip if block doesn't exist
          if (block == blocks.end()) {
            continue;
          }

          // Invalidate current page
          block->second.invalidate(mapping.second, idx);
        }
      }
    }
  }
  else {
    // Create empty mapping
    auto ret = table.emplace(
        req.lpn,
        std::vector<std::pair<uint32_t, uint32_t>>(
            bitsetSize, {param.totalPhysicalBlocks, param.pagesInBlock}));

    if (!ret.second) {
      panic("Failed to insert new mapping");
    }

    mappingList = ret.first;
  }

  // Write data to free block
  block = blocks.find(getLastFreeBlock(req.ioFlag));

  if (block == blocks.end()) {
    panic("No such block");
  }

  if (sendToPAL) {
    if (bRandomTweak) {
      pDRAM->read(&(*mappingList), 8 * req.ioFlag.count(), tick);
      pDRAM->write(&(*mappingList), 8 * req.ioFlag.count(), tick);
    }
    else {
      pDRAM->read(&(*mappingList), 8, tick);
      pDRAM->write(&(*mappingList), 8, tick);
    }
  }

  if (!bRandomTweak && !req.ioFlag.all()) {
    // We have to read old data
    readBeforeWrite = true;
  }

  for (uint32_t idx = 0; idx < bitsetSize; idx++) {
    if (req.ioFlag.test(idx) || !bRandomTweak) {
      uint32_t pageIndex = block->second.getNextWritePageIndex(idx);
      auto &mapping = mappingList->second.at(idx);

      uint64_t beginAt = tick;

      block->second.write(pageIndex, req.lpn, idx, beginAt);

      // Read old data if needed (Only executed when bRandomTweak = false)
      // Maybe some other init procedures want to perform 'partial-write'
      // So check sendToPAL variable
      if (readBeforeWrite && sendToPAL) {
        palRequest.blockIndex = mapping.first;
        palRequest.pageIndex = mapping.second;

        // We don't need to read old data
        palRequest.ioFlag.flip();

        pPAL->read(palRequest, beginAt);
      }

      // update mapping to table
      mapping.first = block->first;
      mapping.second = pageIndex;

      if (sendToPAL) {
        palRequest.blockIndex = block->first;
        palRequest.pageIndex = pageIndex;

        if (bRandomTweak) {
          palRequest.ioFlag.reset();
          palRequest.ioFlag.set(idx);
        }
        else {
          palRequest.ioFlag.set();
        }

        pPAL->write(palRequest, beginAt);
      }
    }
  }

  // Exclude CPU operation when initializing
  if (sendToPAL) {
    tick = beginAt;
    tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE_INTERNAL);
  }

  // GC if needed
  // I assumed that init procedure never invokes GC
  static float gcThreshold = conf.readFloat(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO);

  if (freeBlockRatio() < gcThreshold) {
    if (!sendToPAL) {
      panic("ftl: GC triggered while in initialization");
    }

    std::vector<uint32_t> list;
    uint64_t beginAt = tick;

    selectVictimBlock(list, beginAt);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "GC   | On-demand | %u blocks will be reclaimed", list.size());

    doGarbageCollection(list, beginAt);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "GC   | Done | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")", tick,
               beginAt, beginAt - tick);

    stat.gcCount++;
    stat.reclaimedBlocks += list.size();
  }
}

void PageMapping::trimInternal(Request &req, uint64_t &tick) {
  auto mappingList = table.find(req.lpn);

  if (mappingList != table.end()) {
    if (bRandomTweak) {
      pDRAM->read(&(*mappingList), 8 * req.ioFlag.count(), tick);
    }
    else {
      pDRAM->read(&(*mappingList), 8, tick);
    }

    // Do trim
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      auto &mapping = mappingList->second.at(idx);
      
      // Check if the mapping is valid
      if (mapping.first >= param.totalPhysicalBlocks ||
          mapping.second >= param.pagesInBlock) {
        continue;
      }
      
      auto block = blocks.find(mapping.first);

      if (block == blocks.end()) {
        // Skip if block doesn't exist
        continue;
      }

      block->second.invalidate(mapping.second, idx);
    }

    // Remove mapping
    table.erase(mappingList);

    tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::TRIM_INTERNAL);
  }
}

void PageMapping::eraseInternal(PAL::Request &req, uint64_t &tick) {
  static uint64_t threshold =
      conf.readUint(CONFIG_FTL, FTL_BAD_BLOCK_THRESHOLD);
  auto block = blocks.find(req.blockIndex);

  // Sanity checks
  if (block == blocks.end()) {
    panic("No such block");
  }

  if (block->second.getValidPageCount() != 0) {
    panic("There are valid pages in victim block");
  }

  // Erase block
  block->second.erase();

  pPAL->erase(req, tick);

  // Check erase count
  uint32_t erasedCount = block->second.getEraseCount();

  if (erasedCount < threshold) {
    // Reverse search
    auto iter = freeBlocks.end();

    while (true) {
      iter--;

      if (iter->getEraseCount() <= erasedCount) {
        // emplace: insert before pos
        iter++;

        break;
      }

      if (iter == freeBlocks.begin()) {
        break;
      }
    }

    // Insert block to free block list
    freeBlocks.emplace(iter, std::move(block->second));
    nFreeBlocks++;
  }

  // Remove block from block list
  blocks.erase(block);

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::ERASE_INTERNAL);
}

float PageMapping::calculateWearLeveling() {
  uint64_t totalEraseCnt = 0;
  uint64_t sumOfSquaredEraseCnt = 0;
  uint64_t numOfBlocks = param.totalLogicalBlocks;
  uint64_t eraseCnt;

  for (auto &iter : blocks) {
    eraseCnt = iter.second.getEraseCount();
    totalEraseCnt += eraseCnt;
    sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
  }

  // freeBlocks is sorted
  // Calculate from backward, stop when eraseCnt is zero
  for (auto riter = freeBlocks.rbegin(); riter != freeBlocks.rend(); riter++) {
    eraseCnt = riter->getEraseCount();

    if (eraseCnt == 0) {
      break;
    }

    totalEraseCnt += eraseCnt;
    sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
  }

  if (sumOfSquaredEraseCnt == 0) {
    return -1;  // no meaning of wear-leveling
  }

  return (float)totalEraseCnt * totalEraseCnt /
         (numOfBlocks * sumOfSquaredEraseCnt);
}

void PageMapping::calculateTotalPages(uint64_t &valid, uint64_t &invalid) {
  valid = 0;
  invalid = 0;

  for (auto &iter : blocks) {
    valid += iter.second.getValidPageCount();
    invalid += iter.second.getDirtyPageCount();
  }
}

void PageMapping::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "page_mapping.gc.count";
  temp.desc = "Total GC count";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.reclaimed_blocks";
  temp.desc = "Total reclaimed blocks in GC";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.superpage_copies";
  temp.desc = "Total copied valid superpages during GC";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.page_copies";
  temp.desc = "Total copied valid pages during GC";
  list.push_back(temp);

  // For the exact definition, see following paper:
  // Li, Yongkun, Patrick PC Lee, and John Lui.
  // "Stochastic modeling of large-scale solid-state storage systems: analysis,
  // design tradeoffs and optimization." ACM SIGMETRICS (2013)
  temp.name = prefix + "page_mapping.wear_leveling";
  temp.desc = "Wear-leveling factor";
  list.push_back(temp);

  // Add RL-GC stats if enabled
  if (bEnableRLGC) {
    temp.name = prefix + "ftl.rlgc.gc_invocations";
    temp.desc = "Number of RL-GC invocations";
    list.push_back(temp);
    
    temp.name = prefix + "ftl.rlgc.page_copies";
    temp.desc = "Total pages copied during RL-GC";
    list.push_back(temp);
    
    temp.name = prefix + "ftl.rlgc.intensive_gc";
    temp.desc = "Number of intensive GCs triggered";
    list.push_back(temp);
    
    temp.name = prefix + "ftl.rlgc.avg_reward";
    temp.desc = "Average reward received by RL-GC";
    list.push_back(temp);
  }
}

void PageMapping::getStatValues(std::vector<double> &values) {
  values.push_back(stat.gcCount);
  values.push_back(stat.reclaimedBlocks);
  values.push_back(stat.validSuperPageCopies);
  values.push_back(stat.validPageCopies);
  values.push_back(calculateWearLeveling());

  // Add RL-GC stat values if enabled
  if (bEnableRLGC) {
    uint64_t invocations, pageCopies, intensiveGCs;
    float avgReward;
    
    pRLGC->getStats(invocations, pageCopies, intensiveGCs, avgReward);
    
    values.push_back(invocations);
    values.push_back(pageCopies);
    values.push_back(intensiveGCs);
    values.push_back(avgReward);
  }
}

void PageMapping::resetStatValues() {
  memset(&stat, 0, sizeof(stat));

  // Reset RL-GC stats if enabled
  if (bEnableRLGC) {
    pRLGC->resetStats();
  }
}

uint32_t PageMapping::performPartialGC(uint32_t pagesToCopy, std::vector<uint32_t> &victimBlocks, uint64_t &tick) {
  // Safety check - don't try to copy 0 pages
  if (pagesToCopy == 0) {
    return 0;
  }
  
  // Track statistics
  stat.gcCount++;
  
  // Select victim blocks
  if (victimBlocks.size() == 0) {
    // No victim blocks provided, select them
    uint64_t tickLocal = tick;
    
    // Select victim blocks using the existing method
    selectVictimBlock(victimBlocks, tickLocal);
    
    // If no victims found, return early
    if (victimBlocks.size() == 0) {
      return 0;
    }
  }
  
  uint32_t copiedPages = 0;
  
  // Get the first victim block
  uint32_t victimBlockID = victimBlocks[0];
  auto blockIter = blocks.find(victimBlockID);
  
  // Safety check - make sure block exists
  if (blockIter == blocks.end()) {
    return 0;
  }
  
  Block &victimBlock = blockIter->second;
  
  // Safety check - make sure block has valid pages
  if (victimBlock.getValidPageCount() == 0) {
    // No valid pages to copy, just erase the block
    PAL::Request req(param.ioUnitInPage);
    req.blockIndex = victimBlockID;
    eraseInternal(req, tick);
    return 0;
  }
  
  // Copy valid pages up to the requested number
  for (uint32_t pageIndex = 0; pageIndex < param.pagesInBlock && copiedPages < pagesToCopy; pageIndex++) {
    // First check if the page has any valid data at all to avoid unnecessary processing
    if (victimBlock.getValidPageCount() == 0) {
      break;  // No more valid pages in this block
    }
    
    // Initialize vectors and bitset with proper sizes before calling getPageInfo
    std::vector<uint64_t> lpns(param.ioUnitInPage);
    Bitset validBits(param.ioUnitInPage);
    
    // Get page info safely
    bool hasValidData = victimBlock.getPageInfo(pageIndex, lpns, validBits);
    
    // Skip if no valid data or no valid bits
    if (!hasValidData || !validBits.any()) {
      continue;
    }
    
    // Allocate a new page
    uint32_t newBlockID = getLastFreeBlock(validBits);
    auto newBlockIter = blocks.find(newBlockID);
    if (newBlockIter == blocks.end()) {
      panic("New block not found");
    }
    uint32_t newPageID = newBlockIter->second.getNextWritePageIndex();
    
    // Copy the page data (read from old, write to new)
    PAL::Request palRequest(param.ioUnitInPage);
    palRequest.blockIndex = victimBlockID;
    palRequest.pageIndex = pageIndex;
    palRequest.ioFlag = validBits;
    
    // Read from old location
    pPAL->read(palRequest, tick);
    
    // Write to new location
    palRequest.blockIndex = newBlockID;
    palRequest.pageIndex = newPageID;
    pPAL->write(palRequest, tick);
    
    // Update mapping table for each valid bit
    for (uint32_t idx = 0; idx < param.ioUnitInPage; idx++) {
      if (validBits.test(idx)) {
        uint64_t lpn = lpns[idx];
        auto mappingList = table.find(lpn);
        
        if (mappingList != table.end()) {
          mappingList->second.at(idx).first = newBlockID;
          mappingList->second.at(idx).second = newPageID;
        }
        
        // Invalidate the old page
        victimBlock.invalidate(pageIndex, idx);
      }
    }
    
    copiedPages++;
  }
  
  // If all valid pages were copied, erase the block
  if (victimBlock.getValidPageCount() == 0) {
    PAL::Request req(param.ioUnitInPage);
    req.blockIndex = victimBlockID;
    eraseInternal(req, tick);
  }
  
  // Update statistics
  stat.validPageCopies += copiedPages;
  
  // Return the number of copied pages
  return copiedPages;
}

}  // namespace FTL

}  // namespace SimpleSSD
