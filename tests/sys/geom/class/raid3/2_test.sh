#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..1"

ddbs=2048
nblocks1=1024
nblocks2=`expr $nblocks1 / \( $ddbs / 512 \)`
src=`mktemp $base.XXXXXX` || exit 1
dst=`mktemp $base.XXXXXX` || exit 1

us0=$(attach_md -t malloc -s $(expr $nblocks1 + 1)) || exit 1
us1=$(attach_md -t malloc -s $(expr $nblocks1 + 1)) || exit 1
us2=$(attach_md -t malloc -s $(expr $nblocks1 + 1)) || exit 1

dd if=/dev/random of=${src} bs=$ddbs count=$nblocks2 >/dev/null 2>&1

graid3 label $name /dev/${us0} /dev/${us1} /dev/${us2} || exit 1
devwait

dd if=${src} of=/dev/raid3/${name} bs=$ddbs count=$nblocks2 >/dev/null 2>&1

dd if=/dev/raid3/${name} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 1"
else
	echo "ok 1"
fi

rm -f ${src} ${dst}
