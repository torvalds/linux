/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/xattr.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "eaops.h"
#include "eattr.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

/**
 * ea_calc_size - returns the acutal number of bytes the request will take up
 *                (not counting any unstuffed data blocks)
 * @sdp:
 * @er:
 * @size:
 *
 * Returns: 1 if the EA should be stuffed
 */

static int ea_calc_size(struct gfs2_sbd *sdp, struct gfs2_ea_request *er,
			unsigned int *size)
{
	*size = GFS2_EAREQ_SIZE_STUFFED(er);
	if (*size <= sdp->sd_jbsize)
		return 1;

	*size = GFS2_EAREQ_SIZE_UNSTUFFED(sdp, er);

	return 0;
}

static int ea_check_size(struct gfs2_sbd *sdp, struct gfs2_ea_request *er)
{
	unsigned int size;

	if (er->er_data_len > GFS2_EA_MAX_DATA_LEN)
		return -ERANGE;

	ea_calc_size(sdp, er, &size);

	/* This can only happen with 512 byte blocks */
	if (size > sdp->sd_jbsize)
		return -ERANGE;

	return 0;
}

typedef int (*ea_call_t) (struct gfs2_inode *ip, struct buffer_head *bh,
			  struct gfs2_ea_header *ea,
			  struct gfs2_ea_header *prev, void *private);

static int ea_foreach_i(struct gfs2_inode *ip, struct buffer_head *bh,
			ea_call_t ea_call, void *data)
{
	struct gfs2_ea_header *ea, *prev = NULL;
	int error = 0;

	if (gfs2_metatype_check(GFS2_SB(&ip->i_inode), bh, GFS2_METATYPE_EA))
		return -EIO;

	for (ea = GFS2_EA_BH2FIRST(bh);; prev = ea, ea = GFS2_EA2NEXT(ea)) {
		if (!GFS2_EA_REC_LEN(ea))
			goto fail;
		if (!(bh->b_data <= (char *)ea && (char *)GFS2_EA2NEXT(ea) <=
						  bh->b_data + bh->b_size))
			goto fail;
		if (!GFS2_EATYPE_VALID(ea->ea_type))
			goto fail;

		error = ea_call(ip, bh, ea, prev, data);
		if (error)
			return error;

		if (GFS2_EA_IS_LAST(ea)) {
			if ((char *)GFS2_EA2NEXT(ea) !=
			    bh->b_data + bh->b_size)
				goto fail;
			break;
		}
	}

	return error;

fail:
	gfs2_consist_inode(ip);
	return -EIO;
}

static int ea_foreach(struct gfs2_inode *ip, ea_call_t ea_call, void *data)
{
	struct buffer_head *bh, *eabh;
	__be64 *eablk, *end;
	int error;

	error = gfs2_meta_read(ip->i_gl, ip->i_di.di_eattr, DIO_WAIT, &bh);
	if (error)
		return error;

	if (!(ip->i_di.di_flags & GFS2_DIF_EA_INDIRECT)) {
		error = ea_foreach_i(ip, bh, ea_call, data);
		goto out;
	}

	if (gfs2_metatype_check(GFS2_SB(&ip->i_inode), bh, GFS2_METATYPE_IN)) {
		error = -EIO;
		goto out;
	}

	eablk = (__be64 *)(bh->b_data + sizeof(struct gfs2_meta_header));
	end = eablk + GFS2_SB(&ip->i_inode)->sd_inptrs;

	for (; eablk < end; eablk++) {
		u64 bn;

		if (!*eablk)
			break;
		bn = be64_to_cpu(*eablk);

		error = gfs2_meta_read(ip->i_gl, bn, DIO_WAIT, &eabh);
		if (error)
			break;
		error = ea_foreach_i(ip, eabh, ea_call, data);
		brelse(eabh);
		if (error)
			break;
	}
out:
	brelse(bh);
	return error;
}

struct ea_find {
	struct gfs2_ea_request *ef_er;
	struct gfs2_ea_location *ef_el;
};

static int ea_find_i(struct gfs2_inode *ip, struct buffer_head *bh,
		     struct gfs2_ea_header *ea, struct gfs2_ea_header *prev,
		     void *private)
{
	struct ea_find *ef = private;
	struct gfs2_ea_request *er = ef->ef_er;

	if (ea->ea_type == GFS2_EATYPE_UNUSED)
		return 0;

	if (ea->ea_type == er->er_type) {
		if (ea->ea_name_len == er->er_name_len &&
		    !memcmp(GFS2_EA2NAME(ea), er->er_name, ea->ea_name_len)) {
			struct gfs2_ea_location *el = ef->ef_el;
			get_bh(bh);
			el->el_bh = bh;
			el->el_ea = ea;
			el->el_prev = prev;
			return 1;
		}
	}

	return 0;
}

int gfs2_ea_find(struct gfs2_inode *ip, struct gfs2_ea_request *er,
		 struct gfs2_ea_location *el)
{
	struct ea_find ef;
	int error;

	ef.ef_er = er;
	ef.ef_el = el;

	memset(el, 0, sizeof(struct gfs2_ea_location));

	error = ea_foreach(ip, ea_find_i, &ef);
	if (error > 0)
		return 0;

	return error;
}

/**
 * ea_dealloc_unstuffed -
 * @ip:
 * @bh:
 * @ea:
 * @prev:
 * @private:
 *
 * Take advantage of the fact that all unstuffed blocks are
 * allocated from the same RG.  But watch, this may not always
 * be true.
 *
 * Returns: errno
 */

static int ea_dealloc_unstuffed(struct gfs2_inode *ip, struct buffer_head *bh,
				struct gfs2_ea_header *ea,
				struct gfs2_ea_header *prev, void *private)
{
	int *leave = private;
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder rg_gh;
	struct buffer_head *dibh;
	__be64 *dataptrs;
	u64 bn = 0;
	u64 bstart = 0;
	unsigned int blen = 0;
	unsigned int blks = 0;
	unsigned int x;
	int error;

	if (GFS2_EA_IS_STUFFED(ea))
		return 0;

	dataptrs = GFS2_EA2DATAPTRS(ea);
	for (x = 0; x < ea->ea_num_ptrs; x++, dataptrs++) {
		if (*dataptrs) {
			blks++;
			bn = be64_to_cpu(*dataptrs);
		}
	}
	if (!blks)
		return 0;

	rgd = gfs2_blk2rgrpd(sdp, bn);
	if (!rgd) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rg_gh);
	if (error)
		return error;

	error = gfs2_trans_begin(sdp, rgd->rd_ri.ri_length + RES_DINODE +
				 RES_EATTR + RES_STATFS + RES_QUOTA, blks);
	if (error)
		goto out_gunlock;

	gfs2_trans_add_bh(ip->i_gl, bh, 1);

	dataptrs = GFS2_EA2DATAPTRS(ea);
	for (x = 0; x < ea->ea_num_ptrs; x++, dataptrs++) {
		if (!*dataptrs)
			break;
		bn = be64_to_cpu(*dataptrs);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart)
				gfs2_free_meta(ip, bstart, blen);
			bstart = bn;
			blen = 1;
		}

		*dataptrs = 0;
		if (!ip->i_di.di_blocks)
			gfs2_consist_inode(ip);
		ip->i_di.di_blocks--;
	}
	if (bstart)
		gfs2_free_meta(ip, bstart, blen);

	if (prev && !leave) {
		u32 len;

		len = GFS2_EA_REC_LEN(prev) + GFS2_EA_REC_LEN(ea);
		prev->ea_rec_len = cpu_to_be32(len);

		if (GFS2_EA_IS_LAST(ea))
			prev->ea_flags |= GFS2_EAFLAG_LAST;
	} else {
		ea->ea_type = GFS2_EATYPE_UNUSED;
		ea->ea_num_ptrs = 0;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		ip->i_di.di_ctime = get_seconds();
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(sdp);

out_gunlock:
	gfs2_glock_dq_uninit(&rg_gh);
	return error;
}

static int ea_remove_unstuffed(struct gfs2_inode *ip, struct buffer_head *bh,
			       struct gfs2_ea_header *ea,
			       struct gfs2_ea_header *prev, int leave)
{
	struct gfs2_alloc *al;
	int error;

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out_alloc;

	error = gfs2_rindex_hold(GFS2_SB(&ip->i_inode), &al->al_ri_gh);
	if (error)
		goto out_quota;

	error = ea_dealloc_unstuffed(ip, bh, ea, prev, (leave) ? &error : NULL);

	gfs2_glock_dq_uninit(&al->al_ri_gh);

out_quota:
	gfs2_quota_unhold(ip);
out_alloc:
	gfs2_alloc_put(ip);
	return error;
}

struct ea_list {
	struct gfs2_ea_request *ei_er;
	unsigned int ei_size;
};

static int ea_list_i(struct gfs2_inode *ip, struct buffer_head *bh,
		     struct gfs2_ea_header *ea, struct gfs2_ea_header *prev,
		     void *private)
{
	struct ea_list *ei = private;
	struct gfs2_ea_request *er = ei->ei_er;
	unsigned int ea_size = gfs2_ea_strlen(ea);

	if (ea->ea_type == GFS2_EATYPE_UNUSED)
		return 0;

	if (er->er_data_len) {
		char *prefix = NULL;
		unsigned int l = 0;
		char c = 0;

		if (ei->ei_size + ea_size > er->er_data_len)
			return -ERANGE;

		switch (ea->ea_type) {
		case GFS2_EATYPE_USR:
			prefix = "user.";
			l = 5;
			break;
		case GFS2_EATYPE_SYS:
			prefix = "system.";
			l = 7;
			break;
		case GFS2_EATYPE_SECURITY:
			prefix = "security.";
			l = 9;
			break;
		}

		BUG_ON(l == 0);

		memcpy(er->er_data + ei->ei_size, prefix, l);
		memcpy(er->er_data + ei->ei_size + l, GFS2_EA2NAME(ea),
		       ea->ea_name_len);
		memcpy(er->er_data + ei->ei_size + ea_size - 1, &c, 1);
	}

	ei->ei_size += ea_size;

	return 0;
}

/**
 * gfs2_ea_list -
 * @ip:
 * @er:
 *
 * Returns: actual size of data on success, -errno on error
 */

int gfs2_ea_list(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_holder i_gh;
	int error;

	if (!er->er_data || !er->er_data_len) {
		er->er_data = NULL;
		er->er_data_len = 0;
	}

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return error;

	if (ip->i_di.di_eattr) {
		struct ea_list ei = { .ei_er = er, .ei_size = 0 };

		error = ea_foreach(ip, ea_list_i, &ei);
		if (!error)
			error = ei.ei_size;
	}

	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * ea_get_unstuffed - actually copies the unstuffed data into the
 *                    request buffer
 * @ip: The GFS2 inode
 * @ea: The extended attribute header structure
 * @data: The data to be copied
 *
 * Returns: errno
 */

static int ea_get_unstuffed(struct gfs2_inode *ip, struct gfs2_ea_header *ea,
			    char *data)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head **bh;
	unsigned int amount = GFS2_EA_DATA_LEN(ea);
	unsigned int nptrs = DIV_ROUND_UP(amount, sdp->sd_jbsize);
	__be64 *dataptrs = GFS2_EA2DATAPTRS(ea);
	unsigned int x;
	int error = 0;

	bh = kcalloc(nptrs, sizeof(struct buffer_head *), GFP_KERNEL);
	if (!bh)
		return -ENOMEM;

	for (x = 0; x < nptrs; x++) {
		error = gfs2_meta_read(ip->i_gl, be64_to_cpu(*dataptrs), 0,
				       bh + x);
		if (error) {
			while (x--)
				brelse(bh[x]);
			goto out;
		}
		dataptrs++;
	}

	for (x = 0; x < nptrs; x++) {
		error = gfs2_meta_wait(sdp, bh[x]);
		if (error) {
			for (; x < nptrs; x++)
				brelse(bh[x]);
			goto out;
		}
		if (gfs2_metatype_check(sdp, bh[x], GFS2_METATYPE_ED)) {
			for (; x < nptrs; x++)
				brelse(bh[x]);
			error = -EIO;
			goto out;
		}

		memcpy(data, bh[x]->b_data + sizeof(struct gfs2_meta_header),
		       (sdp->sd_jbsize > amount) ? amount : sdp->sd_jbsize);

		amount -= sdp->sd_jbsize;
		data += sdp->sd_jbsize;

		brelse(bh[x]);
	}

out:
	kfree(bh);
	return error;
}

int gfs2_ea_get_copy(struct gfs2_inode *ip, struct gfs2_ea_location *el,
		     char *data)
{
	if (GFS2_EA_IS_STUFFED(el->el_ea)) {
		memcpy(data, GFS2_EA2DATA(el->el_ea), GFS2_EA_DATA_LEN(el->el_ea));
		return 0;
	} else
		return ea_get_unstuffed(ip, el->el_ea, data);
}

/**
 * gfs2_ea_get_i -
 * @ip: The GFS2 inode
 * @er: The request structure
 *
 * Returns: actual size of data on success, -errno on error
 */

int gfs2_ea_get_i(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_ea_location el;
	int error;

	if (!ip->i_di.di_eattr)
		return -ENODATA;

	error = gfs2_ea_find(ip, er, &el);
	if (error)
		return error;
	if (!el.el_ea)
		return -ENODATA;

	if (er->er_data_len) {
		if (GFS2_EA_DATA_LEN(el.el_ea) > er->er_data_len)
			error =  -ERANGE;
		else
			error = gfs2_ea_get_copy(ip, &el, er->er_data);
	}
	if (!error)
		error = GFS2_EA_DATA_LEN(el.el_ea);

	brelse(el.el_bh);

	return error;
}

/**
 * gfs2_ea_get -
 * @ip: The GFS2 inode
 * @er: The request structure
 *
 * Returns: actual size of data on success, -errno on error
 */

int gfs2_ea_get(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_holder i_gh;
	int error;

	if (!er->er_name_len ||
	    er->er_name_len > GFS2_EA_MAX_NAME_LEN)
		return -EINVAL;
	if (!er->er_data || !er->er_data_len) {
		er->er_data = NULL;
		er->er_data_len = 0;
	}

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return error;

	error = gfs2_ea_ops[er->er_type]->eo_get(ip, er);

	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * ea_alloc_blk - allocates a new block for extended attributes.
 * @ip: A pointer to the inode that's getting extended attributes
 * @bhp: Pointer to pointer to a struct buffer_head
 *
 * Returns: errno
 */

static int ea_alloc_blk(struct gfs2_inode *ip, struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_ea_header *ea;
	u64 block;

	block = gfs2_alloc_meta(ip);

	*bhp = gfs2_meta_new(ip->i_gl, block);
	gfs2_trans_add_bh(ip->i_gl, *bhp, 1);
	gfs2_metatype_set(*bhp, GFS2_METATYPE_EA, GFS2_FORMAT_EA);
	gfs2_buffer_clear_tail(*bhp, sizeof(struct gfs2_meta_header));

	ea = GFS2_EA_BH2FIRST(*bhp);
	ea->ea_rec_len = cpu_to_be32(sdp->sd_jbsize);
	ea->ea_type = GFS2_EATYPE_UNUSED;
	ea->ea_flags = GFS2_EAFLAG_LAST;
	ea->ea_num_ptrs = 0;

	ip->i_di.di_blocks++;

	return 0;
}

/**
 * ea_write - writes the request info to an ea, creating new blocks if
 *            necessary
 * @ip: inode that is being modified
 * @ea: the location of the new ea in a block
 * @er: the write request
 *
 * Note: does not update ea_rec_len or the GFS2_EAFLAG_LAST bin of ea_flags
 *
 * returns : errno
 */

static int ea_write(struct gfs2_inode *ip, struct gfs2_ea_header *ea,
		    struct gfs2_ea_request *er)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	ea->ea_data_len = cpu_to_be32(er->er_data_len);
	ea->ea_name_len = er->er_name_len;
	ea->ea_type = er->er_type;
	ea->__pad = 0;

	memcpy(GFS2_EA2NAME(ea), er->er_name, er->er_name_len);

	if (GFS2_EAREQ_SIZE_STUFFED(er) <= sdp->sd_jbsize) {
		ea->ea_num_ptrs = 0;
		memcpy(GFS2_EA2DATA(ea), er->er_data, er->er_data_len);
	} else {
		__be64 *dataptr = GFS2_EA2DATAPTRS(ea);
		const char *data = er->er_data;
		unsigned int data_len = er->er_data_len;
		unsigned int copy;
		unsigned int x;

		ea->ea_num_ptrs = DIV_ROUND_UP(er->er_data_len, sdp->sd_jbsize);
		for (x = 0; x < ea->ea_num_ptrs; x++) {
			struct buffer_head *bh;
			u64 block;
			int mh_size = sizeof(struct gfs2_meta_header);

			block = gfs2_alloc_meta(ip);

			bh = gfs2_meta_new(ip->i_gl, block);
			gfs2_trans_add_bh(ip->i_gl, bh, 1);
			gfs2_metatype_set(bh, GFS2_METATYPE_ED, GFS2_FORMAT_ED);

			ip->i_di.di_blocks++;

			copy = data_len > sdp->sd_jbsize ? sdp->sd_jbsize :
							   data_len;
			memcpy(bh->b_data + mh_size, data, copy);
			if (copy < sdp->sd_jbsize)
				memset(bh->b_data + mh_size + copy, 0,
				       sdp->sd_jbsize - copy);

			*dataptr++ = cpu_to_be64(bh->b_blocknr);
			data += copy;
			data_len -= copy;

			brelse(bh);
		}

		gfs2_assert_withdraw(sdp, !data_len);
	}

	return 0;
}

typedef int (*ea_skeleton_call_t) (struct gfs2_inode *ip,
				   struct gfs2_ea_request *er, void *private);

static int ea_alloc_skeleton(struct gfs2_inode *ip, struct gfs2_ea_request *er,
			     unsigned int blks,
			     ea_skeleton_call_t skeleton_call, void *private)
{
	struct gfs2_alloc *al;
	struct buffer_head *dibh;
	int error;

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_quota_check(ip, ip->i_inode.i_uid, ip->i_inode.i_gid);
	if (error)
		goto out_gunlock_q;

	al->al_requested = blks;

	error = gfs2_inplace_reserve(ip);
	if (error)
		goto out_gunlock_q;

	error = gfs2_trans_begin(GFS2_SB(&ip->i_inode),
				 blks + al->al_rgd->rd_ri.ri_length +
				 RES_DINODE + RES_STATFS + RES_QUOTA, 0);
	if (error)
		goto out_ipres;

	error = skeleton_call(ip, er, private);
	if (error)
		goto out_end_trans;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		if (er->er_flags & GFS2_ERF_MODE) {
			gfs2_assert_withdraw(GFS2_SB(&ip->i_inode),
					    (ip->i_inode.i_mode & S_IFMT) ==
					    (er->er_mode & S_IFMT));
			ip->i_inode.i_mode = er->er_mode;
		}
		ip->i_di.di_ctime = get_seconds();
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

out_end_trans:
	gfs2_trans_end(GFS2_SB(&ip->i_inode));
out_ipres:
	gfs2_inplace_release(ip);
out_gunlock_q:
	gfs2_quota_unlock(ip);
out:
	gfs2_alloc_put(ip);
	return error;
}

static int ea_init_i(struct gfs2_inode *ip, struct gfs2_ea_request *er,
		     void *private)
{
	struct buffer_head *bh;
	int error;

	error = ea_alloc_blk(ip, &bh);
	if (error)
		return error;

	ip->i_di.di_eattr = bh->b_blocknr;
	error = ea_write(ip, GFS2_EA_BH2FIRST(bh), er);

	brelse(bh);

	return error;
}

/**
 * ea_init - initializes a new eattr block
 * @ip:
 * @er:
 *
 * Returns: errno
 */

static int ea_init(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	unsigned int jbsize = GFS2_SB(&ip->i_inode)->sd_jbsize;
	unsigned int blks = 1;

	if (GFS2_EAREQ_SIZE_STUFFED(er) > jbsize)
		blks += DIV_ROUND_UP(er->er_data_len, jbsize);

	return ea_alloc_skeleton(ip, er, blks, ea_init_i, NULL);
}

static struct gfs2_ea_header *ea_split_ea(struct gfs2_ea_header *ea)
{
	u32 ea_size = GFS2_EA_SIZE(ea);
	struct gfs2_ea_header *new = (struct gfs2_ea_header *)((char *)ea +
				     ea_size);
	u32 new_size = GFS2_EA_REC_LEN(ea) - ea_size;
	int last = ea->ea_flags & GFS2_EAFLAG_LAST;

	ea->ea_rec_len = cpu_to_be32(ea_size);
	ea->ea_flags ^= last;

	new->ea_rec_len = cpu_to_be32(new_size);
	new->ea_flags = last;

	return new;
}

static void ea_set_remove_stuffed(struct gfs2_inode *ip,
				  struct gfs2_ea_location *el)
{
	struct gfs2_ea_header *ea = el->el_ea;
	struct gfs2_ea_header *prev = el->el_prev;
	u32 len;

	gfs2_trans_add_bh(ip->i_gl, el->el_bh, 1);

	if (!prev || !GFS2_EA_IS_STUFFED(ea)) {
		ea->ea_type = GFS2_EATYPE_UNUSED;
		return;
	} else if (GFS2_EA2NEXT(prev) != ea) {
		prev = GFS2_EA2NEXT(prev);
		gfs2_assert_withdraw(GFS2_SB(&ip->i_inode), GFS2_EA2NEXT(prev) == ea);
	}

	len = GFS2_EA_REC_LEN(prev) + GFS2_EA_REC_LEN(ea);
	prev->ea_rec_len = cpu_to_be32(len);

	if (GFS2_EA_IS_LAST(ea))
		prev->ea_flags |= GFS2_EAFLAG_LAST;
}

struct ea_set {
	int ea_split;

	struct gfs2_ea_request *es_er;
	struct gfs2_ea_location *es_el;

	struct buffer_head *es_bh;
	struct gfs2_ea_header *es_ea;
};

static int ea_set_simple_noalloc(struct gfs2_inode *ip, struct buffer_head *bh,
				 struct gfs2_ea_header *ea, struct ea_set *es)
{
	struct gfs2_ea_request *er = es->es_er;
	struct buffer_head *dibh;
	int error;

	error = gfs2_trans_begin(GFS2_SB(&ip->i_inode), RES_DINODE + 2 * RES_EATTR, 0);
	if (error)
		return error;

	gfs2_trans_add_bh(ip->i_gl, bh, 1);

	if (es->ea_split)
		ea = ea_split_ea(ea);

	ea_write(ip, ea, er);

	if (es->es_el)
		ea_set_remove_stuffed(ip, es->es_el);

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	if (er->er_flags & GFS2_ERF_MODE) {
		gfs2_assert_withdraw(GFS2_SB(&ip->i_inode),
			(ip->i_inode.i_mode & S_IFMT) == (er->er_mode & S_IFMT));
		ip->i_inode.i_mode = er->er_mode;
	}
	ip->i_di.di_ctime = get_seconds();
	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);
out:
	gfs2_trans_end(GFS2_SB(&ip->i_inode));
	return error;
}

static int ea_set_simple_alloc(struct gfs2_inode *ip,
			       struct gfs2_ea_request *er, void *private)
{
	struct ea_set *es = private;
	struct gfs2_ea_header *ea = es->es_ea;
	int error;

	gfs2_trans_add_bh(ip->i_gl, es->es_bh, 1);

	if (es->ea_split)
		ea = ea_split_ea(ea);

	error = ea_write(ip, ea, er);
	if (error)
		return error;

	if (es->es_el)
		ea_set_remove_stuffed(ip, es->es_el);

	return 0;
}

static int ea_set_simple(struct gfs2_inode *ip, struct buffer_head *bh,
			 struct gfs2_ea_header *ea, struct gfs2_ea_header *prev,
			 void *private)
{
	struct ea_set *es = private;
	unsigned int size;
	int stuffed;
	int error;

	stuffed = ea_calc_size(GFS2_SB(&ip->i_inode), es->es_er, &size);

	if (ea->ea_type == GFS2_EATYPE_UNUSED) {
		if (GFS2_EA_REC_LEN(ea) < size)
			return 0;
		if (!GFS2_EA_IS_STUFFED(ea)) {
			error = ea_remove_unstuffed(ip, bh, ea, prev, 1);
			if (error)
				return error;
		}
		es->ea_split = 0;
	} else if (GFS2_EA_REC_LEN(ea) - GFS2_EA_SIZE(ea) >= size)
		es->ea_split = 1;
	else
		return 0;

	if (stuffed) {
		error = ea_set_simple_noalloc(ip, bh, ea, es);
		if (error)
			return error;
	} else {
		unsigned int blks;

		es->es_bh = bh;
		es->es_ea = ea;
		blks = 2 + DIV_ROUND_UP(es->es_er->er_data_len,
					GFS2_SB(&ip->i_inode)->sd_jbsize);

		error = ea_alloc_skeleton(ip, es->es_er, blks,
					  ea_set_simple_alloc, es);
		if (error)
			return error;
	}

	return 1;
}

static int ea_set_block(struct gfs2_inode *ip, struct gfs2_ea_request *er,
			void *private)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *indbh, *newbh;
	__be64 *eablk;
	int error;
	int mh_size = sizeof(struct gfs2_meta_header);

	if (ip->i_di.di_flags & GFS2_DIF_EA_INDIRECT) {
		__be64 *end;

		error = gfs2_meta_read(ip->i_gl, ip->i_di.di_eattr, DIO_WAIT,
				       &indbh);
		if (error)
			return error;

		if (gfs2_metatype_check(sdp, indbh, GFS2_METATYPE_IN)) {
			error = -EIO;
			goto out;
		}

		eablk = (__be64 *)(indbh->b_data + mh_size);
		end = eablk + sdp->sd_inptrs;

		for (; eablk < end; eablk++)
			if (!*eablk)
				break;

		if (eablk == end) {
			error = -ENOSPC;
			goto out;
		}

		gfs2_trans_add_bh(ip->i_gl, indbh, 1);
	} else {
		u64 blk;

		blk = gfs2_alloc_meta(ip);

		indbh = gfs2_meta_new(ip->i_gl, blk);
		gfs2_trans_add_bh(ip->i_gl, indbh, 1);
		gfs2_metatype_set(indbh, GFS2_METATYPE_IN, GFS2_FORMAT_IN);
		gfs2_buffer_clear_tail(indbh, mh_size);

		eablk = (__be64 *)(indbh->b_data + mh_size);
		*eablk = cpu_to_be64(ip->i_di.di_eattr);
		ip->i_di.di_eattr = blk;
		ip->i_di.di_flags |= GFS2_DIF_EA_INDIRECT;
		ip->i_di.di_blocks++;

		eablk++;
	}

	error = ea_alloc_blk(ip, &newbh);
	if (error)
		goto out;

	*eablk = cpu_to_be64((u64)newbh->b_blocknr);
	error = ea_write(ip, GFS2_EA_BH2FIRST(newbh), er);
	brelse(newbh);
	if (error)
		goto out;

	if (private)
		ea_set_remove_stuffed(ip, private);

out:
	brelse(indbh);
	return error;
}

static int ea_set_i(struct gfs2_inode *ip, struct gfs2_ea_request *er,
		    struct gfs2_ea_location *el)
{
	struct ea_set es;
	unsigned int blks = 2;
	int error;

	memset(&es, 0, sizeof(struct ea_set));
	es.es_er = er;
	es.es_el = el;

	error = ea_foreach(ip, ea_set_simple, &es);
	if (error > 0)
		return 0;
	if (error)
		return error;

	if (!(ip->i_di.di_flags & GFS2_DIF_EA_INDIRECT))
		blks++;
	if (GFS2_EAREQ_SIZE_STUFFED(er) > GFS2_SB(&ip->i_inode)->sd_jbsize)
		blks += DIV_ROUND_UP(er->er_data_len, GFS2_SB(&ip->i_inode)->sd_jbsize);

	return ea_alloc_skeleton(ip, er, blks, ea_set_block, el);
}

static int ea_set_remove_unstuffed(struct gfs2_inode *ip,
				   struct gfs2_ea_location *el)
{
	if (el->el_prev && GFS2_EA2NEXT(el->el_prev) != el->el_ea) {
		el->el_prev = GFS2_EA2NEXT(el->el_prev);
		gfs2_assert_withdraw(GFS2_SB(&ip->i_inode),
				     GFS2_EA2NEXT(el->el_prev) == el->el_ea);
	}

	return ea_remove_unstuffed(ip, el->el_bh, el->el_ea, el->el_prev,0);
}

int gfs2_ea_set_i(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_ea_location el;
	int error;

	if (!ip->i_di.di_eattr) {
		if (er->er_flags & XATTR_REPLACE)
			return -ENODATA;
		return ea_init(ip, er);
	}

	error = gfs2_ea_find(ip, er, &el);
	if (error)
		return error;

	if (el.el_ea) {
		if (ip->i_di.di_flags & GFS2_DIF_APPENDONLY) {
			brelse(el.el_bh);
			return -EPERM;
		}

		error = -EEXIST;
		if (!(er->er_flags & XATTR_CREATE)) {
			int unstuffed = !GFS2_EA_IS_STUFFED(el.el_ea);
			error = ea_set_i(ip, er, &el);
			if (!error && unstuffed)
				ea_set_remove_unstuffed(ip, &el);
		}

		brelse(el.el_bh);
	} else {
		error = -ENODATA;
		if (!(er->er_flags & XATTR_REPLACE))
			error = ea_set_i(ip, er, NULL);
	}

	return error;
}

int gfs2_ea_set(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_holder i_gh;
	int error;

	if (!er->er_name_len || er->er_name_len > GFS2_EA_MAX_NAME_LEN)
		return -EINVAL;
	if (!er->er_data || !er->er_data_len) {
		er->er_data = NULL;
		er->er_data_len = 0;
	}
	error = ea_check_size(GFS2_SB(&ip->i_inode), er);
	if (error)
		return error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	if (IS_IMMUTABLE(&ip->i_inode))
		error = -EPERM;
	else
		error = gfs2_ea_ops[er->er_type]->eo_set(ip, er);

	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

static int ea_remove_stuffed(struct gfs2_inode *ip, struct gfs2_ea_location *el)
{
	struct gfs2_ea_header *ea = el->el_ea;
	struct gfs2_ea_header *prev = el->el_prev;
	struct buffer_head *dibh;
	int error;

	error = gfs2_trans_begin(GFS2_SB(&ip->i_inode), RES_DINODE + RES_EATTR, 0);
	if (error)
		return error;

	gfs2_trans_add_bh(ip->i_gl, el->el_bh, 1);

	if (prev) {
		u32 len;

		len = GFS2_EA_REC_LEN(prev) + GFS2_EA_REC_LEN(ea);
		prev->ea_rec_len = cpu_to_be32(len);

		if (GFS2_EA_IS_LAST(ea))
			prev->ea_flags |= GFS2_EAFLAG_LAST;
	} else
		ea->ea_type = GFS2_EATYPE_UNUSED;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		ip->i_di.di_ctime = get_seconds();
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(GFS2_SB(&ip->i_inode));

	return error;
}

int gfs2_ea_remove_i(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_ea_location el;
	int error;

	if (!ip->i_di.di_eattr)
		return -ENODATA;

	error = gfs2_ea_find(ip, er, &el);
	if (error)
		return error;
	if (!el.el_ea)
		return -ENODATA;

	if (GFS2_EA_IS_STUFFED(el.el_ea))
		error = ea_remove_stuffed(ip, &el);
	else
		error = ea_remove_unstuffed(ip, el.el_bh, el.el_ea, el.el_prev,
					    0);

	brelse(el.el_bh);

	return error;
}

/**
 * gfs2_ea_remove - sets (or creates or replaces) an extended attribute
 * @ip: pointer to the inode of the target file
 * @er: request information
 *
 * Returns: errno
 */

int gfs2_ea_remove(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct gfs2_holder i_gh;
	int error;

	if (!er->er_name_len || er->er_name_len > GFS2_EA_MAX_NAME_LEN)
		return -EINVAL;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	if (IS_IMMUTABLE(&ip->i_inode) || IS_APPEND(&ip->i_inode))
		error = -EPERM;
	else
		error = gfs2_ea_ops[er->er_type]->eo_remove(ip, er);

	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

static int ea_acl_chmod_unstuffed(struct gfs2_inode *ip,
				  struct gfs2_ea_header *ea, char *data)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head **bh;
	unsigned int amount = GFS2_EA_DATA_LEN(ea);
	unsigned int nptrs = DIV_ROUND_UP(amount, sdp->sd_jbsize);
	__be64 *dataptrs = GFS2_EA2DATAPTRS(ea);
	unsigned int x;
	int error;

	bh = kcalloc(nptrs, sizeof(struct buffer_head *), GFP_KERNEL);
	if (!bh)
		return -ENOMEM;

	error = gfs2_trans_begin(sdp, nptrs + RES_DINODE, 0);
	if (error)
		goto out;

	for (x = 0; x < nptrs; x++) {
		error = gfs2_meta_read(ip->i_gl, be64_to_cpu(*dataptrs), 0,
				       bh + x);
		if (error) {
			while (x--)
				brelse(bh[x]);
			goto fail;
		}
		dataptrs++;
	}

	for (x = 0; x < nptrs; x++) {
		error = gfs2_meta_wait(sdp, bh[x]);
		if (error) {
			for (; x < nptrs; x++)
				brelse(bh[x]);
			goto fail;
		}
		if (gfs2_metatype_check(sdp, bh[x], GFS2_METATYPE_ED)) {
			for (; x < nptrs; x++)
				brelse(bh[x]);
			error = -EIO;
			goto fail;
		}

		gfs2_trans_add_bh(ip->i_gl, bh[x], 1);

		memcpy(bh[x]->b_data + sizeof(struct gfs2_meta_header), data,
		       (sdp->sd_jbsize > amount) ? amount : sdp->sd_jbsize);

		amount -= sdp->sd_jbsize;
		data += sdp->sd_jbsize;

		brelse(bh[x]);
	}

out:
	kfree(bh);
	return error;

fail:
	gfs2_trans_end(sdp);
	kfree(bh);
	return error;
}

int gfs2_ea_acl_chmod(struct gfs2_inode *ip, struct gfs2_ea_location *el,
		      struct iattr *attr, char *data)
{
	struct buffer_head *dibh;
	int error;

	if (GFS2_EA_IS_STUFFED(el->el_ea)) {
		error = gfs2_trans_begin(GFS2_SB(&ip->i_inode), RES_DINODE + RES_EATTR, 0);
		if (error)
			return error;

		gfs2_trans_add_bh(ip->i_gl, el->el_bh, 1);
		memcpy(GFS2_EA2DATA(el->el_ea), data,
		       GFS2_EA_DATA_LEN(el->el_ea));
	} else
		error = ea_acl_chmod_unstuffed(ip, el->el_ea, data);

	if (error)
		return error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		error = inode_setattr(&ip->i_inode, attr);
		gfs2_assert_warn(GFS2_SB(&ip->i_inode), !error);
		gfs2_inode_attr_out(ip);
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(GFS2_SB(&ip->i_inode));

	return error;
}

static int ea_dealloc_indirect(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrp_list rlist;
	struct buffer_head *indbh, *dibh;
	__be64 *eablk, *end;
	unsigned int rg_blocks = 0;
	u64 bstart = 0;
	unsigned int blen = 0;
	unsigned int blks = 0;
	unsigned int x;
	int error;

	memset(&rlist, 0, sizeof(struct gfs2_rgrp_list));

	error = gfs2_meta_read(ip->i_gl, ip->i_di.di_eattr, DIO_WAIT, &indbh);
	if (error)
		return error;

	if (gfs2_metatype_check(sdp, indbh, GFS2_METATYPE_IN)) {
		error = -EIO;
		goto out;
	}

	eablk = (__be64 *)(indbh->b_data + sizeof(struct gfs2_meta_header));
	end = eablk + sdp->sd_inptrs;

	for (; eablk < end; eablk++) {
		u64 bn;

		if (!*eablk)
			break;
		bn = be64_to_cpu(*eablk);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart)
				gfs2_rlist_add(sdp, &rlist, bstart);
			bstart = bn;
			blen = 1;
		}
		blks++;
	}
	if (bstart)
		gfs2_rlist_add(sdp, &rlist, bstart);
	else
		goto out;

	gfs2_rlist_alloc(&rlist, LM_ST_EXCLUSIVE, 0);

	for (x = 0; x < rlist.rl_rgrps; x++) {
		struct gfs2_rgrpd *rgd;
		rgd = rlist.rl_ghs[x].gh_gl->gl_object;
		rg_blocks += rgd->rd_ri.ri_length;
	}

	error = gfs2_glock_nq_m(rlist.rl_rgrps, rlist.rl_ghs);
	if (error)
		goto out_rlist_free;

	error = gfs2_trans_begin(sdp, rg_blocks + RES_DINODE + RES_INDIRECT +
				 RES_STATFS + RES_QUOTA, blks);
	if (error)
		goto out_gunlock;

	gfs2_trans_add_bh(ip->i_gl, indbh, 1);

	eablk = (__be64 *)(indbh->b_data + sizeof(struct gfs2_meta_header));
	bstart = 0;
	blen = 0;

	for (; eablk < end; eablk++) {
		u64 bn;

		if (!*eablk)
			break;
		bn = be64_to_cpu(*eablk);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart)
				gfs2_free_meta(ip, bstart, blen);
			bstart = bn;
			blen = 1;
		}

		*eablk = 0;
		if (!ip->i_di.di_blocks)
			gfs2_consist_inode(ip);
		ip->i_di.di_blocks--;
	}
	if (bstart)
		gfs2_free_meta(ip, bstart, blen);

	ip->i_di.di_flags &= ~GFS2_DIF_EA_INDIRECT;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(sdp);

out_gunlock:
	gfs2_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);
out_rlist_free:
	gfs2_rlist_free(&rlist);
out:
	brelse(indbh);
	return error;
}

static int ea_dealloc_block(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_rgrpd *rgd;
	struct buffer_head *dibh;
	int error;

	rgd = gfs2_blk2rgrpd(sdp, ip->i_di.di_eattr);
	if (!rgd) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0,
				   &al->al_rgd_gh);
	if (error)
		return error;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_DINODE + RES_STATFS +
				 RES_QUOTA, 1);
	if (error)
		goto out_gunlock;

	gfs2_free_meta(ip, ip->i_di.di_eattr, 1);

	ip->i_di.di_eattr = 0;
	if (!ip->i_di.di_blocks)
		gfs2_consist_inode(ip);
	ip->i_di.di_blocks--;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(sdp);

out_gunlock:
	gfs2_glock_dq_uninit(&al->al_rgd_gh);
	return error;
}

/**
 * gfs2_ea_dealloc - deallocate the extended attribute fork
 * @ip: the inode
 *
 * Returns: errno
 */

int gfs2_ea_dealloc(struct gfs2_inode *ip)
{
	struct gfs2_alloc *al;
	int error;

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out_alloc;

	error = gfs2_rindex_hold(GFS2_SB(&ip->i_inode), &al->al_ri_gh);
	if (error)
		goto out_quota;

	error = ea_foreach(ip, ea_dealloc_unstuffed, NULL);
	if (error)
		goto out_rindex;

	if (ip->i_di.di_flags & GFS2_DIF_EA_INDIRECT) {
		error = ea_dealloc_indirect(ip);
		if (error)
			goto out_rindex;
	}

	error = ea_dealloc_block(ip);

out_rindex:
	gfs2_glock_dq_uninit(&al->al_ri_gh);
out_quota:
	gfs2_quota_unhold(ip);
out_alloc:
	gfs2_alloc_put(ip);
	return error;
}

