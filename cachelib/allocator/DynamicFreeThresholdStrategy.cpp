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

 //class-specific dynamic free high threshold strategy


#include "cachelib/allocator/DynamicFreeThresholdStrategy.h"
#include "cachelib/allocator/memory/MemoryPoolManager.h"
#include "cachelib/allocator/memory/MemoryAllocator.h"
#include "cachelib/allocator/Cache.h"
#include <folly/logging/xlog.h>

namespace facebook {
namespace cachelib {



DynamicFreeThresholdStrategy::DynamicFreeThresholdStrategy(double lowEvictionAcWatermark, double highEvictionAcWatermark, uint64_t maxEvictionBatch, uint64_t minEvictionBatch)
    : lowEvictionAcWatermark(lowEvictionAcWatermark), highEvictionAcWatermark(highEvictionAcWatermark), maxEvictionBatch(maxEvictionBatch), minEvictionBatch(minEvictionBatch), 
    highEvictionAcWatermarks(CacheBase::kMaxTiers, 
                                std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                                std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                                std::vector<double>(3)))),
    acBenefits(CacheBase::kMaxTiers, 
                  std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                  std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                  std::vector<double>(2)))),
    acToFreeMemPercents(CacheBase::kMaxTiers,
                        std::vector<std::vector<std::vector<double>>>(MemoryPoolManager::kMaxPools,
                        std::vector<std::vector<double>>(MemoryAllocator::kMaxClasses,
                        std::vector<double>(2)))) {
      /*
      auto numTiers = CacheBase::kMaxTiers;
      auto numPools = MemoryPoolManager::kMaxPools;
      auto numClasses = MemoryAllocator::kMaxClasses;
      for (int i = 0; i < numTiers; i++) {
        for (int j = 0; j < numPools; j++) {
          for (int k = 0; k < numClasses; k++) {
            for (int l = 0; l < 3; l++) {
              highEvictionAcWatermarks[i][j][k][l] = 5.0;
            }
            for (int m = 0; m < 2; m++) {
              acBenefits[i][j][k][m] = 0.0;
              acToFreeMemPercents[i][j][k][m] = 0.0;
            }
          }
        }
      }

      for (int i = 0; i < numTiers; i++) {
        std::vector<std::vector<std::vector<double>>> poolHighThresholds;
        std::vector<std::vector<std::vector<double>>> poolBenefits;
        std::vector<std::vector<std::vector<double>>> poolToFreeMemPercents;
        for (int j = 0; j < numPools; j++) {
          std::vector<std::vector<double>> classHighThresholds;
          std::vector<std::vector<double>> classBenefits;
          std::vector<std::vector<double>> classToFreeMemPercents;
          for (int k = 0; k < numClasses; k++) {
            classHighThresholds.push_back(std::vector<double>{5.0, 5.0, 5.0});
            classBenefits.push_back(std::vector<double>{0.0, 0.0});
            classToFreeMemPercents.push_back(std::vector<double>{0.0, 0.0});
          }
          poolHighThresholds.push_back(classHighThresholds);
          poolBenefits.push_back(classBenefits);
          poolToFreeMemPercents.push_back(classToFreeMemPercents);
        }
        highEvictionAcWatermarks.push_back(poolHighThresholds);
        acBenefits.push_back(poolBenefits);
        acToFreeMemPercents.push_back(poolToFreeMemPercents);
      }
      */
    }

std::vector<size_t> DynamicFreeThresholdStrategy::calculateBatchSizes(const CacheBase& cache, std::vector<std::tuple<TierId, PoolId, ClassId>> acVec) {
  
  std::vector<size_t> batches{};

  for (auto [tid, pid, cid] : acVec) {
    auto stats = cache.getAllocationClassStats(tid, pid, cid);
    auto acFree = stats.approxFreePercent;
    auto acHighThresholdAtI = highEvictionAcWatermarks[tid][pid][cid][0];

    if (acFree >= acHighThresholdAtI) {
      batches.push_back(0);
    } else {
      auto acHighThresholdAtIMinus1 = highEvictionAcWatermarks[tid][pid][cid][1];
      auto acHighThresholdAtIMinus2 = highEvictionAcWatermarks[tid][pid][cid][2];
      auto acHighThresholdAtINew = highEvictionAcWatermarks[tid][pid][cid][0];
      auto toFreeMemPercentAtIMinus1 = acToFreeMemPercents[tid][pid][cid][1];
      auto acAllocLatencyNs = cache.getAllocationClassStats(tid, pid, cid).allocLatencyNs.estimate();
      
      calculateBenefitMig(acAllocLatencyNs, tid, pid, cid);

      if (toFreeMemPercentAtIMinus1 < acFree / 2) {
        acHighThresholdAtINew =- highEvictionDelta;
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
    auto currentBenefit = acBenefits[tid][pid][cid][0];
    acBenefits[tid][pid][cid][1] = currentBenefit;
    acBenefits[tid][pid][cid][0] = 1 / acLatency;
}

BackgroundStrategyStats DynamicFreeThresholdStrategy::getStats() { 
    BackgroundStrategyStats s;

    auto numClasses = MemoryAllocator::kMaxClasses;
    for (int i = 0; i < 1; i++) {
      for (int j = 0; j < 1; j++) {
        for (int k = 0; k < numClasses; k++) {
            s.highEvictionAcWatermarks[k] = std::get<0>( highEvictionAcWatermarks[i][j][k] );
        }
      }
    }
    return s;
}

} // namespace cachelib
} // namespace facebook
