/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UDF_DECL_H
#define __UDF_DECL_H

#define pr_fmt(fmt) "UDF-fs: " fmt

#include "ecma_167.h"
#include "osta_udf.h"

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/udf_fs_i.h>

#include "udf_sb.h"
#include "udfend.h"
#include "udf_i.h"

#define UDF_DEFAULT_PREALLOC_BLOCKS	8

extern __printf(3, 4) void _udf_err(struct super_block *sb,
		const char *function, const char *fmt, ...);
#define udf_err(sb, fmt, ...)					\
	_udf_err(sb, __func__, fmt, ##__VA_ARGS__)

extern __printf(3, 4) void _udf_warn(struct super_block *sb,
		const char *function, const char *fmt, ...);
#define udf_warn(sb, fmt, ...)					\
	_udf_warn(sb, __func__, fmt, ##__VA_ARGS__)

#define udf_info(fmt, ...)					\
	pr_info("INFO " fmt, ##__VA_ARGS__)

#define udf_debug(fmt, ...)					\
	pr_debug("%s:%d:%s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#define UDF_EXTENT_LENGTH_MASK	0x3FFFFFFF
#define UDF_EXTENT_FLAG_MASK	0xC0000000

#define UDF_INVALID_ID ((uint32_t)-1)

#define UDF_NAME_PAD		4
#define UDF_NAME_LEN		254
#define UDF_NAME_LEN_CS0	255

static inline size_t udf_file_entry_alloc_offset(struct inode *inode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	if (iinfo->i_use)
		return sizeof(struct unallocSpaceEntry);
	else if (iinfo->i_efe)
		return sizeof(struct extendedFileEntry) + iinfo->i_lenEAttr;
	else
		return sizeof(struct fileEntry) + iinfo->i_lenEAttr;
}

static inline size_t udf_ext0_offset(struct inode *inode)
{
	if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		return udf_file_entry_alloc_offset(inode);
	else
		return 0;
}

/* computes tag checksum */
u8 udf_tag_checksum(const struct tag *t);

typedef uint32_t udf_pblk_t;

struct dentry;
struct inode;
struct task_struct;
struct buffer_head;
struct super_block;

extern const struct export_operations udf_export_ops;
extern const struct inode_operations udf_dir_inode_operations;
extern const struct file_operations udf_dir_operations;
extern const struct inode_operations udf_file_inode_operations;
extern const struct file_operations udf_file_operations;
extern const struct inode_operations udf_symlink_inode_operations;
extern const struct address_space_operations udf_aops;
extern const struct address_space_operations udf_symlink_aops;

struct udf_fileident_iter {
	struct inode *dir;		/* Directory we are working with */
	loff_t pos;			/* Logical position in a dir */
	struct buffer_head *bh[2];	/* Buffer containing 'pos' and possibly
					 * next buffer if entry straddles
					 * blocks */
	struct kernel_lb_addr eloc;	/* Start of extent containing 'pos' */
	uint32_t elen;			/* Length of extent containing 'pos' */
	sector_t loffset;		/* Block offset of 'pos' within above
					 * extent */
	struct extent_position epos;	/* Position after the above extent */
	struct fileIdentDesc fi;	/* Copied directory entry */
	uint8_t *name;			/* Pointer to entry name */
	uint8_t *namebuf;		/* Storage for entry name in case
					 * the name is split between two blocks
					 */
};

struct udf_vds_record {
	uint32_t block;
	uint32_t volDescSeqNum;
};

struct generic_desc {
	struct tag	descTag;
	__le32		volDescSeqNum;
};


/* super.c */

static inline void udf_updated_lvid(struct super_block *sb)
{
	struct buffer_head *bh = UDF_SB(sb)->s_lvid_bh;

	BUG_ON(!bh);
	WARN_ON_ONCE(((struct logicalVolIntegrityDesc *)
		     bh->b_data)->integrityType !=
		     cpu_to_le32(LVID_INTEGRITY_TYPE_OPEN));
	UDF_SB(sb)->s_lvid_dirty = 1;
}
extern u64 lvid_get_unique_id(struct super_block *sb);
struct inode *udf_find_metadata_inode_efe(struct super_block *sb,
					u32 meta_file_loc, u32 partition_num);

/* namei.c */
static inline unsigned int udf_dir_entry_len(struct fileIdentDesc *cfi)
{
	return ALIGN(sizeof(struct fileIdentDesc) +
		le16_to_cpu(cfi->lengthOfImpUse) + cfi->lengthFileIdent,
		UDF_NAME_PAD);
}

/* file.c */
extern long udf_ioctl(struct file *, unsigned int, unsigned long);

/* inode.c */
extern struct inode *__udf_iget(struct super_block *, struct kernel_lb_addr *,
				bool hidden_inode);
static inline struct inode *udf_iget_special(struct super_block *sb,
					     struct kernel_lb_addr *ino)
{
	return __udf_iget(sb, ino, true);
}
static inline struct inode *udf_iget(struct super_block *sb,
				     struct kernel_lb_addr *ino)
{
	return __udf_iget(sb, ino, false);
}
extern int udf_expand_file_adinicb(struct inode *);
extern struct buffer_head *udf_bread(struct inode *inode, udf_pblk_t block,
				      int create, int *err);
extern int udf_setsize(struct inode *, loff_t);
extern void udf_evict_inode(struct inode *);
extern int udf_write_inode(struct inode *, struct writeback_control *wbc);
extern int inode_bmap(struct inode *inode, sector_t block,
		      struct extent_position *pos, struct kernel_lb_addr *eloc,
		      uint32_t *elen, sector_t *offset, int8_t *etype);
int udf_get_block(struct inode *, sector_t, struct buffer_head *, int);
extern int udf_setup_indirect_aext(struct inode *inode, udf_pblk_t block,
				   struct extent_position *epos);
extern int __udf_add_aext(struct inode *inode, struct extent_position *epos,
			  struct kernel_lb_addr *eloc, uint32_t elen, int inc);
extern int udf_add_aext(struct inode *, struct extent_position *,
			struct kernel_lb_addr *, uint32_t, int);
extern void udf_write_aext(struct inode *, struct extent_position *,
			   struct kernel_lb_addr *, uint32_t, int);
extern int8_t udf_delete_aext(struct inode *, struct extent_position);
extern int udf_next_aext(struct inode *inode, struct extent_position *epos,
			 struct kernel_lb_addr *eloc, uint32_t *elen,
			 int8_t *etype, int inc);
extern int udf_current_aext(struct inode *inode, struct extent_position *epos,
			    struct kernel_lb_addr *eloc, uint32_t *elen,
			    int8_t *etype, int inc);
extern void udf_update_extra_perms(struct inode *inode, umode_t mode);

/* misc.c */
extern struct genericFormat *udf_add_extendedattr(struct inode *, uint32_t,
						  uint32_t, uint8_t);
extern struct genericFormat *udf_get_extendedattr(struct inode *, uint32_t,
						  uint8_t);
extern struct buffer_head *udf_read_tagged(struct super_block *, uint32_t,
					   uint32_t, uint16_t *);
extern struct buffer_head *udf_read_ptagged(struct super_block *,
					    struct kernel_lb_addr *, uint32_t,
					    uint16_t *);
extern void udf_update_tag(char *, int);
extern void udf_new_tag(char *, uint16_t, uint16_t, uint16_t, uint32_t, int);

/* lowlevel.c */
extern unsigned int udf_get_last_session(struct super_block *);
udf_pblk_t udf_get_last_block(struct super_block *);

/* partition.c */
extern uint32_t udf_get_pblock(struct super_block *, uint32_t, uint16_t,
			       uint32_t);
extern uint32_t udf_get_pblock_virt15(struct super_block *, uint32_t, uint16_t,
				      uint32_t);
extern uint32_t udf_get_pblock_virt20(struct super_block *, uint32_t, uint16_t,
				      uint32_t);
extern uint32_t udf_get_pblock_spar15(struct super_block *, uint32_t, uint16_t,
				      uint32_t);
extern uint32_t udf_get_pblock_meta25(struct super_block *, uint32_t, uint16_t,
					  uint32_t);
extern int udf_relocate_blocks(struct super_block *, long, long *);

static inline uint32_t
udf_get_lb_pblock(struct super_block *sb, struct kernel_lb_addr *loc,
		  uint32_t offset)
{
	return udf_get_pblock(sb, loc->logicalBlockNum,
			loc->partitionReferenceNum, offset);
}

/* unicode.c */
extern int udf_get_filename(struct super_block *, const uint8_t *, int,
			    uint8_t *, int);
extern int udf_put_filename(struct super_block *, const uint8_t *, int,
			    uint8_t *, int);
extern int udf_dstrCS0toChar(struct super_block *, uint8_t *, int,
			     const uint8_t *, int);

/* ialloc.c */
extern void udf_free_inode(struct inode *);
extern struct inode *udf_new_inode(struct inode *, umode_t);

/* truncate.c */
extern void udf_truncate_tail_extent(struct inode *);
extern void udf_discard_prealloc(struct inode *);
extern int udf_truncate_extents(struct inode *);

/* balloc.c */
extern void udf_free_blocks(struct super_block *, struct inode *,
			    struct kernel_lb_addr *, uint32_t, uint32_t);
extern int udf_prealloc_blocks(struct super_block *, struct inode *, uint16_t,
			       uint32_t, uint32_t);
extern udf_pblk_t udf_new_block(struct super_block *sb, struct inode *inode,
				 uint16_t partition, uint32_t goal, int *err);

/* directory.c */
int udf_fiiter_init(struct udf_fileident_iter *iter, struct inode *dir,
		    loff_t pos);
int udf_fiiter_advance(struct udf_fileident_iter *iter);
void udf_fiiter_release(struct udf_fileident_iter *iter);
void udf_fiiter_write_fi(struct udf_fileident_iter *iter, uint8_t *impuse);
void udf_fiiter_update_elen(struct udf_fileident_iter *iter, uint32_t new_elen);
int udf_fiiter_append_blk(struct udf_fileident_iter *iter);
extern struct long_ad *udf_get_filelongad(uint8_t *, int, uint32_t *, int);
extern struct short_ad *udf_get_fileshortad(uint8_t *, int, uint32_t *, int);

/* udftime.c */
extern void udf_disk_stamp_to_time(struct timespec64 *dest,
						struct timestamp src);
extern void udf_time_to_disk_stamp(struct timestamp *dest, struct timespec64 src);

#endif				/* __UDF_DECL_H */
