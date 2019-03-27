#!/bin/sh
# $FreeBSD$

name="$(mktemp -u mirror.XXXXXX)"
class="mirror"
base=`basename $0`

gmirror_test_cleanup()
{
	[ -c /dev/$class/$name ] && gmirror destroy $name
	geom_test_cleanup
}
trap gmirror_test_cleanup ABRT EXIT INT TERM

syncwait()
{
	while $(gmirror status -s $name | grep -q SYNCHRONIZING); do
		sleep 0.1;
	done
}

consumerrefs()
{
	gclass=$1
	geom=$2

	if [ $# -ne 2 ]; then
		echo "Bad usage consumerrefs" >&2
		exit 1
	fi

	geom "${gclass}" list "${geom}" | \
	    grep -A5 ^Consumers | \
	    grep Mode | \
	    cut -d: -f2
}

disconnectwait()
{
	gclass=$1
	geom=$2

	if [ $# -ne 2 ]; then
		echo "Bad usage disconnectwait" >&2
		exit 1
	fi

	while [ $(consumerrefs "$gclass" "$geom") != r0w0e0 ]; do
		sleep 0.05
	done
}

. `dirname $0`/../geom_subr.sh
