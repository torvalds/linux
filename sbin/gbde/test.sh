#!/bin/sh
# $FreeBSD$

set -e

MD=99
mdconfig -d -u $MD > /dev/null 2>&1 || true

mdconfig -a -t malloc -s 1m -u $MD

D=/dev/md$MD

./gbde init $D -P foo -L /tmp/_l1
./gbde setkey $D -p foo -l /tmp/_l1 -P bar -L /tmp/_l1
./gbde setkey $D -p bar -l /tmp/_l1 -P foo -L /tmp/_l1

./gbde setkey $D -p foo  -l /tmp/_l1 -n 2 -P foo2 -L /tmp/_l2
./gbde setkey $D -p foo2 -l /tmp/_l2 -n 3 -P foo3 -L /tmp/_l3
./gbde setkey $D -p foo3 -l /tmp/_l3 -n 4 -P foo4 -L /tmp/_l4
./gbde setkey $D -p foo4 -l /tmp/_l4 -n 1 -P foo1 -L /tmp/_l1

./gbde nuke $D -p foo1 -l /tmp/_l1 -n 4
if ./gbde nuke $D -p foo4 -l /tmp/_l4 -n 3 ; then false ; fi
./gbde destroy $D -p foo2 -l /tmp/_l2
if ./gbde destroy $D -p foo2 -l /tmp/_l2 ; then false ; fi

./gbde nuke $D -p foo1 -l /tmp/_l1 -n -1
if ./gbde nuke $D -p foo1 -l /tmp/_l1 -n -1 ; then false ; fi
if ./gbde nuke $D -p foo2 -l /tmp/_l2 -n -1 ; then false ; fi
if ./gbde nuke $D -p foo3 -l /tmp/_l3 -n -1 ; then false ; fi
if ./gbde nuke $D -p foo4 -l /tmp/_l4 -n -1 ; then false ; fi

rm -f /tmp/_l1 /tmp/_l2 /tmp/_l3 /tmp/_l4

./gbde init $D -P foo 
./gbde setkey $D -p foo -P bar
./gbde setkey $D -p bar -P foo

./gbde setkey $D -p foo  -n 2 -P foo2
./gbde setkey $D -p foo2 -n 3 -P foo3
./gbde setkey $D -p foo3 -n 4 -P foo4
./gbde setkey $D -p foo4 -n 1 -P foo1

mdconfig -d -u $MD

mdconfig -a -t malloc -s 1m -u $MD
if [ -f image.uu ] ; then
	uudecode -p image.uu | bzcat > $D
else
	uudecode -p ${1}/image.uu | bzcat > $D
fi

if [ `md5 < $D` != "a4066a739338d451b919e63f9ee4a12c" ] ; then
	echo "Failed to set up md(4) device correctly"
	exit 2
fi

./gbde attach $D -p foo
fsck_ffs ${D}.bde
./gbde detach $D
mdconfig -d -u $MD


echo "***********"
echo "Test passed"
echo "***********"
exit 0
