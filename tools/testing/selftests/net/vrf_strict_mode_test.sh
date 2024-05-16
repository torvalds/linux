#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is designed for testing the new VRF strict_mode functionality.

source lib.sh
ret=0

# identifies the "init" network namespace which is often called root network
# namespace.
INIT_NETNS_NAME="init"

PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}

TESTS="init testns mix"

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		nsuccess=$((nsuccess+1))
		printf "\n    TEST: %-60s  [ OK ]\n" "${msg}"
	else
		ret=1
		nfail=$((nfail+1))
		printf "\n    TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

print_log_test_results()
{
	if [ "$TESTS" != "none" ]; then
		printf "\nTests passed: %3d\n" ${nsuccess}
		printf "Tests failed: %3d\n"   ${nfail}
	fi
}

log_section()
{
	echo
	echo "################################################################################"
	echo "TEST SECTION: $*"
	echo "################################################################################"
}

ip_expand_args()
{
	local nsname=$1
	local nsarg=""

	if [ "${nsname}" != "${INIT_NETNS_NAME}" ]; then
		nsarg="-netns ${nsname}"
	fi

	echo "${nsarg}"
}

vrf_count()
{
	local nsname=$1
	local nsarg="$(ip_expand_args ${nsname})"

	ip ${nsarg} -o link show type vrf | wc -l
}

count_vrf_by_table_id()
{
	local nsname=$1
	local tableid=$2
	local nsarg="$(ip_expand_args ${nsname})"

	ip ${nsarg} -d -o link show type vrf | grep "table ${tableid}" | wc -l
}

add_vrf()
{
	local nsname=$1
	local vrfname=$2
	local vrftable=$3
	local nsarg="$(ip_expand_args ${nsname})"

	ip ${nsarg} link add ${vrfname} type vrf table ${vrftable} &>/dev/null
}

add_vrf_and_check()
{
	local nsname=$1
	local vrfname=$2
	local vrftable=$3
	local cnt
	local rc

	add_vrf ${nsname} ${vrfname} ${vrftable}; rc=$?

	cnt=$(count_vrf_by_table_id ${nsname} ${vrftable})

	log_test ${rc} 0 "${nsname}: add vrf ${vrfname}, ${cnt} vrfs for table ${vrftable}"
}

add_vrf_and_check_fail()
{
	local nsname=$1
	local vrfname=$2
	local vrftable=$3
	local cnt
	local rc

	add_vrf ${nsname} ${vrfname} ${vrftable}; rc=$?

	cnt=$(count_vrf_by_table_id ${nsname} ${vrftable})

	log_test ${rc} 2 "${nsname}: CANNOT add vrf ${vrfname}, ${cnt} vrfs for table ${vrftable}"
}

del_vrf_and_check()
{
	local nsname=$1
	local vrfname=$2
	local nsarg="$(ip_expand_args ${nsname})"

	ip ${nsarg} link del ${vrfname}
	log_test $? 0 "${nsname}: remove vrf ${vrfname}"
}

config_vrf_and_check()
{
	local nsname=$1
	local addr=$2
	local vrfname=$3
	local nsarg="$(ip_expand_args ${nsname})"

	ip ${nsarg} link set dev ${vrfname} up && \
		ip ${nsarg} addr add ${addr} dev ${vrfname}
	log_test $? 0 "${nsname}: vrf ${vrfname} up, addr ${addr}"
}

read_strict_mode()
{
	local nsname=$1
	local rval
	local rc=0
	local nsexec=""

	if [ "${nsname}" != "${INIT_NETNS_NAME}" ]; then
		# a custom network namespace is provided
		nsexec="ip netns exec ${nsname}"
	fi

	rval="$(${nsexec} bash -c "cat /proc/sys/net/vrf/strict_mode" | \
		grep -E "^[0-1]$")" &> /dev/null
	if [ $? -ne 0 ]; then
		# set errors
		rval=255
		rc=1
	fi

	# on success, rval can be only 0 or 1; on error, rval is equal to 255
	echo ${rval}
	return ${rc}
}

read_strict_mode_compare_and_check()
{
	local nsname=$1
	local expected=$2
	local res

	res="$(read_strict_mode ${nsname})"
	log_test ${res} ${expected} "${nsname}: check strict_mode=${res}"
}

set_strict_mode()
{
	local nsname=$1
	local val=$2
	local nsexec=""

	if [ "${nsname}" != "${INIT_NETNS_NAME}" ]; then
		# a custom network namespace is provided
		nsexec="ip netns exec ${nsname}"
	fi

	${nsexec} bash -c "echo ${val} >/proc/sys/net/vrf/strict_mode" &>/dev/null
}

enable_strict_mode()
{
	local nsname=$1

	set_strict_mode ${nsname} 1
}

disable_strict_mode()
{
	local nsname=$1

	set_strict_mode ${nsname} 0
}

disable_strict_mode_and_check()
{
	local nsname=$1

	disable_strict_mode ${nsname}
	log_test $? 0 "${nsname}: disable strict_mode (=0)"
}

enable_strict_mode_and_check()
{
	local nsname=$1

	enable_strict_mode ${nsname}
	log_test $? 0 "${nsname}: enable strict_mode (=1)"
}

enable_strict_mode_and_check_fail()
{
	local nsname=$1

	enable_strict_mode ${nsname}
	log_test $? 1 "${nsname}: CANNOT enable strict_mode"
}

strict_mode_check_default()
{
	local nsname=$1
	local strictmode
	local vrfcnt

	vrfcnt=$(vrf_count ${nsname})
	strictmode=$(read_strict_mode ${nsname})
	log_test ${strictmode} 0 "${nsname}: strict_mode=0 by default, ${vrfcnt} vrfs"
}

setup()
{
	modprobe vrf

	setup_ns testns
}

cleanup()
{
	ip netns del $testns 2>/dev/null

	ip link del vrf100 2>/dev/null
	ip link del vrf101 2>/dev/null
	ip link del vrf102 2>/dev/null

	echo 0 >/proc/sys/net/vrf/strict_mode 2>/dev/null
}

vrf_strict_mode_tests_init()
{
	log_section "VRF strict_mode test on init network namespace"

	vrf_strict_mode_check_support init

	strict_mode_check_default init

	add_vrf_and_check init vrf100 100
	config_vrf_and_check init 172.16.100.1/24 vrf100

	enable_strict_mode_and_check init

	add_vrf_and_check_fail init vrf101 100

	disable_strict_mode_and_check init

	add_vrf_and_check init vrf101 100
	config_vrf_and_check init 172.16.101.1/24 vrf101

	enable_strict_mode_and_check_fail init

	del_vrf_and_check init vrf101

	enable_strict_mode_and_check init

	add_vrf_and_check init vrf102 102
	config_vrf_and_check init 172.16.102.1/24 vrf102

	# the strict_modle is enabled in the init
}

vrf_strict_mode_tests_testns()
{
	log_section "VRF strict_mode test on testns network namespace"

	vrf_strict_mode_check_support $testns

	strict_mode_check_default $testns

	enable_strict_mode_and_check $testns

	add_vrf_and_check $testns vrf100 100
	config_vrf_and_check $testns 10.0.100.1/24 vrf100

	add_vrf_and_check_fail $testns vrf101 100

	add_vrf_and_check_fail $testns vrf102 100

	add_vrf_and_check $testns vrf200 200

	disable_strict_mode_and_check $testns

	add_vrf_and_check $testns vrf101 100

	add_vrf_and_check $testns vrf102 100

	#the strict_mode is disabled in the $testns
}

vrf_strict_mode_tests_mix()
{
	log_section "VRF strict_mode test mixing init and testns network namespaces"

	read_strict_mode_compare_and_check init 1

	read_strict_mode_compare_and_check $testns 0

	del_vrf_and_check $testns vrf101

	del_vrf_and_check $testns vrf102

	disable_strict_mode_and_check init

	enable_strict_mode_and_check $testns

	enable_strict_mode_and_check init
	enable_strict_mode_and_check init

	disable_strict_mode_and_check $testns
	disable_strict_mode_and_check $testns

	read_strict_mode_compare_and_check init 1

	read_strict_mode_compare_and_check $testns 0
}

################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

	-t <test>	Test(s) to run (default: all)
			(options: $TESTS)
EOF
}

################################################################################
# main

while getopts ":t:h" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

vrf_strict_mode_check_support()
{
	local nsname=$1
	local output
	local rc

	output="$(lsmod | grep '^vrf' | awk '{print $1}')"
	if [ -z "${output}" ]; then
		modinfo vrf || return $?
	fi

	# we do not care about the value of the strict_mode; we only check if
	# the strict_mode parameter is available or not.
	read_strict_mode ${nsname} &>/dev/null; rc=$?
	log_test ${rc} 0 "${nsname}: net.vrf.strict_mode is available"

	return ${rc}
}

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

modprobe vrf &>/dev/null
if [ ! -e /proc/sys/net/vrf/strict_mode ]; then
	echo "SKIP: vrf sysctl does not exist"
	exit $ksft_skip
fi

cleanup &> /dev/null

setup
for t in $TESTS
do
	case $t in
	vrf_strict_mode_tests_init|init) vrf_strict_mode_tests_init;;
	vrf_strict_mode_tests_testns|testns) vrf_strict_mode_tests_testns;;
	vrf_strict_mode_tests_mix|mix) vrf_strict_mode_tests_mix;;

	help) echo "Test names: $TESTS"; exit 0;;

	esac
done
cleanup

print_log_test_results

exit $ret
