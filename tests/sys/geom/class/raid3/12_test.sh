#!/bin/sh
# $FreeBSD$

. `dirname $0`/conf.sh

echo "1..1"

nblocks1=9
nblocks2=`expr $nblocks1 - 1`
nblocks3=`expr $nblocks2 / 2`

us0=$(attach_md -t malloc -s $nblocks1) || exit 1
us1=$(attach_md -t malloc -s $nblocks1) || exit 1
us2=$(attach_md -t malloc -s $nblocks1) || exit 1

dd if=/dev/random of=/dev/${us0} count=$nblocks1 >/dev/null 2>&1
dd if=/dev/random of=/dev/${us1} count=$nblocks1 >/dev/null 2>&1
dd if=/dev/random of=/dev/${us2} count=$nblocks1 >/dev/null 2>&1

graid3 label -w $name /dev/${us0} /dev/${us1} /dev/${us2} || exit 1
devwait
# Wait for synchronization.
sleep 2
graid3 stop $name
# Break one component.
dd if=/dev/random of=/dev/${us1} count=$nblocks2 >/dev/null 2>&1
# Provoke retaste of the rest components.
true > /dev/${us0}
true > /dev/${us2}
sleep 1

dd if=/dev/raid3/${name} of=/dev/null bs=1k count=$nblocks3 >/dev/null 2>&1
ec=$?
if [ $ec -eq 0 ]; then
	echo "not ok 1"
else
	echo "ok 1"
fi
