// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

#include "sof-priv.h"

int sof_fw_trace_init(struct snd_sof_dev *sdev)
{
	if (!sdev->ipc->ops->fw_tracing) {
		dev_info(sdev->dev, "Firmware tracing is not available\n");
		sdev->fw_trace_is_supported = false;

		return 0;
	}

	return sdev->ipc->ops->fw_tracing->init(sdev);
}

void sof_fw_trace_free(struct snd_sof_dev *sdev)
{
	if (!sdev->fw_trace_is_supported || !sdev->ipc->ops->fw_tracing)
		return;

	if (sdev->ipc->ops->fw_tracing->free)
		sdev->ipc->ops->fw_tracing->free(sdev);
}

void sof_fw_trace_fw_crashed(struct snd_sof_dev *sdev)
{
	if (!sdev->fw_trace_is_supported)
		return;

	if (sdev->ipc->ops->fw_tracing->fw_crashed)
		sdev->ipc->ops->fw_tracing->fw_crashed(sdev);
}

void sof_fw_trace_suspend(struct snd_sof_dev *sdev, pm_message_t pm_state)
{
	if (!sdev->fw_trace_is_supported)
		return;

	sdev->ipc->ops->fw_tracing->suspend(sdev, pm_state);
}

int sof_fw_trace_resume(struct snd_sof_dev *sdev)
{
	if (!sdev->fw_trace_is_supported)
		return 0;

	return sdev->ipc->ops->fw_tracing->resume(sdev);
}
