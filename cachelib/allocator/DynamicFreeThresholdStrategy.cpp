/*
 * Copyright (c) Intel and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 //class-specific dynamic free (high) threshold strategy


#include "cachelib/allocator/DynamicFreeThresholdStrategy.h"
#include "cachelib/allocator/memory/MemoryPoolManager.h"
#include "cachelib/allocator/memory/MemoryAllocator.h"
#include "cachelib/allocator/Cache.h"
#include "cachelib/allocator/CacheStats.h"
#include <folly/logging/xlog.h>

namespace facebook {
namespace cachelib {



DynamicFreeThresholdStrategy::DynamicFreeThresholdStrategy(double lowEvictionAcWatermark, double highEvictionAcWatermark, uint64_t maxEvictionBatch, uint64_t minEvictionBatch, double highEvictionDelt )
    : lowEvictionAcWatermark(lowEvictionAcWatermark), highEvictionAcWatermark(highEvictionAcWatermark), maxEvictionBatch(maxEvictionBatch), minEvictionBatch(minEvictionBatch), highEvictionDelta(highEvictionDelt), 
    highEvictionAcWatermarks(CacheBase::kMaxTiers, 
                                std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                                std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                                std::vector<double>(3, highEvictionAcWatermark)))), //initialize highEvictionAcWatermarks
    acBenefits(CacheBase::kMaxTiers, 
                  std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                  std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                  std::vector<double>(2, 0.0)))), //initialize acBenefits
    acToFreeMemPercents(CacheBase::kMaxTiers,
                        std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                        std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                        std::vector<double>(2, 0.0)))) {} //initialize acToFreeMemPercents

std::vector<size_t> DynamicFreeThresholdStrategy::calculateBatchSizes(const CacheBase& cache, std::vector<std::tuple<TierId, PoolId, ClassId>> acVec) {
  
  std::vector<size_t> batches{}; //contain number of items to evict for ac classes in the batch

  for (auto [tid, pid, cid] : acVec) {

    auto stats = cache.getAllocationClassStats(tid, pid, cid); //ac class stats
    auto acFree = stats.approxFreePercent; //amount of free memory in the ac class

    if (acFree >= lowEvictionAcWatermark) { //if the amount of free memory in the ac class is above lowEvictionAcWatermark,
      batches.push_back(0); //we do not evict
    } else {
      auto acHighThresholdAtI = highEvictionAcWatermarks[tid][pid][cid][0]; //current high threshold
      auto acHighThresholdAtINew = highEvictionAcWatermarks[tid][pid][cid][0]; //new high threshold, will be adjusted
      auto acHighThresholdAtIMinus1 = highEvictionAcWatermarks[tid][pid][cid][1]; //previous high threshold
      auto acHighThresholdAtIMinus2 = highEvictionAcWatermarks[tid][pid][cid][2]; //previous of previous high threshold
      auto toFreeMemPercentAtI = acToFreeMemPercents[tid][pid][cid][0];
      auto toFreeMemPercentAtIMinus1 = acToFreeMemPercents[tid][pid][cid][1]; //previous amount of memory to free up in the ac class
      auto acAllocLatencyNs = cache.getAllocationClassStats(tid, pid, cid).allocLatencyNs.estimate(); //moving avg latency estimation for ac class
      
      calculateBenefitMig(acAllocLatencyNs, tid, pid, cid);

      if (toFreeMemPercentAtIMinus1 < acFree / 2) { //if we evicted more in the previous period (IMinus1) then half of the current free memory space,
        acHighThresholdAtINew -= highEvictionDelta; //then we would like to discourage eviction by lowering the high threshold to increase the usage of the memory capacity
      } else {
        //hill-climbing algorithm:
        if (acBenefits[tid][pid][cid][0] > acBenefits[tid][pid][cid][1]) { //if the current benefit is greater than the previous benefit,
          if (acHighThresholdAtIMinus1 > acHighThresholdAtIMinus2) { //and the high threshold increased in the previous period,
            acHighThresholdAtINew += highEvictionDelta; //then we increase the high threshold again.
          } else {
            acHighThresholdAtINew -= highEvictionDelta; //else we decrease it.
          }
        } else {
          if (acHighThresholdAtIMinus1 < acHighThresholdAtIMinus2) { //if the current benefit is less than the previous benefit AND the high threshold decreased in the prev. period,
            acHighThresholdAtINew += highEvictionDelta; //then we increase the high threshold.
          } else {
            acHighThresholdAtINew -= highEvictionDelta; //else we decrease it.
          }
        }
      }

      acHighThresholdAtINew = std::max(acHighThresholdAtINew, lowEvictionAcWatermark); // high threshold cannot be less than the low threshold
                                                                                       //std::max(acHighThresholdAtINew, acFree)
      auto toFreeMemPercent = acHighThresholdAtINew - acFree; //calculate amount of memory to free up in the ac class
      acToFreeMemPercents[tid][pid][cid][1] = toFreeMemPercentAtI; //update acToFreeMemPercents
      acToFreeMemPercents[tid][pid][cid][0] = toFreeMemPercent; //update acToFreeMemPercents
      auto toFreeItems = static_cast<size_t>(toFreeMemPercent * stats.memorySize / stats.allocSize); //calculate number of items to evict for current ac class
      batches.push_back(toFreeItems); //append batches

      highEvictionAcWatermarks[tid][pid][cid][0] = acHighThresholdAtINew; //update highEvictionAcWatermarks
      highEvictionAcWatermarks[tid][pid][cid][1] = acHighThresholdAtI; //update highEvictionAcWatermarks
      highEvictionAcWatermarks[tid][pid][cid][2] = acHighThresholdAtIMinus1; //update highEvictionAcWatermarks
    }
  }

  if (batches.size() == 0) {
    return batches;
  }
  auto maxBatch = *std::max_element(batches.begin(), batches.end());
  if (maxBatch == 0)
    return batches;

  std::transform(batches.begin(), batches.end(), batches.begin(), [&](auto numItems){
    if (numItems == 0) {
      return 0UL;
    }

    auto cappedBatchSize = maxEvictionBatch * numItems / maxBatch;
    if (cappedBatchSize < minEvictionBatch)
      return minEvictionBatch;
    else
      return cappedBatchSize;
  });
  return batches;
}

void DynamicFreeThresholdStrategy::calculateBenefitMig(uint64_t acLatency, unsigned int tid, PoolId pid, ClassId cid) {
    auto currentBenefit = acBenefits[tid][pid][cid][0]; //current benefit
    acBenefits[tid][pid][cid][1] = currentBenefit; //update previous benefit
    acBenefits[tid][pid][cid][0] = 1.0 / acLatency; //calculate current benefit based on acLatency
}

BackgroundStrategyStats DynamicFreeThresholdStrategy::getStats() { 
    BackgroundStrategyStats s;

    auto numClasses = MemoryAllocator::kMaxClasses;
    for (int i = 0; i < 1; i++) {
      for (int j = 0; j < 1; j++) {
        for (int k = 0; k < numClasses; k++) {
            s.highEvictionAcWatermarks[k] = highEvictionAcWatermarks[i][j][k][0];
        }
      }
    }
    return s;
}

} // namespace cachelib
} // namespace facebook
