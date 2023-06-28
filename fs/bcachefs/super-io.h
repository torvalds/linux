/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUPER_IO_H
#define _BCACHEFS_SUPER_IO_H

#include "extents.h"
#include "eytzinger.h"
#include "super_types.h"
#include "super.h"

#include <asm/byteorder.h>

void bch2_version_to_text(struct printbuf *, unsigned);

struct bch_sb_field *bch2_sb_field_get(struct bch_sb *, enum bch_sb_field_type);
struct bch_sb_field *bch2_sb_field_resize(struct bch_sb_handle *,
					  enum bch_sb_field_type, unsigned);
void bch2_sb_field_delete(struct bch_sb_handle *, enum bch_sb_field_type);

#define field_to_type(_f, _name)					\
	container_of_or_null(_f, struct bch_sb_field_##_name, field)

#define x(_name, _nr)							\
static inline struct bch_sb_field_##_name *				\
bch2_sb_get_##_name(struct bch_sb *sb)					\
{									\
	return field_to_type(bch2_sb_field_get(sb,			\
				BCH_SB_FIELD_##_name), _name);		\
}									\
									\
static inline struct bch_sb_field_##_name *				\
bch2_sb_resize_##_name(struct bch_sb_handle *sb, unsigned u64s)	\
{									\
	return field_to_type(bch2_sb_field_resize(sb,			\
				BCH_SB_FIELD_##_name, u64s), _name);	\
}

BCH_SB_FIELDS()
#undef x

extern const char * const bch2_sb_fields[];

struct bch_sb_field_ops {
	int	(*validate)(struct bch_sb *, struct bch_sb_field *, struct printbuf *);
	void	(*to_text)(struct printbuf *, struct bch_sb *, struct bch_sb_field *);
};

static inline __le64 bch2_sb_magic(struct bch_fs *c)
{
	__le64 ret;
	memcpy(&ret, &c->sb.uuid, sizeof(ret));
	return ret;
}

static inline __u64 jset_magic(struct bch_fs *c)
{
	return __le64_to_cpu(bch2_sb_magic(c) ^ JSET_MAGIC);
}

static inline __u64 bset_magic(struct bch_fs *c)
{
	return __le64_to_cpu(bch2_sb_magic(c) ^ BSET_MAGIC);
}

int bch2_sb_to_fs(struct bch_fs *, struct bch_sb *);
int bch2_sb_from_fs(struct bch_fs *, struct bch_dev *);

void bch2_free_super(struct bch_sb_handle *);
int bch2_sb_realloc(struct bch_sb_handle *, unsigned);

int bch2_read_super(const char *, struct bch_opts *, struct bch_sb_handle *);
int bch2_write_super(struct bch_fs *);
void __bch2_check_set_feature(struct bch_fs *, unsigned);

static inline void bch2_check_set_feature(struct bch_fs *c, unsigned feat)
{
	if (!(c->sb.features & (1ULL << feat)))
		__bch2_check_set_feature(c, feat);
}

/* BCH_SB_FIELD_members: */

static inline bool bch2_member_exists(struct bch_member *m)
{
	return !bch2_is_zero(&m->uuid, sizeof(m->uuid));
}

static inline bool bch2_dev_exists(struct bch_sb *sb,
				   struct bch_sb_field_members *mi,
				   unsigned dev)
{
	return dev < sb->nr_devices &&
		bch2_member_exists(&mi->members[dev]);
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

/* BCH_SB_FIELD_clean: */

void bch2_journal_super_entries_add_common(struct bch_fs *,
					   struct jset_entry **, u64);

int bch2_sb_clean_validate_late(struct bch_fs *, struct bch_sb_field_clean *, int);

int bch2_fs_mark_dirty(struct bch_fs *);
void bch2_fs_mark_clean(struct bch_fs *);

void bch2_sb_field_to_text(struct printbuf *, struct bch_sb *,
			   struct bch_sb_field *);
void bch2_sb_layout_to_text(struct printbuf *, struct bch_sb_layout *);
void bch2_sb_to_text(struct printbuf *, struct bch_sb *, bool, unsigned);

#endif /* _BCACHEFS_SUPER_IO_H */
