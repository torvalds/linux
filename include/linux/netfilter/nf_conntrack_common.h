/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_COMMON_H
#define _NF_CONNTRACK_COMMON_H

#include <linux/atomic.h>
#include <uapi/linux/netfilter/nf_conntrack_common.h>

struct ip_conntrack_stat {
	unsigned int found;
	unsigned int invalid;
	unsigned int ignore;
	unsigned int insert;
	unsigned int insert_failed;
	unsigned int drop;
	unsigned int early_drop;
	unsigned int error;
	unsigned int expect_new;
	unsigned int expect_create;
	unsigned int expect_delete;
	unsigned int search_restart;
};

#define NFCT_INFOMASK	7UL
#define NFCT_PTRMASK	~(NFCT_INFOMASK)

struct nf_conntrack {
	atomic_t use;
};

void nf_conntrack_destroy(struct nf_conntrack *nfct);
static inline void nf_conntrack_put(struct nf_conntrack *nfct)
{
	if (nfct && atomic_dec_and_test(&nfct->use))
		nf_conntrack_destroy(nfct);
}
static inline void nf_conntrack_get(struct nf_conntrack *nfct)
{
	if (nfct)
		atomic_inc(&nfct->use);
}

#endif /* _NF_CONNTRACK_COMMON_H */
