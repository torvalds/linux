// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "dirent.h"
#include "ec.h"
#include "error.h"
#include "fsck.h"
#include "journal_io.h"
#include "quota.h"
#include "recovery.h"
#include "super-io.h"

#include <linux/stat.h>

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

struct bkey_i *btree_root_find(struct bch_fs *c,
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

static int verify_superblock_clean(struct bch_fs *c,
				   struct bch_sb_field_clean *clean,
				   struct jset *j)
{
	unsigned i;
	int ret = 0;

	if (!clean || !j)
		return 0;

	if (mustfix_fsck_err_on(j->seq != clean->journal_seq, c,
			"superblock journal seq (%llu) doesn't match journal (%llu) after clean shutdown",
			le64_to_cpu(clean->journal_seq),
			le64_to_cpu(j->seq)))
		bch2_fs_mark_clean(c, false);

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
			if (entry->type == BCH_JSET_ENTRY_btree_root)
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
	LIST_HEAD(journal);
	struct jset *j = NULL;
	unsigned i;
	int ret;

	mutex_lock(&c->sb_lock);
	if (!rcu_dereference_protected(c->replicas,
			lockdep_is_held(&c->sb_lock))->nr) {
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

	ret = verify_superblock_clean(c, clean, j);
	if (ret)
		goto err;

	fsck_err_on(clean && !journal_empty(&journal), c,
		    "filesystem marked clean but journal not empty");

	if (clean) {
		c->bucket_clock[READ].hand = le16_to_cpu(clean->read_clock);
		c->bucket_clock[WRITE].hand = le16_to_cpu(clean->write_clock);
	} else {
		c->bucket_clock[READ].hand = le16_to_cpu(j->read_clock);
		c->bucket_clock[WRITE].hand = le16_to_cpu(j->write_clock);
	}

	for (i = 0; i < BTREE_ID_NR; i++) {
		unsigned level;
		struct bkey_i *k;

		k = btree_root_find(c, clean, j, i, &level);
		if (!k)
			continue;

		err = "invalid btree root pointer";
		if (IS_ERR(k))
			goto err;

		err = "error reading btree root";
		if (bch2_btree_root_read(c, i, k, level)) {
			if (i != BTREE_ID_ALLOC)
				goto err;

			mustfix_fsck_err(c, "error reading btree root");
		}
	}

	for (i = 0; i < BTREE_ID_NR; i++)
		if (!c->btree_roots[i].b)
			bch2_btree_root_alloc(c, i);

	err = "error reading allocation information";
	ret = bch2_alloc_read(c, &journal);
	if (ret)
		goto err;

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);

	err = "cannot allocate memory";
	ret = bch2_fs_ec_start(c);
	if (ret)
		goto err;

	bch_verbose(c, "starting mark and sweep:");
	err = "error in recovery";
	ret = bch2_initial_gc(c, &journal);
	if (ret)
		goto err;
	bch_verbose(c, "mark and sweep done");

	clear_bit(BCH_FS_REBUILD_REPLICAS, &c->flags);

	if (c->opts.noreplay)
		goto out;

	/*
	 * Mark dirty before journal replay, fsck:
	 * XXX: after a clean shutdown, this could be done lazily only when fsck
	 * finds an error
	 */
	bch2_fs_mark_clean(c, false);

	/*
	 * bch2_fs_journal_start() can't happen sooner, or btree_gc_finish()
	 * will give spurious errors about oldest_gen > bucket_gen -
	 * this is a hack but oh well.
	 */
	bch2_fs_journal_start(&c->journal);

	err = "error starting allocator";
	ret = bch2_fs_allocator_start(c);
	if (ret)
		goto err;

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

	if (!test_bit(BCH_FS_FSCK_UNFIXED_ERRORS, &c->flags))
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_ATOMIC_NLINK;
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

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);

	for (i = 0; i < BTREE_ID_NR; i++)
		bch2_btree_root_alloc(c, i);

	ret = bch2_initial_gc(c, &journal);
	if (ret)
		goto err;

	err = "unable to allocate journal buckets";
	for_each_online_member(ca, c, i)
		if (bch2_dev_journal_alloc(ca)) {
			percpu_ref_put(&ca->io_ref);
			goto err;
		}

	/*
	 * journal_res_get() will crash if called before this has
	 * set up the journal.pin FIFO and journal.cur pointer:
	 */
	bch2_fs_journal_start(&c->journal);
	bch2_journal_set_replay_done(&c->journal);

	err = "error starting allocator";
	ret = bch2_fs_allocator_start(c);
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

	atomic_long_set(&c->nr_inodes, 2);

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
