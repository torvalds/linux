#!/bin/sh
#
# kernelcruft.sh
#
# Try to find *.c files in /sys which are orphaned
#
# $FreeBSD$

cd /sys/conf
cat files* | sed '
/^[ 	]*#/d
s/[ 	].*//
/^$/d
' | sort -u > /tmp/_0

cd /sys
find * -name '*.c' -print | sed '
/\/compile\//d
/^boot/d
' | sort -u > /tmp/_1

find * -name '*.[ch]' -print | xargs grep 'include.*c[>"]' > /tmp/_2

find * -name 'Makefile*' -print | xargs cat | sed '
/^	/d
s/:.*//
/^[ 	]*$/d
' > /tmp/_3

comm -13 /tmp/_0 /tmp/_1 | while read f
do
	b=`basename $f`
	if grep $b /tmp/_2 > /dev/null ; then
		# echo "2 $f"
		continue
	fi
	if grep $b /tmp/_3 > /dev/null ; then
		# echo "3 $f"
		continue
	fi
	echo $f
done

