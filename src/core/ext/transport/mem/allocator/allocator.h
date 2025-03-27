#ifndef ALLOCATOR_H_
#define ALLOCATOR_H_

#include <cstddef>
namespace grpc_core {
namespace mem {

namespace allocator {

// Got from https://github.com/mtrebi/memory-allocators/blob/master/includes/Allocator.h
class Allocator {
public:
    
    explicit Allocator(const std::size_t totalSize) : m_totalSize { totalSize }, m_used { 0 }, m_peak { 0 } { }

    virtual ~Allocator() { m_totalSize = 0; }

    virtual void* Allocate(const std::size_t size) = 0;

    virtual void Deallocate(void* ptr) = 0;

protected:
    std::size_t m_totalSize;
    std::size_t m_used;   
    std::size_t m_peak;

    virtual void init() = 0;
};

} // namespace allocator

} // namespace mem

} // namespace grpc_core

#endif
