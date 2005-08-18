/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#define	XFS_MACRO_C

#include "xfs.h"
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_ialloc.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_log_priv.h"
#include "xfs_da_btree.h"
#include "xfs_attr_leaf.h"
#include "xfs_dir_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_bit.h"

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_ISNULLDSTARTBLOCK)
int
isnulldstartblock(xfs_dfsbno_t x)
{
	return ISNULLDSTARTBLOCK(x);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_ISNULLSTARTBLOCK)
int
isnullstartblock(xfs_fsblock_t x)
{
	return ISNULLSTARTBLOCK(x);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_NULLSTARTBLOCK)
xfs_fsblock_t
nullstartblock(int k)
{
	return NULLSTARTBLOCK(k);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_STARTBLOCKVAL)
xfs_filblks_t
startblockval(xfs_fsblock_t x)
{
	return STARTBLOCKVAL(x);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AG_CHECK_DADDR)
void
xfs_ag_check_daddr(xfs_mount_t *mp, xfs_daddr_t d, xfs_extlen_t len)
{
	XFS_AG_CHECK_DADDR(mp, d, len);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AG_DADDR)
xfs_daddr_t
xfs_ag_daddr(xfs_mount_t *mp, xfs_agnumber_t agno, xfs_daddr_t d)
{
	return XFS_AG_DADDR(mp, agno, d);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AG_MAXLEVELS)
int
xfs_ag_maxlevels(xfs_mount_t *mp)
{
	return XFS_AG_MAXLEVELS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGB_TO_DADDR)
xfs_daddr_t
xfs_agb_to_daddr(xfs_mount_t *mp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	return XFS_AGB_TO_DADDR(mp, agno, agbno);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGB_TO_FSB)
xfs_fsblock_t
xfs_agb_to_fsb(xfs_mount_t *mp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	return XFS_AGB_TO_FSB(mp, agno, agbno);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGBLOCK_MAX)
xfs_agblock_t
xfs_agblock_max(xfs_agblock_t a, xfs_agblock_t b)
{
	return XFS_AGBLOCK_MAX(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGBLOCK_MIN)
xfs_agblock_t
xfs_agblock_min(xfs_agblock_t a, xfs_agblock_t b)
{
	return XFS_AGBLOCK_MIN(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGF_BLOCK)
xfs_agblock_t
xfs_agf_block(xfs_mount_t *mp)
{
	return XFS_AGF_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGF_GOOD_VERSION)
int
xfs_agf_good_version(unsigned v)
{
	return XFS_AGF_GOOD_VERSION(v);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGFL_BLOCK)
xfs_agblock_t
xfs_agfl_block(xfs_mount_t *mp)
{
	return XFS_AGFL_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGI_BLOCK)
xfs_agblock_t
xfs_agi_block(xfs_mount_t *mp)
{
	return XFS_AGI_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGI_GOOD_VERSION)
int
xfs_agi_good_version(unsigned v)
{
	return XFS_AGI_GOOD_VERSION(v);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGINO_TO_AGBNO)
xfs_agblock_t
xfs_agino_to_agbno(xfs_mount_t *mp, xfs_agino_t i)
{
	return XFS_AGINO_TO_AGBNO(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGINO_TO_INO)
xfs_ino_t
xfs_agino_to_ino(xfs_mount_t *mp, xfs_agnumber_t a, xfs_agino_t i)
{
	return XFS_AGINO_TO_INO(mp, a, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_AGINO_TO_OFFSET)
int
xfs_agino_to_offset(xfs_mount_t *mp, xfs_agino_t i)
{
	return XFS_AGINO_TO_OFFSET(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ALLOC_BLOCK_MAXRECS)
int
xfs_alloc_block_maxrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_ALLOC_BLOCK_MAXRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ALLOC_BLOCK_MINRECS)
int
xfs_alloc_block_minrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_ALLOC_BLOCK_MINRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ALLOC_BLOCK_SIZE)
/*ARGSUSED1*/
int
xfs_alloc_block_size(int lev, xfs_btree_cur_t *cur)
{
	return XFS_ALLOC_BLOCK_SIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ALLOC_KEY_ADDR)
/*ARGSUSED3*/
xfs_alloc_key_t *
xfs_alloc_key_addr(xfs_alloc_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_ALLOC_KEY_ADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ALLOC_PTR_ADDR)
xfs_alloc_ptr_t *
xfs_alloc_ptr_addr(xfs_alloc_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_ALLOC_PTR_ADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ALLOC_REC_ADDR)
/*ARGSUSED3*/
xfs_alloc_rec_t *
xfs_alloc_rec_addr(xfs_alloc_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_ALLOC_REC_ADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_LEAF_ENTSIZE_LOCAL)
int
xfs_attr_leaf_entsize_local(int nlen, int vlen)
{
	return XFS_ATTR_LEAF_ENTSIZE_LOCAL(nlen, vlen);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_LEAF_ENTSIZE_LOCAL_MAX)
int
xfs_attr_leaf_entsize_local_max(int bsize)
{
	return XFS_ATTR_LEAF_ENTSIZE_LOCAL_MAX(bsize);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_LEAF_ENTSIZE_REMOTE)
int
xfs_attr_leaf_entsize_remote(int nlen)
{
	return XFS_ATTR_LEAF_ENTSIZE_REMOTE(nlen);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_LEAF_NAME)
char *
xfs_attr_leaf_name(xfs_attr_leafblock_t *leafp, int idx)
{
	return XFS_ATTR_LEAF_NAME(leafp, idx);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_LEAF_NAME_LOCAL)
xfs_attr_leaf_name_local_t *
xfs_attr_leaf_name_local(xfs_attr_leafblock_t *leafp, int idx)
{
	return XFS_ATTR_LEAF_NAME_LOCAL(leafp, idx);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_LEAF_NAME_REMOTE)
xfs_attr_leaf_name_remote_t *
xfs_attr_leaf_name_remote(xfs_attr_leafblock_t *leafp, int idx)
{
	return XFS_ATTR_LEAF_NAME_REMOTE(leafp, idx);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_SF_ENTSIZE)
int
xfs_attr_sf_entsize(xfs_attr_sf_entry_t *sfep)
{
	return XFS_ATTR_SF_ENTSIZE(sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_SF_ENTSIZE_BYNAME)
int
xfs_attr_sf_entsize_byname(int nlen, int vlen)
{
	return XFS_ATTR_SF_ENTSIZE_BYNAME(nlen, vlen);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_SF_NEXTENTRY)
xfs_attr_sf_entry_t *
xfs_attr_sf_nextentry(xfs_attr_sf_entry_t *sfep)
{
	return XFS_ATTR_SF_NEXTENTRY(sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ATTR_SF_TOTSIZE)
int
xfs_attr_sf_totsize(xfs_inode_t *dp)
{
	return XFS_ATTR_SF_TOTSIZE(dp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BHVTOI)
xfs_inode_t *
xfs_bhvtoi(bhv_desc_t *bhvp)
{
	return XFS_BHVTOI(bhvp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BHVTOM)
xfs_mount_t *
xfs_bhvtom(bhv_desc_t *bdp)
{
	return XFS_BHVTOM(bdp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_VFSTOM)
xfs_mount_t *
xfs_vfstom(vfs_t *vfs)
{
	return XFS_VFSTOM(vfs);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BM_MAXLEVELS)
int
xfs_bm_maxlevels(xfs_mount_t *mp, int w)
{
	return XFS_BM_MAXLEVELS(mp, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BLOCK_DMAXRECS)
int
xfs_bmap_block_dmaxrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_BLOCK_DMAXRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BLOCK_DMINRECS)
int
xfs_bmap_block_dminrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_BLOCK_DMINRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BLOCK_DSIZE)
int
xfs_bmap_block_dsize(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_BLOCK_DSIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BLOCK_IMAXRECS)
int
xfs_bmap_block_imaxrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_BLOCK_IMAXRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BLOCK_IMINRECS)
int
xfs_bmap_block_iminrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_BLOCK_IMINRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BLOCK_ISIZE)
int
xfs_bmap_block_isize(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_BLOCK_ISIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_KEY_ADDR)
/*ARGSUSED3*/
xfs_bmbt_key_t *
xfs_bmap_broot_key_addr(xfs_bmbt_block_t *bb, int i, int sz)
{
	return XFS_BMAP_BROOT_KEY_ADDR(bb, i, sz);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_MAXRECS)
int
xfs_bmap_broot_maxrecs(int sz)
{
	return XFS_BMAP_BROOT_MAXRECS(sz);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_NUMRECS)
int
xfs_bmap_broot_numrecs(xfs_bmdr_block_t *bb)
{
	return XFS_BMAP_BROOT_NUMRECS(bb);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_PTR_ADDR)
xfs_bmbt_ptr_t *
xfs_bmap_broot_ptr_addr(xfs_bmbt_block_t *bb, int i, int sz)
{
	return XFS_BMAP_BROOT_PTR_ADDR(bb, i, sz);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_REC_ADDR)
/*ARGSUSED3*/
xfs_bmbt_rec_t *
xfs_bmap_broot_rec_addr(xfs_bmbt_block_t *bb, int i, int sz)
{
	return XFS_BMAP_BROOT_REC_ADDR(bb, i, sz);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_SPACE)
int
xfs_bmap_broot_space(xfs_bmdr_block_t *bb)
{
	return XFS_BMAP_BROOT_SPACE(bb);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_BROOT_SPACE_CALC)
int
xfs_bmap_broot_space_calc(int nrecs)
{
	return XFS_BMAP_BROOT_SPACE_CALC(nrecs);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_IBLOCK_SIZE)
/*ARGSUSED1*/
int
xfs_bmap_iblock_size(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_IBLOCK_SIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_INIT)
void
xfs_bmap_init(xfs_bmap_free_t *flp, xfs_fsblock_t *fbp)
{
	XFS_BMAP_INIT(flp, fbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_KEY_DADDR)
/*ARGSUSED3*/
xfs_bmbt_key_t *
xfs_bmap_key_daddr(xfs_bmbt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_KEY_DADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_KEY_IADDR)
/*ARGSUSED3*/
xfs_bmbt_key_t *
xfs_bmap_key_iaddr(xfs_bmbt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_KEY_IADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_PTR_DADDR)
xfs_bmbt_ptr_t *
xfs_bmap_ptr_daddr(xfs_bmbt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_PTR_DADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_PTR_IADDR)
xfs_bmbt_ptr_t *
xfs_bmap_ptr_iaddr(xfs_bmbt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_PTR_IADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_RBLOCK_DSIZE)
/*ARGSUSED1*/
int
xfs_bmap_rblock_dsize(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_RBLOCK_DSIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_RBLOCK_ISIZE)
/*ARGSUSED1*/
int
xfs_bmap_rblock_isize(int lev, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_RBLOCK_ISIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_REC_DADDR)
/*ARGSUSED3*/
xfs_bmbt_rec_t *
xfs_bmap_rec_daddr(xfs_bmbt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_REC_DADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_REC_IADDR)
/*ARGSUSED3*/
xfs_bmbt_rec_t *
xfs_bmap_rec_iaddr(xfs_bmbt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_BMAP_REC_IADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAP_SANITY_CHECK)
int
xfs_bmap_sanity_check(xfs_mount_t *mp, xfs_bmbt_block_t *bb, int level)
{
	return XFS_BMAP_SANITY_CHECK(mp, bb, level);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMAPI_AFLAG)
int
xfs_bmapi_aflag(int w)
{
	return XFS_BMAPI_AFLAG(w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BMDR_SPACE_CALC)
int
xfs_bmdr_space_calc(int nrecs)
{
	return XFS_BMDR_SPACE_CALC(nrecs);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BNO_BLOCK)
xfs_agblock_t
xfs_bno_block(xfs_mount_t *mp)
{
	return XFS_BNO_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BTREE_LONG_PTRS)
int
xfs_btree_long_ptrs(xfs_btnum_t btnum)
{
	return XFS_BTREE_LONG_PTRS(btnum);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_AGF)
xfs_agf_t *
xfs_buf_to_agf(xfs_buf_t *bp)
{
	return XFS_BUF_TO_AGF(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_AGFL)
xfs_agfl_t *
xfs_buf_to_agfl(xfs_buf_t *bp)
{
	return XFS_BUF_TO_AGFL(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_AGI)
xfs_agi_t *
xfs_buf_to_agi(xfs_buf_t *bp)
{
	return XFS_BUF_TO_AGI(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_ALLOC_BLOCK)
xfs_alloc_block_t *
xfs_buf_to_alloc_block(xfs_buf_t *bp)
{
	return XFS_BUF_TO_ALLOC_BLOCK(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_BLOCK)
xfs_btree_block_t *
xfs_buf_to_block(xfs_buf_t *bp)
{
	return XFS_BUF_TO_BLOCK(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_BMBT_BLOCK)
xfs_bmbt_block_t *
xfs_buf_to_bmbt_block(xfs_buf_t *bp)
{
	return XFS_BUF_TO_BMBT_BLOCK(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_DINODE)
xfs_dinode_t *
xfs_buf_to_dinode(xfs_buf_t *bp)
{
	return XFS_BUF_TO_DINODE(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_INOBT_BLOCK)
xfs_inobt_block_t *
xfs_buf_to_inobt_block(xfs_buf_t *bp)
{
	return XFS_BUF_TO_INOBT_BLOCK(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_LBLOCK)
xfs_btree_lblock_t *
xfs_buf_to_lblock(xfs_buf_t *bp)
{
	return XFS_BUF_TO_LBLOCK(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_SBLOCK)
xfs_btree_sblock_t *
xfs_buf_to_sblock(xfs_buf_t *bp)
{
	return XFS_BUF_TO_SBLOCK(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_BUF_TO_SBP)
xfs_sb_t *
xfs_buf_to_sbp(xfs_buf_t *bp)
{
	return XFS_BUF_TO_SBP(bp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_ASIZE)
int
xfs_cfork_asize_disk(xfs_dinode_core_t *dcp, xfs_mount_t *mp)
{
	return XFS_CFORK_ASIZE_DISK(dcp, mp);
}
int
xfs_cfork_asize(xfs_dinode_core_t *dcp, xfs_mount_t *mp)
{
	return XFS_CFORK_ASIZE(dcp, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_BOFF)
int
xfs_cfork_boff_disk(xfs_dinode_core_t *dcp)
{
	return XFS_CFORK_BOFF_DISK(dcp);
}
int
xfs_cfork_boff(xfs_dinode_core_t *dcp)
{
	return XFS_CFORK_BOFF(dcp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_DSIZE)
int
xfs_cfork_dsize_disk(xfs_dinode_core_t *dcp, xfs_mount_t *mp)
{
	return XFS_CFORK_DSIZE_DISK(dcp, mp);
}
int
xfs_cfork_dsize(xfs_dinode_core_t *dcp, xfs_mount_t *mp)
{
	return XFS_CFORK_DSIZE(dcp, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_FMT_SET)
void
xfs_cfork_fmt_set(xfs_dinode_core_t *dcp, int w, int n)
{
	XFS_CFORK_FMT_SET(dcp, w, n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_FORMAT)
int
xfs_cfork_format(xfs_dinode_core_t *dcp, int w)
{
	return XFS_CFORK_FORMAT(dcp, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_NEXT_SET)
void
xfs_cfork_next_set(xfs_dinode_core_t *dcp, int w, int n)
{
	XFS_CFORK_NEXT_SET(dcp, w, n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_NEXTENTS)
int
xfs_cfork_nextents_disk(xfs_dinode_core_t *dcp, int w)
{
	return XFS_CFORK_NEXTENTS_DISK(dcp, w);
}
int
xfs_cfork_nextents(xfs_dinode_core_t *dcp, int w)
{
	return XFS_CFORK_NEXTENTS(dcp, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_Q)
int
xfs_cfork_q_disk(xfs_dinode_core_t *dcp)
{
	return XFS_CFORK_Q_DISK(dcp);
}
int
xfs_cfork_q(xfs_dinode_core_t *dcp)
{
	return XFS_CFORK_Q(dcp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CFORK_SIZE)
int
xfs_cfork_size_disk(xfs_dinode_core_t *dcp, xfs_mount_t *mp, int w)
{
	return XFS_CFORK_SIZE_DISK(dcp, mp, w);
}
int
xfs_cfork_size(xfs_dinode_core_t *dcp, xfs_mount_t *mp, int w)
{
	return XFS_CFORK_SIZE(dcp, mp, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_CNT_BLOCK)
xfs_agblock_t
xfs_cnt_block(xfs_mount_t *mp)
{
	return XFS_CNT_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DA_COOKIE_BNO)
xfs_dablk_t
xfs_da_cookie_bno(xfs_mount_t *mp, xfs_off_t cookie)
{
	return XFS_DA_COOKIE_BNO(mp, cookie);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DA_COOKIE_ENTRY)
int
xfs_da_cookie_entry(xfs_mount_t *mp, xfs_off_t cookie)
{
	return XFS_DA_COOKIE_ENTRY(mp, cookie);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DA_COOKIE_HASH)
/*ARGSUSED1*/
xfs_dahash_t
xfs_da_cookie_hash(xfs_mount_t *mp, xfs_off_t cookie)
{
	return XFS_DA_COOKIE_HASH(mp, cookie);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DA_MAKE_BNOENTRY)
__uint32_t
xfs_da_make_bnoentry(xfs_mount_t *mp, xfs_dablk_t bno, int entry)
{
	return XFS_DA_MAKE_BNOENTRY(mp, bno, entry);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DA_MAKE_COOKIE)
xfs_off_t
xfs_da_make_cookie(xfs_mount_t *mp, xfs_dablk_t bno, int entry,
		   xfs_dahash_t hash)
{
	return XFS_DA_MAKE_COOKIE(mp, bno, entry, hash);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DADDR_TO_AGBNO)
xfs_agblock_t
xfs_daddr_to_agbno(xfs_mount_t *mp, xfs_daddr_t d)
{
	return XFS_DADDR_TO_AGBNO(mp, d);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DADDR_TO_AGNO)
xfs_agnumber_t
xfs_daddr_to_agno(xfs_mount_t *mp, xfs_daddr_t d)
{
	return XFS_DADDR_TO_AGNO(mp, d);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DADDR_TO_FSB)
xfs_fsblock_t
xfs_daddr_to_fsb(xfs_mount_t *mp, xfs_daddr_t d)
{
	return XFS_DADDR_TO_FSB(mp, d);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_APTR)
char *
xfs_dfork_aptr(xfs_dinode_t *dip)
{
	return XFS_DFORK_APTR(dip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_ASIZE)
int
xfs_dfork_asize(xfs_dinode_t *dip, xfs_mount_t *mp)
{
	return XFS_DFORK_ASIZE(dip, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_BOFF)
int
xfs_dfork_boff(xfs_dinode_t *dip)
{
	return XFS_DFORK_BOFF(dip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_DPTR)
char *
xfs_dfork_dptr(xfs_dinode_t *dip)
{
	return XFS_DFORK_DPTR(dip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_DSIZE)
int
xfs_dfork_dsize(xfs_dinode_t *dip, xfs_mount_t *mp)
{
	return XFS_DFORK_DSIZE(dip, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_NEXTENTS)
int
xfs_dfork_nextents(xfs_dinode_t *dip, int w)
{
	return XFS_DFORK_NEXTENTS(dip, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_PTR)
char *
xfs_dfork_ptr(xfs_dinode_t *dip, int w)
{
	return XFS_DFORK_PTR(dip, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_Q)
int
xfs_dfork_q(xfs_dinode_t *dip)
{
	return XFS_DFORK_Q(dip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DFORK_SIZE)
int
xfs_dfork_size(xfs_dinode_t *dip, xfs_mount_t *mp, int w)
{
	return XFS_DFORK_SIZE(dip, mp, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DINODE_GOOD_VERSION)
int
xfs_dinode_good_version(int v)
{
	return XFS_DINODE_GOOD_VERSION(v);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_LEAF_ENTSIZE_BYENTRY)
int
xfs_dir_leaf_entsize_byentry(xfs_dir_leaf_entry_t *entry)
{
	return XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_LEAF_ENTSIZE_BYNAME)
int
xfs_dir_leaf_entsize_byname(int len)
{
	return XFS_DIR_LEAF_ENTSIZE_BYNAME(len);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_LEAF_NAMESTRUCT)
xfs_dir_leaf_name_t *
xfs_dir_leaf_namestruct(xfs_dir_leafblock_t *leafp, int offset)
{
	return XFS_DIR_LEAF_NAMESTRUCT(leafp, offset);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_SF_ALLFIT)
int
xfs_dir_sf_allfit(int count, int totallen)
{
	return XFS_DIR_SF_ALLFIT(count, totallen);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_SF_ENTSIZE_BYENTRY)
int
xfs_dir_sf_entsize_byentry(xfs_dir_sf_entry_t *sfep)
{
	return XFS_DIR_SF_ENTSIZE_BYENTRY(sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_SF_ENTSIZE_BYNAME)
int
xfs_dir_sf_entsize_byname(int len)
{
	return XFS_DIR_SF_ENTSIZE_BYNAME(len);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_SF_GET_DIRINO)
void
xfs_dir_sf_get_dirino(xfs_dir_ino_t *from, xfs_ino_t *to)
{
	XFS_DIR_SF_GET_DIRINO(from, to);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_SF_NEXTENTRY)
xfs_dir_sf_entry_t *
xfs_dir_sf_nextentry(xfs_dir_sf_entry_t *sfep)
{
	return XFS_DIR_SF_NEXTENTRY(sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR_SF_PUT_DIRINO)
void
xfs_dir_sf_put_dirino(xfs_ino_t *from, xfs_dir_ino_t *to)
{
	XFS_DIR_SF_PUT_DIRINO(from, to);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_BLOCK_LEAF_P)
xfs_dir2_leaf_entry_t *
xfs_dir2_block_leaf_p(xfs_dir2_block_tail_t *btp)
{
	return XFS_DIR2_BLOCK_LEAF_P(btp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_BLOCK_TAIL_P)
xfs_dir2_block_tail_t *
xfs_dir2_block_tail_p(xfs_mount_t *mp, xfs_dir2_block_t *block)
{
	return XFS_DIR2_BLOCK_TAIL_P(mp, block);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_BYTE_TO_DA)
xfs_dablk_t
xfs_dir2_byte_to_da(xfs_mount_t *mp, xfs_dir2_off_t by)
{
	return XFS_DIR2_BYTE_TO_DA(mp, by);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_BYTE_TO_DATAPTR)
/* ARGSUSED */
xfs_dir2_dataptr_t
xfs_dir2_byte_to_dataptr(xfs_mount_t *mp, xfs_dir2_off_t by)
{
	return XFS_DIR2_BYTE_TO_DATAPTR(mp, by);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_BYTE_TO_DB)
xfs_dir2_db_t
xfs_dir2_byte_to_db(xfs_mount_t *mp, xfs_dir2_off_t by)
{
	return XFS_DIR2_BYTE_TO_DB(mp, by);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_BYTE_TO_OFF)
xfs_dir2_data_aoff_t
xfs_dir2_byte_to_off(xfs_mount_t *mp, xfs_dir2_off_t by)
{
	return XFS_DIR2_BYTE_TO_OFF(mp, by);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DA_TO_BYTE)
xfs_dir2_off_t
xfs_dir2_da_to_byte(xfs_mount_t *mp, xfs_dablk_t da)
{
	return XFS_DIR2_DA_TO_BYTE(mp, da);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DA_TO_DB)
xfs_dir2_db_t
xfs_dir2_da_to_db(xfs_mount_t *mp, xfs_dablk_t da)
{
	return XFS_DIR2_DA_TO_DB(mp, da);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DATA_ENTRY_TAG_P)
xfs_dir2_data_off_t *
xfs_dir2_data_entry_tag_p(xfs_dir2_data_entry_t *dep)
{
	return XFS_DIR2_DATA_ENTRY_TAG_P(dep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DATA_ENTSIZE)
int
xfs_dir2_data_entsize(int n)
{
	return XFS_DIR2_DATA_ENTSIZE(n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DATA_UNUSED_TAG_P)
xfs_dir2_data_off_t *
xfs_dir2_data_unused_tag_p(xfs_dir2_data_unused_t *dup)
{
	return XFS_DIR2_DATA_UNUSED_TAG_P(dup);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DATAPTR_TO_BYTE)
/* ARGSUSED */
xfs_dir2_off_t
xfs_dir2_dataptr_to_byte(xfs_mount_t *mp, xfs_dir2_dataptr_t dp)
{
	return XFS_DIR2_DATAPTR_TO_BYTE(mp, dp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DATAPTR_TO_DB)
xfs_dir2_db_t
xfs_dir2_dataptr_to_db(xfs_mount_t *mp, xfs_dir2_dataptr_t dp)
{
	return XFS_DIR2_DATAPTR_TO_DB(mp, dp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DATAPTR_TO_OFF)
xfs_dir2_data_aoff_t
xfs_dir2_dataptr_to_off(xfs_mount_t *mp, xfs_dir2_dataptr_t dp)
{
	return XFS_DIR2_DATAPTR_TO_OFF(mp, dp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DB_OFF_TO_BYTE)
xfs_dir2_off_t
xfs_dir2_db_off_to_byte(xfs_mount_t *mp, xfs_dir2_db_t db,
			xfs_dir2_data_aoff_t o)
{
	return XFS_DIR2_DB_OFF_TO_BYTE(mp, db, o);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DB_OFF_TO_DATAPTR)
xfs_dir2_dataptr_t
xfs_dir2_db_off_to_dataptr(xfs_mount_t *mp, xfs_dir2_db_t db,
			   xfs_dir2_data_aoff_t o)
{
	return XFS_DIR2_DB_OFF_TO_DATAPTR(mp, db, o);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DB_TO_DA)
xfs_dablk_t
xfs_dir2_db_to_da(xfs_mount_t *mp, xfs_dir2_db_t db)
{
	return XFS_DIR2_DB_TO_DA(mp, db);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DB_TO_FDB)
xfs_dir2_db_t
xfs_dir2_db_to_fdb(xfs_mount_t *mp, xfs_dir2_db_t db)
{
	return XFS_DIR2_DB_TO_FDB(mp, db);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_DB_TO_FDINDEX)
int
xfs_dir2_db_to_fdindex(xfs_mount_t *mp, xfs_dir2_db_t db)
{
	return XFS_DIR2_DB_TO_FDINDEX(mp, db);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_LEAF_BESTS_P)
xfs_dir2_data_off_t *
xfs_dir2_leaf_bests_p(xfs_dir2_leaf_tail_t *ltp)
{
	return XFS_DIR2_LEAF_BESTS_P(ltp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_LEAF_TAIL_P)
xfs_dir2_leaf_tail_t *
xfs_dir2_leaf_tail_p(xfs_mount_t *mp, xfs_dir2_leaf_t *lp)
{
	return XFS_DIR2_LEAF_TAIL_P(mp, lp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_MAX_LEAF_ENTS)
int
xfs_dir2_max_leaf_ents(xfs_mount_t *mp)
{
	return XFS_DIR2_MAX_LEAF_ENTS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_ENTSIZE_BYENTRY)
int
xfs_dir2_sf_entsize_byentry(xfs_dir2_sf_t *sfp, xfs_dir2_sf_entry_t *sfep)
{
	return XFS_DIR2_SF_ENTSIZE_BYENTRY(sfp, sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_FIRSTENTRY)
xfs_dir2_sf_entry_t *
xfs_dir2_sf_firstentry(xfs_dir2_sf_t *sfp)
{
	return XFS_DIR2_SF_FIRSTENTRY(sfp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_ENTSIZE_BYNAME)
int
xfs_dir2_sf_entsize_byname(xfs_dir2_sf_t *sfp, int len)
{
	return XFS_DIR2_SF_ENTSIZE_BYNAME(sfp, len);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_GET_INUMBER)
xfs_intino_t
xfs_dir2_sf_get_inumber(xfs_dir2_sf_t *sfp, xfs_dir2_inou_t *from)
{
	return XFS_DIR2_SF_GET_INUMBER(sfp, from);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_GET_OFFSET)
xfs_dir2_data_aoff_t
xfs_dir2_sf_get_offset(xfs_dir2_sf_entry_t *sfep)
{
	return XFS_DIR2_SF_GET_OFFSET(sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_HDR_SIZE)
int
xfs_dir2_sf_hdr_size(int i8count)
{
	return XFS_DIR2_SF_HDR_SIZE(i8count);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_INUMBERP)
xfs_dir2_inou_t *
xfs_dir2_sf_inumberp(xfs_dir2_sf_entry_t *sfep)
{
	return XFS_DIR2_SF_INUMBERP(sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_NEXTENTRY)
xfs_dir2_sf_entry_t *
xfs_dir2_sf_nextentry(xfs_dir2_sf_t *sfp, xfs_dir2_sf_entry_t *sfep)
{
	return XFS_DIR2_SF_NEXTENTRY(sfp, sfep);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_PUT_INUMBER)
void
xfs_dir2_sf_put_inumber(xfs_dir2_sf_t *sfp, xfs_ino_t *from, xfs_dir2_inou_t *to)
{
	XFS_DIR2_SF_PUT_INUMBER(sfp, from, to);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_DIR2_SF_PUT_OFFSET)
void
xfs_dir2_sf_put_offset(xfs_dir2_sf_entry_t *sfep, xfs_dir2_data_aoff_t off)
{
	XFS_DIR2_SF_PUT_OFFSET(sfep, off);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_EXTFMT_INODE )
xfs_exntfmt_t
xfs_extfmt_inode(struct xfs_inode *ip)
{
	return XFS_EXTFMT_INODE(ip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_EXTLEN_MAX)
xfs_extlen_t
xfs_extlen_max(xfs_extlen_t a, xfs_extlen_t b)
{
	return XFS_EXTLEN_MAX(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_EXTLEN_MIN)
xfs_extlen_t
xfs_extlen_min(xfs_extlen_t a, xfs_extlen_t b)
{
	return XFS_EXTLEN_MIN(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FILBLKS_MAX)
xfs_filblks_t
xfs_filblks_max(xfs_filblks_t a, xfs_filblks_t b)
{
	return XFS_FILBLKS_MAX(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FILBLKS_MIN)
xfs_filblks_t
xfs_filblks_min(xfs_filblks_t a, xfs_filblks_t b)
{
	return XFS_FILBLKS_MIN(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FILEOFF_MAX)
xfs_fileoff_t
xfs_fileoff_max(xfs_fileoff_t a, xfs_fileoff_t b)
{
	return XFS_FILEOFF_MAX(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FILEOFF_MIN)
xfs_fileoff_t
xfs_fileoff_min(xfs_fileoff_t a, xfs_fileoff_t b)
{
	return XFS_FILEOFF_MIN(a, b);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FSB_SANITY_CHECK)
int
xfs_fsb_sanity_check(xfs_mount_t *mp, xfs_fsblock_t fsbno)
{
	return XFS_FSB_SANITY_CHECK(mp, fsbno);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FSB_TO_AGBNO)
xfs_agblock_t
xfs_fsb_to_agbno(xfs_mount_t *mp, xfs_fsblock_t fsbno)
{
	return XFS_FSB_TO_AGBNO(mp, fsbno);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FSB_TO_AGNO)
xfs_agnumber_t
xfs_fsb_to_agno(xfs_mount_t *mp, xfs_fsblock_t fsbno)
{
	return XFS_FSB_TO_AGNO(mp, fsbno);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FSB_TO_DADDR)
xfs_daddr_t
xfs_fsb_to_daddr(xfs_mount_t *mp, xfs_fsblock_t fsbno)
{
	return XFS_FSB_TO_DADDR(mp, fsbno);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_FSB_TO_DB)
xfs_daddr_t
xfs_fsb_to_db(xfs_inode_t *ip, xfs_fsblock_t fsb)
{
	return XFS_FSB_TO_DB(ip, fsb);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_HDR_BLOCK)
xfs_agblock_t
xfs_hdr_block(xfs_mount_t *mp, xfs_daddr_t d)
{
	return XFS_HDR_BLOCK(mp, d);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IALLOC_BLOCKS)
xfs_extlen_t
xfs_ialloc_blocks(xfs_mount_t *mp)
{
	return XFS_IALLOC_BLOCKS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IALLOC_FIND_FREE)
int
xfs_ialloc_find_free(xfs_inofree_t *fp)
{
	return XFS_IALLOC_FIND_FREE(fp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IALLOC_INODES)
int
xfs_ialloc_inodes(xfs_mount_t *mp)
{
	return XFS_IALLOC_INODES(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IBT_BLOCK)
xfs_agblock_t
xfs_ibt_block(xfs_mount_t *mp)
{
	return XFS_IBT_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_ASIZE)
int
xfs_ifork_asize(xfs_inode_t *ip)
{
	return XFS_IFORK_ASIZE(ip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_DSIZE)
int
xfs_ifork_dsize(xfs_inode_t *ip)
{
	return XFS_IFORK_DSIZE(ip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_FMT_SET)
void
xfs_ifork_fmt_set(xfs_inode_t *ip, int w, int n)
{
	XFS_IFORK_FMT_SET(ip, w, n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_FORMAT)
int
xfs_ifork_format(xfs_inode_t *ip, int w)
{
	return XFS_IFORK_FORMAT(ip, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_NEXT_SET)
void
xfs_ifork_next_set(xfs_inode_t *ip, int w, int n)
{
	XFS_IFORK_NEXT_SET(ip, w, n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_NEXTENTS)
int
xfs_ifork_nextents(xfs_inode_t *ip, int w)
{
	return XFS_IFORK_NEXTENTS(ip, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_PTR)
xfs_ifork_t *
xfs_ifork_ptr(xfs_inode_t *ip, int w)
{
	return XFS_IFORK_PTR(ip, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_Q)
int
xfs_ifork_q(xfs_inode_t *ip)
{
	return XFS_IFORK_Q(ip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IFORK_SIZE)
int
xfs_ifork_size(xfs_inode_t *ip, int w)
{
	return XFS_IFORK_SIZE(ip, w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ILOG_FBROOT)
int
xfs_ilog_fbroot(int w)
{
	return XFS_ILOG_FBROOT(w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ILOG_FDATA)
int
xfs_ilog_fdata(int w)
{
	return XFS_ILOG_FDATA(w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ILOG_FEXT)
int
xfs_ilog_fext(int w)
{
	return XFS_ILOG_FEXT(w);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_IN_MAXLEVELS)
int
xfs_in_maxlevels(xfs_mount_t *mp)
{
	return XFS_IN_MAXLEVELS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_AGBNO_BITS)
int
xfs_ino_agbno_bits(xfs_mount_t *mp)
{
	return XFS_INO_AGBNO_BITS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_AGINO_BITS)
int
xfs_ino_agino_bits(xfs_mount_t *mp)
{
	return XFS_INO_AGINO_BITS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_AGNO_BITS)
int
xfs_ino_agno_bits(xfs_mount_t *mp)
{
	return XFS_INO_AGNO_BITS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_BITS)
int
xfs_ino_bits(xfs_mount_t *mp)
{
	return XFS_INO_BITS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_MASK)
__uint32_t
xfs_ino_mask(int k)
{
	return XFS_INO_MASK(k);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_OFFSET_BITS)
int
xfs_ino_offset_bits(xfs_mount_t *mp)
{
	return XFS_INO_OFFSET_BITS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_TO_AGBNO)
xfs_agblock_t
xfs_ino_to_agbno(xfs_mount_t *mp, xfs_ino_t i)
{
	return XFS_INO_TO_AGBNO(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_TO_AGINO)
xfs_agino_t
xfs_ino_to_agino(xfs_mount_t *mp, xfs_ino_t i)
{
	return XFS_INO_TO_AGINO(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_TO_AGNO)
xfs_agnumber_t
xfs_ino_to_agno(xfs_mount_t *mp, xfs_ino_t i)
{
	return XFS_INO_TO_AGNO(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_TO_FSB)
xfs_fsblock_t
xfs_ino_to_fsb(xfs_mount_t *mp, xfs_ino_t i)
{
	return XFS_INO_TO_FSB(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INO_TO_OFFSET)
int
xfs_ino_to_offset(xfs_mount_t *mp, xfs_ino_t i)
{
	return XFS_INO_TO_OFFSET(mp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_BLOCK_MAXRECS)
int
xfs_inobt_block_maxrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_INOBT_BLOCK_MAXRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_BLOCK_MINRECS)
int
xfs_inobt_block_minrecs(int lev, xfs_btree_cur_t *cur)
{
	return XFS_INOBT_BLOCK_MINRECS(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_BLOCK_SIZE)
/*ARGSUSED1*/
int
xfs_inobt_block_size(int lev, xfs_btree_cur_t *cur)
{
	return XFS_INOBT_BLOCK_SIZE(lev, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_CLR_FREE)
void
xfs_inobt_clr_free(xfs_inobt_rec_t *rp, int i)
{
	XFS_INOBT_CLR_FREE(rp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_IS_FREE)
int
xfs_inobt_is_free(xfs_inobt_rec_t *rp, int i)
{
	return XFS_INOBT_IS_FREE(rp, i);
}
int
xfs_inobt_is_free_disk(xfs_inobt_rec_t *rp, int i)
{
	return XFS_INOBT_IS_FREE_DISK(rp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_IS_LAST_REC)
int
xfs_inobt_is_last_rec(xfs_btree_cur_t *cur)
{
	return XFS_INOBT_IS_LAST_REC(cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_KEY_ADDR)
/*ARGSUSED3*/
xfs_inobt_key_t *
xfs_inobt_key_addr(xfs_inobt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_INOBT_KEY_ADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_MASK)
xfs_inofree_t
xfs_inobt_mask(int i)
{
	return XFS_INOBT_MASK(i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_MASKN)
xfs_inofree_t
xfs_inobt_maskn(int i, int n)
{
	return XFS_INOBT_MASKN(i, n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_PTR_ADDR)
xfs_inobt_ptr_t *
xfs_inobt_ptr_addr(xfs_inobt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_INOBT_PTR_ADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_REC_ADDR)
/*ARGSUSED3*/
xfs_inobt_rec_t *
xfs_inobt_rec_addr(xfs_inobt_block_t *bb, int i, xfs_btree_cur_t *cur)
{
	return XFS_INOBT_REC_ADDR(bb, i, cur);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_INOBT_SET_FREE)
void
xfs_inobt_set_free(xfs_inobt_rec_t *rp, int i)
{
	XFS_INOBT_SET_FREE(rp, i);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ITOBHV)
bhv_desc_t *
xfs_itobhv(xfs_inode_t *ip)
{
	return XFS_ITOBHV(ip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_ITOV)
vnode_t *
xfs_itov(xfs_inode_t *ip)
{
	return XFS_ITOV(ip);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LBLOG)
int
xfs_lblog(xfs_mount_t *mp)
{
	return XFS_LBLOG(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LBSIZE)
int
xfs_lbsize(xfs_mount_t *mp)
{
	return XFS_LBSIZE(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_ALL_FREE)
void
xfs_lic_all_free(xfs_log_item_chunk_t *cp)
{
	XFS_LIC_ALL_FREE(cp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_ARE_ALL_FREE)
int
xfs_lic_are_all_free(xfs_log_item_chunk_t *cp)
{
	return XFS_LIC_ARE_ALL_FREE(cp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_CLAIM)
void
xfs_lic_claim(xfs_log_item_chunk_t *cp, int slot)
{
	XFS_LIC_CLAIM(cp, slot);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_DESC_TO_CHUNK)
xfs_log_item_chunk_t *
xfs_lic_desc_to_chunk(xfs_log_item_desc_t *dp)
{
	return XFS_LIC_DESC_TO_CHUNK(dp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_DESC_TO_SLOT)
int
xfs_lic_desc_to_slot(xfs_log_item_desc_t *dp)
{
	return XFS_LIC_DESC_TO_SLOT(dp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_INIT)
void
xfs_lic_init(xfs_log_item_chunk_t *cp)
{
	XFS_LIC_INIT(cp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_INIT_SLOT)
void
xfs_lic_init_slot(xfs_log_item_chunk_t *cp, int slot)
{
	XFS_LIC_INIT_SLOT(cp, slot);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_ISFREE)
int
xfs_lic_isfree(xfs_log_item_chunk_t *cp, int slot)
{
	return XFS_LIC_ISFREE(cp, slot);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_RELSE)
void
xfs_lic_relse(xfs_log_item_chunk_t *cp, int slot)
{
	XFS_LIC_RELSE(cp, slot);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_SLOT)
xfs_log_item_desc_t *
xfs_lic_slot(xfs_log_item_chunk_t *cp, int slot)
{
	return XFS_LIC_SLOT(cp, slot);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LIC_VACANCY)
int
xfs_lic_vacancy(xfs_log_item_chunk_t *cp)
{
	return XFS_LIC_VACANCY(cp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_LITINO)
int
xfs_litino(xfs_mount_t *mp)
{
	return XFS_LITINO(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MAKE_IPTR)
xfs_dinode_t *
xfs_make_iptr(xfs_mount_t *mp, xfs_buf_t *b, int o)
{
	return XFS_MAKE_IPTR(mp, b, o);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MASK32HI)
__uint32_t
xfs_mask32hi(int n)
{
	return XFS_MASK32HI(n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MASK32LO)
__uint32_t
xfs_mask32lo(int n)
{
	return XFS_MASK32LO(n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MASK64HI)
__uint64_t
xfs_mask64hi(int n)
{
	return XFS_MASK64HI(n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MASK64LO)
__uint64_t
xfs_mask64lo(int n)
{
	return XFS_MASK64LO(n);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MIN_FREELIST)
int
xfs_min_freelist(xfs_agf_t *a, xfs_mount_t *mp)
{
	return XFS_MIN_FREELIST(a, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MIN_FREELIST_PAG)
int
xfs_min_freelist_pag(xfs_perag_t *pag, xfs_mount_t *mp)
{
	return XFS_MIN_FREELIST_PAG(pag, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MIN_FREELIST_RAW)
int
xfs_min_freelist_raw(uint bl, uint cl, xfs_mount_t *mp)
{
	return XFS_MIN_FREELIST_RAW(bl, cl, mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_MTOVFS)
vfs_t *
xfs_mtovfs(xfs_mount_t *mp)
{
	return XFS_MTOVFS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_OFFBNO_TO_AGINO)
xfs_agino_t
xfs_offbno_to_agino(xfs_mount_t *mp, xfs_agblock_t b, int o)
{
	return XFS_OFFBNO_TO_AGINO(mp, b, o);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_PREALLOC_BLOCKS)
xfs_agblock_t
xfs_prealloc_blocks(xfs_mount_t *mp)
{
	return XFS_PREALLOC_BLOCKS(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_BLOCK)
xfs_agblock_t
xfs_sb_block(xfs_mount_t *mp)
{
	return XFS_SB_BLOCK(mp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_GOOD_VERSION)
int
xfs_sb_good_version(xfs_sb_t *sbp)
{
	return XFS_SB_GOOD_VERSION(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_ADDATTR)
void
xfs_sb_version_addattr(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_ADDATTR(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_ADDDALIGN)
void
xfs_sb_version_adddalign(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_ADDDALIGN(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_ADDNLINK)
void
xfs_sb_version_addnlink(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_ADDNLINK(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_ADDQUOTA)
void
xfs_sb_version_addquota(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_ADDQUOTA(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_ADDSHARED)
void
xfs_sb_version_addshared(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_ADDSHARED(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASALIGN)
int
xfs_sb_version_hasalign(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASALIGN(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASATTR)
int
xfs_sb_version_hasattr(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASATTR(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASDALIGN)
int
xfs_sb_version_hasdalign(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASDALIGN(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASDIRV2)
int
xfs_sb_version_hasdirv2(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASDIRV2(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASEXTFLGBIT)
int
xfs_sb_version_hasextflgbit(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASEXTFLGBIT(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASNLINK)
int
xfs_sb_version_hasnlink(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASNLINK(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASQUOTA)
int
xfs_sb_version_hasquota(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASQUOTA(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASSHARED)
int
xfs_sb_version_hasshared(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASSHARED(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_NUM)
int
xfs_sb_version_num(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_NUM(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_SUBALIGN)
void
xfs_sb_version_subalign(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_SUBALIGN(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_SUBSHARED)
void
xfs_sb_version_subshared(xfs_sb_t *sbp)
{
	XFS_SB_VERSION_SUBSHARED(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASLOGV2)
int
xfs_sb_version_haslogv2(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASLOGV2(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASSECTOR)
int
xfs_sb_version_hassector(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASSECTOR(sbp);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_TONEW)
unsigned
xfs_sb_version_tonew(unsigned v)
{
	return XFS_SB_VERSION_TONEW(v);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_TOOLD)
unsigned
xfs_sb_version_toold(unsigned v)
{
	return XFS_SB_VERSION_TOOLD(v);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XLOG_GRANT_ADD_SPACE)
void
xlog_grant_add_space(xlog_t *log, int bytes, int type)
{
	XLOG_GRANT_ADD_SPACE(log, bytes, type);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XLOG_GRANT_SUB_SPACE)
void
xlog_grant_sub_space(xlog_t *log, int bytes, int type)
{
	XLOG_GRANT_SUB_SPACE(log, bytes, type);
}
#endif

#if XFS_WANT_FUNCS_C || (XFS_WANT_SPACE_C && XFSSO_XFS_SB_VERSION_HASMOREBITS)
int
xfs_sb_version_hasmorebits(xfs_sb_t *sbp)
{
	return XFS_SB_VERSION_HASMOREBITS(sbp);
}
#endif

