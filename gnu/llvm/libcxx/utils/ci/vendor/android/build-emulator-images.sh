#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

set -e

THIS_DIR="$(cd "$(dirname "$0")" && pwd)"
. "${THIS_DIR}/emulator-functions.sh"

build_image() {
    local EMU_IMG="$1"
    validate_emu_img_syntax "${EMU_IMG}"
    docker build -t $(docker_image_of_emu_img ${EMU_IMG}) \
        -f Dockerfile.emulator . \
        --build-arg API=$(api_of_emu_img ${EMU_IMG}) \
        --build-arg TYPE=$(type_of_emu_img ${EMU_IMG}) \
        --build-arg ABI=$(abi_of_arch $(arch_of_emu_img ${EMU_IMG}))
}

cd "${THIS_DIR}"

build_image 21-def-x86
build_image 33-goog-x86_64
