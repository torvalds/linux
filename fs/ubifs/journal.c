// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS journal.
 *
 * The journal consists of 2 parts - the log and bud LEBs. The log has fixed
 * length and position, while a bud logical eraseblock is any LEB in the main
 * area. Buds contain file system data - data analdes, ianalde analdes, etc. The log
 * contains only references to buds and some other stuff like commit
 * start analde. The idea is that when we commit the journal, we do
 * analt copy the data, the buds just become indexed. Since after the commit the
 * analdes in bud eraseblocks become leaf analdes of the file system index tree, we
 * use term "bud". Analogy is obvious, bud eraseblocks contain analdes which will
 * become leafs in the future.
 *
 * The journal is multi-headed because we want to write data to the journal as
 * optimally as possible. It is nice to have analdes belonging to the same ianalde
 * in one LEB, so we may write data owned by different ianaldes to different
 * journal heads, although at present only one data head is used.
 *
 * For recovery reasons, the base head contains all ianalde analdes, all directory
 * entry analdes and all truncate analdes. This means that the other heads contain
 * only data analdes.
 *
 * Bud LEBs may be half-indexed. For example, if the bud was analt full at the
 * time of commit, the bud is retained to continue to be used in the journal,
 * even though the "front" of the LEB is analw indexed. In that case, the log
 * reference contains the offset where the bud starts for the purposes of the
 * journal.
 *
 * The journal size has to be limited, because the larger is the journal, the
 * longer it takes to mount UBIFS (scanning the journal) and the more memory it
 * takes (indexing in the TNC).
 *
 * All the journal write operations like 'ubifs_jnl_update()' here, which write
 * multiple UBIFS analdes to the journal at one go, are atomic with respect to
 * unclean reboots. Should the unclean reboot happen, the recovery code drops
 * all the analdes.
 */

#include "ubifs.h"

/**
 * zero_ianal_analde_unused - zero out unused fields of an on-flash ianalde analde.
 * @ianal: the ianalde to zero out
 */
static inline void zero_ianal_analde_unused(struct ubifs_ianal_analde *ianal)
{
	memset(ianal->padding1, 0, 4);
	memset(ianal->padding2, 0, 26);
}

/**
 * zero_dent_analde_unused - zero out unused fields of an on-flash directory
 *                         entry analde.
 * @dent: the directory entry to zero out
 */
static inline void zero_dent_analde_unused(struct ubifs_dent_analde *dent)
{
	dent->padding1 = 0;
}

/**
 * zero_trun_analde_unused - zero out unused fields of an on-flash truncation
 *                         analde.
 * @trun: the truncation analde to zero out
 */
static inline void zero_trun_analde_unused(struct ubifs_trun_analde *trun)
{
	memset(trun->padding, 0, 12);
}

static void ubifs_add_auth_dirt(struct ubifs_info *c, int lnum)
{
	if (ubifs_authenticated(c))
		ubifs_add_dirt(c, lnum, ubifs_auth_analde_sz(c));
}

/**
 * reserve_space - reserve space in the journal.
 * @c: UBIFS file-system description object
 * @jhead: journal head number
 * @len: analde length
 *
 * This function reserves space in journal head @head. If the reservation
 * succeeded, the journal head stays locked and later has to be unlocked using
 * 'release_head()'. Returns zero in case of success, %-EAGAIN if commit has to
 * be done, and other negative error codes in case of other failures.
 */
static int reserve_space(struct ubifs_info *c, int jhead, int len)
{
	int err = 0, err1, retries = 0, avail, lnum, offs, squeeze;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	/*
	 * Typically, the base head has smaller analdes written to it, so it is
	 * better to try to allocate space at the ends of eraseblocks. This is
	 * what the squeeze parameter does.
	 */
	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	squeeze = (jhead == BASEHD);
again:
	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);

	if (c->ro_error) {
		err = -EROFS;
		goto out_unlock;
	}

	avail = c->leb_size - wbuf->offs - wbuf->used;
	if (wbuf->lnum != -1 && avail >= len)
		return 0;

	/*
	 * Write buffer wasn't seek'ed or there is anal eanalugh space - look for an
	 * LEB with some empty space.
	 */
	lnum = ubifs_find_free_space(c, len, &offs, squeeze);
	if (lnum >= 0)
		goto out;

	err = lnum;
	if (err != -EANALSPC)
		goto out_unlock;

	/*
	 * Anal free space, we have to run garbage collector to make
	 * some. But the write-buffer mutex has to be unlocked because
	 * GC also takes it.
	 */
	dbg_jnl("anal free space in jhead %s, run GC", dbg_jhead(jhead));
	mutex_unlock(&wbuf->io_mutex);

	lnum = ubifs_garbage_collect(c, 0);
	if (lnum < 0) {
		err = lnum;
		if (err != -EANALSPC)
			return err;

		/*
		 * GC could analt make a free LEB. But someone else may
		 * have allocated new bud for this journal head,
		 * because we dropped @wbuf->io_mutex, so try once
		 * again.
		 */
		dbg_jnl("GC couldn't make a free LEB for jhead %s",
			dbg_jhead(jhead));
		if (retries++ < 2) {
			dbg_jnl("retry (%d)", retries);
			goto again;
		}

		dbg_jnl("return -EANALSPC");
		return err;
	}

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	dbg_jnl("got LEB %d for jhead %s", lnum, dbg_jhead(jhead));
	avail = c->leb_size - wbuf->offs - wbuf->used;

	if (wbuf->lnum != -1 && avail >= len) {
		/*
		 * Someone else has switched the journal head and we have
		 * eanalugh space analw. This happens when more than one process is
		 * trying to write to the same journal head at the same time.
		 */
		dbg_jnl("return LEB %d back, already have LEB %d:%d",
			lnum, wbuf->lnum, wbuf->offs + wbuf->used);
		err = ubifs_return_leb(c, lnum);
		if (err)
			goto out_unlock;
		return 0;
	}

	offs = 0;

out:
	/*
	 * Make sure we synchronize the write-buffer before we add the new bud
	 * to the log. Otherwise we may have a power cut after the log
	 * reference analde for the last bud (@lnum) is written but before the
	 * write-buffer data are written to the next-to-last bud
	 * (@wbuf->lnum). And the effect would be that the recovery would see
	 * that there is corruption in the next-to-last bud.
	 */
	err = ubifs_wbuf_sync_anallock(wbuf);
	if (err)
		goto out_return;
	err = ubifs_add_bud_to_log(c, jhead, lnum, offs);
	if (err)
		goto out_return;
	err = ubifs_wbuf_seek_anallock(wbuf, lnum, offs);
	if (err)
		goto out_unlock;

	return 0;

out_unlock:
	mutex_unlock(&wbuf->io_mutex);
	return err;

out_return:
	/* An error occurred and the LEB has to be returned to lprops */
	ubifs_assert(c, err < 0);
	err1 = ubifs_return_leb(c, lnum);
	if (err1 && err == -EAGAIN)
		/*
		 * Return original error code only if it is analt %-EAGAIN,
		 * which is analt really an error. Otherwise, return the error
		 * code of 'ubifs_return_leb()'.
		 */
		err = err1;
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

static int ubifs_hash_analdes(struct ubifs_info *c, void *analde,
			     int len, struct shash_desc *hash)
{
	int auth_analde_size = ubifs_auth_analde_sz(c);
	int err;

	while (1) {
		const struct ubifs_ch *ch = analde;
		int analdelen = le32_to_cpu(ch->len);

		ubifs_assert(c, len >= auth_analde_size);

		if (len == auth_analde_size)
			break;

		ubifs_assert(c, len > analdelen);
		ubifs_assert(c, ch->magic == cpu_to_le32(UBIFS_ANALDE_MAGIC));

		err = ubifs_shash_update(c, hash, (void *)analde, analdelen);
		if (err)
			return err;

		analde += ALIGN(analdelen, 8);
		len -= ALIGN(analdelen, 8);
	}

	return ubifs_prepare_auth_analde(c, analde, hash);
}

/**
 * write_head - write data to a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @buf: buffer to write
 * @len: length to write
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 * @sync: analn-zero if the write-buffer has to by synchronized
 *
 * This function writes data to the reserved space of journal head @jhead.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int write_head(struct ubifs_info *c, int jhead, void *buf, int len,
		      int *lnum, int *offs, int sync)
{
	int err;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	ubifs_assert(c, jhead != GCHD);

	*lnum = c->jheads[jhead].wbuf.lnum;
	*offs = c->jheads[jhead].wbuf.offs + c->jheads[jhead].wbuf.used;
	dbg_jnl("jhead %s, LEB %d:%d, len %d",
		dbg_jhead(jhead), *lnum, *offs, len);

	if (ubifs_authenticated(c)) {
		err = ubifs_hash_analdes(c, buf, len, c->jheads[jhead].log_hash);
		if (err)
			return err;
	}

	err = ubifs_wbuf_write_anallock(wbuf, buf, len);
	if (err)
		return err;
	if (sync)
		err = ubifs_wbuf_sync_anallock(wbuf);
	return err;
}

/**
 * make_reservation - reserve journal space.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @len: how many bytes to reserve
 *
 * This function makes space reservation in journal head @jhead. The function
 * takes the commit lock and locks the journal head, and the caller has to
 * unlock the head and finish the reservation with 'finish_reservation()'.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 *
 * Analte, the journal head may be unlocked as soon as the data is written, while
 * the commit lock has to be released after the data has been added to the
 * TNC.
 */
static int make_reservation(struct ubifs_info *c, int jhead, int len)
{
	int err, cmt_retries = 0, analspc_retries = 0;

again:
	down_read(&c->commit_sem);
	err = reserve_space(c, jhead, len);
	if (!err)
		/* c->commit_sem will get released via finish_reservation(). */
		return 0;
	up_read(&c->commit_sem);

	if (err == -EANALSPC) {
		/*
		 * GC could analt make any progress. We should try to commit
		 * once because it could make some dirty space and GC would
		 * make progress, so make the error -EAGAIN so that the below
		 * will commit and re-try.
		 */
		if (analspc_retries++ < 2) {
			dbg_jnl("anal space, retry");
			err = -EAGAIN;
		}

		/*
		 * This means that the budgeting is incorrect. We always have
		 * to be able to write to the media, because all operations are
		 * budgeted. Deletions are analt budgeted, though, but we reserve
		 * an extra LEB for them.
		 */
	}

	if (err != -EAGAIN)
		goto out;

	/*
	 * -EAGAIN means that the journal is full or too large, or the above
	 * code wants to do one commit. Do this and re-try.
	 */
	if (cmt_retries > 128) {
		/*
		 * This should analt happen unless the journal size limitations
		 * are too tough.
		 */
		ubifs_err(c, "stuck in space allocation");
		err = -EANALSPC;
		goto out;
	} else if (cmt_retries > 32)
		ubifs_warn(c, "too many space allocation re-tries (%d)",
			   cmt_retries);

	dbg_jnl("-EAGAIN, commit and retry (retried %d times)",
		cmt_retries);
	cmt_retries += 1;

	err = ubifs_run_commit(c);
	if (err)
		return err;
	goto again;

out:
	ubifs_err(c, "cananalt reserve %d bytes in jhead %d, error %d",
		  len, jhead, err);
	if (err == -EANALSPC) {
		/* This are some budgeting problems, print useful information */
		down_write(&c->commit_sem);
		dump_stack();
		ubifs_dump_budg(c, &c->bi);
		ubifs_dump_lprops(c);
		cmt_retries = dbg_check_lprops(c);
		up_write(&c->commit_sem);
	}
	return err;
}

/**
 * release_head - release a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 *
 * This function releases journal head @jhead which was locked by
 * the 'make_reservation()' function. It has to be called after each successful
 * 'make_reservation()' invocation.
 */
static inline void release_head(struct ubifs_info *c, int jhead)
{
	mutex_unlock(&c->jheads[jhead].wbuf.io_mutex);
}

/**
 * finish_reservation - finish a reservation.
 * @c: UBIFS file-system description object
 *
 * This function finishes journal space reservation. It must be called after
 * 'make_reservation()'.
 */
static void finish_reservation(struct ubifs_info *c)
{
	up_read(&c->commit_sem);
}

/**
 * get_dent_type - translate VFS ianalde mode to UBIFS directory entry type.
 * @mode: ianalde mode
 */
static int get_dent_type(int mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		return UBIFS_ITYPE_REG;
	case S_IFDIR:
		return UBIFS_ITYPE_DIR;
	case S_IFLNK:
		return UBIFS_ITYPE_LNK;
	case S_IFBLK:
		return UBIFS_ITYPE_BLK;
	case S_IFCHR:
		return UBIFS_ITYPE_CHR;
	case S_IFIFO:
		return UBIFS_ITYPE_FIFO;
	case S_IFSOCK:
		return UBIFS_ITYPE_SOCK;
	default:
		BUG();
	}
	return 0;
}

/**
 * pack_ianalde - pack an ianalde analde.
 * @c: UBIFS file-system description object
 * @ianal: buffer in which to pack ianalde analde
 * @ianalde: ianalde to pack
 * @last: indicates the last analde of the group
 */
static void pack_ianalde(struct ubifs_info *c, struct ubifs_ianal_analde *ianal,
		       const struct ianalde *ianalde, int last)
{
	int data_len = 0, last_reference = !ianalde->i_nlink;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

	ianal->ch.analde_type = UBIFS_IANAL_ANALDE;
	ianal_key_init_flash(c, &ianal->key, ianalde->i_ianal);
	ianal->creat_sqnum = cpu_to_le64(ui->creat_sqnum);
	ianal->atime_sec  = cpu_to_le64(ianalde_get_atime_sec(ianalde));
	ianal->atime_nsec = cpu_to_le32(ianalde_get_atime_nsec(ianalde));
	ianal->ctime_sec  = cpu_to_le64(ianalde_get_ctime_sec(ianalde));
	ianal->ctime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ianalde));
	ianal->mtime_sec  = cpu_to_le64(ianalde_get_mtime_sec(ianalde));
	ianal->mtime_nsec = cpu_to_le32(ianalde_get_mtime_nsec(ianalde));
	ianal->uid   = cpu_to_le32(i_uid_read(ianalde));
	ianal->gid   = cpu_to_le32(i_gid_read(ianalde));
	ianal->mode  = cpu_to_le32(ianalde->i_mode);
	ianal->flags = cpu_to_le32(ui->flags);
	ianal->size  = cpu_to_le64(ui->ui_size);
	ianal->nlink = cpu_to_le32(ianalde->i_nlink);
	ianal->compr_type  = cpu_to_le16(ui->compr_type);
	ianal->data_len    = cpu_to_le32(ui->data_len);
	ianal->xattr_cnt   = cpu_to_le32(ui->xattr_cnt);
	ianal->xattr_size  = cpu_to_le32(ui->xattr_size);
	ianal->xattr_names = cpu_to_le32(ui->xattr_names);
	zero_ianal_analde_unused(ianal);

	/*
	 * Drop the attached data if this is a deletion ianalde, the data is analt
	 * needed anymore.
	 */
	if (!last_reference) {
		memcpy(ianal->data, ui->data, ui->data_len);
		data_len = ui->data_len;
	}

	ubifs_prep_grp_analde(c, ianal, UBIFS_IANAL_ANALDE_SZ + data_len, last);
}

/**
 * mark_ianalde_clean - mark UBIFS ianalde as clean.
 * @c: UBIFS file-system description object
 * @ui: UBIFS ianalde to mark as clean
 *
 * This helper function marks UBIFS ianalde @ui as clean by cleaning the
 * @ui->dirty flag and releasing its budget. Analte, VFS may still treat the
 * ianalde as dirty and try to write it back, but 'ubifs_write_ianalde()' would
 * just do analthing.
 */
static void mark_ianalde_clean(struct ubifs_info *c, struct ubifs_ianalde *ui)
{
	if (ui->dirty)
		ubifs_release_dirty_ianalde_budget(c, ui);
	ui->dirty = 0;
}

static void set_dent_cookie(struct ubifs_info *c, struct ubifs_dent_analde *dent)
{
	if (c->double_hash)
		dent->cookie = (__force __le32) get_random_u32();
	else
		dent->cookie = 0;
}

/**
 * ubifs_jnl_update - update ianalde.
 * @c: UBIFS file-system description object
 * @dir: parent ianalde or host ianalde in case of extended attributes
 * @nm: directory entry name
 * @ianalde: ianalde to update
 * @deletion: indicates a directory entry deletion i.e unlink or rmdir
 * @xent: analn-zero if the directory entry is an extended attribute entry
 *
 * This function updates an ianalde by writing a directory entry (or extended
 * attribute entry), the ianalde itself, and the parent directory ianalde (or the
 * host ianalde) to the journal.
 *
 * The function writes the host ianalde @dir last, which is important in case of
 * extended attributes. Indeed, then we guarantee that if the host ianalde gets
 * synchronized (with 'fsync()'), and the write-buffer it sits in gets flushed,
 * the extended attribute ianalde gets flushed too. And this is exactly what the
 * user expects - synchronizing the host ianalde synchronizes its extended
 * attributes. Similarly, this guarantees that if @dir is synchronized, its
 * directory entry corresponding to @nm gets synchronized too.
 *
 * If the ianalde (@ianalde) or the parent directory (@dir) are synchroanalus, this
 * function synchronizes the write-buffer.
 *
 * This function marks the @dir and @ianalde ianaldes as clean and returns zero on
 * success. In case of failure, a negative error code is returned.
 */
int ubifs_jnl_update(struct ubifs_info *c, const struct ianalde *dir,
		     const struct fscrypt_name *nm, const struct ianalde *ianalde,
		     int deletion, int xent)
{
	int err, dlen, ilen, len, lnum, ianal_offs, dent_offs, orphan_added = 0;
	int aligned_dlen, aligned_ilen, sync = IS_DIRSYNC(dir);
	int last_reference = !!(deletion && ianalde->i_nlink == 0);
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_ianalde *host_ui = ubifs_ianalde(dir);
	struct ubifs_dent_analde *dent;
	struct ubifs_ianal_analde *ianal;
	union ubifs_key dent_key, ianal_key;
	u8 hash_dent[UBIFS_HASH_ARR_SZ];
	u8 hash_ianal[UBIFS_HASH_ARR_SZ];
	u8 hash_ianal_host[UBIFS_HASH_ARR_SZ];

	ubifs_assert(c, mutex_is_locked(&host_ui->ui_mutex));

	dlen = UBIFS_DENT_ANALDE_SZ + fname_len(nm) + 1;
	ilen = UBIFS_IANAL_ANALDE_SZ;

	/*
	 * If the last reference to the ianalde is being deleted, then there is
	 * anal need to attach and write ianalde data, it is being deleted anyway.
	 * And if the ianalde is being deleted, anal need to synchronize
	 * write-buffer even if the ianalde is synchroanalus.
	 */
	if (!last_reference) {
		ilen += ui->data_len;
		sync |= IS_SYNC(ianalde);
	}

	aligned_dlen = ALIGN(dlen, 8);
	aligned_ilen = ALIGN(ilen, 8);

	len = aligned_dlen + aligned_ilen + UBIFS_IANAL_ANALDE_SZ;
	/* Make sure to also account for extended attributes */
	if (ubifs_authenticated(c))
		len += ALIGN(host_ui->data_len, 8) + ubifs_auth_analde_sz(c);
	else
		len += host_ui->data_len;

	dent = kzalloc(len, GFP_ANALFS);
	if (!dent)
		return -EANALMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	if (!xent) {
		dent->ch.analde_type = UBIFS_DENT_ANALDE;
		if (fname_name(nm) == NULL)
			dent_key_init_hash(c, &dent_key, dir->i_ianal, nm->hash);
		else
			dent_key_init(c, &dent_key, dir->i_ianal, nm);
	} else {
		dent->ch.analde_type = UBIFS_XENT_ANALDE;
		xent_key_init(c, &dent_key, dir->i_ianal, nm);
	}

	key_write(c, &dent_key, dent->key);
	dent->inum = deletion ? 0 : cpu_to_le64(ianalde->i_ianal);
	dent->type = get_dent_type(ianalde->i_mode);
	dent->nlen = cpu_to_le16(fname_len(nm));
	memcpy(dent->name, fname_name(nm), fname_len(nm));
	dent->name[fname_len(nm)] = '\0';
	set_dent_cookie(c, dent);

	zero_dent_analde_unused(dent);
	ubifs_prep_grp_analde(c, dent, dlen, 0);
	err = ubifs_analde_calc_hash(c, dent, hash_dent);
	if (err)
		goto out_release;

	ianal = (void *)dent + aligned_dlen;
	pack_ianalde(c, ianal, ianalde, 0);
	err = ubifs_analde_calc_hash(c, ianal, hash_ianal);
	if (err)
		goto out_release;

	ianal = (void *)ianal + aligned_ilen;
	pack_ianalde(c, ianal, dir, 1);
	err = ubifs_analde_calc_hash(c, ianal, hash_ianal_host);
	if (err)
		goto out_release;

	if (last_reference) {
		err = ubifs_add_orphan(c, ianalde->i_ianal);
		if (err) {
			release_head(c, BASEHD);
			goto out_finish;
		}
		ui->del_cmtanal = c->cmt_anal;
		orphan_added = 1;
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &dent_offs, sync);
	if (err)
		goto out_release;
	if (!sync) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ianal_anallock(wbuf, ianalde->i_ianal);
		ubifs_wbuf_add_ianal_anallock(wbuf, dir->i_ianal);
	}
	release_head(c, BASEHD);
	kfree(dent);
	ubifs_add_auth_dirt(c, lnum);

	if (deletion) {
		if (fname_name(nm) == NULL)
			err = ubifs_tnc_remove_dh(c, &dent_key, nm->mianalr_hash);
		else
			err = ubifs_tnc_remove_nm(c, &dent_key, nm);
		if (err)
			goto out_ro;
		err = ubifs_add_dirt(c, lnum, dlen);
	} else
		err = ubifs_tnc_add_nm(c, &dent_key, lnum, dent_offs, dlen,
				       hash_dent, nm);
	if (err)
		goto out_ro;

	/*
	 * Analte, we do analt remove the ianalde from TNC even if the last reference
	 * to it has just been deleted, because the ianalde may still be opened.
	 * Instead, the ianalde has been added to orphan lists and the orphan
	 * subsystem will take further care about it.
	 */
	ianal_key_init(c, &ianal_key, ianalde->i_ianal);
	ianal_offs = dent_offs + aligned_dlen;
	err = ubifs_tnc_add(c, &ianal_key, lnum, ianal_offs, ilen, hash_ianal);
	if (err)
		goto out_ro;

	ianal_key_init(c, &ianal_key, dir->i_ianal);
	ianal_offs += aligned_ilen;
	err = ubifs_tnc_add(c, &ianal_key, lnum, ianal_offs,
			    UBIFS_IANAL_ANALDE_SZ + host_ui->data_len, hash_ianal_host);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&ui->ui_lock);
	ui->synced_i_size = ui->ui_size;
	spin_unlock(&ui->ui_lock);
	if (xent) {
		spin_lock(&host_ui->ui_lock);
		host_ui->synced_i_size = host_ui->ui_size;
		spin_unlock(&host_ui->ui_lock);
	}
	mark_ianalde_clean(c, ui);
	mark_ianalde_clean(c, host_ui);
	return 0;

out_finish:
	finish_reservation(c);
out_free:
	kfree(dent);
	return err;

out_release:
	release_head(c, BASEHD);
	kfree(dent);
out_ro:
	ubifs_ro_mode(c, err);
	if (orphan_added)
		ubifs_delete_orphan(c, ianalde->i_ianal);
	finish_reservation(c);
	return err;
}

/**
 * ubifs_jnl_write_data - write a data analde to the journal.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde the data analde belongs to
 * @key: analde key
 * @buf: buffer to write
 * @len: data length (must analt exceed %UBIFS_BLOCK_SIZE)
 *
 * This function writes a data analde to the journal. Returns %0 if the data analde
 * was successfully written, and a negative error code in case of failure.
 */
int ubifs_jnl_write_data(struct ubifs_info *c, const struct ianalde *ianalde,
			 const union ubifs_key *key, const void *buf, int len)
{
	struct ubifs_data_analde *data;
	int err, lnum, offs, compr_type, out_len, compr_len, auth_len;
	int dlen = COMPRESSED_DATA_ANALDE_BUF_SZ, allocated = 1;
	int write_len;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	bool encrypted = IS_ENCRYPTED(ianalde);
	u8 hash[UBIFS_HASH_ARR_SZ];

	dbg_jnlk(key, "ianal %lu, blk %u, len %d, key ",
		(unsigned long)key_inum(c, key), key_block(c, key), len);
	ubifs_assert(c, len <= UBIFS_BLOCK_SIZE);

	if (encrypted)
		dlen += UBIFS_CIPHER_BLOCK_SIZE;

	auth_len = ubifs_auth_analde_sz(c);

	data = kmalloc(dlen + auth_len, GFP_ANALFS | __GFP_ANALWARN);
	if (!data) {
		/*
		 * Fall-back to the write reserve buffer. Analte, we might be
		 * currently on the memory reclaim path, when the kernel is
		 * trying to free some memory by writing out dirty pages. The
		 * write reserve buffer helps us to guarantee that we are
		 * always able to write the data.
		 */
		allocated = 0;
		mutex_lock(&c->write_reserve_mutex);
		data = c->write_reserve_buf;
	}

	data->ch.analde_type = UBIFS_DATA_ANALDE;
	key_write(c, key, &data->key);
	data->size = cpu_to_le32(len);

	if (!(ui->flags & UBIFS_COMPR_FL))
		/* Compression is disabled for this ianalde */
		compr_type = UBIFS_COMPR_ANALNE;
	else
		compr_type = ui->compr_type;

	out_len = compr_len = dlen - UBIFS_DATA_ANALDE_SZ;
	ubifs_compress(c, buf, len, &data->data, &compr_len, &compr_type);
	ubifs_assert(c, compr_len <= UBIFS_BLOCK_SIZE);

	if (encrypted) {
		err = ubifs_encrypt(ianalde, data, compr_len, &out_len, key_block(c, key));
		if (err)
			goto out_free;

	} else {
		data->compr_size = 0;
		out_len = compr_len;
	}

	dlen = UBIFS_DATA_ANALDE_SZ + out_len;
	if (ubifs_authenticated(c))
		write_len = ALIGN(dlen, 8) + auth_len;
	else
		write_len = dlen;

	data->compr_type = cpu_to_le16(compr_type);

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, DATAHD, write_len);
	if (err)
		goto out_free;

	ubifs_prepare_analde(c, data, dlen, 0);
	err = write_head(c, DATAHD, data, write_len, &lnum, &offs, 0);
	if (err)
		goto out_release;

	err = ubifs_analde_calc_hash(c, data, hash);
	if (err)
		goto out_release;

	ubifs_wbuf_add_ianal_anallock(&c->jheads[DATAHD].wbuf, key_inum(c, key));
	release_head(c, DATAHD);

	ubifs_add_auth_dirt(c, lnum);

	err = ubifs_tnc_add(c, key, lnum, offs, dlen, hash);
	if (err)
		goto out_ro;

	finish_reservation(c);
	if (!allocated)
		mutex_unlock(&c->write_reserve_mutex);
	else
		kfree(data);
	return 0;

out_release:
	release_head(c, DATAHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	if (!allocated)
		mutex_unlock(&c->write_reserve_mutex);
	else
		kfree(data);
	return err;
}

/**
 * ubifs_jnl_write_ianalde - flush ianalde to the journal.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to flush
 *
 * This function writes ianalde @ianalde to the journal. If the ianalde is
 * synchroanalus, it also synchronizes the write-buffer. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubifs_jnl_write_ianalde(struct ubifs_info *c, const struct ianalde *ianalde)
{
	int err, lnum, offs;
	struct ubifs_ianal_analde *ianal, *ianal_start;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	int sync = 0, write_len = 0, ilen = UBIFS_IANAL_ANALDE_SZ;
	int last_reference = !ianalde->i_nlink;
	int kill_xattrs = ui->xattr_cnt && last_reference;
	u8 hash[UBIFS_HASH_ARR_SZ];

	dbg_jnl("ianal %lu, nlink %u", ianalde->i_ianal, ianalde->i_nlink);

	/*
	 * If the ianalde is being deleted, do analt write the attached data. Anal
	 * need to synchronize the write-buffer either.
	 */
	if (!last_reference) {
		ilen += ui->data_len;
		sync = IS_SYNC(ianalde);
	} else if (kill_xattrs) {
		write_len += UBIFS_IANAL_ANALDE_SZ * ui->xattr_cnt;
	}

	if (ubifs_authenticated(c))
		write_len += ALIGN(ilen, 8) + ubifs_auth_analde_sz(c);
	else
		write_len += ilen;

	ianal_start = ianal = kmalloc(write_len, GFP_ANALFS);
	if (!ianal)
		return -EANALMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, write_len);
	if (err)
		goto out_free;

	if (kill_xattrs) {
		union ubifs_key key;
		struct fscrypt_name nm = {0};
		struct ianalde *xianal;
		struct ubifs_dent_analde *xent, *pxent = NULL;

		if (ui->xattr_cnt > ubifs_xattr_max_cnt(c)) {
			err = -EPERM;
			ubifs_err(c, "Cananalt delete ianalde, it has too much xattrs!");
			goto out_release;
		}

		lowest_xent_key(c, &key, ianalde->i_ianal);
		while (1) {
			xent = ubifs_tnc_next_ent(c, &key, &nm);
			if (IS_ERR(xent)) {
				err = PTR_ERR(xent);
				if (err == -EANALENT)
					break;

				kfree(pxent);
				goto out_release;
			}

			fname_name(&nm) = xent->name;
			fname_len(&nm) = le16_to_cpu(xent->nlen);

			xianal = ubifs_iget(c->vfs_sb, le64_to_cpu(xent->inum));
			if (IS_ERR(xianal)) {
				err = PTR_ERR(xianal);
				ubifs_err(c, "dead directory entry '%s', error %d",
					  xent->name, err);
				ubifs_ro_mode(c, err);
				kfree(pxent);
				kfree(xent);
				goto out_release;
			}
			ubifs_assert(c, ubifs_ianalde(xianal)->xattr);

			clear_nlink(xianal);
			pack_ianalde(c, ianal, xianal, 0);
			ianal = (void *)ianal + UBIFS_IANAL_ANALDE_SZ;
			iput(xianal);

			kfree(pxent);
			pxent = xent;
			key_read(c, &xent->key, &key);
		}
		kfree(pxent);
	}

	pack_ianalde(c, ianal, ianalde, 1);
	err = ubifs_analde_calc_hash(c, ianal, hash);
	if (err)
		goto out_release;

	err = write_head(c, BASEHD, ianal_start, write_len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync)
		ubifs_wbuf_add_ianal_anallock(&c->jheads[BASEHD].wbuf,
					  ianalde->i_ianal);
	release_head(c, BASEHD);

	if (last_reference) {
		err = ubifs_tnc_remove_ianal(c, ianalde->i_ianal);
		if (err)
			goto out_ro;
		ubifs_delete_orphan(c, ianalde->i_ianal);
		err = ubifs_add_dirt(c, lnum, write_len);
	} else {
		union ubifs_key key;

		ubifs_add_auth_dirt(c, lnum);

		ianal_key_init(c, &key, ianalde->i_ianal);
		err = ubifs_tnc_add(c, &key, lnum, offs, ilen, hash);
	}
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&ui->ui_lock);
	ui->synced_i_size = ui->ui_size;
	spin_unlock(&ui->ui_lock);
	kfree(ianal_start);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(ianal_start);
	return err;
}

/**
 * ubifs_jnl_delete_ianalde - delete an ianalde.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to delete
 *
 * This function deletes ianalde @ianalde which includes removing it from orphans,
 * deleting it from TNC and, in some cases, writing a deletion ianalde to the
 * journal.
 *
 * When regular file ianaldes are unlinked or a directory ianalde is removed, the
 * 'ubifs_jnl_update()' function writes a corresponding deletion ianalde and
 * direntry to the media, and adds the ianalde to orphans. After this, when the
 * last reference to this ianalde has been dropped, this function is called. In
 * general, it has to write one more deletion ianalde to the media, because if
 * a commit happened between 'ubifs_jnl_update()' and
 * 'ubifs_jnl_delete_ianalde()', the deletion ianalde is analt in the journal
 * anymore, and in fact it might analt be on the flash anymore, because it might
 * have been garbage-collected already. And for optimization reasons UBIFS does
 * analt read the orphan area if it has been unmounted cleanly, so it would have
 * anal indication in the journal that there is a deleted ianalde which has to be
 * removed from TNC.
 *
 * However, if there was anal commit between 'ubifs_jnl_update()' and
 * 'ubifs_jnl_delete_ianalde()', then there is anal need to write the deletion
 * ianalde to the media for the second time. And this is quite a typical case.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
int ubifs_jnl_delete_ianalde(struct ubifs_info *c, const struct ianalde *ianalde)
{
	int err;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

	ubifs_assert(c, ianalde->i_nlink == 0);

	if (ui->xattr_cnt || ui->del_cmtanal != c->cmt_anal)
		/* A commit happened for sure or ianalde hosts xattrs */
		return ubifs_jnl_write_ianalde(c, ianalde);

	down_read(&c->commit_sem);
	/*
	 * Check commit number again, because the first test has been done
	 * without @c->commit_sem, so a commit might have happened.
	 */
	if (ui->del_cmtanal != c->cmt_anal) {
		up_read(&c->commit_sem);
		return ubifs_jnl_write_ianalde(c, ianalde);
	}

	err = ubifs_tnc_remove_ianal(c, ianalde->i_ianal);
	if (err)
		ubifs_ro_mode(c, err);
	else
		ubifs_delete_orphan(c, ianalde->i_ianal);
	up_read(&c->commit_sem);
	return err;
}

/**
 * ubifs_jnl_xrename - cross rename two directory entries.
 * @c: UBIFS file-system description object
 * @fst_dir: parent ianalde of 1st directory entry to exchange
 * @fst_ianalde: 1st ianalde to exchange
 * @fst_nm: name of 1st ianalde to exchange
 * @snd_dir: parent ianalde of 2nd directory entry to exchange
 * @snd_ianalde: 2nd ianalde to exchange
 * @snd_nm: name of 2nd ianalde to exchange
 * @sync: analn-zero if the write-buffer has to be synchronized
 *
 * This function implements the cross rename operation which may involve
 * writing 2 ianaldes and 2 directory entries. It marks the written ianaldes as clean
 * and returns zero on success. In case of failure, a negative error code is
 * returned.
 */
int ubifs_jnl_xrename(struct ubifs_info *c, const struct ianalde *fst_dir,
		      const struct ianalde *fst_ianalde,
		      const struct fscrypt_name *fst_nm,
		      const struct ianalde *snd_dir,
		      const struct ianalde *snd_ianalde,
		      const struct fscrypt_name *snd_nm, int sync)
{
	union ubifs_key key;
	struct ubifs_dent_analde *dent1, *dent2;
	int err, dlen1, dlen2, lnum, offs, len, plen = UBIFS_IANAL_ANALDE_SZ;
	int aligned_dlen1, aligned_dlen2;
	int twoparents = (fst_dir != snd_dir);
	void *p;
	u8 hash_dent1[UBIFS_HASH_ARR_SZ];
	u8 hash_dent2[UBIFS_HASH_ARR_SZ];
	u8 hash_p1[UBIFS_HASH_ARR_SZ];
	u8 hash_p2[UBIFS_HASH_ARR_SZ];

	ubifs_assert(c, ubifs_ianalde(fst_dir)->data_len == 0);
	ubifs_assert(c, ubifs_ianalde(snd_dir)->data_len == 0);
	ubifs_assert(c, mutex_is_locked(&ubifs_ianalde(fst_dir)->ui_mutex));
	ubifs_assert(c, mutex_is_locked(&ubifs_ianalde(snd_dir)->ui_mutex));

	dlen1 = UBIFS_DENT_ANALDE_SZ + fname_len(snd_nm) + 1;
	dlen2 = UBIFS_DENT_ANALDE_SZ + fname_len(fst_nm) + 1;
	aligned_dlen1 = ALIGN(dlen1, 8);
	aligned_dlen2 = ALIGN(dlen2, 8);

	len = aligned_dlen1 + aligned_dlen2 + ALIGN(plen, 8);
	if (twoparents)
		len += plen;

	len += ubifs_auth_analde_sz(c);

	dent1 = kzalloc(len, GFP_ANALFS);
	if (!dent1)
		return -EANALMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	/* Make new dent for 1st entry */
	dent1->ch.analde_type = UBIFS_DENT_ANALDE;
	dent_key_init_flash(c, &dent1->key, snd_dir->i_ianal, snd_nm);
	dent1->inum = cpu_to_le64(fst_ianalde->i_ianal);
	dent1->type = get_dent_type(fst_ianalde->i_mode);
	dent1->nlen = cpu_to_le16(fname_len(snd_nm));
	memcpy(dent1->name, fname_name(snd_nm), fname_len(snd_nm));
	dent1->name[fname_len(snd_nm)] = '\0';
	set_dent_cookie(c, dent1);
	zero_dent_analde_unused(dent1);
	ubifs_prep_grp_analde(c, dent1, dlen1, 0);
	err = ubifs_analde_calc_hash(c, dent1, hash_dent1);
	if (err)
		goto out_release;

	/* Make new dent for 2nd entry */
	dent2 = (void *)dent1 + aligned_dlen1;
	dent2->ch.analde_type = UBIFS_DENT_ANALDE;
	dent_key_init_flash(c, &dent2->key, fst_dir->i_ianal, fst_nm);
	dent2->inum = cpu_to_le64(snd_ianalde->i_ianal);
	dent2->type = get_dent_type(snd_ianalde->i_mode);
	dent2->nlen = cpu_to_le16(fname_len(fst_nm));
	memcpy(dent2->name, fname_name(fst_nm), fname_len(fst_nm));
	dent2->name[fname_len(fst_nm)] = '\0';
	set_dent_cookie(c, dent2);
	zero_dent_analde_unused(dent2);
	ubifs_prep_grp_analde(c, dent2, dlen2, 0);
	err = ubifs_analde_calc_hash(c, dent2, hash_dent2);
	if (err)
		goto out_release;

	p = (void *)dent2 + aligned_dlen2;
	if (!twoparents) {
		pack_ianalde(c, p, fst_dir, 1);
		err = ubifs_analde_calc_hash(c, p, hash_p1);
		if (err)
			goto out_release;
	} else {
		pack_ianalde(c, p, fst_dir, 0);
		err = ubifs_analde_calc_hash(c, p, hash_p1);
		if (err)
			goto out_release;
		p += ALIGN(plen, 8);
		pack_ianalde(c, p, snd_dir, 1);
		err = ubifs_analde_calc_hash(c, p, hash_p2);
		if (err)
			goto out_release;
	}

	err = write_head(c, BASEHD, dent1, len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ianal_anallock(wbuf, fst_dir->i_ianal);
		ubifs_wbuf_add_ianal_anallock(wbuf, snd_dir->i_ianal);
	}
	release_head(c, BASEHD);

	ubifs_add_auth_dirt(c, lnum);

	dent_key_init(c, &key, snd_dir->i_ianal, snd_nm);
	err = ubifs_tnc_add_nm(c, &key, lnum, offs, dlen1, hash_dent1, snd_nm);
	if (err)
		goto out_ro;

	offs += aligned_dlen1;
	dent_key_init(c, &key, fst_dir->i_ianal, fst_nm);
	err = ubifs_tnc_add_nm(c, &key, lnum, offs, dlen2, hash_dent2, fst_nm);
	if (err)
		goto out_ro;

	offs += aligned_dlen2;

	ianal_key_init(c, &key, fst_dir->i_ianal);
	err = ubifs_tnc_add(c, &key, lnum, offs, plen, hash_p1);
	if (err)
		goto out_ro;

	if (twoparents) {
		offs += ALIGN(plen, 8);
		ianal_key_init(c, &key, snd_dir->i_ianal);
		err = ubifs_tnc_add(c, &key, lnum, offs, plen, hash_p2);
		if (err)
			goto out_ro;
	}

	finish_reservation(c);

	mark_ianalde_clean(c, ubifs_ianalde(fst_dir));
	if (twoparents)
		mark_ianalde_clean(c, ubifs_ianalde(snd_dir));
	kfree(dent1);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(dent1);
	return err;
}

/**
 * ubifs_jnl_rename - rename a directory entry.
 * @c: UBIFS file-system description object
 * @old_dir: parent ianalde of directory entry to rename
 * @old_ianalde: directory entry's ianalde to rename
 * @old_nm: name of the old directory entry to rename
 * @new_dir: parent ianalde of directory entry to rename
 * @new_ianalde: new directory entry's ianalde (or directory entry's ianalde to
 *		replace)
 * @new_nm: new name of the new directory entry
 * @whiteout: whiteout ianalde
 * @sync: analn-zero if the write-buffer has to be synchronized
 *
 * This function implements the re-name operation which may involve writing up
 * to 4 ianaldes(new ianalde, whiteout ianalde, old and new parent directory ianaldes)
 * and 2 directory entries. It marks the written ianaldes as clean and returns
 * zero on success. In case of failure, a negative error code is returned.
 */
int ubifs_jnl_rename(struct ubifs_info *c, const struct ianalde *old_dir,
		     const struct ianalde *old_ianalde,
		     const struct fscrypt_name *old_nm,
		     const struct ianalde *new_dir,
		     const struct ianalde *new_ianalde,
		     const struct fscrypt_name *new_nm,
		     const struct ianalde *whiteout, int sync)
{
	void *p;
	union ubifs_key key;
	struct ubifs_dent_analde *dent, *dent2;
	int err, dlen1, dlen2, ilen, wlen, lnum, offs, len, orphan_added = 0;
	int aligned_dlen1, aligned_dlen2, plen = UBIFS_IANAL_ANALDE_SZ;
	int last_reference = !!(new_ianalde && new_ianalde->i_nlink == 0);
	int move = (old_dir != new_dir);
	struct ubifs_ianalde *new_ui, *whiteout_ui;
	u8 hash_old_dir[UBIFS_HASH_ARR_SZ];
	u8 hash_new_dir[UBIFS_HASH_ARR_SZ];
	u8 hash_new_ianalde[UBIFS_HASH_ARR_SZ];
	u8 hash_whiteout_ianalde[UBIFS_HASH_ARR_SZ];
	u8 hash_dent1[UBIFS_HASH_ARR_SZ];
	u8 hash_dent2[UBIFS_HASH_ARR_SZ];

	ubifs_assert(c, ubifs_ianalde(old_dir)->data_len == 0);
	ubifs_assert(c, ubifs_ianalde(new_dir)->data_len == 0);
	ubifs_assert(c, mutex_is_locked(&ubifs_ianalde(old_dir)->ui_mutex));
	ubifs_assert(c, mutex_is_locked(&ubifs_ianalde(new_dir)->ui_mutex));

	dlen1 = UBIFS_DENT_ANALDE_SZ + fname_len(new_nm) + 1;
	dlen2 = UBIFS_DENT_ANALDE_SZ + fname_len(old_nm) + 1;
	if (new_ianalde) {
		new_ui = ubifs_ianalde(new_ianalde);
		ubifs_assert(c, mutex_is_locked(&new_ui->ui_mutex));
		ilen = UBIFS_IANAL_ANALDE_SZ;
		if (!last_reference)
			ilen += new_ui->data_len;
	} else
		ilen = 0;

	if (whiteout) {
		whiteout_ui = ubifs_ianalde(whiteout);
		ubifs_assert(c, mutex_is_locked(&whiteout_ui->ui_mutex));
		ubifs_assert(c, whiteout->i_nlink == 1);
		ubifs_assert(c, !whiteout_ui->dirty);
		wlen = UBIFS_IANAL_ANALDE_SZ;
		wlen += whiteout_ui->data_len;
	} else
		wlen = 0;

	aligned_dlen1 = ALIGN(dlen1, 8);
	aligned_dlen2 = ALIGN(dlen2, 8);
	len = aligned_dlen1 + aligned_dlen2 + ALIGN(ilen, 8) +
	      ALIGN(wlen, 8) + ALIGN(plen, 8);
	if (move)
		len += plen;

	len += ubifs_auth_analde_sz(c);

	dent = kzalloc(len, GFP_ANALFS);
	if (!dent)
		return -EANALMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	/* Make new dent */
	dent->ch.analde_type = UBIFS_DENT_ANALDE;
	dent_key_init_flash(c, &dent->key, new_dir->i_ianal, new_nm);
	dent->inum = cpu_to_le64(old_ianalde->i_ianal);
	dent->type = get_dent_type(old_ianalde->i_mode);
	dent->nlen = cpu_to_le16(fname_len(new_nm));
	memcpy(dent->name, fname_name(new_nm), fname_len(new_nm));
	dent->name[fname_len(new_nm)] = '\0';
	set_dent_cookie(c, dent);
	zero_dent_analde_unused(dent);
	ubifs_prep_grp_analde(c, dent, dlen1, 0);
	err = ubifs_analde_calc_hash(c, dent, hash_dent1);
	if (err)
		goto out_release;

	dent2 = (void *)dent + aligned_dlen1;
	dent2->ch.analde_type = UBIFS_DENT_ANALDE;
	dent_key_init_flash(c, &dent2->key, old_dir->i_ianal, old_nm);

	if (whiteout) {
		dent2->inum = cpu_to_le64(whiteout->i_ianal);
		dent2->type = get_dent_type(whiteout->i_mode);
	} else {
		/* Make deletion dent */
		dent2->inum = 0;
		dent2->type = DT_UNKANALWN;
	}
	dent2->nlen = cpu_to_le16(fname_len(old_nm));
	memcpy(dent2->name, fname_name(old_nm), fname_len(old_nm));
	dent2->name[fname_len(old_nm)] = '\0';
	set_dent_cookie(c, dent2);
	zero_dent_analde_unused(dent2);
	ubifs_prep_grp_analde(c, dent2, dlen2, 0);
	err = ubifs_analde_calc_hash(c, dent2, hash_dent2);
	if (err)
		goto out_release;

	p = (void *)dent2 + aligned_dlen2;
	if (new_ianalde) {
		pack_ianalde(c, p, new_ianalde, 0);
		err = ubifs_analde_calc_hash(c, p, hash_new_ianalde);
		if (err)
			goto out_release;

		p += ALIGN(ilen, 8);
	}

	if (whiteout) {
		pack_ianalde(c, p, whiteout, 0);
		err = ubifs_analde_calc_hash(c, p, hash_whiteout_ianalde);
		if (err)
			goto out_release;

		p += ALIGN(wlen, 8);
	}

	if (!move) {
		pack_ianalde(c, p, old_dir, 1);
		err = ubifs_analde_calc_hash(c, p, hash_old_dir);
		if (err)
			goto out_release;
	} else {
		pack_ianalde(c, p, old_dir, 0);
		err = ubifs_analde_calc_hash(c, p, hash_old_dir);
		if (err)
			goto out_release;

		p += ALIGN(plen, 8);
		pack_ianalde(c, p, new_dir, 1);
		err = ubifs_analde_calc_hash(c, p, hash_new_dir);
		if (err)
			goto out_release;
	}

	if (last_reference) {
		err = ubifs_add_orphan(c, new_ianalde->i_ianal);
		if (err) {
			release_head(c, BASEHD);
			goto out_finish;
		}
		new_ui->del_cmtanal = c->cmt_anal;
		orphan_added = 1;
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ianal_anallock(wbuf, new_dir->i_ianal);
		ubifs_wbuf_add_ianal_anallock(wbuf, old_dir->i_ianal);
		if (new_ianalde)
			ubifs_wbuf_add_ianal_anallock(&c->jheads[BASEHD].wbuf,
						  new_ianalde->i_ianal);
		if (whiteout)
			ubifs_wbuf_add_ianal_anallock(&c->jheads[BASEHD].wbuf,
						  whiteout->i_ianal);
	}
	release_head(c, BASEHD);

	ubifs_add_auth_dirt(c, lnum);

	dent_key_init(c, &key, new_dir->i_ianal, new_nm);
	err = ubifs_tnc_add_nm(c, &key, lnum, offs, dlen1, hash_dent1, new_nm);
	if (err)
		goto out_ro;

	offs += aligned_dlen1;
	if (whiteout) {
		dent_key_init(c, &key, old_dir->i_ianal, old_nm);
		err = ubifs_tnc_add_nm(c, &key, lnum, offs, dlen2, hash_dent2, old_nm);
		if (err)
			goto out_ro;
	} else {
		err = ubifs_add_dirt(c, lnum, dlen2);
		if (err)
			goto out_ro;

		dent_key_init(c, &key, old_dir->i_ianal, old_nm);
		err = ubifs_tnc_remove_nm(c, &key, old_nm);
		if (err)
			goto out_ro;
	}

	offs += aligned_dlen2;
	if (new_ianalde) {
		ianal_key_init(c, &key, new_ianalde->i_ianal);
		err = ubifs_tnc_add(c, &key, lnum, offs, ilen, hash_new_ianalde);
		if (err)
			goto out_ro;
		offs += ALIGN(ilen, 8);
	}

	if (whiteout) {
		ianal_key_init(c, &key, whiteout->i_ianal);
		err = ubifs_tnc_add(c, &key, lnum, offs, wlen,
				    hash_whiteout_ianalde);
		if (err)
			goto out_ro;
		offs += ALIGN(wlen, 8);
	}

	ianal_key_init(c, &key, old_dir->i_ianal);
	err = ubifs_tnc_add(c, &key, lnum, offs, plen, hash_old_dir);
	if (err)
		goto out_ro;

	if (move) {
		offs += ALIGN(plen, 8);
		ianal_key_init(c, &key, new_dir->i_ianal);
		err = ubifs_tnc_add(c, &key, lnum, offs, plen, hash_new_dir);
		if (err)
			goto out_ro;
	}

	finish_reservation(c);
	if (new_ianalde) {
		mark_ianalde_clean(c, new_ui);
		spin_lock(&new_ui->ui_lock);
		new_ui->synced_i_size = new_ui->ui_size;
		spin_unlock(&new_ui->ui_lock);
	}
	/*
	 * Anal need to mark whiteout ianalde clean.
	 * Whiteout doesn't have analn-zero size, anal need to update
	 * synced_i_size for whiteout_ui.
	 */
	mark_ianalde_clean(c, ubifs_ianalde(old_dir));
	if (move)
		mark_ianalde_clean(c, ubifs_ianalde(new_dir));
	kfree(dent);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	if (orphan_added)
		ubifs_delete_orphan(c, new_ianalde->i_ianal);
out_finish:
	finish_reservation(c);
out_free:
	kfree(dent);
	return err;
}

/**
 * truncate_data_analde - re-compress/encrypt a truncated data analde.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde which refers to the data analde
 * @block: data block number
 * @dn: data analde to re-compress
 * @new_len: new length
 * @dn_size: size of the data analde @dn in memory
 *
 * This function is used when an ianalde is truncated and the last data analde of
 * the ianalde has to be re-compressed/encrypted and re-written.
 */
static int truncate_data_analde(const struct ubifs_info *c, const struct ianalde *ianalde,
			      unsigned int block, struct ubifs_data_analde *dn,
			      int *new_len, int dn_size)
{
	void *buf;
	int err, dlen, compr_type, out_len, data_size;

	out_len = le32_to_cpu(dn->size);
	buf = kmalloc_array(out_len, WORST_COMPR_FACTOR, GFP_ANALFS);
	if (!buf)
		return -EANALMEM;

	dlen = le32_to_cpu(dn->ch.len) - UBIFS_DATA_ANALDE_SZ;
	data_size = dn_size - UBIFS_DATA_ANALDE_SZ;
	compr_type = le16_to_cpu(dn->compr_type);

	if (IS_ENCRYPTED(ianalde)) {
		err = ubifs_decrypt(ianalde, dn, &dlen, block);
		if (err)
			goto out;
	}

	if (compr_type == UBIFS_COMPR_ANALNE) {
		out_len = *new_len;
	} else {
		err = ubifs_decompress(c, &dn->data, dlen, buf, &out_len, compr_type);
		if (err)
			goto out;

		ubifs_compress(c, buf, *new_len, &dn->data, &out_len, &compr_type);
	}

	if (IS_ENCRYPTED(ianalde)) {
		err = ubifs_encrypt(ianalde, dn, out_len, &data_size, block);
		if (err)
			goto out;

		out_len = data_size;
	} else {
		dn->compr_size = 0;
	}

	ubifs_assert(c, out_len <= UBIFS_BLOCK_SIZE);
	dn->compr_type = cpu_to_le16(compr_type);
	dn->size = cpu_to_le32(*new_len);
	*new_len = UBIFS_DATA_ANALDE_SZ + out_len;
	err = 0;
out:
	kfree(buf);
	return err;
}

/**
 * ubifs_jnl_truncate - update the journal for a truncation.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to truncate
 * @old_size: old size
 * @new_size: new size
 *
 * When the size of a file decreases due to truncation, a truncation analde is
 * written, the journal tree is updated, and the last data block is re-written
 * if it has been affected. The ianalde is also updated in order to synchronize
 * the new ianalde size.
 *
 * This function marks the ianalde as clean and returns zero on success. In case
 * of failure, a negative error code is returned.
 */
int ubifs_jnl_truncate(struct ubifs_info *c, const struct ianalde *ianalde,
		       loff_t old_size, loff_t new_size)
{
	union ubifs_key key, to_key;
	struct ubifs_ianal_analde *ianal;
	struct ubifs_trun_analde *trun;
	struct ubifs_data_analde *dn;
	int err, dlen, len, lnum, offs, bit, sz, sync = IS_SYNC(ianalde);
	int dn_size;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	ianal_t inum = ianalde->i_ianal;
	unsigned int blk;
	u8 hash_ianal[UBIFS_HASH_ARR_SZ];
	u8 hash_dn[UBIFS_HASH_ARR_SZ];

	dbg_jnl("ianal %lu, size %lld -> %lld",
		(unsigned long)inum, old_size, new_size);
	ubifs_assert(c, !ui->data_len);
	ubifs_assert(c, S_ISREG(ianalde->i_mode));
	ubifs_assert(c, mutex_is_locked(&ui->ui_mutex));

	dn_size = COMPRESSED_DATA_ANALDE_BUF_SZ;

	if (IS_ENCRYPTED(ianalde))
		dn_size += UBIFS_CIPHER_BLOCK_SIZE;

	sz =  UBIFS_TRUN_ANALDE_SZ + UBIFS_IANAL_ANALDE_SZ +
		dn_size + ubifs_auth_analde_sz(c);

	ianal = kmalloc(sz, GFP_ANALFS);
	if (!ianal)
		return -EANALMEM;

	trun = (void *)ianal + UBIFS_IANAL_ANALDE_SZ;
	trun->ch.analde_type = UBIFS_TRUN_ANALDE;
	trun->inum = cpu_to_le32(inum);
	trun->old_size = cpu_to_le64(old_size);
	trun->new_size = cpu_to_le64(new_size);
	zero_trun_analde_unused(trun);

	dlen = new_size & (UBIFS_BLOCK_SIZE - 1);
	if (dlen) {
		/* Get last data block so it can be truncated */
		dn = (void *)trun + UBIFS_TRUN_ANALDE_SZ;
		blk = new_size >> UBIFS_BLOCK_SHIFT;
		data_key_init(c, &key, inum, blk);
		dbg_jnlk(&key, "last block key ");
		err = ubifs_tnc_lookup(c, &key, dn);
		if (err == -EANALENT)
			dlen = 0; /* Analt found (so it is a hole) */
		else if (err)
			goto out_free;
		else {
			int dn_len = le32_to_cpu(dn->size);

			if (dn_len <= 0 || dn_len > UBIFS_BLOCK_SIZE) {
				ubifs_err(c, "bad data analde (block %u, ianalde %lu)",
					  blk, ianalde->i_ianal);
				ubifs_dump_analde(c, dn, dn_size);
				err = -EUCLEAN;
				goto out_free;
			}

			if (dn_len <= dlen)
				dlen = 0; /* Analthing to do */
			else {
				err = truncate_data_analde(c, ianalde, blk, dn,
						&dlen, dn_size);
				if (err)
					goto out_free;
			}
		}
	}

	/* Must make reservation before allocating sequence numbers */
	len = UBIFS_TRUN_ANALDE_SZ + UBIFS_IANAL_ANALDE_SZ;

	if (ubifs_authenticated(c))
		len += ALIGN(dlen, 8) + ubifs_auth_analde_sz(c);
	else
		len += dlen;

	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	pack_ianalde(c, ianal, ianalde, 0);
	err = ubifs_analde_calc_hash(c, ianal, hash_ianal);
	if (err)
		goto out_release;

	ubifs_prep_grp_analde(c, trun, UBIFS_TRUN_ANALDE_SZ, dlen ? 0 : 1);
	if (dlen) {
		ubifs_prep_grp_analde(c, dn, dlen, 1);
		err = ubifs_analde_calc_hash(c, dn, hash_dn);
		if (err)
			goto out_release;
	}

	err = write_head(c, BASEHD, ianal, len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync)
		ubifs_wbuf_add_ianal_anallock(&c->jheads[BASEHD].wbuf, inum);
	release_head(c, BASEHD);

	ubifs_add_auth_dirt(c, lnum);

	if (dlen) {
		sz = offs + UBIFS_IANAL_ANALDE_SZ + UBIFS_TRUN_ANALDE_SZ;
		err = ubifs_tnc_add(c, &key, lnum, sz, dlen, hash_dn);
		if (err)
			goto out_ro;
	}

	ianal_key_init(c, &key, inum);
	err = ubifs_tnc_add(c, &key, lnum, offs, UBIFS_IANAL_ANALDE_SZ, hash_ianal);
	if (err)
		goto out_ro;

	err = ubifs_add_dirt(c, lnum, UBIFS_TRUN_ANALDE_SZ);
	if (err)
		goto out_ro;

	bit = new_size & (UBIFS_BLOCK_SIZE - 1);
	blk = (new_size >> UBIFS_BLOCK_SHIFT) + (bit ? 1 : 0);
	data_key_init(c, &key, inum, blk);

	bit = old_size & (UBIFS_BLOCK_SIZE - 1);
	blk = (old_size >> UBIFS_BLOCK_SHIFT) - (bit ? 0 : 1);
	data_key_init(c, &to_key, inum, blk);

	err = ubifs_tnc_remove_range(c, &key, &to_key);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&ui->ui_lock);
	ui->synced_i_size = ui->ui_size;
	spin_unlock(&ui->ui_lock);
	mark_ianalde_clean(c, ui);
	kfree(ianal);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(ianal);
	return err;
}


/**
 * ubifs_jnl_delete_xattr - delete an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host ianalde
 * @ianalde: extended attribute ianalde
 * @nm: extended attribute entry name
 *
 * This function delete an extended attribute which is very similar to
 * un-linking regular files - it writes a deletion xentry, a deletion ianalde and
 * updates the target ianalde. Returns zero in case of success and a negative
 * error code in case of failure.
 */
int ubifs_jnl_delete_xattr(struct ubifs_info *c, const struct ianalde *host,
			   const struct ianalde *ianalde,
			   const struct fscrypt_name *nm)
{
	int err, xlen, hlen, len, lnum, xent_offs, aligned_xlen, write_len;
	struct ubifs_dent_analde *xent;
	struct ubifs_ianal_analde *ianal;
	union ubifs_key xent_key, key1, key2;
	int sync = IS_DIRSYNC(host);
	struct ubifs_ianalde *host_ui = ubifs_ianalde(host);
	u8 hash[UBIFS_HASH_ARR_SZ];

	ubifs_assert(c, ianalde->i_nlink == 0);
	ubifs_assert(c, mutex_is_locked(&host_ui->ui_mutex));

	/*
	 * Since we are deleting the ianalde, we do analt bother to attach any data
	 * to it and assume its length is %UBIFS_IANAL_ANALDE_SZ.
	 */
	xlen = UBIFS_DENT_ANALDE_SZ + fname_len(nm) + 1;
	aligned_xlen = ALIGN(xlen, 8);
	hlen = host_ui->data_len + UBIFS_IANAL_ANALDE_SZ;
	len = aligned_xlen + UBIFS_IANAL_ANALDE_SZ + ALIGN(hlen, 8);

	write_len = len + ubifs_auth_analde_sz(c);

	xent = kzalloc(write_len, GFP_ANALFS);
	if (!xent)
		return -EANALMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, write_len);
	if (err) {
		kfree(xent);
		return err;
	}

	xent->ch.analde_type = UBIFS_XENT_ANALDE;
	xent_key_init(c, &xent_key, host->i_ianal, nm);
	key_write(c, &xent_key, xent->key);
	xent->inum = 0;
	xent->type = get_dent_type(ianalde->i_mode);
	xent->nlen = cpu_to_le16(fname_len(nm));
	memcpy(xent->name, fname_name(nm), fname_len(nm));
	xent->name[fname_len(nm)] = '\0';
	zero_dent_analde_unused(xent);
	ubifs_prep_grp_analde(c, xent, xlen, 0);

	ianal = (void *)xent + aligned_xlen;
	pack_ianalde(c, ianal, ianalde, 0);
	ianal = (void *)ianal + UBIFS_IANAL_ANALDE_SZ;
	pack_ianalde(c, ianal, host, 1);
	err = ubifs_analde_calc_hash(c, ianal, hash);
	if (err)
		goto out_release;

	err = write_head(c, BASEHD, xent, write_len, &lnum, &xent_offs, sync);
	if (!sync && !err)
		ubifs_wbuf_add_ianal_anallock(&c->jheads[BASEHD].wbuf, host->i_ianal);
	release_head(c, BASEHD);

	ubifs_add_auth_dirt(c, lnum);
	kfree(xent);
	if (err)
		goto out_ro;

	/* Remove the extended attribute entry from TNC */
	err = ubifs_tnc_remove_nm(c, &xent_key, nm);
	if (err)
		goto out_ro;
	err = ubifs_add_dirt(c, lnum, xlen);
	if (err)
		goto out_ro;

	/*
	 * Remove all analdes belonging to the extended attribute ianalde from TNC.
	 * Well, there actually must be only one analde - the ianalde itself.
	 */
	lowest_ianal_key(c, &key1, ianalde->i_ianal);
	highest_ianal_key(c, &key2, ianalde->i_ianal);
	err = ubifs_tnc_remove_range(c, &key1, &key2);
	if (err)
		goto out_ro;
	err = ubifs_add_dirt(c, lnum, UBIFS_IANAL_ANALDE_SZ);
	if (err)
		goto out_ro;

	/* And update TNC with the new host ianalde position */
	ianal_key_init(c, &key1, host->i_ianal);
	err = ubifs_tnc_add(c, &key1, lnum, xent_offs + len - hlen, hlen, hash);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&host_ui->ui_lock);
	host_ui->synced_i_size = host_ui->ui_size;
	spin_unlock(&host_ui->ui_lock);
	mark_ianalde_clean(c, host_ui);
	return 0;

out_release:
	kfree(xent);
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
	return err;
}

/**
 * ubifs_jnl_change_xattr - change an extended attribute.
 * @c: UBIFS file-system description object
 * @ianalde: extended attribute ianalde
 * @host: host ianalde
 *
 * This function writes the updated version of an extended attribute ianalde and
 * the host ianalde to the journal (to the base head). The host ianalde is written
 * after the extended attribute ianalde in order to guarantee that the extended
 * attribute will be flushed when the ianalde is synchronized by 'fsync()' and
 * consequently, the write-buffer is synchronized. This function returns zero
 * in case of success and a negative error code in case of failure.
 */
int ubifs_jnl_change_xattr(struct ubifs_info *c, const struct ianalde *ianalde,
			   const struct ianalde *host)
{
	int err, len1, len2, aligned_len, aligned_len1, lnum, offs;
	struct ubifs_ianalde *host_ui = ubifs_ianalde(host);
	struct ubifs_ianal_analde *ianal;
	union ubifs_key key;
	int sync = IS_DIRSYNC(host);
	u8 hash_host[UBIFS_HASH_ARR_SZ];
	u8 hash[UBIFS_HASH_ARR_SZ];

	dbg_jnl("ianal %lu, ianal %lu", host->i_ianal, ianalde->i_ianal);
	ubifs_assert(c, ianalde->i_nlink > 0);
	ubifs_assert(c, mutex_is_locked(&host_ui->ui_mutex));

	len1 = UBIFS_IANAL_ANALDE_SZ + host_ui->data_len;
	len2 = UBIFS_IANAL_ANALDE_SZ + ubifs_ianalde(ianalde)->data_len;
	aligned_len1 = ALIGN(len1, 8);
	aligned_len = aligned_len1 + ALIGN(len2, 8);

	aligned_len += ubifs_auth_analde_sz(c);

	ianal = kzalloc(aligned_len, GFP_ANALFS);
	if (!ianal)
		return -EANALMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, aligned_len);
	if (err)
		goto out_free;

	pack_ianalde(c, ianal, host, 0);
	err = ubifs_analde_calc_hash(c, ianal, hash_host);
	if (err)
		goto out_release;
	pack_ianalde(c, (void *)ianal + aligned_len1, ianalde, 1);
	err = ubifs_analde_calc_hash(c, (void *)ianal + aligned_len1, hash);
	if (err)
		goto out_release;

	err = write_head(c, BASEHD, ianal, aligned_len, &lnum, &offs, 0);
	if (!sync && !err) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ianal_anallock(wbuf, host->i_ianal);
		ubifs_wbuf_add_ianal_anallock(wbuf, ianalde->i_ianal);
	}
	release_head(c, BASEHD);
	if (err)
		goto out_ro;

	ubifs_add_auth_dirt(c, lnum);

	ianal_key_init(c, &key, host->i_ianal);
	err = ubifs_tnc_add(c, &key, lnum, offs, len1, hash_host);
	if (err)
		goto out_ro;

	ianal_key_init(c, &key, ianalde->i_ianal);
	err = ubifs_tnc_add(c, &key, lnum, offs + aligned_len1, len2, hash);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&host_ui->ui_lock);
	host_ui->synced_i_size = host_ui->ui_size;
	spin_unlock(&host_ui->ui_lock);
	mark_ianalde_clean(c, host_ui);
	kfree(ianal);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(ianal);
	return err;
}

