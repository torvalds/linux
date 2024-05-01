// SPDX-License-Identifier: GPL-2.0

/*
 * Superblock section that contains a list of recovery passes to run when
 * downgrading past a given version
 */

#include "bcachefs.h"
#include "darray.h"
#include "recovery_passes.h"
#include "sb-downgrade.h"
#include "sb-errors.h"
#include "super-io.h"

#define RECOVERY_PASS_ALL_FSCK		BIT_ULL(63)

/*
 * Upgrade, downgrade tables - run certain recovery passes, fix certain errors
 *
 * x(version, recovery_passes, errors...)
 */
#define UPGRADE_TABLE()						\
	x(backpointers,						\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(inode_v3,						\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(unwritten_extents,					\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(bucket_gens,						\
	  BIT_ULL(BCH_RECOVERY_PASS_bucket_gens_init)|		\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(lru_v2,						\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(fragmentation_lru,					\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(no_bps_in_alloc_keys,					\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(snapshot_trees,					\
	  RECOVERY_PASS_ALL_FSCK)				\
	x(snapshot_skiplists,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_snapshots),		\
	  BCH_FSCK_ERR_snapshot_bad_depth,			\
	  BCH_FSCK_ERR_snapshot_bad_skiplist)			\
	x(deleted_inodes,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_inodes),		\
	  BCH_FSCK_ERR_unlinked_inode_not_on_deleted_list)	\
	x(rebalance_work,					\
	  BIT_ULL(BCH_RECOVERY_PASS_set_fs_needs_rebalance))	\
	x(subvolume_fs_parent,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_dirents),		\
	  BCH_FSCK_ERR_subvol_fs_path_parent_wrong)		\
	x(btree_subvolume_children,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_subvols),		\
	  BCH_FSCK_ERR_subvol_children_not_set)

#define DOWNGRADE_TABLE()

struct upgrade_downgrade_entry {
	u64		recovery_passes;
	u16		version;
	u16		nr_errors;
	const u16	*errors;
};

#define x(ver, passes, ...) static const u16 upgrade_##ver##_errors[] = { __VA_ARGS__ };
UPGRADE_TABLE()
#undef x

static const struct upgrade_downgrade_entry upgrade_table[] = {
#define x(ver, passes, ...) {					\
	.recovery_passes	= passes,			\
	.version		= bcachefs_metadata_version_##ver,\
	.nr_errors		= ARRAY_SIZE(upgrade_##ver##_errors),	\
	.errors			= upgrade_##ver##_errors,	\
},
UPGRADE_TABLE()
#undef x
};

void bch2_sb_set_upgrade(struct bch_fs *c,
			 unsigned old_version,
			 unsigned new_version)
{
	lockdep_assert_held(&c->sb_lock);

	struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);

	for (const struct upgrade_downgrade_entry *i = upgrade_table;
	     i < upgrade_table + ARRAY_SIZE(upgrade_table);
	     i++)
		if (i->version > old_version && i->version <= new_version) {
			u64 passes = i->recovery_passes;

			if (passes & RECOVERY_PASS_ALL_FSCK)
				passes |= bch2_fsck_recovery_passes();
			passes &= ~RECOVERY_PASS_ALL_FSCK;

			ext->recovery_passes_required[0] |=
				cpu_to_le64(bch2_recovery_passes_to_stable(passes));

			for (const u16 *e = i->errors;
			     e < i->errors + i->nr_errors;
			     e++) {
				__set_bit(*e, c->sb.errors_silent);
				ext->errors_silent[*e / 64] |= cpu_to_le64(BIT_ULL(*e % 64));
			}
		}
}

#define x(ver, passes, ...) static const u16 downgrade_ver_##errors[] = { __VA_ARGS__ };
DOWNGRADE_TABLE()
#undef x

static const struct upgrade_downgrade_entry downgrade_table[] = {
#define x(ver, passes, ...) {					\
	.recovery_passes	= passes,			\
	.version		= bcachefs_metadata_version_##ver,\
	.nr_errors		= ARRAY_SIZE(downgrade_##ver##_errors),	\
	.errors			= downgrade_##ver##_errors,	\
},
DOWNGRADE_TABLE()
#undef x
};

static inline const struct bch_sb_field_downgrade_entry *
downgrade_entry_next_c(const struct bch_sb_field_downgrade_entry *e)
{
	return (void *) &e->errors[le16_to_cpu(e->nr_errors)];
}

#define for_each_downgrade_entry(_d, _i)						\
	for (const struct bch_sb_field_downgrade_entry *_i = (_d)->entries;		\
	     (void *) _i	< vstruct_end(&(_d)->field) &&				\
	     (void *) &_i->errors[0] < vstruct_end(&(_d)->field);			\
	     _i = downgrade_entry_next_c(_i))

static int bch2_sb_downgrade_validate(struct bch_sb *sb, struct bch_sb_field *f,
				      struct printbuf *err)
{
	struct bch_sb_field_downgrade *e = field_to_type(f, downgrade);

	for_each_downgrade_entry(e, i) {
		if (BCH_VERSION_MAJOR(le16_to_cpu(i->version)) !=
		    BCH_VERSION_MAJOR(le16_to_cpu(sb->version))) {
			prt_printf(err, "downgrade entry with mismatched major version (%u != %u)",
				   BCH_VERSION_MAJOR(le16_to_cpu(i->version)),
				   BCH_VERSION_MAJOR(le16_to_cpu(sb->version)));
			return -BCH_ERR_invalid_sb_downgrade;
		}
	}

	return 0;
}

static void bch2_sb_downgrade_to_text(struct printbuf *out, struct bch_sb *sb,
				      struct bch_sb_field *f)
{
	struct bch_sb_field_downgrade *e = field_to_type(f, downgrade);

	if (out->nr_tabstops <= 1)
		printbuf_tabstop_push(out, 16);

	for_each_downgrade_entry(e, i) {
		prt_str(out, "version:");
		prt_tab(out);
		bch2_version_to_text(out, le16_to_cpu(i->version));
		prt_newline(out);

		prt_str(out, "recovery passes:");
		prt_tab(out);
		prt_bitflags(out, bch2_recovery_passes,
			     bch2_recovery_passes_from_stable(le64_to_cpu(i->recovery_passes[0])));
		prt_newline(out);

		prt_str(out, "errors:");
		prt_tab(out);
		bool first = true;
		for (unsigned j = 0; j < le16_to_cpu(i->nr_errors); j++) {
			if (!first)
				prt_char(out, ',');
			first = false;
			unsigned e = le16_to_cpu(i->errors[j]);
			prt_str(out, e < BCH_SB_ERR_MAX ? bch2_sb_error_strs[e] : "(unknown)");
		}
		prt_newline(out);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_downgrade = {
	.validate	= bch2_sb_downgrade_validate,
	.to_text	= bch2_sb_downgrade_to_text,
};

int bch2_sb_downgrade_update(struct bch_fs *c)
{
	darray_char table = {};
	int ret = 0;

	for (const struct upgrade_downgrade_entry *src = downgrade_table;
	     src < downgrade_table + ARRAY_SIZE(downgrade_table);
	     src++) {
		if (BCH_VERSION_MAJOR(src->version) != BCH_VERSION_MAJOR(le16_to_cpu(c->disk_sb.sb->version)))
			continue;

		struct bch_sb_field_downgrade_entry *dst;
		unsigned bytes = sizeof(*dst) + sizeof(dst->errors[0]) * src->nr_errors;

		ret = darray_make_room(&table, bytes);
		if (ret)
			goto out;

		dst = (void *) &darray_top(table);
		dst->version = cpu_to_le16(src->version);
		dst->recovery_passes[0]	= cpu_to_le64(src->recovery_passes);
		dst->recovery_passes[1]	= 0;
		dst->nr_errors		= cpu_to_le16(src->nr_errors);
		for (unsigned i = 0; i < src->nr_errors; i++)
			dst->errors[i] = cpu_to_le16(src->errors[i]);

		table.nr += bytes;
	}

	struct bch_sb_field_downgrade *d = bch2_sb_field_get(c->disk_sb.sb, downgrade);

	unsigned sb_u64s = DIV_ROUND_UP(sizeof(*d) + table.nr, sizeof(u64));

	if (d && le32_to_cpu(d->field.u64s) > sb_u64s)
		goto out;

	d = bch2_sb_field_resize(&c->disk_sb, downgrade, sb_u64s);
	if (!d) {
		ret = -BCH_ERR_ENOSPC_sb_downgrade;
		goto out;
	}

	memcpy(d->entries, table.data, table.nr);
	memset_u64s_tail(d->entries, 0, table.nr);
out:
	darray_exit(&table);
	return ret;
}

void bch2_sb_set_downgrade(struct bch_fs *c, unsigned new_minor, unsigned old_minor)
{
	struct bch_sb_field_downgrade *d = bch2_sb_field_get(c->disk_sb.sb, downgrade);
	if (!d)
		return;

	struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);

	for_each_downgrade_entry(d, i) {
		unsigned minor = BCH_VERSION_MINOR(le16_to_cpu(i->version));
		if (new_minor < minor && minor <= old_minor) {
			ext->recovery_passes_required[0] |= i->recovery_passes[0];
			ext->recovery_passes_required[1] |= i->recovery_passes[1];

			for (unsigned j = 0; j < le16_to_cpu(i->nr_errors); j++) {
				unsigned e = le16_to_cpu(i->errors[j]);
				if (e < BCH_SB_ERR_MAX)
					__set_bit(e, c->sb.errors_silent);
				if (e < sizeof(ext->errors_silent) * 8)
					__set_bit_le64(e, ext->errors_silent);
			}
		}
	}
}
