/*
 *  linux/fs/ufs/util.h
 *
 * Copyright (C) 1998 
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 */

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include "swab.h"


/*
 * some useful macros
 */
#define in_range(b,first,len)	((b)>=(first)&&(b)<(first)+(len))

/*
 * functions used for retyping
 */
static inline struct ufs_buffer_head *UCPI_UBH(struct ufs_cg_private_info *cpi)
{
	return &cpi->c_ubh;
}
static inline struct ufs_buffer_head *USPI_UBH(struct ufs_sb_private_info *spi)
{
	return &spi->s_ubh;
}



/*
 * macros used for accessing structures
 */
static inline s32
ufs_get_fs_state(struct super_block *sb, struct ufs_super_block_first *usb1,
		 struct ufs_super_block_third *usb3)
{
	switch (UFS_SB(sb)->s_flags & UFS_ST_MASK) {
	case UFS_ST_SUNOS:
		if (fs32_to_cpu(sb, usb3->fs_postblformat) == UFS_42POSTBLFMT)
			return fs32_to_cpu(sb, usb1->fs_u0.fs_sun.fs_state);
		/* Fall Through to UFS_ST_SUN */
	case UFS_ST_SUN:
		return fs32_to_cpu(sb, usb3->fs_un2.fs_sun.fs_state);
	case UFS_ST_SUNx86:
		return fs32_to_cpu(sb, usb1->fs_u1.fs_sunx86.fs_state);
	case UFS_ST_44BSD:
	default:
		return fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_state);
	}
}

static inline void
ufs_set_fs_state(struct super_block *sb, struct ufs_super_block_first *usb1,
		 struct ufs_super_block_third *usb3, s32 value)
{
	switch (UFS_SB(sb)->s_flags & UFS_ST_MASK) {
	case UFS_ST_SUNOS:
		if (fs32_to_cpu(sb, usb3->fs_postblformat) == UFS_42POSTBLFMT) {
			usb1->fs_u0.fs_sun.fs_state = cpu_to_fs32(sb, value);
			break;
		}
		/* Fall Through to UFS_ST_SUN */
	case UFS_ST_SUN:
		usb3->fs_un2.fs_sun.fs_state = cpu_to_fs32(sb, value);
		break;
	case UFS_ST_SUNx86:
		usb1->fs_u1.fs_sunx86.fs_state = cpu_to_fs32(sb, value);
		break;
	case UFS_ST_44BSD:
		usb3->fs_un2.fs_44.fs_state = cpu_to_fs32(sb, value);
		break;
	}
}

static inline u32
ufs_get_fs_npsect(struct super_block *sb, struct ufs_super_block_first *usb1,
		  struct ufs_super_block_third *usb3)
{
	if ((UFS_SB(sb)->s_flags & UFS_ST_MASK) == UFS_ST_SUNx86)
		return fs32_to_cpu(sb, usb3->fs_un2.fs_sunx86.fs_npsect);
	else
		return fs32_to_cpu(sb, usb1->fs_u1.fs_sun.fs_npsect);
}

static inline u64
ufs_get_fs_qbmask(struct super_block *sb, struct ufs_super_block_third *usb3)
{
	__fs64 tmp;

	switch (UFS_SB(sb)->s_flags & UFS_ST_MASK) {
	case UFS_ST_SUNOS:
	case UFS_ST_SUN:
		((__fs32 *)&tmp)[0] = usb3->fs_un2.fs_sun.fs_qbmask[0];
		((__fs32 *)&tmp)[1] = usb3->fs_un2.fs_sun.fs_qbmask[1];
		break;
	case UFS_ST_SUNx86:
		((__fs32 *)&tmp)[0] = usb3->fs_un2.fs_sunx86.fs_qbmask[0];
		((__fs32 *)&tmp)[1] = usb3->fs_un2.fs_sunx86.fs_qbmask[1];
		break;
	case UFS_ST_44BSD:
		((__fs32 *)&tmp)[0] = usb3->fs_un2.fs_44.fs_qbmask[0];
		((__fs32 *)&tmp)[1] = usb3->fs_un2.fs_44.fs_qbmask[1];
		break;
	}

	return fs64_to_cpu(sb, tmp);
}

static inline u64
ufs_get_fs_qfmask(struct super_block *sb, struct ufs_super_block_third *usb3)
{
	__fs64 tmp;

	switch (UFS_SB(sb)->s_flags & UFS_ST_MASK) {
	case UFS_ST_SUNOS:
	case UFS_ST_SUN:
		((__fs32 *)&tmp)[0] = usb3->fs_un2.fs_sun.fs_qfmask[0];
		((__fs32 *)&tmp)[1] = usb3->fs_un2.fs_sun.fs_qfmask[1];
		break;
	case UFS_ST_SUNx86:
		((__fs32 *)&tmp)[0] = usb3->fs_un2.fs_sunx86.fs_qfmask[0];
		((__fs32 *)&tmp)[1] = usb3->fs_un2.fs_sunx86.fs_qfmask[1];
		break;
	case UFS_ST_44BSD:
		((__fs32 *)&tmp)[0] = usb3->fs_un2.fs_44.fs_qfmask[0];
		((__fs32 *)&tmp)[1] = usb3->fs_un2.fs_44.fs_qfmask[1];
		break;
	}

	return fs64_to_cpu(sb, tmp);
}

static inline u16
ufs_get_de_namlen(struct super_block *sb, struct ufs_dir_entry *de)
{
	if ((UFS_SB(sb)->s_flags & UFS_DE_MASK) == UFS_DE_OLD)
		return fs16_to_cpu(sb, de->d_u.d_namlen);
	else
		return de->d_u.d_44.d_namlen; /* XXX this seems wrong */
}

static inline void
ufs_set_de_namlen(struct super_block *sb, struct ufs_dir_entry *de, u16 value)
{
	if ((UFS_SB(sb)->s_flags & UFS_DE_MASK) == UFS_DE_OLD)
		de->d_u.d_namlen = cpu_to_fs16(sb, value);
	else
		de->d_u.d_44.d_namlen = value; /* XXX this seems wrong */
}

static inline void
ufs_set_de_type(struct super_block *sb, struct ufs_dir_entry *de, int mode)
{
	if ((UFS_SB(sb)->s_flags & UFS_DE_MASK) != UFS_DE_44BSD)
		return;

	/*
	 * TODO turn this into a table lookup
	 */
	switch (mode & S_IFMT) {
	case S_IFSOCK:
		de->d_u.d_44.d_type = DT_SOCK;
		break;
	case S_IFLNK:
		de->d_u.d_44.d_type = DT_LNK;
		break;
	case S_IFREG:
		de->d_u.d_44.d_type = DT_REG;
		break;
	case S_IFBLK:
		de->d_u.d_44.d_type = DT_BLK;
		break;
	case S_IFDIR:
		de->d_u.d_44.d_type = DT_DIR;
		break;
	case S_IFCHR:
		de->d_u.d_44.d_type = DT_CHR;
		break;
	case S_IFIFO:
		de->d_u.d_44.d_type = DT_FIFO;
		break;
	default:
		de->d_u.d_44.d_type = DT_UNKNOWN;
	}
}

static inline u32
ufs_get_inode_uid(struct super_block *sb, struct ufs_inode *inode)
{
	switch (UFS_SB(sb)->s_flags & UFS_UID_MASK) {
	case UFS_UID_44BSD:
		return fs32_to_cpu(sb, inode->ui_u3.ui_44.ui_uid);
	case UFS_UID_EFT:
		if (inode->ui_u1.oldids.ui_suid == 0xFFFF)
			return fs32_to_cpu(sb, inode->ui_u3.ui_sun.ui_uid);
		/* Fall through */
	default:
		return fs16_to_cpu(sb, inode->ui_u1.oldids.ui_suid);
	}
}

static inline void
ufs_set_inode_uid(struct super_block *sb, struct ufs_inode *inode, u32 value)
{
	switch (UFS_SB(sb)->s_flags & UFS_UID_MASK) {
	case UFS_UID_44BSD:
		inode->ui_u3.ui_44.ui_uid = cpu_to_fs32(sb, value);
		inode->ui_u1.oldids.ui_suid = cpu_to_fs16(sb, value);
		break;
	case UFS_UID_EFT:
		inode->ui_u3.ui_sun.ui_uid = cpu_to_fs32(sb, value);
		if (value > 0xFFFF)
			value = 0xFFFF;
		/* Fall through */
	default:
		inode->ui_u1.oldids.ui_suid = cpu_to_fs16(sb, value);
		break;
	}
}

static inline u32
ufs_get_inode_gid(struct super_block *sb, struct ufs_inode *inode)
{
	switch (UFS_SB(sb)->s_flags & UFS_UID_MASK) {
	case UFS_UID_44BSD:
		return fs32_to_cpu(sb, inode->ui_u3.ui_44.ui_gid);
	case UFS_UID_EFT:
		if (inode->ui_u1.oldids.ui_suid == 0xFFFF)
			return fs32_to_cpu(sb, inode->ui_u3.ui_sun.ui_gid);
		/* Fall through */
	default:
		return fs16_to_cpu(sb, inode->ui_u1.oldids.ui_sgid);
	}
}

static inline void
ufs_set_inode_gid(struct super_block *sb, struct ufs_inode *inode, u32 value)
{
	switch (UFS_SB(sb)->s_flags & UFS_UID_MASK) {
	case UFS_UID_44BSD:
		inode->ui_u3.ui_44.ui_gid = cpu_to_fs32(sb, value);
		inode->ui_u1.oldids.ui_sgid =  cpu_to_fs16(sb, value);
		break;
	case UFS_UID_EFT:
		inode->ui_u3.ui_sun.ui_gid = cpu_to_fs32(sb, value);
		if (value > 0xFFFF)
			value = 0xFFFF;
		/* Fall through */
	default:
		inode->ui_u1.oldids.ui_sgid =  cpu_to_fs16(sb, value);
		break;
	}
}

extern dev_t ufs_get_inode_dev(struct super_block *, struct ufs_inode_info *);
extern void ufs_set_inode_dev(struct super_block *, struct ufs_inode_info *, dev_t);
extern int ufs_prepare_chunk(struct page *page, loff_t pos, unsigned len);

/*
 * These functions manipulate ufs buffers
 */
#define ubh_bread(sb,fragment,size) _ubh_bread_(uspi,sb,fragment,size)  
extern struct ufs_buffer_head * _ubh_bread_(struct ufs_sb_private_info *, struct super_block *, u64 , u64);
extern struct ufs_buffer_head * ubh_bread_uspi(struct ufs_sb_private_info *, struct super_block *, u64, u64);
extern void ubh_brelse (struct ufs_buffer_head *);
extern void ubh_brelse_uspi (struct ufs_sb_private_info *);
extern void ubh_mark_buffer_dirty (struct ufs_buffer_head *);
extern void ubh_mark_buffer_uptodate (struct ufs_buffer_head *, int);
extern void ubh_sync_block(struct ufs_buffer_head *);
extern void ubh_bforget (struct ufs_buffer_head *);
extern int  ubh_buffer_dirty (struct ufs_buffer_head *);
#define ubh_ubhcpymem(mem,ubh,size) _ubh_ubhcpymem_(uspi,mem,ubh,size)
extern void _ubh_ubhcpymem_(struct ufs_sb_private_info *, unsigned char *, struct ufs_buffer_head *, unsigned);
#define ubh_memcpyubh(ubh,mem,size) _ubh_memcpyubh_(uspi,ubh,mem,size)
extern void _ubh_memcpyubh_(struct ufs_sb_private_info *, struct ufs_buffer_head *, unsigned char *, unsigned);

/* This functions works with cache pages*/
extern struct page *ufs_get_locked_page(struct address_space *mapping,
					pgoff_t index);
static inline void ufs_put_locked_page(struct page *page)
{
       unlock_page(page);
       put_page(page);
}


/*
 * macros and inline function to get important structures from ufs_sb_private_info
 */

static inline void *get_usb_offset(struct ufs_sb_private_info *uspi,
				   unsigned int offset)
{
	unsigned int index;
	
	index = offset >> uspi->s_fshift;
	offset &= ~uspi->s_fmask;
	return uspi->s_ubh.bh[index]->b_data + offset;
}

#define ubh_get_usb_first(uspi) \
	((struct ufs_super_block_first *)get_usb_offset((uspi), 0))

#define ubh_get_usb_second(uspi) \
	((struct ufs_super_block_second *)get_usb_offset((uspi), UFS_SECTOR_SIZE))

#define ubh_get_usb_third(uspi)	\
	((struct ufs_super_block_third *)get_usb_offset((uspi), 2*UFS_SECTOR_SIZE))


#define ubh_get_ucg(ubh) \
	((struct ufs_cylinder_group *)((ubh)->bh[0]->b_data))


/*
 * Extract byte from ufs_buffer_head
 * Extract the bits for a block from a map inside ufs_buffer_head
 */
#define ubh_get_addr8(ubh,begin) \
	((u8*)(ubh)->bh[(begin) >> uspi->s_fshift]->b_data + \
	((begin) & ~uspi->s_fmask))

#define ubh_get_addr16(ubh,begin) \
	(((__fs16*)((ubh)->bh[(begin) >> (uspi->s_fshift-1)]->b_data)) + \
	((begin) & ((uspi->fsize>>1) - 1)))

#define ubh_get_addr32(ubh,begin) \
	(((__fs32*)((ubh)->bh[(begin) >> (uspi->s_fshift-2)]->b_data)) + \
	((begin) & ((uspi->s_fsize>>2) - 1)))

#define ubh_get_addr64(ubh,begin) \
	(((__fs64*)((ubh)->bh[(begin) >> (uspi->s_fshift-3)]->b_data)) + \
	((begin) & ((uspi->s_fsize>>3) - 1)))

#define ubh_get_addr ubh_get_addr8

static inline void *ubh_get_data_ptr(struct ufs_sb_private_info *uspi,
				     struct ufs_buffer_head *ubh,
				     u64 blk)
{
	if (uspi->fs_magic == UFS2_MAGIC)
		return ubh_get_addr64(ubh, blk);
	else
		return ubh_get_addr32(ubh, blk);
}

#define ubh_blkmap(ubh,begin,bit) \
	((*ubh_get_addr(ubh, (begin) + ((bit) >> 3)) >> ((bit) & 7)) & (0xff >> (UFS_MAXFRAG - uspi->s_fpb)))

/*
 * Determine the number of available frags given a
 * percentage to hold in reserve.
 */
static inline u64
ufs_freespace(struct ufs_sb_private_info *uspi, int percentreserved)
{
	return ufs_blkstofrags(uspi->cs_total.cs_nbfree) +
		uspi->cs_total.cs_nffree -
		(uspi->s_dsize * (percentreserved) / 100);
}

/*
 * Macros to access cylinder group array structures
 */
#define ubh_cg_blktot(ucpi,cylno) \
	(*((__fs32*)ubh_get_addr(UCPI_UBH(ucpi), (ucpi)->c_btotoff + ((cylno) << 2))))

#define ubh_cg_blks(ucpi,cylno,rpos) \
	(*((__fs16*)ubh_get_addr(UCPI_UBH(ucpi), \
	(ucpi)->c_boff + (((cylno) * uspi->s_nrpos + (rpos)) << 1 ))))

/*
 * Bitmap operations
 * These functions work like classical bitmap operations.
 * The difference is that we don't have the whole bitmap
 * in one contiguous chunk of memory, but in several buffers.
 * The parameters of each function are super_block, ufs_buffer_head and
 * position of the beginning of the bitmap.
 */
#define ubh_setbit(ubh,begin,bit) \
	(*ubh_get_addr(ubh, (begin) + ((bit) >> 3)) |= (1 << ((bit) & 7)))

#define ubh_clrbit(ubh,begin,bit) \
	(*ubh_get_addr (ubh, (begin) + ((bit) >> 3)) &= ~(1 << ((bit) & 7)))

#define ubh_isset(ubh,begin,bit) \
	(*ubh_get_addr (ubh, (begin) + ((bit) >> 3)) & (1 << ((bit) & 7)))

#define ubh_isclr(ubh,begin,bit) (!ubh_isset(ubh,begin,bit))

#define ubh_find_first_zero_bit(ubh,begin,size) _ubh_find_next_zero_bit_(uspi,ubh,begin,size,0)

#define ubh_find_next_zero_bit(ubh,begin,size,offset) _ubh_find_next_zero_bit_(uspi,ubh,begin,size,offset)
static inline unsigned _ubh_find_next_zero_bit_(
	struct ufs_sb_private_info * uspi, struct ufs_buffer_head * ubh,
	unsigned begin, unsigned size, unsigned offset)
{
	unsigned base, count, pos;

	size -= offset;
	begin <<= 3;
	offset += begin;
	base = offset >> uspi->s_bpfshift;
	offset &= uspi->s_bpfmask;
	for (;;) {
		count = min_t(unsigned int, size + offset, uspi->s_bpf);
		size -= count - offset;
		pos = find_next_zero_bit_le(ubh->bh[base]->b_data, count, offset);
		if (pos < count || !size)
			break;
		base++;
		offset = 0;
	}
	return (base << uspi->s_bpfshift) + pos - begin;
} 	

static inline unsigned find_last_zero_bit (unsigned char * bitmap,
	unsigned size, unsigned offset)
{
	unsigned bit, i;
	unsigned char * mapp;
	unsigned char map;

	mapp = bitmap + (size >> 3);
	map = *mapp--;
	bit = 1 << (size & 7);
	for (i = size; i > offset; i--) {
		if ((map & bit) == 0)
			break;
		if ((i & 7) != 0) {
			bit >>= 1;
		} else {
			map = *mapp--;
			bit = 1 << 7;
		}
	}
	return i;
}

#define ubh_find_last_zero_bit(ubh,begin,size,offset) _ubh_find_last_zero_bit_(uspi,ubh,begin,size,offset)
static inline unsigned _ubh_find_last_zero_bit_(
	struct ufs_sb_private_info * uspi, struct ufs_buffer_head * ubh,
	unsigned begin, unsigned start, unsigned end)
{
	unsigned base, count, pos, size;

	size = start - end;
	begin <<= 3;
	start += begin;
	base = start >> uspi->s_bpfshift;
	start &= uspi->s_bpfmask;
	for (;;) {
		count = min_t(unsigned int,
			    size + (uspi->s_bpf - start), uspi->s_bpf)
			- (uspi->s_bpf - start);
		size -= count;
		pos = find_last_zero_bit (ubh->bh[base]->b_data,
			start, start - count);
		if (pos > start - count || !size)
			break;
		base--;
		start = uspi->s_bpf;
	}
	return (base << uspi->s_bpfshift) + pos - begin;
} 	

#define ubh_isblockclear(ubh,begin,block) (!_ubh_isblockset_(uspi,ubh,begin,block))

#define ubh_isblockset(ubh,begin,block) _ubh_isblockset_(uspi,ubh,begin,block)
static inline int _ubh_isblockset_(struct ufs_sb_private_info * uspi,
	struct ufs_buffer_head * ubh, unsigned begin, unsigned block)
{
	switch (uspi->s_fpb) {
	case 8:
	    	return (*ubh_get_addr (ubh, begin + block) == 0xff);
	case 4:
		return (*ubh_get_addr (ubh, begin + (block >> 1)) == (0x0f << ((block & 0x01) << 2)));
	case 2:
		return (*ubh_get_addr (ubh, begin + (block >> 2)) == (0x03 << ((block & 0x03) << 1)));
	case 1:
		return (*ubh_get_addr (ubh, begin + (block >> 3)) == (0x01 << (block & 0x07)));
	}
	return 0;	
}

#define ubh_clrblock(ubh,begin,block) _ubh_clrblock_(uspi,ubh,begin,block)
static inline void _ubh_clrblock_(struct ufs_sb_private_info * uspi,
	struct ufs_buffer_head * ubh, unsigned begin, unsigned block)
{
	switch (uspi->s_fpb) {
	case 8:
	    	*ubh_get_addr (ubh, begin + block) = 0x00;
	    	return; 
	case 4:
		*ubh_get_addr (ubh, begin + (block >> 1)) &= ~(0x0f << ((block & 0x01) << 2));
		return;
	case 2:
		*ubh_get_addr (ubh, begin + (block >> 2)) &= ~(0x03 << ((block & 0x03) << 1));
		return;
	case 1:
		*ubh_get_addr (ubh, begin + (block >> 3)) &= ~(0x01 << ((block & 0x07)));
		return;
	}
}

#define ubh_setblock(ubh,begin,block) _ubh_setblock_(uspi,ubh,begin,block)
static inline void _ubh_setblock_(struct ufs_sb_private_info * uspi,
	struct ufs_buffer_head * ubh, unsigned begin, unsigned block)
{
	switch (uspi->s_fpb) {
	case 8:
	    	*ubh_get_addr(ubh, begin + block) = 0xff;
	    	return;
	case 4:
		*ubh_get_addr(ubh, begin + (block >> 1)) |= (0x0f << ((block & 0x01) << 2));
		return;
	case 2:
		*ubh_get_addr(ubh, begin + (block >> 2)) |= (0x03 << ((block & 0x03) << 1));
		return;
	case 1:
		*ubh_get_addr(ubh, begin + (block >> 3)) |= (0x01 << ((block & 0x07)));
		return;
	}
}

static inline void ufs_fragacct (struct super_block * sb, unsigned blockmap,
	__fs32 * fraglist, int cnt)
{
	struct ufs_sb_private_info * uspi;
	unsigned fragsize, pos;
	
	uspi = UFS_SB(sb)->s_uspi;
	
	fragsize = 0;
	for (pos = 0; pos < uspi->s_fpb; pos++) {
		if (blockmap & (1 << pos)) {
			fragsize++;
		}
		else if (fragsize > 0) {
			fs32_add(sb, &fraglist[fragsize], cnt);
			fragsize = 0;
		}
	}
	if (fragsize > 0 && fragsize < uspi->s_fpb)
		fs32_add(sb, &fraglist[fragsize], cnt);
}

static inline void *ufs_get_direct_data_ptr(struct ufs_sb_private_info *uspi,
					    struct ufs_inode_info *ufsi,
					    unsigned blk)
{
	BUG_ON(blk > UFS_TIND_BLOCK);
	return uspi->fs_magic == UFS2_MAGIC ?
		(void *)&ufsi->i_u1.u2_i_data[blk] :
		(void *)&ufsi->i_u1.i_data[blk];
}

static inline u64 ufs_data_ptr_to_cpu(struct super_block *sb, void *p)
{
	return UFS_SB(sb)->s_uspi->fs_magic == UFS2_MAGIC ?
		fs64_to_cpu(sb, *(__fs64 *)p) :
		fs32_to_cpu(sb, *(__fs32 *)p);
}

static inline void ufs_cpu_to_data_ptr(struct super_block *sb, void *p, u64 val)
{
	if (UFS_SB(sb)->s_uspi->fs_magic == UFS2_MAGIC)
		*(__fs64 *)p = cpu_to_fs64(sb, val);
	else
		*(__fs32 *)p = cpu_to_fs32(sb, val);
}

static inline void ufs_data_ptr_clear(struct ufs_sb_private_info *uspi,
				      void *p)
{
	if (uspi->fs_magic == UFS2_MAGIC)
		*(__fs64 *)p = 0;
	else
		*(__fs32 *)p = 0;
}

static inline int ufs_is_data_ptr_zero(struct ufs_sb_private_info *uspi,
				       void *p)
{
	if (uspi->fs_magic == UFS2_MAGIC)
		return *(__fs64 *)p == 0;
	else
		return *(__fs32 *)p == 0;
}
