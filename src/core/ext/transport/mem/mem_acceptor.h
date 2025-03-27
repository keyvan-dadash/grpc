#ifndef MEM_ACCEPTOR
#define MEM_ACCEPTOR

#include "shm/posix_channel.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include <memory>
namespace grpc_core {
namespace mem {

class MEMAcceptor {
public:
  explicit MEMAcceptor(std::string bind_mem_addr);

  ~MEMAcceptor();

  msg::ServerCtrlCommands Accept();

private:
  std::string bind_mem_addr_;
  std::shared_ptr<shm::posix::POSIXChannel<msg::ServerCtrlCommands, 128, 1>>
      accept_ctrl_channel_;
};

} // namespace mem

} // namespace grpc_core

#endif
