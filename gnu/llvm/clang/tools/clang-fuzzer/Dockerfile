#===- llvm/tools/clang/tools/clang-fuzzer ---------------------------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===//
# Produces an image that builds clang-proto-fuzzer
FROM ubuntu:16.04
RUN apt-get update -y
RUN apt-get install -y autoconf automake libtool curl make g++ unzip wget git \
    binutils liblzma-dev libz-dev python-all cmake ninja-build subversion \
    pkg-config docbook2x

WORKDIR /root

# Get protobuf
RUN wget -qO- https://github.com/google/protobuf/releases/download/v3.3.0/protobuf-cpp-3.3.0.tar.gz | tar zxf -
RUN cd protobuf-3.3.0 && ./autogen.sh && ./configure && make -j $(nproc) && make check -j $(nproc) && make install && ldconfig
# Get LLVM
RUN svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
RUN cd llvm/tools && svn co http://llvm.org/svn/llvm-project/cfe/trunk clang -r $(cd ../ && svn info | grep Revision | awk '{print $2}')
RUN cd llvm/projects && svn co http://llvm.org/svn/llvm-project/compiler-rt/trunk compiler-rt -r $(cd ../ && svn info | grep Revision | awk '{print $2}')
# Build plain LLVM (stage 0)
RUN mkdir build0 && cd build0 && cmake -GNinja -DCMAKE_BUILD_TYPE=Release ../llvm && ninja
# Configure instrumented LLVM (stage 1)
RUN mkdir build1 && cd build1 && cmake -GNinja -DCMAKE_BUILD_TYPE=Release ../llvm \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DCMAKE_C_COMPILER=`pwd`/../build0/bin/clang \
    -DCMAKE_CXX_COMPILER=`pwd`/../build0/bin/clang++ \
    -DLLVM_USE_SANITIZE_COVERAGE=YES \
    -DLLVM_USE_SANITIZER=Address -DCLANG_ENABLE_PROTO_FUZZER=ON
# Build the fuzzers
RUN cd build1 && ninja clang-fuzzer
RUN cd build1 && ninja clang-objc-fuzzer
RUN cd build1 && ninja clang-proto-fuzzer
RUN cd build1 && ninja clang-proto-to-cxx
RUN cd build1 && ninja clang-loop-proto-to-cxx
RUN cd build1 && ninja clang-loop-proto-to-llvm
RUN cd build1 && ninja clang-loop-proto-fuzzer
RUN cd build1 && ninja clang-llvm-proto-fuzzer
