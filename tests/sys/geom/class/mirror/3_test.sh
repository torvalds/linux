#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..5"

balance="round-robin"
ddbs=2048
nblocks1=1024
nblocks2=`expr $nblocks1 / \( $ddbs / 512 \)`
src=`mktemp $base.XXXXXX` || exit 1
dst=`mktemp $base.XXXXXX` || exit 1

dd if=/dev/random of=${src} bs=$ddbs count=$nblocks2 >/dev/null 2>&1

us0=$(attach_md -t malloc -s `expr $nblocks1 + 1`) || exit 1
us1=$(attach_md -t malloc -s `expr $nblocks1 + 1`) || exit 1
us2=$(attach_md -t malloc -s `expr $nblocks1 + 1`) || exit 1

gmirror label -b $balance $name /dev/${us0} /dev/${us1} /dev/${us2} || exit 1
devwait

dd if=${src} of=/dev/mirror/${name} bs=$ddbs count=$nblocks2 >/dev/null 2>&1

dd if=/dev/mirror/${name} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 1"
else
	echo "ok 1"
fi

gmirror remove $name ${us0}
dd if=/dev/mirror/${name} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 2"
else
	echo "ok 2"
fi

gmirror remove $name ${us1}
dd if=/dev/mirror/${name} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 3"
else
	echo "ok 3"
fi

gmirror remove $name ${us2}
dd if=/dev/mirror/${name} of=${dst} bs=$ddbs count=$nblocks2 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 4"
else
	echo "ok 4"
fi

# mirror/${name} should be removed.
if [ -c /dev/${name} ]; then
	echo "not ok 5"
else
	echo "ok 5"
fi

rm -f ${src} ${dst}
