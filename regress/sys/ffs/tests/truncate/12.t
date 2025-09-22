#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/truncate/12.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns EFBIG or EINVAL if the length argument was greater than the maximum file size"

n0=`namegen`

expect 0 create ${n0} 0644
r=`${FSTEST} truncate ${n0} 999999999999999`
case "${r}" in
EFBIG|EINVAL)
	expect 0 stat ${n0} size
	;;
0)
	expect 999999999999999 stat ${n0} size
	;;
*)
	echo "not ok ${ntest}"
	ntest=$((ntest + 1))
	;;
esac
expect 0 unlink ${n0}
