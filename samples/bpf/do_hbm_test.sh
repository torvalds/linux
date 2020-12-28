#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Facebook
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of version 2 of the GNU General Public
# License as published by the Free Software Foundation.

Usage() {
  echo "Script for testing HBM (Host Bandwidth Manager) framework."
  echo "It creates a cgroup to use for testing and load a BPF program to limit"
  echo "egress or ingress bandwidht. It then uses iperf3 or netperf to create"
  echo "loads. The output is the goodput in Mbps (unless -D was used)."
  echo ""
  echo "USAGE: $name [out] [-b=<prog>|--bpf=<prog>] [-c=<cc>|--cc=<cc>]"
  echo "             [-D] [-d=<delay>|--delay=<delay>] [--debug] [-E] [--edt]"
  echo "             [-f=<#flows>|--flows=<#flows>] [-h] [-i=<id>|--id=<id >]"
  echo "             [-l] [-N] [--no_cn] [-p=<port>|--port=<port>] [-P]"
  echo "             [-q=<qdisc>] [-R] [-s=<server>|--server=<server]"
  echo "             [-S|--stats] -t=<time>|--time=<time>] [-w] [cubic|dctcp]"
  echo "  Where:"
  echo "    out               egress (default)"
  echo "    -b or --bpf       BPF program filename to load and attach."
  echo "                      Default is hbm_out_kern.o for egress,"
  echo "    -c or -cc         TCP congestion control (cubic or dctcp)"
  echo "    --debug           print BPF trace buffer"
  echo "    -d or --delay     add a delay in ms using netem"
  echo "    -D                In addition to the goodput in Mbps, it also outputs"
  echo "                      other detailed information. This information is"
  echo "                      test dependent (i.e. iperf3 or netperf)."
  echo "    -E                enable ECN (not required for dctcp)"
  echo "    --edt             use fq's Earliest Departure Time (requires fq)"
  echo "    -f or --flows     number of concurrent flows (default=1)"
  echo "    -i or --id        cgroup id (an integer, default is 1)"
  echo "    -N                use netperf instead of iperf3"
  echo "    --no_cn           Do not return CN notifications"
  echo "    -l                do not limit flows using loopback"
  echo "    -h                Help"
  echo "    -p or --port      iperf3 port (default is 5201)"
  echo "    -P                use an iperf3 instance for each flow"
  echo "    -q                use the specified qdisc"
  echo "    -r or --rate      rate in Mbps (default 1s 1Gbps)"
  echo "    -R                Use TCP_RR for netperf. 1st flow has req"
  echo "                      size of 10KB, rest of 1MB. Reply in all"
  echo "                      cases is 1 byte."
  echo "                      More detailed output for each flow can be found"
  echo "                      in the files netperf.<cg>.<flow>, where <cg> is the"
  echo "                      cgroup id as specified with the -i flag, and <flow>"
  echo "                      is the flow id starting at 1 and increasing by 1 for"
  echo "                      flow (as specified by -f)."
  echo "    -s or --server    hostname of netperf server. Used to create netperf"
  echo "                      test traffic between to hosts (default is within host)"
  echo "                      netserver must be running on the host."
  echo "    -S or --stats     whether to update hbm stats (default is yes)."
  echo "    -t or --time      duration of iperf3 in seconds (default=5)"
  echo "    -w                Work conserving flag. cgroup can increase its"
  echo "                      bandwidth beyond the rate limit specified"
  echo "                      while there is available bandwidth. Current"
  echo "                      implementation assumes there is only one NIC"
  echo "                      (eth0), but can be extended to support multiple"
  echo "                       NICs."
  echo "    cubic or dctcp    specify which TCP CC to use"
  echo " "
  exit
}

#set -x

debug_flag=0
args="$@"
name="$0"
netem=0
cc=x
dir="-o"
dir_name="out"
dur=5
flows=1
id=1
prog=""
port=5201
rate=1000
multi_iperf=0
flow_cnt=1
use_netperf=0
rr=0
ecn=0
details=0
server=""
qdisc=""
flags=""
do_stats=0

BPFFS=/sys/fs/bpf
function config_bpffs () {
	if mount | grep $BPFFS > /dev/null; then
		echo "bpffs already mounted"
	else
		echo "bpffs not mounted. Mounting..."
		mount -t bpf none $BPFFS
	fi
}

function start_hbm () {
  rm -f hbm.out
  echo "./hbm $dir -n $id -r $rate -t $dur $flags $dbg $prog" > hbm.out
  echo " " >> hbm.out
  ./hbm $dir -n $id -r $rate -t $dur $flags $dbg $prog >> hbm.out 2>&1  &
  echo $!
}

processArgs () {
  for i in $args ; do
    case $i in
    # Support for upcomming ingress rate limiting
    #in)         # support for upcoming ingress rate limiting
    #  dir="-i"
    #  dir_name="in"
    #  ;;
    out)
      dir="-o"
      dir_name="out"
      ;;
    -b=*|--bpf=*)
      prog="${i#*=}"
      ;;
    -c=*|--cc=*)
      cc="${i#*=}"
      ;;
    --no_cn)
      flags="$flags --no_cn"
      ;;
    --debug)
      flags="$flags -d"
      debug_flag=1
      ;;
    -d=*|--delay=*)
      netem="${i#*=}"
      ;;
    -D)
      details=1
      ;;
    -E)
      ecn=1
      ;;
    --edt)
      flags="$flags --edt"
      qdisc="fq"
     ;;
    -f=*|--flows=*)
      flows="${i#*=}"
      ;;
    -i=*|--id=*)
      id="${i#*=}"
      ;;
    -l)
      flags="$flags -l"
      ;;
    -N)
      use_netperf=1
      ;;
    -p=*|--port=*)
      port="${i#*=}"
      ;;
    -P)
      multi_iperf=1
      ;;
    -q=*)
      qdisc="${i#*=}"
      ;;
    -r=*|--rate=*)
      rate="${i#*=}"
      ;;
    -R)
      rr=1
      ;;
    -s=*|--server=*)
      server="${i#*=}"
      ;;
    -S|--stats)
      flags="$flags -s"
      do_stats=1
      ;;
    -t=*|--time=*)
      dur="${i#*=}"
      ;;
    -w)
      flags="$flags -w"
      ;;
    cubic)
      cc=cubic
      ;;
    dctcp)
      cc=dctcp
      ;;
    *)
      echo "Unknown arg:$i"
      Usage
      ;;
    esac
  done
}

processArgs
config_bpffs

if [ $debug_flag -eq 1 ] ; then
  rm -f hbm_out.log
fi

hbm_pid=$(start_hbm)
usleep 100000

host=`hostname`
cg_base_dir=/sys/fs/cgroup/unified
cg_dir="$cg_base_dir/cgroup-test-work-dir/hbm$id"

echo $$ >> $cg_dir/cgroup.procs

ulimit -l unlimited

rm -f ss.out
rm -f hbm.[0-9]*.$dir_name
if [ $ecn -ne 0 ] ; then
  sysctl -w -q -n net.ipv4.tcp_ecn=1
fi

if [ $use_netperf -eq 0 ] ; then
  cur_cc=`sysctl -n net.ipv4.tcp_congestion_control`
  if [ "$cc" != "x" ] ; then
    sysctl -w -q -n net.ipv4.tcp_congestion_control=$cc
  fi
fi

if [ "$netem" -ne "0" ] ; then
  if [ "$qdisc" != "" ] ; then
    echo "WARNING: Ignoring -q options because -d option used"
  fi
  tc qdisc del dev lo root > /dev/null 2>&1
  tc qdisc add dev lo root netem delay $netem\ms > /dev/null 2>&1
elif [ "$qdisc" != "" ] ; then
  tc qdisc del dev eth0 root > /dev/null 2>&1
  tc qdisc add dev eth0 root $qdisc > /dev/null 2>&1
fi

n=0
m=$[$dur * 5]
hn="::1"
if [ $use_netperf -ne 0 ] ; then
  if [ "$server" != "" ] ; then
    hn=$server
  fi
fi

( ping6 -i 0.2 -c $m $hn > ping.out 2>&1 ) &

if [ $use_netperf -ne 0 ] ; then
  begNetserverPid=`ps ax | grep netserver | grep --invert-match "grep" | \
                   awk '{ print $1 }'`
  if [ "$begNetserverPid" == "" ] ; then
    if [ "$server" == "" ] ; then
      ( ./netserver > /dev/null 2>&1) &
      usleep 100000
    fi
  fi
  flow_cnt=1
  if [ "$server" == "" ] ; then
    np_server=$host
  else
    np_server=$server
  fi
  if [ "$cc" == "x" ] ; then
    np_cc=""
  else
    np_cc="-K $cc,$cc"
  fi
  replySize=1
  while [ $flow_cnt -le $flows ] ; do
    if [ $rr -ne 0 ] ; then
      reqSize=1M
      if [ $flow_cnt -eq 1 ] ; then
        reqSize=10K
      fi
      if [ "$dir" == "-i" ] ; then
        replySize=$reqSize
        reqSize=1
      fi
      ( ./netperf -H $np_server -l $dur -f m -j -t TCP_RR  -- -r $reqSize,$replySize $np_cc -k P50_lATENCY,P90_LATENCY,LOCAL_TRANSPORT_RETRANS,REMOTE_TRANSPORT_RETRANS,LOCAL_SEND_THROUGHPUT,LOCAL_RECV_THROUGHPUT,REQUEST_SIZE,RESPONSE_SIZE > netperf.$id.$flow_cnt ) &
    else
      if [ "$dir" == "-i" ] ; then
        ( ./netperf -H $np_server -l $dur -f m -j -t TCP_RR -- -r 1,10M $np_cc -k P50_LATENCY,P90_LATENCY,LOCAL_TRANSPORT_RETRANS,LOCAL_SEND_THROUGHPUT,REMOTE_TRANSPORT_RETRANS,REMOTE_SEND_THROUGHPUT,REQUEST_SIZE,RESPONSE_SIZE > netperf.$id.$flow_cnt ) &
      else
        ( ./netperf -H $np_server -l $dur -f m -j -t TCP_STREAM -- $np_cc -k P50_lATENCY,P90_LATENCY,LOCAL_TRANSPORT_RETRANS,LOCAL_SEND_THROUGHPUT,REQUEST_SIZE,RESPONSE_SIZE > netperf.$id.$flow_cnt ) &
      fi
    fi
    flow_cnt=$[flow_cnt+1]
  done

# sleep for duration of test (plus some buffer)
  n=$[dur+2]
  sleep $n

# force graceful termination of netperf
  pids=`pgrep netperf`
  for p in $pids ; do
    kill -SIGALRM $p
  done

  flow_cnt=1
  rate=0
  if [ $details -ne 0 ] ; then
    echo ""
    echo "Details for HBM in cgroup $id"
    if [ $do_stats -eq 1 ] ; then
      if [ -e hbm.$id.$dir_name ] ; then
        cat hbm.$id.$dir_name
      fi
    fi
  fi
  while [ $flow_cnt -le $flows ] ; do
    if [ "$dir" == "-i" ] ; then
      r=`cat netperf.$id.$flow_cnt | grep -o "REMOTE_SEND_THROUGHPUT=[0-9]*" | grep -o "[0-9]*"`
    else
      r=`cat netperf.$id.$flow_cnt | grep -o "LOCAL_SEND_THROUGHPUT=[0-9]*" | grep -o "[0-9]*"`
    fi
    echo "rate for flow $flow_cnt: $r"
    rate=$[rate+r]
    if [ $details -ne 0 ] ; then
      echo "-----"
      echo "Details for cgroup $id, flow $flow_cnt"
      cat netperf.$id.$flow_cnt
    fi
    flow_cnt=$[flow_cnt+1]
  done
  if [ $details -ne 0 ] ; then
    echo ""
    delay=`grep "avg" ping.out | grep -o "= [0-9.]*/[0-9.]*" | grep -o "[0-9.]*$"`
    echo "PING AVG DELAY:$delay"
    echo "AGGREGATE_GOODPUT:$rate"
  else
    echo $rate
  fi
elif [ $multi_iperf -eq 0 ] ; then
  (iperf3 -s -p $port -1 > /dev/null 2>&1) &
  usleep 100000
  iperf3 -c $host -p $port -i 0 -P $flows -f m -t $dur > iperf.$id
  rates=`grep receiver iperf.$id | grep -o "[0-9.]* Mbits" | grep -o "^[0-9]*"`
  rate=`echo $rates | grep -o "[0-9]*$"`

  if [ $details -ne 0 ] ; then
    echo ""
    echo "Details for HBM in cgroup $id"
    if [ $do_stats -eq 1 ] ; then
      if [ -e hbm.$id.$dir_name ] ; then
        cat hbm.$id.$dir_name
      fi
    fi
    delay=`grep "avg" ping.out | grep -o "= [0-9.]*/[0-9.]*" | grep -o "[0-9.]*$"`
    echo "PING AVG DELAY:$delay"
    echo "AGGREGATE_GOODPUT:$rate"
  else
    echo $rate
  fi
else
  flow_cnt=1
  while [ $flow_cnt -le $flows ] ; do
    (iperf3 -s -p $port -1 > /dev/null 2>&1) &
    ( iperf3 -c $host -p $port -i 0 -P 1 -f m -t $dur | grep receiver | grep -o "[0-9.]* Mbits" | grep -o "^[0-9]*" | grep -o "[0-9]*$" > iperf3.$id.$flow_cnt ) &
    port=$[port+1]
    flow_cnt=$[flow_cnt+1]
  done
  n=$[dur+1]
  sleep $n
  flow_cnt=1
  rate=0
  if [ $details -ne 0 ] ; then
    echo ""
    echo "Details for HBM in cgroup $id"
    if [ $do_stats -eq 1 ] ; then
      if [ -e hbm.$id.$dir_name ] ; then
        cat hbm.$id.$dir_name
      fi
    fi
  fi

  while [ $flow_cnt -le $flows ] ; do
    r=`cat iperf3.$id.$flow_cnt`
#    echo "rate for flow $flow_cnt: $r"
  if [ $details -ne 0 ] ; then
    echo "Rate for cgroup $id, flow $flow_cnt LOCAL_SEND_THROUGHPUT=$r"
  fi
    rate=$[rate+r]
    flow_cnt=$[flow_cnt+1]
  done
  if [ $details -ne 0 ] ; then
    delay=`grep "avg" ping.out | grep -o "= [0-9.]*/[0-9.]*" | grep -o "[0-9.]*$"`
    echo "PING AVG DELAY:$delay"
    echo "AGGREGATE_GOODPUT:$rate"
  else
    echo $rate
  fi
fi

if [ $use_netperf -eq 0 ] ; then
  sysctl -w -q -n net.ipv4.tcp_congestion_control=$cur_cc
fi
if [ $ecn -ne 0 ] ; then
  sysctl -w -q -n net.ipv4.tcp_ecn=0
fi
if [ "$netem" -ne "0" ] ; then
  tc qdisc del dev lo root > /dev/null 2>&1
fi
if [ "$qdisc" != "" ] ; then
  tc qdisc del dev eth0 root > /dev/null 2>&1
fi
sleep 2

hbmPid=`ps ax | grep "hbm " | grep --invert-match "grep" | awk '{ print $1 }'`
if [ "$hbmPid" == "$hbm_pid" ] ; then
  kill $hbm_pid
fi

sleep 1

# Detach any pinned BPF programs that may have lingered
rm -rf $BPFFS/hbm*

if [ $use_netperf -ne 0 ] ; then
  if [ "$server" == "" ] ; then
    if [ "$begNetserverPid" == "" ] ; then
      netserverPid=`ps ax | grep netserver | grep --invert-match "grep" | awk '{ print $1 }'`
      if [ "$netserverPid" != "" ] ; then
        kill $netserverPid
      fi
    fi
  fi
fi
exit
