#ifndef _IP_SET_H
#define _IP_SET_H

/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 *                         Martin Josefsson <gandalf@wlug.westbo.se>
 * Copyright (C) 2003-2011 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* The protocol version */
#define IPSET_PROTOCOL		6

/* The max length of strings including NUL: set and type identifiers */
#define IPSET_MAXNAMELEN	32

/* Message types and commands */
enum ipset_cmd {
	IPSET_CMD_NONE,
	IPSET_CMD_PROTOCOL,	/* 1: Return protocol version */
	IPSET_CMD_CREATE,	/* 2: Create a new (empty) set */
	IPSET_CMD_DESTROY,	/* 3: Destroy a (empty) set */
	IPSET_CMD_FLUSH,	/* 4: Remove all elements from a set */
	IPSET_CMD_RENAME,	/* 5: Rename a set */
	IPSET_CMD_SWAP,		/* 6: Swap two sets */
	IPSET_CMD_LIST,		/* 7: List sets */
	IPSET_CMD_SAVE,		/* 8: Save sets */
	IPSET_CMD_ADD,		/* 9: Add an element to a set */
	IPSET_CMD_DEL,		/* 10: Delete an element from a set */
	IPSET_CMD_TEST,		/* 11: Test an element in a set */
	IPSET_CMD_HEADER,	/* 12: Get set header data only */
	IPSET_CMD_TYPE,		/* 13: Get set type */
	IPSET_MSG_MAX,		/* Netlink message commands */

	/* Commands in userspace: */
	IPSET_CMD_RESTORE = IPSET_MSG_MAX, /* 14: Enter restore mode */
	IPSET_CMD_HELP,		/* 15: Get help */
	IPSET_CMD_VERSION,	/* 16: Get program version */
	IPSET_CMD_QUIT,		/* 17: Quit from interactive mode */

	IPSET_CMD_MAX,

	IPSET_CMD_COMMIT = IPSET_CMD_MAX, /* 18: Commit buffered commands */
};

/* Attributes at command level */
enum {
	IPSET_ATTR_UNSPEC,
	IPSET_ATTR_PROTOCOL,	/* 1: Protocol version */
	IPSET_ATTR_SETNAME,	/* 2: Name of the set */
	IPSET_ATTR_TYPENAME,	/* 3: Typename */
	IPSET_ATTR_SETNAME2 = IPSET_ATTR_TYPENAME, /* Setname at rename/swap */
	IPSET_ATTR_REVISION,	/* 4: Settype revision */
	IPSET_ATTR_FAMILY,	/* 5: Settype family */
	IPSET_ATTR_FLAGS,	/* 6: Flags at command level */
	IPSET_ATTR_DATA,	/* 7: Nested attributes */
	IPSET_ATTR_ADT,		/* 8: Multiple data containers */
	IPSET_ATTR_LINENO,	/* 9: Restore lineno */
	IPSET_ATTR_PROTOCOL_MIN, /* 10: Minimal supported version number */
	IPSET_ATTR_REVISION_MIN	= IPSET_ATTR_PROTOCOL_MIN, /* type rev min */
	__IPSET_ATTR_CMD_MAX,
};
#define IPSET_ATTR_CMD_MAX	(__IPSET_ATTR_CMD_MAX - 1)

/* CADT specific attributes */
enum {
	IPSET_ATTR_IP = IPSET_ATTR_UNSPEC + 1,
	IPSET_ATTR_IP_FROM = IPSET_ATTR_IP,
	IPSET_ATTR_IP_TO,	/* 2 */
	IPSET_ATTR_CIDR,	/* 3 */
	IPSET_ATTR_PORT,	/* 4 */
	IPSET_ATTR_PORT_FROM = IPSET_ATTR_PORT,
	IPSET_ATTR_PORT_TO,	/* 5 */
	IPSET_ATTR_TIMEOUT,	/* 6 */
	IPSET_ATTR_PROTO,	/* 7 */
	IPSET_ATTR_CADT_FLAGS,	/* 8 */
	IPSET_ATTR_CADT_LINENO = IPSET_ATTR_LINENO,	/* 9 */
	/* Reserve empty slots */
	IPSET_ATTR_CADT_MAX = 16,
	/* Create-only specific attributes */
	IPSET_ATTR_GC,
	IPSET_ATTR_HASHSIZE,
	IPSET_ATTR_MAXELEM,
	IPSET_ATTR_NETMASK,
	IPSET_ATTR_PROBES,
	IPSET_ATTR_RESIZE,
	IPSET_ATTR_SIZE,
	/* Kernel-only */
	IPSET_ATTR_ELEMENTS,
	IPSET_ATTR_REFERENCES,
	IPSET_ATTR_MEMSIZE,

	__IPSET_ATTR_CREATE_MAX,
};
#define IPSET_ATTR_CREATE_MAX	(__IPSET_ATTR_CREATE_MAX - 1)

/* ADT specific attributes */
enum {
	IPSET_ATTR_ETHER = IPSET_ATTR_CADT_MAX + 1,
	IPSET_ATTR_NAME,
	IPSET_ATTR_NAMEREF,
	IPSET_ATTR_IP2,
	IPSET_ATTR_CIDR2,
	__IPSET_ATTR_ADT_MAX,
};
#define IPSET_ATTR_ADT_MAX	(__IPSET_ATTR_ADT_MAX - 1)

/* IP specific attributes */
enum {
	IPSET_ATTR_IPADDR_IPV4 = IPSET_ATTR_UNSPEC + 1,
	IPSET_ATTR_IPADDR_IPV6,
	__IPSET_ATTR_IPADDR_MAX,
};
#define IPSET_ATTR_IPADDR_MAX	(__IPSET_ATTR_IPADDR_MAX - 1)

/* Error codes */
enum ipset_errno {
	IPSET_ERR_PRIVATE = 4096,
	IPSET_ERR_PROTOCOL,
	IPSET_ERR_FIND_TYPE,
	IPSET_ERR_MAX_SETS,
	IPSET_ERR_BUSY,
	IPSET_ERR_EXIST_SETNAME2,
	IPSET_ERR_TYPE_MISMATCH,
	IPSET_ERR_EXIST,
	IPSET_ERR_INVALID_CIDR,
	IPSET_ERR_INVALID_NETMASK,
	IPSET_ERR_INVALID_FAMILY,
	IPSET_ERR_TIMEOUT,
	IPSET_ERR_REFERENCED,
	IPSET_ERR_IPADDR_IPV4,
	IPSET_ERR_IPADDR_IPV6,

	/* Type specific error codes */
	IPSET_ERR_TYPE_SPECIFIC = 4352,
};

/* Flags at command level */
enum ipset_cmd_flags {
	IPSET_FLAG_BIT_EXIST	= 0,
	IPSET_FLAG_EXIST	= (1 << IPSET_FLAG_BIT_EXIST),
	IPSET_FLAG_BIT_LIST_SETNAME = 1,
	IPSET_FLAG_LIST_SETNAME	= (1 << IPSET_FLAG_BIT_LIST_SETNAME),
	IPSET_FLAG_BIT_LIST_HEADER = 2,
	IPSET_FLAG_LIST_HEADER	= (1 << IPSET_FLAG_BIT_LIST_HEADER),
};

/* Flags at CADT attribute level */
enum ipset_cadt_flags {
	IPSET_FLAG_BIT_BEFORE	= 0,
	IPSET_FLAG_BEFORE	= (1 << IPSET_FLAG_BIT_BEFORE),
};

/* Commands with settype-specific attributes */
enum ipset_adt {
	IPSET_ADD,
	IPSET_DEL,
	IPSET_TEST,
	IPSET_ADT_MAX,
	IPSET_CREATE = IPSET_ADT_MAX,
	IPSET_CADT_MAX,
};

#ifdef __KERNEL__
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/vmalloc.h>
#include <net/netlink.h>

/* Sets are identified by an index in kernel space. Tweak with ip_set_id_t
 * and IPSET_INVALID_ID if you want to increase the max number of sets.
 */
typedef u16 ip_set_id_t;

#define IPSET_INVALID_ID		65535

enum ip_set_dim {
	IPSET_DIM_ZERO = 0,
	IPSET_DIM_ONE,
	IPSET_DIM_TWO,
	IPSET_DIM_THREE,
	/* Max dimension in elements.
	 * If changed, new revision of iptables match/target is required.
	 */
	IPSET_DIM_MAX = 6,
};

/* Option flags for kernel operations */
enum ip_set_kopt {
	IPSET_INV_MATCH = (1 << IPSET_DIM_ZERO),
	IPSET_DIM_ONE_SRC = (1 << IPSET_DIM_ONE),
	IPSET_DIM_TWO_SRC = (1 << IPSET_DIM_TWO),
	IPSET_DIM_THREE_SRC = (1 << IPSET_DIM_THREE),
};

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
	/* Strictly speaking not a feature, but a flag for dumping:
	 * this settype must be dumped last */
	IPSET_DUMP_LAST_FLAG = 7,
	IPSET_DUMP_LAST = (1 << IPSET_DUMP_LAST_FLAG),
};

struct ip_set;

typedef int (*ipset_adtfn)(struct ip_set *set, void *value,
			   u32 timeout, u32 flags);

/* Kernel API function options */
struct ip_set_adt_opt {
	u8 family;		/* Actual protocol family */
	u8 dim;			/* Dimension of match/target */
	u8 flags;		/* Direction and negation flags */
	u32 cmdflags;		/* Command-like flags */
	u32 timeout;		/* Timeout value */
};

/* Set type, variant-specific part */
struct ip_set_type_variant {
	/* Kernelspace: test/add/del entries
	 *		returns negative error code,
	 *			zero for no match/success to add/delete
	 *			positive for matching element */
	int (*kadt)(struct ip_set *set, const struct sk_buff * skb,
		    enum ipset_adt adt, const struct ip_set_adt_opt *opt);

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

	/* Return true if "b" set is the same as "a"
	 * according to the create set parameters */
	bool (*same_set)(const struct ip_set *a, const struct ip_set *b);
};

/* The core set type structure */
struct ip_set_type {
	struct list_head list;

	/* Typename */
	char name[IPSET_MAXNAMELEN];
	/* Protocol version */
	u8 protocol;
	/* Set features to control swapping */
	u8 features;
	/* Set type dimension */
	u8 dimension;
	/* Supported family: may be AF_UNSPEC for both AF_INET/AF_INET6 */
	u8 family;
	/* Type revision */
	u8 revision;

	/* Create set */
	int (*create)(struct ip_set *set, struct nlattr *tb[], u32 flags);

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
	rwlock_t lock;
	/* References to the set */
	u32 ref;
	/* The core set type */
	struct ip_set_type *type;
	/* The type variant doing the real job */
	const struct ip_set_type_variant *variant;
	/* The actual INET family of the set */
	u8 family;
	/* The type specific data */
	void *data;
};

/* register and unregister set references */
extern ip_set_id_t ip_set_get_byname(const char *name, struct ip_set **set);
extern void ip_set_put_byindex(ip_set_id_t index);
extern const char * ip_set_name_byindex(ip_set_id_t index);
extern ip_set_id_t ip_set_nfnl_get(const char *name);
extern ip_set_id_t ip_set_nfnl_get_byindex(ip_set_id_t index);
extern void ip_set_nfnl_put(ip_set_id_t index);

/* API for iptables set match, and SET target */

extern int ip_set_add(ip_set_id_t id, const struct sk_buff *skb,
		      const struct ip_set_adt_opt *opt);
extern int ip_set_del(ip_set_id_t id, const struct sk_buff *skb,
		      const struct ip_set_adt_opt *opt);
extern int ip_set_test(ip_set_id_t id, const struct sk_buff *skb,
		       const struct ip_set_adt_opt *opt);

/* Utility functions */
extern void * ip_set_alloc(size_t size);
extern void ip_set_free(void *members);
extern int ip_set_get_ipaddr4(struct nlattr *nla,  __be32 *ipaddr);
extern int ip_set_get_ipaddr6(struct nlattr *nla, union nf_inet_addr *ipaddr);

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

#define ipset_nest_start(skb, attr) nla_nest_start(skb, attr | NLA_F_NESTED)
#define ipset_nest_end(skb, start)  nla_nest_end(skb, start)

#define NLA_PUT_IPADDR4(skb, type, ipaddr)			\
do {								\
	struct nlattr *__nested = ipset_nest_start(skb, type);	\
								\
	if (!__nested)						\
		goto nla_put_failure;				\
	NLA_PUT_NET32(skb, IPSET_ATTR_IPADDR_IPV4, ipaddr);	\
	ipset_nest_end(skb, __nested);				\
} while (0)

#define NLA_PUT_IPADDR6(skb, type, ipaddrptr)			\
do {								\
	struct nlattr *__nested = ipset_nest_start(skb, type);	\
								\
	if (!__nested)						\
		goto nla_put_failure;				\
	NLA_PUT(skb, IPSET_ATTR_IPADDR_IPV6,			\
		sizeof(struct in6_addr), ipaddrptr);		\
	ipset_nest_end(skb, __nested);				\
} while (0)

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

/* Calculate the bytes required to store the inclusive range of a-b */
static inline int
bitmap_bytes(u32 a, u32 b)
{
	return 4 * ((((b - a + 8) / 8) + 3) / 4);
}

/* Interface to iptables/ip6tables */

#define SO_IP_SET		83

union ip_set_name_index {
	char name[IPSET_MAXNAMELEN];
	ip_set_id_t index;
};

#define IP_SET_OP_GET_BYNAME	0x00000006	/* Get set index by name */
struct ip_set_req_get_set {
	unsigned op;
	unsigned version;
	union ip_set_name_index set;
};

#define IP_SET_OP_GET_BYINDEX	0x00000007	/* Get set name by index */
/* Uses ip_set_req_get_set */

#define IP_SET_OP_VERSION	0x00000100	/* Ask kernel version */
struct ip_set_req_version {
	unsigned op;
	unsigned version;
};

#endif	/* __KERNEL__ */

#endif /*_IP_SET_H */
