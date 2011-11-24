#!/bin/sh

bindir=$WORKAREA/host/.output/$ATH_PLATFORM/image

$bindir/mboxping --delay -v -i  $NETIF  -t $1  -r $2  -s $3  -c 4



