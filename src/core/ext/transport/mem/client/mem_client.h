#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_MEM_CLIENT_CHAOTIC_GOOD_CONNECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_MEM_CLIENT_CHAOTIC_GOOD_CONNECTOR_H

#include <iostream>
#include <memory>

#include "shm/posix_channel.h"
#include "src/core/client_channel/client_channel_factory.h"
#include "src/core/client_channel/connector.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace mem {

class MemConnector final : public SubchannelConnector {
  void Connect(const Args &args, Result *result, grpc_closure *notify) override;

  void Shutdown(grpc_error_handle error) override;
  private:

  std::shared_ptr<shm::posix::POSIXChannel<msg::ClientCtrlCommands, 128, 1>> srv_connection_;
};

class MemChannelFactory final : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& args) override {
    return Subchannel::Create(MakeOrphanable<MemConnector>(), address,
                              args);
  }
};

} // namespace mem
} // namespace grpc_core

grpc_channel *grpc_mem_client_channel_create(const char *target,
                                      const grpc_channel_args *args);

#endif // GRPC_SRC_CORE_EXT_TRANSPORT_MEM_CLIENT_CHAOTIC_GOOD_CONNECTOR_H
