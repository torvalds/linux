#!/usr/bin/env bash

# Usage
#  ./run_netperf.sh [ip] [test_name] [use_taskset] [num_runs]

set -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
hijack_script=${script_dir}/../bin/lkl-hijack.sh

num_runs="1"
test_name="TCP_STREAM"
use_taskset="0"
host_ip="localhost"
taskset_cmd="taskset -c 1"
test_len=10  # second

if [ ! -x "$(which netperf)" ]; then
    echo "WARNING: Cannot find a netserver executable, skipping netperf tests."
    exit $TEST_SKIP
fi

if [ $# -ge 1 ]; then
    host_ip=$1
fi
if [ $# -ge 2 ]; then
    test_name=$2
fi
if [ $# -ge 3 ]; then
    use_taskset=$2
fi
if [ $# -ge 4 ]; then
    num_runs=$3
fi
if [ $# -ge 5 ]; then
    echo "BAD NUMBER of INPUTS."
    exit 1
fi

if [ $use_taskset = "0" ]; then
  taskset_cmd=""
fi

clean() {
    kill %1 || true
}

clean_with_tap() {
    tap_cleanup &> /dev/null || true
    clean
    rm -rf ${work_dir}
}

# LKL_HIJACK_CONFIG_FILE is not set, which means it's not called from
# hijack-test.sh. Needs to set up things first.
if [ -z ${LKL_HIJACK_CONFIG_FILE+x} ]; then

    # Setting up environmental vars and TAP
    work_dir=$(mktemp -d)
    cfgjson=${work_dir}/hijack-test.conf
    export LKL_HIJACK_CONFIG_FILE=$cfgjson

    cat <<EOF > ${cfgjson}
    {
         "interfaces": [
               {
                    "type": "tap"
                    "param": "$(tap_ifname)"
                    "ip": "$(ip_lkl)"
                    "masklen":"$TEST_IP_NETMASK"
                    "ipv6":"$(ip6_lkl)"
                    "masklen6":"$TEST_IP6_NETMASK"
               }
         ]
    }
EOF

    . $script_dir/net-setup.sh
    host_ip=$(ip_host)

    tap_prepare
    tap_setup
    trap clean_with_tap EXIT
fi

netserver -D -N -p $TEST_NETSERVER_PORT &

trap clean EXIT

echo NUM=$num_runs, TEST=$test_name, TASKSET=$use_taskset
for i in `seq $num_runs`; do
    echo Test: $i
    set -x
    $taskset_cmd ${hijack_script} netperf -p $TEST_NETSERVER_PORT -H $host_ip \
		         -t $test_name -l $test_len
    set +x
done
