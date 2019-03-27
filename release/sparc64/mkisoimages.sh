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

if [ $# -lt 3 ]; then
	echo "Usage: $0 [-b] image-label image-name base-bits-dir [extra-bits-dir]" > /dev/stderr
	exit 1
fi

case "$1" in
-b)	BOPT="$1"; shift ;;
esac
LABEL=`echo "$1" | tr '[:lower:]' '[:upper:]'`; shift
NAME="$1"; shift
BASEBITSDIR="$1"

# Create an ISO image
publisher="The FreeBSD Project.  https://www.FreeBSD.org/"
echo "/dev/iso9660/$LABEL / cd9660 ro 0 0" > "$BASEBITSDIR/etc/fstab"
makefs -t cd9660 -o rockridge -o label="$LABEL" -o publisher="$publisher" "$NAME.tmp" "$@"
rm -f "$BASEBITSDIR/etc/fstab"

if [ "$BOPT" != "-b" ]; then
	mv "$NAME.tmp" "$NAME"
	exit 0
fi

TMPIMGDIR=`mktemp -d /tmp/bootfs.XXXXXXXX` || exit 1
BOOTFSDIR="$TMPIMGDIR/bootfs"
BOOTFSIMG="$TMPIMGDIR/bootfs.img"

# Create a boot filesystem
mkdir -p "$BOOTFSDIR/boot"
cp -p "$BASEBITSDIR/boot/loader" "$BOOTFSDIR/boot"
makefs -t ffs -B be -M 512k "$BOOTFSIMG" "$BOOTFSDIR"
dd if="$BASEBITSDIR/boot/boot1" of="$BOOTFSIMG" bs=512 conv=notrunc,sync

# Create a boot ISO image
: ${CYLSIZE:=640}
ISOSIZE=$(stat -f %z "$NAME.tmp")
ISOBLKS=$((($ISOSIZE + 511) / 512))
ISOCYLS=$((($ISOBLKS + ($CYLSIZE - 1)) / $CYLSIZE))

BOOTFSSIZE=$(stat -f %z "$BOOTFSIMG")
BOOTFSBLKS=$((($BOOTFSSIZE + 511) / 512))
BOOTFSCYLS=$((($BOOTFSBLKS + ($CYLSIZE - 1)) / $CYLSIZE))

ENDCYL=$(($ISOCYLS + $BOOTFSCYLS))
NSECTS=$(($ENDCYL * 1 * $CYLSIZE))

dd if="$NAME.tmp" of="$NAME" bs="${CYLSIZE}b" conv=notrunc,sync
dd if="$BOOTFSIMG" of="$NAME" bs="${CYLSIZE}b" seek=$ISOCYLS conv=notrunc,sync
# The number of alternative cylinders is always 2.
dd if=/dev/zero of="$NAME" bs="${CYLSIZE}b" seek=$ENDCYL count=2 conv=notrunc,sync
rm -rf "$NAME.tmp" "$TMPIMGDIR"

# Write VTOC8 label to boot ISO image
MD=`mdconfig -a -t vnode -S 512 -y 1 -x "$CYLSIZE" -f "$NAME"`
gpart create -s VTOC8 $MD
# !4: usr, for ISO image part
gpart add -i 1 -s "$(($ISOCYLS * $CYLSIZE * 512))b" -t \!4 $MD
# !2: root, for bootfs part.
gpart add -i 6 -s "$(($BOOTFSCYLS * $CYLSIZE * 512))b" -t \!2 $MD
mdconfig -d -u ${MD#md}
