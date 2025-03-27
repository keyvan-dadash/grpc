#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H

#include "absl/container/flat_hash_map.h"
#include "shm/posix_channel.h"
#include "shm/xsi_channel.h"
#include "src/core/ext/transport/mem/common/commands.h"
#include "libcuckoo/cuckoohash_map.hh"

#include "src/core/ext/transport/mem/mem_acceptor.h"
#include "src/core/ext/transport/mem/mem_client_data_connection.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/ext/transport/mem/server_data_connection_pool.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/server/server.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include <algorithm>
#include <cstdint>
#include <memory>

namespace grpc_core {
namespace mem {

class MemServerListener final : public Server::ListenerInterface {
public:
  explicit MemServerListener(Server *server, const ChannelArgs &args,
                             const char *addr);

  ~MemServerListener() override;

  void Start() override;

  const ChannelArgs& args() const { return args_; }

  channelz::ListenSocketNode *channelz_listen_socket_node() const override {
    return nullptr;
  };

  void SetServerListenerState(
      RefCountedPtr<Server::ListenerState> listener_state) override{};

  const grpc_resolved_address *resolved_address() const override {
    // mem doesn't use the new ListenerState interface yet.
    Crash("Unimplemented");
    return nullptr;
  }

  void OnClientMsg(msg::ClientMsg);

  void SetOnDestroyDone(grpc_closure *on_destroy_done) override {
    MutexLock lock(&mu_);
    on_destroy_done_ = on_destroy_done;
  }

  void Orphan() override;

  class ActiveConnection : public InternallyRefCounted<ActiveConnection> {
  public:
    explicit ActiveConnection(RefCountedPtr<MemServerListener> listener_, const msg::ClientRequestConnection &cr);

    ~ActiveConnection() override;

    const ChannelArgs& args() const { return listener_->args(); }

    void ProcessCommandRequest(const msg::ServerCtrlCommands& sc);

    void ProcessClientRequest(const msg::ClientMsg& cm);

    void Orphan() override;

  private:
    void establishConnection();

    msg::ClientRequestConnection cr_;
    std::shared_ptr<MEMClientDataConnection> data_connection_;
    RefCountedPtr<MemServerListener> listener_;
  };

private:
  Server *const server_;
  ChannelArgs args_;
  std::unique_ptr<MEMAcceptor> acceptor_;
  std::unique_ptr<MEMServerDataConnectionPool> data_connections_;
  std::shared_ptr<grpc_event_engine::experimental::ThreadPool> executor_;
  Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  grpc_closure *on_destroy_done_ ABSL_GUARDED_BY(mu_) = nullptr;
  libcuckoo::cuckoohash_map<std::string, RefCountedPtr<ActiveConnection>> connection_list_;
  int64_t connection_id_cnt_ = 0;
};
} // namespace mem
} // namespace grpc_core

int grpc_server_add_mem_port(grpc_server *server, const char *addr);

#endif // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_CHAOTIC_GOOD_SERVER_H
