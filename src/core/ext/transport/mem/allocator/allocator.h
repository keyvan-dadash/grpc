#ifndef SHARED_MEM_ALLOCATOR_
#define SHARED_MEM_ALLOCATOR_

#include <cstddef>
#include <cstdint>
#include <memory>
#include "snmalloc/pal/pal_consts.h"
#include "snmalloc/backend/fixedglobalconfig.h"
#include "snmalloc/mem/secondary.h"
#include "snmalloc/snmalloc.h"

namespace grpc_core {
namespace mem {
namespace allocator {

using namespace snmalloc;

using CustomGlobals = FixedRangeConfig<DefaultPal>;
using FixedAlloc = Allocator<CustomGlobals>;
using SharedPtrAllocator = std::shared_ptr<ScopedAllocator<FixedAlloc>>;

inline void init_allocator(void* region, std::size_t size_of_region) {
  auto oe_base = region;
  CustomGlobals::init(nullptr, oe_base, size_of_region);
};

} // namespace allocator
} // namespace mem
} // namespace grpc_core

#endif
