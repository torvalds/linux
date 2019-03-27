#!/bin/sh
# $FreeBSD$

name="$(mktemp -u shsec.XXXXXX)"
class="shsec"
base=`basename $0`

shsec_test_cleanup()
{
	[ -c /dev/$class/$name ] && gshsec stop $name
	geom_test_cleanup
}
trap shsec_test_cleanup ABRT EXIT INT TERM

. `dirname $0`/../geom_subr.sh
