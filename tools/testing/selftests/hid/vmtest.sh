#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Red Hat
# Copyright (c) 2025 Meta Platforms, Inc. and affiliates
#
# Dependencies:
#		* virtme-ng
#		* busybox-static (used by virtme-ng)
#		* qemu	(used by virtme-ng)

readonly SCRIPT_DIR="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly KERNEL_CHECKOUT=$(realpath "${SCRIPT_DIR}"/../../../../)

source "${SCRIPT_DIR}"/../kselftest/ktap_helpers.sh

readonly HID_BPF_TEST="${SCRIPT_DIR}"/hid_bpf
readonly HIDRAW_TEST="${SCRIPT_DIR}"/hidraw
readonly HID_BPF_PROGS="${KERNEL_CHECKOUT}/drivers/hid/bpf/progs"
readonly SSH_GUEST_PORT=22
readonly WAIT_PERIOD=3
readonly WAIT_PERIOD_MAX=60
readonly WAIT_TOTAL=$(( WAIT_PERIOD * WAIT_PERIOD_MAX ))
readonly QEMU_PIDFILE=$(mktemp /tmp/qemu_hid_vmtest_XXXX.pid)

readonly QEMU_OPTS="\
	 --pidfile ${QEMU_PIDFILE} \
"
readonly KERNEL_CMDLINE=""
readonly LOG=$(mktemp /tmp/hid_vmtest_XXXX.log)
readonly TEST_NAMES=(vm_hid_bpf vm_hidraw vm_pytest)
readonly TEST_DESCS=(
	"Run hid_bpf tests in the VM."
	"Run hidraw tests in the VM."
	"Run the hid-tools test-suite in the VM."
)

VERBOSE=0
SHELL_MODE=0
BUILD_HOST=""
BUILD_HOST_PODMAN_CONTAINER_NAME=""

usage() {
	local name
	local desc
	local i

	echo
	echo "$0 [OPTIONS] [TEST]... [-- tests-args]"
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  -b: build the kernel from the current source tree and use it for guest VMs"
	echo "  -H: hostname for remote build host (used with -b)"
	echo "  -p: podman container name for remote build host (used with -b)"
	echo "      Example: -H beefyserver -p vng"
	echo "  -q: set the path to or name of qemu binary"
	echo "  -s: start a shell in the VM instead of running tests"
	echo "  -v: more verbose output (can be repeated multiple times)"
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
	# vng --ssh-client keeps shouting "Warning: Permanently added 'virtme-ng%22'
	# (ED25519) to the list of known hosts.",
	# So replace the command with what's actually called and add the "-q" option
	stdbuf -oL ssh -q \
		       -F ${HOME}/.cache/virtme-ng/.ssh/virtme-ng-ssh.conf \
		       -l root virtme-ng%${SSH_GUEST_PORT} \
		       "$@"
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
	for dep in vng ${QEMU} busybox pkill ssh pytest; do
		if [[ ! -x $(command -v "${dep}") ]]; then
			echo -e "skip:    dependency ${dep} not found!\n"
			exit "${KSFT_SKIP}"
		fi
	done

	if [[ ! -x $(command -v "${HID_BPF_TEST}") ]]; then
		printf "skip:    %s not found!" "${HID_BPF_TEST}"
		printf " Please build the kselftest hid_bpf target.\n"
		exit "${KSFT_SKIP}"
	fi

	if [[ ! -x $(command -v "${HIDRAW_TEST}") ]]; then
		printf "skip:    %s not found!" "${HIDRAW_TEST}"
		printf " Please build the kselftest hidraw target.\n"
		exit "${KSFT_SKIP}"
	fi
}

check_vng() {
	local tested_versions
	local version
	local ok

	tested_versions=("1.36" "1.37")
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

	local vng_args=("-v" "--config" "${SCRIPT_DIR}/config" "--build")

	if [[ -n "${BUILD_HOST}" ]]; then
		vng_args+=("--build-host" "${BUILD_HOST}")
	fi

	if [[ -n "${BUILD_HOST_PODMAN_CONTAINER_NAME}" ]]; then
		vng_args+=("--build-host-exec-prefix" \
			   "podman exec -ti ${BUILD_HOST_PODMAN_CONTAINER_NAME}")
	fi

	if ! vng "${vng_args[@]}"; then
		die "failed to build kernel from source tree (${KERNEL_CHECKOUT})"
	fi

	if ! make -j$(nproc) -C "${HID_BPF_PROGS}"; then
		die "failed to build HID bpf objects from source tree (${HID_BPF_PROGS})"
	fi

	if ! make -j$(nproc) -C "${SCRIPT_DIR}"; then
		die "failed to build HID selftests from source tree (${SCRIPT_DIR})"
	fi

	popd &>/dev/null
}

vm_start() {
	local logfile=/dev/null
	local verbose_opt=""
	local kernel_opt=""
	local qemu

	qemu=$(command -v "${QEMU}")

	if [[ "${VERBOSE}" -eq 2 ]]; then
		verbose_opt="--verbose"
		logfile=/dev/stdout
	fi

	# If we are running from within the kernel source tree, use the kernel source tree
	# as the kernel to boot, otherwise use the currently running kernel.
	if [[ "$(realpath "$(pwd)")" == "${KERNEL_CHECKOUT}"* ]]; then
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
		--ssh "${SSH_GUEST_PORT}" \
		--rw  &> ${logfile} &

	local vng_pid=$!
	local elapsed=0

	while [[ ! -s "${QEMU_PIDFILE}" ]]; do
		if ! kill -0 "${vng_pid}" 2>/dev/null; then
			echo "vng process (PID ${vng_pid}) exited early, check logs for details" >&2
			die "failed to boot VM"
		fi

		if [[ ${elapsed} -ge ${WAIT_TOTAL} ]]; then
			echo "Timed out after ${WAIT_TOTAL} seconds waiting for VM to boot" >&2
			die "failed to boot VM"
		fi

		sleep 1
		elapsed=$((elapsed + 1))
	done
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

vm_mount_bpffs() {
	vm_ssh -- mount bpffs -t bpf /sys/fs/bpf
}

__log_stdin() {
	stdbuf -oL awk '{ printf "%s:\t%s\n","'"${prefix}"'", $0; fflush() }'
}

__log_args() {
	echo "$*" | awk '{ printf "%s:\t%s\n","'"${prefix}"'", $0 }'
}

log() {
	local verbose="$1"
	shift

	local prefix="$1"

	shift
	local redirect=
	if [[ ${verbose} -le 0 ]]; then
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
	log $((VERBOSE-1)) "setup" "$@"
}

log_host() {
	local testname=$1

	shift
	log $((VERBOSE-1)) "test:${testname}:host" "$@"
}

log_guest() {
	local testname=$1

	shift
	log ${VERBOSE} "# test:${testname}" "$@"
}

test_vm_hid_bpf() {
	local testname="${FUNCNAME[0]#test_}"

	vm_ssh -- "${HID_BPF_TEST}" \
		2>&1 | log_guest "${testname}"

	return ${PIPESTATUS[0]}
}

test_vm_hidraw() {
	local testname="${FUNCNAME[0]#test_}"

	vm_ssh -- "${HIDRAW_TEST}" \
		2>&1 | log_guest "${testname}"

	return ${PIPESTATUS[0]}
}

test_vm_pytest() {
	local testname="${FUNCNAME[0]#test_}"

	shift

	vm_ssh -- pytest ${SCRIPT_DIR}/tests --color=yes "$@" \
		2>&1 | log_guest "${testname}"

	return ${PIPESTATUS[0]}
}

run_test() {
	local vm_oops_cnt_before
	local vm_warn_cnt_before
	local vm_oops_cnt_after
	local vm_warn_cnt_after
	local name
	local rc

	vm_oops_cnt_before=$(vm_ssh -- dmesg | grep -c -i 'Oops')
	vm_error_cnt_before=$(vm_ssh -- dmesg --level=err | wc -l)

	name=$(echo "${1}" | awk '{ print $1 }')
	eval test_"${name}" "$@"
	rc=$?

	vm_oops_cnt_after=$(vm_ssh -- dmesg | grep -i 'Oops' | wc -l)
	if [[ ${vm_oops_cnt_after} -gt ${vm_oops_cnt_before} ]]; then
		echo "FAIL: kernel oops detected on vm" | log_host "${name}"
		rc=$KSFT_FAIL
	fi

	vm_error_cnt_after=$(vm_ssh -- dmesg --level=err | wc -l)
	if [[ ${vm_error_cnt_after} -gt ${vm_error_cnt_before} ]]; then
		echo "FAIL: kernel error detected on vm" | log_host "${name}"
		vm_ssh -- dmesg --level=err | log_host "${name}"
		rc=$KSFT_FAIL
	fi

	return "${rc}"
}

QEMU="qemu-system-$(uname -m)"

while getopts :hvsbq:H:p: o
do
	case $o in
	v) VERBOSE=$((VERBOSE+1));;
	s) SHELL_MODE=1;;
	b) BUILD=1;;
	q) QEMU=$OPTARG;;
	H) BUILD_HOST=$OPTARG;;
	p) BUILD_HOST_PODMAN_CONTAINER_NAME=$OPTARG;;
	h|*) usage;;
	esac
done
shift $((OPTIND-1))

trap cleanup EXIT

PARAMS=""

if [[ ${#} -eq 0 ]]; then
	ARGS=("${TEST_NAMES[@]}")
else
	ARGS=()
	COUNT=0
	for arg in $@; do
		COUNT=$((COUNT+1))
		if [[ x"$arg" == x"--" ]]; then
			break
		fi
		ARGS+=($arg)
	done
	shift $COUNT
	PARAMS="$@"
fi

if [[ "${SHELL_MODE}" -eq 0 ]]; then
	check_args "${ARGS[@]}"
	echo "1..${#ARGS[@]}"
fi
check_deps
check_vng
handle_build

log_setup "Booting up VM"
vm_start
vm_wait_for_ssh
vm_mount_bpffs
log_setup "VM booted up"

if [[ "${SHELL_MODE}" -eq 1 ]]; then
	log_setup "Starting interactive shell in VM"
	echo "Starting shell in VM. Use 'exit' to quit and shutdown the VM."
	CURRENT_DIR="$(pwd)"
	vm_ssh -t -- "cd '${CURRENT_DIR}' && exec bash -l"
	exit "$KSFT_PASS"
fi

cnt_pass=0
cnt_fail=0
cnt_skip=0
cnt_total=0
for arg in "${ARGS[@]}"; do
	run_test "${arg}" "${PARAMS}"
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
