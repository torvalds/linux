#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..1"

us0=$(attach_md -t malloc -s 1M) || exit 1
us1=$(attach_md -t malloc -s 2M) || exit 1
us2=$(attach_md -t malloc -s 3M) || exit 1

gmirror label $name /dev/$us0 /dev/$us1 /dev/$us2 || exit 1
devwait

# Size of created device should be 1MB - 512b.

size=`diskinfo /dev/mirror/${name} | awk '{print $3}'`

if [ $size -eq 1048064 ]; then
	echo "ok 1"
else
	echo "not ok 1"
fi
