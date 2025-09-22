#===- llvm/utils/docker/nvidia-cuda/build/Dockerfile ---------------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===//
# Stage 1. Check out LLVM source code and run the build.
FROM nvidia/cuda:8.0-devel as builder
LABEL maintainer "LLVM Developers"
# Install llvm build dependencies.
RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates cmake python \
        subversion ninja-build git && \
    rm -rf /var/lib/apt/lists/*

ADD checksums /tmp/checksums
ADD scripts /tmp/scripts

# Checkout the source code.
ARG checkout_args
RUN /tmp/scripts/checkout.sh ${checkout_args}
# Run the build. Results of the build will be available at /tmp/clang-install/.
ARG buildscript_args
RUN /tmp/scripts/build_install_llvm.sh --to /tmp/clang-install ${buildscript_args}


# Stage 2. Produce a minimal release image with build results.
FROM nvidia/cuda:8.0-devel
LABEL maintainer "LLVM Developers"
# Copy clang installation into this container.
COPY --from=builder /tmp/clang-install/ /usr/local/
# C++ standard library and binutils are already included in the base package.
