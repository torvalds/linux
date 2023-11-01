/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_MEMBERS_H
#define _BCACHEFS_SB_MEMBERS_H

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
	if (!bch2_dev_list_has_dev(*devs, dev)) {
		BUG_ON(devs->nr >= ARRAY_SIZE(devs->devs));
		devs->devs[devs->nr++] = dev;
	}
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
	};
}

void bch2_sb_members_from_cpu(struct bch_fs *);

void bch2_dev_io_errors_to_text(struct printbuf *, struct bch_dev *);
void bch2_dev_errors_reset(struct bch_dev *);

#endif /* _BCACHEFS_SB_MEMBERS_H */
