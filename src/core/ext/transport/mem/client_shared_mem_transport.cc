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
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "grpc/event_engine/event_engine.h"
#include "shm/posix_channel.h"
#include "shm/posix_shm_area.h"
#include "snmalloc/global/scopedalloc.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/inproc/legacy_inproc_transport.h"
#include "src/core/ext/transport/mem/allocator/allocator.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/ext/transport/mem/types.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/switch.h"
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


auto MemClientTransport::LoopServerMessageProcess() {
  return Seq(Loop([this]() {
    return TrySeq(
        incoming_msg_.Next(),
        [this](msg::ServerMsg msg) {
          return Switch(msg.header.msg_type,
              Case<msg::MsgType::kServerInitialMetadata>([&, this]() {
                    auto request_info = GetRequest(msg.header.response_of_request_id);
                    //std::cout << "Server Intital Metadata and isValid? " << (request_info != nullptr) << std::endl;
                    return If(
                        request_info != nullptr,
                        [this, &request_info, &msg]() {
                          return request_info->call.SpawnWaitable("push-server-initial", [this, request_info, msg = std::move(msg)]() mutable {
                              //std::cout << "We are starting to push server intital metadata" << std::endl;
                              auto& call = request_info->call;
                              //std::cout << "Lets apply offset" << std::endl;
                              msg.response_data.msm.ApplyOffsetFrom(response_pool_->GetSHMAddr());
                              return Map(call.CancelIfFails(
                                            TrySeq([this, request_info = std::move(request_info), msg = std::move(msg)]() mutable {
                                              std::cout << msg.response_data.msm.msg.str_size << std::endl;
                                              auto md = msg::CreateServerMetadataFromMemMetadata(msg.response_data.msm);
                                              if (!md.ok()) {
                                                //std::cout << "We are fucked because the server initial metadata couldnt be parsed" << std::endl;
                                                return StatusFlag{Failure{}};
                                              }

                                              return request_info->call.PushServerInitialMetadata(std::move(*md));
                                            })), [](auto) { return absl::OkStatus(); });
                          });
                        },
                        []() { return absl::OkStatus(); }
                    );
              }),
              Case<msg::MsgType::kMessage>([&, this]() {
                    auto request_info = GetRequest(msg.header.response_of_request_id);
                    //std::cout << "Server Message and isValid? " << (request_info != nullptr) << std::endl;
                    return If(
                        request_info != nullptr,
                        [this, &request_info, &msg]() {
                          return request_info->call.SpawnWaitable("push-server-message", [this, request_info, msg = std::move(msg)]() mutable {
                              //std::cout << "We are starting to push server message" << std::endl;
                              auto& call = request_info->call;
                              //std::cout << "Lets apply offset" << std::endl;
                              msg.response_data.msg.ApplyOffsetFrom(response_pool_->GetSHMAddr());
                              return Map(call.CancelIfFails(
                                            TrySeq(call.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromExternalString(msg.response_data.msg.msg.GetStringView())), 0))
                                            )), [](auto) { return absl::OkStatus(); });
                          });
                        },
                        []() { return absl::OkStatus(); }
                    );

              }),
              Case<msg::MsgType::kServerTrailingMetadata>([&, this]() {
                    auto request_info = GetRequest(msg.header.response_of_request_id);
                    //std::cout << "Server Trailing Metadata and isValid? " << (request_info != nullptr) << std::endl;
                    return If(
                        request_info != nullptr,
                        [this, &request_info, &msg]() {
                          return request_info->call.SpawnWaitable("push-server-trail", [this, request_info, msg = std::move(msg)]() mutable {
                              //std::cout << "We are starting to push server trail metadata" << std::endl;
                              auto& call = request_info->call;
                              //std::cout << "Lets apply offset" << std::endl;
                              msg.response_data.msm.ApplyOffsetFrom(response_pool_->GetSHMAddr());
                              return Map(call.CancelIfFails(
                                            TrySeq([this, request_info = std::move(request_info), msg = std::move(msg)]() {
                                              auto md = msg::CreateServerMetadataFromMemMetadata(msg.response_data.msm);
                                              if (!md.ok()) {
                                                //std::cout << "We are fucked because the server trail metadata couldnt be parsed" << std::endl;
                                                request_info->call.PushServerTrailingMetadata(CancelledServerMetadataFromStatus(md.status()));
                                              } else {
                                                request_info->call.PushServerTrailingMetadata(std::move(*md));
                                              }

                                              return StatusFlag{Success{}};
                                            })), [](auto) { return absl::OkStatus(); });
                          });
                        },
                        []() { return absl::OkStatus(); }
                    );

              }),
              Default([&, this]() {
                  std::cout << "Unkown message?" << std::endl;
                  return absl::OkStatus();
              })
          );
        },
        []() -> LoopCtl<absl::Status> { return Continue{}; });
  }));
}


MemClientTransport::MemClientTransport(std::string server_addr)
    : srv_addr_(server_addr),
      executor_(grpc_event_engine::experimental::MakeThreadPool(
          grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
      incoming_msg_(4) {
  srv_connection_ = std::make_shared<
      shm::posix::POSIXChannel<msg::ServerCtrlCommands, 128, 1>>(
      server_addr, shm::posix::POSIX_CHANNEL_EXC);

  // TODO(keyvan): handle exceptions
  auto shm_queue = srv_connection_->GetQueue();
  cm_channel_name_ = std::string("test_client");
  response_channel_name_ = std::string("test_client_response");
  request_pool_name_ = std::string("test_client_request_pool");

  cm_channel_ = std::make_shared<
      shm::posix::POSIXChannel<msg::ClientCtrlCommands, 16, 1>>(
      cm_channel_name_,
      shm::posix::POSIX_CHANNEL_CREATE | shm::posix::POSIX_CHANNEL_CLEAN);
  response_channel_ =
      std::make_shared<shm::posix::POSIXChannel<msg::ServerMsg, 128, 1>>(
          response_channel_name_,
          shm::posix::POSIX_CHANNEL_CREATE | shm::posix::POSIX_CHANNEL_CLEAN);
  request_pool_ = std::make_shared<shm::posix::POSIXSharedMemory<char *>>(
      request_pool_name_, 200 * MB);
  request_pool_->AttachSHM();

  // Fixed region allocator
  allocator::init_allocator(request_pool_->GetSHMAddr(), 200 * MB);
  allocator_ =
      std::make_shared<snmalloc::ScopedAllocator<allocator::FixedAlloc>>();

  msg::ServerCtrlCommands scc;
  scc.command_op = msg::CTRL_REQUEST_CON;

  // Ctrl Channel
  memset(scc.command.req_connection.ctrl_command_channel_name, '\0',
         msg::maxChannelName);
  memcpy(scc.command.req_connection.ctrl_command_channel_name,
         cm_channel_name_.c_str(), cm_channel_name_.size());

  // Response channel (compeletion queue)
  memset(scc.command.req_connection.response_channel_name, '\0',
         msg::maxChannelName);
  memcpy(scc.command.req_connection.response_channel_name,
         response_channel_name_.c_str(), response_channel_name_.size());

  // Request pool
  memset(scc.command.req_connection.request_memory_pool_name, '\0',
         msg::maxSHMRegionName);
  memcpy(scc.command.req_connection.request_memory_pool_name,
         request_pool_name_.c_str(), request_pool_name_.size());

  scc.command.req_connection.memory_pool_size = 200 * MB;

  shm_queue->queue.push(scc);

  // Now we should wait for acceptance of the connection
  auto gotDataConnection = waitForDataConnection();
  if (!gotDataConnection) {
  }

  executor_->Run([this]() { LoopRead(); });

  auto party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(grpc_event_engine::experimental::GetDefaultEventEngine().get());
  party_ = Party::Make(std::move(party_arena));
  party_->Spawn("client-mem-reader", LoopServerMessageProcess(),
                OnTransportActivityDone("reader"));
}

RefCountedPtr<MemClientTransport::Request> MemClientTransport::GetRequest(uint32_t request_id) {
  MutexLock lock(&mu_);
  auto it = request_map_.find(request_id);
  if (it == request_map_.end()) return nullptr;
  return it->second;
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
  auto sender = incoming_msg_.MakeSender();
  while (true) {
    msg::ServerMsg sm;
    auto status = shm_queue->queue.try_pop(sm);
    if (!status) {
      // TODO(keyvan): do something
      std::this_thread::sleep_for(std::chrono::microseconds(20));
      continue;
    }
    //std::cout << "New request with request id of " << sm.header.response_of_request_id << std::endl;
    auto send_result = sender.Send(sm)();
    if (send_result.pending()) {
      // Handle pending state
      // TODO(keyvan): handle this;
      //std::cout << "Oh fuck" << std::endl;
    } else if (send_result.ready()) {
      /*std::cout << "Sender has finished sending the message "*/
                /*<< send_result.value() << std::endl;*/
    }
  }
}

auto MemClientTransport::sendRequestOrWait(msg::ClientMsg msg) {
  // TODO(keyvan): we have to also implement the logic of send loop for efficent
  // send
  return TrySeq([this, msg = msg]() {
    auto queue = request_channel_->GetQueue();
    while (true) {
      auto status = queue->queue.try_push(msg);
      if (!status) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      } else {
        /*std::cout << msg.header.connection_id << " 0x" << std::hex*/
                  /*<< static_cast<int>(msg.header.msg_type) << std::endl;*/
        break;
      }
    }
    return absl::OkStatus();
  });
}

auto MemClientTransport::CallOutboundLoop(int request_id,
                                          CallHandler call_handler) {
  return TrySeq(
      call_handler.PullClientInitialMetadata(),
      [this, request_id = request_id](ClientMetadataHandle md) mutable {
        //std::cout << "Generating metadata" << std::endl;
        msg::ClientMsg cm;
        auto memMetadata = msg::CreateMetadataFromGrpc(*md, allocator_);
        memMetadata->CalcualteOffssetFrom(request_pool_->GetSHMAddr());
        cm.header.request_id = request_id;
        cm.header.connection_id = connection_id_;
        cm.header.msg_type = msg::MsgType::kClientInitialMetadata;

        cm.request_data.mcm = *memMetadata;
        unfinished_req_[request_id] = memMetadata;

        /*std::cout << "Sending the metadta with connection id: "*/
                  /*<< cm.header.connection_id << std::endl;*/

        return sendRequestOrWait(cm);
      },
      ForEach(MessagesFrom(call_handler),
              [this, request_id = request_id](MessageHandle message) mutable {
                //std::cout << "Sending message" << std::endl;
                msg::ClientMsg cm;
                cm.header.request_id = request_id;
                cm.header.connection_id = connection_id_;
                cm.header.msg_type = msg::MsgType::kMessage;

                types::String str;
                str.str = (char *)allocator_->alloc->alloc(
                    message->payload()->Length());
                str.str_size = message->payload()->Length();
                message->payload()->CopyToBuffer((uint8_t *)(str.str));
                str.CalcualteOffssetFrom(request_pool_->GetSHMAddr());
                cm.request_data.mcmsg = msg::MemClientMsg(str);

                return sendRequestOrWait(cm);
              }),
      [this, request_id = request_id]() mutable {
        //std::cout << "Sending end of stream" << std::endl;
        // Finished sending message
        msg::ClientMsg cm;
        cm.header.request_id = request_id;
        cm.header.connection_id = connection_id_;
        cm.header.msg_type = msg::MsgType::kClientEndOfStream;
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
  //std::cout << "We started the call" << std::endl;
  call_handler.SpawnGuarded(
      "outbound_loop",
      [self = RefAsSubclass<MemClientTransport>(), call_handler]() mutable {
        int request_id = self->StoreRequestCall(call_handler);
        return If(request_id != 0,
                  Map(self->CallOutboundLoop(request_id, call_handler),
                      [](absl::Status result) {
                        /*std::cout << "Finished the call with result: "*/
                                  /*<< result.ToString() << std::endl;*/
                        if (!result.ok()) {
                          //std::cout << "We are fucked" << std::endl;
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
  std::cout << "Going to get info of accepted request of new client"
            << std::endl;
  auto queue = cm_channel_->GetQueue();
  msg::ClientCtrlCommands ccc;
  while (true) {
    auto status = queue->queue.try_pop(ccc);
    if (status) {
      break;
    }
  }
  std::cout << "Get got acceptance!" << std::endl;

  connection_id_ = ccc.command.accept_connection_info.connection_id;
  request_channel_ =
      std::make_shared<shm::posix::POSIXChannel<msg::ClientMsg, 1024, 1>>(
          ccc.command.accept_connection_info.request_channel_name,
          shm::posix::POSIX_CHANNEL_EXC);
  response_pool_ = std::make_shared<shm::posix::POSIXSharedMemory<char *>>(
      ccc.command.accept_connection_info.response_memory_pool_name,
      ccc.command.accept_connection_info.response_memory_pool_size);
  response_pool_->AttachSHM();

  return true;
}

} // namespace mem
} // namespace grpc_core
