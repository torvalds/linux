
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
 * to a set of osds)
 */
static int pgid_cmp(struct ceph_pg l, struct ceph_pg r)
{
	u64 a = *(u64 *)&l;
	u64 b = *(u64 *)&r;

	if (a < b)
		return -1;
	if (a > b)
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
			dout("__lookup_pg_mapping %llx got %p\n",
			     *(u64 *)&pgid, pg);
			return pg;
		}
	}
	return NULL;
}

static int __remove_pg_mapping(struct rb_root *root, struct ceph_pg pgid)
{
	struct ceph_pg_mapping *pg = __lookup_pg_mapping(root, pgid);

	if (pg) {
		dout("__remove_pg_mapping %llx %p\n", *(u64 *)&pgid, pg);
		rb_erase(&pg->node, root);
		kfree(pg);
		return 0;
	}
	dout("__remove_pg_mapping %llx dne\n", *(u64 *)&pgid);
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

static struct ceph_pg_pool_info *__lookup_pg_pool(struct rb_root *root, int id)
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

static int __decode_pool(void **p, void *end, struct ceph_pg_pool_info *pi)
{
	unsigned n, m;

	ceph_decode_copy(p, &pi->v, sizeof(pi->v));
	calc_pg_masks(pi);

	/* num_snaps * snap_info_t */
	n = le32_to_cpu(pi->v.num_snaps);
	while (n--) {
		ceph_decode_need(p, end, sizeof(u64) + 1 + sizeof(u64) +
				 sizeof(struct ceph_timespec), bad);
		*p += sizeof(u64) +       /* key */
			1 + sizeof(u64) + /* u8, snapid */
			sizeof(struct ceph_timespec);
		m = ceph_decode_32(p);    /* snap name */
		*p += m;
	}

	*p += le32_to_cpu(pi->v.num_removed_snap_intervals) * sizeof(u64) * 2;
	return 0;

bad:
	return -EINVAL;
}

static int __decode_pool_names(void **p, void *end, struct ceph_osdmap *map)
{
	struct ceph_pg_pool_info *pi;
	u32 num, len, pool;

	ceph_decode_32_safe(p, end, num, bad);
	dout(" %d pool names\n", num);
	while (num--) {
		ceph_decode_32_safe(p, end, pool, bad);
		ceph_decode_32_safe(p, end, len, bad);
		dout("  pool %d len %d\n", pool, len);
		pi = __lookup_pg_pool(&map->pg_pools, pool);
		if (pi) {
			kfree(pi->name);
			pi->name = kmalloc(len + 1, GFP_NOFS);
			if (pi->name) {
				memcpy(pi->name, *p, len);
				pi->name[len] = '\0';
				dout("  name is %s\n", pi->name);
			}
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
	while (!RB_EMPTY_ROOT(&map->pg_pools)) {
		struct ceph_pg_pool_info *pi =
			rb_entry(rb_first(&map->pg_pools),
				 struct ceph_pg_pool_info, node);
		__remove_pg_pool(&map->pg_pools, pi);
	}
	kfree(map->osd_state);
	kfree(map->osd_weight);
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
 * decode a full map.
 */
struct ceph_osdmap *osdmap_decode(void **p, void *end)
{
	struct ceph_osdmap *map;
	u16 version;
	u32 len, max, i;
	u8 ev;
	int err = -EINVAL;
	void *start = *p;
	struct ceph_pg_pool_info *pi;

	dout("osdmap_decode %p to %p len %d\n", *p, end, (int)(end - *p));

	map = kzalloc(sizeof(*map), GFP_NOFS);
	if (map == NULL)
		return ERR_PTR(-ENOMEM);
	map->pg_temp = RB_ROOT;

	ceph_decode_16_safe(p, end, version, bad);
	if (version > CEPH_OSDMAP_VERSION) {
		pr_warning("got unknown v %d > %d of osdmap\n", version,
			   CEPH_OSDMAP_VERSION);
		goto bad;
	}

	ceph_decode_need(p, end, 2*sizeof(u64)+6*sizeof(u32), bad);
	ceph_decode_copy(p, &map->fsid, sizeof(map->fsid));
	map->epoch = ceph_decode_32(p);
	ceph_decode_copy(p, &map->created, sizeof(map->created));
	ceph_decode_copy(p, &map->modified, sizeof(map->modified));

	ceph_decode_32_safe(p, end, max, bad);
	while (max--) {
		ceph_decode_need(p, end, 4 + 1 + sizeof(pi->v), bad);
		pi = kzalloc(sizeof(*pi), GFP_NOFS);
		if (!pi)
			goto bad;
		pi->id = ceph_decode_32(p);
		ev = ceph_decode_8(p); /* encoding version */
		if (ev > CEPH_PG_POOL_VERSION) {
			pr_warning("got unknown v %d > %d of ceph_pg_pool\n",
				   ev, CEPH_PG_POOL_VERSION);
			kfree(pi);
			goto bad;
		}
		err = __decode_pool(p, end, pi);
		if (err < 0) {
			kfree(pi);
			goto bad;
		}
		__insert_pg_pool(&map->pg_pools, pi);
	}

	if (version >= 5 && __decode_pool_names(p, end, map) < 0)
		goto bad;

	ceph_decode_32_safe(p, end, map->pool_max, bad);

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
	for (i = 0; i < map->max_osd; i++)
		ceph_decode_addr(&map->osd_addr[i]);

	/* pg_temp */
	ceph_decode_32_safe(p, end, len, bad);
	for (i = 0; i < len; i++) {
		int n, j;
		struct ceph_pg pgid;
		struct ceph_pg_mapping *pg;

		ceph_decode_need(p, end, sizeof(u32) + sizeof(u64), bad);
		ceph_decode_copy(p, &pgid, sizeof(pgid));
		n = ceph_decode_32(p);
		ceph_decode_need(p, end, n * sizeof(u32), bad);
		err = -ENOMEM;
		pg = kmalloc(sizeof(*pg) + n*sizeof(u32), GFP_NOFS);
		if (!pg)
			goto bad;
		pg->pgid = pgid;
		pg->len = n;
		for (j = 0; j < n; j++)
			pg->osds[j] = ceph_decode_32(p);

		err = __insert_pg_mapping(pg, &map->pg_temp);
		if (err)
			goto bad;
		dout(" added pg_temp %llx len %d\n", *(u64 *)&pgid, len);
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
	struct crush_map *newcrush = NULL;
	struct ceph_fsid fsid;
	u32 epoch = 0;
	struct ceph_timespec modified;
	u32 len, pool;
	__s32 new_pool_max, new_flags, max;
	void *start = *p;
	int err = -EINVAL;
	u16 version;

	ceph_decode_16_safe(p, end, version, bad);
	if (version > CEPH_OSDMAP_INC_VERSION) {
		pr_warning("got unknown v %d > %d of inc osdmap\n", version,
			   CEPH_OSDMAP_INC_VERSION);
		goto bad;
	}

	ceph_decode_need(p, end, sizeof(fsid)+sizeof(modified)+2*sizeof(u32),
			 bad);
	ceph_decode_copy(p, &fsid, sizeof(fsid));
	epoch = ceph_decode_32(p);
	BUG_ON(epoch != map->epoch+1);
	ceph_decode_copy(p, &modified, sizeof(modified));
	new_pool_max = ceph_decode_32(p);
	new_flags = ceph_decode_32(p);

	/* full map? */
	ceph_decode_32_safe(p, end, len, bad);
	if (len > 0) {
		dout("apply_incremental full map len %d, %p to %p\n",
		     len, *p, end);
		return osdmap_decode(p, min(*p+len, end));
	}

	/* new crush? */
	ceph_decode_32_safe(p, end, len, bad);
	if (len > 0) {
		dout("apply_incremental new crush map len %d, %p to %p\n",
		     len, *p, end);
		newcrush = crush_decode(*p, min(*p+len, end));
		if (IS_ERR(newcrush))
			return ERR_CAST(newcrush);
		*p += len;
	}

	/* new flags? */
	if (new_flags >= 0)
		map->flags = new_flags;
	if (new_pool_max >= 0)
		map->pool_max = new_pool_max;

	ceph_decode_need(p, end, 5*sizeof(u32), bad);

	/* new max? */
	max = ceph_decode_32(p);
	if (max >= 0) {
		err = osdmap_set_max_osd(map, max);
		if (err < 0)
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

	/* new_pool */
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		__u8 ev;
		struct ceph_pg_pool_info *pi;

		ceph_decode_32_safe(p, end, pool, bad);
		ceph_decode_need(p, end, 1 + sizeof(pi->v), bad);
		ev = ceph_decode_8(p);  /* encoding version */
		if (ev > CEPH_PG_POOL_VERSION) {
			pr_warning("got unknown v %d > %d of ceph_pg_pool\n",
				   ev, CEPH_PG_POOL_VERSION);
			goto bad;
		}
		pi = __lookup_pg_pool(&map->pg_pools, pool);
		if (!pi) {
			pi = kzalloc(sizeof(*pi), GFP_NOFS);
			if (!pi) {
				err = -ENOMEM;
				goto bad;
			}
			pi->id = pool;
			__insert_pg_pool(&map->pg_pools, pi);
		}
		err = __decode_pool(p, end, pi);
		if (err < 0)
			goto bad;
	}
	if (version >= 5 && __decode_pool_names(p, end, map) < 0)
		goto bad;

	/* old_pool */
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		struct ceph_pg_pool_info *pi;

		ceph_decode_32_safe(p, end, pool, bad);
		pi = __lookup_pg_pool(&map->pg_pools, pool);
		if (pi)
			__remove_pg_pool(&map->pg_pools, pi);
	}

	/* new_up */
	err = -EINVAL;
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		u32 osd;
		struct ceph_entity_addr addr;
		ceph_decode_32_safe(p, end, osd, bad);
		ceph_decode_copy_safe(p, end, &addr, sizeof(addr), bad);
		ceph_decode_addr(&addr);
		pr_info("osd%d up\n", osd);
		BUG_ON(osd >= map->max_osd);
		map->osd_state[osd] |= CEPH_OSD_UP;
		map->osd_addr[osd] = addr;
	}

	/* new_state */
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		u32 osd;
		u8 xorstate;
		ceph_decode_32_safe(p, end, osd, bad);
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
	ceph_decode_32_safe(p, end, len, bad);
	while (len--) {
		struct ceph_pg_mapping *pg;
		int j;
		struct ceph_pg pgid;
		u32 pglen;
		ceph_decode_need(p, end, sizeof(u64) + sizeof(u32), bad);
		ceph_decode_copy(p, &pgid, sizeof(pgid));
		pglen = ceph_decode_32(p);

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
			for (j = 0; j < pglen; j++)
				pg->osds[j] = ceph_decode_32(p);
			err = __insert_pg_mapping(pg, &map->pg_temp);
			if (err) {
				kfree(pg);
				goto bad;
			}
			dout(" added pg_temp %llx len %d\n", *(u64 *)&pgid,
			     pglen);
		} else {
			/* remove */
			__remove_pg_mapping(&map->pg_temp, pgid);
		}
	}

	/* ignore the rest */
	*p = end;
	return map;

bad:
	pr_err("corrupt inc osdmap epoch %d off %d (%p of %p-%p)\n",
	       epoch, (int)(*p - start), *p, start, end);
	print_hex_dump(KERN_DEBUG, "osdmap: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       start, end - start, true);
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
	u64 t, su_offset;

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
	su_offset = do_div(t, su);
	*oxoff = su_offset + (stripeno % su_per_object) * su;

	/*
	 * Calculate the length of the extent being written to the selected
	 * object. This is the minimum of the full length requested (plen) or
	 * the remainder of the current stripe being written to.
	 */
	*oxlen = min_t(u64, *plen, su - su_offset);
	*plen = *oxlen;

	dout(" obj extent %llu~%llu\n", *oxoff, *oxlen);
}
EXPORT_SYMBOL(ceph_calc_file_object_mapping);

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
	struct ceph_pg pgid;
	int poolid = le32_to_cpu(fl->fl_pg_pool);
	struct ceph_pg_pool_info *pool;
	unsigned ps;

	BUG_ON(!osdmap);

	pool = __lookup_pg_pool(&osdmap->pg_pools, poolid);
	if (!pool)
		return -EIO;
	ps = ceph_str_hash(pool->v.object_hash, oid, strlen(oid));
	num = le32_to_cpu(pool->v.pg_num);
	num_mask = pool->pg_num_mask;

	pgid.ps = cpu_to_le16(ps);
	pgid.preferred = cpu_to_le16(-1);
	pgid.pool = fl->fl_pg_pool;
	dout("calc_object_layout '%s' pgid %d.%x\n", oid, poolid, ps);

	ol->ol_pgid = pgid;
	ol->ol_stripe_unit = fl->fl_object_stripe_unit;
	return 0;
}
EXPORT_SYMBOL(ceph_calc_object_layout);

/*
 * Calculate raw osd vector for the given pgid.  Return pointer to osd
 * array, or NULL on failure.
 */
static int *calc_pg_raw(struct ceph_osdmap *osdmap, struct ceph_pg pgid,
			int *osds, int *num)
{
	struct ceph_pg_mapping *pg;
	struct ceph_pg_pool_info *pool;
	int ruleno;
	unsigned poolid, ps, pps, t;

	poolid = le32_to_cpu(pgid.pool);
	ps = le16_to_cpu(pgid.ps);

	pool = __lookup_pg_pool(&osdmap->pg_pools, poolid);
	if (!pool)
		return NULL;

	/* pg_temp? */
	t = ceph_stable_mod(ps, le32_to_cpu(pool->v.pg_num),
			    pool->pgp_num_mask);
	pgid.ps = cpu_to_le16(t);
	pg = __lookup_pg_mapping(&osdmap->pg_temp, pgid);
	if (pg) {
		*num = pg->len;
		return pg->osds;
	}

	/* crush */
	ruleno = crush_find_rule(osdmap->crush, pool->v.crush_ruleset,
				 pool->v.type, pool->v.size);
	if (ruleno < 0) {
		pr_err("no crush rule pool %d ruleset %d type %d size %d\n",
		       poolid, pool->v.crush_ruleset, pool->v.type,
		       pool->v.size);
		return NULL;
	}

	pps = ceph_stable_mod(ps,
			      le32_to_cpu(pool->v.pgp_num),
			      pool->pgp_num_mask);
	pps += poolid;
	*num = crush_do_rule(osdmap->crush, ruleno, pps, osds,
			     min_t(int, pool->v.size, *num),
			     -1, osdmap->osd_weight);
	return osds;
}

/*
 * Return acting set for given pgid.
 */
int ceph_calc_pg_acting(struct ceph_osdmap *osdmap, struct ceph_pg pgid,
			int *acting)
{
	int rawosds[CEPH_PG_MAX_SIZE], *osds;
	int i, o, num = CEPH_PG_MAX_SIZE;

	osds = calc_pg_raw(osdmap, pgid, rawosds, &num);
	if (!osds)
		return -1;

	/* primary is first up osd */
	o = 0;
	for (i = 0; i < num; i++)
		if (ceph_osd_is_up(osdmap, osds[i]))
			acting[o++] = osds[i];
	return o;
}

/*
 * Return primary osd for given pgid, or -1 if none.
 */
int ceph_calc_pg_primary(struct ceph_osdmap *osdmap, struct ceph_pg pgid)
{
	int rawosds[CEPH_PG_MAX_SIZE], *osds;
	int i, num = CEPH_PG_MAX_SIZE;

	osds = calc_pg_raw(osdmap, pgid, rawosds, &num);
	if (!osds)
		return -1;

	/* primary is first up osd */
	for (i = 0; i < num; i++)
		if (ceph_osd_is_up(osdmap, osds[i]))
			return osds[i];
	return -1;
}
EXPORT_SYMBOL(ceph_calc_pg_primary);
