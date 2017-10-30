#ifndef _LINUX_IF_TAP_H_
#define _LINUX_IF_TAP_H_

#if IS_ENABLED(CONFIG_TAP)
struct socket *tap_get_socket(struct file *);
struct skb_array *tap_get_skb_array(struct file *file);
#else
#include <linux/err.h>
#include <linux/errno.h>
struct file;
struct socket;
static inline struct socket *tap_get_socket(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
static inline struct skb_array *tap_get_skb_array(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
#endif /* CONFIG_TAP */

#include <net/sock.h>
#include <linux/skb_array.h>

#define MAX_TAP_QUEUES 256

struct tap_queue;

struct tap_dev {
	struct net_device	*dev;
	u16			flags;
	/* This array tracks active taps. */
	struct tap_queue    __rcu *taps[MAX_TAP_QUEUES];
	/* This list tracks all taps (both enabled and disabled) */
	struct list_head	queue_list;
	int			numvtaps;
	int			numqueues;
	netdev_features_t	tap_features;
	int			minor;

	void (*update_features)(struct tap_dev *tap, netdev_features_t features);
	void (*count_tx_dropped)(struct tap_dev *tap);
	void (*count_rx_dropped)(struct tap_dev *tap);
};

/*
 * A tap queue is the central object of tap module, it connects
 * an open character device to virtual interface. There can be
 * multiple queues on one interface, which map back to queues
 * implemented in hardware on the underlying device.
 *
 * tap_proto is used to allocate queues through the sock allocation
 * mechanism.
 *
 */

struct tap_queue {
	struct sock sk;
	struct socket sock;
	struct socket_wq wq;
	int vnet_hdr_sz;
	struct tap_dev __rcu *tap;
	struct file *file;
	unsigned int flags;
	u16 queue_index;
	bool enabled;
	struct list_head next;
	struct skb_array skb_array;
};

rx_handler_result_t tap_handle_frame(struct sk_buff **pskb);
void tap_del_queues(struct tap_dev *tap);
int tap_get_minor(dev_t major, struct tap_dev *tap);
void tap_free_minor(dev_t major, struct tap_dev *tap);
int tap_queue_resize(struct tap_dev *tap);
int tap_create_cdev(struct cdev *tap_cdev, dev_t *tap_major,
		    const char *device_name, struct module *module);
void tap_destroy_cdev(dev_t major, struct cdev *tap_cdev);

#endif /*_LINUX_IF_TAP_H_*/
