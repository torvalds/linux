#!/bin/sh -e

if ! [ -x ../lklfuse ]; then
    echo "lklfuse not available, skipping tests"
    exit 0
fi

if ! [ -e /dev/fuse ]; then
    echo "fuse not available, skipping tests"
    exit 0
fi

if [ "$1" = "-t" ]; then
    shift
    fstype=$1
    shift
fi

if [ -z "$fstype" ]; then
    fstype="ext4"
fi

file=`mktemp`
dir=`mktemp -d`

cleanup()
{
    cd $olddir
    sleep 1
    fusermount -u $dir
    rm $file
    rmdir $dir
}

basic()
{
    touch a
    if ! [ -e ]; then exit 1; fi
    rm a
    mkdir a
    if ! [ -d ]; then exit 1; fi
    rmdir a
}

trap cleanup EXIT

olddir=`pwd`

# create empty filesystem
dd if=/dev/zero of=$file bs=1024 seek=500000 count=1
yes | mkfs.$fstype $file

# mount with fuse
../lklfuse $file $dir -o type=$fstype

cd $dir

# run basic tests
basic

# run stress-ng
if which stress-ng; then
    if [ "$fstype" = "vfat" ]; then
	exclude="chmod,filename,link,mknod,symlink,xattr"
    fi
    stress-ng --class filesystem --all 0 --timeout 10 \
	      --exclude fiemap,$exclude --fallocate-bytes 10m \
	      --sync-file-bytes 10m
else
    echo "could not find stress-ng, skipping"
fi
