// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2008-2013 Jozsef Kadlecsik <kadlec@netfilter.org> */

/* Kernel module implementing an IP set type: the list:set type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/errno.h>

#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_list.h>

#define IPSET_TYPE_REV_MIN	0
/*				1    Counters support added */
/*				2    Comments support added */
#define IPSET_TYPE_REV_MAX	3 /* skbinfo support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@netfilter.org>");
IP_SET_MODULE_DESC("list:set", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_list:set");

/* Member elements  */
struct set_elem {
	struct rcu_head rcu;
	struct list_head list;
	struct ip_set *set;	/* Sigh, in order to cleanup reference */
	ip_set_id_t id;
} __aligned(__alignof__(u64));

struct set_adt_elem {
	ip_set_id_t id;
	ip_set_id_t refid;
	int before;
};

/* Type structure */
struct list_set {
	u32 size;		/* size of set list array */
	struct timer_list gc;	/* garbage collection */
	struct ip_set *set;	/* attached to this ip_set */
	struct net *net;	/* namespace */
	struct list_head members; /* the set members */
};

static int
list_set_ktest(struct ip_set *set, const struct sk_buff *skb,
	       const struct xt_action_param *par,
	       struct ip_set_adt_opt *opt, const struct ip_set_ext *ext)
{
	struct list_set *map = set->data;
	struct ip_set_ext *mext = &opt->ext;
	struct set_elem *e;
	u32 flags = opt->cmdflags;
	int ret;

	/* Don't lookup sub-counters at all */
	opt->cmdflags &= ~IPSET_FLAG_MATCH_COUNTERS;
	if (opt->cmdflags & IPSET_FLAG_SKIP_SUBCOUNTER_UPDATE)
		opt->cmdflags &= ~IPSET_FLAG_SKIP_COUNTER_UPDATE;
	list_for_each_entry_rcu(e, &map->members, list) {
		ret = ip_set_test(e->id, skb, par, opt);
		if (ret <= 0)
			continue;
		if (ip_set_match_extensions(set, ext, mext, flags, e))
			return 1;
	}
	return 0;
}

static int
list_set_kadd(struct ip_set *set, const struct sk_buff *skb,
	      const struct xt_action_param *par,
	      struct ip_set_adt_opt *opt, const struct ip_set_ext *ext)
{
	struct list_set *map = set->data;
	struct set_elem *e;
	int ret;

	list_for_each_entry(e, &map->members, list) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		ret = ip_set_add(e->id, skb, par, opt);
		if (ret == 0)
			return ret;
	}
	return 0;
}

static int
list_set_kdel(struct ip_set *set, const struct sk_buff *skb,
	      const struct xt_action_param *par,
	      struct ip_set_adt_opt *opt, const struct ip_set_ext *ext)
{
	struct list_set *map = set->data;
	struct set_elem *e;
	int ret;

	list_for_each_entry(e, &map->members, list) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		ret = ip_set_del(e->id, skb, par, opt);
		if (ret == 0)
			return ret;
	}
	return 0;
}

static int
list_set_kadt(struct ip_set *set, const struct sk_buff *skb,
	      const struct xt_action_param *par,
	      enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);
	int ret = -EINVAL;

	rcu_read_lock();
	switch (adt) {
	case IPSET_TEST:
		ret = list_set_ktest(set, skb, par, opt, &ext);
		break;
	case IPSET_ADD:
		ret = list_set_kadd(set, skb, par, opt, &ext);
		break;
	case IPSET_DEL:
		ret = list_set_kdel(set, skb, par, opt, &ext);
		break;
	default:
		break;
	}
	rcu_read_unlock();

	return ret;
}

/* Userspace interfaces: we are protected by the nfnl mutex */

static void
__list_set_del_rcu(struct rcu_head * rcu)
{
	struct set_elem *e = container_of(rcu, struct set_elem, rcu);
	struct ip_set *set = e->set;

	ip_set_ext_destroy(set, e);
	kfree(e);
}

static void
list_set_del(struct ip_set *set, struct set_elem *e)
{
	struct list_set *map = set->data;

	set->elements--;
	list_del_rcu(&e->list);
	ip_set_put_byindex(map->net, e->id);
	call_rcu(&e->rcu, __list_set_del_rcu);
}

static void
list_set_replace(struct ip_set *set, struct set_elem *e, struct set_elem *old)
{
	struct list_set *map = set->data;

	list_replace_rcu(&old->list, &e->list);
	ip_set_put_byindex(map->net, old->id);
	call_rcu(&old->rcu, __list_set_del_rcu);
}

static void
set_cleanup_entries(struct ip_set *set)
{
	struct list_set *map = set->data;
	struct set_elem *e, *n;

	list_for_each_entry_safe(e, n, &map->members, list)
		if (ip_set_timeout_expired(ext_timeout(e, set)))
			list_set_del(set, e);
}

static int
list_set_utest(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	       struct ip_set_ext *mext, u32 flags)
{
	struct list_set *map = set->data;
	struct set_adt_elem *d = value;
	struct set_elem *e, *next, *prev = NULL;
	int ret;

	list_for_each_entry(e, &map->members, list) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		else if (e->id != d->id) {
			prev = e;
			continue;
		}

		if (d->before == 0) {
			ret = 1;
		} else if (d->before > 0) {
			next = list_next_entry(e, list);
			ret = !list_is_last(&e->list, &map->members) &&
			      next->id == d->refid;
		} else {
			ret = prev && prev->id == d->refid;
		}
		return ret;
	}
	return 0;
}

static void
list_set_init_extensions(struct ip_set *set, const struct ip_set_ext *ext,
			 struct set_elem *e)
{
	if (SET_WITH_COUNTER(set))
		ip_set_init_counter(ext_counter(e, set), ext);
	if (SET_WITH_COMMENT(set))
		ip_set_init_comment(set, ext_comment(e, set), ext);
	if (SET_WITH_SKBINFO(set))
		ip_set_init_skbinfo(ext_skbinfo(e, set), ext);
	/* Update timeout last */
	if (SET_WITH_TIMEOUT(set))
		ip_set_timeout_set(ext_timeout(e, set), ext->timeout);
}

static int
list_set_uadd(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	      struct ip_set_ext *mext, u32 flags)
{
	struct list_set *map = set->data;
	struct set_adt_elem *d = value;
	struct set_elem *e, *n, *prev, *next;
	bool flag_exist = flags & IPSET_FLAG_EXIST;

	/* Find where to add the new entry */
	n = prev = next = NULL;
	list_for_each_entry(e, &map->members, list) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		else if (d->id == e->id)
			n = e;
		else if (d->before == 0 || e->id != d->refid)
			continue;
		else if (d->before > 0)
			next = e;
		else
			prev = e;
	}

	/* If before/after is used on an empty set */
	if ((d->before > 0 && !next) ||
	    (d->before < 0 && !prev))
		return -IPSET_ERR_REF_EXIST;

	/* Re-add already existing element */
	if (n) {
		if (!flag_exist)
			return -IPSET_ERR_EXIST;
		/* Update extensions */
		ip_set_ext_destroy(set, n);
		list_set_init_extensions(set, ext, n);

		/* Set is already added to the list */
		ip_set_put_byindex(map->net, d->id);
		return 0;
	}
	/* Add new entry */
	if (d->before == 0) {
		/* Append  */
		n = list_empty(&map->members) ? NULL :
		    list_last_entry(&map->members, struct set_elem, list);
	} else if (d->before > 0) {
		/* Insert after next element */
		if (!list_is_last(&next->list, &map->members))
			n = list_next_entry(next, list);
	} else {
		/* Insert before prev element */
		if (prev->list.prev != &map->members)
			n = list_prev_entry(prev, list);
	}
	/* Can we replace a timed out entry? */
	if (n &&
	    !(SET_WITH_TIMEOUT(set) &&
	      ip_set_timeout_expired(ext_timeout(n, set))))
		n = NULL;

	e = kzalloc(set->dsize, GFP_ATOMIC);
	if (!e)
		return -ENOMEM;
	e->id = d->id;
	e->set = set;
	INIT_LIST_HEAD(&e->list);
	list_set_init_extensions(set, ext, e);
	if (n)
		list_set_replace(set, e, n);
	else if (next)
		list_add_tail_rcu(&e->list, &next->list);
	else if (prev)
		list_add_rcu(&e->list, &prev->list);
	else
		list_add_tail_rcu(&e->list, &map->members);
	set->elements++;

	return 0;
}

static int
list_set_udel(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	      struct ip_set_ext *mext, u32 flags)
{
	struct list_set *map = set->data;
	struct set_adt_elem *d = value;
	struct set_elem *e, *next, *prev = NULL;

	list_for_each_entry(e, &map->members, list) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		else if (e->id != d->id) {
			prev = e;
			continue;
		}

		if (d->before > 0) {
			next = list_next_entry(e, list);
			if (list_is_last(&e->list, &map->members) ||
			    next->id != d->refid)
				return -IPSET_ERR_REF_EXIST;
		} else if (d->before < 0) {
			if (!prev || prev->id != d->refid)
				return -IPSET_ERR_REF_EXIST;
		}
		list_set_del(set, e);
		return 0;
	}
	return d->before != 0 ? -IPSET_ERR_REF_EXIST : -IPSET_ERR_EXIST;
}

static int
list_set_uadt(struct ip_set *set, struct nlattr *tb[],
	      enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	struct list_set *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct set_adt_elem e = { .refid = IPSET_INVALID_ID };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	struct ip_set *s;
	int ret = 0;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	if (unlikely(!tb[IPSET_ATTR_NAME] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;
	e.id = ip_set_get_byname(map->net, nla_data(tb[IPSET_ATTR_NAME]), &s);
	if (e.id == IPSET_INVALID_ID)
		return -IPSET_ERR_NAME;
	/* "Loop detection" */
	if (s->type->features & IPSET_TYPE_NAME) {
		ret = -IPSET_ERR_LOOP;
		goto finish;
	}

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 f = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);

		e.before = f & IPSET_FLAG_BEFORE;
	}

	if (e.before && !tb[IPSET_ATTR_NAMEREF]) {
		ret = -IPSET_ERR_BEFORE;
		goto finish;
	}

	if (tb[IPSET_ATTR_NAMEREF]) {
		e.refid = ip_set_get_byname(map->net,
					    nla_data(tb[IPSET_ATTR_NAMEREF]),
					    &s);
		if (e.refid == IPSET_INVALID_ID) {
			ret = -IPSET_ERR_NAMEREF;
			goto finish;
		}
		if (!e.before)
			e.before = -1;
	}
	if (adt != IPSET_TEST && SET_WITH_TIMEOUT(set))
		set_cleanup_entries(set);

	ret = adtfn(set, &e, &ext, &ext, flags);

finish:
	if (e.refid != IPSET_INVALID_ID)
		ip_set_put_byindex(map->net, e.refid);
	if (adt != IPSET_ADD || ret)
		ip_set_put_byindex(map->net, e.id);

	return ip_set_eexist(ret, flags) ? 0 : ret;
}

static void
list_set_flush(struct ip_set *set)
{
	struct list_set *map = set->data;
	struct set_elem *e, *n;

	list_for_each_entry_safe(e, n, &map->members, list)
		list_set_del(set, e);
	set->elements = 0;
	set->ext_size = 0;
}

static void
list_set_destroy(struct ip_set *set)
{
	struct list_set *map = set->data;
	struct set_elem *e, *n;

	if (SET_WITH_TIMEOUT(set))
		del_timer_sync(&map->gc);

	list_for_each_entry_safe(e, n, &map->members, list) {
		list_del(&e->list);
		ip_set_put_byindex(map->net, e->id);
		ip_set_ext_destroy(set, e);
		kfree(e);
	}
	kfree(map);

	set->data = NULL;
}

/* Calculate the actual memory size of the set data */
static size_t
list_set_memsize(const struct list_set *map, size_t dsize)
{
	struct set_elem *e;
	u32 n = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &map->members, list)
		n++;
	rcu_read_unlock();

	return (sizeof(*map) + n * dsize);
}

static int
list_set_head(struct ip_set *set, struct sk_buff *skb)
{
	const struct list_set *map = set->data;
	struct nlattr *nested;
	size_t memsize = list_set_memsize(map, set->dsize) + set->ext_size;

	nested = nla_nest_start(skb, IPSET_ATTR_DATA);
	if (!nested)
		goto nla_put_failure;
	if (nla_put_net32(skb, IPSET_ATTR_SIZE, htonl(map->size)) ||
	    nla_put_net32(skb, IPSET_ATTR_REFERENCES, htonl(set->ref)) ||
	    nla_put_net32(skb, IPSET_ATTR_MEMSIZE, htonl(memsize)) ||
	    nla_put_net32(skb, IPSET_ATTR_ELEMENTS, htonl(set->elements)))
		goto nla_put_failure;
	if (unlikely(ip_set_put_flags(skb, set)))
		goto nla_put_failure;
	nla_nest_end(skb, nested);

	return 0;
nla_put_failure:
	return -EMSGSIZE;
}

static int
list_set_list(const struct ip_set *set,
	      struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct list_set *map = set->data;
	struct nlattr *atd, *nested;
	u32 i = 0, first = cb->args[IPSET_CB_ARG0];
	char name[IPSET_MAXNAMELEN];
	struct set_elem *e;
	int ret = 0;

	atd = nla_nest_start(skb, IPSET_ATTR_ADT);
	if (!atd)
		return -EMSGSIZE;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &map->members, list) {
		if (i < first ||
		    (SET_WITH_TIMEOUT(set) &&
		     ip_set_timeout_expired(ext_timeout(e, set)))) {
			i++;
			continue;
		}
		nested = nla_nest_start(skb, IPSET_ATTR_DATA);
		if (!nested)
			goto nla_put_failure;
		ip_set_name_byindex(map->net, e->id, name);
		if (nla_put_string(skb, IPSET_ATTR_NAME, name))
			goto nla_put_failure;
		if (ip_set_put_extensions(skb, set, e, true))
			goto nla_put_failure;
		nla_nest_end(skb, nested);
		i++;
	}

	nla_nest_end(skb, atd);
	/* Set listing finished */
	cb->args[IPSET_CB_ARG0] = 0;
	goto out;

nla_put_failure:
	nla_nest_cancel(skb, nested);
	if (unlikely(i == first)) {
		nla_nest_cancel(skb, atd);
		cb->args[IPSET_CB_ARG0] = 0;
		ret = -EMSGSIZE;
	} else {
		cb->args[IPSET_CB_ARG0] = i;
		nla_nest_end(skb, atd);
	}
out:
	rcu_read_unlock();
	return ret;
}

static bool
list_set_same_set(const struct ip_set *a, const struct ip_set *b)
{
	const struct list_set *x = a->data;
	const struct list_set *y = b->data;

	return x->size == y->size &&
	       a->timeout == b->timeout &&
	       a->extensions == b->extensions;
}

static const struct ip_set_type_variant set_variant = {
	.kadt	= list_set_kadt,
	.uadt	= list_set_uadt,
	.adt	= {
		[IPSET_ADD] = list_set_uadd,
		[IPSET_DEL] = list_set_udel,
		[IPSET_TEST] = list_set_utest,
	},
	.destroy = list_set_destroy,
	.flush	= list_set_flush,
	.head	= list_set_head,
	.list	= list_set_list,
	.same_set = list_set_same_set,
};

static void
list_set_gc(struct timer_list *t)
{
	struct list_set *map = from_timer(map, t, gc);
	struct ip_set *set = map->set;

	spin_lock_bh(&set->lock);
	set_cleanup_entries(set);
	spin_unlock_bh(&set->lock);

	map->gc.expires = jiffies + IPSET_GC_PERIOD(set->timeout) * HZ;
	add_timer(&map->gc);
}

static void
list_set_gc_init(struct ip_set *set, void (*gc)(struct timer_list *t))
{
	struct list_set *map = set->data;

	timer_setup(&map->gc, gc, 0);
	mod_timer(&map->gc, jiffies + IPSET_GC_PERIOD(set->timeout) * HZ);
}

/* Create list:set type of sets */

static bool
init_list_set(struct net *net, struct ip_set *set, u32 size)
{
	struct list_set *map;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return false;

	map->size = size;
	map->net = net;
	map->set = set;
	INIT_LIST_HEAD(&map->members);
	set->data = map;

	return true;
}

static int
list_set_create(struct net *net, struct ip_set *set, struct nlattr *tb[],
		u32 flags)
{
	u32 size = IP_SET_LIST_DEFAULT_SIZE;

	if (unlikely(!ip_set_optattr_netorder(tb, IPSET_ATTR_SIZE) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_SIZE])
		size = ip_set_get_h32(tb[IPSET_ATTR_SIZE]);
	if (size < IP_SET_LIST_MIN_SIZE)
		size = IP_SET_LIST_MIN_SIZE;

	set->variant = &set_variant;
	set->dsize = ip_set_elem_len(set, tb, sizeof(struct set_elem),
				     __alignof__(struct set_elem));
	if (!init_list_set(net, set, size))
		return -ENOMEM;
	if (tb[IPSET_ATTR_TIMEOUT]) {
		set->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
		list_set_gc_init(set, list_set_gc);
	}
	return 0;
}

static struct ip_set_type list_set_type __read_mostly = {
	.name		= "list:set",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_NAME | IPSET_DUMP_LAST,
	.dimension	= IPSET_DIM_ONE,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= list_set_create,
	.create_policy	= {
		[IPSET_ATTR_SIZE]	= { .type = NLA_U32 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
		[IPSET_ATTR_NAME]	= { .type = NLA_STRING,
					    .len = IPSET_MAXNAMELEN },
		[IPSET_ATTR_NAMEREF]	= { .type = NLA_STRING,
					    .len = IPSET_MAXNAMELEN },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
		[IPSET_ATTR_BYTES]	= { .type = NLA_U64 },
		[IPSET_ATTR_PACKETS]	= { .type = NLA_U64 },
		[IPSET_ATTR_COMMENT]	= { .type = NLA_NUL_STRING,
					    .len  = IPSET_MAX_COMMENT_SIZE },
		[IPSET_ATTR_SKBMARK]	= { .type = NLA_U64 },
		[IPSET_ATTR_SKBPRIO]	= { .type = NLA_U32 },
		[IPSET_ATTR_SKBQUEUE]	= { .type = NLA_U16 },
	},
	.me		= THIS_MODULE,
};

static int __init
list_set_init(void)
{
	return ip_set_type_register(&list_set_type);
}

static void __exit
list_set_fini(void)
{
	rcu_barrier();
	ip_set_type_unregister(&list_set_type);
}

module_init(list_set_init);
module_exit(list_set_fini);
