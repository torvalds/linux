# SPDX-License-Identifier: GPL-2.0-or-later
set -e

source $(dirname -- "${BASH_SOURCE[0]}")/lib.sh

function main() {
	local device_bdf
	local device_dir
	local numvfs
	local driver

	if [ $# = 0 ]; then
		echo "usage: $0 segment:bus:device.function ..." >&2
		exit 1
	fi

	for device_bdf in "$@"; do
		test -d /sys/bus/pci/devices/${device_bdf}

		device_dir=${DEVICES_DIR}/${device_bdf}
		if [ -d "${device_dir}" ]; then
			echo "${device_bdf} has already been set up, exiting."
			exit 0
		fi

		mkdir -p ${device_dir}

		numvfs=$(get_sriov_numvfs ${device_bdf})
		if [ "${numvfs}" ]; then
			set_sriov_numvfs ${device_bdf} 0
			echo ${numvfs} > ${device_dir}/sriov_numvfs
		fi

		driver=$(get_driver ${device_bdf})
		if [ "${driver}" ]; then
			unbind ${device_bdf} ${driver}
			echo ${driver} > ${device_dir}/driver
		fi

		set_driver_override ${device_bdf} vfio-pci
		touch ${device_dir}/driver_override

		bind ${device_bdf} vfio-pci
		touch ${device_dir}/vfio-pci
	done
}

main "$@"
