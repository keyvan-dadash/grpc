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

#include "src/core/ext/transport/mem/client_shared_mem_transport.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "shm/posix_channel.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/inproc/legacy_inproc_transport.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace mem {

MemClientTransport::MemClientTransport(std::string server_addr)
    : srv_addr_(server_addr) {
  srv_connection_ = std::make_shared<
      shm::posix::POSIXChannel<msg::ServerCtrlCommands, 128, 1>>(
      server_addr, shm::posix::POSIX_CHANNEL_EXC);

  // TODO(keyvan): handle exceptions
  auto shm_queue = srv_connection_->GetQueue();
  cm_channel_name_ = std::string("test_client");
  response_channel_name_ = std::string("test_client_response");

  cm_channel_ = std::make_shared<
      shm::posix::POSIXChannel<msg::ClientCtrlCommands, 16, 1>>(
      cm_channel_name_,
      shm::posix::POSIX_CHANNEL_CREATE | shm::posix::POSIX_CHANNEL_CLEAN);
  response_channel_ =
      std::make_shared<shm::posix::POSIXChannel<msg::ServerMsg, 128, 1>>(
          response_channel_name_,
          shm::posix::POSIX_CHANNEL_CREATE | shm::posix::POSIX_CHANNEL_CLEAN);

  msg::ServerCtrlCommands scc;
  scc.command_op = msg::CTRL_REQUEST_CON;

  memset(scc.command.req_connection.ctrl_command_channel_name, '\0',
         msg::maxChannelName);
  memcpy(scc.command.req_connection.ctrl_command_channel_name,
         cm_channel_name_.c_str(), cm_channel_name_.size());

  memset(scc.command.req_connection.response_channel_name, '\0',
         msg::maxChannelName);
  memcpy(scc.command.req_connection.response_channel_name,
         response_channel_name_.c_str(), response_channel_name_.size());

  shm_queue->queue.push(scc);

  // Now we should wait for acceptance of the connection
  auto gotDataConnection = waitForDataConnection();
  if (!gotDataConnection) {
  }
}

int MemClientTransport::StoreRequestCall(CallHandler call_handler) {
  MutexLock lock(&mu_);
  const int request_id = req_id_++;
  const bool on_done_added = call_handler.OnDone(
      [self = RefAsSubclass<MemClientTransport>(), request_id](bool cancelled) {
        std::cout << "We are done with the call: " << request_id << std::endl;
        if (cancelled) {
          // TODO(keyvan): do something upon cancelled.
        }

        MutexLock lock(&self->mu_);
        self->request_map_.erase(request_id);
      });
  if (!on_done_added) {
    return 0;
  }
  request_map_.emplace(request_id, MakeRefCounted<Request>(call_handler));
  return request_id;
}

void MemClientTransport::Orphan() {}

void MemClientTransport::LoopRead() {
  auto shm_queue = response_channel_->GetQueue();
  while(true) {
    msg::ServerMsg sm;
    auto status = shm_queue->queue.try_pop(sm);
    if (!status) {
      // TODO(keyvan): do something
      continue;
    }
    // TODO(keyvan): do somehting with sm
  }
}

auto MemClientTransport::sendRequestOrWait(msg::ClientMsg &msg) {
  // TODO(keyvan): we have to also implement the logic of send loop for efficent
  // send
  return TrySeq([&]() {
    auto queue = request_channel_->GetQueue();
    while (true) {
      auto status = queue->queue.try_push(msg);
      if (!status) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
      return absl::OkStatus();
    }
  });
}

auto MemClientTransport::CallOutboundLoop(int request_id,
                                          CallHandler call_handler) {
  return TrySeq(
      call_handler.PullClientInitialMetadata(),
      [this](ClientMetadataHandle md) mutable {
        msg::ClientMsg cm;
        return sendRequestOrWait(cm);
      },
      ForEach(MessagesFrom(call_handler),
              [this](MessageHandle message) mutable {
                msg::ClientMsg cm;
                return sendRequestOrWait(cm);
              }),
      [this]() mutable {
        // Finished sending message
        msg::ClientMsg cm;
        return sendRequestOrWait(cm);
      },
      [call_handler]() mutable {
        return Map(call_handler.WasCancelled(), [](bool cancelled) {
          if (cancelled) {
            return absl::CancelledError();
          }
          return absl::OkStatus();
        });
      });
}

void MemClientTransport::StartCall(CallHandler call_handler) {
  call_handler.SpawnGuarded(
      "outbound_loop",
      [self = RefAsSubclass<MemClientTransport>(), call_handler]() mutable {
        int request_id = self->StoreRequestCall(call_handler);
        return If(request_id != 0,
                  Map(self->CallOutboundLoop(request_id, call_handler),
                      [](absl::Status result) {
                        std::cout << "Finished the call with result: "
                                  << result.ToString() << std::endl;
                        if (!result.ok()) {
                          std::cout << "We are fucked" << std::endl;
                        }
                        return result;
                      }),
                  []() { return absl::OkStatus(); });
      });
}

void MemClientTransport::PerformOp(grpc_transport_op *op) {
  std::cout << "per" << std::endl;
}

bool MemClientTransport::waitForDataConnection() {
  auto queue = cm_channel_->GetQueue();
  msg::ClientCtrlCommands ccc;
  while (true) {
    auto status = queue->queue.try_pop(ccc);
    if (!status) {
      continue;
    }
  }

  request_channel_ =
      std::make_shared<shm::posix::POSIXChannel<msg::ClientMsg, 1024, 1>>(
          ccc.command.accept_connection_info.request_channel_name,
          shm::posix::POSIX_CHANNEL_EXC);

  return true;
}

} // namespace mem
} // namespace grpc_core
