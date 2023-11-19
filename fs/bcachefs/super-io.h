/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUPER_IO_H
#define _BCACHEFS_SUPER_IO_H

#include "extents.h"
#include "eytzinger.h"
#include "super_types.h"
#include "super.h"
#include "sb-members.h"

#include <asm/byteorder.h>

static inline bool bch2_version_compatible(u16 version)
{
	return BCH_VERSION_MAJOR(version) <= BCH_VERSION_MAJOR(bcachefs_metadata_version_current) &&
		version >= bcachefs_metadata_version_min;
}

void bch2_version_to_text(struct printbuf *, unsigned);
unsigned bch2_latest_compatible_version(unsigned);

u64 bch2_upgrade_recovery_passes(struct bch_fs *c,
				 unsigned,
				 unsigned);

static inline size_t bch2_sb_field_bytes(struct bch_sb_field *f)
{
	return le32_to_cpu(f->u64s) * sizeof(u64);
}

#define field_to_type(_f, _name)					\
	container_of_or_null(_f, struct bch_sb_field_##_name, field)

struct bch_sb_field *bch2_sb_field_get_id(struct bch_sb *, enum bch_sb_field_type);
#define bch2_sb_field_get(_sb, _name)					\
	field_to_type(bch2_sb_field_get_id(_sb, BCH_SB_FIELD_##_name), _name)

struct bch_sb_field *bch2_sb_field_resize_id(struct bch_sb_handle *,
					     enum bch_sb_field_type, unsigned);
#define bch2_sb_field_resize(_sb, _name, _u64s)				\
	field_to_type(bch2_sb_field_resize_id(_sb, BCH_SB_FIELD_##_name, _u64s), _name)

struct bch_sb_field *bch2_sb_field_get_minsize_id(struct bch_sb_handle *,
					enum bch_sb_field_type, unsigned);
#define bch2_sb_field_get_minsize(_sb, _name, _u64s)				\
	field_to_type(bch2_sb_field_get_minsize_id(_sb, BCH_SB_FIELD_##_name, _u64s), _name)

#define bch2_sb_field_nr_entries(_f)					\
	(_f ? ((bch2_sb_field_bytes(&_f->field) - sizeof(*_f)) /	\
	       sizeof(_f->entries[0]))					\
	    : 0)

void bch2_sb_field_delete(struct bch_sb_handle *, enum bch_sb_field_type);

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
int bch2_read_super_silent(const char *, struct bch_opts *, struct bch_sb_handle *);
int bch2_write_super(struct bch_fs *);
void __bch2_check_set_feature(struct bch_fs *, unsigned);

static inline void bch2_check_set_feature(struct bch_fs *c, unsigned feat)
{
	if (!(c->sb.features & (1ULL << feat)))
		__bch2_check_set_feature(c, feat);
}

bool bch2_check_version_downgrade(struct bch_fs *);
void bch2_sb_upgrade(struct bch_fs *, unsigned);

void bch2_sb_field_to_text(struct printbuf *, struct bch_sb *,
			   struct bch_sb_field *);
void bch2_sb_layout_to_text(struct printbuf *, struct bch_sb_layout *);
void bch2_sb_to_text(struct printbuf *, struct bch_sb *, bool, unsigned);

#endif /* _BCACHEFS_SUPER_IO_H */
