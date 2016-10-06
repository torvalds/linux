#!/bin/sh
# Runs bitmap infrastructure tests using test_bitmap kernel module

if /sbin/modprobe -q test_bitmap; then
	/sbin/modprobe -q -r test_bitmap
	echo "bitmap: ok"
else
	echo "bitmap: [FAIL]"
	exit 1
fi
