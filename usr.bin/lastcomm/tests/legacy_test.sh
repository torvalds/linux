#!/bin/sh
#
# $FreeBSD$
#

DIR=`dirname $0`
ARCH=`uname -m`

TZ=UTC; export TZ

check()
{
	NUM=$1
	shift
	# Remove tty field, which varies between systems.
	awk '{$4 = ""; print}' |
	if diff -a - $1 >&2
	then
		echo "ok $NUM"
	else
		echo "not ok $NUM"
	fi
}


cat $DIR/v1-$ARCH-acct.in $DIR/v2-$ARCH-acct.in >v1v2-$ARCH-acct.in
cat $DIR/v2-$ARCH.out $DIR/v1-$ARCH.out >v1v2-$ARCH.out

echo 1..6

lastcomm -cesuS -f $DIR/v1-$ARCH-acct.in | check 1 $DIR/v1-$ARCH.out
lastcomm -cesuS -f - <$DIR/v1-$ARCH-acct.in | tail -r | check 2 $DIR/v1-$ARCH.out
lastcomm -cesuS -f $DIR/v2-$ARCH-acct.in | check 3 $DIR/v2-$ARCH.out
lastcomm -cesuS -f - <$DIR/v2-$ARCH-acct.in | tail -r | check 4 $DIR/v2-$ARCH.out
lastcomm -cesuS -f v1v2-$ARCH-acct.in | check 5 v1v2-$ARCH.out
lastcomm -cesuS -f - <v1v2-$ARCH-acct.in | tail -r | check 6 v1v2-$ARCH.out

exit 0
