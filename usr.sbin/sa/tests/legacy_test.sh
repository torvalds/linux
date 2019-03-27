#!/bin/sh
#
# $FreeBSD$
#

DIR=`dirname $0`
LCDIR=`dirname $0`/../../usr.bin/lastcomm
ARCH=`uname -m`

collapse_whitespace()
{
	sed -E 's,[ 	]+, ,g'
}

check()
{
	NUM=$1
	shift
	collapse_whitespace | \
	if diff -q - $1
	then
		echo "ok $NUM"
	else
		echo "not ok $NUM"
	fi
}

install -c -m 644 $LCDIR/v1-$ARCH-acct.in v1-$ARCH-acct.in
install -c -m 644 $LCDIR/v2-$ARCH-acct.in v2-$ARCH-acct.in

echo 1..13

# Command listings of the two acct versions
sa -u v1-$ARCH-acct.in | check 1 $DIR/v1-$ARCH-u.out
sa -u v2-$ARCH-acct.in | check 2 $DIR/v2-$ARCH-u.out

# Plain summaries of user/process
sa -i v1-$ARCH-acct.in | check 3 $DIR/v1-$ARCH-sav.out
sa -im v1-$ARCH-acct.in | check 4 $DIR/v1-$ARCH-usr.out

# Backward compatibility of v1 summary files
sa -P $DIR/v1-$ARCH-sav.in -U $DIR/v1-$ARCH-usr.in /dev/null |
	check 5 $DIR/v1-$ARCH-sav.out
sa -m -P $DIR/v1-$ARCH-sav.in -U $DIR/v1-$ARCH-usr.in /dev/null |
	check 6 $DIR/v1-$ARCH-usr.out

# Convert old summary format to new 
install -c -m 644 $DIR/v1-$ARCH-sav.in v2c-$ARCH-sav.in
install -c -m 644 $DIR/v1-$ARCH-usr.in v2c-$ARCH-usr.in
sa -s -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in /dev/null >/dev/null
sa -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in /dev/null |
	check 7 $DIR/v1-$ARCH-sav.out
sa -m -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in /dev/null |
	check 8 $DIR/v1-$ARCH-usr.out

# Reading v2 summary files
sa -P $DIR/v2-$ARCH-sav.in -U $DIR/v2-$ARCH-usr.in /dev/null |
	check 9 $DIR/v1-$ARCH-sav.out
sa -m -P $DIR/v2-$ARCH-sav.in -U $DIR/v2-$ARCH-usr.in /dev/null |
	check 10 $DIR/v1-$ARCH-usr.out

# Summarize
sa -is -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in v1-$ARCH-acct.in >/dev/null
sa -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in /dev/null |
	check 11 $DIR/v1-$ARCH-sav.out
sa -m -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in /dev/null |
	check 12 $DIR/v1-$ARCH-usr.out

# Accumulate
install -c -m 644 $LCDIR/v1-$ARCH-acct.in v1-$ARCH-acct.in
sa -is -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in v1-$ARCH-acct.in >/dev/null
install -c -m 644 $LCDIR/v1-$ARCH-acct.in v1-$ARCH-acct.in
sa -s -P v2c-$ARCH-sav.in -U v2c-$ARCH-usr.in v1-$ARCH-acct.in \
    | collapse_whitespace >double
cp $LCDIR/v1-$ARCH-acct.in v1-$ARCH-acct.in
sa -i v1-$ARCH-acct.in v1-$ARCH-acct.in | check 13 double

exit 0
