/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NF_CONNTRACK_BPF_H
#define _NF_CONNTRACK_BPF_H

#include <linux/kconfig.h>
#include <net/netfilter/nf_conntrack.h>

struct nf_conn___init {
	struct nf_conn ct;
};

#if (IS_BUILTIN(CONFIG_NF_CONNTRACK) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) || \
    (IS_MODULE(CONFIG_NF_CONNTRACK) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF_MODULES))

extern int register_nf_conntrack_bpf(void);
extern void cleanup_nf_conntrack_bpf(void);

#else

static inline int register_nf_conntrack_bpf(void)
{
	return 0;
}

static inline void cleanup_nf_conntrack_bpf(void)
{
}

#endif

#if (IS_BUILTIN(CONFIG_NF_NAT) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) || \
    (IS_MODULE(CONFIG_NF_NAT) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF_MODULES))

extern int register_nf_nat_bpf(void);

#else

static inline int register_nf_nat_bpf(void)
{
	return 0;
}

#endif

#endif /* _NF_CONNTRACK_BPF_H */
