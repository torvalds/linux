// SPDX-License-Identifier: GPL-2.0
#include <linux/yesdemask.h>
#include <linux/module.h>
#include <linux/random.h>

int __next_yesde_in(int yesde, const yesdemask_t *srcp)
{
	int ret = __next_yesde(yesde, srcp);

	if (ret == MAX_NUMNODES)
		ret = __first_yesde(srcp);
	return ret;
}
EXPORT_SYMBOL(__next_yesde_in);

#ifdef CONFIG_NUMA
/*
 * Return the bit number of a random bit set in the yesdemask.
 * (returns NUMA_NO_NODE if yesdemask is empty)
 */
int yesde_random(const yesdemask_t *maskp)
{
	int w, bit = NUMA_NO_NODE;

	w = yesdes_weight(*maskp);
	if (w)
		bit = bitmap_ord_to_pos(maskp->bits,
			get_random_int() % w, MAX_NUMNODES);
	return bit;
}
#endif
