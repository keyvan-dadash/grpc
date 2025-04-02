#include "src/core/ext/transport/mem/allocator/allocator.h"
#include "src/core/ext/transport/mem/mem_message.h"
#include "src/core/ext/transport/mem/types.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include <cstring>
#include <tuple>
#include <vector>

namespace grpc_core {
namespace mem {
namespace msg {

struct ClientMetadataEncoder {

  explicit ClientMetadataEncoder(allocator::SharedPtrAllocator allocator)
      : allocator_(std::move(allocator)) {
    out = (MemClientMetadata *)allocator_->alloc->alloc(
        sizeof(MemClientMetadata));
    new (out) MemClientMetadata();
  }

  void Encode(HttpPathMetadata,
              const typename HttpPathMetadata::ValueType &value) {
    auto buffer = allocator_.get()->alloc->alloc(value.size());
    memcpy(buffer, value.data(), value.size());
    out->path = types::String((char *)buffer, value.size());
  }

  void Encode(HttpAuthorityMetadata,
              const typename HttpAuthorityMetadata::ValueType &value) {
    auto buffer = allocator_.get()->alloc->alloc(value.size());
    memcpy(buffer, value.data(), value.size());
    out->authority = types::String((char *)buffer, value.size());
  }

  void Encode(GrpcTimeoutMetadata,
              const typename GrpcTimeoutMetadata::ValueType &value) {
    auto now = Timestamp::Now();
    if (now > value) {
      out->timeout_ms = 0;
    } else {
      // TODO(keyvan): I dont have any fucking idea what the hell is this
      // timeout shit
      out->timeout_ms = (value - now).millis();
    }
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType &value) {
    EncodeWithWarning(Slice::FromExternalString(Which::key()),
                      Slice(Which::Encode(value)));
  }

  void EncodeWithWarning(const Slice &key, const Slice &value) {
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "encoding known key " << key.as_string_view()
        << " with unknown encoding";
    Encode(key, value);
  }

  void Encode(const Slice &key, const Slice &value) {
    key_values_.push_back(
        std::make_tuple((char *)key.data(), (char *)value.data()));
  }

  void FilloutUnkownMetadata() {
    out->num_of_additional_metadata = key_values_.size();
    out->additional_metadata = (types::KeyValue *)allocator_->alloc->alloc(
        sizeof(types::KeyValue) * key_values_.size());
    int index = 0;
    for (auto &it : key_values_) {
      auto key = std::get<0>(it);
      char *buffer = (char *)allocator_->alloc->alloc(key.size());
      memcpy(buffer, key.data(), key.size());
      auto Key = types::String(buffer, key.size());

      auto value = std::get<1>(it);
      buffer = (char *)allocator_->alloc->alloc(value.size());
      memcpy(buffer, value.data(), value.size());
      auto Value = types::String(buffer, value.size());

      out->additional_metadata[index] = types::KeyValue(Key, Value);
      index++;
    }
  }

  MemClientMetadata *out;
  allocator::SharedPtrAllocator allocator_;
  std::vector<std::tuple<std::string, std::string>> key_values_;
};

MemClientMetadata *
CreateMetadataFromGrpc(const ClientMetadata &md,
                       allocator::SharedPtrAllocator allocator) {
  ClientMetadataEncoder e(std::move(allocator));
  md.Encode(&e);
  e.FilloutUnkownMetadata();
  return e.out;
}

template <typename T, typename M>
absl::StatusOr<T> ReadUnknownFields(const M &msg, T md) {
  absl::Status error = absl::OkStatus();
  for (int i = 0; i < msg.num_of_additional_metadata; i++) {
    auto metadata = msg.additional_metadata[i];
    md->Append(metadata.Key.GetString(), Slice::FromCopiedString(metadata.Value.GetString()),
               [&error](absl::string_view error_msg, const Slice &) {
                 if (!error.ok()) {
                   return;
                   }
                 error = absl::InternalError(error_msg);
               });
  }

  if (!error.ok()) {
    return error;
  }
  return std::move(md);
}

absl::StatusOr<ClientMetadataHandle>
CreateClientMetadataFromMemMetadata(const MemClientMetadata &mcm) {
  auto md = Arena::MakePooled<ClientMetadata>();
  md->Set(GrpcStatusFromWire(), true);
  if (!mcm.path.IsEmpty()) {
    md->Set(HttpPathMetadata(), Slice::FromCopiedString(mcm.path.GetString()));
  }

  if (!mcm.authority.IsEmpty()) {
    md->Set(HttpAuthorityMetadata(),
            Slice::FromCopiedString(mcm.authority.GetString()));
  }

  if (mcm.timeout_ms != -1) {
    md->Set(GrpcTimeoutMetadata(), Timestamp::Now() + Duration::Milliseconds(mcm.timeout_ms));
  }
  return ReadUnknownFields(mcm, std::move(md));
}

struct ServerMetadataEncoder {
  explicit ServerMetadataEncoder(allocator::SharedPtrAllocator allocator)
      : allocator_(std::move(allocator)) {
    out = (MemServerMetadata *)allocator_->alloc->alloc(
        sizeof(MemServerMetadata));
    new (out) MemServerMetadata();
  }

  void Encode(GrpcStatusMetadata, grpc_status_code code) {
    out->status = code;
  }

  void Encode(GrpcMessageMetadata, const Slice &value) {
    std::cout << "Value is " << value.size() << std::endl;
    auto buffer = allocator_.get()->alloc->alloc(value.size());
    memcpy(buffer, value.data(), value.size());
    out->msg = types::String((char *)buffer, value.size());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType &value) {
    EncodeWithWarning(Slice::FromExternalString(Which::key()),
                      Slice(Which::Encode(value)));
  }

  void EncodeWithWarning(const Slice &key, const Slice &value) {
    LOG_EVERY_N_SEC(INFO, 10) << "encoding known key " << key.as_string_view()
                              << " with unknown encoding";
    Encode(key, value);
  }

  void Encode(const Slice &key, const Slice &value) {
    key_values_.push_back(
        std::make_tuple((char *)key.data(), (char *)value.data()));
  }

  void FilloutUnkownMetadata() {
    out->num_of_additional_metadata = key_values_.size();
    out->additional_metadata = (types::KeyValue *)allocator_->alloc->alloc(
        sizeof(types::KeyValue) * key_values_.size());
    int index = 0;
    for (auto &it : key_values_) {
      auto key = std::get<0>(it);
      char *buffer = (char *)allocator_->alloc->alloc(key.size());
      memcpy(buffer, key.data(), key.size());
      auto Key = types::String(buffer, key.size());

      auto value = std::get<1>(it);
      buffer = (char *)allocator_->alloc->alloc(value.size());
      memcpy(buffer, value.data(), value.size());
      auto Value = types::String(buffer, value.size());

      out->additional_metadata[index] = types::KeyValue(Key, Value);
      index++;
    }
  }

  MemServerMetadata *out;
  allocator::SharedPtrAllocator allocator_;
  std::vector<std::tuple<std::string, std::string>> key_values_;
};

MemServerMetadata*
CreateServerMetadataFromGrpc(const ServerMetadata &md,
                       allocator::SharedPtrAllocator allocator) {
  ServerMetadataEncoder e(std::move(allocator));
  md.Encode(&e);
  e.FilloutUnkownMetadata();
  return e.out;
}

absl::StatusOr<ServerMetadataHandle>
CreateServerMetadataFromMemMetadata(const MemServerMetadata &mcm) {
  auto md = Arena::MakePooled<ClientMetadata>();
  md->Set(GrpcStatusFromWire(), true);
  if (mcm.status != -1) {
    md->Set(GrpcStatusMetadata(), static_cast<grpc_status_code>(mcm.status));
  }

  if (!mcm.msg.IsEmpty()) {
    md->Set(GrpcMessageMetadata(),
            Slice::FromCopiedString(mcm.msg.GetString()));
  }

  return ReadUnknownFields(mcm, std::move(md));
}

} // namespace msg

} // namespace mem

} // namespace grpc_core
