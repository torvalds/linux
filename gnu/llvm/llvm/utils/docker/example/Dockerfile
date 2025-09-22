#===- llvm/utils/docker/example/build/Dockerfile -------------------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===//
# This is an example Dockerfile to build an image that compiles clang.
# Replace FIXMEs to prepare your own image.

# Stage 1. Check out LLVM source code and run the build.
# FIXME: Replace 'ubuntu' with your base image
FROM ubuntu as builder
# FIXME: Change maintainer name
LABEL maintainer "Maintainer <maintainer@email>"
# FIXME: Install llvm/clang build dependencies here. Including compiler to
# build stage1, cmake, subversion, ninja, etc.

ADD checksums /tmp/checksums
ADD scripts /tmp/scripts

# Checkout the source code.
ARG checkout_args
RUN /tmp/scripts/checkout.sh ${checkout_args}
# Run the build. Results of the build will be available at /tmp/clang-install/.
ARG buildscript_args
RUN /tmp/scripts/build_install_llvm.sh --to /tmp/clang-install ${buildscript_args}


# Stage 2. Produce a minimal release image with build results.
# FIXME: Replace 'ubuntu' with your base image.
FROM ubuntu
# FIXME: Change maintainer name.
LABEL maintainer "Maintainer <maintainer@email>"
# FIXME: Install all packages you want to have in your release container.
# A minimal useful installation should include at least libstdc++ and binutils.

# Copy build results of stage 1 to /usr/local.
COPY --from=builder /tmp/clang-install/ /usr/local/
