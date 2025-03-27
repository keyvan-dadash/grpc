// Copyright 2024 gRPC authors.
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
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>

#include <memory>

#include "src/core/ext/transport/mem/client/mem_client.h"
#include "src/core/ext/transport/mem/server/mem_server.h"

namespace grpc {

namespace {

class MemInsecureChannelCredentialsImpl final : public ChannelCredentials {
public:
  MemInsecureChannelCredentialsImpl() : ChannelCredentials(nullptr) {}

private:
  std::shared_ptr<Channel> CreateChannelWithInterceptors(
      const grpc::string &target, const grpc::ChannelArguments &args,
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
          interceptor_creators) override {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    auto channel = grpc::CreateChannelInternal(
        "", grpc_mem_client_channel_create(target.c_str(), &channel_args),
        std::move(interceptor_creators));
    return channel;
  }
};

class MemInsecureServerCredentialsImpl final : public ServerCredentials {
public:
  MemInsecureServerCredentialsImpl() : ServerCredentials(nullptr) {}

  int AddPortToServer(const std::string &addr, grpc_server *server) override {
    return grpc_server_add_mem_port(server, addr.c_str());
  }
};

} // namespace

namespace experimental {

std::shared_ptr<ChannelCredentials> MemInsecureChannelCredentials() {
  return std::make_shared<MemInsecureChannelCredentialsImpl>();
}

std::shared_ptr<ServerCredentials> MemInsecureServerCredentials() {
  return std::make_shared<MemInsecureServerCredentialsImpl>();
}

} // namespace experimental

} // namespace grpc
