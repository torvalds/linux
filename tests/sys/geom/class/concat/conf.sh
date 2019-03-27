#!/bin/sh
# $FreeBSD$

name="$(mktemp -u concat.XXXXXX)"
class="concat"
base=`basename $0`

gconcat_test_cleanup()
{
	[ -c /dev/$class/$name ] && gconcat destroy $name
	geom_test_cleanup
}
trap gconcat_test_cleanup ABRT EXIT INT TERM

. `dirname $0`/../geom_subr.sh
