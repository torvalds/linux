#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e
set -u
set -o pipefail

IMA_POLICY_FILE="/sys/kernel/security/ima/policy"
TEST_BINARY="/bin/true"

usage()
{
	echo "Usage: $0 <setup|cleanup|run> <existing_tmp_dir>"
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
	else
		echo "Unknown action: ${action}"
		exit 1
	fi
}

main "$@"
