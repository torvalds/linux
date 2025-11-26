# SPDX-License-Identifier: GPL-2.0-or-later

source $(dirname -- "${BASH_SOURCE[0]}")/lib.sh

function main() {
	local device_bdfs=$(ls ${DEVICES_DIR})

	if [ -z "${device_bdfs}" ]; then
		echo "No devices found, skipping."
		exit 4
	fi

	"$@" ${device_bdfs}
}

main "$@"
