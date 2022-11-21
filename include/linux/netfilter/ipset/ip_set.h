/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 *                         Martin Josefsson <gandalf@wlug.westbo.se>
 * Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@netfilter.org>
 */
#ifndef _IP_SET_H
#define _IP_SET_H

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/stringify.h>
#include <linux/vmalloc.h>
#include <net/netlink.h>
#include <uapi/linux/netfilter/ipset/ip_set.h>

#define _IP_SET_MODULE_DESC(a, b, c)		\
	MODULE_DESCRIPTION(a " type of IP sets, revisions " b "-" c)
#define IP_SET_MODULE_DESC(a, b, c)		\
	_IP_SET_MODULE_DESC(a, __stringify(b), __stringify(c))

/* Set features */
enum ip_set_feature {
	IPSET_TYPE_IP_FLAG = 0,
	IPSET_TYPE_IP = (1 << IPSET_TYPE_IP_FLAG),
	IPSET_TYPE_PORT_FLAG = 1,
	IPSET_TYPE_PORT = (1 << IPSET_TYPE_PORT_FLAG),
	IPSET_TYPE_MAC_FLAG = 2,
	IPSET_TYPE_MAC = (1 << IPSET_TYPE_MAC_FLAG),
	IPSET_TYPE_IP2_FLAG = 3,
	IPSET_TYPE_IP2 = (1 << IPSET_TYPE_IP2_FLAG),
	IPSET_TYPE_NAME_FLAG = 4,
	IPSET_TYPE_NAME = (1 << IPSET_TYPE_NAME_FLAG),
	IPSET_TYPE_IFACE_FLAG = 5,
	IPSET_TYPE_IFACE = (1 << IPSET_TYPE_IFACE_FLAG),
	IPSET_TYPE_MARK_FLAG = 6,
	IPSET_TYPE_MARK = (1 << IPSET_TYPE_MARK_FLAG),
	IPSET_TYPE_NOMATCH_FLAG = 7,
	IPSET_TYPE_NOMATCH = (1 << IPSET_TYPE_NOMATCH_FLAG),
	/* Strictly speaking not a feature, but a flag for dumping:
	 * this settype must be dumped last */
	IPSET_DUMP_LAST_FLAG = 8,
	IPSET_DUMP_LAST = (1 << IPSET_DUMP_LAST_FLAG),
};

/* Set extensions */
enum ip_set_extension {
	IPSET_EXT_BIT_TIMEOUT = 0,
	IPSET_EXT_TIMEOUT = (1 << IPSET_EXT_BIT_TIMEOUT),
	IPSET_EXT_BIT_COUNTER = 1,
	IPSET_EXT_COUNTER = (1 << IPSET_EXT_BIT_COUNTER),
	IPSET_EXT_BIT_COMMENT = 2,
	IPSET_EXT_COMMENT = (1 << IPSET_EXT_BIT_COMMENT),
	IPSET_EXT_BIT_SKBINFO = 3,
	IPSET_EXT_SKBINFO = (1 << IPSET_EXT_BIT_SKBINFO),
	/* Mark set with an extension which needs to call destroy */
	IPSET_EXT_BIT_DESTROY = 7,
	IPSET_EXT_DESTROY = (1 << IPSET_EXT_BIT_DESTROY),
};

#define SET_WITH_TIMEOUT(s)	((s)->extensions & IPSET_EXT_TIMEOUT)
#define SET_WITH_COUNTER(s)	((s)->extensions & IPSET_EXT_COUNTER)
#define SET_WITH_COMMENT(s)	((s)->extensions & IPSET_EXT_COMMENT)
#define SET_WITH_SKBINFO(s)	((s)->extensions & IPSET_EXT_SKBINFO)
#define SET_WITH_FORCEADD(s)	((s)->flags & IPSET_CREATE_FLAG_FORCEADD)

/* Extension id, in size order */
enum ip_set_ext_id {
	IPSET_EXT_ID_COUNTER = 0,
	IPSET_EXT_ID_TIMEOUT,
	IPSET_EXT_ID_SKBINFO,
	IPSET_EXT_ID_COMMENT,
	IPSET_EXT_ID_MAX,
};

struct ip_set;

/* Extension type */
struct ip_set_ext_type {
	/* Destroy extension private data (can be NULL) */
	void (*destroy)(struct ip_set *set, void *ext);
	enum ip_set_extension type;
	enum ipset_cadt_flags flag;
	/* Size and minimal alignment */
	u8 len;
	u8 align;
};

extern const struct ip_set_ext_type ip_set_extensions[];

struct ip_set_counter {
	atomic64_t bytes;
	atomic64_t packets;
};

struct ip_set_comment_rcu {
	struct rcu_head rcu;
	char str[];
};

struct ip_set_comment {
	struct ip_set_comment_rcu __rcu *c;
};

struct ip_set_skbinfo {
	u32 skbmark;
	u32 skbmarkmask;
	u32 skbprio;
	u16 skbqueue;
	u16 __pad;
};

struct ip_set_ext {
	struct ip_set_skbinfo skbinfo;
	u64 packets;
	u64 bytes;
	char *comment;
	u32 timeout;
	u8 packets_op;
	u8 bytes_op;
	bool target;
};

#define ext_timeout(e, s)	\
((unsigned long *)(((void *)(e)) + (s)->offset[IPSET_EXT_ID_TIMEOUT]))
#define ext_counter(e, s)	\
((struct ip_set_counter *)(((void *)(e)) + (s)->offset[IPSET_EXT_ID_COUNTER]))
#define ext_comment(e, s)	\
((struct ip_set_comment *)(((void *)(e)) + (s)->offset[IPSET_EXT_ID_COMMENT]))
#define ext_skbinfo(e, s)	\
((struct ip_set_skbinfo *)(((void *)(e)) + (s)->offset[IPSET_EXT_ID_SKBINFO]))

typedef int (*ipset_adtfn)(struct ip_set *set, void *value,
			   const struct ip_set_ext *ext,
			   struct ip_set_ext *mext, u32 cmdflags);

/* Kernel API function options */
struct ip_set_adt_opt {
	u8 family;		/* Actual protocol family */
	u8 dim;			/* Dimension of match/target */
	u8 flags;		/* Direction and negation flags */
	u32 cmdflags;		/* Command-like flags */
	struct ip_set_ext ext;	/* Extensions */
};

/* Set type, variant-specific part */
struct ip_set_type_variant {
	/* Kernelspace: test/add/del entries
	 *		returns negative error code,
	 *			zero for no match/success to add/delete
	 *			positive for matching element */
	int (*kadt)(struct ip_set *set, const struct sk_buff *skb,
		    const struct xt_action_param *par,
		    enum ipset_adt adt, struct ip_set_adt_opt *opt);

	/* Userspace: test/add/del entries
	 *		returns negative error code,
	 *			zero for no match/success to add/delete
	 *			positive for matching element */
	int (*uadt)(struct ip_set *set, struct nlattr *tb[],
		    enum ipset_adt adt, u32 *lineno, u32 flags, bool retried);

	/* Low level add/del/test functions */
	ipset_adtfn adt[IPSET_ADT_MAX];

	/* When adding entries and set is full, try to resize the set */
	int (*resize)(struct ip_set *set, bool retried);
	/* Destroy the set */
	void (*destroy)(struct ip_set *set);
	/* Flush the elements */
	void (*flush)(struct ip_set *set);
	/* Expire entries before listing */
	void (*expire)(struct ip_set *set);
	/* List set header data */
	int (*head)(struct ip_set *set, struct sk_buff *skb);
	/* List elements */
	int (*list)(const struct ip_set *set, struct sk_buff *skb,
		    struct netlink_callback *cb);
	/* Keep listing private when resizing runs parallel */
	void (*uref)(struct ip_set *set, struct netlink_callback *cb,
		     bool start);

	/* Return true if "b" set is the same as "a"
	 * according to the create set parameters */
	bool (*same_set)(const struct ip_set *a, const struct ip_set *b);
	/* Region-locking is used */
	bool region_lock;
};

struct ip_set_region {
	spinlock_t lock;	/* Region lock */
	size_t ext_size;	/* Size of the dynamic extensions */
	u32 elements;		/* Number of elements vs timeout */
};

/* Max range where every element is added/deleted in one step */
#define IPSET_MAX_RANGE		(1<<20)

/* The max revision number supported by any set type + 1 */
#define IPSET_REVISION_MAX	9

/* The core set type structure */
struct ip_set_type {
	struct list_head list;

	/* Typename */
	char name[IPSET_MAXNAMELEN];
	/* Protocol version */
	u8 protocol;
	/* Set type dimension */
	u8 dimension;
	/*
	 * Supported family: may be NFPROTO_UNSPEC for both
	 * NFPROTO_IPV4/NFPROTO_IPV6.
	 */
	u8 family;
	/* Type revisions */
	u8 revision_min, revision_max;
	/* Revision-specific supported (create) flags */
	u8 create_flags[IPSET_REVISION_MAX+1];
	/* Set features to control swapping */
	u16 features;

	/* Create set */
	int (*create)(struct net *net, struct ip_set *set,
		      struct nlattr *tb[], u32 flags);

	/* Attribute policies */
	const struct nla_policy create_policy[IPSET_ATTR_CREATE_MAX + 1];
	const struct nla_policy adt_policy[IPSET_ATTR_ADT_MAX + 1];

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

/* register and unregister set type */
extern int ip_set_type_register(struct ip_set_type *set_type);
extern void ip_set_type_unregister(struct ip_set_type *set_type);

/* A generic IP set */
struct ip_set {
	/* The name of the set */
	char name[IPSET_MAXNAMELEN];
	/* Lock protecting the set data */
	spinlock_t lock;
	/* References to the set */
	u32 ref;
	/* References to the set for netlink events like dump,
	 * ref can be swapped out by ip_set_swap
	 */
	u32 ref_netlink;
	/* The core set type */
	struct ip_set_type *type;
	/* The type variant doing the real job */
	const struct ip_set_type_variant *variant;
	/* The actual INET family of the set */
	u8 family;
	/* The type revision */
	u8 revision;
	/* Extensions */
	u8 extensions;
	/* Create flags */
	u8 flags;
	/* Default timeout value, if enabled */
	u32 timeout;
	/* Number of elements (vs timeout) */
	u32 elements;
	/* Size of the dynamic extensions (vs timeout) */
	size_t ext_size;
	/* Element data size */
	size_t dsize;
	/* Offsets to extensions in elements */
	size_t offset[IPSET_EXT_ID_MAX];
	/* The type specific data */
	void *data;
};

static inline void
ip_set_ext_destroy(struct ip_set *set, void *data)
{
	/* Check that the extension is enabled for the set and
	 * call it's destroy function for its extension part in data.
	 */
	if (SET_WITH_COMMENT(set)) {
		struct ip_set_comment *c = ext_comment(data, set);

		ip_set_extensions[IPSET_EXT_ID_COMMENT].destroy(set, c);
	}
}

int ip_set_put_flags(struct sk_buff *skb, struct ip_set *set);

/* Netlink CB args */
enum {
	IPSET_CB_NET = 0,	/* net namespace */
	IPSET_CB_PROTO,		/* ipset protocol */
	IPSET_CB_DUMP,		/* dump single set/all sets */
	IPSET_CB_INDEX,		/* set index */
	IPSET_CB_PRIVATE,	/* set private data */
	IPSET_CB_ARG0,		/* type specific */
};

/* register and unregister set references */
extern ip_set_id_t ip_set_get_byname(struct net *net,
				     const char *name, struct ip_set **set);
extern void ip_set_put_byindex(struct net *net, ip_set_id_t index);
extern void ip_set_name_byindex(struct net *net, ip_set_id_t index, char *name);
extern ip_set_id_t ip_set_nfnl_get_byindex(struct net *net, ip_set_id_t index);
extern void ip_set_nfnl_put(struct net *net, ip_set_id_t index);

/* API for iptables set match, and SET target */

extern int ip_set_add(ip_set_id_t id, const struct sk_buff *skb,
		      const struct xt_action_param *par,
		      struct ip_set_adt_opt *opt);
extern int ip_set_del(ip_set_id_t id, const struct sk_buff *skb,
		      const struct xt_action_param *par,
		      struct ip_set_adt_opt *opt);
extern int ip_set_test(ip_set_id_t id, const struct sk_buff *skb,
		       const struct xt_action_param *par,
		       struct ip_set_adt_opt *opt);

/* Utility functions */
extern void *ip_set_alloc(size_t size);
extern void ip_set_free(void *members);
extern int ip_set_get_ipaddr4(struct nlattr *nla,  __be32 *ipaddr);
extern int ip_set_get_ipaddr6(struct nlattr *nla, union nf_inet_addr *ipaddr);
extern size_t ip_set_elem_len(struct ip_set *set, struct nlattr *tb[],
			      size_t len, size_t align);
extern int ip_set_get_extensions(struct ip_set *set, struct nlattr *tb[],
				 struct ip_set_ext *ext);
extern int ip_set_put_extensions(struct sk_buff *skb, const struct ip_set *set,
				 const void *e, bool active);
extern bool ip_set_match_extensions(struct ip_set *set,
				    const struct ip_set_ext *ext,
				    struct ip_set_ext *mext,
				    u32 flags, void *data);

static inline int
ip_set_get_hostipaddr4(struct nlattr *nla, u32 *ipaddr)
{
	__be32 ip;
	int ret = ip_set_get_ipaddr4(nla, &ip);

	if (ret)
		return ret;
	*ipaddr = ntohl(ip);
	return 0;
}

/* Ignore IPSET_ERR_EXIST errors if asked to do so? */
static inline bool
ip_set_eexist(int ret, u32 flags)
{
	return ret == -IPSET_ERR_EXIST && (flags & IPSET_FLAG_EXIST);
}

/* Match elements marked with nomatch */
static inline bool
ip_set_enomatch(int ret, u32 flags, enum ipset_adt adt, struct ip_set *set)
{
	return adt == IPSET_TEST &&
	       (set->type->features & IPSET_TYPE_NOMATCH) &&
	       ((flags >> 16) & IPSET_FLAG_NOMATCH) &&
	       (ret > 0 || ret == -ENOTEMPTY);
}

/* Check the NLA_F_NET_BYTEORDER flag */
static inline bool
ip_set_attr_netorder(struct nlattr *tb[], int type)
{
	return tb[type] && (tb[type]->nla_type & NLA_F_NET_BYTEORDER);
}

static inline bool
ip_set_optattr_netorder(struct nlattr *tb[], int type)
{
	return !tb[type] || (tb[type]->nla_type & NLA_F_NET_BYTEORDER);
}

/* Useful converters */
static inline u32
ip_set_get_h32(const struct nlattr *attr)
{
	return ntohl(nla_get_be32(attr));
}

static inline u16
ip_set_get_h16(const struct nlattr *attr)
{
	return ntohs(nla_get_be16(attr));
}

static inline int nla_put_ipaddr4(struct sk_buff *skb, int type, __be32 ipaddr)
{
	struct nlattr *__nested = nla_nest_start(skb, type);
	int ret;

	if (!__nested)
		return -EMSGSIZE;
	ret = nla_put_in_addr(skb, IPSET_ATTR_IPADDR_IPV4, ipaddr);
	if (!ret)
		nla_nest_end(skb, __nested);
	return ret;
}

static inline int nla_put_ipaddr6(struct sk_buff *skb, int type,
				  const struct in6_addr *ipaddrptr)
{
	struct nlattr *__nested = nla_nest_start(skb, type);
	int ret;

	if (!__nested)
		return -EMSGSIZE;
	ret = nla_put_in6_addr(skb, IPSET_ATTR_IPADDR_IPV6, ipaddrptr);
	if (!ret)
		nla_nest_end(skb, __nested);
	return ret;
}

/* Get address from skbuff */
static inline __be32
ip4addr(const struct sk_buff *skb, bool src)
{
	return src ? ip_hdr(skb)->saddr : ip_hdr(skb)->daddr;
}

static inline void
ip4addrptr(const struct sk_buff *skb, bool src, __be32 *addr)
{
	*addr = src ? ip_hdr(skb)->saddr : ip_hdr(skb)->daddr;
}

static inline void
ip6addrptr(const struct sk_buff *skb, bool src, struct in6_addr *addr)
{
	memcpy(addr, src ? &ipv6_hdr(skb)->saddr : &ipv6_hdr(skb)->daddr,
	       sizeof(*addr));
}

/* How often should the gc be run by default */
#define IPSET_GC_TIME			(3 * 60)

/* Timeout period depending on the timeout value of the given set */
#define IPSET_GC_PERIOD(timeout) \
	((timeout/3) ? min_t(u32, (timeout)/3, IPSET_GC_TIME) : 1)

/* Entry is set with no timeout value */
#define IPSET_ELEM_PERMANENT	0

/* Set is defined with timeout support: timeout value may be 0 */
#define IPSET_NO_TIMEOUT	UINT_MAX

/* Max timeout value, see msecs_to_jiffies() in jiffies.h */
#define IPSET_MAX_TIMEOUT	(UINT_MAX >> 1)/MSEC_PER_SEC

#define ip_set_adt_opt_timeout(opt, set)	\
((opt)->ext.timeout != IPSET_NO_TIMEOUT ? (opt)->ext.timeout : (set)->timeout)

static inline unsigned int
ip_set_timeout_uget(struct nlattr *tb)
{
	unsigned int timeout = ip_set_get_h32(tb);

	/* Normalize to fit into jiffies */
	if (timeout > IPSET_MAX_TIMEOUT)
		timeout = IPSET_MAX_TIMEOUT;

	return timeout;
}

static inline bool
ip_set_timeout_expired(const unsigned long *t)
{
	return *t != IPSET_ELEM_PERMANENT && time_is_before_jiffies(*t);
}

static inline void
ip_set_timeout_set(unsigned long *timeout, u32 value)
{
	unsigned long t;

	if (!value) {
		*timeout = IPSET_ELEM_PERMANENT;
		return;
	}

	t = msecs_to_jiffies(value * MSEC_PER_SEC) + jiffies;
	if (t == IPSET_ELEM_PERMANENT)
		/* Bingo! :-) */
		t--;
	*timeout = t;
}

void ip_set_init_comment(struct ip_set *set, struct ip_set_comment *comment,
			 const struct ip_set_ext *ext);

static inline void
ip_set_init_counter(struct ip_set_counter *counter,
		    const struct ip_set_ext *ext)
{
	if (ext->bytes != ULLONG_MAX)
		atomic64_set(&(counter)->bytes, (long long)(ext->bytes));
	if (ext->packets != ULLONG_MAX)
		atomic64_set(&(counter)->packets, (long long)(ext->packets));
}

static inline void
ip_set_init_skbinfo(struct ip_set_skbinfo *skbinfo,
		    const struct ip_set_ext *ext)
{
	*skbinfo = ext->skbinfo;
}

#define IP_SET_INIT_KEXT(skb, opt, set)			\
	{ .bytes = (skb)->len, .packets = 1, .target = true,\
	  .timeout = ip_set_adt_opt_timeout(opt, set) }

#define IP_SET_INIT_UEXT(set)				\
	{ .bytes = ULLONG_MAX, .packets = ULLONG_MAX,	\
	  .timeout = (set)->timeout }

#define IPSET_CONCAT(a, b)		a##b
#define IPSET_TOKEN(a, b)		IPSET_CONCAT(a, b)

#endif /*_IP_SET_H */
