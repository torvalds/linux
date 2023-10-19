# SPDX-License-Identifier: GPL-2.0
source ../rif_mac_profile_scale.sh

rif_mac_profile_get_target()
{
	local should_fail=$1
	local target

	target=$(devlink_resource_size_get rif_mac_profiles)

	if ((! should_fail)); then
		echo $target
	else
		echo $((target + 1))
	fi
}
