#ifndef _VIRTIO_NET_FD_H
#define _VIRTIO_NET_FD_H

struct ifreq;

struct lkl_netdev_fd {
	struct lkl_netdev dev;
	/* file-descriptor based device */
	int fd;
	/*
	 * Controlls the poll mask for fd. Can be acccessed concurrently from
	 * poll, tx, or rx routines but there is no need for syncronization
	 * because:
	 *
	 * (a) TX and RX routines set different variables so even if they update
	 * at the same time there is no race condition
	 *
	 * (b) Even if poll and TX / RX update at the same time poll cannot
	 * stall: when poll resets the poll variable we know that TX / RX will
	 * run which means that eventually the poll variable will be set.
	 */
	int poll_tx, poll_rx;
	/* controle pipe */
	int pipe[2];
};

/**
 * lkl_register_netdev_linux_fdnet - register a file descriptor-based network
 * device as a NIC
 *
 * @fd - a POSIX file descriptor number for input/output
 * @returns a struct lkl_netdev_linux_fdnet entry for virtio-net
 */
struct lkl_netdev_fd *lkl_register_netdev_fd(int fd);


/**
 * lkl_unregister_netdev_linux_fdnet - unregister a file descriptor-based
 * network device as a NIC
 *
 * @nd - a struct lkl_netdev_linux_fdnet entry to be unregistered
 */
void lkl_unregister_netdev_fd(struct lkl_netdev_fd *nd);

/**
 * lkl_netdev_tap_init - initialize tap related structure fot lkl_netdev.
 *
 * @path - the path to open the device.
 * @offload - offload bits for the device
 * @ifr - struct ifreq for ioctl.
 */
struct lkl_netdev_fd *lkl_netdev_tap_init(const char *path, int offload,
					  struct ifreq *ifr);

#endif /* _VIRTIO_NET_FD_H*/
