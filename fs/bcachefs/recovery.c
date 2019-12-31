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
#include "fs-common.h"
#include "fsck.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "quota.h"
#include "recovery.h"
#include "replicas.h"
#include "super-io.h"

#include <linux/sort.h>
#include <linux/stat.h>

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

/* iterate over keys read from the journal: */

struct journal_iter bch2_journal_iter_init(struct journal_keys *keys,
					   enum btree_id id)
{
	return (struct journal_iter) {
		.keys		= keys,
		.k		= keys->d,
		.btree_id	= id,
	};
}

struct bkey_s_c bch2_journal_iter_peek(struct journal_iter *iter)
{
	while (1) {
		if (iter->k == iter->keys->d + iter->keys->nr)
			return bkey_s_c_null;

		if (iter->k->btree_id == iter->btree_id)
			return bkey_i_to_s_c(iter->k->k);

		iter->k++;
	}

	return bkey_s_c_null;
}

struct bkey_s_c bch2_journal_iter_next(struct journal_iter *iter)
{
	if (iter->k == iter->keys->d + iter->keys->nr)
		return bkey_s_c_null;

	iter->k++;
	return bch2_journal_iter_peek(iter);
}

/* sort and dedup all keys in the journal: */

static void journal_entries_free(struct list_head *list)
{

	while (!list_empty(list)) {
		struct journal_replay *i =
			list_first_entry(list, struct journal_replay, list);
		list_del(&i->list);
		kvpfree(i, offsetof(struct journal_replay, j) +
			vstruct_bytes(&i->j));
	}
}

static int journal_sort_key_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = _l;
	const struct journal_key *r = _r;

	return cmp_int(l->btree_id, r->btree_id) ?:
		bkey_cmp(l->pos, r->pos) ?:
		cmp_int(l->journal_seq, r->journal_seq) ?:
		cmp_int(l->journal_offset, r->journal_offset);
}

static int journal_sort_seq_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = _l;
	const struct journal_key *r = _r;

	return cmp_int(l->journal_seq, r->journal_seq) ?:
		cmp_int(l->btree_id, r->btree_id) ?:
		bkey_cmp(l->pos, r->pos);
}

static void journal_keys_sift(struct journal_keys *keys, struct journal_key *i)
{
	while (i + 1 < keys->d + keys->nr &&
	       journal_sort_key_cmp(i, i + 1) > 0) {
		swap(i[0], i[1]);
		i++;
	}
}

static void journal_keys_free(struct journal_keys *keys)
{
	struct journal_key *i;

	for_each_journal_key(*keys, i)
		if (i->allocated)
			kfree(i->k);
	kvfree(keys->d);
	keys->d = NULL;
	keys->nr = 0;
}

static struct journal_keys journal_keys_sort(struct list_head *journal_entries)
{
	struct journal_replay *p;
	struct jset_entry *entry;
	struct bkey_i *k, *_n;
	struct journal_keys keys = { NULL }, keys_deduped = { NULL };
	struct journal_key *i;
	size_t nr_keys = 0;

	list_for_each_entry(p, journal_entries, list)
		for_each_jset_key(k, _n, entry, &p->j)
			nr_keys++;

	keys.journal_seq_base = keys_deduped.journal_seq_base =
		le64_to_cpu(list_first_entry(journal_entries,
					     struct journal_replay,
					     list)->j.seq);

	keys.d = kvmalloc(sizeof(keys.d[0]) * nr_keys, GFP_KERNEL);
	if (!keys.d)
		goto err;

	keys_deduped.d = kvmalloc(sizeof(keys.d[0]) * nr_keys * 2, GFP_KERNEL);
	if (!keys_deduped.d)
		goto err;

	list_for_each_entry(p, journal_entries, list)
		for_each_jset_key(k, _n, entry, &p->j)
			keys.d[keys.nr++] = (struct journal_key) {
				.btree_id	= entry->btree_id,
				.pos		= bkey_start_pos(&k->k),
				.k		= k,
				.journal_seq	= le64_to_cpu(p->j.seq) -
					keys.journal_seq_base,
				.journal_offset	= k->_data - p->j._data,
			};

	sort(keys.d, nr_keys, sizeof(keys.d[0]), journal_sort_key_cmp, NULL);

	i = keys.d;
	while (i < keys.d + keys.nr) {
		if (i + 1 < keys.d + keys.nr &&
		    i[0].btree_id == i[1].btree_id &&
		    !bkey_cmp(i[0].pos, i[1].pos)) {
			if (bkey_cmp(i[0].k->k.p, i[1].k->k.p) <= 0) {
				i++;
			} else {
				bch2_cut_front(i[1].k->k.p, i[0].k);
				i[0].pos = i[1].k->k.p;
				journal_keys_sift(&keys, i);
			}
			continue;
		}

		if (i + 1 < keys.d + keys.nr &&
		    i[0].btree_id == i[1].btree_id &&
		    bkey_cmp(i[0].k->k.p, bkey_start_pos(&i[1].k->k)) > 0) {
			if ((cmp_int(i[0].journal_seq, i[1].journal_seq) ?:
			     cmp_int(i[0].journal_offset, i[1].journal_offset)) < 0) {
				if (bkey_cmp(i[0].k->k.p, i[1].k->k.p) <= 0) {
					bch2_cut_back(bkey_start_pos(&i[1].k->k), i[0].k);
				} else {
					struct bkey_i *split =
						kmalloc(bkey_bytes(i[0].k), GFP_KERNEL);

					if (!split)
						goto err;

					bkey_copy(split, i[0].k);
					bch2_cut_back(bkey_start_pos(&i[1].k->k), split);
					keys_deduped.d[keys_deduped.nr++] = (struct journal_key) {
						.btree_id	= i[0].btree_id,
						.allocated	= true,
						.pos		= bkey_start_pos(&split->k),
						.k		= split,
						.journal_seq	= i[0].journal_seq,
						.journal_offset	= i[0].journal_offset,
					};

					bch2_cut_front(i[1].k->k.p, i[0].k);
					i[0].pos = i[1].k->k.p;
					journal_keys_sift(&keys, i);
					continue;
				}
			} else {
				if (bkey_cmp(i[0].k->k.p, i[1].k->k.p) >= 0) {
					i[1] = i[0];
					i++;
					continue;
				} else {
					bch2_cut_front(i[0].k->k.p, i[1].k);
					i[1].pos = i[0].k->k.p;
					journal_keys_sift(&keys, i + 1);
					continue;
				}
			}
		}

		keys_deduped.d[keys_deduped.nr++] = *i++;
	}

	kvfree(keys.d);
	return keys_deduped;
err:
	journal_keys_free(&keys_deduped);
	kvfree(keys.d);
	return (struct journal_keys) { NULL };
}

/* journal replay: */

static void replay_now_at(struct journal *j, u64 seq)
{
	BUG_ON(seq < j->replay_journal_seq);
	BUG_ON(seq > j->replay_journal_seq_end);

	while (j->replay_journal_seq < seq)
		bch2_journal_pin_put(j, j->replay_journal_seq++);
}

static int bch2_extent_replay_key(struct bch_fs *c, enum btree_id btree_id,
				  struct bkey_i *k)
{
	struct btree_trans trans;
	struct btree_iter *iter, *split_iter;
	/*
	 * We might cause compressed extents to be split, so we need to pass in
	 * a disk_reservation:
	 */
	struct disk_reservation disk_res =
		bch2_disk_reservation_init(c, 0);
	struct bkey_i *split;
	struct bpos atomic_end;
	/*
	 * Some extents aren't equivalent - w.r.t. what the triggers do
	 * - if they're split:
	 */
	bool remark_if_split = bch2_bkey_sectors_compressed(bkey_i_to_s_c(k)) ||
		k->k.type == KEY_TYPE_reflink_p;
	bool remark = false;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);
retry:
	bch2_trans_begin(&trans);

	iter = bch2_trans_get_iter(&trans, btree_id,
				   bkey_start_pos(&k->k),
				   BTREE_ITER_INTENT);

	do {
		ret = bch2_btree_iter_traverse(iter);
		if (ret)
			goto err;

		atomic_end = bpos_min(k->k.p, iter->l[0].b->key.k.p);

		split_iter = bch2_trans_copy_iter(&trans, iter);
		ret = PTR_ERR_OR_ZERO(split_iter);
		if (ret)
			goto err;

		split = bch2_trans_kmalloc(&trans, bkey_bytes(&k->k));
		ret = PTR_ERR_OR_ZERO(split);
		if (ret)
			goto err;

		if (!remark &&
		    remark_if_split &&
		    bkey_cmp(atomic_end, k->k.p) < 0) {
			ret = bch2_disk_reservation_add(c, &disk_res,
					k->k.size *
					bch2_bkey_nr_ptrs_allocated(bkey_i_to_s_c(k)),
					BCH_DISK_RESERVATION_NOFAIL);
			BUG_ON(ret);

			remark = true;
		}

		bkey_copy(split, k);
		bch2_cut_front(split_iter->pos, split);
		bch2_cut_back(atomic_end, split);

		bch2_trans_update(&trans, split_iter, split, !remark
				  ? BTREE_TRIGGER_NORUN
				  : BTREE_TRIGGER_NOOVERWRITES);
		bch2_btree_iter_set_pos(iter, split->k.p);
	} while (bkey_cmp(iter->pos, k->k.p) < 0);

	if (remark) {
		ret = bch2_trans_mark_key(&trans, bkey_i_to_s_c(k),
					  0, -((s64) k->k.size),
					  BTREE_TRIGGER_OVERWRITE);
		if (ret)
			goto err;
	}

	ret = bch2_trans_commit(&trans, &disk_res, NULL,
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_LAZY_RW|
				BTREE_INSERT_JOURNAL_REPLAY);
err:
	if (ret == -EINTR)
		goto retry;

	bch2_disk_reservation_put(c, &disk_res);

	return bch2_trans_exit(&trans) ?: ret;
}

static int __bch2_journal_replay_key(struct btree_trans *trans,
				     enum btree_id id, struct bkey_i *k)
{
	struct btree_iter *iter;

	iter = bch2_trans_get_iter(trans, id, bkey_start_pos(&k->k),
				   BTREE_ITER_INTENT);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	bch2_trans_update(trans, iter, k, BTREE_TRIGGER_NORUN);
	return 0;
}

static int bch2_journal_replay_key(struct bch_fs *c, enum btree_id id,
				   struct bkey_i *k)
{
	return bch2_trans_do(c, NULL, NULL,
			     BTREE_INSERT_NOFAIL|
			     BTREE_INSERT_LAZY_RW|
			     BTREE_INSERT_JOURNAL_REPLAY,
			     __bch2_journal_replay_key(&trans, id, k));
}

static int bch2_journal_replay(struct bch_fs *c,
			       struct journal_keys keys)
{
	struct journal *j = &c->journal;
	struct journal_key *i;
	int ret;

	sort(keys.d, keys.nr, sizeof(keys.d[0]), journal_sort_seq_cmp, NULL);

	for_each_journal_key(keys, i) {
		replay_now_at(j, keys.journal_seq_base + i->journal_seq);

		if (i->btree_id == BTREE_ID_ALLOC)
			ret = bch2_alloc_replay_key(c, i->k);
		else if (btree_node_type_is_extents(i->btree_id))
			ret = bch2_extent_replay_key(c, i->btree_id, i->k);
		else
			ret = bch2_journal_replay_key(c, i->btree_id, i->k);

		if (ret) {
			bch_err(c, "journal replay: error %d while replaying key",
				ret);
			return ret;
		}

		cond_resched();
	}

	replay_now_at(j, j->replay_journal_seq_end);
	j->replay_journal_seq = 0;

	bch2_journal_set_replay_done(j);
	bch2_journal_flush_all_pins(j);
	return bch2_journal_error(j);
}

static bool journal_empty(struct list_head *journal)
{
	return list_empty(journal) ||
		journal_entry_empty(&list_last_entry(journal,
					struct journal_replay, list)->j);
}

static int
verify_journal_entries_not_blacklisted_or_missing(struct bch_fs *c,
						  struct list_head *journal)
{
	struct journal_replay *i =
		list_last_entry(journal, struct journal_replay, list);
	u64 start_seq	= le64_to_cpu(i->j.last_seq);
	u64 end_seq	= le64_to_cpu(i->j.seq);
	u64 seq		= start_seq;
	int ret = 0;

	list_for_each_entry(i, journal, list) {
		fsck_err_on(seq != le64_to_cpu(i->j.seq), c,
			"journal entries %llu-%llu missing! (replaying %llu-%llu)",
			seq, le64_to_cpu(i->j.seq) - 1,
			start_seq, end_seq);

		seq = le64_to_cpu(i->j.seq);

		fsck_err_on(bch2_journal_seq_is_blacklisted(c, seq, false), c,
			    "found blacklisted journal entry %llu", seq);

		do {
			seq++;
		} while (bch2_journal_seq_is_blacklisted(c, seq, false));
	}
fsck_err:
	return ret;
}

/* journal replay early: */

static int journal_replay_entry_early(struct bch_fs *c,
				      struct jset_entry *entry)
{
	int ret = 0;

	switch (entry->type) {
	case BCH_JSET_ENTRY_btree_root: {
		struct btree_root *r;

		if (entry->btree_id >= BTREE_ID_NR) {
			bch_err(c, "filesystem has unknown btree type %u",
				entry->btree_id);
			return -EINVAL;
		}

		r = &c->btree_roots[entry->btree_id];

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
				c->usage_base->persistent_reserved[entry->level] =
					le64_to_cpu(u->v);
			break;
		case FS_USAGE_INODES:
			c->usage_base->nr_inodes = le64_to_cpu(u->v);
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
	case BCH_JSET_ENTRY_blacklist: {
		struct jset_entry_blacklist *bl_entry =
			container_of(entry, struct jset_entry_blacklist, entry);

		ret = bch2_journal_seq_blacklist_add(c,
				le64_to_cpu(bl_entry->seq),
				le64_to_cpu(bl_entry->seq) + 1);
		break;
	}
	case BCH_JSET_ENTRY_blacklist_v2: {
		struct jset_entry_blacklist_v2 *bl_entry =
			container_of(entry, struct jset_entry_blacklist_v2, entry);

		ret = bch2_journal_seq_blacklist_add(c,
				le64_to_cpu(bl_entry->start),
				le64_to_cpu(bl_entry->end) + 1);
		break;
	}
	}

	return ret;
}

static int journal_replay_early(struct bch_fs *c,
				struct bch_sb_field_clean *clean,
				struct list_head *journal)
{
	struct jset_entry *entry;
	int ret;

	if (clean) {
		c->bucket_clock[READ].hand = le16_to_cpu(clean->read_clock);
		c->bucket_clock[WRITE].hand = le16_to_cpu(clean->write_clock);

		for (entry = clean->start;
		     entry != vstruct_end(&clean->field);
		     entry = vstruct_next(entry)) {
			ret = journal_replay_entry_early(c, entry);
			if (ret)
				return ret;
		}
	} else {
		struct journal_replay *i =
			list_last_entry(journal, struct journal_replay, list);

		c->bucket_clock[READ].hand = le16_to_cpu(i->j.read_clock);
		c->bucket_clock[WRITE].hand = le16_to_cpu(i->j.write_clock);

		list_for_each_entry(i, journal, list)
			vstruct_for_each(&i->j, entry) {
				ret = journal_replay_entry_early(c, entry);
				if (ret)
					return ret;
			}
	}

	bch2_fs_usage_initialize(c);

	return 0;
}

/* sb clean section: */

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

static int verify_superblock_clean(struct bch_fs *c,
				   struct bch_sb_field_clean **cleanp,
				   struct jset *j)
{
	unsigned i;
	struct bch_sb_field_clean *clean = *cleanp;
	int ret = 0;

	if (!c->sb.clean || !j)
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

static struct bch_sb_field_clean *read_superblock_clean(struct bch_fs *c)
{
	struct bch_sb_field_clean *clean, *sb_clean;
	int ret;

	mutex_lock(&c->sb_lock);
	sb_clean = bch2_sb_get_clean(c->disk_sb.sb);

	if (fsck_err_on(!sb_clean, c,
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
		return ERR_PTR(-ENOMEM);
	}

	if (le16_to_cpu(c->disk_sb.sb->version) <
	    bcachefs_metadata_version_bkey_renumber)
		bch2_sb_clean_renumber(clean, READ);

	mutex_unlock(&c->sb_lock);

	return clean;
fsck_err:
	mutex_unlock(&c->sb_lock);
	return ERR_PTR(ret);
}

static int read_btree_roots(struct bch_fs *c)
{
	unsigned i;
	int ret = 0;

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct btree_root *r = &c->btree_roots[i];

		if (!r->alive)
			continue;

		if (i == BTREE_ID_ALLOC &&
		    c->opts.reconstruct_alloc) {
			c->sb.compat &= ~(1ULL << BCH_COMPAT_FEAT_ALLOC_INFO);
			continue;
		}


		if (r->error) {
			__fsck_err(c, i == BTREE_ID_ALLOC
				   ? FSCK_CAN_IGNORE : 0,
				   "invalid btree root %s",
				   bch2_btree_ids[i]);
			if (i == BTREE_ID_ALLOC)
				c->sb.compat &= ~(1ULL << BCH_COMPAT_FEAT_ALLOC_INFO);
		}

		ret = bch2_btree_root_read(c, i, &r->key, r->level);
		if (ret) {
			__fsck_err(c, i == BTREE_ID_ALLOC
				   ? FSCK_CAN_IGNORE : 0,
				   "error reading btree root %s",
				   bch2_btree_ids[i]);
			if (i == BTREE_ID_ALLOC)
				c->sb.compat &= ~(1ULL << BCH_COMPAT_FEAT_ALLOC_INFO);
		}
	}

	for (i = 0; i < BTREE_ID_NR; i++)
		if (!c->btree_roots[i].b)
			bch2_btree_root_alloc(c, i);
fsck_err:
	return ret;
}

int bch2_fs_recovery(struct bch_fs *c)
{
	const char *err = "cannot allocate memory";
	struct bch_sb_field_clean *clean = NULL;
	u64 journal_seq;
	LIST_HEAD(journal_entries);
	struct journal_keys journal_keys = { NULL };
	bool wrote = false, write_sb = false;
	int ret;

	if (c->sb.clean)
		clean = read_superblock_clean(c);
	ret = PTR_ERR_OR_ZERO(clean);
	if (ret)
		goto err;

	if (c->sb.clean)
		bch_info(c, "recovering from clean shutdown, journal seq %llu",
			 le64_to_cpu(clean->journal_seq));

	if (!c->replicas.entries) {
		bch_info(c, "building replicas info");
		set_bit(BCH_FS_REBUILD_REPLICAS, &c->flags);
	}

	if (!c->sb.clean || c->opts.fsck) {
		struct jset *j;

		ret = bch2_journal_read(c, &journal_entries);
		if (ret)
			goto err;

		if (mustfix_fsck_err_on(c->sb.clean && !journal_empty(&journal_entries), c,
				"filesystem marked clean but journal not empty")) {
			c->sb.compat &= ~(1ULL << BCH_COMPAT_FEAT_ALLOC_INFO);
			SET_BCH_SB_CLEAN(c->disk_sb.sb, false);
			c->sb.clean = false;
		}

		if (!c->sb.clean && list_empty(&journal_entries)) {
			bch_err(c, "no journal entries found");
			ret = BCH_FSCK_REPAIR_IMPOSSIBLE;
			goto err;
		}

		journal_keys = journal_keys_sort(&journal_entries);
		if (!journal_keys.d) {
			ret = -ENOMEM;
			goto err;
		}

		j = &list_last_entry(&journal_entries,
				     struct journal_replay, list)->j;

		ret = verify_superblock_clean(c, &clean, j);
		if (ret)
			goto err;

		journal_seq = le64_to_cpu(j->seq) + 1;
	} else {
		journal_seq = le64_to_cpu(clean->journal_seq) + 1;
	}

	ret = journal_replay_early(c, clean, &journal_entries);
	if (ret)
		goto err;

	if (!c->sb.clean) {
		ret = bch2_journal_seq_blacklist_add(c,
						     journal_seq,
						     journal_seq + 4);
		if (ret) {
			bch_err(c, "error creating new journal seq blacklist entry");
			goto err;
		}

		journal_seq += 4;
	}

	ret = bch2_blacklist_table_initialize(c);

	if (!list_empty(&journal_entries)) {
		ret = verify_journal_entries_not_blacklisted_or_missing(c,
							&journal_entries);
		if (ret)
			goto err;
	}

	ret = bch2_fs_journal_start(&c->journal, journal_seq,
				    &journal_entries);
	if (ret)
		goto err;

	ret = read_btree_roots(c);
	if (ret)
		goto err;

	bch_verbose(c, "starting alloc read");
	err = "error reading allocation information";
	ret = bch2_alloc_read(c, &journal_keys);
	if (ret)
		goto err;
	bch_verbose(c, "alloc read done");

	bch_verbose(c, "starting stripes_read");
	err = "error reading stripes";
	ret = bch2_stripes_read(c, &journal_keys);
	if (ret)
		goto err;
	bch_verbose(c, "stripes_read done");

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);

	if ((c->sb.compat & (1ULL << BCH_COMPAT_FEAT_ALLOC_INFO)) &&
	    !(c->sb.compat & (1ULL << BCH_COMPAT_FEAT_ALLOC_METADATA))) {
		/*
		 * interior btree node updates aren't consistent with the
		 * journal; after an unclean shutdown we have to walk all
		 * pointers to metadata:
		 */
		bch_info(c, "starting metadata mark and sweep");
		err = "error in mark and sweep";
		ret = bch2_gc(c, NULL, true, true);
		if (ret)
			goto err;
		bch_verbose(c, "mark and sweep done");
	}

	if (c->opts.fsck ||
	    !(c->sb.compat & (1ULL << BCH_COMPAT_FEAT_ALLOC_INFO)) ||
	    test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags)) {
		bch_info(c, "starting mark and sweep");
		err = "error in mark and sweep";
		ret = bch2_gc(c, &journal_keys, true, false);
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

	if (c->opts.norecovery)
		goto out;

	bch_verbose(c, "starting journal replay");
	err = "journal replay failed";
	ret = bch2_journal_replay(c, journal_keys);
	if (ret)
		goto err;
	bch_verbose(c, "journal replay done");

	if (!c->opts.nochanges) {
		/*
		 * note that even when filesystem was clean there might be work
		 * to do here, if we ran gc (because of fsck) which recalculated
		 * oldest_gen:
		 */
		bch_verbose(c, "writing allocation info");
		err = "error writing out alloc info";
		ret = bch2_stripes_write(c, BTREE_INSERT_LAZY_RW, &wrote) ?:
			bch2_alloc_write(c, BTREE_INSERT_LAZY_RW, &wrote);
		if (ret) {
			bch_err(c, "error writing alloc info");
			goto err;
		}
		bch_verbose(c, "alloc write done");

		set_bit(BCH_FS_ALLOC_WRITTEN, &c->flags);
	}

	if (!c->sb.clean) {
		if (!(c->sb.features & (1 << BCH_FEATURE_atomic_nlink))) {
			bch_info(c, "checking inode link counts");
			err = "error in recovery";
			ret = bch2_fsck_inode_nlink(c);
			if (ret)
				goto err;
			bch_verbose(c, "check inodes done");

		} else {
			bch_verbose(c, "checking for deleted inodes");
			err = "error in recovery";
			ret = bch2_fsck_walk_inodes_only(c);
			if (ret)
				goto err;
			bch_verbose(c, "check inodes done");
		}
	}

	if (c->opts.fsck) {
		bch_info(c, "starting fsck");
		err = "error in fsck";
		ret = bch2_fsck_full(c);
		if (ret)
			goto err;
		bch_verbose(c, "fsck done");
	}

	if (enabled_qtypes(c)) {
		bch_verbose(c, "reading quotas");
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
		bch_verbose(c, "quotas done");
	}

	mutex_lock(&c->sb_lock);
	if (c->opts.version_upgrade) {
		if (c->sb.version < bcachefs_metadata_version_new_versioning)
			c->disk_sb.sb->version_min =
				le16_to_cpu(bcachefs_metadata_version_min);
		c->disk_sb.sb->version = le16_to_cpu(bcachefs_metadata_version_current);
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_new_siphash;
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_new_extent_overwrite;
		write_sb = true;
	}

	if (!test_bit(BCH_FS_ERROR, &c->flags)) {
		c->disk_sb.sb->compat[0] |= 1ULL << BCH_COMPAT_FEAT_ALLOC_INFO;
		write_sb = true;
	}

	if (c->opts.fsck &&
	    !test_bit(BCH_FS_ERROR, &c->flags)) {
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_atomic_nlink;
		SET_BCH_SB_HAS_ERRORS(c->disk_sb.sb, 0);
		write_sb = true;
	}

	if (write_sb)
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (c->journal_seq_blacklist_table &&
	    c->journal_seq_blacklist_table->nr > 128)
		queue_work(system_long_wq, &c->journal_seq_blacklist_gc_work);
out:
	ret = 0;
err:
fsck_err:
	set_bit(BCH_FS_FSCK_DONE, &c->flags);
	bch2_flush_fsck_errs(c);

	journal_keys_free(&journal_keys);
	journal_entries_free(&journal_entries);
	kfree(clean);
	if (ret)
		bch_err(c, "Error in recovery: %s (%i)", err, ret);
	else
		bch_verbose(c, "ret %i", ret);
	return ret;
}

int bch2_fs_initialize(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	struct bkey_inode_buf packed_inode;
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
	bch2_fs_journal_start(&c->journal, 1, &journal);
	bch2_journal_set_replay_done(&c->journal);

	bch2_inode_init(c, &root_inode, 0, 0,
			S_IFDIR|S_IRWXU|S_IRUGO|S_IXUGO, 0, NULL);
	root_inode.bi_inum = BCACHEFS_ROOT_INO;
	bch2_inode_pack(&packed_inode, &root_inode);

	err = "error creating root directory";
	ret = bch2_btree_insert(c, BTREE_ID_INODES,
				&packed_inode.inode.k_i,
				NULL, NULL, BTREE_INSERT_LAZY_RW);
	if (ret)
		goto err;

	bch2_inode_init_early(c, &lostfound_inode);

	err = "error creating lost+found";
	ret = bch2_trans_do(c, NULL, NULL, 0,
		bch2_create_trans(&trans, BCACHEFS_ROOT_INO,
				  &root_inode, &lostfound_inode,
				  &lostfound,
				  0, 0, S_IFDIR|0700, 0,
				  NULL, NULL));
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
	c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_atomic_nlink;
	c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_new_siphash;
	c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_new_extent_overwrite;

	SET_BCH_SB_INITIALIZED(c->disk_sb.sb, true);
	SET_BCH_SB_CLEAN(c->disk_sb.sb, false);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
err:
	pr_err("Error initializing new filesystem: %s (%i)", err, ret);
	return ret;
}
