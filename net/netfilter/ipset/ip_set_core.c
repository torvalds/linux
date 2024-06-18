// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 * Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@netfilter.org>
 */

/* Kernel module for IP set management */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/rculist.h>
#include <net/netlink.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/ipset/ip_set.h>

static LIST_HEAD(ip_set_type_list);		/* all registered set types */
static DEFINE_MUTEX(ip_set_type_mutex);		/* protects ip_set_type_list */
static DEFINE_RWLOCK(ip_set_ref_lock);		/* protects the set refs */

struct ip_set_net {
	struct ip_set * __rcu *ip_set_list;	/* all individual sets */
	ip_set_id_t	ip_set_max;	/* max number of sets */
	bool		is_deleted;	/* deleted by ip_set_net_exit */
	bool		is_destroyed;	/* all sets are destroyed */
};

static unsigned int ip_set_net_id __read_mostly;

static struct ip_set_net *ip_set_pernet(struct net *net)
{
	return net_generic(net, ip_set_net_id);
}

#define IP_SET_INC	64
#define STRNCMP(a, b)	(strncmp(a, b, IPSET_MAXNAMELEN) == 0)

static unsigned int max_sets;

module_param(max_sets, int, 0600);
MODULE_PARM_DESC(max_sets, "maximal number of sets");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@netfilter.org>");
MODULE_DESCRIPTION("core IP set support");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_IPSET);

/* When the nfnl mutex or ip_set_ref_lock is held: */
#define ip_set_dereference(p)		\
	rcu_dereference_protected(p,	\
		lockdep_nfnl_is_held(NFNL_SUBSYS_IPSET) || \
		lockdep_is_held(&ip_set_ref_lock))
#define ip_set(inst, id)		\
	ip_set_dereference((inst)->ip_set_list)[id]
#define ip_set_ref_netlink(inst,id)	\
	rcu_dereference_raw((inst)->ip_set_list)[id]
#define ip_set_dereference_nfnl(p)	\
	rcu_dereference_check(p, lockdep_nfnl_is_held(NFNL_SUBSYS_IPSET))

/* The set types are implemented in modules and registered set types
 * can be found in ip_set_type_list. Adding/deleting types is
 * serialized by ip_set_type_mutex.
 */

static void
ip_set_type_lock(void)
{
	mutex_lock(&ip_set_type_mutex);
}

static void
ip_set_type_unlock(void)
{
	mutex_unlock(&ip_set_type_mutex);
}

/* Register and deregister settype */

static struct ip_set_type *
find_set_type(const char *name, u8 family, u8 revision)
{
	struct ip_set_type *type;

	list_for_each_entry_rcu(type, &ip_set_type_list, list,
				lockdep_is_held(&ip_set_type_mutex))
		if (STRNCMP(type->name, name) &&
		    (type->family == family ||
		     type->family == NFPROTO_UNSPEC) &&
		    revision >= type->revision_min &&
		    revision <= type->revision_max)
			return type;
	return NULL;
}

/* Unlock, try to load a set type module and lock again */
static bool
load_settype(const char *name)
{
	nfnl_unlock(NFNL_SUBSYS_IPSET);
	pr_debug("try to load ip_set_%s\n", name);
	if (request_module("ip_set_%s", name) < 0) {
		pr_warn("Can't find ip_set type %s\n", name);
		nfnl_lock(NFNL_SUBSYS_IPSET);
		return false;
	}
	nfnl_lock(NFNL_SUBSYS_IPSET);
	return true;
}

/* Find a set type and reference it */
#define find_set_type_get(name, family, revision, found)	\
	__find_set_type_get(name, family, revision, found, false)

static int
__find_set_type_get(const char *name, u8 family, u8 revision,
		    struct ip_set_type **found, bool retry)
{
	struct ip_set_type *type;
	int err;

	if (retry && !load_settype(name))
		return -IPSET_ERR_FIND_TYPE;

	rcu_read_lock();
	*found = find_set_type(name, family, revision);
	if (*found) {
		err = !try_module_get((*found)->me) ? -EFAULT : 0;
		goto unlock;
	}
	/* Make sure the type is already loaded
	 * but we don't support the revision
	 */
	list_for_each_entry_rcu(type, &ip_set_type_list, list)
		if (STRNCMP(type->name, name)) {
			err = -IPSET_ERR_FIND_TYPE;
			goto unlock;
		}
	rcu_read_unlock();

	return retry ? -IPSET_ERR_FIND_TYPE :
		__find_set_type_get(name, family, revision, found, true);

unlock:
	rcu_read_unlock();
	return err;
}

/* Find a given set type by name and family.
 * If we succeeded, the supported minimal and maximum revisions are
 * filled out.
 */
#define find_set_type_minmax(name, family, min, max) \
	__find_set_type_minmax(name, family, min, max, false)

static int
__find_set_type_minmax(const char *name, u8 family, u8 *min, u8 *max,
		       bool retry)
{
	struct ip_set_type *type;
	bool found = false;

	if (retry && !load_settype(name))
		return -IPSET_ERR_FIND_TYPE;

	*min = 255; *max = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(type, &ip_set_type_list, list)
		if (STRNCMP(type->name, name) &&
		    (type->family == family ||
		     type->family == NFPROTO_UNSPEC)) {
			found = true;
			if (type->revision_min < *min)
				*min = type->revision_min;
			if (type->revision_max > *max)
				*max = type->revision_max;
		}
	rcu_read_unlock();
	if (found)
		return 0;

	return retry ? -IPSET_ERR_FIND_TYPE :
		__find_set_type_minmax(name, family, min, max, true);
}

#define family_name(f)	((f) == NFPROTO_IPV4 ? "inet" : \
			 (f) == NFPROTO_IPV6 ? "inet6" : "any")

/* Register a set type structure. The type is identified by
 * the unique triple of name, family and revision.
 */
int
ip_set_type_register(struct ip_set_type *type)
{
	int ret = 0;

	if (type->protocol != IPSET_PROTOCOL) {
		pr_warn("ip_set type %s, family %s, revision %u:%u uses wrong protocol version %u (want %u)\n",
			type->name, family_name(type->family),
			type->revision_min, type->revision_max,
			type->protocol, IPSET_PROTOCOL);
		return -EINVAL;
	}

	ip_set_type_lock();
	if (find_set_type(type->name, type->family, type->revision_min)) {
		/* Duplicate! */
		pr_warn("ip_set type %s, family %s with revision min %u already registered!\n",
			type->name, family_name(type->family),
			type->revision_min);
		ip_set_type_unlock();
		return -EINVAL;
	}
	list_add_rcu(&type->list, &ip_set_type_list);
	pr_debug("type %s, family %s, revision %u:%u registered.\n",
		 type->name, family_name(type->family),
		 type->revision_min, type->revision_max);
	ip_set_type_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(ip_set_type_register);

/* Unregister a set type. There's a small race with ip_set_create */
void
ip_set_type_unregister(struct ip_set_type *type)
{
	ip_set_type_lock();
	if (!find_set_type(type->name, type->family, type->revision_min)) {
		pr_warn("ip_set type %s, family %s with revision min %u not registered\n",
			type->name, family_name(type->family),
			type->revision_min);
		ip_set_type_unlock();
		return;
	}
	list_del_rcu(&type->list);
	pr_debug("type %s, family %s with revision min %u unregistered.\n",
		 type->name, family_name(type->family), type->revision_min);
	ip_set_type_unlock();

	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(ip_set_type_unregister);

/* Utility functions */
void *
ip_set_alloc(size_t size)
{
	return kvzalloc(size, GFP_KERNEL_ACCOUNT);
}
EXPORT_SYMBOL_GPL(ip_set_alloc);

void
ip_set_free(void *members)
{
	pr_debug("%p: free with %s\n", members,
		 is_vmalloc_addr(members) ? "vfree" : "kfree");
	kvfree(members);
}
EXPORT_SYMBOL_GPL(ip_set_free);

static bool
flag_nested(const struct nlattr *nla)
{
	return nla->nla_type & NLA_F_NESTED;
}

static const struct nla_policy ipaddr_policy[IPSET_ATTR_IPADDR_MAX + 1] = {
	[IPSET_ATTR_IPADDR_IPV4]	= { .type = NLA_U32 },
	[IPSET_ATTR_IPADDR_IPV6]	= NLA_POLICY_EXACT_LEN(sizeof(struct in6_addr)),
};

int
ip_set_get_ipaddr4(struct nlattr *nla,  __be32 *ipaddr)
{
	struct nlattr *tb[IPSET_ATTR_IPADDR_MAX + 1];

	if (unlikely(!flag_nested(nla)))
		return -IPSET_ERR_PROTOCOL;
	if (nla_parse_nested(tb, IPSET_ATTR_IPADDR_MAX, nla,
			     ipaddr_policy, NULL))
		return -IPSET_ERR_PROTOCOL;
	if (unlikely(!ip_set_attr_netorder(tb, IPSET_ATTR_IPADDR_IPV4)))
		return -IPSET_ERR_PROTOCOL;

	*ipaddr = nla_get_be32(tb[IPSET_ATTR_IPADDR_IPV4]);
	return 0;
}
EXPORT_SYMBOL_GPL(ip_set_get_ipaddr4);

int
ip_set_get_ipaddr6(struct nlattr *nla, union nf_inet_addr *ipaddr)
{
	struct nlattr *tb[IPSET_ATTR_IPADDR_MAX + 1];

	if (unlikely(!flag_nested(nla)))
		return -IPSET_ERR_PROTOCOL;

	if (nla_parse_nested(tb, IPSET_ATTR_IPADDR_MAX, nla,
			     ipaddr_policy, NULL))
		return -IPSET_ERR_PROTOCOL;
	if (unlikely(!ip_set_attr_netorder(tb, IPSET_ATTR_IPADDR_IPV6)))
		return -IPSET_ERR_PROTOCOL;

	memcpy(ipaddr, nla_data(tb[IPSET_ATTR_IPADDR_IPV6]),
	       sizeof(struct in6_addr));
	return 0;
}
EXPORT_SYMBOL_GPL(ip_set_get_ipaddr6);

static u32
ip_set_timeout_get(const unsigned long *timeout)
{
	u32 t;

	if (*timeout == IPSET_ELEM_PERMANENT)
		return 0;

	t = jiffies_to_msecs(*timeout - jiffies) / MSEC_PER_SEC;
	/* Zero value in userspace means no timeout */
	return t == 0 ? 1 : t;
}

static char *
ip_set_comment_uget(struct nlattr *tb)
{
	return nla_data(tb);
}

/* Called from uadd only, protected by the set spinlock.
 * The kadt functions don't use the comment extensions in any way.
 */
void
ip_set_init_comment(struct ip_set *set, struct ip_set_comment *comment,
		    const struct ip_set_ext *ext)
{
	struct ip_set_comment_rcu *c = rcu_dereference_protected(comment->c, 1);
	size_t len = ext->comment ? strlen(ext->comment) : 0;

	if (unlikely(c)) {
		set->ext_size -= sizeof(*c) + strlen(c->str) + 1;
		kfree_rcu(c, rcu);
		rcu_assign_pointer(comment->c, NULL);
	}
	if (!len)
		return;
	if (unlikely(len > IPSET_MAX_COMMENT_SIZE))
		len = IPSET_MAX_COMMENT_SIZE;
	c = kmalloc(sizeof(*c) + len + 1, GFP_ATOMIC);
	if (unlikely(!c))
		return;
	strscpy(c->str, ext->comment, len + 1);
	set->ext_size += sizeof(*c) + strlen(c->str) + 1;
	rcu_assign_pointer(comment->c, c);
}
EXPORT_SYMBOL_GPL(ip_set_init_comment);

/* Used only when dumping a set, protected by rcu_read_lock() */
static int
ip_set_put_comment(struct sk_buff *skb, const struct ip_set_comment *comment)
{
	struct ip_set_comment_rcu *c = rcu_dereference(comment->c);

	if (!c)
		return 0;
	return nla_put_string(skb, IPSET_ATTR_COMMENT, c->str);
}

/* Called from uadd/udel, flush or the garbage collectors protected
 * by the set spinlock.
 * Called when the set is destroyed and when there can't be any user
 * of the set data anymore.
 */
static void
ip_set_comment_free(struct ip_set *set, void *ptr)
{
	struct ip_set_comment *comment = ptr;
	struct ip_set_comment_rcu *c;

	c = rcu_dereference_protected(comment->c, 1);
	if (unlikely(!c))
		return;
	set->ext_size -= sizeof(*c) + strlen(c->str) + 1;
	kfree_rcu(c, rcu);
	rcu_assign_pointer(comment->c, NULL);
}

typedef void (*destroyer)(struct ip_set *, void *);
/* ipset data extension types, in size order */

const struct ip_set_ext_type ip_set_extensions[] = {
	[IPSET_EXT_ID_COUNTER] = {
		.type	= IPSET_EXT_COUNTER,
		.flag	= IPSET_FLAG_WITH_COUNTERS,
		.len	= sizeof(struct ip_set_counter),
		.align	= __alignof__(struct ip_set_counter),
	},
	[IPSET_EXT_ID_TIMEOUT] = {
		.type	= IPSET_EXT_TIMEOUT,
		.len	= sizeof(unsigned long),
		.align	= __alignof__(unsigned long),
	},
	[IPSET_EXT_ID_SKBINFO] = {
		.type	= IPSET_EXT_SKBINFO,
		.flag	= IPSET_FLAG_WITH_SKBINFO,
		.len	= sizeof(struct ip_set_skbinfo),
		.align	= __alignof__(struct ip_set_skbinfo),
	},
	[IPSET_EXT_ID_COMMENT] = {
		.type	 = IPSET_EXT_COMMENT | IPSET_EXT_DESTROY,
		.flag	 = IPSET_FLAG_WITH_COMMENT,
		.len	 = sizeof(struct ip_set_comment),
		.align	 = __alignof__(struct ip_set_comment),
		.destroy = ip_set_comment_free,
	},
};
EXPORT_SYMBOL_GPL(ip_set_extensions);

static bool
add_extension(enum ip_set_ext_id id, u32 flags, struct nlattr *tb[])
{
	return ip_set_extensions[id].flag ?
		(flags & ip_set_extensions[id].flag) :
		!!tb[IPSET_ATTR_TIMEOUT];
}

size_t
ip_set_elem_len(struct ip_set *set, struct nlattr *tb[], size_t len,
		size_t align)
{
	enum ip_set_ext_id id;
	u32 cadt_flags = 0;

	if (tb[IPSET_ATTR_CADT_FLAGS])
		cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);
	if (cadt_flags & IPSET_FLAG_WITH_FORCEADD)
		set->flags |= IPSET_CREATE_FLAG_FORCEADD;
	if (!align)
		align = 1;
	for (id = 0; id < IPSET_EXT_ID_MAX; id++) {
		if (!add_extension(id, cadt_flags, tb))
			continue;
		if (align < ip_set_extensions[id].align)
			align = ip_set_extensions[id].align;
		len = ALIGN(len, ip_set_extensions[id].align);
		set->offset[id] = len;
		set->extensions |= ip_set_extensions[id].type;
		len += ip_set_extensions[id].len;
	}
	return ALIGN(len, align);
}
EXPORT_SYMBOL_GPL(ip_set_elem_len);

int
ip_set_get_extensions(struct ip_set *set, struct nlattr *tb[],
		      struct ip_set_ext *ext)
{
	u64 fullmark;

	if (unlikely(!ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBMARK) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBPRIO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBQUEUE)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_TIMEOUT]) {
		if (!SET_WITH_TIMEOUT(set))
			return -IPSET_ERR_TIMEOUT;
		ext->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
	}
	if (tb[IPSET_ATTR_BYTES] || tb[IPSET_ATTR_PACKETS]) {
		if (!SET_WITH_COUNTER(set))
			return -IPSET_ERR_COUNTER;
		if (tb[IPSET_ATTR_BYTES])
			ext->bytes = be64_to_cpu(nla_get_be64(
						 tb[IPSET_ATTR_BYTES]));
		if (tb[IPSET_ATTR_PACKETS])
			ext->packets = be64_to_cpu(nla_get_be64(
						   tb[IPSET_ATTR_PACKETS]));
	}
	if (tb[IPSET_ATTR_COMMENT]) {
		if (!SET_WITH_COMMENT(set))
			return -IPSET_ERR_COMMENT;
		ext->comment = ip_set_comment_uget(tb[IPSET_ATTR_COMMENT]);
	}
	if (tb[IPSET_ATTR_SKBMARK]) {
		if (!SET_WITH_SKBINFO(set))
			return -IPSET_ERR_SKBINFO;
		fullmark = be64_to_cpu(nla_get_be64(tb[IPSET_ATTR_SKBMARK]));
		ext->skbinfo.skbmark = fullmark >> 32;
		ext->skbinfo.skbmarkmask = fullmark & 0xffffffff;
	}
	if (tb[IPSET_ATTR_SKBPRIO]) {
		if (!SET_WITH_SKBINFO(set))
			return -IPSET_ERR_SKBINFO;
		ext->skbinfo.skbprio =
			be32_to_cpu(nla_get_be32(tb[IPSET_ATTR_SKBPRIO]));
	}
	if (tb[IPSET_ATTR_SKBQUEUE]) {
		if (!SET_WITH_SKBINFO(set))
			return -IPSET_ERR_SKBINFO;
		ext->skbinfo.skbqueue =
			be16_to_cpu(nla_get_be16(tb[IPSET_ATTR_SKBQUEUE]));
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ip_set_get_extensions);

static u64
ip_set_get_bytes(const struct ip_set_counter *counter)
{
	return (u64)atomic64_read(&(counter)->bytes);
}

static u64
ip_set_get_packets(const struct ip_set_counter *counter)
{
	return (u64)atomic64_read(&(counter)->packets);
}

static bool
ip_set_put_counter(struct sk_buff *skb, const struct ip_set_counter *counter)
{
	return nla_put_net64(skb, IPSET_ATTR_BYTES,
			     cpu_to_be64(ip_set_get_bytes(counter)),
			     IPSET_ATTR_PAD) ||
	       nla_put_net64(skb, IPSET_ATTR_PACKETS,
			     cpu_to_be64(ip_set_get_packets(counter)),
			     IPSET_ATTR_PAD);
}

static bool
ip_set_put_skbinfo(struct sk_buff *skb, const struct ip_set_skbinfo *skbinfo)
{
	/* Send nonzero parameters only */
	return ((skbinfo->skbmark || skbinfo->skbmarkmask) &&
		nla_put_net64(skb, IPSET_ATTR_SKBMARK,
			      cpu_to_be64((u64)skbinfo->skbmark << 32 |
					  skbinfo->skbmarkmask),
			      IPSET_ATTR_PAD)) ||
	       (skbinfo->skbprio &&
		nla_put_net32(skb, IPSET_ATTR_SKBPRIO,
			      cpu_to_be32(skbinfo->skbprio))) ||
	       (skbinfo->skbqueue &&
		nla_put_net16(skb, IPSET_ATTR_SKBQUEUE,
			      cpu_to_be16(skbinfo->skbqueue)));
}

int
ip_set_put_extensions(struct sk_buff *skb, const struct ip_set *set,
		      const void *e, bool active)
{
	if (SET_WITH_TIMEOUT(set)) {
		unsigned long *timeout = ext_timeout(e, set);

		if (nla_put_net32(skb, IPSET_ATTR_TIMEOUT,
			htonl(active ? ip_set_timeout_get(timeout)
				: *timeout)))
			return -EMSGSIZE;
	}
	if (SET_WITH_COUNTER(set) &&
	    ip_set_put_counter(skb, ext_counter(e, set)))
		return -EMSGSIZE;
	if (SET_WITH_COMMENT(set) &&
	    ip_set_put_comment(skb, ext_comment(e, set)))
		return -EMSGSIZE;
	if (SET_WITH_SKBINFO(set) &&
	    ip_set_put_skbinfo(skb, ext_skbinfo(e, set)))
		return -EMSGSIZE;
	return 0;
}
EXPORT_SYMBOL_GPL(ip_set_put_extensions);

static bool
ip_set_match_counter(u64 counter, u64 match, u8 op)
{
	switch (op) {
	case IPSET_COUNTER_NONE:
		return true;
	case IPSET_COUNTER_EQ:
		return counter == match;
	case IPSET_COUNTER_NE:
		return counter != match;
	case IPSET_COUNTER_LT:
		return counter < match;
	case IPSET_COUNTER_GT:
		return counter > match;
	}
	return false;
}

static void
ip_set_add_bytes(u64 bytes, struct ip_set_counter *counter)
{
	atomic64_add((long long)bytes, &(counter)->bytes);
}

static void
ip_set_add_packets(u64 packets, struct ip_set_counter *counter)
{
	atomic64_add((long long)packets, &(counter)->packets);
}

static void
ip_set_update_counter(struct ip_set_counter *counter,
		      const struct ip_set_ext *ext, u32 flags)
{
	if (ext->packets != ULLONG_MAX &&
	    !(flags & IPSET_FLAG_SKIP_COUNTER_UPDATE)) {
		ip_set_add_bytes(ext->bytes, counter);
		ip_set_add_packets(ext->packets, counter);
	}
}

static void
ip_set_get_skbinfo(struct ip_set_skbinfo *skbinfo,
		   const struct ip_set_ext *ext,
		   struct ip_set_ext *mext, u32 flags)
{
	mext->skbinfo = *skbinfo;
}

bool
ip_set_match_extensions(struct ip_set *set, const struct ip_set_ext *ext,
			struct ip_set_ext *mext, u32 flags, void *data)
{
	if (SET_WITH_TIMEOUT(set) &&
	    ip_set_timeout_expired(ext_timeout(data, set)))
		return false;
	if (SET_WITH_COUNTER(set)) {
		struct ip_set_counter *counter = ext_counter(data, set);

		ip_set_update_counter(counter, ext, flags);

		if (flags & IPSET_FLAG_MATCH_COUNTERS &&
		    !(ip_set_match_counter(ip_set_get_packets(counter),
				mext->packets, mext->packets_op) &&
		      ip_set_match_counter(ip_set_get_bytes(counter),
				mext->bytes, mext->bytes_op)))
			return false;
	}
	if (SET_WITH_SKBINFO(set))
		ip_set_get_skbinfo(ext_skbinfo(data, set),
				   ext, mext, flags);
	return true;
}
EXPORT_SYMBOL_GPL(ip_set_match_extensions);

/* Creating/destroying/renaming/swapping affect the existence and
 * the properties of a set. All of these can be executed from userspace
 * only and serialized by the nfnl mutex indirectly from nfnetlink.
 *
 * Sets are identified by their index in ip_set_list and the index
 * is used by the external references (set/SET netfilter modules).
 *
 * The set behind an index may change by swapping only, from userspace.
 */

static void
__ip_set_get(struct ip_set *set)
{
	write_lock_bh(&ip_set_ref_lock);
	set->ref++;
	write_unlock_bh(&ip_set_ref_lock);
}

static void
__ip_set_put(struct ip_set *set)
{
	write_lock_bh(&ip_set_ref_lock);
	BUG_ON(set->ref == 0);
	set->ref--;
	write_unlock_bh(&ip_set_ref_lock);
}

/* set->ref can be swapped out by ip_set_swap, netlink events (like dump) need
 * a separate reference counter
 */
static void
__ip_set_get_netlink(struct ip_set *set)
{
	write_lock_bh(&ip_set_ref_lock);
	set->ref_netlink++;
	write_unlock_bh(&ip_set_ref_lock);
}

static void
__ip_set_put_netlink(struct ip_set *set)
{
	write_lock_bh(&ip_set_ref_lock);
	BUG_ON(set->ref_netlink == 0);
	set->ref_netlink--;
	write_unlock_bh(&ip_set_ref_lock);
}

/* Add, del and test set entries from kernel.
 *
 * The set behind the index must exist and must be referenced
 * so it can't be destroyed (or changed) under our foot.
 */

static struct ip_set *
ip_set_rcu_get(struct net *net, ip_set_id_t index)
{
	struct ip_set_net *inst = ip_set_pernet(net);

	/* ip_set_list and the set pointer need to be protected */
	return ip_set_dereference_nfnl(inst->ip_set_list)[index];
}

static inline void
ip_set_lock(struct ip_set *set)
{
	if (!set->variant->region_lock)
		spin_lock_bh(&set->lock);
}

static inline void
ip_set_unlock(struct ip_set *set)
{
	if (!set->variant->region_lock)
		spin_unlock_bh(&set->lock);
}

int
ip_set_test(ip_set_id_t index, const struct sk_buff *skb,
	    const struct xt_action_param *par, struct ip_set_adt_opt *opt)
{
	struct ip_set *set = ip_set_rcu_get(xt_net(par), index);
	int ret = 0;

	BUG_ON(!set);
	pr_debug("set %s, index %u\n", set->name, index);

	if (opt->dim < set->type->dimension ||
	    !(opt->family == set->family || set->family == NFPROTO_UNSPEC))
		return 0;

	ret = set->variant->kadt(set, skb, par, IPSET_TEST, opt);

	if (ret == -EAGAIN) {
		/* Type requests element to be completed */
		pr_debug("element must be completed, ADD is triggered\n");
		ip_set_lock(set);
		set->variant->kadt(set, skb, par, IPSET_ADD, opt);
		ip_set_unlock(set);
		ret = 1;
	} else {
		/* --return-nomatch: invert matched element */
		if ((opt->cmdflags & IPSET_FLAG_RETURN_NOMATCH) &&
		    (set->type->features & IPSET_TYPE_NOMATCH) &&
		    (ret > 0 || ret == -ENOTEMPTY))
			ret = -ret;
	}

	/* Convert error codes to nomatch */
	return (ret < 0 ? 0 : ret);
}
EXPORT_SYMBOL_GPL(ip_set_test);

int
ip_set_add(ip_set_id_t index, const struct sk_buff *skb,
	   const struct xt_action_param *par, struct ip_set_adt_opt *opt)
{
	struct ip_set *set = ip_set_rcu_get(xt_net(par), index);
	int ret;

	BUG_ON(!set);
	pr_debug("set %s, index %u\n", set->name, index);

	if (opt->dim < set->type->dimension ||
	    !(opt->family == set->family || set->family == NFPROTO_UNSPEC))
		return -IPSET_ERR_TYPE_MISMATCH;

	ip_set_lock(set);
	ret = set->variant->kadt(set, skb, par, IPSET_ADD, opt);
	ip_set_unlock(set);

	return ret;
}
EXPORT_SYMBOL_GPL(ip_set_add);

int
ip_set_del(ip_set_id_t index, const struct sk_buff *skb,
	   const struct xt_action_param *par, struct ip_set_adt_opt *opt)
{
	struct ip_set *set = ip_set_rcu_get(xt_net(par), index);
	int ret = 0;

	BUG_ON(!set);
	pr_debug("set %s, index %u\n", set->name, index);

	if (opt->dim < set->type->dimension ||
	    !(opt->family == set->family || set->family == NFPROTO_UNSPEC))
		return -IPSET_ERR_TYPE_MISMATCH;

	ip_set_lock(set);
	ret = set->variant->kadt(set, skb, par, IPSET_DEL, opt);
	ip_set_unlock(set);

	return ret;
}
EXPORT_SYMBOL_GPL(ip_set_del);

/* Find set by name, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet.
 *
 */
ip_set_id_t
ip_set_get_byname(struct net *net, const char *name, struct ip_set **set)
{
	ip_set_id_t i, index = IPSET_INVALID_ID;
	struct ip_set *s;
	struct ip_set_net *inst = ip_set_pernet(net);

	rcu_read_lock();
	for (i = 0; i < inst->ip_set_max; i++) {
		s = rcu_dereference(inst->ip_set_list)[i];
		if (s && STRNCMP(s->name, name)) {
			__ip_set_get(s);
			index = i;
			*set = s;
			break;
		}
	}
	rcu_read_unlock();

	return index;
}
EXPORT_SYMBOL_GPL(ip_set_get_byname);

/* If the given set pointer points to a valid set, decrement
 * reference count by 1. The caller shall not assume the index
 * to be valid, after calling this function.
 *
 */

static void
__ip_set_put_byindex(struct ip_set_net *inst, ip_set_id_t index)
{
	struct ip_set *set;

	rcu_read_lock();
	set = rcu_dereference(inst->ip_set_list)[index];
	if (set)
		__ip_set_put(set);
	rcu_read_unlock();
}

void
ip_set_put_byindex(struct net *net, ip_set_id_t index)
{
	struct ip_set_net *inst = ip_set_pernet(net);

	__ip_set_put_byindex(inst, index);
}
EXPORT_SYMBOL_GPL(ip_set_put_byindex);

/* Get the name of a set behind a set index.
 * Set itself is protected by RCU, but its name isn't: to protect against
 * renaming, grab ip_set_ref_lock as reader (see ip_set_rename()) and copy the
 * name.
 */
void
ip_set_name_byindex(struct net *net, ip_set_id_t index, char *name)
{
	struct ip_set *set = ip_set_rcu_get(net, index);

	BUG_ON(!set);

	read_lock_bh(&ip_set_ref_lock);
	strscpy_pad(name, set->name, IPSET_MAXNAMELEN);
	read_unlock_bh(&ip_set_ref_lock);
}
EXPORT_SYMBOL_GPL(ip_set_name_byindex);

/* Routines to call by external subsystems, which do not
 * call nfnl_lock for us.
 */

/* Find set by index, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet.
 *
 * The nfnl mutex is used in the function.
 */
ip_set_id_t
ip_set_nfnl_get_byindex(struct net *net, ip_set_id_t index)
{
	struct ip_set *set;
	struct ip_set_net *inst = ip_set_pernet(net);

	if (index >= inst->ip_set_max)
		return IPSET_INVALID_ID;

	nfnl_lock(NFNL_SUBSYS_IPSET);
	set = ip_set(inst, index);
	if (set)
		__ip_set_get(set);
	else
		index = IPSET_INVALID_ID;
	nfnl_unlock(NFNL_SUBSYS_IPSET);

	return index;
}
EXPORT_SYMBOL_GPL(ip_set_nfnl_get_byindex);

/* If the given set pointer points to a valid set, decrement
 * reference count by 1. The caller shall not assume the index
 * to be valid, after calling this function.
 *
 * The nfnl mutex is used in the function.
 */
void
ip_set_nfnl_put(struct net *net, ip_set_id_t index)
{
	struct ip_set *set;
	struct ip_set_net *inst = ip_set_pernet(net);

	nfnl_lock(NFNL_SUBSYS_IPSET);
	if (!inst->is_deleted) { /* already deleted from ip_set_net_exit() */
		set = ip_set(inst, index);
		if (set)
			__ip_set_put(set);
	}
	nfnl_unlock(NFNL_SUBSYS_IPSET);
}
EXPORT_SYMBOL_GPL(ip_set_nfnl_put);

/* Communication protocol with userspace over netlink.
 *
 * The commands are serialized by the nfnl mutex.
 */

static inline u8 protocol(const struct nlattr * const tb[])
{
	return nla_get_u8(tb[IPSET_ATTR_PROTOCOL]);
}

static inline bool
protocol_failed(const struct nlattr * const tb[])
{
	return !tb[IPSET_ATTR_PROTOCOL] || protocol(tb) != IPSET_PROTOCOL;
}

static inline bool
protocol_min_failed(const struct nlattr * const tb[])
{
	return !tb[IPSET_ATTR_PROTOCOL] || protocol(tb) < IPSET_PROTOCOL_MIN;
}

static inline u32
flag_exist(const struct nlmsghdr *nlh)
{
	return nlh->nlmsg_flags & NLM_F_EXCL ? 0 : IPSET_FLAG_EXIST;
}

static struct nlmsghdr *
start_msg(struct sk_buff *skb, u32 portid, u32 seq, unsigned int flags,
	  enum ipset_cmd cmd)
{
	return nfnl_msg_put(skb, portid, seq,
			    nfnl_msg_type(NFNL_SUBSYS_IPSET, cmd), flags,
			    NFPROTO_IPV4, NFNETLINK_V0, 0);
}

/* Create a set */

static const struct nla_policy ip_set_create_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_TYPENAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1},
	[IPSET_ATTR_REVISION]	= { .type = NLA_U8 },
	[IPSET_ATTR_FAMILY]	= { .type = NLA_U8 },
	[IPSET_ATTR_DATA]	= { .type = NLA_NESTED },
};

static struct ip_set *
find_set_and_id(struct ip_set_net *inst, const char *name, ip_set_id_t *id)
{
	struct ip_set *set = NULL;
	ip_set_id_t i;

	*id = IPSET_INVALID_ID;
	for (i = 0; i < inst->ip_set_max; i++) {
		set = ip_set(inst, i);
		if (set && STRNCMP(set->name, name)) {
			*id = i;
			break;
		}
	}
	return (*id == IPSET_INVALID_ID ? NULL : set);
}

static inline struct ip_set *
find_set(struct ip_set_net *inst, const char *name)
{
	ip_set_id_t id;

	return find_set_and_id(inst, name, &id);
}

static int
find_free_id(struct ip_set_net *inst, const char *name, ip_set_id_t *index,
	     struct ip_set **set)
{
	struct ip_set *s;
	ip_set_id_t i;

	*index = IPSET_INVALID_ID;
	for (i = 0;  i < inst->ip_set_max; i++) {
		s = ip_set(inst, i);
		if (!s) {
			if (*index == IPSET_INVALID_ID)
				*index = i;
		} else if (STRNCMP(name, s->name)) {
			/* Name clash */
			*set = s;
			return -EEXIST;
		}
	}
	if (*index == IPSET_INVALID_ID)
		/* No free slot remained */
		return -IPSET_ERR_MAX_SETS;
	return 0;
}

static int ip_set_none(struct sk_buff *skb, const struct nfnl_info *info,
		       const struct nlattr * const attr[])
{
	return -EOPNOTSUPP;
}

static int ip_set_create(struct sk_buff *skb, const struct nfnl_info *info,
			 const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct ip_set *set, *clash = NULL;
	ip_set_id_t index = IPSET_INVALID_ID;
	struct nlattr *tb[IPSET_ATTR_CREATE_MAX + 1] = {};
	const char *name, *typename;
	u8 family, revision;
	u32 flags = flag_exist(info->nlh);
	int ret = 0;

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME] ||
		     !attr[IPSET_ATTR_TYPENAME] ||
		     !attr[IPSET_ATTR_REVISION] ||
		     !attr[IPSET_ATTR_FAMILY] ||
		     (attr[IPSET_ATTR_DATA] &&
		      !flag_nested(attr[IPSET_ATTR_DATA]))))
		return -IPSET_ERR_PROTOCOL;

	name = nla_data(attr[IPSET_ATTR_SETNAME]);
	typename = nla_data(attr[IPSET_ATTR_TYPENAME]);
	family = nla_get_u8(attr[IPSET_ATTR_FAMILY]);
	revision = nla_get_u8(attr[IPSET_ATTR_REVISION]);
	pr_debug("setname: %s, typename: %s, family: %s, revision: %u\n",
		 name, typename, family_name(family), revision);

	/* First, and without any locks, allocate and initialize
	 * a normal base set structure.
	 */
	set = kzalloc(sizeof(*set), GFP_KERNEL);
	if (!set)
		return -ENOMEM;
	spin_lock_init(&set->lock);
	strscpy(set->name, name, IPSET_MAXNAMELEN);
	set->family = family;
	set->revision = revision;

	/* Next, check that we know the type, and take
	 * a reference on the type, to make sure it stays available
	 * while constructing our new set.
	 *
	 * After referencing the type, we try to create the type
	 * specific part of the set without holding any locks.
	 */
	ret = find_set_type_get(typename, family, revision, &set->type);
	if (ret)
		goto out;

	/* Without holding any locks, create private part. */
	if (attr[IPSET_ATTR_DATA] &&
	    nla_parse_nested(tb, IPSET_ATTR_CREATE_MAX, attr[IPSET_ATTR_DATA],
			     set->type->create_policy, NULL)) {
		ret = -IPSET_ERR_PROTOCOL;
		goto put_out;
	}
	/* Set create flags depending on the type revision */
	set->flags |= set->type->create_flags[revision];

	ret = set->type->create(info->net, set, tb, flags);
	if (ret != 0)
		goto put_out;

	/* BTW, ret==0 here. */

	/* Here, we have a valid, constructed set and we are protected
	 * by the nfnl mutex. Find the first free index in ip_set_list
	 * and check clashing.
	 */
	ret = find_free_id(inst, set->name, &index, &clash);
	if (ret == -EEXIST) {
		/* If this is the same set and requested, ignore error */
		if ((flags & IPSET_FLAG_EXIST) &&
		    STRNCMP(set->type->name, clash->type->name) &&
		    set->type->family == clash->type->family &&
		    set->type->revision_min == clash->type->revision_min &&
		    set->type->revision_max == clash->type->revision_max &&
		    set->variant->same_set(set, clash))
			ret = 0;
		goto cleanup;
	} else if (ret == -IPSET_ERR_MAX_SETS) {
		struct ip_set **list, **tmp;
		ip_set_id_t i = inst->ip_set_max + IP_SET_INC;

		if (i < inst->ip_set_max || i == IPSET_INVALID_ID)
			/* Wraparound */
			goto cleanup;

		list = kvcalloc(i, sizeof(struct ip_set *), GFP_KERNEL);
		if (!list)
			goto cleanup;
		/* nfnl mutex is held, both lists are valid */
		tmp = ip_set_dereference(inst->ip_set_list);
		memcpy(list, tmp, sizeof(struct ip_set *) * inst->ip_set_max);
		rcu_assign_pointer(inst->ip_set_list, list);
		/* Make sure all current packets have passed through */
		synchronize_net();
		/* Use new list */
		index = inst->ip_set_max;
		inst->ip_set_max = i;
		kvfree(tmp);
		ret = 0;
	} else if (ret) {
		goto cleanup;
	}

	/* Finally! Add our shiny new set to the list, and be done. */
	pr_debug("create: '%s' created with index %u!\n", set->name, index);
	ip_set(inst, index) = set;

	return ret;

cleanup:
	set->variant->cancel_gc(set);
	set->variant->destroy(set);
put_out:
	module_put(set->type->me);
out:
	kfree(set);
	return ret;
}

/* Destroy sets */

static const struct nla_policy
ip_set_setname_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
};

/* In order to return quickly when destroying a single set, it is split
 * into two stages:
 * - Cancel garbage collector
 * - Destroy the set itself via call_rcu()
 */

static void
ip_set_destroy_set_rcu(struct rcu_head *head)
{
	struct ip_set *set = container_of(head, struct ip_set, rcu);

	set->variant->destroy(set);
	module_put(set->type->me);
	kfree(set);
}

static void
_destroy_all_sets(struct ip_set_net *inst)
{
	struct ip_set *set;
	ip_set_id_t i;
	bool need_wait = false;

	/* First cancel gc's: set:list sets are flushed as well */
	for (i = 0; i < inst->ip_set_max; i++) {
		set = ip_set(inst, i);
		if (set) {
			set->variant->cancel_gc(set);
			if (set->type->features & IPSET_TYPE_NAME)
				need_wait = true;
		}
	}
	/* Must wait for flush to be really finished  */
	if (need_wait)
		rcu_barrier();
	for (i = 0; i < inst->ip_set_max; i++) {
		set = ip_set(inst, i);
		if (set) {
			ip_set(inst, i) = NULL;
			set->variant->destroy(set);
			module_put(set->type->me);
			kfree(set);
		}
	}
}

static int ip_set_destroy(struct sk_buff *skb, const struct nfnl_info *info,
			  const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct ip_set *s;
	ip_set_id_t i;
	int ret = 0;

	if (unlikely(protocol_min_failed(attr)))
		return -IPSET_ERR_PROTOCOL;

	/* Commands are serialized and references are
	 * protected by the ip_set_ref_lock.
	 * External systems (i.e. xt_set) must call
	 * ip_set_nfnl_get_* functions, that way we
	 * can safely check references here.
	 *
	 * list:set timer can only decrement the reference
	 * counter, so if it's already zero, we can proceed
	 * without holding the lock.
	 */
	if (!attr[IPSET_ATTR_SETNAME]) {
		read_lock_bh(&ip_set_ref_lock);
		for (i = 0; i < inst->ip_set_max; i++) {
			s = ip_set(inst, i);
			if (s && (s->ref || s->ref_netlink)) {
				ret = -IPSET_ERR_BUSY;
				goto out;
			}
		}
		inst->is_destroyed = true;
		read_unlock_bh(&ip_set_ref_lock);
		_destroy_all_sets(inst);
		/* Modified by ip_set_destroy() only, which is serialized */
		inst->is_destroyed = false;
	} else {
		u32 flags = flag_exist(info->nlh);
		u16 features = 0;

		read_lock_bh(&ip_set_ref_lock);
		s = find_set_and_id(inst, nla_data(attr[IPSET_ATTR_SETNAME]),
				    &i);
		if (!s) {
			if (!(flags & IPSET_FLAG_EXIST))
				ret = -ENOENT;
			goto out;
		} else if (s->ref || s->ref_netlink) {
			ret = -IPSET_ERR_BUSY;
			goto out;
		}
		features = s->type->features;
		ip_set(inst, i) = NULL;
		read_unlock_bh(&ip_set_ref_lock);
		/* Must cancel garbage collectors */
		s->variant->cancel_gc(s);
		if (features & IPSET_TYPE_NAME) {
			/* Must wait for flush to be really finished  */
			rcu_barrier();
		}
		call_rcu(&s->rcu, ip_set_destroy_set_rcu);
	}
	return 0;
out:
	read_unlock_bh(&ip_set_ref_lock);
	return ret;
}

/* Flush sets */

static void
ip_set_flush_set(struct ip_set *set)
{
	pr_debug("set: %s\n",  set->name);

	ip_set_lock(set);
	set->variant->flush(set);
	ip_set_unlock(set);
}

static int ip_set_flush(struct sk_buff *skb, const struct nfnl_info *info,
			const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct ip_set *s;
	ip_set_id_t i;

	if (unlikely(protocol_min_failed(attr)))
		return -IPSET_ERR_PROTOCOL;

	if (!attr[IPSET_ATTR_SETNAME]) {
		for (i = 0; i < inst->ip_set_max; i++) {
			s = ip_set(inst, i);
			if (s)
				ip_set_flush_set(s);
		}
	} else {
		s = find_set(inst, nla_data(attr[IPSET_ATTR_SETNAME]));
		if (!s)
			return -ENOENT;

		ip_set_flush_set(s);
	}

	return 0;
}

/* Rename a set */

static const struct nla_policy
ip_set_setname2_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_SETNAME2]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
};

static int ip_set_rename(struct sk_buff *skb, const struct nfnl_info *info,
			 const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct ip_set *set, *s;
	const char *name2;
	ip_set_id_t i;
	int ret = 0;

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME] ||
		     !attr[IPSET_ATTR_SETNAME2]))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(inst, nla_data(attr[IPSET_ATTR_SETNAME]));
	if (!set)
		return -ENOENT;

	write_lock_bh(&ip_set_ref_lock);
	if (set->ref != 0 || set->ref_netlink != 0) {
		ret = -IPSET_ERR_REFERENCED;
		goto out;
	}

	name2 = nla_data(attr[IPSET_ATTR_SETNAME2]);
	for (i = 0; i < inst->ip_set_max; i++) {
		s = ip_set(inst, i);
		if (s && STRNCMP(s->name, name2)) {
			ret = -IPSET_ERR_EXIST_SETNAME2;
			goto out;
		}
	}
	strscpy_pad(set->name, name2, IPSET_MAXNAMELEN);

out:
	write_unlock_bh(&ip_set_ref_lock);
	return ret;
}

/* Swap two sets so that name/index points to the other.
 * References and set names are also swapped.
 *
 * The commands are serialized by the nfnl mutex and references are
 * protected by the ip_set_ref_lock. The kernel interfaces
 * do not hold the mutex but the pointer settings are atomic
 * so the ip_set_list always contains valid pointers to the sets.
 */

static int ip_set_swap(struct sk_buff *skb, const struct nfnl_info *info,
		       const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct ip_set *from, *to;
	ip_set_id_t from_id, to_id;
	char from_name[IPSET_MAXNAMELEN];

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME] ||
		     !attr[IPSET_ATTR_SETNAME2]))
		return -IPSET_ERR_PROTOCOL;

	from = find_set_and_id(inst, nla_data(attr[IPSET_ATTR_SETNAME]),
			       &from_id);
	if (!from)
		return -ENOENT;

	to = find_set_and_id(inst, nla_data(attr[IPSET_ATTR_SETNAME2]),
			     &to_id);
	if (!to)
		return -IPSET_ERR_EXIST_SETNAME2;

	/* Features must not change.
	 * Not an artifical restriction anymore, as we must prevent
	 * possible loops created by swapping in setlist type of sets.
	 */
	if (!(from->type->features == to->type->features &&
	      from->family == to->family))
		return -IPSET_ERR_TYPE_MISMATCH;

	write_lock_bh(&ip_set_ref_lock);

	if (from->ref_netlink || to->ref_netlink) {
		write_unlock_bh(&ip_set_ref_lock);
		return -EBUSY;
	}

	strscpy_pad(from_name, from->name, IPSET_MAXNAMELEN);
	strscpy_pad(from->name, to->name, IPSET_MAXNAMELEN);
	strscpy_pad(to->name, from_name, IPSET_MAXNAMELEN);

	swap(from->ref, to->ref);
	ip_set(inst, from_id) = to;
	ip_set(inst, to_id) = from;
	write_unlock_bh(&ip_set_ref_lock);

	return 0;
}

/* List/save set data */

#define DUMP_INIT	0
#define DUMP_ALL	1
#define DUMP_ONE	2
#define DUMP_LAST	3

#define DUMP_TYPE(arg)		(((u32)(arg)) & 0x0000FFFF)
#define DUMP_FLAGS(arg)		(((u32)(arg)) >> 16)

int
ip_set_put_flags(struct sk_buff *skb, struct ip_set *set)
{
	u32 cadt_flags = 0;

	if (SET_WITH_TIMEOUT(set))
		if (unlikely(nla_put_net32(skb, IPSET_ATTR_TIMEOUT,
					   htonl(set->timeout))))
			return -EMSGSIZE;
	if (SET_WITH_COUNTER(set))
		cadt_flags |= IPSET_FLAG_WITH_COUNTERS;
	if (SET_WITH_COMMENT(set))
		cadt_flags |= IPSET_FLAG_WITH_COMMENT;
	if (SET_WITH_SKBINFO(set))
		cadt_flags |= IPSET_FLAG_WITH_SKBINFO;
	if (SET_WITH_FORCEADD(set))
		cadt_flags |= IPSET_FLAG_WITH_FORCEADD;

	if (!cadt_flags)
		return 0;
	return nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(cadt_flags));
}
EXPORT_SYMBOL_GPL(ip_set_put_flags);

static int
ip_set_dump_done(struct netlink_callback *cb)
{
	if (cb->args[IPSET_CB_ARG0]) {
		struct ip_set_net *inst =
			(struct ip_set_net *)cb->args[IPSET_CB_NET];
		ip_set_id_t index = (ip_set_id_t)cb->args[IPSET_CB_INDEX];
		struct ip_set *set = ip_set_ref_netlink(inst, index);

		if (set->variant->uref)
			set->variant->uref(set, cb, false);
		pr_debug("release set %s\n", set->name);
		__ip_set_put_netlink(set);
	}
	return 0;
}

static inline void
dump_attrs(struct nlmsghdr *nlh)
{
	const struct nlattr *attr;
	int rem;

	pr_debug("dump nlmsg\n");
	nlmsg_for_each_attr(attr, nlh, sizeof(struct nfgenmsg), rem) {
		pr_debug("type: %u, len %u\n", nla_type(attr), attr->nla_len);
	}
}

static const struct nla_policy
ip_set_dump_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_FLAGS]	= { .type = NLA_U32 },
};

static int
ip_set_dump_start(struct netlink_callback *cb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(cb->skb);
	int min_len = nlmsg_total_size(sizeof(struct nfgenmsg));
	struct nlattr *cda[IPSET_ATTR_CMD_MAX + 1];
	struct nlattr *attr = (void *)nlh + min_len;
	struct sk_buff *skb = cb->skb;
	struct ip_set_net *inst = ip_set_pernet(sock_net(skb->sk));
	u32 dump_type;
	int ret;

	ret = nla_parse(cda, IPSET_ATTR_CMD_MAX, attr,
			nlh->nlmsg_len - min_len,
			ip_set_dump_policy, NULL);
	if (ret)
		goto error;

	cb->args[IPSET_CB_PROTO] = nla_get_u8(cda[IPSET_ATTR_PROTOCOL]);
	if (cda[IPSET_ATTR_SETNAME]) {
		ip_set_id_t index;
		struct ip_set *set;

		set = find_set_and_id(inst, nla_data(cda[IPSET_ATTR_SETNAME]),
				      &index);
		if (!set) {
			ret = -ENOENT;
			goto error;
		}
		dump_type = DUMP_ONE;
		cb->args[IPSET_CB_INDEX] = index;
	} else {
		dump_type = DUMP_ALL;
	}

	if (cda[IPSET_ATTR_FLAGS]) {
		u32 f = ip_set_get_h32(cda[IPSET_ATTR_FLAGS]);

		dump_type |= (f << 16);
	}
	cb->args[IPSET_CB_NET] = (unsigned long)inst;
	cb->args[IPSET_CB_DUMP] = dump_type;

	return 0;

error:
	/* We have to create and send the error message manually :-( */
	if (nlh->nlmsg_flags & NLM_F_ACK) {
		netlink_ack(cb->skb, nlh, ret, NULL);
	}
	return ret;
}

static int
ip_set_dump_do(struct sk_buff *skb, struct netlink_callback *cb)
{
	ip_set_id_t index = IPSET_INVALID_ID, max;
	struct ip_set *set = NULL;
	struct nlmsghdr *nlh = NULL;
	unsigned int flags = NETLINK_CB(cb->skb).portid ? NLM_F_MULTI : 0;
	struct ip_set_net *inst = ip_set_pernet(sock_net(skb->sk));
	u32 dump_type, dump_flags;
	bool is_destroyed;
	int ret = 0;

	if (!cb->args[IPSET_CB_DUMP])
		return -EINVAL;

	if (cb->args[IPSET_CB_INDEX] >= inst->ip_set_max)
		goto out;

	dump_type = DUMP_TYPE(cb->args[IPSET_CB_DUMP]);
	dump_flags = DUMP_FLAGS(cb->args[IPSET_CB_DUMP]);
	max = dump_type == DUMP_ONE ? cb->args[IPSET_CB_INDEX] + 1
				    : inst->ip_set_max;
dump_last:
	pr_debug("dump type, flag: %u %u index: %ld\n",
		 dump_type, dump_flags, cb->args[IPSET_CB_INDEX]);
	for (; cb->args[IPSET_CB_INDEX] < max; cb->args[IPSET_CB_INDEX]++) {
		index = (ip_set_id_t)cb->args[IPSET_CB_INDEX];
		write_lock_bh(&ip_set_ref_lock);
		set = ip_set(inst, index);
		is_destroyed = inst->is_destroyed;
		if (!set || is_destroyed) {
			write_unlock_bh(&ip_set_ref_lock);
			if (dump_type == DUMP_ONE) {
				ret = -ENOENT;
				goto out;
			}
			if (is_destroyed) {
				/* All sets are just being destroyed */
				ret = 0;
				goto out;
			}
			continue;
		}
		/* When dumping all sets, we must dump "sorted"
		 * so that lists (unions of sets) are dumped last.
		 */
		if (dump_type != DUMP_ONE &&
		    ((dump_type == DUMP_ALL) ==
		     !!(set->type->features & IPSET_DUMP_LAST))) {
			write_unlock_bh(&ip_set_ref_lock);
			continue;
		}
		pr_debug("List set: %s\n", set->name);
		if (!cb->args[IPSET_CB_ARG0]) {
			/* Start listing: make sure set won't be destroyed */
			pr_debug("reference set\n");
			set->ref_netlink++;
		}
		write_unlock_bh(&ip_set_ref_lock);
		nlh = start_msg(skb, NETLINK_CB(cb->skb).portid,
				cb->nlh->nlmsg_seq, flags,
				IPSET_CMD_LIST);
		if (!nlh) {
			ret = -EMSGSIZE;
			goto release_refcount;
		}
		if (nla_put_u8(skb, IPSET_ATTR_PROTOCOL,
			       cb->args[IPSET_CB_PROTO]) ||
		    nla_put_string(skb, IPSET_ATTR_SETNAME, set->name))
			goto nla_put_failure;
		if (dump_flags & IPSET_FLAG_LIST_SETNAME)
			goto next_set;
		switch (cb->args[IPSET_CB_ARG0]) {
		case 0:
			/* Core header data */
			if (nla_put_string(skb, IPSET_ATTR_TYPENAME,
					   set->type->name) ||
			    nla_put_u8(skb, IPSET_ATTR_FAMILY,
				       set->family) ||
			    nla_put_u8(skb, IPSET_ATTR_REVISION,
				       set->revision))
				goto nla_put_failure;
			if (cb->args[IPSET_CB_PROTO] > IPSET_PROTOCOL_MIN &&
			    nla_put_net16(skb, IPSET_ATTR_INDEX, htons(index)))
				goto nla_put_failure;
			ret = set->variant->head(set, skb);
			if (ret < 0)
				goto release_refcount;
			if (dump_flags & IPSET_FLAG_LIST_HEADER)
				goto next_set;
			if (set->variant->uref)
				set->variant->uref(set, cb, true);
			fallthrough;
		default:
			ret = set->variant->list(set, skb, cb);
			if (!cb->args[IPSET_CB_ARG0])
				/* Set is done, proceed with next one */
				goto next_set;
			goto release_refcount;
		}
	}
	/* If we dump all sets, continue with dumping last ones */
	if (dump_type == DUMP_ALL) {
		dump_type = DUMP_LAST;
		cb->args[IPSET_CB_DUMP] = dump_type | (dump_flags << 16);
		cb->args[IPSET_CB_INDEX] = 0;
		if (set && set->variant->uref)
			set->variant->uref(set, cb, false);
		goto dump_last;
	}
	goto out;

nla_put_failure:
	ret = -EFAULT;
next_set:
	if (dump_type == DUMP_ONE)
		cb->args[IPSET_CB_INDEX] = IPSET_INVALID_ID;
	else
		cb->args[IPSET_CB_INDEX]++;
release_refcount:
	/* If there was an error or set is done, release set */
	if (ret || !cb->args[IPSET_CB_ARG0]) {
		set = ip_set_ref_netlink(inst, index);
		if (set->variant->uref)
			set->variant->uref(set, cb, false);
		pr_debug("release set %s\n", set->name);
		__ip_set_put_netlink(set);
		cb->args[IPSET_CB_ARG0] = 0;
	}
out:
	if (nlh) {
		nlmsg_end(skb, nlh);
		pr_debug("nlmsg_len: %u\n", nlh->nlmsg_len);
		dump_attrs(nlh);
	}

	return ret < 0 ? ret : skb->len;
}

static int ip_set_dump(struct sk_buff *skb, const struct nfnl_info *info,
		       const struct nlattr * const attr[])
{
	if (unlikely(protocol_min_failed(attr)))
		return -IPSET_ERR_PROTOCOL;

	{
		struct netlink_dump_control c = {
			.start = ip_set_dump_start,
			.dump = ip_set_dump_do,
			.done = ip_set_dump_done,
		};
		return netlink_dump_start(info->sk, skb, info->nlh, &c);
	}
}

/* Add, del and test */

static const struct nla_policy ip_set_adt_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
	[IPSET_ATTR_DATA]	= { .type = NLA_NESTED },
	[IPSET_ATTR_ADT]	= { .type = NLA_NESTED },
};

static int
call_ad(struct net *net, struct sock *ctnl, struct sk_buff *skb,
	struct ip_set *set, struct nlattr *tb[], enum ipset_adt adt,
	u32 flags, bool use_lineno)
{
	int ret;
	u32 lineno = 0;
	bool eexist = flags & IPSET_FLAG_EXIST, retried = false;

	do {
		if (retried) {
			__ip_set_get_netlink(set);
			nfnl_unlock(NFNL_SUBSYS_IPSET);
			cond_resched();
			nfnl_lock(NFNL_SUBSYS_IPSET);
			__ip_set_put_netlink(set);
		}

		ip_set_lock(set);
		ret = set->variant->uadt(set, tb, adt, &lineno, flags, retried);
		ip_set_unlock(set);
		retried = true;
	} while (ret == -ERANGE ||
		 (ret == -EAGAIN &&
		  set->variant->resize &&
		  (ret = set->variant->resize(set, retried)) == 0));

	if (!ret || (ret == -IPSET_ERR_EXIST && eexist))
		return 0;
	if (lineno && use_lineno) {
		/* Error in restore/batch mode: send back lineno */
		struct nlmsghdr *rep, *nlh = nlmsg_hdr(skb);
		struct sk_buff *skb2;
		struct nlmsgerr *errmsg;
		size_t payload = min(SIZE_MAX,
				     sizeof(*errmsg) + nlmsg_len(nlh));
		int min_len = nlmsg_total_size(sizeof(struct nfgenmsg));
		struct nlattr *cda[IPSET_ATTR_CMD_MAX + 1];
		struct nlattr *cmdattr;
		u32 *errline;

		skb2 = nlmsg_new(payload, GFP_KERNEL);
		if (!skb2)
			return -ENOMEM;
		rep = nlmsg_put(skb2, NETLINK_CB(skb).portid,
				nlh->nlmsg_seq, NLMSG_ERROR, payload, 0);
		errmsg = nlmsg_data(rep);
		errmsg->error = ret;
		unsafe_memcpy(&errmsg->msg, nlh, nlh->nlmsg_len,
			      /* Bounds checked by the skb layer. */);

		cmdattr = (void *)&errmsg->msg + min_len;

		ret = nla_parse(cda, IPSET_ATTR_CMD_MAX, cmdattr,
				nlh->nlmsg_len - min_len, ip_set_adt_policy,
				NULL);

		if (ret) {
			nlmsg_free(skb2);
			return ret;
		}
		errline = nla_data(cda[IPSET_ATTR_LINENO]);

		*errline = lineno;

		nfnetlink_unicast(skb2, net, NETLINK_CB(skb).portid);
		/* Signal netlink not to send its ACK/errmsg.  */
		return -EINTR;
	}

	return ret;
}

static int ip_set_ad(struct net *net, struct sock *ctnl,
		     struct sk_buff *skb,
		     enum ipset_adt adt,
		     const struct nlmsghdr *nlh,
		     const struct nlattr * const attr[],
		     struct netlink_ext_ack *extack)
{
	struct ip_set_net *inst = ip_set_pernet(net);
	struct ip_set *set;
	struct nlattr *tb[IPSET_ATTR_ADT_MAX + 1] = {};
	const struct nlattr *nla;
	u32 flags = flag_exist(nlh);
	bool use_lineno;
	int ret = 0;

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME] ||
		     !((attr[IPSET_ATTR_DATA] != NULL) ^
		       (attr[IPSET_ATTR_ADT] != NULL)) ||
		     (attr[IPSET_ATTR_DATA] &&
		      !flag_nested(attr[IPSET_ATTR_DATA])) ||
		     (attr[IPSET_ATTR_ADT] &&
		      (!flag_nested(attr[IPSET_ATTR_ADT]) ||
		       !attr[IPSET_ATTR_LINENO]))))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(inst, nla_data(attr[IPSET_ATTR_SETNAME]));
	if (!set)
		return -ENOENT;

	use_lineno = !!attr[IPSET_ATTR_LINENO];
	if (attr[IPSET_ATTR_DATA]) {
		if (nla_parse_nested(tb, IPSET_ATTR_ADT_MAX,
				     attr[IPSET_ATTR_DATA],
				     set->type->adt_policy, NULL))
			return -IPSET_ERR_PROTOCOL;
		ret = call_ad(net, ctnl, skb, set, tb, adt, flags,
			      use_lineno);
	} else {
		int nla_rem;

		nla_for_each_nested(nla, attr[IPSET_ATTR_ADT], nla_rem) {
			if (nla_type(nla) != IPSET_ATTR_DATA ||
			    !flag_nested(nla) ||
			    nla_parse_nested(tb, IPSET_ATTR_ADT_MAX, nla,
					     set->type->adt_policy, NULL))
				return -IPSET_ERR_PROTOCOL;
			ret = call_ad(net, ctnl, skb, set, tb, adt,
				      flags, use_lineno);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}

static int ip_set_uadd(struct sk_buff *skb, const struct nfnl_info *info,
		       const struct nlattr * const attr[])
{
	return ip_set_ad(info->net, info->sk, skb,
			 IPSET_ADD, info->nlh, attr, info->extack);
}

static int ip_set_udel(struct sk_buff *skb, const struct nfnl_info *info,
		       const struct nlattr * const attr[])
{
	return ip_set_ad(info->net, info->sk, skb,
			 IPSET_DEL, info->nlh, attr, info->extack);
}

static int ip_set_utest(struct sk_buff *skb, const struct nfnl_info *info,
			const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct ip_set *set;
	struct nlattr *tb[IPSET_ATTR_ADT_MAX + 1] = {};
	int ret = 0;
	u32 lineno;

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME] ||
		     !attr[IPSET_ATTR_DATA] ||
		     !flag_nested(attr[IPSET_ATTR_DATA])))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(inst, nla_data(attr[IPSET_ATTR_SETNAME]));
	if (!set)
		return -ENOENT;

	if (nla_parse_nested(tb, IPSET_ATTR_ADT_MAX, attr[IPSET_ATTR_DATA],
			     set->type->adt_policy, NULL))
		return -IPSET_ERR_PROTOCOL;

	rcu_read_lock_bh();
	ret = set->variant->uadt(set, tb, IPSET_TEST, &lineno, 0, 0);
	rcu_read_unlock_bh();
	/* Userspace can't trigger element to be re-added */
	if (ret == -EAGAIN)
		ret = 1;

	return ret > 0 ? 0 : -IPSET_ERR_EXIST;
}

/* Get headed data of a set */

static int ip_set_header(struct sk_buff *skb, const struct nfnl_info *info,
			 const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	const struct ip_set *set;
	struct sk_buff *skb2;
	struct nlmsghdr *nlh2;

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME]))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(inst, nla_data(attr[IPSET_ATTR_SETNAME]));
	if (!set)
		return -ENOENT;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).portid, info->nlh->nlmsg_seq, 0,
			 IPSET_CMD_HEADER);
	if (!nlh2)
		goto nlmsg_failure;
	if (nla_put_u8(skb2, IPSET_ATTR_PROTOCOL, protocol(attr)) ||
	    nla_put_string(skb2, IPSET_ATTR_SETNAME, set->name) ||
	    nla_put_string(skb2, IPSET_ATTR_TYPENAME, set->type->name) ||
	    nla_put_u8(skb2, IPSET_ATTR_FAMILY, set->family) ||
	    nla_put_u8(skb2, IPSET_ATTR_REVISION, set->revision))
		goto nla_put_failure;
	nlmsg_end(skb2, nlh2);

	return nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);

nla_put_failure:
	nlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EMSGSIZE;
}

/* Get type data */

static const struct nla_policy ip_set_type_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_TYPENAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_FAMILY]	= { .type = NLA_U8 },
};

static int ip_set_type(struct sk_buff *skb, const struct nfnl_info *info,
		       const struct nlattr * const attr[])
{
	struct sk_buff *skb2;
	struct nlmsghdr *nlh2;
	u8 family, min, max;
	const char *typename;
	int ret = 0;

	if (unlikely(protocol_min_failed(attr) ||
		     !attr[IPSET_ATTR_TYPENAME] ||
		     !attr[IPSET_ATTR_FAMILY]))
		return -IPSET_ERR_PROTOCOL;

	family = nla_get_u8(attr[IPSET_ATTR_FAMILY]);
	typename = nla_data(attr[IPSET_ATTR_TYPENAME]);
	ret = find_set_type_minmax(typename, family, &min, &max);
	if (ret)
		return ret;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).portid, info->nlh->nlmsg_seq, 0,
			 IPSET_CMD_TYPE);
	if (!nlh2)
		goto nlmsg_failure;
	if (nla_put_u8(skb2, IPSET_ATTR_PROTOCOL, protocol(attr)) ||
	    nla_put_string(skb2, IPSET_ATTR_TYPENAME, typename) ||
	    nla_put_u8(skb2, IPSET_ATTR_FAMILY, family) ||
	    nla_put_u8(skb2, IPSET_ATTR_REVISION, max) ||
	    nla_put_u8(skb2, IPSET_ATTR_REVISION_MIN, min))
		goto nla_put_failure;
	nlmsg_end(skb2, nlh2);

	pr_debug("Send TYPE, nlmsg_len: %u\n", nlh2->nlmsg_len);
	return nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);

nla_put_failure:
	nlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EMSGSIZE;
}

/* Get protocol version */

static const struct nla_policy
ip_set_protocol_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
};

static int ip_set_protocol(struct sk_buff *skb, const struct nfnl_info *info,
			   const struct nlattr * const attr[])
{
	struct sk_buff *skb2;
	struct nlmsghdr *nlh2;

	if (unlikely(!attr[IPSET_ATTR_PROTOCOL]))
		return -IPSET_ERR_PROTOCOL;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).portid, info->nlh->nlmsg_seq, 0,
			 IPSET_CMD_PROTOCOL);
	if (!nlh2)
		goto nlmsg_failure;
	if (nla_put_u8(skb2, IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL))
		goto nla_put_failure;
	if (nla_put_u8(skb2, IPSET_ATTR_PROTOCOL_MIN, IPSET_PROTOCOL_MIN))
		goto nla_put_failure;
	nlmsg_end(skb2, nlh2);

	return nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);

nla_put_failure:
	nlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EMSGSIZE;
}

/* Get set by name or index, from userspace */

static int ip_set_byname(struct sk_buff *skb, const struct nfnl_info *info,
			 const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct sk_buff *skb2;
	struct nlmsghdr *nlh2;
	ip_set_id_t id = IPSET_INVALID_ID;
	const struct ip_set *set;

	if (unlikely(protocol_failed(attr) ||
		     !attr[IPSET_ATTR_SETNAME]))
		return -IPSET_ERR_PROTOCOL;

	set = find_set_and_id(inst, nla_data(attr[IPSET_ATTR_SETNAME]), &id);
	if (id == IPSET_INVALID_ID)
		return -ENOENT;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).portid, info->nlh->nlmsg_seq, 0,
			 IPSET_CMD_GET_BYNAME);
	if (!nlh2)
		goto nlmsg_failure;
	if (nla_put_u8(skb2, IPSET_ATTR_PROTOCOL, protocol(attr)) ||
	    nla_put_u8(skb2, IPSET_ATTR_FAMILY, set->family) ||
	    nla_put_net16(skb2, IPSET_ATTR_INDEX, htons(id)))
		goto nla_put_failure;
	nlmsg_end(skb2, nlh2);

	return nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);

nla_put_failure:
	nlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EMSGSIZE;
}

static const struct nla_policy ip_set_index_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_INDEX]	= { .type = NLA_U16 },
};

static int ip_set_byindex(struct sk_buff *skb, const struct nfnl_info *info,
			  const struct nlattr * const attr[])
{
	struct ip_set_net *inst = ip_set_pernet(info->net);
	struct sk_buff *skb2;
	struct nlmsghdr *nlh2;
	ip_set_id_t id = IPSET_INVALID_ID;
	const struct ip_set *set;

	if (unlikely(protocol_failed(attr) ||
		     !attr[IPSET_ATTR_INDEX]))
		return -IPSET_ERR_PROTOCOL;

	id = ip_set_get_h16(attr[IPSET_ATTR_INDEX]);
	if (id >= inst->ip_set_max)
		return -ENOENT;
	set = ip_set(inst, id);
	if (set == NULL)
		return -ENOENT;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).portid, info->nlh->nlmsg_seq, 0,
			 IPSET_CMD_GET_BYINDEX);
	if (!nlh2)
		goto nlmsg_failure;
	if (nla_put_u8(skb2, IPSET_ATTR_PROTOCOL, protocol(attr)) ||
	    nla_put_string(skb2, IPSET_ATTR_SETNAME, set->name))
		goto nla_put_failure;
	nlmsg_end(skb2, nlh2);

	return nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);

nla_put_failure:
	nlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EMSGSIZE;
}

static const struct nfnl_callback ip_set_netlink_subsys_cb[IPSET_MSG_MAX] = {
	[IPSET_CMD_NONE]	= {
		.call		= ip_set_none,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
	},
	[IPSET_CMD_CREATE]	= {
		.call		= ip_set_create,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_create_policy,
	},
	[IPSET_CMD_DESTROY]	= {
		.call		= ip_set_destroy,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname_policy,
	},
	[IPSET_CMD_FLUSH]	= {
		.call		= ip_set_flush,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname_policy,
	},
	[IPSET_CMD_RENAME]	= {
		.call		= ip_set_rename,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname2_policy,
	},
	[IPSET_CMD_SWAP]	= {
		.call		= ip_set_swap,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname2_policy,
	},
	[IPSET_CMD_LIST]	= {
		.call		= ip_set_dump,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_dump_policy,
	},
	[IPSET_CMD_SAVE]	= {
		.call		= ip_set_dump,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname_policy,
	},
	[IPSET_CMD_ADD]	= {
		.call		= ip_set_uadd,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_adt_policy,
	},
	[IPSET_CMD_DEL]	= {
		.call		= ip_set_udel,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_adt_policy,
	},
	[IPSET_CMD_TEST]	= {
		.call		= ip_set_utest,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_adt_policy,
	},
	[IPSET_CMD_HEADER]	= {
		.call		= ip_set_header,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname_policy,
	},
	[IPSET_CMD_TYPE]	= {
		.call		= ip_set_type,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_type_policy,
	},
	[IPSET_CMD_PROTOCOL]	= {
		.call		= ip_set_protocol,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_protocol_policy,
	},
	[IPSET_CMD_GET_BYNAME]	= {
		.call		= ip_set_byname,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_setname_policy,
	},
	[IPSET_CMD_GET_BYINDEX]	= {
		.call		= ip_set_byindex,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= IPSET_ATTR_CMD_MAX,
		.policy		= ip_set_index_policy,
	},
};

static struct nfnetlink_subsystem ip_set_netlink_subsys __read_mostly = {
	.name		= "ip_set",
	.subsys_id	= NFNL_SUBSYS_IPSET,
	.cb_count	= IPSET_MSG_MAX,
	.cb		= ip_set_netlink_subsys_cb,
};

/* Interface to iptables/ip6tables */

static int
ip_set_sockfn_get(struct sock *sk, int optval, void __user *user, int *len)
{
	unsigned int *op;
	void *data;
	int copylen = *len, ret = 0;
	struct net *net = sock_net(sk);
	struct ip_set_net *inst = ip_set_pernet(net);

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;
	if (optval != SO_IP_SET)
		return -EBADF;
	if (*len < sizeof(unsigned int))
		return -EINVAL;

	data = vmalloc(*len);
	if (!data)
		return -ENOMEM;
	if (copy_from_user(data, user, *len) != 0) {
		ret = -EFAULT;
		goto done;
	}
	op = data;

	if (*op < IP_SET_OP_VERSION) {
		/* Check the version at the beginning of operations */
		struct ip_set_req_version *req_version = data;

		if (*len < sizeof(struct ip_set_req_version)) {
			ret = -EINVAL;
			goto done;
		}

		if (req_version->version < IPSET_PROTOCOL_MIN) {
			ret = -EPROTO;
			goto done;
		}
	}

	switch (*op) {
	case IP_SET_OP_VERSION: {
		struct ip_set_req_version *req_version = data;

		if (*len != sizeof(struct ip_set_req_version)) {
			ret = -EINVAL;
			goto done;
		}

		req_version->version = IPSET_PROTOCOL;
		if (copy_to_user(user, req_version,
				 sizeof(struct ip_set_req_version)))
			ret = -EFAULT;
		goto done;
	}
	case IP_SET_OP_GET_BYNAME: {
		struct ip_set_req_get_set *req_get = data;
		ip_set_id_t id;

		if (*len != sizeof(struct ip_set_req_get_set)) {
			ret = -EINVAL;
			goto done;
		}
		req_get->set.name[IPSET_MAXNAMELEN - 1] = '\0';
		nfnl_lock(NFNL_SUBSYS_IPSET);
		find_set_and_id(inst, req_get->set.name, &id);
		req_get->set.index = id;
		nfnl_unlock(NFNL_SUBSYS_IPSET);
		goto copy;
	}
	case IP_SET_OP_GET_FNAME: {
		struct ip_set_req_get_set_family *req_get = data;
		ip_set_id_t id;

		if (*len != sizeof(struct ip_set_req_get_set_family)) {
			ret = -EINVAL;
			goto done;
		}
		req_get->set.name[IPSET_MAXNAMELEN - 1] = '\0';
		nfnl_lock(NFNL_SUBSYS_IPSET);
		find_set_and_id(inst, req_get->set.name, &id);
		req_get->set.index = id;
		if (id != IPSET_INVALID_ID)
			req_get->family = ip_set(inst, id)->family;
		nfnl_unlock(NFNL_SUBSYS_IPSET);
		goto copy;
	}
	case IP_SET_OP_GET_BYINDEX: {
		struct ip_set_req_get_set *req_get = data;
		struct ip_set *set;

		if (*len != sizeof(struct ip_set_req_get_set) ||
		    req_get->set.index >= inst->ip_set_max) {
			ret = -EINVAL;
			goto done;
		}
		nfnl_lock(NFNL_SUBSYS_IPSET);
		set = ip_set(inst, req_get->set.index);
		ret = strscpy(req_get->set.name, set ? set->name : "",
			      IPSET_MAXNAMELEN);
		nfnl_unlock(NFNL_SUBSYS_IPSET);
		if (ret < 0)
			goto done;
		goto copy;
	}
	default:
		ret = -EBADMSG;
		goto done;
	}	/* end of switch(op) */

copy:
	if (copy_to_user(user, data, copylen))
		ret = -EFAULT;

done:
	vfree(data);
	if (ret > 0)
		ret = 0;
	return ret;
}

static struct nf_sockopt_ops so_set __read_mostly = {
	.pf		= PF_INET,
	.get_optmin	= SO_IP_SET,
	.get_optmax	= SO_IP_SET + 1,
	.get		= ip_set_sockfn_get,
	.owner		= THIS_MODULE,
};

static int __net_init
ip_set_net_init(struct net *net)
{
	struct ip_set_net *inst = ip_set_pernet(net);
	struct ip_set **list;

	inst->ip_set_max = max_sets ? max_sets : CONFIG_IP_SET_MAX;
	if (inst->ip_set_max >= IPSET_INVALID_ID)
		inst->ip_set_max = IPSET_INVALID_ID - 1;

	list = kvcalloc(inst->ip_set_max, sizeof(struct ip_set *), GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	inst->is_deleted = false;
	inst->is_destroyed = false;
	rcu_assign_pointer(inst->ip_set_list, list);
	return 0;
}

static void __net_exit
ip_set_net_pre_exit(struct net *net)
{
	struct ip_set_net *inst = ip_set_pernet(net);

	inst->is_deleted = true; /* flag for ip_set_nfnl_put */
}

static void __net_exit
ip_set_net_exit(struct net *net)
{
	struct ip_set_net *inst = ip_set_pernet(net);

	_destroy_all_sets(inst);
	kvfree(rcu_dereference_protected(inst->ip_set_list, 1));
}

static struct pernet_operations ip_set_net_ops = {
	.init	= ip_set_net_init,
	.pre_exit = ip_set_net_pre_exit,
	.exit   = ip_set_net_exit,
	.id	= &ip_set_net_id,
	.size	= sizeof(struct ip_set_net),
};

static int __init
ip_set_init(void)
{
	int ret = register_pernet_subsys(&ip_set_net_ops);

	if (ret) {
		pr_err("ip_set: cannot register pernet_subsys.\n");
		return ret;
	}

	ret = nfnetlink_subsys_register(&ip_set_netlink_subsys);
	if (ret != 0) {
		pr_err("ip_set: cannot register with nfnetlink.\n");
		unregister_pernet_subsys(&ip_set_net_ops);
		return ret;
	}

	ret = nf_register_sockopt(&so_set);
	if (ret != 0) {
		pr_err("SO_SET registry failed: %d\n", ret);
		nfnetlink_subsys_unregister(&ip_set_netlink_subsys);
		unregister_pernet_subsys(&ip_set_net_ops);
		return ret;
	}

	return 0;
}

static void __exit
ip_set_fini(void)
{
	nf_unregister_sockopt(&so_set);
	nfnetlink_subsys_unregister(&ip_set_netlink_subsys);
	unregister_pernet_subsys(&ip_set_net_ops);

	/* Wait for call_rcu() in destroy */
	rcu_barrier();

	pr_debug("these are the famous last words\n");
}

module_init(ip_set_init);
module_exit(ip_set_fini);

MODULE_DESCRIPTION("ip_set: protocol " __stringify(IPSET_PROTOCOL));
