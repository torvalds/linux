#!/bin/sh
# $FreeBSD$

MD=34
TMP=/tmp/$$

set -e

# Start from the right directory so we can find all our data files.
cd `dirname $0`

(cd MdLoad && make) > /dev/null 2>&1

# Print the test header
echo -n '1..'
echo `ls -1 Data/disk.*.xml | wc -l`

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
		echo "Bail out!"
		echo "/dev/md$MD is busy"
		exit 1
	fi
	MdLoad/MdLoad md${MD} $f
	if [ -f Ref/$b ] ; then
		if diskinfo /dev/md${MD}* | 
		   diff -I '$FreeBSD' -u Ref/$b - > $TMP; then
			echo "ok - $b"
		else
			echo "not ok - $b" 
			sed 's/^/# /' $TMP
		fi
	else
		diskinfo /dev/md${MD}* > Ref/`basename $f`
	fi
done

mdconfig -d -u $MD > /dev/null 2>&1 || true
rm -f $TMP

exit 0
