// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include "sof-priv.h"
#include "ipc3-ops.h"

static int sof_ipc3_ctx_ipc(struct snd_sof_dev *sdev, int cmd)
{
	struct sof_ipc_pm_ctx pm_ctx = {
		.hdr.size = sizeof(pm_ctx),
		.hdr.cmd = SOF_IPC_GLB_PM_MSG | cmd,
	};
	struct sof_ipc_reply reply;

	/* send ctx save ipc to dsp */
	return sof_ipc_tx_message(sdev->ipc, pm_ctx.hdr.cmd, &pm_ctx,
				  sizeof(pm_ctx), &reply, sizeof(reply));
}

static int sof_ipc3_ctx_save(struct snd_sof_dev *sdev)
{
	return sof_ipc3_ctx_ipc(sdev, SOF_IPC_PM_CTX_SAVE);
}

static int sof_ipc3_ctx_restore(struct snd_sof_dev *sdev)
{
	return sof_ipc3_ctx_ipc(sdev, SOF_IPC_PM_CTX_RESTORE);
}

static const struct sof_ipc_pm_ops ipc3_pm_ops = {
	.ctx_save = sof_ipc3_ctx_save,
	.ctx_restore = sof_ipc3_ctx_restore,
};

const struct sof_ipc_ops ipc3_ops = {
	.tplg = &ipc3_tplg_ops,
	.pm = &ipc3_pm_ops,
};
