/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include "hostfs.h"
#include "kern_util.h"
#include "user.h"

int stat_file(const char *path, unsigned long long *inode_out, int *mode_out,
	      int *nlink_out, int *uid_out, int *gid_out,
	      unsigned long long *size_out, struct timespec *atime_out,
	      struct timespec *mtime_out, struct timespec *ctime_out,
	      int *blksize_out, unsigned long long *blocks_out, int fd)
{
	struct stat64 buf;

	if(fd >= 0) {
		if (fstat64(fd, &buf) < 0)
			return -errno;
	} else if(lstat64(path, &buf) < 0) {
		return -errno;
	}

	if(inode_out != NULL) *inode_out = buf.st_ino;
	if(mode_out != NULL) *mode_out = buf.st_mode;
	if(nlink_out != NULL) *nlink_out = buf.st_nlink;
	if(uid_out != NULL) *uid_out = buf.st_uid;
	if(gid_out != NULL) *gid_out = buf.st_gid;
	if(size_out != NULL) *size_out = buf.st_size;
	if(atime_out != NULL) {
		atime_out->tv_sec = buf.st_atime;
		atime_out->tv_nsec = 0;
	}
	if(mtime_out != NULL) {
		mtime_out->tv_sec = buf.st_mtime;
		mtime_out->tv_nsec = 0;
	}
	if(ctime_out != NULL) {
		ctime_out->tv_sec = buf.st_ctime;
		ctime_out->tv_nsec = 0;
	}
	if(blksize_out != NULL) *blksize_out = buf.st_blksize;
	if(blocks_out != NULL) *blocks_out = buf.st_blocks;
	return 0;
}

int file_type(const char *path, int *maj, int *min)
{
 	struct stat64 buf;

	if(lstat64(path, &buf) < 0)
		return -errno;
	/*We cannot pass rdev as is because glibc and the kernel disagree
	 *about its definition.*/
	if(maj != NULL)
		*maj = major(buf.st_rdev);
	if(min != NULL)
		*min = minor(buf.st_rdev);

	if(S_ISDIR(buf.st_mode)) return OS_TYPE_DIR;
	else if(S_ISLNK(buf.st_mode)) return OS_TYPE_SYMLINK;
	else if(S_ISCHR(buf.st_mode)) return OS_TYPE_CHARDEV;
	else if(S_ISBLK(buf.st_mode)) return OS_TYPE_BLOCKDEV;
	else if(S_ISFIFO(buf.st_mode))return OS_TYPE_FIFO;
	else if(S_ISSOCK(buf.st_mode))return OS_TYPE_SOCK;
	else return OS_TYPE_FILE;
}

int access_file(char *path, int r, int w, int x)
{
	int mode = 0;

	if(r) mode = R_OK;
	if(w) mode |= W_OK;
	if(x) mode |= X_OK;
	if(access(path, mode) != 0)
		return -errno;
	else return 0;
}

int open_file(char *path, int r, int w, int append)
{
	int mode = 0, fd;

	if(r && !w)
		mode = O_RDONLY;
	else if(!r && w)
		mode = O_WRONLY;
	else if(r && w)
		mode = O_RDWR;
	else panic("Impossible mode in open_file");

	if(append)
		mode |= O_APPEND;
	fd = open64(path, mode);
	if(fd < 0)
		return -errno;
	else return fd;
}

void *open_dir(char *path, int *err_out)
{
	DIR *dir;

	dir = opendir(path);
	*err_out = errno;
	if(dir == NULL)
		return NULL;
	return dir;
}

char *read_dir(void *stream, unsigned long long *pos,
	       unsigned long long *ino_out, int *len_out)
{
	DIR *dir = stream;
	struct dirent *ent;

	seekdir(dir, *pos);
	ent = readdir(dir);
	if(ent == NULL)
		return NULL;
	*len_out = strlen(ent->d_name);
	*ino_out = ent->d_ino;
	*pos = telldir(dir);
	return ent->d_name;
}

int read_file(int fd, unsigned long long *offset, char *buf, int len)
{
	int n;

	n = pread64(fd, buf, len, *offset);
	if(n < 0)
		return -errno;
	*offset += n;
	return n;
}

int write_file(int fd, unsigned long long *offset, const char *buf, int len)
{
	int n;

	n = pwrite64(fd, buf, len, *offset);
	if(n < 0)
		return -errno;
	*offset += n;
	return n;
}

int lseek_file(int fd, long long offset, int whence)
{
	int ret;

	ret = lseek64(fd, offset, whence);
	if(ret < 0)
		return -errno;
	return 0;
}

int fsync_file(int fd, int datasync)
{
	int ret;
	if (datasync)
		ret = fdatasync(fd);
	else
		ret = fsync(fd);

	if (ret < 0)
		return -errno;
	return 0;
}

void close_file(void *stream)
{
	close(*((int *) stream));
}

void close_dir(void *stream)
{
	closedir(stream);
}

int file_create(char *name, int ur, int uw, int ux, int gr,
		int gw, int gx, int or, int ow, int ox)
{
	int mode, fd;

	mode = 0;
	mode |= ur ? S_IRUSR : 0;
	mode |= uw ? S_IWUSR : 0;
	mode |= ux ? S_IXUSR : 0;
	mode |= gr ? S_IRGRP : 0;
	mode |= gw ? S_IWGRP : 0;
	mode |= gx ? S_IXGRP : 0;
	mode |= or ? S_IROTH : 0;
	mode |= ow ? S_IWOTH : 0;
	mode |= ox ? S_IXOTH : 0;
	fd = open64(name, O_CREAT | O_RDWR, mode);
	if(fd < 0)
		return -errno;
	return fd;
}

int set_attr(const char *file, struct hostfs_iattr *attrs, int fd)
{
	struct timeval times[2];
	struct timespec atime_ts, mtime_ts;
	int err, ma;

	if (attrs->ia_valid & HOSTFS_ATTR_MODE) {
		if (fd >= 0) {
			if (fchmod(fd, attrs->ia_mode) != 0)
				return (-errno);
		} else if (chmod(file, attrs->ia_mode) != 0) {
			return -errno;
		}
	}
	if (attrs->ia_valid & HOSTFS_ATTR_UID) {
		if (fd >= 0) {
			if (fchown(fd, attrs->ia_uid, -1))
				return -errno;
		} else if(chown(file, attrs->ia_uid, -1)) {
			return -errno;
		}
	}
	if (attrs->ia_valid & HOSTFS_ATTR_GID) {
		if (fd >= 0) {
			if (fchown(fd, -1, attrs->ia_gid))
				return -errno;
		} else if (chown(file, -1, attrs->ia_gid)) {
			return -errno;
		}
	}
	if (attrs->ia_valid & HOSTFS_ATTR_SIZE) {
		if (fd >= 0) {
			if (ftruncate(fd, attrs->ia_size))
				return -errno;
		} else if (truncate(file, attrs->ia_size)) {
			return -errno;
		}
	}

	/* Update accessed and/or modified time, in two parts: first set
	 * times according to the changes to perform, and then call futimes()
	 * or utimes() to apply them. */
	ma = (HOSTFS_ATTR_ATIME_SET | HOSTFS_ATTR_MTIME_SET);
	if (attrs->ia_valid & ma) {
		err = stat_file(file, NULL, NULL, NULL, NULL, NULL, NULL,
				&atime_ts, &mtime_ts, NULL, NULL, NULL, fd);
		if (err != 0)
			return err;

		times[0].tv_sec = atime_ts.tv_sec;
		times[0].tv_usec = atime_ts.tv_nsec * 1000;
		times[1].tv_sec = mtime_ts.tv_sec;
		times[1].tv_usec = mtime_ts.tv_nsec * 1000;

		if (attrs->ia_valid & HOSTFS_ATTR_ATIME_SET) {
			times[0].tv_sec = attrs->ia_atime.tv_sec;
			times[0].tv_usec = attrs->ia_atime.tv_nsec * 1000;
		}
		if (attrs->ia_valid & HOSTFS_ATTR_MTIME_SET) {
			times[1].tv_sec = attrs->ia_mtime.tv_sec;
			times[1].tv_usec = attrs->ia_mtime.tv_nsec * 1000;
		}

		if (fd >= 0) {
			if (futimes(fd, times) != 0)
				return -errno;
		} else if (utimes(file, times) != 0) {
			return -errno;
		}
	}

	/* Note: ctime is not handled */
	if(attrs->ia_valid & (HOSTFS_ATTR_ATIME | HOSTFS_ATTR_MTIME)){
		err = stat_file(file, NULL, NULL, NULL, NULL, NULL, NULL,
				&attrs->ia_atime, &attrs->ia_mtime, NULL,
				NULL, NULL, fd);
		if(err != 0)
			return err;
	}
	return 0;
}

int make_symlink(const char *from, const char *to)
{
	int err;

	err = symlink(to, from);
	if(err)
		return -errno;
	return 0;
}

int unlink_file(const char *file)
{
	int err;

	err = unlink(file);
	if(err)
		return -errno;
	return 0;
}

int do_mkdir(const char *file, int mode)
{
	int err;

	err = mkdir(file, mode);
	if(err)
		return -errno;
	return 0;
}

int do_rmdir(const char *file)
{
	int err;

	err = rmdir(file);
	if(err)
		return -errno;
	return 0;
}

int do_mknod(const char *file, int mode, unsigned int major, unsigned int minor)
{
	int err;

	err = mknod(file, mode, makedev(major, minor));
	if(err)
		return -errno;
	return 0;
}

int link_file(const char *to, const char *from)
{
	int err;

	err = link(to, from);
	if(err)
		return -errno;
	return 0;
}

int do_readlink(char *file, char *buf, int size)
{
	int n;

	n = readlink(file, buf, size);
	if(n < 0)
		return -errno;
	if(n < size)
		buf[n] = '\0';
	return n;
}

int rename_file(char *from, char *to)
{
	int err;

	err = rename(from, to);
	if(err < 0)
		return -errno;
	return 0;
}

int do_statfs(char *root, long *bsize_out, long long *blocks_out,
	      long long *bfree_out, long long *bavail_out,
	      long long *files_out, long long *ffree_out,
	      void *fsid_out, int fsid_size, long *namelen_out,
	      long *spare_out)
{
	struct statfs64 buf;
	int err;

	err = statfs64(root, &buf);
	if(err < 0)
		return -errno;

	*bsize_out = buf.f_bsize;
	*blocks_out = buf.f_blocks;
	*bfree_out = buf.f_bfree;
	*bavail_out = buf.f_bavail;
	*files_out = buf.f_files;
	*ffree_out = buf.f_ffree;
	memcpy(fsid_out, &buf.f_fsid,
	       sizeof(buf.f_fsid) > fsid_size ? fsid_size :
	       sizeof(buf.f_fsid));
	*namelen_out = buf.f_namelen;
	spare_out[0] = buf.f_spare[0];
	spare_out[1] = buf.f_spare[1];
	spare_out[2] = buf.f_spare[2];
	spare_out[3] = buf.f_spare[3];
	spare_out[4] = buf.f_spare[4];
	return 0;
}
