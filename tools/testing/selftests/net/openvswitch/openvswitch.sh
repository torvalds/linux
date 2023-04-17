#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# OVS kernel module self tests

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
TRACING=0

tests="
	netlink_checks				ovsnl: validate netlink attrs and settings
	upcall_interfaces			ovs: test the upcall interfaces"

info() {
    [ $VERBOSE = 0 ] || echo $*
}

ovs_base=`pwd`
sbxs=
sbx_add () {
	info "adding sandbox '$1'"

	sbxs="$sbxs $1"

	NO_BIN=0

	# Create sandbox.
	local d="$ovs_base"/$1
	if [ -e $d ]; then
		info "removing $d"
		rm -rf "$d"
	fi
	mkdir "$d" || return 1
	ovs_setenv $1
}

ovs_exit_sig() {
	[ -e ${ovs_dir}/cleanup ] && . "$ovs_dir/cleanup"
}

on_exit() {
	echo "$1" > ${ovs_dir}/cleanup.tmp
	cat ${ovs_dir}/cleanup >> ${ovs_dir}/cleanup.tmp
	mv ${ovs_dir}/cleanup.tmp ${ovs_dir}/cleanup
}

ovs_setenv() {
	sandbox=$1

	ovs_dir=$ovs_base${1:+/$1}; export ovs_dir

	test -e ${ovs_dir}/cleanup || : > ${ovs_dir}/cleanup
}

ovs_sbx() {
	if test "X$2" != X; then
		(ovs_setenv $1; shift; "$@" >> ${ovs_dir}/debug.log)
	else
		ovs_setenv $1
	fi
}

ovs_add_dp () {
	info "Adding DP/Bridge IF: sbx:$1 dp:$2 {$3, $4, $5}"
	sbxname="$1"
	shift
	ovs_sbx "$sbxname" python3 $ovs_base/ovs-dpctl.py add-dp $*
	on_exit "ovs_sbx $sbxname python3 $ovs_base/ovs-dpctl.py del-dp $1;"
}

ovs_add_if () {
	info "Adding IF to DP: br:$2 if:$3"
	if [ "$4" != "-u" ]; then
		ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py add-if "$2" "$3" \
		    || return 1
	else
		python3 $ovs_base/ovs-dpctl.py add-if \
		    -u "$2" "$3" >$ovs_dir/$3.out 2>$ovs_dir/$3.err &
		pid=$!
		on_exit "ovs_sbx $1 kill -TERM $pid 2>/dev/null"
	fi
}

ovs_del_if () {
	info "Deleting IF from DP: br:$2 if:$3"
	ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py del-if "$2" "$3" || return 1
}

ovs_netns_spawn_daemon() {
	sbx=$1
	shift
	netns=$1
	shift
	info "spawning cmd: $*"
	ip netns exec $netns $*  >> $ovs_dir/stdout  2>> $ovs_dir/stderr &
	pid=$!
	ovs_sbx "$sbx" on_exit "kill -TERM $pid 2>/dev/null"
}

ovs_add_netns_and_veths () {
	info "Adding netns attached: sbx:$1 dp:$2 {$3, $4, $5}"
	ovs_sbx "$1" ip netns add "$3" || return 1
	on_exit "ovs_sbx $1 ip netns del $3"
	ovs_sbx "$1" ip link add "$4" type veth peer name "$5" || return 1
	on_exit "ovs_sbx $1 ip link del $4 >/dev/null 2>&1"
	ovs_sbx "$1" ip link set "$4" up || return 1
	ovs_sbx "$1" ip link set "$5" netns "$3" || return 1
	ovs_sbx "$1" ip netns exec "$3" ip link set "$5" up || return 1

	if [ "$6" != "" ]; then
		ovs_sbx "$1" ip netns exec "$3" ip addr add "$6" dev "$5" \
		    || return 1
	fi

	if [ "$7" != "-u" ]; then
		ovs_add_if "$1" "$2" "$4" || return 1
	else
		ovs_add_if "$1" "$2" "$4" -u || return 1
	fi

	[ $TRACING -eq 1 ] && ovs_netns_spawn_daemon "$1" "$ns" \
			tcpdump -i any -s 65535

	return 0
}

usage() {
	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  -t: capture traffic via tcpdump"
	echo "  -v: verbose"
	echo "  -p: pause on failure"
	echo
	echo "Available tests${tests}"
	exit 1
}

# netlink_validation
# - Create a dp
# - check no warning with "old version" simulation
test_netlink_checks () {
	sbx_add "test_netlink_checks" || return 1

	info "setting up new DP"
	ovs_add_dp "test_netlink_checks" nv0 || return 1
	# now try again
	PRE_TEST=$(dmesg | grep -E "RIP: [0-9a-fA-Fx]+:ovs_dp_cmd_new\+")
	ovs_add_dp "test_netlink_checks" nv0 -V 0 || return 1
	POST_TEST=$(dmesg | grep -E "RIP: [0-9a-fA-Fx]+:ovs_dp_cmd_new\+")
	if [ "$PRE_TEST" != "$POST_TEST" ]; then
		info "failed - gen warning"
		return 1
	fi

	ovs_add_netns_and_veths "test_netlink_checks" nv0 left left0 l0 || \
	    return 1
	ovs_add_netns_and_veths "test_netlink_checks" nv0 right right0 r0 || \
	    return 1
	[ $(python3 $ovs_base/ovs-dpctl.py show nv0 | grep port | \
	    wc -l) == 3 ] || \
	      return 1
	ovs_del_if "test_netlink_checks" nv0 right0 || return 1
	[ $(python3 $ovs_base/ovs-dpctl.py show nv0 | grep port | \
	    wc -l) == 2 ] || \
	      return 1

	return 0
}

test_upcall_interfaces() {
	sbx_add "test_upcall_interfaces" || return 1

	info "setting up new DP"
	ovs_add_dp "test_upcall_interfaces" ui0 -V 2:1 || return 1

	ovs_add_netns_and_veths "test_upcall_interfaces" ui0 upc left0 l0 \
	    172.31.110.1/24 -u || return 1

	sleep 1
	info "sending arping"
	ip netns exec upc arping -I l0 172.31.110.20 -c 1 \
	    >$ovs_dir/arping.stdout 2>$ovs_dir/arping.stderr

	grep -E "MISS upcall\[0/yes\]: .*arp\(sip=172.31.110.1,tip=172.31.110.20,op=1,sha=" $ovs_dir/left0.out >/dev/null 2>&1 || return 1
	return 0
}

run_test() {
	(
	tname="$1"
	tdesc="$2"

	if ! lsmod | grep openvswitch >/dev/null 2>&1; then
		stdbuf -o0 printf "TEST: %-60s  [NOMOD]\n" "${tdesc}"
		return $ksft_skip
	fi

	if python3 ovs-dpctl.py -h 2>&1 | \
	     grep "Need to install the python" >/dev/null 2>&1; then
		stdbuf -o0 printf "TEST: %-60s  [PYLIB]\n" "${tdesc}"
		return $ksft_skip
	fi
	printf "TEST: %-60s  [START]\n" "${tname}"

	unset IFS

	eval test_${tname}
	ret=$?

	if [ $ret -eq 0 ]; then
		printf "TEST: %-60s  [ OK ]\n" "${tdesc}"
		ovs_exit_sig
		rm -rf "$ovs_dir"
	elif [ $ret -eq 1 ]; then
		printf "TEST: %-60s  [FAIL]\n" "${tdesc}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "Pausing. Logs in $ovs_dir/. Hit enter to continue"
			read a
		fi
		ovs_exit_sig
		[ "${PAUSE_ON_FAIL}" = "yes" ] || rm -rf "$ovs_dir"
		exit 1
	elif [ $ret -eq $ksft_skip ]; then
		printf "TEST: %-60s  [SKIP]\n" "${tdesc}"
	elif [ $ret -eq 2 ]; then
		rm -rf test_${tname}
		run_test "$1" "$2"
	fi

	return $ret
	)
	ret=$?
	case $ret in
		0)
			[ $all_skipped = true ] && [ $exitcode=$ksft_skip ] && exitcode=0
			all_skipped=false
		;;
		$ksft_skip)
			[ $all_skipped = true ] && exitcode=$ksft_skip
		;;
		*)
			all_skipped=false
			exitcode=1
		;;
	esac

	return $ret
}


exitcode=0
desc=0
all_skipped=true

while getopts :pvt o
do
	case $o in
	p) PAUSE_ON_FAIL=yes;;
	v) VERBOSE=1;;
	t) if which tcpdump > /dev/null 2>&1; then
		TRACING=1
	   else
		echo "=== tcpdump not available, tracing disabled"
	   fi
	   ;;
	*) usage;;
	esac
done
shift $(($OPTIND-1))

IFS="	
"

for arg do
	# Check first that all requested tests are available before running any
	command -v > /dev/null "test_${arg}" || { echo "=== Test ${arg} not found"; usage; }
done

name=""
desc=""
for t in ${tests}; do
	[ "${name}" = "" ]	&& name="${t}"	&& continue
	[ "${desc}" = "" ]	&& desc="${t}"

	run_this=1
	for arg do
		[ "${arg}" != "${arg#--*}" ] && continue
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	if [ $run_this -eq 1 ]; then
		run_test "${name}" "${desc}"
	fi
	name=""
	desc=""
done

exit ${exitcode}
