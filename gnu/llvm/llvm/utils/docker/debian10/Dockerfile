#===- llvm/utils/docker/debian10/build/Dockerfile -------------------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===//
# Stage 1. Check out LLVM source code and run the build.
FROM launcher.gcr.io/google/debian10:latest as builder
LABEL maintainer "LLVM Developers"
# Install build dependencies of llvm.
# First, Update the apt's source list and include the sources of the packages.
RUN grep deb /etc/apt/sources.list | \
    sed 's/^deb/deb-src /g' >> /etc/apt/sources.list
# Install compiler, python and subversion.
RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates gnupg \
           build-essential cmake make python3 zlib1g wget subversion unzip git && \
    rm -rf /var/lib/apt/lists/*
# Install a newer ninja release. It seems the older version in the debian repos
# randomly crashes when compiling llvm.
RUN wget "https://github.com/ninja-build/ninja/releases/download/v1.8.2/ninja-linux.zip" && \
    echo "d2fea9ff33b3ef353161ed906f260d565ca55b8ca0568fa07b1d2cab90a84a07 ninja-linux.zip" \
        | sha256sum -c  && \
    unzip ninja-linux.zip -d /usr/local/bin && \
    rm ninja-linux.zip

ADD checksums /tmp/checksums
ADD scripts /tmp/scripts

# Checkout the source code.
ARG checkout_args
RUN /tmp/scripts/checkout.sh ${checkout_args}
# Run the build. Results of the build will be available at /tmp/clang-install/.
ARG buildscript_args
RUN /tmp/scripts/build_install_llvm.sh --to /tmp/clang-install ${buildscript_args}


# Stage 2. Produce a minimal release image with build results.
FROM launcher.gcr.io/google/debian10:latest
LABEL maintainer "LLVM Developers"
# Install packages for minimal useful image.
RUN apt-get update && \
    apt-get install -y --no-install-recommends libstdc++-7-dev binutils && \
    rm -rf /var/lib/apt/lists/*
# Copy build results of stage 1 to /usr/local.
COPY --from=builder /tmp/clang-install/ /usr/local/
