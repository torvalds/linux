// SPDX-License-Identifier: GPL-2.0

#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/slab.h>

#include <linux/ceph/libceph.h>
#include <linux/ceph/osdmap.h>
#include <linux/ceph/decode.h>
#include <linux/crush/hash.h>
#include <linux/crush/mapper.h>

char *ceph_osdmap_state_str(char *str, int len, u32 state)
{
	if (!len)
		return str;

	if ((state & CEPH_OSD_EXISTS) && (state & CEPH_OSD_UP))
		snprintf(str, len, "exists, up");
	else if (state & CEPH_OSD_EXISTS)
		snprintf(str, len, "exists");
	else if (state & CEPH_OSD_UP)
		snprintf(str, len, "up");
	else
		snprintf(str, len, "doesn't exist");

	return str;
}

/* maps */

static int calc_bits_of(unsigned int t)
{
	int b = 0;
	while (t) {
		t = t >> 1;
		b++;
	}
	return b;
}

/*
 * the foo_mask is the smallest value 2^n-1 that is >= foo.
 */
static void calc_pg_masks(struct ceph_pg_pool_info *pi)
{
	pi->pg_num_mask = (1 << calc_bits_of(pi->pg_num-1)) - 1;
	pi->pgp_num_mask = (1 << calc_bits_of(pi->pgp_num-1)) - 1;
}

/*
 * decode crush map
 */
static int crush_decode_uniform_bucket(void **p, void *end,
				       struct crush_bucket_uniform *b)
{
	dout("crush_decode_uniform_bucket %p to %p\n", *p, end);
	ceph_decode_need(p, end, (1+b->h.size) * sizeof(u32), bad);
	b->item_weight = ceph_decode_32(p);
	return 0;
bad:
	return -EINVAL;
}

static int crush_decode_list_bucket(void **p, void *end,
				    struct crush_bucket_list *b)
{
	int j;
	dout("crush_decode_list_bucket %p to %p\n", *p, end);
	b->item_weights = kcalloc(b->h.size, sizeof(u32), GFP_NOFS);
	if (b->item_weights == NULL)
		return -ENOMEM;
	b->sum_weights = kcalloc(b->h.size, sizeof(u32), GFP_NOFS);
	if (b->sum_weights == NULL)
		return -ENOMEM;
	ceph_decode_need(p, end, 2 * b->h.size * sizeof(u32), bad);
	for (j = 0; j < b->h.size; j++) {
		b->item_weights[j] = ceph_decode_32(p);
		b->sum_weights[j] = ceph_decode_32(p);
	}
	return 0;
bad:
	return -EINVAL;
}

static int crush_decode_tree_bucket(void **p, void *end,
				    struct crush_bucket_tree *b)
{
	int j;
	dout("crush_decode_tree_bucket %p to %p\n", *p, end);
	ceph_decode_8_safe(p, end, b->num_nodes, bad);
	b->node_weights = kcalloc(b->num_nodes, sizeof(u32), GFP_NOFS);
	if (b->node_weights == NULL)
		return -ENOMEM;
	ceph_decode_need(p, end, b->num_nodes * sizeof(u32), bad);
	for (j = 0; j < b->num_nodes; j++)
		b->node_weights[j] = ceph_decode_32(p);
	return 0;
bad:
	return -EINVAL;
}

static int crush_decode_straw_bucket(void **p, void *end,
				     struct crush_bucket_straw *b)
{
	int j;
	dout("crush_decode_straw_bucket %p to %p\n", *p, end);
	b->item_weights = kcalloc(b->h.size, sizeof(u32), GFP_NOFS);
	if (b->item_weights == NULL)
		return -ENOMEM;
	b->straws = kcalloc(b->h.size, sizeof(u32), GFP_NOFS);
	if (b->straws == NULL)
		return -ENOMEM;
	ceph_decode_need(p, end, 2 * b->h.size * sizeof(u32), bad);
	for (j = 0; j < b->h.size; j++) {
		b->item_weights[j] = ceph_decode_32(p);
		b->straws[j] = ceph_decode_32(p);
	}
	return 0;
bad:
	return -EINVAL;
}

static int crush_decode_straw2_bucket(void **p, void *end,
				      struct crush_bucket_straw2 *b)
{
	int j;
	dout("crush_decode_straw2_bucket %p to %p\n", *p, end);
	b->item_weights = kcalloc(b->h.size, sizeof(u32), GFP_NOFS);
	if (b->item_weights == NULL)
		return -ENOMEM;
	ceph_decode_need(p, end, b->h.size * sizeof(u32), bad);
	for (j = 0; j < b->h.size; j++)
		b->item_weights[j] = ceph_decode_32(p);
	return 0;
bad:
	return -EINVAL;
}

struct crush_name_node {
	struct rb_node cn_node;
	int cn_id;
	char cn_name[];
};

static struct crush_name_node *alloc_crush_name(size_t name_len)
{
	struct crush_name_node *cn;

	cn = kmalloc(sizeof(*cn) + name_len + 1, GFP_NOIO);
	if (!cn)
		return NULL;

	RB_CLEAR_NODE(&cn->cn_node);
	return cn;
}

static void free_crush_name(struct crush_name_node *cn)
{
	WARN_ON(!RB_EMPTY_NODE(&cn->cn_node));

	kfree(cn);
}

DEFINE_RB_FUNCS(crush_name, struct crush_name_node, cn_id, cn_node)

static int decode_crush_names(void **p, void *end, struct rb_root *root)
{
	u32 n;

	ceph_decode_32_safe(p, end, n, e_inval);
	while (n--) {
		struct crush_name_node *cn;
		int id;
		u32 name_len;

		ceph_decode_32_safe(p, end, id, e_inval);
		ceph_decode_32_safe(p, end, name_len, e_inval);
		ceph_decode_need(p, end, name_len, e_inval);

		cn = alloc_crush_name(name_len);
		if (!cn)
			return -ENOMEM;

		cn->cn_id = id;
		memcpy(cn->cn_name, *p, name_len);
		cn->cn_name[name_len] = '\0';
		*p += name_len;

		if (!__insert_crush_name(root, cn)) {
			free_crush_name(cn);
			return -EEXIST;
		}
	}

	return 0;

e_inval:
	return -EINVAL;
}

void clear_crush_names(struct rb_root *root)
{
	while (!RB_EMPTY_ROOT(root)) {
		struct crush_name_node *cn =
		    rb_entry(rb_first(root), struct crush_name_node, cn_node);

		erase_crush_name(root, cn);
		free_crush_name(cn);
	}
}

static struct crush_choose_arg_map *alloc_choose_arg_map(void)
{
	struct crush_choose_arg_map *arg_map;

	arg_map = kzalloc(sizeof(*arg_map), GFP_NOIO);
	if (!arg_map)
		return NULL;

	RB_CLEAR_NODE(&arg_map->node);
	return arg_map;
}

static void free_choose_arg_map(struct crush_choose_arg_map *arg_map)
{
	if (arg_map) {
		int i, j;

		WARN_ON(!RB_EMPTY_NODE(&arg_map->node));

		for (i = 0; i < arg_map->size; i++) {
			struct crush_choose_arg *arg = &arg_map->args[i];

			for (j = 0; j < arg->weight_set_size; j++)
				kfree(arg->weight_set[j].weights);
			kfree(arg->weight_set);
			kfree(arg->ids);
		}
		kfree(arg_map->args);
		kfree(arg_map);
	}
}

DEFINE_RB_FUNCS(choose_arg_map, struct crush_choose_arg_map, choose_args_index,
		node);

void clear_choose_args(struct crush_map *c)
{
	while (!RB_EMPTY_ROOT(&c->choose_args)) {
		struct crush_choose_arg_map *arg_map =
		    rb_entry(rb_first(&c->choose_args),
			     struct crush_choose_arg_map, node);

		erase_choose_arg_map(&c->choose_args, arg_map);
		free_choose_arg_map(arg_map);
	}
}

static u32 *decode_array_32_alloc(void **p, void *end, u32 *plen)
{
	u32 *a = NULL;
	u32 len;
	int ret;

	ceph_decode_32_safe(p, end, len, e_inval);
	if (len) {
		u32 i;

		a = kmalloc_array(len, sizeof(u32), GFP_NOIO);
		if (!a) {
			ret = -ENOMEM;
			goto fail;
		}

		ceph_decode_need(p, end, len * sizeof(u32), e_inval);
		for (i = 0; i < len; i++)
			a[i] = ceph_decode_32(p);
	}

	*plen = len;
	return a;

e_inval:
	ret = -EINVAL;
fail:
	kfree(a);
	return ERR_PTR(ret);
}

/*
 * Assumes @arg is zero-initialized.
 */
static int decode_choose_arg(void **p, void *end, struct crush_choose_arg *arg)
{
	int ret;

	ceph_decode_32_safe(p, end, arg->weight_set_size, e_inval);
	if (arg->weight_set_size) {
		u32 i;

		arg->weight_set = kmalloc_array(arg->weight_set_size,
						sizeof(*arg->weight_set),
						GFP_NOIO);
		if (!arg->weight_set)
			return -ENOMEM;

		for (i = 0; i < arg->weight_set_size; i++) {
			struct crush_weight_set *w = &arg->weight_set[i];

			w->weights = decode_array_32_alloc(p, end, &w->size);
			if (IS_ERR(w->weights)) {
				ret = PTR_ERR(w->weights);
				w->weights = NULL;
				return ret;
			}
		}
	}

	arg->ids = decode_array_32_alloc(p, end, &arg->ids_size);
	if (IS_ERR(arg->ids)) {
		ret = PTR_ERR(arg->ids);
		arg->ids = NULL;
		return ret;
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int decode_choose_args(void **p, void *end, struct crush_map *c)
{
	struct crush_choose_arg_map *arg_map = NULL;
	u32 num_choose_arg_maps, num_buckets;
	int ret;

	ceph_decode_32_safe(p, end, num_choose_arg_maps, e_inval);
	while (num_choose_arg_maps--) {
		arg_map = alloc_choose_arg_map();
		if (!arg_map) {
			ret = -ENOMEM;
			goto fail;
		}

		ceph_decode_64_safe(p, end, arg_map->choose_args_index,
				    e_inval);
		arg_map->size = c->max_buckets;
		arg_map->args = kcalloc(arg_map->size, sizeof(*arg_map->args),
					GFP_NOIO);
		if (!arg_map->args) {
			ret = -ENOMEM;
			goto fail;
		}

		ceph_decode_32_safe(p, end, num_buckets, e_inval);
		while (num_buckets--) {
			struct crush_choose_arg *arg;
			u32 bucket_index;

			ceph_decode_32_safe(p, end, bucket_index, e_inval);
			if (bucket_index >= arg_map->size)
				goto e_inval;

			arg = &arg_map->args[bucket_index];
			ret = decode_choose_arg(p, end, arg);
			if (ret)
				goto fail;

			if (arg->ids_size &&
			    arg->ids_size != c->buckets[bucket_index]->size)
				goto e_inval;
		}

		insert_choose_arg_map(&c->choose_args, arg_map);
	}

	return 0;

e_inval:
	ret = -EINVAL;
fail:
	free_choose_arg_map(arg_map);
	return ret;
}

static void crush_finalize(struct crush_map *c)
{
	__s32 b;

	/* Space for the array of pointers to per-bucket workspace */
	c->working_size = sizeof(struct crush_work) +
	    c->max_buckets * sizeof(struct crush_work_bucket *);

	for (b = 0; b < c->max_buckets; b++) {
		if (!c->buckets[b])
			continue;

		switch (c->buckets[b]->alg) {
		default:
			/*
			 * The base case, permutation variables and
			 * the pointer to the permutation array.
			 */
			c->working_size += sizeof(struct crush_work_bucket);
			break;
		}
		/* Every bucket has a permutation array. */
		c->working_size += c->buckets[b]->size * sizeof(__u32);
	}
}

static struct crush_map *crush_decode(void *pbyval, void *end)
{
	struct crush_map *c;
	int err;
	int i, j;
	void **p = &pbyval;
	void *start = pbyval;
	u32 magic;

	dout("crush_decode %p to %p len %d\n", *p, end, (int)(end - *p));

	c = kzalloc(sizeof(*c), GFP_NOFS);
	if (c == NULL)
		return ERR_PTR(-ENOMEM);

	c->type_names = RB_ROOT;
	c->names = RB_ROOT;
	c->choose_args = RB_ROOT;

        /* set tunables to default values */
        c->choose_local_tries = 2;
        c->choose_local_fallback_tries = 5;
        c->choose_total_tries = 19;
	c->chooseleaf_descend_once = 0;

	ceph_decode_need(p, end, 4*sizeof(u32), bad);
	magic = ceph_decode_32(p);
	if (magic != CRUSH_MAGIC) {
		pr_err("crush_decode magic %x != current %x\n",
		       (unsigned int)magic, (unsigned int)CRUSH_MAGIC);
		goto bad;
	}
	c->max_buckets = ceph_decode_32(p);
	c->max_rules = ceph_decode_32(p);
	c->max_devices = ceph_decode_32(p);

	c->buckets = kcalloc(c->max_buckets, sizeof(*c->buckets), GFP_NOFS);
	if (c->buckets == NULL)
		goto badmem;
	c->rules = kcalloc(c->max_rules, sizeof(*c->rules), GFP_NOFS);
	if (c->rules == NULL)
		goto badmem;

	/* buckets */
	for (i = 0; i < c->max_buckets; i++) {
		int size = 0;
		u32 alg;
		struct crush_bucket *b;

		ceph_decode_32_safe(p, end, alg, bad);
		if (alg == 0) {
			c->buckets[i] = NULL;
			continue;
		}
		dout("crush_decode bucket %d off %x %p to %p\n",
		     i, (int)(*p-start), *p, end);

		switch (alg) {
		case CRUSH_BUCKET_UNIFORM:
			size = sizeof(struct crush_bucket_uniform);
			break;
		case CRUSH_BUCKET_LIST:
			size = sizeof(struct crush_bucket_list);
			break;
		case CRUSH_BUCKET_TREE:
			size = sizeof(struct crush_bucket_tree);
			break;
		case CRUSH_BUCKET_STRAW:
			size = sizeof(struct crush_bucket_straw);
			break;
		case CRUSH_BUCKET_STRAW2:
			size = sizeof(struct crush_bucket_straw2);
			break;
		default:
			goto bad;
		}
		BUG_ON(size == 0);
		b = c->buckets[i] = kzalloc(size, GFP_NOFS);
		if (b == NULL)
			goto badmem;

		ceph_decode_need(p, end, 4*sizeof(u32), bad);
		b->id = ceph_decode_32(p);
		b->type = ceph_decode_16(p);
		b->alg = ceph_decode_8(p);
		b->hash = ceph_decode_8(p);
		b->weight = ceph_decode_32(p);
		b->size = ceph_decode_32(p);

		dout("crush_decode bucket size %d off %x %p to %p\n",
		     b->size, (int)(*p-start), *p, end);

		b->items = kcalloc(b->size, sizeof(__s32), GFP_NOFS);
		if (b->items == NULL)
			goto badmem;

		ceph_decode_need(p, end, b->size*sizeof(u32), bad);
		for (j = 0; j < b->size; j++)
			b->items[j] = ceph_decode_32(p);

		switch (b->alg) {
		case CRUSH_BUCKET_UNIFORM:
			err = crush_decode_uniform_bucket(p, end,
				  (struct crush_bucket_uniform *)b);
			if (err < 0)
				goto fail;
			break;
		case CRUSH_BUCKET_LIST:
			err = crush_decode_list_bucket(p, end,
			       (struct crush_bucket_list *)b);
			if (err < 0)
				goto fail;
			break;
		case CRUSH_BUCKET_TREE:
			err = crush_decode_tree_bucket(p, end,
				(struct crush_bucket_tree *)b);
			if (err < 0)
				goto fail;
			break;
		case CRUSH_BUCKET_STRAW:
			err = crush_decode_straw_bucket(p, end,
				(struct crush_bucket_straw *)b);
			if (err < 0)
				goto fail;
			break;
		case CRUSH_BUCKET_STRAW2:
			err = crush_decode_straw2_bucket(p, end,
				(struct crush_bucket_straw2 *)b);
			if (err < 0)
				goto fail;
			break;
		}
	}

	/* rules */
	dout("rule vec is %p\n", c->rules);
	for (i = 0; i < c->max_rules; i++) {
		u32 yes;
		struct crush_rule *r;

		ceph_decode_32_safe(p, end, yes, bad);
		if (!yes) {
			dout("crush_decode NO rule %d off %x %p to %p\n",
			     i, (int)(*p-start), *p, end);
			c->rules[i] = NULL;
			continue;
		}

		dout("crush_decode rule %d off %x %p to %p\n",
		     i, (int)(*p-start), *p, end);

		/* len */
		ceph_decode_32_safe(p, end, yes, bad);
#if BITS_PER_LONG == 32
		if (yes > (ULONG_MAX - sizeof(*r))
			  / sizeof(struct crush_rule_step))
			goto bad;
#endif
		r = kmalloc(struct_size(r, steps, yes), GFP_NOFS);
		c->rules[i] = r;
		if (r == NULL)
			goto badmem;
		dout(" rule %d is at %p\n", i, r);
		r->len = yes;
		ceph_decode_copy_safe(p, end, &r->mask, 4, bad); /* 4 u8's */
		ceph_decode_need(p, end, r->len*3*sizeof(u32), bad);
		for (j = 0; j < r->len; j++) {
			r->steps[j].op = ceph_decode_32(p);
			r->steps[j].arg1 = ceph_decode_32(p);
			r->steps[j].arg2 = ceph_decode_32(p);
		}
	}

	err = decode_crush_names(p, end, &c->type_names);
	if (err)
		goto fail;

	err = decode_crush_names(p, end, &c->names);
	if (err)
		goto fail;

	ceph_decode_skip_map(p, end, 32, string, bad); /* rule_name_map */

        /* tunables */
        ceph_decode_need(p, end, 3*sizeof(u32), done);
        c->choose_local_tries = ceph_decode_32(p);
        c->choose_local_fallback_tries =  ceph_decode_32(p);
        c->choose_total_tries = ceph_decode_32(p);
        dout("crush decode tunable choose_local_tries = %d\n",
             c->choose_local_tries);
        dout("crush decode tunable choose_local_fallback_tries = %d\n",
             c->choose_local_fallback_tries);
        dout("crush decode tunable choose_total_tries = %d\n",
             c->choose_total_tries);

	ceph_decode_need(p, end, sizeof(u32), done);
	c->chooseleaf_descend_once = ceph_decode_32(p);
	dout("crush decode tunable chooseleaf_descend_once = %d\n",
	     c->chooseleaf_descend_once);

	ceph_decode_need(p, end, sizeof(u8), done);
	c->chooseleaf_vary_r = ceph_decode_8(p);
	dout("crush decode tunable chooseleaf_vary_r = %d\n",
	     c->chooseleaf_vary_r);

	/* skip straw_calc_version, allowed_bucket_algs */
	ceph_decode_need(p, end, sizeof(u8) + sizeof(u32), done);
	*p += sizeof(u8) + sizeof(u32);

	ceph_decode_need(p, end, sizeof(u8), done);
	c->chooseleaf_stable = ceph_decode_8(p);
	dout("crush decode tunable chooseleaf_stable = %d\n",
	     c->chooseleaf_stable);

	if (*p != end) {
		/* class_map */
		ceph_decode_skip_map(p, end, 32, 32, bad);
		/* class_name */
		ceph_decode_skip_map(p, end, 32, string, bad);
		/* class_bucket */
		ceph_decode_skip_map_of_map(p, end, 32, 32, 32, bad);
	}

	if (*p != end) {
		err = decode_choose_args(p, end, c);
		if (err)
			goto fail;
	}

done:
	crush_finalize(c);
	dout("crush_decode success\n");
	return c;

badmem:
	err = -ENOMEM;
fail:
	dout("crush_decode fail %d\n", err);
	crush_destroy(c);
	return ERR_PTR(err);

bad:
	err = -EINVAL;
	goto fail;
}

int ceph_pg_compare(const struct ceph_pg *lhs, const struct ceph_pg *rhs)
{
	if (lhs->pool < rhs->pool)
		return -1;
	if (lhs->pool > rhs->pool)
		return 1;
	if (lhs->seed < rhs->seed)
		return -1;
	if (lhs->seed > rhs->seed)
		return 1;

	return 0;
}

int ceph_spg_compare(const struct ceph_spg *lhs, const struct ceph_spg *rhs)
{
	int ret;

	ret = ceph_pg_compare(&lhs->pgid, &rhs->pgid);
	if (ret)
		return ret;

	if (lhs->shard < rhs->shard)
		return -1;
	if (lhs->shard > rhs->shard)
		return 1;

	return 0;
}

static struct ceph_pg_mapping *alloc_pg_mapping(size_t payload_len)
{
	struct ceph_pg_mapping *pg;

	pg = kmalloc(sizeof(*pg) + payload_len, GFP_NOIO);
	if (!pg)
		return NULL;

	RB_CLEAR_NODE(&pg->node);
	return pg;
}

static void free_pg_mapping(struct ceph_pg_mapping *pg)
{
	WARN_ON(!RB_EMPTY_NODE(&pg->node));

	kfree(pg);
}

/*
 * rbtree of pg_mapping for handling pg_temp (explicit mapping of pgid
 * to a set of osds) and primary_temp (explicit primary setting)
 */
DEFINE_RB_FUNCS2(pg_mapping, struct ceph_pg_mapping, pgid, ceph_pg_compare,
		 RB_BYPTR, const struct ceph_pg *, node)

/*
 * rbtree of pg pool info
 */
DEFINE_RB_FUNCS(pg_pool, struct ceph_pg_pool_info, id, node)

struct ceph_pg_pool_info *ceph_pg_pool_by_id(struct ceph_osdmap *map, u64 id)
{
	return lookup_pg_pool(&map->pg_pools, id);
}

const char *ceph_pg_pool_name_by_id(struct ceph_osdmap *map, u64 id)
{
	struct ceph_pg_pool_info *pi;

	if (id == CEPH_NOPOOL)
		return NULL;

	if (WARN_ON_ONCE(id > (u64) INT_MAX))
		return NULL;

	pi = lookup_pg_pool(&map->pg_pools, id);
	return pi ? pi->name : NULL;
}
EXPORT_SYMBOL(ceph_pg_pool_name_by_id);

int ceph_pg_poolid_by_name(struct ceph_osdmap *map, const char *name)
{
	struct rb_node *rbp;

	for (rbp = rb_first(&map->pg_pools); rbp; rbp = rb_next(rbp)) {
		struct ceph_pg_pool_info *pi =
			rb_entry(rbp, struct ceph_pg_pool_info, node);
		if (pi->name && strcmp(pi->name, name) == 0)
			return pi->id;
	}
	return -ENOENT;
}
EXPORT_SYMBOL(ceph_pg_poolid_by_name);

u64 ceph_pg_pool_flags(struct ceph_osdmap *map, u64 id)
{
	struct ceph_pg_pool_info *pi;

	pi = lookup_pg_pool(&map->pg_pools, id);
	return pi ? pi->flags : 0;
}
EXPORT_SYMBOL(ceph_pg_pool_flags);

static void __remove_pg_pool(struct rb_root *root, struct ceph_pg_pool_info *pi)
{
	erase_pg_pool(root, pi);
	kfree(pi->name);
	kfree(pi);
}

static int decode_pool(void **p, void *end, struct ceph_pg_pool_info *pi)
{
	u8 ev, cv;
	unsigned len, num;
	void *pool_end;

	ceph_decode_need(p, end, 2 + 4, bad);
	ev = ceph_decode_8(p);  /* encoding version */
	cv = ceph_decode_8(p); /* compat version */
	if (ev < 5) {
		pr_warn("got v %d < 5 cv %d of ceph_pg_pool\n", ev, cv);
		return -EINVAL;
	}
	if (cv > 9) {
		pr_warn("got v %d cv %d > 9 of ceph_pg_pool\n", ev, cv);
		return -EINVAL;
	}
	len = ceph_decode_32(p);
	ceph_decode_need(p, end, len, bad);
	pool_end = *p + len;

	pi->type = ceph_decode_8(p);
	pi->size = ceph_decode_8(p);
	pi->crush_ruleset = ceph_decode_8(p);
	pi->object_hash = ceph_decode_8(p);

	pi->pg_num = ceph_decode_32(p);
	pi->pgp_num = ceph_decode_32(p);

	*p += 4 + 4;  /* skip lpg* */
	*p += 4;      /* skip last_change */
	*p += 8 + 4;  /* skip snap_seq, snap_epoch */

	/* skip snaps */
	num = ceph_decode_32(p);
	while (num--) {
		*p += 8;  /* snapid key */
		*p += 1 + 1; /* versions */
		len = ceph_decode_32(p);
		*p += len;
	}

	/* skip removed_snaps */
	num = ceph_decode_32(p);
	*p += num * (8 + 8);

	*p += 8;  /* skip auid */
	pi->flags = ceph_decode_64(p);
	*p += 4;  /* skip crash_replay_interval */

	if (ev >= 7)
		pi->min_size = ceph_decode_8(p);
	else
		pi->min_size = pi->size - pi->size / 2;

	if (ev >= 8)
		*p += 8 + 8;  /* skip quota_max_* */

	if (ev >= 9) {
		/* skip tiers */
		num = ceph_decode_32(p);
		*p += num * 8;

		*p += 8;  /* skip tier_of */
		*p += 1;  /* skip cache_mode */

		pi->read_tier = ceph_decode_64(p);
		pi->write_tier = ceph_decode_64(p);
	} else {
		pi->read_tier = -1;
		pi->write_tier = -1;
	}

	if (ev >= 10) {
		/* skip properties */
		num = ceph_decode_32(p);
		while (num--) {
			len = ceph_decode_32(p);
			*p += len; /* key */
			len = ceph_decode_32(p);
			*p += len; /* val */
		}
	}

	if (ev >= 11) {
		/* skip hit_set_params */
		*p += 1 + 1; /* versions */
		len = ceph_decode_32(p);
		*p += len;

		*p += 4; /* skip hit_set_period */
		*p += 4; /* skip hit_set_count */
	}

	if (ev >= 12)
		*p += 4; /* skip stripe_width */

	if (ev >= 13) {
		*p += 8; /* skip target_max_bytes */
		*p += 8; /* skip target_max_objects */
		*p += 4; /* skip cache_target_dirty_ratio_micro */
		*p += 4; /* skip cache_target_full_ratio_micro */
		*p += 4; /* skip cache_min_flush_age */
		*p += 4; /* skip cache_min_evict_age */
	}

	if (ev >=  14) {
		/* skip erasure_code_profile */
		len = ceph_decode_32(p);
		*p += len;
	}

	/*
	 * last_force_op_resend_preluminous, will be overridden if the
	 * map was encoded with RESEND_ON_SPLIT
	 */
	if (ev >= 15)
		pi->last_force_request_resend = ceph_decode_32(p);
	else
		pi->last_force_request_resend = 0;

	if (ev >= 16)
		*p += 4; /* skip min_read_recency_for_promote */

	if (ev >= 17)
		*p += 8; /* skip expected_num_objects */

	if (ev >= 19)
		*p += 4; /* skip cache_target_dirty_high_ratio_micro */

	if (ev >= 20)
		*p += 4; /* skip min_write_recency_for_promote */

	if (ev >= 21)
		*p += 1; /* skip use_gmt_hitset */

	if (ev >= 22)
		*p += 1; /* skip fast_read */

	if (ev >= 23) {
		*p += 4; /* skip hit_set_grade_decay_rate */
		*p += 4; /* skip hit_set_search_last_n */
	}

	if (ev >= 24) {
		/* skip opts */
		*p += 1 + 1; /* versions */
		len = ceph_decode_32(p);
		*p += len;
	}

	if (ev >= 25)
		pi->last_force_request_resend = ceph_decode_32(p);

	/* ignore the rest */

	*p = pool_end;
	calc_pg_masks(pi);
	return 0;

bad:
	return -EINVAL;
}

static int decode_pool_names(void **p, void *end, struct ceph_osdmap *map)
{
	struct ceph_pg_pool_info *pi;
	u32 num, len;
	u64 pool;

	ceph_decode_32_safe(p, end, num, bad);
	dout(" %d pool names\n", num);
	while (num--) {
		ceph_decode_64_safe(p, end, pool, bad);
		ceph_decode_32_safe(p, end, len, bad);
		dout("  pool %llu len %d\n", pool, len);
		ceph_decode_need(p, end, len, bad);
		pi = lookup_pg_pool(&map->pg_pools, pool);
		if (pi) {
			char *name = kstrndup(*p, len, GFP_NOFS);

			if (!name)
				return -ENOMEM;
			kfree(pi->name);
			pi->name = name;
			dout("  name is %s\n", pi->name);
		}
		*p += len;
	}
	return 0;

bad:
	return -EINVAL;
}

/*
 * CRUSH workspaces
 *
 * workspace_manager framework borrowed from fs/btrfs/compression.c.
 * Two simplifications: there is only one type of workspace and there
 * is always at least one workspace.
 */
static struct crush_work *alloc_workspace(const struct crush_map *c)
{
	struct crush_work *work;
	size_t work_size;

	WARN_ON(!c->working_size);
	work_size = crush_work_size(c, CEPH_PG_MAX_SIZE);
	dout("%s work_size %zu bytes\n", __func__, work_size);

	work = kvmalloc(work_size, GFP_NOIO);
	if (!work)
		return NULL;

	INIT_LIST_HEAD(&work->item);
	crush_init_workspace(c, work);
	return work;
}

static void free_workspace(struct crush_work *work)
{
	WARN_ON(!list_empty(&work->item));
	kvfree(work);
}

static void init_workspace_manager(struct workspace_manager *wsm)
{
	INIT_LIST_HEAD(&wsm->idle_ws);
	spin_lock_init(&wsm->ws_lock);
	atomic_set(&wsm->total_ws, 0);
	wsm->free_ws = 0;
	init_waitqueue_head(&wsm->ws_wait);
}

static void add_initial_workspace(struct workspace_manager *wsm,
				  struct crush_work *work)
{
	WARN_ON(!list_empty(&wsm->idle_ws));

	list_add(&work->item, &wsm->idle_ws);
	atomic_set(&wsm->total_ws, 1);
	wsm->free_ws = 1;
}

static void cleanup_workspace_manager(struct workspace_manager *wsm)
{
	struct crush_work *work;

	while (!list_empty(&wsm->idle_ws)) {
		work = list_first_entry(&wsm->idle_ws, struct crush_work,
					item);
		list_del_init(&work->item);
		free_workspace(work);
	}
	atomic_set(&wsm->total_ws, 0);
	wsm->free_ws = 0;
}

/*
 * Finds an available workspace or allocates a new one.  If it's not
 * possible to allocate a new one, waits until there is one.
 */
static struct crush_work *get_workspace(struct workspace_manager *wsm,
					const struct crush_map *c)
{
	struct crush_work *work;
	int cpus = num_online_cpus();

again:
	spin_lock(&wsm->ws_lock);
	if (!list_empty(&wsm->idle_ws)) {
		work = list_first_entry(&wsm->idle_ws, struct crush_work,
					item);
		list_del_init(&work->item);
		wsm->free_ws--;
		spin_unlock(&wsm->ws_lock);
		return work;

	}
	if (atomic_read(&wsm->total_ws) > cpus) {
		DEFINE_WAIT(wait);

		spin_unlock(&wsm->ws_lock);
		prepare_to_wait(&wsm->ws_wait, &wait, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&wsm->total_ws) > cpus && !wsm->free_ws)
			schedule();
		finish_wait(&wsm->ws_wait, &wait);
		goto again;
	}
	atomic_inc(&wsm->total_ws);
	spin_unlock(&wsm->ws_lock);

	work = alloc_workspace(c);
	if (!work) {
		atomic_dec(&wsm->total_ws);
		wake_up(&wsm->ws_wait);

		/*
		 * Do not return the error but go back to waiting.  We
		 * have the initial workspace and the CRUSH computation
		 * time is bounded so we will get it eventually.
		 */
		WARN_ON(atomic_read(&wsm->total_ws) < 1);
		goto again;
	}
	return work;
}

/*
 * Puts a workspace back on the list or frees it if we have enough
 * idle ones sitting around.
 */
static void put_workspace(struct workspace_manager *wsm,
			  struct crush_work *work)
{
	spin_lock(&wsm->ws_lock);
	if (wsm->free_ws <= num_online_cpus()) {
		list_add(&work->item, &wsm->idle_ws);
		wsm->free_ws++;
		spin_unlock(&wsm->ws_lock);
		goto wake;
	}
	spin_unlock(&wsm->ws_lock);

	free_workspace(work);
	atomic_dec(&wsm->total_ws);
wake:
	if (wq_has_sleeper(&wsm->ws_wait))
		wake_up(&wsm->ws_wait);
}

/*
 * osd map
 */
struct ceph_osdmap *ceph_osdmap_alloc(void)
{
	struct ceph_osdmap *map;

	map = kzalloc(sizeof(*map), GFP_NOIO);
	if (!map)
		return NULL;

	map->pg_pools = RB_ROOT;
	map->pool_max = -1;
	map->pg_temp = RB_ROOT;
	map->primary_temp = RB_ROOT;
	map->pg_upmap = RB_ROOT;
	map->pg_upmap_items = RB_ROOT;

	init_workspace_manager(&map->crush_wsm);

	return map;
}

void ceph_osdmap_destroy(struct ceph_osdmap *map)
{
	dout("osdmap_destroy %p\n", map);

	if (map->crush)
		crush_destroy(map->crush);
	cleanup_workspace_manager(&map->crush_wsm);

	while (!RB_EMPTY_ROOT(&map->pg_temp)) {
		struct ceph_pg_mapping *pg =
			rb_entry(rb_first(&map->pg_temp),
				 struct ceph_pg_mapping, node);
		erase_pg_mapping(&map->pg_temp, pg);
		free_pg_mapping(pg);
	}
	while (!RB_EMPTY_ROOT(&map->primary_temp)) {
		struct ceph_pg_mapping *pg =
			rb_entry(rb_first(&map->primary_temp),
				 struct ceph_pg_mapping, node);
		erase_pg_mapping(&map->primary_temp, pg);
		free_pg_mapping(pg);
	}
	while (!RB_EMPTY_ROOT(&map->pg_upmap)) {
		struct ceph_pg_mapping *pg =
			rb_entry(rb_first(&map->pg_upmap),
				 struct ceph_pg_mapping, node);
		rb_erase(&pg->node, &map->pg_upmap);
		kfree(pg);
	}
	while (!RB_EMPTY_ROOT(&map->pg_upmap_items)) {
		struct ceph_pg_mapping *pg =
			rb_entry(rb_first(&map->pg_upmap_items),
				 struct ceph_pg_mapping, node);
		rb_erase(&pg->node, &map->pg_upmap_items);
		kfree(pg);
	}
	while (!RB_EMPTY_ROOT(&map->pg_pools)) {
		struct ceph_pg_pool_info *pi =
			rb_entry(rb_first(&map->pg_pools),
				 struct ceph_pg_pool_info, node);
		__remove_pg_pool(&map->pg_pools, pi);
	}
	kvfree(map->osd_state);
	kvfree(map->osd_weight);
	kvfree(map->osd_addr);
	kvfree(map->osd_primary_affinity);
	kfree(map);
}

/*
 * Adjust max_osd value, (re)allocate arrays.
 *
 * The new elements are properly initialized.
 */
static int osdmap_set_max_osd(struct ceph_osdmap *map, u32 max)
{
	u32 *state;
	u32 *weight;
	struct ceph_entity_addr *addr;
	u32 to_copy;
	int i;

	dout("%s old %u new %u\n", __func__, map->max_osd, max);
	if (max == map->max_osd)
		return 0;

	state = kvmalloc(array_size(max, sizeof(*state)), GFP_NOFS);
	weight = kvmalloc(array_size(max, sizeof(*weight)), GFP_NOFS);
	addr = kvmalloc(array_size(max, sizeof(*addr)), GFP_NOFS);
	if (!state || !weight || !addr) {
		kvfree(state);
		kvfree(weight);
		kvfree(addr);
		return -ENOMEM;
	}

	to_copy = min(map->max_osd, max);
	if (map->osd_state) {
		memcpy(state, map->osd_state, to_copy * sizeof(*state));
		memcpy(weight, map->osd_weight, to_copy * sizeof(*weight));
		memcpy(addr, map->osd_addr, to_copy * sizeof(*addr));
		kvfree(map->osd_state);
		kvfree(map->osd_weight);
		kvfree(map->osd_addr);
	}

	map->osd_state = state;
	map->osd_weight = weight;
	map->osd_addr = addr;
	for (i = map->max_osd; i < max; i++) {
		map->osd_state[i] = 0;
		map->osd_weight[i] = CEPH_OSD_OUT;
		memset(map->osd_addr + i, 0, sizeof(*map->osd_addr));
	}

	if (map->osd_primary_affinity) {
		u32 *affinity;

		affinity = kvmalloc(array_size(max, sizeof(*affinity)),
					 GFP_NOFS);
		if (!affinity)
			return -ENOMEM;

		memcpy(affinity, map->osd_primary_affinity,
		       to_copy * sizeof(*affinity));
		kvfree(map->osd_primary_affinity);

		map->osd_primary_affinity = affinity;
		for (i = map->max_osd; i < max; i++)
			map->osd_primary_affinity[i] =
			    CEPH_OSD_DEFAULT_PRIMARY_AFFINITY;
	}

	map->max_osd = max;

	return 0;
}

static int osdmap_set_crush(struct ceph_osdmap *map, struct crush_map *crush)
{
	struct crush_work *work;

	if (IS_ERR(crush))
		return PTR_ERR(crush);

	work = alloc_workspace(crush);
	if (!work) {
		crush_destroy(crush);
		return -ENOMEM;
	}

	if (map->crush)
		crush_destroy(map->crush);
	cleanup_workspace_manager(&map->crush_wsm);
	map->crush = crush;
	add_initial_workspace(&map->crush_wsm, work);
	return 0;
}

#define OSDMAP_WRAPPER_COMPAT_VER	7
#define OSDMAP_CLIENT_DATA_COMPAT_VER	1

/*
 * Return 0 or error.  On success, *v is set to 0 for old (v6) osdmaps,
 * to struct_v of the client_data section for new (v7 and above)
 * osdmaps.
 */
static int get_osdmap_client_data_v(void **p, void *end,
				    const char *prefix, u8 *v)
{
	u8 struct_v;

	ceph_decode_8_safe(p, end, struct_v, e_inval);
	if (struct_v >= 7) {
		u8 struct_compat;

		ceph_decode_8_safe(p, end, struct_compat, e_inval);
		if (struct_compat > OSDMAP_WRAPPER_COMPAT_VER) {
			pr_warn("got v %d cv %d > %d of %s ceph_osdmap\n",
				struct_v, struct_compat,
				OSDMAP_WRAPPER_COMPAT_VER, prefix);
			return -EINVAL;
		}
		*p += 4; /* ignore wrapper struct_len */

		ceph_decode_8_safe(p, end, struct_v, e_inval);
		ceph_decode_8_safe(p, end, struct_compat, e_inval);
		if (struct_compat > OSDMAP_CLIENT_DATA_COMPAT_VER) {
			pr_warn("got v %d cv %d > %d of %s ceph_osdmap client data\n",
				struct_v, struct_compat,
				OSDMAP_CLIENT_DATA_COMPAT_VER, prefix);
			return -EINVAL;
		}
		*p += 4; /* ignore client data struct_len */
	} else {
		u16 version;

		*p -= 1;
		ceph_decode_16_safe(p, end, version, e_inval);
		if (version < 6) {
			pr_warn("got v %d < 6 of %s ceph_osdmap\n",
				version, prefix);
			return -EINVAL;
		}

		/* old osdmap encoding */
		struct_v = 0;
	}

	*v = struct_v;
	return 0;

e_inval:
	return -EINVAL;
}

static int __decode_pools(void **p, void *end, struct ceph_osdmap *map,
			  bool incremental)
{
	u32 n;

	ceph_decode_32_safe(p, end, n, e_inval);
	while (n--) {
		struct ceph_pg_pool_info *pi;
		u64 pool;
		int ret;

		ceph_decode_64_safe(p, end, pool, e_inval);

		pi = lookup_pg_pool(&map->pg_pools, pool);
		if (!incremental || !pi) {
			pi = kzalloc(sizeof(*pi), GFP_NOFS);
			if (!pi)
				return -ENOMEM;

			RB_CLEAR_NODE(&pi->node);
			pi->id = pool;

			if (!__insert_pg_pool(&map->pg_pools, pi)) {
				kfree(pi);
				return -EEXIST;
			}
		}

		ret = decode_pool(p, end, pi);
		if (ret)
			return ret;
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int decode_pools(void **p, void *end, struct ceph_osdmap *map)
{
	return __decode_pools(p, end, map, false);
}

static int decode_new_pools(void **p, void *end, struct ceph_osdmap *map)
{
	return __decode_pools(p, end, map, true);
}

typedef struct ceph_pg_mapping *(*decode_mapping_fn_t)(void **, void *, bool);

static int decode_pg_mapping(void **p, void *end, struct rb_root *mapping_root,
			     decode_mapping_fn_t fn, bool incremental)
{
	u32 n;

	WARN_ON(!incremental && !fn);

	ceph_decode_32_safe(p, end, n, e_inval);
	while (n--) {
		struct ceph_pg_mapping *pg;
		struct ceph_pg pgid;
		int ret;

		ret = ceph_decode_pgid(p, end, &pgid);
		if (ret)
			return ret;

		pg = lookup_pg_mapping(mapping_root, &pgid);
		if (pg) {
			WARN_ON(!incremental);
			erase_pg_mapping(mapping_root, pg);
			free_pg_mapping(pg);
		}

		if (fn) {
			pg = fn(p, end, incremental);
			if (IS_ERR(pg))
				return PTR_ERR(pg);

			if (pg) {
				pg->pgid = pgid; /* struct */
				insert_pg_mapping(mapping_root, pg);
			}
		}
	}

	return 0;

e_inval:
	return -EINVAL;
}

static struct ceph_pg_mapping *__decode_pg_temp(void **p, void *end,
						bool incremental)
{
	struct ceph_pg_mapping *pg;
	u32 len, i;

	ceph_decode_32_safe(p, end, len, e_inval);
	if (len == 0 && incremental)
		return NULL;	/* new_pg_temp: [] to remove */
	if (len > (SIZE_MAX - sizeof(*pg)) / sizeof(u32))
		return ERR_PTR(-EINVAL);

	ceph_decode_need(p, end, len * sizeof(u32), e_inval);
	pg = alloc_pg_mapping(len * sizeof(u32));
	if (!pg)
		return ERR_PTR(-ENOMEM);

	pg->pg_temp.len = len;
	for (i = 0; i < len; i++)
		pg->pg_temp.osds[i] = ceph_decode_32(p);

	return pg;

e_inval:
	return ERR_PTR(-EINVAL);
}

static int decode_pg_temp(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_temp, __decode_pg_temp,
				 false);
}

static int decode_new_pg_temp(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_temp, __decode_pg_temp,
				 true);
}

static struct ceph_pg_mapping *__decode_primary_temp(void **p, void *end,
						     bool incremental)
{
	struct ceph_pg_mapping *pg;
	u32 osd;

	ceph_decode_32_safe(p, end, osd, e_inval);
	if (osd == (u32)-1 && incremental)
		return NULL;	/* new_primary_temp: -1 to remove */

	pg = alloc_pg_mapping(0);
	if (!pg)
		return ERR_PTR(-ENOMEM);

	pg->primary_temp.osd = osd;
	return pg;

e_inval:
	return ERR_PTR(-EINVAL);
}

static int decode_primary_temp(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->primary_temp,
				 __decode_primary_temp, false);
}

static int decode_new_primary_temp(void **p, void *end,
				   struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->primary_temp,
				 __decode_primary_temp, true);
}

u32 ceph_get_primary_affinity(struct ceph_osdmap *map, int osd)
{
	BUG_ON(osd >= map->max_osd);

	if (!map->osd_primary_affinity)
		return CEPH_OSD_DEFAULT_PRIMARY_AFFINITY;

	return map->osd_primary_affinity[osd];
}

static int set_primary_affinity(struct ceph_osdmap *map, int osd, u32 aff)
{
	BUG_ON(osd >= map->max_osd);

	if (!map->osd_primary_affinity) {
		int i;

		map->osd_primary_affinity = kvmalloc(
		    array_size(map->max_osd, sizeof(*map->osd_primary_affinity)),
		    GFP_NOFS);
		if (!map->osd_primary_affinity)
			return -ENOMEM;

		for (i = 0; i < map->max_osd; i++)
			map->osd_primary_affinity[i] =
			    CEPH_OSD_DEFAULT_PRIMARY_AFFINITY;
	}

	map->osd_primary_affinity[osd] = aff;

	return 0;
}

static int decode_primary_affinity(void **p, void *end,
				   struct ceph_osdmap *map)
{
	u32 len, i;

	ceph_decode_32_safe(p, end, len, e_inval);
	if (len == 0) {
		kvfree(map->osd_primary_affinity);
		map->osd_primary_affinity = NULL;
		return 0;
	}
	if (len != map->max_osd)
		goto e_inval;

	ceph_decode_need(p, end, map->max_osd*sizeof(u32), e_inval);

	for (i = 0; i < map->max_osd; i++) {
		int ret;

		ret = set_primary_affinity(map, i, ceph_decode_32(p));
		if (ret)
			return ret;
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int decode_new_primary_affinity(void **p, void *end,
				       struct ceph_osdmap *map)
{
	u32 n;

	ceph_decode_32_safe(p, end, n, e_inval);
	while (n--) {
		u32 osd, aff;
		int ret;

		ceph_decode_32_safe(p, end, osd, e_inval);
		ceph_decode_32_safe(p, end, aff, e_inval);

		ret = set_primary_affinity(map, osd, aff);
		if (ret)
			return ret;

		pr_info("osd%d primary-affinity 0x%x\n", osd, aff);
	}

	return 0;

e_inval:
	return -EINVAL;
}

static struct ceph_pg_mapping *__decode_pg_upmap(void **p, void *end,
						 bool __unused)
{
	return __decode_pg_temp(p, end, false);
}

static int decode_pg_upmap(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_upmap, __decode_pg_upmap,
				 false);
}

static int decode_new_pg_upmap(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_upmap, __decode_pg_upmap,
				 true);
}

static int decode_old_pg_upmap(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_upmap, NULL, true);
}

static struct ceph_pg_mapping *__decode_pg_upmap_items(void **p, void *end,
						       bool __unused)
{
	struct ceph_pg_mapping *pg;
	u32 len, i;

	ceph_decode_32_safe(p, end, len, e_inval);
	if (len > (SIZE_MAX - sizeof(*pg)) / (2 * sizeof(u32)))
		return ERR_PTR(-EINVAL);

	ceph_decode_need(p, end, 2 * len * sizeof(u32), e_inval);
	pg = alloc_pg_mapping(2 * len * sizeof(u32));
	if (!pg)
		return ERR_PTR(-ENOMEM);

	pg->pg_upmap_items.len = len;
	for (i = 0; i < len; i++) {
		pg->pg_upmap_items.from_to[i][0] = ceph_decode_32(p);
		pg->pg_upmap_items.from_to[i][1] = ceph_decode_32(p);
	}

	return pg;

e_inval:
	return ERR_PTR(-EINVAL);
}

static int decode_pg_upmap_items(void **p, void *end, struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_upmap_items,
				 __decode_pg_upmap_items, false);
}

static int decode_new_pg_upmap_items(void **p, void *end,
				     struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_upmap_items,
				 __decode_pg_upmap_items, true);
}

static int decode_old_pg_upmap_items(void **p, void *end,
				     struct ceph_osdmap *map)
{
	return decode_pg_mapping(p, end, &map->pg_upmap_items, NULL, true);
}

/*
 * decode a full map.
 */
static int osdmap_decode(void **p, void *end, bool msgr2,
			 struct ceph_osdmap *map)
{
	u8 struct_v;
	u32 epoch = 0;
	void *start = *p;
	u32 max;
	u32 len, i;
	int err;

	dout("%s %p to %p len %d\n", __func__, *p, end, (int)(end - *p));

	err = get_osdmap_client_data_v(p, end, "full", &struct_v);
	if (err)
		goto bad;

	/* fsid, epoch, created, modified */
	ceph_decode_need(p, end, sizeof(map->fsid) + sizeof(u32) +
			 sizeof(map->created) + sizeof(map->modified), e_inval);
	ceph_decode_copy(p, &map->fsid, sizeof(map->fsid));
	epoch = map->epoch = ceph_decode_32(p);
	ceph_decode_copy(p, &map->created, sizeof(map->created));
	ceph_decode_copy(p, &map->modified, sizeof(map->modified));

	/* pools */
	err = decode_pools(p, end, map);
	if (err)
		goto bad;

	/* pool_name */
	err = decode_pool_names(p, end, map);
	if (err)
		goto bad;

	ceph_decode_32_safe(p, end, map->pool_max, e_inval);

	ceph_decode_32_safe(p, end, map->flags, e_inval);

	/* max_osd */
	ceph_decode_32_safe(p, end, max, e_inval);

	/* (re)alloc osd arrays */
	err = osdmap_set_max_osd(map, max);
	if (err)
		goto bad;

	/* osd_state, osd_weight, osd_addrs->client_addr */
	ceph_decode_need(p, end, 3*sizeof(u32) +
			 map->max_osd*(struct_v >= 5 ? sizeof(u32) :
						       sizeof(u8)) +
				       sizeof(*map->osd_weight), e_inval);
	if (ceph_decode_32(p) != map->max_osd)
		goto e_inval;

	if (struct_v >= 5) {
		for (i = 0; i < map->max_osd; i++)
			map->osd_state[i] = ceph_decode_32(p);
	} else {
		for (i = 0; i < map->max_osd; i++)
			map->osd_state[i] = ceph_decode_8(p);
	}

	if (ceph_decode_32(p) != map->max_osd)
		goto e_inval;

	for (i = 0; i < map->max_osd; i++)
		map->osd_weight[i] = ceph_decode_32(p);

	if (ceph_decode_32(p) != map->max_osd)
		goto e_inval;

	for (i = 0; i < map->max_osd; i++) {
		struct ceph_entity_addr *addr = &map->osd_addr[i];

		if (struct_v >= 8)
			err = ceph_decode_entity_addrvec(p, end, msgr2, addr);
		else
			err = ceph_decode_entity_addr(p, end, addr);
		if (err)
			goto bad;

		dout("%s osd%d addr %s\n", __func__, i, ceph_pr_addr(addr));
	}

	/* pg_temp */
	err = decode_pg_temp(p, end, map);
	if (err)
		goto bad;

	/* primary_temp */
	if (struct_v >= 1) {
		err = decode_primary_temp(p, end, map);
		if (err)
			goto bad;
	}

	/* primary_affinity */
	if (struct_v >= 2) {
		err = decode_primary_affinity(p, end, map);
		if (err)
			goto bad;
	} else {
		WARN_ON(map->osd_primary_affinity);
	}

	/* crush */
	ceph_decode_32_safe(p, end, len, e_inval);
	err = osdmap_set_crush(map, crush_decode(*p, min(*p + len, end)));
	if (err)
		goto bad;

	*p += len;
	if (struct_v >= 3) {
		/* erasure_code_profiles */
		ceph_decode_skip_map_of_map(p, end, string, string, string,
					    e_inval);
	}

	if (struct_v >= 4) {
		err = decode_pg_upmap(p, end, map);
		if (err)
			goto bad;

		err = decode_pg_upmap_items(p, end, map);
		if (err)
			goto bad;
	} else {
		WARN_ON(!RB_EMPTY_ROOT(&map->pg_upmap));
		WARN_ON(!RB_EMPTY_ROOT(&map->pg_upmap_items));
	}

	/* ignore the rest */
	*p = end;

	dout("full osdmap epoch %d max_osd %d\n", map->epoch, map->max_osd);
	return 0;

e_inval:
	err = -EINVAL;
bad:
	pr_err("corrupt full osdmap (%d) epoch %d off %d (%p of %p-%p)\n",
	       err, epoch, (int)(*p - start), *p, start, end);
	print_hex_dump(KERN_DEBUG, "osdmap: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       start, end - start, true);
	return err;
}

/*
 * Allocate and decode a full map.
 */
struct ceph_osdmap *ceph_osdmap_decode(void **p, void *end, bool msgr2)
{
	struct ceph_osdmap *map;
	int ret;

	map = ceph_osdmap_alloc();
	if (!map)
		return ERR_PTR(-ENOMEM);

	ret = osdmap_decode(p, end, msgr2, map);
	if (ret) {
		ceph_osdmap_destroy(map);
		return ERR_PTR(ret);
	}

	return map;
}

/*
 * Encoding order is (new_up_client, new_state, new_weight).  Need to
 * apply in the (new_weight, new_state, new_up_client) order, because
 * an incremental map may look like e.g.
 *
 *     new_up_client: { osd=6, addr=... } # set osd_state and addr
 *     new_state: { osd=6, xorstate=EXISTS } # clear osd_state
 */
static int decode_new_up_state_weight(void **p, void *end, u8 struct_v,
				      bool msgr2, struct ceph_osdmap *map)
{
	void *new_up_client;
	void *new_state;
	void *new_weight_end;
	u32 len;
	int ret;
	int i;

	new_up_client = *p;
	ceph_decode_32_safe(p, end, len, e_inval);
	for (i = 0; i < len; ++i) {
		struct ceph_entity_addr addr;

		ceph_decode_skip_32(p, end, e_inval);
		if (struct_v >= 7)
			ret = ceph_decode_entity_addrvec(p, end, msgr2, &addr);
		else
			ret = ceph_decode_entity_addr(p, end, &addr);
		if (ret)
			return ret;
	}

	new_state = *p;
	ceph_decode_32_safe(p, end, len, e_inval);
	len *= sizeof(u32) + (struct_v >= 5 ? sizeof(u32) : sizeof(u8));
	ceph_decode_need(p, end, len, e_inval);
	*p += len;

	/* new_weight */
	ceph_decode_32_safe(p, end, len, e_inval);
	while (len--) {
		s32 osd;
		u32 w;

		ceph_decode_need(p, end, 2*sizeof(u32), e_inval);
		osd = ceph_decode_32(p);
		w = ceph_decode_32(p);
		BUG_ON(osd >= map->max_osd);
		pr_info("osd%d weight 0x%x %s\n", osd, w,
		     w == CEPH_OSD_IN ? "(in)" :
		     (w == CEPH_OSD_OUT ? "(out)" : ""));
		map->osd_weight[osd] = w;

		/*
		 * If we are marking in, set the EXISTS, and clear the
		 * AUTOOUT and NEW bits.
		 */
		if (w) {
			map->osd_state[osd] |= CEPH_OSD_EXISTS;
			map->osd_state[osd] &= ~(CEPH_OSD_AUTOOUT |
						 CEPH_OSD_NEW);
		}
	}
	new_weight_end = *p;

	/* new_state (up/down) */
	*p = new_state;
	len = ceph_decode_32(p);
	while (len--) {
		s32 osd;
		u32 xorstate;

		osd = ceph_decode_32(p);
		if (struct_v >= 5)
			xorstate = ceph_decode_32(p);
		else
			xorstate = ceph_decode_8(p);
		if (xorstate == 0)
			xorstate = CEPH_OSD_UP;
		BUG_ON(osd >= map->max_osd);
		if ((map->osd_state[osd] & CEPH_OSD_UP) &&
		    (xorstate & CEPH_OSD_UP))
			pr_info("osd%d down\n", osd);
		if ((map->osd_state[osd] & CEPH_OSD_EXISTS) &&
		    (xorstate & CEPH_OSD_EXISTS)) {
			pr_info("osd%d does not exist\n", osd);
			ret = set_primary_affinity(map, osd,
						   CEPH_OSD_DEFAULT_PRIMARY_AFFINITY);
			if (ret)
				return ret;
			memset(map->osd_addr + osd, 0, sizeof(*map->osd_addr));
			map->osd_state[osd] = 0;
		} else {
			map->osd_state[osd] ^= xorstate;
		}
	}

	/* new_up_client */
	*p = new_up_client;
	len = ceph_decode_32(p);
	while (len--) {
		s32 osd;
		struct ceph_entity_addr addr;

		osd = ceph_decode_32(p);
		BUG_ON(osd >= map->max_osd);
		if (struct_v >= 7)
			ret = ceph_decode_entity_addrvec(p, end, msgr2, &addr);
		else
			ret = ceph_decode_entity_addr(p, end, &addr);
		if (ret)
			return ret;

		dout("%s osd%d addr %s\n", __func__, osd, ceph_pr_addr(&addr));

		pr_info("osd%d up\n", osd);
		map->osd_state[osd] |= CEPH_OSD_EXISTS | CEPH_OSD_UP;
		map->osd_addr[osd] = addr;
	}

	*p = new_weight_end;
	return 0;

e_inval:
	return -EINVAL;
}

/*
 * decode and apply an incremental map update.
 */
struct ceph_osdmap *osdmap_apply_incremental(void **p, void *end, bool msgr2,
					     struct ceph_osdmap *map)
{
	struct ceph_fsid fsid;
	u32 epoch = 0;
	struct ceph_timespec modified;
	s32 len;
	u64 pool;
	__s64 new_pool_max;
	__s32 new_flags, max;
	void *start = *p;
	int err;
	u8 struct_v;

	dout("%s %p to %p len %d\n", __func__, *p, end, (int)(end - *p));

	err = get_osdmap_client_data_v(p, end, "inc", &struct_v);
	if (err)
		goto bad;

	/* fsid, epoch, modified, new_pool_max, new_flags */
	ceph_decode_need(p, end, sizeof(fsid) + sizeof(u32) + sizeof(modified) +
			 sizeof(u64) + sizeof(u32), e_inval);
	ceph_decode_copy(p, &fsid, sizeof(fsid));
	epoch = ceph_decode_32(p);
	BUG_ON(epoch != map->epoch+1);
	ceph_decode_copy(p, &modified, sizeof(modified));
	new_pool_max = ceph_decode_64(p);
	new_flags = ceph_decode_32(p);

	/* full map? */
	ceph_decode_32_safe(p, end, len, e_inval);
	if (len > 0) {
		dout("apply_incremental full map len %d, %p to %p\n",
		     len, *p, end);
		return ceph_osdmap_decode(p, min(*p+len, end), msgr2);
	}

	/* new crush? */
	ceph_decode_32_safe(p, end, len, e_inval);
	if (len > 0) {
		err = osdmap_set_crush(map,
				       crush_decode(*p, min(*p + len, end)));
		if (err)
			goto bad;
		*p += len;
	}

	/* new flags? */
	if (new_flags >= 0)
		map->flags = new_flags;
	if (new_pool_max >= 0)
		map->pool_max = new_pool_max;

	/* new max? */
	ceph_decode_32_safe(p, end, max, e_inval);
	if (max >= 0) {
		err = osdmap_set_max_osd(map, max);
		if (err)
			goto bad;
	}

	map->epoch++;
	map->modified = modified;

	/* new_pools */
	err = decode_new_pools(p, end, map);
	if (err)
		goto bad;

	/* new_pool_names */
	err = decode_pool_names(p, end, map);
	if (err)
		goto bad;

	/* old_pool */
	ceph_decode_32_safe(p, end, len, e_inval);
	while (len--) {
		struct ceph_pg_pool_info *pi;

		ceph_decode_64_safe(p, end, pool, e_inval);
		pi = lookup_pg_pool(&map->pg_pools, pool);
		if (pi)
			__remove_pg_pool(&map->pg_pools, pi);
	}

	/* new_up_client, new_state, new_weight */
	err = decode_new_up_state_weight(p, end, struct_v, msgr2, map);
	if (err)
		goto bad;

	/* new_pg_temp */
	err = decode_new_pg_temp(p, end, map);
	if (err)
		goto bad;

	/* new_primary_temp */
	if (struct_v >= 1) {
		err = decode_new_primary_temp(p, end, map);
		if (err)
			goto bad;
	}

	/* new_primary_affinity */
	if (struct_v >= 2) {
		err = decode_new_primary_affinity(p, end, map);
		if (err)
			goto bad;
	}

	if (struct_v >= 3) {
		/* new_erasure_code_profiles */
		ceph_decode_skip_map_of_map(p, end, string, string, string,
					    e_inval);
		/* old_erasure_code_profiles */
		ceph_decode_skip_set(p, end, string, e_inval);
	}

	if (struct_v >= 4) {
		err = decode_new_pg_upmap(p, end, map);
		if (err)
			goto bad;

		err = decode_old_pg_upmap(p, end, map);
		if (err)
			goto bad;

		err = decode_new_pg_upmap_items(p, end, map);
		if (err)
			goto bad;

		err = decode_old_pg_upmap_items(p, end, map);
		if (err)
			goto bad;
	}

	/* ignore the rest */
	*p = end;

	dout("inc osdmap epoch %d max_osd %d\n", map->epoch, map->max_osd);
	return map;

e_inval:
	err = -EINVAL;
bad:
	pr_err("corrupt inc osdmap (%d) epoch %d off %d (%p of %p-%p)\n",
	       err, epoch, (int)(*p - start), *p, start, end);
	print_hex_dump(KERN_DEBUG, "osdmap: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       start, end - start, true);
	return ERR_PTR(err);
}

void ceph_oloc_copy(struct ceph_object_locator *dest,
		    const struct ceph_object_locator *src)
{
	ceph_oloc_destroy(dest);

	dest->pool = src->pool;
	if (src->pool_ns)
		dest->pool_ns = ceph_get_string(src->pool_ns);
	else
		dest->pool_ns = NULL;
}
EXPORT_SYMBOL(ceph_oloc_copy);

void ceph_oloc_destroy(struct ceph_object_locator *oloc)
{
	ceph_put_string(oloc->pool_ns);
}
EXPORT_SYMBOL(ceph_oloc_destroy);

void ceph_oid_copy(struct ceph_object_id *dest,
		   const struct ceph_object_id *src)
{
	ceph_oid_destroy(dest);

	if (src->name != src->inline_name) {
		/* very rare, see ceph_object_id definition */
		dest->name = kmalloc(src->name_len + 1,
				     GFP_NOIO | __GFP_NOFAIL);
	} else {
		dest->name = dest->inline_name;
	}
	memcpy(dest->name, src->name, src->name_len + 1);
	dest->name_len = src->name_len;
}
EXPORT_SYMBOL(ceph_oid_copy);

static __printf(2, 0)
int oid_printf_vargs(struct ceph_object_id *oid, const char *fmt, va_list ap)
{
	int len;

	WARN_ON(!ceph_oid_empty(oid));

	len = vsnprintf(oid->inline_name, sizeof(oid->inline_name), fmt, ap);
	if (len >= sizeof(oid->inline_name))
		return len;

	oid->name_len = len;
	return 0;
}

/*
 * If oid doesn't fit into inline buffer, BUG.
 */
void ceph_oid_printf(struct ceph_object_id *oid, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	BUG_ON(oid_printf_vargs(oid, fmt, ap));
	va_end(ap);
}
EXPORT_SYMBOL(ceph_oid_printf);

static __printf(3, 0)
int oid_aprintf_vargs(struct ceph_object_id *oid, gfp_t gfp,
		      const char *fmt, va_list ap)
{
	va_list aq;
	int len;

	va_copy(aq, ap);
	len = oid_printf_vargs(oid, fmt, aq);
	va_end(aq);

	if (len) {
		char *external_name;

		external_name = kmalloc(len + 1, gfp);
		if (!external_name)
			return -ENOMEM;

		oid->name = external_name;
		WARN_ON(vsnprintf(oid->name, len + 1, fmt, ap) != len);
		oid->name_len = len;
	}

	return 0;
}

/*
 * If oid doesn't fit into inline buffer, allocate.
 */
int ceph_oid_aprintf(struct ceph_object_id *oid, gfp_t gfp,
		     const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = oid_aprintf_vargs(oid, gfp, fmt, ap);
	va_end(ap);

	return ret;
}
EXPORT_SYMBOL(ceph_oid_aprintf);

void ceph_oid_destroy(struct ceph_object_id *oid)
{
	if (oid->name != oid->inline_name)
		kfree(oid->name);
}
EXPORT_SYMBOL(ceph_oid_destroy);

/*
 * osds only
 */
static bool __osds_equal(const struct ceph_osds *lhs,
			 const struct ceph_osds *rhs)
{
	if (lhs->size == rhs->size &&
	    !memcmp(lhs->osds, rhs->osds, rhs->size * sizeof(rhs->osds[0])))
		return true;

	return false;
}

/*
 * osds + primary
 */
static bool osds_equal(const struct ceph_osds *lhs,
		       const struct ceph_osds *rhs)
{
	if (__osds_equal(lhs, rhs) &&
	    lhs->primary == rhs->primary)
		return true;

	return false;
}

static bool osds_valid(const struct ceph_osds *set)
{
	/* non-empty set */
	if (set->size > 0 && set->primary >= 0)
		return true;

	/* empty can_shift_osds set */
	if (!set->size && set->primary == -1)
		return true;

	/* empty !can_shift_osds set - all NONE */
	if (set->size > 0 && set->primary == -1) {
		int i;

		for (i = 0; i < set->size; i++) {
			if (set->osds[i] != CRUSH_ITEM_NONE)
				break;
		}
		if (i == set->size)
			return true;
	}

	return false;
}

void ceph_osds_copy(struct ceph_osds *dest, const struct ceph_osds *src)
{
	memcpy(dest->osds, src->osds, src->size * sizeof(src->osds[0]));
	dest->size = src->size;
	dest->primary = src->primary;
}

bool ceph_pg_is_split(const struct ceph_pg *pgid, u32 old_pg_num,
		      u32 new_pg_num)
{
	int old_bits = calc_bits_of(old_pg_num);
	int old_mask = (1 << old_bits) - 1;
	int n;

	WARN_ON(pgid->seed >= old_pg_num);
	if (new_pg_num <= old_pg_num)
		return false;

	for (n = 1; ; n++) {
		int next_bit = n << (old_bits - 1);
		u32 s = next_bit | pgid->seed;

		if (s < old_pg_num || s == pgid->seed)
			continue;
		if (s >= new_pg_num)
			break;

		s = ceph_stable_mod(s, old_pg_num, old_mask);
		if (s == pgid->seed)
			return true;
	}

	return false;
}

bool ceph_is_new_interval(const struct ceph_osds *old_acting,
			  const struct ceph_osds *new_acting,
			  const struct ceph_osds *old_up,
			  const struct ceph_osds *new_up,
			  int old_size,
			  int new_size,
			  int old_min_size,
			  int new_min_size,
			  u32 old_pg_num,
			  u32 new_pg_num,
			  bool old_sort_bitwise,
			  bool new_sort_bitwise,
			  bool old_recovery_deletes,
			  bool new_recovery_deletes,
			  const struct ceph_pg *pgid)
{
	return !osds_equal(old_acting, new_acting) ||
	       !osds_equal(old_up, new_up) ||
	       old_size != new_size ||
	       old_min_size != new_min_size ||
	       ceph_pg_is_split(pgid, old_pg_num, new_pg_num) ||
	       old_sort_bitwise != new_sort_bitwise ||
	       old_recovery_deletes != new_recovery_deletes;
}

static int calc_pg_rank(int osd, const struct ceph_osds *acting)
{
	int i;

	for (i = 0; i < acting->size; i++) {
		if (acting->osds[i] == osd)
			return i;
	}

	return -1;
}

static bool primary_changed(const struct ceph_osds *old_acting,
			    const struct ceph_osds *new_acting)
{
	if (!old_acting->size && !new_acting->size)
		return false; /* both still empty */

	if (!old_acting->size ^ !new_acting->size)
		return true; /* was empty, now not, or vice versa */

	if (old_acting->primary != new_acting->primary)
		return true; /* primary changed */

	if (calc_pg_rank(old_acting->primary, old_acting) !=
	    calc_pg_rank(new_acting->primary, new_acting))
		return true;

	return false; /* same primary (tho replicas may have changed) */
}

bool ceph_osds_changed(const struct ceph_osds *old_acting,
		       const struct ceph_osds *new_acting,
		       bool any_change)
{
	if (primary_changed(old_acting, new_acting))
		return true;

	if (any_change && !__osds_equal(old_acting, new_acting))
		return true;

	return false;
}

/*
 * Map an object into a PG.
 *
 * Should only be called with target_oid and target_oloc (as opposed to
 * base_oid and base_oloc), since tiering isn't taken into account.
 */
void __ceph_object_locator_to_pg(struct ceph_pg_pool_info *pi,
				 const struct ceph_object_id *oid,
				 const struct ceph_object_locator *oloc,
				 struct ceph_pg *raw_pgid)
{
	WARN_ON(pi->id != oloc->pool);

	if (!oloc->pool_ns) {
		raw_pgid->pool = oloc->pool;
		raw_pgid->seed = ceph_str_hash(pi->object_hash, oid->name,
					     oid->name_len);
		dout("%s %s -> raw_pgid %llu.%x\n", __func__, oid->name,
		     raw_pgid->pool, raw_pgid->seed);
	} else {
		char stack_buf[256];
		char *buf = stack_buf;
		int nsl = oloc->pool_ns->len;
		size_t total = nsl + 1 + oid->name_len;

		if (total > sizeof(stack_buf))
			buf = kmalloc(total, GFP_NOIO | __GFP_NOFAIL);
		memcpy(buf, oloc->pool_ns->str, nsl);
		buf[nsl] = '\037';
		memcpy(buf + nsl + 1, oid->name, oid->name_len);
		raw_pgid->pool = oloc->pool;
		raw_pgid->seed = ceph_str_hash(pi->object_hash, buf, total);
		if (buf != stack_buf)
			kfree(buf);
		dout("%s %s ns %.*s -> raw_pgid %llu.%x\n", __func__,
		     oid->name, nsl, oloc->pool_ns->str,
		     raw_pgid->pool, raw_pgid->seed);
	}
}

int ceph_object_locator_to_pg(struct ceph_osdmap *osdmap,
			      const struct ceph_object_id *oid,
			      const struct ceph_object_locator *oloc,
			      struct ceph_pg *raw_pgid)
{
	struct ceph_pg_pool_info *pi;

	pi = ceph_pg_pool_by_id(osdmap, oloc->pool);
	if (!pi)
		return -ENOENT;

	__ceph_object_locator_to_pg(pi, oid, oloc, raw_pgid);
	return 0;
}
EXPORT_SYMBOL(ceph_object_locator_to_pg);

/*
 * Map a raw PG (full precision ps) into an actual PG.
 */
static void raw_pg_to_pg(struct ceph_pg_pool_info *pi,
			 const struct ceph_pg *raw_pgid,
			 struct ceph_pg *pgid)
{
	pgid->pool = raw_pgid->pool;
	pgid->seed = ceph_stable_mod(raw_pgid->seed, pi->pg_num,
				     pi->pg_num_mask);
}

/*
 * Map a raw PG (full precision ps) into a placement ps (placement
 * seed).  Include pool id in that value so that different pools don't
 * use the same seeds.
 */
static u32 raw_pg_to_pps(struct ceph_pg_pool_info *pi,
			 const struct ceph_pg *raw_pgid)
{
	if (pi->flags & CEPH_POOL_FLAG_HASHPSPOOL) {
		/* hash pool id and seed so that pool PGs do not overlap */
		return crush_hash32_2(CRUSH_HASH_RJENKINS1,
				      ceph_stable_mod(raw_pgid->seed,
						      pi->pgp_num,
						      pi->pgp_num_mask),
				      raw_pgid->pool);
	} else {
		/*
		 * legacy behavior: add ps and pool together.  this is
		 * not a great approach because the PGs from each pool
		 * will overlap on top of each other: 0.5 == 1.4 ==
		 * 2.3 == ...
		 */
		return ceph_stable_mod(raw_pgid->seed, pi->pgp_num,
				       pi->pgp_num_mask) +
		       (unsigned)raw_pgid->pool;
	}
}

/*
 * Magic value used for a "default" fallback choose_args, used if the
 * crush_choose_arg_map passed to do_crush() does not exist.  If this
 * also doesn't exist, fall back to canonical weights.
 */
#define CEPH_DEFAULT_CHOOSE_ARGS	-1

static int do_crush(struct ceph_osdmap *map, int ruleno, int x,
		    int *result, int result_max,
		    const __u32 *weight, int weight_max,
		    s64 choose_args_index)
{
	struct crush_choose_arg_map *arg_map;
	struct crush_work *work;
	int r;

	BUG_ON(result_max > CEPH_PG_MAX_SIZE);

	arg_map = lookup_choose_arg_map(&map->crush->choose_args,
					choose_args_index);
	if (!arg_map)
		arg_map = lookup_choose_arg_map(&map->crush->choose_args,
						CEPH_DEFAULT_CHOOSE_ARGS);

	work = get_workspace(&map->crush_wsm, map->crush);
	r = crush_do_rule(map->crush, ruleno, x, result, result_max,
			  weight, weight_max, work,
			  arg_map ? arg_map->args : NULL);
	put_workspace(&map->crush_wsm, work);
	return r;
}

static void remove_nonexistent_osds(struct ceph_osdmap *osdmap,
				    struct ceph_pg_pool_info *pi,
				    struct ceph_osds *set)
{
	int i;

	if (ceph_can_shift_osds(pi)) {
		int removed = 0;

		/* shift left */
		for (i = 0; i < set->size; i++) {
			if (!ceph_osd_exists(osdmap, set->osds[i])) {
				removed++;
				continue;
			}
			if (removed)
				set->osds[i - removed] = set->osds[i];
		}
		set->size -= removed;
	} else {
		/* set dne devices to NONE */
		for (i = 0; i < set->size; i++) {
			if (!ceph_osd_exists(osdmap, set->osds[i]))
				set->osds[i] = CRUSH_ITEM_NONE;
		}
	}
}

/*
 * Calculate raw set (CRUSH output) for given PG and filter out
 * nonexistent OSDs.  ->primary is undefined for a raw set.
 *
 * Placement seed (CRUSH input) is returned through @ppps.
 */
static void pg_to_raw_osds(struct ceph_osdmap *osdmap,
			   struct ceph_pg_pool_info *pi,
			   const struct ceph_pg *raw_pgid,
			   struct ceph_osds *raw,
			   u32 *ppps)
{
	u32 pps = raw_pg_to_pps(pi, raw_pgid);
	int ruleno;
	int len;

	ceph_osds_init(raw);
	if (ppps)
		*ppps = pps;

	ruleno = crush_find_rule(osdmap->crush, pi->crush_ruleset, pi->type,
				 pi->size);
	if (ruleno < 0) {
		pr_err("no crush rule: pool %lld ruleset %d type %d size %d\n",
		       pi->id, pi->crush_ruleset, pi->type, pi->size);
		return;
	}

	if (pi->size > ARRAY_SIZE(raw->osds)) {
		pr_err_ratelimited("pool %lld ruleset %d type %d too wide: size %d > %zu\n",
		       pi->id, pi->crush_ruleset, pi->type, pi->size,
		       ARRAY_SIZE(raw->osds));
		return;
	}

	len = do_crush(osdmap, ruleno, pps, raw->osds, pi->size,
		       osdmap->osd_weight, osdmap->max_osd, pi->id);
	if (len < 0) {
		pr_err("error %d from crush rule %d: pool %lld ruleset %d type %d size %d\n",
		       len, ruleno, pi->id, pi->crush_ruleset, pi->type,
		       pi->size);
		return;
	}

	raw->size = len;
	remove_nonexistent_osds(osdmap, pi, raw);
}

/* apply pg_upmap[_items] mappings */
static void apply_upmap(struct ceph_osdmap *osdmap,
			const struct ceph_pg *pgid,
			struct ceph_osds *raw)
{
	struct ceph_pg_mapping *pg;
	int i, j;

	pg = lookup_pg_mapping(&osdmap->pg_upmap, pgid);
	if (pg) {
		/* make sure targets aren't marked out */
		for (i = 0; i < pg->pg_upmap.len; i++) {
			int osd = pg->pg_upmap.osds[i];

			if (osd != CRUSH_ITEM_NONE &&
			    osd < osdmap->max_osd &&
			    osdmap->osd_weight[osd] == 0) {
				/* reject/ignore explicit mapping */
				return;
			}
		}
		for (i = 0; i < pg->pg_upmap.len; i++)
			raw->osds[i] = pg->pg_upmap.osds[i];
		raw->size = pg->pg_upmap.len;
		/* check and apply pg_upmap_items, if any */
	}

	pg = lookup_pg_mapping(&osdmap->pg_upmap_items, pgid);
	if (pg) {
		/*
		 * Note: this approach does not allow a bidirectional swap,
		 * e.g., [[1,2],[2,1]] applied to [0,1,2] -> [0,2,1].
		 */
		for (i = 0; i < pg->pg_upmap_items.len; i++) {
			int from = pg->pg_upmap_items.from_to[i][0];
			int to = pg->pg_upmap_items.from_to[i][1];
			int pos = -1;
			bool exists = false;

			/* make sure replacement doesn't already appear */
			for (j = 0; j < raw->size; j++) {
				int osd = raw->osds[j];

				if (osd == to) {
					exists = true;
					break;
				}
				/* ignore mapping if target is marked out */
				if (osd == from && pos < 0 &&
				    !(to != CRUSH_ITEM_NONE &&
				      to < osdmap->max_osd &&
				      osdmap->osd_weight[to] == 0)) {
					pos = j;
				}
			}
			if (!exists && pos >= 0)
				raw->osds[pos] = to;
		}
	}
}

/*
 * Given raw set, calculate up set and up primary.  By definition of an
 * up set, the result won't contain nonexistent or down OSDs.
 *
 * This is done in-place - on return @set is the up set.  If it's
 * empty, ->primary will remain undefined.
 */
static void raw_to_up_osds(struct ceph_osdmap *osdmap,
			   struct ceph_pg_pool_info *pi,
			   struct ceph_osds *set)
{
	int i;

	/* ->primary is undefined for a raw set */
	BUG_ON(set->primary != -1);

	if (ceph_can_shift_osds(pi)) {
		int removed = 0;

		/* shift left */
		for (i = 0; i < set->size; i++) {
			if (ceph_osd_is_down(osdmap, set->osds[i])) {
				removed++;
				continue;
			}
			if (removed)
				set->osds[i - removed] = set->osds[i];
		}
		set->size -= removed;
		if (set->size > 0)
			set->primary = set->osds[0];
	} else {
		/* set down/dne devices to NONE */
		for (i = set->size - 1; i >= 0; i--) {
			if (ceph_osd_is_down(osdmap, set->osds[i]))
				set->osds[i] = CRUSH_ITEM_NONE;
			else
				set->primary = set->osds[i];
		}
	}
}

static void apply_primary_affinity(struct ceph_osdmap *osdmap,
				   struct ceph_pg_pool_info *pi,
				   u32 pps,
				   struct ceph_osds *up)
{
	int i;
	int pos = -1;

	/*
	 * Do we have any non-default primary_affinity values for these
	 * osds?
	 */
	if (!osdmap->osd_primary_affinity)
		return;

	for (i = 0; i < up->size; i++) {
		int osd = up->osds[i];

		if (osd != CRUSH_ITEM_NONE &&
		    osdmap->osd_primary_affinity[osd] !=
					CEPH_OSD_DEFAULT_PRIMARY_AFFINITY) {
			break;
		}
	}
	if (i == up->size)
		return;

	/*
	 * Pick the primary.  Feed both the seed (for the pg) and the
	 * osd into the hash/rng so that a proportional fraction of an
	 * osd's pgs get rejected as primary.
	 */
	for (i = 0; i < up->size; i++) {
		int osd = up->osds[i];
		u32 aff;

		if (osd == CRUSH_ITEM_NONE)
			continue;

		aff = osdmap->osd_primary_affinity[osd];
		if (aff < CEPH_OSD_MAX_PRIMARY_AFFINITY &&
		    (crush_hash32_2(CRUSH_HASH_RJENKINS1,
				    pps, osd) >> 16) >= aff) {
			/*
			 * We chose not to use this primary.  Note it
			 * anyway as a fallback in case we don't pick
			 * anyone else, but keep looking.
			 */
			if (pos < 0)
				pos = i;
		} else {
			pos = i;
			break;
		}
	}
	if (pos < 0)
		return;

	up->primary = up->osds[pos];

	if (ceph_can_shift_osds(pi) && pos > 0) {
		/* move the new primary to the front */
		for (i = pos; i > 0; i--)
			up->osds[i] = up->osds[i - 1];
		up->osds[0] = up->primary;
	}
}

/*
 * Get pg_temp and primary_temp mappings for given PG.
 *
 * Note that a PG may have none, only pg_temp, only primary_temp or
 * both pg_temp and primary_temp mappings.  This means @temp isn't
 * always a valid OSD set on return: in the "only primary_temp" case,
 * @temp will have its ->primary >= 0 but ->size == 0.
 */
static void get_temp_osds(struct ceph_osdmap *osdmap,
			  struct ceph_pg_pool_info *pi,
			  const struct ceph_pg *pgid,
			  struct ceph_osds *temp)
{
	struct ceph_pg_mapping *pg;
	int i;

	ceph_osds_init(temp);

	/* pg_temp? */
	pg = lookup_pg_mapping(&osdmap->pg_temp, pgid);
	if (pg) {
		for (i = 0; i < pg->pg_temp.len; i++) {
			if (ceph_osd_is_down(osdmap, pg->pg_temp.osds[i])) {
				if (ceph_can_shift_osds(pi))
					continue;

				temp->osds[temp->size++] = CRUSH_ITEM_NONE;
			} else {
				temp->osds[temp->size++] = pg->pg_temp.osds[i];
			}
		}

		/* apply pg_temp's primary */
		for (i = 0; i < temp->size; i++) {
			if (temp->osds[i] != CRUSH_ITEM_NONE) {
				temp->primary = temp->osds[i];
				break;
			}
		}
	}

	/* primary_temp? */
	pg = lookup_pg_mapping(&osdmap->primary_temp, pgid);
	if (pg)
		temp->primary = pg->primary_temp.osd;
}

/*
 * Map a PG to its acting set as well as its up set.
 *
 * Acting set is used for data mapping purposes, while up set can be
 * recorded for detecting interval changes and deciding whether to
 * resend a request.
 */
void ceph_pg_to_up_acting_osds(struct ceph_osdmap *osdmap,
			       struct ceph_pg_pool_info *pi,
			       const struct ceph_pg *raw_pgid,
			       struct ceph_osds *up,
			       struct ceph_osds *acting)
{
	struct ceph_pg pgid;
	u32 pps;

	WARN_ON(pi->id != raw_pgid->pool);
	raw_pg_to_pg(pi, raw_pgid, &pgid);

	pg_to_raw_osds(osdmap, pi, raw_pgid, up, &pps);
	apply_upmap(osdmap, &pgid, up);
	raw_to_up_osds(osdmap, pi, up);
	apply_primary_affinity(osdmap, pi, pps, up);
	get_temp_osds(osdmap, pi, &pgid, acting);
	if (!acting->size) {
		memcpy(acting->osds, up->osds, up->size * sizeof(up->osds[0]));
		acting->size = up->size;
		if (acting->primary == -1)
			acting->primary = up->primary;
	}
	WARN_ON(!osds_valid(up) || !osds_valid(acting));
}

bool ceph_pg_to_primary_shard(struct ceph_osdmap *osdmap,
			      struct ceph_pg_pool_info *pi,
			      const struct ceph_pg *raw_pgid,
			      struct ceph_spg *spgid)
{
	struct ceph_pg pgid;
	struct ceph_osds up, acting;
	int i;

	WARN_ON(pi->id != raw_pgid->pool);
	raw_pg_to_pg(pi, raw_pgid, &pgid);

	if (ceph_can_shift_osds(pi)) {
		spgid->pgid = pgid; /* struct */
		spgid->shard = CEPH_SPG_NOSHARD;
		return true;
	}

	ceph_pg_to_up_acting_osds(osdmap, pi, &pgid, &up, &acting);
	for (i = 0; i < acting.size; i++) {
		if (acting.osds[i] == acting.primary) {
			spgid->pgid = pgid; /* struct */
			spgid->shard = i;
			return true;
		}
	}

	return false;
}

/*
 * Return acting primary for given PG, or -1 if none.
 */
int ceph_pg_to_acting_primary(struct ceph_osdmap *osdmap,
			      const struct ceph_pg *raw_pgid)
{
	struct ceph_pg_pool_info *pi;
	struct ceph_osds up, acting;

	pi = ceph_pg_pool_by_id(osdmap, raw_pgid->pool);
	if (!pi)
		return -1;

	ceph_pg_to_up_acting_osds(osdmap, pi, raw_pgid, &up, &acting);
	return acting.primary;
}
EXPORT_SYMBOL(ceph_pg_to_acting_primary);

static struct crush_loc_node *alloc_crush_loc(size_t type_name_len,
					      size_t name_len)
{
	struct crush_loc_node *loc;

	loc = kmalloc(sizeof(*loc) + type_name_len + name_len + 2, GFP_NOIO);
	if (!loc)
		return NULL;

	RB_CLEAR_NODE(&loc->cl_node);
	return loc;
}

static void free_crush_loc(struct crush_loc_node *loc)
{
	WARN_ON(!RB_EMPTY_NODE(&loc->cl_node));

	kfree(loc);
}

static int crush_loc_compare(const struct crush_loc *loc1,
			     const struct crush_loc *loc2)
{
	return strcmp(loc1->cl_type_name, loc2->cl_type_name) ?:
	       strcmp(loc1->cl_name, loc2->cl_name);
}

DEFINE_RB_FUNCS2(crush_loc, struct crush_loc_node, cl_loc, crush_loc_compare,
		 RB_BYPTR, const struct crush_loc *, cl_node)

/*
 * Parses a set of <bucket type name>':'<bucket name> pairs separated
 * by '|', e.g. "rack:foo1|rack:foo2|datacenter:bar".
 *
 * Note that @crush_location is modified by strsep().
 */
int ceph_parse_crush_location(char *crush_location, struct rb_root *locs)
{
	struct crush_loc_node *loc;
	const char *type_name, *name, *colon;
	size_t type_name_len, name_len;

	dout("%s '%s'\n", __func__, crush_location);
	while ((type_name = strsep(&crush_location, "|"))) {
		colon = strchr(type_name, ':');
		if (!colon)
			return -EINVAL;

		type_name_len = colon - type_name;
		if (type_name_len == 0)
			return -EINVAL;

		name = colon + 1;
		name_len = strlen(name);
		if (name_len == 0)
			return -EINVAL;

		loc = alloc_crush_loc(type_name_len, name_len);
		if (!loc)
			return -ENOMEM;

		loc->cl_loc.cl_type_name = loc->cl_data;
		memcpy(loc->cl_loc.cl_type_name, type_name, type_name_len);
		loc->cl_loc.cl_type_name[type_name_len] = '\0';

		loc->cl_loc.cl_name = loc->cl_data + type_name_len + 1;
		memcpy(loc->cl_loc.cl_name, name, name_len);
		loc->cl_loc.cl_name[name_len] = '\0';

		if (!__insert_crush_loc(locs, loc)) {
			free_crush_loc(loc);
			return -EEXIST;
		}

		dout("%s type_name '%s' name '%s'\n", __func__,
		     loc->cl_loc.cl_type_name, loc->cl_loc.cl_name);
	}

	return 0;
}

int ceph_compare_crush_locs(struct rb_root *locs1, struct rb_root *locs2)
{
	struct rb_node *n1 = rb_first(locs1);
	struct rb_node *n2 = rb_first(locs2);
	int ret;

	for ( ; n1 && n2; n1 = rb_next(n1), n2 = rb_next(n2)) {
		struct crush_loc_node *loc1 =
		    rb_entry(n1, struct crush_loc_node, cl_node);
		struct crush_loc_node *loc2 =
		    rb_entry(n2, struct crush_loc_node, cl_node);

		ret = crush_loc_compare(&loc1->cl_loc, &loc2->cl_loc);
		if (ret)
			return ret;
	}

	if (!n1 && n2)
		return -1;
	if (n1 && !n2)
		return 1;
	return 0;
}

void ceph_clear_crush_locs(struct rb_root *locs)
{
	while (!RB_EMPTY_ROOT(locs)) {
		struct crush_loc_node *loc =
		    rb_entry(rb_first(locs), struct crush_loc_node, cl_node);

		erase_crush_loc(locs, loc);
		free_crush_loc(loc);
	}
}

/*
 * [a-zA-Z0-9-_.]+
 */
static bool is_valid_crush_name(const char *name)
{
	do {
		if (!('a' <= *name && *name <= 'z') &&
		    !('A' <= *name && *name <= 'Z') &&
		    !('0' <= *name && *name <= '9') &&
		    *name != '-' && *name != '_' && *name != '.')
			return false;
	} while (*++name != '\0');

	return true;
}

/*
 * Gets the parent of an item.  Returns its id (<0 because the
 * parent is always a bucket), type id (>0 for the same reason,
 * via @parent_type_id) and location (via @parent_loc).  If no
 * parent, returns 0.
 *
 * Does a linear search, as there are no parent pointers of any
 * kind.  Note that the result is ambiguous for items that occur
 * multiple times in the map.
 */
static int get_immediate_parent(struct crush_map *c, int id,
				u16 *parent_type_id,
				struct crush_loc *parent_loc)
{
	struct crush_bucket *b;
	struct crush_name_node *type_cn, *cn;
	int i, j;

	for (i = 0; i < c->max_buckets; i++) {
		b = c->buckets[i];
		if (!b)
			continue;

		/* ignore per-class shadow hierarchy */
		cn = lookup_crush_name(&c->names, b->id);
		if (!cn || !is_valid_crush_name(cn->cn_name))
			continue;

		for (j = 0; j < b->size; j++) {
			if (b->items[j] != id)
				continue;

			*parent_type_id = b->type;
			type_cn = lookup_crush_name(&c->type_names, b->type);
			parent_loc->cl_type_name = type_cn->cn_name;
			parent_loc->cl_name = cn->cn_name;
			return b->id;
		}
	}

	return 0;  /* no parent */
}

/*
 * Calculates the locality/distance from an item to a client
 * location expressed in terms of CRUSH hierarchy as a set of
 * (bucket type name, bucket name) pairs.  Specifically, looks
 * for the lowest-valued bucket type for which the location of
 * @id matches one of the locations in @locs, so for standard
 * bucket types (host = 1, rack = 3, datacenter = 8, zone = 9)
 * a matching host is closer than a matching rack and a matching
 * data center is closer than a matching zone.
 *
 * Specifying multiple locations (a "multipath" location) such
 * as "rack=foo1 rack=foo2 datacenter=bar" is allowed -- @locs
 * is a multimap.  The locality will be:
 *
 * - 3 for OSDs in racks foo1 and foo2
 * - 8 for OSDs in data center bar
 * - -1 for all other OSDs
 *
 * The lowest possible bucket type is 1, so the best locality
 * for an OSD is 1 (i.e. a matching host).  Locality 0 would be
 * the OSD itself.
 */
int ceph_get_crush_locality(struct ceph_osdmap *osdmap, int id,
			    struct rb_root *locs)
{
	struct crush_loc loc;
	u16 type_id;

	/*
	 * Instead of repeated get_immediate_parent() calls,
	 * the location of @id could be obtained with a single
	 * depth-first traversal.
	 */
	for (;;) {
		id = get_immediate_parent(osdmap->crush, id, &type_id, &loc);
		if (id >= 0)
			return -1;  /* not local */

		if (lookup_crush_loc(locs, &loc))
			return type_id;
	}
}
