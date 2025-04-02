// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CLIENT_SHARED_MEM_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CLIENT_SHARED_MEM_TRANSPORT_H

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <memory>

#include "shm/posix_channel.h"
#include "shm/posix_shm_area.h"
#include "src/core/ext/transport/mem/allocator/allocator.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace mem {

class MemClientTransport final : public ClientTransport {
private:
  struct Request : RefCounted<Request> {
    explicit Request(CallHandler call_handler)
        : call(std::move(call_handler)) {}
    CallHandler call;
  };
  using RequestMap = absl::flat_hash_map<uint32_t, RefCountedPtr<Request>>;

public:
  explicit MemClientTransport(std::string server_addr);
  ~MemClientTransport() override{};

  FilterStackTransport *filter_stack_transport() override { return nullptr; }
  ClientTransport *client_transport() override { return this; }
  ServerTransport *server_transport() override { return nullptr; }
  absl::string_view GetTransportName() const override {
    return "mem_transport";
  }
  void SetPollset(grpc_stream *, grpc_pollset *) override {}
  void SetPollsetSet(grpc_stream *, grpc_pollset_set *) override {}
  void PerformOp(grpc_transport_op *) override;
  void Orphan() override;

  auto LoopServerMessageProcess();
  void LoopRead();

  RefCountedPtr<Request> GetRequest(uint32_t request_id);
  int StoreRequestCall(CallHandler call_handler);

  auto CallOutboundLoop(int request_id, CallHandler call_handler);
  void StartCall(CallHandler call_handler) override;

private:
  bool waitForDataConnection();
  auto sendRequestOrWait(msg::ClientMsg msg);

  auto OnTransportActivityDone(absl::string_view activity) {
    return [self = RefAsSubclass<MemClientTransport>(),
            activity](absl::Status status) {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << "MEM: OnTransportActivityDone: activity=" << activity
          << " status=" << status;
    };
  }

  std::shared_ptr<grpc_event_engine::experimental::ThreadPool> executor_;
  Mutex mu_;
  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(mu_){
      "mem_client_transport", GRPC_CHANNEL_READY};
  std::string srv_addr_;
  std::string cm_channel_name_;
  std::string response_channel_name_;
  std::string request_pool_name_;

  // Channels
  std::shared_ptr<shm::posix::POSIXChannel<msg::ServerCtrlCommands, 128, 1>>
      srv_connection_;
  std::shared_ptr<shm::posix::POSIXChannel<msg::ClientCtrlCommands, 16, 1>>
      cm_channel_;
  std::shared_ptr<shm::posix::POSIXChannel<msg::ServerMsg, 128, 1>>
      response_channel_;

  // Pools
  std::shared_ptr<shm::posix::POSIXSharedMemory<char *>> request_pool_;
  std::shared_ptr<shm::posix::POSIXSharedMemory<char *>> response_pool_;

  std::shared_ptr<shm::posix::POSIXChannel<msg::ClientMsg, 1024, 1>>
      request_channel_;
  int req_id_ = 1;
  int connection_id_;
  RequestMap request_map_ ABSL_GUARDED_BY(mu_);
  allocator::SharedPtrAllocator allocator_;
  std::map<int, msg::MemClientMetadata *> unfinished_req_;
  RefCountedPtr<Party> party_;
  MpscReceiver<msg::ServerMsg> incoming_msg_;
};
} // namespace mem
} // namespace grpc_core

#endif // GRPC_SRC_CORE_EXT_TRANSPORT_CLIENT_SHARED_MEM_TRANSPORT_H
