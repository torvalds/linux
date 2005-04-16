/*
 * efs_fs_i.h
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from IRIX header files (c) 1988 Silicon Graphics
 */

#ifndef	__EFS_FS_I_H__
#define	__EFS_FS_I_H__

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

#endif	/* __EFS_FS_I_H__ */

