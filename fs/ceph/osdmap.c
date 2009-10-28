
#include <asm/div64.h>

#include "super.h"
#include "osdmap.h"
#include "crush/hash.h"
#include "crush/mapper.h"
#include "decode.h"
#include "ceph_debug.h"

char *ceph_osdmap_state_str(char *str, int len, int state)
{
	int flag = 0;

	if (!len)
		goto done;

	*str = '\0';
	if (state) {
		if (state & CEPH_OSD_EXISTS) {
			snprintf(str, len, "exists");
			flag = 1;
		}
		if (state & CEPH_OSD_UP) {
			snprintf(str, len, "%s%s%s", str, (flag ? ", " : ""),
				 "up");
			flag = 1;
		}
	} else {
		snprintf(str, len, "doesn't exist");
	}
done:
	return str;
}

/* maps */

static int calc_bits_of(unsigned t)
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
	pi->pg_num_mask = (1 << calc_bits_of(le32_to_cpu(pi->v.pg_num)-1)) - 1;
	pi->pgp_num_mask =
		(1 << calc_bits_of(le32_to_cpu(pi->v.pgp_num)-1)) - 1;
	pi->lpg_num_mask =
		(1 << calc_bits_of(le32_to_cpu(pi->v.lpg_num)-1)) - 1;
	pi->lpgp_num_mask =
		(1 << calc_bits_of(le32_to_cpu(pi->v.lpgp_num)-1)) - 1;
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
	ceph_decode_32_safe(p, end, b->num_nodes, bad);
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

static struct crush_map *crush_decode(void *pbyval, void *end)
{
	struct crush_map *c;
	int err = -EINVAL;
	int i, j;
	void **p = &pbyval;
	void *start = pbyval;
	u32 magic;

	dout("crush_decode %p to %p len %d\n", *p, end, (int)(end - *p));

	c = kzalloc(sizeof(*c), GFP_NOFS);
	if (c == NULL)
		return ERR_PTR(-ENOMEM);

	ceph_decode_need(p, end, 4*sizeof(u32), bad);
	magic = ceph_decode_32(p);
	if (magic != CRUSH_MAGIC) {
		pr_err("crush_decode magic %x != current %x\n",
		       (unsigned)magic, (unsigned)CRUSH_MAGIC);
		goto bad;
	}
	c->max_buckets = ceph_decode_32(p);
	c->max_rules = ceph_decode_32(p);
	c->max_devices = ceph_decode_32(p);

	c->device_parents = kcalloc(c->max_devices, sizeof(u32), GFP_NOFS);
	if (c->device_parents == NULL)
		goto badmem;
	c->bucket_parents = kcalloc(c->max_buckets, sizeof(u32), GFP_NOFS);
	if (c->bucket_parents == NULL)
		goto badmem;

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
		b->alg = ceph_decode_16(p);
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
		if (yes > ULONG_MAX / sizeof(struct crush_rule_step))
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
 * osd map
 */
void ceph_osdmap_destroy(struct ceph_osdmap *map)
{
	dout("osdmap_destroy %p\n", map);
	if (map->crush)
		crush_destroy(map->crush);
	while (!RB_EMPTY_ROOT(&map->pg_temp))
		rb_erase(rb_first(&map->pg_temp), &map->pg_temp);
	kfree(map->osd_state);
	kfree(map->osd_weight);
	kfree(map->pg_pool);
	kfree(map->osd_addr);
	kfree(map);
}

/*
 * adjust max osd value.  reallocate arrays.
 */
static int osdmap_set_max_osd(struct ceph_osdmap *map, int max)
{
	u8 *state;
	struct ceph_entity_addr *addr;
	u32 *weight;

	state = kcalloc(max, sizeof(*state), GFP_NOFS);
	addr = kcalloc(max, sizeof(*addr), GFP_NOFS);
	weight = kcalloc(max, sizeof(*weight), GFP_NOFS);
	if (state == NULL || addr == NULL || weight == NULL) {
		kfree(state);
		kfree(addr);
		kfree(weight);
		return -ENOMEM;
	}

	/* copy old? */
	if (map->osd_state) {
		memcpy(state, map->osd_state, map->max_osd*sizeof(*state));
		memcpy(addr, map->osd_addr, map->max_osd*sizeof(*addr));
		memcpy(weight, map->osd_weight, map->max_osd*sizeof(*weight));
		kfree(map->osd_state);
		kfree(map->osd_addr);
		kfree(map->osd_weight);
	}

	map->osd_state = state;
	map->osd_weight = weight;
	map->osd_addr = addr;
	map->max_osd = max;
	return 0;
}

/*
 * Insert a new pg_temp mapping
 */
static int __insert_pg_mapping(struct ceph_pg_mapping *new,
			       struct rb_root *root)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct ceph_pg_mapping *pg = NULL;

	while (*p) {
		parent = *p;
		pg = rb_entry(parent, struct ceph_pg_mapping, node);
		if (new->pgid < pg->pgid)
			p = &(*p)->rb_left;
		else if (new->pgid > pg->pgid)
			p = &(*p)->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, root);
	return 0;
}

/*
 * decode a full map.
 */
struct ceph_osdmap *osdmap_decode(void **p, void *end)
{
	struct ceph_osdmap *map;
	u16 version;
	u32 len, max, i;
	int err = -EINVAL;
	void *start = *p;

	dout("osdmap_decode %p to %p len %d\n", *p, end, (int)(end - *p));

	map = kzalloc(sizeof(*map), GFP_NOFS);
	if (map == NULL)
		return ERR_PTR(-ENOMEM);
	map->pg_temp = RB_ROOT;

	ceph_decode_16_safe(p, end, version, bad);

	ceph_decode_need(p, end, 2*sizeof(u64)+6*sizeof(u32), bad);
	ceph_decode_copy(p, &map->fsid, sizeof(map->fsid));
	map->epoch = ceph_decode_32(p);
	ceph_decode_copy(p, &map->created, sizeof(map->created));
	ceph_decode_copy(p, &map->modified, sizeof(map->modified));

	map->num_pools = ceph_decode_32(p);
	map->pg_pool = kcalloc(map->num_pools, sizeof(*map->pg_pool),
			       GFP_NOFS);
	if (!map->pg_pool) {
		err = -ENOMEM;
		goto bad;
	}
	ceph_decode_32_safe(p, end, max, bad);
	while (max--) {
		ceph_decode_need(p, end, 4+sizeof(map->pg_pool->v), bad);
		i = ceph_decode_32(p);
		if (i >= map->num_pools)
			goto bad;
		ceph_decode_copy(p, &map->pg_pool[i].v,
				 sizeof(map->pg_pool->v));
		calc_pg_masks(&map->pg_pool[i]);
		p += le32_to_cpu(map->pg_pool[i].v.num_snaps) * sizeof(u64);
		p += le32_to_cpu(map->pg_pool[i].v.num_removed_snap_intervals)
			* sizeof(u64) * 2;
	}

	ceph_decode_32_safe(p, end, map->flags, bad);

	max = ceph_decode_32(p);

	/* (re)alloc osd arrays */
	err = osdmap_set_max_osd(map, max);
	if (err < 0)
		goto bad;
	dout("osdmap_decode max_osd = %d\n", map->max_osd);

	/* osds */
	err = -EINVAL;
	ceph_decode_need(p, end, 3*sizeof(u32) +
			 map->max_osd*(1 + sizeof(*map->osd_weight) +
				       sizeof(*map->osd_addr)), bad);
	*p += 4; /* skip length field (should match max) */
	ceph_decode_copy(p, map->osd_state, map->max_osd);

	*p += 4; /* skip length field (should match max) */
	for (i = 0; i < map->max_osd; i++)
		map->osd_weight[i] = ceph_decode_32(p);

	*p += 4; /* skip length field (should match max) */
	ceph_decode_copy(p, map->osd_addr, map->max_osd*sizeof(*map->osd_addr));

	/* pg_temp */
	ceph_decode_32_safe(p, end, len, bad);
	for (i = 0; i < len; i++) {
		int n, j;
		u64 pgid;
		struct ceph_pg_mapping *pg;

		ceph_decode_need(p, end, sizeof(u32) + sizeof(u64), bad);
		pgid = ceph_decode_64(p);
		n = ceph_decode_32(p);
		ceph_decode_need(p, end, n * sizeof(u32), bad);
		pg = kmalloc(sizeof(*pg) + n*sizeof(u32), GFP_NOFS);
		if (!pg) {
			err = -ENOMEM;
			goto bad;
		}
		pg->pgid = pgid;
		pg->len = n;
		for (j = 0; j < n; j++)
			pg->osds[j] = ceph_decode_32(p);

		err = __insert_pg_mapping(pg, &map->pg_temp);
		if (err)
			goto bad;
		dout(" added pg_temp %llx len %d\n", pgid, len);
	}

	/* crush */
	ceph_decode_32_safe(p, end, len, bad);
	dout("osdmap_decode crush len %d from off 0x%x\n", len,
	     (int)(*p - start));
	ceph_decode_need(p, end, len, bad);
	map->crush = crush_decode(*p, end);
	*p += len;
	if (IS_ERR(map->crush)) {
		err = PTR_ERR(map->crush);
		map->crush = NULL;
		goto bad;
	}

	/* ignore the rest of the map */
	*p = end;

	dout("osdmap_decode done %p %p\n", *p, end);
	return map;

bad:
	dout("osdmap_decode fail\n");
	ceph_osdmap_destroy(map);
	return ERR_PTR(err);
}

/*
 * decode and apply an incremental map update.
 */
struct ceph_osdmap *osdmap_apply_incremental(void **p, void *end,
					     struct ceph_osdmap *map,
					     struct ceph_messenger *msgr)
{
	struct ceph_osdmap *newmap = map;
	struct crush_map *newcrush = NULL;
	struct ceph_fsid fsid;
	u32 epoch = 0;
	struct ceph_timespec modified;
	u32 len, pool;
	__s32 new_flags, max;
	void *start = *p;
	int err = -EINVAL;
	u16 version;
	struct rb_node *rbp;

	ceph_decode_16_safe(p, end, version, bad);

	ceph_decode_need(p, end, sizeof(fsid)+sizeof(modified)+2*sizeof(u32),
			 bad);
	ceph_decode_copy(p, &fsid, sizeof(fsid));
	epoch = ceph_decode_32(p);
	BUG_ON(epoch != map->epoch+1);
	ceph_decode_copy(p, &modified, sizeof(modified));
	new_flags = ceph_decode_32(p);

	/* full map? */
	ceph_decode_32_safe(p, end, len, bad);
	if (len > 0) {
		dout("apply_incremental full map len %d, %p to %p\n",
		     len, *p, end);
		newmap = osdmap_decode(p, min(*p+len, end));
		return newmap;  /* error or not */
	}

	/* new crush? */
	ceph_decode_32_safe(p, end, len, bad);
	if (len > 0) {
		dout("apply_incremental new crush map len %d, %p to %p\n",
		     len, *p, end);
		newcrush = crush_decode(*p, min(*p+len, end));
		if (IS_ERR(newcrush))
			return ERR_PTR(PTR_ERR(newcrush));
	}

	/* new flags? */
	if (new_flags >= 0)
		map->flags = new_flags;

	ceph_decode_need(p, end, 5*sizeof(u32), bad);

	/* new max? */
	max = ceph_decode_32(p);
	if (max >= 0) {
		err = osdmap_set_max_osd(map, max);
		if (err < 0)
			goto bad;
	}

	map->epoch++;
	map->modified = map->modified;
	if (newcrush) {
		if (map->crush)
			crush_destroy(map->crush);
		map->crush = newcrush;
		newcrush = NULL;
	}

	/* new_pool */
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		ceph_decode_32_safe(p, end, pool, bad);
		if (pool >= map->num_pools) {
			void *pg_pool = kcalloc(pool + 1,
						sizeof(*map->pg_pool),
						GFP_NOFS);
			if (!pg_pool) {
				err = -ENOMEM;
				goto bad;
			}
			memcpy(pg_pool, map->pg_pool,
			       map->num_pools * sizeof(*map->pg_pool));
			kfree(map->pg_pool);
			map->pg_pool = pg_pool;
			map->num_pools = pool+1;
		}
		ceph_decode_copy(p, &map->pg_pool[pool].v,
				 sizeof(map->pg_pool->v));
		calc_pg_masks(&map->pg_pool[pool]);
	}

	/* old_pool (ignore) */
	ceph_decode_32_safe(p, end, len, bad);
	*p += len * sizeof(u32);

	/* new_up */
	err = -EINVAL;
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		u32 osd;
		struct ceph_entity_addr addr;
		ceph_decode_32_safe(p, end, osd, bad);
		ceph_decode_copy_safe(p, end, &addr, sizeof(addr), bad);
		pr_info("osd%d up\n", osd);
		BUG_ON(osd >= map->max_osd);
		map->osd_state[osd] |= CEPH_OSD_UP;
		map->osd_addr[osd] = addr;
	}

	/* new_down */
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		u32 osd;
		ceph_decode_32_safe(p, end, osd, bad);
		(*p)++;  /* clean flag */
		pr_info("ceph osd%d down\n", osd);
		if (osd < map->max_osd)
			map->osd_state[osd] &= ~CEPH_OSD_UP;
	}

	/* new_weight */
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		u32 osd, off;
		ceph_decode_need(p, end, sizeof(u32)*2, bad);
		osd = ceph_decode_32(p);
		off = ceph_decode_32(p);
		pr_info("osd%d weight 0x%x %s\n", osd, off,
		     off == CEPH_OSD_IN ? "(in)" :
		     (off == CEPH_OSD_OUT ? "(out)" : ""));
		if (osd < map->max_osd)
			map->osd_weight[osd] = off;
	}

	/* new_pg_temp */
	rbp = rb_first(&map->pg_temp);
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		struct ceph_pg_mapping *pg;
		int j;
		u64 pgid;
		u32 pglen;
		ceph_decode_need(p, end, sizeof(u64) + sizeof(u32), bad);
		pgid = ceph_decode_64(p);
		pglen = ceph_decode_32(p);

		/* remove any? */
		while (rbp && rb_entry(rbp, struct ceph_pg_mapping,
				       node)->pgid <= pgid) {
			struct rb_node *cur = rbp;
			rbp = rb_next(rbp);
			dout(" removed pg_temp %llx\n",
			     rb_entry(cur, struct ceph_pg_mapping, node)->pgid);
			rb_erase(cur, &map->pg_temp);
		}

		if (pglen) {
			/* insert */
			ceph_decode_need(p, end, pglen*sizeof(u32), bad);
			pg = kmalloc(sizeof(*pg) + sizeof(u32)*pglen, GFP_NOFS);
			if (!pg) {
				err = -ENOMEM;
				goto bad;
			}
			pg->pgid = pgid;
			pg->len = pglen;
			for (j = 0; j < len; j++)
				pg->osds[j] = ceph_decode_32(p);
			err = __insert_pg_mapping(pg, &map->pg_temp);
			if (err)
				goto bad;
			dout(" added pg_temp %llx len %d\n", pgid, pglen);
		}
	}
	while (rbp) {
		struct rb_node *cur = rbp;
		rbp = rb_next(rbp);
		dout(" removed pg_temp %llx\n",
		     rb_entry(cur, struct ceph_pg_mapping, node)->pgid);
		rb_erase(cur, &map->pg_temp);
	}

	/* ignore the rest */
	*p = end;
	return map;

bad:
	pr_err("corrupt inc osdmap epoch %d off %d (%p of %p-%p)\n",
	       epoch, (int)(*p - start), *p, start, end);
	if (newcrush)
		crush_destroy(newcrush);
	return ERR_PTR(err);
}




/*
 * calculate file layout from given offset, length.
 * fill in correct oid, logical length, and object extent
 * offset, length.
 *
 * for now, we write only a single su, until we can
 * pass a stride back to the caller.
 */
void ceph_calc_file_object_mapping(struct ceph_file_layout *layout,
				   u64 off, u64 *plen,
				   u64 *ono,
				   u64 *oxoff, u64 *oxlen)
{
	u32 osize = le32_to_cpu(layout->fl_object_size);
	u32 su = le32_to_cpu(layout->fl_stripe_unit);
	u32 sc = le32_to_cpu(layout->fl_stripe_count);
	u32 bl, stripeno, stripepos, objsetno;
	u32 su_per_object;
	u64 t;

	dout("mapping %llu~%llu  osize %u fl_su %u\n", off, *plen,
	     osize, su);
	su_per_object = osize / su;
	dout("osize %u / su %u = su_per_object %u\n", osize, su,
	     su_per_object);

	BUG_ON((su & ~PAGE_MASK) != 0);
	/* bl = *off / su; */
	t = off;
	do_div(t, su);
	bl = t;
	dout("off %llu / su %u = bl %u\n", off, su, bl);

	stripeno = bl / sc;
	stripepos = bl % sc;
	objsetno = stripeno / su_per_object;

	*ono = objsetno * sc + stripepos;
	dout("objset %u * sc %u = ono %u\n", objsetno, sc, (unsigned)*ono);

	/* *oxoff = *off % layout->fl_stripe_unit;  # offset in su */
	t = off;
	*oxoff = do_div(t, su);
	*oxoff += (stripeno % su_per_object) * su;

	*oxlen = min_t(u64, *plen, su - *oxoff);
	*plen = *oxlen;

	dout(" obj extent %llu~%llu\n", *oxoff, *oxlen);
}

/*
 * calculate an object layout (i.e. pgid) from an oid,
 * file_layout, and osdmap
 */
int ceph_calc_object_layout(struct ceph_object_layout *ol,
			    const char *oid,
			    struct ceph_file_layout *fl,
			    struct ceph_osdmap *osdmap)
{
	unsigned num, num_mask;
	union ceph_pg pgid;
	s32 preferred = (s32)le32_to_cpu(fl->fl_pg_preferred);
	int poolid = le32_to_cpu(fl->fl_pg_pool);
	struct ceph_pg_pool_info *pool;

	if (poolid >= osdmap->num_pools)
		return -EIO;
	pool = &osdmap->pg_pool[poolid];

	if (preferred >= 0) {
		num = le32_to_cpu(pool->v.lpg_num);
		num_mask = pool->lpg_num_mask;
	} else {
		num = le32_to_cpu(pool->v.pg_num);
		num_mask = pool->pg_num_mask;
	}

	pgid.pg64 = 0;   /* start with it zeroed out */
	pgid.pg.ps = ceph_full_name_hash(oid, strlen(oid));
	pgid.pg.preferred = preferred;
	if (preferred >= 0)
		pgid.pg.ps += preferred;
	pgid.pg.pool = le32_to_cpu(fl->fl_pg_pool);
	if (preferred >= 0)
		dout("calc_object_layout '%s' pgid %d.%xp%d (%llx)\n", oid,
		     pgid.pg.pool, pgid.pg.ps, (int)preferred, pgid.pg64);
	else
		dout("calc_object_layout '%s' pgid %d.%x (%llx)\n", oid,
		     pgid.pg.pool, pgid.pg.ps, pgid.pg64);

	ol->ol_pgid = cpu_to_le64(pgid.pg64);
	ol->ol_stripe_unit = fl->fl_object_stripe_unit;

	return 0;
}

/*
 * Calculate raw osd vector for the given pgid.  Return pointer to osd
 * array, or NULL on failure.
 */
static int *calc_pg_raw(struct ceph_osdmap *osdmap, union ceph_pg pgid,
			int *osds, int *num)
{
	struct rb_node *n = osdmap->pg_temp.rb_node;
	struct ceph_pg_mapping *pg;
	struct ceph_pg_pool_info *pool;
	int ruleno;
	unsigned pps; /* placement ps */

	/* pg_temp? */
	while (n) {
		pg = rb_entry(n, struct ceph_pg_mapping, node);
		if (pgid.pg64 < pg->pgid)
			n = n->rb_left;
		else if (pgid.pg64 > pg->pgid)
			n = n->rb_right;
		else {
			*num = pg->len;
			return pg->osds;
		}
	}

	/* crush */
	if (pgid.pg.pool >= osdmap->num_pools)
		return NULL;
	pool = &osdmap->pg_pool[pgid.pg.pool];
	ruleno = crush_find_rule(osdmap->crush, pool->v.crush_ruleset,
				 pool->v.type, pool->v.size);
	if (ruleno < 0) {
		pr_err("no crush rule pool %d type %d size %d\n",
		       pgid.pg.pool, pool->v.type, pool->v.size);
		return NULL;
	}

	if (pgid.pg.preferred >= 0)
		pps = ceph_stable_mod(pgid.pg.ps,
				      le32_to_cpu(pool->v.lpgp_num),
				      pool->lpgp_num_mask);
	else
		pps = ceph_stable_mod(pgid.pg.ps,
				      le32_to_cpu(pool->v.pgp_num),
				      pool->pgp_num_mask);
	pps += pgid.pg.pool;
	*num = crush_do_rule(osdmap->crush, ruleno, pps, osds,
			     min_t(int, pool->v.size, *num),
			     pgid.pg.preferred, osdmap->osd_weight);
	return osds;
}

/*
 * Return primary osd for given pgid, or -1 if none.
 */
int ceph_calc_pg_primary(struct ceph_osdmap *osdmap, union ceph_pg pgid)
{
	int rawosds[10], *osds;
	int i, num = ARRAY_SIZE(rawosds);

	osds = calc_pg_raw(osdmap, pgid, rawosds, &num);
	if (!osds)
		return -1;

	/* primary is first up osd */
	for (i = 0; i < num; i++)
		if (ceph_osd_is_up(osdmap, osds[i])) {
			return osds[i];
			break;
		}
	return -1;
}
