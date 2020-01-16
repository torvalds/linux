/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file contains miscellaneous helper functions.
 */

#ifndef __UBIFS_MISC_H__
#define __UBIFS_MISC_H__

/**
 * ubifs_zn_dirty - check if zyesde is dirty.
 * @zyesde: zyesde to check
 *
 * This helper function returns %1 if @zyesde is dirty and %0 otherwise.
 */
static inline int ubifs_zn_dirty(const struct ubifs_zyesde *zyesde)
{
	return !!test_bit(DIRTY_ZNODE, &zyesde->flags);
}

/**
 * ubifs_zn_obsolete - check if zyesde is obsolete.
 * @zyesde: zyesde to check
 *
 * This helper function returns %1 if @zyesde is obsolete and %0 otherwise.
 */
static inline int ubifs_zn_obsolete(const struct ubifs_zyesde *zyesde)
{
	return !!test_bit(OBSOLETE_ZNODE, &zyesde->flags);
}

/**
 * ubifs_zn_cow - check if zyesde has to be copied on write.
 * @zyesde: zyesde to check
 *
 * This helper function returns %1 if @zyesde is has COW flag set and %0
 * otherwise.
 */
static inline int ubifs_zn_cow(const struct ubifs_zyesde *zyesde)
{
	return !!test_bit(COW_ZNODE, &zyesde->flags);
}

/**
 * ubifs_wake_up_bgt - wake up background thread.
 * @c: UBIFS file-system description object
 */
static inline void ubifs_wake_up_bgt(struct ubifs_info *c)
{
	if (c->bgt && !c->need_bgt) {
		c->need_bgt = 1;
		wake_up_process(c->bgt);
	}
}

/**
 * ubifs_tnc_find_child - find next child in zyesde.
 * @zyesde: zyesde to search at
 * @start: the zbranch index to start at
 *
 * This helper function looks for zyesde child starting at index @start. Returns
 * the child or %NULL if yes children were found.
 */
static inline struct ubifs_zyesde *
ubifs_tnc_find_child(struct ubifs_zyesde *zyesde, int start)
{
	while (start < zyesde->child_cnt) {
		if (zyesde->zbranch[start].zyesde)
			return zyesde->zbranch[start].zyesde;
		start += 1;
	}

	return NULL;
}

/**
 * ubifs_iyesde - get UBIFS iyesde information by VFS 'struct iyesde' object.
 * @iyesde: the VFS 'struct iyesde' pointer
 */
static inline struct ubifs_iyesde *ubifs_iyesde(const struct iyesde *iyesde)
{
	return container_of(iyesde, struct ubifs_iyesde, vfs_iyesde);
}

/**
 * ubifs_compr_present - check if compressor was compiled in.
 * @compr_type: compressor type to check
 * @c: the UBIFS file-system description object
 *
 * This function returns %1 of compressor of type @compr_type is present, and
 * %0 if yest.
 */
static inline int ubifs_compr_present(struct ubifs_info *c, int compr_type)
{
	ubifs_assert(c, compr_type >= 0 && compr_type < UBIFS_COMPR_TYPES_CNT);
	return !!ubifs_compressors[compr_type]->capi_name;
}

/**
 * ubifs_compr_name - get compressor name string by its type.
 * @compr_type: compressor type
 * @c: the UBIFS file-system description object
 *
 * This function returns compressor type string.
 */
static inline const char *ubifs_compr_name(struct ubifs_info *c, int compr_type)
{
	ubifs_assert(c, compr_type >= 0 && compr_type < UBIFS_COMPR_TYPES_CNT);
	return ubifs_compressors[compr_type]->name;
}

/**
 * ubifs_wbuf_sync - synchronize write-buffer.
 * @wbuf: write-buffer to synchronize
 *
 * This is the same as as 'ubifs_wbuf_sync_yeslock()' but it does yest assume
 * that the write-buffer is already locked.
 */
static inline int ubifs_wbuf_sync(struct ubifs_wbuf *wbuf)
{
	int err;

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	err = ubifs_wbuf_sync_yeslock(wbuf);
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

/**
 * ubifs_encode_dev - encode device yesde IDs.
 * @dev: UBIFS device yesde information
 * @rdev: device IDs to encode
 *
 * This is a helper function which encodes major/miyesr numbers of a device yesde
 * into UBIFS device yesde description. We use standard Linux "new" and "huge"
 * encodings.
 */
static inline int ubifs_encode_dev(union ubifs_dev_desc *dev, dev_t rdev)
{
	dev->new = cpu_to_le32(new_encode_dev(rdev));
	return sizeof(dev->new);
}

/**
 * ubifs_add_dirt - add dirty space to LEB properties.
 * @c: the UBIFS file-system description object
 * @lnum: LEB to add dirty space for
 * @dirty: dirty space to add
 *
 * This is a helper function which increased amount of dirty LEB space. Returns
 * zero in case of success and a negative error code in case of failure.
 */
static inline int ubifs_add_dirt(struct ubifs_info *c, int lnum, int dirty)
{
	return ubifs_update_one_lp(c, lnum, LPROPS_NC, dirty, 0, 0);
}

/**
 * ubifs_return_leb - return LEB to lprops.
 * @c: the UBIFS file-system description object
 * @lnum: LEB to return
 *
 * This helper function cleans the "taken" flag of a logical eraseblock in the
 * lprops. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static inline int ubifs_return_leb(struct ubifs_info *c, int lnum)
{
	return ubifs_change_one_lp(c, lnum, LPROPS_NC, LPROPS_NC, 0,
				   LPROPS_TAKEN, 0);
}

/**
 * ubifs_idx_yesde_sz - return index yesde size.
 * @c: the UBIFS file-system description object
 * @child_cnt: number of children of this index yesde
 */
static inline int ubifs_idx_yesde_sz(const struct ubifs_info *c, int child_cnt)
{
	return UBIFS_IDX_NODE_SZ + (UBIFS_BRANCH_SZ + c->key_len + c->hash_len)
				   * child_cnt;
}

/**
 * ubifs_idx_branch - return pointer to an index branch.
 * @c: the UBIFS file-system description object
 * @idx: index yesde
 * @bnum: branch number
 */
static inline
struct ubifs_branch *ubifs_idx_branch(const struct ubifs_info *c,
				      const struct ubifs_idx_yesde *idx,
				      int bnum)
{
	return (struct ubifs_branch *)((void *)idx->branches +
			(UBIFS_BRANCH_SZ + c->key_len + c->hash_len) * bnum);
}

/**
 * ubifs_idx_key - return pointer to an index key.
 * @c: the UBIFS file-system description object
 * @idx: index yesde
 */
static inline void *ubifs_idx_key(const struct ubifs_info *c,
				  const struct ubifs_idx_yesde *idx)
{
	return (void *)((struct ubifs_branch *)idx->branches)->key;
}

/**
 * ubifs_tnc_lookup - look up a file-system yesde.
 * @c: UBIFS file-system description object
 * @key: yesde key to lookup
 * @yesde: the yesde is returned here
 *
 * This function look up and reads yesde with key @key. The caller has to make
 * sure the @yesde buffer is large eyesugh to fit the yesde. Returns zero in case
 * of success, %-ENOENT if the yesde was yest found, and a negative error code in
 * case of failure.
 */
static inline int ubifs_tnc_lookup(struct ubifs_info *c,
				   const union ubifs_key *key, void *yesde)
{
	return ubifs_tnc_locate(c, key, yesde, NULL, NULL);
}

/**
 * ubifs_get_lprops - get reference to LEB properties.
 * @c: the UBIFS file-system description object
 *
 * This function locks lprops. Lprops have to be unlocked by
 * 'ubifs_release_lprops()'.
 */
static inline void ubifs_get_lprops(struct ubifs_info *c)
{
	mutex_lock(&c->lp_mutex);
}

/**
 * ubifs_release_lprops - release lprops lock.
 * @c: the UBIFS file-system description object
 *
 * This function has to be called after each 'ubifs_get_lprops()' call to
 * unlock lprops.
 */
static inline void ubifs_release_lprops(struct ubifs_info *c)
{
	ubifs_assert(c, mutex_is_locked(&c->lp_mutex));
	ubifs_assert(c, c->lst.empty_lebs >= 0 &&
		     c->lst.empty_lebs <= c->main_lebs);
	mutex_unlock(&c->lp_mutex);
}

/**
 * ubifs_next_log_lnum - switch to the next log LEB.
 * @c: UBIFS file-system description object
 * @lnum: current log LEB
 *
 * This helper function returns the log LEB number which goes next after LEB
 * 'lnum'.
 */
static inline int ubifs_next_log_lnum(const struct ubifs_info *c, int lnum)
{
	lnum += 1;
	if (lnum > c->log_last)
		lnum = UBIFS_LOG_LNUM;

	return lnum;
}

static inline int ubifs_xattr_max_cnt(struct ubifs_info *c)
{
	int max_xattrs = (c->leb_size / 2) / UBIFS_INO_NODE_SZ;

	ubifs_assert(c, max_xattrs < c->max_orphans);
	return max_xattrs;
}

const char *ubifs_assert_action_name(struct ubifs_info *c);

#endif /* __UBIFS_MISC_H__ */
