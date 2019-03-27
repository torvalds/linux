#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..2"

us0=$(attach_md -t malloc -s 1M) || exit 1
us1=$(attach_md -t malloc -s 2M) || exit 1
us2=$(attach_md -t malloc -s 3M) || exit 1

gshsec label $name /dev/${us0} /dev/${us1} /dev/${us2} 2>/dev/null || exit 1
devwait

# Size of created device should be 1MB - 512B.

mediasize=`diskinfo /dev/shsec/${name} | awk '{print $3}'`
if [ $mediasize -eq 1048064 ]; then
	echo "ok 1"
else
	echo "not ok 1"
fi
sectorsize=`diskinfo /dev/shsec/${name} | awk '{print $2}'`
if [ $sectorsize -eq 512 ]; then
	echo "ok 2"
else
	echo "not ok 2"
fi
