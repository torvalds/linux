#!/bin/sh
# $FreeBSD$

# Regression test for r317712.

. `dirname $0`/conf.sh

echo 1..1

ddbs=2048
m1=`mktemp $base.XXXXXX` || exit 1
m2=`mktemp $base.XXXXXX` || exit 1

dd if=/dev/zero of=$m1 bs=$ddbs count=1024 >/dev/null 2>&1
dd if=/dev/zero of=$m2 bs=$ddbs count=1024 >/dev/null 2>&1

us0=$(mdconfig -t vnode -f $m1) || exit 1
us1=$(mdconfig -t vnode -f $m2) || exit 1

gmirror label $name /dev/$us0 /dev/$us1 || exit 1
devwait

# Ensure that the mirrors are marked dirty, and then disconnect them.
# We need to have the gmirror provider open when destroying the MDs since
# gmirror will automatically mark the mirrors clean when the provider is closed.
exec 9>/dev/mirror/$name
dd if=/dev/zero bs=$ddbs count=1 >&9 2>/dev/null
mdconfig -d -u ${us0#md} -o force || exit 1
mdconfig -d -u ${us1#md} -o force || exit 1
exec 9>&-

dd if=/dev/random of=$m1 bs=$ddbs count=1 conv=notrunc >/dev/null 2>&1
us0=$(attach_md -t vnode -f $m1) || exit 1
devwait # This will take kern.geom.mirror.timeout seconds.

# Re-attach the second mirror and wait for it to synchronize.
us1=$(attach_md -t vnode -f $m2) || exit 1
syncwait

# Verify the two mirrors are identical. Destroy the gmirror first so that
# the mirror metadata is wiped; otherwise the metadata blocks will fail
# the comparison. It would be nice to do this with a "gmirror verify"
# command instead.
gmirror destroy $name
if cmp -s ${m1} ${m2}; then
	echo "ok 1"
else
	echo "not ok 1"
fi

rm -f $m1 $m2
