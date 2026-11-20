/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_RECURSION_H_
#define _NF_RECURSION_H_

#include <linux/netdevice.h>

#define NF_RECURSION_LIMIT	2

#ifndef CONFIG_PREEMPT_RT
static inline bool nf_dev_xmit_recursion(void)
{
	return unlikely(__this_cpu_read(softnet_data.xmit.nf_xmit_skb_recursion) >
			NF_RECURSION_LIMIT);
}

static inline void nf_dev_xmit_recursion_inc(void)
{
	__this_cpu_inc(softnet_data.xmit.nf_xmit_skb_recursion);
}

static inline void nf_dev_xmit_recursion_dec(void)
{
	__this_cpu_dec(softnet_data.xmit.nf_xmit_skb_recursion);
}
#else
static inline bool nf_dev_xmit_recursion(void)
{
	return unlikely(current->net_xmit.nf_xmit_skb_recursion > NF_RECURSION_LIMIT);
}

static inline void nf_dev_xmit_recursion_inc(void)
{
	current->net_xmit.nf_xmit_skb_recursion++;
}

static inline void nf_dev_xmit_recursion_dec(void)
{
	current->net_xmit.nf_xmit_skb_recursion--;
}
#endif

#endif /* _NF_RECURSION_H_ */
