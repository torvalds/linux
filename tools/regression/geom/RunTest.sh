#!/bin/sh
# $FreeBSD$

MD=34
TMP=/tmp/$$

set -e

r=0

(cd MdLoad && make) > /dev/null 2>&1

for f in Data/disk.*.xml
do
	b=`basename $f`
	mdconfig -d -u $MD > /dev/null 2>&1 || true
	if [ -c /dev/md$MD ] ; then
		sleep 1
	fi
	if [ -c /dev/md$MD ] ; then
		sleep 1
	fi
	if [ -c /dev/md$MD ] ; then
		echo "/dev/md$MD is busy" 1>&2
		exit 1
	fi
	MdLoad/MdLoad md${MD} $f
	if [ -f Ref/$b ] ; then
		if diskinfo /dev/md${MD}* | 
		   diff -I '$FreeBSD' -u Ref/$b - > $TMP; then
			echo "PASSED: $b"
		else
			echo "FAILED: $b" 
			sed 's/^/	/' $TMP
			r=2;
		fi
	else
		diskinfo /dev/md${MD}* > Ref/`basename $f`
	fi
done

mdconfig -d -u $MD > /dev/null 2>&1 || true
rm -f $TMP
exit $r
