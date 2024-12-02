#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e
set -u
set -o pipefail

IMA_POLICY_FILE="/sys/kernel/security/ima/policy"
TEST_BINARY="/bin/true"
VERBOSE="${SELFTESTS_VERBOSE:=0}"
LOG_FILE="$(mktemp /tmp/ima_setup.XXXX.log)"

usage()
{
	echo "Usage: $0 <setup|cleanup|run|modify-bin|restore-bin|load-policy> <existing_tmp_dir>"
	exit 1
}

ensure_mount_securityfs()
{
	local securityfs_dir=$(grep "securityfs" /proc/mounts | awk '{print $2}')

	if [ -z "${securityfs_dir}" ]; then
		securityfs_dir=/sys/kernel/security
		mount -t securityfs security "${securityfs_dir}"
	fi

	if [ ! -d "${securityfs_dir}" ]; then
		echo "${securityfs_dir}: securityfs is not mounted" && exit 1
	fi
}

setup()
{
	local tmp_dir="$1"
	local mount_img="${tmp_dir}/test.img"
	local mount_dir="${tmp_dir}/mnt"
	local copied_bin_path="${mount_dir}/$(basename ${TEST_BINARY})"
	mkdir -p ${mount_dir}

	dd if=/dev/zero of="${mount_img}" bs=1M count=10

	losetup -f "${mount_img}"
	local loop_device=$(losetup -a | grep ${mount_img:?} | cut -d ":" -f1)

	mkfs.ext2 "${loop_device:?}"
	mount "${loop_device}" "${mount_dir}"

	cp "${TEST_BINARY}" "${mount_dir}"
	local mount_uuid="$(blkid ${loop_device} | sed 's/.*UUID="\([^"]*\)".*/\1/')"

	ensure_mount_securityfs
	echo "measure func=BPRM_CHECK fsuuid=${mount_uuid}" > ${IMA_POLICY_FILE}
	echo "measure func=BPRM_CHECK fsuuid=${mount_uuid}" > ${mount_dir}/policy_test
}

cleanup() {
	local tmp_dir="$1"
	local mount_img="${tmp_dir}/test.img"
	local mount_dir="${tmp_dir}/mnt"

	local loop_devices=$(losetup -a | grep ${mount_img:?} | cut -d ":" -f1)

	for loop_dev in "${loop_devices}"; do
		losetup -d $loop_dev
	done

	umount ${mount_dir}
	rm -rf ${tmp_dir}
}

run()
{
	local tmp_dir="$1"
	local mount_dir="${tmp_dir}/mnt"
	local copied_bin_path="${mount_dir}/$(basename ${TEST_BINARY})"

	exec "${copied_bin_path}"
}

modify_bin()
{
	local tmp_dir="$1"
	local mount_dir="${tmp_dir}/mnt"
	local copied_bin_path="${mount_dir}/$(basename ${TEST_BINARY})"

	echo "mod" >> "${copied_bin_path}"
}

restore_bin()
{
	local tmp_dir="$1"
	local mount_dir="${tmp_dir}/mnt"
	local copied_bin_path="${mount_dir}/$(basename ${TEST_BINARY})"

	truncate -s -4 "${copied_bin_path}"
}

load_policy()
{
	local tmp_dir="$1"
	local mount_dir="${tmp_dir}/mnt"

	echo ${mount_dir}/policy_test > ${IMA_POLICY_FILE} 2> /dev/null
}

catch()
{
	local exit_code="$1"
	local log_file="$2"

	if [[ "${exit_code}" -ne 0 ]]; then
		cat "${log_file}" >&3
	fi

	rm -f "${log_file}"
	exit ${exit_code}
}

main()
{
	[[ $# -ne 2 ]] && usage

	local action="$1"
	local tmp_dir="$2"

	[[ ! -d "${tmp_dir}" ]] && echo "Directory ${tmp_dir} doesn't exist" && exit 1

	if [[ "${action}" == "setup" ]]; then
		setup "${tmp_dir}"
	elif [[ "${action}" == "cleanup" ]]; then
		cleanup "${tmp_dir}"
	elif [[ "${action}" == "run" ]]; then
		run "${tmp_dir}"
	elif [[ "${action}" == "modify-bin" ]]; then
		modify_bin "${tmp_dir}"
	elif [[ "${action}" == "restore-bin" ]]; then
		restore_bin "${tmp_dir}"
	elif [[ "${action}" == "load-policy" ]]; then
		load_policy "${tmp_dir}"
	else
		echo "Unknown action: ${action}"
		exit 1
	fi
}

trap 'catch "$?" "${LOG_FILE}"' EXIT

if [[ "${VERBOSE}" -eq 0 ]]; then
	# Save the stderr to 3 so that we can output back to
	# it incase of an error.
	exec 3>&2 1>"${LOG_FILE}" 2>&1
fi

main "$@"
rm -f "${LOG_FILE}"
