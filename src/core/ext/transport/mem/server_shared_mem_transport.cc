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

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "shm/posix_channel.h"
#include "shm/posix_shm_area.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/inproc/legacy_inproc_transport.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/ext/transport/mem/server_shared_mem_transport.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"

namespace grpc_core {
namespace mem {

auto MEMServerTansport::ReadLoop() {
  return Seq(got_acceptor_.Wait(), Loop([this]() {
               return TrySeq(
                   incoming_msg_.Next(),
                   [](msg::ClientMsg msg) {
                     std::cout << msg.header.connection_id << std::endl;
                   },
                   []() -> LoopCtl<absl::Status> { return absl::OkStatus(); });
             }));
}

MEMServerTansport::MEMServerTansport(
    const ChannelArgs &args,
    std::shared_ptr<MEMClientDataConnection> data_connection)
    : incoming_msg_(4),
      call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
          args.GetObject<ResourceQuota>()
              ->memory_quota()
              ->CreateMemoryAllocator("chaotic-good"),
          1024)),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      data_connection_(data_connection) {
  auto party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine_.get());
  party_ = Party::Make(std::move(party_arena));
  party_->Spawn("server-mem-reader", ReadLoop(),
                OnTransportActivityDone("reader"));
}

auto MEMServerTansport::WriteLoop() {}

void MEMServerTansport::PerformOp(grpc_transport_op *op) {}

void MEMServerTansport::Orphan() {}

void MEMServerTansport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {
  CHECK(call_destination_ == nullptr);
  CHECK(call_destination != nullptr);
  call_destination_ = call_destination;
  got_acceptor_.Set();
}

void MEMServerTansport::AbortWithError() {}

} // namespace mem
} // namespace grpc_core
