#include "absl/log/check.h"
#include "src/core/ext/transport/mem/allocator/allocator.h"
#include "src/core/ext/transport/mem/allocator/shared_mem_allocator.h"
#include <cmath>

namespace grpc_core {
namespace mem {
namespace allocator {

SharedMemoryAllocator::SharedMemoryAllocator(char *buffer,
                                             size_t size_of_buffer,
                                             size_t chunk_size)
    : Allocator(size_of_buffer), size_of_buffer_(size_of_buffer),
      chunk_size_(chunk_size), buffer_(buffer) {
  CHECK_EQ(size_of_buffer % chunk_size, 0);
  init();
}

SharedMemoryAllocator::~SharedMemoryAllocator() {}

void *SharedMemoryAllocator::Allocate(const std::size_t size) {
  auto num_of_needed_chunks = get_num_chunks_for_size(size);
  block* block_for_allocation = find_window_of_blocks(size);

}

void SharedMemoryAllocator::Deallocate(void *ptr) {}

void SharedMemoryAllocator::init() {
  auto block_size = sizeof(block);
  auto number_of_possible_blocks = size_of_buffer_ % chunk_size_;
  auto number_of_possible_blocks_within_chunk =
      std::floor((double)chunk_size_ / (double)block_size);
  chunks_for_blocks_ =
      number_of_possible_blocks / (number_of_possible_blocks_within_chunk + 1);
  chunks_for_allocation_ = number_of_possible_blocks - chunks_for_blocks_;
  init_blocks();
}

void SharedMemoryAllocator::init_blocks() {
  blocks_ = (block *)buffer_;
  for (int i = 0; i < chunks_for_allocation_; i++) {
    new (blocks_ + i) block();
    blocks_[i].block_num = i;
    blocks_[i].block_address = get_address_of_chunk(i + chunks_for_blocks_);
  }
}

void *SharedMemoryAllocator::get_address_of_chunk(int32_t chunk_num) {
  return buffer_ + chunk_size_ * chunk_num;
}

void *
SharedMemoryAllocator::get_address_of_chunk_from_block_num(int32_t block_num) {
  return blocks_[block_num].block_address;
}

SharedMemoryAllocator::block *
SharedMemoryAllocator::get_block_from_ptr(void *ptr) {
  auto diff = (char *)ptr - buffer_;
  auto chunk_num = diff / chunk_size_;
  return &blocks_[chunk_num - chunks_for_blocks_];
}

int SharedMemoryAllocator::get_num_chunks_for_size(const std::size_t size) {
  return std::ceil((double)size / (double)chunk_size_);
}

SharedMemoryAllocator::block *
SharedMemoryAllocator::find_window_of_blocks(const std::size_t size) {
  block *ref = nullptr;
  auto num_of_needed_window = get_num_chunks_for_size(size);
  int found_seq = 0;
  for (int i = 0; i < chunks_for_allocation_; i++) {
    if (blocks_[i].status == block_status::kFree) {
      if (!found_seq) {
        ref = &blocks_[i];
      }
      found_seq++;
      if (found_seq >= num_of_needed_window) {
        break;
      }
    } else {
      ref = nullptr;
      found_seq = 0;
    }
  }

  return ref;
}

} // namespace allocator
} // namespace mem
} // namespace grpc_core
