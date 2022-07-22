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

 //NOTES!!!!
 //NEED TO:
 // 1) fix the accesses to certain functions to get the dimensions (num of tiers, pools, classes)
 // 2) change the benefit function to work with class-level allocation latency
 //allocLatencyNs.estimate() from AllocationStats
 //THEN
 // we can test it!

#include "cachelib/allocator/DynamicFreeThresholdStrategy.h"
#include "cachelib/allocator/Cache.h"
#include "cachelib/allocator/CacheStats.h"
#include <vector>
#include <tuple>
#include <folly/logging/xlog.h>

namespace facebook {
namespace cachelib {



DynamicFreeThresholdStrategy::DynamicFreeThresholdStrategy(double lowEvictionAcWatermark, double highEvictionAcWatermark, uint64_t evictionHotnessThreshold)
    : lowEvictionAcWatermark(lowEvictionAcWatermark), highEvictionAcWatermark(highEvictionAcWatermark), evictionHotnessThreshold(evictionHotnessThreshold) {
        auto numTiers = getNumTiers();
        auto numPools = getPoolIds().size();
        auto numClasses = getClassIds().size;
        for (int i = 0; i < numTiers; i++) {
          std::vector<std::vector<std::tuple<double, double, double>>> poolHighThresholds;
          std::vector<std::vector<std::tuple<double, double>>> poolBenefits;
          for (int j = 0; j < numPools; j++) {
            std::vector<std::tuple<double, double, double>> classHighThresholds;
            std::vector<std::tuple<double, double>> classBenefits;
            for (int k = 0; k < numClasses; k++) {
                classHighThresholds.push_back(std::make_tuple(highEvictionAcWatermark, highEvictionAcWatermark, highEvictionAcWatermark));
                classBenefits.push_back(std::make_tuple(0.0, 0.0));
            }
            poolHighThresholds.push_back(classHighThresholds);
            poolBenefits.push_back(classBenefits);
          }
          highEvictionAcWatermarks.push_back(poolHighThresholds);
          acBenefits.push_back(poolBenefits);
        }
      }

size_t DynamicFreeThresholdStrategy::calculateBatchSize(const CacheBase& cache,
                                       unsigned int tid,
                                       PoolId pid,
                                       ClassId cid,
                                       size_t allocSize,
                                       size_t acMemorySize) {

  auto acFree = cache.acFreePercentage(tid, pid, cid);
  auto acHighThresholdAtI = std::get<0>(highEvictionAcWatermarks[tid][pid][cid]);
  auto acHighThresholdAtIMinus1 = std::get<1>(highEvictionAcWatermarks[tid][pid][cid]);
  auto acHighThresholdAtIMinus2 = std::get<2>(highEvictionAcWatermarks[tid][pid][cid]);
  auto acHighThresholdAtINew = std::get<0>(highEvictionAcWatermarks[tid][pid][cid]);

  if (acFree >= acHighThresholdAtI)
    return 0;

  //TODO: add class-based allocation latency for highEvictionWatermark
  //TODO: add class-based read latency for lowEvictionWatermark
  //auto latencies = cache.getAllocationLatency();

  uint64_t p99 = 1;
  //uint64_t p99 = latencies.back(); //is p99 for now, should change to class-level latency value (e.g. rolling avg)
  //if (p99 == 0) {
      //p99 = 1;
  //}
  calculateBenefitMig(p99, tid, pid, cid);

  if (toFreeMemPercent < acFree / 2) {
    acHighThresholdAtINew =- highEvictionDelta;
  } else {
    if (std::get<0>(acBenefits[tid][pid][cid]) > std::get<1>(acBenefits[tid][pid][cid])) {
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
  toFreeMemPercent = acHighThresholdAtINew - acFree;
  auto toFreeItems = static_cast<size_t>(toFreeMemPercent * acMemorySize / allocSize);

  std::get<0>(highEvictionAcWatermarks[tid][pid][cid]) = acHighThresholdAtINew;
  std::get<1>(highEvictionAcWatermarks[tid][pid][cid]) = acHighThresholdAtI;
  std::get<2>(highEvictionAcWatermarks[tid][pid][cid]) = acHighThresholdAtIMinus1;

  return toFreeItems;
}

void DynamicFreeThresholdStrategy::calculateBenefitMig(uint64_t p99, unsigned int tid, PoolId pid, ClassId cid) {
    auto currentBenefit = std::get<0>(acBenefits[tid][pid][cid]);
    std::get<1>(acBenefits[tid][pid][cid]) = currentBenefit;
    std::get<0>(acBenefits[tid][pid][cid]) = 1 / p99;
}

} // namespace cachelib
} // namespace facebook
