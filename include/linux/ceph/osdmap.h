#ifndef _FS_CEPH_OSDMAP_H
#define _FS_CEPH_OSDMAP_H

#include <linux/rbtree.h>
#include <linux/ceph/types.h>
#include <linux/ceph/ceph_fs.h>
#include <linux/crush/crush.h>

/*
 * The osd map describes the current membership of the osd cluster and
 * specifies the mapping of objects to placement groups and placement
 * groups to (sets of) osds.  That is, it completely specifies the
 * (desired) distribution of all data objects in the system at some
 * point in time.
 *
 * Each map version is identified by an epoch, which increases monotonically.
 *
 * The map can be updated either via an incremental map (diff) describing
 * the change between two successive epochs, or as a fully encoded map.
 */
struct ceph_pg {
	uint64_t pool;
	uint32_t seed;
};

#define CEPH_POOL_FLAG_HASHPSPOOL  1

struct ceph_pg_pool_info {
	struct rb_node node;
	s64 id;
	u8 type;
	u8 size;
	u8 crush_ruleset;
	u8 object_hash;
	u32 pg_num, pgp_num;
	int pg_num_mask, pgp_num_mask;
	u64 flags;
	char *name;
};

struct ceph_object_locator {
	uint64_t pool;
	char *key;
};

struct ceph_pg_mapping {
	struct rb_node node;
	struct ceph_pg pgid;
	int len;
	int osds[];
};

struct ceph_osdmap {
	struct ceph_fsid fsid;
	u32 epoch;
	u32 mkfs_epoch;
	struct ceph_timespec created, modified;

	u32 flags;         /* CEPH_OSDMAP_* */

	u32 max_osd;       /* size of osd_state, _offload, _addr arrays */
	u8 *osd_state;     /* CEPH_OSD_* */
	u32 *osd_weight;   /* 0 = failed, 0x10000 = 100% normal */
	struct ceph_entity_addr *osd_addr;

	struct rb_root pg_temp;
	struct rb_root pg_pools;
	u32 pool_max;

	/* the CRUSH map specifies the mapping of placement groups to
	 * the list of osds that store+replicate them. */
	struct crush_map *crush;
};

/*
 * file layout helpers
 */
#define ceph_file_layout_su(l) ((__s32)le32_to_cpu((l).fl_stripe_unit))
#define ceph_file_layout_stripe_count(l) \
	((__s32)le32_to_cpu((l).fl_stripe_count))
#define ceph_file_layout_object_size(l) ((__s32)le32_to_cpu((l).fl_object_size))
#define ceph_file_layout_cas_hash(l) ((__s32)le32_to_cpu((l).fl_cas_hash))
#define ceph_file_layout_object_su(l) \
	((__s32)le32_to_cpu((l).fl_object_stripe_unit))
#define ceph_file_layout_pg_pool(l) \
	((__s32)le32_to_cpu((l).fl_pg_pool))

static inline unsigned ceph_file_layout_stripe_width(struct ceph_file_layout *l)
{
	return le32_to_cpu(l->fl_stripe_unit) *
		le32_to_cpu(l->fl_stripe_count);
}

/* "period" == bytes before i start on a new set of objects */
static inline unsigned ceph_file_layout_period(struct ceph_file_layout *l)
{
	return le32_to_cpu(l->fl_object_size) *
		le32_to_cpu(l->fl_stripe_count);
}


static inline int ceph_osd_is_up(struct ceph_osdmap *map, int osd)
{
	return (osd < map->max_osd) && (map->osd_state[osd] & CEPH_OSD_UP);
}

static inline bool ceph_osdmap_flag(struct ceph_osdmap *map, int flag)
{
	return map && (map->flags & flag);
}

extern char *ceph_osdmap_state_str(char *str, int len, int state);

static inline struct ceph_entity_addr *ceph_osd_addr(struct ceph_osdmap *map,
						     int osd)
{
	if (osd >= map->max_osd)
		return NULL;
	return &map->osd_addr[osd];
}

extern struct ceph_osdmap *osdmap_decode(void **p, void *end);
extern struct ceph_osdmap *osdmap_apply_incremental(void **p, void *end,
					    struct ceph_osdmap *map,
					    struct ceph_messenger *msgr);
extern void ceph_osdmap_destroy(struct ceph_osdmap *map);

/* calculate mapping of a file extent to an object */
extern int ceph_calc_file_object_mapping(struct ceph_file_layout *layout,
					 u64 off, u64 len,
					 u64 *bno, u64 *oxoff, u64 *oxlen);

/* calculate mapping of object to a placement group */
extern int ceph_calc_object_layout(struct ceph_pg *pg,
				   const char *oid,
				   struct ceph_file_layout *fl,
				   struct ceph_osdmap *osdmap);
extern int ceph_calc_pg_acting(struct ceph_osdmap *osdmap,
			       struct ceph_pg pgid,
			       int *acting);
extern int ceph_calc_pg_primary(struct ceph_osdmap *osdmap,
				struct ceph_pg pgid);

extern const char *ceph_pg_pool_name_by_id(struct ceph_osdmap *map, u64 id);
extern int ceph_pg_poolid_by_name(struct ceph_osdmap *map, const char *name);

#endif
