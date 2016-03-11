#!/bin/sh

dpdk_version="2.2.0"

git clone -b v${dpdk_version} git://dpdk.org/dpdk dpdk-${dpdk_version}

RTE_SDK=$(pwd)/dpdk-${dpdk_version}
RTE_TARGET=$(uname -m)-native-linuxapp-gcc
export RTE_SDK
export RTE_TARGET
export EXTRA_CFLAGS="-fPIC -O0 -g3"

set -e
cd dpdk-${dpdk_version}
make -j1 T=${RTE_TARGET} config
make -j3 CONFIG_RTE_BUILD_COMBINE_LIBS=y \
  || (echo "dpdk build failed" && exit 1)
