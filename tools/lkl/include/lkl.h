#ifndef _LKL_H
#define _LKL_H

#ifdef __cplusplus
extern "C" {
#endif

#define _LKL_LIBC_COMPAT_H

#ifdef __cplusplus
#define class __lkl__class
#endif
#include <lkl/asm/syscalls.h>
#ifdef __cplusplus
#undef class
#endif

#if defined(__MINGW32__)
#define strtok_r strtok_s
#endif

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

int lkl_netdev_add(struct lkl_netdev *nd, struct lkl_netdev_args* args);

/**
* lkl_netdev_remove - remove a previously added network device
*
* Attempts to release all resources held by a network device created
* via lkl_netdev_add.
*
* @id - the network device id, as return by @lkl_netdev_add
*/
void lkl_netdev_remove(int id);

/**
 * lkl_netdev_free - frees a network device
 *
 * @nd - the network device to free
 */
void lkl_netdev_free(struct lkl_netdev *nd);

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
struct lkl_netdev *lkl_netdev_tap_create(const char *ifname, int offload);

/**
 * lkl_netdev_dpdk_create - create DPDK net_device for the virtio net backend
 *
 * @ifname - interface name for the DPDK device. The name for DPDK device is
 * only used for an internal use.
 */
struct lkl_netdev *lkl_netdev_dpdk_create(const char *ifname);

/**
 * lkl_netdev_vde_create - create VDE net_device for the virtio net backend
 *
 * @switch_path - path to the VDE switch directory. Needs to be started on host
 * in advance.
 */
struct lkl_netdev *lkl_netdev_vde_create(const char *switch_path);

/**
 * lkl_netdev_raw_create - create raw socket net_device for the virtio net
 *                         backend
 *
 * @ifname - interface name for the snoop device.
 */
struct lkl_netdev *lkl_netdev_raw_create(const char *ifname);

/**
 * lkl_netdev_macvtap_create - create macvtap net_device for the virtio
 * net backend
 *
 * @path - a file name for the macvtap device. need to be configured
 * on host in advance
 * @offload - offload bits for the device
 */
struct lkl_netdev *lkl_netdev_macvtap_create(const char *path, int offload);

/*
 * lkl_register_dbg_handler- register a signal handler that loads a debug lib.
 *
 * The signal handler is triggered by Ctrl-Z. It creates a new pthread which
 * call dbg_entrance().
 *
 * If you run the program from shell script, make sure you ignore SIGTSTP by
 * "trap '' TSTP" in the shell script.
 */
void lkl_register_dbg_handler();

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
int lkl_qdisc_add(int ifindex, char *root, char *type);

/**
 * lkl_qdisc_parse_add - Add a qdisc entry for an interface with strings
 *
 * @ifindex - the ifindex of the interface
 * @entries - strings of qdisc configurations in the form of
 *            "root|type;root|type;..."
 */
void lkl_qdisc_parse_add(int ifindex, char *entries);

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
