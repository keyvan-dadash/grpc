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
#include <cstdint>
#include <memory>
#include <optional>

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
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/switch.h"
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

auto MEMServerTansport::sendRequestOrWait(msg::ServerMsg msg) {
  // TODO(keyvan): we have to also implement the logic of send loop for efficent
  // send
  return TrySeq([this, msg = msg]() mutable {
    data_connection_->WriteResponse(msg);
    std::cout << "sendRequestOrWait " << msg.header.Diff() << std::endl;
    return absl::OkStatus();
  });
}

auto MEMServerTansport::SendCallBody(int64_t start, uint32_t req_id,
                                     CallInitiator call_initiator) {
  return ForEach(MessagesFrom(call_initiator),
                 [this, req_id, call_initiator, start](MessageHandle message) mutable {
                   msg::ServerMsg sm;
                   sm.header.issued_time = start;
                   //std::cout << "SendCallBody(1) " << sm.header.Diff() << std::endl;
                   sm.header.response_of_request_id = req_id;
                   sm.header.connection_id = connection_id_;
                   sm.header.msg_type = msg::MsgType::kMessage;

                   types::String str;
                   str.str = (char *)allocator_->alloc->alloc(
                       message->payload()->Length());
                   str.str_size = message->payload()->Length();

                   // bottleneck
                   message->payload()->CopyToBuffer((uint8_t *)(str.str));
                   str.CalcualteOffssetFrom(
                       data_connection_->GetOriginOfResponsePoolMemory());
                   sm.response_data.msg = msg::MemServerMsg(str);

                   //std::cout << "SendCallBody(2) " << sm.header.Diff() << std::endl;

                   return sendRequestOrWait(sm);
                 });
}

auto MEMServerTansport::SendCallInitialMetadataAndBody(
    int64_t start, uint32_t req_id, CallInitiator call_initiator) {
  return TrySeq(call_initiator.PullServerInitialMetadata(),
                [req_id, call_initiator, start,
                 this](std::optional<ServerMetadataHandle> md) mutable {
                  /*std::cout << "Does initial metadata have anything? "*/
                            /*<< md.has_value() << std::endl;*/
                  return If(
                      md.has_value(),
                      [&md, req_id, start, &call_initiator, this]() {

                        msg::ServerMsg sm;
                        sm.header.issued_time = start;
                        //std::cout << "SendCallInitial(1)... " << sm.header.Diff() << std::endl;
                        auto memMetadata =
                            msg::CreateServerMetadataFromGrpc(**md, allocator_);
                        memMetadata->CalcualteOffssetFrom(
                            data_connection_->GetOriginOfResponsePoolMemory());
                        sm.header.response_of_request_id = req_id;
                        sm.header.connection_id = connection_id_;
                        sm.header.msg_type =
                            msg::MsgType::kServerInitialMetadata;

                        // TODO(keyvan): we have to track this allocation later.
                        sm.response_data.msm = *memMetadata;
                        //std::cout << "SendCallInitial(2)... " << sm.header.Diff() << std::endl;

                        return TrySeq(sendRequestOrWait(sm),
                                      SendCallBody(sm.header.issued_time, req_id, call_initiator));
                      },
                      []() { return absl::OkStatus(); });
                });
}

auto MEMServerTansport::CallOutboundLoop(int64_t start, uint32_t req_id,
                                         CallInitiator call_initiator) {
  return Seq(SendCallInitialMetadataAndBody(start, req_id, call_initiator),
             call_initiator.PullServerTrailingMetadata(),
             [this, req_id, start](ServerMetadataHandle md) mutable {
               msg::ServerMsg sm;
               sm.header.issued_time = start;
               auto memMetadata =
                   msg::CreateServerMetadataFromGrpc(*md, allocator_);
               memMetadata->CalcualteOffssetFrom(
                   data_connection_->GetOriginOfResponsePoolMemory());
               sm.header.response_of_request_id = req_id;
               sm.header.connection_id = connection_id_;
               sm.header.msg_type = msg::MsgType::kServerTrailingMetadata;

               // TODO(keyvan): we have to track this allocation later.
               sm.response_data.msm = *memMetadata;
               
               //std::cout << "CallOutBound " << sm.header.Diff() << std::endl;

               return sendRequestOrWait(sm);
             });
}


auto MEMServerTansport::ProcessMessageData(msg::ServerMsg msg) {

}

auto MEMServerTansport::ReadLoop() {
  return Seq(
      got_acceptor_.Wait(), Loop([this]() mutable {
        return TrySeq(
            incoming_msg_.Next(),
            [this](msg::ClientMsg msg) {
              return Switch(
                  msg.header.msg_type,
                  Case<msg::MsgType::kClientInitialMetadata>([&, this]() {

                    /*std::cout << "We are going to start the call on the call "*/
                                 /*"destination in the trasnport"*/
                              /*<< std::endl;*/
                    msg.request_data.mcm.ApplyOffsetFrom(
                        data_connection_->GetOriginOfRequestMemory());
                    auto md = msg::CreateClientMetadataFromMemMetadata(
                        msg.request_data.mcm);
                    if (!md.ok()) {
                      return md.status();
                    }

                    /*std::cout << "The path is "*/
                              /*<< msg.request_data.mcm.path.GetString()*/
                              /*<< " authority is "*/
                              /*<< msg.request_data.mcm.authority.GetString()*/
                              /*<< std::endl;*/

                    RefCountedPtr<Arena> arena(
                        call_arena_allocator_->MakeArena());
                    arena->SetContext<
                        grpc_event_engine::experimental::EventEngine>(
                        event_engine_.get());
                    std::optional<CallInitiator> call_initiator;
                    auto call = MakeCallPair(std::move(*md), std::move(arena));
                    call_initiator.emplace(std::move(call.initiator));
                    const auto request_id = msg.header.request_id;
                    auto add_result =
                        AddNewRequestInfo(request_id, *call_initiator);
                    if (!add_result.ok()) {
                      call_initiator.reset();
                      return add_result;
                    }

                    call_initiator->SpawnGuarded(
                        "server-write",
                        [this, request_id, call_initiator = *call_initiator,
                         call_handler = std::move(call.handler), msg = std::move(msg)]() mutable {
                          call_destination_->StartCall(std::move(call_handler));
                          //std::cout << "Call has been started" << std::endl;

                          //std::cout << "StartCall " << msg.header.Diff()  << std::endl;
                          return CallOutboundLoop(msg.header.issued_time, request_id, call_initiator);
                        });
                    //std::cout << "Client Intial metadata " << msg.header.Diff()  << std::endl;

                    return absl::OkStatus();
                  }),
                  Case<msg::MsgType::kMessage>([&, this]() mutable {
                    auto request_info = GetRequestInfo(msg.header.request_id);
                    //std::cout << "Message and isValid? " << (request_info != nullptr) << std::endl;
                    return If(
                        request_info != nullptr,
                        [this, &request_info, &msg]() {
                          return request_info->call.SpawnWaitable("push-message", [this, request_info, msg = std::move(msg)]() mutable {
                              //std::cout << "We are starting to push message" << std::endl;
                              auto& call = request_info->call;
                              //std::cout << "Lets apply offset" << std::endl;
                              msg.request_data.mcmsg.ApplyOffsetFrom(data_connection_->GetOriginOfRequestMemory());
                              return call.UntilCallCompletes(Map(call.CancelIfFails(
                                            TrySeq(call.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromExternalString(msg.request_data.mcmsg.msg.GetStringView())), 0))
                                            )), [msg = std::move(msg)](auto) mutable { 
                                  //std::cout << "Client Message " << msg.header.Diff()  << std::endl;
                                  return absl::OkStatus(); })
                              );
                          });
                        },
                        []() { return absl::OkStatus(); }
                    );
                  }),
                  Case<msg::MsgType::kClientEndOfStream>([&, this]() mutable {
                    auto request_info = GetRequestInfo(msg.header.request_id);
                    //std::cout << "End of client stream and isValid? " << (request_info != nullptr) << std::endl;
                    return If(
                        request_info != nullptr,
                        [this, &request_info, &msg]() {
                          return request_info->call.SpawnWaitable("eof-message", [this, request_info, msg = std::move(msg)]() mutable {
                              //std::cout << "We are starting to push message (eof)" << std::endl;
                              auto& call = request_info->call;
                              return call.UntilCallCompletes(Map(call.CancelIfFails(
                                            TrySeq([this, request_info = std::move(request_info), msg = std::move(msg)]() mutable {
                                              //std::cout << "We are going to end call " << std::endl;
                                              request_info->call.FinishSends();
                                              std::cout << "Client Eof " << msg.header.Diff()  << std::endl;
                                              return StatusFlag{Success{}};
                                            })), [](auto) { /*std::cout << "Fuck3" << std::endl;*/  return absl::OkStatus(); })
                              );
                          });
                        },
                        []() { return absl::OkStatus(); }
                    );
                  }),
                  Case<msg::MsgType::kCancel>([&, this]() {
                    //std::cout << "Cancel" << std::endl;
                    return absl::OkStatus();
                  }),
                  Default([&]() {
                    //std::cout << "fuck" << std::endl;
                    return absl::OkStatus();
                  }));
            },
            []() -> LoopCtl<absl::Status> { return Continue{}; });
      }));
}

absl::Status
MEMServerTansport::AddNewRequestInfo(uint32_t request_id,
                                     CallInitiator call_initiator) {
  MutexLock lock(&mu_);
  auto is_exist = request_info_map_.find(request_id);
  if (is_exist != request_info_map_.end()) {
    return absl::InternalError("Stream already exists");
  }

  const bool on_done_added = call_initiator.OnDone(
      [self = RefAsSubclass<MEMServerTansport>(), request_id](bool) {
        //std::cout << "Something happent?" << std::endl;
        auto request_info = self->ExtractRequestInfo(request_id);
        if (request_info != nullptr) {
          auto& call = request_info->call;
          call.SpawnInfallible(
                  "cancel", [request_info = std::move(request_info)]() mutable {
                    request_info->call.Cancel();
                  });

        }
        /*self->request_info_map_.erase_fn(*/
            /*request_id, [](RefCountedPtr<RequestInfo> request_info) {*/
              /*auto &call = request_info->call;*/
              /*call.SpawnInfallible(*/
                  /*"cancel", [request_info = std::move(request_info)]() mutable {*/
                    /*request_info->call.Cancel();*/
                  /*});*/
              /*return true;*/
            /*});*/
      });
  if (!on_done_added) {
    return absl::CancelledError();
  }

  request_info_map_.emplace(
      request_id, MakeRefCounted<RequestInfo>(std::move(call_initiator)));
  return absl::OkStatus();
}


RefCountedPtr<MEMServerTansport::RequestInfo> MEMServerTansport::GetRequestInfo(uint32_t request_id) {
  /*RefCountedPtr<MEMServerTansport::RequestInfo> out;*/
  /*request_info_map_.find(request_id, out);*/
  /*return std::move(out);*/
  MutexLock lock(&mu_);
  auto it = request_info_map_.find(request_id);
  if (it == request_info_map_.end()) return nullptr;
  return it->second;
}


RefCountedPtr<MEMServerTansport::RequestInfo> MEMServerTansport::ExtractRequestInfo(uint32_t request_id) {
  MutexLock lock(&mu_);
  auto it = request_info_map_.find(request_id);
  if (it == request_info_map_.end()) return nullptr;
  auto r = std::move(it->second);
  request_info_map_.erase(it);
  return r;
}

MEMServerTansport::MEMServerTansport(
    const ChannelArgs &args,
    std::shared_ptr<MEMClientDataConnection> data_connection, int connection_id)
    : incoming_msg_(4),
      call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
          args.GetObject<ResourceQuota>()
              ->memory_quota()
              ->CreateMemoryAllocator("chaotic-good"),
          1024)),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      data_connection_(data_connection), connection_id_(connection_id) {
  auto party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine_.get());
  party_ = Party::Make(std::move(party_arena));
  party_->Spawn("server-mem-reader", ReadLoop(),
                OnTransportActivityDone("reader"));
  allocator_ =
      std::make_shared<snmalloc::ScopedAllocator<allocator::FixedAlloc>>();
}

auto MEMServerTansport::WriteLoop() {}

void MEMServerTansport::PerformOp(grpc_transport_op *op) {}

void MEMServerTansport::Orphan() {}

void MEMServerTansport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {
  //std::cout << "Call destination has been setup up" << std::endl;
  CHECK(call_destination_ == nullptr);
  CHECK(call_destination != nullptr);
  call_destination_ = call_destination;
  got_acceptor_.Set();
}

void MEMServerTansport::AbortWithError() {}

} // namespace mem
} // namespace grpc_core
