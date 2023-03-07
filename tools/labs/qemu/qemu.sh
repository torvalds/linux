#!/bin/bash
#
# This script runs qemu and creates a symbolic link named serial.pts
# to the qemu serial console (pts based). Because the qemu pts
# allocation is dynamic, it is preferable to have a stable path to
# avoid visual inspection of the qemu output when connecting to the
# serial console.

SCRIPT_DIR=$(dirname "${BASH_SOURCE[0]}")

case $ARCH in
    x86)
	qemu=qemu-system-i386
	;;
    arm)
	qemu=qemu-system-arm
	;;
esac

echo info chardev | nc -U -l qemu.mon | egrep --line-buffered -o "/dev/pts/[0-9]*" | xargs -I PTS ln -fs PTS serial.pts &
$qemu "$@" -monitor unix:qemu.mon
rm qemu.mon 
rm serial.pts
$SCRIPT_DIR/cleanup-net.sh
