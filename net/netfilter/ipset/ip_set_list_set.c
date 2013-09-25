/* Copyright (C) 2008-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the list:set type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/errno.h>

#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_list.h>

#define IPSET_TYPE_REV_MIN	0
/*				1    Counters support added */
#define IPSET_TYPE_REV_MAX	2 /* Comments support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("list:set", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_list:set");

/* Member elements  */
struct set_elem {
	ip_set_id_t id;
};

struct set_adt_elem {
	ip_set_id_t id;
	ip_set_id_t refid;
	int before;
};

/* Type structure */
struct list_set {
	u32 size;		/* size of set list array */
	struct timer_list gc;	/* garbage collection */
	struct set_elem members[0]; /* the set members */
};

#define list_set_elem(set, map, id)	\
	(struct set_elem *)((void *)(map)->members + (id) * (set)->dsize)

static int
list_set_ktest(struct ip_set *set, const struct sk_buff *skb,
	       const struct xt_action_param *par,
	       struct ip_set_adt_opt *opt, const struct ip_set_ext *ext)
{
	struct list_set *map = set->data;
	struct set_elem *e;
	u32 i, cmdflags = opt->cmdflags;
	int ret;

	/* Don't lookup sub-counters at all */
	opt->cmdflags &= ~IPSET_FLAG_MATCH_COUNTERS;
	if (opt->cmdflags & IPSET_FLAG_SKIP_SUBCOUNTER_UPDATE)
		opt->cmdflags &= ~IPSET_FLAG_SKIP_COUNTER_UPDATE;
	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			return 0;
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		ret = ip_set_test(e->id, skb, par, opt);
		if (ret > 0) {
			if (SET_WITH_COUNTER(set))
				ip_set_update_counter(ext_counter(e, set),
						      ext, &opt->ext,
						      cmdflags);
			return ret;
		}
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
	u32 i;
	int ret;

	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			return 0;
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
	u32 i;
	int ret;

	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			return 0;
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

	switch (adt) {
	case IPSET_TEST:
		return list_set_ktest(set, skb, par, opt, &ext);
	case IPSET_ADD:
		return list_set_kadd(set, skb, par, opt, &ext);
	case IPSET_DEL:
		return list_set_kdel(set, skb, par, opt, &ext);
	default:
		break;
	}
	return -EINVAL;
}

static bool
id_eq(const struct ip_set *set, u32 i, ip_set_id_t id)
{
	const struct list_set *map = set->data;
	const struct set_elem *e;

	if (i >= map->size)
		return 0;

	e = list_set_elem(set, map, i);
	return !!(e->id == id &&
		 !(SET_WITH_TIMEOUT(set) &&
		   ip_set_timeout_expired(ext_timeout(e, set))));
}

static int
list_set_add(struct ip_set *set, u32 i, struct set_adt_elem *d,
	     const struct ip_set_ext *ext)
{
	struct list_set *map = set->data;
	struct set_elem *e = list_set_elem(set, map, i);

	if (e->id != IPSET_INVALID_ID) {
		if (i == map->size - 1) {
			/* Last element replaced: e.g. add new,before,last */
			ip_set_put_byindex(e->id);
			ip_set_ext_destroy(set, e);
		} else {
			struct set_elem *x = list_set_elem(set, map,
							   map->size - 1);

			/* Last element pushed off */
			if (x->id != IPSET_INVALID_ID) {
				ip_set_put_byindex(x->id);
				ip_set_ext_destroy(set, x);
			}
			memmove(list_set_elem(set, map, i + 1), e,
				set->dsize * (map->size - (i + 1)));
			/* Extensions must be initialized to zero */
			memset(e, 0, set->dsize);
		}
	}

	e->id = d->id;
	if (SET_WITH_TIMEOUT(set))
		ip_set_timeout_set(ext_timeout(e, set), ext->timeout);
	if (SET_WITH_COUNTER(set))
		ip_set_init_counter(ext_counter(e, set), ext);
	if (SET_WITH_COMMENT(set))
		ip_set_init_comment(ext_comment(e, set), ext);
	return 0;
}

static int
list_set_del(struct ip_set *set, u32 i)
{
	struct list_set *map = set->data;
	struct set_elem *e = list_set_elem(set, map, i);

	ip_set_put_byindex(e->id);
	ip_set_ext_destroy(set, e);

	if (i < map->size - 1)
		memmove(e, list_set_elem(set, map, i + 1),
			set->dsize * (map->size - (i + 1)));

	/* Last element */
	e = list_set_elem(set, map, map->size - 1);
	e->id = IPSET_INVALID_ID;
	return 0;
}

static void
set_cleanup_entries(struct ip_set *set)
{
	struct list_set *map = set->data;
	struct set_elem *e;
	u32 i = 0;

	while (i < map->size) {
		e = list_set_elem(set, map, i);
		if (e->id != IPSET_INVALID_ID &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			list_set_del(set, i);
			/* Check element moved to position i in next loop */
		else
			i++;
	}
}

static int
list_set_utest(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	       struct ip_set_ext *mext, u32 flags)
{
	struct list_set *map = set->data;
	struct set_adt_elem *d = value;
	struct set_elem *e;
	u32 i;
	int ret;

	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			return 0;
		else if (SET_WITH_TIMEOUT(set) &&
			 ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		else if (e->id != d->id)
			continue;

		if (d->before == 0)
			return 1;
		else if (d->before > 0)
			ret = id_eq(set, i + 1, d->refid);
		else
			ret = i > 0 && id_eq(set, i - 1, d->refid);
		return ret;
	}
	return 0;
}


static int
list_set_uadd(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	      struct ip_set_ext *mext, u32 flags)
{
	struct list_set *map = set->data;
	struct set_adt_elem *d = value;
	struct set_elem *e;
	bool flag_exist = flags & IPSET_FLAG_EXIST;
	u32 i, ret = 0;

	if (SET_WITH_TIMEOUT(set))
		set_cleanup_entries(set);

	/* Check already added element */
	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			goto insert;
		else if (e->id != d->id)
			continue;

		if ((d->before > 1 && !id_eq(set, i + 1, d->refid)) ||
		    (d->before < 0 &&
		     (i == 0 || !id_eq(set, i - 1, d->refid))))
			/* Before/after doesn't match */
			return -IPSET_ERR_REF_EXIST;
		if (!flag_exist)
			/* Can't re-add */
			return -IPSET_ERR_EXIST;
		/* Update extensions */
		ip_set_ext_destroy(set, e);

		if (SET_WITH_TIMEOUT(set))
			ip_set_timeout_set(ext_timeout(e, set), ext->timeout);
		if (SET_WITH_COUNTER(set))
			ip_set_init_counter(ext_counter(e, set), ext);
		if (SET_WITH_COMMENT(set))
			ip_set_init_comment(ext_comment(e, set), ext);
		/* Set is already added to the list */
		ip_set_put_byindex(d->id);
		return 0;
	}
insert:
	ret = -IPSET_ERR_LIST_FULL;
	for (i = 0; i < map->size && ret == -IPSET_ERR_LIST_FULL; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			ret = d->before != 0 ? -IPSET_ERR_REF_EXIST
				: list_set_add(set, i, d, ext);
		else if (e->id != d->refid)
			continue;
		else if (d->before > 0)
			ret = list_set_add(set, i, d, ext);
		else if (i + 1 < map->size)
			ret = list_set_add(set, i + 1, d, ext);
	}

	return ret;
}

static int
list_set_udel(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	      struct ip_set_ext *mext, u32 flags)
{
	struct list_set *map = set->data;
	struct set_adt_elem *d = value;
	struct set_elem *e;
	u32 i;

	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			return d->before != 0 ? -IPSET_ERR_REF_EXIST
					      : -IPSET_ERR_EXIST;
		else if (SET_WITH_TIMEOUT(set) &&
			 ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		else if (e->id != d->id)
			continue;

		if (d->before == 0)
			return list_set_del(set, i);
		else if (d->before > 0) {
			if (!id_eq(set, i + 1, d->refid))
				return -IPSET_ERR_REF_EXIST;
			return list_set_del(set, i);
		} else if (i == 0 || !id_eq(set, i - 1, d->refid))
			return -IPSET_ERR_REF_EXIST;
		else
			return list_set_del(set, i);
	}
	return -IPSET_ERR_EXIST;
}

static int
list_set_uadt(struct ip_set *set, struct nlattr *tb[],
	      enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct set_adt_elem e = { .refid = IPSET_INVALID_ID };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	struct ip_set *s;
	int ret = 0;

	if (unlikely(!tb[IPSET_ATTR_NAME] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;
	e.id = ip_set_get_byname(nla_data(tb[IPSET_ATTR_NAME]), &s);
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
		e.refid = ip_set_get_byname(nla_data(tb[IPSET_ATTR_NAMEREF]),
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
		ip_set_put_byindex(e.refid);
	if (adt != IPSET_ADD || ret)
		ip_set_put_byindex(e.id);

	return ip_set_eexist(ret, flags) ? 0 : ret;
}

static void
list_set_flush(struct ip_set *set)
{
	struct list_set *map = set->data;
	struct set_elem *e;
	u32 i;

	for (i = 0; i < map->size; i++) {
		e = list_set_elem(set, map, i);
		if (e->id != IPSET_INVALID_ID) {
			ip_set_put_byindex(e->id);
			ip_set_ext_destroy(set, e);
			e->id = IPSET_INVALID_ID;
		}
	}
}

static void
list_set_destroy(struct ip_set *set)
{
	struct list_set *map = set->data;

	if (SET_WITH_TIMEOUT(set))
		del_timer_sync(&map->gc);
	list_set_flush(set);
	kfree(map);

	set->data = NULL;
}

static int
list_set_head(struct ip_set *set, struct sk_buff *skb)
{
	const struct list_set *map = set->data;
	struct nlattr *nested;

	nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
	if (!nested)
		goto nla_put_failure;
	if (nla_put_net32(skb, IPSET_ATTR_SIZE, htonl(map->size)) ||
	    nla_put_net32(skb, IPSET_ATTR_REFERENCES, htonl(set->ref - 1)) ||
	    nla_put_net32(skb, IPSET_ATTR_MEMSIZE,
			  htonl(sizeof(*map) + map->size * set->dsize)))
		goto nla_put_failure;
	if (unlikely(ip_set_put_flags(skb, set)))
		goto nla_put_failure;
	ipset_nest_end(skb, nested);

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
	u32 i, first = cb->args[2];
	const struct set_elem *e;

	atd = ipset_nest_start(skb, IPSET_ATTR_ADT);
	if (!atd)
		return -EMSGSIZE;
	for (; cb->args[2] < map->size; cb->args[2]++) {
		i = cb->args[2];
		e = list_set_elem(set, map, i);
		if (e->id == IPSET_INVALID_ID)
			goto finish;
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(e, set)))
			continue;
		nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
		if (!nested) {
			if (i == first) {
				nla_nest_cancel(skb, atd);
				return -EMSGSIZE;
			} else
				goto nla_put_failure;
		}
		if (nla_put_string(skb, IPSET_ATTR_NAME,
				   ip_set_name_byindex(e->id)))
			goto nla_put_failure;
		if (ip_set_put_extensions(skb, set, e, true))
			goto nla_put_failure;
		ipset_nest_end(skb, nested);
	}
finish:
	ipset_nest_end(skb, atd);
	/* Set listing finished */
	cb->args[2] = 0;
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nested);
	if (unlikely(i == first)) {
		cb->args[2] = 0;
		return -EMSGSIZE;
	}
	ipset_nest_end(skb, atd);
	return 0;
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
list_set_gc(unsigned long ul_set)
{
	struct ip_set *set = (struct ip_set *) ul_set;
	struct list_set *map = set->data;

	write_lock_bh(&set->lock);
	set_cleanup_entries(set);
	write_unlock_bh(&set->lock);

	map->gc.expires = jiffies + IPSET_GC_PERIOD(set->timeout) * HZ;
	add_timer(&map->gc);
}

static void
list_set_gc_init(struct ip_set *set, void (*gc)(unsigned long ul_set))
{
	struct list_set *map = set->data;

	init_timer(&map->gc);
	map->gc.data = (unsigned long) set;
	map->gc.function = gc;
	map->gc.expires = jiffies + IPSET_GC_PERIOD(set->timeout) * HZ;
	add_timer(&map->gc);
}

/* Create list:set type of sets */

static bool
init_list_set(struct ip_set *set, u32 size)
{
	struct list_set *map;
	struct set_elem *e;
	u32 i;

	map = kzalloc(sizeof(*map) + size * set->dsize, GFP_KERNEL);
	if (!map)
		return false;

	map->size = size;
	set->data = map;

	for (i = 0; i < size; i++) {
		e = list_set_elem(set, map, i);
		e->id = IPSET_INVALID_ID;
	}

	return true;
}

static int
list_set_create(struct ip_set *set, struct nlattr *tb[], u32 flags)
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
	set->dsize = ip_set_elem_len(set, tb, sizeof(struct set_elem));
	if (!init_list_set(set, size))
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
		[IPSET_ATTR_COMMENT]	= { .type = NLA_NUL_STRING },
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
	ip_set_type_unregister(&list_set_type);
}

module_init(list_set_init);
module_exit(list_set_fini);
