#ifndef CLIENT_CONNECTION_H
#define CLIENT_CONNECTION_H

#include "shm/posix_channel.h"
#include "shm/posix_shm_area.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include <cstdint>
#include <memory>
namespace grpc_core {
namespace mem {

class MEMClientDataConnection {
public:
  explicit MEMClientDataConnection(msg::ClientRequestConnection req_connection, char* response_pool);

  ~MEMClientDataConnection();

  void WriteCommand(msg::ClientCtrlCommands cm);

  void WriteResponse(msg::ServerMsg srv_msg);

  char* GetOriginOfRequestMemory() {
    return client_request_memory_area_->GetSHMAddr();
  }

  char* GetOriginOfResponsePoolMemory() {
    return response_pool_;
  }

private:
  msg::ClientRequestConnection connection_info_;
  std::shared_ptr<shm::posix::POSIXChannel<msg::ClientCtrlCommands, 16, 1>> client_ctrl_channel_;
  std::shared_ptr<shm::posix::POSIXChannel<msg::ServerMsg, 128, 1>> client_response_channel_;
  std::shared_ptr<shm::posix::POSIXSharedMemory<char*>> client_request_memory_area_;
  char* response_pool_;
};

} // namespace mem

} // namespace grpc_core

#endif
