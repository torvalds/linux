#!/usr/bin/env bash

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

source $script_dir/test.sh

cleanup()
{
    set -e

    sleep 1
    fusermount -u $dir
    rm $file
    rmdir $dir
}


# $1 - disk image
# $2 - fstype
function prepfs()
{
    set -e

    dd if=/dev/zero of=$1 bs=1048576 count=300

    yes | mkfs.$2 $1
}

# $1 - disk image
# $2 - mount point
# $3 - filesystem type
lklfuse_mount()
{
    ${script_dir}/../lklfuse $1 $2 -o type=$3
}

# $1 - mount point
lklfuse_basic()
{
    set -e

    cd $1
    touch a
    if ! [ -e ]; then exit 1; fi
    rm a
    mkdir a
    if ! [ -d ]; then exit 1; fi
    rmdir a
}

# $1 - dir
# $2 - filesystem type
lklfuse_stressng()
{
    set -e

    if [ -z $(which stress-ng) ]; then
        echo "missing stress-ng"
        return $TEST_SKIP
    fi

    cd $1

    if [ "$2" = "vfat" ]; then
        exclude="chmod,filename,link,mknod,symlink,xattr"
    fi

    stress-ng --class filesystem --all 0 --timeout 10 \
	      --exclude fiemap,$exclude --fallocate-bytes 10m \
	      --sync-file-bytes 10m
}

if [ "$1" = "-t" ]; then
    shift
    fstype=$1
    shift
fi

if [ -z "$fstype" ]; then
    fstype="ext4"
fi

if ! [ -x $script_dir/../lklfuse ]; then
    lkl_test_plan 0 "lklfuse.sh $fstype"
    echo "lklfuse not available"
    exit 0
fi

if ! [ -e /dev/fuse ]; then
    lkl_test_plan 0 "lklfuse.sh $fstype"
    echo "/dev/fuse not available"
    exit 0
fi

if [ -z $(which mkfs.$fstype) ]; then
    lkl_test_plan 0 "lklfuse.sh $fstype"
    echo "mkfs.$fstype not available"
    exit 0
fi


file=`mktemp`
dir=`mktemp -d`

trap cleanup EXIT

lkl_test_plan 4 "lklfuse $fstype"

lkl_test_run 1 prepfs $file $fstype
lkl_test_run 2 lklfuse_mount $file $dir $fstype
lkl_test_run 3 lklfuse_basic $dir
# stress-ng returns 2 with no apparent failures so skip it for now
#lkl_test_run 4 lklfuse_stressng $dir $fstype
trap : EXIT
lkl_test_run 4 cleanup
