#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

##############################################################################
# Defines

if [[ ! -v MLXSW_CHIP ]]; then
	MLXSW_CHIP=$(devlink -j dev info $DEVLINK_DEV | jq -r '.[][]["driver"]')
	if [ -z "$MLXSW_CHIP" ]; then
		echo "SKIP: Device $DEVLINK_DEV doesn't support devlink info command"
		exit 1
	fi
fi

MLXSW_SPECTRUM_REV=$(case $MLXSW_CHIP in
			     mlxsw_spectrum)
				     echo 1 ;;
			     mlxsw_spectrum*)
				     echo ${MLXSW_CHIP#mlxsw_spectrum} ;;
			     *)
				     echo "Couldn't determine Spectrum chip revision." \
					  > /dev/stderr ;;
		     esac)

mlxsw_on_spectrum()
{
	local rev=$1; shift
	local op="=="
	local rev2=${rev%+}

	if [[ $rev2 != $rev ]]; then
		op=">="
	fi

	((MLXSW_SPECTRUM_REV $op rev2))
}

__mlxsw_only_on_spectrum()
{
	local rev=$1; shift
	local caller=$1; shift
	local src=$1; shift

	if ! mlxsw_on_spectrum "$rev"; then
		log_test_skip $src:$caller "(Spectrum-$rev only)"
		return 1
	fi
}

mlxsw_only_on_spectrum()
{
	local caller=${FUNCNAME[1]}
	local src=${BASH_SOURCE[1]}
	local rev

	for rev in "$@"; do
		if __mlxsw_only_on_spectrum "$rev" "$caller" "$src"; then
			return 0
		fi
	done

	return 1
}
