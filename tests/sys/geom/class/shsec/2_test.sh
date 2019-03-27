#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..4"

nblocks1=1024
nblocks2=`expr $nblocks1 + 1`
src=`mktemp $base.XXXXXX` || exit 1
dst=`mktemp $base.XXXXXX` || exit 1

dd if=/dev/random of=${src} count=$nblocks1 >/dev/null 2>&1

us0=$(attach_md -t malloc -s $nblocks2) || exit 1
us1=$(attach_md -t malloc -s $nblocks2) || exit 1
us2=$(attach_md -t malloc -s $nblocks2) || exit 1

gshsec label $name /dev/$us0 /dev/$us1 /dev/$us2 || exit 1
devwait

dd if=${src} of=/dev/shsec/${name} count=$nblocks1 >/dev/null 2>&1

dd if=/dev/shsec/${name} of=${dst} count=$nblocks1 >/dev/null 2>&1
if [ `md5 -q ${src}` != `md5 -q ${dst}` ]; then
	echo "not ok 1"
else
	echo "ok 1"
fi

dd if=/dev/${us0} of=${dst} count=$nblocks1 >/dev/null 2>&1
if [ `md5 -q ${src}` = `md5 -q ${dst}` ]; then
	echo "not ok 2"
else
	echo "ok 2"
fi

dd if=/dev/${us1} of=${dst} count=$nblocks1 >/dev/null 2>&1
if [ `md5 -q ${src}` = `md5 -q ${dst}` ]; then
	echo "not ok 3"
else
	echo "ok 3"
fi

dd if=/dev/${us2} of=${dst} count=$nblocks1 >/dev/null 2>&1
if [ `md5 -q ${src}` = `md5 -q ${dst}` ]; then
	echo "not ok 4"
else
	echo "ok 4"
fi

rm -f ${src} ${dst}
