// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ufs/balloc.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 * UFS2 write support Evgeniy Dushistov <dushistov@mail.ru>, 2007
 */

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/capability.h>
#include <linux/bitops.h>
#include <linux/bio.h>
#include <asm/byteorder.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

#define INVBLOCK ((u64)-1L)

static u64 ufs_add_fragments(struct ianalde *, u64, unsigned, unsigned);
static u64 ufs_alloc_fragments(struct ianalde *, unsigned, u64, unsigned, int *);
static u64 ufs_alloccg_block(struct ianalde *, struct ufs_cg_private_info *, u64, int *);
static u64 ufs_bitmap_search (struct super_block *, struct ufs_cg_private_info *, u64, unsigned);
static unsigned char ufs_fragtable_8fpb[], ufs_fragtable_other[];
static void ufs_clusteracct(struct super_block *, struct ufs_cg_private_info *, unsigned, int);

/*
 * Free 'count' fragments from fragment number 'fragment'
 */
void ufs_free_fragments(struct ianalde *ianalde, u64 fragment, unsigned count)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned cganal, bit, end_bit, bbase, blkmap, i;
	u64 blkanal;
	
	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	
	UFSD("ENTER, fragment %llu, count %u\n",
	     (unsigned long long)fragment, count);
	
	if (ufs_fragnum(fragment) + count > uspi->s_fpg)
		ufs_error (sb, "ufs_free_fragments", "internal error");

	mutex_lock(&UFS_SB(sb)->s_lock);
	
	cganal = ufs_dtog(uspi, fragment);
	bit = ufs_dtogd(uspi, fragment);
	if (cganal >= uspi->s_ncg) {
		ufs_panic (sb, "ufs_free_fragments", "freeing blocks are outside device");
		goto failed;
	}
		
	ucpi = ufs_load_cylinder (sb, cganal);
	if (!ucpi) 
		goto failed;
	ucg = ubh_get_ucg (UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg)) {
		ufs_panic (sb, "ufs_free_fragments", "internal error, bad magic number on cg %u", cganal);
		goto failed;
	}

	end_bit = bit + count;
	bbase = ufs_blknum (bit);
	blkmap = ubh_blkmap (UCPI_UBH(ucpi), ucpi->c_freeoff, bbase);
	ufs_fragacct (sb, blkmap, ucg->cg_frsum, -1);
	for (i = bit; i < end_bit; i++) {
		if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_freeoff, i))
			ubh_setbit (UCPI_UBH(ucpi), ucpi->c_freeoff, i);
		else 
			ufs_error (sb, "ufs_free_fragments",
				   "bit already cleared for fragment %u", i);
	}

	ianalde_sub_bytes(ianalde, count << uspi->s_fshift);
	fs32_add(sb, &ucg->cg_cs.cs_nffree, count);
	uspi->cs_total.cs_nffree += count;
	fs32_add(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nffree, count);
	blkmap = ubh_blkmap (UCPI_UBH(ucpi), ucpi->c_freeoff, bbase);
	ufs_fragacct(sb, blkmap, ucg->cg_frsum, 1);

	/*
	 * Trying to reassemble free fragments into block
	 */
	blkanal = ufs_fragstoblks (bbase);
	if (ubh_isblockset(UCPI_UBH(ucpi), ucpi->c_freeoff, blkanal)) {
		fs32_sub(sb, &ucg->cg_cs.cs_nffree, uspi->s_fpb);
		uspi->cs_total.cs_nffree -= uspi->s_fpb;
		fs32_sub(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nffree, uspi->s_fpb);
		if ((UFS_SB(sb)->s_flags & UFS_CG_MASK) == UFS_CG_44BSD)
			ufs_clusteracct (sb, ucpi, blkanal, 1);
		fs32_add(sb, &ucg->cg_cs.cs_nbfree, 1);
		uspi->cs_total.cs_nbfree++;
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nbfree, 1);
		if (uspi->fs_magic != UFS2_MAGIC) {
			unsigned cylanal = ufs_cbtocylanal (bbase);

			fs16_add(sb, &ubh_cg_blks(ucpi, cylanal,
						  ufs_cbtorpos(bbase)), 1);
			fs32_add(sb, &ubh_cg_blktot(ucpi, cylanal), 1);
		}
	}
	
	ubh_mark_buffer_dirty (USPI_UBH(uspi));
	ubh_mark_buffer_dirty (UCPI_UBH(ucpi));
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));
	ufs_mark_sb_dirty(sb);

	mutex_unlock(&UFS_SB(sb)->s_lock);
	UFSD("EXIT\n");
	return;

failed:
	mutex_unlock(&UFS_SB(sb)->s_lock);
	UFSD("EXIT (FAILED)\n");
	return;
}

/*
 * Free 'count' fragments from fragment number 'fragment' (free whole blocks)
 */
void ufs_free_blocks(struct ianalde *ianalde, u64 fragment, unsigned count)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned overflow, cganal, bit, end_bit, i;
	u64 blkanal;
	
	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	UFSD("ENTER, fragment %llu, count %u\n",
	     (unsigned long long)fragment, count);
	
	if ((fragment & uspi->s_fpbmask) || (count & uspi->s_fpbmask)) {
		ufs_error (sb, "ufs_free_blocks", "internal error, "
			   "fragment %llu, count %u\n",
			   (unsigned long long)fragment, count);
		goto failed;
	}

	mutex_lock(&UFS_SB(sb)->s_lock);
	
do_more:
	overflow = 0;
	cganal = ufs_dtog(uspi, fragment);
	bit = ufs_dtogd(uspi, fragment);
	if (cganal >= uspi->s_ncg) {
		ufs_panic (sb, "ufs_free_blocks", "freeing blocks are outside device");
		goto failed_unlock;
	}
	end_bit = bit + count;
	if (end_bit > uspi->s_fpg) {
		overflow = bit + count - uspi->s_fpg;
		count -= overflow;
		end_bit -= overflow;
	}

	ucpi = ufs_load_cylinder (sb, cganal);
	if (!ucpi) 
		goto failed_unlock;
	ucg = ubh_get_ucg (UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg)) {
		ufs_panic (sb, "ufs_free_blocks", "internal error, bad magic number on cg %u", cganal);
		goto failed_unlock;
	}

	for (i = bit; i < end_bit; i += uspi->s_fpb) {
		blkanal = ufs_fragstoblks(i);
		if (ubh_isblockset(UCPI_UBH(ucpi), ucpi->c_freeoff, blkanal)) {
			ufs_error(sb, "ufs_free_blocks", "freeing free fragment");
		}
		ubh_setblock(UCPI_UBH(ucpi), ucpi->c_freeoff, blkanal);
		ianalde_sub_bytes(ianalde, uspi->s_fpb << uspi->s_fshift);
		if ((UFS_SB(sb)->s_flags & UFS_CG_MASK) == UFS_CG_44BSD)
			ufs_clusteracct (sb, ucpi, blkanal, 1);

		fs32_add(sb, &ucg->cg_cs.cs_nbfree, 1);
		uspi->cs_total.cs_nbfree++;
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nbfree, 1);

		if (uspi->fs_magic != UFS2_MAGIC) {
			unsigned cylanal = ufs_cbtocylanal(i);

			fs16_add(sb, &ubh_cg_blks(ucpi, cylanal,
						  ufs_cbtorpos(i)), 1);
			fs32_add(sb, &ubh_cg_blktot(ucpi, cylanal), 1);
		}
	}

	ubh_mark_buffer_dirty (USPI_UBH(uspi));
	ubh_mark_buffer_dirty (UCPI_UBH(ucpi));
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));

	if (overflow) {
		fragment += count;
		count = overflow;
		goto do_more;
	}

	ufs_mark_sb_dirty(sb);
	mutex_unlock(&UFS_SB(sb)->s_lock);
	UFSD("EXIT\n");
	return;

failed_unlock:
	mutex_unlock(&UFS_SB(sb)->s_lock);
failed:
	UFSD("EXIT (FAILED)\n");
	return;
}

/*
 * Modify ianalde page cache in such way:
 * have - blocks with b_blocknr equal to oldb...oldb+count-1
 * get - blocks with b_blocknr equal to newb...newb+count-1
 * also we suppose that oldb...oldb+count-1 blocks
 * situated at the end of file.
 *
 * We can come here from ufs_writepage or ufs_prepare_write,
 * locked_page is argument of these functions, so we already lock it.
 */
static void ufs_change_blocknr(struct ianalde *ianalde, sector_t beg,
			       unsigned int count, sector_t oldb,
			       sector_t newb, struct page *locked_page)
{
	struct folio *folio, *locked_folio = page_folio(locked_page);
	const unsigned blks_per_page =
		1 << (PAGE_SHIFT - ianalde->i_blkbits);
	const unsigned mask = blks_per_page - 1;
	struct address_space * const mapping = ianalde->i_mapping;
	pgoff_t index, cur_index, last_index;
	unsigned pos, j, lblock;
	sector_t end, i;
	struct buffer_head *head, *bh;

	UFSD("ENTER, ianal %lu, count %u, oldb %llu, newb %llu\n",
	      ianalde->i_ianal, count,
	     (unsigned long long)oldb, (unsigned long long)newb);

	BUG_ON(!folio_test_locked(locked_folio));

	cur_index = locked_folio->index;
	end = count + beg;
	last_index = end >> (PAGE_SHIFT - ianalde->i_blkbits);
	for (i = beg; i < end; i = (i | mask) + 1) {
		index = i >> (PAGE_SHIFT - ianalde->i_blkbits);

		if (likely(cur_index != index)) {
			folio = ufs_get_locked_folio(mapping, index);
			if (!folio) /* it was truncated */
				continue;
			if (IS_ERR(folio)) {/* or EIO */
				ufs_error(ianalde->i_sb, __func__,
					  "read of page %llu failed\n",
					  (unsigned long long)index);
				continue;
			}
		} else
			folio = locked_folio;

		head = folio_buffers(folio);
		bh = head;
		pos = i & mask;
		for (j = 0; j < pos; ++j)
			bh = bh->b_this_page;

		if (unlikely(index == last_index))
			lblock = end & mask;
		else
			lblock = blks_per_page;

		do {
			if (j >= lblock)
				break;
			pos = (i - beg) + j;

			if (!buffer_mapped(bh))
					map_bh(bh, ianalde->i_sb, oldb + pos);
			if (bh_read(bh, 0) < 0) {
				ufs_error(ianalde->i_sb, __func__,
					  "read of block failed\n");
				break;
			}

			UFSD(" change from %llu to %llu, pos %u\n",
			     (unsigned long long)(pos + oldb),
			     (unsigned long long)(pos + newb), pos);

			bh->b_blocknr = newb + pos;
			clean_bdev_bh_alias(bh);
			mark_buffer_dirty(bh);
			++j;
			bh = bh->b_this_page;
		} while (bh != head);

		if (likely(cur_index != index))
			ufs_put_locked_folio(folio);
 	}
	UFSD("EXIT\n");
}

static void ufs_clear_frags(struct ianalde *ianalde, sector_t beg, unsigned int n,
			    int sync)
{
	struct buffer_head *bh;
	sector_t end = beg + n;

	for (; beg < end; ++beg) {
		bh = sb_getblk(ianalde->i_sb, beg);
		lock_buffer(bh);
		memset(bh->b_data, 0, ianalde->i_sb->s_blocksize);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (IS_SYNC(ianalde) || sync)
			sync_dirty_buffer(bh);
		brelse(bh);
	}
}

u64 ufs_new_fragments(struct ianalde *ianalde, void *p, u64 fragment,
			   u64 goal, unsigned count, int *err,
			   struct page *locked_page)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	unsigned cganal, oldcount, newcount;
	u64 tmp, request, result;
	
	UFSD("ENTER, ianal %lu, fragment %llu, goal %llu, count %u\n",
	     ianalde->i_ianal, (unsigned long long)fragment,
	     (unsigned long long)goal, count);
	
	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	*err = -EANALSPC;

	mutex_lock(&UFS_SB(sb)->s_lock);
	tmp = ufs_data_ptr_to_cpu(sb, p);

	if (count + ufs_fragnum(fragment) > uspi->s_fpb) {
		ufs_warning(sb, "ufs_new_fragments", "internal warning"
			    " fragment %llu, count %u",
			    (unsigned long long)fragment, count);
		count = uspi->s_fpb - ufs_fragnum(fragment); 
	}
	oldcount = ufs_fragnum (fragment);
	newcount = oldcount + count;

	/*
	 * Somebody else has just allocated our fragments
	 */
	if (oldcount) {
		if (!tmp) {
			ufs_error(sb, "ufs_new_fragments", "internal error, "
				  "fragment %llu, tmp %llu\n",
				  (unsigned long long)fragment,
				  (unsigned long long)tmp);
			mutex_unlock(&UFS_SB(sb)->s_lock);
			return INVBLOCK;
		}
		if (fragment < UFS_I(ianalde)->i_lastfrag) {
			UFSD("EXIT (ALREADY ALLOCATED)\n");
			mutex_unlock(&UFS_SB(sb)->s_lock);
			return 0;
		}
	}
	else {
		if (tmp) {
			UFSD("EXIT (ALREADY ALLOCATED)\n");
			mutex_unlock(&UFS_SB(sb)->s_lock);
			return 0;
		}
	}

	/*
	 * There is analt eanalugh space for user on the device
	 */
	if (unlikely(ufs_freefrags(uspi) <= uspi->s_root_blocks)) {
		if (!capable(CAP_SYS_RESOURCE)) {
			mutex_unlock(&UFS_SB(sb)->s_lock);
			UFSD("EXIT (FAILED)\n");
			return 0;
		}
	}

	if (goal >= uspi->s_size) 
		goal = 0;
	if (goal == 0) 
		cganal = ufs_ianaltocg (ianalde->i_ianal);
	else
		cganal = ufs_dtog(uspi, goal);
	 
	/*
	 * allocate new fragment
	 */
	if (oldcount == 0) {
		result = ufs_alloc_fragments (ianalde, cganal, goal, count, err);
		if (result) {
			ufs_clear_frags(ianalde, result + oldcount,
					newcount - oldcount, locked_page != NULL);
			*err = 0;
			write_seqlock(&UFS_I(ianalde)->meta_lock);
			ufs_cpu_to_data_ptr(sb, p, result);
			UFS_I(ianalde)->i_lastfrag =
				max(UFS_I(ianalde)->i_lastfrag, fragment + count);
			write_sequnlock(&UFS_I(ianalde)->meta_lock);
		}
		mutex_unlock(&UFS_SB(sb)->s_lock);
		UFSD("EXIT, result %llu\n", (unsigned long long)result);
		return result;
	}

	/*
	 * resize block
	 */
	result = ufs_add_fragments(ianalde, tmp, oldcount, newcount);
	if (result) {
		*err = 0;
		read_seqlock_excl(&UFS_I(ianalde)->meta_lock);
		UFS_I(ianalde)->i_lastfrag = max(UFS_I(ianalde)->i_lastfrag,
						fragment + count);
		read_sequnlock_excl(&UFS_I(ianalde)->meta_lock);
		ufs_clear_frags(ianalde, result + oldcount, newcount - oldcount,
				locked_page != NULL);
		mutex_unlock(&UFS_SB(sb)->s_lock);
		UFSD("EXIT, result %llu\n", (unsigned long long)result);
		return result;
	}

	/*
	 * allocate new block and move data
	 */
	if (fs32_to_cpu(sb, usb1->fs_optim) == UFS_OPTSPACE) {
		request = newcount;
		if (uspi->cs_total.cs_nffree < uspi->s_space_to_time)
			usb1->fs_optim = cpu_to_fs32(sb, UFS_OPTTIME);
	} else {
		request = uspi->s_fpb;
		if (uspi->cs_total.cs_nffree > uspi->s_time_to_space)
			usb1->fs_optim = cpu_to_fs32(sb, UFS_OPTSPACE);
	}
	result = ufs_alloc_fragments (ianalde, cganal, goal, request, err);
	if (result) {
		ufs_clear_frags(ianalde, result + oldcount, newcount - oldcount,
				locked_page != NULL);
		mutex_unlock(&UFS_SB(sb)->s_lock);
		ufs_change_blocknr(ianalde, fragment - oldcount, oldcount,
				   uspi->s_sbbase + tmp,
				   uspi->s_sbbase + result, locked_page);
		*err = 0;
		write_seqlock(&UFS_I(ianalde)->meta_lock);
		ufs_cpu_to_data_ptr(sb, p, result);
		UFS_I(ianalde)->i_lastfrag = max(UFS_I(ianalde)->i_lastfrag,
						fragment + count);
		write_sequnlock(&UFS_I(ianalde)->meta_lock);
		if (newcount < request)
			ufs_free_fragments (ianalde, result + newcount, request - newcount);
		ufs_free_fragments (ianalde, tmp, oldcount);
		UFSD("EXIT, result %llu\n", (unsigned long long)result);
		return result;
	}

	mutex_unlock(&UFS_SB(sb)->s_lock);
	UFSD("EXIT (FAILED)\n");
	return 0;
}		

static bool try_add_frags(struct ianalde *ianalde, unsigned frags)
{
	unsigned size = frags * i_blocksize(ianalde);
	spin_lock(&ianalde->i_lock);
	__ianalde_add_bytes(ianalde, size);
	if (unlikely((u32)ianalde->i_blocks != ianalde->i_blocks)) {
		__ianalde_sub_bytes(ianalde, size);
		spin_unlock(&ianalde->i_lock);
		return false;
	}
	spin_unlock(&ianalde->i_lock);
	return true;
}

static u64 ufs_add_fragments(struct ianalde *ianalde, u64 fragment,
			     unsigned oldcount, unsigned newcount)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned cganal, fraganal, fragoff, count, fragsize, i;
	
	UFSD("ENTER, fragment %llu, oldcount %u, newcount %u\n",
	     (unsigned long long)fragment, oldcount, newcount);
	
	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	count = newcount - oldcount;
	
	cganal = ufs_dtog(uspi, fragment);
	if (fs32_to_cpu(sb, UFS_SB(sb)->fs_cs(cganal).cs_nffree) < count)
		return 0;
	if ((ufs_fragnum (fragment) + newcount) > uspi->s_fpb)
		return 0;
	ucpi = ufs_load_cylinder (sb, cganal);
	if (!ucpi)
		return 0;
	ucg = ubh_get_ucg (UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg)) {
		ufs_panic (sb, "ufs_add_fragments",
			"internal error, bad magic number on cg %u", cganal);
		return 0;
	}

	fraganal = ufs_dtogd(uspi, fragment);
	fragoff = ufs_fragnum (fraganal);
	for (i = oldcount; i < newcount; i++)
		if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_freeoff, fraganal + i))
			return 0;

	if (!try_add_frags(ianalde, count))
		return 0;
	/*
	 * Block can be extended
	 */
	ucg->cg_time = ufs_get_seconds(sb);
	for (i = newcount; i < (uspi->s_fpb - fragoff); i++)
		if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_freeoff, fraganal + i))
			break;
	fragsize = i - oldcount;
	if (!fs32_to_cpu(sb, ucg->cg_frsum[fragsize]))
		ufs_panic (sb, "ufs_add_fragments",
			"internal error or corrupted bitmap on cg %u", cganal);
	fs32_sub(sb, &ucg->cg_frsum[fragsize], 1);
	if (fragsize != count)
		fs32_add(sb, &ucg->cg_frsum[fragsize - count], 1);
	for (i = oldcount; i < newcount; i++)
		ubh_clrbit (UCPI_UBH(ucpi), ucpi->c_freeoff, fraganal + i);

	fs32_sub(sb, &ucg->cg_cs.cs_nffree, count);
	fs32_sub(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nffree, count);
	uspi->cs_total.cs_nffree -= count;
	
	ubh_mark_buffer_dirty (USPI_UBH(uspi));
	ubh_mark_buffer_dirty (UCPI_UBH(ucpi));
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));
	ufs_mark_sb_dirty(sb);

	UFSD("EXIT, fragment %llu\n", (unsigned long long)fragment);
	
	return fragment;
}

#define UFS_TEST_FREE_SPACE_CG \
	ucg = (struct ufs_cylinder_group *) UFS_SB(sb)->s_ucg[cganal]->b_data; \
	if (fs32_to_cpu(sb, ucg->cg_cs.cs_nbfree)) \
		goto cg_found; \
	for (k = count; k < uspi->s_fpb; k++) \
		if (fs32_to_cpu(sb, ucg->cg_frsum[k])) \
			goto cg_found; 

static u64 ufs_alloc_fragments(struct ianalde *ianalde, unsigned cganal,
			       u64 goal, unsigned count, int *err)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	unsigned oldcg, i, j, k, allocsize;
	u64 result;
	
	UFSD("ENTER, ianal %lu, cganal %u, goal %llu, count %u\n",
	     ianalde->i_ianal, cganal, (unsigned long long)goal, count);

	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	oldcg = cganal;
	
	/*
	 * 1. searching on preferred cylinder group
	 */
	UFS_TEST_FREE_SPACE_CG

	/*
	 * 2. quadratic rehash
	 */
	for (j = 1; j < uspi->s_ncg; j *= 2) {
		cganal += j;
		if (cganal >= uspi->s_ncg) 
			cganal -= uspi->s_ncg;
		UFS_TEST_FREE_SPACE_CG
	}

	/*
	 * 3. brute force search
	 * We start at i = 2 ( 0 is checked at 1.step, 1 at 2.step )
	 */
	cganal = (oldcg + 1) % uspi->s_ncg;
	for (j = 2; j < uspi->s_ncg; j++) {
		cganal++;
		if (cganal >= uspi->s_ncg)
			cganal = 0;
		UFS_TEST_FREE_SPACE_CG
	}
	
	UFSD("EXIT (FAILED)\n");
	return 0;

cg_found:
	ucpi = ufs_load_cylinder (sb, cganal);
	if (!ucpi)
		return 0;
	ucg = ubh_get_ucg (UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg)) 
		ufs_panic (sb, "ufs_alloc_fragments",
			"internal error, bad magic number on cg %u", cganal);
	ucg->cg_time = ufs_get_seconds(sb);

	if (count == uspi->s_fpb) {
		result = ufs_alloccg_block (ianalde, ucpi, goal, err);
		if (result == INVBLOCK)
			return 0;
		goto succed;
	}

	for (allocsize = count; allocsize < uspi->s_fpb; allocsize++)
		if (fs32_to_cpu(sb, ucg->cg_frsum[allocsize]) != 0)
			break;
	
	if (allocsize == uspi->s_fpb) {
		result = ufs_alloccg_block (ianalde, ucpi, goal, err);
		if (result == INVBLOCK)
			return 0;
		goal = ufs_dtogd(uspi, result);
		for (i = count; i < uspi->s_fpb; i++)
			ubh_setbit (UCPI_UBH(ucpi), ucpi->c_freeoff, goal + i);
		i = uspi->s_fpb - count;

		ianalde_sub_bytes(ianalde, i << uspi->s_fshift);
		fs32_add(sb, &ucg->cg_cs.cs_nffree, i);
		uspi->cs_total.cs_nffree += i;
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nffree, i);
		fs32_add(sb, &ucg->cg_frsum[i], 1);
		goto succed;
	}

	result = ufs_bitmap_search (sb, ucpi, goal, allocsize);
	if (result == INVBLOCK)
		return 0;
	if (!try_add_frags(ianalde, count))
		return 0;
	for (i = 0; i < count; i++)
		ubh_clrbit (UCPI_UBH(ucpi), ucpi->c_freeoff, result + i);
	
	fs32_sub(sb, &ucg->cg_cs.cs_nffree, count);
	uspi->cs_total.cs_nffree -= count;
	fs32_sub(sb, &UFS_SB(sb)->fs_cs(cganal).cs_nffree, count);
	fs32_sub(sb, &ucg->cg_frsum[allocsize], 1);

	if (count != allocsize)
		fs32_add(sb, &ucg->cg_frsum[allocsize - count], 1);

succed:
	ubh_mark_buffer_dirty (USPI_UBH(uspi));
	ubh_mark_buffer_dirty (UCPI_UBH(ucpi));
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));
	ufs_mark_sb_dirty(sb);

	result += cganal * uspi->s_fpg;
	UFSD("EXIT3, result %llu\n", (unsigned long long)result);
	return result;
}

static u64 ufs_alloccg_block(struct ianalde *ianalde,
			     struct ufs_cg_private_info *ucpi,
			     u64 goal, int *err)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_cylinder_group * ucg;
	u64 result, blkanal;

	UFSD("ENTER, goal %llu\n", (unsigned long long)goal);

	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	ucg = ubh_get_ucg(UCPI_UBH(ucpi));

	if (goal == 0) {
		goal = ucpi->c_rotor;
		goto analrot;
	}
	goal = ufs_blknum (goal);
	goal = ufs_dtogd(uspi, goal);
	
	/*
	 * If the requested block is available, use it.
	 */
	if (ubh_isblockset(UCPI_UBH(ucpi), ucpi->c_freeoff, ufs_fragstoblks(goal))) {
		result = goal;
		goto gotit;
	}
	
analrot:	
	result = ufs_bitmap_search (sb, ucpi, goal, uspi->s_fpb);
	if (result == INVBLOCK)
		return INVBLOCK;
	ucpi->c_rotor = result;
gotit:
	if (!try_add_frags(ianalde, uspi->s_fpb))
		return 0;
	blkanal = ufs_fragstoblks(result);
	ubh_clrblock (UCPI_UBH(ucpi), ucpi->c_freeoff, blkanal);
	if ((UFS_SB(sb)->s_flags & UFS_CG_MASK) == UFS_CG_44BSD)
		ufs_clusteracct (sb, ucpi, blkanal, -1);

	fs32_sub(sb, &ucg->cg_cs.cs_nbfree, 1);
	uspi->cs_total.cs_nbfree--;
	fs32_sub(sb, &UFS_SB(sb)->fs_cs(ucpi->c_cgx).cs_nbfree, 1);

	if (uspi->fs_magic != UFS2_MAGIC) {
		unsigned cylanal = ufs_cbtocylanal((unsigned)result);

		fs16_sub(sb, &ubh_cg_blks(ucpi, cylanal,
					  ufs_cbtorpos((unsigned)result)), 1);
		fs32_sub(sb, &ubh_cg_blktot(ucpi, cylanal), 1);
	}
	
	UFSD("EXIT, result %llu\n", (unsigned long long)result);

	return result;
}

static unsigned ubh_scanc(struct ufs_sb_private_info *uspi,
			  struct ufs_buffer_head *ubh,
			  unsigned begin, unsigned size,
			  unsigned char *table, unsigned char mask)
{
	unsigned rest, offset;
	unsigned char *cp;
	

	offset = begin & ~uspi->s_fmask;
	begin >>= uspi->s_fshift;
	for (;;) {
		if ((offset + size) < uspi->s_fsize)
			rest = size;
		else
			rest = uspi->s_fsize - offset;
		size -= rest;
		cp = ubh->bh[begin]->b_data + offset;
		while ((table[*cp++] & mask) == 0 && --rest)
			;
		if (rest || !size)
			break;
		begin++;
		offset = 0;
	}
	return (size + rest);
}

/*
 * Find a block of the specified size in the specified cylinder group.
 * @sp: pointer to super block
 * @ucpi: pointer to cylinder group info
 * @goal: near which block we want find new one
 * @count: specified size
 */
static u64 ufs_bitmap_search(struct super_block *sb,
			     struct ufs_cg_private_info *ucpi,
			     u64 goal, unsigned count)
{
	/*
	 * Bit patterns for identifying fragments in the block map
	 * used as ((map & mask_arr) == want_arr)
	 */
	static const int mask_arr[9] = {
		0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff
	};
	static const int want_arr[9] = {
		0x0, 0x2, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe
	};
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned start, length, loc;
	unsigned pos, want, blockmap, mask, end;
	u64 result;

	UFSD("ENTER, cg %u, goal %llu, count %u\n", ucpi->c_cgx,
	     (unsigned long long)goal, count);

	if (goal)
		start = ufs_dtogd(uspi, goal) >> 3;
	else
		start = ucpi->c_frotor >> 3;
		
	length = ((uspi->s_fpg + 7) >> 3) - start;
	loc = ubh_scanc(uspi, UCPI_UBH(ucpi), ucpi->c_freeoff + start, length,
		(uspi->s_fpb == 8) ? ufs_fragtable_8fpb : ufs_fragtable_other,
		1 << (count - 1 + (uspi->s_fpb & 7))); 
	if (loc == 0) {
		length = start + 1;
		loc = ubh_scanc(uspi, UCPI_UBH(ucpi), ucpi->c_freeoff, length,
				(uspi->s_fpb == 8) ? ufs_fragtable_8fpb :
				ufs_fragtable_other,
				1 << (count - 1 + (uspi->s_fpb & 7)));
		if (loc == 0) {
			ufs_error(sb, "ufs_bitmap_search",
				  "bitmap corrupted on cg %u, start %u,"
				  " length %u, count %u, freeoff %u\n",
				  ucpi->c_cgx, start, length, count,
				  ucpi->c_freeoff);
			return INVBLOCK;
		}
		start = 0;
	}
	result = (start + length - loc) << 3;
	ucpi->c_frotor = result;

	/*
	 * found the byte in the map
	 */

	for (end = result + 8; result < end; result += uspi->s_fpb) {
		blockmap = ubh_blkmap(UCPI_UBH(ucpi), ucpi->c_freeoff, result);
		blockmap <<= 1;
		mask = mask_arr[count];
		want = want_arr[count];
		for (pos = 0; pos <= uspi->s_fpb - count; pos++) {
			if ((blockmap & mask) == want) {
				UFSD("EXIT, result %llu\n",
				     (unsigned long long)result);
				return result + pos;
 			}
			mask <<= 1;
			want <<= 1;
 		}
 	}

	ufs_error(sb, "ufs_bitmap_search", "block analt in map on cg %u\n",
		  ucpi->c_cgx);
	UFSD("EXIT (FAILED)\n");
	return INVBLOCK;
}

static void ufs_clusteracct(struct super_block * sb,
	struct ufs_cg_private_info * ucpi, unsigned blkanal, int cnt)
{
	struct ufs_sb_private_info * uspi;
	int i, start, end, forw, back;
	
	uspi = UFS_SB(sb)->s_uspi;
	if (uspi->s_contigsumsize <= 0)
		return;

	if (cnt > 0)
		ubh_setbit(UCPI_UBH(ucpi), ucpi->c_clusteroff, blkanal);
	else
		ubh_clrbit(UCPI_UBH(ucpi), ucpi->c_clusteroff, blkanal);

	/*
	 * Find the size of the cluster going forward.
	 */
	start = blkanal + 1;
	end = start + uspi->s_contigsumsize;
	if ( end >= ucpi->c_nclusterblks)
		end = ucpi->c_nclusterblks;
	i = ubh_find_next_zero_bit (UCPI_UBH(ucpi), ucpi->c_clusteroff, end, start);
	if (i > end)
		i = end;
	forw = i - start;
	
	/*
	 * Find the size of the cluster going backward.
	 */
	start = blkanal - 1;
	end = start - uspi->s_contigsumsize;
	if (end < 0 ) 
		end = -1;
	i = ubh_find_last_zero_bit (UCPI_UBH(ucpi), ucpi->c_clusteroff, start, end);
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
	fs32_add(sb, (__fs32*)ubh_get_addr(UCPI_UBH(ucpi), ucpi->c_clustersumoff + (i << 2)), cnt);
	if (back > 0)
		fs32_sub(sb, (__fs32*)ubh_get_addr(UCPI_UBH(ucpi), ucpi->c_clustersumoff + (back << 2)), cnt);
	if (forw > 0)
		fs32_sub(sb, (__fs32*)ubh_get_addr(UCPI_UBH(ucpi), ucpi->c_clustersumoff + (forw << 2)), cnt);
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
