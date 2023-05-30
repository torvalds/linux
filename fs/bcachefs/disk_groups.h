/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DISK_GROUPS_H
#define _BCACHEFS_DISK_GROUPS_H

extern const struct bch_sb_field_ops bch_sb_field_ops_disk_groups;

static inline unsigned disk_groups_nr(struct bch_sb_field_disk_groups *groups)
{
	return groups
		? (vstruct_end(&groups->field) -
		   (void *) &groups->entries[0]) / sizeof(struct bch_disk_group)
		: 0;
}

struct target {
	enum {
		TARGET_NULL,
		TARGET_DEV,
		TARGET_GROUP,
	}			type;
	union {
		unsigned	dev;
		unsigned	group;
	};
};

#define TARGET_DEV_START	1
#define TARGET_GROUP_START	(256 + TARGET_DEV_START)

static inline u16 dev_to_target(unsigned dev)
{
	return TARGET_DEV_START + dev;
}

static inline u16 group_to_target(unsigned group)
{
	return TARGET_GROUP_START + group;
}

static inline struct target target_decode(unsigned target)
{
	if (target >= TARGET_GROUP_START)
		return (struct target) {
			.type	= TARGET_GROUP,
			.group	= target - TARGET_GROUP_START
		};

	if (target >= TARGET_DEV_START)
		return (struct target) {
			.type	= TARGET_DEV,
			.group	= target - TARGET_DEV_START
		};

	return (struct target) { .type = TARGET_NULL };
}

const struct bch_devs_mask *bch2_target_to_mask(struct bch_fs *, unsigned);

static inline struct bch_devs_mask target_rw_devs(struct bch_fs *c,
						  enum bch_data_type data_type,
						  u16 target)
{
	struct bch_devs_mask devs = c->rw_devs[data_type];
	const struct bch_devs_mask *t = bch2_target_to_mask(c, target);

	if (t)
		bitmap_and(devs.d, devs.d, t->d, BCH_SB_MEMBERS_MAX);
	return devs;
}

static inline bool bch2_target_accepts_data(struct bch_fs *c,
					    enum bch_data_type data_type,
					    u16 target)
{
	struct bch_devs_mask rw_devs = target_rw_devs(c, data_type, target);
	return !bitmap_empty(rw_devs.d, BCH_SB_MEMBERS_MAX);
}

bool bch2_dev_in_target(struct bch_fs *, unsigned, unsigned);

int bch2_disk_path_find(struct bch_sb_handle *, const char *);

/* Exported for userspace bcachefs-tools: */
int bch2_disk_path_find_or_create(struct bch_sb_handle *, const char *);

void bch2_disk_path_to_text(struct printbuf *, struct bch_sb *, unsigned);

int bch2_opt_target_parse(struct bch_fs *, const char *, u64 *);
void bch2_opt_target_to_text(struct printbuf *, struct bch_fs *, struct bch_sb *, u64);

int bch2_sb_disk_groups_to_cpu(struct bch_fs *);

int __bch2_dev_group_set(struct bch_fs *, struct bch_dev *, const char *);
int bch2_dev_group_set(struct bch_fs *, struct bch_dev *, const char *);

const char *bch2_sb_validate_disk_groups(struct bch_sb *,
					 struct bch_sb_field *);

void bch2_disk_groups_to_text(struct printbuf *, struct bch_fs *);

#endif /* _BCACHEFS_DISK_GROUPS_H */
