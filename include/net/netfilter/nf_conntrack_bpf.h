/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NF_CONNTRACK_BPF_H
#define _NF_CONNTRACK_BPF_H

#include <linux/btf.h>
#include <linux/kconfig.h>

#if (IS_BUILTIN(CONFIG_NF_CONNTRACK) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) || \
    (IS_MODULE(CONFIG_NF_CONNTRACK) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF_MODULES))

extern int register_nf_conntrack_bpf(void);

#else

static inline int register_nf_conntrack_bpf(void)
{
	return 0;
}

#endif

#endif /* _NF_CONNTRACK_BPF_H */
