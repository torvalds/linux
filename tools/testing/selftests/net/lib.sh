#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

##############################################################################
# Defines

: "${WAIT_TIMEOUT:=20}"

BUSYWAIT_TIMEOUT=$((WAIT_TIMEOUT * 1000)) # ms

# Kselftest framework constants.
ksft_pass=0
ksft_fail=1
ksft_xfail=2
ksft_skip=4

# namespace list created by setup_ns
NS_LIST=""

##############################################################################
# Helpers

__ksft_status_merge()
{
	local a=$1; shift
	local b=$1; shift
	local -A weights
	local weight=0

	for i in "$@"; do
		weights[$i]=$((weight++))
	done

	if [[ ${weights[$a]} > ${weights[$b]} ]]; then
		echo "$a"
		return 0
	else
		echo "$b"
		return 1
	fi
}

ksft_status_merge()
{
	local a=$1; shift
	local b=$1; shift

	__ksft_status_merge "$a" "$b" \
		$ksft_pass $ksft_xfail $ksft_skip $ksft_fail
}

ksft_exit_status_merge()
{
	local a=$1; shift
	local b=$1; shift

	__ksft_status_merge "$a" "$b" \
		$ksft_xfail $ksft_pass $ksft_skip $ksft_fail
}

busywait()
{
	local timeout=$1; shift

	local start_time="$(date -u +%s%3N)"
	while true
	do
		local out
		out=$("$@")
		local ret=$?
		if ((!ret)); then
			echo -n "$out"
			return 0
		fi

		local current_time="$(date -u +%s%3N)"
		if ((current_time - start_time > timeout)); then
			echo -n "$out"
			return 1
		fi
	done
}

cleanup_ns()
{
	local ns=""
	local errexit=0
	local ret=0

	# disable errexit temporary
	if [[ $- =~ "e" ]]; then
		errexit=1
		set +e
	fi

	for ns in "$@"; do
		ip netns delete "${ns}" &> /dev/null
		if ! busywait $BUSYWAIT_TIMEOUT ip netns list \| grep -vq "^$ns$" &> /dev/null; then
			echo "Warn: Failed to remove namespace $ns"
			ret=1
		fi
	done

	[ $errexit -eq 1 ] && set -e
	return $ret
}

cleanup_all_ns()
{
	cleanup_ns $NS_LIST
}

# setup netns with given names as prefix. e.g
# setup_ns local remote
setup_ns()
{
	local ns=""
	local ns_name=""
	local ns_list=""
	for ns_name in "$@"; do
		# Some test may setup/remove same netns multi times
		if unset ${ns_name} 2> /dev/null; then
			ns="${ns_name,,}-$(mktemp -u XXXXXX)"
			eval readonly ${ns_name}="$ns"
		else
			eval ns='$'${ns_name}
			cleanup_ns "$ns"

		fi

		if ! ip netns add "$ns"; then
			echo "Failed to create namespace $ns_name"
			cleanup_ns "$ns_list"
			return $ksft_skip
		fi
		ip -n "$ns" link set lo up
		ns_list="$ns_list $ns"
	done
	NS_LIST="$NS_LIST $ns_list"
}
