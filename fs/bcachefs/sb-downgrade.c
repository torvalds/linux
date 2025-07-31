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
	x(snapshot_2,						\
	  RECOVERY_PASS_ALL_FSCK,				\
	  BCH_FSCK_ERR_subvol_root_wrong_bi_subvol,		\
	  BCH_FSCK_ERR_subvol_not_master_and_not_snapshot)	\
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
	  BCH_FSCK_ERR_subvol_children_not_set)			\
	x(mi_btree_bitmap,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_btree_bitmap_not_marked)			\
	x(disk_accounting_v2,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_bkey_version_in_future,			\
	  BCH_FSCK_ERR_dev_usage_buckets_wrong,			\
	  BCH_FSCK_ERR_dev_usage_sectors_wrong,			\
	  BCH_FSCK_ERR_dev_usage_fragmented_wrong,		\
	  BCH_FSCK_ERR_accounting_mismatch)			\
	x(disk_accounting_v3,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_bkey_version_in_future,			\
	  BCH_FSCK_ERR_dev_usage_buckets_wrong,			\
	  BCH_FSCK_ERR_dev_usage_sectors_wrong,			\
	  BCH_FSCK_ERR_dev_usage_fragmented_wrong,		\
	  BCH_FSCK_ERR_accounting_mismatch,			\
	  BCH_FSCK_ERR_accounting_key_replicas_nr_devs_0,	\
	  BCH_FSCK_ERR_accounting_key_replicas_nr_required_bad,	\
	  BCH_FSCK_ERR_accounting_key_replicas_devs_unsorted,	\
	  BCH_FSCK_ERR_accounting_key_junk_at_end)		\
	x(disk_accounting_inum,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_accounting_mismatch)			\
	x(rebalance_work_acct_fix,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_accounting_mismatch)			\
	x(inode_has_child_snapshots,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_inodes),		\
	  BCH_FSCK_ERR_inode_has_child_snapshots_wrong)		\
	x(backpointer_bucket_gen,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_extents_to_backpointers),\
	  BCH_FSCK_ERR_backpointer_to_missing_ptr,		\
	  BCH_FSCK_ERR_ptr_to_missing_backpointer)		\
	x(disk_accounting_big_endian,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_accounting_mismatch,			\
	  BCH_FSCK_ERR_accounting_key_replicas_nr_devs_0,	\
	  BCH_FSCK_ERR_accounting_key_junk_at_end)		\
	x(cached_backpointers,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_extents_to_backpointers),\
	  BCH_FSCK_ERR_ptr_to_missing_backpointer)		\
	x(stripe_backpointers,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_extents_to_backpointers),\
	  BCH_FSCK_ERR_ptr_to_missing_backpointer)		\
	x(inode_has_case_insensitive,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_inodes),		\
	  BCH_FSCK_ERR_inode_has_case_insensitive_not_set,	\
	  BCH_FSCK_ERR_inode_parent_has_case_insensitive_not_set)

#define DOWNGRADE_TABLE()					\
	x(bucket_stripe_sectors,				\
	  0)							\
	x(disk_accounting_v2,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_dev_usage_buckets_wrong,			\
	  BCH_FSCK_ERR_dev_usage_sectors_wrong,			\
	  BCH_FSCK_ERR_dev_usage_fragmented_wrong,		\
	  BCH_FSCK_ERR_fs_usage_hidden_wrong,			\
	  BCH_FSCK_ERR_fs_usage_btree_wrong,			\
	  BCH_FSCK_ERR_fs_usage_data_wrong,			\
	  BCH_FSCK_ERR_fs_usage_cached_wrong,			\
	  BCH_FSCK_ERR_fs_usage_reserved_wrong,			\
	  BCH_FSCK_ERR_fs_usage_nr_inodes_wrong,		\
	  BCH_FSCK_ERR_fs_usage_persistent_reserved_wrong,	\
	  BCH_FSCK_ERR_fs_usage_replicas_wrong,			\
	  BCH_FSCK_ERR_bkey_version_in_future)			\
	x(disk_accounting_v3,					\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_dev_usage_buckets_wrong,			\
	  BCH_FSCK_ERR_dev_usage_sectors_wrong,			\
	  BCH_FSCK_ERR_dev_usage_fragmented_wrong,		\
	  BCH_FSCK_ERR_fs_usage_hidden_wrong,			\
	  BCH_FSCK_ERR_fs_usage_btree_wrong,			\
	  BCH_FSCK_ERR_fs_usage_data_wrong,			\
	  BCH_FSCK_ERR_fs_usage_cached_wrong,			\
	  BCH_FSCK_ERR_fs_usage_reserved_wrong,			\
	  BCH_FSCK_ERR_fs_usage_nr_inodes_wrong,		\
	  BCH_FSCK_ERR_fs_usage_persistent_reserved_wrong,	\
	  BCH_FSCK_ERR_fs_usage_replicas_wrong,			\
	  BCH_FSCK_ERR_accounting_replicas_not_marked,		\
	  BCH_FSCK_ERR_bkey_version_in_future)			\
	x(rebalance_work_acct_fix,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_accounting_mismatch,			\
	  BCH_FSCK_ERR_accounting_key_replicas_nr_devs_0,	\
	  BCH_FSCK_ERR_accounting_key_junk_at_end)		\
	x(backpointer_bucket_gen,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_extents_to_backpointers),\
	  BCH_FSCK_ERR_backpointer_bucket_offset_wrong,		\
	  BCH_FSCK_ERR_backpointer_to_missing_ptr,		\
	  BCH_FSCK_ERR_ptr_to_missing_backpointer)		\
	x(disk_accounting_big_endian,				\
	  BIT_ULL(BCH_RECOVERY_PASS_check_allocations),		\
	  BCH_FSCK_ERR_accounting_mismatch,			\
	  BCH_FSCK_ERR_accounting_key_replicas_nr_devs_0,	\
	  BCH_FSCK_ERR_accounting_key_junk_at_end)

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

static int have_stripes(struct bch_fs *c)
{
	if (IS_ERR_OR_NULL(c->btree_roots_known[BTREE_ID_stripes].b))
		return 0;

	return !btree_node_fake(c->btree_roots_known[BTREE_ID_stripes].b);
}

int bch2_sb_set_upgrade_extra(struct bch_fs *c)
{
	unsigned old_version = c->sb.version_upgrade_complete ?: c->sb.version;
	unsigned new_version = c->sb.version;
	bool write_sb = false;
	int ret = 0;

	mutex_lock(&c->sb_lock);
	struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);

	if (old_version <  bcachefs_metadata_version_bucket_stripe_sectors &&
	    new_version >= bcachefs_metadata_version_bucket_stripe_sectors &&
	    (ret = have_stripes(c) > 0)) {
		__set_bit_le64(BCH_RECOVERY_PASS_STABLE_check_allocations, ext->recovery_passes_required);
		__set_bit_le64(BCH_FSCK_ERR_alloc_key_dirty_sectors_wrong, ext->errors_silent);
		__set_bit_le64(BCH_FSCK_ERR_alloc_key_stripe_sectors_wrong, ext->errors_silent);
		write_sb = true;
	}

	if (write_sb)
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return ret < 0 ? ret : 0;
}

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

			for (const u16 *e = i->errors; e < i->errors + i->nr_errors; e++)
				__set_bit_le64(*e, ext->errors_silent);
		}
}

#define x(ver, passes, ...) static const u16 downgrade_##ver##_errors[] = { __VA_ARGS__ };
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

static int downgrade_table_extra(struct bch_fs *c, darray_char *table)
{
	unsigned dst_offset = table->nr;
	struct bch_sb_field_downgrade_entry *dst = (void *) &darray_top(*table);
	unsigned bytes = sizeof(*dst) + sizeof(dst->errors[0]) * le16_to_cpu(dst->nr_errors);
	int ret = 0;

	unsigned nr_errors = le16_to_cpu(dst->nr_errors);

	switch (le16_to_cpu(dst->version)) {
	case bcachefs_metadata_version_bucket_stripe_sectors:
		if (have_stripes(c)) {
			bytes += sizeof(dst->errors[0]) * 2;

			ret = darray_make_room(table, bytes);
			if (ret)
				return ret;

			dst = (void *) &table->data[dst_offset];
			dst->nr_errors = cpu_to_le16(nr_errors + 1);

			/* open coded __set_bit_le64, as dst is packed and
			 * dst->recovery_passes is misaligned */
			unsigned b = BCH_RECOVERY_PASS_STABLE_check_allocations;
			dst->recovery_passes[b / 64] |= cpu_to_le64(BIT_ULL(b % 64));

			dst->errors[nr_errors++] = cpu_to_le16(BCH_FSCK_ERR_alloc_key_dirty_sectors_wrong);
		}
		break;
	}

	return ret;
}

static inline const struct bch_sb_field_downgrade_entry *
downgrade_entry_next_c(const struct bch_sb_field_downgrade_entry *e)
{
	return (void *) &e->errors[le16_to_cpu(e->nr_errors)];
}

#define for_each_downgrade_entry(_d, _i)						\
	for (const struct bch_sb_field_downgrade_entry *_i = (_d)->entries;		\
	     (void *) _i	< vstruct_end(&(_d)->field) &&				\
	     (void *) &_i->errors[0] <= vstruct_end(&(_d)->field) &&			\
	     (void *) downgrade_entry_next_c(_i) <= vstruct_end(&(_d)->field);		\
	     _i = downgrade_entry_next_c(_i))

static int bch2_sb_downgrade_validate(struct bch_sb *sb, struct bch_sb_field *f,
				      enum bch_validate_flags flags, struct printbuf *err)
{
	struct bch_sb_field_downgrade *e = field_to_type(f, downgrade);

	for (const struct bch_sb_field_downgrade_entry *i = e->entries;
	     (void *) i	< vstruct_end(&e->field);
	     i = downgrade_entry_next_c(i)) {
		/*
		 * Careful: sb_field_downgrade_entry is only 2 byte aligned, but
		 * section sizes are 8 byte aligned - an empty entry spanning
		 * the end of the section is allowed (and ignored):
		 */
		if ((void *) &i->errors[0] > vstruct_end(&e->field))
			break;

		if (flags & BCH_VALIDATE_write &&
		    (void *) downgrade_entry_next_c(i) > vstruct_end(&e->field)) {
			prt_printf(err, "downgrade entry overruns end of superblock section");
			return -BCH_ERR_invalid_sb_downgrade;
		}

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
		prt_str(out, "version:\t");
		bch2_version_to_text(out, le16_to_cpu(i->version));
		prt_newline(out);

		prt_str(out, "recovery passes:\t");
		prt_bitflags(out, bch2_recovery_passes,
			     bch2_recovery_passes_from_stable(le64_to_cpu(i->recovery_passes[0])));
		prt_newline(out);

		prt_str(out, "errors:\t");
		bool first = true;
		for (unsigned j = 0; j < le16_to_cpu(i->nr_errors); j++) {
			if (!first)
				prt_char(out, ',');
			first = false;
			bch2_sb_error_id_to_text(out, le16_to_cpu(i->errors[j]));
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
	if (!test_bit(BCH_FS_btree_running, &c->flags))
		return 0;

	darray_char table = {};
	int ret = 0;

	for (const struct upgrade_downgrade_entry *src = downgrade_table;
	     src < downgrade_table + ARRAY_SIZE(downgrade_table);
	     src++) {
		if (BCH_VERSION_MAJOR(src->version) != BCH_VERSION_MAJOR(le16_to_cpu(c->disk_sb.sb->version)))
			continue;

		if (src->version < c->sb.version_incompat)
			continue;

		struct bch_sb_field_downgrade_entry *dst;
		unsigned bytes = sizeof(*dst) + sizeof(dst->errors[0]) * src->nr_errors;

		ret = darray_make_room(&table, bytes);
		if (ret)
			goto out;

		dst = (void *) &darray_top(table);
		dst->version = cpu_to_le16(src->version);
		dst->recovery_passes[0]	= cpu_to_le64(bch2_recovery_passes_to_stable(src->recovery_passes));
		dst->recovery_passes[1]	= 0;
		dst->nr_errors		= cpu_to_le16(src->nr_errors);
		for (unsigned i = 0; i < src->nr_errors; i++)
			dst->errors[i] = cpu_to_le16(src->errors[i]);

		ret = downgrade_table_extra(c, &table);
		if (ret)
			goto out;

		if (!dst->recovery_passes[0] &&
		    !dst->recovery_passes[1] &&
		    !dst->nr_errors)
			continue;

		table.nr += sizeof(*dst) + sizeof(dst->errors[0]) * le16_to_cpu(dst->nr_errors);
	}

	struct bch_sb_field_downgrade *d = bch2_sb_field_get(c->disk_sb.sb, downgrade);

	unsigned sb_u64s = DIV_ROUND_UP(sizeof(*d) + table.nr, sizeof(u64));

	if (d && le32_to_cpu(d->field.u64s) > sb_u64s)
		goto out;

	d = bch2_sb_field_resize(&c->disk_sb, downgrade, sb_u64s);
	if (!d) {
		ret = bch_err_throw(c, ENOSPC_sb_downgrade);
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
				if (e < BCH_FSCK_ERR_MAX)
					__set_bit(e, c->sb.errors_silent);
				if (e < sizeof(ext->errors_silent) * 8)
					__set_bit_le64(e, ext->errors_silent);
			}
		}
	}
}
