#include <absl/time/civil_time.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "shm/posix_channel.h"
#include "src/core/ext/transport/mem/allocator/allocator.h"
#include "src/core/ext/transport/mem/client_shared_mem_transport.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_acceptor.h"
#include "src/core/ext/transport/mem/mem_client_data_connection.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/ext/transport/mem/server/mem_server.h"
#include "src/core/ext/transport/mem/server_data_connection_pool.h"
#include "src/core/ext/transport/mem/server_shared_mem_transport.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace mem {

MemServerListener::MemServerListener(Server *server, const ChannelArgs &args,
                                     const char *addr)
    : server_(server), args_(args),
      executor_(grpc_event_engine::experimental::MakeThreadPool(
          grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()) {
  acceptor_ = std::make_unique<MEMAcceptor>(std::string(addr));

  data_connections_ = std::make_unique<MEMServerDataConnectionPool>(
      executor_, [this](msg::ClientMsg cm) { return OnClientMsg(cm); }, 1);
  data_connections_->StartDataConnectionPool();

  response_pool_name_ = "server_response_pool";
  response_pool_ = std::make_shared<shm::posix::POSIXSharedMemory<char *>>(
      response_pool_name_, 500 * MB);
  response_pool_->AttachSHM();

  allocator::init_allocator(response_pool_->GetSHMAddr(), 500 * MB);
}

MemServerListener::~MemServerListener() {
  std::cout << "server destuctor" << std::endl;
}

void MemServerListener::Start() {
  std::cout << "server start?" << std::endl;
  executor_->Run([this]() {
    while (true) {
      auto command_ctrl = acceptor_->Accept();
      if (command_ctrl.command_op & msg::CTRL_REQUEST_CON) {
        std::cout << command_ctrl.command.req_connection.response_channel_name
                  << std::endl;
        CHECK_EQ(
            connection_list_.insert(connection_id_cnt_,
                                    MakeRefCounted<ActiveConnection>(
                                        RefAsSubclass<MemServerListener>(),
                                        command_ctrl.command.req_connection)),
            true);
        connection_list_.find_fn(
            connection_id_cnt_,
            [](const RefCountedPtr<ActiveConnection> connection) {
              connection->establishConnection();
            });
      }
    }
  });
}

void MemServerListener::OnClientMsg(msg::ClientMsg cm) {
  CHECK_EQ(connection_list_.find_fn(
               cm.header.connection_id,
               [msg = cm](const RefCountedPtr<ActiveConnection> connection) {
                 connection->ProcessClientRequest(msg);
               }),
           true);
}

void MemServerListener::Orphan() { std::cout << "server orphan" << std::endl; }

MemServerListener::ActiveConnection::ActiveConnection(
    RefCountedPtr<MemServerListener> listener,
    const msg::ClientRequestConnection &cr)
    : listener_(listener), cr_(cr) {}

MemServerListener::ActiveConnection::~ActiveConnection() {}

void MemServerListener::ActiveConnection::ProcessCommandRequest(
    const msg::ServerCtrlCommands &sc) {}

void MemServerListener::ActiveConnection::ProcessClientRequest(
    const msg::ClientMsg &cm) {
  /*std::cout << "Sending the client message to the trasnport"*/
            /*<< cm.header.connection_id << " 0x" << std::hex*/
            /*<< static_cast<int>(cm.header.msg_type) << std::endl;*/
  //listener_->event_engine_->Run([&]() {
    //std::cout << "Using sender to send message" << std::endl;
    auto status = sender_->Send(cm);
    Poll<bool> send_result = status();
    if (send_result.pending()) {
      // Handle pending state
      // TODO(keyvan): handle this;
      //std::cout << "Oh fuck" << std::endl;
    } else if (send_result.ready()) {
    /*  std::cout << "Sender has finished sending the message "*/
                /*<< send_result.value() << std::endl;*/
    }

    std::cout << "ProcessClientRequest " << cm.header.Diff() << std::endl;
  //});
}

void MemServerListener::ActiveConnection::establishConnection() {
  data_connection_ =
      std::make_shared<MEMClientDataConnection>(MEMClientDataConnection(cr_, listener_->response_pool_->GetSHMAddr()));

  // TODO(keyvan): We should set channel name as well pool memoryname
  msg::ServerAcceptConnection sac;
  memset(sac.request_channel_name, '\0', msg::maxChannelName);
  memset(sac.response_memory_pool_name, '\0', msg::maxSHMRegionName);

  auto channel_name = listener_->data_connections_->GetChannel();
  memcpy(sac.request_channel_name, channel_name.c_str(), channel_name.size());
  memcpy(sac.response_memory_pool_name, listener_->response_pool_name_.c_str(),
         listener_->response_pool_name_.size());
  sac.response_memory_pool_size = 500 * MB;
  sac.connection_id = listener_->connection_id_cnt_++;

  std::cout << "Establishing connection..." << std::endl;
  msg::ClientCtrlCommands ccc;
  ccc.command_op = msg::CTRL_ACCEPT_CON;
  ccc.command.accept_connection_info = sac;
  data_connection_->WriteCommand(ccc);
  std::cout << "Establishing connection done" << std::endl;

  auto transport = new MEMServerTansport(args(), std::move(data_connection_), sac.connection_id);
  sender_ = std::make_shared<MpscSender<msg::ClientMsg>>(
      std::move(transport->GetSender()));
  auto err = listener_->server_->SetupTransport(transport, nullptr,
                                                listener_->args(), nullptr);

  if (!err.ok()) {
    // TODO(keyvan) handle error
    std::cout << "fuck" << std::endl;
  }
}

void MemServerListener::ActiveConnection::Orphan() {
  std::cout << "active connection" << std::endl;
}

} // namespace mem
} // namespace grpc_core

int grpc_server_add_mem_port(grpc_server *server, const char *addr) {
  auto *const core_server = grpc_core::Server::FromC(server);
  auto listener = grpc_core::MakeOrphanable<grpc_core::mem::MemServerListener>(
      grpc_core::Server::FromC(server), core_server->channel_args(), addr);
  core_server->AddListener(std::move(listener));

  return 12000;
}
