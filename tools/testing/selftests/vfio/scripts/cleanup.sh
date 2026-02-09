# SPDX-License-Identifier: GPL-2.0-or-later

source $(dirname -- "${BASH_SOURCE[0]}")/lib.sh

function cleanup_devices() {
	local device_bdf
	local device_dir

	for device_bdf in "$@"; do
		device_dir=${DEVICES_DIR}/${device_bdf}

		if [ -f ${device_dir}/vfio-pci ]; then
			unbind ${device_bdf} vfio-pci
		fi

		if [ -f ${device_dir}/driver_override ]; then
			clear_driver_override ${device_bdf}
		fi

		if [ -f ${device_dir}/driver ]; then
			bind ${device_bdf} $(cat ${device_dir}/driver)
		fi

		if [ -f ${device_dir}/sriov_numvfs ]; then
			set_sriov_numvfs ${device_bdf} $(cat ${device_dir}/sriov_numvfs)
		fi

		rm -rf ${device_dir}
	done
}

function main() {
	if [ $# = 0 ]; then
		cleanup_devices $(ls ${DEVICES_DIR})
		rmdir ${DEVICES_DIR}
	else
		cleanup_devices "$@"
	fi
}

main "$@"
