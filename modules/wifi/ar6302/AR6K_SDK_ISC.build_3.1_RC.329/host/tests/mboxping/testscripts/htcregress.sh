#!/bin/sh

duration=$1
bindir=$WORKAREA/host/.output/$ATH_PLATFORM/image

flags="-q -v --random --delay"

if [ ! -x "$bindir/mboxping" ]; then	
	echo "$bindir/mboxping not found or executable!"
	exit -1
fi

echo "HTC regression test starting..."
date
$bindir/mboxping $flags -i $NETIF -t 0 -r 0 -s 1530  -p 3  -d $duration &
$bindir/mboxping $flags -i $NETIF -t 1 -r 1 -s 1530  -p 3 -d $duration &
$bindir/mboxping $flags -i $NETIF -t 2 -r 2 -s 1530  -p 3 -d $duration &
$bindir/mboxping $flags -i $NETIF -t 3 -r 3 -s 1530  -p 3 -d $duration &

#launch finisher
$WORKAREA/host/tests/mboxping/testscripts/post_htcregress.sh  $duration  &



