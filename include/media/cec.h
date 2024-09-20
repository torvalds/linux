/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cec - HDMI Consumer Electronics Control support header
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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

#define CEC_CAP_DEFAULTS (CEC_CAP_LOG_ADDRS | CEC_CAP_TRANSMIT | \
			  CEC_CAP_PASSTHROUGH | CEC_CAP_RC)

/**
 * struct cec_devnode - cec device node
 * @dev:	cec device
 * @cdev:	cec character device
 * @minor:	device node minor number
 * @lock:	lock to serialize open/release and registration
 * @registered:	the device was correctly registered
 * @unregistered: the device was unregistered
 * @lock_fhs:	lock to control access to @fhs
 * @fhs:	the list of open filehandles (cec_fh)
 *
 * This structure represents a cec-related device node.
 *
 * To add or remove filehandles from @fhs the @lock must be taken first,
 * followed by @lock_fhs. It is safe to access @fhs if either lock is held.
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
	/* serialize open/release and registration */
	struct mutex lock;
	bool registered;
	bool unregistered;
	/* protect access to fhs */
	struct mutex lock_fhs;
	struct list_head fhs;
};

struct cec_adapter;
struct cec_data;
struct cec_pin;
struct cec_notifier;

struct cec_data {
	struct list_head list;
	struct list_head xfer_list;
	struct cec_adapter *adap;
	struct cec_msg msg;
	struct cec_fh *fh;
	struct delayed_work work;
	struct completion c;
	u8 attempts;
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
#define CEC_NUM_EVENTS CEC_EVENT_PIN_5V_HIGH

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
	u16			queued_events[CEC_NUM_EVENTS];
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
	/* Low-level callbacks, called with adap->lock held */
	int (*adap_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_monitor_all_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_monitor_pin_enable)(struct cec_adapter *adap, bool enable);
	int (*adap_log_addr)(struct cec_adapter *adap, u8 logical_addr);
	void (*adap_unconfigured)(struct cec_adapter *adap);
	int (*adap_transmit)(struct cec_adapter *adap, u8 attempts,
			     u32 signal_free_time, struct cec_msg *msg);
	void (*adap_nb_transmit_canceled)(struct cec_adapter *adap,
					  const struct cec_msg *msg);
	void (*adap_status)(struct cec_adapter *adap, struct seq_file *file);
	void (*adap_free)(struct cec_adapter *adap);

	/* Error injection callbacks, called without adap->lock held */
	int (*error_inj_show)(struct cec_adapter *adap, struct seq_file *sf);
	bool (*error_inj_parse_line)(struct cec_adapter *adap, char *line);

	/* High-level CEC message callback, called without adap->lock held */
	void (*configured)(struct cec_adapter *adap);
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

/**
 * struct cec_adapter - cec adapter structure
 * @owner:		module owner
 * @name:		name of the CEC adapter
 * @devnode:		device node for the /dev/cecX device
 * @lock:		mutex controlling access to this structure
 * @rc:			remote control device
 * @transmit_queue:	queue of pending transmits
 * @transmit_queue_sz:	number of pending transmits
 * @wait_queue:		queue of transmits waiting for a reply
 * @transmitting:	CEC messages currently being transmitted
 * @transmit_in_progress: true if a transmit is in progress
 * @transmit_in_progress_aborted: true if a transmit is in progress is to be
 *			aborted. This happens if the logical address is
 *			invalidated while the transmit is ongoing. In that
 *			case the transmit will finish, but will not retransmit
 *			and be marked as ABORTED.
 * @xfer_timeout_ms:	the transfer timeout in ms.
 *			If 0, then timeout after 2100 ms.
 * @kthread_config:	kthread used to configure a CEC adapter
 * @config_completion:	used to signal completion of the config kthread
 * @kthread:		main CEC processing thread
 * @kthread_waitq:	main CEC processing wait_queue
 * @ops:		cec adapter ops
 * @priv:		cec driver's private data
 * @capabilities:	cec adapter capabilities
 * @available_log_addrs: maximum number of available logical addresses
 * @phys_addr:		the current physical address
 * @needs_hpd:		if true, then the HDMI HotPlug Detect pin must be high
 *	in order to transmit or receive CEC messages. This is usually a HW
 *	limitation.
 * @is_enabled:		the CEC adapter is enabled
 * @is_claiming_log_addrs:  true if cec_claim_log_addrs() is running
 * @is_configuring:	the CEC adapter is configuring (i.e. claiming LAs)
 * @must_reconfigure:	while configuring, the PA changed, so reclaim LAs
 * @is_configured:	the CEC adapter is configured (i.e. has claimed LAs)
 * @cec_pin_is_high:	if true then the CEC pin is high. Only used with the
 *	CEC pin framework.
 * @adap_controls_phys_addr: if true, then the CEC adapter controls the
 *	physical address, i.e. the CEC hardware can detect HPD changes and
 *	read the EDID and is not dependent on an external HDMI driver.
 *	Drivers that need this can set this field to true after the
 *	cec_allocate_adapter() call.
 * @last_initiator:	the initiator of the last transmitted message.
 * @monitor_all_cnt:	number of filehandles monitoring all msgs
 * @monitor_pin_cnt:	number of filehandles monitoring pin changes
 * @follower_cnt:	number of filehandles in follower mode
 * @cec_follower:	filehandle of the exclusive follower
 * @cec_initiator:	filehandle of the exclusive initiator
 * @passthrough:	if true, then the exclusive follower is in
 *	passthrough mode.
 * @log_addrs:		current logical addresses
 * @conn_info:		current connector info
 * @tx_timeout_cnt:	count the number of Timed Out transmits.
 *			Reset to 0 when this is reported in cec_adap_status().
 * @tx_low_drive_cnt:	count the number of Low Drive transmits.
 *			Reset to 0 when this is reported in cec_adap_status().
 * @tx_error_cnt:	count the number of Error transmits.
 *			Reset to 0 when this is reported in cec_adap_status().
 * @tx_arb_lost_cnt:	count the number of Arb Lost transmits.
 *			Reset to 0 when this is reported in cec_adap_status().
 * @tx_low_drive_log_cnt: number of logged Low Drive transmits since the
 *			adapter was enabled. Used to avoid flooding the kernel
 *			log if this happens a lot.
 * @tx_error_log_cnt:	number of logged Error transmits since the adapter was
 *                      enabled. Used to avoid flooding the kernel log if this
 *                      happens a lot.
 * @notifier:		CEC notifier
 * @pin:		CEC pin status struct
 * @cec_dir:		debugfs cec directory
 * @sequence:		transmit sequence counter
 * @input_phys:		remote control input_phys name
 *
 * This structure represents a cec adapter.
 */
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
	bool transmit_in_progress;
	bool transmit_in_progress_aborted;
	unsigned int xfer_timeout_ms;

	struct task_struct *kthread_config;
	struct completion config_completion;

	struct task_struct *kthread;
	wait_queue_head_t kthread_waitq;

	const struct cec_adap_ops *ops;
	void *priv;
	u32 capabilities;
	u8 available_log_addrs;

	u16 phys_addr;
	bool needs_hpd;
	bool is_enabled;
	bool is_claiming_log_addrs;
	bool is_configuring;
	bool must_reconfigure;
	bool is_configured;
	bool cec_pin_is_high;
	bool adap_controls_phys_addr;
	u8 last_initiator;
	u32 monitor_all_cnt;
	u32 monitor_pin_cnt;
	u32 follower_cnt;
	struct cec_fh *cec_follower;
	struct cec_fh *cec_initiator;
	bool passthrough;
	struct cec_log_addrs log_addrs;
	struct cec_connector_info conn_info;

	u32 tx_timeout_cnt;
	u32 tx_low_drive_cnt;
	u32 tx_error_cnt;
	u32 tx_arb_lost_cnt;
	u32 tx_low_drive_log_cnt;
	u32 tx_error_log_cnt;

#ifdef CONFIG_CEC_NOTIFIER
	struct cec_notifier *notifier;
#endif
#ifdef CONFIG_CEC_PIN
	struct cec_pin *pin;
#endif

	struct dentry *cec_dir;

	u32 sequence;

	char input_phys[40];
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
struct drm_connector;

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
void cec_s_conn_info(struct cec_adapter *adap,
		     const struct cec_connector_info *conn_info);
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
 * @dropped_events: when true some events were dropped
 * @ts:		the timestamp for this event
 *
 */
void cec_queue_pin_cec_event(struct cec_adapter *adap, bool is_high,
			     bool dropped_events, ktime_t ts);

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
 * cec_queue_pin_5v_event() - queue a pin event with a given timestamp.
 *
 * @adap:	pointer to the cec adapter
 * @is_high:	when true the 5V pin is high, otherwise it is low
 * @ts:		the timestamp for this event
 *
 */
void cec_queue_pin_5v_event(struct cec_adapter *adap, bool is_high, ktime_t ts);

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

void cec_fill_conn_info_from_drm(struct cec_connector_info *conn_info,
				 const struct drm_connector *connector);

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

static inline void cec_s_conn_info(struct cec_adapter *adap,
				   const struct cec_connector_info *conn_info)
{
}

static inline void
cec_fill_conn_info_from_drm(struct cec_connector_info *conn_info,
			    const struct drm_connector *connector)
{
	memset(conn_info, 0, sizeof(*conn_info));
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

/**
 * cec_get_edid_spa_location() - find location of the Source Physical Address
 *
 * @edid: the EDID
 * @size: the size of the EDID
 *
 * This EDID is expected to be a CEA-861 compliant, which means that there are
 * at least two blocks and one or more of the extensions blocks are CEA-861
 * blocks.
 *
 * The returned location is guaranteed to be <= size-2.
 *
 * This is an inline function since it is used by both CEC and V4L2.
 * Ideally this would go in a module shared by both, but it is overkill to do
 * that for just a single function.
 */
static inline unsigned int cec_get_edid_spa_location(const u8 *edid,
						     unsigned int size)
{
	unsigned int blocks = size / 128;
	unsigned int block;
	u8 d;

	/* Sanity check: at least 2 blocks and a multiple of the block size */
	if (blocks < 2 || size % 128)
		return 0;

	/*
	 * If there are fewer extension blocks than the size, then update
	 * 'blocks'. It is allowed to have more extension blocks than the size,
	 * since some hardware can only read e.g. 256 bytes of the EDID, even
	 * though more blocks are present. The first CEA-861 extension block
	 * should normally be in block 1 anyway.
	 */
	if (edid[0x7e] + 1 < blocks)
		blocks = edid[0x7e] + 1;

	for (block = 1; block < blocks; block++) {
		unsigned int offset = block * 128;

		/* Skip any non-CEA-861 extension blocks */
		if (edid[offset] != 0x02 || edid[offset + 1] != 0x03)
			continue;

		/* search Vendor Specific Data Block (tag 3) */
		d = edid[offset + 2] & 0x7f;
		/* Check if there are Data Blocks */
		if (d <= 4)
			continue;
		if (d > 4) {
			unsigned int i = offset + 4;
			unsigned int end = offset + d;

			/* Note: 'end' is always < 'size' */
			do {
				u8 tag = edid[i] >> 5;
				u8 len = edid[i] & 0x1f;

				if (tag == 3 && len >= 5 && i + len <= end &&
				    edid[i + 1] == 0x03 &&
				    edid[i + 2] == 0x0c &&
				    edid[i + 3] == 0x00)
					return i + 4;
				i += len + 1;
			} while (i < end);
		}
	}
	return 0;
}

#endif /* _MEDIA_CEC_H */
