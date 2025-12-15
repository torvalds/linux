/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_FS_HOSTFS
#define __UM_FS_HOSTFS

#include <os.h>
#include <generated/asm-offsets.h>

struct hostfs_timespec {
	long long tv_sec;
	long long tv_nsec;
};

struct hostfs_iattr {
	unsigned int		ia_valid;
	unsigned short		ia_mode;
	uid_t			ia_uid;
	gid_t			ia_gid;
	loff_t			ia_size;
	struct hostfs_timespec	ia_atime;
	struct hostfs_timespec	ia_mtime;
	struct hostfs_timespec	ia_ctime;
};

struct hostfs_stat {
	unsigned long long ino;
	unsigned int mode;
	unsigned int nlink;
	unsigned int uid;
	unsigned int gid;
	unsigned long long size;
	struct hostfs_timespec atime, mtime, ctime, btime;
	unsigned int blksize;
	unsigned long long blocks;
	struct {
		unsigned int maj;
		unsigned int min;
	} rdev, dev;
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
extern int link_file(const char *to, const char *from);
extern int hostfs_do_readlink(char *file, char *buf, int size);
extern int rename_file(char *from, char *to);
extern int rename2_file(char *from, char *to, unsigned int flags);
extern int do_statfs(char *root, long *bsize_out, long long *blocks_out,
		     long long *bfree_out, long long *bavail_out,
		     long long *files_out, long long *ffree_out,
		     void *fsid_out, int fsid_size, long *namelen_out);

#endif
