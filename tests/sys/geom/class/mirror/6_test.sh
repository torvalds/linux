#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..2"

balance="split"
ddbs=8192
nblocks1=1024
nblocks2=`expr $nblocks1 / \( $ddbs / 512 \)`
src=`mktemp $base.XXXXXX` || exit 1
dst=`mktemp $base.XXXXXX` || exit 1

dd if=/dev/random of=${src} bs=$ddbs count=$nblocks2 >/dev/null 2>&1

us0=$(attach_md -t malloc -s `expr $nblocks1 + 1`) || exit 1
us1=$(attach_md -t malloc -s `expr $nblocks1 + 1`) || exit 1
us2=$(attach_md -t malloc -s `expr $nblocks1 + 1`) || exit 1

gmirror label -b $balance -s `expr $ddbs / 2` $name /dev/${us0} /dev/${us1} || exit 1
devwait

dd if=${src} of=/dev/mirror/${name} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
dd if=/dev/zero of=/dev/${us2} bs=$ddbs count=$nblocks2 >/dev/null 2>&1

dd if=/dev/mirror/${name} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 1"
else
	echo "ok 1"
fi

# Connect disk to the mirror.
gmirror insert ${name} ${us2}
# Wait for synchronization.
sleep 1
dd if=/dev/${us2} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 2"
else
	echo "ok 2"
fi

rm -f ${src} ${dst}
