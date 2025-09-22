#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##
#
# This file defines the buildkite and github actions builder images.
# You can build both images using:
#
#   docker compose build
#
# Or you can select a single image to build
#
#  docker compose build buildkite-builder
#
# The final images can be found at
#
#  ghcr.io/libcxx/buildkite-builder
#  ghcr.io/libcxx/actions-builder
#  ghcr.io/libcxx/android-buildkite-builder
#
# Members of the github.com/libcxx/ organizations can push new images to the CI.
# This is done by GitHub actions in the https://github.com/libcxx/builders repo.
#
# ===----------------------------------------------------------------------===##
#                     Running the buildkite image
# ===----------------------------------------------------------------------===##
#
# To start a Buildkite Agent, run it as:
#   $ docker run --env-file <secrets> -it $(docker build -q libcxx/utils/ci)
#
# The environment variables in `<secrets>` should be the ones necessary
# to run a BuildKite agent:
#
#   BUILDKITE_AGENT_TOKEN=<token>
#
# If you're only looking to run the Docker image locally for debugging a
# build bot, see the `run-buildbot-container` script located in this directory.


# HACK: We set the base image in the docker-compose file depending on the final target (buildkite vs github actions).
# This means we have a much slower container build, but we can use the same Dockerfile for both targets.
ARG BASE_IMAGE
FROM $BASE_IMAGE AS builder-base

# Make sure apt-get doesn't try to prompt for stuff like our time zone, etc.
ENV DEBIAN_FRONTEND=noninteractive

# populated in the docker-compose file
ARG GCC_LATEST_VERSION
ENV GCC_LATEST_VERSION=${GCC_LATEST_VERSION}

# populated in the docker-compose file
ARG LLVM_HEAD_VERSION
ENV LLVM_HEAD_VERSION=${LLVM_HEAD_VERSION}

# HACK: The github actions runner image already has sudo and requires its use. The buildkite base image does not.
# Reconcile this.
RUN <<EOF
  apt-get update || true
  apt-get install -y sudo || true
  echo "ALL ALL = (ALL) NOPASSWD: ALL" | tee /etc/sudoers || true
EOF

# Installing tzdata before other packages avoids the time zone prompts.
# These prompts seem to ignore DEBIAN_FRONTEND=noninteractive.
RUN sudo apt-get update \
    && sudo apt-get install -y \
        tzdata

RUN sudo apt-get update \
    && sudo apt-get install -y \
        bash \
        ccache \
        curl \
        gdb \
        git \
        gpg \
        language-pack-en \
        language-pack-fr \
        language-pack-ja \
        language-pack-ru \
        language-pack-zh-hans \
        libedit-dev \
        libncurses5-dev \
        libpython3-dev \
        libxml2-dev \
        lsb-release \
        make \
        python3 \
        python3-dev \
        python3-packaging \
        python3-setuptools \
        python3-psutil \
        software-properties-common \
        swig \
        unzip \
        uuid-dev \
        wget \
        xz-utils \
    && sudo rm -rf /var/lib/apt/lists/*

# Install various tools used by the build or the test suite
#RUN apt-get update && apt-get install -y ninja-build python3 python3-distutils python3-psutil git gdb ccache
# TODO add ninja-build once 1.11 is available in Ubuntu, also remove the manual installation.
RUN <<EOF
  wget -qO /tmp/ninja.gz https://github.com/ninja-build/ninja/releases/latest/download/ninja-linux.zip
  gunzip /tmp/ninja.gz
  chmod a+x /tmp/ninja
  sudo mv /tmp/ninja /usr/local/bin/ninja
EOF


# These two locales are not enabled by default so generate them
RUN <<EOF
  printf "fr_CA ISO-8859-1\ncs_CZ ISO-8859-2" | sudo tee -a /etc/locale.gen
  sudo mkdir /usr/local/share/i1en/
  printf "fr_CA ISO-8859-1\ncs_CZ ISO-8859-2" | sudo tee -a /usr/local/share/i1en/SUPPORTED
  sudo locale-gen
EOF

# Install Clang <latest>, <latest-1> and ToT, which are the ones we support.
# We also install <latest-2> because we need to support the "latest-1" of the
# current LLVM release branch, which is effectively the <latest-2> of the
# tip-of-trunk LLVM. For example, after branching LLVM 14 but before branching
# LLVM 15, we still need to have Clang 12 in this Docker image because the LLVM
# 14 release branch CI uses it. The tip-of-trunk CI will never use Clang 12,
# though.
RUN <<EOF
  sudo apt-get update
  wget https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh
  chmod +x /tmp/llvm.sh
  sudo /tmp/llvm.sh $(($LLVM_HEAD_VERSION - 3)) all  # for CI transitions
  sudo /tmp/llvm.sh $(($LLVM_HEAD_VERSION - 2)) all  # previous release
  sudo /tmp/llvm.sh $(($LLVM_HEAD_VERSION - 1)) all  # latest release
  sudo /tmp/llvm.sh $LLVM_HEAD_VERSION          all  # current ToT
  sudo apt-get install -y libomp5-$LLVM_HEAD_VERSION
  sudo rm -rf /var/lib/apt/lists/*
EOF

# Install the most recent GCC, like clang install the previous version as a transition.
RUN <<EOF
  sudo git clone https://github.com/compiler-explorer/infra.git /tmp/ce-infra
  (cd /tmp/ce-infra && sudo make ce)
  sudo /tmp/ce-infra/bin/ce_install install compilers/c++/x86/gcc $GCC_LATEST_VERSION.1.0
  sudo /tmp/ce-infra/bin/ce_install install compilers/c++/x86/gcc $((GCC_LATEST_VERSION - 1)).1.0
  sudo ln -s /opt/compiler-explorer/gcc-$GCC_LATEST_VERSION.1.0/bin/gcc /usr/bin/gcc-$GCC_LATEST_VERSION
  sudo ln -s /opt/compiler-explorer/gcc-$GCC_LATEST_VERSION.1.0/bin/g++ /usr/bin/g++-$GCC_LATEST_VERSION
  sudo ln -s /opt/compiler-explorer/gcc-$((GCC_LATEST_VERSION - 1)).1.0/bin/gcc /usr/bin/gcc-$((GCC_LATEST_VERSION - 1))
  sudo ln -s /opt/compiler-explorer/gcc-$((GCC_LATEST_VERSION - 1)).1.0/bin/g++ /usr/bin/g++-$((GCC_LATEST_VERSION - 1))
  sudo rm -rf /tmp/ce-infra
EOF

RUN <<EOF
    # Install a recent CMake
    wget https://github.com/Kitware/CMake/releases/download/v3.21.1/cmake-3.21.1-linux-x86_64.sh -O /tmp/install-cmake.sh
    sudo bash /tmp/install-cmake.sh --prefix=/usr --exclude-subdir --skip-license
    rm /tmp/install-cmake.sh
EOF

# ===----------------------------------------------------------------------===##
#                       Android Buildkite Image
# ===----------------------------------------------------------------------===##

FROM ubuntu:jammy AS android-builder-base

ARG ANDROID_CLANG_VERSION
ARG ANDROID_CLANG_PREBUILTS_COMMIT
ARG ANDROID_SYSROOT_BID

RUN  apt-get update && apt-get install -y curl unzip git

# Install the Android platform tools (e.g. adb) into /opt/android/sdk.
RUN <<EOF
  mkdir -p /opt/android/sdk
  cd /opt/android/sdk
  curl -LO https://dl.google.com/android/repository/platform-tools-latest-linux.zip
  unzip platform-tools-latest-linux.zip
  rm platform-tools-latest-linux.zip
EOF

# Install the current Android compiler. Specify the prebuilts commit to retrieve
# this compiler version even after it's removed from HEAD.

ENV ANDROID_CLANG_VERSION=$ANDROID_CLANG_VERSION
ENV ANDROID_CLANG_PREBUILTS_COMMIT=$ANDROID_CLANG_PREBUILTS_COMMIT
RUN <<EOF
    git clone --filter=blob:none --sparse \
        https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86 \
        /opt/android/clang
    git -C /opt/android/clang checkout ${ANDROID_CLANG_PREBUILTS_COMMIT}
    git -C /opt/android/clang sparse-checkout add clang-${ANDROID_CLANG_VERSION}
    rm -fr /opt/android/clang/.git
    ln -sf /opt/android/clang/clang-${ANDROID_CLANG_VERSION} /opt/android/clang/clang-current
    # The "git sparse-checkout" and "ln" commands succeed even if nothing was
    # checked out, so use this "ls" command to fix that.
    ls /opt/android/clang/clang-current/bin/clang
EOF

# Install an Android sysroot. New AOSP sysroots are available at
# https://ci.android.com/builds/branches/aosp-main/grid, the "ndk" target. The
# NDK also makes its sysroot prebuilt available at
# https://android.googlesource.com/platform/prebuilts/ndk/+/refs/heads/dev/platform/sysroot.

ENV ANDROID_SYSROOT_BID=$ANDROID_SYSROOT_BID
RUN <<EOF
  cd /opt/android
  curl -L -o ndk_platform.tar.bz2 \
      https://androidbuildinternal.googleapis.com/android/internal/build/v3/builds/${ANDROID_SYSROOT_BID}/ndk/attempts/latest/artifacts/ndk_platform.tar.bz2/url
  tar xf ndk_platform.tar.bz2
  rm ndk_platform.tar.bz2
EOF

# Install Docker
RUN <<EOF
  curl -fsSL https://get.docker.com -o /tmp/get-docker.sh
  sh /tmp/get-docker.sh
  rm /tmp/get-docker.sh

  # Install Docker. Mark the binary setuid so it can be run without prefixing it
  # with sudo. Adding the container user to the docker group doesn't work because
  # /var/run/docker.sock is owned by the host's docker GID, not the container's
  # docker GID.
  chmod u+s /usr/bin/docker
EOF

# ===----------------------------------------------------------------------===##
#                    Buildkite Builder Image
# ===----------------------------------------------------------------------===##
#
# IMAGE: ghcr.io/libcxx/buildkite-builder.
#
FROM builder-base AS buildkite-builder

# Create the libcxx-builder user, regardless of if we use it or not
RUN sudo useradd --create-home libcxx-builder

USER libcxx-builder
WORKDIR /home/libcxx-builder

# Install the Buildkite agent and dependencies. This must be done as non-root
# for the Buildkite agent to be installed in a path where we can find it.
RUN <<EOF
  cd /home/libcxx-builder
  curl -sL https://raw.githubusercontent.com/buildkite/agent/main/install.sh -o /tmp/install-agent.sh
  bash /tmp/install-agent.sh
  rm /tmp/install-agent.sh
  echo "tags=\"queue=libcxx-builders,arch=$(uname -m),os=linux\"" \
    >> /home/libcxx-builder/.buildkite-agent/buildkite-agent.cfg
EOF

USER libcxx-builder
WORKDIR /home/libcxx-builder

ENV PATH="${PATH}:/home/libcxx-builder/.buildkite-agent/bin"

CMD ["buildkite-agent", "start"]

# ===----------------------------------------------------------------------===##
#                    Android Buildkite Builder Image
# ===----------------------------------------------------------------------===##
#
# IMAGE: ghcr.io/libcxx/android-buildkite-builder.
#
FROM buildkite-builder AS android-buildkite-builder

COPY --from=android-builder-base /opt/android /opt/android
COPY ./vendor/android/container-setup.sh /opt/android/container-setup.sh

ENV PATH="/opt/android/sdk/platform-tools:${PATH}"

USER libcxx-builder
WORKDIR /home/libcxx-builder

# Reset the configuration, we pass the configuration via the environment.
RUN cp /home/libcxx-builder/.buildkite-agent/buildkite-agent.dist.cfg \
       /home/libcxx-builder/.buildkite-agent/buildkite-agent.cfg

# Modify the Buildkite agent cmdline to do Android setup stuff first.
CMD /opt/android/container-setup.sh && buildkite-agent start

# ===----------------------------------------------------------------------===##
#                    Github Actions Builder Image
# ===----------------------------------------------------------------------===##
#
# IMAGE: ghcr.io/libcxx/actions-builder.
#
FROM builder-base AS actions-builder

WORKDIR /home/runner
USER runner
