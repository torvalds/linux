/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUPER_H
#define _BCACHEFS_SUPER_H

#include "extents.h"

#include "bcachefs_ioctl.h"

#include <linux/math64.h>

static inline size_t sector_to_bucket(const struct bch_dev *ca, sector_t s)
{
	return div_u64(s, ca->mi.bucket_size);
}

static inline sector_t bucket_to_sector(const struct bch_dev *ca, size_t b)
{
	return ((sector_t) b) * ca->mi.bucket_size;
}

static inline sector_t bucket_remainder(const struct bch_dev *ca, sector_t s)
{
	u32 remainder;

	div_u64_rem(s, ca->mi.bucket_size, &remainder);
	return remainder;
}

static inline size_t sector_to_bucket_and_offset(const struct bch_dev *ca, sector_t s,
						 u32 *offset)
{
	return div_u64_rem(s, ca->mi.bucket_size, offset);
}

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
	unsigned i;

	for (i = 0; i < devs.nr; i++)
		if (devs.devs[i] == dev)
			return true;

	return false;
}

static inline void bch2_dev_list_drop_dev(struct bch_devs_list *devs,
					  unsigned dev)
{
	unsigned i;

	for (i = 0; i < devs->nr; i++)
		if (devs->devs[i] == dev) {
			array_remove_item(devs->devs, devs->nr, i);
			return;
		}
}

static inline void bch2_dev_list_add_dev(struct bch_devs_list *devs,
					 unsigned dev)
{
	BUG_ON(bch2_dev_list_has_dev(*devs, dev));
	BUG_ON(devs->nr >= ARRAY_SIZE(devs->devs));
	devs->devs[devs->nr++] = dev;
}

static inline struct bch_devs_list bch2_dev_list_single(unsigned dev)
{
	return (struct bch_devs_list) { .nr = 1, .devs[0] = dev };
}

static inline struct bch_dev *__bch2_next_dev(struct bch_fs *c, unsigned *iter,
					      const struct bch_devs_mask *mask)
{
	struct bch_dev *ca = NULL;

	while ((*iter = mask
		? find_next_bit(mask->d, c->sb.nr_devices, *iter)
		: *iter) < c->sb.nr_devices &&
	       !(ca = rcu_dereference_check(c->devs[*iter],
					    lockdep_is_held(&c->state_lock))))
		(*iter)++;

	return ca;
}

#define for_each_member_device_rcu(ca, c, iter, mask)			\
	for ((iter) = 0; ((ca) = __bch2_next_dev((c), &(iter), mask)); (iter)++)

static inline struct bch_dev *bch2_get_next_dev(struct bch_fs *c, unsigned *iter)
{
	struct bch_dev *ca;

	rcu_read_lock();
	if ((ca = __bch2_next_dev(c, iter, NULL)))
		percpu_ref_get(&ca->ref);
	rcu_read_unlock();

	return ca;
}

/*
 * If you break early, you must drop your ref on the current device
 */
#define for_each_member_device(ca, c, iter)				\
	for ((iter) = 0;						\
	     (ca = bch2_get_next_dev(c, &(iter)));			\
	     percpu_ref_put(&ca->ref), (iter)++)

static inline struct bch_dev *bch2_get_next_online_dev(struct bch_fs *c,
						      unsigned *iter,
						      int state_mask)
{
	struct bch_dev *ca;

	rcu_read_lock();
	while ((ca = __bch2_next_dev(c, iter, NULL)) &&
	       (!((1 << ca->mi.state) & state_mask) ||
		!percpu_ref_tryget(&ca->io_ref)))
		(*iter)++;
	rcu_read_unlock();

	return ca;
}

#define __for_each_online_member(ca, c, iter, state_mask)		\
	for ((iter) = 0;						\
	     (ca = bch2_get_next_online_dev(c, &(iter), state_mask));	\
	     percpu_ref_put(&ca->io_ref), (iter)++)

#define for_each_online_member(ca, c, iter)				\
	__for_each_online_member(ca, c, iter, ~0)

#define for_each_rw_member(ca, c, iter)					\
	__for_each_online_member(ca, c, iter, 1 << BCH_MEMBER_STATE_rw)

#define for_each_readable_member(ca, c, iter)				\
	__for_each_online_member(ca, c, iter,				\
		(1 << BCH_MEMBER_STATE_rw)|(1 << BCH_MEMBER_STATE_ro))

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
	struct bch_dev *ca;
	unsigned i;

	memset(&devs, 0, sizeof(devs));
	for_each_online_member(ca, c, i)
		__set_bit(ca->dev_idx, devs.d);
	return devs;
}

static inline bool is_superblock_bucket(struct bch_dev *ca, u64 b)
{
	struct bch_sb_layout *layout = &ca->disk_sb.sb->layout;
	u64 b_offset	= bucket_to_sector(ca, b);
	u64 b_end	= bucket_to_sector(ca, b + 1);
	unsigned i;

	if (!b)
		return true;

	for (i = 0; i < layout->nr_superblocks; i++) {
		u64 offset = le64_to_cpu(layout->sb_offset[i]);
		u64 end = offset + (1 << layout->sb_max_size_bits);

		if (!(offset >= b_end || end <= b_offset))
			return true;
	}

	return false;
}

struct bch_fs *bch2_dev_to_fs(dev_t);
struct bch_fs *bch2_uuid_to_fs(__uuid_t);

bool bch2_dev_state_allowed(struct bch_fs *, struct bch_dev *,
			   enum bch_member_state, int);
int __bch2_dev_set_state(struct bch_fs *, struct bch_dev *,
			enum bch_member_state, int);
int bch2_dev_set_state(struct bch_fs *, struct bch_dev *,
		      enum bch_member_state, int);

int bch2_dev_fail(struct bch_dev *, int);
int bch2_dev_remove(struct bch_fs *, struct bch_dev *, int);
int bch2_dev_add(struct bch_fs *, const char *);
int bch2_dev_online(struct bch_fs *, const char *);
int bch2_dev_offline(struct bch_fs *, struct bch_dev *, int);
int bch2_dev_resize(struct bch_fs *, struct bch_dev *, u64);
struct bch_dev *bch2_dev_lookup(struct bch_fs *, const char *);

bool bch2_fs_emergency_read_only(struct bch_fs *);
void bch2_fs_read_only(struct bch_fs *);

int bch2_fs_read_write(struct bch_fs *);
int bch2_fs_read_write_early(struct bch_fs *);

/*
 * Only for use in the recovery/fsck path:
 */
static inline void bch2_fs_lazy_rw(struct bch_fs *c)
{
	if (!test_bit(BCH_FS_RW, &c->flags) &&
	    !test_bit(BCH_FS_WAS_RW, &c->flags))
		bch2_fs_read_write_early(c);
}

void __bch2_fs_stop(struct bch_fs *);
void bch2_fs_free(struct bch_fs *);
void bch2_fs_stop(struct bch_fs *);

int bch2_fs_start(struct bch_fs *);
struct bch_fs *bch2_fs_open(char * const *, unsigned, struct bch_opts);

#endif /* _BCACHEFS_SUPER_H */
