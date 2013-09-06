/* Copyright (C) 2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __IP_SET_BITMAP_IP_GEN_H
#define __IP_SET_BITMAP_IP_GEN_H

#define mtype_do_test		IPSET_TOKEN(MTYPE, _do_test)
#define mtype_gc_test		IPSET_TOKEN(MTYPE, _gc_test)
#define mtype_is_filled		IPSET_TOKEN(MTYPE, _is_filled)
#define mtype_do_add		IPSET_TOKEN(MTYPE, _do_add)
#define mtype_do_del		IPSET_TOKEN(MTYPE, _do_del)
#define mtype_do_list		IPSET_TOKEN(MTYPE, _do_list)
#define mtype_do_head		IPSET_TOKEN(MTYPE, _do_head)
#define mtype_adt_elem		IPSET_TOKEN(MTYPE, _adt_elem)
#define mtype_add_timeout	IPSET_TOKEN(MTYPE, _add_timeout)
#define mtype_gc_init		IPSET_TOKEN(MTYPE, _gc_init)
#define mtype_kadt		IPSET_TOKEN(MTYPE, _kadt)
#define mtype_uadt		IPSET_TOKEN(MTYPE, _uadt)
#define mtype_destroy		IPSET_TOKEN(MTYPE, _destroy)
#define mtype_flush		IPSET_TOKEN(MTYPE, _flush)
#define mtype_head		IPSET_TOKEN(MTYPE, _head)
#define mtype_same_set		IPSET_TOKEN(MTYPE, _same_set)
#define mtype_elem		IPSET_TOKEN(MTYPE, _elem)
#define mtype_test		IPSET_TOKEN(MTYPE, _test)
#define mtype_add		IPSET_TOKEN(MTYPE, _add)
#define mtype_del		IPSET_TOKEN(MTYPE, _del)
#define mtype_list		IPSET_TOKEN(MTYPE, _list)
#define mtype_gc		IPSET_TOKEN(MTYPE, _gc)
#define mtype			MTYPE

#define ext_timeout(e, m)	\
	(unsigned long *)((e) + (m)->offset[IPSET_EXT_ID_TIMEOUT])
#define ext_counter(e, m)	\
	(struct ip_set_counter *)((e) + (m)->offset[IPSET_EXT_ID_COUNTER])
#define get_ext(map, id)	((map)->extensions + (map)->dsize * (id))

static void
mtype_gc_init(struct ip_set *set, void (*gc)(unsigned long ul_set))
{
	struct mtype *map = set->data;

	init_timer(&map->gc);
	map->gc.data = (unsigned long) set;
	map->gc.function = gc;
	map->gc.expires = jiffies + IPSET_GC_PERIOD(map->timeout) * HZ;
	add_timer(&map->gc);
}

static void
mtype_destroy(struct ip_set *set)
{
	struct mtype *map = set->data;

	if (SET_WITH_TIMEOUT(set))
		del_timer_sync(&map->gc);

	ip_set_free(map->members);
	if (map->dsize)
		ip_set_free(map->extensions);
	kfree(map);

	set->data = NULL;
}

static void
mtype_flush(struct ip_set *set)
{
	struct mtype *map = set->data;

	memset(map->members, 0, map->memsize);
}

static int
mtype_head(struct ip_set *set, struct sk_buff *skb)
{
	const struct mtype *map = set->data;
	struct nlattr *nested;

	nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
	if (!nested)
		goto nla_put_failure;
	if (mtype_do_head(skb, map) ||
	    nla_put_net32(skb, IPSET_ATTR_REFERENCES, htonl(set->ref - 1)) ||
	    nla_put_net32(skb, IPSET_ATTR_MEMSIZE,
			  htonl(sizeof(*map) +
				map->memsize +
				map->dsize * map->elements)) ||
	    (SET_WITH_TIMEOUT(set) &&
	     nla_put_net32(skb, IPSET_ATTR_TIMEOUT, htonl(map->timeout))) ||
	    (SET_WITH_COUNTER(set) &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS,
			   htonl(IPSET_FLAG_WITH_COUNTERS))))
		goto nla_put_failure;
	ipset_nest_end(skb, nested);

	return 0;
nla_put_failure:
	return -EMSGSIZE;
}

static int
mtype_test(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	   struct ip_set_ext *mext, u32 flags)
{
	struct mtype *map = set->data;
	const struct mtype_adt_elem *e = value;
	void *x = get_ext(map, e->id);
	int ret = mtype_do_test(e, map);

	if (ret <= 0)
		return ret;
	if (SET_WITH_TIMEOUT(set) &&
	    ip_set_timeout_expired(ext_timeout(x, map)))
		return 0;
	if (SET_WITH_COUNTER(set))
		ip_set_update_counter(ext_counter(x, map), ext, mext, flags);
	return 1;
}

static int
mtype_add(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	  struct ip_set_ext *mext, u32 flags)
{
	struct mtype *map = set->data;
	const struct mtype_adt_elem *e = value;
	void *x = get_ext(map, e->id);
	int ret = mtype_do_add(e, map, flags);

	if (ret == IPSET_ADD_FAILED) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(x, map)))
			ret = 0;
		else if (!(flags & IPSET_FLAG_EXIST))
			return -IPSET_ERR_EXIST;
	}

	if (SET_WITH_TIMEOUT(set))
#ifdef IP_SET_BITMAP_STORED_TIMEOUT
		mtype_add_timeout(ext_timeout(x, map), e, ext, map, ret);
#else
		ip_set_timeout_set(ext_timeout(x, map), ext->timeout);
#endif

	if (SET_WITH_COUNTER(set))
		ip_set_init_counter(ext_counter(x, map), ext);
	return 0;
}

static int
mtype_del(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	  struct ip_set_ext *mext, u32 flags)
{
	struct mtype *map = set->data;
	const struct mtype_adt_elem *e = value;
	const void *x = get_ext(map, e->id);

	if (mtype_do_del(e, map) ||
	    (SET_WITH_TIMEOUT(set) &&
	     ip_set_timeout_expired(ext_timeout(x, map))))
		return -IPSET_ERR_EXIST;

	return 0;
}

static int
mtype_list(const struct ip_set *set,
	   struct sk_buff *skb, struct netlink_callback *cb)
{
	struct mtype *map = set->data;
	struct nlattr *adt, *nested;
	void *x;
	u32 id, first = cb->args[2];

	adt = ipset_nest_start(skb, IPSET_ATTR_ADT);
	if (!adt)
		return -EMSGSIZE;
	for (; cb->args[2] < map->elements; cb->args[2]++) {
		id = cb->args[2];
		x = get_ext(map, id);
		if (!test_bit(id, map->members) ||
		    (SET_WITH_TIMEOUT(set) &&
#ifdef IP_SET_BITMAP_STORED_TIMEOUT
		     mtype_is_filled((const struct mtype_elem *) x) &&
#endif
		     ip_set_timeout_expired(ext_timeout(x, map))))
			continue;
		nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
		if (!nested) {
			if (id == first) {
				nla_nest_cancel(skb, adt);
				return -EMSGSIZE;
			} else
				goto nla_put_failure;
		}
		if (mtype_do_list(skb, map, id))
			goto nla_put_failure;
		if (SET_WITH_TIMEOUT(set)) {
#ifdef IP_SET_BITMAP_STORED_TIMEOUT
			if (nla_put_net32(skb, IPSET_ATTR_TIMEOUT,
					  htonl(ip_set_timeout_stored(map, id,
							ext_timeout(x, map)))))
				goto nla_put_failure;
#else
			if (nla_put_net32(skb, IPSET_ATTR_TIMEOUT,
					  htonl(ip_set_timeout_get(
							ext_timeout(x, map)))))
				goto nla_put_failure;
#endif
		}
		if (SET_WITH_COUNTER(set) &&
		    ip_set_put_counter(skb, ext_counter(x, map)))
			goto nla_put_failure;
		ipset_nest_end(skb, nested);
	}
	ipset_nest_end(skb, adt);

	/* Set listing finished */
	cb->args[2] = 0;

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nested);
	if (unlikely(id == first)) {
		cb->args[2] = 0;
		return -EMSGSIZE;
	}
	ipset_nest_end(skb, adt);
	return 0;
}

static void
mtype_gc(unsigned long ul_set)
{
	struct ip_set *set = (struct ip_set *) ul_set;
	struct mtype *map = set->data;
	const void *x;
	u32 id;

	/* We run parallel with other readers (test element)
	 * but adding/deleting new entries is locked out */
	read_lock_bh(&set->lock);
	for (id = 0; id < map->elements; id++)
		if (mtype_gc_test(id, map)) {
			x = get_ext(map, id);
			if (ip_set_timeout_expired(ext_timeout(x, map)))
				clear_bit(id, map->members);
		}
	read_unlock_bh(&set->lock);

	map->gc.expires = jiffies + IPSET_GC_PERIOD(map->timeout) * HZ;
	add_timer(&map->gc);
}

static const struct ip_set_type_variant mtype = {
	.kadt	= mtype_kadt,
	.uadt	= mtype_uadt,
	.adt	= {
		[IPSET_ADD] = mtype_add,
		[IPSET_DEL] = mtype_del,
		[IPSET_TEST] = mtype_test,
	},
	.destroy = mtype_destroy,
	.flush	= mtype_flush,
	.head	= mtype_head,
	.list	= mtype_list,
	.same_set = mtype_same_set,
};

#endif /* __IP_SET_BITMAP_IP_GEN_H */
