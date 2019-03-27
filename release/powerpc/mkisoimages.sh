#!/bin/sh
#
# Module: mkisoimages.sh
# Author: Jordan K Hubbard
# Date:   22 June 2001
#
# $FreeBSD$
#
# This script is used by release/Makefile to build the (optional) ISO images
# for a FreeBSD release.  It is considered architecture dependent since each
# platform has a slightly unique way of making bootable CDs.  This script
# is also allowed to generate any number of images since that is more of
# publishing decision than anything else.
#
# Usage:
#
# mkisoimages.sh [-b] image-label image-name base-bits-dir [extra-bits-dir]
#
# Where -b is passed if the ISO image should be made "bootable" by
# whatever standards this architecture supports (may be unsupported),
# image-label is the ISO image label, image-name is the filename of the
# resulting ISO image, base-bits-dir contains the image contents and
# extra-bits-dir, if provided, contains additional files to be merged
# into base-bits-dir as part of making the image.

set -e

if [ "$1" = "-b" ]; then
	bootable=1
	shift
else
	bootable=""
fi

if [ $# -lt 3 ]; then
	echo "Usage: $0 [-b] image-label image-name base-bits-dir [extra-bits-dir]"
	exit 1
fi

LABEL=`echo "$1" | tr '[:lower:]' '[:upper:]'`; shift
NAME="$1"; shift

if [ -n "$bootable" ]; then
	echo "Building bootable disc"

	# Apple boot code
	uudecode -o /tmp/hfs-boot-block.bz2 "`dirname "$0"`/hfs-boot.bz2.uu"
	bzip2 -d /tmp/hfs-boot-block.bz2
	OFFSET=$(hd /tmp/hfs-boot-block | grep 'Loader START' | cut -f 1 -d ' ')
	OFFSET=0x$(echo 0x$OFFSET | awk '{printf("%x\n",$1/512);}')
	dd if="$1/boot/loader" of=/tmp/hfs-boot-block seek=$OFFSET conv=notrunc

	bootable="-o bootimage=macppc;/tmp/hfs-boot-block -o no-emul-boot"

	# pSeries/PAPR boot code
	mkdir -p "$1/ppc/chrp"
	cp "$1/boot/loader" "$1/ppc/chrp"
	cat > "$1/ppc/bootinfo.txt" << EOF
<chrp-boot>
<description>FreeBSD Install</description>
<os-name>FreeBSD</os-name>
<boot-script>boot &device;:,\ppc\chrp\loader</boot-script>
</chrp-boot>
EOF
	bootable="$bootable -o chrp-boot"

	# Petitboot config for PS3/PowerNV
	echo FreeBSD Install=\'/boot/kernel/kernel vfs.root.mountfrom=cd9660:/dev/iso9660/$LABEL\' > "$1/etc/kboot.conf"
fi

publisher="The FreeBSD Project.  https://www.FreeBSD.org/"
echo "/dev/iso9660/$LABEL / cd9660 ro 0 0" > "$1/etc/fstab"
makefs -t cd9660 $bootable -o rockridge -o label="$LABEL" -o publisher="$publisher" "$NAME" "$@"
rm -f "$1/etc/fstab"
rm -f /tmp/hfs-boot-block
rm -rf "$1/ppc"
