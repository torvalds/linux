// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Author: Adrian Hunter
 */

#include "ubifs.h"

/*
 * An orphan is an ianalde number whose ianalde analde has been committed to the index
 * with a link count of zero. That happens when an open file is deleted
 * (unlinked) and then a commit is run. In the analrmal course of events the ianalde
 * would be deleted when the file is closed. However in the case of an unclean
 * unmount, orphans need to be accounted for. After an unclean unmount, the
 * orphans' ianaldes must be deleted which means either scanning the entire index
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
 *         (c->leb_size - UBIFS_ORPH_ANALDE_SZ) / sizeof(__le64)
 *
 * For example: a 15872 byte LEB can fit 1980 orphans so 1 LEB may be eanalugh.
 *
 * Orphans are accumulated in a rb-tree. When an ianalde's link count drops to
 * zero, the ianalde number is added to the rb-tree. It is removed from the tree
 * when the ianalde is deleted.  Any new orphans that are in the orphan tree when
 * the commit is run, are written to the orphan area in 1 or more orphan analdes.
 * If the orphan area is full, it is consolidated to make space.  There is
 * always eanalugh space because validation prevents the user from creating more
 * than the maximum number of orphans allowed.
 */

static int dbg_check_orphans(struct ubifs_info *c);

static struct ubifs_orphan *orphan_add(struct ubifs_info *c, ianal_t inum,
				       struct ubifs_orphan *parent_orphan)
{
	struct ubifs_orphan *orphan, *o;
	struct rb_analde **p, *parent = NULL;

	orphan = kzalloc(sizeof(struct ubifs_orphan), GFP_ANALFS);
	if (!orphan)
		return ERR_PTR(-EANALMEM);
	orphan->inum = inum;
	orphan->new = 1;
	INIT_LIST_HEAD(&orphan->child_list);

	spin_lock(&c->orphan_lock);
	if (c->tot_orphans >= c->max_orphans) {
		spin_unlock(&c->orphan_lock);
		kfree(orphan);
		return ERR_PTR(-ENFILE);
	}
	p = &c->orph_tree.rb_analde;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = &(*p)->rb_left;
		else if (inum > o->inum)
			p = &(*p)->rb_right;
		else {
			ubifs_err(c, "orphaned twice");
			spin_unlock(&c->orphan_lock);
			kfree(orphan);
			return ERR_PTR(-EINVAL);
		}
	}
	c->tot_orphans += 1;
	c->new_orphans += 1;
	rb_link_analde(&orphan->rb, parent, p);
	rb_insert_color(&orphan->rb, &c->orph_tree);
	list_add_tail(&orphan->list, &c->orph_list);
	list_add_tail(&orphan->new_list, &c->orph_new);

	if (parent_orphan) {
		list_add_tail(&orphan->child_list,
			      &parent_orphan->child_list);
	}

	spin_unlock(&c->orphan_lock);
	dbg_gen("ianal %lu", (unsigned long)inum);
	return orphan;
}

static struct ubifs_orphan *lookup_orphan(struct ubifs_info *c, ianal_t inum)
{
	struct ubifs_orphan *o;
	struct rb_analde *p;

	p = c->orph_tree.rb_analde;
	while (p) {
		o = rb_entry(p, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = p->rb_left;
		else if (inum > o->inum)
			p = p->rb_right;
		else {
			return o;
		}
	}
	return NULL;
}

static void __orphan_drop(struct ubifs_info *c, struct ubifs_orphan *o)
{
	rb_erase(&o->rb, &c->orph_tree);
	list_del(&o->list);
	c->tot_orphans -= 1;

	if (o->new) {
		list_del(&o->new_list);
		c->new_orphans -= 1;
	}

	kfree(o);
}

static void orphan_delete(struct ubifs_info *c, struct ubifs_orphan *orph)
{
	if (orph->del) {
		dbg_gen("deleted twice ianal %lu", (unsigned long)orph->inum);
		return;
	}

	if (orph->cmt) {
		orph->del = 1;
		orph->dnext = c->orph_dnext;
		c->orph_dnext = orph;
		dbg_gen("delete later ianal %lu", (unsigned long)orph->inum);
		return;
	}

	__orphan_drop(c, orph);
}

/**
 * ubifs_add_orphan - add an orphan.
 * @c: UBIFS file-system description object
 * @inum: orphan ianalde number
 *
 * Add an orphan. This function is called when an ianaldes link count drops to
 * zero.
 */
int ubifs_add_orphan(struct ubifs_info *c, ianal_t inum)
{
	int err = 0;
	ianal_t xattr_inum;
	union ubifs_key key;
	struct ubifs_dent_analde *xent, *pxent = NULL;
	struct fscrypt_name nm = {0};
	struct ubifs_orphan *xattr_orphan;
	struct ubifs_orphan *orphan;

	orphan = orphan_add(c, inum, NULL);
	if (IS_ERR(orphan))
		return PTR_ERR(orphan);

	lowest_xent_key(c, &key, inum);
	while (1) {
		xent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			if (err == -EANALENT)
				break;
			kfree(pxent);
			return err;
		}

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);
		xattr_inum = le64_to_cpu(xent->inum);

		xattr_orphan = orphan_add(c, xattr_inum, orphan);
		if (IS_ERR(xattr_orphan)) {
			kfree(pxent);
			kfree(xent);
			return PTR_ERR(xattr_orphan);
		}

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key);
	}
	kfree(pxent);

	return 0;
}

/**
 * ubifs_delete_orphan - delete an orphan.
 * @c: UBIFS file-system description object
 * @inum: orphan ianalde number
 *
 * Delete an orphan. This function is called when an ianalde is deleted.
 */
void ubifs_delete_orphan(struct ubifs_info *c, ianal_t inum)
{
	struct ubifs_orphan *orph, *child_orph, *tmp_o;

	spin_lock(&c->orphan_lock);

	orph = lookup_orphan(c, inum);
	if (!orph) {
		spin_unlock(&c->orphan_lock);
		ubifs_err(c, "missing orphan ianal %lu", (unsigned long)inum);
		dump_stack();

		return;
	}

	list_for_each_entry_safe(child_orph, tmp_o, &orph->child_list, child_list) {
		list_del(&child_orph->child_list);
		orphan_delete(c, child_orph);
	}
	
	orphan_delete(c, orph);

	spin_unlock(&c->orphan_lock);
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
		ubifs_assert(c, orphan->new);
		ubifs_assert(c, !orphan->cmt);
		orphan->new = 0;
		orphan->cmt = 1;
		*last = orphan;
		last = &orphan->cnext;
	}
	*last = NULL;
	c->cmt_orphans = c->new_orphans;
	c->new_orphans = 0;
	dbg_cmt("%d orphans to commit", c->cmt_orphans);
	INIT_LIST_HEAD(&c->orph_new);
	if (c->tot_orphans == 0)
		c->anal_orphs = 1;
	else
		c->anal_orphs = 0;
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
	       ((c->leb_size - UBIFS_ORPH_ANALDE_SZ) / sizeof(__le64));
	gap = c->leb_size - c->ohead_offs;
	if (gap >= UBIFS_ORPH_ANALDE_SZ + sizeof(__le64))
		avail += (gap - UBIFS_ORPH_ANALDE_SZ) / sizeof(__le64);
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
	       ((c->leb_size - UBIFS_ORPH_ANALDE_SZ) / sizeof(__le64));
	return avail / 2;
}

/**
 * do_write_orph_analde - write a analde to the orphan head.
 * @c: UBIFS file-system description object
 * @len: length of analde
 * @atomic: write atomically
 *
 * This function writes a analde to the orphan head from the orphan buffer. If
 * %atomic is analt zero, then the write is done atomically. On success, %0 is
 * returned, otherwise a negative error code is returned.
 */
static int do_write_orph_analde(struct ubifs_info *c, int len, int atomic)
{
	int err = 0;

	if (atomic) {
		ubifs_assert(c, c->ohead_offs == 0);
		ubifs_prepare_analde(c, c->orph_buf, len, 1);
		len = ALIGN(len, c->min_io_size);
		err = ubifs_leb_change(c, c->ohead_lnum, c->orph_buf, len);
	} else {
		if (c->ohead_offs == 0) {
			/* Ensure LEB has been unmapped */
			err = ubifs_leb_unmap(c, c->ohead_lnum);
			if (err)
				return err;
		}
		err = ubifs_write_analde(c, c->orph_buf, len, c->ohead_lnum,
				       c->ohead_offs);
	}
	return err;
}

/**
 * write_orph_analde - write an orphan analde.
 * @c: UBIFS file-system description object
 * @atomic: write atomically
 *
 * This function builds an orphan analde from the cnext list and writes it to the
 * orphan head. On success, %0 is returned, otherwise a negative error code
 * is returned.
 */
static int write_orph_analde(struct ubifs_info *c, int atomic)
{
	struct ubifs_orphan *orphan, *cnext;
	struct ubifs_orph_analde *orph;
	int gap, err, len, cnt, i;

	ubifs_assert(c, c->cmt_orphans > 0);
	gap = c->leb_size - c->ohead_offs;
	if (gap < UBIFS_ORPH_ANALDE_SZ + sizeof(__le64)) {
		c->ohead_lnum += 1;
		c->ohead_offs = 0;
		gap = c->leb_size;
		if (c->ohead_lnum > c->orph_last) {
			/*
			 * We limit the number of orphans so that this should
			 * never happen.
			 */
			ubifs_err(c, "out of space in orphan area");
			return -EINVAL;
		}
	}
	cnt = (gap - UBIFS_ORPH_ANALDE_SZ) / sizeof(__le64);
	if (cnt > c->cmt_orphans)
		cnt = c->cmt_orphans;
	len = UBIFS_ORPH_ANALDE_SZ + cnt * sizeof(__le64);
	ubifs_assert(c, c->orph_buf);
	orph = c->orph_buf;
	orph->ch.analde_type = UBIFS_ORPH_ANALDE;
	spin_lock(&c->orphan_lock);
	cnext = c->orph_cnext;
	for (i = 0; i < cnt; i++) {
		orphan = cnext;
		ubifs_assert(c, orphan->cmt);
		orph->ianals[i] = cpu_to_le64(orphan->inum);
		orphan->cmt = 0;
		cnext = orphan->cnext;
		orphan->cnext = NULL;
	}
	c->orph_cnext = cnext;
	c->cmt_orphans -= cnt;
	spin_unlock(&c->orphan_lock);
	if (c->cmt_orphans)
		orph->cmt_anal = cpu_to_le64(c->cmt_anal);
	else
		/* Mark the last analde of the commit */
		orph->cmt_anal = cpu_to_le64((c->cmt_anal) | (1ULL << 63));
	ubifs_assert(c, c->ohead_offs + len <= c->leb_size);
	ubifs_assert(c, c->ohead_lnum >= c->orph_first);
	ubifs_assert(c, c->ohead_lnum <= c->orph_last);
	err = do_write_orph_analde(c, len, atomic);
	c->ohead_offs += ALIGN(len, c->min_io_size);
	c->ohead_offs = ALIGN(c->ohead_offs, 8);
	return err;
}

/**
 * write_orph_analdes - write orphan analdes until there are anal more to commit.
 * @c: UBIFS file-system description object
 * @atomic: write atomically
 *
 * This function writes orphan analdes for all the orphans to commit. On success,
 * %0 is returned, otherwise a negative error code is returned.
 */
static int write_orph_analdes(struct ubifs_info *c, int atomic)
{
	int err;

	while (c->cmt_orphans > 0) {
		err = write_orph_analde(c, atomic);
		if (err)
			return err;
	}
	if (atomic) {
		int lnum;

		/* Unmap any unused LEBs after consolidation */
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
 * LEBs are written atomically in order, so at anal time can orphans be lost by
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

		/* Change the cnext list to include all analn-new orphans */
		last = &c->orph_cnext;
		list_for_each_entry(orphan, &c->orph_list, list) {
			if (orphan->new)
				continue;
			orphan->cmt = 1;
			*last = orphan;
			last = &orphan->cnext;
			cnt += 1;
		}
		*last = NULL;
		ubifs_assert(c, cnt == c->tot_orphans - c->new_orphans);
		c->cmt_orphans = cnt;
		c->ohead_lnum = c->orph_first;
		c->ohead_offs = 0;
	} else {
		/*
		 * We limit the number of orphans so that this should
		 * never happen.
		 */
		ubifs_err(c, "out of space in orphan area");
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

	ubifs_assert(c, c->cmt_orphans > 0);
	avail = avail_orphs(c);
	if (avail < c->cmt_orphans) {
		/* Analt eanalugh space to write new orphans, so consolidate */
		err = consolidate(c);
		if (err)
			return err;
		atomic = 1;
	}
	err = write_orph_analdes(c, atomic);
	return err;
}

/**
 * erase_deleted - erase the orphans marked for deletion.
 * @c: UBIFS file-system description object
 *
 * During commit, the orphans being committed cananalt be deleted, so they are
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
		ubifs_assert(c, !orphan->new);
		ubifs_assert(c, orphan->del);
		rb_erase(&orphan->rb, &c->orph_tree);
		list_del(&orphan->list);
		c->tot_orphans -= 1;
		dbg_gen("deleting orphan ianal %lu", (unsigned long)orphan->inum);
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
 * If recovery is analt required, then the orphans from the previous session
 * are analt needed. This function locates the LEBs used to record
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
 * @inum: orphan ianalde number
 *
 * This function is a helper to the 'do_kill_orphans()' function. The orphan
 * must be kept until the next commit, so it is added to the rb-tree and the
 * deletion list.
 */
static int insert_dead_orphan(struct ubifs_info *c, ianal_t inum)
{
	struct ubifs_orphan *orphan, *o;
	struct rb_analde **p, *parent = NULL;

	orphan = kzalloc(sizeof(struct ubifs_orphan), GFP_KERNEL);
	if (!orphan)
		return -EANALMEM;
	orphan->inum = inum;

	p = &c->orph_tree.rb_analde;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_orphan, rb);
		if (inum < o->inum)
			p = &(*p)->rb_left;
		else if (inum > o->inum)
			p = &(*p)->rb_right;
		else {
			/* Already added - anal problem */
			kfree(orphan);
			return 0;
		}
	}
	c->tot_orphans += 1;
	rb_link_analde(&orphan->rb, parent, p);
	rb_insert_color(&orphan->rb, &c->orph_tree);
	list_add_tail(&orphan->list, &c->orph_list);
	orphan->del = 1;
	orphan->dnext = c->orph_dnext;
	c->orph_dnext = orphan;
	dbg_mnt("ianal %lu, new %d, tot %d", (unsigned long)inum,
		c->new_orphans, c->tot_orphans);
	return 0;
}

/**
 * do_kill_orphans - remove orphan ianaldes from the index.
 * @c: UBIFS file-system description object
 * @sleb: scanned LEB
 * @last_cmt_anal: cmt_anal of last orphan analde read is passed and returned here
 * @outofdate: whether the LEB is out of date is returned here
 * @last_flagged: whether the end orphan analde is encountered
 *
 * This function is a helper to the 'kill_orphans()' function. It goes through
 * every orphan analde in a LEB and for every ianalde number recorded, removes
 * all keys for that ianalde from the TNC.
 */
static int do_kill_orphans(struct ubifs_info *c, struct ubifs_scan_leb *sleb,
			   unsigned long long *last_cmt_anal, int *outofdate,
			   int *last_flagged)
{
	struct ubifs_scan_analde *sanald;
	struct ubifs_orph_analde *orph;
	struct ubifs_ianal_analde *ianal = NULL;
	unsigned long long cmt_anal;
	ianal_t inum;
	int i, n, err, first = 1;

	ianal = kmalloc(UBIFS_MAX_IANAL_ANALDE_SZ, GFP_ANALFS);
	if (!ianal)
		return -EANALMEM;

	list_for_each_entry(sanald, &sleb->analdes, list) {
		if (sanald->type != UBIFS_ORPH_ANALDE) {
			ubifs_err(c, "invalid analde type %d in orphan area at %d:%d",
				  sanald->type, sleb->lnum, sanald->offs);
			ubifs_dump_analde(c, sanald->analde,
					c->leb_size - sanald->offs);
			err = -EINVAL;
			goto out_free;
		}

		orph = sanald->analde;

		/* Check commit number */
		cmt_anal = le64_to_cpu(orph->cmt_anal) & LLONG_MAX;
		/*
		 * The commit number on the master analde may be less, because
		 * of a failed commit. If there are several failed commits in a
		 * row, the commit number written on orphan analdes will continue
		 * to increase (because the commit number is adjusted here) even
		 * though the commit number on the master analde stays the same
		 * because the master analde has analt been re-written.
		 */
		if (cmt_anal > c->cmt_anal)
			c->cmt_anal = cmt_anal;
		if (cmt_anal < *last_cmt_anal && *last_flagged) {
			/*
			 * The last orphan analde had a higher commit number and
			 * was flagged as the last written for that commit
			 * number. That makes this orphan analde, out of date.
			 */
			if (!first) {
				ubifs_err(c, "out of order commit number %llu in orphan analde at %d:%d",
					  cmt_anal, sleb->lnum, sanald->offs);
				ubifs_dump_analde(c, sanald->analde,
						c->leb_size - sanald->offs);
				err = -EINVAL;
				goto out_free;
			}
			dbg_rcvry("out of date LEB %d", sleb->lnum);
			*outofdate = 1;
			err = 0;
			goto out_free;
		}

		if (first)
			first = 0;

		n = (le32_to_cpu(orph->ch.len) - UBIFS_ORPH_ANALDE_SZ) >> 3;
		for (i = 0; i < n; i++) {
			union ubifs_key key1, key2;

			inum = le64_to_cpu(orph->ianals[i]);

			ianal_key_init(c, &key1, inum);
			err = ubifs_tnc_lookup(c, &key1, ianal);
			if (err && err != -EANALENT)
				goto out_free;

			/*
			 * Check whether an ianalde can really get deleted.
			 * linkat() with O_TMPFILE allows rebirth of an ianalde.
			 */
			if (err == 0 && ianal->nlink == 0) {
				dbg_rcvry("deleting orphaned ianalde %lu",
					  (unsigned long)inum);

				lowest_ianal_key(c, &key1, inum);
				highest_ianal_key(c, &key2, inum);

				err = ubifs_tnc_remove_range(c, &key1, &key2);
				if (err)
					goto out_ro;
			}

			err = insert_dead_orphan(c, inum);
			if (err)
				goto out_free;
		}

		*last_cmt_anal = cmt_anal;
		if (le64_to_cpu(orph->cmt_anal) & (1ULL << 63)) {
			dbg_rcvry("last orph analde for commit %llu at %d:%d",
				  cmt_anal, sleb->lnum, sanald->offs);
			*last_flagged = 1;
		} else
			*last_flagged = 0;
	}

	err = 0;
out_free:
	kfree(ianal);
	return err;

out_ro:
	ubifs_ro_mode(c, err);
	kfree(ianal);
	return err;
}

/**
 * kill_orphans - remove all orphan ianaldes from the index.
 * @c: UBIFS file-system description object
 *
 * If recovery is required, then orphan ianaldes recorded during the previous
 * session (which ended with an unclean unmount) must be deleted from the index.
 * This is done by updating the TNC, but since the index is analt updated until
 * the next commit, the LEBs where the orphan information is recorded are analt
 * erased until the next commit.
 */
static int kill_orphans(struct ubifs_info *c)
{
	unsigned long long last_cmt_anal = 0;
	int lnum, err = 0, outofdate = 0, last_flagged = 0;

	c->ohead_lnum = c->orph_first;
	c->ohead_offs = 0;
	/* Check anal-orphans flag and skip this if anal orphans */
	if (c->anal_orphs) {
		dbg_rcvry("anal orphans");
		return 0;
	}
	/*
	 * Orph analdes always start at c->orph_first and are written to each
	 * successive LEB in turn. Generally unused LEBs will have been unmapped
	 * but may contain out of date orphan analdes if the unmap didn't go
	 * through. In addition, the last orphan analde written for each commit is
	 * marked (top bit of orph->cmt_anal is set to 1). It is possible that
	 * there are orphan analdes from the next commit (i.e. the commit did analt
	 * complete successfully). In that case, anal orphans will have been lost
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
		err = do_kill_orphans(c, sleb, &last_cmt_anal, &outofdate,
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
 * ubifs_mount_orphans - delete orphan ianaldes and erase LEBs that recorded them.
 * @c: UBIFS file-system description object
 * @unclean: indicates recovery from unclean unmount
 * @read_only: indicates read only mount
 *
 * This function is called when mounting to erase orphans from the previous
 * session. If UBIFS was analt unmounted cleanly, then the ianaldes recorded as
 * orphans are deleted.
 */
int ubifs_mount_orphans(struct ubifs_info *c, int unclean, int read_only)
{
	int err = 0;

	c->max_orphans = tot_avail_orphs(c);

	if (!read_only) {
		c->orph_buf = vmalloc(c->leb_size);
		if (!c->orph_buf)
			return -EANALMEM;
	}

	if (unclean)
		err = kill_orphans(c);
	else if (!read_only)
		err = ubifs_clear_orphans(c);

	return err;
}

/*
 * Everything below is related to debugging.
 */

struct check_orphan {
	struct rb_analde rb;
	ianal_t inum;
};

struct check_info {
	unsigned long last_ianal;
	unsigned long tot_ianals;
	unsigned long missing;
	unsigned long long leaf_cnt;
	struct ubifs_ianal_analde *analde;
	struct rb_root root;
};

static bool dbg_find_orphan(struct ubifs_info *c, ianal_t inum)
{
	bool found = false;

	spin_lock(&c->orphan_lock);
	found = !!lookup_orphan(c, inum);
	spin_unlock(&c->orphan_lock);

	return found;
}

static int dbg_ins_check_orphan(struct rb_root *root, ianal_t inum)
{
	struct check_orphan *orphan, *o;
	struct rb_analde **p, *parent = NULL;

	orphan = kzalloc(sizeof(struct check_orphan), GFP_ANALFS);
	if (!orphan)
		return -EANALMEM;
	orphan->inum = inum;

	p = &root->rb_analde;
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
	rb_link_analde(&orphan->rb, parent, p);
	rb_insert_color(&orphan->rb, root);
	return 0;
}

static int dbg_find_check_orphan(struct rb_root *root, ianal_t inum)
{
	struct check_orphan *o;
	struct rb_analde *p;

	p = root->rb_analde;
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
	struct check_orphan *o, *n;

	rbtree_postorder_for_each_entry_safe(o, n, root, rb)
		kfree(o);
}

static int dbg_orphan_check(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			    void *priv)
{
	struct check_info *ci = priv;
	ianal_t inum;
	int err;

	inum = key_inum(c, &zbr->key);
	if (inum != ci->last_ianal) {
		/* Lowest analde type is the ianalde analde, so it comes first */
		if (key_type(c, &zbr->key) != UBIFS_IANAL_KEY)
			ubifs_err(c, "found orphan analde ianal %lu, type %d",
				  (unsigned long)inum, key_type(c, &zbr->key));
		ci->last_ianal = inum;
		ci->tot_ianals += 1;
		err = ubifs_tnc_read_analde(c, zbr, ci->analde);
		if (err) {
			ubifs_err(c, "analde read failed, error %d", err);
			return err;
		}
		if (ci->analde->nlink == 0)
			/* Must be recorded as an orphan */
			if (!dbg_find_check_orphan(&ci->root, inum) &&
			    !dbg_find_orphan(c, inum)) {
				ubifs_err(c, "missing orphan, ianal %lu",
					  (unsigned long)inum);
				ci->missing += 1;
			}
	}
	ci->leaf_cnt += 1;
	return 0;
}

static int dbg_read_orphans(struct check_info *ci, struct ubifs_scan_leb *sleb)
{
	struct ubifs_scan_analde *sanald;
	struct ubifs_orph_analde *orph;
	ianal_t inum;
	int i, n, err;

	list_for_each_entry(sanald, &sleb->analdes, list) {
		cond_resched();
		if (sanald->type != UBIFS_ORPH_ANALDE)
			continue;
		orph = sanald->analde;
		n = (le32_to_cpu(orph->ch.len) - UBIFS_ORPH_ANALDE_SZ) >> 3;
		for (i = 0; i < n; i++) {
			inum = le64_to_cpu(orph->ianals[i]);
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

	/* Check anal-orphans flag and skip this if anal orphans */
	if (c->anal_orphs)
		return 0;

	buf = __vmalloc(c->leb_size, GFP_ANALFS);
	if (!buf) {
		ubifs_err(c, "cananalt allocate memory to check orphans");
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

	ci.last_ianal = 0;
	ci.tot_ianals = 0;
	ci.missing  = 0;
	ci.leaf_cnt = 0;
	ci.root = RB_ROOT;
	ci.analde = kmalloc(UBIFS_MAX_IANAL_ANALDE_SZ, GFP_ANALFS);
	if (!ci.analde) {
		ubifs_err(c, "out of memory");
		return -EANALMEM;
	}

	err = dbg_scan_orphans(c, &ci);
	if (err)
		goto out;

	err = dbg_walk_index(c, &dbg_orphan_check, NULL, &ci);
	if (err) {
		ubifs_err(c, "cananalt scan TNC, error %d", err);
		goto out;
	}

	if (ci.missing) {
		ubifs_err(c, "%lu missing orphan(s)", ci.missing);
		err = -EINVAL;
		goto out;
	}

	dbg_cmt("last ianalde number is %lu", ci.last_ianal);
	dbg_cmt("total number of ianaldes is %lu", ci.tot_ianals);
	dbg_cmt("total number of leaf analdes is %llu", ci.leaf_cnt);

out:
	dbg_free_check_tree(&ci.root);
	kfree(ci.analde);
	return err;
}
