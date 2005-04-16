/*
 *  linux/include/linux/adfs_fs_sb.h
 *
 * Copyright (C) 1997-1999 Russell King
 */

#ifndef _ADFS_FS_SB
#define _ADFS_FS_SB

/*
 * Forward-declare this
 */
struct adfs_discmap;
struct adfs_dir_ops;

/*
 * ADFS file system superblock data in memory
 */
struct adfs_sb_info {
	struct adfs_discmap *s_map;	/* bh list containing map		 */
	struct adfs_dir_ops *s_dir;	/* directory operations			 */

	uid_t		s_uid;		/* owner uid				 */
	gid_t		s_gid;		/* owner gid				 */
	umode_t		s_owner_mask;	/* ADFS owner perm -> unix perm		 */
	umode_t		s_other_mask;	/* ADFS other perm -> unix perm		 */

	__u32		s_ids_per_zone;	/* max. no ids in one zone		 */
	__u32		s_idlen;	/* length of ID in map			 */
	__u32		s_map_size;	/* sector size of a map			 */
	unsigned long	s_size;		/* total size (in blocks) of this fs	 */
	signed int	s_map2blk;	/* shift left by this for map->sector	 */
	unsigned int	s_log2sharesize;/* log2 share size			 */
	__le32		s_version;	/* disc format version			 */
	unsigned int	s_namelen;	/* maximum number of characters in name	 */
};

#endif
