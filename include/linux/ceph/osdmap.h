#ifndef _FS_CEPH_OSDMAP_H
#define _FS_CEPH_OSDMAP_H

#include <linux/rbtree.h>
#include <linux/ceph/types.h>
#include <linux/ceph/decode.h>
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
	s64 read_tier;
	s64 write_tier; /* wins for read+write ops */
	u64 flags;
	char *name;
};

static inline bool ceph_can_shift_osds(struct ceph_pg_pool_info *pool)
{
	switch (pool->type) {
	case CEPH_POOL_TYPE_REP:
		return true;
	case CEPH_POOL_TYPE_EC:
		return false;
	default:
		BUG_ON(1);
	}
}

struct ceph_object_locator {
	s64 pool;
};

/*
 * Maximum supported by kernel client object name length
 *
 * (probably outdated: must be >= RBD_MAX_MD_NAME_LEN -- currently 100)
 */
#define CEPH_MAX_OID_NAME_LEN 100

struct ceph_object_id {
	char name[CEPH_MAX_OID_NAME_LEN];
	int name_len;
};

struct ceph_pg_mapping {
	struct rb_node node;
	struct ceph_pg pgid;

	union {
		struct {
			int len;
			int osds[];
		} pg_temp;
		struct {
			int osd;
		} primary_temp;
	};
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
	struct rb_root primary_temp;

	u32 *osd_primary_affinity;

	struct rb_root pg_pools;
	u32 pool_max;

	/* the CRUSH map specifies the mapping of placement groups to
	 * the list of osds that store+replicate them. */
	struct crush_map *crush;

	struct mutex crush_scratch_mutex;
	int crush_scratch_ary[CEPH_PG_MAX_SIZE * 3];
};

static inline void ceph_oid_set_name(struct ceph_object_id *oid,
				     const char *name)
{
	int len;

	len = strlen(name);
	if (len > sizeof(oid->name)) {
		WARN(1, "ceph_oid_set_name '%s' len %d vs %zu, truncating\n",
		     name, len, sizeof(oid->name));
		len = sizeof(oid->name);
	}

	memcpy(oid->name, name, len);
	oid->name_len = len;
}

static inline void ceph_oid_copy(struct ceph_object_id *dest,
				 struct ceph_object_id *src)
{
	BUG_ON(src->name_len > sizeof(dest->name));
	memcpy(dest->name, src->name, src->name_len);
	dest->name_len = src->name_len;
}

static inline int ceph_osd_exists(struct ceph_osdmap *map, int osd)
{
	return osd >= 0 && osd < map->max_osd &&
	       (map->osd_state[osd] & CEPH_OSD_EXISTS);
}

static inline int ceph_osd_is_up(struct ceph_osdmap *map, int osd)
{
	return ceph_osd_exists(map, osd) &&
	       (map->osd_state[osd] & CEPH_OSD_UP);
}

static inline int ceph_osd_is_down(struct ceph_osdmap *map, int osd)
{
	return !ceph_osd_is_up(map, osd);
}

static inline bool ceph_osdmap_flag(struct ceph_osdmap *map, int flag)
{
	return map && (map->flags & flag);
}

extern char *ceph_osdmap_state_str(char *str, int len, int state);
extern u32 ceph_get_primary_affinity(struct ceph_osdmap *map, int osd);

static inline struct ceph_entity_addr *ceph_osd_addr(struct ceph_osdmap *map,
						     int osd)
{
	if (osd >= map->max_osd)
		return NULL;
	return &map->osd_addr[osd];
}

static inline int ceph_decode_pgid(void **p, void *end, struct ceph_pg *pgid)
{
	__u8 version;

	if (!ceph_has_room(p, end, 1 + 8 + 4 + 4)) {
		pr_warning("incomplete pg encoding");

		return -EINVAL;
	}
	version = ceph_decode_8(p);
	if (version > 1) {
		pr_warning("do not understand pg encoding %d > 1",
			(int)version);
		return -EINVAL;
	}

	pgid->pool = ceph_decode_64(p);
	pgid->seed = ceph_decode_32(p);
	*p += 4;	/* skip deprecated preferred value */

	return 0;
}

extern struct ceph_osdmap *ceph_osdmap_decode(void **p, void *end);
extern struct ceph_osdmap *osdmap_apply_incremental(void **p, void *end,
					    struct ceph_osdmap *map,
					    struct ceph_messenger *msgr);
extern void ceph_osdmap_destroy(struct ceph_osdmap *map);

/* calculate mapping of a file extent to an object */
extern int ceph_calc_file_object_mapping(struct ceph_file_layout *layout,
					 u64 off, u64 len,
					 u64 *bno, u64 *oxoff, u64 *oxlen);

/* calculate mapping of object to a placement group */
extern int ceph_oloc_oid_to_pg(struct ceph_osdmap *osdmap,
			       struct ceph_object_locator *oloc,
			       struct ceph_object_id *oid,
			       struct ceph_pg *pg_out);

extern int ceph_calc_pg_acting(struct ceph_osdmap *osdmap,
			       struct ceph_pg pgid,
			       int *osds, int *primary);
extern int ceph_calc_pg_primary(struct ceph_osdmap *osdmap,
				struct ceph_pg pgid);

extern struct ceph_pg_pool_info *ceph_pg_pool_by_id(struct ceph_osdmap *map,
						    u64 id);

extern const char *ceph_pg_pool_name_by_id(struct ceph_osdmap *map, u64 id);
extern int ceph_pg_poolid_by_name(struct ceph_osdmap *map, const char *name);

#endif
