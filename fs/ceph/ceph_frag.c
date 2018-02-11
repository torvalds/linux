// SPDX-License-Identifier: GPL-2.0
/*
 * Ceph 'frag' type
 */
#include <linux/module.h>
#include <linux/ceph/types.h>

int ceph_frag_compare(__u32 a, __u32 b)
{
	unsigned va = ceph_frag_value(a);
	unsigned vb = ceph_frag_value(b);
	if (va < vb)
		return -1;
	if (va > vb)
		return 1;
	va = ceph_frag_bits(a);
	vb = ceph_frag_bits(b);
	if (va < vb)
		return -1;
	if (va > vb)
		return 1;
	return 0;
}
