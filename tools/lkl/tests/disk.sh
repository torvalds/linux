#!/usr/bin/env bash

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

source $script_dir/test.sh

function prepfs()
{
    set -e

    file=`mktemp`

    dd if=/dev/zero of=$file bs=1024 count=102400

    yes | mkfs.$1 $file

    if ! [ -z $ANDROID_WDIR ]; then
        adb shell mkdir -p $ANDROID_WDIR
        adb push $file $ANDROID_WDIR
        rm $file
        file=$ANDROID_WDIR/$(basename $file)
    fi
    if ! [ -z $BSD_WDIR ]; then
        $MYSSH mkdir -p $BSD_WDIR
        ssh_copy $file $BSD_WDIR
        rm $file
        file=$BSD_WDIR/$(basename $file)
    fi

    export_vars file
}

function cleanfs()
{
    set -e

    if ! [ -z $ANDROID_WDIR ]; then
        adb shell rm $1
        adb shell rm $ANDROID_WDIR/disk
    elif ! [ -z $BSD_WDIR ]; then
        $MYSSH rm $1
        $MYSSH rm $BSD_WDIR/disk
    else
        rm $1
    fi
}

if [ "$1" = "-t" ]; then
    shift
    fstype=$1
    shift
fi

if [ -z "$fstype" ]; then
    fstype="ext4"
fi

if [ -z $(which mkfs.$fstype) ]; then
    lkl_test_plan 0 "disk $fstype"
    echo "no mkfs.$fstype command"
    exit 0
fi

lkl_test_plan 1 "disk $fstype"
lkl_test_run 1 prepfs $fstype
lkl_test_exec $script_dir/disk -d $file -t $fstype $@
lkl_test_plan 1 "disk $fstype"
lkl_test_run 1 cleanfs $file

