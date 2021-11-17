# SPDX-License-Identifier: GPL-2.0
source ../tc_flower_scale.sh

tc_flower_get_target()
{
	local should_fail=$1; shift

	# 6144 (6x1024) is the theoretical maximum.
	# One bank of 512 rules is taken by the 18-byte MC router rule.
	# One rule is the ACL catch-all.
	# 6144 - 512 - 1 = 5631
	local target=5631

	if ((! should_fail)); then
		echo $target
	else
		echo $((target + 1))
	fi
}
