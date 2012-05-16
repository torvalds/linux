/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS journal.
 *
 * The journal consists of 2 parts - the log and bud LEBs. The log has fixed
 * length and position, while a bud logical eraseblock is any LEB in the main
 * area. Buds contain file system data - data nodes, inode nodes, etc. The log
 * contains only references to buds and some other stuff like commit
 * start node. The idea is that when we commit the journal, we do
 * not copy the data, the buds just become indexed. Since after the commit the
 * nodes in bud eraseblocks become leaf nodes of the file system index tree, we
 * use term "bud". Analogy is obvious, bud eraseblocks contain nodes which will
 * become leafs in the future.
 *
 * The journal is multi-headed because we want to write data to the journal as
 * optimally as possible. It is nice to have nodes belonging to the same inode
 * in one LEB, so we may write data owned by different inodes to different
 * journal heads, although at present only one data head is used.
 *
 * For recovery reasons, the base head contains all inode nodes, all directory
 * entry nodes and all truncate nodes. This means that the other heads contain
 * only data nodes.
 *
 * Bud LEBs may be half-indexed. For example, if the bud was not full at the
 * time of commit, the bud is retained to continue to be used in the journal,
 * even though the "front" of the LEB is now indexed. In that case, the log
 * reference contains the offset where the bud starts for the purposes of the
 * journal.
 *
 * The journal size has to be limited, because the larger is the journal, the
 * longer it takes to mount UBIFS (scanning the journal) and the more memory it
 * takes (indexing in the TNC).
 *
 * All the journal write operations like 'ubifs_jnl_update()' here, which write
 * multiple UBIFS nodes to the journal at one go, are atomic with respect to
 * unclean reboots. Should the unclean reboot happen, the recovery code drops
 * all the nodes.
 */

#include "ubifs.h"

/**
 * zero_ino_node_unused - zero out unused fields of an on-flash inode node.
 * @ino: the inode to zero out
 */
static inline void zero_ino_node_unused(struct ubifs_ino_node *ino)
{
	memset(ino->padding1, 0, 4);
	memset(ino->padding2, 0, 26);
}

/**
 * zero_dent_node_unused - zero out unused fields of an on-flash directory
 *                         entry node.
 * @dent: the directory entry to zero out
 */
static inline void zero_dent_node_unused(struct ubifs_dent_node *dent)
{
	dent->padding1 = 0;
	memset(dent->padding2, 0, 4);
}

/**
 * zero_data_node_unused - zero out unused fields of an on-flash data node.
 * @data: the data node to zero out
 */
static inline void zero_data_node_unused(struct ubifs_data_node *data)
{
	memset(data->padding, 0, 2);
}

/**
 * zero_trun_node_unused - zero out unused fields of an on-flash truncation
 *                         node.
 * @trun: the truncation node to zero out
 */
static inline void zero_trun_node_unused(struct ubifs_trun_node *trun)
{
	memset(trun->padding, 0, 12);
}

/**
 * reserve_space - reserve space in the journal.
 * @c: UBIFS file-system description object
 * @jhead: journal head number
 * @len: node length
 *
 * This function reserves space in journal head @head. If the reservation
 * succeeded, the journal head stays locked and later has to be unlocked using
 * 'release_head()'. 'write_node()' and 'write_head()' functions also unlock
 * it. Returns zero in case of success, %-EAGAIN if commit has to be done, and
 * other negative error codes in case of other failures.
 */
static int reserve_space(struct ubifs_info *c, int jhead, int len)
{
	int err = 0, err1, retries = 0, avail, lnum, offs, squeeze;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	/*
	 * Typically, the base head has smaller nodes written to it, so it is
	 * better to try to allocate space at the ends of eraseblocks. This is
	 * what the squeeze parameter does.
	 */
	ubifs_assert(!c->ro_media && !c->ro_mount);
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
	 * Write buffer wasn't seek'ed or there is no enough space - look for an
	 * LEB with some empty space.
	 */
	lnum = ubifs_find_free_space(c, len, &offs, squeeze);
	if (lnum >= 0)
		goto out;

	err = lnum;
	if (err != -ENOSPC)
		goto out_unlock;

	/*
	 * No free space, we have to run garbage collector to make
	 * some. But the write-buffer mutex has to be unlocked because
	 * GC also takes it.
	 */
	dbg_jnl("no free space in jhead %s, run GC", dbg_jhead(jhead));
	mutex_unlock(&wbuf->io_mutex);

	lnum = ubifs_garbage_collect(c, 0);
	if (lnum < 0) {
		err = lnum;
		if (err != -ENOSPC)
			return err;

		/*
		 * GC could not make a free LEB. But someone else may
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

		dbg_jnl("return -ENOSPC");
		return err;
	}

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	dbg_jnl("got LEB %d for jhead %s", lnum, dbg_jhead(jhead));
	avail = c->leb_size - wbuf->offs - wbuf->used;

	if (wbuf->lnum != -1 && avail >= len) {
		/*
		 * Someone else has switched the journal head and we have
		 * enough space now. This happens when more than one process is
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
	 * reference node for the last bud (@lnum) is written but before the
	 * write-buffer data are written to the next-to-last bud
	 * (@wbuf->lnum). And the effect would be that the recovery would see
	 * that there is corruption in the next-to-last bud.
	 */
	err = ubifs_wbuf_sync_nolock(wbuf);
	if (err)
		goto out_return;
	err = ubifs_add_bud_to_log(c, jhead, lnum, offs);
	if (err)
		goto out_return;
	err = ubifs_wbuf_seek_nolock(wbuf, lnum, offs, wbuf->dtype);
	if (err)
		goto out_unlock;

	return 0;

out_unlock:
	mutex_unlock(&wbuf->io_mutex);
	return err;

out_return:
	/* An error occurred and the LEB has to be returned to lprops */
	ubifs_assert(err < 0);
	err1 = ubifs_return_leb(c, lnum);
	if (err1 && err == -EAGAIN)
		/*
		 * Return original error code only if it is not %-EAGAIN,
		 * which is not really an error. Otherwise, return the error
		 * code of 'ubifs_return_leb()'.
		 */
		err = err1;
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

/**
 * write_node - write node to a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @node: node to write
 * @len: node length
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 *
 * This function writes a node to reserved space of journal head @jhead.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int write_node(struct ubifs_info *c, int jhead, void *node, int len,
		      int *lnum, int *offs)
{
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	ubifs_assert(jhead != GCHD);

	*lnum = c->jheads[jhead].wbuf.lnum;
	*offs = c->jheads[jhead].wbuf.offs + c->jheads[jhead].wbuf.used;

	dbg_jnl("jhead %s, LEB %d:%d, len %d",
		dbg_jhead(jhead), *lnum, *offs, len);
	ubifs_prepare_node(c, node, len, 0);

	return ubifs_wbuf_write_nolock(wbuf, node, len);
}

/**
 * write_head - write data to a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @buf: buffer to write
 * @len: length to write
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 * @sync: non-zero if the write-buffer has to by synchronized
 *
 * This function is the same as 'write_node()' but it does not assume the
 * buffer it is writing is a node, so it does not prepare it (which means
 * initializing common header and calculating CRC).
 */
static int write_head(struct ubifs_info *c, int jhead, void *buf, int len,
		      int *lnum, int *offs, int sync)
{
	int err;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	ubifs_assert(jhead != GCHD);

	*lnum = c->jheads[jhead].wbuf.lnum;
	*offs = c->jheads[jhead].wbuf.offs + c->jheads[jhead].wbuf.used;
	dbg_jnl("jhead %s, LEB %d:%d, len %d",
		dbg_jhead(jhead), *lnum, *offs, len);

	err = ubifs_wbuf_write_nolock(wbuf, buf, len);
	if (err)
		return err;
	if (sync)
		err = ubifs_wbuf_sync_nolock(wbuf);
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
 * Note, the journal head may be unlocked as soon as the data is written, while
 * the commit lock has to be released after the data has been added to the
 * TNC.
 */
static int make_reservation(struct ubifs_info *c, int jhead, int len)
{
	int err, cmt_retries = 0, nospc_retries = 0;

again:
	down_read(&c->commit_sem);
	err = reserve_space(c, jhead, len);
	if (!err)
		return 0;
	up_read(&c->commit_sem);

	if (err == -ENOSPC) {
		/*
		 * GC could not make any progress. We should try to commit
		 * once because it could make some dirty space and GC would
		 * make progress, so make the error -EAGAIN so that the below
		 * will commit and re-try.
		 */
		if (nospc_retries++ < 2) {
			dbg_jnl("no space, retry");
			err = -EAGAIN;
		}

		/*
		 * This means that the budgeting is incorrect. We always have
		 * to be able to write to the media, because all operations are
		 * budgeted. Deletions are not budgeted, though, but we reserve
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
		 * This should not happen unless the journal size limitations
		 * are too tough.
		 */
		ubifs_err("stuck in space allocation");
		err = -ENOSPC;
		goto out;
	} else if (cmt_retries > 32)
		ubifs_warn("too many space allocation re-tries (%d)",
			   cmt_retries);

	dbg_jnl("-EAGAIN, commit and retry (retried %d times)",
		cmt_retries);
	cmt_retries += 1;

	err = ubifs_run_commit(c);
	if (err)
		return err;
	goto again;

out:
	ubifs_err("cannot reserve %d bytes in jhead %d, error %d",
		  len, jhead, err);
	if (err == -ENOSPC) {
		/* This are some budgeting problems, print useful information */
		down_write(&c->commit_sem);
		dump_stack();
		dbg_dump_budg(c, &c->bi);
		dbg_dump_lprops(c);
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
 * get_dent_type - translate VFS inode mode to UBIFS directory entry type.
 * @mode: inode mode
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
 * pack_inode - pack an inode node.
 * @c: UBIFS file-system description object
 * @ino: buffer in which to pack inode node
 * @inode: inode to pack
 * @last: indicates the last node of the group
 */
static void pack_inode(struct ubifs_info *c, struct ubifs_ino_node *ino,
		       const struct inode *inode, int last)
{
	int data_len = 0, last_reference = !inode->i_nlink;
	struct ubifs_inode *ui = ubifs_inode(inode);

	ino->ch.node_type = UBIFS_INO_NODE;
	ino_key_init_flash(c, &ino->key, inode->i_ino);
	ino->creat_sqnum = cpu_to_le64(ui->creat_sqnum);
	ino->atime_sec  = cpu_to_le64(inode->i_atime.tv_sec);
	ino->atime_nsec = cpu_to_le32(inode->i_atime.tv_nsec);
	ino->ctime_sec  = cpu_to_le64(inode->i_ctime.tv_sec);
	ino->ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	ino->mtime_sec  = cpu_to_le64(inode->i_mtime.tv_sec);
	ino->mtime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);
	ino->uid   = cpu_to_le32(inode->i_uid);
	ino->gid   = cpu_to_le32(inode->i_gid);
	ino->mode  = cpu_to_le32(inode->i_mode);
	ino->flags = cpu_to_le32(ui->flags);
	ino->size  = cpu_to_le64(ui->ui_size);
	ino->nlink = cpu_to_le32(inode->i_nlink);
	ino->compr_type  = cpu_to_le16(ui->compr_type);
	ino->data_len    = cpu_to_le32(ui->data_len);
	ino->xattr_cnt   = cpu_to_le32(ui->xattr_cnt);
	ino->xattr_size  = cpu_to_le32(ui->xattr_size);
	ino->xattr_names = cpu_to_le32(ui->xattr_names);
	zero_ino_node_unused(ino);

	/*
	 * Drop the attached data if this is a deletion inode, the data is not
	 * needed anymore.
	 */
	if (!last_reference) {
		memcpy(ino->data, ui->data, ui->data_len);
		data_len = ui->data_len;
	}

	ubifs_prep_grp_node(c, ino, UBIFS_INO_NODE_SZ + data_len, last);
}

/**
 * mark_inode_clean - mark UBIFS inode as clean.
 * @c: UBIFS file-system description object
 * @ui: UBIFS inode to mark as clean
 *
 * This helper function marks UBIFS inode @ui as clean by cleaning the
 * @ui->dirty flag and releasing its budget. Note, VFS may still treat the
 * inode as dirty and try to write it back, but 'ubifs_write_inode()' would
 * just do nothing.
 */
static void mark_inode_clean(struct ubifs_info *c, struct ubifs_inode *ui)
{
	if (ui->dirty)
		ubifs_release_dirty_inode_budget(c, ui);
	ui->dirty = 0;
}

/**
 * ubifs_jnl_update - update inode.
 * @c: UBIFS file-system description object
 * @dir: parent inode or host inode in case of extended attributes
 * @nm: directory entry name
 * @inode: inode to update
 * @deletion: indicates a directory entry deletion i.e unlink or rmdir
 * @xent: non-zero if the directory entry is an extended attribute entry
 *
 * This function updates an inode by writing a directory entry (or extended
 * attribute entry), the inode itself, and the parent directory inode (or the
 * host inode) to the journal.
 *
 * The function writes the host inode @dir last, which is important in case of
 * extended attributes. Indeed, then we guarantee that if the host inode gets
 * synchronized (with 'fsync()'), and the write-buffer it sits in gets flushed,
 * the extended attribute inode gets flushed too. And this is exactly what the
 * user expects - synchronizing the host inode synchronizes its extended
 * attributes. Similarly, this guarantees that if @dir is synchronized, its
 * directory entry corresponding to @nm gets synchronized too.
 *
 * If the inode (@inode) or the parent directory (@dir) are synchronous, this
 * function synchronizes the write-buffer.
 *
 * This function marks the @dir and @inode inodes as clean and returns zero on
 * success. In case of failure, a negative error code is returned.
 */
int ubifs_jnl_update(struct ubifs_info *c, const struct inode *dir,
		     const struct qstr *nm, const struct inode *inode,
		     int deletion, int xent)
{
	int err, dlen, ilen, len, lnum, ino_offs, dent_offs;
	int aligned_dlen, aligned_ilen, sync = IS_DIRSYNC(dir);
	int last_reference = !!(deletion && inode->i_nlink == 0);
	struct ubifs_inode *ui = ubifs_inode(inode);
	struct ubifs_inode *dir_ui = ubifs_inode(dir);
	struct ubifs_dent_node *dent;
	struct ubifs_ino_node *ino;
	union ubifs_key dent_key, ino_key;

	dbg_jnl("ino %lu, dent '%.*s', data len %d in dir ino %lu",
		inode->i_ino, nm->len, nm->name, ui->data_len, dir->i_ino);
	ubifs_assert(dir_ui->data_len == 0);
	ubifs_assert(mutex_is_locked(&dir_ui->ui_mutex));

	dlen = UBIFS_DENT_NODE_SZ + nm->len + 1;
	ilen = UBIFS_INO_NODE_SZ;

	/*
	 * If the last reference to the inode is being deleted, then there is
	 * no need to attach and write inode data, it is being deleted anyway.
	 * And if the inode is being deleted, no need to synchronize
	 * write-buffer even if the inode is synchronous.
	 */
	if (!last_reference) {
		ilen += ui->data_len;
		sync |= IS_SYNC(inode);
	}

	aligned_dlen = ALIGN(dlen, 8);
	aligned_ilen = ALIGN(ilen, 8);
	len = aligned_dlen + aligned_ilen + UBIFS_INO_NODE_SZ;
	dent = kmalloc(len, GFP_NOFS);
	if (!dent)
		return -ENOMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	if (!xent) {
		dent->ch.node_type = UBIFS_DENT_NODE;
		dent_key_init(c, &dent_key, dir->i_ino, nm);
	} else {
		dent->ch.node_type = UBIFS_XENT_NODE;
		xent_key_init(c, &dent_key, dir->i_ino, nm);
	}

	key_write(c, &dent_key, dent->key);
	dent->inum = deletion ? 0 : cpu_to_le64(inode->i_ino);
	dent->type = get_dent_type(inode->i_mode);
	dent->nlen = cpu_to_le16(nm->len);
	memcpy(dent->name, nm->name, nm->len);
	dent->name[nm->len] = '\0';
	zero_dent_node_unused(dent);
	ubifs_prep_grp_node(c, dent, dlen, 0);

	ino = (void *)dent + aligned_dlen;
	pack_inode(c, ino, inode, 0);
	ino = (void *)ino + aligned_ilen;
	pack_inode(c, ino, dir, 1);

	if (last_reference) {
		err = ubifs_add_orphan(c, inode->i_ino);
		if (err) {
			release_head(c, BASEHD);
			goto out_finish;
		}
		ui->del_cmtno = c->cmt_no;
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &dent_offs, sync);
	if (err)
		goto out_release;
	if (!sync) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ino_nolock(wbuf, inode->i_ino);
		ubifs_wbuf_add_ino_nolock(wbuf, dir->i_ino);
	}
	release_head(c, BASEHD);
	kfree(dent);

	if (deletion) {
		err = ubifs_tnc_remove_nm(c, &dent_key, nm);
		if (err)
			goto out_ro;
		err = ubifs_add_dirt(c, lnum, dlen);
	} else
		err = ubifs_tnc_add_nm(c, &dent_key, lnum, dent_offs, dlen, nm);
	if (err)
		goto out_ro;

	/*
	 * Note, we do not remove the inode from TNC even if the last reference
	 * to it has just been deleted, because the inode may still be opened.
	 * Instead, the inode has been added to orphan lists and the orphan
	 * subsystem will take further care about it.
	 */
	ino_key_init(c, &ino_key, inode->i_ino);
	ino_offs = dent_offs + aligned_dlen;
	err = ubifs_tnc_add(c, &ino_key, lnum, ino_offs, ilen);
	if (err)
		goto out_ro;

	ino_key_init(c, &ino_key, dir->i_ino);
	ino_offs += aligned_ilen;
	err = ubifs_tnc_add(c, &ino_key, lnum, ino_offs, UBIFS_INO_NODE_SZ);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&ui->ui_lock);
	ui->synced_i_size = ui->ui_size;
	spin_unlock(&ui->ui_lock);
	mark_inode_clean(c, ui);
	mark_inode_clean(c, dir_ui);
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
	if (last_reference)
		ubifs_delete_orphan(c, inode->i_ino);
	finish_reservation(c);
	return err;
}

/**
 * ubifs_jnl_write_data - write a data node to the journal.
 * @c: UBIFS file-system description object
 * @inode: inode the data node belongs to
 * @key: node key
 * @buf: buffer to write
 * @len: data length (must not exceed %UBIFS_BLOCK_SIZE)
 *
 * This function writes a data node to the journal. Returns %0 if the data node
 * was successfully written, and a negative error code in case of failure.
 */
int ubifs_jnl_write_data(struct ubifs_info *c, const struct inode *inode,
			 const union ubifs_key *key, const void *buf, int len)
{
	struct ubifs_data_node *data;
	int err, lnum, offs, compr_type, out_len;
	int dlen = COMPRESSED_DATA_NODE_BUF_SZ, allocated = 1;
	struct ubifs_inode *ui = ubifs_inode(inode);

	dbg_jnlk(key, "ino %lu, blk %u, len %d, key ",
		(unsigned long)key_inum(c, key), key_block(c, key), len);
	ubifs_assert(len <= UBIFS_BLOCK_SIZE);

	data = kmalloc(dlen, GFP_NOFS | __GFP_NOWARN);
	if (!data) {
		/*
		 * Fall-back to the write reserve buffer. Note, we might be
		 * currently on the memory reclaim path, when the kernel is
		 * trying to free some memory by writing out dirty pages. The
		 * write reserve buffer helps us to guarantee that we are
		 * always able to write the data.
		 */
		allocated = 0;
		mutex_lock(&c->write_reserve_mutex);
		data = c->write_reserve_buf;
	}

	data->ch.node_type = UBIFS_DATA_NODE;
	key_write(c, key, &data->key);
	data->size = cpu_to_le32(len);
	zero_data_node_unused(data);

	if (!(ui->flags & UBIFS_COMPR_FL))
		/* Compression is disabled for this inode */
		compr_type = UBIFS_COMPR_NONE;
	else
		compr_type = ui->compr_type;

	out_len = dlen - UBIFS_DATA_NODE_SZ;
	ubifs_compress(buf, len, &data->data, &out_len, &compr_type);
	ubifs_assert(out_len <= UBIFS_BLOCK_SIZE);

	dlen = UBIFS_DATA_NODE_SZ + out_len;
	data->compr_type = cpu_to_le16(compr_type);

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, DATAHD, dlen);
	if (err)
		goto out_free;

	err = write_node(c, DATAHD, data, dlen, &lnum, &offs);
	if (err)
		goto out_release;
	ubifs_wbuf_add_ino_nolock(&c->jheads[DATAHD].wbuf, key_inum(c, key));
	release_head(c, DATAHD);

	err = ubifs_tnc_add(c, key, lnum, offs, dlen);
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
 * ubifs_jnl_write_inode - flush inode to the journal.
 * @c: UBIFS file-system description object
 * @inode: inode to flush
 *
 * This function writes inode @inode to the journal. If the inode is
 * synchronous, it also synchronizes the write-buffer. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubifs_jnl_write_inode(struct ubifs_info *c, const struct inode *inode)
{
	int err, lnum, offs;
	struct ubifs_ino_node *ino;
	struct ubifs_inode *ui = ubifs_inode(inode);
	int sync = 0, len = UBIFS_INO_NODE_SZ, last_reference = !inode->i_nlink;

	dbg_jnl("ino %lu, nlink %u", inode->i_ino, inode->i_nlink);

	/*
	 * If the inode is being deleted, do not write the attached data. No
	 * need to synchronize the write-buffer either.
	 */
	if (!last_reference) {
		len += ui->data_len;
		sync = IS_SYNC(inode);
	}
	ino = kmalloc(len, GFP_NOFS);
	if (!ino)
		return -ENOMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	pack_inode(c, ino, inode, 1);
	err = write_head(c, BASEHD, ino, len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync)
		ubifs_wbuf_add_ino_nolock(&c->jheads[BASEHD].wbuf,
					  inode->i_ino);
	release_head(c, BASEHD);

	if (last_reference) {
		err = ubifs_tnc_remove_ino(c, inode->i_ino);
		if (err)
			goto out_ro;
		ubifs_delete_orphan(c, inode->i_ino);
		err = ubifs_add_dirt(c, lnum, len);
	} else {
		union ubifs_key key;

		ino_key_init(c, &key, inode->i_ino);
		err = ubifs_tnc_add(c, &key, lnum, offs, len);
	}
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&ui->ui_lock);
	ui->synced_i_size = ui->ui_size;
	spin_unlock(&ui->ui_lock);
	kfree(ino);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(ino);
	return err;
}

/**
 * ubifs_jnl_delete_inode - delete an inode.
 * @c: UBIFS file-system description object
 * @inode: inode to delete
 *
 * This function deletes inode @inode which includes removing it from orphans,
 * deleting it from TNC and, in some cases, writing a deletion inode to the
 * journal.
 *
 * When regular file inodes are unlinked or a directory inode is removed, the
 * 'ubifs_jnl_update()' function writes a corresponding deletion inode and
 * direntry to the media, and adds the inode to orphans. After this, when the
 * last reference to this inode has been dropped, this function is called. In
 * general, it has to write one more deletion inode to the media, because if
 * a commit happened between 'ubifs_jnl_update()' and
 * 'ubifs_jnl_delete_inode()', the deletion inode is not in the journal
 * anymore, and in fact it might not be on the flash anymore, because it might
 * have been garbage-collected already. And for optimization reasons UBIFS does
 * not read the orphan area if it has been unmounted cleanly, so it would have
 * no indication in the journal that there is a deleted inode which has to be
 * removed from TNC.
 *
 * However, if there was no commit between 'ubifs_jnl_update()' and
 * 'ubifs_jnl_delete_inode()', then there is no need to write the deletion
 * inode to the media for the second time. And this is quite a typical case.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
int ubifs_jnl_delete_inode(struct ubifs_info *c, const struct inode *inode)
{
	int err;
	struct ubifs_inode *ui = ubifs_inode(inode);

	ubifs_assert(inode->i_nlink == 0);

	if (ui->del_cmtno != c->cmt_no)
		/* A commit happened for sure */
		return ubifs_jnl_write_inode(c, inode);

	down_read(&c->commit_sem);
	/*
	 * Check commit number again, because the first test has been done
	 * without @c->commit_sem, so a commit might have happened.
	 */
	if (ui->del_cmtno != c->cmt_no) {
		up_read(&c->commit_sem);
		return ubifs_jnl_write_inode(c, inode);
	}

	err = ubifs_tnc_remove_ino(c, inode->i_ino);
	if (err)
		ubifs_ro_mode(c, err);
	else
		ubifs_delete_orphan(c, inode->i_ino);
	up_read(&c->commit_sem);
	return err;
}

/**
 * ubifs_jnl_rename - rename a directory entry.
 * @c: UBIFS file-system description object
 * @old_dir: parent inode of directory entry to rename
 * @old_dentry: directory entry to rename
 * @new_dir: parent inode of directory entry to rename
 * @new_dentry: new directory entry (or directory entry to replace)
 * @sync: non-zero if the write-buffer has to be synchronized
 *
 * This function implements the re-name operation which may involve writing up
 * to 3 inodes and 2 directory entries. It marks the written inodes as clean
 * and returns zero on success. In case of failure, a negative error code is
 * returned.
 */
int ubifs_jnl_rename(struct ubifs_info *c, const struct inode *old_dir,
		     const struct dentry *old_dentry,
		     const struct inode *new_dir,
		     const struct dentry *new_dentry, int sync)
{
	void *p;
	union ubifs_key key;
	struct ubifs_dent_node *dent, *dent2;
	int err, dlen1, dlen2, ilen, lnum, offs, len;
	const struct inode *old_inode = old_dentry->d_inode;
	const struct inode *new_inode = new_dentry->d_inode;
	int aligned_dlen1, aligned_dlen2, plen = UBIFS_INO_NODE_SZ;
	int last_reference = !!(new_inode && new_inode->i_nlink == 0);
	int move = (old_dir != new_dir);
	struct ubifs_inode *uninitialized_var(new_ui);

	dbg_jnl("dent '%.*s' in dir ino %lu to dent '%.*s' in dir ino %lu",
		old_dentry->d_name.len, old_dentry->d_name.name,
		old_dir->i_ino, new_dentry->d_name.len,
		new_dentry->d_name.name, new_dir->i_ino);
	ubifs_assert(ubifs_inode(old_dir)->data_len == 0);
	ubifs_assert(ubifs_inode(new_dir)->data_len == 0);
	ubifs_assert(mutex_is_locked(&ubifs_inode(old_dir)->ui_mutex));
	ubifs_assert(mutex_is_locked(&ubifs_inode(new_dir)->ui_mutex));

	dlen1 = UBIFS_DENT_NODE_SZ + new_dentry->d_name.len + 1;
	dlen2 = UBIFS_DENT_NODE_SZ + old_dentry->d_name.len + 1;
	if (new_inode) {
		new_ui = ubifs_inode(new_inode);
		ubifs_assert(mutex_is_locked(&new_ui->ui_mutex));
		ilen = UBIFS_INO_NODE_SZ;
		if (!last_reference)
			ilen += new_ui->data_len;
	} else
		ilen = 0;

	aligned_dlen1 = ALIGN(dlen1, 8);
	aligned_dlen2 = ALIGN(dlen2, 8);
	len = aligned_dlen1 + aligned_dlen2 + ALIGN(ilen, 8) + ALIGN(plen, 8);
	if (old_dir != new_dir)
		len += plen;
	dent = kmalloc(len, GFP_NOFS);
	if (!dent)
		return -ENOMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	/* Make new dent */
	dent->ch.node_type = UBIFS_DENT_NODE;
	dent_key_init_flash(c, &dent->key, new_dir->i_ino, &new_dentry->d_name);
	dent->inum = cpu_to_le64(old_inode->i_ino);
	dent->type = get_dent_type(old_inode->i_mode);
	dent->nlen = cpu_to_le16(new_dentry->d_name.len);
	memcpy(dent->name, new_dentry->d_name.name, new_dentry->d_name.len);
	dent->name[new_dentry->d_name.len] = '\0';
	zero_dent_node_unused(dent);
	ubifs_prep_grp_node(c, dent, dlen1, 0);

	/* Make deletion dent */
	dent2 = (void *)dent + aligned_dlen1;
	dent2->ch.node_type = UBIFS_DENT_NODE;
	dent_key_init_flash(c, &dent2->key, old_dir->i_ino,
			    &old_dentry->d_name);
	dent2->inum = 0;
	dent2->type = DT_UNKNOWN;
	dent2->nlen = cpu_to_le16(old_dentry->d_name.len);
	memcpy(dent2->name, old_dentry->d_name.name, old_dentry->d_name.len);
	dent2->name[old_dentry->d_name.len] = '\0';
	zero_dent_node_unused(dent2);
	ubifs_prep_grp_node(c, dent2, dlen2, 0);

	p = (void *)dent2 + aligned_dlen2;
	if (new_inode) {
		pack_inode(c, p, new_inode, 0);
		p += ALIGN(ilen, 8);
	}

	if (!move)
		pack_inode(c, p, old_dir, 1);
	else {
		pack_inode(c, p, old_dir, 0);
		p += ALIGN(plen, 8);
		pack_inode(c, p, new_dir, 1);
	}

	if (last_reference) {
		err = ubifs_add_orphan(c, new_inode->i_ino);
		if (err) {
			release_head(c, BASEHD);
			goto out_finish;
		}
		new_ui->del_cmtno = c->cmt_no;
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ino_nolock(wbuf, new_dir->i_ino);
		ubifs_wbuf_add_ino_nolock(wbuf, old_dir->i_ino);
		if (new_inode)
			ubifs_wbuf_add_ino_nolock(&c->jheads[BASEHD].wbuf,
						  new_inode->i_ino);
	}
	release_head(c, BASEHD);

	dent_key_init(c, &key, new_dir->i_ino, &new_dentry->d_name);
	err = ubifs_tnc_add_nm(c, &key, lnum, offs, dlen1, &new_dentry->d_name);
	if (err)
		goto out_ro;

	err = ubifs_add_dirt(c, lnum, dlen2);
	if (err)
		goto out_ro;

	dent_key_init(c, &key, old_dir->i_ino, &old_dentry->d_name);
	err = ubifs_tnc_remove_nm(c, &key, &old_dentry->d_name);
	if (err)
		goto out_ro;

	offs += aligned_dlen1 + aligned_dlen2;
	if (new_inode) {
		ino_key_init(c, &key, new_inode->i_ino);
		err = ubifs_tnc_add(c, &key, lnum, offs, ilen);
		if (err)
			goto out_ro;
		offs += ALIGN(ilen, 8);
	}

	ino_key_init(c, &key, old_dir->i_ino);
	err = ubifs_tnc_add(c, &key, lnum, offs, plen);
	if (err)
		goto out_ro;

	if (old_dir != new_dir) {
		offs += ALIGN(plen, 8);
		ino_key_init(c, &key, new_dir->i_ino);
		err = ubifs_tnc_add(c, &key, lnum, offs, plen);
		if (err)
			goto out_ro;
	}

	finish_reservation(c);
	if (new_inode) {
		mark_inode_clean(c, new_ui);
		spin_lock(&new_ui->ui_lock);
		new_ui->synced_i_size = new_ui->ui_size;
		spin_unlock(&new_ui->ui_lock);
	}
	mark_inode_clean(c, ubifs_inode(old_dir));
	if (move)
		mark_inode_clean(c, ubifs_inode(new_dir));
	kfree(dent);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	if (last_reference)
		ubifs_delete_orphan(c, new_inode->i_ino);
out_finish:
	finish_reservation(c);
out_free:
	kfree(dent);
	return err;
}

/**
 * recomp_data_node - re-compress a truncated data node.
 * @dn: data node to re-compress
 * @new_len: new length
 *
 * This function is used when an inode is truncated and the last data node of
 * the inode has to be re-compressed and re-written.
 */
static int recomp_data_node(struct ubifs_data_node *dn, int *new_len)
{
	void *buf;
	int err, len, compr_type, out_len;

	out_len = le32_to_cpu(dn->size);
	buf = kmalloc(out_len * WORST_COMPR_FACTOR, GFP_NOFS);
	if (!buf)
		return -ENOMEM;

	len = le32_to_cpu(dn->ch.len) - UBIFS_DATA_NODE_SZ;
	compr_type = le16_to_cpu(dn->compr_type);
	err = ubifs_decompress(&dn->data, len, buf, &out_len, compr_type);
	if (err)
		goto out;

	ubifs_compress(buf, *new_len, &dn->data, &out_len, &compr_type);
	ubifs_assert(out_len <= UBIFS_BLOCK_SIZE);
	dn->compr_type = cpu_to_le16(compr_type);
	dn->size = cpu_to_le32(*new_len);
	*new_len = UBIFS_DATA_NODE_SZ + out_len;
out:
	kfree(buf);
	return err;
}

/**
 * ubifs_jnl_truncate - update the journal for a truncation.
 * @c: UBIFS file-system description object
 * @inode: inode to truncate
 * @old_size: old size
 * @new_size: new size
 *
 * When the size of a file decreases due to truncation, a truncation node is
 * written, the journal tree is updated, and the last data block is re-written
 * if it has been affected. The inode is also updated in order to synchronize
 * the new inode size.
 *
 * This function marks the inode as clean and returns zero on success. In case
 * of failure, a negative error code is returned.
 */
int ubifs_jnl_truncate(struct ubifs_info *c, const struct inode *inode,
		       loff_t old_size, loff_t new_size)
{
	union ubifs_key key, to_key;
	struct ubifs_ino_node *ino;
	struct ubifs_trun_node *trun;
	struct ubifs_data_node *uninitialized_var(dn);
	int err, dlen, len, lnum, offs, bit, sz, sync = IS_SYNC(inode);
	struct ubifs_inode *ui = ubifs_inode(inode);
	ino_t inum = inode->i_ino;
	unsigned int blk;

	dbg_jnl("ino %lu, size %lld -> %lld",
		(unsigned long)inum, old_size, new_size);
	ubifs_assert(!ui->data_len);
	ubifs_assert(S_ISREG(inode->i_mode));
	ubifs_assert(mutex_is_locked(&ui->ui_mutex));

	sz = UBIFS_TRUN_NODE_SZ + UBIFS_INO_NODE_SZ +
	     UBIFS_MAX_DATA_NODE_SZ * WORST_COMPR_FACTOR;
	ino = kmalloc(sz, GFP_NOFS);
	if (!ino)
		return -ENOMEM;

	trun = (void *)ino + UBIFS_INO_NODE_SZ;
	trun->ch.node_type = UBIFS_TRUN_NODE;
	trun->inum = cpu_to_le32(inum);
	trun->old_size = cpu_to_le64(old_size);
	trun->new_size = cpu_to_le64(new_size);
	zero_trun_node_unused(trun);

	dlen = new_size & (UBIFS_BLOCK_SIZE - 1);
	if (dlen) {
		/* Get last data block so it can be truncated */
		dn = (void *)trun + UBIFS_TRUN_NODE_SZ;
		blk = new_size >> UBIFS_BLOCK_SHIFT;
		data_key_init(c, &key, inum, blk);
		dbg_jnlk(&key, "last block key ");
		err = ubifs_tnc_lookup(c, &key, dn);
		if (err == -ENOENT)
			dlen = 0; /* Not found (so it is a hole) */
		else if (err)
			goto out_free;
		else {
			if (le32_to_cpu(dn->size) <= dlen)
				dlen = 0; /* Nothing to do */
			else {
				int compr_type = le16_to_cpu(dn->compr_type);

				if (compr_type != UBIFS_COMPR_NONE) {
					err = recomp_data_node(dn, &dlen);
					if (err)
						goto out_free;
				} else {
					dn->size = cpu_to_le32(dlen);
					dlen += UBIFS_DATA_NODE_SZ;
				}
				zero_data_node_unused(dn);
			}
		}
	}

	/* Must make reservation before allocating sequence numbers */
	len = UBIFS_TRUN_NODE_SZ + UBIFS_INO_NODE_SZ;
	if (dlen)
		len += dlen;
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	pack_inode(c, ino, inode, 0);
	ubifs_prep_grp_node(c, trun, UBIFS_TRUN_NODE_SZ, dlen ? 0 : 1);
	if (dlen)
		ubifs_prep_grp_node(c, dn, dlen, 1);

	err = write_head(c, BASEHD, ino, len, &lnum, &offs, sync);
	if (err)
		goto out_release;
	if (!sync)
		ubifs_wbuf_add_ino_nolock(&c->jheads[BASEHD].wbuf, inum);
	release_head(c, BASEHD);

	if (dlen) {
		sz = offs + UBIFS_INO_NODE_SZ + UBIFS_TRUN_NODE_SZ;
		err = ubifs_tnc_add(c, &key, lnum, sz, dlen);
		if (err)
			goto out_ro;
	}

	ino_key_init(c, &key, inum);
	err = ubifs_tnc_add(c, &key, lnum, offs, UBIFS_INO_NODE_SZ);
	if (err)
		goto out_ro;

	err = ubifs_add_dirt(c, lnum, UBIFS_TRUN_NODE_SZ);
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
	mark_inode_clean(c, ui);
	kfree(ino);
	return 0;

out_release:
	release_head(c, BASEHD);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(ino);
	return err;
}


/**
 * ubifs_jnl_delete_xattr - delete an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host inode
 * @inode: extended attribute inode
 * @nm: extended attribute entry name
 *
 * This function delete an extended attribute which is very similar to
 * un-linking regular files - it writes a deletion xentry, a deletion inode and
 * updates the target inode. Returns zero in case of success and a negative
 * error code in case of failure.
 */
int ubifs_jnl_delete_xattr(struct ubifs_info *c, const struct inode *host,
			   const struct inode *inode, const struct qstr *nm)
{
	int err, xlen, hlen, len, lnum, xent_offs, aligned_xlen;
	struct ubifs_dent_node *xent;
	struct ubifs_ino_node *ino;
	union ubifs_key xent_key, key1, key2;
	int sync = IS_DIRSYNC(host);
	struct ubifs_inode *host_ui = ubifs_inode(host);

	dbg_jnl("host %lu, xattr ino %lu, name '%s', data len %d",
		host->i_ino, inode->i_ino, nm->name,
		ubifs_inode(inode)->data_len);
	ubifs_assert(inode->i_nlink == 0);
	ubifs_assert(mutex_is_locked(&host_ui->ui_mutex));

	/*
	 * Since we are deleting the inode, we do not bother to attach any data
	 * to it and assume its length is %UBIFS_INO_NODE_SZ.
	 */
	xlen = UBIFS_DENT_NODE_SZ + nm->len + 1;
	aligned_xlen = ALIGN(xlen, 8);
	hlen = host_ui->data_len + UBIFS_INO_NODE_SZ;
	len = aligned_xlen + UBIFS_INO_NODE_SZ + ALIGN(hlen, 8);

	xent = kmalloc(len, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err) {
		kfree(xent);
		return err;
	}

	xent->ch.node_type = UBIFS_XENT_NODE;
	xent_key_init(c, &xent_key, host->i_ino, nm);
	key_write(c, &xent_key, xent->key);
	xent->inum = 0;
	xent->type = get_dent_type(inode->i_mode);
	xent->nlen = cpu_to_le16(nm->len);
	memcpy(xent->name, nm->name, nm->len);
	xent->name[nm->len] = '\0';
	zero_dent_node_unused(xent);
	ubifs_prep_grp_node(c, xent, xlen, 0);

	ino = (void *)xent + aligned_xlen;
	pack_inode(c, ino, inode, 0);
	ino = (void *)ino + UBIFS_INO_NODE_SZ;
	pack_inode(c, ino, host, 1);

	err = write_head(c, BASEHD, xent, len, &lnum, &xent_offs, sync);
	if (!sync && !err)
		ubifs_wbuf_add_ino_nolock(&c->jheads[BASEHD].wbuf, host->i_ino);
	release_head(c, BASEHD);
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
	 * Remove all nodes belonging to the extended attribute inode from TNC.
	 * Well, there actually must be only one node - the inode itself.
	 */
	lowest_ino_key(c, &key1, inode->i_ino);
	highest_ino_key(c, &key2, inode->i_ino);
	err = ubifs_tnc_remove_range(c, &key1, &key2);
	if (err)
		goto out_ro;
	err = ubifs_add_dirt(c, lnum, UBIFS_INO_NODE_SZ);
	if (err)
		goto out_ro;

	/* And update TNC with the new host inode position */
	ino_key_init(c, &key1, host->i_ino);
	err = ubifs_tnc_add(c, &key1, lnum, xent_offs + len - hlen, hlen);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&host_ui->ui_lock);
	host_ui->synced_i_size = host_ui->ui_size;
	spin_unlock(&host_ui->ui_lock);
	mark_inode_clean(c, host_ui);
	return 0;

out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
	return err;
}

/**
 * ubifs_jnl_change_xattr - change an extended attribute.
 * @c: UBIFS file-system description object
 * @inode: extended attribute inode
 * @host: host inode
 *
 * This function writes the updated version of an extended attribute inode and
 * the host inode to the journal (to the base head). The host inode is written
 * after the extended attribute inode in order to guarantee that the extended
 * attribute will be flushed when the inode is synchronized by 'fsync()' and
 * consequently, the write-buffer is synchronized. This function returns zero
 * in case of success and a negative error code in case of failure.
 */
int ubifs_jnl_change_xattr(struct ubifs_info *c, const struct inode *inode,
			   const struct inode *host)
{
	int err, len1, len2, aligned_len, aligned_len1, lnum, offs;
	struct ubifs_inode *host_ui = ubifs_inode(host);
	struct ubifs_ino_node *ino;
	union ubifs_key key;
	int sync = IS_DIRSYNC(host);

	dbg_jnl("ino %lu, ino %lu", host->i_ino, inode->i_ino);
	ubifs_assert(host->i_nlink > 0);
	ubifs_assert(inode->i_nlink > 0);
	ubifs_assert(mutex_is_locked(&host_ui->ui_mutex));

	len1 = UBIFS_INO_NODE_SZ + host_ui->data_len;
	len2 = UBIFS_INO_NODE_SZ + ubifs_inode(inode)->data_len;
	aligned_len1 = ALIGN(len1, 8);
	aligned_len = aligned_len1 + ALIGN(len2, 8);

	ino = kmalloc(aligned_len, GFP_NOFS);
	if (!ino)
		return -ENOMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, aligned_len);
	if (err)
		goto out_free;

	pack_inode(c, ino, host, 0);
	pack_inode(c, (void *)ino + aligned_len1, inode, 1);

	err = write_head(c, BASEHD, ino, aligned_len, &lnum, &offs, 0);
	if (!sync && !err) {
		struct ubifs_wbuf *wbuf = &c->jheads[BASEHD].wbuf;

		ubifs_wbuf_add_ino_nolock(wbuf, host->i_ino);
		ubifs_wbuf_add_ino_nolock(wbuf, inode->i_ino);
	}
	release_head(c, BASEHD);
	if (err)
		goto out_ro;

	ino_key_init(c, &key, host->i_ino);
	err = ubifs_tnc_add(c, &key, lnum, offs, len1);
	if (err)
		goto out_ro;

	ino_key_init(c, &key, inode->i_ino);
	err = ubifs_tnc_add(c, &key, lnum, offs + aligned_len1, len2);
	if (err)
		goto out_ro;

	finish_reservation(c);
	spin_lock(&host_ui->ui_lock);
	host_ui->synced_i_size = host_ui->ui_size;
	spin_unlock(&host_ui->ui_lock);
	mark_inode_clean(c, host_ui);
	kfree(ino);
	return 0;

out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
out_free:
	kfree(ino);
	return err;
}

