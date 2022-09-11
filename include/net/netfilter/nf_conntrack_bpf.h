/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NF_CONNTRACK_BPF_H
#define _NF_CONNTRACK_BPF_H

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/kconfig.h>
#include <linux/mutex.h>

#if (IS_BUILTIN(CONFIG_NF_CONNTRACK) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) || \
    (IS_MODULE(CONFIG_NF_CONNTRACK) && IS_ENABLED(CONFIG_DEBUG_INFO_BTF_MODULES))

extern int register_nf_conntrack_bpf(void);
extern void cleanup_nf_conntrack_bpf(void);

extern struct mutex nf_conn_btf_access_lock;
extern int (*nfct_bsa)(struct bpf_verifier_log *log, const struct btf *btf,
		       const struct btf_type *t, int off, int size,
		       enum bpf_access_type atype, u32 *next_btf_id,
		       enum bpf_type_flag *flag);

#else

static inline int register_nf_conntrack_bpf(void)
{
	return 0;
}

static inline void cleanup_nf_conntrack_bpf(void)
{
}

static inline int nf_conntrack_btf_struct_access(struct bpf_verifier_log *log,
						 const struct btf *btf,
						 const struct btf_type *t, int off,
						 int size, enum bpf_access_type atype,
						 u32 *next_btf_id,
						 enum bpf_type_flag *flag)
{
	return -EACCES;
}

#endif

#endif /* _NF_CONNTRACK_BPF_H */
