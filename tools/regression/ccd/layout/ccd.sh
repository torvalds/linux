#!/bin/sh
# $FreeBSD$

set -e

if mount | grep ccd3 ; then
	echo "ccd3 seems to be mounted"
	exit 1
fi

(
ccdconfig -u ccd3 || true
mdconfig -d -u 90 || true
mdconfig -d -u 91 || true
mdconfig -d -u 92 || true
mdconfig -d -u 93 || true
) > /dev/null 2>&1

mdconfig -a -t malloc -s $1 -u 90
mdconfig -a -t malloc -s $2 -u 91
mdconfig -a -t malloc -s $3 -u 92
mdconfig -a -t malloc -s $4 -u 93

ccdconfig -v ccd3 $5 $6 /dev/md90 /dev/md91 /dev/md92 /dev/md93
./a > /dev/ccd3

md5 < /dev/md90
md5 < /dev/md91
md5 < /dev/md92
md5 < /dev/md93

(
./b < /dev/md90 | sed -e 1,16d -e 's/^/md90	/'
./b < /dev/md91 | sed -e 1,16d -e 's/^/md91	/'
./b < /dev/md92 | sed -e 1,16d -e 's/^/md92	/'
./b < /dev/md93 | sed -e 1,16d -e 's/^/md93	/'
) | sort +2n | awk '
	{
	if ($1 != l1) {
		if (l1 != "") {
			if (l1 == "md90") printf ""
			if (l1 == "md91") printf "		"
			if (l1 == "md92") printf "				"
			if (l1 == "md93") printf "						"
			print l3,l2,l
		}
		l1 = $1
		l2 = $2
		l3 = $3
		l = 0;
	}
	l++;
	}
END	{
	if (l1 == "md90") printf ""
	if (l1 == "md91") printf "		"
	if (l1 == "md92") printf "				"
	if (l1 == "md93") printf "						"
	print l3,l2,l
	}
'
