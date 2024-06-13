#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run filesystem operations tests on an 1 MiB disk image that is formatted with
# a vfat filesystem and mounted in a temporary directory using a loop device.
#
# Copyright 2022 Red Hat Inc.
# Author: Javier Martinez Canillas <javierm@redhat.com>

set -e
set -u
set -o pipefail

BASE_DIR="$(dirname $0)"
TMP_DIR="$(mktemp -d /tmp/fat_tests_tmp.XXXX)"
IMG_PATH="${TMP_DIR}/fat.img"
MNT_PATH="${TMP_DIR}/mnt"

cleanup()
{
    mountpoint -q "${MNT_PATH}" && unmount_image
    rm -rf "${TMP_DIR}"
}
trap cleanup SIGINT SIGTERM EXIT

create_loopback()
{
    touch "${IMG_PATH}"
    chattr +C "${IMG_PATH}" >/dev/null 2>&1 || true

    truncate -s 1M "${IMG_PATH}"
    mkfs.vfat "${IMG_PATH}" >/dev/null 2>&1
}

mount_image()
{
    mkdir -p "${MNT_PATH}"
    sudo mount -o loop "${IMG_PATH}" "${MNT_PATH}"
}

rename_exchange_test()
{
    local rename_exchange="${BASE_DIR}/rename_exchange"
    local old_path="${MNT_PATH}/old_file"
    local new_path="${MNT_PATH}/new_file"

    echo old | sudo tee "${old_path}" >/dev/null 2>&1
    echo new | sudo tee "${new_path}" >/dev/null 2>&1
    sudo "${rename_exchange}" "${old_path}" "${new_path}" >/dev/null 2>&1
    sudo sync -f "${MNT_PATH}"
    grep new "${old_path}" >/dev/null 2>&1
    grep old "${new_path}" >/dev/null 2>&1
}

rename_exchange_subdir_test()
{
    local rename_exchange="${BASE_DIR}/rename_exchange"
    local dir_path="${MNT_PATH}/subdir"
    local old_path="${MNT_PATH}/old_file"
    local new_path="${dir_path}/new_file"

    sudo mkdir -p "${dir_path}"
    echo old | sudo tee "${old_path}" >/dev/null 2>&1
    echo new | sudo tee "${new_path}" >/dev/null 2>&1
    sudo "${rename_exchange}" "${old_path}" "${new_path}" >/dev/null 2>&1
    sudo sync -f "${MNT_PATH}"
    grep new "${old_path}" >/dev/null 2>&1
    grep old "${new_path}" >/dev/null 2>&1
}

unmount_image()
{
    sudo umount "${MNT_PATH}" &> /dev/null
}

create_loopback
mount_image
rename_exchange_test
rename_exchange_subdir_test
unmount_image

exit 0
