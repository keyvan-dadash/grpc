#include "shm/posix_channel.h"
#include "shm/posix_shm_area.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_client_data_connection.h"
#include "src/core/ext/transport/mem/mem_message.h"
namespace grpc_core {
namespace mem {
MEMClientDataConnection::MEMClientDataConnection(
    msg::ClientRequestConnection req_connection)
    : connection_info_(req_connection) {
  client_ctrl_channel_ = std::make_shared<
      shm::posix::POSIXChannel<msg::ClientCtrlCommands, 16, 1>>(
      std::string(connection_info_.ctrl_command_channel_name),
      shm::posix::POSIX_CHANNEL_EXC);

  client_response_channel_ =
      std::make_shared<shm::posix::POSIXChannel<msg::ServerMsg, 128, 1>>(
          std::string(connection_info_.response_channel_name),
          shm::posix::POSIX_CHANNEL_EXC);


  // TODO(keyvan) we have to fix this memory area issue.

  /*client_request_memory_area_ =*/
      /*std::make_shared<shm::posix::POSIXSharedMemory<SharedMemArea>>(*/
          /*std::string(connection_info_.request_memory_pool_name),*/
          /*connection_info_.memory_pool_size);*/
  /*client_request_memory_area_->AttachSHM();*/
}

MEMClientDataConnection::~MEMClientDataConnection() {}

void MEMClientDataConnection::WriteCommand(msg::ClientCtrlCommands cm) {
  auto shm_queue = client_ctrl_channel_->GetQueue();
  shm_queue->queue.push(cm);
}

void MEMClientDataConnection::WriteResponse(msg::ServerMsg srv_msg) {
  auto shm_queue = client_response_channel_->GetQueue();
  shm_queue->queue.push(srv_msg);
}

} // namespace mem
} // namespace grpc_core
