# SPDX-License-Identifier: GPL-2.0-or-later

# Global variables initialized in main() and then used during cleanup() when
# the script exits.
declare DEVICE_BDF
declare NEW_DRIVER
declare OLD_DRIVER
declare OLD_NUMVFS
declare DRIVER_OVERRIDE

function write_to() {
	# Unfortunately set -x does not show redirects so use echo to manually
	# tell the user what commands are being run.
	echo "+ echo \"${2}\" > ${1}"
	echo "${2}" > ${1}
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

function set_driver_override() {
	write_to /sys/bus/pci/devices/${1}/driver_override ${2}
}

function clear_driver_override() {
	set_driver_override ${1} ""
}

function cleanup() {
	if [ "${NEW_DRIVER}"      ]; then unbind ${DEVICE_BDF} ${NEW_DRIVER} ; fi
	if [ "${DRIVER_OVERRIDE}" ]; then clear_driver_override ${DEVICE_BDF} ; fi
	if [ "${OLD_DRIVER}"      ]; then bind ${DEVICE_BDF} ${OLD_DRIVER} ; fi
	if [ "${OLD_NUMVFS}"      ]; then set_sriov_numvfs ${DEVICE_BDF} ${OLD_NUMVFS} ; fi
}

function usage() {
	echo "usage: $0 [-d segment:bus:device.function] [-s] [-h] [cmd ...]" >&2
	echo >&2
	echo "  -d: The BDF of the device to use for the test (required)" >&2
	echo "  -h: Show this help message" >&2
	echo "  -s: Drop into a shell rather than running a command" >&2
	echo >&2
	echo "   cmd: The command to run and arguments to pass to it." >&2
	echo "        Required when not using -s. The SBDF will be " >&2
	echo "        appended to the argument list." >&2
	exit 1
}

function main() {
	local shell

	while getopts "d:hs" opt; do
		case $opt in
			d) DEVICE_BDF="$OPTARG" ;;
			s) shell=true ;;
			*) usage ;;
		esac
	done

	# Shift past all optional arguments.
	shift $((OPTIND - 1))

	# Check that the user passed in the command to run.
	[ ! "${shell}" ] && [ $# = 0 ] && usage

	# Check that the user passed in a BDF.
	[ "${DEVICE_BDF}" ] || usage

	trap cleanup EXIT
	set -e

	test -d /sys/bus/pci/devices/${DEVICE_BDF}

	if [ -f /sys/bus/pci/devices/${DEVICE_BDF}/sriov_numvfs ]; then
		OLD_NUMVFS=$(cat /sys/bus/pci/devices/${DEVICE_BDF}/sriov_numvfs)
		set_sriov_numvfs ${DEVICE_BDF} 0
	fi

	if [ -L /sys/bus/pci/devices/${DEVICE_BDF}/driver ]; then
		OLD_DRIVER=$(basename $(readlink -m /sys/bus/pci/devices/${DEVICE_BDF}/driver))
		unbind ${DEVICE_BDF} ${OLD_DRIVER}
	fi

	set_driver_override ${DEVICE_BDF} vfio-pci
	DRIVER_OVERRIDE=true

	bind ${DEVICE_BDF} vfio-pci
	NEW_DRIVER=vfio-pci

	echo
	if [ "${shell}" ]; then
		echo "Dropping into ${SHELL} with VFIO_SELFTESTS_BDF=${DEVICE_BDF}"
		VFIO_SELFTESTS_BDF=${DEVICE_BDF} ${SHELL}
	else
		"$@" ${DEVICE_BDF}
	fi
	echo
}

main "$@"
