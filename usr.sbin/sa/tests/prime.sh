#!/bin/sh
#
# Configure and run this script to create the files for regression testing
# for a new architecture/configuration.
#
# $FreeBSD$
#

TZ=UTC; export TZ

# Set this to the path of the current sa command
SANEW=/usr/sbin/sa

# Set this to the path of the sa as of 2007-05-19.
# You can obtain it with a command like:
# cvs co -D '2007-05-19' sa
# To compile it you will also need sys/acct.h from that date
# and sa configured to use that file, instead of the current version.
SAOLD=/$HOME/src/sa/sa

# Machine architecture
ARCH=`uname -m`

# Location of lastcomm regression files
LCDIR=../../usr.bin/lastcomm

$SANEW -u $LCDIR/v1-$ARCH-acct.in >v1-$ARCH-u.out
$SANEW -u $LCDIR/v2-$ARCH-acct.in >v2-$ARCH-u.out
$SANEW -i $LCDIR/v1-$ARCH-acct.in >v1-$ARCH-sav.out
$SANEW -im $LCDIR/v1-$ARCH-acct.in >v1-$ARCH-usr.out
cp $LCDIR/v1-$ARCH-acct.in acct.in
rm -f v1-$ARCH-sav.in v1-$ARCH-usr.in
$SAOLD -s -P v1-$ARCH-sav.in -U v1-$ARCH-usr.in acct.in >/dev/null
cp $LCDIR/v1-$ARCH-acct.in acct.in
rm -f v2-$ARCH-sav.in v2-$ARCH-usr.in
$SANEW -s -P v2-$ARCH-sav.in -U v2-$ARCH-usr.in acct.in >/dev/null
rm acct.in
