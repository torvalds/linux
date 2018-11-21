#ifndef _LKL_H
#define _LKL_H

#include "lkl_autoconf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _LKL_LIBC_COMPAT_H

#ifdef __cplusplus
#define class __lkl__class
#endif

/*
 * Avoid collisions between Android which defines __unused and
 * linux/icmp.h which uses __unused as a structure field.
 */
#pragma push_macro("__unused")
#undef __unused

#include <lkl/asm/syscalls.h>

#pragma pop_macro("__unused")

#ifdef __cplusplus
#undef class
#endif

#if defined(__MINGW32__)
#define strtok_r strtok_s
#define inet_pton lkl_inet_pton

int inet_pton(int af, const char *src, void *dst);
#endif

#if defined(__ANDROID__) && __LKL__BITS_PER_LONG == 32
#define __lkl__NR_fcntl __lkl__NR_fcntl64
#endif

#if __LKL__BITS_PER_LONG == 64
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
#define lkl_sys_fcntl lkl_sys_fcntl64

#define lkl_statfs lkl_statfs64

static inline int lkl_sys_statfs(const char *path, struct lkl_statfs *buf)
{
	return lkl_sys_statfs64(path, sizeof(*buf), buf);
}

static inline int lkl_sys_fstatfs(unsigned int fd, struct lkl_statfs *buf)
{
	return lkl_sys_fstatfs64(fd, sizeof(*buf), buf);
}

#endif

static inline int lkl_sys_stat(const char *path, struct lkl_stat *buf)
{
	return lkl_sys_fstatat(LKL_AT_FDCWD, path, buf, 0);
}

static inline int lkl_sys_lstat(const char *path, struct lkl_stat *buf)
{
	return lkl_sys_fstatat(LKL_AT_FDCWD, path, buf,
			       LKL_AT_SYMLINK_NOFOLLOW);
}

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

static inline void *lkl_sys_mmap(void *addr, size_t length, int prot, int flags,
				 int fd, off_t offset)
{
	return (void *)lkl_sys_mmap_pgoff((long)addr, length, prot, flags, fd,
					  offset >> 12);
}

#define lkl_sys_mmap2 lkl_sys_mmap_pgoff

#ifdef __lkl__NR_openat
/**
 * lkl_sys_open - wrapper for lkl_sys_openat
 */
static inline long lkl_sys_open(const char *file, int flags, int mode)
{
	return lkl_sys_openat(LKL_AT_FDCWD, file, flags, mode);
}

/**
 * lkl_sys_creat - wrapper for lkl_sys_openat
 */
static inline long lkl_sys_creat(const char *file, int mode)
{
	return lkl_sys_openat(LKL_AT_FDCWD, file,
			      LKL_O_CREAT|LKL_O_WRONLY|LKL_O_TRUNC, mode);
}
#endif


#ifdef __lkl__NR_faccessat
/**
 * lkl_sys_access - wrapper for lkl_sys_faccessat
 */
static inline long lkl_sys_access(const char *file, int mode)
{
	return lkl_sys_faccessat(LKL_AT_FDCWD, file, mode);
}
#endif

#ifdef __lkl__NR_fchownat
/**
 * lkl_sys_chown - wrapper for lkl_sys_fchownat
 */
static inline long lkl_sys_chown(const char *path, lkl_uid_t uid, lkl_gid_t gid)
{
	return lkl_sys_fchownat(LKL_AT_FDCWD, path, uid, gid, 0);
}
#endif

#ifdef __lkl__NR_fchmodat
/**
 * lkl_sys_chmod - wrapper for lkl_sys_fchmodat
 */
static inline long lkl_sys_chmod(const char *path, mode_t mode)
{
	return lkl_sys_fchmodat(LKL_AT_FDCWD, path, mode);
}
#endif

#ifdef __lkl__NR_linkat
/**
 * lkl_sys_link - wrapper for lkl_sys_linkat
 */
static inline long lkl_sys_link(const char *existing, const char *new)
{
	return lkl_sys_linkat(LKL_AT_FDCWD, existing, LKL_AT_FDCWD, new, 0);
}
#endif

#ifdef __lkl__NR_unlinkat
/**
 * lkl_sys_unlink - wrapper for lkl_sys_unlinkat
 */
static inline long lkl_sys_unlink(const char *path)
{
	return lkl_sys_unlinkat(LKL_AT_FDCWD, path, 0);
}
#endif

#ifdef __lkl__NR_symlinkat
/**
 * lkl_sys_symlink - wrapper for lkl_sys_symlinkat
 */
static inline long lkl_sys_symlink(const char *existing, const char *new)
{
	return lkl_sys_symlinkat(existing, LKL_AT_FDCWD, new);
}
#endif

#ifdef __lkl__NR_readlinkat
/**
 * lkl_sys_readlink - wrapper for lkl_sys_readlinkat
 */
static inline long lkl_sys_readlink(const char *path, char *buf, size_t bufsize)
{
	return lkl_sys_readlinkat(LKL_AT_FDCWD, path, buf, bufsize);
}
#endif

#ifdef __lkl__NR_renameat
/**
 * lkl_sys_rename - wrapper for lkl_sys_renameat
 */
static inline long lkl_sys_rename(const char *old, const char *new)
{
	return lkl_sys_renameat(LKL_AT_FDCWD, old, LKL_AT_FDCWD, new);
}
#endif

#ifdef __lkl__NR_mkdirat
/**
 * lkl_sys_mkdir - wrapper for lkl_sys_mkdirat
 */
static inline long lkl_sys_mkdir(const char *path, mode_t mode)
{
	return lkl_sys_mkdirat(LKL_AT_FDCWD, path, mode);
}
#endif

#ifdef __lkl__NR_unlinkat
/**
 * lkl_sys_rmdir - wrapper for lkl_sys_unlinkrat
 */
static inline long lkl_sys_rmdir(const char *path)
{
	return lkl_sys_unlinkat(LKL_AT_FDCWD, path, LKL_AT_REMOVEDIR);
}
#endif

#ifdef __lkl__NR_mknodat
/**
 * lkl_sys_mknod - wrapper for lkl_sys_mknodat
 */
static inline long lkl_sys_mknod(const char *path, mode_t mode, dev_t dev)
{
	return lkl_sys_mknodat(LKL_AT_FDCWD, path, mode, dev);
}
#endif

#ifdef __lkl__NR_pipe2
/**
 * lkl_sys_pipe - wrapper for lkl_sys_pipe2
 */
static inline long lkl_sys_pipe(int fd[2])
{
	return lkl_sys_pipe2(fd, 0);
}
#endif

#ifdef __lkl__NR_sendto
/**
 * lkl_sys_send - wrapper for lkl_sys_sendto
 */
static inline long lkl_sys_send(int fd, void *buf, size_t len, int flags)
{
	return lkl_sys_sendto(fd, buf, len, flags, 0, 0);
}
#endif

#ifdef __lkl__NR_recvfrom
/**
 * lkl_sys_recv - wrapper for lkl_sys_recvfrom
 */
static inline long lkl_sys_recv(int fd, void *buf, size_t len, int flags)
{
	return lkl_sys_recvfrom(fd, buf, len, flags, 0, 0);
}
#endif

#ifdef __lkl__NR_pselect6
/**
 * lkl_sys_select - wrapper for lkl_sys_pselect
 */
static inline long lkl_sys_select(int n, lkl_fd_set *rfds, lkl_fd_set *wfds,
				  lkl_fd_set *efds, struct lkl_timeval *tv)
{
	long data[2] = { 0, _LKL_NSIG/8 };
	struct lkl_timespec ts;
	lkl_time_t extra_secs;
	const lkl_time_t max_time = ((1ULL<<8)*sizeof(time_t)-1)-1;

	if (tv) {
		if (tv->tv_sec < 0 || tv->tv_usec < 0)
			return -LKL_EINVAL;

		extra_secs = tv->tv_usec / 1000000;
		ts.tv_nsec = tv->tv_usec % 1000000 * 1000;
		ts.tv_sec = extra_secs > max_time - tv->tv_sec ?
			max_time : tv->tv_sec + extra_secs;
	}
	return lkl_sys_pselect6(n, rfds, wfds, efds, tv ? &ts : 0, data);
}
#endif

#ifdef __lkl__NR_ppoll
/**
 * lkl_sys_poll - wrapper for lkl_sys_ppoll
 */
static inline long lkl_sys_poll(struct lkl_pollfd *fds, int n, int timeout)
{
	return lkl_sys_ppoll(fds, n, timeout >= 0 ?
			     &((struct lkl_timespec){ .tv_sec = timeout/1000,
				   .tv_nsec = timeout%1000*1000000 }) : 0,
			     0, _LKL_NSIG/8);
}
#endif

#ifdef __lkl__NR_epoll_create1
/**
 * lkl_sys_epoll_create - wrapper for lkl_sys_epoll_create1
 */
static inline long lkl_sys_epoll_create(int size)
{
	return lkl_sys_epoll_create1(0);
}
#endif

#ifdef __lkl__NR_epoll_pwait
/**
 * lkl_sys_epoll_wait - wrapper for lkl_sys_epoll_pwait
 */
static inline long lkl_sys_epoll_wait(int fd, struct lkl_epoll_event *ev,
				      int cnt, int to)
{
	return lkl_sys_epoll_pwait(fd, ev, cnt, to, 0, _LKL_NSIG/8);
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
 * lkl_perror - prints a string describing the given error code
 *
 * @msg - prefix for the error message
 * @err - error code
 */
void lkl_perror(char *msg, int err);

/**
 * struct lkl_dev_blk_ops - block device host operations, defined in lkl_host.h.
 */
struct lkl_dev_blk_ops;

/**
 * lkl_disk - host disk handle
 *
 * @dev - a pointer to 'virtio_blk_dev' structure for this disk
 * @fd - a POSIX file descriptor that can be used by preadv/pwritev
 * @handle - an NT file handle that can be used by ReadFile/WriteFile
 */
struct lkl_disk {
	void *dev;
	union {
		int fd;
		void *handle;
	};
	struct lkl_dev_blk_ops *ops;
};

/**
 * lkl_disk_add - add a new disk
 *
 * @disk - the host disk handle
 * @returns a disk id (0 is valid) or a strictly negative value in case of error
 */
int lkl_disk_add(struct lkl_disk *disk);

/**
 * lkl_disk_remove - remove a disk
 *
 * This function makes a cleanup of the @disk's virtio_dev structure
 * that was initialized by lkl_disk_add before.
 *
 * @disk - the host disk handle
 */
int lkl_disk_remove(struct lkl_disk disk);

/**
 * lkl_get_virtiolkl_encode_dev_from_sysfs_blkdev - extract device id from sysfs
 *
 * This function returns the device id for the given sysfs dev node.
 * The content of the node has to be in the form 'MAJOR:MINOR'.
 * Also, this function expects an absolute path which means that sysfs
 * already has to be mounted at the given path
 *
 * @sysfs_path - absolute path to the sysfs dev node
 * @pdevid - pointer to memory where dev id will be returned
 * @returns - 0 on success, a negative value on error
 */
int lkl_encode_dev_from_sysfs(const char *sysfs_path, uint32_t *pdevid);

/**
 * lkl_get_virtio_blkdev - get device id of a disk (partition)
 *
 * This function returns the device id for the given disk.
 *
 * @disk_id - the disk id identifying the disk
 * @part - disk partition or zero for full disk
 * @pdevid - pointer to memory where dev id will be returned
 * @returns - 0 on success, a negative value on error
 */
int lkl_get_virtio_blkdev(int disk_id, unsigned int part, uint32_t *pdevid);


/**
 * lkl_mount_dev - mount a disk
 *
 * This functions creates a device file for the given disk, creates a mount
 * point and mounts the device over the mount point.
 *
 * @disk_id - the disk id identifying the disk to be mounted
 * @part - disk partition or zero for full disk
 * @fs_type - filesystem type
 * @flags - mount flags
 * @opts - additional filesystem specific mount options
 * @mnt_str - a string that will be filled by this function with the path where
 * the filesystem has been mounted
 * @mnt_str_len - size of mnt_str
 * @returns - 0 on success, a negative value on error
 */
long lkl_mount_dev(unsigned int disk_id, unsigned int part, const char *fs_type,
		   int flags, const char *opts,
		   char *mnt_str, unsigned int mnt_str_len);

/**
 * lkl_umount_dev - umount a disk
 *
 * This functions umounts the given disks and removes the device file and the
 * mount point.
 *
 * @disk_id - the disk id identifying the disk to be mounted
 * @part - disk partition or zero for full disk
 * @flags - umount flags
 * @timeout_ms - timeout to wait for the kernel to flush closed files so that
 * umount can succeed
 * @returns - 0 on success, a negative value on error
 */
long lkl_umount_dev(unsigned int disk_id, unsigned int part, int flags,
		    long timeout_ms);

/**
 * lkl_umount_timeout - umount filesystem with timeout
 *
 * @path - the path to unmount
 * @flags - umount flags
 * @timeout_ms - timeout to wait for the kernel to flush closed files so that
 * umount can succeed
 * @returns - 0 on success, a negative value on error
 */
long lkl_umount_timeout(char *path, int flags, long timeout_ms);

/**
 * lkl_opendir - open a directory
 *
 * @path - directory path
 * @err - pointer to store the error in case of failure
 * @returns - a handle to be used when calling lkl_readdir
 */
struct lkl_dir *lkl_opendir(const char *path, int *err);

/**
 * lkl_fdopendir - open a directory
 *
 * @fd - file descriptor
 * @err - pointer to store the error in case of failure
 * @returns - a handle to be used when calling lkl_readdir
 */
struct lkl_dir *lkl_fdopendir(int fd, int *err);

/**
 * lkl_rewinddir - reset directory stream
 *
 * @dir - the directory handler as returned by lkl_opendir
 */
void lkl_rewinddir(struct lkl_dir *dir);

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
 * lkl_if_set_mtu - set MTU on interface
 *
 * @ifindex - the ifindex of the interface
 * @mtu - the requested MTU size
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_set_mtu(int ifindex, int mtu);

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
 * lkl_if_set_ipv4_gateway - add an IPv4 default route in rule table
 *
 * @ifindex - the ifindex of the interface, used for tableid calculation
 * @addr - 4-byte IP address of the interface
 * @netmask_len - prefix length of the @addr
 * @gw_addr - 4-byte IP address of the gateway
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_set_ipv4_gateway(int ifindex, unsigned int addr,
		unsigned int netmask_len, unsigned int gw_addr);

/**
 * lkl_if_set_ipv6 - set IPv6 address on interface
 * must be called after interface is up.
 *
 * @ifindex - the ifindex of the interface
 * @addr - 16-byte IPv6 address (i.e., struct in6_addr)
 * @netprefix_len - prefix length of the @addr
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_set_ipv6(int ifindex, void* addr, unsigned int netprefix_len);

/**
 * lkl_set_ipv6_gateway - add an IPv6 default route
 *
 * @addr - 16-byte IPv6 address of the gateway (i.e., struct in6_addr)
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_set_ipv6_gateway(void* addr);

/**
 * lkl_if_set_ipv6_gateway - add an IPv6 default route in rule table
 *
 * @ifindex - the ifindex of the interface, used for tableid calculation
 * @addr - 16-byte IP address of the interface
 * @netmask_len - prefix length of the @addr
 * @gw_addr - 16-byte IP address of the gateway (i.e., struct in_addr)
 * @returns - return 0 if no error: otherwise negative value returns
 */
int lkl_if_set_ipv6_gateway(int ifindex, void *addr,
		unsigned int netmask_len, void *gw_addr);

/**
 * lkl_ifname_to_ifindex - obtain ifindex of an interface by name
 *
 * @name - string of an interface
 * @returns - return an integer of ifindex if no error
 */
int lkl_ifname_to_ifindex(const char *name);

/**
 * lkl_netdev - host network device handle, defined in lkl_host.h.
 */
struct lkl_netdev;

/**
* lkl_netdev_args - arguments to lkl_netdev_add
* @mac - optional MAC address for the device
* @offload - offload bits for the device
*/
struct lkl_netdev_args {
	void *mac;
	unsigned int offload;
};

/**
 * lkl_netdev_add - add a new network device
 *
 * Must be called before calling lkl_start_kernel.
 *
 * @nd - the network device host handle
 * @args - arguments that configs the netdev. Can be NULL
 * @returns a network device id (0 is valid) or a strictly negative value in
 * case of error
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET
int lkl_netdev_add(struct lkl_netdev *nd, struct lkl_netdev_args* args);
#else
static inline int lkl_netdev_add(struct lkl_netdev *nd,
				 struct lkl_netdev_args *args)
{
	return -LKL_ENOSYS;
}
#endif

/**
* lkl_netdev_remove - remove a previously added network device
*
* Attempts to release all resources held by a network device created
* via lkl_netdev_add.
*
* @id - the network device id, as return by @lkl_netdev_add
*/
#ifdef LKL_HOST_CONFIG_VIRTIO_NET
void lkl_netdev_remove(int id);
#else
static inline void lkl_netdev_remove(int id)
{
}
#endif

/**
 * lkl_netdev_free - frees a network device
 *
 * @nd - the network device to free
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET
void lkl_netdev_free(struct lkl_netdev *nd);
#else
static inline void lkl_netdev_free(struct lkl_netdev *nd)
{
}
#endif

/**
 * lkl_netdev_get_ifindex - retrieve the interface index for a given network
 * device id
 *
 * @id - the network device id
 * @returns the interface index or a stricly negative value in case of error
 */
int lkl_netdev_get_ifindex(int id);

/**
 * lkl_netdev_tap_create - create TAP net_device for the virtio net backend
 *
 * @ifname - interface name for the TAP device. need to be configured
 * on host in advance
 * @offload - offload bits for the device
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET
struct lkl_netdev *lkl_netdev_tap_create(const char *ifname, int offload);
#else
static inline struct lkl_netdev *
lkl_netdev_tap_create(const char *ifname, int offload)
{
	return NULL;
}
#endif

/**
 * lkl_netdev_dpdk_create - create DPDK net_device for the virtio net backend
 *
 * @ifname - interface name for the DPDK device. The name for DPDK device is
 * only used for an internal use.
 * @offload - offload bits for the device
 * @mac - mac address pointer of dpdk-ed device
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET_DPDK
struct lkl_netdev *lkl_netdev_dpdk_create(const char *ifname, int offload,
					unsigned char *mac);
#else
static inline struct lkl_netdev *
lkl_netdev_dpdk_create(const char *ifname, int offload, unsigned char *mac)
{
	return NULL;
}
#endif

/**
 * lkl_netdev_vde_create - create VDE net_device for the virtio net backend
 *
 * @switch_path - path to the VDE switch directory. Needs to be started on host
 * in advance.
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET_VDE
struct lkl_netdev *lkl_netdev_vde_create(const char *switch_path);
#else
static inline struct lkl_netdev *lkl_netdev_vde_create(const char *switch_path)
{
	return NULL;
}
#endif

/**
 * lkl_netdev_raw_create - create raw socket net_device for the virtio net
 *                         backend
 *
 * @ifname - interface name for the snoop device.
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET
struct lkl_netdev *lkl_netdev_raw_create(const char *ifname);
#else
static inline struct lkl_netdev *lkl_netdev_raw_create(const char *ifname)
{
	return NULL;
}
#endif

/**
 * lkl_netdev_macvtap_create - create macvtap net_device for the virtio
 * net backend
 *
 * @path - a file name for the macvtap device. need to be configured
 * on host in advance
 * @offload - offload bits for the device
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET_MACVTAP
struct lkl_netdev *lkl_netdev_macvtap_create(const char *path, int offload);
#else
static inline struct lkl_netdev *
lkl_netdev_macvtap_create(const char *path, int offload)
{
	return NULL;
}
#endif

/**
 * lkl_netdev_pipe_create - create pipe net_device for the virtio
 * net backend
 *
 * @ifname - a file name for the rx and tx pipe device. need to be configured
 * on host in advance. delimiter is "|". e.g. "rx_name|tx_name".
 * @offload - offload bits for the device
 */
#ifdef LKL_HOST_CONFIG_VIRTIO_NET
struct lkl_netdev *lkl_netdev_pipe_create(const char *ifname, int offload);
#else
static inline struct lkl_netdev *
lkl_netdev_pipe_create(const char *ifname, int offload)
{
	return NULL;
}
#endif

/*
 * lkl_register_dbg_handler- register a signal handler that loads a debug lib.
 *
 * The signal handler is triggered by Ctrl-Z. It creates a new pthread which
 * call dbg_entrance().
 *
 * If you run the program from shell script, make sure you ignore SIGTSTP by
 * "trap '' TSTP" in the shell script.
 */
void lkl_register_dbg_handler(void);

/**
 * lkl_add_neighbor - add a permanent arp entry
 * @ifindex - the ifindex of the interface
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @ip - ip address of the entry in network byte order
 * @mac - mac address of the entry
 */
int lkl_add_neighbor(int ifindex, int af, void* addr, void* mac);

/**
 * lkl_mount_fs - mount a file system type like proc, sys
 * @fstype - file system type. e.g. proc, sys
 * @returns - 0 on success. 1 if it's already mounted. negative on failure.
 */
int lkl_mount_fs(char *fstype);

/**
 * lkl_if_add_ip - add an ip address
 * @ifindex - the ifindex of the interface
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @addr - ip address of the entry in network byte order
 * @netprefix_len - prefix length of the @addr
 */
int lkl_if_add_ip(int ifindex, int af, void *addr, unsigned int netprefix_len);

/**
 * lkl_if_del_ip - add an ip address
 * @ifindex - the ifindex of the interface
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @addr - ip address of the entry in network byte order
 * @netprefix_len - prefix length of the @addr
 */
int lkl_if_del_ip(int ifindex, int af, void *addr, unsigned int netprefix_len);

/**
 * lkl_add_gateway - add a gateway
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @gwaddr - 4-byte IP address of the gateway (i.e., struct in_addr)
 */
int lkl_add_gateway(int af, void *gwaddr);

/**
 * XXX Should I use OIF selector?
 * temporary table idx = ifindex * 2 + 0 <- ipv4
 * temporary table idx = ifindex * 2 + 1 <- ipv6
 */
/**
 * lkl_if_add_rule_from_addr - create an ip rule table with "from" selector
 * @ifindex - the ifindex of the interface, used for table id calculation
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @saddr - network byte order ip address, "from" selector address of this rule
 */
int lkl_if_add_rule_from_saddr(int ifindex, int af, void *saddr);

/**
 * lkl_if_add_gateway - add gateway to rule table
 * @ifindex - the ifindex of the interface, used for table id calculation
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @gwaddr - 4-byte IP address of the gateway (i.e., struct in_addr)
 */
int lkl_if_add_gateway(int ifindex, int af, void *gwaddr);

/**
 * lkl_if_add_linklocal - add linklocal route to rule table
 * @ifindex - the ifindex of the interface, used for table id calculation
 * @af - address family of the ip address. Must be LKL_AF_INET or LKL_AF_INET6
 * @addr - ip address of the entry in network byte order
 * @netprefix_len - prefix length of the @addr
 */
int lkl_if_add_linklocal(int ifindex, int af,  void *addr, int netprefix_len);

/**
 * lkl_if_wait_ipv6_dad - wait for DAD to be done for a ipv6 address
 * must be called after interface is up
 *
 * @ifindex - the ifindex of the interface
 * @addr - ip address of the entry in network byte order
 */
int lkl_if_wait_ipv6_dad(int ifindex, void *addr);

/**
 * lkl_set_fd_limit - set the maximum number of file descriptors allowed
 * @fd_limit - fd max limit
 */
int lkl_set_fd_limit(unsigned int fd_limit);

/**
 * lkl_qdisc_add - set qdisc rule onto an interface
 *
 * @ifindex - the ifindex of the interface
 * @root - the name of root class (e.g., "root");
 * @type - the type of qdisc (e.g., "fq")
 */
int lkl_qdisc_add(int ifindex, const char *root, const char *type);

/**
 * lkl_qdisc_parse_add - Add a qdisc entry for an interface with strings
 *
 * @ifindex - the ifindex of the interface
 * @entries - strings of qdisc configurations in the form of
 *            "root|type;root|type;..."
 */
void lkl_qdisc_parse_add(int ifindex, const char *entries);

/**
 * lkl_sysctl - write a sysctl value
 *
 * @path - the path to an sysctl entry (e.g., "net.ipv4.tcp_wmem");
 * @value - the value of the sysctl (e.g., "4096 87380 2147483647")
 */
int lkl_sysctl(const char *path, const char *value);

/**
 * lkl_sysctl_parse_write - Configure sysctl parameters with strings
 *
 * @sysctls - Configure sysctl parameters as the form of "key=value;..."
 */
void lkl_sysctl_parse_write(const char *sysctls);

#ifdef __cplusplus
}
#endif

#endif
