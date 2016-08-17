#ifndef _VIRTIO_NET_FD_H
#define _VIRTIO_NET_FD_H

struct ifreq;

/**
 * lkl_register_netdev_linux_fdnet - register a file descriptor-based network
 * device as a NIC
 *
 * @fd - a POSIX file descriptor number for input/output
 * @returns a struct lkl_netdev_linux_fdnet entry for virtio-net
 */
struct lkl_netdev *lkl_register_netdev_fd(int fd);


/**
 * lkl_netdev_tap_init - initialize tap related structure fot lkl_netdev.
 *
 * @path - the path to open the device.
 * @offload - offload bits for the device
 * @ifr - struct ifreq for ioctl.
 */
struct lkl_netdev *lkl_netdev_tap_init(const char *path, int offload,
				       struct ifreq *ifr);

#endif /* _VIRTIO_NET_FD_H*/
