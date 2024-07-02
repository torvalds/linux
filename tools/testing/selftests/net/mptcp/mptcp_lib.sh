#! /bin/bash
# SPDX-License-Identifier: GPL-2.0

readonly KSFT_PASS=0
readonly KSFT_FAIL=1
readonly KSFT_SKIP=4

# shellcheck disable=SC2155 # declare and assign separately
readonly KSFT_TEST="${MPTCP_LIB_KSFT_TEST:-$(basename "${0}" .sh)}"

# These variables are used in some selftests, read-only
declare -rx MPTCP_LIB_EVENT_ANNOUNCED=6         # MPTCP_EVENT_ANNOUNCED
declare -rx MPTCP_LIB_EVENT_REMOVED=7           # MPTCP_EVENT_REMOVED
declare -rx MPTCP_LIB_EVENT_SUB_ESTABLISHED=10  # MPTCP_EVENT_SUB_ESTABLISHED
declare -rx MPTCP_LIB_EVENT_SUB_CLOSED=11       # MPTCP_EVENT_SUB_CLOSED
declare -rx MPTCP_LIB_EVENT_LISTENER_CREATED=15 # MPTCP_EVENT_LISTENER_CREATED
declare -rx MPTCP_LIB_EVENT_LISTENER_CLOSED=16  # MPTCP_EVENT_LISTENER_CLOSED

declare -rx MPTCP_LIB_AF_INET=2
declare -rx MPTCP_LIB_AF_INET6=10

MPTCP_LIB_SUBTESTS=()
MPTCP_LIB_SUBTESTS_DUPLICATED=0
MPTCP_LIB_TEST_COUNTER=0
MPTCP_LIB_TEST_FORMAT="%02u %-50s"
MPTCP_LIB_IP_MPTCP=0

# only if supported (or forced) and not disabled, see no-color.org
if { [ -t 1 ] || [ "${SELFTESTS_MPTCP_LIB_COLOR_FORCE:-}" = "1" ]; } &&
   [ "${NO_COLOR:-}" != "1" ]; then
	readonly MPTCP_LIB_COLOR_RED="\E[1;31m"
	readonly MPTCP_LIB_COLOR_GREEN="\E[1;32m"
	readonly MPTCP_LIB_COLOR_YELLOW="\E[1;33m"
	readonly MPTCP_LIB_COLOR_BLUE="\E[1;34m"
	readonly MPTCP_LIB_COLOR_RESET="\E[0m"
else
	readonly MPTCP_LIB_COLOR_RED=
	readonly MPTCP_LIB_COLOR_GREEN=
	readonly MPTCP_LIB_COLOR_YELLOW=
	readonly MPTCP_LIB_COLOR_BLUE=
	readonly MPTCP_LIB_COLOR_RESET=
fi

# $1: color, $2: text
mptcp_lib_print_color() {
	echo -e "${MPTCP_LIB_START_PRINT:-}${*}${MPTCP_LIB_COLOR_RESET}"
}

mptcp_lib_print_ok() {
	mptcp_lib_print_color "${MPTCP_LIB_COLOR_GREEN}${*}"
}

mptcp_lib_print_warn() {
	mptcp_lib_print_color "${MPTCP_LIB_COLOR_YELLOW}${*}"
}

mptcp_lib_print_info() {
	mptcp_lib_print_color "${MPTCP_LIB_COLOR_BLUE}${*}"
}

mptcp_lib_print_err() {
	mptcp_lib_print_color "${MPTCP_LIB_COLOR_RED}${*}"
}

# shellcheck disable=SC2120 # parameters are optional
mptcp_lib_pr_ok() {
	mptcp_lib_print_ok "[ OK ]${1:+ ${*}}"
}

mptcp_lib_pr_skip() {
	mptcp_lib_print_warn "[SKIP]${1:+ ${*}}"
}

mptcp_lib_pr_fail() {
	mptcp_lib_print_err "[FAIL]${1:+ ${*}}"
}

mptcp_lib_pr_info() {
	mptcp_lib_print_info "INFO: ${*}"
}

# SELFTESTS_MPTCP_LIB_EXPECT_ALL_FEATURES env var can be set when validating all
# features using the last version of the kernel and the selftests to make sure
# a test is not being skipped by mistake.
mptcp_lib_expect_all_features() {
	[ "${SELFTESTS_MPTCP_LIB_EXPECT_ALL_FEATURES:-}" = "1" ]
}

# $1: msg
mptcp_lib_fail_if_expected_feature() {
	if mptcp_lib_expect_all_features; then
		echo "ERROR: missing feature: ${*}"
		exit ${KSFT_FAIL}
	fi

	return 1
}

# $1: file
mptcp_lib_has_file() {
	local f="${1}"

	if [ -f "${f}" ]; then
		return 0
	fi

	mptcp_lib_fail_if_expected_feature "${f} file not found"
}

mptcp_lib_check_mptcp() {
	if ! mptcp_lib_has_file "/proc/sys/net/mptcp/enabled"; then
		mptcp_lib_pr_skip "MPTCP support is not available"
		exit ${KSFT_SKIP}
	fi
}

mptcp_lib_check_kallsyms() {
	if ! mptcp_lib_has_file "/proc/kallsyms"; then
		mptcp_lib_pr_skip "CONFIG_KALLSYMS is missing"
		exit ${KSFT_SKIP}
	fi
}

# Internal: use mptcp_lib_kallsyms_has() instead
__mptcp_lib_kallsyms_has() {
	local sym="${1}"

	mptcp_lib_check_kallsyms

	grep -q " ${sym}" /proc/kallsyms
}

# $1: part of a symbol to look at, add '$' at the end for full name
mptcp_lib_kallsyms_has() {
	local sym="${1}"

	if __mptcp_lib_kallsyms_has "${sym}"; then
		return 0
	fi

	mptcp_lib_fail_if_expected_feature "${sym} symbol not found"
}

# $1: part of a symbol to look at, add '$' at the end for full name
mptcp_lib_kallsyms_doesnt_have() {
	local sym="${1}"

	if ! __mptcp_lib_kallsyms_has "${sym}"; then
		return 0
	fi

	mptcp_lib_fail_if_expected_feature "${sym} symbol has been found"
}

# !!!AVOID USING THIS!!!
# Features might not land in the expected version and features can be backported
#
# $1: kernel version, e.g. 6.3
mptcp_lib_kversion_ge() {
	local exp_maj="${1%.*}"
	local exp_min="${1#*.}"
	local v maj min

	# If the kernel has backported features, set this env var to 1:
	if [ "${SELFTESTS_MPTCP_LIB_NO_KVERSION_CHECK:-}" = "1" ]; then
		return 0
	fi

	v=$(uname -r | cut -d'.' -f1,2)
	maj=${v%.*}
	min=${v#*.}

	if   [ "${maj}" -gt "${exp_maj}" ] ||
	   { [ "${maj}" -eq "${exp_maj}" ] && [ "${min}" -ge "${exp_min}" ]; }; then
		return 0
	fi

	mptcp_lib_fail_if_expected_feature "kernel version ${1} lower than ${v}"
}

__mptcp_lib_result_check_duplicated() {
	local subtest

	for subtest in "${MPTCP_LIB_SUBTESTS[@]}"; do
		if [[ "${subtest}" == *" - ${KSFT_TEST}: ${*%% #*}" ]]; then
			MPTCP_LIB_SUBTESTS_DUPLICATED=1
			mptcp_lib_print_err "Duplicated entry: ${*}"
			break
		fi
	done
}

__mptcp_lib_result_add() {
	local result="${1}"
	shift

	local id=$((${#MPTCP_LIB_SUBTESTS[@]} + 1))

	__mptcp_lib_result_check_duplicated "${*}"

	MPTCP_LIB_SUBTESTS+=("${result} ${id} - ${KSFT_TEST}: ${*}")
}

# $1: test name
mptcp_lib_result_pass() {
	__mptcp_lib_result_add "ok" "${1}"
}

# $1: test name
mptcp_lib_result_fail() {
	__mptcp_lib_result_add "not ok" "${1}"
}

# $1: test name
mptcp_lib_result_skip() {
	__mptcp_lib_result_add "ok" "${1} # SKIP"
}

# $1: result code ; $2: test name
mptcp_lib_result_code() {
	local ret="${1}"
	local name="${2}"

	case "${ret}" in
		"${KSFT_PASS}")
			mptcp_lib_result_pass "${name}"
			;;
		"${KSFT_FAIL}")
			mptcp_lib_result_fail "${name}"
			;;
		"${KSFT_SKIP}")
			mptcp_lib_result_skip "${name}"
			;;
		*)
			echo "ERROR: wrong result code: ${ret}"
			exit ${KSFT_FAIL}
			;;
	esac
}

mptcp_lib_result_print_all_tap() {
	local subtest

	if [ ${#MPTCP_LIB_SUBTESTS[@]} -eq 0 ] ||
	   [ "${SELFTESTS_MPTCP_LIB_NO_TAP:-}" = "1" ]; then
		return
	fi

	printf "\nTAP version 13\n"
	printf "1..%d\n" "${#MPTCP_LIB_SUBTESTS[@]}"

	for subtest in "${MPTCP_LIB_SUBTESTS[@]}"; do
		printf "%s\n" "${subtest}"
	done

	if [ "${MPTCP_LIB_SUBTESTS_DUPLICATED}" = 1 ] &&
	   mptcp_lib_expect_all_features; then
		mptcp_lib_print_err "Duplicated test entries"
		exit ${KSFT_FAIL}
	fi
}

# get the value of keyword $1 in the line marked by keyword $2
mptcp_lib_get_info_value() {
	grep "${2}" | sed -n 's/.*\('"${1}"':\)\([0-9a-f:.]*\).*$/\2/p;q'
}

# $1: info name ; $2: evts_ns ; [$3: event type; [$4: addr]]
mptcp_lib_evts_get_info() {
	grep "${4:-}" "${2}" | mptcp_lib_get_info_value "${1}" "^type:${3:-1},"
}

# $1: PID
mptcp_lib_kill_wait() {
	[ "${1}" -eq 0 ] && return 0

	kill -SIGUSR1 "${1}" > /dev/null 2>&1
	kill "${1}" > /dev/null 2>&1
	wait "${1}" 2>/dev/null
}

# $1: IP address
mptcp_lib_is_v6() {
	[ -z "${1##*:*}" ]
}

# $1: ns, $2: MIB counter
mptcp_lib_get_counter() {
	local ns="${1}"
	local counter="${2}"
	local count

	count=$(ip netns exec "${ns}" nstat -asz "${counter}" |
		awk 'NR==1 {next} {print $2}')
	if [ -z "${count}" ]; then
		mptcp_lib_fail_if_expected_feature "${counter} counter"
		return 1
	fi

	echo "${count}"
}

mptcp_lib_make_file() {
	local name="${1}"
	local bs="${2}"
	local size="${3}"

	dd if=/dev/urandom of="${name}" bs="${bs}" count="${size}" 2> /dev/null
	echo -e "\nMPTCP_TEST_FILE_END_MARKER" >> "${name}"
}

# $1: file
mptcp_lib_print_file_err() {
	ls -l "${1}" 1>&2
	echo "Trailing bytes are: "
	tail -c 27 "${1}"
}

# $1: input file ; $2: output file ; $3: what kind of file
mptcp_lib_check_transfer() {
	local in="${1}"
	local out="${2}"
	local what="${3}"

	if ! cmp "$in" "$out" > /dev/null 2>&1; then
		mptcp_lib_pr_fail "$what does not match (in, out):"
		mptcp_lib_print_file_err "$in"
		mptcp_lib_print_file_err "$out"

		return 1
	fi

	return 0
}

# $1: ns, $2: port
mptcp_lib_wait_local_port_listen() {
	local listener_ns="${1}"
	local port="${2}"

	local port_hex
	port_hex="$(printf "%04X" "${port}")"

	local _
	for _ in $(seq 10); do
		ip netns exec "${listener_ns}" cat /proc/net/tcp* | \
			awk "BEGIN {rc=1} {if (\$2 ~ /:${port_hex}\$/ && \$4 ~ /0A/) \
			     {rc=0; exit}} END {exit rc}" &&
			break
		sleep 0.1
	done
}

mptcp_lib_check_output() {
	local err="${1}"
	local cmd="${2}"
	local expected="${3}"
	local cmd_ret=0
	local out

	if ! out=$(${cmd} 2>"${err}"); then
		cmd_ret=${?}
	fi

	if [ ${cmd_ret} -ne 0 ]; then
		mptcp_lib_pr_fail "command execution '${cmd}' stderr"
		cat "${err}"
		return 2
	elif [ "${out}" = "${expected}" ]; then
		return 0
	else
		mptcp_lib_pr_fail "expected '${expected}' got '${out}'"
		return 1
	fi
}

mptcp_lib_check_tools() {
	local tool

	for tool in "${@}"; do
		case "${tool}" in
		"ip")
			if ! ip -Version &> /dev/null; then
				mptcp_lib_pr_skip "Could not run test without ip tool"
				exit ${KSFT_SKIP}
			fi
			;;
		"tc")
			if ! tc -help &> /dev/null; then
				mptcp_lib_pr_skip "Could not run test without tc tool"
				exit ${KSFT_SKIP}
			fi
			;;
		"ss")
			if ! ss -h | grep -q MPTCP; then
				mptcp_lib_pr_skip "ss tool does not support MPTCP"
				exit ${KSFT_SKIP}
			fi
			;;
		"iptables"* | "ip6tables"*)
			if ! "${tool}" -V &> /dev/null; then
				mptcp_lib_pr_skip "Could not run all tests without ${tool}"
				exit ${KSFT_SKIP}
			fi
			;;
		*)
			mptcp_lib_pr_fail "Internal error: unsupported tool: ${tool}"
			exit ${KSFT_FAIL}
			;;
		esac
	done
}

mptcp_lib_ns_init() {
	local sec rndh

	sec=$(date +%s)
	rndh=$(printf %x "${sec}")-$(mktemp -u XXXXXX)

	local netns
	for netns in "${@}"; do
		eval "${netns}=${netns}-${rndh}"

		ip netns add "${!netns}" || exit ${KSFT_SKIP}
		ip -net "${!netns}" link set lo up
		ip netns exec "${!netns}" sysctl -q net.mptcp.enabled=1
		ip netns exec "${!netns}" sysctl -q net.ipv4.conf.all.rp_filter=0
		ip netns exec "${!netns}" sysctl -q net.ipv4.conf.default.rp_filter=0
	done
}

mptcp_lib_ns_exit() {
	local netns
	for netns in "${@}"; do
		ip netns del "${netns}"
		rm -f /tmp/"${netns}".{nstat,out}
	done
}

mptcp_lib_events() {
	local ns="${1}"
	local evts="${2}"
	declare -n pid="${3}"

	:>"${evts}"

	mptcp_lib_kill_wait "${pid:-0}"
	ip netns exec "${ns}" ./pm_nl_ctl events >> "${evts}" 2>&1 &
	pid=$!
}

mptcp_lib_print_title() {
	: "${MPTCP_LIB_TEST_COUNTER:?}"
	: "${MPTCP_LIB_TEST_FORMAT:?}"

	# shellcheck disable=SC2059 # the format is in a variable
	printf "${MPTCP_LIB_TEST_FORMAT}" "$((++MPTCP_LIB_TEST_COUNTER))" "${*}"
}

# $1: var name ; $2: prev ret
mptcp_lib_check_expected_one() {
	local var="${1}"
	local exp="e_${var}"
	local prev_ret="${2}"

	if [ "${!var}" = "${!exp}" ]; then
		return 0
	fi

	if [ "${prev_ret}" = "0" ]; then
		mptcp_lib_pr_fail
	fi

	mptcp_lib_print_err "Expected value for '${var}': '${!exp}', got '${!var}'."
	return 1
}

# $@: all var names to check
mptcp_lib_check_expected() {
	local rc=0
	local var

	for var in "${@}"; do
		mptcp_lib_check_expected_one "${var}" "${rc}" || rc=1
	done

	return "${rc}"
}

# shellcheck disable=SC2034 # Some variables are used below but indirectly
mptcp_lib_verify_listener_events() {
	local evt=${1}
	local e_type=${2}
	local e_family=${3}
	local e_saddr=${4}
	local e_sport=${5}
	local type
	local family
	local saddr
	local sport
	local rc=0

	type=$(mptcp_lib_evts_get_info type "${evt}" "${e_type}")
	family=$(mptcp_lib_evts_get_info family "${evt}" "${e_type}")
	if [ "${family}" ] && [ "${family}" = "${AF_INET6}" ]; then
		saddr=$(mptcp_lib_evts_get_info saddr6 "${evt}" "${e_type}")
	else
		saddr=$(mptcp_lib_evts_get_info saddr4 "${evt}" "${e_type}")
	fi
	sport=$(mptcp_lib_evts_get_info sport "${evt}" "${e_type}")

	mptcp_lib_check_expected "type" "family" "saddr" "sport" || rc="${?}"
	return "${rc}"
}

mptcp_lib_set_ip_mptcp() {
	MPTCP_LIB_IP_MPTCP=1
}

mptcp_lib_is_ip_mptcp() {
	[ "${MPTCP_LIB_IP_MPTCP}" = "1" ]
}

# format: <id>,<ip>,<flags>,<dev>
mptcp_lib_pm_nl_format_endpoints() {
	local entry id ip flags dev port

	for entry in "${@}"; do
		IFS=, read -r id ip flags dev port <<< "${entry}"
		if mptcp_lib_is_ip_mptcp; then
			echo -n "${ip}"
			[ -n "${port}" ] && echo -n " port ${port}"
			echo -n " id ${id}"
			[ -n "${flags}" ] && echo -n " ${flags}"
			[ -n "${dev}" ] && echo -n " dev ${dev}"
			echo " " # always a space at the end
		else
			echo -n "id ${id}"
			echo -n " flags ${flags//" "/","}"
			[ -n "${dev}" ] && echo -n " dev ${dev}"
			echo -n " ${ip}"
			[ -n "${port}" ] && echo -n " ${port}"
			echo ""
		fi
	done
}

mptcp_lib_pm_nl_get_endpoint() {
	local ns=${1}
	local id=${2}

	if mptcp_lib_is_ip_mptcp; then
		ip -n "${ns}" mptcp endpoint show id "${id}"
	else
		ip netns exec "${ns}" ./pm_nl_ctl get "${id}"
	fi
}

mptcp_lib_pm_nl_set_limits() {
	local ns=${1}
	local addrs=${2}
	local subflows=${3}

	if mptcp_lib_is_ip_mptcp; then
		ip -n "${ns}" mptcp limits set add_addr_accepted "${addrs}" subflows "${subflows}"
	else
		ip netns exec "${ns}" ./pm_nl_ctl limits "${addrs}" "${subflows}"
	fi
}

mptcp_lib_pm_nl_add_endpoint() {
	local ns=${1}
	local addr=${2}
	local flags dev id port
	local nr=2

	local p
	for p in "${@}"; do
		case "${p}" in
		"flags" | "dev" | "id" | "port")
			eval "${p}"=\$"${nr}"
			;;
		esac

		nr=$((nr + 1))
	done

	if mptcp_lib_is_ip_mptcp; then
		# shellcheck disable=SC2086 # blanks in flags, no double quote
		ip -n "${ns}" mptcp endpoint add "${addr}" ${flags//","/" "} \
			${dev:+dev "${dev}"} ${id:+id "${id}"} ${port:+port "${port}"}
	else
		ip netns exec "${ns}" ./pm_nl_ctl add "${addr}" ${flags:+flags "${flags}"} \
			${dev:+dev "${dev}"} ${id:+id "${id}"} ${port:+port "${port}"}
	fi
}

mptcp_lib_pm_nl_del_endpoint() {
	local ns=${1}
	local id=${2}
	local addr=${3}

	if mptcp_lib_is_ip_mptcp; then
		[ "${id}" -ne 0 ] && addr=''
		ip -n "${ns}" mptcp endpoint delete id "${id}" ${addr:+"${addr}"}
	else
		ip netns exec "${ns}" ./pm_nl_ctl del "${id}" "${addr}"
	fi
}

mptcp_lib_pm_nl_flush_endpoint() {
	local ns=${1}

	if mptcp_lib_is_ip_mptcp; then
		ip -n "${ns}" mptcp endpoint flush
	else
		ip netns exec "${ns}" ./pm_nl_ctl flush
	fi
}

mptcp_lib_pm_nl_show_endpoints() {
	local ns=${1}

	if mptcp_lib_is_ip_mptcp; then
		ip -n "${ns}" mptcp endpoint show
	else
		ip netns exec "${ns}" ./pm_nl_ctl dump
	fi
}

mptcp_lib_pm_nl_change_endpoint() {
	local ns=${1}
	local id=${2}
	local flags=${3}

	if mptcp_lib_is_ip_mptcp; then
		# shellcheck disable=SC2086 # blanks in flags, no double quote
		ip -n "${ns}" mptcp endpoint change id "${id}" ${flags//","/" "}
	else
		ip netns exec "${ns}" ./pm_nl_ctl set id "${id}" flags "${flags}"
	fi
}
