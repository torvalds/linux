#!/bin/sh
# $FreeBSD$

# Regression test for r306743.

. `dirname $0`/conf.sh

echo 1..1

ddbs=2048
m1=`mktemp $base.XXXXXX` || exit 1
m2=`mktemp $base.XXXXXX` || exit 1
m3=`mktemp $base.XXXXXX` || exit 1

dd if=/dev/zero of=$m1 bs=$ddbs count=1024 >/dev/null 2>&1
dd if=/dev/zero of=$m2 bs=$ddbs count=1024 >/dev/null 2>&1
dd if=/dev/zero of=$m3 bs=$ddbs count=1024 >/dev/null 2>&1

us0=$(attach_md -t vnode -f $m1) || exit 1
us1=$(attach_md -t vnode -f $m2) || exit 1
us2=$(attach_md -t vnode -f $m3) || exit 1

gmirror label $name /dev/$us0 /dev/$us1 || exit 1
devwait

# Break one of the mirrors by forcing a single metadata write error.
# When dd closes the mirror provider, gmirror will attempt to mark the mirrors
# clean, and will kick one of the mirrors out upon hitting the error.
sysctl debug.fail_point.g_mirror_metadata_write='1*return(5)' || exit 1
dd if=/dev/random of=/dev/mirror/$name bs=$ddbs count=1 >/dev/null 2>&1
sysctl debug.fail_point.g_mirror_metadata_write='off' || exit 1

# Replace the broken mirror, and then stop the gmirror.
gmirror forget $name || exit 1
gmirror insert $name /dev/$us2 || exit 1
syncwait
gmirror stop $name || exit 1

# Restart the gmirror on the original two mirrors. One of them is broken,
# so we should end up with a degraded gmirror.
gmirror activate $name /dev/$us0 /dev/$us1 || exit 1
devwait
dd if=/dev/random of=/dev/mirror/$name bs=$ddbs count=1 >/dev/null 2>&1

# Re-add the replacement mirror and verify the two mirrors are synchronized.
# Destroy the gmirror first so that the mirror metadata is wiped; otherwise
# the metadata blocks will fail the comparison. It would be nice to do this
# with a "gmirror verify" command instead.
gmirror activate $name /dev/$us2 || exit 1
syncwait
gmirror destroy $name || exit 1
if cmp -s $m1 $m3; then
	echo "ok 1"
else
	echo "not ok 1"
fi

rm -f $m1 $m2 $m3
