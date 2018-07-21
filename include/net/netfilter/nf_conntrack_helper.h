/* SPDX-License-Identifier: GPL-2.0 */
/*
 * connection tracking helpers.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_helper.h
 */

#ifndef _NF_CONNTRACK_HELPER_H
#define _NF_CONNTRACK_HELPER_H
#include <linux/refcount.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_expect.h>

struct module;

enum nf_ct_helper_flags {
	NF_CT_HELPER_F_USERSPACE	= (1 << 0),
	NF_CT_HELPER_F_CONFIGURED	= (1 << 1),
};

#define NF_CT_HELPER_NAME_LEN	16

struct nf_conntrack_helper {
	struct hlist_node hnode;	/* Internal use. */

	char name[NF_CT_HELPER_NAME_LEN]; /* name of the module */
	refcount_t refcnt;
	struct module *me;		/* pointer to self */
	const struct nf_conntrack_expect_policy *expect_policy;

	/* Tuple of things we will help (compared against server response) */
	struct nf_conntrack_tuple tuple;

	/* Function to call when data passes; return verdict, or -1 to
           invalidate. */
	int (*help)(struct sk_buff *skb,
		    unsigned int protoff,
		    struct nf_conn *ct,
		    enum ip_conntrack_info conntrackinfo);

	void (*destroy)(struct nf_conn *ct);

	int (*from_nlattr)(struct nlattr *attr, struct nf_conn *ct);
	int (*to_nlattr)(struct sk_buff *skb, const struct nf_conn *ct);
	unsigned int expect_class_max;

	unsigned int flags;

	/* For user-space helpers: */
	unsigned int queue_num;
	/* length of userspace private data stored in nf_conn_help->data */
	u16 data_len;
};

/* Must be kept in sync with the classes defined by helpers */
#define NF_CT_MAX_EXPECT_CLASSES	4

/* nf_conn feature for connections that have a helper */
struct nf_conn_help {
	/* Helper. if any */
	struct nf_conntrack_helper __rcu *helper;

	struct hlist_head expectations;

	/* Current number of expected connections */
	u8 expecting[NF_CT_MAX_EXPECT_CLASSES];

	/* private helper information. */
	char data[32] __aligned(8);
};

#define NF_CT_HELPER_BUILD_BUG_ON(structsize) \
	BUILD_BUG_ON((structsize) > FIELD_SIZEOF(struct nf_conn_help, data))

struct nf_conntrack_helper *__nf_conntrack_helper_find(const char *name,
						       u16 l3num, u8 protonum);

struct nf_conntrack_helper *nf_conntrack_helper_try_module_get(const char *name,
							       u16 l3num,
							       u8 protonum);
void nf_conntrack_helper_put(struct nf_conntrack_helper *helper);

void nf_ct_helper_init(struct nf_conntrack_helper *helper,
		       u16 l3num, u16 protonum, const char *name,
		       u16 default_port, u16 spec_port, u32 id,
		       const struct nf_conntrack_expect_policy *exp_pol,
		       u32 expect_class_max,
		       int (*help)(struct sk_buff *skb, unsigned int protoff,
				   struct nf_conn *ct,
				   enum ip_conntrack_info ctinfo),
		       int (*from_nlattr)(struct nlattr *attr,
					  struct nf_conn *ct),
		       struct module *module);

int nf_conntrack_helper_register(struct nf_conntrack_helper *);
void nf_conntrack_helper_unregister(struct nf_conntrack_helper *);

int nf_conntrack_helpers_register(struct nf_conntrack_helper *, unsigned int);
void nf_conntrack_helpers_unregister(struct nf_conntrack_helper *,
				     unsigned int);

struct nf_conn_help *nf_ct_helper_ext_add(struct nf_conn *ct, gfp_t gfp);

int __nf_ct_try_assign_helper(struct nf_conn *ct, struct nf_conn *tmpl,
			      gfp_t flags);

void nf_ct_helper_destroy(struct nf_conn *ct);

static inline struct nf_conn_help *nfct_help(const struct nf_conn *ct)
{
	return nf_ct_ext_find(ct, NF_CT_EXT_HELPER);
}

static inline void *nfct_help_data(const struct nf_conn *ct)
{
	struct nf_conn_help *help;

	help = nf_ct_ext_find(ct, NF_CT_EXT_HELPER);

	return (void *)help->data;
}

int nf_conntrack_helper_pernet_init(struct net *net);
void nf_conntrack_helper_pernet_fini(struct net *net);

int nf_conntrack_helper_init(void);
void nf_conntrack_helper_fini(void);

int nf_conntrack_broadcast_help(struct sk_buff *skb, struct nf_conn *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int timeout);

struct nf_ct_helper_expectfn {
	struct list_head head;
	const char *name;
	void (*expectfn)(struct nf_conn *ct, struct nf_conntrack_expect *exp);
};

__printf(3,4)
void nf_ct_helper_log(struct sk_buff *skb, const struct nf_conn *ct,
		      const char *fmt, ...);

void nf_ct_helper_expectfn_register(struct nf_ct_helper_expectfn *n);
void nf_ct_helper_expectfn_unregister(struct nf_ct_helper_expectfn *n);
struct nf_ct_helper_expectfn *
nf_ct_helper_expectfn_find_by_name(const char *name);
struct nf_ct_helper_expectfn *
nf_ct_helper_expectfn_find_by_symbol(const void *symbol);

extern struct hlist_head *nf_ct_helper_hash;
extern unsigned int nf_ct_helper_hsize;

#endif /*_NF_CONNTRACK_HELPER_H*/
