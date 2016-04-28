
#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include <linux/ceph/libceph.h>
#include <linux/ceph/osdmap.h>
#include <linux/ceph/decode.h>
#include <linux/crush/hash.h>
#include <linux/crush/mapper.h>

char *ceph_osdmap_state_str(char *str, int len, int state)
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

static int skip_name_map(void **p, void *end)
{
        int len;
        ceph_decode_32_safe(p, end, len ,bad);
        while (len--) {
                int strlen;
                *p += sizeof(u32);
                ceph_decode_32_safe(p, end, strlen, bad);
                *p += strlen;
}
        return 0;
bad:
        return -EINVAL;
}

static struct crush_map *crush_decode(void *pbyval, void *end)
{
	struct crush_map *c;
	int err = -EINVAL;
	int i, j;
	void **p = &pbyval;
	void *start = pbyval;
	u32 magic;
	u32 num_name_maps;

	dout("crush_decode %p to %p len %d\n", *p, end, (int)(end - *p));

	c = kzalloc(sizeof(*c), GFP_NOFS);
	if (c == NULL)
		return ERR_PTR(-ENOMEM);

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
			err = -EINVAL;
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
		b->perm = kcalloc(b->size, sizeof(u32), GFP_NOFS);
		if (b->perm == NULL)
			goto badmem;
		b->perm_n = 0;

		ceph_decode_need(p, end, b->size*sizeof(u32), bad);
		for (j = 0; j < b->size; j++)
			b->items[j] = ceph_decode_32(p);

		switch (b->alg) {
		case CRUSH_BUCKET_UNIFORM:
			err = crush_decode_uniform_bucket(p, end,
				  (struct crush_bucket_uniform *)b);
			if (err < 0)
				goto bad;
			break;
		case CRUSH_BUCKET_LIST:
			err = crush_decode_list_bucket(p, end,
			       (struct crush_bucket_list *)b);
			if (err < 0)
				goto bad;
			break;
		case CRUSH_BUCKET_TREE:
			err = crush_decode_tree_bucket(p, end,
				(struct crush_bucket_tree *)b);
			if (err < 0)
				goto bad;
			break;
		case CRUSH_BUCKET_STRAW:
			err = crush_decode_straw_bucket(p, end,
				(struct crush_bucket_straw *)b);
			if (err < 0)
				goto bad;
			break;
		case CRUSH_BUCKET_STRAW2:
			err = crush_decode_straw2_bucket(p, end,
				(struct crush_bucket_straw2 *)b);
			if (err < 0)
				goto bad;
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
		err = -EINVAL;
		if (yes > (ULONG_MAX - sizeof(*r))
			  / sizeof(struct crush_rule_step))
			goto bad;
#endif
		r = c->rules[i] = kmalloc(sizeof(*r) +
					  yes*sizeof(struct crush_rule_step),
					  GFP_NOFS);
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

	/* ignore trailing name maps. */
        for (num_name_maps = 0; num_name_maps < 3; num_name_maps++) {
                err = skip_name_map(p, end);
                if (err < 0)
                        goto done;
        }

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

done:
	dout("crush_decode success\n");
	return c;

badmem:
	err = -ENOMEM;
bad:
	dout("crush_decode fail %d\n", err);
	crush_destroy(c);
	return ERR_PTR(err);
}

/*
 * rbtree of pg_mapping for handling pg_temp (explicit mapping of pgid
 * to a set of osds) and primary_temp (explicit primary setting)
 */
static int pgid_cmp(struct ceph_pg l, struct ceph_pg r)
{
	if (l.pool < r.pool)
		return -1;
	if (l.pool > r.pool)
		return 1;
	if (l.seed < r.seed)
		return -1;
	if (l.seed > r.seed)
		return 1;
	return 0;
}

static int __insert_pg_mapping(struct ceph_pg_mapping *new,
			       struct rb_root *root)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct ceph_pg_mapping *pg = NULL;
	int c;

	dout("__insert_pg_mapping %llx %p\n", *(u64 *)&new->pgid, new);
	while (*p) {
		parent = *p;
		pg = rb_entry(parent, struct ceph_pg_mapping, node);
		c = pgid_cmp(new->pgid, pg->pgid);
		if (c < 0)
			p = &(*p)->rb_left;
		else if (c > 0)
			p = &(*p)->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, root);
	return 0;
}

static struct ceph_pg_mapping *__lookup_pg_mapping(struct rb_root *root,
						   struct ceph_pg pgid)
{
	struct rb_node *n = root->rb_node;
	struct ceph_pg_mapping *pg;
	int c;

	while (n) {
		pg = rb_entry(n, struct ceph_pg_mapping, node);
		c = pgid_cmp(pgid, pg->pgid);
		if (c < 0) {
			n = n->rb_left;
		} else if (c > 0) {
			n = n->rb_right;
		} else {
			dout("__lookup_pg_mapping %lld.%x got %p\n",
			     pgid.pool, pgid.seed, pg);
			return pg;
		}
	}
	return NULL;
}

static int __remove_pg_mapping(struct rb_root *root, struct ceph_pg pgid)
{
	struct ceph_pg_mapping *pg = __lookup_pg_mapping(root, pgid);

	if (pg) {
		dout("__remove_pg_mapping %lld.%x %p\n", pgid.pool, pgid.seed,
		     pg);
		rb_erase(&pg->node, root);
		kfree(pg);
		return 0;
	}
	dout("__remove_pg_mapping %lld.%x dne\n", pgid.pool, pgid.seed);
	return -ENOENT;
}

/*
 * rbtree of pg pool info
 */
static int __insert_pg_pool(struct rb_root *root, struct ceph_pg_pool_info *new)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct ceph_pg_pool_info *pi = NULL;

	while (*p) {
		parent = *p;
		pi = rb_entry(parent, struct ceph_pg_pool_info, node);
		if (new->id < pi->id)
			p = &(*p)->rb_left;
		else if (new->id > pi->id)
			p = &(*p)->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, root);
	return 0;
}

static struct ceph_pg_pool_info *__lookup_pg_pool(struct rb_root *root, u64 id)
{
	struct ceph_pg_pool_info *pi;
	struct rb_node *n = root->rb_node;

	while (n) {
		pi = rb_entry(n, struct ceph_pg_pool_info, node);
		if (id < pi->id)
			n = n->rb_left;
		else if (id > pi->id)
			n = n->rb_right;
		else
			return pi;
	}
	return NULL;
}

struct ceph_pg_pool_info *ceph_pg_pool_by_id(struct ceph_osdmap *map, u64 id)
{
	return __lookup_pg_pool(&map->pg_pools, id);
}

const char *ceph_pg_pool_name_by_id(struct ceph_osdmap *map, u64 id)
{
	struct ceph_pg_pool_info *pi;

	if (id == CEPH_NOPOOL)
		return NULL;

	if (WARN_ON_ONCE(id > (u64) INT_MAX))
		return NULL;

	pi = __lookup_pg_pool(&map->pg_pools, (int) id);

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

static void __remove_pg_pool(struct rb_root *root, struct ceph_pg_pool_info *pi)
{
	rb_erase(&pi->node, root);
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
		*p += 1;  /* skip min_size */

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
		pi = __lookup_pg_pool(&map->pg_pools, pool);
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
 * osd map
 */
void ceph_osdmap_destroy(struct ceph_osdmap *map)
{
	dout("osdmap_destroy %p\n", map);
	if (map->crush)
		crush_destroy(map->crush);
	while (!RB_EMPTY_ROOT(&map->pg_temp)) {
		struct ceph_pg_mapping *pg =
			rb_entry(rb_first(&map->pg_temp),
				 struct ceph_pg_mapping, node);
		rb_erase(&pg->node, &map->pg_temp);
		kfree(pg);
	}
	while (!RB_EMPTY_ROOT(&map->primary_temp)) {
		struct ceph_pg_mapping *pg =
			rb_entry(rb_first(&map->primary_temp),
				 struct ceph_pg_mapping, node);
		rb_erase(&pg->node, &map->primary_temp);
		kfree(pg);
	}
	while (!RB_EMPTY_ROOT(&map->pg_pools)) {
		struct ceph_pg_pool_info *pi =
			rb_entry(rb_first(&map->pg_pools),
				 struct ceph_pg_pool_info, node);
		__remove_pg_pool(&map->pg_pools, pi);
	}
	kfree(map->osd_state);
	kfree(map->osd_weight);
	kfree(map->osd_addr);
	kfree(map->osd_primary_affinity);
	kfree(map);
}

/*
 * Adjust max_osd value, (re)allocate arrays.
 *
 * The new elements are properly initialized.
 */
static int osdmap_set_max_osd(struct ceph_osdmap *map, int max)
{
	u8 *state;
	u32 *weight;
	struct ceph_entity_addr *addr;
	int i;

	state = krealloc(map->osd_state, max*sizeof(*state), GFP_NOFS);
	if (!state)
		return -ENOMEM;
	map->osd_state = state;

	weight = krealloc(map->osd_weight, max*sizeof(*weight), GFP_NOFS);
	if (!weight)
		return -ENOMEM;
	map->osd_weight = weight;

	addr = krealloc(map->osd_addr, max*sizeof(*addr), GFP_NOFS);
	if (!addr)
		return -ENOMEM;
	map->osd_addr = addr;

	for (i = map->max_osd; i < max; i++) {
		map->osd_state[i] = 0;
		map->osd_weight[i] = CEPH_OSD_OUT;
		memset(map->osd_addr + i, 0, sizeof(*map->osd_addr));
	}

	if (map->osd_primary_affinity) {
		u32 *affinity;

		affinity = krealloc(map->osd_primary_affinity,
				    max*sizeof(*affinity), GFP_NOFS);
		if (!affinity)
			return -ENOMEM;
		map->osd_primary_affinity = affinity;

		for (i = map->max_osd; i < max; i++)
			map->osd_primary_affinity[i] =
			    CEPH_OSD_DEFAULT_PRIMARY_AFFINITY;
	}

	map->max_osd = max;

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

		/* old osdmap enconding */
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

		pi = __lookup_pg_pool(&map->pg_pools, pool);
		if (!incremental || !pi) {
			pi = kzalloc(sizeof(*pi), GFP_NOFS);
			if (!pi)
				return -ENOMEM;

			pi->id = pool;

			ret = __insert_pg_pool(&map->pg_pools, pi);
			if (ret) {
				kfree(pi);
				return ret;
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

static int __decode_pg_temp(void **p, void *end, struct ceph_osdmap *map,
			    bool incremental)
{
	u32 n;

	ceph_decode_32_safe(p, end, n, e_inval);
	while (n--) {
		struct ceph_pg pgid;
		u32 len, i;
		int ret;

		ret = ceph_decode_pgid(p, end, &pgid);
		if (ret)
			return ret;

		ceph_decode_32_safe(p, end, len, e_inval);

		ret = __remove_pg_mapping(&map->pg_temp, pgid);
		BUG_ON(!incremental && ret != -ENOENT);

		if (!incremental || len > 0) {
			struct ceph_pg_mapping *pg;

			ceph_decode_need(p, end, len*sizeof(u32), e_inval);

			if (len > (UINT_MAX - sizeof(*pg)) / sizeof(u32))
				return -EINVAL;

			pg = kzalloc(sizeof(*pg) + len*sizeof(u32), GFP_NOFS);
			if (!pg)
				return -ENOMEM;

			pg->pgid = pgid;
			pg->pg_temp.len = len;
			for (i = 0; i < len; i++)
				pg->pg_temp.osds[i] = ceph_decode_32(p);

			ret = __insert_pg_mapping(pg, &map->pg_temp);
			if (ret) {
				kfree(pg);
				return ret;
			}
		}
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int decode_pg_temp(void **p, void *end, struct ceph_osdmap *map)
{
	return __decode_pg_temp(p, end, map, false);
}

static int decode_new_pg_temp(void **p, void *end, struct ceph_osdmap *map)
{
	return __decode_pg_temp(p, end, map, true);
}

static int __decode_primary_temp(void **p, void *end, struct ceph_osdmap *map,
				 bool incremental)
{
	u32 n;

	ceph_decode_32_safe(p, end, n, e_inval);
	while (n--) {
		struct ceph_pg pgid;
		u32 osd;
		int ret;

		ret = ceph_decode_pgid(p, end, &pgid);
		if (ret)
			return ret;

		ceph_decode_32_safe(p, end, osd, e_inval);

		ret = __remove_pg_mapping(&map->primary_temp, pgid);
		BUG_ON(!incremental && ret != -ENOENT);

		if (!incremental || osd != (u32)-1) {
			struct ceph_pg_mapping *pg;

			pg = kzalloc(sizeof(*pg), GFP_NOFS);
			if (!pg)
				return -ENOMEM;

			pg->pgid = pgid;
			pg->primary_temp.osd = osd;

			ret = __insert_pg_mapping(pg, &map->primary_temp);
			if (ret) {
				kfree(pg);
				return ret;
			}
		}
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int decode_primary_temp(void **p, void *end, struct ceph_osdmap *map)
{
	return __decode_primary_temp(p, end, map, false);
}

static int decode_new_primary_temp(void **p, void *end,
				   struct ceph_osdmap *map)
{
	return __decode_primary_temp(p, end, map, true);
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

		map->osd_primary_affinity = kmalloc(map->max_osd*sizeof(u32),
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
		kfree(map->osd_primary_affinity);
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

/*
 * decode a full map.
 */
static int osdmap_decode(void **p, void *end, struct ceph_osdmap *map)
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
			 map->max_osd*(1 + sizeof(*map->osd_weight) +
				       sizeof(*map->osd_addr)), e_inval);

	if (ceph_decode_32(p) != map->max_osd)
		goto e_inval;

	ceph_decode_copy(p, map->osd_state, map->max_osd);

	if (ceph_decode_32(p) != map->max_osd)
		goto e_inval;

	for (i = 0; i < map->max_osd; i++)
		map->osd_weight[i] = ceph_decode_32(p);

	if (ceph_decode_32(p) != map->max_osd)
		goto e_inval;

	ceph_decode_copy(p, map->osd_addr, map->max_osd*sizeof(*map->osd_addr));
	for (i = 0; i < map->max_osd; i++)
		ceph_decode_addr(&map->osd_addr[i]);

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
		/* XXX can this happen? */
		kfree(map->osd_primary_affinity);
		map->osd_primary_affinity = NULL;
	}

	/* crush */
	ceph_decode_32_safe(p, end, len, e_inval);
	map->crush = crush_decode(*p, min(*p + len, end));
	if (IS_ERR(map->crush)) {
		err = PTR_ERR(map->crush);
		map->crush = NULL;
		goto bad;
	}
	*p += len;

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
struct ceph_osdmap *ceph_osdmap_decode(void **p, void *end)
{
	struct ceph_osdmap *map;
	int ret;

	map = kzalloc(sizeof(*map), GFP_NOFS);
	if (!map)
		return ERR_PTR(-ENOMEM);

	map->pg_temp = RB_ROOT;
	map->primary_temp = RB_ROOT;
	mutex_init(&map->crush_scratch_mutex);

	ret = osdmap_decode(p, end, map);
	if (ret) {
		ceph_osdmap_destroy(map);
		return ERR_PTR(ret);
	}

	return map;
}

/*
 * decode and apply an incremental map update.
 */
struct ceph_osdmap *osdmap_apply_incremental(void **p, void *end,
					     struct ceph_osdmap *map)
{
	struct crush_map *newcrush = NULL;
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
		return ceph_osdmap_decode(p, min(*p+len, end));
	}

	/* new crush? */
	ceph_decode_32_safe(p, end, len, e_inval);
	if (len > 0) {
		newcrush = crush_decode(*p, min(*p+len, end));
		if (IS_ERR(newcrush)) {
			err = PTR_ERR(newcrush);
			newcrush = NULL;
			goto bad;
		}
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
	if (newcrush) {
		if (map->crush)
			crush_destroy(map->crush);
		map->crush = newcrush;
		newcrush = NULL;
	}

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
		pi = __lookup_pg_pool(&map->pg_pools, pool);
		if (pi)
			__remove_pg_pool(&map->pg_pools, pi);
	}

	/* new_up */
	ceph_decode_32_safe(p, end, len, e_inval);
	while (len--) {
		u32 osd;
		struct ceph_entity_addr addr;
		ceph_decode_32_safe(p, end, osd, e_inval);
		ceph_decode_copy_safe(p, end, &addr, sizeof(addr), e_inval);
		ceph_decode_addr(&addr);
		pr_info("osd%d up\n", osd);
		BUG_ON(osd >= map->max_osd);
		map->osd_state[osd] |= CEPH_OSD_UP | CEPH_OSD_EXISTS;
		map->osd_addr[osd] = addr;
	}

	/* new_state */
	ceph_decode_32_safe(p, end, len, e_inval);
	while (len--) {
		u32 osd;
		u8 xorstate;
		ceph_decode_32_safe(p, end, osd, e_inval);
		xorstate = **(u8 **)p;
		(*p)++;  /* clean flag */
		if (xorstate == 0)
			xorstate = CEPH_OSD_UP;
		if (xorstate & CEPH_OSD_UP)
			pr_info("osd%d down\n", osd);
		if (osd < map->max_osd)
			map->osd_state[osd] ^= xorstate;
	}

	/* new_weight */
	ceph_decode_32_safe(p, end, len, e_inval);
	while (len--) {
		u32 osd, off;
		ceph_decode_need(p, end, sizeof(u32)*2, e_inval);
		osd = ceph_decode_32(p);
		off = ceph_decode_32(p);
		pr_info("osd%d weight 0x%x %s\n", osd, off,
		     off == CEPH_OSD_IN ? "(in)" :
		     (off == CEPH_OSD_OUT ? "(out)" : ""));
		if (osd < map->max_osd)
			map->osd_weight[osd] = off;
	}

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
	if (newcrush)
		crush_destroy(newcrush);
	return ERR_PTR(err);
}

void ceph_oid_copy(struct ceph_object_id *dest,
		   const struct ceph_object_id *src)
{
	WARN_ON(!ceph_oid_empty(dest));

	if (src->name != src->inline_name) {
		/* very rare, see ceph_object_id definition */
		dest->name = kmalloc(src->name_len + 1,
				     GFP_NOIO | __GFP_NOFAIL);
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

/*
 * calculate file layout from given offset, length.
 * fill in correct oid, logical length, and object extent
 * offset, length.
 *
 * for now, we write only a single su, until we can
 * pass a stride back to the caller.
 */
int ceph_calc_file_object_mapping(struct ceph_file_layout *layout,
				   u64 off, u64 len,
				   u64 *ono,
				   u64 *oxoff, u64 *oxlen)
{
	u32 osize = le32_to_cpu(layout->fl_object_size);
	u32 su = le32_to_cpu(layout->fl_stripe_unit);
	u32 sc = le32_to_cpu(layout->fl_stripe_count);
	u32 bl, stripeno, stripepos, objsetno;
	u32 su_per_object;
	u64 t, su_offset;

	dout("mapping %llu~%llu  osize %u fl_su %u\n", off, len,
	     osize, su);
	if (su == 0 || sc == 0)
		goto invalid;
	su_per_object = osize / su;
	if (su_per_object == 0)
		goto invalid;
	dout("osize %u / su %u = su_per_object %u\n", osize, su,
	     su_per_object);

	if ((su & ~PAGE_MASK) != 0)
		goto invalid;

	/* bl = *off / su; */
	t = off;
	do_div(t, su);
	bl = t;
	dout("off %llu / su %u = bl %u\n", off, su, bl);

	stripeno = bl / sc;
	stripepos = bl % sc;
	objsetno = stripeno / su_per_object;

	*ono = objsetno * sc + stripepos;
	dout("objset %u * sc %u = ono %u\n", objsetno, sc, (unsigned int)*ono);

	/* *oxoff = *off % layout->fl_stripe_unit;  # offset in su */
	t = off;
	su_offset = do_div(t, su);
	*oxoff = su_offset + (stripeno % su_per_object) * su;

	/*
	 * Calculate the length of the extent being written to the selected
	 * object. This is the minimum of the full length requested (len) or
	 * the remainder of the current stripe being written to.
	 */
	*oxlen = min_t(u64, len, su - su_offset);

	dout(" obj extent %llu~%llu\n", *oxoff, *oxlen);
	return 0;

invalid:
	dout(" invalid layout\n");
	*ono = 0;
	*oxoff = 0;
	*oxlen = 0;
	return -EINVAL;
}
EXPORT_SYMBOL(ceph_calc_file_object_mapping);

/*
 * Map an object into a PG.
 *
 * Should only be called with target_oid and target_oloc (as opposed to
 * base_oid and base_oloc), since tiering isn't taken into account.
 */
int ceph_object_locator_to_pg(struct ceph_osdmap *osdmap,
			      struct ceph_object_id *oid,
			      struct ceph_object_locator *oloc,
			      struct ceph_pg *raw_pgid)
{
	struct ceph_pg_pool_info *pi;

	pi = ceph_pg_pool_by_id(osdmap, oloc->pool);
	if (!pi)
		return -ENOENT;

	raw_pgid->pool = oloc->pool;
	raw_pgid->seed = ceph_str_hash(pi->object_hash, oid->name,
				       oid->name_len);

	dout("%s %*pE -> raw_pgid %llu.%x\n", __func__, oid->name_len,
	     oid->name, raw_pgid->pool, raw_pgid->seed);
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

static int do_crush(struct ceph_osdmap *map, int ruleno, int x,
		    int *result, int result_max,
		    const __u32 *weight, int weight_max)
{
	int r;

	BUG_ON(result_max > CEPH_PG_MAX_SIZE);

	mutex_lock(&map->crush_scratch_mutex);
	r = crush_do_rule(map->crush, ruleno, x, result, result_max,
			  weight, weight_max, map->crush_scratch_ary);
	mutex_unlock(&map->crush_scratch_mutex);

	return r;
}

/*
 * Calculate raw set (CRUSH output) for given PG.  The result may
 * contain nonexistent OSDs.  ->primary is undefined for a raw set.
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

	len = do_crush(osdmap, ruleno, pps, raw->osds,
		       min_t(int, pi->size, ARRAY_SIZE(raw->osds)),
		       osdmap->osd_weight, osdmap->max_osd);
	if (len < 0) {
		pr_err("error %d from crush rule %d: pool %lld ruleset %d type %d size %d\n",
		       len, ruleno, pi->id, pi->crush_ruleset, pi->type,
		       pi->size);
		return;
	}

	raw->size = len;
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
			  const struct ceph_pg *raw_pgid,
			  struct ceph_osds *temp)
{
	struct ceph_pg pgid;
	struct ceph_pg_mapping *pg;
	int i;

	raw_pg_to_pg(pi, raw_pgid, &pgid);
	ceph_osds_init(temp);

	/* pg_temp? */
	pg = __lookup_pg_mapping(&osdmap->pg_temp, pgid);
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
	pg = __lookup_pg_mapping(&osdmap->primary_temp, pgid);
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
			       const struct ceph_pg *raw_pgid,
			       struct ceph_osds *up,
			       struct ceph_osds *acting)
{
	struct ceph_pg_pool_info *pi;
	u32 pps;

	pi = ceph_pg_pool_by_id(osdmap, raw_pgid->pool);
	if (!pi) {
		ceph_osds_init(up);
		ceph_osds_init(acting);
		goto out;
	}

	pg_to_raw_osds(osdmap, pi, raw_pgid, up, &pps);
	raw_to_up_osds(osdmap, pi, up);
	apply_primary_affinity(osdmap, pi, pps, up);
	get_temp_osds(osdmap, pi, raw_pgid, acting);
	if (!acting->size) {
		memcpy(acting->osds, up->osds, up->size * sizeof(up->osds[0]));
		acting->size = up->size;
		if (acting->primary == -1)
			acting->primary = up->primary;
	}
out:
	WARN_ON(!osds_valid(up) || !osds_valid(acting));
}

/*
 * Return primary osd for given pgid, or -1 if none.
 */
int ceph_calc_pg_primary(struct ceph_osdmap *osdmap, struct ceph_pg pgid)
{
	struct ceph_osds up, acting;

	ceph_pg_to_up_acting_osds(osdmap, &pgid, &up, &acting);
	return acting.primary;
}
EXPORT_SYMBOL(ceph_calc_pg_primary);
