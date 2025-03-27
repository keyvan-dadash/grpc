
if(NOT ATOMIC_QUEUE_ROOT_DIR)
    set(ATOMIC_QUEUE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/atomic_queue)
endif()

if(EXISTS "${ATOMIC_QUEUE_ROOT_DIR}/CMakeLists.txt")
    add_subdirectory(${ATOMIC_QUEUE_ROOT_DIR} third_party/atomic_queue)
else()
    message(WARNING "gRPC_ATOMIC_QUEUE_PROVIDER is \"module\" but ATOMIC_QUEUE_ROOT_DIR is wrong")
endif()