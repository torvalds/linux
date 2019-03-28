// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "buckets.h"
#include "dirent.h"
#include "ec.h"
#include "error.h"
#include "fsck.h"
#include "journal_io.h"
#include "quota.h"
#include "recovery.h"
#include "replicas.h"
#include "super-io.h"

#include <linux/stat.h>

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

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

static int journal_replay_entry_early(struct bch_fs *c,
				      struct jset_entry *entry)
{
	int ret = 0;

	switch (entry->type) {
	case BCH_JSET_ENTRY_btree_root: {
		struct btree_root *r = &c->btree_roots[entry->btree_id];

		if (entry->u64s) {
			r->level = entry->level;
			bkey_copy(&r->key, &entry->start[0]);
			r->error = 0;
		} else {
			r->error = -EIO;
		}
		r->alive = true;
		break;
	}
	case BCH_JSET_ENTRY_usage: {
		struct jset_entry_usage *u =
			container_of(entry, struct jset_entry_usage, entry);

		switch (entry->btree_id) {
		case FS_USAGE_RESERVED:
			if (entry->level < BCH_REPLICAS_MAX)
				percpu_u64_set(&c->usage[0]->
					       persistent_reserved[entry->level],
					       le64_to_cpu(u->v));
			break;
		case FS_USAGE_INODES:
			percpu_u64_set(&c->usage[0]->nr_inodes,
				       le64_to_cpu(u->v));
			break;
		case FS_USAGE_KEY_VERSION:
			atomic64_set(&c->key_version,
				     le64_to_cpu(u->v));
			break;
		}

		break;
	}
	case BCH_JSET_ENTRY_data_usage: {
		struct jset_entry_data_usage *u =
			container_of(entry, struct jset_entry_data_usage, entry);
		ret = bch2_replicas_set_usage(c, &u->r,
					      le64_to_cpu(u->v));
		break;
	}
	}

	return ret;
}

static int verify_superblock_clean(struct bch_fs *c,
				   struct bch_sb_field_clean **cleanp,
				   struct jset *j)
{
	unsigned i;
	struct bch_sb_field_clean *clean = *cleanp;
	int ret = 0;

	if (!clean || !j)
		return 0;

	if (mustfix_fsck_err_on(j->seq != clean->journal_seq, c,
			"superblock journal seq (%llu) doesn't match journal (%llu) after clean shutdown",
			le64_to_cpu(clean->journal_seq),
			le64_to_cpu(j->seq))) {
		kfree(clean);
		*cleanp = NULL;
		return 0;
	}

	mustfix_fsck_err_on(j->read_clock != clean->read_clock, c,
			"superblock read clock doesn't match journal after clean shutdown");
	mustfix_fsck_err_on(j->write_clock != clean->write_clock, c,
			"superblock read clock doesn't match journal after clean shutdown");

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct bkey_i *k1, *k2;
		unsigned l1 = 0, l2 = 0;

		k1 = btree_root_find(c, clean, NULL, i, &l1);
		k2 = btree_root_find(c, NULL, j, i, &l2);

		if (!k1 && !k2)
			continue;

		mustfix_fsck_err_on(!k1 || !k2 ||
				    IS_ERR(k1) ||
				    IS_ERR(k2) ||
				    k1->k.u64s != k2->k.u64s ||
				    memcmp(k1, k2, bkey_bytes(k1)) ||
				    l1 != l2, c,
			"superblock btree root doesn't match journal after clean shutdown");
	}
fsck_err:
	return ret;
}

static bool journal_empty(struct list_head *journal)
{
	struct journal_replay *i;
	struct jset_entry *entry;

	if (list_empty(journal))
		return true;

	i = list_last_entry(journal, struct journal_replay, list);

	if (i->j.last_seq != i->j.seq)
		return false;

	list_for_each_entry(i, journal, list) {
		vstruct_for_each(&i->j, entry) {
			if (entry->type == BCH_JSET_ENTRY_btree_root ||
			    entry->type == BCH_JSET_ENTRY_usage ||
			    entry->type == BCH_JSET_ENTRY_data_usage)
				continue;

			if (entry->type == BCH_JSET_ENTRY_btree_keys &&
			    !entry->u64s)
				continue;
			return false;
		}
	}

	return true;
}

int bch2_fs_recovery(struct bch_fs *c)
{
	const char *err = "cannot allocate memory";
	struct bch_sb_field_clean *clean = NULL, *sb_clean = NULL;
	struct jset_entry *entry;
	LIST_HEAD(journal);
	struct jset *j = NULL;
	unsigned i;
	bool run_gc = c->opts.fsck ||
		!(c->sb.compat & (1ULL << BCH_COMPAT_FEAT_ALLOC_INFO));
	int ret;

	mutex_lock(&c->sb_lock);
	if (!c->replicas.entries) {
		bch_info(c, "building replicas info");
		set_bit(BCH_FS_REBUILD_REPLICAS, &c->flags);
	}

	if (c->sb.clean)
		sb_clean = bch2_sb_get_clean(c->disk_sb.sb);
	if (sb_clean) {
		clean = kmemdup(sb_clean, vstruct_bytes(&sb_clean->field),
				GFP_KERNEL);
		if (!clean) {
			ret = -ENOMEM;
			mutex_unlock(&c->sb_lock);
			goto err;
		}

		if (le16_to_cpu(c->disk_sb.sb->version) <
		    bcachefs_metadata_version_bkey_renumber)
			bch2_sb_clean_renumber(clean, READ);
	}
	mutex_unlock(&c->sb_lock);

	if (clean)
		bch_info(c, "recovering from clean shutdown, journal seq %llu",
			 le64_to_cpu(clean->journal_seq));

	if (!clean || c->opts.fsck) {
		ret = bch2_journal_read(c, &journal);
		if (ret)
			goto err;

		j = &list_entry(journal.prev, struct journal_replay, list)->j;
	} else {
		ret = bch2_journal_set_seq(c,
					   le64_to_cpu(clean->journal_seq),
					   le64_to_cpu(clean->journal_seq));
		BUG_ON(ret);
	}

	ret = verify_superblock_clean(c, &clean, j);
	if (ret)
		goto err;

	fsck_err_on(clean && !journal_empty(&journal), c,
		    "filesystem marked clean but journal not empty");

	err = "insufficient memory";
	if (clean) {
		c->bucket_clock[READ].hand = le16_to_cpu(clean->read_clock);
		c->bucket_clock[WRITE].hand = le16_to_cpu(clean->write_clock);

		for (entry = clean->start;
		     entry != vstruct_end(&clean->field);
		     entry = vstruct_next(entry)) {
			ret = journal_replay_entry_early(c, entry);
			if (ret)
				goto err;
		}
	} else {
		struct journal_replay *i;

		c->bucket_clock[READ].hand = le16_to_cpu(j->read_clock);
		c->bucket_clock[WRITE].hand = le16_to_cpu(j->write_clock);

		list_for_each_entry(i, &journal, list)
			vstruct_for_each(&i->j, entry) {
				ret = journal_replay_entry_early(c, entry);
				if (ret)
					goto err;
			}
	}

	bch2_fs_usage_initialize(c);

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct btree_root *r = &c->btree_roots[i];

		if (!r->alive)
			continue;

		err = "invalid btree root pointer";
		ret = -1;
		if (r->error)
			goto err;

		if (i == BTREE_ID_ALLOC &&
		    test_reconstruct_alloc(c))
			continue;

		err = "error reading btree root";
		ret = bch2_btree_root_read(c, i, &r->key, r->level);
		if (ret) {
			if (i != BTREE_ID_ALLOC)
				goto err;

			mustfix_fsck_err(c, "error reading btree root");
			run_gc = true;
		}
	}

	for (i = 0; i < BTREE_ID_NR; i++)
		if (!c->btree_roots[i].b)
			bch2_btree_root_alloc(c, i);

	err = "error reading allocation information";
	ret = bch2_alloc_read(c, &journal);
	if (ret)
		goto err;

	bch_verbose(c, "starting stripes_read");
	ret = bch2_stripes_read(c, &journal);
	if (ret)
		goto err;
	bch_verbose(c, "stripes_read done");

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);

	if (run_gc) {
		bch_verbose(c, "starting mark and sweep:");
		err = "error in recovery";
		ret = bch2_gc(c, &journal, true);
		if (ret)
			goto err;
		bch_verbose(c, "mark and sweep done");
	}

	clear_bit(BCH_FS_REBUILD_REPLICAS, &c->flags);
	set_bit(BCH_FS_INITIAL_GC_DONE, &c->flags);

	/*
	 * Skip past versions that might have possibly been used (as nonces),
	 * but hadn't had their pointers written:
	 */
	if (c->sb.encryption_type && !c->sb.clean)
		atomic64_add(1 << 16, &c->key_version);

	if (c->opts.noreplay)
		goto out;

	/*
	 * bch2_fs_journal_start() can't happen sooner, or btree_gc_finish()
	 * will give spurious errors about oldest_gen > bucket_gen -
	 * this is a hack but oh well.
	 */
	bch2_fs_journal_start(&c->journal);

	bch_verbose(c, "starting journal replay:");
	err = "journal replay failed";
	ret = bch2_journal_replay(c, &journal);
	if (ret)
		goto err;
	bch_verbose(c, "journal replay done");

	if (c->opts.norecovery)
		goto out;

	err = "error in fsck";
	ret = bch2_fsck(c);
	if (ret)
		goto err;

	mutex_lock(&c->sb_lock);
	if (c->opts.version_upgrade) {
		if (c->sb.version < bcachefs_metadata_version_new_versioning)
			c->disk_sb.sb->version_min =
				le16_to_cpu(bcachefs_metadata_version_min);
		c->disk_sb.sb->version = le16_to_cpu(bcachefs_metadata_version_current);
	}

	if (c->opts.fsck &&
	    !test_bit(BCH_FS_ERROR, &c->flags)) {
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_ATOMIC_NLINK;
		SET_BCH_SB_HAS_ERRORS(c->disk_sb.sb, 0);
	}
	mutex_unlock(&c->sb_lock);

	if (enabled_qtypes(c)) {
		bch_verbose(c, "reading quotas:");
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
		bch_verbose(c, "quotas done");
	}

out:
	bch2_journal_entries_free(&journal);
	kfree(clean);
	return ret;
err:
fsck_err:
	pr_err("Error in recovery: %s (%i)", err, ret);
	goto out;
}

int bch2_fs_initialize(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	struct bkey_inode_buf packed_inode;
	struct bch_hash_info root_hash_info;
	struct qstr lostfound = QSTR("lost+found");
	const char *err = "cannot allocate memory";
	struct bch_dev *ca;
	LIST_HEAD(journal);
	unsigned i;
	int ret;

	bch_notice(c, "initializing new filesystem");

	mutex_lock(&c->sb_lock);
	for_each_online_member(ca, c, i)
		bch2_mark_dev_superblock(c, ca, 0);
	mutex_unlock(&c->sb_lock);

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);
	set_bit(BCH_FS_INITIAL_GC_DONE, &c->flags);

	for (i = 0; i < BTREE_ID_NR; i++)
		bch2_btree_root_alloc(c, i);

	err = "unable to allocate journal buckets";
	for_each_online_member(ca, c, i) {
		ret = bch2_dev_journal_alloc(ca);
		if (ret) {
			percpu_ref_put(&ca->io_ref);
			goto err;
		}
	}

	/*
	 * journal_res_get() will crash if called before this has
	 * set up the journal.pin FIFO and journal.cur pointer:
	 */
	bch2_fs_journal_start(&c->journal);
	bch2_journal_set_replay_done(&c->journal);

	err = "error going read write";
	ret = __bch2_fs_read_write(c, true);
	if (ret)
		goto err;

	bch2_inode_init(c, &root_inode, 0, 0,
			S_IFDIR|S_IRWXU|S_IRUGO|S_IXUGO, 0, NULL);
	root_inode.bi_inum = BCACHEFS_ROOT_INO;
	root_inode.bi_nlink++; /* lost+found */
	bch2_inode_pack(&packed_inode, &root_inode);

	err = "error creating root directory";
	ret = bch2_btree_insert(c, BTREE_ID_INODES,
				&packed_inode.inode.k_i,
				NULL, NULL, 0);
	if (ret)
		goto err;

	bch2_inode_init(c, &lostfound_inode, 0, 0,
			S_IFDIR|S_IRWXU|S_IRUGO|S_IXUGO, 0,
			&root_inode);
	lostfound_inode.bi_inum = BCACHEFS_ROOT_INO + 1;
	bch2_inode_pack(&packed_inode, &lostfound_inode);

	err = "error creating lost+found";
	ret = bch2_btree_insert(c, BTREE_ID_INODES,
				&packed_inode.inode.k_i,
				NULL, NULL, 0);
	if (ret)
		goto err;

	root_hash_info = bch2_hash_info_init(c, &root_inode);

	ret = bch2_dirent_create(c, BCACHEFS_ROOT_INO, &root_hash_info, DT_DIR,
				 &lostfound, lostfound_inode.bi_inum, NULL,
				 BTREE_INSERT_NOFAIL);
	if (ret)
		goto err;

	if (enabled_qtypes(c)) {
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
	}

	err = "error writing first journal entry";
	ret = bch2_journal_meta(&c->journal);
	if (ret)
		goto err;

	mutex_lock(&c->sb_lock);
	c->disk_sb.sb->version = c->disk_sb.sb->version_min =
		le16_to_cpu(bcachefs_metadata_version_current);
	c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_ATOMIC_NLINK;

	SET_BCH_SB_INITIALIZED(c->disk_sb.sb, true);
	SET_BCH_SB_CLEAN(c->disk_sb.sb, false);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
err:
	pr_err("Error initializing new filesystem: %s (%i)", err, ret);
	return ret;
}
