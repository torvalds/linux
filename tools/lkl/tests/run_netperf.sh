#!/bin/bash

# Usage
#  ./run_netperf.sh [host_ip] [num_runs] [use_taskset] [test_name]

set -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
hijack_script=${script_dir}/../bin/lkl-hijack.sh

num_runs="1"
test_name="TCP_STREAM"
use_taskset="0"
host_ip="192.168.13.1"
taskset_cmd="taskset -c 1"
test_len=10  # second

if [ ! -x "$(which netperf)" ]; then
    echo "WARNING: Cannot find a netserver executable, skipping netperf tests."
    exit 0
fi

if [ $# -ge 1 ]
then
    host_ip=$1
fi
if [ $# -ge 2 ]
then
    num_runs=$2
fi
if [ $# -ge 3 ]
then
    use_taskset=$3
fi
if [ $# -ge 4 ]
then
    test_name=$4
fi
if [ $# -ge 5 ]
then
    echo "BAD NUMBER of INPUTS."
    exit 1
fi

if [ $use_taskset = "0" ]
then
  taskset_cmd=""
fi

# Starts the netsever. If it fails, there is no clean up needed.
existing_netserver=$(ps -ef | grep -v grep | grep -c netserver) || true

if [ $existing_netserver -ne 0 ]
then
  echo "netserver is running. You must kill it."
  exit 1
fi

clean() {
    sudo killall netserver &> /dev/null || true
}

clean_with_tap() {
    sudo ip link set dev $LKL_HIJACK_NET_IFPARAMS down &> /dev/null || true
    sudo ip tuntap del dev $LKL_HIJACK_NET_IFPARAMS mode tap &> /dev/null || true
    clean
}

trap clean EXIT

# LKL_HIJACK_NET_IFTYPE is not set, which means it's not called from
# hijack-test.sh. Needs to set up things first.
if [ -z ${LKL_HIJACK_NET_IFTYPE+x} ]
then
    # Setting up environmental vars and TAP
    export LKL_HIJACK_NET_IFTYPE=tap
    export LKL_HIJACK_NET_IFPARAMS=lkl_ptt0
    export LKL_HIJACK_NET_IP=192.168.13.2
    export LKL_HIJACK_NET_NETMASK_LEN=24
    export LKL_HIJACK_NET_IPV6=fc03::2
    export LKL_HIJACK_NET_NETMASK6_LEN=64

    sudo ip tuntap del dev $LKL_HIJACK_NET_IFPARAMS mode tap || true
    sudo ip tuntap add dev $LKL_HIJACK_NET_IFPARAMS mode tap user $USER
    sudo ip link set dev $LKL_HIJACK_NET_IFPARAMS up
    sudo ip addr add dev $LKL_HIJACK_NET_IFPARAMS $host_ip/$LKL_HIJACK_NET_NETMASK_LEN

    trap clean_with_tap EXIT
fi

sudo netserver

echo NUM=$num_runs, TEST=$test_name, TASKSET=$use_taskset
for i in `seq $num_runs`; do
    echo Test: $i
    $taskset_cmd ${hijack_script} netperf -H $host_ip -t $test_name -l $test_len
done
