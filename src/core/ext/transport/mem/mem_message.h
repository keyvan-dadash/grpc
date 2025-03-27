#ifndef MEM_MSG
#define MEM_MSG

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


enum class MsgUsageStatus : uint8_t {
  kUnused = 0x10,
  kUsed = 0x20,
};

struct MsgData {
  MsgUsageStatus msg_usage_status;
  char *data;
};

struct ClientMsgHeader {
  int64_t connection_id;
  int64_t request_id;
  MsgType msg_type;
};

struct ClientMsg {
  ClientMsgHeader header;
  MsgData request_data;
};

struct ServerMsgHeader {
  int64_t response_of_request_id;
  MsgType msg_type;
};

struct ServerMsg {
  ServerMsgHeader header;
  MsgData response_data;
};

} // namespace msg
} // namespace mem
} // namespace grpc_core

#endif
