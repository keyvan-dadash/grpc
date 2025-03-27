

#include "src/core/ext/transport/mem/mem_acceptor.h"
#include "src/core/ext/transport/mem/mem_message.h"
namespace grpc_core {
namespace mem {

MEMAcceptor::MEMAcceptor(std::string bind_mem_addr)
    : bind_mem_addr_(bind_mem_addr) {
  accept_ctrl_channel_ =
      std::make_shared<shm::posix::POSIXChannel<msg::ServerCtrlCommands, 128, 1>>(
          std::string(bind_mem_addr_),
          shm::posix::POSIX_CHANNEL_CREATE | shm::posix::POSIX_CHANNEL_CLEAN);
}

MEMAcceptor::~MEMAcceptor() {
}

msg::ServerCtrlCommands MEMAcceptor::Accept() {
  auto recv_queue = accept_ctrl_channel_->GetQueue();
  while (true) {
    msg::ServerCtrlCommands cm;
    auto status = recv_queue->queue.try_pop(cm);
    if (!status) {
      continue;
    }
    return cm;
  }
}

} // namespace mem

} // namespace grpc_core
