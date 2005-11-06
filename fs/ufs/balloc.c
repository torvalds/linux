/*
 *  linux/fs/ufs/balloc.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

#include "swab.h"
#include "util.h"

#undef UFS_BALLOC_DEBUG

#ifdef UFS_BALLOC_DEBUG
#define UFSD(x) printk("(%s, %d), %s:", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif

static unsigned ufs_add_fragments (struct inode *, unsigned, unsigned, unsigned, int *);
static unsigned ufs_alloc_fragments (struct inode *, unsigned, unsigned, unsigned, int *);
static unsigned ufs_alloccg_block (struct inode *, struct ufs_cg_private_info *, unsigned, int *);
static unsigned ufs_bitmap_search (struct super_block *, struct ufs_cg_private_info *, unsigned, unsigned);
static unsigned char ufs_fragtable_8fpb[], ufs_fragtable_other[];
static void ufs_clusteracct(struct super_block *, struct ufs_cg_private_info *, unsigned, int);

/*
 * Free 'count' fragments from fragment number 'fragment'
 */
void ufs_free_fragments (struct inode * inode, unsigned fragment, unsigned count) {
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned cgno, bit, end_bit, bbase, blkmap, i, blkno, cylno;
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	
	UFSD(("ENTER, fragment %u, count %u\n", fragment, count))
	
	if (ufs_fragnum(fragment) + count > uspi->s_fpg)
		ufs_error (sb, "ufs_free_fragments", "internal error");
	
	lock_super(sb);
	
	cgno = ufs_dtog(fragment);
	bit = ufs_dtogd(fragment);
	if (cgno >= uspi->s_ncg) {
		ufs_panic (sb, "ufs_free_fragments", "freeing blocks are outside device");
		goto failed;
	}
		
	ucpi = ufs_load_cylinder (sb, cgno);
	if (!ucpi) 
		goto failed;
	ucg = ubh_get_ucg (UCPI_UBH);
	if (!ufs_cg_chkmagic(sb, ucg)) {
		ufs_panic (sb, "ufs_free_fragments", "internal error, bad magic number on cg %u", cgno);
		goto failed;
	}

	end_bit = bit + count;
	bbase = ufs_blknum (bit);
	blkmap = ubh_blkmap (UCPI_UBH, ucpi->c_freeoff, bbase);
	ufs_fragacct (sb, blkmap, ucg->cg_frsum, -1);
	for (i = bit; i < end_bit; i++) {
		if (ubh_isclr (UCPI_UBH, ucpi->c_freeoff, i))
			ubh_setbit (UCPI_UBH, ucpi->c_freeoff, i);
		else ufs_error (sb, "ufs_free_fragments",
			"bit already cleared for fragment %u", i);
	}
	
	DQUOT_FREE_BLOCK (inode, count);

	
	fs32_add(sb, &ucg->cg_cs.cs_nffree, count);
	fs32_add(sb, &usb1->fs_cstotal.cs_nffree, count);
	fs32_add(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nffree, count);
	blkmap = ubh_blkmap (UCPI_UBH, ucpi->c_freeoff, bbase);
	ufs_fragacct(sb, blkmap, ucg->cg_frsum, 1);

	/*
	 * Trying to reassemble free fragments into block
	 */
	blkno = ufs_fragstoblks (bbase);
	if (ubh_isblockset(UCPI_UBH, ucpi->c_freeoff, blkno)) {
		fs32_sub(sb, &ucg->cg_cs.cs_nffree, uspi->s_fpb);
		fs32_sub(sb, &usb1->fs_cstotal.cs_nffree, uspi->s_fpb);
		fs32_sub(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nffree, uspi->s_fpb);
		if ((UFS_SB(sb)->s_flags & UFS_CG_MASK) == UFS_CG_44BSD)
			ufs_clusteracct (sb, ucpi, blkno, 1);
		fs32_add(sb, &ucg->cg_cs.cs_nbfree, 1);
		fs32_add(sb, &usb1->fs_cstotal.cs_nbfree, 1);
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nbfree, 1);
		cylno = ufs_cbtocylno (bbase);
		fs16_add(sb, &ubh_cg_blks(ucpi, cylno, ufs_cbtorpos(bbase)), 1);
		fs32_add(sb, &ubh_cg_blktot(ucpi, cylno), 1);
	}
	
	ubh_mark_buffer_dirty (USPI_UBH);
	ubh_mark_buffer_dirty (UCPI_UBH);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block (SWRITE, 1, (struct ufs_buffer_head **)&ucpi);
		ubh_wait_on_buffer (UCPI_UBH);
	}
	sb->s_dirt = 1;
	
	unlock_super (sb);
	UFSD(("EXIT\n"))
	return;

failed:
	unlock_super (sb);
	UFSD(("EXIT (FAILED)\n"))
	return;
}

/*
 * Free 'count' fragments from fragment number 'fragment' (free whole blocks)
 */
void ufs_free_blocks (struct inode * inode, unsigned fragment, unsigned count) {
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned overflow, cgno, bit, end_bit, blkno, i, cylno;
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);

	UFSD(("ENTER, fragment %u, count %u\n", fragment, count))
	
	if ((fragment & uspi->s_fpbmask) || (count & uspi->s_fpbmask)) {
		ufs_error (sb, "ufs_free_blocks", "internal error, "
			"fragment %u, count %u\n", fragment, count);
		goto failed;
	}

	lock_super(sb);
	
do_more:
	overflow = 0;
	cgno = ufs_dtog (fragment);
	bit = ufs_dtogd (fragment);
	if (cgno >= uspi->s_ncg) {
		ufs_panic (sb, "ufs_free_blocks", "freeing blocks are outside device");
		goto failed;
	}
	end_bit = bit + count;
	if (end_bit > uspi->s_fpg) {
		overflow = bit + count - uspi->s_fpg;
		count -= overflow;
		end_bit -= overflow;
	}

	ucpi = ufs_load_cylinder (sb, cgno);
	if (!ucpi) 
		goto failed;
	ucg = ubh_get_ucg (UCPI_UBH);
	if (!ufs_cg_chkmagic(sb, ucg)) {
		ufs_panic (sb, "ufs_free_blocks", "internal error, bad magic number on cg %u", cgno);
		goto failed;
	}

	for (i = bit; i < end_bit; i += uspi->s_fpb) {
		blkno = ufs_fragstoblks(i);
		if (ubh_isblockset(UCPI_UBH, ucpi->c_freeoff, blkno)) {
			ufs_error(sb, "ufs_free_blocks", "freeing free fragment");
		}
		ubh_setblock(UCPI_UBH, ucpi->c_freeoff, blkno);
		if ((UFS_SB(sb)->s_flags & UFS_CG_MASK) == UFS_CG_44BSD)
			ufs_clusteracct (sb, ucpi, blkno, 1);
		DQUOT_FREE_BLOCK(inode, uspi->s_fpb);

		fs32_add(sb, &ucg->cg_cs.cs_nbfree, 1);
		fs32_add(sb, &usb1->fs_cstotal.cs_nbfree, 1);
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nbfree, 1);
		cylno = ufs_cbtocylno(i);
		fs16_add(sb, &ubh_cg_blks(ucpi, cylno, ufs_cbtorpos(i)), 1);
		fs32_add(sb, &ubh_cg_blktot(ucpi, cylno), 1);
	}

	ubh_mark_buffer_dirty (USPI_UBH);
	ubh_mark_buffer_dirty (UCPI_UBH);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block (SWRITE, 1, (struct ufs_buffer_head **)&ucpi);
		ubh_wait_on_buffer (UCPI_UBH);
	}

	if (overflow) {
		fragment += count;
		count = overflow;
		goto do_more;
	}

	sb->s_dirt = 1;
	unlock_super (sb);
	UFSD(("EXIT\n"))
	return;

failed:
	unlock_super (sb);
	UFSD(("EXIT (FAILED)\n"))
	return;
}



#define NULLIFY_FRAGMENTS \
	for (i = oldcount; i < newcount; i++) { \
		bh = sb_getblk(sb, result + i); \
		memset (bh->b_data, 0, sb->s_blocksize); \
		set_buffer_uptodate(bh); \
		mark_buffer_dirty (bh); \
		if (IS_SYNC(inode)) \
			sync_dirty_buffer(bh); \
		brelse (bh); \
	}

unsigned ufs_new_fragments (struct inode * inode, __fs32 * p, unsigned fragment,
	unsigned goal, unsigned count, int * err )
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct buffer_head * bh;
	unsigned cgno, oldcount, newcount, tmp, request, i, result;
	
	UFSD(("ENTER, ino %lu, fragment %u, goal %u, count %u\n", inode->i_ino, fragment, goal, count))
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	*err = -ENOSPC;

	lock_super (sb);
	
	tmp = fs32_to_cpu(sb, *p);
	if (count + ufs_fragnum(fragment) > uspi->s_fpb) {
		ufs_warning (sb, "ufs_new_fragments", "internal warning"
			" fragment %u, count %u", fragment, count);
		count = uspi->s_fpb - ufs_fragnum(fragment); 
	}
	oldcount = ufs_fragnum (fragment);
	newcount = oldcount + count;

	/*
	 * Somebody else has just allocated our fragments
	 */
	if (oldcount) {
		if (!tmp) {
			ufs_error (sb, "ufs_new_fragments", "internal error, "
				"fragment %u, tmp %u\n", fragment, tmp);
			unlock_super (sb);
			return (unsigned)-1;
		}
		if (fragment < UFS_I(inode)->i_lastfrag) {
			UFSD(("EXIT (ALREADY ALLOCATED)\n"))
			unlock_super (sb);
			return 0;
		}
	}
	else {
		if (tmp) {
			UFSD(("EXIT (ALREADY ALLOCATED)\n"))
			unlock_super(sb);
			return 0;
		}
	}

	/*
	 * There is not enough space for user on the device
	 */
	if (!capable(CAP_SYS_RESOURCE) && ufs_freespace(usb1, UFS_MINFREE) <= 0) {
		unlock_super (sb);
		UFSD(("EXIT (FAILED)\n"))
		return 0;
	}

	if (goal >= uspi->s_size) 
		goal = 0;
	if (goal == 0) 
		cgno = ufs_inotocg (inode->i_ino);
	else
		cgno = ufs_dtog (goal);
	 
	/*
	 * allocate new fragment
	 */
	if (oldcount == 0) {
		result = ufs_alloc_fragments (inode, cgno, goal, count, err);
		if (result) {
			*p = cpu_to_fs32(sb, result);
			*err = 0;
			inode->i_blocks += count << uspi->s_nspfshift;
			UFS_I(inode)->i_lastfrag = max_t(u32, UFS_I(inode)->i_lastfrag, fragment + count);
			NULLIFY_FRAGMENTS
		}
		unlock_super(sb);
		UFSD(("EXIT, result %u\n", result))
		return result;
	}

	/*
	 * resize block
	 */
	result = ufs_add_fragments (inode, tmp, oldcount, newcount, err);
	if (result) {
		*err = 0;
		inode->i_blocks += count << uspi->s_nspfshift;
		UFS_I(inode)->i_lastfrag = max_t(u32, UFS_I(inode)->i_lastfrag, fragment + count);
		NULLIFY_FRAGMENTS
		unlock_super(sb);
		UFSD(("EXIT, result %u\n", result))
		return result;
	}

	/*
	 * allocate new block and move data
	 */
	switch (fs32_to_cpu(sb, usb1->fs_optim)) {
	    case UFS_OPTSPACE:
		request = newcount;
		if (uspi->s_minfree < 5 || fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree) 
		    > uspi->s_dsize * uspi->s_minfree / (2 * 100) )
			break;
		usb1->fs_optim = cpu_to_fs32(sb, UFS_OPTTIME);
		break;
	    default:
		usb1->fs_optim = cpu_to_fs32(sb, UFS_OPTTIME);
	
	    case UFS_OPTTIME:
		request = uspi->s_fpb;
		if (fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree) < uspi->s_dsize *
		    (uspi->s_minfree - 2) / 100)
			break;
		usb1->fs_optim = cpu_to_fs32(sb, UFS_OPTTIME);
		break;
	}
	result = ufs_alloc_fragments (inode, cgno, goal, request, err);
	if (result) {
		for (i = 0; i < oldcount; i++) {
			bh = sb_bread(sb, tmp + i);
			if(bh)
			{
				clear_buffer_dirty(bh);
				bh->b_blocknr = result + i;
				mark_buffer_dirty (bh);
				if (IS_SYNC(inode))
					sync_dirty_buffer(bh);
				brelse (bh);
			}
			else
			{
				printk(KERN_ERR "ufs_new_fragments: bread fail\n");
				unlock_super(sb);
				return 0;
			}
		}
		*p = cpu_to_fs32(sb, result);
		*err = 0;
		inode->i_blocks += count << uspi->s_nspfshift;
		UFS_I(inode)->i_lastfrag = max_t(u32, UFS_I(inode)->i_lastfrag, fragment + count);
		NULLIFY_FRAGMENTS
		unlock_super(sb);
		if (newcount < request)
			ufs_free_fragments (inode, result + newcount, request - newcount);
		ufs_free_fragments (inode, tmp, oldcount);
		UFSD(("EXIT, result %u\n", result))
		return result;
	}

	unlock_super(sb);
	UFSD(("EXIT (FAILED)\n"))
	return 0;
}		

static unsigned
ufs_add_fragments (struct inode * inode, unsigned fragment,
		   unsigned oldcount, unsigned newcount, int * err)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned cgno, fragno, fragoff, count, fragsize, i;
	
	UFSD(("ENTER, fragment %u, oldcount %u, newcount %u\n", fragment, oldcount, newcount))
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first (USPI_UBH);
	count = newcount - oldcount;
	
	cgno = ufs_dtog(fragment);
	if (fs32_to_cpu(sb, UFS_SB(sb)->fs_cs(cgno).cs_nffree) < count)
		return 0;
	if ((ufs_fragnum (fragment) + newcount) > uspi->s_fpb)
		return 0;
	ucpi = ufs_load_cylinder (sb, cgno);
	if (!ucpi)
		return 0;
	ucg = ubh_get_ucg (UCPI_UBH);
	if (!ufs_cg_chkmagic(sb, ucg)) {
		ufs_panic (sb, "ufs_add_fragments",
			"internal error, bad magic number on cg %u", cgno);
		return 0;
	}

	fragno = ufs_dtogd (fragment);
	fragoff = ufs_fragnum (fragno);
	for (i = oldcount; i < newcount; i++)
		if (ubh_isclr (UCPI_UBH, ucpi->c_freeoff, fragno + i))
			return 0;
	/*
	 * Block can be extended
	 */
	ucg->cg_time = cpu_to_fs32(sb, get_seconds());
	for (i = newcount; i < (uspi->s_fpb - fragoff); i++)
		if (ubh_isclr (UCPI_UBH, ucpi->c_freeoff, fragno + i))
			break;
	fragsize = i - oldcount;
	if (!fs32_to_cpu(sb, ucg->cg_frsum[fragsize]))
		ufs_panic (sb, "ufs_add_fragments",
			"internal error or corrupted bitmap on cg %u", cgno);
	fs32_sub(sb, &ucg->cg_frsum[fragsize], 1);
	if (fragsize != count)
		fs32_add(sb, &ucg->cg_frsum[fragsize - count], 1);
	for (i = oldcount; i < newcount; i++)
		ubh_clrbit (UCPI_UBH, ucpi->c_freeoff, fragno + i);
	if(DQUOT_ALLOC_BLOCK(inode, count)) {
		*err = -EDQUOT;
		return 0;
	}

	fs32_sub(sb, &ucg->cg_cs.cs_nffree, count);
	fs32_sub(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nffree, count);
	fs32_sub(sb, &usb1->fs_cstotal.cs_nffree, count);
	
	ubh_mark_buffer_dirty (USPI_UBH);
	ubh_mark_buffer_dirty (UCPI_UBH);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block (SWRITE, 1, (struct ufs_buffer_head **)&ucpi);
		ubh_wait_on_buffer (UCPI_UBH);
	}
	sb->s_dirt = 1;

	UFSD(("EXIT, fragment %u\n", fragment))
	
	return fragment;
}

#define UFS_TEST_FREE_SPACE_CG \
	ucg = (struct ufs_cylinder_group *) UFS_SB(sb)->s_ucg[cgno]->b_data; \
	if (fs32_to_cpu(sb, ucg->cg_cs.cs_nbfree)) \
		goto cg_found; \
	for (k = count; k < uspi->s_fpb; k++) \
		if (fs32_to_cpu(sb, ucg->cg_frsum[k])) \
			goto cg_found; 

static unsigned ufs_alloc_fragments (struct inode * inode, unsigned cgno,
	unsigned goal, unsigned count, int * err)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned oldcg, i, j, k, result, allocsize;
	
	UFSD(("ENTER, ino %lu, cgno %u, goal %u, count %u\n", inode->i_ino, cgno, goal, count))

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	oldcg = cgno;
	
	/*
	 * 1. searching on preferred cylinder group
	 */
	UFS_TEST_FREE_SPACE_CG

	/*
	 * 2. quadratic rehash
	 */
	for (j = 1; j < uspi->s_ncg; j *= 2) {
		cgno += j;
		if (cgno >= uspi->s_ncg) 
			cgno -= uspi->s_ncg;
		UFS_TEST_FREE_SPACE_CG
	}

	/*
	 * 3. brute force search
	 * We start at i = 2 ( 0 is checked at 1.step, 1 at 2.step )
	 */
	cgno = (oldcg + 1) % uspi->s_ncg;
	for (j = 2; j < uspi->s_ncg; j++) {
		cgno++;
		if (cgno >= uspi->s_ncg)
			cgno = 0;
		UFS_TEST_FREE_SPACE_CG
	}
	
	UFSD(("EXIT (FAILED)\n"))
	return 0;

cg_found:
	ucpi = ufs_load_cylinder (sb, cgno);
	if (!ucpi)
		return 0;
	ucg = ubh_get_ucg (UCPI_UBH);
	if (!ufs_cg_chkmagic(sb, ucg)) 
		ufs_panic (sb, "ufs_alloc_fragments",
			"internal error, bad magic number on cg %u", cgno);
	ucg->cg_time = cpu_to_fs32(sb, get_seconds());

	if (count == uspi->s_fpb) {
		result = ufs_alloccg_block (inode, ucpi, goal, err);
		if (result == (unsigned)-1)
			return 0;
		goto succed;
	}

	for (allocsize = count; allocsize < uspi->s_fpb; allocsize++)
		if (fs32_to_cpu(sb, ucg->cg_frsum[allocsize]) != 0)
			break;
	
	if (allocsize == uspi->s_fpb) {
		result = ufs_alloccg_block (inode, ucpi, goal, err);
		if (result == (unsigned)-1)
			return 0;
		goal = ufs_dtogd (result);
		for (i = count; i < uspi->s_fpb; i++)
			ubh_setbit (UCPI_UBH, ucpi->c_freeoff, goal + i);
		i = uspi->s_fpb - count;
		DQUOT_FREE_BLOCK(inode, i);

		fs32_add(sb, &ucg->cg_cs.cs_nffree, i);
		fs32_add(sb, &usb1->fs_cstotal.cs_nffree, i);
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nffree, i);
		fs32_add(sb, &ucg->cg_frsum[i], 1);
		goto succed;
	}

	result = ufs_bitmap_search (sb, ucpi, goal, allocsize);
	if (result == (unsigned)-1)
		return 0;
	if(DQUOT_ALLOC_BLOCK(inode, count)) {
		*err = -EDQUOT;
		return 0;
	}
	for (i = 0; i < count; i++)
		ubh_clrbit (UCPI_UBH, ucpi->c_freeoff, result + i);
	
	fs32_sub(sb, &ucg->cg_cs.cs_nffree, count);
	fs32_sub(sb, &usb1->fs_cstotal.cs_nffree, count);
	fs32_sub(sb, &UFS_SB(sb)->fs_cs(cgno).cs_nffree, count);
	fs32_sub(sb, &ucg->cg_frsum[allocsize], 1);

	if (count != allocsize)
		fs32_add(sb, &ucg->cg_frsum[allocsize - count], 1);

succed:
	ubh_mark_buffer_dirty (USPI_UBH);
	ubh_mark_buffer_dirty (UCPI_UBH);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block (SWRITE, 1, (struct ufs_buffer_head **)&ucpi);
		ubh_wait_on_buffer (UCPI_UBH);
	}
	sb->s_dirt = 1;

	result += cgno * uspi->s_fpg;
	UFSD(("EXIT3, result %u\n", result))
	return result;
}

static unsigned ufs_alloccg_block (struct inode * inode,
	struct ufs_cg_private_info * ucpi, unsigned goal, int * err)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cylinder_group * ucg;
	unsigned result, cylno, blkno;

	UFSD(("ENTER, goal %u\n", goal))

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	ucg = ubh_get_ucg(UCPI_UBH);

	if (goal == 0) {
		goal = ucpi->c_rotor;
		goto norot;
	}
	goal = ufs_blknum (goal);
	goal = ufs_dtogd (goal);
	
	/*
	 * If the requested block is available, use it.
	 */
	if (ubh_isblockset(UCPI_UBH, ucpi->c_freeoff, ufs_fragstoblks(goal))) {
		result = goal;
		goto gotit;
	}
	
norot:	
	result = ufs_bitmap_search (sb, ucpi, goal, uspi->s_fpb);
	if (result == (unsigned)-1)
		return (unsigned)-1;
	ucpi->c_rotor = result;
gotit:
	blkno = ufs_fragstoblks(result);
	ubh_clrblock (UCPI_UBH, ucpi->c_freeoff, blkno);
	if ((UFS_SB(sb)->s_flags & UFS_CG_MASK) == UFS_CG_44BSD)
		ufs_clusteracct (sb, ucpi, blkno, -1);
	if(DQUOT_ALLOC_BLOCK(inode, uspi->s_fpb)) {
		*err = -EDQUOT;
		return (unsigned)-1;
	}

	fs32_sub(sb, &ucg->cg_cs.cs_nbfree, 1);
	fs32_sub(sb, &usb1->fs_cstotal.cs_nbfree, 1);
	fs32_sub(sb, &UFS_SB(sb)->fs_cs(ucpi->c_cgx).cs_nbfree, 1);
	cylno = ufs_cbtocylno(result);
	fs16_sub(sb, &ubh_cg_blks(ucpi, cylno, ufs_cbtorpos(result)), 1);
	fs32_sub(sb, &ubh_cg_blktot(ucpi, cylno), 1);
	
	UFSD(("EXIT, result %u\n", result))

	return result;
}

static unsigned ufs_bitmap_search (struct super_block * sb,
	struct ufs_cg_private_info * ucpi, unsigned goal, unsigned count)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cylinder_group * ucg;
	unsigned start, length, location, result;
	unsigned possition, fragsize, blockmap, mask;
	
	UFSD(("ENTER, cg %u, goal %u, count %u\n", ucpi->c_cgx, goal, count))

	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first (USPI_UBH);
	ucg = ubh_get_ucg(UCPI_UBH);

	if (goal)
		start = ufs_dtogd(goal) >> 3;
	else
		start = ucpi->c_frotor >> 3;
		
	length = ((uspi->s_fpg + 7) >> 3) - start;
	location = ubh_scanc(UCPI_UBH, ucpi->c_freeoff + start, length,
		(uspi->s_fpb == 8) ? ufs_fragtable_8fpb : ufs_fragtable_other,
		1 << (count - 1 + (uspi->s_fpb & 7))); 
	if (location == 0) {
		length = start + 1;
		location = ubh_scanc(UCPI_UBH, ucpi->c_freeoff, length, 
			(uspi->s_fpb == 8) ? ufs_fragtable_8fpb : ufs_fragtable_other,
			1 << (count - 1 + (uspi->s_fpb & 7)));
		if (location == 0) {
			ufs_error (sb, "ufs_bitmap_search",
			"bitmap corrupted on cg %u, start %u, length %u, count %u, freeoff %u\n",
			ucpi->c_cgx, start, length, count, ucpi->c_freeoff);
			return (unsigned)-1;
		}
		start = 0;
	}
	result = (start + length - location) << 3;
	ucpi->c_frotor = result;

	/*
	 * found the byte in the map
	 */
	blockmap = ubh_blkmap(UCPI_UBH, ucpi->c_freeoff, result);
	fragsize = 0;
	for (possition = 0, mask = 1; possition < 8; possition++, mask <<= 1) {
		if (blockmap & mask) {
			if (!(possition & uspi->s_fpbmask))
				fragsize = 1;
			else 
				fragsize++;
		}
		else {
			if (fragsize == count) {
				result += possition - count;
				UFSD(("EXIT, result %u\n", result))
				return result;
			}
			fragsize = 0;
		}
	}
	if (fragsize == count) {
		result += possition - count;
		UFSD(("EXIT, result %u\n", result))
		return result;
	}
	ufs_error (sb, "ufs_bitmap_search", "block not in map on cg %u\n", ucpi->c_cgx);
	UFSD(("EXIT (FAILED)\n"))
	return (unsigned)-1;
}

static void ufs_clusteracct(struct super_block * sb,
	struct ufs_cg_private_info * ucpi, unsigned blkno, int cnt)
{
	struct ufs_sb_private_info * uspi;
	int i, start, end, forw, back;
	
	uspi = UFS_SB(sb)->s_uspi;
	if (uspi->s_contigsumsize <= 0)
		return;

	if (cnt > 0)
		ubh_setbit(UCPI_UBH, ucpi->c_clusteroff, blkno);
	else
		ubh_clrbit(UCPI_UBH, ucpi->c_clusteroff, blkno);

	/*
	 * Find the size of the cluster going forward.
	 */
	start = blkno + 1;
	end = start + uspi->s_contigsumsize;
	if ( end >= ucpi->c_nclusterblks)
		end = ucpi->c_nclusterblks;
	i = ubh_find_next_zero_bit (UCPI_UBH, ucpi->c_clusteroff, end, start);
	if (i > end)
		i = end;
	forw = i - start;
	
	/*
	 * Find the size of the cluster going backward.
	 */
	start = blkno - 1;
	end = start - uspi->s_contigsumsize;
	if (end < 0 ) 
		end = -1;
	i = ubh_find_last_zero_bit (UCPI_UBH, ucpi->c_clusteroff, start, end);
	if ( i < end) 
		i = end;
	back = start - i;
	
	/*
	 * Account for old cluster and the possibly new forward and
	 * back clusters.
	 */
	i = back + forw + 1;
	if (i > uspi->s_contigsumsize)
		i = uspi->s_contigsumsize;
	fs32_add(sb, (__fs32*)ubh_get_addr(UCPI_UBH, ucpi->c_clustersumoff + (i << 2)), cnt);
	if (back > 0)
		fs32_sub(sb, (__fs32*)ubh_get_addr(UCPI_UBH, ucpi->c_clustersumoff + (back << 2)), cnt);
	if (forw > 0)
		fs32_sub(sb, (__fs32*)ubh_get_addr(UCPI_UBH, ucpi->c_clustersumoff + (forw << 2)), cnt);
}


static unsigned char ufs_fragtable_8fpb[] = {
	0x00, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x04, 0x01, 0x01, 0x01, 0x03, 0x02, 0x03, 0x04, 0x08,
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x02, 0x03, 0x03, 0x02, 0x04, 0x05, 0x08, 0x10,
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x05, 0x09,
	0x02, 0x03, 0x03, 0x02, 0x03, 0x03, 0x02, 0x06, 0x04, 0x05, 0x05, 0x06, 0x08, 0x09, 0x10, 0x20,
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x05, 0x09,	
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x03, 0x03, 0x03, 0x03, 0x05, 0x05, 0x09, 0x11,
	0x02, 0x03, 0x03, 0x02, 0x03, 0x03, 0x02, 0x06, 0x03, 0x03, 0x03, 0x03, 0x02, 0x03, 0x06, 0x0A,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x05, 0x06, 0x04, 0x08, 0x09, 0x09, 0x0A, 0x10, 0x11, 0x20, 0x40,
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x05, 0x09,
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x03, 0x03, 0x03, 0x03, 0x05, 0x05, 0x09, 0x11,
	0x01, 0x01, 0x01, 0x03, 0x01, 0x01, 0x03, 0x05, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x05, 0x09,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x05, 0x05, 0x05, 0x07, 0x09, 0x09, 0x11, 0x21,
	0x02, 0x03, 0x03, 0x02, 0x03, 0x03, 0x02, 0x06, 0x03, 0x03, 0x03, 0x03, 0x02, 0x03, 0x06, 0x0A,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x02, 0x03, 0x03, 0x02, 0x06, 0x07, 0x0A, 0x12,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x05, 0x06, 0x04, 0x05, 0x05, 0x05, 0x07, 0x06, 0x07, 0x04, 0x0C,
	0x08, 0x09, 0x09, 0x0A, 0x09, 0x09, 0x0A, 0x0C, 0x10, 0x11, 0x11, 0x12, 0x20, 0x21, 0x40, 0x80,
};

static unsigned char ufs_fragtable_other[] = {
	0x00, 0x16, 0x16, 0x2A, 0x16, 0x16, 0x26, 0x4E, 0x16, 0x16, 0x16, 0x3E, 0x2A, 0x3E, 0x4E, 0x8A,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x2A, 0x3E, 0x3E, 0x2A, 0x3E, 0x3E, 0x2E, 0x6E, 0x3E, 0x3E, 0x3E, 0x3E, 0x2A, 0x3E, 0x6E, 0xAA,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x26, 0x36, 0x36, 0x2E, 0x36, 0x36, 0x26, 0x6E, 0x36, 0x36, 0x36, 0x3E, 0x2E, 0x3E, 0x6E, 0xAE,
	0x4E, 0x5E, 0x5E, 0x6E, 0x5E, 0x5E, 0x6E, 0x4E, 0x5E, 0x5E, 0x5E, 0x7E, 0x6E, 0x7E, 0x4E, 0xCE,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x16, 0x16, 0x16, 0x3E, 0x16, 0x16, 0x36, 0x5E, 0x16, 0x16, 0x16, 0x3E, 0x3E, 0x3E, 0x5E, 0x9E,
	0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x7E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x7E, 0xBE,
	0x2A, 0x3E, 0x3E, 0x2A, 0x3E, 0x3E, 0x2E, 0x6E, 0x3E, 0x3E, 0x3E, 0x3E, 0x2A, 0x3E, 0x6E, 0xAA,
	0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x7E,	0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x7E, 0xBE,
	0x4E, 0x5E, 0x5E, 0x6E, 0x5E, 0x5E, 0x6E, 0x4E, 0x5E, 0x5E, 0x5E, 0x7E, 0x6E, 0x7E, 0x4E, 0xCE,
	0x8A, 0x9E, 0x9E, 0xAA, 0x9E, 0x9E, 0xAE, 0xCE, 0x9E, 0x9E, 0x9E, 0xBE, 0xAA, 0xBE, 0xCE, 0x8A,
};
