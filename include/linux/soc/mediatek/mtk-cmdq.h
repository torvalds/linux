/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 MediaTek Inc.
 *
 */

#ifndef __MTK_CMDQ_H__
#define __MTK_CMDQ_H__

#include <linux/mailbox_client.h>
#include <linux/mailbox/mtk-cmdq-mailbox.h>
#include <linux/timer.h>

#define CMDQ_NO_TIMEOUT		0xffffffffu

struct cmdq_pkt;

struct cmdq_client_reg {
	u8 subsys;
	u16 offset;
	u16 size;
};

struct cmdq_client {
	spinlock_t lock;
	u32 pkt_cnt;
	struct mbox_client client;
	struct mbox_chan *chan;
	struct timer_list timer;
	u32 timeout_ms; /* in unit of microsecond */
};

/**
 * cmdq_dev_get_client_reg() - parse cmdq client reg from the device
 *			       node of CMDQ client
 * @dev:	device of CMDQ mailbox client
 * @client_reg: CMDQ client reg pointer
 * @idx:	the index of desired reg
 *
 * Return: 0 for success; else the error code is returned
 *
 * Help CMDQ client parsing the cmdq client reg
 * from the device node of CMDQ client.
 */
int cmdq_dev_get_client_reg(struct device *dev,
			    struct cmdq_client_reg *client_reg, int idx);

/**
 * cmdq_mbox_create() - create CMDQ mailbox client and channel
 * @dev:	device of CMDQ mailbox client
 * @index:	index of CMDQ mailbox channel
 * @timeout:	timeout of a pkt execution by GCE, in unit of microsecond, set
 *		CMDQ_NO_TIMEOUT if a timer is not used.
 *
 * Return: CMDQ mailbox client pointer
 */
struct cmdq_client *cmdq_mbox_create(struct device *dev, int index,
				     u32 timeout);

/**
 * cmdq_mbox_destroy() - destroy CMDQ mailbox client and channel
 * @client:	the CMDQ mailbox client
 */
void cmdq_mbox_destroy(struct cmdq_client *client);

/**
 * cmdq_pkt_create() - create a CMDQ packet
 * @client:	the CMDQ mailbox client
 * @size:	required CMDQ buffer size
 *
 * Return: CMDQ packet pointer
 */
struct cmdq_pkt *cmdq_pkt_create(struct cmdq_client *client, size_t size);

/**
 * cmdq_pkt_destroy() - destroy the CMDQ packet
 * @pkt:	the CMDQ packet
 */
void cmdq_pkt_destroy(struct cmdq_pkt *pkt);

/**
 * cmdq_pkt_write() - append write command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_write(struct cmdq_pkt *pkt, u8 subsys, u16 offset, u32 value);

/**
 * cmdq_pkt_write_mask() - append write command with mask to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 * @mask:	the specified target register mask
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_write_mask(struct cmdq_pkt *pkt, u8 subsys,
			u16 offset, u32 value, u32 mask);

/**
 * cmdq_pkt_wfe() - append wait for event command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @event:	the desired event type to "wait and CLEAR"
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_wfe(struct cmdq_pkt *pkt, u16 event);

/**
 * cmdq_pkt_clear_event() - append clear event command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @event:	the desired event to be cleared
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_clear_event(struct cmdq_pkt *pkt, u16 event);

/**
 * cmdq_pkt_poll() - Append polling command to the CMDQ packet, ask GCE to
 *		     execute an instruction that wait for a specified
 *		     hardware register to check for the value w/o mask.
 *		     All GCE hardware threads will be blocked by this
 *		     instruction.
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_poll(struct cmdq_pkt *pkt, u8 subsys,
		  u16 offset, u32 value);

/**
 * cmdq_pkt_poll_mask() - Append polling command to the CMDQ packet, ask GCE to
 *		          execute an instruction that wait for a specified
 *		          hardware register to check for the value w/ mask.
 *		          All GCE hardware threads will be blocked by this
 *		          instruction.
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 * @mask:	the specified target register mask
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_poll_mask(struct cmdq_pkt *pkt, u8 subsys,
		       u16 offset, u32 value, u32 mask);
/**
 * cmdq_pkt_flush_async() - trigger CMDQ to asynchronously execute the CMDQ
 *                          packet and call back at the end of done packet
 * @pkt:	the CMDQ packet
 * @cb:		called at the end of done packet
 * @data:	this data will pass back to cb
 *
 * Return: 0 for success; else the error code is returned
 *
 * Trigger CMDQ to asynchronously execute the CMDQ packet and call back
 * at the end of done packet. Note that this is an ASYNC function. When the
 * function returned, it may or may not be finished.
 */
int cmdq_pkt_flush_async(struct cmdq_pkt *pkt, cmdq_async_flush_cb cb,
			 void *data);

/**
 * cmdq_pkt_flush() - trigger CMDQ to execute the CMDQ packet
 * @pkt:	the CMDQ packet
 *
 * Return: 0 for success; else the error code is returned
 *
 * Trigger CMDQ to execute the CMDQ packet. Note that this is a
 * synchronous flush function. When the function returned, the recorded
 * commands have been done.
 */
int cmdq_pkt_flush(struct cmdq_pkt *pkt);

#endif	/* __MTK_CMDQ_H__ */
