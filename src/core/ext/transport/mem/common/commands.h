#ifndef COMMANDS_H
#define COMMANDS_H

#include <string>
namespace grpc_core {
namespace mem {

// copied from
// https://stackoverflow.com/questions/440133/how-do-i-create-a-random-alpha-numeric-string-in-c
static std::string gen_random(const int len) {
  static const char alphanum[] = "0123456789"
                                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz";
  std::string tmp_s;
  tmp_s.reserve(len);

  for (int i = 0; i < len; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  return tmp_s;
}

struct SharedMemArea {
  char *memory_area;
};

} // namespace mem
} // namespace grpc_core

#endif
