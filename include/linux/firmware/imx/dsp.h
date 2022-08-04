/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2019 NXP
 *
 * Header file for the DSP IPC implementation
 */

#ifndef _IMX_DSP_IPC_H
#define _IMX_DSP_IPC_H

#include <linux/device.h>
#include <linux/types.h>
#include <linux/mailbox_client.h>

#define DSP_MU_CHAN_NUM		4

struct imx_dsp_chan {
	struct imx_dsp_ipc *ipc;
	struct mbox_client cl;
	struct mbox_chan *ch;
	char *name;
	int idx;
};

struct imx_dsp_ops {
	void (*handle_reply)(struct imx_dsp_ipc *ipc);
	void (*handle_request)(struct imx_dsp_ipc *ipc);
};

struct imx_dsp_ipc {
	/* Host <-> DSP communication uses 2 txdb and 2 rxdb channels */
	struct imx_dsp_chan chans[DSP_MU_CHAN_NUM];
	struct device *dev;
	struct imx_dsp_ops *ops;
	void *private_data;
};

static inline void imx_dsp_set_data(struct imx_dsp_ipc *ipc, void *data)
{
	if (!ipc)
		return;

	ipc->private_data = data;
}

static inline void *imx_dsp_get_data(struct imx_dsp_ipc *ipc)
{
	if (!ipc)
		return NULL;

	return ipc->private_data;
}

#if IS_ENABLED(CONFIG_IMX_DSP)

int imx_dsp_ring_doorbell(struct imx_dsp_ipc *dsp, unsigned int chan_idx);

struct mbox_chan *imx_dsp_request_channel(struct imx_dsp_ipc *ipc, int idx);
void imx_dsp_free_channel(struct imx_dsp_ipc *ipc, int idx);

#else

static inline int imx_dsp_ring_doorbell(struct imx_dsp_ipc *ipc,
					unsigned int chan_idx)
{
	return -ENOTSUPP;
}

struct mbox_chan *imx_dsp_request_channel(struct imx_dsp_ipc *ipc, int idx)
{
	return ERR_PTR(-EOPNOTSUPP);
}

void imx_dsp_free_channel(struct imx_dsp_ipc *ipc, int idx) { }

#endif
#endif /* _IMX_DSP_IPC_H */
