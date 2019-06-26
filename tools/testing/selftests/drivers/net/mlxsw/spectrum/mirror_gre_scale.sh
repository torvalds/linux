# SPDX-License-Identifier: GPL-2.0
source ../mirror_gre_scale.sh

mirror_gre_get_target()
{
	local should_fail=$1; shift

	if ((! should_fail)); then
		echo 3
	else
		echo 4
	fi
}
