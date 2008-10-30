/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_btree_trace.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_quota.h"

/*
 * Prototypes for internal btree functions.
 */


STATIC void xfs_bmbt_log_keys(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_bmbt_log_ptrs(xfs_btree_cur_t *, xfs_buf_t *, int, int);

#undef EXIT

#define ENTRY	XBT_ENTRY
#define ERROR	XBT_ERROR
#define EXIT	XBT_EXIT

/*
 * Keep the XFS_BMBT_TRACE_ names around for now until all code using them
 * is converted to be generic and thus switches to the XFS_BTREE_TRACE_ names.
 */
#define	XFS_BMBT_TRACE_ARGBI(c,b,i) \
	XFS_BTREE_TRACE_ARGBI(c,b,i)
#define	XFS_BMBT_TRACE_ARGBII(c,b,i,j) \
	XFS_BTREE_TRACE_ARGBII(c,b,i,j)
#define	XFS_BMBT_TRACE_ARGFFFI(c,o,b,i,j) \
	XFS_BTREE_TRACE_ARGFFFI(c,o,b,i,j)
#define	XFS_BMBT_TRACE_ARGI(c,i) \
	XFS_BTREE_TRACE_ARGI(c,i)
#define	XFS_BMBT_TRACE_ARGIFK(c,i,f,s) \
	XFS_BTREE_TRACE_ARGIPK(c,i,(union xfs_btree_ptr)f,s)
#define	XFS_BMBT_TRACE_ARGIFR(c,i,f,r) \
	XFS_BTREE_TRACE_ARGIPR(c,i, \
		(union xfs_btree_ptr)f, (union xfs_btree_rec *)r)
#define	XFS_BMBT_TRACE_ARGIK(c,i,k) \
	XFS_BTREE_TRACE_ARGIK(c,i,(union xfs_btree_key *)k)
#define	XFS_BMBT_TRACE_CURSOR(c,s) \
	XFS_BTREE_TRACE_CURSOR(c,s)


/*
 * Internal functions.
 */

/*
 * Delete record pointed to by cur/level.
 */
STATIC int					/* error */
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	xfs_fsblock_t		bno;		/* fs-relative block number */
	xfs_buf_t		*bp;		/* buffer for block */
	int			error;		/* error return value */
	int			i;		/* loop counter */
	int			j;		/* temp state */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_bmbt_key_t		*kp=NULL;	/* pointer to bmap btree key */
	xfs_fsblock_t		lbno;		/* left sibling block number */
	xfs_buf_t		*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	int			lrecs=0;	/* left record count */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */
	int			ptr;		/* key/record index */
	xfs_fsblock_t		rbno;		/* right sibling block number */
	xfs_buf_t		*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_rec_t		*rp;		/* pointer to bmap btree rec */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_block_t	*rrblock;	/* right-right btree block */
	xfs_buf_t		*rrbp;		/* right-right buffer pointer */
	int			rrecs=0;	/* right record count */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */
	xfs_btree_cur_t		*tcur;		/* temporary btree cursor */
	int			numrecs;	/* temporary numrec count */
	int			numlrecs, numrrecs;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, level);
	ptr = cur->bc_ptrs[level];
	tcur = NULL;
	if (ptr == 0) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	block = xfs_bmbt_get_block(cur, level, &bp);
	numrecs = be16_to_cpu(block->bb_numrecs);
#ifdef DEBUG
	if ((error = xfs_btree_check_lblock(cur, block, level, bp))) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
#endif
	if (ptr > numrecs) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	XFS_STATS_INC(xs_bmbt_delrec);
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < numrecs; i++) {
			if ((error = xfs_btree_check_lptr_disk(cur, pp[i], level))) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
		}
#endif
		if (ptr < numrecs) {
			memmove(&kp[ptr - 1], &kp[ptr],
				(numrecs - ptr) * sizeof(*kp));
			memmove(&pp[ptr - 1], &pp[ptr],
				(numrecs - ptr) * sizeof(*pp));
			xfs_bmbt_log_ptrs(cur, bp, ptr, numrecs - 1);
			xfs_bmbt_log_keys(cur, bp, ptr, numrecs - 1);
		}
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		if (ptr < numrecs) {
			memmove(&rp[ptr - 1], &rp[ptr],
				(numrecs - ptr) * sizeof(*rp));
			xfs_bmbt_log_recs(cur, bp, ptr, numrecs - 1);
		}
		if (ptr == 1) {
			key.br_startoff =
				cpu_to_be64(xfs_bmbt_disk_get_startoff(rp));
			kp = &key;
		}
	}
	numrecs--;
	block->bb_numrecs = cpu_to_be16(numrecs);
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
	/*
	 * We're at the root level.
	 * First, shrink the root block in-memory.
	 * Try to get rid of the next level down.
	 * If we can't then there's nothing left to do.
	 */
	if (level == cur->bc_nlevels - 1) {
		xfs_iroot_realloc(cur->bc_private.b.ip, -1,
			cur->bc_private.b.whichfork);
		if ((error = xfs_btree_kill_iroot(cur))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &j))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	if (ptr == 1 && (error = xfs_btree_updkey(cur, (union xfs_btree_key *)kp, level + 1))) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
	if (numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &j))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	rbno = be64_to_cpu(block->bb_rightsib);
	lbno = be64_to_cpu(block->bb_leftsib);
	/*
	 * One child of root, need to get a chance to copy its contents
	 * into the root and delete it. Can't go up to next level,
	 * there's nothing to delete there.
	 */
	if (lbno == NULLFSBLOCK && rbno == NULLFSBLOCK &&
	    level == cur->bc_nlevels - 2) {
		if ((error = xfs_btree_kill_iroot(cur))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	ASSERT(rbno != NULLFSBLOCK || lbno != NULLFSBLOCK);
	if ((error = xfs_btree_dup_cursor(cur, &tcur))) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
	bno = NULLFSBLOCK;
	if (rbno != NULLFSBLOCK) {
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_increment(tcur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
#ifdef DEBUG
		if ((error = xfs_btree_check_lblock(cur, right, level, rbp))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
#endif
		bno = be64_to_cpu(right->bb_leftsib);
		if (be16_to_cpu(right->bb_numrecs) - 1 >=
		    XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if ((error = xfs_btree_lshift(tcur, level, &i))) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				tcur = NULL;
				if (level > 0) {
					if ((error = xfs_btree_decrement(cur,
							level, &i))) {
						XFS_BMBT_TRACE_CURSOR(cur,
							ERROR);
						goto error0;
					}
				}
				XFS_BMBT_TRACE_CURSOR(cur, EXIT);
				*stat = 1;
				return 0;
			}
		}
		rrecs = be16_to_cpu(right->bb_numrecs);
		if (lbno != NULLFSBLOCK) {
			i = xfs_btree_firstrec(tcur, level);
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			if ((error = xfs_btree_decrement(tcur, level, &i))) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		}
	}
	if (lbno != NULLFSBLOCK) {
		i = xfs_btree_firstrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * decrement to last in block
		 */
		if ((error = xfs_btree_decrement(tcur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		i = xfs_btree_firstrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
#ifdef DEBUG
		if ((error = xfs_btree_check_lblock(cur, left, level, lbp))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
#endif
		bno = be64_to_cpu(left->bb_rightsib);
		if (be16_to_cpu(left->bb_numrecs) - 1 >=
		    XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if ((error = xfs_btree_rshift(tcur, level, &i))) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				tcur = NULL;
				if (level == 0)
					cur->bc_ptrs[0]++;
				XFS_BMBT_TRACE_CURSOR(cur, EXIT);
				*stat = 1;
				return 0;
			}
		}
		lrecs = be16_to_cpu(left->bb_numrecs);
	}
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	tcur = NULL;
	mp = cur->bc_mp;
	ASSERT(bno != NULLFSBLOCK);
	if (lbno != NULLFSBLOCK &&
	    lrecs + be16_to_cpu(block->bb_numrecs) <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbp = bp;
		if ((error = xfs_btree_read_bufl(mp, cur->bc_tp, lbno, 0, &lbp,
				XFS_BMAP_BTREE_REF))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
		if ((error = xfs_btree_check_lblock(cur, left, level, lbp))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
	} else if (rbno != NULLFSBLOCK &&
		   rrecs + be16_to_cpu(block->bb_numrecs) <=
		   XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbp = bp;
		if ((error = xfs_btree_read_bufl(mp, cur->bc_tp, rbno, 0, &rbp,
				XFS_BMAP_BTREE_REF))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
		if ((error = xfs_btree_check_lblock(cur, right, level, rbp))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		lrecs = be16_to_cpu(left->bb_numrecs);
	} else {
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	numlrecs = be16_to_cpu(left->bb_numrecs);
	numrrecs = be16_to_cpu(right->bb_numrecs);
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, numlrecs + 1, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, numlrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < numrrecs; i++) {
			if ((error = xfs_btree_check_lptr_disk(cur, rpp[i], level))) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
		}
#endif
		memcpy(lkp, rkp, numrrecs * sizeof(*lkp));
		memcpy(lpp, rpp, numrrecs * sizeof(*lpp));
		xfs_bmbt_log_keys(cur, lbp, numlrecs + 1, numlrecs + numrrecs);
		xfs_bmbt_log_ptrs(cur, lbp, numlrecs + 1, numlrecs + numrrecs);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, numlrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		memcpy(lrp, rrp, numrrecs * sizeof(*lrp));
		xfs_bmbt_log_recs(cur, lbp, numlrecs + 1, numlrecs + numrrecs);
	}
	be16_add_cpu(&left->bb_numrecs, numrrecs);
	left->bb_rightsib = right->bb_rightsib;
	xfs_bmbt_log_block(cur, lbp, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
	if (be64_to_cpu(left->bb_rightsib) != NULLDFSBNO) {
		if ((error = xfs_btree_read_bufl(mp, cur->bc_tp,
				be64_to_cpu(left->bb_rightsib),
				0, &rrbp, XFS_BMAP_BTREE_REF))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		if ((error = xfs_btree_check_lblock(cur, rrblock, level, rrbp))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		rrblock->bb_leftsib = cpu_to_be64(lbno);
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(mp, XFS_BUF_ADDR(rbp)), 1,
		cur->bc_private.b.flist, mp);
	cur->bc_private.b.ip->i_d.di_nblocks--;
	xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	XFS_TRANS_MOD_DQUOT_BYINO(mp, cur->bc_tp, cur->bc_private.b.ip,
			XFS_TRANS_DQ_BCOUNT, -1L);
	xfs_trans_binval(cur->bc_tp, rbp);
	if (bp != lbp) {
		cur->bc_bufs[level] = lbp;
		cur->bc_ptrs[level] += lrecs;
		cur->bc_ra[level] = 0;
	} else if ((error = xfs_btree_increment(cur, level + 1, &i))) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
	if (level > 0)
		cur->bc_ptrs[level]--;
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 2;
	return 0;

error0:
	if (tcur)
		xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t	*cur,
	xfs_buf_t	*bp,
	int		kfirst,
	int		klast)
{
	xfs_trans_t	*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBII(cur, bp, kfirst, klast);
	tp = cur->bc_tp;
	if (bp) {
		xfs_bmbt_block_t	*block;
		int			first;
		xfs_bmbt_key_t		*kp;
		int			last;

		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		kp = XFS_BMAP_KEY_DADDR(block, 1, cur);
		first = (int)((xfs_caddr_t)&kp[kfirst - 1] - (xfs_caddr_t)block);
		last = (int)(((xfs_caddr_t)&kp[klast] - 1) - (xfs_caddr_t)block);
		xfs_trans_log_buf(tp, bp, first, last);
	} else {
		xfs_inode_t		 *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Log pointer values from the btree block.
 */
STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t	*cur,
	xfs_buf_t	*bp,
	int		pfirst,
	int		plast)
{
	xfs_trans_t	*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBII(cur, bp, pfirst, plast);
	tp = cur->bc_tp;
	if (bp) {
		xfs_bmbt_block_t	*block;
		int			first;
		int			last;
		xfs_bmbt_ptr_t		*pp;

		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		pp = XFS_BMAP_PTR_DADDR(block, 1, cur);
		first = (int)((xfs_caddr_t)&pp[pfirst - 1] - (xfs_caddr_t)block);
		last = (int)(((xfs_caddr_t)&pp[plast] - 1) - (xfs_caddr_t)block);
		xfs_trans_log_buf(tp, bp, first, last);
	} else {
		xfs_inode_t		*ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Determine the extent state.
 */
/* ARGSUSED */
STATIC xfs_exntst_t
xfs_extent_state(
	xfs_filblks_t		blks,
	int			extent_flag)
{
	if (extent_flag) {
		ASSERT(blks != 0);	/* saved for DMIG */
		return XFS_EXT_UNWRITTEN;
	}
	return XFS_EXT_NORM;
}

/*
 * Convert on-disk form of btree root to in-memory form.
 */
void
xfs_bmdr_to_bmbt(
	xfs_bmdr_block_t	*dblock,
	int			dblocklen,
	xfs_bmbt_block_t	*rblock,
	int			rblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	__be64			*fpp;
	xfs_bmbt_key_t		*tkp;
	__be64			*tpp;

	rblock->bb_magic = cpu_to_be32(XFS_BMAP_MAGIC);
	rblock->bb_level = dblock->bb_level;
	ASSERT(be16_to_cpu(rblock->bb_level) > 0);
	rblock->bb_numrecs = dblock->bb_numrecs;
	rblock->bb_leftsib = cpu_to_be64(NULLDFSBNO);
	rblock->bb_rightsib = cpu_to_be64(NULLDFSBNO);
	dmxr = (int)XFS_BTREE_BLOCK_MAXRECS(dblocklen, xfs_bmdr, 0);
	fkp = XFS_BTREE_KEY_ADDR(xfs_bmdr, dblock, 1);
	tkp = XFS_BMAP_BROOT_KEY_ADDR(rblock, 1, rblocklen);
	fpp = XFS_BTREE_PTR_ADDR(xfs_bmdr, dblock, 1, dmxr);
	tpp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, rblocklen);
	dmxr = be16_to_cpu(dblock->bb_numrecs);
	memcpy(tkp, fkp, sizeof(*fkp) * dmxr);
	memcpy(tpp, fpp, sizeof(*fpp) * dmxr);
}

/*
 * Delete the record pointed to by cur.
 */
int					/* error */
xfs_bmbt_delete(
	xfs_btree_cur_t	*cur,
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
	int		i;
	int		level;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	for (level = 0, i = 2; i == 2; level++) {
		if ((error = xfs_bmbt_delrec(cur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
	}
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				if ((error = xfs_btree_decrement(cur, level,
						&i))) {
					XFS_BMBT_TRACE_CURSOR(cur, ERROR);
					return error;
				}
				break;
			}
		}
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = i;
	return 0;
}

/*
 * Convert a compressed bmap extent record to an uncompressed form.
 * This code must be in sync with the routines xfs_bmbt_get_startoff,
 * xfs_bmbt_get_startblock, xfs_bmbt_get_blockcount and xfs_bmbt_get_state.
 */

STATIC_INLINE void
__xfs_bmbt_get_all(
		__uint64_t l0,
		__uint64_t l1,
		xfs_bmbt_irec_t *s)
{
	int	ext_flag;
	xfs_exntst_t st;

	ext_flag = (int)(l0 >> (64 - BMBT_EXNTFLAG_BITLEN));
	s->br_startoff = ((xfs_fileoff_t)l0 &
			   XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
#if XFS_BIG_BLKNOS
	s->br_startblock = (((xfs_fsblock_t)l0 & XFS_MASK64LO(9)) << 43) |
			   (((xfs_fsblock_t)l1) >> 21);
#else
#ifdef DEBUG
	{
		xfs_dfsbno_t	b;

		b = (((xfs_dfsbno_t)l0 & XFS_MASK64LO(9)) << 43) |
		    (((xfs_dfsbno_t)l1) >> 21);
		ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
		s->br_startblock = (xfs_fsblock_t)b;
	}
#else	/* !DEBUG */
	s->br_startblock = (xfs_fsblock_t)(((xfs_dfsbno_t)l1) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_BLKNOS */
	s->br_blockcount = (xfs_filblks_t)(l1 & XFS_MASK64LO(21));
	/* This is xfs_extent_state() in-line */
	if (ext_flag) {
		ASSERT(s->br_blockcount != 0);	/* saved for DMIG */
		st = XFS_EXT_UNWRITTEN;
	} else
		st = XFS_EXT_NORM;
	s->br_state = st;
}

void
xfs_bmbt_get_all(
	xfs_bmbt_rec_host_t *r,
	xfs_bmbt_irec_t *s)
{
	__xfs_bmbt_get_all(r->l0, r->l1, s);
}

/*
 * Get the block pointer for the given level of the cursor.
 * Fill in the buffer pointer, if applicable.
 */
xfs_bmbt_block_t *
xfs_bmbt_get_block(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_buf_t		**bpp)
{
	xfs_ifork_t		*ifp;
	xfs_bmbt_block_t	*rval;

	if (level < cur->bc_nlevels - 1) {
		*bpp = cur->bc_bufs[level];
		rval = XFS_BUF_TO_BMBT_BLOCK(*bpp);
	} else {
		*bpp = NULL;
		ifp = XFS_IFORK_PTR(cur->bc_private.b.ip,
			cur->bc_private.b.whichfork);
		rval = ifp->if_broot;
	}
	return rval;
}

/*
 * Extract the blockcount field from an in memory bmap extent record.
 */
xfs_filblks_t
xfs_bmbt_get_blockcount(
	xfs_bmbt_rec_host_t	*r)
{
	return (xfs_filblks_t)(r->l1 & XFS_MASK64LO(21));
}

/*
 * Extract the startblock field from an in memory bmap extent record.
 */
xfs_fsblock_t
xfs_bmbt_get_startblock(
	xfs_bmbt_rec_host_t	*r)
{
#if XFS_BIG_BLKNOS
	return (((xfs_fsblock_t)r->l0 & XFS_MASK64LO(9)) << 43) |
	       (((xfs_fsblock_t)r->l1) >> 21);
#else
#ifdef DEBUG
	xfs_dfsbno_t	b;

	b = (((xfs_dfsbno_t)r->l0 & XFS_MASK64LO(9)) << 43) |
	    (((xfs_dfsbno_t)r->l1) >> 21);
	ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
	return (xfs_fsblock_t)b;
#else	/* !DEBUG */
	return (xfs_fsblock_t)(((xfs_dfsbno_t)r->l1) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_BLKNOS */
}

/*
 * Extract the startoff field from an in memory bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_get_startoff(
	xfs_bmbt_rec_host_t	*r)
{
	return ((xfs_fileoff_t)r->l0 &
		 XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
}

xfs_exntst_t
xfs_bmbt_get_state(
	xfs_bmbt_rec_host_t	*r)
{
	int	ext_flag;

	ext_flag = (int)((r->l0) >> (64 - BMBT_EXNTFLAG_BITLEN));
	return xfs_extent_state(xfs_bmbt_get_blockcount(r),
				ext_flag);
}

/* Endian flipping versions of the bmbt extraction functions */
void
xfs_bmbt_disk_get_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t *s)
{
	__xfs_bmbt_get_all(be64_to_cpu(r->l0), be64_to_cpu(r->l1), s);
}

/*
 * Extract the blockcount field from an on disk bmap extent record.
 */
xfs_filblks_t
xfs_bmbt_disk_get_blockcount(
	xfs_bmbt_rec_t	*r)
{
	return (xfs_filblks_t)(be64_to_cpu(r->l1) & XFS_MASK64LO(21));
}

/*
 * Extract the startoff field from a disk format bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_disk_get_startoff(
	xfs_bmbt_rec_t	*r)
{
	return ((xfs_fileoff_t)be64_to_cpu(r->l0) &
		 XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
}

/*
 * Log fields from the btree block header.
 */
void
xfs_bmbt_log_block(
	xfs_btree_cur_t		*cur,
	xfs_buf_t		*bp,
	int			fields)
{
	int			first;
	int			last;
	xfs_trans_t		*tp;
	static const short	offsets[] = {
		offsetof(xfs_bmbt_block_t, bb_magic),
		offsetof(xfs_bmbt_block_t, bb_level),
		offsetof(xfs_bmbt_block_t, bb_numrecs),
		offsetof(xfs_bmbt_block_t, bb_leftsib),
		offsetof(xfs_bmbt_block_t, bb_rightsib),
		sizeof(xfs_bmbt_block_t)
	};

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBI(cur, bp, fields);
	tp = cur->bc_tp;
	if (bp) {
		xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first,
				  &last);
		xfs_trans_log_buf(tp, bp, first, last);
	} else
		xfs_trans_log_inode(tp, cur->bc_private.b.ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Log record values from the btree block.
 */
void
xfs_bmbt_log_recs(
	xfs_btree_cur_t		*cur,
	xfs_buf_t		*bp,
	int			rfirst,
	int			rlast)
{
	xfs_bmbt_block_t	*block;
	int			first;
	int			last;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBII(cur, bp, rfirst, rlast);
	ASSERT(bp);
	tp = cur->bc_tp;
	block = XFS_BUF_TO_BMBT_BLOCK(bp);
	rp = XFS_BMAP_REC_DADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&rp[rfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&rp[rlast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(tp, bp, first, last);
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Set all the fields in a bmap extent record from the arguments.
 */
void
xfs_bmbt_set_allf(
	xfs_bmbt_rec_host_t	*r,
	xfs_fileoff_t		startoff,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount,
	xfs_exntst_t		state)
{
	int		extent_flag = (state == XFS_EXT_NORM) ? 0 : 1;

	ASSERT(state == XFS_EXT_NORM || state == XFS_EXT_UNWRITTEN);
	ASSERT((startoff & XFS_MASK64HI(64-BMBT_STARTOFF_BITLEN)) == 0);
	ASSERT((blockcount & XFS_MASK64HI(64-BMBT_BLOCKCOUNT_BITLEN)) == 0);

#if XFS_BIG_BLKNOS
	ASSERT((startblock & XFS_MASK64HI(64-BMBT_STARTBLOCK_BITLEN)) == 0);

	r->l0 = ((xfs_bmbt_rec_base_t)extent_flag << 63) |
		((xfs_bmbt_rec_base_t)startoff << 9) |
		((xfs_bmbt_rec_base_t)startblock >> 43);
	r->l1 = ((xfs_bmbt_rec_base_t)startblock << 21) |
		((xfs_bmbt_rec_base_t)blockcount &
		(xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
#else	/* !XFS_BIG_BLKNOS */
	if (ISNULLSTARTBLOCK(startblock)) {
		r->l0 = ((xfs_bmbt_rec_base_t)extent_flag << 63) |
			((xfs_bmbt_rec_base_t)startoff << 9) |
			 (xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)startblock << 21) |
			  ((xfs_bmbt_rec_base_t)blockcount &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	} else {
		r->l0 = ((xfs_bmbt_rec_base_t)extent_flag << 63) |
			((xfs_bmbt_rec_base_t)startoff << 9);
		r->l1 = ((xfs_bmbt_rec_base_t)startblock << 21) |
			 ((xfs_bmbt_rec_base_t)blockcount &
			 (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	}
#endif	/* XFS_BIG_BLKNOS */
}

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
void
xfs_bmbt_set_all(
	xfs_bmbt_rec_host_t *r,
	xfs_bmbt_irec_t	*s)
{
	xfs_bmbt_set_allf(r, s->br_startoff, s->br_startblock,
			     s->br_blockcount, s->br_state);
}


/*
 * Set all the fields in a disk format bmap extent record from the arguments.
 */
void
xfs_bmbt_disk_set_allf(
	xfs_bmbt_rec_t		*r,
	xfs_fileoff_t		startoff,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount,
	xfs_exntst_t		state)
{
	int			extent_flag = (state == XFS_EXT_NORM) ? 0 : 1;

	ASSERT(state == XFS_EXT_NORM || state == XFS_EXT_UNWRITTEN);
	ASSERT((startoff & XFS_MASK64HI(64-BMBT_STARTOFF_BITLEN)) == 0);
	ASSERT((blockcount & XFS_MASK64HI(64-BMBT_BLOCKCOUNT_BITLEN)) == 0);

#if XFS_BIG_BLKNOS
	ASSERT((startblock & XFS_MASK64HI(64-BMBT_STARTBLOCK_BITLEN)) == 0);

	r->l0 = cpu_to_be64(
		((xfs_bmbt_rec_base_t)extent_flag << 63) |
		 ((xfs_bmbt_rec_base_t)startoff << 9) |
		 ((xfs_bmbt_rec_base_t)startblock >> 43));
	r->l1 = cpu_to_be64(
		((xfs_bmbt_rec_base_t)startblock << 21) |
		 ((xfs_bmbt_rec_base_t)blockcount &
		  (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
#else	/* !XFS_BIG_BLKNOS */
	if (ISNULLSTARTBLOCK(startblock)) {
		r->l0 = cpu_to_be64(
			((xfs_bmbt_rec_base_t)extent_flag << 63) |
			 ((xfs_bmbt_rec_base_t)startoff << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK64LO(9));
		r->l1 = cpu_to_be64(XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)startblock << 21) |
			  ((xfs_bmbt_rec_base_t)blockcount &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	} else {
		r->l0 = cpu_to_be64(
			((xfs_bmbt_rec_base_t)extent_flag << 63) |
			 ((xfs_bmbt_rec_base_t)startoff << 9));
		r->l1 = cpu_to_be64(
			((xfs_bmbt_rec_base_t)startblock << 21) |
			 ((xfs_bmbt_rec_base_t)blockcount &
			  (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	}
#endif	/* XFS_BIG_BLKNOS */
}

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
void
xfs_bmbt_disk_set_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t *s)
{
	xfs_bmbt_disk_set_allf(r, s->br_startoff, s->br_startblock,
				  s->br_blockcount, s->br_state);
}

/*
 * Set the blockcount field in a bmap extent record.
 */
void
xfs_bmbt_set_blockcount(
	xfs_bmbt_rec_host_t *r,
	xfs_filblks_t	v)
{
	ASSERT((v & XFS_MASK64HI(43)) == 0);
	r->l1 = (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64HI(43)) |
		  (xfs_bmbt_rec_base_t)(v & XFS_MASK64LO(21));
}

/*
 * Set the startblock field in a bmap extent record.
 */
void
xfs_bmbt_set_startblock(
	xfs_bmbt_rec_host_t *r,
	xfs_fsblock_t	v)
{
#if XFS_BIG_BLKNOS
	ASSERT((v & XFS_MASK64HI(12)) == 0);
	r->l0 = (r->l0 & (xfs_bmbt_rec_base_t)XFS_MASK64HI(55)) |
		  (xfs_bmbt_rec_base_t)(v >> 43);
	r->l1 = (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)) |
		  (xfs_bmbt_rec_base_t)(v << 21);
#else	/* !XFS_BIG_BLKNOS */
	if (ISNULLSTARTBLOCK(v)) {
		r->l0 |= (xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = (xfs_bmbt_rec_base_t)XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)v << 21) |
			  (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	} else {
		r->l0 &= ~(xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = ((xfs_bmbt_rec_base_t)v << 21) |
			  (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	}
#endif	/* XFS_BIG_BLKNOS */
}

/*
 * Set the startoff field in a bmap extent record.
 */
void
xfs_bmbt_set_startoff(
	xfs_bmbt_rec_host_t *r,
	xfs_fileoff_t	v)
{
	ASSERT((v & XFS_MASK64HI(9)) == 0);
	r->l0 = (r->l0 & (xfs_bmbt_rec_base_t) XFS_MASK64HI(1)) |
		((xfs_bmbt_rec_base_t)v << 9) |
		  (r->l0 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(9));
}

/*
 * Set the extent state field in a bmap extent record.
 */
void
xfs_bmbt_set_state(
	xfs_bmbt_rec_host_t *r,
	xfs_exntst_t	v)
{
	ASSERT(v == XFS_EXT_NORM || v == XFS_EXT_UNWRITTEN);
	if (v == XFS_EXT_NORM)
		r->l0 &= XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN);
	else
		r->l0 |= XFS_MASK64HI(BMBT_EXNTFLAG_BITLEN);
}

/*
 * Convert in-memory form of btree root to on-disk form.
 */
void
xfs_bmbt_to_bmdr(
	xfs_bmbt_block_t	*rblock,
	int			rblocklen,
	xfs_bmdr_block_t	*dblock,
	int			dblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	__be64			*fpp;
	xfs_bmbt_key_t		*tkp;
	__be64			*tpp;

	ASSERT(be32_to_cpu(rblock->bb_magic) == XFS_BMAP_MAGIC);
	ASSERT(be64_to_cpu(rblock->bb_leftsib) == NULLDFSBNO);
	ASSERT(be64_to_cpu(rblock->bb_rightsib) == NULLDFSBNO);
	ASSERT(be16_to_cpu(rblock->bb_level) > 0);
	dblock->bb_level = rblock->bb_level;
	dblock->bb_numrecs = rblock->bb_numrecs;
	dmxr = (int)XFS_BTREE_BLOCK_MAXRECS(dblocklen, xfs_bmdr, 0);
	fkp = XFS_BMAP_BROOT_KEY_ADDR(rblock, 1, rblocklen);
	tkp = XFS_BTREE_KEY_ADDR(xfs_bmdr, dblock, 1);
	fpp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, rblocklen);
	tpp = XFS_BTREE_PTR_ADDR(xfs_bmdr, dblock, 1, dmxr);
	dmxr = be16_to_cpu(dblock->bb_numrecs);
	memcpy(tkp, fkp, sizeof(*fkp) * dmxr);
	memcpy(tpp, fpp, sizeof(*fpp) * dmxr);
}

/*
 * Check extent records, which have just been read, for
 * any bit in the extent flag field. ASSERT on debug
 * kernels, as this condition should not occur.
 * Return an error condition (1) if any flags found,
 * otherwise return 0.
 */

int
xfs_check_nostate_extents(
	xfs_ifork_t		*ifp,
	xfs_extnum_t		idx,
	xfs_extnum_t		num)
{
	for (; num > 0; num--, idx++) {
		xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, idx);
		if ((ep->l0 >>
		     (64 - BMBT_EXNTFLAG_BITLEN)) != 0) {
			ASSERT(0);
			return 1;
		}
	}
	return 0;
}


STATIC struct xfs_btree_cur *
xfs_bmbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	struct xfs_btree_cur	*new;

	new = xfs_bmbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.b.ip, cur->bc_private.b.whichfork);

	/*
	 * Copy the firstblock, flist, and flags values,
	 * since init cursor doesn't get them.
	 */
	new->bc_private.b.firstblock = cur->bc_private.b.firstblock;
	new->bc_private.b.flist = cur->bc_private.b.flist;
	new->bc_private.b.flags = cur->bc_private.b.flags;

	return new;
}

STATIC void
xfs_bmbt_update_cursor(
	struct xfs_btree_cur	*src,
	struct xfs_btree_cur	*dst)
{
	ASSERT((dst->bc_private.b.firstblock != NULLFSBLOCK) ||
	       (dst->bc_private.b.ip->i_d.di_flags & XFS_DIFLAG_REALTIME));
	ASSERT(dst->bc_private.b.flist == src->bc_private.b.flist);

	dst->bc_private.b.allocated += src->bc_private.b.allocated;
	dst->bc_private.b.firstblock = src->bc_private.b.firstblock;

	src->bc_private.b.allocated = 0;
}

STATIC int
xfs_bmbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			length,
	int			*stat)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.fsbno = cur->bc_private.b.firstblock;
	args.firstblock = args.fsbno;

	if (args.fsbno == NULLFSBLOCK) {
		args.fsbno = be64_to_cpu(start->l);
		args.type = XFS_ALLOCTYPE_START_BNO;
		/*
		 * Make sure there is sufficient room left in the AG to
		 * complete a full tree split for an extent insert.  If
		 * we are converting the middle part of an extent then
		 * we may need space for two tree splits.
		 *
		 * We are relying on the caller to make the correct block
		 * reservation for this operation to succeed.  If the
		 * reservation amount is insufficient then we may fail a
		 * block allocation here and corrupt the filesystem.
		 */
		args.minleft = xfs_trans_get_block_res(args.tp);
	} else if (cur->bc_private.b.flist->xbf_low) {
		args.type = XFS_ALLOCTYPE_START_BNO;
	} else {
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
	}

	args.minlen = args.maxlen = args.prod = 1;
	args.wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	if (!args.wasdel && xfs_trans_get_block_res(args.tp) == 0) {
		error = XFS_ERROR(ENOSPC);
		goto error0;
	}
	error = xfs_alloc_vextent(&args);
	if (error)
		goto error0;

	if (args.fsbno == NULLFSBLOCK && args.minleft) {
		/*
		 * Could not find an AG with enough free space to satisfy
		 * a full btree split.  Try again without minleft and if
		 * successful activate the lowspace algorithm.
		 */
		args.fsbno = 0;
		args.type = XFS_ALLOCTYPE_FIRST_AG;
		args.minleft = 0;
		error = xfs_alloc_vextent(&args);
		if (error)
			goto error0;
		cur->bc_private.b.flist->xbf_low = 1;
	}
	if (args.fsbno == NULLFSBLOCK) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	cur->bc_private.b.firstblock = args.fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	xfs_trans_log_inode(args.tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	XFS_TRANS_MOD_DQUOT_BYINO(args.mp, args.tp, cur->bc_private.b.ip,
			XFS_TRANS_DQ_BCOUNT, 1L);

	new->l = cpu_to_be64(args.fsbno);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
	*stat = 1;
	return 0;

 error0:
	XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
	return error;
}

STATIC int
xfs_bmbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_inode	*ip = cur->bc_private.b.ip;
	struct xfs_trans	*tp = cur->bc_tp;
	xfs_fsblock_t		fsbno = XFS_DADDR_TO_FSB(mp, XFS_BUF_ADDR(bp));

	xfs_bmap_add_free(fsbno, 1, cur->bc_private.b.flist, mp);
	ip->i_d.di_nblocks--;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	XFS_TRANS_MOD_DQUOT_BYINO(mp, tp, ip, XFS_TRANS_DQ_BCOUNT, -1L);
	xfs_trans_binval(tp, bp);
	return 0;
}

STATIC int
xfs_bmbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return XFS_BMAP_BLOCK_IMAXRECS(level, cur);
}

/*
 * Get the maximum records we could store in the on-disk format.
 *
 * For non-root nodes this is equivalent to xfs_bmbt_get_maxrecs, but
 * for the root node this checks the available space in the dinode fork
 * so that we can resize the in-memory buffer to match it.  After a
 * resize to the maximum size this function returns the same value
 * as xfs_bmbt_get_maxrecs for the root node, too.
 */
STATIC int
xfs_bmbt_get_dmaxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return XFS_BMAP_BLOCK_DMAXRECS(level, cur);
}

STATIC void
xfs_bmbt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->bmbt.br_startoff =
		cpu_to_be64(xfs_bmbt_disk_get_startoff(&rec->bmbt));
}

STATIC void
xfs_bmbt_init_rec_from_key(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	ASSERT(key->bmbt.br_startoff != 0);

	xfs_bmbt_disk_set_allf(&rec->bmbt, be64_to_cpu(key->bmbt.br_startoff),
			       0, 0, XFS_EXT_NORM);
}

STATIC void
xfs_bmbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	xfs_bmbt_disk_set_all(&rec->bmbt, &cur->bc_rec.b);
}

STATIC void
xfs_bmbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	ptr->l = 0;
}

STATIC __int64_t
xfs_bmbt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	return (__int64_t)be64_to_cpu(key->bmbt.br_startoff) -
				      cur->bc_rec.b.br_startoff;
}

#ifdef XFS_BTREE_TRACE
ktrace_t	*xfs_bmbt_trace_buf;

STATIC void
xfs_bmbt_trace_enter(
	struct xfs_btree_cur	*cur,
	const char		*func,
	char			*s,
	int			type,
	int			line,
	__psunsigned_t		a0,
	__psunsigned_t		a1,
	__psunsigned_t		a2,
	__psunsigned_t		a3,
	__psunsigned_t		a4,
	__psunsigned_t		a5,
	__psunsigned_t		a6,
	__psunsigned_t		a7,
	__psunsigned_t		a8,
	__psunsigned_t		a9,
	__psunsigned_t		a10)
{
	struct xfs_inode	*ip = cur->bc_private.b.ip;
	int			whichfork = cur->bc_private.b.whichfork;

	ktrace_enter(xfs_bmbt_trace_buf,
		(void *)((__psint_t)type | (whichfork << 8) | (line << 16)),
		(void *)func, (void *)s, (void *)ip, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10);
	ktrace_enter(ip->i_btrace,
		(void *)((__psint_t)type | (whichfork << 8) | (line << 16)),
		(void *)func, (void *)s, (void *)ip, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10);
}

STATIC void
xfs_bmbt_trace_cursor(
	struct xfs_btree_cur	*cur,
	__uint32_t		*s0,
	__uint64_t		*l0,
	__uint64_t		*l1)
{
	struct xfs_bmbt_rec_host r;

	xfs_bmbt_set_all(&r, &cur->bc_rec.b);

	*s0 = (cur->bc_nlevels << 24) |
	      (cur->bc_private.b.flags << 16) |
	       cur->bc_private.b.allocated;
	*l0 = r.l0;
	*l1 = r.l1;
}

STATIC void
xfs_bmbt_trace_key(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key,
	__uint64_t		*l0,
	__uint64_t		*l1)
{
	*l0 = be64_to_cpu(key->bmbt.br_startoff);
	*l1 = 0;
}

STATIC void
xfs_bmbt_trace_record(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec,
	__uint64_t		*l0,
	__uint64_t		*l1,
	__uint64_t		*l2)
{
	struct xfs_bmbt_irec	irec;

	xfs_bmbt_disk_get_all(&rec->bmbt, &irec);
	*l0 = irec.br_startoff;
	*l1 = irec.br_startblock;
	*l2 = irec.br_blockcount;
}
#endif /* XFS_BTREE_TRACE */

static const struct xfs_btree_ops xfs_bmbt_ops = {
	.rec_len		= sizeof(xfs_bmbt_rec_t),
	.key_len		= sizeof(xfs_bmbt_key_t),

	.dup_cursor		= xfs_bmbt_dup_cursor,
	.update_cursor		= xfs_bmbt_update_cursor,
	.alloc_block		= xfs_bmbt_alloc_block,
	.free_block		= xfs_bmbt_free_block,
	.get_maxrecs		= xfs_bmbt_get_maxrecs,
	.get_dmaxrecs		= xfs_bmbt_get_dmaxrecs,
	.init_key_from_rec	= xfs_bmbt_init_key_from_rec,
	.init_rec_from_key	= xfs_bmbt_init_rec_from_key,
	.init_rec_from_cur	= xfs_bmbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_bmbt_init_ptr_from_cur,
	.key_diff		= xfs_bmbt_key_diff,

#ifdef XFS_BTREE_TRACE
	.trace_enter		= xfs_bmbt_trace_enter,
	.trace_cursor		= xfs_bmbt_trace_cursor,
	.trace_key		= xfs_bmbt_trace_key,
	.trace_record		= xfs_bmbt_trace_record,
#endif
};

/*
 * Allocate a new bmap btree cursor.
 */
struct xfs_btree_cur *				/* new bmap btree cursor */
xfs_bmbt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* inode owning the btree */
	int			whichfork)	/* data or attr fork */
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	struct xfs_btree_cur	*cur;

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_SLEEP);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_nlevels = be16_to_cpu(ifp->if_broot->bb_level) + 1;
	cur->bc_btnum = XFS_BTNUM_BMAP;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	cur->bc_ops = &xfs_bmbt_ops;
	cur->bc_flags = XFS_BTREE_LONG_PTRS | XFS_BTREE_ROOT_IN_INODE;

	cur->bc_private.b.forksize = XFS_IFORK_SIZE(ip, whichfork);
	cur->bc_private.b.ip = ip;
	cur->bc_private.b.firstblock = NULLFSBLOCK;
	cur->bc_private.b.flist = NULL;
	cur->bc_private.b.allocated = 0;
	cur->bc_private.b.flags = 0;
	cur->bc_private.b.whichfork = whichfork;

	return cur;
}
