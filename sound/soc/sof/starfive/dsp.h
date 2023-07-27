/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DSP IPC driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#ifndef _STARFIVE_DSP_IPC_H
#define _STARFIVE_DSP_IPC_H

#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define DSP_MU_CHAN_NUM		2

struct jh7110_dsp_chan {
	struct jh7110_dsp_ipc *ipc;
	struct mbox_client cl;
	struct mbox_chan *ch;
	char *name;
	int idx;
};

struct jh7110_dsp_rx_work {
	struct work_struct rx_work;
	u32 data;
};

struct jh7110_dsp_ops {
	void (*handle_reply)(struct jh7110_dsp_ipc *ipc);
	void (*handle_request)(struct jh7110_dsp_ipc *ipc);
};

struct jh7110_dsp_ipc {
	/* Host <-> DSP communication uses 1 tx and 1 rx channels */
	struct jh7110_dsp_chan chans[DSP_MU_CHAN_NUM];
	struct device *dev;
	struct jh7110_dsp_ops *ops;
	struct workqueue_struct *dsp_ipc_wq;
	struct jh7110_dsp_rx_work work;
	u32 request_cnt;
	u32 reply_cnt;
	void *private_data;
};

static inline void jh7110_dsp_set_data(struct jh7110_dsp_ipc *ipc, void *data)
{
	if (!ipc)
		return;

	ipc->private_data = data;
}

static inline void *jh7110_dsp_get_data(struct jh7110_dsp_ipc *ipc)
{
	if (!ipc)
		return NULL;

	return ipc->private_data;
}

#if IS_ENABLED(CONFIG_STARFIVE_DSP)

int jh7110_dsp_ring_doorbell(struct jh7110_dsp_ipc *dsp, unsigned int is_ack);

struct mbox_chan *jh7110_dsp_request_channel(struct jh7110_dsp_ipc *ipc, int idx);
void jh7110_dsp_free_channel(struct jh7110_dsp_ipc *ipc, int idx);

#else

static inline int jh7110_dsp_ring_doorbell(struct jh7110_dsp_ipc *ipc,
					unsigned int is_ack)
{
	return -ENOTSUPP;
}

struct mbox_chan *jh7110_dsp_request_channel(struct jh7110_dsp_ipc *ipc, int idx)
{
	return ERR_PTR(-EOPNOTSUPP);
}

void jh7110_dsp_free_channel(struct jh7110_dsp_ipc *ipc, int idx) { }

#endif
#endif /* _STARFIVE_DSP_IPC_H */
