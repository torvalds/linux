/* SPDX-License-Identifier: GPL-2.0 */
/*
 * connection tracking expectations.
 */

#ifndef _NF_CONNTRACK_EXPECT_H
#define _NF_CONNTRACK_EXPECT_H

#include <linux/refcount.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_zones.h>

extern unsigned int nf_ct_expect_hsize;
extern unsigned int nf_ct_expect_max;
extern struct hlist_head *nf_ct_expect_hash;

struct nf_conntrack_expect {
	/* Conntrack expectation list member */
	struct hlist_node lnode;

	/* Hash member */
	struct hlist_node hnode;

	/* We expect this tuple, with the following mask */
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_mask mask;

	/* Usage count. */
	refcount_t use;

	/* Flags */
	unsigned int flags;

	/* Expectation class */
	unsigned int class;

	/* Function to call after setup and insertion */
	void (*expectfn)(struct nf_conn *new,
			 struct nf_conntrack_expect *this);

	/* Helper to assign to new connection */
	struct nf_conntrack_helper *helper;

	/* The conntrack of the master connection */
	struct nf_conn *master;

	/* Timer function; deletes the expectation. */
	struct timer_list timeout;

#if IS_ENABLED(CONFIG_NF_NAT)
	union nf_inet_addr saved_addr;
	/* This is the original per-proto part, used to map the
	 * expected connection the way the recipient expects. */
	union nf_conntrack_man_proto saved_proto;
	/* Direction relative to the master connection. */
	enum ip_conntrack_dir dir;
#endif

	struct rcu_head rcu;
};

static inline struct net *nf_ct_exp_net(struct nf_conntrack_expect *exp)
{
	return nf_ct_net(exp->master);
}

#define NF_CT_EXP_POLICY_NAME_LEN	16

struct nf_conntrack_expect_policy {
	unsigned int	max_expected;
	unsigned int	timeout;
	char		name[NF_CT_EXP_POLICY_NAME_LEN];
};

#define NF_CT_EXPECT_CLASS_DEFAULT	0
#define NF_CT_EXPECT_MAX_CNT		255

/* Allow to reuse expectations with the same tuples from different master
 * conntracks.
 */
#define NF_CT_EXP_F_SKIP_MASTER	0x1

int nf_conntrack_expect_pernet_init(struct net *net);
void nf_conntrack_expect_pernet_fini(struct net *net);

int nf_conntrack_expect_init(void);
void nf_conntrack_expect_fini(void);

struct nf_conntrack_expect *
__nf_ct_expect_find(struct net *net,
		    const struct nf_conntrack_zone *zone,
		    const struct nf_conntrack_tuple *tuple);

struct nf_conntrack_expect *
nf_ct_expect_find_get(struct net *net,
		      const struct nf_conntrack_zone *zone,
		      const struct nf_conntrack_tuple *tuple);

struct nf_conntrack_expect *
nf_ct_find_expectation(struct net *net,
		       const struct nf_conntrack_zone *zone,
		       const struct nf_conntrack_tuple *tuple, bool unlink);

void nf_ct_unlink_expect_report(struct nf_conntrack_expect *exp,
				u32 portid, int report);
static inline void nf_ct_unlink_expect(struct nf_conntrack_expect *exp)
{
	nf_ct_unlink_expect_report(exp, 0, 0);
}

void nf_ct_remove_expectations(struct nf_conn *ct);
void nf_ct_unexpect_related(struct nf_conntrack_expect *exp);
bool nf_ct_remove_expect(struct nf_conntrack_expect *exp);

void nf_ct_expect_iterate_destroy(bool (*iter)(struct nf_conntrack_expect *e, void *data), void *data);
void nf_ct_expect_iterate_net(struct net *net,
			      bool (*iter)(struct nf_conntrack_expect *e, void *data),
                              void *data, u32 portid, int report);

/* Allocate space for an expectation: this is mandatory before calling
   nf_ct_expect_related.  You will have to call put afterwards. */
struct nf_conntrack_expect *nf_ct_expect_alloc(struct nf_conn *me);
void nf_ct_expect_init(struct nf_conntrack_expect *, unsigned int, u_int8_t,
		       const union nf_inet_addr *,
		       const union nf_inet_addr *,
		       u_int8_t, const __be16 *, const __be16 *);
void nf_ct_expect_put(struct nf_conntrack_expect *exp);
int nf_ct_expect_related_report(struct nf_conntrack_expect *expect,
				u32 portid, int report, unsigned int flags);
static inline int nf_ct_expect_related(struct nf_conntrack_expect *expect,
				       unsigned int flags)
{
	return nf_ct_expect_related_report(expect, 0, 0, flags);
}

#endif /*_NF_CONNTRACK_EXPECT_H*/

