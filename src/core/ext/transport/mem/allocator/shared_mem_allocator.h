#ifndef SHARED_MEM_ALLOCATOR_H
#define SHARED_MEM_ALLOCATOR_H

#include "src/core/ext/transport/mem/allocator/allocator.h"
#include <cstddef>
#include <cstdint>
#include <memory>
namespace grpc_core {
namespace mem {
namespace allocator {

class SharedMemoryAllocator final : public Allocator {
public:
  SharedMemoryAllocator(char *buffer, size_t size_of_buffer, size_t chunk_size);
  ~SharedMemoryAllocator() override;

  void* Allocate(const std::size_t size) override;
  void Deallocate(void *ptr) override;

protected:
  void init() override;

private:
  enum class block_status : int8_t {
    kFree = 0x10,
    kAllocate = 0x20,
    kAllocateWithSeq = 0x30,
    kAllocateAsSeq = 0x40,
  };

  struct block {
    block();

    block_status status = block_status::kFree;
    int32_t block_num = 0;
    int32_t num_of_seq_allocated = 0;
    void* block_address;
  };

  void init_blocks();

  void* get_address_of_chunk(int32_t chunk_num);
  void* get_address_of_chunk_from_block_num(int32_t block_num);
  block* get_block_from_ptr(void* ptr);
  int get_num_chunks_for_size(const std::size_t size);
  block* find_window_of_blocks(const std::size_t size);

  
  size_t size_of_buffer_;
  size_t chunk_size_;
  int32_t chunks_for_allocation_;
  int32_t chunks_for_blocks_;
  char *buffer_;
  block *blocks_;
};

} // namespace allocator

} // namespace mem

} // namespace grpc_core

#endif
