#ifndef SERVER_DATA_CONNECTION_POOL_H
#define SERVER_DATA_CONNECTION_POOL_H

#include "shm/posix_channel.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/util/ref_counted_ptr.h"
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace grpc_core {
namespace mem {

class MEMServerDataConnectionPool {
public:
  explicit MEMServerDataConnectionPool(
      std::shared_ptr<grpc_event_engine::experimental::ThreadPool> executor,
      absl::AnyInvocable<void(msg::ClientMsg)> on_receive_callback,
      int num_of_channels);

  ~MEMServerDataConnectionPool();

  void StartDataConnectionPool();

  std::string GetChannel();

private:
  struct DataChannel {
    explicit DataChannel(std::string channel_name): channel_name_(channel_name) {
      channel_ =
          std::make_unique<shm::posix::POSIXChannel<msg::ClientMsg, 1024, 1>>(
              channel_name, shm::posix::POSIX_CHANNEL_CREATE |
                                shm::posix::POSIX_CHANNEL_CLEAN);
    }

    msg::ClientMsg GetMessage() {
      msg::ClientMsg msg;
      auto status = false;
      auto shm_queue = channel_->GetQueue();
      while (!status) {
        status = shm_queue->queue.try_pop(msg);
        if (status) {
          return msg;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(20));
      }

      return msg::ClientMsg{};
    }

    std::string channel_name_;
    int num_of_clients_;
    std::unique_ptr<shm::posix::POSIXChannel<msg::ClientMsg, 1024, 1>> channel_;
  };

  int num_data_channels_;
  std::shared_ptr<grpc_event_engine::experimental::ThreadPool> executor_;
  std::vector<std::shared_ptr<DataChannel>> data_channels_;
  absl::AnyInvocable<void(msg::ClientMsg)> on_receive_callback_;
};
} // namespace mem
} // namespace grpc_core

#endif
