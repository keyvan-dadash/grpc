# gRPC Bazel BUILD file.
#
# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
)

licenses(["notice"])

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_library(
    name = "grpc_transport_cronet_client_secure",
    srcs = [
        "client/secure/cronet_channel_create.cc",
        "transport/cronet_api_phony.cc",
        "transport/cronet_status.cc",
        "transport/cronet_status.h",
        "transport/cronet_transport.cc",
        "transport/cronet_transport.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "cronet_c_for_grpc",
    ],
    public_hdrs = [
        "client/secure/cronet_channel_create.h",
    ],
    deps = [
        "//:channel",
        "//:channel_create",
        "//:grpc_base",
        "//:grpc_transport_chttp2",
        "//src/core:channel_args",
        "//src/core:closure",
    ],
)
