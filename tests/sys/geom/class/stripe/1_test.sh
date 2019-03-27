#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..1"

us0=$(attach_md -t malloc -s 1M) || exit 1
us1=$(attach_md -t malloc -s 2M) || exit 1
us2=$(attach_md -t malloc -s 3M) || exit 1

gstripe create -s 16384 $name /dev/$us0 /dev/$us1 /dev/$us2 || exit 1
devwait

# Size of created device should be 1MB * 3.

size=`diskinfo /dev/stripe/${name} | awk '{print $3}'`

if [ $size -eq 3145728 ]; then
	echo "ok 1"
else
	echo "not ok 1"
fi
