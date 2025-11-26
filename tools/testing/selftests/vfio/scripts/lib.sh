# SPDX-License-Identifier: GPL-2.0-or-later

readonly DEVICES_DIR="${TMPDIR:-/tmp}/vfio-selftests-devices"

function write_to() {
	# Unfortunately set -x does not show redirects so use echo to manually
	# tell the user what commands are being run.
	echo "+ echo \"${2}\" > ${1}"
	echo "${2}" > ${1}
}

function get_driver() {
	if [ -L /sys/bus/pci/devices/${1}/driver ]; then
		basename $(readlink -m /sys/bus/pci/devices/${1}/driver)
	fi
}

function bind() {
	write_to /sys/bus/pci/drivers/${2}/bind ${1}
}

function unbind() {
	write_to /sys/bus/pci/drivers/${2}/unbind ${1}
}

function set_sriov_numvfs() {
	write_to /sys/bus/pci/devices/${1}/sriov_numvfs ${2}
}

function get_sriov_numvfs() {
	if [ -f /sys/bus/pci/devices/${1}/sriov_numvfs ]; then
		cat /sys/bus/pci/devices/${1}/sriov_numvfs
	fi
}

function set_driver_override() {
	write_to /sys/bus/pci/devices/${1}/driver_override ${2}
}

function clear_driver_override() {
	set_driver_override ${1} ""
}
