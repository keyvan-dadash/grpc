
if(NOT LIBCUCKOO_ROOT_DIR)
set(LIBCUCKOO_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcuckoo)
endif()

if(EXISTS "${LIBCUCKOO_ROOT_DIR}/CMakeLists.txt")
add_subdirectory(${LIBCUCKOO_ROOT_DIR} third_party/libcuckoo)
else()
message(WARNING "gRPC_LIBCUCKOO_PROVIDER is \"module\" but LIBCUCKOO_ROOT_DIR is wrong")
endif()