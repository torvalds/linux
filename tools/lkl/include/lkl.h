#ifndef _LKL_H
#define _LKL_H

#include <lkl/asm/unistd.h>

/**
 * lkl_sys_lseek - wrapper for lkl_sys_llseek
 */
static inline long lkl_sys_lseek(unsigned int fd, __lkl__kernel_loff_t off,
				 __lkl__kernel_loff_t *res, unsigned int whence)
{
	return lkl_sys_llseek(fd, off >> 32, off & 0xffffffff, res, whence);
}

/**
 * lkl_strerror - returns a string describing the given error code
 *
 * @err - error code
 * @returns - string for the given error code
 */
const char *lkl_strerror(int err);

/**
 * lkl_disk_backstore - host dependend disk backstore
 *
 * @fd - a POSIX file descriptor that can be used by preadv/pwritev
 * @handle - an NT file handle that can be used by ReadFile/WriteFile
 */
union lkl_disk_backstore {
	int fd;
	void *handle;
};

/**
 * lkl_disk_add - add a new disk
 *
 * Must be called before calling lkl_start_kernel.
 *
 * @backstore - the disk backstore
 * @returns a disk id (0 is valid) or a strictly negative value in case of error
 */
int lkl_disk_add(union lkl_disk_backstore backstore);

/**
 * lkl_mount_dev - mount a disk
 *
 * This functions creates a device file for the given disk, creates a mount
 * point and mounts the device over the mount point.
 *
 * @disk_id - the disk id identifying the disk to be mounted
 * @fs_type - filesystem type
 * @flags - mount flags
 * @data - additional filesystem specific mount data
 * @mnt_str - a string that will be filled by this function with the path where
 * the filisystem has been mounted
 * @mnt_str_len - size of mnt_str
 * @returns - 0 on success, a negative value on error
 */
long lkl_mount_dev(unsigned int disk_id, const char *fs_type, int flags,
		   void *data, char *mnt_str, int mnt_str_len);

/**
 * lkl_umount_dev - umount a disk
 *
 * This functions umounts the given disks and removes the device file and the
 * mount point.
 *
 * @disk_id - the disk id identifying the disk to be mounted
 * @flags - umount flags
 * @timeout_ms - timeout to wait for the kernel to flush closed files so that
 * umount can succeed
 * @returns - 0 on success, a negative value on error
 */
long lkl_umount_dev(unsigned int disk_id, int flags, long timeout_ms);

/**
 * lkl_opendir - open a directory
 *
 * @path - directory path
 * @err - pointer to store the error in case of failure
 * @returns - a handle to be used when calling lkl_readdir
 */
struct lkl_dir *lkl_opendir(const char *path, int *err);

/**
 * lkl_closedir - close the directory
 *
 * @dir - the directory handler as returned by lkl_opendir
 */
void lkl_closedir(struct lkl_dir *dir);

/**
 * lkl_readdir - get the next available entry of the directory
 *
 * @dir - the directory handler as returned by lkl_opendir
 * @returns - a lkl_dirent64 entry or NULL if the end of the directory stream is
 * reached or if an error occurred; check lkl_errdir() to distinguish between
 * errors or end of the directory stream
 */
struct lkl_dirent64 *lkl_readdir(struct lkl_dir *dir);

/**
 * lkl_errdir - checks if an error occurred during the last lkl_readdir call
 *
 * @dir - the directory handler as returned by lkl_opendir
 * @returns - 0 if no error occurred, or a negative value otherwise
 */
int lkl_errdir(struct lkl_dir *dir);

#endif
