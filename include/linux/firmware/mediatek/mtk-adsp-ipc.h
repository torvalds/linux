/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef MTK_ADSP_IPC_H
#define MTK_ADSP_IPC_H

#include <linux/device.h>
#include <linux/types.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>

#define MTK_ADSP_IPC_REQ 0
#define MTK_ADSP_IPC_RSP 1
#define MTK_ADSP_IPC_OP_REQ 0x1
#define MTK_ADSP_IPC_OP_RSP 0x2

enum {
	MTK_ADSP_MBOX_REPLY,
	MTK_ADSP_MBOX_REQUEST,
	MTK_ADSP_MBOX_NUM,
};

struct mtk_adsp_ipc;

struct mtk_adsp_ipc_ops {
	void (*handle_reply)(struct mtk_adsp_ipc *ipc);
	void (*handle_request)(struct mtk_adsp_ipc *ipc);
};

struct mtk_adsp_chan {
	struct mtk_adsp_ipc *ipc;
	struct mbox_client cl;
	struct mbox_chan *ch;
	char *name;
	int idx;
};

struct mtk_adsp_ipc {
	struct mtk_adsp_chan chans[MTK_ADSP_MBOX_NUM];
	struct device *dev;
	struct mtk_adsp_ipc_ops *ops;
	void *private_data;
};

static inline void mtk_adsp_ipc_set_data(struct mtk_adsp_ipc *ipc, void *data)
{
	ipc->private_data = data;
}

static inline void *mtk_adsp_ipc_get_data(struct mtk_adsp_ipc *ipc)
{
	return ipc->private_data;
}

int mtk_adsp_ipc_send(struct mtk_adsp_ipc *ipc, unsigned int idx, uint32_t op);

#endif /* MTK_ADSP_IPC_H */
