#!/bin/bash

MY_DIR=$(dirname $0)
# Details on the bpf prog
BPF_CGRP2_ARRAY_NAME='test_cgrp2_array_pin'
BPF_PROG="$MY_DIR/test_cgrp2_tc_kern.o"
BPF_SECTION='filter'

[ -z "$TC" ] && TC='tc'
[ -z "$IP" ] && IP='ip'

# Names of the veth interface, net namespace...etc.
HOST_IFC='ve'
NS_IFC='vens'
NS='ns'

find_mnt() {
    cat /proc/mounts | \
	awk '{ if ($3 == "'$1'" && mnt == "") { mnt = $2 }} END { print mnt }'
}

# Init cgroup2 vars
init_cgrp2_vars() {
    CGRP2_ROOT=$(find_mnt cgroup2)
    if [ -z "$CGRP2_ROOT" ]
    then
	CGRP2_ROOT='/mnt/cgroup2'
	MOUNT_CGRP2="yes"
    fi
    CGRP2_TC="$CGRP2_ROOT/tc"
    CGRP2_TC_LEAF="$CGRP2_TC/leaf"
}

# Init bpf fs vars
init_bpf_fs_vars() {
    local bpf_fs_root=$(find_mnt bpf)
    [ -n "$bpf_fs_root" ] || return -1
    BPF_FS_TC_SHARE="$bpf_fs_root/tc/globals"
}

setup_cgrp2() {
    case $1 in
	start)
	    if [ "$MOUNT_CGRP2" == 'yes' ]
	    then
		[ -d $CGRP2_ROOT ] || mkdir -p $CGRP2_ROOT
		mount -t cgroup2 none $CGRP2_ROOT || return $?
	    fi
	    mkdir -p $CGRP2_TC_LEAF
	    ;;
	*)
	    rmdir $CGRP2_TC_LEAF && rmdir $CGRP2_TC
	    [ "$MOUNT_CGRP2" == 'yes' ] && umount $CGRP2_ROOT
	    ;;
    esac
}

setup_bpf_cgrp2_array() {
    local bpf_cgrp2_array="$BPF_FS_TC_SHARE/$BPF_CGRP2_ARRAY_NAME"
    case $1 in
	start)
	    $MY_DIR/test_cgrp2_array_pin -U $bpf_cgrp2_array -v $CGRP2_TC
	    ;;
	*)
	    [ -d "$BPF_FS_TC_SHARE" ] && rm -f $bpf_cgrp2_array
	    ;;
    esac
}

setup_net() {
    case $1 in
	start)
	    $IP link add $HOST_IFC type veth peer name $NS_IFC || return $?
	    $IP link set dev $HOST_IFC up || return $?
	    sysctl -q net.ipv6.conf.$HOST_IFC.accept_dad=0

	    $IP netns add ns || return $?
	    $IP link set dev $NS_IFC netns ns || return $?
	    $IP -n $NS link set dev $NS_IFC up || return $?
	    $IP netns exec $NS sysctl -q net.ipv6.conf.$NS_IFC.accept_dad=0
	    $TC qdisc add dev $HOST_IFC clsact || return $?
	    $TC filter add dev $HOST_IFC egress bpf da obj $BPF_PROG sec $BPF_SECTION || return $?
	    ;;
	*)
	    $IP netns del $NS
	    $IP link del $HOST_IFC
	    ;;
    esac
}

run_in_cgrp() {
    # Fork another bash and move it under the specified cgroup.
    # It makes the cgroup cleanup easier at the end of the test.
    cmd='echo $$ > '
    cmd="$cmd $1/cgroup.procs; exec $2"
    bash -c "$cmd"
}

do_test() {
    run_in_cgrp $CGRP2_TC_LEAF "ping -6 -c3 ff02::1%$HOST_IFC >& /dev/null"
    local dropped=$($TC -s qdisc show dev $HOST_IFC | tail -3 | \
			   awk '/drop/{print substr($7, 0, index($7, ",")-1)}')
    if [[ $dropped -eq 0 ]]
    then
	echo "FAIL"
	return 1
    else
	echo "Successfully filtered $dropped packets"
	return 0
    fi
}

do_exit() {
    if [ "$DEBUG" == "yes" ] && [ "$MODE" != 'cleanuponly' ]
    then
	echo "------ DEBUG ------"
	echo "mount: "; mount | egrep '(cgroup2|bpf)'; echo
	echo "$CGRP2_TC_LEAF: "; ls -l $CGRP2_TC_LEAF; echo
	if [ -d "$BPF_FS_TC_SHARE" ]
	then
	    echo "$BPF_FS_TC_SHARE: "; ls -l $BPF_FS_TC_SHARE; echo
	fi
	echo "Host net:"
	$IP netns
	$IP link show dev $HOST_IFC
	$IP -6 a show dev $HOST_IFC
	$TC -s qdisc show dev $HOST_IFC
	echo
	echo "$NS net:"
	$IP -n $NS link show dev $NS_IFC
	$IP -n $NS -6 link show dev $NS_IFC
	echo "------ DEBUG ------"
	echo
    fi

    if [ "$MODE" != 'nocleanup' ]
    then
	setup_net stop
	setup_bpf_cgrp2_array stop
	setup_cgrp2 stop
    fi
}

init_cgrp2_vars
init_bpf_fs_vars

while [[ $# -ge 1 ]]
do
    a="$1"
    case $a in
	debug)
	    DEBUG='yes'
	    shift 1
	    ;;
	cleanup-only)
	    MODE='cleanuponly'
	    shift 1
	    ;;
	no-cleanup)
	    MODE='nocleanup'
	    shift 1
	    ;;
	*)
	    echo "test_cgrp2_tc [debug] [cleanup-only | no-cleanup]"
	    echo "  debug: Print cgrp and network setup details at the end of the test"
	    echo "  cleanup-only: Try to cleanup things from last test.  No test will be run"
	    echo "  no-cleanup: Run the test but don't do cleanup at the end"
	    echo "[Note: If no arg is given, it will run the test and do cleanup at the end]"
	    echo
	    exit -1
	    ;;
    esac
done

trap do_exit 0

[ "$MODE" == 'cleanuponly' ] && exit

setup_cgrp2 start || exit $?
setup_net start || exit $?
init_bpf_fs_vars || exit $?
setup_bpf_cgrp2_array start || exit $?
do_test
echo
