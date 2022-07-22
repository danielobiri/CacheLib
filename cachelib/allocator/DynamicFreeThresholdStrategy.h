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

  //class-specific dynamic free high threshold strategy

#pragma once

#include "cachelib/allocator/Cache.h"
#include "cachelib/allocator/BackgroundEvictorStrategy.h"
#include "cachelib/allocator/CacheStats.h"
#include "cachelib/allocator/memory/MemoryPoolManager.h"
#include "cachelib/allocator/memory/MemoryAllocator.h"
#include <vector>
#include <tuple>

namespace facebook {
namespace cachelib {


// Base class for background eviction strategy.
class DynamicFreeThresholdStrategy : public BackgroundEvictorStrategy {

public:
  DynamicFreeThresholdStrategy(double lowEvictionAcWatermark, double highEvictionAcWatermark, uint64_t maxEvictionBatch, uint64_t minEvictionBatch);
  ~DynamicFreeThresholdStrategy() {}

  std::vector<size_t> calculateBatchSizes(const CacheBase& cache, std::vector<std::tuple<TierId, PoolId, ClassId>> acVec);
                                       //unsigned int tid,
                                       //PoolId pid,
                                       //ClassId cid,
                                       //size_t allocSize,
                                       //size_t acMemorySize);

private:
  double lowEvictionAcWatermark{2.0}; //this threshold is used outside this class and is not adjusted currently
  double highEvictionAcWatermark{5.0}; //this threshold is adjusted internally within this class
  //double toFreeMemPercent{0.0}; //Q: What happens to this value when the background thread is not activated in a certain period in Class x? Should we set it to 0?
  std::vector<std::vector<std::vector<std::tuple<double, double>>>> acToFreeMemPercents;
  double highEvictionDelta{1.0}; //TODO: tune this param, experiment with multiple values, (maybe base it on access freq or other access stat), perhaps use the benefit function to adjust this param (binned)?
  std::vector<std::vector<std::vector<std::tuple<double, double, double>>>> highEvictionAcWatermarks;
  //individual thresholds for each class, adjusted internally within this class
  //index 0 for i-th window
  //index 1 for i-1 window
  //index 2 for i-2 window

  //std::array<std::array<std::array<MemoryAllocator::kMaxClasses>, MemoryPoolManager::kMaxPools>, CacheBase::kMaxTiers> highEvictionAcWatermarks; //change structure, but use this for dimensions

  std::vector<std::vector<std::vector<std::tuple<double, double>>>> acBenefits;
  //index 0 for current benefit (i-th window)
  //index 1 for previous benefit (i-1 window)


private:
  void calculateBenefitMig(uint64_t p99, unsigned int tid, PoolId pid, ClassId cid);

};

} // namespace cachelib
} // namespace facebook
