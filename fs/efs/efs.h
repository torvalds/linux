/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 * Portions derived from IRIX header files (c) 1988 Silicon Graphics
 */
#ifndef _EFS_EFS_H_
#define _EFS_EFS_H_

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/uaccess.h>

#define EFS_VERSION "1.0a"

static const char cprt[] = "EFS: "EFS_VERSION" - (c) 1999 Al Smith <Al.Smith@aeschi.ch.eu.org>";


/* 1 block is 512 bytes */
#define	EFS_BLOCKSIZE_BITS	9
#define	EFS_BLOCKSIZE		(1 << EFS_BLOCKSIZE_BITS)

typedef	int32_t		efs_block_t;
typedef uint32_t	efs_ino_t;

#define	EFS_DIRECTEXTENTS	12

/*
 * layout of an extent, in memory and on disk. 8 bytes exactly.
 */
typedef union extent_u {
	unsigned char raw[8];
	struct extent_s {
		unsigned int	ex_magic:8;	/* magic # (zero) */
		unsigned int	ex_bn:24;	/* basic block */
		unsigned int	ex_length:8;	/* numblocks in this extent */
		unsigned int	ex_offset:24;	/* logical offset into file */
	} cooked;
} efs_extent;

typedef struct edevs {
	__be16		odev;
	__be32		ndev;
} efs_devs;

/*
 * extent based filesystem inode as it appears on disk.  The efs inode
 * is exactly 128 bytes long.
 */
struct	efs_dinode {
	__be16		di_mode;	/* mode and type of file */
	__be16		di_nlink;	/* number of links to file */
	__be16		di_uid;		/* owner's user id */
	__be16		di_gid;		/* owner's group id */
	__be32		di_size;	/* number of bytes in file */
	__be32		di_atime;	/* time last accessed */
	__be32		di_mtime;	/* time last modified */
	__be32		di_ctime;	/* time created */
	__be32		di_gen;		/* generation number */
	__be16		di_numextents;	/* # of extents */
	u_char		di_version;	/* version of inode */
	u_char		di_spare;	/* spare - used by AFS */
	union di_addr {
		efs_extent	di_extents[EFS_DIRECTEXTENTS];
		efs_devs	di_dev;	/* device for IFCHR/IFBLK */
	} di_u;
};

/* efs inode storage in memory */
struct efs_inode_info {
	int		numextents;
	int		lastextent;

	efs_extent	extents[EFS_DIRECTEXTENTS];
	struct inode	vfs_inode;
};

#include <linux/efs_fs_sb.h>

#define EFS_DIRBSIZE_BITS	EFS_BLOCKSIZE_BITS
#define EFS_DIRBSIZE		(1 << EFS_DIRBSIZE_BITS)

struct efs_dentry {
	__be32		inode;
	unsigned char	namelen;
	char		name[3];
};

#define EFS_DENTSIZE	(sizeof(struct efs_dentry) - 3 + 1)
#define EFS_MAXNAMELEN  ((1 << (sizeof(char) * 8)) - 1)

#define EFS_DIRBLK_HEADERSIZE	4
#define EFS_DIRBLK_MAGIC	0xbeef	/* moo */

struct efs_dir {
	__be16	magic;
	unsigned char	firstused;
	unsigned char	slots;

	unsigned char	space[EFS_DIRBSIZE - EFS_DIRBLK_HEADERSIZE];
};

#define EFS_MAXENTS \
	((EFS_DIRBSIZE - EFS_DIRBLK_HEADERSIZE) / \
	 (EFS_DENTSIZE + sizeof(char)))

#define EFS_SLOTAT(dir, slot) EFS_REALOFF((dir)->space[slot])

#define EFS_REALOFF(offset) ((offset << 1))


static inline struct efs_inode_info *INODE_INFO(struct inode *inode)
{
	return container_of(inode, struct efs_inode_info, vfs_inode);
}

static inline struct efs_sb_info *SUPER_INFO(struct super_block *sb)
{
	return sb->s_fs_info;
}

struct statfs;
struct fid;

extern const struct inode_operations efs_dir_inode_operations;
extern const struct file_operations efs_dir_operations;
extern const struct address_space_operations efs_symlink_aops;

extern struct inode *efs_iget(struct super_block *, unsigned long);
extern efs_block_t efs_map_block(struct inode *, efs_block_t);
extern int efs_get_block(struct inode *, sector_t, struct buffer_head *, int);

extern struct dentry *efs_lookup(struct inode *, struct dentry *, unsigned int);
extern struct dentry *efs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type);
extern struct dentry *efs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type);
extern struct dentry *efs_get_parent(struct dentry *);
extern int efs_bmap(struct inode *, int);

#endif /* _EFS_EFS_H_ */
