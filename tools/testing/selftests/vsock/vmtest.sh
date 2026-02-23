#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Meta Platforms, Inc. and affiliates
#
# Dependencies:
#		* virtme-ng
#		* busybox-static (used by virtme-ng)
#		* qemu	(used by virtme-ng)
#		* socat
#
# shellcheck disable=SC2317,SC2119

readonly SCRIPT_DIR="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly KERNEL_CHECKOUT=$(realpath "${SCRIPT_DIR}"/../../../../)

source "${SCRIPT_DIR}"/../kselftest/ktap_helpers.sh

readonly VSOCK_TEST="${SCRIPT_DIR}"/vsock_test
readonly TEST_GUEST_PORT=51000
readonly TEST_HOST_PORT=50000
readonly TEST_HOST_PORT_LISTENER=50001
readonly SSH_GUEST_PORT=22
readonly SSH_HOST_PORT=2222
readonly VSOCK_CID=1234
readonly WAIT_PERIOD=3
readonly WAIT_PERIOD_MAX=60
readonly WAIT_QEMU=5
readonly PIDFILE_TEMPLATE=/tmp/vsock_vmtest_XXXX.pid
declare -A PIDFILES

# virtme-ng offers a netdev for ssh when using "--ssh", but we also need a
# control port forwarded for vsock_test.  Because virtme-ng doesn't support
# adding an additional port to forward to the device created from "--ssh" and
# virtme-init mistakenly sets identical IPs to the ssh device and additional
# devices, we instead opt out of using --ssh, add the device manually, and also
# add the kernel cmdline options that virtme-init uses to setup the interface.
readonly QEMU_TEST_PORT_FWD="hostfwd=tcp::${TEST_HOST_PORT}-:${TEST_GUEST_PORT}"
readonly QEMU_SSH_PORT_FWD="hostfwd=tcp::${SSH_HOST_PORT}-:${SSH_GUEST_PORT}"
readonly KERNEL_CMDLINE="\
	virtme.dhcp net.ifnames=0 biosdevname=0 \
	virtme.ssh virtme_ssh_channel=tcp virtme_ssh_user=$USER \
"
readonly LOG=$(mktemp /tmp/vsock_vmtest_XXXX.log)

# Namespace tests must use the ns_ prefix. This is checked in check_netns() and
# is used to determine if a test needs namespace setup before test execution.
readonly TEST_NAMES=(
	vm_server_host_client
	vm_client_host_server
	vm_loopback
	ns_host_vsock_ns_mode_ok
	ns_host_vsock_child_ns_mode_ok
	ns_global_same_cid_fails
	ns_local_same_cid_ok
	ns_global_local_same_cid_ok
	ns_local_global_same_cid_ok
	ns_diff_global_host_connect_to_global_vm_ok
	ns_diff_global_host_connect_to_local_vm_fails
	ns_diff_global_vm_connect_to_global_host_ok
	ns_diff_global_vm_connect_to_local_host_fails
	ns_diff_local_host_connect_to_local_vm_fails
	ns_diff_local_vm_connect_to_local_host_fails
	ns_diff_global_to_local_loopback_local_fails
	ns_diff_local_to_global_loopback_fails
	ns_diff_local_to_local_loopback_fails
	ns_diff_global_to_global_loopback_ok
	ns_same_local_loopback_ok
	ns_same_local_host_connect_to_local_vm_ok
	ns_same_local_vm_connect_to_local_host_ok
	ns_delete_vm_ok
	ns_delete_host_ok
	ns_delete_both_ok
)
readonly TEST_DESCS=(
	# vm_server_host_client
	"Run vsock_test in server mode on the VM and in client mode on the host."

	# vm_client_host_server
	"Run vsock_test in client mode on the VM and in server mode on the host."

	# vm_loopback
	"Run vsock_test using the loopback transport in the VM."

	# ns_host_vsock_ns_mode_ok
	"Check /proc/sys/net/vsock/ns_mode strings on the host."

	# ns_host_vsock_child_ns_mode_ok
	"Check /proc/sys/net/vsock/ns_mode is read-only and child_ns_mode is writable."

	# ns_global_same_cid_fails
	"Check QEMU fails to start two VMs with same CID in two different global namespaces."

	# ns_local_same_cid_ok
	"Check QEMU successfully starts two VMs with same CID in two different local namespaces."

	# ns_global_local_same_cid_ok
	"Check QEMU successfully starts one VM in a global ns and then another VM in a local ns with the same CID."

	# ns_local_global_same_cid_ok
	"Check QEMU successfully starts one VM in a local ns and then another VM in a global ns with the same CID."

	# ns_diff_global_host_connect_to_global_vm_ok
	"Run vsock_test client in global ns with server in VM in another global ns."

	# ns_diff_global_host_connect_to_local_vm_fails
	"Run socat to test a process in a global ns fails to connect to a VM in a local ns."

	# ns_diff_global_vm_connect_to_global_host_ok
	"Run vsock_test client in VM in a global ns with server in another global ns."

	# ns_diff_global_vm_connect_to_local_host_fails
	"Run socat to test a VM in a global ns fails to connect to a host process in a local ns."

	# ns_diff_local_host_connect_to_local_vm_fails
	"Run socat to test a host process in a local ns fails to connect to a VM in another local ns."

	# ns_diff_local_vm_connect_to_local_host_fails
	"Run socat to test a VM in a local ns fails to connect to a host process in another local ns."

	# ns_diff_global_to_local_loopback_local_fails
	"Run socat to test a loopback vsock in a global ns fails to connect to a vsock in a local ns."

	# ns_diff_local_to_global_loopback_fails
	"Run socat to test a loopback vsock in a local ns fails to connect to a vsock in a global ns."

	# ns_diff_local_to_local_loopback_fails
	"Run socat to test a loopback vsock in a local ns fails to connect to a vsock in another local ns."

	# ns_diff_global_to_global_loopback_ok
	"Run socat to test a loopback vsock in a global ns successfully connects to a vsock in another global ns."

	# ns_same_local_loopback_ok
	"Run socat to test a loopback vsock in a local ns successfully connects to a vsock in the same ns."

	# ns_same_local_host_connect_to_local_vm_ok
	"Run vsock_test client in a local ns with server in VM in same ns."

	# ns_same_local_vm_connect_to_local_host_ok
	"Run vsock_test client in VM in a local ns with server in same ns."

	# ns_delete_vm_ok
	"Check that deleting the VM's namespace does not break the socket connection"

	# ns_delete_host_ok
	"Check that deleting the host's namespace does not break the socket connection"

	# ns_delete_both_ok
	"Check that deleting the VM and host's namespaces does not break the socket connection"
)

readonly USE_SHARED_VM=(
	vm_server_host_client
	vm_client_host_server
	vm_loopback
)
readonly NS_MODES=("local" "global")

VERBOSE=0

usage() {
	local name
	local desc
	local i

	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  -b: build the kernel from the current source tree and use it for guest VMs"
	echo "  -q: set the path to or name of qemu binary"
	echo "  -v: verbose output"
	echo
	echo "Available tests"

	for ((i = 0; i < ${#TEST_NAMES[@]}; i++)); do
		name=${TEST_NAMES[${i}]}
		desc=${TEST_DESCS[${i}]}
		printf "\t%-55s%-35s\n" "${name}" "${desc}"
	done
	echo

	exit 1
}

die() {
	echo "$*" >&2
	exit "${KSFT_FAIL}"
}

check_result() {
	local rc arg

	rc=$1
	arg=$2

	cnt_total=$(( cnt_total + 1 ))

	if [[ ${rc} -eq ${KSFT_PASS} ]]; then
		cnt_pass=$(( cnt_pass + 1 ))
		echo "ok ${cnt_total} ${arg}"
	elif [[ ${rc} -eq ${KSFT_SKIP} ]]; then
		cnt_skip=$(( cnt_skip + 1 ))
		echo "ok ${cnt_total} ${arg} # SKIP"
	elif [[ ${rc} -eq ${KSFT_FAIL} ]]; then
		cnt_fail=$(( cnt_fail + 1 ))
		echo "not ok ${cnt_total} ${arg} # exit=${rc}"
	fi
}

add_namespaces() {
	local orig_mode
	orig_mode=$(cat /proc/sys/net/vsock/child_ns_mode)

	for mode in "${NS_MODES[@]}"; do
		echo "${mode}" > /proc/sys/net/vsock/child_ns_mode
		ip netns add "${mode}0" 2>/dev/null
		ip netns add "${mode}1" 2>/dev/null
	done

	echo "${orig_mode}" > /proc/sys/net/vsock/child_ns_mode
}

init_namespaces() {
	for mode in "${NS_MODES[@]}"; do
		# we need lo for qemu port forwarding
		ip netns exec "${mode}0" ip link set dev lo up
		ip netns exec "${mode}1" ip link set dev lo up
	done
}

del_namespaces() {
	for mode in "${NS_MODES[@]}"; do
		ip netns del "${mode}0" &>/dev/null
		ip netns del "${mode}1" &>/dev/null
		log_host "removed ns ${mode}0"
		log_host "removed ns ${mode}1"
	done
}

vm_ssh() {
	local ns_exec

	if [[ "${1}" == init_ns ]]; then
		ns_exec=""
	else
		ns_exec="ip netns exec ${1}"
	fi

	shift

	${ns_exec} ssh -q -o UserKnownHostsFile=/dev/null -p "${SSH_HOST_PORT}" localhost "$@"

	return $?
}

cleanup() {
	terminate_pidfiles "${!PIDFILES[@]}"
	del_namespaces
}

check_args() {
	local found

	for arg in "$@"; do
		found=0
		for name in "${TEST_NAMES[@]}"; do
			if [[ "${name}" = "${arg}" ]]; then
				found=1
				break
			fi
		done

		if [[ "${found}" -eq 0 ]]; then
			echo "${arg} is not an available test" >&2
			usage
		fi
	done

	for arg in "$@"; do
		if ! command -v > /dev/null "test_${arg}"; then
			echo "Test ${arg} not found" >&2
			usage
		fi
	done
}

check_deps() {
	for dep in vng ${QEMU} busybox pkill ssh ss socat; do
		if [[ ! -x $(command -v "${dep}") ]]; then
			echo -e "skip:    dependency ${dep} not found!\n"
			exit "${KSFT_SKIP}"
		fi
	done

	if [[ ! -x $(command -v "${VSOCK_TEST}") ]]; then
		printf "skip:    %s not found!" "${VSOCK_TEST}"
		printf " Please build the kselftest vsock target.\n"
		exit "${KSFT_SKIP}"
	fi
}

check_netns() {
	local tname=$1

	# If the test requires NS support, check if NS support exists
	# using /proc/self/ns
	if [[ "${tname}" =~ ^ns_ ]] &&
	   [[ ! -e /proc/self/ns ]]; then
		log_host "No NS support detected for test ${tname}"
		return 1
	fi

	return 0
}

check_vng() {
	local tested_versions
	local version
	local ok

	tested_versions=("1.33" "1.36" "1.37")
	version="$(vng --version)"

	ok=0
	for tv in "${tested_versions[@]}"; do
		if [[ "${version}" == *"${tv}"* ]]; then
			ok=1
			break
		fi
	done

	if [[ ! "${ok}" -eq 1 ]]; then
		printf "warning: vng version '%s' has not been tested and may " "${version}" >&2
		printf "not function properly.\n\tThe following versions have been tested: " >&2
		echo "${tested_versions[@]}" >&2
	fi
}

check_socat() {
	local support_string

	support_string="$(socat -V)"

	if [[ "${support_string}" != *"WITH_VSOCK 1"* ]]; then
		die "err: socat is missing vsock support"
	fi

	if [[ "${support_string}" != *"WITH_UNIX 1"* ]]; then
		die "err: socat is missing unix support"
	fi
}

handle_build() {
	if [[ ! "${BUILD}" -eq 1 ]]; then
		return
	fi

	if [[ ! -d "${KERNEL_CHECKOUT}" ]]; then
		echo "-b requires vmtest.sh called from the kernel source tree" >&2
		exit 1
	fi

	pushd "${KERNEL_CHECKOUT}" &>/dev/null

	if ! vng --kconfig --config "${SCRIPT_DIR}"/config; then
		die "failed to generate .config for kernel source tree (${KERNEL_CHECKOUT})"
	fi

	if ! make -j$(nproc); then
		die "failed to build kernel from source tree (${KERNEL_CHECKOUT})"
	fi

	popd &>/dev/null
}

create_pidfile() {
	local pidfile

	pidfile=$(mktemp "${PIDFILE_TEMPLATE}")
	PIDFILES["${pidfile}"]=1

	echo "${pidfile}"
}

terminate_pidfiles() {
	local pidfile

	for pidfile in "$@"; do
		if [[ -s "${pidfile}" ]]; then
			pkill -SIGTERM -F "${pidfile}" > /dev/null 2>&1
		fi

		if [[ -e "${pidfile}" ]]; then
			rm -f "${pidfile}"
		fi

		unset "PIDFILES[${pidfile}]"
	done
}

terminate_pids() {
	local pid

	for pid in "$@"; do
		kill -SIGTERM "${pid}" &>/dev/null || :
	done
}

vm_start() {
	local pidfile=$1
	local ns=$2
	local logfile=/dev/null
	local verbose_opt=""
	local kernel_opt=""
	local qemu_opts=""
	local ns_exec=""
	local qemu

	qemu=$(command -v "${QEMU}")

	if [[ "${VERBOSE}" -eq 1 ]]; then
		verbose_opt="--verbose"
		logfile=/dev/stdout
	fi

	qemu_opts="\
		 -netdev user,id=n0,${QEMU_TEST_PORT_FWD},${QEMU_SSH_PORT_FWD} \
		 -device virtio-net-pci,netdev=n0 \
		 -device vhost-vsock-pci,guest-cid=${VSOCK_CID} \
		--pidfile ${pidfile}
	"

	if [[ "${BUILD}" -eq 1 ]]; then
		kernel_opt="${KERNEL_CHECKOUT}"
	fi

	if [[ "${ns}" != "init_ns" ]]; then
		ns_exec="ip netns exec ${ns}"
	fi

	${ns_exec} vng \
		--run \
		${kernel_opt} \
		${verbose_opt} \
		--qemu-opts="${qemu_opts}" \
		--qemu="${qemu}" \
		--user root \
		--append "${KERNEL_CMDLINE}" \
		--rw  &> ${logfile} &

	timeout "${WAIT_QEMU}" \
		bash -c 'while [[ ! -s '"${pidfile}"' ]]; do sleep 1; done; exit 0'
}

vm_wait_for_ssh() {
	local ns=$1
	local i

	i=0
	while true; do
		if [[ ${i} -gt ${WAIT_PERIOD_MAX} ]]; then
			die "Timed out waiting for guest ssh"
		fi

		if vm_ssh "${ns}" -- true; then
			break
		fi
		i=$(( i + 1 ))
		sleep ${WAIT_PERIOD}
	done
}

# derived from selftests/net/net_helper.sh
wait_for_listener()
{
	local port=$1
	local interval=$2
	local max_intervals=$3
	local protocol=$4
	local i

	for i in $(seq "${max_intervals}"); do
		case "${protocol}" in
		tcp)
			if ss --listening --tcp --numeric | grep -q ":${port} "; then
				break
			fi
			;;
		vsock)
			if ss --listening --vsock --numeric | grep -q ":${port} "; then
				break
			fi
			;;
		unix)
			# For unix sockets, port is actually the socket path
			if ss --listening --unix | grep -q "${port}"; then
				break
			fi
			;;
		*)
			echo "Unknown protocol: ${protocol}" >&2
			break
			;;
		esac
		sleep "${interval}"
	done
}

vm_wait_for_listener() {
	local ns=$1
	local port=$2
	local protocol=$3

	vm_ssh "${ns}" <<EOF
$(declare -f wait_for_listener)
wait_for_listener ${port} ${WAIT_PERIOD} ${WAIT_PERIOD_MAX} ${protocol}
EOF
}

host_wait_for_listener() {
	local ns=$1
	local port=$2
	local protocol=$3

	if [[ "${ns}" == "init_ns" ]]; then
		wait_for_listener "${port}" "${WAIT_PERIOD}" "${WAIT_PERIOD_MAX}" "${protocol}"
	else
		ip netns exec "${ns}" bash <<-EOF
			$(declare -f wait_for_listener)
			wait_for_listener ${port} ${WAIT_PERIOD} ${WAIT_PERIOD_MAX} ${protocol}
		EOF
	fi
}

vm_dmesg_oops_count() {
	local ns=$1

	vm_ssh "${ns}" -- dmesg 2>/dev/null | grep -c -i 'Oops'
}

vm_dmesg_warn_count() {
	local ns=$1

	vm_ssh "${ns}" -- dmesg --level=warn 2>/dev/null | grep -c -i 'vsock'
}

vm_dmesg_check() {
	local pidfile=$1
	local ns=$2
	local oops_before=$3
	local warn_before=$4
	local oops_after warn_after

	oops_after=$(vm_dmesg_oops_count "${ns}")
	if [[ "${oops_after}" -gt "${oops_before}" ]]; then
		echo "FAIL: kernel oops detected on vm in ns ${ns}" | log_host
		return 1
	fi

	warn_after=$(vm_dmesg_warn_count "${ns}")
	if [[ "${warn_after}" -gt "${warn_before}" ]]; then
		echo "FAIL: kernel warning detected on vm in ns ${ns}" | log_host
		return 1
	fi

	return 0
}

vm_vsock_test() {
	local ns=$1
	local host=$2
	local cid=$3
	local port=$4
	local rc

	# log output and use pipefail to respect vsock_test errors
	set -o pipefail
	if [[ "${host}" != server ]]; then
		vm_ssh "${ns}" -- "${VSOCK_TEST}" \
			--mode=client \
			--control-host="${host}" \
			--peer-cid="${cid}" \
			--control-port="${port}" \
			2>&1 | log_guest
		rc=$?
	else
		vm_ssh "${ns}" -- "${VSOCK_TEST}" \
			--mode=server \
			--peer-cid="${cid}" \
			--control-port="${port}" \
			2>&1 | log_guest &
		rc=$?

		if [[ $rc -ne 0 ]]; then
			set +o pipefail
			return $rc
		fi

		vm_wait_for_listener "${ns}" "${port}" "tcp"
		rc=$?
	fi
	set +o pipefail

	return $rc
}

host_vsock_test() {
	local ns=$1
	local host=$2
	local cid=$3
	local port=$4
	shift 4
	local extra_args=("$@")
	local rc

	local cmd="${VSOCK_TEST}"
	if [[ "${ns}" != "init_ns" ]]; then
		cmd="ip netns exec ${ns} ${cmd}"
	fi

	# log output and use pipefail to respect vsock_test errors
	set -o pipefail
	if [[ "${host}" != server ]]; then
		${cmd} \
			--mode=client \
			--peer-cid="${cid}" \
			--control-host="${host}" \
			--control-port="${port}" \
			"${extra_args[@]}" 2>&1 | log_host
		rc=$?
	else
		${cmd} \
			--mode=server \
			--peer-cid="${cid}" \
			--control-port="${port}" \
			"${extra_args[@]}" 2>&1 | log_host &
		rc=$?

		if [[ $rc -ne 0 ]]; then
			set +o pipefail
			return $rc
		fi

		host_wait_for_listener "${ns}" "${port}" "tcp"
		rc=$?
	fi
	set +o pipefail

	return $rc
}

log() {
	local redirect
	local prefix

	if [[ ${VERBOSE} -eq 0 ]]; then
		redirect=/dev/null
	else
		redirect=/dev/stdout
	fi

	prefix="${LOG_PREFIX:-}"

	if [[ "$#" -eq 0 ]]; then
		if [[ -n "${prefix}" ]]; then
			awk -v prefix="${prefix}" '{printf "%s: %s\n", prefix, $0}'
		else
			cat
		fi
	else
		if [[ -n "${prefix}" ]]; then
			echo "${prefix}: " "$@"
		else
			echo "$@"
		fi
	fi | tee -a "${LOG}" > "${redirect}"
}

log_host() {
	LOG_PREFIX=host log "$@"
}

log_guest() {
	LOG_PREFIX=guest log "$@"
}

ns_get_mode() {
	local ns=$1

	ip netns exec "${ns}" cat /proc/sys/net/vsock/ns_mode 2>/dev/null
}

test_ns_host_vsock_ns_mode_ok() {
	for mode in "${NS_MODES[@]}"; do
		local actual

		actual=$(ns_get_mode "${mode}0")
		if [[ "${actual}" != "${mode}" ]]; then
			log_host "expected mode ${mode}, got ${actual}"
			return "${KSFT_FAIL}"
		fi
	done

	return "${KSFT_PASS}"
}

test_ns_diff_global_host_connect_to_global_vm_ok() {
	local oops_before warn_before
	local pids pid pidfile
	local ns0 ns1 port
	declare -a pids
	local unixfile
	ns0="global0"
	ns1="global1"
	port=1234
	local rc

	init_namespaces

	pidfile="$(create_pidfile)"

	if ! vm_start "${pidfile}" "${ns0}"; then
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns0}"
	oops_before=$(vm_dmesg_oops_count "${ns0}")
	warn_before=$(vm_dmesg_warn_count "${ns0}")

	unixfile=$(mktemp -u /tmp/XXXX.sock)
	ip netns exec "${ns1}" \
		socat TCP-LISTEN:"${TEST_HOST_PORT}",fork \
			UNIX-CONNECT:"${unixfile}" &
	pids+=($!)
	host_wait_for_listener "${ns1}" "${TEST_HOST_PORT}" "tcp"

	ip netns exec "${ns0}" socat UNIX-LISTEN:"${unixfile}",fork \
		TCP-CONNECT:localhost:"${TEST_HOST_PORT}" &
	pids+=($!)
	host_wait_for_listener "${ns0}" "${unixfile}" "unix"

	vm_vsock_test "${ns0}" "server" 2 "${TEST_GUEST_PORT}"
	vm_wait_for_listener "${ns0}" "${TEST_GUEST_PORT}" "tcp"
	host_vsock_test "${ns1}" "127.0.0.1" "${VSOCK_CID}" "${TEST_HOST_PORT}"
	rc=$?

	vm_dmesg_check "${pidfile}" "${ns0}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pids "${pids[@]}"
	terminate_pidfiles "${pidfile}"

	if [[ "${rc}" -ne 0 ]] || [[ "${dmesg_rc}" -ne 0 ]]; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

test_ns_diff_global_host_connect_to_local_vm_fails() {
	local oops_before warn_before
	local ns0="global0"
	local ns1="local0"
	local port=12345
	local dmesg_rc
	local pidfile
	local result
	local pid

	init_namespaces

	outfile=$(mktemp)

	pidfile="$(create_pidfile)"
	if ! vm_start "${pidfile}" "${ns1}"; then
		log_host "failed to start vm (cid=${VSOCK_CID}, ns=${ns0})"
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns1}"
	oops_before=$(vm_dmesg_oops_count "${ns1}")
	warn_before=$(vm_dmesg_warn_count "${ns1}")

	vm_ssh "${ns1}" -- socat VSOCK-LISTEN:"${port}" STDOUT > "${outfile}" &
	vm_wait_for_listener "${ns1}" "${port}" "vsock"
	echo TEST | ip netns exec "${ns0}" \
		socat STDIN VSOCK-CONNECT:"${VSOCK_CID}":"${port}" 2>/dev/null

	vm_dmesg_check "${pidfile}" "${ns1}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"
	result=$(cat "${outfile}")
	rm -f "${outfile}"

	if [[ "${result}" == "TEST" ]] || [[ "${dmesg_rc}" -ne 0 ]]; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

test_ns_diff_global_vm_connect_to_global_host_ok() {
	local oops_before warn_before
	local ns0="global0"
	local ns1="global1"
	local port=12345
	local unixfile
	local dmesg_rc
	local pidfile
	local pids
	local rc

	init_namespaces

	declare -a pids

	log_host "Setup socat bridge from ns ${ns0} to ns ${ns1} over port ${port}"

	unixfile=$(mktemp -u /tmp/XXXX.sock)

	ip netns exec "${ns0}" \
		socat TCP-LISTEN:"${port}" UNIX-CONNECT:"${unixfile}" &
	pids+=($!)
	host_wait_for_listener "${ns0}" "${port}" "tcp"

	ip netns exec "${ns1}" \
		socat UNIX-LISTEN:"${unixfile}" TCP-CONNECT:127.0.0.1:"${port}" &
	pids+=($!)
	host_wait_for_listener "${ns1}" "${unixfile}" "unix"

	log_host "Launching ${VSOCK_TEST} in ns ${ns1}"
	host_vsock_test "${ns1}" "server" "${VSOCK_CID}" "${port}"

	pidfile="$(create_pidfile)"
	if ! vm_start "${pidfile}" "${ns0}"; then
		log_host "failed to start vm (cid=${cid}, ns=${ns0})"
		terminate_pids "${pids[@]}"
		rm -f "${unixfile}"
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns0}"

	oops_before=$(vm_dmesg_oops_count "${ns0}")
	warn_before=$(vm_dmesg_warn_count "${ns0}")

	vm_vsock_test "${ns0}" "10.0.2.2" 2 "${port}"
	rc=$?

	vm_dmesg_check "${pidfile}" "${ns0}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"
	terminate_pids "${pids[@]}"
	rm -f "${unixfile}"

	if [[ "${rc}" -ne 0 ]] || [[ "${dmesg_rc}" -ne 0 ]]; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"

}

test_ns_diff_global_vm_connect_to_local_host_fails() {
	local ns0="global0"
	local ns1="local0"
	local port=12345
	local oops_before warn_before
	local dmesg_rc
	local pidfile
	local result
	local pid

	init_namespaces

	log_host "Launching socat in ns ${ns1}"
	outfile=$(mktemp)

	ip netns exec "${ns1}" socat VSOCK-LISTEN:"${port}" STDOUT &> "${outfile}" &
	pid=$!
	host_wait_for_listener "${ns1}" "${port}" "vsock"

	pidfile="$(create_pidfile)"
	if ! vm_start "${pidfile}" "${ns0}"; then
		log_host "failed to start vm (cid=${cid}, ns=${ns0})"
		terminate_pids "${pid}"
		rm -f "${outfile}"
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns0}"

	oops_before=$(vm_dmesg_oops_count "${ns0}")
	warn_before=$(vm_dmesg_warn_count "${ns0}")

	vm_ssh "${ns0}" -- \
		bash -c "echo TEST | socat STDIN VSOCK-CONNECT:2:${port}" 2>&1 | log_guest

	vm_dmesg_check "${pidfile}" "${ns0}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"
	terminate_pids "${pid}"

	result=$(cat "${outfile}")
	rm -f "${outfile}"

	if [[ "${result}" != TEST ]] && [[ "${dmesg_rc}" -eq 0 ]]; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_diff_local_host_connect_to_local_vm_fails() {
	local ns0="local0"
	local ns1="local1"
	local port=12345
	local oops_before warn_before
	local dmesg_rc
	local pidfile
	local result
	local pid

	init_namespaces

	outfile=$(mktemp)

	pidfile="$(create_pidfile)"
	if ! vm_start "${pidfile}" "${ns1}"; then
		log_host "failed to start vm (cid=${cid}, ns=${ns0})"
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns1}"
	oops_before=$(vm_dmesg_oops_count "${ns1}")
	warn_before=$(vm_dmesg_warn_count "${ns1}")

	vm_ssh "${ns1}" -- socat VSOCK-LISTEN:"${port}" STDOUT > "${outfile}" &
	vm_wait_for_listener "${ns1}" "${port}" "vsock"

	echo TEST | ip netns exec "${ns0}" \
		socat STDIN VSOCK-CONNECT:"${VSOCK_CID}":"${port}" 2>/dev/null

	vm_dmesg_check "${pidfile}" "${ns1}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"

	result=$(cat "${outfile}")
	rm -f "${outfile}"

	if [[ "${result}" != TEST ]] && [[ "${dmesg_rc}" -eq 0 ]]; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_diff_local_vm_connect_to_local_host_fails() {
	local oops_before warn_before
	local ns0="local0"
	local ns1="local1"
	local port=12345
	local dmesg_rc
	local pidfile
	local result
	local pid

	init_namespaces

	log_host "Launching socat in ns ${ns1}"
	outfile=$(mktemp)
	ip netns exec "${ns1}" socat VSOCK-LISTEN:"${port}" STDOUT &> "${outfile}" &
	pid=$!
	host_wait_for_listener "${ns1}" "${port}" "vsock"

	pidfile="$(create_pidfile)"
	if ! vm_start "${pidfile}" "${ns0}"; then
		log_host "failed to start vm (cid=${cid}, ns=${ns0})"
		rm -f "${outfile}"
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns0}"
	oops_before=$(vm_dmesg_oops_count "${ns0}")
	warn_before=$(vm_dmesg_warn_count "${ns0}")

	vm_ssh "${ns0}" -- \
		bash -c "echo TEST | socat STDIN VSOCK-CONNECT:2:${port}" 2>&1 | log_guest

	vm_dmesg_check "${pidfile}" "${ns0}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"
	terminate_pids "${pid}"

	result=$(cat "${outfile}")
	rm -f "${outfile}"

	if [[ "${result}" != TEST ]] && [[ "${dmesg_rc}" -eq 0 ]]; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

__test_loopback_two_netns() {
	local ns0=$1
	local ns1=$2
	local port=12345
	local result
	local pid

	modprobe vsock_loopback &> /dev/null || :

	log_host "Launching socat in ns ${ns1}"
	outfile=$(mktemp)

	ip netns exec "${ns1}" socat VSOCK-LISTEN:"${port}" STDOUT > "${outfile}" 2>/dev/null &
	pid=$!
	host_wait_for_listener "${ns1}" "${port}" "vsock"

	log_host "Launching socat in ns ${ns0}"
	echo TEST | ip netns exec "${ns0}" socat STDIN VSOCK-CONNECT:1:"${port}" 2>/dev/null
	terminate_pids "${pid}"

	result=$(cat "${outfile}")
	rm -f "${outfile}"

	if [[ "${result}" == TEST ]]; then
		return 0
	fi

	return 1
}

test_ns_diff_global_to_local_loopback_local_fails() {
	init_namespaces

	if ! __test_loopback_two_netns "global0" "local0"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_diff_local_to_global_loopback_fails() {
	init_namespaces

	if ! __test_loopback_two_netns "local0" "global0"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_diff_local_to_local_loopback_fails() {
	init_namespaces

	if ! __test_loopback_two_netns "local0" "local1"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_diff_global_to_global_loopback_ok() {
	init_namespaces

	if __test_loopback_two_netns "global0" "global1"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_same_local_loopback_ok() {
	init_namespaces

	if __test_loopback_two_netns "local0" "local0"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_same_local_host_connect_to_local_vm_ok() {
	local oops_before warn_before
	local ns="local0"
	local port=1234
	local dmesg_rc
	local pidfile
	local rc

	init_namespaces

	pidfile="$(create_pidfile)"

	if ! vm_start "${pidfile}" "${ns}"; then
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns}"
	oops_before=$(vm_dmesg_oops_count "${ns}")
	warn_before=$(vm_dmesg_warn_count "${ns}")

	vm_vsock_test "${ns}" "server" 2 "${TEST_GUEST_PORT}"

	# Skip test 29 (transport release use-after-free): This test attempts
	# binding both G2H and H2G CIDs. Because virtio-vsock (G2H) doesn't
	# support local namespaces the test will fail when
	# transport_g2h->stream_allow() returns false. This edge case only
	# happens for vsock_test in client mode on the host in a local
	# namespace. This is a false positive.
	host_vsock_test "${ns}" "127.0.0.1" "${VSOCK_CID}" "${TEST_HOST_PORT}" --skip=29
	rc=$?

	vm_dmesg_check "${pidfile}" "${ns}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"

	if [[ "${rc}" -ne 0 ]] || [[ "${dmesg_rc}" -ne 0 ]]; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

test_ns_same_local_vm_connect_to_local_host_ok() {
	local oops_before warn_before
	local ns="local0"
	local port=1234
	local dmesg_rc
	local pidfile
	local rc

	init_namespaces

	pidfile="$(create_pidfile)"

	if ! vm_start "${pidfile}" "${ns}"; then
		return "${KSFT_FAIL}"
	fi

	vm_wait_for_ssh "${ns}"
	oops_before=$(vm_dmesg_oops_count "${ns}")
	warn_before=$(vm_dmesg_warn_count "${ns}")

	host_vsock_test "${ns}" "server" "${VSOCK_CID}" "${port}"
	vm_vsock_test "${ns}" "10.0.2.2" 2 "${port}"
	rc=$?

	vm_dmesg_check "${pidfile}" "${ns}" "${oops_before}" "${warn_before}"
	dmesg_rc=$?

	terminate_pidfiles "${pidfile}"

	if [[ "${rc}" -ne 0 ]] || [[ "${dmesg_rc}" -ne 0 ]]; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

namespaces_can_boot_same_cid() {
	local ns0=$1
	local ns1=$2
	local pidfile1 pidfile2
	local rc

	pidfile1="$(create_pidfile)"

	# The first VM should be able to start. If it can't then we have
	# problems and need to return non-zero.
	if ! vm_start "${pidfile1}" "${ns0}"; then
		return 1
	fi

	pidfile2="$(create_pidfile)"
	vm_start "${pidfile2}" "${ns1}"
	rc=$?
	terminate_pidfiles "${pidfile1}" "${pidfile2}"

	return "${rc}"
}

test_ns_global_same_cid_fails() {
	init_namespaces

	if namespaces_can_boot_same_cid "global0" "global1"; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

test_ns_local_global_same_cid_ok() {
	init_namespaces

	if namespaces_can_boot_same_cid "local0" "global0"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_global_local_same_cid_ok() {
	init_namespaces

	if namespaces_can_boot_same_cid "global0" "local0"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_local_same_cid_ok() {
	init_namespaces

	if namespaces_can_boot_same_cid "local0" "local1"; then
		return "${KSFT_PASS}"
	fi

	return "${KSFT_FAIL}"
}

test_ns_host_vsock_child_ns_mode_ok() {
	local orig_mode
	local rc

	orig_mode=$(cat /proc/sys/net/vsock/child_ns_mode)

	rc="${KSFT_PASS}"
	for mode in "${NS_MODES[@]}"; do
		local ns="${mode}0"

		if echo "${mode}" 2>/dev/null > /proc/sys/net/vsock/ns_mode; then
			log_host "ns_mode should be read-only but write succeeded"
			rc="${KSFT_FAIL}"
			continue
		fi

		if ! echo "${mode}" > /proc/sys/net/vsock/child_ns_mode; then
			log_host "child_ns_mode should be writable to ${mode}"
			rc="${KSFT_FAIL}"
			continue
		fi
	done

	echo "${orig_mode}" > /proc/sys/net/vsock/child_ns_mode

	return "${rc}"
}

test_vm_server_host_client() {
	if ! vm_vsock_test "init_ns" "server" 2 "${TEST_GUEST_PORT}"; then
		return "${KSFT_FAIL}"
	fi

	if ! host_vsock_test "init_ns" "127.0.0.1" "${VSOCK_CID}" "${TEST_HOST_PORT}"; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

test_vm_client_host_server() {
	if ! host_vsock_test "init_ns" "server" "${VSOCK_CID}" "${TEST_HOST_PORT_LISTENER}"; then
		return "${KSFT_FAIL}"
	fi

	if ! vm_vsock_test "init_ns" "10.0.2.2" 2 "${TEST_HOST_PORT_LISTENER}"; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

test_vm_loopback() {
	local port=60000 # non-forwarded local port

	vm_ssh "init_ns" -- modprobe vsock_loopback &> /dev/null || :

	if ! vm_vsock_test "init_ns" "server" 1 "${port}"; then
		return "${KSFT_FAIL}"
	fi


	if ! vm_vsock_test "init_ns" "127.0.0.1" 1 "${port}"; then
		return "${KSFT_FAIL}"
	fi

	return "${KSFT_PASS}"
}

check_ns_delete_doesnt_break_connection() {
	local pipefile pidfile outfile
	local ns0="global0"
	local ns1="global1"
	local port=12345
	local pids=()
	local rc=0

	init_namespaces

	pidfile="$(create_pidfile)"
	if ! vm_start "${pidfile}" "${ns0}"; then
		return "${KSFT_FAIL}"
	fi
	vm_wait_for_ssh "${ns0}"

	outfile=$(mktemp)
	vm_ssh "${ns0}" -- \
		socat VSOCK-LISTEN:"${port}",fork STDOUT > "${outfile}" 2>/dev/null &
	pids+=($!)
	vm_wait_for_listener "${ns0}" "${port}" "vsock"

	# We use a pipe here so that we can echo into the pipe instead of using
	# socat and a unix socket file. We just need a name for the pipe (not a
	# regular file) so use -u.
	pipefile=$(mktemp -u /tmp/vmtest_pipe_XXXX)
	ip netns exec "${ns1}" \
		socat PIPE:"${pipefile}" VSOCK-CONNECT:"${VSOCK_CID}":"${port}" &
	pids+=($!)

	timeout "${WAIT_PERIOD}" \
		bash -c 'while [[ ! -e '"${pipefile}"' ]]; do sleep 1; done; exit 0'

	if [[ "$1" == "vm" ]]; then
		ip netns del "${ns0}"
	elif [[ "$1" == "host" ]]; then
		ip netns del "${ns1}"
	elif [[ "$1" == "both" ]]; then
		ip netns del "${ns0}"
		ip netns del "${ns1}"
	fi

	echo "TEST" > "${pipefile}"

	timeout "${WAIT_PERIOD}" \
		bash -c 'while [[ ! -s '"${outfile}"' ]]; do sleep 1; done; exit 0'

	if grep -q "TEST" "${outfile}"; then
		rc="${KSFT_PASS}"
	else
		rc="${KSFT_FAIL}"
	fi

	terminate_pidfiles "${pidfile}"
	terminate_pids "${pids[@]}"
	rm -f "${outfile}" "${pipefile}"

	return "${rc}"
}

test_ns_delete_vm_ok() {
	check_ns_delete_doesnt_break_connection "vm"
}

test_ns_delete_host_ok() {
	check_ns_delete_doesnt_break_connection "host"
}

test_ns_delete_both_ok() {
	check_ns_delete_doesnt_break_connection "both"
}

shared_vm_test() {
	local tname

	tname="${1}"

	for testname in "${USE_SHARED_VM[@]}"; do
		if [[ "${tname}" == "${testname}" ]]; then
			return 0
		fi
	done

	return 1
}

shared_vm_tests_requested() {
	for arg in "$@"; do
		if shared_vm_test "${arg}"; then
			return 0
		fi
	done

	return 1
}

run_shared_vm_tests() {
	local arg

	for arg in "$@"; do
		if ! shared_vm_test "${arg}"; then
			continue
		fi

		if ! check_netns "${arg}"; then
			check_result "${KSFT_SKIP}" "${arg}"
			continue
		fi

		run_shared_vm_test "${arg}"
		check_result "$?" "${arg}"
	done
}

run_shared_vm_test() {
	local host_oops_cnt_before
	local host_warn_cnt_before
	local vm_oops_cnt_before
	local vm_warn_cnt_before
	local host_oops_cnt_after
	local host_warn_cnt_after
	local vm_oops_cnt_after
	local vm_warn_cnt_after
	local name
	local rc

	host_oops_cnt_before=$(dmesg | grep -c -i 'Oops')
	host_warn_cnt_before=$(dmesg --level=warn | grep -c -i 'vsock')
	vm_oops_cnt_before=$(vm_dmesg_oops_count "init_ns")
	vm_warn_cnt_before=$(vm_dmesg_warn_count "init_ns")

	name=$(echo "${1}" | awk '{ print $1 }')
	eval test_"${name}"
	rc=$?

	host_oops_cnt_after=$(dmesg | grep -i 'Oops' | wc -l)
	if [[ ${host_oops_cnt_after} -gt ${host_oops_cnt_before} ]]; then
		echo "FAIL: kernel oops detected on host" | log_host
		rc=$KSFT_FAIL
	fi

	host_warn_cnt_after=$(dmesg --level=warn | grep -c -i 'vsock')
	if [[ ${host_warn_cnt_after} -gt ${host_warn_cnt_before} ]]; then
		echo "FAIL: kernel warning detected on host" | log_host
		rc=$KSFT_FAIL
	fi

	vm_oops_cnt_after=$(vm_dmesg_oops_count "init_ns")
	if [[ ${vm_oops_cnt_after} -gt ${vm_oops_cnt_before} ]]; then
		echo "FAIL: kernel oops detected on vm" | log_host
		rc=$KSFT_FAIL
	fi

	vm_warn_cnt_after=$(vm_dmesg_warn_count "init_ns")
	if [[ ${vm_warn_cnt_after} -gt ${vm_warn_cnt_before} ]]; then
		echo "FAIL: kernel warning detected on vm" | log_host
		rc=$KSFT_FAIL
	fi

	return "${rc}"
}

run_ns_tests() {
	for arg in "${ARGS[@]}"; do
		if shared_vm_test "${arg}"; then
			continue
		fi

		if ! check_netns "${arg}"; then
			check_result "${KSFT_SKIP}" "${arg}"
			continue
		fi

		add_namespaces

		name=$(echo "${arg}" | awk '{ print $1 }')
		log_host "Executing test_${name}"

		host_oops_before=$(dmesg 2>/dev/null | grep -c -i 'Oops')
		host_warn_before=$(dmesg --level=warn 2>/dev/null | grep -c -i 'vsock')
		eval test_"${name}"
		rc=$?

		host_oops_after=$(dmesg 2>/dev/null | grep -c -i 'Oops')
		if [[ "${host_oops_after}" -gt "${host_oops_before}" ]]; then
			echo "FAIL: kernel oops detected on host" | log_host
			check_result "${KSFT_FAIL}" "${name}"
			del_namespaces
			continue
		fi

		host_warn_after=$(dmesg --level=warn 2>/dev/null | grep -c -i 'vsock')
		if [[ "${host_warn_after}" -gt "${host_warn_before}" ]]; then
			echo "FAIL: kernel warning detected on host" | log_host
			check_result "${KSFT_FAIL}" "${name}"
			del_namespaces
			continue
		fi

		check_result "${rc}" "${name}"

		del_namespaces
	done
}

BUILD=0
QEMU="qemu-system-$(uname -m)"

while getopts :hvsq:b o
do
	case $o in
	v) VERBOSE=1;;
	b) BUILD=1;;
	q) QEMU=$OPTARG;;
	h|*) usage;;
	esac
done
shift $((OPTIND-1))

trap cleanup EXIT

if [[ ${#} -eq 0 ]]; then
	ARGS=("${TEST_NAMES[@]}")
else
	ARGS=("$@")
fi

check_args "${ARGS[@]}"
check_deps
check_vng
check_socat
handle_build

echo "1..${#ARGS[@]}"

cnt_pass=0
cnt_fail=0
cnt_skip=0
cnt_total=0

if shared_vm_tests_requested "${ARGS[@]}"; then
	log_host "Booting up VM"
	pidfile="$(create_pidfile)"
	vm_start "${pidfile}" "init_ns"
	vm_wait_for_ssh "init_ns"
	log_host "VM booted up"

	run_shared_vm_tests "${ARGS[@]}"
	terminate_pidfiles "${pidfile}"
fi

run_ns_tests "${ARGS[@]}"

echo "SUMMARY: PASS=${cnt_pass} SKIP=${cnt_skip} FAIL=${cnt_fail}"
echo "Log: ${LOG}"

if [ $((cnt_pass + cnt_skip)) -eq ${cnt_total} ]; then
	exit "$KSFT_PASS"
else
	exit "$KSFT_FAIL"
fi
