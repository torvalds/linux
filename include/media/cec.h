/*
 * cec - HDMI Consumer Electronics Control support header
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MEDIA_CEC_H
#define _MEDIA_CEC_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/timer.h>
#include <linux/cec-funcs.h>
#include <media/rc-core.h>
#include <media/cec-notifier.h>

#define CEC_CAP_DEFAULTS (CEC_CAP_LOG_ADDRS | CEC_CAP_TRANSMIT | \
			  CEC_CAP_PASSTHROUGH | CEC_CAP_RC)

/**
 * struct cec_devnode - cec device node
 * @dev:	cec device
 * @cdev:	cec character device
 * @minor:	device node minor number
 * @registered:	the device was correctly registered
 * @unregistered: the device was unregistered
 * @fhs_lock:	lock to control access to the filehandle list
 * @fhs:	the list of open filehandles (cec_fh)
 *
 * This structure represents a cec-related device node.
 *
 * The @parent is a physical device. It must be set by core or device drivers
 * before registering the node.
 */
struct cec_devnode {
	/* sysfs */
	struct device dev;
	struct cdev cdev;

	/* device info */
	int minor;
	bool registered;
	bool unregistered;
	struct list_head fhs;
	struct mutex lock;
};

struct cec_adapter;
struct cec_data;
struct cec_pin;

struct cec_data {
	struct list_head list;
	struct list_head xfer_list;
	struct cec_adapter *adap;
	struct cec_msg msg;
	struct cec_fh *fh;
	struct delayed_work work;
	struct completion c;
	u8 attempts;
	bool new_initiator;
	bool blocking;
	bool completed;
};

struct cec_msg_entry {
	struct list_head	list;
	struct cec_msg		msg;
};

struct cec_event_entry {
	struct list_head	list;
	struct cec_event	ev;
};

#define CEC_NUM_CORE_EVENTS 2
#define CEC_NUM_EVENTS CEC_EVENT_PIN_HPD_HIGH

struct cec_fh {
	struct list_head	list;
	struct list_head	xfer_list;
	struct cec_adapter	*adap;
	u8			mode_initiator;
	u8			mode_follower;

	/* Events */
	wait_queue_head_t	wait;
	struct mutex		lock;
	struct list_head	events[CEC_NUM_EVENTS]; /* queued events */
	u8			queued_events[CEC_NUM_EVENTS];
	unsigned int		total_queued_events;
	struct cec_event_entry	core_events[CEC_NUM_CORE_EVENTS];
	struct list_head	msgs; /* queued messages */
	unsigned int		queued_msgs;
};

#define CEC_SIGNAL_FREE_TIME_RETRY		3
#define CEC_SIGNAL_FREE_TIME_NEW_INITIATOR	5
#define CEC_SIGNAL_FREE_TIME_NEXT_XFER		7

/* The nominal data bit period is 2.4 ms */
#define CEC_FREE_TIME_TO_USEC(ft)		((ft) * 2400)

struct cec_adap_ops {
	/* Low-level callbacks */
	int (*adap_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_monitor_all_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_monitor_pin_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_log_addr)(struct cec_adapter *adap, u8 logical_addr);
	int (*adap_transmit)(struct cec_adapter *adap, u8 attempts,
			     u32 signal_free_time, struct cec_msg *msg);
	void (*adap_status)(struct cec_adapter *adap, struct seq_file *file);
	void (*adap_free)(struct cec_adapter *adap);

	/* High-level CEC message callback */
	int (*received)(struct cec_adapter *adap, struct cec_msg *msg);
};

/*
 * The minimum message length you can receive (excepting poll messages) is 2.
 * With a transfer rate of at most 36 bytes per second this makes 18 messages
 * per second worst case.
 *
 * We queue at most 3 seconds worth of received messages. The CEC specification
 * requires that messages are replied to within a second, so 3 seconds should
 * give more than enough margin. Since most messages are actually more than 2
 * bytes, this is in practice a lot more than 3 seconds.
 */
#define CEC_MAX_MSG_RX_QUEUE_SZ		(18 * 3)

/*
 * The transmit queue is limited to 1 second worth of messages (worst case).
 * Messages can be transmitted by userspace and kernel space. But for both it
 * makes no sense to have a lot of messages queued up. One second seems
 * reasonable.
 */
#define CEC_MAX_MSG_TX_QUEUE_SZ		(18 * 1)

struct cec_adapter {
	struct module *owner;
	char name[32];
	struct cec_devnode devnode;
	struct mutex lock;
	struct rc_dev *rc;

	struct list_head transmit_queue;
	unsigned int transmit_queue_sz;
	struct list_head wait_queue;
	struct cec_data *transmitting;

	struct task_struct *kthread_config;
	struct completion config_completion;

	struct task_struct *kthread;
	wait_queue_head_t kthread_waitq;
	wait_queue_head_t waitq;

	const struct cec_adap_ops *ops;
	void *priv;
	u32 capabilities;
	u8 available_log_addrs;

	u16 phys_addr;
	bool needs_hpd;
	bool is_configuring;
	bool is_configured;
	bool cec_pin_is_high;
	u32 monitor_all_cnt;
	u32 monitor_pin_cnt;
	u32 follower_cnt;
	struct cec_fh *cec_follower;
	struct cec_fh *cec_initiator;
	bool passthrough;
	struct cec_log_addrs log_addrs;

	u32 tx_timeouts;

#ifdef CONFIG_CEC_NOTIFIER
	struct cec_notifier *notifier;
#endif
#ifdef CONFIG_CEC_PIN
	struct cec_pin *pin;
#endif

	struct dentry *cec_dir;
	struct dentry *status_file;

	u16 phys_addrs[15];
	u32 sequence;

	char device_name[32];
	char input_phys[32];
	char input_drv[32];
};

static inline void *cec_get_drvdata(const struct cec_adapter *adap)
{
	return adap->priv;
}

static inline bool cec_has_log_addr(const struct cec_adapter *adap, u8 log_addr)
{
	return adap->log_addrs.log_addr_mask & (1 << log_addr);
}

static inline bool cec_is_sink(const struct cec_adapter *adap)
{
	return adap->phys_addr == 0;
}

/**
 * cec_is_registered() - is the CEC adapter registered?
 *
 * @adap:	the CEC adapter, may be NULL.
 *
 * Return: true if the adapter is registered, false otherwise.
 */
static inline bool cec_is_registered(const struct cec_adapter *adap)
{
	return adap && adap->devnode.registered;
}

#define cec_phys_addr_exp(pa) \
	((pa) >> 12), ((pa) >> 8) & 0xf, ((pa) >> 4) & 0xf, (pa) & 0xf

struct edid;

#if IS_REACHABLE(CONFIG_CEC_CORE)
struct cec_adapter *cec_allocate_adapter(const struct cec_adap_ops *ops,
		void *priv, const char *name, u32 caps, u8 available_las);
int cec_register_adapter(struct cec_adapter *adap, struct device *parent);
void cec_unregister_adapter(struct cec_adapter *adap);
void cec_delete_adapter(struct cec_adapter *adap);

int cec_s_log_addrs(struct cec_adapter *adap, struct cec_log_addrs *log_addrs,
		    bool block);
void cec_s_phys_addr(struct cec_adapter *adap, u16 phys_addr,
		     bool block);
void cec_s_phys_addr_from_edid(struct cec_adapter *adap,
			       const struct edid *edid);
int cec_transmit_msg(struct cec_adapter *adap, struct cec_msg *msg,
		     bool block);

/* Called by the adapter */
void cec_transmit_done_ts(struct cec_adapter *adap, u8 status,
			  u8 arb_lost_cnt, u8 nack_cnt, u8 low_drive_cnt,
			  u8 error_cnt, ktime_t ts);

static inline void cec_transmit_done(struct cec_adapter *adap, u8 status,
				     u8 arb_lost_cnt, u8 nack_cnt,
				     u8 low_drive_cnt, u8 error_cnt)
{
	cec_transmit_done_ts(adap, status, arb_lost_cnt, nack_cnt,
			     low_drive_cnt, error_cnt, ktime_get());
}
/*
 * Simplified version of cec_transmit_done for hardware that doesn't retry
 * failed transmits. So this is always just one attempt in which case
 * the status is sufficient.
 */
void cec_transmit_attempt_done_ts(struct cec_adapter *adap,
				  u8 status, ktime_t ts);

static inline void cec_transmit_attempt_done(struct cec_adapter *adap,
					     u8 status)
{
	cec_transmit_attempt_done_ts(adap, status, ktime_get());
}

void cec_received_msg_ts(struct cec_adapter *adap,
			 struct cec_msg *msg, ktime_t ts);

static inline void cec_received_msg(struct cec_adapter *adap,
				    struct cec_msg *msg)
{
	cec_received_msg_ts(adap, msg, ktime_get());
}

/**
 * cec_queue_pin_cec_event() - queue a CEC pin event with a given timestamp.
 *
 * @adap:	pointer to the cec adapter
 * @is_high:	when true the CEC pin is high, otherwise it is low
 * @ts:		the timestamp for this event
 *
 */
void cec_queue_pin_cec_event(struct cec_adapter *adap,
			     bool is_high, ktime_t ts);

/**
 * cec_queue_pin_hpd_event() - queue a pin event with a given timestamp.
 *
 * @adap:	pointer to the cec adapter
 * @is_high:	when true the HPD pin is high, otherwise it is low
 * @ts:		the timestamp for this event
 *
 */
void cec_queue_pin_hpd_event(struct cec_adapter *adap, bool is_high, ktime_t ts);

/**
 * cec_get_edid_phys_addr() - find and return the physical address
 *
 * @edid:	pointer to the EDID data
 * @size:	size in bytes of the EDID data
 * @offset:	If not %NULL then the location of the physical address
 *		bytes in the EDID will be returned here. This is set to 0
 *		if there is no physical address found.
 *
 * Return: the physical address or CEC_PHYS_ADDR_INVALID if there is none.
 */
u16 cec_get_edid_phys_addr(const u8 *edid, unsigned int size,
			   unsigned int *offset);

/**
 * cec_set_edid_phys_addr() - find and set the physical address
 *
 * @edid:	pointer to the EDID data
 * @size:	size in bytes of the EDID data
 * @phys_addr:	the new physical address
 *
 * This function finds the location of the physical address in the EDID
 * and fills in the given physical address and updates the checksum
 * at the end of the EDID block. It does nothing if the EDID doesn't
 * contain a physical address.
 */
void cec_set_edid_phys_addr(u8 *edid, unsigned int size, u16 phys_addr);

/**
 * cec_phys_addr_for_input() - calculate the PA for an input
 *
 * @phys_addr:	the physical address of the parent
 * @input:	the number of the input port, must be between 1 and 15
 *
 * This function calculates a new physical address based on the input
 * port number. For example:
 *
 * PA = 0.0.0.0 and input = 2 becomes 2.0.0.0
 *
 * PA = 3.0.0.0 and input = 1 becomes 3.1.0.0
 *
 * PA = 3.2.1.0 and input = 5 becomes 3.2.1.5
 *
 * PA = 3.2.1.3 and input = 5 becomes f.f.f.f since it maxed out the depth.
 *
 * Return: the new physical address or CEC_PHYS_ADDR_INVALID.
 */
u16 cec_phys_addr_for_input(u16 phys_addr, u8 input);

/**
 * cec_phys_addr_validate() - validate a physical address from an EDID
 *
 * @phys_addr:	the physical address to validate
 * @parent:	if not %NULL, then this is filled with the parents PA.
 * @port:	if not %NULL, then this is filled with the input port.
 *
 * This validates a physical address as read from an EDID. If the
 * PA is invalid (such as 1.0.1.0 since '0' is only allowed at the end),
 * then it will return -EINVAL.
 *
 * The parent PA is passed into %parent and the input port is passed into
 * %port. For example:
 *
 * PA = 0.0.0.0: has parent 0.0.0.0 and input port 0.
 *
 * PA = 1.0.0.0: has parent 0.0.0.0 and input port 1.
 *
 * PA = 3.2.0.0: has parent 3.0.0.0 and input port 2.
 *
 * PA = f.f.f.f: has parent f.f.f.f and input port 0.
 *
 * Return: 0 if the PA is valid, -EINVAL if not.
 */
int cec_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port);

#else

static inline int cec_register_adapter(struct cec_adapter *adap,
				       struct device *parent)
{
	return 0;
}

static inline void cec_unregister_adapter(struct cec_adapter *adap)
{
}

static inline void cec_delete_adapter(struct cec_adapter *adap)
{
}

static inline void cec_s_phys_addr(struct cec_adapter *adap, u16 phys_addr,
				   bool block)
{
}

static inline void cec_s_phys_addr_from_edid(struct cec_adapter *adap,
					     const struct edid *edid)
{
}

static inline u16 cec_get_edid_phys_addr(const u8 *edid, unsigned int size,
					 unsigned int *offset)
{
	if (offset)
		*offset = 0;
	return CEC_PHYS_ADDR_INVALID;
}

static inline void cec_set_edid_phys_addr(u8 *edid, unsigned int size,
					  u16 phys_addr)
{
}

static inline u16 cec_phys_addr_for_input(u16 phys_addr, u8 input)
{
	return CEC_PHYS_ADDR_INVALID;
}

static inline int cec_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port)
{
	if (parent)
		*parent = phys_addr;
	if (port)
		*port = 0;
	return 0;
}

#endif

/**
 * cec_phys_addr_invalidate() - set the physical address to INVALID
 *
 * @adap:	the CEC adapter
 *
 * This is a simple helper function to invalidate the physical
 * address.
 */
static inline void cec_phys_addr_invalidate(struct cec_adapter *adap)
{
	cec_s_phys_addr(adap, CEC_PHYS_ADDR_INVALID, false);
}

#endif /* _MEDIA_CEC_H */
