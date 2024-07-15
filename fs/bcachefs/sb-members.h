/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_MEMBERS_H
#define _BCACHEFS_SB_MEMBERS_H

#include "darray.h"
#include "bkey_types.h"

extern char * const bch2_member_error_strs[];

static inline struct bch_member *
__bch2_members_v2_get_mut(struct bch_sb_field_members_v2 *mi, unsigned i)
{
	return (void *) mi->_members + (i * le16_to_cpu(mi->member_bytes));
}

int bch2_sb_members_v2_init(struct bch_fs *c);
int bch2_sb_members_cpy_v2_v1(struct bch_sb_handle *disk_sb);
struct bch_member *bch2_members_v2_get_mut(struct bch_sb *sb, int i);
struct bch_member bch2_sb_member_get(struct bch_sb *sb, int i);

static inline bool bch2_dev_is_online(struct bch_dev *ca)
{
	return !percpu_ref_is_zero(&ca->io_ref);
}

static inline bool bch2_dev_is_readable(struct bch_dev *ca)
{
	return bch2_dev_is_online(ca) &&
		ca->mi.state != BCH_MEMBER_STATE_failed;
}

static inline bool bch2_dev_get_ioref(struct bch_dev *ca, int rw)
{
	if (!percpu_ref_tryget(&ca->io_ref))
		return false;

	if (ca->mi.state == BCH_MEMBER_STATE_rw ||
	    (ca->mi.state == BCH_MEMBER_STATE_ro && rw == READ))
		return true;

	percpu_ref_put(&ca->io_ref);
	return false;
}

static inline unsigned dev_mask_nr(const struct bch_devs_mask *devs)
{
	return bitmap_weight(devs->d, BCH_SB_MEMBERS_MAX);
}

static inline bool bch2_dev_list_has_dev(struct bch_devs_list devs,
					 unsigned dev)
{
	darray_for_each(devs, i)
		if (*i == dev)
			return true;
	return false;
}

static inline void bch2_dev_list_drop_dev(struct bch_devs_list *devs,
					  unsigned dev)
{
	darray_for_each(*devs, i)
		if (*i == dev) {
			darray_remove_item(devs, i);
			return;
		}
}

static inline void bch2_dev_list_add_dev(struct bch_devs_list *devs,
					 unsigned dev)
{
	if (!bch2_dev_list_has_dev(*devs, dev)) {
		BUG_ON(devs->nr >= ARRAY_SIZE(devs->data));
		devs->data[devs->nr++] = dev;
	}
}

static inline struct bch_devs_list bch2_dev_list_single(unsigned dev)
{
	return (struct bch_devs_list) { .nr = 1, .data[0] = dev };
}

static inline struct bch_dev *__bch2_next_dev_idx(struct bch_fs *c, unsigned idx,
						  const struct bch_devs_mask *mask)
{
	struct bch_dev *ca = NULL;

	while ((idx = mask
		? find_next_bit(mask->d, c->sb.nr_devices, idx)
		: idx) < c->sb.nr_devices &&
	       !(ca = rcu_dereference_check(c->devs[idx],
					    lockdep_is_held(&c->state_lock))))
		idx++;

	return ca;
}

static inline struct bch_dev *__bch2_next_dev(struct bch_fs *c, struct bch_dev *ca,
					      const struct bch_devs_mask *mask)
{
	return __bch2_next_dev_idx(c, ca ? ca->dev_idx + 1 : 0, mask);
}

#define for_each_member_device_rcu(_c, _ca, _mask)			\
	for (struct bch_dev *_ca = NULL;				\
	     (_ca = __bch2_next_dev((_c), _ca, (_mask)));)

static inline struct bch_dev *bch2_get_next_dev(struct bch_fs *c, struct bch_dev *ca)
{
	rcu_read_lock();
	if (ca)
		percpu_ref_put(&ca->ref);

	if ((ca = __bch2_next_dev(c, ca, NULL)))
		percpu_ref_get(&ca->ref);
	rcu_read_unlock();

	return ca;
}

/*
 * If you break early, you must drop your ref on the current device
 */
#define __for_each_member_device(_c, _ca)				\
	for (;	(_ca = bch2_get_next_dev(_c, _ca));)

#define for_each_member_device(_c, _ca)					\
	for (struct bch_dev *_ca = NULL;				\
	     (_ca = bch2_get_next_dev(_c, _ca));)

static inline struct bch_dev *bch2_get_next_online_dev(struct bch_fs *c,
						       struct bch_dev *ca,
						       unsigned state_mask)
{
	rcu_read_lock();
	if (ca)
		percpu_ref_put(&ca->io_ref);

	while ((ca = __bch2_next_dev(c, ca, NULL)) &&
	       (!((1 << ca->mi.state) & state_mask) ||
		!percpu_ref_tryget(&ca->io_ref)))
		;
	rcu_read_unlock();

	return ca;
}

#define __for_each_online_member(_c, _ca, state_mask)			\
	for (struct bch_dev *_ca = NULL;				\
	     (_ca = bch2_get_next_online_dev(_c, _ca, state_mask));)

#define for_each_online_member(c, ca)					\
	__for_each_online_member(c, ca, ~0)

#define for_each_rw_member(c, ca)					\
	__for_each_online_member(c, ca, BIT(BCH_MEMBER_STATE_rw))

#define for_each_readable_member(c, ca)				\
	__for_each_online_member(c, ca,	BIT( BCH_MEMBER_STATE_rw)|BIT(BCH_MEMBER_STATE_ro))

/*
 * If a key exists that references a device, the device won't be going away and
 * we can omit rcu_read_lock():
 */
static inline struct bch_dev *bch_dev_bkey_exists(const struct bch_fs *c, unsigned idx)
{
	EBUG_ON(idx >= c->sb.nr_devices || !c->devs[idx]);

	return rcu_dereference_check(c->devs[idx], 1);
}

static inline struct bch_dev *bch_dev_locked(struct bch_fs *c, unsigned idx)
{
	EBUG_ON(idx >= c->sb.nr_devices || !c->devs[idx]);

	return rcu_dereference_protected(c->devs[idx],
					 lockdep_is_held(&c->sb_lock) ||
					 lockdep_is_held(&c->state_lock));
}

/* XXX kill, move to struct bch_fs */
static inline struct bch_devs_mask bch2_online_devs(struct bch_fs *c)
{
	struct bch_devs_mask devs;

	memset(&devs, 0, sizeof(devs));
	for_each_online_member(c, ca)
		__set_bit(ca->dev_idx, devs.d);
	return devs;
}

extern const struct bch_sb_field_ops bch_sb_field_ops_members_v1;
extern const struct bch_sb_field_ops bch_sb_field_ops_members_v2;

static inline bool bch2_member_exists(struct bch_member *m)
{
	return !bch2_is_zero(&m->uuid, sizeof(m->uuid));
}

static inline bool bch2_dev_exists(struct bch_sb *sb, unsigned dev)
{
	if (dev < sb->nr_devices) {
		struct bch_member m = bch2_sb_member_get(sb, dev);
		return bch2_member_exists(&m);
	}
	return false;
}

static inline struct bch_member_cpu bch2_mi_to_cpu(struct bch_member *mi)
{
	return (struct bch_member_cpu) {
		.nbuckets	= le64_to_cpu(mi->nbuckets),
		.first_bucket	= le16_to_cpu(mi->first_bucket),
		.bucket_size	= le16_to_cpu(mi->bucket_size),
		.group		= BCH_MEMBER_GROUP(mi),
		.state		= BCH_MEMBER_STATE(mi),
		.discard	= BCH_MEMBER_DISCARD(mi),
		.data_allowed	= BCH_MEMBER_DATA_ALLOWED(mi),
		.durability	= BCH_MEMBER_DURABILITY(mi)
			? BCH_MEMBER_DURABILITY(mi) - 1
			: 1,
		.freespace_initialized = BCH_MEMBER_FREESPACE_INITIALIZED(mi),
		.valid		= bch2_member_exists(mi),
		.btree_bitmap_shift	= mi->btree_bitmap_shift,
		.btree_allocated_bitmap = le64_to_cpu(mi->btree_allocated_bitmap),
	};
}

void bch2_sb_members_from_cpu(struct bch_fs *);

void bch2_dev_io_errors_to_text(struct printbuf *, struct bch_dev *);
void bch2_dev_errors_reset(struct bch_dev *);

static inline bool bch2_dev_btree_bitmap_marked_sectors(struct bch_dev *ca, u64 start, unsigned sectors)
{
	u64 end = start + sectors;

	if (end > 64ULL << ca->mi.btree_bitmap_shift)
		return false;

	for (unsigned bit = start >> ca->mi.btree_bitmap_shift;
	     (u64) bit << ca->mi.btree_bitmap_shift < end;
	     bit++)
		if (!(ca->mi.btree_allocated_bitmap & BIT_ULL(bit)))
			return false;
	return true;
}

bool bch2_dev_btree_bitmap_marked(struct bch_fs *, struct bkey_s_c);
void bch2_dev_btree_bitmap_mark(struct bch_fs *, struct bkey_s_c);

#endif /* _BCACHEFS_SB_MEMBERS_H */
