/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_FILEATTR_H
#define _LINUX_FILEATTR_H

/* Flags shared betwen flags/xflags */
#define FS_COMMON_FL \
	(FS_SYNC_FL | FS_IMMUTABLE_FL | FS_APPEND_FL | \
	 FS_NODUMP_FL |	FS_NOATIME_FL | FS_DAX_FL | \
	 FS_PROJINHERIT_FL)

#define FS_XFLAG_COMMON \
	(FS_XFLAG_SYNC | FS_XFLAG_IMMUTABLE | FS_XFLAG_APPEND | \
	 FS_XFLAG_NODUMP | FS_XFLAG_NOATIME | FS_XFLAG_DAX | \
	 FS_XFLAG_PROJINHERIT)

/* Read-only inode flags */
#define FS_XFLAG_RDONLY_MASK \
	(FS_XFLAG_PREALLOC | FS_XFLAG_HASATTR)

/* Flags to indicate valid value of fsx_ fields */
#define FS_XFLAG_VALUES_MASK \
	(FS_XFLAG_EXTSIZE | FS_XFLAG_COWEXTSIZE)

/* Flags for directories */
#define FS_XFLAG_DIRONLY_MASK \
	(FS_XFLAG_RTINHERIT | FS_XFLAG_NOSYMLINKS | FS_XFLAG_EXTSZINHERIT)

/* Misc settable flags */
#define FS_XFLAG_MISC_MASK \
	(FS_XFLAG_REALTIME | FS_XFLAG_NODEFRAG | FS_XFLAG_FILESTREAM)

#define FS_XFLAGS_MASK \
	(FS_XFLAG_COMMON | FS_XFLAG_RDONLY_MASK | FS_XFLAG_VALUES_MASK | \
	 FS_XFLAG_DIRONLY_MASK | FS_XFLAG_MISC_MASK)

/*
 * Merged interface for miscellaneous file attributes.  'flags' originates from
 * ext* and 'fsx_flags' from xfs.  There's some overlap between the two, which
 * is handled by the VFS helpers, so filesystems are free to implement just one
 * or both of these sub-interfaces.
 */
struct file_kattr {
	u32	flags;		/* flags (FS_IOC_GETFLAGS/FS_IOC_SETFLAGS) */
	/* struct fsxattr: */
	u32	fsx_xflags;	/* xflags field value (get/set) */
	u32	fsx_extsize;	/* extsize field value (get/set)*/
	u32	fsx_nextents;	/* nextents field value (get)	*/
	u32	fsx_projid;	/* project identifier (get/set) */
	u32	fsx_cowextsize;	/* CoW extsize field value (get/set)*/
	/* selectors: */
	bool	flags_valid:1;
	bool	fsx_valid:1;
};

int copy_fsxattr_to_user(const struct file_kattr *fa, struct fsxattr __user *ufa);

void fileattr_fill_xflags(struct file_kattr *fa, u32 xflags);
void fileattr_fill_flags(struct file_kattr *fa, u32 flags);

/**
 * fileattr_has_fsx - check for extended flags/attributes
 * @fa:		fileattr pointer
 *
 * Return: true if any attributes are present that are not represented in
 * ->flags.
 */
static inline bool fileattr_has_fsx(const struct file_kattr *fa)
{
	return fa->fsx_valid &&
		((fa->fsx_xflags & ~FS_XFLAG_COMMON) || fa->fsx_extsize != 0 ||
		 fa->fsx_projid != 0 ||	fa->fsx_cowextsize != 0);
}

int vfs_fileattr_get(struct dentry *dentry, struct file_kattr *fa);
int vfs_fileattr_set(struct mnt_idmap *idmap, struct dentry *dentry,
		     struct file_kattr *fa);
int ioctl_getflags(struct file *file, unsigned int __user *argp);
int ioctl_setflags(struct file *file, unsigned int __user *argp);
int ioctl_fsgetxattr(struct file *file, void __user *argp);
int ioctl_fssetxattr(struct file *file, void __user *argp);

#endif /* _LINUX_FILEATTR_H */
