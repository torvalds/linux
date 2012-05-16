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
 * Author: Adrian Hunter
 */

#include "ubifs.h"

/*
 * An orphan is an inode number whose inode node has been committed to the index
 * with a link count of zero. That happens when an open file is deleted
 * (unlinked) and then a commit is run. In the normal course of events the inode
 * would be deleted when the file is closed. However in the case of an unclean
 * unmount, orphans need to be accounted for. After an unclean unmount, the
 * orphans' inodes must be deleted which means either scanning the entire index
 * looking for them, or keeping a list on flash somewhere. This unit implements
 * the latter approach.
 *
 * The orphan area is a fixed number of LEBs situated between the LPT area and
 * the main area. The number of orphan area LEBs is specified when the file
 * system is created. The minimum number is 1. The size of the orphan area
 * should be so that it can hold the maximum number of orphans that are expected
 * to ever exist at one time.
 *
 * The number of orphans that can fit in a LEB is:
 *
 *         (c->leb_size - UBIFS_ORPH_NODE_SZ) / sizeof(__le64)
 *
 * For example: a 15872 byte LEB can fit 1980 orphans so 1 LEB may be enough.
 *
 * Orphans are accumulated in a rb-tree. When an inode's link count drops to
 * zero, the inode number is added to the rb-tree. It is removed from the tree
 * when the inode is deleted.  Any new orphans that are in the orphan tree when
 * the commit is run, are written to the orphan area in 1 or more orphan nodes.
 * If the orphan area is full, it is consolidated to make space.  There is
 * always enough space because validation prevents the user from creating more
 * than the maximum number of orphans allowed.
 */

#ifdef CONFIG_UBIFS_FS_DEBUG
static int dbg_check_orphans(struct ubifs_info *c);
#else
#define dbg_check_orphans(c) 0
#endif

/**
 * ubifs_add_orphan - add an orphan.
 * @c: UBIFS file-system description object
 * @inum: orphan inode number
 *
 * Add an orphan. This function is called when an inodes link count drops to
 * zero.
 */
int ubifs_add_orphan(struct ubifs_info *c, ino_t inum)
{
	struct ubifs_orphan *orphan, *o;
	struct rb_node **p, *parent = NULL;

	orphan = kzalloc(sizeof(struct ubifs_orphan), GFP_NOFS);
	if (!orphan)
		return -ENOMEM;
	orphan->inum = inum;
	orphan->new = 1;

	spin_lock(&c->orphan_lock);
	if (c->tot_orphans >= c->max_orphans) {
		spin_unlock(&c->orphan_lock);
		kfree(orphan);
		return -ENFILE;
	}
	p = &c->orph_tree.rb_node;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = &(*p)->rb_left;
		else if (inum > o->inum)
			p = &(*p)->rb_right;
		else {
			dbg_err("orphaned twice");
			spin_unlock(&c->orphan_lock);
			kfree(orphan);
			return 0;
		}
	}
	c->tot_orphans += 1;
	c->new_orphans += 1;
	rb_link_node(&orphan->rb, parent, p);
	rb_insert_color(&orphan->rb, &c->orph_tree);
	list_add_tail(&orphan->list, &c->orph_list);
	list_add_tail(&orphan->new_list, &c->orph_new);
	spin_unlock(&c->orphan_lock);
	dbg_gen("ino %lu", (unsigned long)inum);
	return 0;
}

/**
 * ubifs_delete_orphan - delete an orphan.
 * @c: UBIFS file-system description object
 * @inum: orphan inode number
 *
 * Delete an orphan. This function is called when an inode is deleted.
 */
void ubifs_delete_orphan(struct ubifs_info *c, ino_t inum)
{
	struct ubifs_orphan *o;
	struct rb_node *p;

	spin_lock(&c->orphan_lock);
	p = c->orph_tree.rb_node;
	while (p) {
		o = rb_entry(p, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = p->rb_left;
		else if (inum > o->inum)
			p = p->rb_right;
		else {
			if (o->dnext) {
				spin_unlock(&c->orphan_lock);
				dbg_gen("deleted twice ino %lu",
					(unsigned long)inum);
				return;
			}
			if (o->cnext) {
				o->dnext = c->orph_dnext;
				c->orph_dnext = o;
				spin_unlock(&c->orphan_lock);
				dbg_gen("delete later ino %lu",
					(unsigned long)inum);
				return;
			}
			rb_erase(p, &c->orph_tree);
			list_del(&o->list);
			c->tot_orphans -= 1;
			if (o->new) {
				list_del(&o->new_list);
				c->new_orphans -= 1;
			}
			spin_unlock(&c->orphan_lock);
			kfree(o);
			dbg_gen("inum %lu", (unsigned long)inum);
			return;
		}
	}
	spin_unlock(&c->orphan_lock);
	dbg_err("missing orphan ino %lu", (unsigned long)inum);
	dump_stack();
}

/**
 * ubifs_orphan_start_commit - start commit of orphans.
 * @c: UBIFS file-system description object
 *
 * Start commit of orphans.
 */
int ubifs_orphan_start_commit(struct ubifs_info *c)
{
	struct ubifs_orphan *orphan, **last;

	spin_lock(&c->orphan_lock);
	last = &c->orph_cnext;
	list_for_each_entry(orphan, &c->orph_new, new_list) {
		ubifs_assert(orphan->new);
		orphan->new = 0;
		*last = orphan;
		last = &orphan->cnext;
	}
	*last = orphan->cnext;
	c->cmt_orphans = c->new_orphans;
	c->new_orphans = 0;
	dbg_cmt("%d orphans to commit", c->cmt_orphans);
	INIT_LIST_HEAD(&c->orph_new);
	if (c->tot_orphans == 0)
		c->no_orphs = 1;
	else
		c->no_orphs = 0;
	spin_unlock(&c->orphan_lock);
	return 0;
}

/**
 * avail_orphs - calculate available space.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of orphans that can be written in the
 * available space.
 */
static int avail_orphs(struct ubifs_info *c)
{
	int avail_lebs, avail, gap;

	avail_lebs = c->orph_lebs - (c->ohead_lnum - c->orph_first) - 1;
	avail = avail_lebs *
	       ((c->leb_size - UBIFS_ORPH_NODE_SZ) / sizeof(__le64));
	gap = c->leb_size - c->ohead_offs;
	if (gap >= UBIFS_ORPH_NODE_SZ + sizeof(__le64))
		avail += (gap - UBIFS_ORPH_NODE_SZ) / sizeof(__le64);
	return avail;
}

/**
 * tot_avail_orphs - calculate total space.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of orphans that can be written in half
 * the total space. That leaves half the space for adding new orphans.
 */
static int tot_avail_orphs(struct ubifs_info *c)
{
	int avail_lebs, avail;

	avail_lebs = c->orph_lebs;
	avail = avail_lebs *
	       ((c->leb_size - UBIFS_ORPH_NODE_SZ) / sizeof(__le64));
	return avail / 2;
}

/**
 * do_write_orph_node - write a node to the orphan head.
 * @c: UBIFS file-system description object
 * @len: length of node
 * @atomic: write atomically
 *
 * This function writes a node to the orphan head from the orphan buffer. If
 * %atomic is not zero, then the write is done atomically. On success, %0 is
 * returned, otherwise a negative error code is returned.
 */
static int do_write_orph_node(struct ubifs_info *c, int len, int atomic)
{
	int err = 0;

	if (atomic) {
		ubifs_assert(c->ohead_offs == 0);
		ubifs_prepare_node(c, c->orph_buf, len, 1);
		len = ALIGN(len, c->min_io_size);
		err = ubifs_leb_change(c, c->ohead_lnum, c->orph_buf, len,
				       UBI_SHORTTERM);
	} else {
		if (c->ohead_offs == 0) {
			/* Ensure LEB has been unmapped */
			err = ubifs_leb_unmap(c, c->ohead_lnum);
			if (err)
				return err;
		}
		err = ubifs_write_node(c, c->orph_buf, len, c->ohead_lnum,
				       c->ohead_offs, UBI_SHORTTERM);
	}
	return err;
}

/**
 * write_orph_node - write an orphan node.
 * @c: UBIFS file-system description object
 * @atomic: write atomically
 *
 * This function builds an orphan node from the cnext list and writes it to the
 * orphan head. On success, %0 is returned, otherwise a negative error code
 * is returned.
 */
static int write_orph_node(struct ubifs_info *c, int atomic)
{
	struct ubifs_orphan *orphan, *cnext;
	struct ubifs_orph_node *orph;
	int gap, err, len, cnt, i;

	ubifs_assert(c->cmt_orphans > 0);
	gap = c->leb_size - c->ohead_offs;
	if (gap < UBIFS_ORPH_NODE_SZ + sizeof(__le64)) {
		c->ohead_lnum += 1;
		c->ohead_offs = 0;
		gap = c->leb_size;
		if (c->ohead_lnum > c->orph_last) {
			/*
			 * We limit the number of orphans so that this should
			 * never happen.
			 */
			ubifs_err("out of space in orphan area");
			return -EINVAL;
		}
	}
	cnt = (gap - UBIFS_ORPH_NODE_SZ) / sizeof(__le64);
	if (cnt > c->cmt_orphans)
		cnt = c->cmt_orphans;
	len = UBIFS_ORPH_NODE_SZ + cnt * sizeof(__le64);
	ubifs_assert(c->orph_buf);
	orph = c->orph_buf;
	orph->ch.node_type = UBIFS_ORPH_NODE;
	spin_lock(&c->orphan_lock);
	cnext = c->orph_cnext;
	for (i = 0; i < cnt; i++) {
		orphan = cnext;
		orph->inos[i] = cpu_to_le64(orphan->inum);
		cnext = orphan->cnext;
		orphan->cnext = NULL;
	}
	c->orph_cnext = cnext;
	c->cmt_orphans -= cnt;
	spin_unlock(&c->orphan_lock);
	if (c->cmt_orphans)
		orph->cmt_no = cpu_to_le64(c->cmt_no);
	else
		/* Mark the last node of the commit */
		orph->cmt_no = cpu_to_le64((c->cmt_no) | (1ULL << 63));
	ubifs_assert(c->ohead_offs + len <= c->leb_size);
	ubifs_assert(c->ohead_lnum >= c->orph_first);
	ubifs_assert(c->ohead_lnum <= c->orph_last);
	err = do_write_orph_node(c, len, atomic);
	c->ohead_offs += ALIGN(len, c->min_io_size);
	c->ohead_offs = ALIGN(c->ohead_offs, 8);
	return err;
}

/**
 * write_orph_nodes - write orphan nodes until there are no more to commit.
 * @c: UBIFS file-system description object
 * @atomic: write atomically
 *
 * This function writes orphan nodes for all the orphans to commit. On success,
 * %0 is returned, otherwise a negative error code is returned.
 */
static int write_orph_nodes(struct ubifs_info *c, int atomic)
{
	int err;

	while (c->cmt_orphans > 0) {
		err = write_orph_node(c, atomic);
		if (err)
			return err;
	}
	if (atomic) {
		int lnum;

		/* Unmap any unused LEBs after consolidation */
		lnum = c->ohead_lnum + 1;
		for (lnum = c->ohead_lnum + 1; lnum <= c->orph_last; lnum++) {
			err = ubifs_leb_unmap(c, lnum);
			if (err)
				return err;
		}
	}
	return 0;
}

/**
 * consolidate - consolidate the orphan area.
 * @c: UBIFS file-system description object
 *
 * This function enables consolidation by putting all the orphans into the list
 * to commit. The list is in the order that the orphans were added, and the
 * LEBs are written atomically in order, so at no time can orphans be lost by
 * an unclean unmount.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int consolidate(struct ubifs_info *c)
{
	int tot_avail = tot_avail_orphs(c), err = 0;

	spin_lock(&c->orphan_lock);
	dbg_cmt("there is space for %d orphans and there are %d",
		tot_avail, c->tot_orphans);
	if (c->tot_orphans - c->new_orphans <= tot_avail) {
		struct ubifs_orphan *orphan, **last;
		int cnt = 0;

		/* Change the cnext list to include all non-new orphans */
		last = &c->orph_cnext;
		list_for_each_entry(orphan, &c->orph_list, list) {
			if (orphan->new)
				continue;
			*last = orphan;
			last = &orphan->cnext;
			cnt += 1;
		}
		*last = orphan->cnext;
		ubifs_assert(cnt == c->tot_orphans - c->new_orphans);
		c->cmt_orphans = cnt;
		c->ohead_lnum = c->orph_first;
		c->ohead_offs = 0;
	} else {
		/*
		 * We limit the number of orphans so that this should
		 * never happen.
		 */
		ubifs_err("out of space in orphan area");
		err = -EINVAL;
	}
	spin_unlock(&c->orphan_lock);
	return err;
}

/**
 * commit_orphans - commit orphans.
 * @c: UBIFS file-system description object
 *
 * This function commits orphans to flash. On success, %0 is returned,
 * otherwise a negative error code is returned.
 */
static int commit_orphans(struct ubifs_info *c)
{
	int avail, atomic = 0, err;

	ubifs_assert(c->cmt_orphans > 0);
	avail = avail_orphs(c);
	if (avail < c->cmt_orphans) {
		/* Not enough space to write new orphans, so consolidate */
		err = consolidate(c);
		if (err)
			return err;
		atomic = 1;
	}
	err = write_orph_nodes(c, atomic);
	return err;
}

/**
 * erase_deleted - erase the orphans marked for deletion.
 * @c: UBIFS file-system description object
 *
 * During commit, the orphans being committed cannot be deleted, so they are
 * marked for deletion and deleted by this function. Also, the recovery
 * adds killed orphans to the deletion list, and therefore they are deleted
 * here too.
 */
static void erase_deleted(struct ubifs_info *c)
{
	struct ubifs_orphan *orphan, *dnext;

	spin_lock(&c->orphan_lock);
	dnext = c->orph_dnext;
	while (dnext) {
		orphan = dnext;
		dnext = orphan->dnext;
		ubifs_assert(!orphan->new);
		rb_erase(&orphan->rb, &c->orph_tree);
		list_del(&orphan->list);
		c->tot_orphans -= 1;
		dbg_gen("deleting orphan ino %lu", (unsigned long)orphan->inum);
		kfree(orphan);
	}
	c->orph_dnext = NULL;
	spin_unlock(&c->orphan_lock);
}

/**
 * ubifs_orphan_end_commit - end commit of orphans.
 * @c: UBIFS file-system description object
 *
 * End commit of orphans.
 */
int ubifs_orphan_end_commit(struct ubifs_info *c)
{
	int err;

	if (c->cmt_orphans != 0) {
		err = commit_orphans(c);
		if (err)
			return err;
	}
	erase_deleted(c);
	err = dbg_check_orphans(c);
	return err;
}

/**
 * ubifs_clear_orphans - erase all LEBs used for orphans.
 * @c: UBIFS file-system description object
 *
 * If recovery is not required, then the orphans from the previous session
 * are not needed. This function locates the LEBs used to record
 * orphans, and un-maps them.
 */
int ubifs_clear_orphans(struct ubifs_info *c)
{
	int lnum, err;

	for (lnum = c->orph_first; lnum <= c->orph_last; lnum++) {
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
	}
	c->ohead_lnum = c->orph_first;
	c->ohead_offs = 0;
	return 0;
}

/**
 * insert_dead_orphan - insert an orphan.
 * @c: UBIFS file-system description object
 * @inum: orphan inode number
 *
 * This function is a helper to the 'do_kill_orphans()' function. The orphan
 * must be kept until the next commit, so it is added to the rb-tree and the
 * deletion list.
 */
static int insert_dead_orphan(struct ubifs_info *c, ino_t inum)
{
	struct ubifs_orphan *orphan, *o;
	struct rb_node **p, *parent = NULL;

	orphan = kzalloc(sizeof(struct ubifs_orphan), GFP_KERNEL);
	if (!orphan)
		return -ENOMEM;
	orphan->inum = inum;

	p = &c->orph_tree.rb_node;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = &(*p)->rb_left;
		else if (inum > o->inum)
			p = &(*p)->rb_right;
		else {
			/* Already added - no problem */
			kfree(orphan);
			return 0;
		}
	}
	c->tot_orphans += 1;
	rb_link_node(&orphan->rb, parent, p);
	rb_insert_color(&orphan->rb, &c->orph_tree);
	list_add_tail(&orphan->list, &c->orph_list);
	orphan->dnext = c->orph_dnext;
	c->orph_dnext = orphan;
	dbg_mnt("ino %lu, new %d, tot %d", (unsigned long)inum,
		c->new_orphans, c->tot_orphans);
	return 0;
}

/**
 * do_kill_orphans - remove orphan inodes from the index.
 * @c: UBIFS file-system description object
 * @sleb: scanned LEB
 * @last_cmt_no: cmt_no of last orphan node read is passed and returned here
 * @outofdate: whether the LEB is out of date is returned here
 * @last_flagged: whether the end orphan node is encountered
 *
 * This function is a helper to the 'kill_orphans()' function. It goes through
 * every orphan node in a LEB and for every inode number recorded, removes
 * all keys for that inode from the TNC.
 */
static int do_kill_orphans(struct ubifs_info *c, struct ubifs_scan_leb *sleb,
			   unsigned long long *last_cmt_no, int *outofdate,
			   int *last_flagged)
{
	struct ubifs_scan_node *snod;
	struct ubifs_orph_node *orph;
	unsigned long long cmt_no;
	ino_t inum;
	int i, n, err, first = 1;

	list_for_each_entry(snod, &sleb->nodes, list) {
		if (snod->type != UBIFS_ORPH_NODE) {
			ubifs_err("invalid node type %d in orphan area at "
				  "%d:%d", snod->type, sleb->lnum, snod->offs);
			ubifs_dump_node(c, snod->node);
			return -EINVAL;
		}

		orph = snod->node;

		/* Check commit number */
		cmt_no = le64_to_cpu(orph->cmt_no) & LLONG_MAX;
		/*
		 * The commit number on the master node may be less, because
		 * of a failed commit. If there are several failed commits in a
		 * row, the commit number written on orphan nodes will continue
		 * to increase (because the commit number is adjusted here) even
		 * though the commit number on the master node stays the same
		 * because the master node has not been re-written.
		 */
		if (cmt_no > c->cmt_no)
			c->cmt_no = cmt_no;
		if (cmt_no < *last_cmt_no && *last_flagged) {
			/*
			 * The last orphan node had a higher commit number and
			 * was flagged as the last written for that commit
			 * number. That makes this orphan node, out of date.
			 */
			if (!first) {
				ubifs_err("out of order commit number %llu in "
					  "orphan node at %d:%d",
					  cmt_no, sleb->lnum, snod->offs);
				ubifs_dump_node(c, snod->node);
				return -EINVAL;
			}
			dbg_rcvry("out of date LEB %d", sleb->lnum);
			*outofdate = 1;
			return 0;
		}

		if (first)
			first = 0;

		n = (le32_to_cpu(orph->ch.len) - UBIFS_ORPH_NODE_SZ) >> 3;
		for (i = 0; i < n; i++) {
			inum = le64_to_cpu(orph->inos[i]);
			dbg_rcvry("deleting orphaned inode %lu",
				  (unsigned long)inum);
			err = ubifs_tnc_remove_ino(c, inum);
			if (err)
				return err;
			err = insert_dead_orphan(c, inum);
			if (err)
				return err;
		}

		*last_cmt_no = cmt_no;
		if (le64_to_cpu(orph->cmt_no) & (1ULL << 63)) {
			dbg_rcvry("last orph node for commit %llu at %d:%d",
				  cmt_no, sleb->lnum, snod->offs);
			*last_flagged = 1;
		} else
			*last_flagged = 0;
	}

	return 0;
}

/**
 * kill_orphans - remove all orphan inodes from the index.
 * @c: UBIFS file-system description object
 *
 * If recovery is required, then orphan inodes recorded during the previous
 * session (which ended with an unclean unmount) must be deleted from the index.
 * This is done by updating the TNC, but since the index is not updated until
 * the next commit, the LEBs where the orphan information is recorded are not
 * erased until the next commit.
 */
static int kill_orphans(struct ubifs_info *c)
{
	unsigned long long last_cmt_no = 0;
	int lnum, err = 0, outofdate = 0, last_flagged = 0;

	c->ohead_lnum = c->orph_first;
	c->ohead_offs = 0;
	/* Check no-orphans flag and skip this if no orphans */
	if (c->no_orphs) {
		dbg_rcvry("no orphans");
		return 0;
	}
	/*
	 * Orph nodes always start at c->orph_first and are written to each
	 * successive LEB in turn. Generally unused LEBs will have been unmapped
	 * but may contain out of date orphan nodes if the unmap didn't go
	 * through. In addition, the last orphan node written for each commit is
	 * marked (top bit of orph->cmt_no is set to 1). It is possible that
	 * there are orphan nodes from the next commit (i.e. the commit did not
	 * complete successfully). In that case, no orphans will have been lost
	 * due to the way that orphans are written, and any orphans added will
	 * be valid orphans anyway and so can be deleted.
	 */
	for (lnum = c->orph_first; lnum <= c->orph_last; lnum++) {
		struct ubifs_scan_leb *sleb;

		dbg_rcvry("LEB %d", lnum);
		sleb = ubifs_scan(c, lnum, 0, c->sbuf, 1);
		if (IS_ERR(sleb)) {
			if (PTR_ERR(sleb) == -EUCLEAN)
				sleb = ubifs_recover_leb(c, lnum, 0,
							 c->sbuf, -1);
			if (IS_ERR(sleb)) {
				err = PTR_ERR(sleb);
				break;
			}
		}
		err = do_kill_orphans(c, sleb, &last_cmt_no, &outofdate,
				      &last_flagged);
		if (err || outofdate) {
			ubifs_scan_destroy(sleb);
			break;
		}
		if (sleb->endpt) {
			c->ohead_lnum = lnum;
			c->ohead_offs = sleb->endpt;
		}
		ubifs_scan_destroy(sleb);
	}
	return err;
}

/**
 * ubifs_mount_orphans - delete orphan inodes and erase LEBs that recorded them.
 * @c: UBIFS file-system description object
 * @unclean: indicates recovery from unclean unmount
 * @read_only: indicates read only mount
 *
 * This function is called when mounting to erase orphans from the previous
 * session. If UBIFS was not unmounted cleanly, then the inodes recorded as
 * orphans are deleted.
 */
int ubifs_mount_orphans(struct ubifs_info *c, int unclean, int read_only)
{
	int err = 0;

	c->max_orphans = tot_avail_orphs(c);

	if (!read_only) {
		c->orph_buf = vmalloc(c->leb_size);
		if (!c->orph_buf)
			return -ENOMEM;
	}

	if (unclean)
		err = kill_orphans(c);
	else if (!read_only)
		err = ubifs_clear_orphans(c);

	return err;
}

#ifdef CONFIG_UBIFS_FS_DEBUG

struct check_orphan {
	struct rb_node rb;
	ino_t inum;
};

struct check_info {
	unsigned long last_ino;
	unsigned long tot_inos;
	unsigned long missing;
	unsigned long long leaf_cnt;
	struct ubifs_ino_node *node;
	struct rb_root root;
};

static int dbg_find_orphan(struct ubifs_info *c, ino_t inum)
{
	struct ubifs_orphan *o;
	struct rb_node *p;

	spin_lock(&c->orphan_lock);
	p = c->orph_tree.rb_node;
	while (p) {
		o = rb_entry(p, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = p->rb_left;
		else if (inum > o->inum)
			p = p->rb_right;
		else {
			spin_unlock(&c->orphan_lock);
			return 1;
		}
	}
	spin_unlock(&c->orphan_lock);
	return 0;
}

static int dbg_ins_check_orphan(struct rb_root *root, ino_t inum)
{
	struct check_orphan *orphan, *o;
	struct rb_node **p, *parent = NULL;

	orphan = kzalloc(sizeof(struct check_orphan), GFP_NOFS);
	if (!orphan)
		return -ENOMEM;
	orphan->inum = inum;

	p = &root->rb_node;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct check_orphan, rb);
		if (inum < o->inum)
			p = &(*p)->rb_left;
		else if (inum > o->inum)
			p = &(*p)->rb_right;
		else {
			kfree(orphan);
			return 0;
		}
	}
	rb_link_node(&orphan->rb, parent, p);
	rb_insert_color(&orphan->rb, root);
	return 0;
}

static int dbg_find_check_orphan(struct rb_root *root, ino_t inum)
{
	struct check_orphan *o;
	struct rb_node *p;

	p = root->rb_node;
	while (p) {
		o = rb_entry(p, struct check_orphan, rb);
		if (inum < o->inum)
			p = p->rb_left;
		else if (inum > o->inum)
			p = p->rb_right;
		else
			return 1;
	}
	return 0;
}

static void dbg_free_check_tree(struct rb_root *root)
{
	struct rb_node *this = root->rb_node;
	struct check_orphan *o;

	while (this) {
		if (this->rb_left) {
			this = this->rb_left;
			continue;
		} else if (this->rb_right) {
			this = this->rb_right;
			continue;
		}
		o = rb_entry(this, struct check_orphan, rb);
		this = rb_parent(this);
		if (this) {
			if (this->rb_left == &o->rb)
				this->rb_left = NULL;
			else
				this->rb_right = NULL;
		}
		kfree(o);
	}
}

static int dbg_orphan_check(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			    void *priv)
{
	struct check_info *ci = priv;
	ino_t inum;
	int err;

	inum = key_inum(c, &zbr->key);
	if (inum != ci->last_ino) {
		/* Lowest node type is the inode node, so it comes first */
		if (key_type(c, &zbr->key) != UBIFS_INO_KEY)
			ubifs_err("found orphan node ino %lu, type %d",
				  (unsigned long)inum, key_type(c, &zbr->key));
		ci->last_ino = inum;
		ci->tot_inos += 1;
		err = ubifs_tnc_read_node(c, zbr, ci->node);
		if (err) {
			ubifs_err("node read failed, error %d", err);
			return err;
		}
		if (ci->node->nlink == 0)
			/* Must be recorded as an orphan */
			if (!dbg_find_check_orphan(&ci->root, inum) &&
			    !dbg_find_orphan(c, inum)) {
				ubifs_err("missing orphan, ino %lu",
					  (unsigned long)inum);
				ci->missing += 1;
			}
	}
	ci->leaf_cnt += 1;
	return 0;
}

static int dbg_read_orphans(struct check_info *ci, struct ubifs_scan_leb *sleb)
{
	struct ubifs_scan_node *snod;
	struct ubifs_orph_node *orph;
	ino_t inum;
	int i, n, err;

	list_for_each_entry(snod, &sleb->nodes, list) {
		cond_resched();
		if (snod->type != UBIFS_ORPH_NODE)
			continue;
		orph = snod->node;
		n = (le32_to_cpu(orph->ch.len) - UBIFS_ORPH_NODE_SZ) >> 3;
		for (i = 0; i < n; i++) {
			inum = le64_to_cpu(orph->inos[i]);
			err = dbg_ins_check_orphan(&ci->root, inum);
			if (err)
				return err;
		}
	}
	return 0;
}

static int dbg_scan_orphans(struct ubifs_info *c, struct check_info *ci)
{
	int lnum, err = 0;
	void *buf;

	/* Check no-orphans flag and skip this if no orphans */
	if (c->no_orphs)
		return 0;

	buf = __vmalloc(c->leb_size, GFP_NOFS, PAGE_KERNEL);
	if (!buf) {
		ubifs_err("cannot allocate memory to check orphans");
		return 0;
	}

	for (lnum = c->orph_first; lnum <= c->orph_last; lnum++) {
		struct ubifs_scan_leb *sleb;

		sleb = ubifs_scan(c, lnum, 0, buf, 0);
		if (IS_ERR(sleb)) {
			err = PTR_ERR(sleb);
			break;
		}

		err = dbg_read_orphans(ci, sleb);
		ubifs_scan_destroy(sleb);
		if (err)
			break;
	}

	vfree(buf);
	return err;
}

static int dbg_check_orphans(struct ubifs_info *c)
{
	struct check_info ci;
	int err;

	if (!dbg_is_chk_orph(c))
		return 0;

	ci.last_ino = 0;
	ci.tot_inos = 0;
	ci.missing  = 0;
	ci.leaf_cnt = 0;
	ci.root = RB_ROOT;
	ci.node = kmalloc(UBIFS_MAX_INO_NODE_SZ, GFP_NOFS);
	if (!ci.node) {
		ubifs_err("out of memory");
		return -ENOMEM;
	}

	err = dbg_scan_orphans(c, &ci);
	if (err)
		goto out;

	err = dbg_walk_index(c, &dbg_orphan_check, NULL, &ci);
	if (err) {
		ubifs_err("cannot scan TNC, error %d", err);
		goto out;
	}

	if (ci.missing) {
		ubifs_err("%lu missing orphan(s)", ci.missing);
		err = -EINVAL;
		goto out;
	}

	dbg_cmt("last inode number is %lu", ci.last_ino);
	dbg_cmt("total number of inodes is %lu", ci.tot_inos);
	dbg_cmt("total number of leaf nodes is %llu", ci.leaf_cnt);

out:
	dbg_free_check_tree(&ci.root);
	kfree(ci.node);
	return err;
}

#endif /* CONFIG_UBIFS_FS_DEBUG */
