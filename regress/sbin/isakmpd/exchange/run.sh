#!/bin/sh
#	$OpenBSD: run.sh,v 1.1 2005/04/08 17:12:49 cloder Exp $
#	$EOM: run.sh,v 1.6 1999/08/05 15:02:33 niklas Exp $

#
# Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# This code was written under funding by Ericsson Radio Systems.
#

# Defaults
SRCPORT=1500
DSTPORT=1501
FIFO=test.fifo
TIMEOUT=2

NC=${NC:-/usr/bin/nc}
ISAKMPD=${ISAKMPD:-/sbin/isakmpd}

progname=`basename $0`
indent=`echo -n $progname |sed 's/./ /g'`
seed=980801
initiator=yes
retval=0
verbose=no
clean=yes

usage ()
{
  echo "usage: $progname [-nrv] [-d dst-port] [-f fifo] [-s src-port]" >&2
  echo "       $indent [-t timeout] testsuite" >&2
  exit 2
}

set -- `getopt d:f:nrs:t:v $*`
if [ $? != 0 ]; then
  usage
fi
for i; do
  case "$i" in
  -d)
    DSTPORT=$2; shift; shift;;
  -f)
    FIFO=$2; shift; shift;;
  -n)
    clean=no; shift;;
  -r)
    initiator=no; shift;;
  -s)
    SRCPORT=$2; shift; shift;;
  -t)
    TIMEOUT=$2; shift; shift;;
  -v)
    verbose=yes; shift;;
  --)
    shift; break;;
  esac
done

if [ $# -eq 1 ]; then
  suite=$1
else
  usage
fi

[ ${verbose} = yes ] && set -x

# Start isakmpd and wait for the fifo to get created
echo Removing ${FIFO}
rm -f ${FIFO}
${ISAKMPD} -d -p${SRCPORT} -f${FIFO} -r${seed} &
isakmpd_pid=$!
echo isakmpd pid is $isakmpd_pid
trap 'echo Got signal; kill $isakmpd_pid; echo Removing ${FIFO}; rm -f ${FIFO}; [ $clean = yes ] && rm -f packet' 1 2 15
while [ ! -p ${FIFO} ]; do
  sleep 1
done

# Start the exchange
if [ $initiator = yes ]; then
  ${NC} -nul -w${TIMEOUT} 127.0.0.1 ${DSTPORT} </dev/null >packet &
#  ${NC} -nu -w${TIMEOUT} -p${SRCPORT} 127.0.0.1 ${DSTPORT} </dev/null >packet
  sleep 1
  echo "c udp 127.0.0.1:${DSTPORT} 2 1" >${FIFO}
  in_packets=`ls ${suite}-i.* 2>/dev/null`
  out_packets=`ls ${suite}-r.* 2>/dev/null`
else
  in_packets=`ls ${suite}-r.* 2>/dev/null`
  out_packets=`ls ${suite}-i.* 2>/dev/null`
fi
his_turn=$initiator
while [ \( $his_turn = yes -a X"$in_packets" != X \) \
        -o \( $his_turn = no -a X"$out_packets" != X \) ]; do
  if [ $his_turn = no ]; then
    set $out_packets
    packet=$1
    shift
    out_packets=$*
    cat $packet |${NC} -nu -w${TIMEOUT} -p${SRCPORT} 127.0.0.1 ${DSTPORT} \
      >packet
    my_turn=no
  else
    set $in_packets
    packet=$1
    shift
    in_packets=$*
    if ! cmp $packet packet 2>/dev/null; then
      retval=1
      break
    fi
    my_turn=yes
  fi
done
kill $isakmpd_pid
rm -f ${FIFO}
[ $clean = yes ] && rm -f packet
exit $retval
