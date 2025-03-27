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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_SHARED_MEM_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_SHARED_MEM_TRANSPORT_H

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <memory>

#include "shm/posix_channel.h"
#include "shm/posix_shm_area.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_client_data_connection.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace mem {

class MEMServerTansport final : public ServerTransport {

public:
  MEMServerTansport(const ChannelArgs &args,
                    std::shared_ptr<MEMClientDataConnection> data_connection);

  FilterStackTransport *filter_stack_transport() override { return nullptr; }
  ClientTransport *client_transport() override { return nullptr; }
  ServerTransport *server_transport() override { return this; }
  absl::string_view GetTransportName() const override { return "mem_server"; }
  void SetPollset(grpc_stream *, grpc_pollset *) override {}
  void SetPollsetSet(grpc_stream *, grpc_pollset_set *) override {}
  void PerformOp(grpc_transport_op *) override;
  void Orphan() override;

  auto ReadLoop();
  auto WriteLoop();

  void SetCallDestination(
      RefCountedPtr<UnstartedCallDestination> call_destination) override;
  void AbortWithError();

  MpscSender<msg::ClientMsg> GetSender() { return incoming_msg_.MakeSender(); }

  auto OnTransportActivityDone(absl::string_view activity) {
    return [self = RefAsSubclass<MEMServerTansport>(),
            activity](absl::Status status) {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << "MEM: OnTransportActivityDone: activity=" << activity
          << " status=" << status;
      self->AbortWithError();
    };
  }

private:
  std::shared_ptr<MEMClientDataConnection> data_connection_;
  RefCountedPtr<UnstartedCallDestination> call_destination_;
  const RefCountedPtr<CallArenaAllocator> call_arena_allocator_;
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  MpscReceiver<msg::ClientMsg> incoming_msg_;
  InterActivityLatch<void> got_acceptor_;
  RefCountedPtr<Party> party_;
};

} // namespace mem

} // namespace grpc_core

#endif // GRPC_SRC_CORE_EXT_TRANSPORT_SHARED_MEM_TRANSPORT_H
