#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -u
set -e

# This script currently only works for x86_64 and s390x, as
# it is based on the VM image used by the BPF CI, which is
# available only for these architectures.
ARCH="$(uname -m)"
case "${ARCH}" in
s390x)
	QEMU_BINARY=qemu-system-s390x
	QEMU_CONSOLE="ttyS1"
	QEMU_FLAGS=(-smp 2)
	BZIMAGE="arch/s390/boot/compressed/vmlinux"
	;;
x86_64)
	QEMU_BINARY=qemu-system-x86_64
	QEMU_CONSOLE="ttyS0,115200"
	QEMU_FLAGS=(-cpu host -smp 8)
	BZIMAGE="arch/x86/boot/bzImage"
	;;
*)
	echo "Unsupported architecture"
	exit 1
	;;
esac
DEFAULT_COMMAND="./test_progs"
MOUNT_DIR="mnt"
ROOTFS_IMAGE="root.img"
OUTPUT_DIR="$HOME/.bpf_selftests"
KCONFIG_REL_PATHS=("tools/testing/selftests/bpf/config" "tools/testing/selftests/bpf/config.${ARCH}")
INDEX_URL="https://raw.githubusercontent.com/libbpf/ci/master/INDEX"
NUM_COMPILE_JOBS="$(nproc)"
LOG_FILE_BASE="$(date +"bpf_selftests.%Y-%m-%d_%H-%M-%S")"
LOG_FILE="${LOG_FILE_BASE}.log"
EXIT_STATUS_FILE="${LOG_FILE_BASE}.exit_status"

usage()
{
	cat <<EOF
Usage: $0 [-i] [-s] [-d <output_dir>] -- [<command>]

<command> is the command you would normally run when you are in
tools/testing/selftests/bpf. e.g:

	$0 -- ./test_progs -t test_lsm

If no command is specified and a debug shell (-s) is not requested,
"${DEFAULT_COMMAND}" will be run by default.

If you build your kernel using KBUILD_OUTPUT= or O= options, these
can be passed as environment variables to the script:

  O=<kernel_build_path> $0 -- ./test_progs -t test_lsm

or

  KBUILD_OUTPUT=<kernel_build_path> $0 -- ./test_progs -t test_lsm

Options:

	-i)		Update the rootfs image with a newer version.
	-d)		Update the output directory (default: ${OUTPUT_DIR})
	-j)		Number of jobs for compilation, similar to -j in make
			(default: ${NUM_COMPILE_JOBS})
	-s)		Instead of powering off the VM, start an interactive
			shell. If <command> is specified, the shell runs after
			the command finishes executing
EOF
}

unset URLS
populate_url_map()
{
	if ! declare -p URLS &> /dev/null; then
		# URLS contain the mapping from file names to URLs where
		# those files can be downloaded from.
		declare -gA URLS
		while IFS=$'\t' read -r name url; do
			URLS["$name"]="$url"
		done < <(curl -Lsf ${INDEX_URL})
	fi
}

download()
{
	local file="$1"

	if [[ ! -v URLS[$file] ]]; then
		echo "$file not found" >&2
		return 1
	fi

	echo "Downloading $file..." >&2
	curl -Lsf "${URLS[$file]}" "${@:2}"
}

newest_rootfs_version()
{
	{
	for file in "${!URLS[@]}"; do
		if [[ $file =~ ^"${ARCH}"/libbpf-vmtest-rootfs-(.*)\.tar\.zst$ ]]; then
			echo "${BASH_REMATCH[1]}"
		fi
	done
	} | sort -rV | head -1
}

download_rootfs()
{
	local rootfsversion="$1"
	local dir="$2"

	if ! which zstd &> /dev/null; then
		echo 'Could not find "zstd" on the system, please install zstd'
		exit 1
	fi

	download "${ARCH}/libbpf-vmtest-rootfs-$rootfsversion.tar.zst" |
		zstd -d | sudo tar -C "$dir" -x
}

recompile_kernel()
{
	local kernel_checkout="$1"
	local make_command="$2"

	cd "${kernel_checkout}"

	${make_command} olddefconfig
	${make_command}
}

mount_image()
{
	local rootfs_img="${OUTPUT_DIR}/${ROOTFS_IMAGE}"
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"

	sudo mount -o loop "${rootfs_img}" "${mount_dir}"
}

unmount_image()
{
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"

	sudo umount "${mount_dir}" &> /dev/null
}

update_selftests()
{
	local kernel_checkout="$1"
	local selftests_dir="${kernel_checkout}/tools/testing/selftests/bpf"

	cd "${selftests_dir}"
	${make_command}

	# Mount the image and copy the selftests to the image.
	mount_image
	sudo rm -rf "${mount_dir}/root/bpf"
	sudo cp -r "${selftests_dir}" "${mount_dir}/root"
	unmount_image
}

update_init_script()
{
	local init_script_dir="${OUTPUT_DIR}/${MOUNT_DIR}/etc/rcS.d"
	local init_script="${init_script_dir}/S50-startup"
	local command="$1"
	local exit_command="$2"

	mount_image

	if [[ ! -d "${init_script_dir}" ]]; then
		cat <<EOF
Could not find ${init_script_dir} in the mounted image.
This likely indicates a bad rootfs image, Please download
a new image by passing "-i" to the script
EOF
		exit 1

	fi

	sudo bash -c "echo '#!/bin/bash' > ${init_script}"

	if [[ "${command}" != "" ]]; then
		sudo bash -c "cat >>${init_script}" <<EOF
# Have a default value in the exit status file
# incase the VM is forcefully stopped.
echo "130" > "/root/${EXIT_STATUS_FILE}"

{
	cd /root/bpf
	echo ${command}
	stdbuf -oL -eL ${command}
	echo "\$?" > "/root/${EXIT_STATUS_FILE}"
} 2>&1 | tee "/root/${LOG_FILE}"
# Ensure that the logs are written to disk
sync
EOF
	fi

	sudo bash -c "echo ${exit_command} >> ${init_script}"
	sudo chmod a+x "${init_script}"
	unmount_image
}

create_vm_image()
{
	local rootfs_img="${OUTPUT_DIR}/${ROOTFS_IMAGE}"
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"

	rm -rf "${rootfs_img}"
	touch "${rootfs_img}"
	chattr +C "${rootfs_img}" >/dev/null 2>&1 || true

	truncate -s 2G "${rootfs_img}"
	mkfs.ext4 -q "${rootfs_img}"

	mount_image
	download_rootfs "$(newest_rootfs_version)" "${mount_dir}"
	unmount_image
}

run_vm()
{
	local kernel_bzimage="$1"
	local rootfs_img="${OUTPUT_DIR}/${ROOTFS_IMAGE}"

	if ! which "${QEMU_BINARY}" &> /dev/null; then
		cat <<EOF
Could not find ${QEMU_BINARY}
Please install qemu or set the QEMU_BINARY environment variable.
EOF
		exit 1
	fi

	${QEMU_BINARY} \
		-nodefaults \
		-display none \
		-serial mon:stdio \
		"${QEMU_FLAGS[@]}" \
		-enable-kvm \
		-m 4G \
		-drive file="${rootfs_img}",format=raw,index=1,media=disk,if=virtio,cache=none \
		-kernel "${kernel_bzimage}" \
		-append "root=/dev/vda rw console=${QEMU_CONSOLE}"
}

copy_logs()
{
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"
	local log_file="${mount_dir}/root/${LOG_FILE}"
	local exit_status_file="${mount_dir}/root/${EXIT_STATUS_FILE}"

	mount_image
	sudo cp ${log_file} "${OUTPUT_DIR}"
	sudo cp ${exit_status_file} "${OUTPUT_DIR}"
	sudo rm -f ${log_file}
	unmount_image
}

is_rel_path()
{
	local path="$1"

	[[ ${path:0:1} != "/" ]]
}

do_update_kconfig()
{
	local kernel_checkout="$1"
	local kconfig_file="$2"

	rm -f "$kconfig_file" 2> /dev/null

	for config in "${KCONFIG_REL_PATHS[@]}"; do
		local kconfig_src="${kernel_checkout}/${config}"
		cat "$kconfig_src" >> "$kconfig_file"
	done
}

update_kconfig()
{
	local kernel_checkout="$1"
	local kconfig_file="$2"

	if [[ -f "${kconfig_file}" ]]; then
		local local_modified="$(stat -c %Y "${kconfig_file}")"

		for config in "${KCONFIG_REL_PATHS[@]}"; do
			local kconfig_src="${kernel_checkout}/${config}"
			local src_modified="$(stat -c %Y "${kconfig_src}")"
			# Only update the config if it has been updated after the
			# previously cached config was created. This avoids
			# unnecessarily compiling the kernel and selftests.
			if [[ "${src_modified}" -gt "${local_modified}" ]]; then
				do_update_kconfig "$kernel_checkout" "$kconfig_file"
				# Once we have found one outdated configuration
				# there is no need to check other ones.
				break
			fi
		done
	else
		do_update_kconfig "$kernel_checkout" "$kconfig_file"
	fi
}

main()
{
	local script_dir="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
	local kernel_checkout=$(realpath "${script_dir}"/../../../../)
	# By default the script searches for the kernel in the checkout directory but
	# it also obeys environment variables O= and KBUILD_OUTPUT=
	local kernel_bzimage="${kernel_checkout}/${BZIMAGE}"
	local command="${DEFAULT_COMMAND}"
	local update_image="no"
	local exit_command="poweroff -f"
	local debug_shell="no"

	while getopts 'hskid:j:' opt; do
		case ${opt} in
		i)
			update_image="yes"
			;;
		d)
			OUTPUT_DIR="$OPTARG"
			;;
		j)
			NUM_COMPILE_JOBS="$OPTARG"
			;;
		s)
			command=""
			debug_shell="yes"
			exit_command="bash"
			;;
		h)
			usage
			exit 0
			;;
		\? )
			echo "Invalid Option: -$OPTARG"
			usage
			exit 1
			;;
		: )
			echo "Invalid Option: -$OPTARG requires an argument"
			usage
			exit 1
			;;
		esac
	done
	shift $((OPTIND -1))

	if [[ $# -eq 0  && "${debug_shell}" == "no" ]]; then
		echo "No command specified, will run ${DEFAULT_COMMAND} in the vm"
	else
		command="$@"
	fi

	local kconfig_file="${OUTPUT_DIR}/latest.config"
	local make_command="make -j ${NUM_COMPILE_JOBS} KCONFIG_CONFIG=${kconfig_file}"

	# Figure out where the kernel is being built.
	# O takes precedence over KBUILD_OUTPUT.
	if [[ "${O:=""}" != "" ]]; then
		if is_rel_path "${O}"; then
			O="$(realpath "${PWD}/${O}")"
		fi
		kernel_bzimage="${O}/${BZIMAGE}"
		make_command="${make_command} O=${O}"
	elif [[ "${KBUILD_OUTPUT:=""}" != "" ]]; then
		if is_rel_path "${KBUILD_OUTPUT}"; then
			KBUILD_OUTPUT="$(realpath "${PWD}/${KBUILD_OUTPUT}")"
		fi
		kernel_bzimage="${KBUILD_OUTPUT}/${BZIMAGE}"
		make_command="${make_command} KBUILD_OUTPUT=${KBUILD_OUTPUT}"
	fi

	populate_url_map

	local rootfs_img="${OUTPUT_DIR}/${ROOTFS_IMAGE}"
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"

	echo "Output directory: ${OUTPUT_DIR}"

	mkdir -p "${OUTPUT_DIR}"
	mkdir -p "${mount_dir}"
	update_kconfig "${kernel_checkout}" "${kconfig_file}"

	recompile_kernel "${kernel_checkout}" "${make_command}"

	if [[ "${update_image}" == "no" && ! -f "${rootfs_img}" ]]; then
		echo "rootfs image not found in ${rootfs_img}"
		update_image="yes"
	fi

	if [[ "${update_image}" == "yes" ]]; then
		create_vm_image
	fi

	update_selftests "${kernel_checkout}" "${make_command}"
	update_init_script "${command}" "${exit_command}"
	run_vm "${kernel_bzimage}"
	if [[ "${command}" != "" ]]; then
		copy_logs
		echo "Logs saved in ${OUTPUT_DIR}/${LOG_FILE}"
	fi
}

catch()
{
	local exit_code=$1
	local exit_status_file="${OUTPUT_DIR}/${EXIT_STATUS_FILE}"
	# This is just a cleanup and the directory may
	# have already been unmounted. So, don't let this
	# clobber the error code we intend to return.
	unmount_image || true
	if [[ -f "${exit_status_file}" ]]; then
		exit_code="$(cat ${exit_status_file})"
	fi
	exit ${exit_code}
}

trap 'catch "$?"' EXIT

main "$@"
