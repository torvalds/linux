#ifndef _CEC_DEVNODE_H
#define _CEC_DEVNODE_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/cec.h>
#include <media/rc-core.h>

#define cec_phys_addr_exp(pa) \
	((pa) >> 12), ((pa) >> 8) & 0xf, ((pa) >> 4) & 0xf, (pa) & 0xf

/*
 * Flag to mark the cec_devnode struct as registered. Drivers must not touch
 * this flag directly, it will be set and cleared by cec_devnode_register and
 * cec_devnode_unregister.
 */
#define CEC_FLAG_REGISTERED	0

/**
 * struct cec_devnode - cec device node
 * @parent:	parent device
 * @minor:	device node minor number
 * @flags:	flags, combination of the CEC_FLAG_* constants
 *
 * This structure represents a cec-related device node.
 *
 * The @parent is a physical device. It must be set by core or device drivers
 * before registering the node.
 */
struct cec_devnode {
	/* sysfs */
	struct device dev;		/* cec device */
	struct cdev cdev;		/* character device */
	struct device *parent;		/* device parent */

	/* device info */
	int minor;
	unsigned long flags;		/* Use bitops to access flags */

	/* callbacks */
	void (*release)(struct cec_devnode *cecdev);
};

static inline int cec_devnode_is_registered(struct cec_devnode *cecdev)
{
	return test_bit(CEC_FLAG_REGISTERED, &cecdev->flags);
}

struct cec_adapter;
struct cec_data;

typedef int (*cec_notify)(struct cec_adapter *adap, struct cec_data *data, void *priv);
typedef int (*cec_recv_notify)(struct cec_adapter *adap, struct cec_msg *msg);

struct cec_data {
	struct cec_msg msg;
	cec_notify func;
	void *priv;
};

/* Unconfigured state */
#define CEC_ADAP_STATE_DISABLED		0
#define CEC_ADAP_STATE_UNCONF		1
#define CEC_ADAP_STATE_IDLE		2
#define CEC_ADAP_STATE_TRANSMITTING	3
#define CEC_ADAP_STATE_WAIT		4
#define CEC_ADAP_STATE_RECEIVED		5

#define CEC_TX_QUEUE_SZ	(4)
#define CEC_RX_QUEUE_SZ	(4)
#define CEC_EV_QUEUE_SZ	(16)

struct cec_adapter {
	struct module *owner;
	const char *name;
	struct cec_devnode devnode;
	struct mutex lock;
	struct rc_dev *rc;

	struct cec_data tx_queue[CEC_TX_QUEUE_SZ];
	u8 tx_qstart, tx_qcount;

	struct cec_msg rx_queue[CEC_RX_QUEUE_SZ];
	u8 rx_qstart, rx_qcount;

	struct cec_event ev_queue[CEC_EV_QUEUE_SZ];
	u8 ev_qstart, ev_qcount;

	cec_recv_notify recv_notifier;
	struct task_struct *kthread_config;

	struct task_struct *kthread;
	wait_queue_head_t kthread_waitq;
	wait_queue_head_t waitq;

	u8 state;
	u32 capabilities;
	u16 phys_addr;
	u8 version;
	u8 num_log_addrs;
	u8 key_passthrough;
	u8 prim_device[CEC_MAX_LOG_ADDRS];
	u8 log_addr_type[CEC_MAX_LOG_ADDRS];
	u8 log_addr[CEC_MAX_LOG_ADDRS];

	char input_name[32];
	char input_phys[32];
	char input_drv[32];

	int (*adap_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_log_addr)(struct cec_adapter *adap, u8 logical_addr);
	int (*adap_transmit)(struct cec_adapter *adap, struct cec_msg *msg);
	void (*adap_transmit_timed_out)(struct cec_adapter *adap);

	void (*claimed_log_addr)(struct cec_adapter *adap, u8 idx);
	int (*received)(struct cec_adapter *adap, struct cec_msg *msg);
};

#define to_cec_adapter(node) container_of(node, struct cec_adapter, devnode)

int cec_create_adapter(struct cec_adapter *adap, const char *name, u32 caps);
void cec_delete_adapter(struct cec_adapter *adap);
int cec_transmit_msg(struct cec_adapter *adap, struct cec_data *data, bool block);
int cec_receive_msg(struct cec_adapter *adap, struct cec_msg *msg, bool block);
void cec_post_event(struct cec_adapter *adap, u32 event);
int cec_claim_log_addrs(struct cec_adapter *adap, struct cec_log_addrs *log_addrs, bool block);
int cec_enable(struct cec_adapter *adap, bool enable);

/* Called by the adapter */
void cec_transmit_done(struct cec_adapter *adap, u32 status);
void cec_received_msg(struct cec_adapter *adap, struct cec_msg *msg);

#endif /* _CEC_DEVNODE_H */
