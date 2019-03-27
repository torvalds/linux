#!/bin/csh
#
# $FreeBSD$
#
# Run tcpp -s -p 8 on the server, then this on the client.
#
# Note awkwardly hard-coded IP address below.
#
# Accepts two arguments: [filename] [csvprefix]
#

set totalbytes=4800000		# Bytes per connection
set cores=8
set trials=6
set ptcps=24			# Max TCPs concurrently
set ntcps=240			# Total TCPs over test
set nips=4			# Number of local IP addresses to use
set baseip=192.168.100.200	# First IP address to use

foreach core (`jot $cores`)
  foreach trial (`jot $trials`)
    set mflag=`echo $ptcps / $core | bc`
    set tflag=`echo $ntcps / $core | bc`
    echo -n $2,${core},${trial}, >> $1
    ./tcpp -c 192.168.100.102 -p $core -b $totalbytes -m $mflag \
      -t $tflag -M $nips -l $baseip >> $1
  end
end
