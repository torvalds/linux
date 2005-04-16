
#ifndef _ISOFS_FS_H
#define _ISOFS_FS_H

#include <linux/types.h>
/*
 * The isofs filesystem constants/structures
 */

/* This part borrowed from the bsd386 isofs */
#define ISODCL(from, to) (to - from + 1)

struct iso_volume_descriptor {
	char type[ISODCL(1,1)]; /* 711 */
	char id[ISODCL(2,6)];
	char version[ISODCL(7,7)];
	char data[ISODCL(8,2048)];
};

/* volume descriptor types */
#define ISO_VD_PRIMARY 1
#define ISO_VD_SUPPLEMENTARY 2
#define ISO_VD_END 255

#define ISO_STANDARD_ID "CD001"

struct iso_primary_descriptor {
	char type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	char version			[ISODCL (  7,   7)]; /* 711 */
	char unused1			[ISODCL (  8,   8)];
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	char unused2			[ISODCL ( 73,  80)];
	char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	char unused3			[ISODCL ( 89, 120)];
	char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	char path_table_size		[ISODCL (133, 140)]; /* 733 */
	char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	char unused4			[ISODCL (883, 883)];
	char application_data		[ISODCL (884, 1395)];
	char unused5			[ISODCL (1396, 2048)];
};

/* Almost the same as the primary descriptor but two fields are specified */
struct iso_supplementary_descriptor {
	char type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	char version			[ISODCL (  7,   7)]; /* 711 */
	char flags			[ISODCL (  8,   8)]; /* 853 */
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	char unused2			[ISODCL ( 73,  80)];
	char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	char escape			[ISODCL ( 89, 120)]; /* 856 */
	char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	char path_table_size		[ISODCL (133, 140)]; /* 733 */
	char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	char unused4			[ISODCL (883, 883)];
	char application_data		[ISODCL (884, 1395)];
	char unused5			[ISODCL (1396, 2048)];
};


#define HS_STANDARD_ID "CDROM"

struct  hs_volume_descriptor {
	char foo			[ISODCL (  1,   8)]; /* 733 */
	char type			[ISODCL (  9,   9)]; /* 711 */
	char id				[ISODCL ( 10,  14)];
	char version			[ISODCL ( 15,  15)]; /* 711 */
	char data[ISODCL(16,2048)];
};


struct hs_primary_descriptor {
	char foo			[ISODCL (  1,   8)]; /* 733 */
	char type			[ISODCL (  9,   9)]; /* 711 */
	char id				[ISODCL ( 10,  14)];
	char version			[ISODCL ( 15,  15)]; /* 711 */
	char unused1			[ISODCL ( 16,  16)]; /* 711 */
	char system_id			[ISODCL ( 17,  48)]; /* achars */
	char volume_id			[ISODCL ( 49,  80)]; /* dchars */
	char unused2			[ISODCL ( 81,  88)]; /* 733 */
	char volume_space_size		[ISODCL ( 89,  96)]; /* 733 */
	char unused3			[ISODCL ( 97, 128)]; /* 733 */
	char volume_set_size		[ISODCL (129, 132)]; /* 723 */
	char volume_sequence_number	[ISODCL (133, 136)]; /* 723 */
	char logical_block_size		[ISODCL (137, 140)]; /* 723 */
	char path_table_size		[ISODCL (141, 148)]; /* 733 */
	char type_l_path_table		[ISODCL (149, 152)]; /* 731 */
	char unused4			[ISODCL (153, 180)]; /* 733 */
	char root_directory_record	[ISODCL (181, 214)]; /* 9.1 */
};

/* We use this to help us look up the parent inode numbers. */

struct iso_path_table{
	unsigned char  name_len[2];	/* 721 */
	char extent[4];		/* 731 */
	char  parent[2];	/* 721 */
	char name[0];
} __attribute__((packed));

/* high sierra is identical to iso, except that the date is only 6 bytes, and
   there is an extra reserved byte after the flags */

struct iso_directory_record {
	char length			[ISODCL (1, 1)]; /* 711 */
	char ext_attr_length		[ISODCL (2, 2)]; /* 711 */
	char extent			[ISODCL (3, 10)]; /* 733 */
	char size			[ISODCL (11, 18)]; /* 733 */
	char date			[ISODCL (19, 25)]; /* 7 by 711 */
	char flags			[ISODCL (26, 26)];
	char file_unit_size		[ISODCL (27, 27)]; /* 711 */
	char interleave			[ISODCL (28, 28)]; /* 711 */
	char volume_sequence_number	[ISODCL (29, 32)]; /* 723 */
	unsigned char name_len		[ISODCL (33, 33)]; /* 711 */
	char name			[0];
} __attribute__((packed));

#define ISOFS_BLOCK_BITS 11
#define ISOFS_BLOCK_SIZE 2048

#define ISOFS_BUFFER_SIZE(INODE) ((INODE)->i_sb->s_blocksize)
#define ISOFS_BUFFER_BITS(INODE) ((INODE)->i_sb->s_blocksize_bits)

#define ISOFS_SUPER_MAGIC 0x9660

#ifdef __KERNEL__
/* Number conversion inlines, named after the section in ISO 9660
   they correspond to. */

#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/iso_fs_i.h>
#include <linux/iso_fs_sb.h>

static inline struct isofs_sb_info *ISOFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct iso_inode_info *ISOFS_I(struct inode *inode)
{
	return container_of(inode, struct iso_inode_info, vfs_inode);
}

static inline int isonum_711(char *p)
{
	return *(u8 *)p;
}
static inline int isonum_712(char *p)
{
	return *(s8 *)p;
}
static inline unsigned int isonum_721(char *p)
{
	return le16_to_cpu(get_unaligned((__le16 *)p));
}
static inline unsigned int isonum_722(char *p)
{
	return be16_to_cpu(get_unaligned((__le16 *)p));
}
static inline unsigned int isonum_723(char *p)
{
	/* Ignore bigendian datum due to broken mastering programs */
	return le16_to_cpu(get_unaligned((__le16 *)p));
}
static inline unsigned int isonum_731(char *p)
{
	return le32_to_cpu(get_unaligned((__le32 *)p));
}
static inline unsigned int isonum_732(char *p)
{
	return be32_to_cpu(get_unaligned((__le32 *)p));
}
static inline unsigned int isonum_733(char *p)
{
	/* Ignore bigendian datum due to broken mastering programs */
	return le32_to_cpu(get_unaligned((__le32 *)p));
}
extern int iso_date(char *, int);

struct inode;		/* To make gcc happy */

extern int parse_rock_ridge_inode(struct iso_directory_record *, struct inode *);
extern int get_rock_ridge_filename(struct iso_directory_record *, char *, struct inode *);
extern int isofs_name_translate(struct iso_directory_record *, char *, struct inode *);

int get_joliet_filename(struct iso_directory_record *, unsigned char *, struct inode *);
int get_acorn_filename(struct iso_directory_record *, char *, struct inode *);

extern struct dentry *isofs_lookup(struct inode *, struct dentry *, struct nameidata *);
extern struct buffer_head *isofs_bread(struct inode *, sector_t);
extern int isofs_get_blocks(struct inode *, sector_t, struct buffer_head **, unsigned long);

extern struct inode *isofs_iget(struct super_block *sb,
                                unsigned long block,
                                unsigned long offset);

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

extern struct inode_operations isofs_dir_inode_operations;
extern struct file_operations isofs_dir_operations;
extern struct address_space_operations isofs_symlink_aops;
extern struct export_operations isofs_export_ops;

/* The following macros are used to check for memory leaks. */
#ifdef LEAK_CHECK
#define free_s leak_check_free_s
#define malloc leak_check_malloc
#define sb_bread leak_check_bread
#define brelse leak_check_brelse
extern void * leak_check_malloc(unsigned int size);
extern void leak_check_free_s(void * obj, int size);
extern struct buffer_head * leak_check_bread(struct super_block *sb, int block);
extern void leak_check_brelse(struct buffer_head * bh);
#endif /* LEAK_CHECK */

#endif /* __KERNEL__ */

#endif
