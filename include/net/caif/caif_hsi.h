/*
 * Copyright (C) ST-Ericsson AB 2010
 * Contact: Sjur Brendeland / sjur.brandeland@stericsson.com
 * Author:  Daniel Martensson / daniel.martensson@stericsson.com
 *	    Dmitry.Tarnyagin  / dmitry.tarnyagin@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CAIF_HSI_H_
#define CAIF_HSI_H_

#include <net/caif/caif_layer.h>
#include <net/caif/caif_device.h>
#include <linux/atomic.h>

/*
 * Maximum number of CAIF frames that can reside in the same HSI frame.
 */
#define CFHSI_MAX_PKTS 15

/*
 * Maximum number of bytes used for the frame that can be embedded in the
 * HSI descriptor.
 */
#define CFHSI_MAX_EMB_FRM_SZ 96

/*
 * Decides if HSI buffers should be prefilled with 0xFF pattern for easier
 * debugging. Both TX and RX buffers will be filled before the transfer.
 */
#define CFHSI_DBG_PREFILL		0

/* Structure describing a HSI packet descriptor. */
#pragma pack(1) /* Byte alignment. */
struct cfhsi_desc {
	u8 header;
	u8 offset;
	u16 cffrm_len[CFHSI_MAX_PKTS];
	u8 emb_frm[CFHSI_MAX_EMB_FRM_SZ];
};
#pragma pack() /* Default alignment. */

/* Size of the complete HSI packet descriptor. */
#define CFHSI_DESC_SZ (sizeof(struct cfhsi_desc))

/*
 * Size of the complete HSI packet descriptor excluding the optional embedded
 * CAIF frame.
 */
#define CFHSI_DESC_SHORT_SZ (CFHSI_DESC_SZ - CFHSI_MAX_EMB_FRM_SZ)

/*
 * Maximum bytes transferred in one transfer.
 */
#define CFHSI_MAX_CAIF_FRAME_SZ 4096

#define CFHSI_MAX_PAYLOAD_SZ (CFHSI_MAX_PKTS * CFHSI_MAX_CAIF_FRAME_SZ)

/* Size of the complete HSI TX buffer. */
#define CFHSI_BUF_SZ_TX (CFHSI_DESC_SZ + CFHSI_MAX_PAYLOAD_SZ)

/* Size of the complete HSI RX buffer. */
#define CFHSI_BUF_SZ_RX ((2 * CFHSI_DESC_SZ) + CFHSI_MAX_PAYLOAD_SZ)

/* Bitmasks for the HSI descriptor. */
#define CFHSI_PIGGY_DESC		(0x01 << 7)

#define CFHSI_TX_STATE_IDLE			0
#define CFHSI_TX_STATE_XFER			1

#define CFHSI_RX_STATE_DESC			0
#define CFHSI_RX_STATE_PAYLOAD			1

/* Bitmasks for power management. */
#define CFHSI_WAKE_UP				0
#define CFHSI_WAKE_UP_ACK			1
#define CFHSI_WAKE_DOWN_ACK			2
#define CFHSI_AWAKE				3
#define CFHSI_WAKELOCK_HELD			4
#define CFHSI_SHUTDOWN				5
#define CFHSI_FLUSH_FIFO			6

#ifndef CFHSI_INACTIVITY_TOUT
#define CFHSI_INACTIVITY_TOUT			(1 * HZ)
#endif /* CFHSI_INACTIVITY_TOUT */

#ifndef CFHSI_WAKE_TOUT
#define CFHSI_WAKE_TOUT			(3 * HZ)
#endif /* CFHSI_WAKE_TOUT */

#ifndef CFHSI_MAX_RX_RETRIES
#define CFHSI_MAX_RX_RETRIES		(10 * HZ)
#endif

/* Structure implemented by the CAIF HSI driver. */
struct cfhsi_cb_ops {
	void (*tx_done_cb) (struct cfhsi_cb_ops *drv);
	void (*rx_done_cb) (struct cfhsi_cb_ops *drv);
	void (*wake_up_cb) (struct cfhsi_cb_ops *drv);
	void (*wake_down_cb) (struct cfhsi_cb_ops *drv);
};

/* Structure implemented by HSI device. */
struct cfhsi_ops {
	int (*cfhsi_up) (struct cfhsi_ops *dev);
	int (*cfhsi_down) (struct cfhsi_ops *dev);
	int (*cfhsi_tx) (u8 *ptr, int len, struct cfhsi_ops *dev);
	int (*cfhsi_rx) (u8 *ptr, int len, struct cfhsi_ops *dev);
	int (*cfhsi_wake_up) (struct cfhsi_ops *dev);
	int (*cfhsi_wake_down) (struct cfhsi_ops *dev);
	int (*cfhsi_get_peer_wake) (struct cfhsi_ops *dev, bool *status);
	int (*cfhsi_fifo_occupancy) (struct cfhsi_ops *dev, size_t *occupancy);
	int (*cfhsi_rx_cancel)(struct cfhsi_ops *dev);
	struct cfhsi_cb_ops *cb_ops;
};

/* Structure holds status of received CAIF frames processing */
struct cfhsi_rx_state {
	int state;
	int nfrms;
	int pld_len;
	int retries;
	bool piggy_desc;
};

/* Priority mapping */
enum {
	CFHSI_PRIO_CTL = 0,
	CFHSI_PRIO_VI,
	CFHSI_PRIO_VO,
	CFHSI_PRIO_BEBK,
	CFHSI_PRIO_LAST,
};

struct cfhsi_config {
	u32 inactivity_timeout;
	u32 aggregation_timeout;
	u32 head_align;
	u32 tail_align;
	u32 q_high_mark;
	u32 q_low_mark;
};

/* Structure implemented by CAIF HSI drivers. */
struct cfhsi {
	struct caif_dev_common cfdev;
	struct net_device *ndev;
	struct platform_device *pdev;
	struct sk_buff_head qhead[CFHSI_PRIO_LAST];
	struct cfhsi_cb_ops cb_ops;
	struct cfhsi_ops *ops;
	int tx_state;
	struct cfhsi_rx_state rx_state;
	struct cfhsi_config cfg;
	int rx_len;
	u8 *rx_ptr;
	u8 *tx_buf;
	u8 *rx_buf;
	u8 *rx_flip_buf;
	spinlock_t lock;
	int flow_off_sent;
	struct list_head list;
	struct work_struct wake_up_work;
	struct work_struct wake_down_work;
	struct work_struct out_of_sync_work;
	struct workqueue_struct *wq;
	wait_queue_head_t wake_up_wait;
	wait_queue_head_t wake_down_wait;
	wait_queue_head_t flush_fifo_wait;
	struct timer_list inactivity_timer;
	struct timer_list rx_slowpath_timer;

	/* TX aggregation */
	int aggregation_len;
	struct timer_list aggregation_timer;

	unsigned long bits;
};
extern struct platform_driver cfhsi_driver;

/**
 * enum ifla_caif_hsi - CAIF HSI NetlinkRT parameters.
 * @IFLA_CAIF_HSI_INACTIVITY_TOUT: Inactivity timeout before
 *			taking the HSI wakeline down, in milliseconds.
 * When using RT Netlink to create, destroy or configure a CAIF HSI interface,
 * enum ifla_caif_hsi is used to specify the configuration attributes.
 */
enum ifla_caif_hsi {
	__IFLA_CAIF_HSI_UNSPEC,
	__IFLA_CAIF_HSI_INACTIVITY_TOUT,
	__IFLA_CAIF_HSI_AGGREGATION_TOUT,
	__IFLA_CAIF_HSI_HEAD_ALIGN,
	__IFLA_CAIF_HSI_TAIL_ALIGN,
	__IFLA_CAIF_HSI_QHIGH_WATERMARK,
	__IFLA_CAIF_HSI_QLOW_WATERMARK,
	__IFLA_CAIF_HSI_MAX
};

extern struct cfhsi_ops *cfhsi_get_ops(void);

#endif		/* CAIF_HSI_H_ */
