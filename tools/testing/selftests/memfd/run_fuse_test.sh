#!/bin/sh

if test -d "./mnt" ; then
	fusermount -u ./mnt
	rmdir ./mnt
fi

set -e

mkdir mnt
./fuse_mnt ./mnt
./fuse_test ./mnt/memfd
fusermount -u ./mnt
rmdir ./mnt
