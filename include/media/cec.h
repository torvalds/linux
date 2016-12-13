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
#include <media/cec-edid.h>
#include <media/cec-notifier.h>

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

#define CEC_NUM_EVENTS		CEC_EVENT_LOST_MSGS

struct cec_fh {
	struct list_head	list;
	struct list_head	xfer_list;
	struct cec_adapter	*adap;
	u8			mode_initiator;
	u8			mode_follower;

	/* Events */
	wait_queue_head_t	wait;
	unsigned int		pending_events;
	struct cec_event	events[CEC_NUM_EVENTS];
	struct mutex		lock;
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
	int (*adap_log_addr)(struct cec_adapter *adap, u8 logical_addr);
	int (*adap_transmit)(struct cec_adapter *adap, u8 attempts,
			     u32 signal_free_time, struct cec_msg *msg);
	void (*adap_status)(struct cec_adapter *adap, struct seq_file *file);

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
	bool is_configuring;
	bool is_configured;
	u32 monitor_all_cnt;
	u32 follower_cnt;
	struct cec_fh *cec_follower;
	struct cec_fh *cec_initiator;
	bool passthrough;
	struct cec_log_addrs log_addrs;

#ifdef CONFIG_MEDIA_CEC_NOTIFIER
	struct cec_notifier *notifier;
#endif

	struct dentry *cec_dir;
	struct dentry *status_file;

	u16 phys_addrs[15];
	u32 sequence;

	char input_name[32];
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

#if IS_ENABLED(CONFIG_MEDIA_CEC_SUPPORT)
struct cec_adapter *cec_allocate_adapter(const struct cec_adap_ops *ops,
		void *priv, const char *name, u32 caps, u8 available_las);
int cec_register_adapter(struct cec_adapter *adap, struct device *parent);
void cec_unregister_adapter(struct cec_adapter *adap);
void cec_delete_adapter(struct cec_adapter *adap);

int cec_s_log_addrs(struct cec_adapter *adap, struct cec_log_addrs *log_addrs,
		    bool block);
void cec_s_phys_addr(struct cec_adapter *adap, u16 phys_addr,
		     bool block);
int cec_transmit_msg(struct cec_adapter *adap, struct cec_msg *msg,
		     bool block);

/* Called by the adapter */
void cec_transmit_done(struct cec_adapter *adap, u8 status, u8 arb_lost_cnt,
		       u8 nack_cnt, u8 low_drive_cnt, u8 error_cnt);
void cec_received_msg(struct cec_adapter *adap, struct cec_msg *msg);

#ifdef CONFIG_MEDIA_CEC_NOTIFIER
void cec_register_cec_notifier(struct cec_adapter *adap,
			       struct cec_notifier *notifier);
#endif

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

#endif

#endif /* _MEDIA_CEC_H */
