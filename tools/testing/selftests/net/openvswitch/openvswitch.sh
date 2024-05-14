#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# OVS kernel module self tests

trap ovs_exit_sig EXIT TERM INT ERR

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
TRACING=0

tests="
	netlink_checks				ovsnl: validate netlink attrs and settings"

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
	     grep -E "Need to (install|upgrade) the python" >/dev/null 2>&1; then
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
