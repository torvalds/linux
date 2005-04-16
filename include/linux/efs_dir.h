/*
 * efs_dir.h
 *
 * Copyright (c) 1999 Al Smith
 */

#ifndef __EFS_DIR_H__
#define __EFS_DIR_H__

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

#endif /* __EFS_DIR_H__ */

