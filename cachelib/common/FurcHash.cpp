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

#include <folly/logging/xlog.h>

#include <cstdint>

#include "cachelib/common/Hash.h"

namespace facebook {
namespace cachelib {

namespace {

// Maximum tries for in-range result before just returning 0. Should be >=
// kFurcShift + 9 to maintain a smooth distribution
constexpr uint32_t kMaxTries = 32;

// Gap in bit index per try; limits us to 2^FURCHASH shards.  Making this
// larger will sacrifice a modest amount of performance and require a larger
// value for kHashCacheSize
constexpr uint32_t kFurcShift = 23;

// Size of cache for hash values; should be >
// (kFurcShift * (kMaxTries * kFurcShift + 1)) / 64
// This value should work up to kFurcShift == 24 :
constexpr int32_t kHashCacheSize = 300;

struct FurcHashState {
  uint32_t nParts;
  int32_t hashIdx;
  uint64_t hashCache[kHashCacheSize];
};

// MurmurHash2, 64-bit versions, by Austin Appleby
//
// The same caveats as 32-bit MurmurHash2 apply here - beware of alignment
// and endian-ness issues if used across multiple platforms.
//
// 64-bit hash for 64-bit platforms
uint64_t murmurHash64A(const void* key, int len, uint64_t seed) {
  const uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;

  uint64_t h = seed ^ (len * m);

  const uint64_t* data = reinterpret_cast<const uint64_t*>(key);
  const uint64_t* end = data + (len / 8);

  while (data != end) {
    uint64_t k = *data++;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const uint8_t* data2 = (const uint8_t*)data;

  switch (len & 7) {
  case 7:
    h ^= (uint64_t)data2[6] << 48;
    FOLLY_FALLTHROUGH;
  case 6:
    h ^= (uint64_t)data2[5] << 40;
    FOLLY_FALLTHROUGH;
  case 5:
    h ^= (uint64_t)data2[4] << 32;
    FOLLY_FALLTHROUGH;
  case 4:
    h ^= (uint64_t)data2[3] << 24;
    FOLLY_FALLTHROUGH;
  case 3:
    h ^= (uint64_t)data2[2] << 16;
    FOLLY_FALLTHROUGH;
  case 2:
    h ^= (uint64_t)data2[1] << 8;
    FOLLY_FALLTHROUGH;
  case 1:
    h ^= (uint64_t)data2[0];
    h *= m;
  };

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

// MurmurHash64A performance-optimized for hash of uint64_t keys
uint64_t murmurRehash64A(uint64_t key) {
  const uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;

  uint64_t h =
      static_cast<uint64_t>(MurmurHash2::kMurmur2Seed) ^ (sizeof(uint64_t) * m);

  key *= m;
  key ^= key >> r;
  key *= m;

  h ^= key;
  h *= m;

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

// getbit -- The bitstream generator
// Given state and a bit index, provide a pseudorandom bit dependent on both.
// Caches hash values.
uint32_t getbit(FurcHashState* statep, uint32_t choice) {
  int32_t newHashIdx = (choice >> 6); // divide by 64 to get 64-bit cache index

  // See comment above for kHashCacheSize -- this should only fire if the
  // constants are modified inappropriately.
  XDCHECK_LT(newHashIdx, kHashCacheSize);

  if (statep->hashIdx < newHashIdx) {
    for (int n = statep->hashIdx + 1; n <= newHashIdx; n++) {
      statep->hashCache[n] = murmurRehash64A(statep->hashCache[n - 1]);
    }
    statep->hashIdx = newHashIdx;
  }
  // now return selected bit from cache
  return (statep->hashCache[newHashIdx] >> (choice & 0x3f)) & 0x1;
}

} // namespace

// furcHash -- a consistent hash function using a binary decision tree.
// Based on an algorithm by Mark Rabkin with two changes:
//    1) Uses murmurHash64A to hash the original key and to generate
//       additional bits by recursively rehashing
//    2) the original recursive algorithm for the decision tree has been
//       made iterative
//
// Assumes that "m" is 8 million or less (2^kFurcShift).  Making kFurcShift
// bigger also makes FurcHash modestly slower.
//
// Performance is in the sub-500ns range to over 100,000 shards with 13-byte
// keys. This version of furcHash is fairly insensitive to key length since
// additional bits are generated by re-hashing the initial murmurHash64A.
uint32_t furcHash(const void* key, size_t len, uint32_t nPart) {
  XDCHECK_LE(len, static_cast<size_t>(std::numeric_limits<int>::max()));

  if (nPart <= 1) {
    return 0;
  }

  FurcHashState state;
  state.hashCache[0] =
      murmurHash64A(key, static_cast<int>(len), MurmurHash2::kMurmur2Seed);
  state.hashIdx = 0;

  uint32_t bitNum = 0;
  for (bitNum = 0; nPart > (1ul << bitNum); bitNum++) {
  }

  uint32_t choice = bitNum;
  uint32_t result = 0;
  for (uint32_t tries = 0; tries < kMaxTries; tries++) {
    while (!getbit(&state, choice)) {
      if (--bitNum == 0) {
        return 0;
      }
      choice = bitNum;
    }
    choice += kFurcShift;
    result = 1;
    for (uint32_t i = 0; i < bitNum - 1; i++) {
      result = (result << 1) | getbit(&state, choice);
      choice += kFurcShift;
    }
    if (result < nPart) {
      return result;
    }
  }
  // give up; 0 is legal value in all cases.
  return 0;
}

} // namespace cachelib
} // namespace facebook
