// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "bkey_buf.h"
#include "btree_journal_iter.h"
#include "btree_node_scan.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "buckets.h"
#include "dirent.h"
#include "disk_accounting.h"
#include "errcode.h"
#include "error.h"
#include "fs-common.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "logged_ops.h"
#include "move.h"
#include "quota.h"
#include "rebalance.h"
#include "recovery.h"
#include "recovery_passes.h"
#include "replicas.h"
#include "sb-clean.h"
#include "sb-downgrade.h"
#include "snapshot.h"
#include "super-io.h"

#include <linux/sort.h>
#include <linux/stat.h>

void bch2_btree_lost_data(struct bch_fs *c, enum btree_id btree)
{
	if (btree >= BTREE_ID_NR_MAX)
		return;

	u64 b = BIT_ULL(btree);

	if (!(c->sb.btrees_lost_data & b)) {
		bch_err(c, "flagging btree %s lost data", bch2_btree_id_str(btree));

		mutex_lock(&c->sb_lock);
		bch2_sb_field_get(c->disk_sb.sb, ext)->btrees_lost_data |= cpu_to_le64(b);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}
}

/* for -o reconstruct_alloc: */
static void bch2_reconstruct_alloc(struct bch_fs *c)
{
	bch2_journal_log_msg(c, "dropping alloc info");
	bch_info(c, "dropping and reconstructing all alloc info");

	mutex_lock(&c->sb_lock);
	struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);

	__set_bit_le64(BCH_RECOVERY_PASS_STABLE_check_allocations, ext->recovery_passes_required);
	__set_bit_le64(BCH_RECOVERY_PASS_STABLE_check_alloc_info, ext->recovery_passes_required);
	__set_bit_le64(BCH_RECOVERY_PASS_STABLE_check_lrus, ext->recovery_passes_required);
	__set_bit_le64(BCH_RECOVERY_PASS_STABLE_check_extents_to_backpointers, ext->recovery_passes_required);
	__set_bit_le64(BCH_RECOVERY_PASS_STABLE_check_alloc_to_lru_refs, ext->recovery_passes_required);

	__set_bit_le64(BCH_FSCK_ERR_ptr_to_missing_alloc_key, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_ptr_gen_newer_than_bucket_gen, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_stale_dirty_ptr, ext->errors_silent);

	__set_bit_le64(BCH_FSCK_ERR_dev_usage_buckets_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_dev_usage_sectors_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_dev_usage_fragmented_wrong, ext->errors_silent);

	__set_bit_le64(BCH_FSCK_ERR_fs_usage_btree_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_fs_usage_cached_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_fs_usage_persistent_reserved_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_fs_usage_replicas_wrong, ext->errors_silent);

	__set_bit_le64(BCH_FSCK_ERR_alloc_key_data_type_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_alloc_key_gen_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_alloc_key_dirty_sectors_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_alloc_key_cached_sectors_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_alloc_key_stripe_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_alloc_key_stripe_redundancy_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_need_discard_key_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_freespace_key_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_bucket_gens_key_wrong, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_freespace_hole_missing, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_ptr_to_missing_backpointer, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_lru_entry_bad, ext->errors_silent);
	__set_bit_le64(BCH_FSCK_ERR_accounting_mismatch, ext->errors_silent);
	c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);

	c->opts.recovery_passes |= bch2_recovery_passes_from_stable(le64_to_cpu(ext->recovery_passes_required[0]));

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	bch2_shoot_down_journal_keys(c, BTREE_ID_alloc,
				     0, BTREE_MAX_DEPTH, POS_MIN, SPOS_MAX);
	bch2_shoot_down_journal_keys(c, BTREE_ID_backpointers,
				     0, BTREE_MAX_DEPTH, POS_MIN, SPOS_MAX);
	bch2_shoot_down_journal_keys(c, BTREE_ID_need_discard,
				     0, BTREE_MAX_DEPTH, POS_MIN, SPOS_MAX);
	bch2_shoot_down_journal_keys(c, BTREE_ID_freespace,
				     0, BTREE_MAX_DEPTH, POS_MIN, SPOS_MAX);
	bch2_shoot_down_journal_keys(c, BTREE_ID_bucket_gens,
				     0, BTREE_MAX_DEPTH, POS_MIN, SPOS_MAX);
}

/*
 * Btree node pointers have a field to stack a pointer to the in memory btree
 * node; we need to zero out this field when reading in btree nodes, or when
 * reading in keys from the journal:
 */
static void zero_out_btree_mem_ptr(struct journal_keys *keys)
{
	darray_for_each(*keys, i)
		if (i->k->k.type == KEY_TYPE_btree_ptr_v2)
			bkey_i_to_btree_ptr_v2(i->k)->v.mem_ptr = 0;
}

/* journal replay: */

static void replay_now_at(struct journal *j, u64 seq)
{
	BUG_ON(seq < j->replay_journal_seq);

	seq = min(seq, j->replay_journal_seq_end);

	while (j->replay_journal_seq < seq)
		bch2_journal_pin_put(j, j->replay_journal_seq++);
}

static int bch2_journal_replay_accounting_key(struct btree_trans *trans,
					      struct journal_key *k)
{
	struct btree_iter iter;
	bch2_trans_node_iter_init(trans, &iter, k->btree_id, k->k->k.p,
				  BTREE_MAX_DEPTH, k->level,
				  BTREE_ITER_intent);
	int ret = bch2_btree_iter_traverse(&iter);
	if (ret)
		goto out;

	struct bkey u;
	struct bkey_s_c old = bch2_btree_path_peek_slot(btree_iter_path(trans, &iter), &u);

	/* Has this delta already been applied to the btree? */
	if (bversion_cmp(old.k->bversion, k->k->k.bversion) >= 0) {
		ret = 0;
		goto out;
	}

	struct bkey_i *new = k->k;
	if (old.k->type == KEY_TYPE_accounting) {
		new = bch2_bkey_make_mut_noupdate(trans, bkey_i_to_s_c(k->k));
		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			goto out;

		bch2_accounting_accumulate(bkey_i_to_accounting(new),
					   bkey_s_c_to_accounting(old));
	}

	trans->journal_res.seq = k->journal_seq;

	ret = bch2_trans_update(trans, &iter, new, BTREE_TRIGGER_norun);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_journal_replay_key(struct btree_trans *trans,
				   struct journal_key *k)
{
	struct btree_iter iter;
	unsigned iter_flags =
		BTREE_ITER_intent|
		BTREE_ITER_not_extents;
	unsigned update_flags = BTREE_TRIGGER_norun;
	int ret;

	if (k->overwritten)
		return 0;

	trans->journal_res.seq = k->journal_seq;

	/*
	 * BTREE_UPDATE_key_cache_reclaim disables key cache lookup/update to
	 * keep the key cache coherent with the underlying btree. Nothing
	 * besides the allocator is doing updates yet so we don't need key cache
	 * coherency for non-alloc btrees, and key cache fills for snapshots
	 * btrees use BTREE_ITER_filter_snapshots, which isn't available until
	 * the snapshots recovery pass runs.
	 */
	if (!k->level && k->btree_id == BTREE_ID_alloc)
		iter_flags |= BTREE_ITER_cached;
	else
		update_flags |= BTREE_UPDATE_key_cache_reclaim;

	bch2_trans_node_iter_init(trans, &iter, k->btree_id, k->k->k.p,
				  BTREE_MAX_DEPTH, k->level,
				  iter_flags);
	ret = bch2_btree_iter_traverse(&iter);
	if (ret)
		goto out;

	struct btree_path *path = btree_iter_path(trans, &iter);
	if (unlikely(!btree_path_node(path, k->level))) {
		bch2_trans_iter_exit(trans, &iter);
		bch2_trans_node_iter_init(trans, &iter, k->btree_id, k->k->k.p,
					  BTREE_MAX_DEPTH, 0, iter_flags);
		ret =   bch2_btree_iter_traverse(&iter) ?:
			bch2_btree_increase_depth(trans, iter.path, 0) ?:
			-BCH_ERR_transaction_restart_nested;
		goto out;
	}

	/* Must be checked with btree locked: */
	if (k->overwritten)
		goto out;

	if (k->k->k.type == KEY_TYPE_accounting) {
		ret = bch2_trans_update_buffered(trans, BTREE_ID_accounting, k->k);
		goto out;
	}

	ret = bch2_trans_update(trans, &iter, k->k, update_flags);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int journal_sort_seq_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = *((const struct journal_key **)_l);
	const struct journal_key *r = *((const struct journal_key **)_r);

	/*
	 * Map 0 to U64_MAX, so that keys with journal_seq === 0 come last
	 *
	 * journal_seq == 0 means that the key comes from early repair, and
	 * should be inserted last so as to avoid overflowing the journal
	 */
	return cmp_int(l->journal_seq - 1, r->journal_seq - 1);
}

int bch2_journal_replay(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;
	DARRAY(struct journal_key *) keys_sorted = { 0 };
	struct journal *j = &c->journal;
	u64 start_seq	= c->journal_replay_seq_start;
	u64 end_seq	= c->journal_replay_seq_start;
	struct btree_trans *trans = NULL;
	bool immediate_flush = false;
	int ret = 0;

	if (keys->nr) {
		ret = bch2_journal_log_msg(c, "Starting journal replay (%zu keys in entries %llu-%llu)",
					   keys->nr, start_seq, end_seq);
		if (ret)
			goto err;
	}

	BUG_ON(!atomic_read(&keys->ref));

	move_gap(keys, keys->nr);
	trans = bch2_trans_get(c);

	/*
	 * Replay accounting keys first: we can't allow the write buffer to
	 * flush accounting keys until we're done
	 */
	darray_for_each(*keys, k) {
		if (!(k->k->k.type == KEY_TYPE_accounting && !k->allocated))
			continue;

		cond_resched();

		ret = commit_do(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc|
				BCH_TRANS_COMMIT_journal_reclaim|
				BCH_TRANS_COMMIT_skip_accounting_apply|
				BCH_TRANS_COMMIT_no_journal_res|
				BCH_WATERMARK_reclaim,
			     bch2_journal_replay_accounting_key(trans, k));
		if (bch2_fs_fatal_err_on(ret, c, "error replaying accounting; %s", bch2_err_str(ret)))
			goto err;

		k->overwritten = true;
	}

	set_bit(BCH_FS_accounting_replay_done, &c->flags);

	/*
	 * First, attempt to replay keys in sorted order. This is more
	 * efficient - better locality of btree access -  but some might fail if
	 * that would cause a journal deadlock.
	 */
	darray_for_each(*keys, k) {
		cond_resched();

		/*
		 * k->allocated means the key wasn't read in from the journal,
		 * rather it was from early repair code
		 */
		if (k->allocated)
			immediate_flush = true;

		/* Skip fastpath if we're low on space in the journal */
		ret = c->journal.watermark ? -1 :
			commit_do(trans, NULL, NULL,
				  BCH_TRANS_COMMIT_no_enospc|
				  BCH_TRANS_COMMIT_journal_reclaim|
				  BCH_TRANS_COMMIT_skip_accounting_apply|
				  (!k->allocated ? BCH_TRANS_COMMIT_no_journal_res : 0),
			     bch2_journal_replay_key(trans, k));
		BUG_ON(!ret && !k->overwritten && k->k->k.type != KEY_TYPE_accounting);
		if (ret) {
			ret = darray_push(&keys_sorted, k);
			if (ret)
				goto err;
		}
	}

	bch2_trans_unlock_long(trans);
	/*
	 * Now, replay any remaining keys in the order in which they appear in
	 * the journal, unpinning those journal entries as we go:
	 */
	sort(keys_sorted.data, keys_sorted.nr,
	     sizeof(keys_sorted.data[0]),
	     journal_sort_seq_cmp, NULL);

	darray_for_each(keys_sorted, kp) {
		cond_resched();

		struct journal_key *k = *kp;

		if (k->journal_seq)
			replay_now_at(j, k->journal_seq);
		else
			replay_now_at(j, j->replay_journal_seq_end);

		ret = commit_do(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc|
				BCH_TRANS_COMMIT_skip_accounting_apply|
				(!k->allocated
				 ? BCH_TRANS_COMMIT_no_journal_res|BCH_WATERMARK_reclaim
				 : 0),
			     bch2_journal_replay_key(trans, k));
		bch_err_msg(c, ret, "while replaying key at btree %s level %u:",
			    bch2_btree_id_str(k->btree_id), k->level);
		if (ret)
			goto err;

		BUG_ON(k->btree_id != BTREE_ID_accounting && !k->overwritten);
	}

	/*
	 * We need to put our btree_trans before calling flush_all_pins(), since
	 * that will use a btree_trans internally
	 */
	bch2_trans_put(trans);
	trans = NULL;

	if (!c->opts.retain_recovery_info &&
	    c->recovery_pass_done >= BCH_RECOVERY_PASS_journal_replay)
		bch2_journal_keys_put_initial(c);

	replay_now_at(j, j->replay_journal_seq_end);
	j->replay_journal_seq = 0;

	bch2_journal_set_replay_done(j);

	/* if we did any repair, flush it immediately */
	if (immediate_flush) {
		bch2_journal_flush_all_pins(&c->journal);
		ret = bch2_journal_meta(&c->journal);
	}

	if (keys->nr)
		bch2_journal_log_msg(c, "journal replay finished");
err:
	if (trans)
		bch2_trans_put(trans);
	darray_exit(&keys_sorted);
	bch_err_fn(c, ret);
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

		if (fsck_err_on(entry->btree_id >= BTREE_ID_NR_MAX,
				c, invalid_btree_id,
				"invalid btree id %u (max %u)",
				entry->btree_id, BTREE_ID_NR_MAX))
			return 0;

		while (entry->btree_id >= c->btree_roots_extra.nr + BTREE_ID_NR) {
			ret = darray_push(&c->btree_roots_extra, (struct btree_root) { NULL });
			if (ret)
				return ret;
		}

		r = bch2_btree_id_root(c, entry->btree_id);

		if (entry->u64s) {
			r->level = entry->level;
			bkey_copy(&r->key, (struct bkey_i *) entry->start);
			r->error = 0;
		} else {
			r->error = -BCH_ERR_btree_node_read_error;
		}
		r->alive = true;
		break;
	}
	case BCH_JSET_ENTRY_usage: {
		struct jset_entry_usage *u =
			container_of(entry, struct jset_entry_usage, entry);

		switch (entry->btree_id) {
		case BCH_FS_USAGE_key_version:
			atomic64_set(&c->key_version, le64_to_cpu(u->v));
			break;
		}
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
	case BCH_JSET_ENTRY_clock: {
		struct jset_entry_clock *clock =
			container_of(entry, struct jset_entry_clock, entry);

		atomic64_set(&c->io_clock[clock->rw].now, le64_to_cpu(clock->time));
	}
	}
fsck_err:
	return ret;
}

static int journal_replay_early(struct bch_fs *c,
				struct bch_sb_field_clean *clean)
{
	if (clean) {
		for (struct jset_entry *entry = clean->start;
		     entry != vstruct_end(&clean->field);
		     entry = vstruct_next(entry)) {
			int ret = journal_replay_entry_early(c, entry);
			if (ret)
				return ret;
		}
	} else {
		struct genradix_iter iter;
		struct journal_replay *i, **_i;

		genradix_for_each(&c->journal_entries, iter, _i) {
			i = *_i;

			if (journal_replay_ignore(i))
				continue;

			vstruct_for_each(&i->j, entry) {
				int ret = journal_replay_entry_early(c, entry);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

/* sb clean section: */

static int read_btree_roots(struct bch_fs *c)
{
	int ret = 0;

	for (unsigned i = 0; i < btree_id_nr_alive(c); i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (!r->alive)
			continue;

		if (btree_id_is_alloc(i) && c->opts.reconstruct_alloc)
			continue;

		if (mustfix_fsck_err_on((ret = r->error),
					c, btree_root_bkey_invalid,
					"invalid btree root %s",
					bch2_btree_id_str(i)) ||
		    mustfix_fsck_err_on((ret = r->error = bch2_btree_root_read(c, i, &r->key, r->level)),
					c, btree_root_read_error,
					"error reading btree root %s l=%u: %s",
					bch2_btree_id_str(i), r->level, bch2_err_str(ret))) {
			if (btree_id_is_alloc(i)) {
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_allocations);
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_alloc_info);
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_lrus);
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_extents_to_backpointers);
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_alloc_to_lru_refs);
				c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
				r->error = 0;
			} else if (!(c->opts.recovery_passes & BIT_ULL(BCH_RECOVERY_PASS_scan_for_btree_nodes))) {
				bch_info(c, "will run btree node scan");
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_scan_for_btree_nodes);
				c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_topology);
			}

			ret = 0;
			bch2_btree_lost_data(c, i);
		}
	}

	for (unsigned i = 0; i < BTREE_ID_NR; i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (!r->b && !r->error) {
			r->alive = false;
			r->level = 0;
			bch2_btree_root_alloc_fake(c, i, 0);
		}
	}
fsck_err:
	return ret;
}

static bool check_version_upgrade(struct bch_fs *c)
{
	unsigned latest_version	= bcachefs_metadata_version_current;
	unsigned latest_compatible = min(latest_version,
					 bch2_latest_compatible_version(c->sb.version));
	unsigned old_version = c->sb.version_upgrade_complete ?: c->sb.version;
	unsigned new_version = 0;

	if (old_version < bcachefs_metadata_required_upgrade_below) {
		if (c->opts.version_upgrade == BCH_VERSION_UPGRADE_incompatible ||
		    latest_compatible < bcachefs_metadata_required_upgrade_below)
			new_version = latest_version;
		else
			new_version = latest_compatible;
	} else {
		switch (c->opts.version_upgrade) {
		case BCH_VERSION_UPGRADE_compatible:
			new_version = latest_compatible;
			break;
		case BCH_VERSION_UPGRADE_incompatible:
			new_version = latest_version;
			break;
		case BCH_VERSION_UPGRADE_none:
			new_version = min(old_version, latest_version);
			break;
		}
	}

	if (new_version > old_version) {
		struct printbuf buf = PRINTBUF;

		if (old_version < bcachefs_metadata_required_upgrade_below)
			prt_str(&buf, "Version upgrade required:\n");

		if (old_version != c->sb.version) {
			prt_str(&buf, "Version upgrade from ");
			bch2_version_to_text(&buf, c->sb.version_upgrade_complete);
			prt_str(&buf, " to ");
			bch2_version_to_text(&buf, c->sb.version);
			prt_str(&buf, " incomplete\n");
		}

		prt_printf(&buf, "Doing %s version upgrade from ",
			   BCH_VERSION_MAJOR(old_version) != BCH_VERSION_MAJOR(new_version)
			   ? "incompatible" : "compatible");
		bch2_version_to_text(&buf, old_version);
		prt_str(&buf, " to ");
		bch2_version_to_text(&buf, new_version);
		prt_newline(&buf);

		struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);
		__le64 passes = ext->recovery_passes_required[0];
		bch2_sb_set_upgrade(c, old_version, new_version);
		passes = ext->recovery_passes_required[0] & ~passes;

		if (passes) {
			prt_str(&buf, "  running recovery passes: ");
			prt_bitflags(&buf, bch2_recovery_passes,
				     bch2_recovery_passes_from_stable(le64_to_cpu(passes)));
		}

		bch_info(c, "%s", buf.buf);

		bch2_sb_upgrade(c, new_version);

		printbuf_exit(&buf);
		return true;
	}

	return false;
}

int bch2_fs_recovery(struct bch_fs *c)
{
	struct bch_sb_field_clean *clean = NULL;
	struct jset *last_journal_entry = NULL;
	u64 last_seq = 0, blacklist_seq, journal_seq;
	int ret = 0;

	if (c->sb.clean) {
		clean = bch2_read_superblock_clean(c);
		ret = PTR_ERR_OR_ZERO(clean);
		if (ret)
			goto err;

		bch_info(c, "recovering from clean shutdown, journal seq %llu",
			 le64_to_cpu(clean->journal_seq));
	} else {
		bch_info(c, "recovering from unclean shutdown");
	}

	if (!(c->sb.features & (1ULL << BCH_FEATURE_new_extent_overwrite))) {
		bch_err(c, "feature new_extent_overwrite not set, filesystem no longer supported");
		ret = -EINVAL;
		goto err;
	}

	if (!c->sb.clean &&
	    !(c->sb.features & (1ULL << BCH_FEATURE_extents_above_btree_updates))) {
		bch_err(c, "filesystem needs recovery from older version; run fsck from older bcachefs-tools to fix");
		ret = -EINVAL;
		goto err;
	}

	if (c->opts.norecovery)
		c->opts.recovery_pass_last = BCH_RECOVERY_PASS_journal_replay - 1;

	mutex_lock(&c->sb_lock);
	struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);
	bool write_sb = false;

	if (BCH_SB_HAS_TOPOLOGY_ERRORS(c->disk_sb.sb)) {
		ext->recovery_passes_required[0] |=
			cpu_to_le64(bch2_recovery_passes_to_stable(BIT_ULL(BCH_RECOVERY_PASS_check_topology)));
		write_sb = true;
	}

	u64 sb_passes = bch2_recovery_passes_from_stable(le64_to_cpu(ext->recovery_passes_required[0]));
	if (sb_passes) {
		struct printbuf buf = PRINTBUF;
		prt_str(&buf, "superblock requires following recovery passes to be run:\n  ");
		prt_bitflags(&buf, bch2_recovery_passes, sb_passes);
		bch_info(c, "%s", buf.buf);
		printbuf_exit(&buf);
	}

	if (bch2_check_version_downgrade(c)) {
		struct printbuf buf = PRINTBUF;

		prt_str(&buf, "Version downgrade required:");

		__le64 passes = ext->recovery_passes_required[0];
		bch2_sb_set_downgrade(c,
				      BCH_VERSION_MINOR(bcachefs_metadata_version_current),
				      BCH_VERSION_MINOR(c->sb.version));
		passes = ext->recovery_passes_required[0] & ~passes;
		if (passes) {
			prt_str(&buf, "\n  running recovery passes: ");
			prt_bitflags(&buf, bch2_recovery_passes,
				     bch2_recovery_passes_from_stable(le64_to_cpu(passes)));
		}

		bch_info(c, "%s", buf.buf);
		printbuf_exit(&buf);
		write_sb = true;
	}

	if (check_version_upgrade(c))
		write_sb = true;

	c->opts.recovery_passes |= bch2_recovery_passes_from_stable(le64_to_cpu(ext->recovery_passes_required[0]));

	if (write_sb)
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (c->opts.fsck && IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		c->opts.recovery_passes |= BIT_ULL(BCH_RECOVERY_PASS_check_topology);

	if (c->opts.fsck)
		set_bit(BCH_FS_fsck_running, &c->flags);
	if (c->sb.clean)
		set_bit(BCH_FS_clean_recovery, &c->flags);

	ret = bch2_blacklist_table_initialize(c);
	if (ret) {
		bch_err(c, "error initializing blacklist table");
		goto err;
	}

	bch2_journal_pos_from_member_info_resume(c);

	if (!c->sb.clean || c->opts.retain_recovery_info) {
		struct genradix_iter iter;
		struct journal_replay **i;

		bch_verbose(c, "starting journal read");
		ret = bch2_journal_read(c, &last_seq, &blacklist_seq, &journal_seq);
		if (ret)
			goto err;

		/*
		 * note: cmd_list_journal needs the blacklist table fully up to date so
		 * it can asterisk ignored journal entries:
		 */
		if (c->opts.read_journal_only)
			goto out;

		genradix_for_each_reverse(&c->journal_entries, iter, i)
			if (!journal_replay_ignore(*i)) {
				last_journal_entry = &(*i)->j;
				break;
			}

		if (mustfix_fsck_err_on(c->sb.clean &&
					last_journal_entry &&
					!journal_entry_empty(last_journal_entry), c,
				clean_but_journal_not_empty,
				"filesystem marked clean but journal not empty")) {
			c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
			SET_BCH_SB_CLEAN(c->disk_sb.sb, false);
			c->sb.clean = false;
		}

		if (!last_journal_entry) {
			fsck_err_on(!c->sb.clean, c,
				    dirty_but_no_journal_entries,
				    "no journal entries found");
			if (clean)
				goto use_clean;

			genradix_for_each_reverse(&c->journal_entries, iter, i)
				if (*i) {
					last_journal_entry = &(*i)->j;
					(*i)->ignore_blacklisted = false;
					(*i)->ignore_not_dirty= false;
					/*
					 * This was probably a NO_FLUSH entry,
					 * so last_seq was garbage - but we know
					 * we're only using a single journal
					 * entry, set it here:
					 */
					(*i)->j.last_seq = (*i)->j.seq;
					break;
				}
		}

		ret = bch2_journal_keys_sort(c);
		if (ret)
			goto err;

		if (c->sb.clean && last_journal_entry) {
			ret = bch2_verify_superblock_clean(c, &clean,
						      last_journal_entry);
			if (ret)
				goto err;
		}
	} else {
use_clean:
		if (!clean) {
			bch_err(c, "no superblock clean section found");
			ret = -BCH_ERR_fsck_repair_impossible;
			goto err;

		}
		blacklist_seq = journal_seq = le64_to_cpu(clean->journal_seq) + 1;
	}

	c->journal_replay_seq_start	= last_seq;
	c->journal_replay_seq_end	= blacklist_seq - 1;

	if (c->opts.reconstruct_alloc)
		bch2_reconstruct_alloc(c);

	zero_out_btree_mem_ptr(&c->journal_keys);

	ret = journal_replay_early(c, clean);
	if (ret)
		goto err;

	/*
	 * After an unclean shutdown, skip then next few journal sequence
	 * numbers as they may have been referenced by btree writes that
	 * happened before their corresponding journal writes - those btree
	 * writes need to be ignored, by skipping and blacklisting the next few
	 * journal sequence numbers:
	 */
	if (!c->sb.clean)
		journal_seq += 8;

	if (blacklist_seq != journal_seq) {
		ret =   bch2_journal_log_msg(c, "blacklisting entries %llu-%llu",
					     blacklist_seq, journal_seq) ?:
			bch2_journal_seq_blacklist_add(c,
					blacklist_seq, journal_seq);
		if (ret) {
			bch_err_msg(c, ret, "error creating new journal seq blacklist entry");
			goto err;
		}
	}

	ret =   bch2_journal_log_msg(c, "starting journal at entry %llu, replaying %llu-%llu",
				     journal_seq, last_seq, blacklist_seq - 1) ?:
		bch2_fs_journal_start(&c->journal, journal_seq);
	if (ret)
		goto err;

	/*
	 * Skip past versions that might have possibly been used (as nonces),
	 * but hadn't had their pointers written:
	 */
	if (c->sb.encryption_type && !c->sb.clean)
		atomic64_add(1 << 16, &c->key_version);

	ret = read_btree_roots(c);
	if (ret)
		goto err;

	set_bit(BCH_FS_btree_running, &c->flags);

	ret = bch2_sb_set_upgrade_extra(c);

	ret = bch2_run_recovery_passes(c);
	if (ret)
		goto err;

	/*
	 * Normally set by the appropriate recovery pass: when cleared, this
	 * indicates we're in early recovery and btree updates should be done by
	 * being applied to the journal replay keys. _Must_ be cleared before
	 * multithreaded use:
	 */
	set_bit(BCH_FS_may_go_rw, &c->flags);
	clear_bit(BCH_FS_fsck_running, &c->flags);

	/* in case we don't run journal replay, i.e. norecovery mode */
	set_bit(BCH_FS_accounting_replay_done, &c->flags);

	/* fsync if we fixed errors */
	if (test_bit(BCH_FS_errors_fixed, &c->flags) &&
	    bch2_write_ref_tryget(c, BCH_WRITE_REF_fsync)) {
		bch2_journal_flush_all_pins(&c->journal);
		bch2_journal_meta(&c->journal);
		bch2_write_ref_put(c, BCH_WRITE_REF_fsync);
	}

	/* If we fixed errors, verify that fs is actually clean now: */
	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG) &&
	    test_bit(BCH_FS_errors_fixed, &c->flags) &&
	    !test_bit(BCH_FS_errors_not_fixed, &c->flags) &&
	    !test_bit(BCH_FS_error, &c->flags)) {
		bch2_flush_fsck_errs(c);

		bch_info(c, "Fixed errors, running fsck a second time to verify fs is clean");
		clear_bit(BCH_FS_errors_fixed, &c->flags);

		c->curr_recovery_pass = BCH_RECOVERY_PASS_check_alloc_info;

		ret = bch2_run_recovery_passes(c);
		if (ret)
			goto err;

		if (test_bit(BCH_FS_errors_fixed, &c->flags) ||
		    test_bit(BCH_FS_errors_not_fixed, &c->flags)) {
			bch_err(c, "Second fsck run was not clean");
			set_bit(BCH_FS_errors_not_fixed, &c->flags);
		}

		set_bit(BCH_FS_errors_fixed, &c->flags);
	}

	if (enabled_qtypes(c)) {
		bch_verbose(c, "reading quotas");
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
		bch_verbose(c, "quotas done");
	}

	mutex_lock(&c->sb_lock);
	ext = bch2_sb_field_get(c->disk_sb.sb, ext);
	write_sb = false;

	if (BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb) != le16_to_cpu(c->disk_sb.sb->version)) {
		SET_BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb, le16_to_cpu(c->disk_sb.sb->version));
		write_sb = true;
	}

	if (!test_bit(BCH_FS_error, &c->flags) &&
	    !(c->disk_sb.sb->compat[0] & cpu_to_le64(1ULL << BCH_COMPAT_alloc_info))) {
		c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_alloc_info);
		write_sb = true;
	}

	if (!test_bit(BCH_FS_error, &c->flags) &&
	    !bch2_is_zero(ext->errors_silent, sizeof(ext->errors_silent))) {
		memset(ext->errors_silent, 0, sizeof(ext->errors_silent));
		write_sb = true;
	}

	if (c->opts.fsck &&
	    !test_bit(BCH_FS_error, &c->flags) &&
	    c->recovery_pass_done == BCH_RECOVERY_PASS_NR - 1 &&
	    ext->btrees_lost_data) {
		ext->btrees_lost_data = 0;
		write_sb = true;
	}

	if (c->opts.fsck &&
	    !test_bit(BCH_FS_error, &c->flags) &&
	    !test_bit(BCH_FS_errors_not_fixed, &c->flags)) {
		SET_BCH_SB_HAS_ERRORS(c->disk_sb.sb, 0);
		SET_BCH_SB_HAS_TOPOLOGY_ERRORS(c->disk_sb.sb, 0);
		write_sb = true;
	}

	if (bch2_blacklist_entries_gc(c))
		write_sb = true;

	if (write_sb)
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (!(c->sb.compat & (1ULL << BCH_COMPAT_extents_above_btree_updates_done)) ||
	    c->sb.version_min < bcachefs_metadata_version_btree_ptr_sectors_written) {
		struct bch_move_stats stats;

		bch2_move_stats_init(&stats, "recovery");

		struct printbuf buf = PRINTBUF;
		bch2_version_to_text(&buf, c->sb.version_min);
		bch_info(c, "scanning for old btree nodes: min_version %s", buf.buf);
		printbuf_exit(&buf);

		ret =   bch2_fs_read_write_early(c) ?:
			bch2_scan_old_btree_nodes(c, &stats);
		if (ret)
			goto err;
		bch_info(c, "scanning for old btree nodes done");
	}

	ret = 0;
out:
	bch2_flush_fsck_errs(c);

	if (!c->opts.retain_recovery_info) {
		bch2_journal_keys_put_initial(c);
		bch2_find_btree_nodes_exit(&c->found_btree_nodes);
	}
	if (!IS_ERR(clean))
		kfree(clean);

	if (!ret &&
	    test_bit(BCH_FS_need_delete_dead_snapshots, &c->flags) &&
	    !c->opts.nochanges) {
		bch2_fs_read_write_early(c);
		bch2_delete_dead_snapshots_async(c);
	}

	bch_err_fn(c, ret);
	return ret;
err:
fsck_err:
	bch2_fs_emergency_read_only(c);
	goto out;
}

int bch2_fs_initialize(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	struct bkey_inode_buf packed_inode;
	struct qstr lostfound = QSTR("lost+found");
	struct bch_member *m;
	int ret;

	bch_notice(c, "initializing new filesystem");
	set_bit(BCH_FS_new_fs, &c->flags);

	mutex_lock(&c->sb_lock);
	c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_extents_above_btree_updates_done);
	c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_bformat_overflow_done);

	bch2_check_version_downgrade(c);

	if (c->opts.version_upgrade != BCH_VERSION_UPGRADE_none) {
		bch2_sb_upgrade(c, bcachefs_metadata_version_current);
		SET_BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb, bcachefs_metadata_version_current);
		bch2_write_super(c);
	}

	for_each_member_device(c, ca) {
		m = bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
		SET_BCH_MEMBER_FREESPACE_INITIALIZED(m, false);
		ca->mi = bch2_mi_to_cpu(m);
	}

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	c->curr_recovery_pass = BCH_RECOVERY_PASS_NR;
	set_bit(BCH_FS_btree_running, &c->flags);
	set_bit(BCH_FS_may_go_rw, &c->flags);

	for (unsigned i = 0; i < BTREE_ID_NR; i++)
		bch2_btree_root_alloc_fake(c, i, 0);

	ret = bch2_fs_journal_alloc(c);
	if (ret)
		goto err;

	/*
	 * journal_res_get() will crash if called before this has
	 * set up the journal.pin FIFO and journal.cur pointer:
	 */
	bch2_fs_journal_start(&c->journal, 1);
	set_bit(BCH_FS_accounting_replay_done, &c->flags);
	bch2_journal_set_replay_done(&c->journal);

	ret = bch2_fs_read_write_early(c);
	if (ret)
		goto err;

	for_each_member_device(c, ca) {
		ret = bch2_dev_usage_init(ca, false);
		if (ret) {
			bch2_dev_put(ca);
			goto err;
		}
	}

	/*
	 * Write out the superblock and journal buckets, now that we can do
	 * btree updates
	 */
	bch_verbose(c, "marking superblocks");
	ret = bch2_trans_mark_dev_sbs(c);
	bch_err_msg(c, ret, "marking superblocks");
	if (ret)
		goto err;

	for_each_online_member(c, ca)
		ca->new_fs_bucket_idx = 0;

	ret = bch2_fs_freespace_init(c);
	if (ret)
		goto err;

	ret = bch2_initialize_subvolumes(c);
	if (ret)
		goto err;

	bch_verbose(c, "reading snapshots table");
	ret = bch2_snapshots_read(c);
	if (ret)
		goto err;
	bch_verbose(c, "reading snapshots done");

	bch2_inode_init(c, &root_inode, 0, 0, S_IFDIR|0755, 0, NULL);
	root_inode.bi_inum	= BCACHEFS_ROOT_INO;
	root_inode.bi_subvol	= BCACHEFS_ROOT_SUBVOL;
	bch2_inode_pack(&packed_inode, &root_inode);
	packed_inode.inode.k.p.snapshot = U32_MAX;

	ret = bch2_btree_insert(c, BTREE_ID_inodes, &packed_inode.inode.k_i, NULL, 0, 0);
	bch_err_msg(c, ret, "creating root directory");
	if (ret)
		goto err;

	bch2_inode_init_early(c, &lostfound_inode);

	ret = bch2_trans_commit_do(c, NULL, NULL, 0,
		bch2_create_trans(trans,
				  BCACHEFS_ROOT_SUBVOL_INUM,
				  &root_inode, &lostfound_inode,
				  &lostfound,
				  0, 0, S_IFDIR|0700, 0,
				  NULL, NULL, (subvol_inum) { 0 }, 0));
	bch_err_msg(c, ret, "creating lost+found");
	if (ret)
		goto err;

	c->recovery_pass_done = BCH_RECOVERY_PASS_NR - 1;

	if (enabled_qtypes(c)) {
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
	}

	ret = bch2_journal_flush(&c->journal);
	bch_err_msg(c, ret, "writing first journal entry");
	if (ret)
		goto err;

	mutex_lock(&c->sb_lock);
	SET_BCH_SB_INITIALIZED(c->disk_sb.sb, true);
	SET_BCH_SB_CLEAN(c->disk_sb.sb, false);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
err:
	bch_err_fn(c, ret);
	return ret;
}
