/*
 * Header for the HDMI CEC core infrastructure
 */
#ifndef __HDMI_CEC_H
#define __HDMI_CEC_H

#include <linux/kernel.h>
#include <linux/types.h>

/* Common defines for HDMI CEC */
#define CEC_BCAST_ADDR		(0x0f)
#define CEC_ADDR_MAX		CEC_BCAST_ADDR

#define CEC_MAX_MSG_LEN		(16)	/* 16 blocks */

/**
 * struct cec_msg - user-space exposed cec message cookie
 * @data:	cec message payload
 * @len:	cec message length
 * @timeout:	signed 32-bits timeout value in seconds
 * @flags:	optionnal flag field
 */
struct cec_msg {
	__u8	data[CEC_MAX_MSG_LEN];
	__u8	len;
	__s32	timeout;
	__u32	 flags;
};

#define CEC_MSG_NONBLOCK	(1 << 0)
#define CEC_MSG_BLOCK		(1 << 1)

/* Counters */

/**
 * struct cec_rx_counters - cec adpater RX counters
 * @inv_start_bit:	number of invalid start bit detected
 * @ack:		number of frames acknowledged
 * @timeout:		number of timeouts
 * @bytes:		number of bytes received
 * @error:		number of general purpose errors
 */
struct cec_rx_counters {
	__u32	inv_start_bit;
	__u32	ack;
	__u32	timeout;
	__u32	bytes;
	__u32	error;
};

/**
 * struct cec_tx_counters - cec adapter TX counters
 * @busy:	number of busy events while attempting transmission
 * @bytes:	number of bytes transfered
 * @error:	number of general purpose errors
 * @retrans:	number of retransmissions
 * @arb_loss:	number of arbitration losses
 */
struct cec_tx_counters {
	__u32	busy;
	__u32	bytes;
	__u32	error;
	__u32	retrans;
	__u32	arb_loss;
};

/**
 * struct cec_counters - tx and rx cec counters
 * @rx:	struct cec_rx_counters
 * @tx: struct cec_tx_counters
 */
struct cec_counters {
	struct cec_rx_counters	rx;
	struct cec_tx_counters	tx;
};

/**
 * enum cec_rx_mode - cec adapter rx mode
 * @CEC_RX_MODE_DEFAULT:	accept only unicast traffic
 * @CEC_RX_MODE_ACCEPT_ALL:	accept all incoming RX traffic (sniffing mode)
 * @CEC_RX_MODE_MAX:		sentinel
 */
enum cec_rx_mode {
	CEC_RX_MODE_DEFAULT	= 0,
	CEC_RX_MODE_ACCEPT_ALL,
	CEC_RX_MODE_MAX
};

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

struct cec_driver;

#define CEC_HW_HAS_COUNTERS	(1 << 0)	/* HW counts events */
#define CEC_HW_HAS_RX_FILTER	(1 << 1)	/* HW has receive filter */

/**
 * struct cec_driver_ops - cec driver low-level operations
 * @set_logical_address:	callback to set the logical address
 * @send:	callback to send a cec payload
 * @reset:	callback to reset the hardware
 * @get_counters:	callback to get the counters (if supported by HW)
 * @set_rx_mode:	callback to set the receive mode
 * @attach:	callback to attach the host to the device
 * @detach:	callbackt to detach the host from the device
 */
struct cec_driver_ops {
	int	(*set_logical_address)(struct cec_driver *, const u8);
	int	(*send)(struct cec_driver *, const u8 *, const u8);
	int	(*reset)(struct cec_driver *);
	int	(*get_counters)(struct cec_driver *d, struct cec_counters *);
	int	(*set_rx_mode)(struct cec_driver *, enum cec_rx_mode);
	int	(*attach)(struct cec_driver *);
	int	(*detach)(struct cec_driver *);
};

/**
 * struct cec_driver - cec driver structure
 * @name:		driver name (should be unique)
 * @module:		module pointer for refcounting
 * @driver:		struct device_driver cookie for driver model
 * @ops:		struct cec_driver_ops pointer
 * @flags:		driver flags bitmask
 * @lock:		driver all-purpose mutex for exclusive locking
 * @attached:		driver attached to host or not
 * @tx_work:		transmit work queue
 * @tx_msg_list:	transmit message list head
 * @tx_msg_list_lock:	transmit lessage list lock
 * @rx_msg_list:	receive message list head
 * @rx_msg_list_lock:	receive message list lock
 * @rx_msg_len:		receive message queue len
 * @rx_wait:		receive waitqueue (used for poll, read)
 * @priv:		driver private pointer
 */
struct cec_driver {
	const char		*name;
	struct module		*module;
	struct device_driver	driver;
	struct cec_driver_ops	*ops;
	unsigned int		flags;

	/* private */
	struct mutex		lock;
	bool			attached;

	/* transmit message list */
	struct work_struct	tx_work;
	struct list_head	tx_msg_list;
	struct mutex		tx_msg_list_lock;

	/* receive message list */
	struct list_head	rx_msg_list;
	struct mutex		rx_msg_list_lock;
	unsigned int		rx_msg_len;
	wait_queue_head_t	rx_wait;

	/* driver private cookie */
	void			*priv;
};

static inline struct cec_driver *to_cec_driver(struct device_driver *d)
{
	return container_of(d, struct cec_driver, driver);
}

static inline void *cec_driver_priv(struct cec_driver *d)
{
	return d->priv;
}

int register_cec_driver(struct cec_driver *);
void unregister_cec_driver(struct cec_driver *);

/**
 * struct cec_device - CEC device main structure
 * @name:	device name (used to create the character device)
 * @list:	list node
 * @major:	device major number
 * @minor:	device minor number
 * @cdev:	character device node
 * @dev:	device structure for device/driver model interaction
 * @class_dev:	class device pointer
 */
struct cec_device {
	const char		*name;
	struct list_head	list;
	int			major;
	int			minor;
	struct cdev		cdev;
	struct device		dev;
	struct device		*class_dev;
};

static inline struct cec_device *to_cec_device(struct device *d)
{
	return container_of(d, struct cec_device, dev);
}

int register_cec_device(struct cec_device *);
void unregister_cec_device(struct cec_device *);

/*
 * CEC messages
 */
enum {
	CEC_MSG_QUEUED = 0,
	CEC_MSG_SENT,
	CEC_MSG_COMPLETED
};

enum {
	CEC_MSG_NO_RESP = 0,
	CEC_MSG_RESP,
};

/**
 * struct cec_kmsg - kernel-side cec message cookie
 * @status:	message status (QUEUED, SENT, COMPLETED)
 * @ret:	message sending return code
 * @next:	list pointer to next message
 * @completion:	message completion cookie
 * @msg:	user-side cec message cookie
 */
struct cec_kmsg {
	int			status;
	int			ret;
	struct list_head	next;
	struct completion	completion;
	struct cec_msg		msg;
};

int cec_receive_message(struct cec_driver *drv, const u8 *data, const u8 len);
int cec_dequeue_message(struct cec_driver *drv, struct cec_msg *msg);
int cec_read_message(struct cec_driver *drv, struct cec_msg *msg);
int cec_send_message(struct cec_driver *drv, struct cec_msg *msg);
int cec_reset_device(struct cec_driver *drv);
int cec_get_counters(struct cec_driver *drv, struct cec_counters *cnt);
int cec_set_logical_address(struct cec_driver *drv, const u8 addr);
int cec_set_rx_mode(struct cec_driver *drv, enum cec_rx_mode mode);
void cec_flush_queues(struct cec_driver *drv);
unsigned __cec_rx_queue_len(struct cec_driver *drv);
int cec_attach_host(struct cec_driver *drv);
int cec_detach_host(struct cec_driver *drv);

#endif /* __KERNEL__ */

#endif /* __HDMI_CEC_H */
