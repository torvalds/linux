#!/bin/sh
#
# This script generates a "memstick image" (image that can be copied to a
# USB memory stick) from a directory tree.  Note that the script does not
# clean up after itself very well for error conditions on purpose so the
# problem can be diagnosed (full filesystem most likely but ...).
#
# Usage: make-memstick.sh <directory tree> <image filename>
#
# $FreeBSD$
#

set -e

PATH=/bin:/usr/bin:/sbin:/usr/sbin
export PATH

scriptdir=$(dirname $(realpath $0))
. ${scriptdir}/../../tools/boot/install-boot.sh

if [ $# -ne 2 ]; then
	echo "make-memstick.sh /path/to/directory /path/to/image/file"
	exit 1
fi

if [ ! -d ${1} ]; then
	echo "${1} must be a directory"
	exit 1
fi

if [ -e ${2} ]; then
	echo "won't overwrite ${2}"
	exit 1
fi

echo '/dev/ufs/FreeBSD_Install / ufs ro,noatime 1 1' > ${1}/etc/fstab
echo 'root_rw_mount="NO"' > ${1}/etc/rc.conf.local
makefs -B little -o label=FreeBSD_Install -o version=2 ${2}.part ${1}
rm ${1}/etc/fstab
rm ${1}/etc/rc.conf.local

# Make an ESP in a file.
espfilename=$(mktemp /tmp/efiboot.XXXXXX)
make_esp_file ${espfilename} ${fat32min} ${1}/boot/loader.efi

mkimg -s gpt \
    -p efi:=${espfilename} \
    -p freebsd:=${2}.part \
    -o ${2}
rm ${espfilename}
rm ${2}.part

