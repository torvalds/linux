// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "error.h"
#include "journal_io.h"
#include "replicas.h"
#include "sb-clean.h"
#include "super-io.h"

/*
 * BCH_SB_FIELD_clean:
 *
 * Btree roots, and a few other things, are recovered from the journal after an
 * unclean shutdown - but after a clean shutdown, to avoid having to read the
 * journal, we can store them in the superblock.
 *
 * bch_sb_field_clean simply contains a list of journal entries, stored exactly
 * as they would be in the journal:
 */

int bch2_sb_clean_validate_late(struct bch_fs *c, struct bch_sb_field_clean *clean,
				int write)
{
	struct jset_entry *entry;
	int ret;

	for (entry = clean->start;
	     entry < (struct jset_entry *) vstruct_end(&clean->field);
	     entry = vstruct_next(entry)) {
		if (vstruct_end(entry) > vstruct_end(&clean->field)) {
			bch_err(c, "journal entry (u64s %u) overran end of superblock clean section (u64s %u) by %zu",
				le16_to_cpu(entry->u64s), le32_to_cpu(clean->field.u64s),
				(u64 *) vstruct_end(entry) - (u64 *) vstruct_end(&clean->field));
			bch2_sb_error_count(c, BCH_FSCK_ERR_sb_clean_entry_overrun);
			return -BCH_ERR_fsck_repair_unimplemented;
		}

		ret = bch2_journal_entry_validate(c, NULL, entry,
						  le16_to_cpu(c->disk_sb.sb->version),
						  BCH_SB_BIG_ENDIAN(c->disk_sb.sb),
						  write);
		if (ret)
			return ret;
	}

	return 0;
}

static struct bkey_i *btree_root_find(struct bch_fs *c,
				      struct bch_sb_field_clean *clean,
				      struct jset *j,
				      enum btree_id id, unsigned *level)
{
	struct bkey_i *k;
	struct jset_entry *entry, *start, *end;

	if (clean) {
		start = clean->start;
		end = vstruct_end(&clean->field);
	} else {
		start = j->start;
		end = vstruct_last(j);
	}

	for (entry = start; entry < end; entry = vstruct_next(entry))
		if (entry->type == BCH_JSET_ENTRY_btree_root &&
		    entry->btree_id == id)
			goto found;

	return NULL;
found:
	if (!entry->u64s)
		return ERR_PTR(-EINVAL);

	k = entry->start;
	*level = entry->level;
	return k;
}

int bch2_verify_superblock_clean(struct bch_fs *c,
				 struct bch_sb_field_clean **cleanp,
				 struct jset *j)
{
	unsigned i;
	struct bch_sb_field_clean *clean = *cleanp;
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	int ret = 0;

	if (mustfix_fsck_err_on(j->seq != clean->journal_seq, c,
			sb_clean_journal_seq_mismatch,
			"superblock journal seq (%llu) doesn't match journal (%llu) after clean shutdown",
			le64_to_cpu(clean->journal_seq),
			le64_to_cpu(j->seq))) {
		kfree(clean);
		*cleanp = NULL;
		return 0;
	}

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct bkey_i *k1, *k2;
		unsigned l1 = 0, l2 = 0;

		k1 = btree_root_find(c, clean, NULL, i, &l1);
		k2 = btree_root_find(c, NULL, j, i, &l2);

		if (!k1 && !k2)
			continue;

		printbuf_reset(&buf1);
		printbuf_reset(&buf2);

		if (k1)
			bch2_bkey_val_to_text(&buf1, c, bkey_i_to_s_c(k1));
		else
			prt_printf(&buf1, "(none)");

		if (k2)
			bch2_bkey_val_to_text(&buf2, c, bkey_i_to_s_c(k2));
		else
			prt_printf(&buf2, "(none)");

		mustfix_fsck_err_on(!k1 || !k2 ||
				    IS_ERR(k1) ||
				    IS_ERR(k2) ||
				    k1->k.u64s != k2->k.u64s ||
				    memcmp(k1, k2, bkey_bytes(&k1->k)) ||
				    l1 != l2, c,
			sb_clean_btree_root_mismatch,
			"superblock btree root %u doesn't match journal after clean shutdown\n"
			"sb:      l=%u %s\n"
			"journal: l=%u %s\n", i,
			l1, buf1.buf,
			l2, buf2.buf);
	}
fsck_err:
	printbuf_exit(&buf2);
	printbuf_exit(&buf1);
	return ret;
}

struct bch_sb_field_clean *bch2_read_superblock_clean(struct bch_fs *c)
{
	struct bch_sb_field_clean *clean, *sb_clean;
	int ret;

	mutex_lock(&c->sb_lock);
	sb_clean = bch2_sb_field_get(c->disk_sb.sb, clean);

	if (fsck_err_on(!sb_clean, c,
			sb_clean_missing,
			"superblock marked clean but clean section not present")) {
		SET_BCH_SB_CLEAN(c->disk_sb.sb, false);
		c->sb.clean = false;
		mutex_unlock(&c->sb_lock);
		return NULL;
	}

	clean = kmemdup(sb_clean, vstruct_bytes(&sb_clean->field),
			GFP_KERNEL);
	if (!clean) {
		mutex_unlock(&c->sb_lock);
		return ERR_PTR(-BCH_ERR_ENOMEM_read_superblock_clean);
	}

	ret = bch2_sb_clean_validate_late(c, clean, READ);
	if (ret) {
		mutex_unlock(&c->sb_lock);
		return ERR_PTR(ret);
	}

	mutex_unlock(&c->sb_lock);

	return clean;
fsck_err:
	mutex_unlock(&c->sb_lock);
	return ERR_PTR(ret);
}

void bch2_journal_super_entries_add_common(struct bch_fs *c,
					   struct jset_entry **end,
					   u64 journal_seq)
{
	percpu_down_read(&c->mark_lock);

	if (!journal_seq) {
		for (unsigned i = 0; i < ARRAY_SIZE(c->usage); i++)
			bch2_fs_usage_acc_to_base(c, i);
	} else {
		bch2_fs_usage_acc_to_base(c, journal_seq & JOURNAL_BUF_MASK);
	}

	{
		struct jset_entry_usage *u =
			container_of(jset_entry_init(end, sizeof(*u)),
				     struct jset_entry_usage, entry);

		u->entry.type	= BCH_JSET_ENTRY_usage;
		u->entry.btree_id = BCH_FS_USAGE_inodes;
		u->v		= cpu_to_le64(c->usage_base->b.nr_inodes);
	}

	{
		struct jset_entry_usage *u =
			container_of(jset_entry_init(end, sizeof(*u)),
				     struct jset_entry_usage, entry);

		u->entry.type	= BCH_JSET_ENTRY_usage;
		u->entry.btree_id = BCH_FS_USAGE_key_version;
		u->v		= cpu_to_le64(atomic64_read(&c->key_version));
	}

	for (unsigned i = 0; i < BCH_REPLICAS_MAX; i++) {
		struct jset_entry_usage *u =
			container_of(jset_entry_init(end, sizeof(*u)),
				     struct jset_entry_usage, entry);

		u->entry.type	= BCH_JSET_ENTRY_usage;
		u->entry.btree_id = BCH_FS_USAGE_reserved;
		u->entry.level	= i;
		u->v		= cpu_to_le64(c->usage_base->persistent_reserved[i]);
	}

	for (unsigned i = 0; i < c->replicas.nr; i++) {
		struct bch_replicas_entry_v1 *e =
			cpu_replicas_entry(&c->replicas, i);
		struct jset_entry_data_usage *u =
			container_of(jset_entry_init(end, sizeof(*u) + e->nr_devs),
				     struct jset_entry_data_usage, entry);

		u->entry.type	= BCH_JSET_ENTRY_data_usage;
		u->v		= cpu_to_le64(c->usage_base->replicas[i]);
		unsafe_memcpy(&u->r, e, replicas_entry_bytes(e),
			      "embedded variable length struct");
	}

	for_each_member_device(c, ca) {
		unsigned b = sizeof(struct jset_entry_dev_usage) +
			sizeof(struct jset_entry_dev_usage_type) * BCH_DATA_NR;
		struct jset_entry_dev_usage *u =
			container_of(jset_entry_init(end, b),
				     struct jset_entry_dev_usage, entry);

		u->entry.type = BCH_JSET_ENTRY_dev_usage;
		u->dev = cpu_to_le32(ca->dev_idx);

		for (unsigned i = 0; i < BCH_DATA_NR; i++) {
			u->d[i].buckets = cpu_to_le64(ca->usage_base->d[i].buckets);
			u->d[i].sectors	= cpu_to_le64(ca->usage_base->d[i].sectors);
			u->d[i].fragmented = cpu_to_le64(ca->usage_base->d[i].fragmented);
		}
	}

	percpu_up_read(&c->mark_lock);

	for (unsigned i = 0; i < 2; i++) {
		struct jset_entry_clock *clock =
			container_of(jset_entry_init(end, sizeof(*clock)),
				     struct jset_entry_clock, entry);

		clock->entry.type = BCH_JSET_ENTRY_clock;
		clock->rw	= i;
		clock->time	= cpu_to_le64(atomic64_read(&c->io_clock[i].now));
	}
}

static int bch2_sb_clean_validate(struct bch_sb *sb, struct bch_sb_field *f,
				  enum bch_validate_flags flags, struct printbuf *err)
{
	struct bch_sb_field_clean *clean = field_to_type(f, clean);

	if (vstruct_bytes(&clean->field) < sizeof(*clean)) {
		prt_printf(err, "wrong size (got %zu should be %zu)",
		       vstruct_bytes(&clean->field), sizeof(*clean));
		return -BCH_ERR_invalid_sb_clean;
	}

	for (struct jset_entry *entry = clean->start;
	     entry != vstruct_end(&clean->field);
	     entry = vstruct_next(entry)) {
		if ((void *) vstruct_next(entry) > vstruct_end(&clean->field)) {
			prt_str(err, "entry type ");
			bch2_prt_jset_entry_type(err, entry->type);
			prt_str(err, " overruns end of section");
			return -BCH_ERR_invalid_sb_clean;
		}
	}

	return 0;
}

static void bch2_sb_clean_to_text(struct printbuf *out, struct bch_sb *sb,
				  struct bch_sb_field *f)
{
	struct bch_sb_field_clean *clean = field_to_type(f, clean);
	struct jset_entry *entry;

	prt_printf(out, "flags:          %x\n",		le32_to_cpu(clean->flags));
	prt_printf(out, "journal_seq:    %llu\n",	le64_to_cpu(clean->journal_seq));

	for (entry = clean->start;
	     entry != vstruct_end(&clean->field);
	     entry = vstruct_next(entry)) {
		if ((void *) vstruct_next(entry) > vstruct_end(&clean->field))
			break;

		if (entry->type == BCH_JSET_ENTRY_btree_keys &&
		    !entry->u64s)
			continue;

		bch2_journal_entry_to_text(out, NULL, entry);
		prt_newline(out);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_clean = {
	.validate	= bch2_sb_clean_validate,
	.to_text	= bch2_sb_clean_to_text,
};

int bch2_fs_mark_dirty(struct bch_fs *c)
{
	int ret;

	/*
	 * Unconditionally write superblock, to verify it hasn't changed before
	 * we go rw:
	 */

	mutex_lock(&c->sb_lock);
	SET_BCH_SB_CLEAN(c->disk_sb.sb, false);
	c->disk_sb.sb->features[0] |= cpu_to_le64(BCH_SB_FEATURES_ALWAYS);

	ret = bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return ret;
}

void bch2_fs_mark_clean(struct bch_fs *c)
{
	struct bch_sb_field_clean *sb_clean;
	struct jset_entry *entry;
	unsigned u64s;
	int ret;

	mutex_lock(&c->sb_lock);
	if (BCH_SB_CLEAN(c->disk_sb.sb))
		goto out;

	SET_BCH_SB_CLEAN(c->disk_sb.sb, true);

	c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_alloc_info);
	c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_alloc_metadata);
	c->disk_sb.sb->features[0] &= cpu_to_le64(~(1ULL << BCH_FEATURE_extents_above_btree_updates));
	c->disk_sb.sb->features[0] &= cpu_to_le64(~(1ULL << BCH_FEATURE_btree_updates_journalled));

	u64s = sizeof(*sb_clean) / sizeof(u64) + c->journal.entry_u64s_reserved;

	sb_clean = bch2_sb_field_resize(&c->disk_sb, clean, u64s);
	if (!sb_clean) {
		bch_err(c, "error resizing superblock while setting filesystem clean");
		goto out;
	}

	sb_clean->flags		= 0;
	sb_clean->journal_seq	= cpu_to_le64(atomic64_read(&c->journal.seq));

	/* Trying to catch outstanding bug: */
	BUG_ON(le64_to_cpu(sb_clean->journal_seq) > S64_MAX);

	entry = sb_clean->start;
	bch2_journal_super_entries_add_common(c, &entry, 0);
	entry = bch2_btree_roots_to_journal_entries(c, entry, 0);
	BUG_ON((void *) entry > vstruct_end(&sb_clean->field));

	memset(entry, 0,
	       vstruct_end(&sb_clean->field) - (void *) entry);

	/*
	 * this should be in the write path, and we should be validating every
	 * superblock section:
	 */
	ret = bch2_sb_clean_validate_late(c, sb_clean, WRITE);
	if (ret) {
		bch_err(c, "error writing marking filesystem clean: validate error");
		goto out;
	}

	bch2_journal_pos_from_member_info_set(c);

	bch2_write_super(c);
out:
	mutex_unlock(&c->sb_lock);
}
