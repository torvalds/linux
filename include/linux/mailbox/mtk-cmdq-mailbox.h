/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 MediaTek Inc.
 *
 */

#ifndef __MTK_CMDQ_MAILBOX_H__
#define __MTK_CMDQ_MAILBOX_H__

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#define CMDQ_INST_SIZE			8 /* instruction is 64-bit */
#define CMDQ_SUBSYS_SHIFT		16
#define CMDQ_OP_CODE_SHIFT		24
#define CMDQ_JUMP_PASS			CMDQ_INST_SIZE

#define CMDQ_WFE_UPDATE			BIT(31)
#define CMDQ_WFE_UPDATE_VALUE		BIT(16)
#define CMDQ_WFE_WAIT			BIT(15)
#define CMDQ_WFE_WAIT_VALUE		0x1

/*
 * WFE arg_b
 * bit 0-11: wait value
 * bit 15: 1 - wait, 0 - no wait
 * bit 16-27: update value
 * bit 31: 1 - update, 0 - no update
 */
#define CMDQ_WFE_OPTION			(CMDQ_WFE_UPDATE | CMDQ_WFE_WAIT | \
					CMDQ_WFE_WAIT_VALUE)

/** cmdq event maximum */
#define CMDQ_MAX_EVENT			0x3ff

/*
 * CMDQ_CODE_MASK:
 *   set write mask
 *   format: op mask
 * CMDQ_CODE_WRITE:
 *   write value into target register
 *   format: op subsys address value
 * CMDQ_CODE_JUMP:
 *   jump by offset
 *   format: op offset
 * CMDQ_CODE_WFE:
 *   wait for event and clear
 *   it is just clear if no wait
 *   format: [wait]  op event update:1 to_wait:1 wait:1
 *           [clear] op event update:1 to_wait:0 wait:0
 * CMDQ_CODE_EOC:
 *   end of command
 *   format: op irq_flag
 */
enum cmdq_code {
	CMDQ_CODE_MASK = 0x02,
	CMDQ_CODE_WRITE = 0x04,
	CMDQ_CODE_POLL = 0x08,
	CMDQ_CODE_JUMP = 0x10,
	CMDQ_CODE_WFE = 0x20,
	CMDQ_CODE_EOC = 0x40,
	CMDQ_CODE_READ_S = 0x80,
	CMDQ_CODE_WRITE_S = 0x90,
	CMDQ_CODE_WRITE_S_MASK = 0x91,
	CMDQ_CODE_LOGIC = 0xa0,
};

enum cmdq_cb_status {
	CMDQ_CB_NORMAL = 0,
	CMDQ_CB_ERROR
};

struct cmdq_cb_data {
	enum cmdq_cb_status	sta;
	void			*data;
};

typedef void (*cmdq_async_flush_cb)(struct cmdq_cb_data data);

struct cmdq_task_cb {
	cmdq_async_flush_cb	cb;
	void			*data;
};

struct cmdq_pkt {
	void			*va_base;
	dma_addr_t		pa_base;
	size_t			cmd_buf_size; /* command occupied size */
	size_t			buf_size; /* real buffer size */
	struct cmdq_task_cb	cb;
	struct cmdq_task_cb	async_cb;
	void			*cl;
};

u8 cmdq_get_shift_pa(struct mbox_chan *chan);

#endif /* __MTK_CMDQ_MAILBOX_H__ */
