/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/iso_fs.h>
#include <linux/unaligned.h>

enum isofs_file_format {
	isofs_file_normal = 0,
	isofs_file_sparse = 1,
	isofs_file_compressed = 2,
};
	
/*
 * iso fs inode data in memory
 */
struct iso_inode_info {
	unsigned long i_iget5_block;
	unsigned long i_iget5_offset;
	unsigned int i_first_extent;
	unsigned char i_file_format;
	unsigned char i_format_parm[3];
	unsigned long i_next_section_block;
	unsigned long i_next_section_offset;
	off_t i_section_size;
	struct inode vfs_inode;
};

/*
 * iso9660 super-block data in memory
 */
struct isofs_sb_info {
	unsigned long s_ninodes;
	unsigned long s_nzones;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	
	int           s_rock_offset; /* offset of SUSP fields within SU area */
	s32           s_sbsector;
	unsigned char s_joliet_level;
	unsigned char s_mapping;
	unsigned char s_check;
	unsigned char s_session;
	unsigned int  s_high_sierra:1;
	unsigned int  s_rock:2;
	unsigned int  s_cruft:1; /* Broken disks with high byte of length
				  * containing junk */
	unsigned int  s_nocompress:1;
	unsigned int  s_hide:1;
	unsigned int  s_showassoc:1;
	unsigned int  s_overriderockperm:1;
	unsigned int  s_uid_set:1;
	unsigned int  s_gid_set:1;

	umode_t s_fmode;
	umode_t s_dmode;
	kgid_t s_gid;
	kuid_t s_uid;
	struct nls_table *s_nls_iocharset; /* Native language support table */
};

#define ISOFS_INVALID_MODE ((umode_t) -1)

static inline struct isofs_sb_info *ISOFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct iso_inode_info *ISOFS_I(struct inode *inode)
{
	return container_of(inode, struct iso_inode_info, vfs_inode);
}

static inline int isonum_711(u8 *p)
{
	return *p;
}
static inline int isonum_712(s8 *p)
{
	return *p;
}
static inline unsigned int isonum_721(u8 *p)
{
	return get_unaligned_le16(p);
}
static inline unsigned int isonum_722(u8 *p)
{
	return get_unaligned_be16(p);
}
static inline unsigned int isonum_723(u8 *p)
{
	/* Ignore bigendian datum due to broken mastering programs */
	return get_unaligned_le16(p);
}
static inline unsigned int isonum_731(u8 *p)
{
	return get_unaligned_le32(p);
}
static inline unsigned int isonum_732(u8 *p)
{
	return get_unaligned_be32(p);
}
static inline unsigned int isonum_733(u8 *p)
{
	/* Ignore bigendian datum due to broken mastering programs */
	return get_unaligned_le32(p);
}
#define ISO_DATE_HIGH_SIERRA (1 << 0)
#define ISO_DATE_LONG_FORM (1 << 1)
struct timespec64 iso_date(u8 *p, int flags);

struct inode;		/* To make gcc happy */

extern int parse_rock_ridge_inode(struct iso_directory_record *, struct inode *, int relocated);
extern int get_rock_ridge_filename(struct iso_directory_record *, char *, struct inode *);
extern int isofs_name_translate(struct iso_directory_record *, char *, struct inode *);

int get_joliet_filename(struct iso_directory_record *, unsigned char *, struct inode *);
int get_acorn_filename(struct iso_directory_record *, char *, struct inode *);

extern struct dentry *isofs_lookup(struct inode *, struct dentry *, unsigned int flags);
extern struct buffer_head *isofs_bread(struct inode *, sector_t);
extern int isofs_get_blocks(struct inode *, sector_t, struct buffer_head **, unsigned long);

struct inode *__isofs_iget(struct super_block *sb,
			   unsigned long block,
			   unsigned long offset,
			   int relocated);

static inline struct inode *isofs_iget(struct super_block *sb,
				       unsigned long block,
				       unsigned long offset)
{
	return __isofs_iget(sb, block, offset, 0);
}

static inline struct inode *isofs_iget_reloc(struct super_block *sb,
					     unsigned long block,
					     unsigned long offset)
{
	return __isofs_iget(sb, block, offset, 1);
}

/* Because the inode number is no longer relevant to finding the
 * underlying meta-data for an inode, we are free to choose a more
 * convenient 32-bit number as the inode number.  The inode numbering
 * scheme was recommended by Sergey Vlasov and Eric Lammerts. */
static inline unsigned long isofs_get_ino(unsigned long block,
					  unsigned long offset,
					  unsigned long bufbits)
{
	return (block << (bufbits - 5)) | (offset >> 5);
}

/* Every directory can have many redundant directory entries scattered
 * throughout the directory tree.  First there is the directory entry
 * with the name of the directory stored in the parent directory.
 * Then, there is the "." directory entry stored in the directory
 * itself.  Finally, there are possibly many ".." directory entries
 * stored in all the subdirectories.
 *
 * In order for the NFS get_parent() method to work and for the
 * general consistency of the dcache, we need to make sure the
 * "i_iget5_block" and "i_iget5_offset" all point to exactly one of
 * the many redundant entries for each directory.  We normalize the
 * block and offset by always making them point to the "."  directory.
 *
 * Notice that we do not use the entry for the directory with the name
 * that is located in the parent directory.  Even though choosing this
 * first directory is more natural, it is much easier to find the "."
 * entry in the NFS get_parent() method because it is implicitly
 * encoded in the "extent + ext_attr_length" fields of _all_ the
 * redundant entries for the directory.  Thus, it can always be
 * reached regardless of which directory entry you have in hand.
 *
 * This works because the "." entry is simply the first directory
 * record when you start reading the file that holds all the directory
 * records, and this file starts at "extent + ext_attr_length" blocks.
 * Because the "." entry is always the first entry listed in the
 * directories file, the normalized "offset" value is always 0.
 *
 * You should pass the directory entry in "de".  On return, "block"
 * and "offset" will hold normalized values.  Only directories are
 * affected making it safe to call even for non-directory file
 * types. */
static inline void
isofs_normalize_block_and_offset(struct iso_directory_record* de,
				 unsigned long *block,
				 unsigned long *offset)
{
	/* Only directories are normalized. */
	if (de->flags[0] & 2) {
		*offset = 0;
		*block = (unsigned long)isonum_733(de->extent)
			+ (unsigned long)isonum_711(de->ext_attr_length);
	}
}

extern const struct inode_operations isofs_dir_inode_operations;
extern const struct file_operations isofs_dir_operations;
extern const struct address_space_operations isofs_symlink_aops;
extern const struct export_operations isofs_export_ops;
