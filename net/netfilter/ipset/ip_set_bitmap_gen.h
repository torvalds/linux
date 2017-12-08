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
#define mtype_ext_cleanup	IPSET_TOKEN(MTYPE, _ext_cleanup)
#define mtype_do_del		IPSET_TOKEN(MTYPE, _do_del)
#define mtype_do_list		IPSET_TOKEN(MTYPE, _do_list)
#define mtype_do_head		IPSET_TOKEN(MTYPE, _do_head)
#define mtype_adt_elem		IPSET_TOKEN(MTYPE, _adt_elem)
#define mtype_add_timeout	IPSET_TOKEN(MTYPE, _add_timeout)
#define mtype_gc_init		IPSET_TOKEN(MTYPE, _gc_init)
#define mtype_kadt		IPSET_TOKEN(MTYPE, _kadt)
#define mtype_uadt		IPSET_TOKEN(MTYPE, _uadt)
#define mtype_destroy		IPSET_TOKEN(MTYPE, _destroy)
#define mtype_memsize		IPSET_TOKEN(MTYPE, _memsize)
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

#define get_ext(set, map, id)	((map)->extensions + ((set)->dsize * (id)))

static void
mtype_gc_init(struct ip_set *set, void (*gc)(struct timer_list *t))
{
	struct mtype *map = set->data;

	timer_setup(&map->gc, gc, 0);
	mod_timer(&map->gc, jiffies + IPSET_GC_PERIOD(set->timeout) * HZ);
}

static void
mtype_ext_cleanup(struct ip_set *set)
{
	struct mtype *map = set->data;
	u32 id;

	for (id = 0; id < map->elements; id++)
		if (test_bit(id, map->members))
			ip_set_ext_destroy(set, get_ext(set, map, id));
}

static void
mtype_destroy(struct ip_set *set)
{
	struct mtype *map = set->data;

	if (SET_WITH_TIMEOUT(set))
		del_timer_sync(&map->gc);

	ip_set_free(map->members);
	if (set->dsize && set->extensions & IPSET_EXT_DESTROY)
		mtype_ext_cleanup(set);
	ip_set_free(map);

	set->data = NULL;
}

static void
mtype_flush(struct ip_set *set)
{
	struct mtype *map = set->data;

	if (set->extensions & IPSET_EXT_DESTROY)
		mtype_ext_cleanup(set);
	memset(map->members, 0, map->memsize);
	set->elements = 0;
	set->ext_size = 0;
}

/* Calculate the actual memory size of the set data */
static size_t
mtype_memsize(const struct mtype *map, size_t dsize)
{
	return sizeof(*map) + map->memsize +
	       map->elements * dsize;
}

static int
mtype_head(struct ip_set *set, struct sk_buff *skb)
{
	const struct mtype *map = set->data;
	struct nlattr *nested;
	size_t memsize = mtype_memsize(map, set->dsize) + set->ext_size;

	nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
	if (!nested)
		goto nla_put_failure;
	if (mtype_do_head(skb, map) ||
	    nla_put_net32(skb, IPSET_ATTR_REFERENCES, htonl(set->ref)) ||
	    nla_put_net32(skb, IPSET_ATTR_MEMSIZE, htonl(memsize)) ||
	    nla_put_net32(skb, IPSET_ATTR_ELEMENTS, htonl(set->elements)))
		goto nla_put_failure;
	if (unlikely(ip_set_put_flags(skb, set)))
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
	void *x = get_ext(set, map, e->id);
	int ret = mtype_do_test(e, map, set->dsize);

	if (ret <= 0)
		return ret;
	if (SET_WITH_TIMEOUT(set) &&
	    ip_set_timeout_expired(ext_timeout(x, set)))
		return 0;
	if (SET_WITH_COUNTER(set))
		ip_set_update_counter(ext_counter(x, set), ext, mext, flags);
	if (SET_WITH_SKBINFO(set))
		ip_set_get_skbinfo(ext_skbinfo(x, set), ext, mext, flags);
	return 1;
}

static int
mtype_add(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	  struct ip_set_ext *mext, u32 flags)
{
	struct mtype *map = set->data;
	const struct mtype_adt_elem *e = value;
	void *x = get_ext(set, map, e->id);
	int ret = mtype_do_add(e, map, flags, set->dsize);

	if (ret == IPSET_ADD_FAILED) {
		if (SET_WITH_TIMEOUT(set) &&
		    ip_set_timeout_expired(ext_timeout(x, set))) {
			set->elements--;
			ret = 0;
		} else if (!(flags & IPSET_FLAG_EXIST)) {
			set_bit(e->id, map->members);
			return -IPSET_ERR_EXIST;
		}
		/* Element is re-added, cleanup extensions */
		ip_set_ext_destroy(set, x);
	}
	if (ret > 0)
		set->elements--;

	if (SET_WITH_TIMEOUT(set))
#ifdef IP_SET_BITMAP_STORED_TIMEOUT
		mtype_add_timeout(ext_timeout(x, set), e, ext, set, map, ret);
#else
		ip_set_timeout_set(ext_timeout(x, set), ext->timeout);
#endif

	if (SET_WITH_COUNTER(set))
		ip_set_init_counter(ext_counter(x, set), ext);
	if (SET_WITH_COMMENT(set))
		ip_set_init_comment(set, ext_comment(x, set), ext);
	if (SET_WITH_SKBINFO(set))
		ip_set_init_skbinfo(ext_skbinfo(x, set), ext);

	/* Activate element */
	set_bit(e->id, map->members);
	set->elements++;

	return 0;
}

static int
mtype_del(struct ip_set *set, void *value, const struct ip_set_ext *ext,
	  struct ip_set_ext *mext, u32 flags)
{
	struct mtype *map = set->data;
	const struct mtype_adt_elem *e = value;
	void *x = get_ext(set, map, e->id);

	if (mtype_do_del(e, map))
		return -IPSET_ERR_EXIST;

	ip_set_ext_destroy(set, x);
	set->elements--;
	if (SET_WITH_TIMEOUT(set) &&
	    ip_set_timeout_expired(ext_timeout(x, set)))
		return -IPSET_ERR_EXIST;

	return 0;
}

#ifndef IP_SET_BITMAP_STORED_TIMEOUT
static inline bool
mtype_is_filled(const struct mtype_elem *x)
{
	return true;
}
#endif

static int
mtype_list(const struct ip_set *set,
	   struct sk_buff *skb, struct netlink_callback *cb)
{
	struct mtype *map = set->data;
	struct nlattr *adt, *nested;
	void *x;
	u32 id, first = cb->args[IPSET_CB_ARG0];
	int ret = 0;

	adt = ipset_nest_start(skb, IPSET_ATTR_ADT);
	if (!adt)
		return -EMSGSIZE;
	/* Extensions may be replaced */
	rcu_read_lock();
	for (; cb->args[IPSET_CB_ARG0] < map->elements;
	     cb->args[IPSET_CB_ARG0]++) {
		id = cb->args[IPSET_CB_ARG0];
		x = get_ext(set, map, id);
		if (!test_bit(id, map->members) ||
		    (SET_WITH_TIMEOUT(set) &&
#ifdef IP_SET_BITMAP_STORED_TIMEOUT
		     mtype_is_filled(x) &&
#endif
		     ip_set_timeout_expired(ext_timeout(x, set))))
			continue;
		nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
		if (!nested) {
			if (id == first) {
				nla_nest_cancel(skb, adt);
				ret = -EMSGSIZE;
				goto out;
			}

			goto nla_put_failure;
		}
		if (mtype_do_list(skb, map, id, set->dsize))
			goto nla_put_failure;
		if (ip_set_put_extensions(skb, set, x, mtype_is_filled(x)))
			goto nla_put_failure;
		ipset_nest_end(skb, nested);
	}
	ipset_nest_end(skb, adt);

	/* Set listing finished */
	cb->args[IPSET_CB_ARG0] = 0;

	goto out;

nla_put_failure:
	nla_nest_cancel(skb, nested);
	if (unlikely(id == first)) {
		cb->args[IPSET_CB_ARG0] = 0;
		ret = -EMSGSIZE;
	}
	ipset_nest_end(skb, adt);
out:
	rcu_read_unlock();
	return ret;
}

static void
mtype_gc(struct timer_list *t)
{
	struct mtype *map = from_timer(map, t, gc);
	struct ip_set *set = map->set;
	void *x;
	u32 id;

	/* We run parallel with other readers (test element)
	 * but adding/deleting new entries is locked out
	 */
	spin_lock_bh(&set->lock);
	for (id = 0; id < map->elements; id++)
		if (mtype_gc_test(id, map, set->dsize)) {
			x = get_ext(set, map, id);
			if (ip_set_timeout_expired(ext_timeout(x, set))) {
				clear_bit(id, map->members);
				ip_set_ext_destroy(set, x);
				set->elements--;
			}
		}
	spin_unlock_bh(&set->lock);

	map->gc.expires = jiffies + IPSET_GC_PERIOD(set->timeout) * HZ;
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
