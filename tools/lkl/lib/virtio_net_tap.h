#ifndef _VIRTIO_NET_TAP_H
#define _VIRTIO_NET_TAP_H

extern struct lkl_netdev_tap_ops {
	/* We need this so that we can "unhijack" this function in
	 * case we decided to hijack it. */
	int (*eventfd)(unsigned int initval, int flags);
} lkl_netdev_tap_ops;

#endif /* _VIRTIO_NET_TAP_H*/
