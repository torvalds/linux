#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -e

# Use $KERNEL and $INITRAMFS to pass custom Kernel and optional initramfs

KERNEL="${KERNEL:-/boot/bzImage}"
set -- -l -s --reuse-cmdline "$KERNEL"

INITRAMFS="${INITRAMFS:-/boot/initramfs}"
if [ -f "$INITRAMFS" ]; then
    set -- "$@" --initrd="$INITRAMFS"
fi

kexec "$@"
kexec -e
