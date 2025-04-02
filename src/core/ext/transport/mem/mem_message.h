#ifndef MEM_MSG
#define MEM_MSG

#include "snmalloc/snmalloc.h"
#include "absl/status/statusor.h"
#include "src/core/ext/transport/mem/allocator/allocator.h"
#include "src/core/ext/transport/mem/types.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"
#include <chrono>
#include <cstdint>

namespace grpc_core {
namespace mem {
namespace msg {

const int maxChannelName = 64;
const int maxSHMRegionName = 64;

struct ClientRequestConnection {
  char ctrl_command_channel_name[maxChannelName];
  char response_channel_name[maxChannelName];
  char request_memory_pool_name[maxSHMRegionName];
  int64_t memory_pool_size;
};

struct ServerAcceptConnection {
  int64_t connection_id;
  char request_channel_name[maxChannelName];
  char response_memory_pool_name[maxSHMRegionName];
  int64_t response_memory_pool_size;
};

enum CtrlOP {
  CTRL_REQUEST_CON = 0x1,
};

struct ServerCtrlCommands {
  int command_op;
  union {
    ClientRequestConnection req_connection;
  } command;
};

enum ClientCtrlOP {
  CTRL_ACCEPT_CON = 0x1,
};

struct ClientCtrlCommands {
  int command_op;
  union {
    ServerAcceptConnection accept_connection_info;
  } command;
};

enum class MsgType : uint8_t {
  kClientInitialMetadata = 0x80,
  kClientEndOfStream = 0x81,
  kServerInitialMetadata = 0x91,
  kServerTrailingMetadata = 0x92,
  kMessage = 0xa0,
  kBeginMessage = 0xa1,
  kCancel = 0xff,
};

struct MemClientMetadata {
  MemClientMetadata(): path(), authority() {}
  types::String path;
  types::String authority;
  uint64_t timeout_ms = -1;
  types::KeyValue *additional_metadata;
  uint32_t num_of_additional_metadata = 0;
  int64_t additional_metadata_offset;

  void CalcualteOffssetFrom(char *origin) {
    path.CalcualteOffssetFrom(origin);
    authority.CalcualteOffssetFrom(origin);
    if (IsAdditionalMetadataExist()) {
      additional_metadata_offset = (char *)additional_metadata - origin;
      for (int i = 0; i < num_of_additional_metadata; i++) {
        additional_metadata[i].Key.CalcualteOffssetFrom(origin);
        additional_metadata[i].Value.CalcualteOffssetFrom(origin);
      }
    }
  }

  void ApplyOffsetFrom(char *origin) {
    path.ApplyOffsetFrom(origin);
    authority.ApplyOffsetFrom(origin);
    if (IsAdditionalMetadataExist()) {
      additional_metadata = (types::KeyValue *)snmalloc::pointer_offset(
          origin, additional_metadata_offset);
      for (int i = 0; i < num_of_additional_metadata; i++) {
        additional_metadata[i].Key.ApplyOffsetFrom(origin);
        additional_metadata[i].Value.ApplyOffsetFrom(origin);
      }
    }
  }

  bool IsAdditionalMetadataExist() { return num_of_additional_metadata != 0; }
};

MemClientMetadata *
CreateMetadataFromGrpc(const ClientMetadata &md,
                       allocator::SharedPtrAllocator allocator);

absl::StatusOr<ClientMetadataHandle>
CreateClientMetadataFromMemMetadata(const MemClientMetadata &mcm);

struct MemClientMsg {
  explicit MemClientMsg(types::String str) : msg(str) {}
  types::String msg;

  void CalcualteOffssetFrom(char *origin) { msg.CalcualteOffssetFrom(origin); }

  void ApplyOffsetFrom(char *origin) { msg.ApplyOffsetFrom(origin); }

  MessageHandle GetMessageHandle() {
    grpc_slice slice = grpc_slice_from_copied_buffer(msg.str, msg.str_size);
    grpc_core::SliceBuffer slice_buffer;
    grpc_slice_buffer_init(slice_buffer.c_slice_buffer());
    grpc_slice_buffer_add(slice_buffer.c_slice_buffer(), slice);
    return grpc_core::Arena::MakePooled<grpc_core::Message>(std::move(slice_buffer), 0);
  }
};

struct MsgData {
  MsgData() {}
  union {
    MemClientMetadata mcm;
    MemClientMsg mcmsg;
  };
};

struct ClientMsgHeader {
  ClientMsgHeader() {
    auto now = std::chrono::high_resolution_clock::now();

    issued_time = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
  }

  int64_t connection_id;
  int64_t request_id;
  MsgType msg_type;
  int64_t issued_time;

  int64_t Diff() const {
    auto end = std::chrono::high_resolution_clock::now();

    auto endd = std::chrono::duration_cast<std::chrono::microseconds>(
        end.time_since_epoch()).count();

    return endd - issued_time;
  }
};

struct ClientMsg {
  ClientMsg() {}
  ClientMsgHeader header;
  MsgData request_data;
};

struct MemServerMetadata {
  MemServerMetadata(): msg() {}
  uint32_t status = -1;
  types::String msg;
  types::KeyValue *additional_metadata;
  uint32_t num_of_additional_metadata = 0;
  int64_t additional_metadata_offset;

  void CalcualteOffssetFrom(char *origin) {
    msg.CalcualteOffssetFrom(origin);
    if (IsAdditionalMetadataExist()) {
      additional_metadata_offset = (char *)additional_metadata - origin;
      for (int i = 0; i < num_of_additional_metadata; i++) {
        additional_metadata[i].Key.CalcualteOffssetFrom(origin);
        additional_metadata[i].Value.CalcualteOffssetFrom(origin);
      }
    }
  }

  void ApplyOffsetFrom(char *origin) {
    msg.ApplyOffsetFrom(origin);
    if (IsAdditionalMetadataExist()) {
      additional_metadata = (types::KeyValue *)snmalloc::pointer_offset(
          origin, additional_metadata_offset);
      for (int i = 0; i < num_of_additional_metadata; i++) {
        additional_metadata[i].Key.ApplyOffsetFrom(origin);
        additional_metadata[i].Value.ApplyOffsetFrom(origin);
      }
    }
  }

  bool IsAdditionalMetadataExist() { return num_of_additional_metadata != 0; }
};

MemServerMetadata*
CreateServerMetadataFromGrpc(const ServerMetadata &md,
                       allocator::SharedPtrAllocator allocator);

absl::StatusOr<ServerMetadataHandle>
CreateServerMetadataFromMemMetadata(const MemServerMetadata &mcm);

MessageHandle GenerateMessageFromString(types::String str);

struct MemServerMsg {
  explicit MemServerMsg(types::String str) : msg(str) {}
  types::String msg;

  void CalcualteOffssetFrom(char *origin) { msg.CalcualteOffssetFrom(origin); }

  void ApplyOffsetFrom(char *origin) { msg.ApplyOffsetFrom(origin); }
};


struct ServerMsgData {
  ServerMsgData() {}
  union {
    MemServerMetadata msm;
    MemServerMsg msg;
  };
};

struct ServerMsgHeader {
  ServerMsgHeader() {
  auto now = std::chrono::high_resolution_clock::now();

    issued_time = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

  }
  int64_t response_of_request_id;
  int64_t connection_id;
  MsgType msg_type;
  int64_t issued_time;

  int64_t Diff() const {
    auto end = std::chrono::high_resolution_clock::now();

    auto endd = std::chrono::duration_cast<std::chrono::microseconds>(
        end.time_since_epoch()).count();

    return endd - issued_time;
  }
};

struct ServerMsg {
  ServerMsg() {}
  ServerMsgHeader header;
  ServerMsgData response_data;
};

} // namespace msg
} // namespace mem
} // namespace grpc_core

#endif
