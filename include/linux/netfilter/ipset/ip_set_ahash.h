#ifndef _IP_SET_AHASH_H
#define _IP_SET_AHASH_H

#include <linux/rcupdate.h>
#include <linux/jhash.h>
#include <linux/netfilter/ipset/ip_set_timeout.h>

#define CONCAT(a, b, c)		a##b##c
#define TOKEN(a, b, c)		CONCAT(a, b, c)

#define type_pf_next		TOKEN(TYPE, PF, _elem)

/* Hashing which uses arrays to resolve clashing. The hash table is resized
 * (doubled) when searching becomes too long.
 * Internally jhash is used with the assumption that the size of the
 * stored data is a multiple of sizeof(u32). If storage supports timeout,
 * the timeout field must be the last one in the data structure - that field
 * is ignored when computing the hash key.
 *
 * Readers and resizing
 *
 * Resizing can be triggered by userspace command only, and those
 * are serialized by the nfnl mutex. During resizing the set is
 * read-locked, so the only possible concurrent operations are
 * the kernel side readers. Those must be protected by proper RCU locking.
 */

/* Number of elements to store in an initial array block */
#define AHASH_INIT_SIZE			4
/* Max number of elements to store in an array block */
#define AHASH_MAX_SIZE			(3*AHASH_INIT_SIZE)

/* Max number of elements can be tuned */
#ifdef IP_SET_HASH_WITH_MULTI
#define AHASH_MAX(h)			((h)->ahash_max)

static inline u8
tune_ahash_max(u8 curr, u32 multi)
{
	u32 n;

	if (multi < curr)
		return curr;

	n = curr + AHASH_INIT_SIZE;
	/* Currently, at listing one hash bucket must fit into a message.
	 * Therefore we have a hard limit here.
	 */
	return n > curr && n <= 64 ? n : curr;
}
#define TUNE_AHASH_MAX(h, multi)	\
	((h)->ahash_max = tune_ahash_max((h)->ahash_max, multi))
#else
#define AHASH_MAX(h)			AHASH_MAX_SIZE
#define TUNE_AHASH_MAX(h, multi)
#endif

/* A hash bucket */
struct hbucket {
	void *value;		/* the array of the values */
	u8 size;		/* size of the array */
	u8 pos;			/* position of the first free entry */
};

/* The hash table: the table size stored here in order to make resizing easy */
struct htable {
	u8 htable_bits;		/* size of hash table == 2^htable_bits */
	struct hbucket bucket[0]; /* hashtable buckets */
};

#define hbucket(h, i)		(&((h)->bucket[i]))

/* Book-keeping of the prefixes added to the set */
struct ip_set_hash_nets {
	u8 cidr;		/* the different cidr values in the set */
	u32 nets;		/* number of elements per cidr */
};

/* The generic ip_set hash structure */
struct ip_set_hash {
	struct htable *table;	/* the hash table */
	u32 maxelem;		/* max elements in the hash */
	u32 elements;		/* current element (vs timeout) */
	u32 initval;		/* random jhash init value */
	u32 timeout;		/* timeout value, if enabled */
	struct timer_list gc;	/* garbage collection when timeout enabled */
	struct type_pf_next next; /* temporary storage for uadd */
#ifdef IP_SET_HASH_WITH_MULTI
	u8 ahash_max;		/* max elements in an array block */
#endif
#ifdef IP_SET_HASH_WITH_NETMASK
	u8 netmask;		/* netmask value for subnets to store */
#endif
#ifdef IP_SET_HASH_WITH_RBTREE
	struct rb_root rbtree;
#endif
#ifdef IP_SET_HASH_WITH_NETS
	struct ip_set_hash_nets nets[0]; /* book-keeping of prefixes */
#endif
};

/* Compute htable_bits from the user input parameter hashsize */
static u8
htable_bits(u32 hashsize)
{
	/* Assume that hashsize == 2^htable_bits */
	u8 bits = fls(hashsize - 1);
	if (jhash_size(bits) != hashsize)
		/* Round up to the first 2^n value */
		bits = fls(hashsize);

	return bits;
}

#ifdef IP_SET_HASH_WITH_NETS

#define SET_HOST_MASK(family)	(family == AF_INET ? 32 : 128)

/* Network cidr size book keeping when the hash stores different
 * sized networks */
static void
add_cidr(struct ip_set_hash *h, u8 cidr, u8 host_mask)
{
	u8 i;

	++h->nets[cidr-1].nets;

	pr_debug("add_cidr added %u: %u\n", cidr, h->nets[cidr-1].nets);

	if (h->nets[cidr-1].nets > 1)
		return;

	/* New cidr size */
	for (i = 0; i < host_mask && h->nets[i].cidr; i++) {
		/* Add in increasing prefix order, so larger cidr first */
		if (h->nets[i].cidr < cidr)
			swap(h->nets[i].cidr, cidr);
	}
	if (i < host_mask)
		h->nets[i].cidr = cidr;
}

static void
del_cidr(struct ip_set_hash *h, u8 cidr, u8 host_mask)
{
	u8 i;

	--h->nets[cidr-1].nets;

	pr_debug("del_cidr deleted %u: %u\n", cidr, h->nets[cidr-1].nets);

	if (h->nets[cidr-1].nets != 0)
		return;

	/* All entries with this cidr size deleted, so cleanup h->cidr[] */
	for (i = 0; i < host_mask - 1 && h->nets[i].cidr; i++) {
		if (h->nets[i].cidr == cidr)
			h->nets[i].cidr = cidr = h->nets[i+1].cidr;
	}
	h->nets[i - 1].cidr = 0;
}
#endif

/* Destroy the hashtable part of the set */
static void
ahash_destroy(struct htable *t)
{
	struct hbucket *n;
	u32 i;

	for (i = 0; i < jhash_size(t->htable_bits); i++) {
		n = hbucket(t, i);
		if (n->size)
			/* FIXME: use slab cache */
			kfree(n->value);
	}

	ip_set_free(t);
}

/* Calculate the actual memory size of the set data */
static size_t
ahash_memsize(const struct ip_set_hash *h, size_t dsize, u8 host_mask)
{
	u32 i;
	struct htable *t = h->table;
	size_t memsize = sizeof(*h)
			 + sizeof(*t)
#ifdef IP_SET_HASH_WITH_NETS
			 + sizeof(struct ip_set_hash_nets) * host_mask
#endif
			 + jhash_size(t->htable_bits) * sizeof(struct hbucket);

	for (i = 0; i < jhash_size(t->htable_bits); i++)
			memsize += t->bucket[i].size * dsize;

	return memsize;
}

/* Flush a hash type of set: destroy all elements */
static void
ip_set_hash_flush(struct ip_set *set)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	struct hbucket *n;
	u32 i;

	for (i = 0; i < jhash_size(t->htable_bits); i++) {
		n = hbucket(t, i);
		if (n->size) {
			n->size = n->pos = 0;
			/* FIXME: use slab cache */
			kfree(n->value);
		}
	}
#ifdef IP_SET_HASH_WITH_NETS
	memset(h->nets, 0, sizeof(struct ip_set_hash_nets)
			   * SET_HOST_MASK(set->family));
#endif
	h->elements = 0;
}

/* Destroy a hash type of set */
static void
ip_set_hash_destroy(struct ip_set *set)
{
	struct ip_set_hash *h = set->data;

	if (with_timeout(h->timeout))
		del_timer_sync(&h->gc);

	ahash_destroy(h->table);
#ifdef IP_SET_HASH_WITH_RBTREE
	rbtree_destroy(&h->rbtree);
#endif
	kfree(h);

	set->data = NULL;
}

#endif /* _IP_SET_AHASH_H */

#ifndef HKEY_DATALEN
#define HKEY_DATALEN	sizeof(struct type_pf_elem)
#endif

#define HKEY(data, initval, htable_bits)			\
(jhash2((u32 *)(data), HKEY_DATALEN/sizeof(u32), initval)	\
	& jhash_mask(htable_bits))

#define CONCAT(a, b, c)		a##b##c
#define TOKEN(a, b, c)		CONCAT(a, b, c)

/* Type/family dependent function prototypes */

#define type_pf_data_equal	TOKEN(TYPE, PF, _data_equal)
#define type_pf_data_isnull	TOKEN(TYPE, PF, _data_isnull)
#define type_pf_data_copy	TOKEN(TYPE, PF, _data_copy)
#define type_pf_data_zero_out	TOKEN(TYPE, PF, _data_zero_out)
#define type_pf_data_netmask	TOKEN(TYPE, PF, _data_netmask)
#define type_pf_data_list	TOKEN(TYPE, PF, _data_list)
#define type_pf_data_tlist	TOKEN(TYPE, PF, _data_tlist)
#define type_pf_data_next	TOKEN(TYPE, PF, _data_next)

#define type_pf_elem		TOKEN(TYPE, PF, _elem)
#define type_pf_telem		TOKEN(TYPE, PF, _telem)
#define type_pf_data_timeout	TOKEN(TYPE, PF, _data_timeout)
#define type_pf_data_expired	TOKEN(TYPE, PF, _data_expired)
#define type_pf_data_timeout_set TOKEN(TYPE, PF, _data_timeout_set)

#define type_pf_elem_add	TOKEN(TYPE, PF, _elem_add)
#define type_pf_add		TOKEN(TYPE, PF, _add)
#define type_pf_del		TOKEN(TYPE, PF, _del)
#define type_pf_test_cidrs	TOKEN(TYPE, PF, _test_cidrs)
#define type_pf_test		TOKEN(TYPE, PF, _test)

#define type_pf_elem_tadd	TOKEN(TYPE, PF, _elem_tadd)
#define type_pf_del_telem	TOKEN(TYPE, PF, _ahash_del_telem)
#define type_pf_expire		TOKEN(TYPE, PF, _expire)
#define type_pf_tadd		TOKEN(TYPE, PF, _tadd)
#define type_pf_tdel		TOKEN(TYPE, PF, _tdel)
#define type_pf_ttest_cidrs	TOKEN(TYPE, PF, _ahash_ttest_cidrs)
#define type_pf_ttest		TOKEN(TYPE, PF, _ahash_ttest)

#define type_pf_resize		TOKEN(TYPE, PF, _resize)
#define type_pf_tresize		TOKEN(TYPE, PF, _tresize)
#define type_pf_flush		ip_set_hash_flush
#define type_pf_destroy		ip_set_hash_destroy
#define type_pf_head		TOKEN(TYPE, PF, _head)
#define type_pf_list		TOKEN(TYPE, PF, _list)
#define type_pf_tlist		TOKEN(TYPE, PF, _tlist)
#define type_pf_same_set	TOKEN(TYPE, PF, _same_set)
#define type_pf_kadt		TOKEN(TYPE, PF, _kadt)
#define type_pf_uadt		TOKEN(TYPE, PF, _uadt)
#define type_pf_gc		TOKEN(TYPE, PF, _gc)
#define type_pf_gc_init		TOKEN(TYPE, PF, _gc_init)
#define type_pf_variant		TOKEN(TYPE, PF, _variant)
#define type_pf_tvariant	TOKEN(TYPE, PF, _tvariant)

/* Flavour without timeout */

/* Get the ith element from the array block n */
#define ahash_data(n, i)	\
	((struct type_pf_elem *)((n)->value) + (i))

/* Add an element to the hash table when resizing the set:
 * we spare the maintenance of the internal counters. */
static int
type_pf_elem_add(struct hbucket *n, const struct type_pf_elem *value,
		 u8 ahash_max)
{
	if (n->pos >= n->size) {
		void *tmp;

		if (n->size >= ahash_max)
			/* Trigger rehashing */
			return -EAGAIN;

		tmp = kzalloc((n->size + AHASH_INIT_SIZE)
			      * sizeof(struct type_pf_elem),
			      GFP_ATOMIC);
		if (!tmp)
			return -ENOMEM;
		if (n->size) {
			memcpy(tmp, n->value,
			       sizeof(struct type_pf_elem) * n->size);
			kfree(n->value);
		}
		n->value = tmp;
		n->size += AHASH_INIT_SIZE;
	}
	type_pf_data_copy(ahash_data(n, n->pos++), value);
	return 0;
}

/* Resize a hash: create a new hash table with doubling the hashsize
 * and inserting the elements to it. Repeat until we succeed or
 * fail due to memory pressures. */
static int
type_pf_resize(struct ip_set *set, bool retried)
{
	struct ip_set_hash *h = set->data;
	struct htable *t, *orig = h->table;
	u8 htable_bits = orig->htable_bits;
	const struct type_pf_elem *data;
	struct hbucket *n, *m;
	u32 i, j;
	int ret;

retry:
	ret = 0;
	htable_bits++;
	pr_debug("attempt to resize set %s from %u to %u, t %p\n",
		 set->name, orig->htable_bits, htable_bits, orig);
	if (!htable_bits) {
		/* In case we have plenty of memory :-) */
		pr_warning("Cannot increase the hashsize of set %s further\n",
			   set->name);
		return -IPSET_ERR_HASH_FULL;
	}
	t = ip_set_alloc(sizeof(*t)
			 + jhash_size(htable_bits) * sizeof(struct hbucket));
	if (!t)
		return -ENOMEM;
	t->htable_bits = htable_bits;

	read_lock_bh(&set->lock);
	for (i = 0; i < jhash_size(orig->htable_bits); i++) {
		n = hbucket(orig, i);
		for (j = 0; j < n->pos; j++) {
			data = ahash_data(n, j);
			m = hbucket(t, HKEY(data, h->initval, htable_bits));
			ret = type_pf_elem_add(m, data, AHASH_MAX(h));
			if (ret < 0) {
				read_unlock_bh(&set->lock);
				ahash_destroy(t);
				if (ret == -EAGAIN)
					goto retry;
				return ret;
			}
		}
	}

	rcu_assign_pointer(h->table, t);
	read_unlock_bh(&set->lock);

	/* Give time to other readers of the set */
	synchronize_rcu_bh();

	pr_debug("set %s resized from %u (%p) to %u (%p)\n", set->name,
		 orig->htable_bits, orig, t->htable_bits, t);
	ahash_destroy(orig);

	return 0;
}

static inline void
type_pf_data_next(struct ip_set_hash *h, const struct type_pf_elem *d);

/* Add an element to a hash and update the internal counters when succeeded,
 * otherwise report the proper error code. */
static int
type_pf_add(struct ip_set *set, void *value, u32 timeout, u32 flags)
{
	struct ip_set_hash *h = set->data;
	struct htable *t;
	const struct type_pf_elem *d = value;
	struct hbucket *n;
	int i, ret = 0;
	u32 key, multi = 0;

	if (h->elements >= h->maxelem) {
		if (net_ratelimit())
			pr_warning("Set %s is full, maxelem %u reached\n",
				   set->name, h->maxelem);
		return -IPSET_ERR_HASH_FULL;
	}

	rcu_read_lock_bh();
	t = rcu_dereference_bh(h->table);
	key = HKEY(value, h->initval, t->htable_bits);
	n = hbucket(t, key);
	for (i = 0; i < n->pos; i++)
		if (type_pf_data_equal(ahash_data(n, i), d, &multi)) {
			ret = -IPSET_ERR_EXIST;
			goto out;
		}
	TUNE_AHASH_MAX(h, multi);
	ret = type_pf_elem_add(n, value, AHASH_MAX(h));
	if (ret != 0) {
		if (ret == -EAGAIN)
			type_pf_data_next(h, d);
		goto out;
	}

#ifdef IP_SET_HASH_WITH_NETS
	add_cidr(h, d->cidr, HOST_MASK);
#endif
	h->elements++;
out:
	rcu_read_unlock_bh();
	return ret;
}

/* Delete an element from the hash: swap it with the last element
 * and free up space if possible.
 */
static int
type_pf_del(struct ip_set *set, void *value, u32 timeout, u32 flags)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	const struct type_pf_elem *d = value;
	struct hbucket *n;
	int i;
	struct type_pf_elem *data;
	u32 key, multi = 0;

	key = HKEY(value, h->initval, t->htable_bits);
	n = hbucket(t, key);
	for (i = 0; i < n->pos; i++) {
		data = ahash_data(n, i);
		if (!type_pf_data_equal(data, d, &multi))
			continue;
		if (i != n->pos - 1)
			/* Not last one */
			type_pf_data_copy(data, ahash_data(n, n->pos - 1));

		n->pos--;
		h->elements--;
#ifdef IP_SET_HASH_WITH_NETS
		del_cidr(h, d->cidr, HOST_MASK);
#endif
		if (n->pos + AHASH_INIT_SIZE < n->size) {
			void *tmp = kzalloc((n->size - AHASH_INIT_SIZE)
					    * sizeof(struct type_pf_elem),
					    GFP_ATOMIC);
			if (!tmp)
				return 0;
			n->size -= AHASH_INIT_SIZE;
			memcpy(tmp, n->value,
			       n->size * sizeof(struct type_pf_elem));
			kfree(n->value);
			n->value = tmp;
		}
		return 0;
	}

	return -IPSET_ERR_EXIST;
}

#ifdef IP_SET_HASH_WITH_NETS

/* Special test function which takes into account the different network
 * sizes added to the set */
static int
type_pf_test_cidrs(struct ip_set *set, struct type_pf_elem *d, u32 timeout)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	struct hbucket *n;
	const struct type_pf_elem *data;
	int i, j = 0;
	u32 key, multi = 0;
	u8 host_mask = SET_HOST_MASK(set->family);

	pr_debug("test by nets\n");
	for (; j < host_mask && h->nets[j].cidr && !multi; j++) {
		type_pf_data_netmask(d, h->nets[j].cidr);
		key = HKEY(d, h->initval, t->htable_bits);
		n = hbucket(t, key);
		for (i = 0; i < n->pos; i++) {
			data = ahash_data(n, i);
			if (type_pf_data_equal(data, d, &multi))
				return 1;
		}
	}
	return 0;
}
#endif

/* Test whether the element is added to the set */
static int
type_pf_test(struct ip_set *set, void *value, u32 timeout, u32 flags)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	struct type_pf_elem *d = value;
	struct hbucket *n;
	const struct type_pf_elem *data;
	int i;
	u32 key, multi = 0;

#ifdef IP_SET_HASH_WITH_NETS
	/* If we test an IP address and not a network address,
	 * try all possible network sizes */
	if (d->cidr == SET_HOST_MASK(set->family))
		return type_pf_test_cidrs(set, d, timeout);
#endif

	key = HKEY(d, h->initval, t->htable_bits);
	n = hbucket(t, key);
	for (i = 0; i < n->pos; i++) {
		data = ahash_data(n, i);
		if (type_pf_data_equal(data, d, &multi))
			return 1;
	}
	return 0;
}

/* Reply a HEADER request: fill out the header part of the set */
static int
type_pf_head(struct ip_set *set, struct sk_buff *skb)
{
	const struct ip_set_hash *h = set->data;
	struct nlattr *nested;
	size_t memsize;

	read_lock_bh(&set->lock);
	memsize = ahash_memsize(h, with_timeout(h->timeout)
					? sizeof(struct type_pf_telem)
					: sizeof(struct type_pf_elem),
				set->family == AF_INET ? 32 : 128);
	read_unlock_bh(&set->lock);

	nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
	if (!nested)
		goto nla_put_failure;
	NLA_PUT_NET32(skb, IPSET_ATTR_HASHSIZE,
		      htonl(jhash_size(h->table->htable_bits)));
	NLA_PUT_NET32(skb, IPSET_ATTR_MAXELEM, htonl(h->maxelem));
#ifdef IP_SET_HASH_WITH_NETMASK
	if (h->netmask != HOST_MASK)
		NLA_PUT_U8(skb, IPSET_ATTR_NETMASK, h->netmask);
#endif
	NLA_PUT_NET32(skb, IPSET_ATTR_REFERENCES, htonl(set->ref - 1));
	NLA_PUT_NET32(skb, IPSET_ATTR_MEMSIZE, htonl(memsize));
	if (with_timeout(h->timeout))
		NLA_PUT_NET32(skb, IPSET_ATTR_TIMEOUT, htonl(h->timeout));
	ipset_nest_end(skb, nested);

	return 0;
nla_put_failure:
	return -EMSGSIZE;
}

/* Reply a LIST/SAVE request: dump the elements of the specified set */
static int
type_pf_list(const struct ip_set *set,
	     struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct ip_set_hash *h = set->data;
	const struct htable *t = h->table;
	struct nlattr *atd, *nested;
	const struct hbucket *n;
	const struct type_pf_elem *data;
	u32 first = cb->args[2];
	/* We assume that one hash bucket fills into one page */
	void *incomplete;
	int i;

	atd = ipset_nest_start(skb, IPSET_ATTR_ADT);
	if (!atd)
		return -EMSGSIZE;
	pr_debug("list hash set %s\n", set->name);
	for (; cb->args[2] < jhash_size(t->htable_bits); cb->args[2]++) {
		incomplete = skb_tail_pointer(skb);
		n = hbucket(t, cb->args[2]);
		pr_debug("cb->args[2]: %lu, t %p n %p\n", cb->args[2], t, n);
		for (i = 0; i < n->pos; i++) {
			data = ahash_data(n, i);
			pr_debug("list hash %lu hbucket %p i %u, data %p\n",
				 cb->args[2], n, i, data);
			nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
			if (!nested) {
				if (cb->args[2] == first) {
					nla_nest_cancel(skb, atd);
					return -EMSGSIZE;
				} else
					goto nla_put_failure;
			}
			if (type_pf_data_list(skb, data))
				goto nla_put_failure;
			ipset_nest_end(skb, nested);
		}
	}
	ipset_nest_end(skb, atd);
	/* Set listing finished */
	cb->args[2] = 0;

	return 0;

nla_put_failure:
	nlmsg_trim(skb, incomplete);
	ipset_nest_end(skb, atd);
	if (unlikely(first == cb->args[2])) {
		pr_warning("Can't list set %s: one bucket does not fit into "
			   "a message. Please report it!\n", set->name);
		cb->args[2] = 0;
		return -EMSGSIZE;
	}
	return 0;
}

static int
type_pf_kadt(struct ip_set *set, const struct sk_buff * skb,
	     const struct xt_action_param *par,
	     enum ipset_adt adt, const struct ip_set_adt_opt *opt);
static int
type_pf_uadt(struct ip_set *set, struct nlattr *tb[],
	     enum ipset_adt adt, u32 *lineno, u32 flags, bool retried);

static const struct ip_set_type_variant type_pf_variant = {
	.kadt	= type_pf_kadt,
	.uadt	= type_pf_uadt,
	.adt	= {
		[IPSET_ADD] = type_pf_add,
		[IPSET_DEL] = type_pf_del,
		[IPSET_TEST] = type_pf_test,
	},
	.destroy = type_pf_destroy,
	.flush	= type_pf_flush,
	.head	= type_pf_head,
	.list	= type_pf_list,
	.resize	= type_pf_resize,
	.same_set = type_pf_same_set,
};

/* Flavour with timeout support */

#define ahash_tdata(n, i) \
	(struct type_pf_elem *)((struct type_pf_telem *)((n)->value) + (i))

static inline u32
type_pf_data_timeout(const struct type_pf_elem *data)
{
	const struct type_pf_telem *tdata =
		(const struct type_pf_telem *) data;

	return tdata->timeout;
}

static inline bool
type_pf_data_expired(const struct type_pf_elem *data)
{
	const struct type_pf_telem *tdata =
		(const struct type_pf_telem *) data;

	return ip_set_timeout_expired(tdata->timeout);
}

static inline void
type_pf_data_timeout_set(struct type_pf_elem *data, u32 timeout)
{
	struct type_pf_telem *tdata = (struct type_pf_telem *) data;

	tdata->timeout = ip_set_timeout_set(timeout);
}

static int
type_pf_elem_tadd(struct hbucket *n, const struct type_pf_elem *value,
		  u8 ahash_max, u32 timeout)
{
	struct type_pf_elem *data;

	if (n->pos >= n->size) {
		void *tmp;

		if (n->size >= ahash_max)
			/* Trigger rehashing */
			return -EAGAIN;

		tmp = kzalloc((n->size + AHASH_INIT_SIZE)
			      * sizeof(struct type_pf_telem),
			      GFP_ATOMIC);
		if (!tmp)
			return -ENOMEM;
		if (n->size) {
			memcpy(tmp, n->value,
			       sizeof(struct type_pf_telem) * n->size);
			kfree(n->value);
		}
		n->value = tmp;
		n->size += AHASH_INIT_SIZE;
	}
	data = ahash_tdata(n, n->pos++);
	type_pf_data_copy(data, value);
	type_pf_data_timeout_set(data, timeout);
	return 0;
}

/* Delete expired elements from the hashtable */
static void
type_pf_expire(struct ip_set_hash *h)
{
	struct htable *t = h->table;
	struct hbucket *n;
	struct type_pf_elem *data;
	u32 i;
	int j;

	for (i = 0; i < jhash_size(t->htable_bits); i++) {
		n = hbucket(t, i);
		for (j = 0; j < n->pos; j++) {
			data = ahash_tdata(n, j);
			if (type_pf_data_expired(data)) {
				pr_debug("expired %u/%u\n", i, j);
#ifdef IP_SET_HASH_WITH_NETS
				del_cidr(h, data->cidr, HOST_MASK);
#endif
				if (j != n->pos - 1)
					/* Not last one */
					type_pf_data_copy(data,
						ahash_tdata(n, n->pos - 1));
				n->pos--;
				h->elements--;
			}
		}
		if (n->pos + AHASH_INIT_SIZE < n->size) {
			void *tmp = kzalloc((n->size - AHASH_INIT_SIZE)
					    * sizeof(struct type_pf_telem),
					    GFP_ATOMIC);
			if (!tmp)
				/* Still try to delete expired elements */
				continue;
			n->size -= AHASH_INIT_SIZE;
			memcpy(tmp, n->value,
			       n->size * sizeof(struct type_pf_telem));
			kfree(n->value);
			n->value = tmp;
		}
	}
}

static int
type_pf_tresize(struct ip_set *set, bool retried)
{
	struct ip_set_hash *h = set->data;
	struct htable *t, *orig = h->table;
	u8 htable_bits = orig->htable_bits;
	const struct type_pf_elem *data;
	struct hbucket *n, *m;
	u32 i, j;
	int ret;

	/* Try to cleanup once */
	if (!retried) {
		i = h->elements;
		write_lock_bh(&set->lock);
		type_pf_expire(set->data);
		write_unlock_bh(&set->lock);
		if (h->elements <  i)
			return 0;
	}

retry:
	ret = 0;
	htable_bits++;
	if (!htable_bits) {
		/* In case we have plenty of memory :-) */
		pr_warning("Cannot increase the hashsize of set %s further\n",
			   set->name);
		return -IPSET_ERR_HASH_FULL;
	}
	t = ip_set_alloc(sizeof(*t)
			 + jhash_size(htable_bits) * sizeof(struct hbucket));
	if (!t)
		return -ENOMEM;
	t->htable_bits = htable_bits;

	read_lock_bh(&set->lock);
	for (i = 0; i < jhash_size(orig->htable_bits); i++) {
		n = hbucket(orig, i);
		for (j = 0; j < n->pos; j++) {
			data = ahash_tdata(n, j);
			m = hbucket(t, HKEY(data, h->initval, htable_bits));
			ret = type_pf_elem_tadd(m, data, AHASH_MAX(h),
						type_pf_data_timeout(data));
			if (ret < 0) {
				read_unlock_bh(&set->lock);
				ahash_destroy(t);
				if (ret == -EAGAIN)
					goto retry;
				return ret;
			}
		}
	}

	rcu_assign_pointer(h->table, t);
	read_unlock_bh(&set->lock);

	/* Give time to other readers of the set */
	synchronize_rcu_bh();

	ahash_destroy(orig);

	return 0;
}

static int
type_pf_tadd(struct ip_set *set, void *value, u32 timeout, u32 flags)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	const struct type_pf_elem *d = value;
	struct hbucket *n;
	struct type_pf_elem *data;
	int ret = 0, i, j = AHASH_MAX(h) + 1;
	bool flag_exist = flags & IPSET_FLAG_EXIST;
	u32 key, multi = 0;

	if (h->elements >= h->maxelem)
		/* FIXME: when set is full, we slow down here */
		type_pf_expire(h);
	if (h->elements >= h->maxelem) {
		if (net_ratelimit())
			pr_warning("Set %s is full, maxelem %u reached\n",
				   set->name, h->maxelem);
		return -IPSET_ERR_HASH_FULL;
	}

	rcu_read_lock_bh();
	t = rcu_dereference_bh(h->table);
	key = HKEY(d, h->initval, t->htable_bits);
	n = hbucket(t, key);
	for (i = 0; i < n->pos; i++) {
		data = ahash_tdata(n, i);
		if (type_pf_data_equal(data, d, &multi)) {
			if (type_pf_data_expired(data) || flag_exist)
				j = i;
			else {
				ret = -IPSET_ERR_EXIST;
				goto out;
			}
		} else if (j == AHASH_MAX(h) + 1 &&
			   type_pf_data_expired(data))
			j = i;
	}
	if (j != AHASH_MAX(h) + 1) {
		data = ahash_tdata(n, j);
#ifdef IP_SET_HASH_WITH_NETS
		del_cidr(h, data->cidr, HOST_MASK);
		add_cidr(h, d->cidr, HOST_MASK);
#endif
		type_pf_data_copy(data, d);
		type_pf_data_timeout_set(data, timeout);
		goto out;
	}
	TUNE_AHASH_MAX(h, multi);
	ret = type_pf_elem_tadd(n, d, AHASH_MAX(h), timeout);
	if (ret != 0) {
		if (ret == -EAGAIN)
			type_pf_data_next(h, d);
		goto out;
	}

#ifdef IP_SET_HASH_WITH_NETS
	add_cidr(h, d->cidr, HOST_MASK);
#endif
	h->elements++;
out:
	rcu_read_unlock_bh();
	return ret;
}

static int
type_pf_tdel(struct ip_set *set, void *value, u32 timeout, u32 flags)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	const struct type_pf_elem *d = value;
	struct hbucket *n;
	int i;
	struct type_pf_elem *data;
	u32 key, multi = 0;

	key = HKEY(value, h->initval, t->htable_bits);
	n = hbucket(t, key);
	for (i = 0; i < n->pos; i++) {
		data = ahash_tdata(n, i);
		if (!type_pf_data_equal(data, d, &multi))
			continue;
		if (type_pf_data_expired(data))
			return -IPSET_ERR_EXIST;
		if (i != n->pos - 1)
			/* Not last one */
			type_pf_data_copy(data, ahash_tdata(n, n->pos - 1));

		n->pos--;
		h->elements--;
#ifdef IP_SET_HASH_WITH_NETS
		del_cidr(h, d->cidr, HOST_MASK);
#endif
		if (n->pos + AHASH_INIT_SIZE < n->size) {
			void *tmp = kzalloc((n->size - AHASH_INIT_SIZE)
					    * sizeof(struct type_pf_telem),
					    GFP_ATOMIC);
			if (!tmp)
				return 0;
			n->size -= AHASH_INIT_SIZE;
			memcpy(tmp, n->value,
			       n->size * sizeof(struct type_pf_telem));
			kfree(n->value);
			n->value = tmp;
		}
		return 0;
	}

	return -IPSET_ERR_EXIST;
}

#ifdef IP_SET_HASH_WITH_NETS
static int
type_pf_ttest_cidrs(struct ip_set *set, struct type_pf_elem *d, u32 timeout)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	struct type_pf_elem *data;
	struct hbucket *n;
	int i, j = 0;
	u32 key, multi = 0;
	u8 host_mask = SET_HOST_MASK(set->family);

	for (; j < host_mask && h->nets[j].cidr && !multi; j++) {
		type_pf_data_netmask(d, h->nets[j].cidr);
		key = HKEY(d, h->initval, t->htable_bits);
		n = hbucket(t, key);
		for (i = 0; i < n->pos; i++) {
			data = ahash_tdata(n, i);
			if (type_pf_data_equal(data, d, &multi))
				return !type_pf_data_expired(data);
		}
	}
	return 0;
}
#endif

static int
type_pf_ttest(struct ip_set *set, void *value, u32 timeout, u32 flags)
{
	struct ip_set_hash *h = set->data;
	struct htable *t = h->table;
	struct type_pf_elem *data, *d = value;
	struct hbucket *n;
	int i;
	u32 key, multi = 0;

#ifdef IP_SET_HASH_WITH_NETS
	if (d->cidr == SET_HOST_MASK(set->family))
		return type_pf_ttest_cidrs(set, d, timeout);
#endif
	key = HKEY(d, h->initval, t->htable_bits);
	n = hbucket(t, key);
	for (i = 0; i < n->pos; i++) {
		data = ahash_tdata(n, i);
		if (type_pf_data_equal(data, d, &multi))
			return !type_pf_data_expired(data);
	}
	return 0;
}

static int
type_pf_tlist(const struct ip_set *set,
	      struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct ip_set_hash *h = set->data;
	const struct htable *t = h->table;
	struct nlattr *atd, *nested;
	const struct hbucket *n;
	const struct type_pf_elem *data;
	u32 first = cb->args[2];
	/* We assume that one hash bucket fills into one page */
	void *incomplete;
	int i;

	atd = ipset_nest_start(skb, IPSET_ATTR_ADT);
	if (!atd)
		return -EMSGSIZE;
	for (; cb->args[2] < jhash_size(t->htable_bits); cb->args[2]++) {
		incomplete = skb_tail_pointer(skb);
		n = hbucket(t, cb->args[2]);
		for (i = 0; i < n->pos; i++) {
			data = ahash_tdata(n, i);
			pr_debug("list %p %u\n", n, i);
			if (type_pf_data_expired(data))
				continue;
			pr_debug("do list %p %u\n", n, i);
			nested = ipset_nest_start(skb, IPSET_ATTR_DATA);
			if (!nested) {
				if (cb->args[2] == first) {
					nla_nest_cancel(skb, atd);
					return -EMSGSIZE;
				} else
					goto nla_put_failure;
			}
			if (type_pf_data_tlist(skb, data))
				goto nla_put_failure;
			ipset_nest_end(skb, nested);
		}
	}
	ipset_nest_end(skb, atd);
	/* Set listing finished */
	cb->args[2] = 0;

	return 0;

nla_put_failure:
	nlmsg_trim(skb, incomplete);
	ipset_nest_end(skb, atd);
	if (unlikely(first == cb->args[2])) {
		pr_warning("Can't list set %s: one bucket does not fit into "
			   "a message. Please report it!\n", set->name);
		cb->args[2] = 0;
		return -EMSGSIZE;
	}
	return 0;
}

static const struct ip_set_type_variant type_pf_tvariant = {
	.kadt	= type_pf_kadt,
	.uadt	= type_pf_uadt,
	.adt	= {
		[IPSET_ADD] = type_pf_tadd,
		[IPSET_DEL] = type_pf_tdel,
		[IPSET_TEST] = type_pf_ttest,
	},
	.destroy = type_pf_destroy,
	.flush	= type_pf_flush,
	.head	= type_pf_head,
	.list	= type_pf_tlist,
	.resize	= type_pf_tresize,
	.same_set = type_pf_same_set,
};

static void
type_pf_gc(unsigned long ul_set)
{
	struct ip_set *set = (struct ip_set *) ul_set;
	struct ip_set_hash *h = set->data;

	pr_debug("called\n");
	write_lock_bh(&set->lock);
	type_pf_expire(h);
	write_unlock_bh(&set->lock);

	h->gc.expires = jiffies + IPSET_GC_PERIOD(h->timeout) * HZ;
	add_timer(&h->gc);
}

static void
type_pf_gc_init(struct ip_set *set)
{
	struct ip_set_hash *h = set->data;

	init_timer(&h->gc);
	h->gc.data = (unsigned long) set;
	h->gc.function = type_pf_gc;
	h->gc.expires = jiffies + IPSET_GC_PERIOD(h->timeout) * HZ;
	add_timer(&h->gc);
	pr_debug("gc initialized, run in every %u\n",
		 IPSET_GC_PERIOD(h->timeout));
}

#undef HKEY_DATALEN
#undef HKEY
#undef type_pf_data_equal
#undef type_pf_data_isnull
#undef type_pf_data_copy
#undef type_pf_data_zero_out
#undef type_pf_data_list
#undef type_pf_data_tlist

#undef type_pf_elem
#undef type_pf_telem
#undef type_pf_data_timeout
#undef type_pf_data_expired
#undef type_pf_data_netmask
#undef type_pf_data_timeout_set

#undef type_pf_elem_add
#undef type_pf_add
#undef type_pf_del
#undef type_pf_test_cidrs
#undef type_pf_test

#undef type_pf_elem_tadd
#undef type_pf_expire
#undef type_pf_tadd
#undef type_pf_tdel
#undef type_pf_ttest_cidrs
#undef type_pf_ttest

#undef type_pf_resize
#undef type_pf_tresize
#undef type_pf_flush
#undef type_pf_destroy
#undef type_pf_head
#undef type_pf_list
#undef type_pf_tlist
#undef type_pf_same_set
#undef type_pf_kadt
#undef type_pf_uadt
#undef type_pf_gc
#undef type_pf_gc_init
#undef type_pf_variant
#undef type_pf_tvariant
