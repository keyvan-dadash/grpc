#ifndef TYPES_H_
#define TYPES_H_

#include <absl/strings/string_view.h>
#include <cstdint>
#include "absl/strings/string_view.h"
#include "snmalloc/snmalloc.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace mem {
namespace types {

struct String {
  String() : str(nullptr), str_size(0) {}
  String(char *str, uint32_t str_size) : str(str), str_size(str_size) {}

  char *str;
  uint32_t str_size = 0;
  int64_t ptr_offset;

  void CalcualteOffssetFrom(char *origin) {
    if (!IsEmpty()) {
      ptr_offset = str - origin;
    }
  }

  void ApplyOffsetFrom(char *origin) {
    if (!IsEmpty()) {
      str = (char *)snmalloc::pointer_offset(origin, ptr_offset);
    }
  }

  bool IsEmpty() const {
    return str_size == 0;
  }

  std::string GetString() const {
    return std::string(str, str+str_size);
  }

  absl::string_view GetStringView() const {
    return absl::string_view(str, str+str_size);
  }
};

struct KeyValue {
  KeyValue(String key, String value) : Key(key), Value(value) {}
  String Key;
  String Value;
};

} // namespace types

} // namespace mem

} // namespace grpc_core

#endif
