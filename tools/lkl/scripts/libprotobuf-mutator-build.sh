#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

if [ -z ${PROTOBUF_MUTATOR_DIR+x} ]; then
    echo PROTOBUF_MUTATOR_DIR is not defined
    exit 1
fi

rm -rf "${PROTOBUF_MUTATOR_DIR}"

git clone https://github.com/google/libprotobuf-mutator.git \
    "${PROTOBUF_MUTATOR_DIR}"
cd "${PROTOBUF_MUTATOR_DIR}"
git checkout tags/v1.4

mkdir -p build && cd build
cmake .. -GNinja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug -DLIB_PROTO_MUTATOR_DOWNLOAD_PROTOBUF=on
ninja
