#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -u
set -e

# This script currently only works for x86_64, as
# it is based on the VM image used by the BPF CI which is
# x86_64.
QEMU_BINARY="${QEMU_BINARY:="qemu-system-x86_64"}"
X86_BZIMAGE="arch/x86/boot/bzImage"
DEFAULT_COMMAND="./test_progs"
MOUNT_DIR="mnt"
ROOTFS_IMAGE="root.img"
OUTPUT_DIR="$HOME/.bpf_selftests"
KCONFIG_URL="https://raw.githubusercontent.com/libbpf/libbpf/master/travis-ci/vmtest/configs/latest.config"
KCONFIG_API_URL="https://api.github.com/repos/libbpf/libbpf/contents/travis-ci/vmtest/configs/latest.config"
INDEX_URL="https://raw.githubusercontent.com/libbpf/libbpf/master/travis-ci/vmtest/configs/INDEX"
NUM_COMPILE_JOBS="$(nproc)"

usage()
{
	cat <<EOF
Usage: $0 [-i] [-d <output_dir>] -- [<command>]

<command> is the command you would normally run when you are in
tools/testing/selftests/bpf. e.g:

	$0 -- ./test_progs -t test_lsm

If no command is specified, "${DEFAULT_COMMAND}" will be run by
default.

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
		if [[ $file =~ ^libbpf-vmtest-rootfs-(.*)\.tar\.zst$ ]]; then
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

	download "libbpf-vmtest-rootfs-$rootfsversion.tar.zst" |
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
	local log_file="$2"

	mount_image

	if [[ ! -d "${init_script_dir}" ]]; then
		cat <<EOF
Could not find ${init_script_dir} in the mounted image.
This likely indicates a bad rootfs image, Please download
a new image by passing "-i" to the script
EOF
		exit 1

	fi

	sudo bash -c "cat >${init_script}" <<EOF
#!/bin/bash

{
	cd /root/bpf
	echo ${command}
	stdbuf -oL -eL ${command}
} 2>&1 | tee /root/${log_file}
poweroff -f
EOF

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
		-cpu kvm64 \
		-enable-kvm \
		-smp 4 \
		-m 2G \
		-drive file="${rootfs_img}",format=raw,index=1,media=disk,if=virtio,cache=none \
		-kernel "${kernel_bzimage}" \
		-append "root=/dev/vda rw console=ttyS0,115200"
}

copy_logs()
{
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"
	local log_file="${mount_dir}/root/$1"

	mount_image
	sudo cp ${log_file} "${OUTPUT_DIR}"
	sudo rm -f ${log_file}
	unmount_image
}

is_rel_path()
{
	local path="$1"

	[[ ${path:0:1} != "/" ]]
}

update_kconfig()
{
	local kconfig_file="$1"
	local update_command="curl -sLf ${KCONFIG_URL} -o ${kconfig_file}"
	# Github does not return the "last-modified" header when retrieving the
	# raw contents of the file. Use the API call to get the last-modified
	# time of the kernel config and only update the config if it has been
	# updated after the previously cached config was created. This avoids
	# unnecessarily compiling the kernel and selftests.
	if [[ -f "${kconfig_file}" ]]; then
		local last_modified_date="$(curl -sL -D - "${KCONFIG_API_URL}" -o /dev/null | \
			grep "last-modified" | awk -F ': ' '{print $2}')"
		local remote_modified_timestamp="$(date -d "${last_modified_date}" +"%s")"
		local local_creation_timestamp="$(stat -c %Y "${kconfig_file}")"

		if [[ "${remote_modified_timestamp}" -gt "${local_creation_timestamp}" ]]; then
			${update_command}
		fi
	else
		${update_command}
	fi
}

main()
{
	local script_dir="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
	local kernel_checkout=$(realpath "${script_dir}"/../../../../)
	local log_file="$(date +"bpf_selftests.%Y-%m-%d_%H-%M-%S.log")"
	# By default the script searches for the kernel in the checkout directory but
	# it also obeys environment variables O= and KBUILD_OUTPUT=
	local kernel_bzimage="${kernel_checkout}/${X86_BZIMAGE}"
	local command="${DEFAULT_COMMAND}"
	local update_image="no"

	while getopts 'hkid:j:' opt; do
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

	if [[ $# -eq 0 ]]; then
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
		kernel_bzimage="${O}/${X86_BZIMAGE}"
		make_command="${make_command} O=${O}"
	elif [[ "${KBUILD_OUTPUT:=""}" != "" ]]; then
		if is_rel_path "${KBUILD_OUTPUT}"; then
			KBUILD_OUTPUT="$(realpath "${PWD}/${KBUILD_OUTPUT}")"
		fi
		kernel_bzimage="${KBUILD_OUTPUT}/${X86_BZIMAGE}"
		make_command="${make_command} KBUILD_OUTPUT=${KBUILD_OUTPUT}"
	fi

	populate_url_map

	local rootfs_img="${OUTPUT_DIR}/${ROOTFS_IMAGE}"
	local mount_dir="${OUTPUT_DIR}/${MOUNT_DIR}"

	echo "Output directory: ${OUTPUT_DIR}"

	mkdir -p "${OUTPUT_DIR}"
	mkdir -p "${mount_dir}"
	update_kconfig "${kconfig_file}"

	recompile_kernel "${kernel_checkout}" "${make_command}"

	if [[ "${update_image}" == "no" && ! -f "${rootfs_img}" ]]; then
		echo "rootfs image not found in ${rootfs_img}"
		update_image="yes"
	fi

	if [[ "${update_image}" == "yes" ]]; then
		create_vm_image
	fi

	update_selftests "${kernel_checkout}" "${make_command}"
	update_init_script "${command}" "${log_file}"
	run_vm "${kernel_bzimage}"
	copy_logs "${log_file}"
	echo "Logs saved in ${OUTPUT_DIR}/${log_file}"
}

catch()
{
	local exit_code=$1
	# This is just a cleanup and the directory may
	# have already been unmounted. So, don't let this
	# clobber the error code we intend to return.
	unmount_image || true
	exit ${exit_code}
}

trap 'catch "$?"' EXIT

main "$@"
