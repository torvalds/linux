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

BLOCKSIZE=10240

if [ $# -ne 2 ]; then
  echo "make-memstick.sh /path/to/directory /path/to/image/file"
  exit 1
fi

tempfile="${2}.$$"

if [ ! -d ${1} ]; then
  echo "${1} must be a directory"
  exit 1
fi

if [ -e ${2} ]; then
  echo "won't overwrite ${2}"
  exit 1
fi

echo '/dev/da0s3 / ufs ro,noatime 1 1' > ${1}/etc/fstab
echo 'root_rw_mount="NO"' > ${1}/etc/rc.conf.local
rm -f ${tempfile}
makefs -B big -o version=2 ${tempfile} ${1}
rm ${1}/etc/fstab
rm ${1}/etc/rc.conf.local

mkimg -s apm \
    -p freebsd-boot:=${1}/boot/boot1.hfs \
    -p freebsd-ufs/FreeBSD_Install:=${tempfile} \
    -o ${2}

rm -f ${tempfile}

