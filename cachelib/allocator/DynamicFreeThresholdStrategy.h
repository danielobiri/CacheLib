/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#pragma once

#include "cachelib/allocator/BackgroundEvictorStrategy.h"
#include "cachelib/allocator/Cache.h"
#include "cachelib/allocator/CacheStats.h"
#include <vector>

namespace facebook {
namespace cachelib {


// Base class for background eviction strategy.
class DynamicFreeThresholdStrategy : public BackgroundEvictorStrategy {

public:
  DynamicFreeThresholdStrategy(double lowEvictionAcWatermark, double highEvictionAcWatermark, uint64_t maxEvictionBatch, uint64_t minEvictionBatch);
  ~DynamicFreeThresholdStrategy() {}

  std::vector<size_t> calculateBatchSizes(const CacheBase& cache, std::vector<std::tuple<TierId, PoolId, ClassId>> acVecs);

  BackgroundStrategyStats getStats();

private:
  double lowEvictionAcWatermark{3.0}; //for now: static threshold to trigger eviction
  double highEvictionAcWatermark{5.0}; //this threshold is adjusted internally, individually for each ac class, determines the number of items to evict
  uint64_t maxEvictionBatch{0}; //not used
  uint64_t minEvictionBatch{0}; //not used
  double highEvictionDelta{0.5}; //TODO: tune this param, experiment with multiple values, (maybe base it on access freq or other access stat), perhaps use the benefit function to adjust this param (binned)?

  std::vector<std::vector<std::vector<std::vector<double>>>> highEvictionAcWatermarks; //individual dynamic thresholds for each ac class
  //index 0 for i-th window
  //index 1 for i-1 window
  //index 2 for i-2 window

  std::vector<std::vector<std::vector<std::vector<double>>>> acBenefits;
  //index 0 for current benefit (i-th window)
  //index 1 for previous benefit (i-1 window)
  
  std::vector<std::vector<std::vector<std::vector<double>>>> acToFreeMemPercents; //logging toFreeMemPercents for comparison
  //index 0 for current toFreeMemPercent (i-th window)
  //index 1 for previous toFreeMemPercent (i-1 window)

private:
  void calculateBenefitMig(uint64_t p99, unsigned int tid, PoolId pid, ClassId cid);

};

} // namespace cachelib
} // namespace facebook
