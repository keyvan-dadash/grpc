
#include "src/core/ext/transport/mem/client/mem_client.h"
#include <memory>
#include "shm/posix_channel.h"
#include "src/core/ext/transport/mem/client_shared_mem_transport.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/util/orphanable.h"

namespace grpc_core {
namespace mem {

void MemConnector::Connect(const Args &args, Result *result,
                           grpc_closure *notify) {

  std::cout << "helllo" << std::endl;
  /*srv_connection_ = std::make_shared<shm::posix::POSIXChannel<CtrlCommand, 128, 1>>(args.address, shm::posix::POSIX_CHANNEL_EXC);*/

  /*// TODO(keyvan): handle exceptions*/
  /*auto shm_queue = srv_connection_->GetQueue();*/

  /*CtrlCommand cm;*/
  /*cm.command_op = CTRL_REQUEST_CHANNEL;*/
  /*cm.command.response_channel_name;*/
  /*shm_queue->queue.push(cm);*/
}

void MemConnector::Shutdown(grpc_error_handle error) {
  std::cout << "buy" << std::endl;
}

} // namespace mem
} // namespace grpc_core

grpc_channel *grpc_mem_client_channel_create(const char *target,
                                             const grpc_channel_args *args) {
  GRPC_TRACE_LOG(api, INFO) << "grpc_mem_channel_create(target=" << target
                            << ",  args=" << (void *)args << ")";
  grpc_channel *channel = nullptr;
  grpc_error_handle error;

  auto t = grpc_core::MakeOrphanable<grpc_core::mem::MemClientTransport>(target);

  std::cout << target << std::endl;
  auto r = grpc_core::ChannelCreate(
      target,
      grpc_core::CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(args)
          .SetObject(grpc_core::NoDestructSingleton<
                     grpc_core::mem::MemChannelFactory>::Get())
          .Set(GRPC_ARG_USE_V3_STACK, true)
          .Set(GRPC_ARG_DEFAULT_AUTHORITY, "mem.authority"),
      GRPC_CLIENT_DIRECT_CHANNEL, t.release());

  if (r.ok()) {
    return r->release()->c_ptr();
  }

  LOG(ERROR) << "Failed to create mem client channel: " << r.status();
  error = absl_status_to_grpc_error(r.status());
  intptr_t integer;
  grpc_status_code status = GRPC_STATUS_INTERNAL;
  if (grpc_error_get_int(error, grpc_core::StatusIntProperty::kRpcStatus,
                         &integer)) {
    status = static_cast<grpc_status_code>(integer);
  }
  channel = grpc_lame_client_channel_create(
      target, status, "Failed to create mem client channel");
  return channel;
}
