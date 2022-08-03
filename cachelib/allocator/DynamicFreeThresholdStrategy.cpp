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



DynamicFreeThresholdStrategy::DynamicFreeThresholdStrategy(double lowEvictionAcWatermark, double highEvictionAcWatermark, uint64_t maxEvictionBatch, uint64_t minEvictionBatch)
    : lowEvictionAcWatermark(lowEvictionAcWatermark), highEvictionAcWatermark(highEvictionAcWatermark), maxEvictionBatch(maxEvictionBatch), minEvictionBatch(minEvictionBatch), 
    highEvictionAcWatermarks(CacheBase::kMaxTiers, 
                                std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                                std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                                std::vector<double>(3, highEvictionAcWatermark)))),
    acBenefits(CacheBase::kMaxTiers, 
                  std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                  std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                  std::vector<double>(2, 0.0)))),
    acToFreeMemPercents(CacheBase::kMaxTiers,
                        std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                        std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                        std::vector<double>(2, 0.0)))) {}

std::vector<size_t> DynamicFreeThresholdStrategy::calculateBatchSizes(const CacheBase& cache, std::vector<std::tuple<TierId, PoolId, ClassId>> acVec) {
  
  std::vector<size_t> batches{};

  for (auto [tid, pid, cid] : acVec) {

    auto stats = cache.getAllocationClassStats(tid, pid, cid);
    auto acFree = stats.approxFreePercent;

    if (acFree >= lowEvictionAcWatermark) {
      batches.push_back(0);
    } else {
      auto acHighThresholdAtI = highEvictionAcWatermarks[tid][pid][cid][0];
      auto acHighThresholdAtINew = highEvictionAcWatermarks[tid][pid][cid][0];
      auto acHighThresholdAtIMinus1 = highEvictionAcWatermarks[tid][pid][cid][1];
      auto acHighThresholdAtIMinus2 = highEvictionAcWatermarks[tid][pid][cid][2];
      auto toFreeMemPercentAtIMinus1 = acToFreeMemPercents[tid][pid][cid][1];
      auto acAllocLatencyNs = cache.getAllocationClassStats(tid, pid, cid).allocLatencyNs.estimate();
      
      calculateBenefitMig(acAllocLatencyNs, tid, pid, cid);

      if (toFreeMemPercentAtIMinus1 < acFree / 2) {
        acHighThresholdAtINew -= highEvictionDelta;
      } else {
        if (acBenefits[tid][pid][cid][0] > acBenefits[tid][pid][cid][1]) {
          if (acHighThresholdAtIMinus1 > acHighThresholdAtIMinus2) {
            acHighThresholdAtINew += highEvictionDelta;
          } else {
            acHighThresholdAtINew -= highEvictionDelta;
          }
        } else {
          if (acHighThresholdAtIMinus1 < acHighThresholdAtIMinus2) {
            acHighThresholdAtINew += highEvictionDelta;
          } else {
            acHighThresholdAtINew -= highEvictionDelta;
          }
        }
      }

      acHighThresholdAtINew = std::max(acHighThresholdAtINew, lowEvictionAcWatermark); //std::max(acHighThresholdAtINew, acFree)
      auto toFreeMemPercent = acHighThresholdAtINew - acFree;
      acToFreeMemPercents[tid][pid][cid][1] = toFreeMemPercent;
      auto toFreeItems = static_cast<size_t>(toFreeMemPercent * stats.memorySize / stats.allocSize);
      batches.push_back(toFreeItems);

      highEvictionAcWatermarks[tid][pid][cid][0] = acHighThresholdAtINew;
      highEvictionAcWatermarks[tid][pid][cid][1] = acHighThresholdAtI;
      highEvictionAcWatermarks[tid][pid][cid][2] = acHighThresholdAtIMinus1;
    }
  }

  if (batches.size() == 0) {
    return batches;
  }
  
  return batches;
}

void DynamicFreeThresholdStrategy::calculateBenefitMig(uint64_t acLatency, unsigned int tid, PoolId pid, ClassId cid) {
    auto currentBenefit = acBenefits[tid][pid][cid][0];
    acBenefits[tid][pid][cid][1] = currentBenefit;
    acBenefits[tid][pid][cid][0] = 1.0 / acLatency;
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
