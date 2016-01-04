#ifndef _LKL_H
#define _LKL_H

#define _LKL_LIBC_COMPAT_H

#include <lkl/asm/syscalls.h>

#if __LKL__BITS_PER_LONG == 64
#define lkl_sys_stat lkl_sys_newstat
#define lkl_sys_lstat lkl_sys_newlstat
#define lkl_sys_fstatat lkl_sys_newfstatat
#define lkl_sys_fstat lkl_sys_newfstat
#else
#define lkl_stat lkl_stat64
#define lkl_sys_stat lkl_sys_stat64
#define lkl_sys_lstat lkl_sys_lstat64
#define lkl_sys_truncate lkl_sys_truncate64
#define lkl_sys_ftruncate lkl_sys_ftruncate64
#define lkl_sys_sendfile lkl_sys_sendfile64
#define lkl_sys_fstatat lkl_sys_fstatat64
#define lkl_sys_fstat lkl_sys_fstat64

#define lkl_statfs lkl_statfs64

static inline int lkl_sys_statfs(const char *path, struct lkl_statfs *buf)
{
	return lkl_sys_statfs64(path, sizeof(*buf), buf);
}

static inline int lkl_sys_fstatfs(unsigned int fd, struct lkl_statfs *buf)
{
	return lkl_sys_fstatfs64(fd, sizeof(*buf), buf);
}

#define lkl_sys_statfs lkl_sys_statsf64
#define lkl_sys_fstatfs lkl_sys_fstatsf64
#endif

#ifdef __lkl__NR_llseek
/**
 * lkl_sys_lseek - wrapper for lkl_sys_llseek
 */
static inline long long lkl_sys_lseek(unsigned int fd, __lkl__kernel_loff_t off,
				      unsigned int whence)
{
	long long res;
	long ret = lkl_sys_llseek(fd, off >> 32, off & 0xffffffff, &res, whence);

	return ret < 0 ? ret : res;
}
#endif

/**
 * lkl_strerror - returns a string describing the given error code
 *
 * @err - error code
 * @returns - string for the given error code
 */
const char *lkl_strerror(int err);

/**
 * lkl_disk - host disk handle
 *
 * @fd - a POSIX file descriptor that can be used by preadv/pwritev
 * @handle - an NT file handle that can be used by ReadFile/WriteFile
 */
union lkl_disk {
	int fd;
	void *handle;
};

/**
 * lkl_disk_add - add a new disk
 *
 * Must be called before calling lkl_start_kernel.
 *
 * @disk - the host disk handle
 * @returns a disk id (0 is valid) or a strictly negative value in case of error
 */
int lkl_disk_add(union lkl_disk disk);

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
 * the filesystem has been mounted
 * @mnt_str_len - size of mnt_str
 * @returns - 0 on success, a negative value on error
 */
long lkl_mount_dev(unsigned int disk_id, const char *fs_type, int flags,
		   void *data, char *mnt_str, unsigned int mnt_str_len);

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
int lkl_closedir(struct lkl_dir *dir);

/**
 * lkl_readdir - get the next available entry of the directory
 *
 * @dir - the directory handler as returned by lkl_opendir
 * @returns - a lkl_dirent64 entry or NULL if the end of the directory stream is
 * reached or if an error occurred; check lkl_errdir() to distinguish between
 * errors or end of the directory stream
 */
struct lkl_linux_dirent64 *lkl_readdir(struct lkl_dir *dir);

/**
 * lkl_errdir - checks if an error occurred during the last lkl_readdir call
 *
 * @dir - the directory handler as returned by lkl_opendir
 * @returns - 0 if no error occurred, or a negative value otherwise
 */
int lkl_errdir(struct lkl_dir *dir);

/**
 * lkl_dirfd - gets the file descriptor associated with the directory handle
 *
 * @dir - the directory handle as returned by lkl_opendir
 * @returns - a positive value,which is the LKL file descriptor associated with
 * the directory handle, or a negative value otherwise
 */
int lkl_dirfd(struct lkl_dir *dir);

/**
 * lkl_if_up - activate network interface
 *
 * @ifindex - the ifindex of the interface
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_up(int ifindex);

/**
 * lkl_if_down - deactivate network interface
 *
 * @ifindex - the ifindex of the interface
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_down(int ifindex);

/**
 * lkl_if_set_ipv4 - set IPv4 address on interface
 *
 * @ifindex - the ifindex of the interface
 * @addr - 4-byte IP address (i.e., struct in_addr)
 * @netmask_len - prefix length of the @addr
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_set_ipv4(int ifindex, unsigned int addr, unsigned int netmask_len);

/**
 * lkl_set_ipv4_gateway - add an IPv4 default route
 *
 * @addr - 4-byte IP address of the gateway (i.e., struct in_addr)
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_set_ipv4_gateway(unsigned int addr);

/**
 * lkl_netdev - host network device handle
 *
 * @fd - TAP device or packet socket file descriptor
 */
union lkl_netdev {
	int fd;
};

/**
 * lkl_netdev_add - add a new network device
 *
 * Must be called before calling lkl_start_kernel.
 *
 * @nd - the network device host handle
 * @mac - optional MAC address for the device
 * @returns a network device id (0 is valid) or a strictly negative value in
 * case of error
 */
int lkl_netdev_add(union lkl_netdev nd, void *mac);

/**
 * lkl_netdev_get_ifindex - retrieve the interface index for a given network
 * device id
 *
 * @id - the network device id
 * @returns the interface index or a stricly negative value in case of error
 */
int lkl_netdev_get_ifindex(int id);

#endif
