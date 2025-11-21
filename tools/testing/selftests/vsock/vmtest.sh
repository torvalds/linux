#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Meta Platforms, Inc. and affiliates
#
# Dependencies:
#		* virtme-ng
#		* busybox-static (used by virtme-ng)
#		* qemu	(used by virtme-ng)

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
readonly WAIT_TOTAL=$(( WAIT_PERIOD * WAIT_PERIOD_MAX ))
readonly QEMU_PIDFILE=$(mktemp /tmp/qemu_vsock_vmtest_XXXX.pid)

# virtme-ng offers a netdev for ssh when using "--ssh", but we also need a
# control port forwarded for vsock_test.  Because virtme-ng doesn't support
# adding an additional port to forward to the device created from "--ssh" and
# virtme-init mistakenly sets identical IPs to the ssh device and additional
# devices, we instead opt out of using --ssh, add the device manually, and also
# add the kernel cmdline options that virtme-init uses to setup the interface.
readonly QEMU_TEST_PORT_FWD="hostfwd=tcp::${TEST_HOST_PORT}-:${TEST_GUEST_PORT}"
readonly QEMU_SSH_PORT_FWD="hostfwd=tcp::${SSH_HOST_PORT}-:${SSH_GUEST_PORT}"
readonly QEMU_OPTS="\
	 -netdev user,id=n0,${QEMU_TEST_PORT_FWD},${QEMU_SSH_PORT_FWD} \
	 -device virtio-net-pci,netdev=n0 \
	 -device vhost-vsock-pci,guest-cid=${VSOCK_CID} \
	 --pidfile ${QEMU_PIDFILE} \
"
readonly KERNEL_CMDLINE="\
	virtme.dhcp net.ifnames=0 biosdevname=0 \
	virtme.ssh virtme_ssh_channel=tcp virtme_ssh_user=$USER \
"
readonly LOG=$(mktemp /tmp/vsock_vmtest_XXXX.log)
readonly TEST_NAMES=(vm_server_host_client vm_client_host_server vm_loopback)
readonly TEST_DESCS=(
	"Run vsock_test in server mode on the VM and in client mode on the host."
	"Run vsock_test in client mode on the VM and in server mode on the host."
	"Run vsock_test using the loopback transport in the VM."
)

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
		printf "\t%-35s%-35s\n" "${name}" "${desc}"
	done
	echo

	exit 1
}

die() {
	echo "$*" >&2
	exit "${KSFT_FAIL}"
}

vm_ssh() {
	ssh -q -o UserKnownHostsFile=/dev/null -p ${SSH_HOST_PORT} localhost "$@"
	return $?
}

cleanup() {
	if [[ -s "${QEMU_PIDFILE}" ]]; then
		pkill -SIGTERM -F "${QEMU_PIDFILE}" > /dev/null 2>&1
	fi

	# If failure occurred during or before qemu start up, then we need
	# to clean this up ourselves.
	if [[ -e "${QEMU_PIDFILE}" ]]; then
		rm "${QEMU_PIDFILE}"
	fi
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
	for dep in vng ${QEMU} busybox pkill ssh; do
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

check_vng() {
	local tested_versions
	local version
	local ok

	tested_versions=("1.33" "1.36")
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

vm_start() {
	local logfile=/dev/null
	local verbose_opt=""
	local kernel_opt=""
	local qemu

	qemu=$(command -v "${QEMU}")

	if [[ "${VERBOSE}" -eq 1 ]]; then
		verbose_opt="--verbose"
		logfile=/dev/stdout
	fi

	if [[ "${BUILD}" -eq 1 ]]; then
		kernel_opt="${KERNEL_CHECKOUT}"
	fi

	vng \
		--run \
		${kernel_opt} \
		${verbose_opt} \
		--qemu-opts="${QEMU_OPTS}" \
		--qemu="${qemu}" \
		--user root \
		--append "${KERNEL_CMDLINE}" \
		--rw  &> ${logfile} &

	if ! timeout ${WAIT_TOTAL} \
		bash -c 'while [[ ! -s '"${QEMU_PIDFILE}"' ]]; do sleep 1; done; exit 0'; then
		die "failed to boot VM"
	fi
}

vm_wait_for_ssh() {
	local i

	i=0
	while true; do
		if [[ ${i} -gt ${WAIT_PERIOD_MAX} ]]; then
			die "Timed out waiting for guest ssh"
		fi
		if vm_ssh -- true; then
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
	local protocol=tcp
	local pattern
	local i

	pattern=":$(printf "%04X" "${port}") "

	# for tcp protocol additionally check the socket state
	[ "${protocol}" = "tcp" ] && pattern="${pattern}0A"
	for i in $(seq "${max_intervals}"); do
		if awk '{print $2" "$4}' /proc/net/"${protocol}"* | \
		   grep -q "${pattern}"; then
			break
		fi
		sleep "${interval}"
	done
}

vm_wait_for_listener() {
	local port=$1

	vm_ssh <<EOF
$(declare -f wait_for_listener)
wait_for_listener ${port} ${WAIT_PERIOD} ${WAIT_PERIOD_MAX}
EOF
}

host_wait_for_listener() {
	wait_for_listener "${TEST_HOST_PORT_LISTENER}" "${WAIT_PERIOD}" "${WAIT_PERIOD_MAX}"
}

__log_stdin() {
	cat | awk '{ printf "%s:\t%s\n","'"${prefix}"'", $0 }'
}

__log_args() {
	echo "$*" | awk '{ printf "%s:\t%s\n","'"${prefix}"'", $0 }'
}

log() {
	local prefix="$1"

	shift
	local redirect=
	if [[ ${VERBOSE} -eq 0 ]]; then
		redirect=/dev/null
	else
		redirect=/dev/stdout
	fi

	if [[ "$#" -eq 0 ]]; then
		__log_stdin | tee -a "${LOG}" > ${redirect}
	else
		__log_args "$@" | tee -a "${LOG}" > ${redirect}
	fi
}

log_setup() {
	log "setup" "$@"
}

log_host() {
	local testname=$1

	shift
	log "test:${testname}:host" "$@"
}

log_guest() {
	local testname=$1

	shift
	log "test:${testname}:guest" "$@"
}

test_vm_server_host_client() {
	local testname="${FUNCNAME[0]#test_}"

	vm_ssh -- "${VSOCK_TEST}" \
		--mode=server \
		--control-port="${TEST_GUEST_PORT}" \
		--peer-cid=2 \
		2>&1 | log_guest "${testname}" &

	vm_wait_for_listener "${TEST_GUEST_PORT}"

	${VSOCK_TEST} \
		--mode=client \
		--control-host=127.0.0.1 \
		--peer-cid="${VSOCK_CID}" \
		--control-port="${TEST_HOST_PORT}" 2>&1 | log_host "${testname}"

	return $?
}

test_vm_client_host_server() {
	local testname="${FUNCNAME[0]#test_}"

	${VSOCK_TEST} \
		--mode "server" \
		--control-port "${TEST_HOST_PORT_LISTENER}" \
		--peer-cid "${VSOCK_CID}" 2>&1 | log_host "${testname}" &

	host_wait_for_listener

	vm_ssh -- "${VSOCK_TEST}" \
		--mode=client \
		--control-host=10.0.2.2 \
		--peer-cid=2 \
		--control-port="${TEST_HOST_PORT_LISTENER}" 2>&1 | log_guest "${testname}"

	return $?
}

test_vm_loopback() {
	local testname="${FUNCNAME[0]#test_}"
	local port=60000 # non-forwarded local port

	vm_ssh -- "${VSOCK_TEST}" \
		--mode=server \
		--control-port="${port}" \
		--peer-cid=1 2>&1 | log_guest "${testname}" &

	vm_wait_for_listener "${port}"

	vm_ssh -- "${VSOCK_TEST}" \
		--mode=client \
		--control-host="127.0.0.1" \
		--control-port="${port}" \
		--peer-cid=1 2>&1 | log_guest "${testname}"

	return $?
}

run_test() {
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
	vm_oops_cnt_before=$(vm_ssh -- dmesg | grep -c -i 'Oops')
	vm_warn_cnt_before=$(vm_ssh -- dmesg --level=warn | grep -c -i 'vsock')

	name=$(echo "${1}" | awk '{ print $1 }')
	eval test_"${name}"
	rc=$?

	host_oops_cnt_after=$(dmesg | grep -i 'Oops' | wc -l)
	if [[ ${host_oops_cnt_after} -gt ${host_oops_cnt_before} ]]; then
		echo "FAIL: kernel oops detected on host" | log_host "${name}"
		rc=$KSFT_FAIL
	fi

	host_warn_cnt_after=$(dmesg --level=warn | grep -c -i 'vsock')
	if [[ ${host_warn_cnt_after} -gt ${host_warn_cnt_before} ]]; then
		echo "FAIL: kernel warning detected on host" | log_host "${name}"
		rc=$KSFT_FAIL
	fi

	vm_oops_cnt_after=$(vm_ssh -- dmesg | grep -i 'Oops' | wc -l)
	if [[ ${vm_oops_cnt_after} -gt ${vm_oops_cnt_before} ]]; then
		echo "FAIL: kernel oops detected on vm" | log_host "${name}"
		rc=$KSFT_FAIL
	fi

	vm_warn_cnt_after=$(vm_ssh -- dmesg --level=warn | grep -c -i 'vsock')
	if [[ ${vm_warn_cnt_after} -gt ${vm_warn_cnt_before} ]]; then
		echo "FAIL: kernel warning detected on vm" | log_host "${name}"
		rc=$KSFT_FAIL
	fi

	return "${rc}"
}

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
handle_build

echo "1..${#ARGS[@]}"

log_setup "Booting up VM"
vm_start
vm_wait_for_ssh
log_setup "VM booted up"

cnt_pass=0
cnt_fail=0
cnt_skip=0
cnt_total=0
for arg in "${ARGS[@]}"; do
	run_test "${arg}"
	rc=$?
	if [[ ${rc} -eq $KSFT_PASS ]]; then
		cnt_pass=$(( cnt_pass + 1 ))
		echo "ok ${cnt_total} ${arg}"
	elif [[ ${rc} -eq $KSFT_SKIP ]]; then
		cnt_skip=$(( cnt_skip + 1 ))
		echo "ok ${cnt_total} ${arg} # SKIP"
	elif [[ ${rc} -eq $KSFT_FAIL ]]; then
		cnt_fail=$(( cnt_fail + 1 ))
		echo "not ok ${cnt_total} ${arg} # exit=$rc"
	fi
	cnt_total=$(( cnt_total + 1 ))
done

echo "SUMMARY: PASS=${cnt_pass} SKIP=${cnt_skip} FAIL=${cnt_fail}"
echo "Log: ${LOG}"

if [ $((cnt_pass + cnt_skip)) -eq ${cnt_total} ]; then
	exit "$KSFT_PASS"
else
	exit "$KSFT_FAIL"
fi
