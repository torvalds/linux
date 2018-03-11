/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_FS_HOSTFS
#define __UM_FS_HOSTFS

#include <os.h>

/*
 * These are exactly the same definitions as in fs.h, but the names are
 * changed so that this file can be included in both kernel and user files.
 */

#define HOSTFS_ATTR_MODE	1
#define HOSTFS_ATTR_UID 	2
#define HOSTFS_ATTR_GID 	4
#define HOSTFS_ATTR_SIZE	8
#define HOSTFS_ATTR_ATIME	16
#define HOSTFS_ATTR_MTIME	32
#define HOSTFS_ATTR_CTIME	64
#define HOSTFS_ATTR_ATIME_SET	128
#define HOSTFS_ATTR_MTIME_SET	256

/* These two are unused by hostfs. */
#define HOSTFS_ATTR_FORCE	512	/* Not a change, but a change it */
#define HOSTFS_ATTR_ATTR_FLAG	1024

/*
 * If you are very careful, you'll notice that these two are missing:
 *
 * #define ATTR_KILL_SUID	2048
 * #define ATTR_KILL_SGID	4096
 *
 * and this is because they were added in 2.5 development.
 * Actually, they are not needed by most ->setattr() methods - they are set by
 * callers of notify_change() to notify that the setuid/setgid bits must be
 * dropped.
 * notify_change() will delete those flags, make sure attr->ia_valid & ATTR_MODE
 * is on, and remove the appropriate bits from attr->ia_mode (attr is a
 * "struct iattr *"). -BlaisorBlade
 */

struct hostfs_iattr {
	unsigned int	ia_valid;
	unsigned short	ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	loff_t		ia_size;
	struct timespec	ia_atime;
	struct timespec	ia_mtime;
	struct timespec	ia_ctime;
};

struct hostfs_stat {
	unsigned long long ino;
	unsigned int mode;
	unsigned int nlink;
	unsigned int uid;
	unsigned int gid;
	unsigned long long size;
	struct timespec atime, mtime, ctime;
	unsigned int blksize;
	unsigned long long blocks;
	unsigned int maj;
	unsigned int min;
};

extern int stat_file(const char *path, struct hostfs_stat *p, int fd);
extern int access_file(char *path, int r, int w, int x);
extern int open_file(char *path, int r, int w, int append);
extern void *open_dir(char *path, int *err_out);
extern void seek_dir(void *stream, unsigned long long pos);
extern char *read_dir(void *stream, unsigned long long *pos_out,
		      unsigned long long *ino_out, int *len_out,
		      unsigned int *type_out);
extern void close_file(void *stream);
extern int replace_file(int oldfd, int fd);
extern void close_dir(void *stream);
extern int read_file(int fd, unsigned long long *offset, char *buf, int len);
extern int write_file(int fd, unsigned long long *offset, const char *buf,
		      int len);
extern int lseek_file(int fd, long long offset, int whence);
extern int fsync_file(int fd, int datasync);
extern int file_create(char *name, int mode);
extern int set_attr(const char *file, struct hostfs_iattr *attrs, int fd);
extern int make_symlink(const char *from, const char *to);
extern int unlink_file(const char *file);
extern int do_mkdir(const char *file, int mode);
extern int hostfs_do_rmdir(const char *file);
extern int do_mknod(const char *file, int mode, unsigned int major,
		    unsigned int minor);
extern int link_file(const char *from, const char *to);
extern int hostfs_do_readlink(char *file, char *buf, int size);
extern int rename_file(char *from, char *to);
extern int rename2_file(char *from, char *to, unsigned int flags);
extern int do_statfs(char *root, long *bsize_out, long long *blocks_out,
		     long long *bfree_out, long long *bavail_out,
		     long long *files_out, long long *ffree_out,
		     void *fsid_out, int fsid_size, long *namelen_out);

#endif
