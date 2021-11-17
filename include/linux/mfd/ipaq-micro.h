/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for the compaq Micro MFD
 */

#ifndef _MFD_IPAQ_MICRO_H_
#define _MFD_IPAQ_MICRO_H_

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/list.h>

#define TX_BUF_SIZE	32
#define RX_BUF_SIZE	16
#define CHAR_SOF	0x02

/*
 * These are the different messages that can be sent to the microcontroller
 * to control various aspects.
 */
#define MSG_VERSION		0x0
#define MSG_KEYBOARD		0x2
#define MSG_TOUCHSCREEN		0x3
#define MSG_EEPROM_READ		0x4
#define MSG_EEPROM_WRITE	0x5
#define MSG_THERMAL_SENSOR	0x6
#define MSG_NOTIFY_LED		0x8
#define MSG_BATTERY		0x9
#define MSG_SPI_READ		0xb
#define MSG_SPI_WRITE		0xc
#define MSG_BACKLIGHT		0xd /* H3600 only */
#define MSG_CODEC_CTRL		0xe /* H3100 only */
#define MSG_DISPLAY_CTRL	0xf /* H3100 only */

/* state of receiver parser */
enum rx_state {
	STATE_SOF = 0,     /* Next byte should be start of frame */
	STATE_ID,          /* Next byte is ID & message length   */
	STATE_DATA,        /* Next byte is a data byte           */
	STATE_CHKSUM       /* Next byte should be checksum       */
};

/**
 * struct ipaq_micro_txdev - TX state
 * @len: length of message in TX buffer
 * @index: current index into TX buffer
 * @buf: TX buffer
 */
struct ipaq_micro_txdev {
	u8 len;
	u8 index;
	u8 buf[TX_BUF_SIZE];
};

/**
 * struct ipaq_micro_rxdev - RX state
 * @state: context of RX state machine
 * @chksum: calculated checksum
 * @id: message ID from packet
 * @len: RX buffer length
 * @index: RX buffer index
 * @buf: RX buffer
 */
struct ipaq_micro_rxdev {
	enum rx_state state;
	unsigned char chksum;
	u8            id;
	unsigned int  len;
	unsigned int  index;
	u8            buf[RX_BUF_SIZE];
};

/**
 * struct ipaq_micro_msg - message to the iPAQ microcontroller
 * @id: 4-bit ID of the message
 * @tx_len: length of TX data
 * @tx_data: TX data to send
 * @rx_len: length of receieved RX data
 * @rx_data: RX data to recieve
 * @ack: a completion that will be completed when RX is complete
 * @node: list node if message gets queued
 */
struct ipaq_micro_msg {
	u8 id;
	u8 tx_len;
	u8 tx_data[TX_BUF_SIZE];
	u8 rx_len;
	u8 rx_data[RX_BUF_SIZE];
	struct completion ack;
	struct list_head node;
};

/**
 * struct ipaq_micro - iPAQ microcontroller state
 * @dev: corresponding platform device
 * @base: virtual memory base for underlying serial device
 * @sdlc: virtual memory base for Synchronous Data Link Controller
 * @version: version string
 * @tx: TX state
 * @rx: RX state
 * @lock: lock for this state container
 * @msg: current message
 * @queue: message queue
 * @key: callback for asynchronous key events
 * @key_data: data to pass along with key events
 * @ts: callback for asynchronous touchscreen events
 * @ts_data: data to pass along with key events
 */
struct ipaq_micro {
	struct device *dev;
	void __iomem *base;
	void __iomem *sdlc;
	char version[5];
	struct ipaq_micro_txdev tx;	/* transmit ISR state */
	struct ipaq_micro_rxdev rx;	/* receive ISR state */
	spinlock_t lock;
	struct ipaq_micro_msg *msg;
	struct list_head queue;
	void (*key) (void *data, int len, unsigned char *rxdata);
	void *key_data;
	void (*ts) (void *data, int len, unsigned char *rxdata);
	void *ts_data;
};

extern int
ipaq_micro_tx_msg(struct ipaq_micro *micro, struct ipaq_micro_msg *msg);

static inline int
ipaq_micro_tx_msg_sync(struct ipaq_micro *micro,
		       struct ipaq_micro_msg *msg)
{
	int ret;

	init_completion(&msg->ack);
	ret = ipaq_micro_tx_msg(micro, msg);
	wait_for_completion(&msg->ack);

	return ret;
}

static inline int
ipaq_micro_tx_msg_async(struct ipaq_micro *micro,
			struct ipaq_micro_msg *msg)
{
	init_completion(&msg->ack);
	return ipaq_micro_tx_msg(micro, msg);
}

#endif /* _MFD_IPAQ_MICRO_H_ */
