#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -u
set -e

# This script currently only works for x86_64
ARCH="$(uname -m)"
case "${ARCH}" in
x86_64)
	QEMU_BINARY=qemu-system-x86_64
	BZIMAGE="arch/x86/boot/bzImage"
	;;
*)
	echo "Unsupported architecture"
	exit 1
	;;
esac
DEFAULT_COMMAND="./hid_bpf"
SCRIPT_DIR="$(dirname $(realpath $0))"
OUTPUT_DIR="$SCRIPT_DIR/results"
KCONFIG_REL_PATHS=("${SCRIPT_DIR}/config" "${SCRIPT_DIR}/config.common" "${SCRIPT_DIR}/config.${ARCH}")
B2C_URL="https://gitlab.freedesktop.org/mupuf/boot2container/-/raw/master/vm2c.py"
NUM_COMPILE_JOBS="$(nproc)"
LOG_FILE_BASE="$(date +"hid_selftests.%Y-%m-%d_%H-%M-%S")"
LOG_FILE="${LOG_FILE_BASE}.log"
EXIT_STATUS_FILE="${LOG_FILE_BASE}.exit_status"
CONTAINER_IMAGE="registry.fedoraproject.org/fedora:36"

usage()
{
	cat <<EOF
Usage: $0 [-i] [-s] [-d <output_dir>] -- [<command>]

<command> is the command you would normally run when you are in
tools/testing/selftests/bpf. e.g:

	$0 -- ./hid_bpf

If no command is specified and a debug shell (-s) is not requested,
"${DEFAULT_COMMAND}" will be run by default.

If you build your kernel using KBUILD_OUTPUT= or O= options, these
can be passed as environment variables to the script:

  O=<kernel_build_path> $0 -- ./hid_bpf

or

  KBUILD_OUTPUT=<kernel_build_path> $0 -- ./hid_bpf

Options:

	-u)		Update the boot2container script to a newer version.
	-d)		Update the output directory (default: ${OUTPUT_DIR})
	-j)		Number of jobs for compilation, similar to -j in make
			(default: ${NUM_COMPILE_JOBS})
	-s)		Instead of powering off the VM, start an interactive
			shell. If <command> is specified, the shell runs after
			the command finishes executing
EOF
}

download()
{
	local file="$1"

	echo "Downloading $file..." >&2
	curl -Lsf "$file" -o "${@:2}"
}

recompile_kernel()
{
	local kernel_checkout="$1"
	local make_command="$2"

	cd "${kernel_checkout}"

	${make_command} olddefconfig
	${make_command}
}

update_selftests()
{
	local kernel_checkout="$1"
	local selftests_dir="${kernel_checkout}/tools/testing/selftests/hid"

	cd "${selftests_dir}"
	${make_command}
}

run_vm()
{
	local b2c="$1"
	local kernel_bzimage="$2"
	local command="$3"
	local post_command=""

	if ! which "${QEMU_BINARY}" &> /dev/null; then
		cat <<EOF
Could not find ${QEMU_BINARY}
Please install qemu or set the QEMU_BINARY environment variable.
EOF
		exit 1
	fi

	# alpine (used in post-container requires the PATH to have /bin
	export PATH=$PATH:/bin

	if [[ "${debug_shell}" != "yes" ]]
	then
		touch ${OUTPUT_DIR}/${LOG_FILE}
		command="mount bpffs -t bpf /sys/fs/bpf/; set -o pipefail ; ${command} 2>&1 | tee ${OUTPUT_DIR}/${LOG_FILE}"
		post_command="cat ${OUTPUT_DIR}/${LOG_FILE}"
	else
		command="mount bpffs -t bpf /sys/fs/bpf/; ${command}"
	fi

	set +e
	$b2c --command "${command}" \
	     --kernel ${kernel_bzimage} \
	     --workdir ${OUTPUT_DIR} \
	     --image ${CONTAINER_IMAGE}

	echo $? > ${OUTPUT_DIR}/${EXIT_STATUS_FILE}

	set -e

	${post_command}
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
		local kconfig_src="${config}"
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
			local kconfig_src="${config}"
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
	local update_b2c="no"
	local debug_shell="no"

	while getopts ':hsud:j:' opt; do
		case ${opt} in
		u)
			update_b2c="yes"
			;;
		d)
			OUTPUT_DIR="$OPTARG"
			;;
		j)
			NUM_COMPILE_JOBS="$OPTARG"
			;;
		s)
			command="/bin/sh"
			debug_shell="yes"
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

	# trap 'catch "$?"' EXIT

	if [[ "${debug_shell}" == "no" ]]; then
		if [[ $# -eq 0 ]]; then
			echo "No command specified, will run ${DEFAULT_COMMAND} in the vm"
		else
			command="$@"

			if [[ "${command}" == "/bin/bash" || "${command}" == "bash" ]]
			then
				debug_shell="yes"
			fi
		fi
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

	local b2c="${OUTPUT_DIR}/vm2c.py"

	echo "Output directory: ${OUTPUT_DIR}"

	mkdir -p "${OUTPUT_DIR}"
	update_kconfig "${kernel_checkout}" "${kconfig_file}"

	recompile_kernel "${kernel_checkout}" "${make_command}"

	if [[ "${update_b2c}" == "no" && ! -f "${b2c}" ]]; then
		echo "vm2c script not found in ${b2c}"
		update_b2c="yes"
	fi

	if [[ "${update_b2c}" == "yes" ]]; then
		download $B2C_URL $b2c
		chmod +x $b2c
	fi

	update_selftests "${kernel_checkout}" "${make_command}"
	run_vm $b2c "${kernel_bzimage}" "${command}"
	if [[ "${debug_shell}" != "yes" ]]; then
		echo "Logs saved in ${OUTPUT_DIR}/${LOG_FILE}"
	fi

	exit $(cat ${OUTPUT_DIR}/${EXIT_STATUS_FILE})
}

main "$@"
