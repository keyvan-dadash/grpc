
#include "src/core/ext/transport/mem/common/commands.h"
#include "src/core/ext/transport/mem/server_data_connection_pool.h"
#include <cstdlib>
#include <memory>
#include <string>

namespace grpc_core {
namespace mem {

MEMServerDataConnectionPool::MEMServerDataConnectionPool(
    std::shared_ptr<grpc_event_engine::experimental::ThreadPool> executor,
absl::AnyInvocable<void(msg::ClientMsg)> on_receive_callback,
    int num_of_channels)
    : executor_(executor), num_data_channels_(num_of_channels), on_receive_callback_(std::move(on_receive_callback)) {
  for (int i = 0; i < num_of_channels; i++) {
    auto data_channel =
        std::make_shared<DataChannel>(DataChannel(gen_random(10)));
    data_channels_.push_back(std::move(data_channel));
  }
}

MEMServerDataConnectionPool::~MEMServerDataConnectionPool() {}

void MEMServerDataConnectionPool::StartDataConnectionPool() {
  for (int i = 0; i < num_data_channels_; i++) {
    executor_->Run([&, data_channel = data_channels_[i]]() {
      while (true) {
      // right now connection list is empty.
        auto msg = data_channel->GetMessage();
        on_receive_callback_(msg);
      }
    });
  }
}

std::string MEMServerDataConnectionPool::GetChannel() {
  // TODO(keyvan): we have to implement a logic here
  return data_channels_[0]->channel_name_;
}


} // namespace mem

} // namespace grpc_core
