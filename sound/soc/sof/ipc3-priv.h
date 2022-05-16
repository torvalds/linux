/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */

#ifndef __SOUND_SOC_SOF_IPC3_PRIV_H
#define __SOUND_SOC_SOF_IPC3_PRIV_H

#include "sof-priv.h"

/* IPC3 specific ops */
extern const struct sof_ipc_pcm_ops ipc3_pcm_ops;
extern const struct sof_ipc_tplg_ops ipc3_tplg_ops;
extern const struct sof_ipc_tplg_control_ops tplg_ipc3_control_ops;
extern const struct sof_ipc_fw_loader_ops ipc3_loader_ops;
extern const struct sof_ipc_fw_tracing_ops ipc3_dtrace_ops;

/* helpers for fw_ready and ext_manifest parsing */
int sof_ipc3_get_ext_windows(struct snd_sof_dev *sdev,
			     const struct sof_ipc_ext_data_hdr *ext_hdr);
int sof_ipc3_get_cc_info(struct snd_sof_dev *sdev,
			 const struct sof_ipc_ext_data_hdr *ext_hdr);
int sof_ipc3_validate_fw_version(struct snd_sof_dev *sdev);

/* dtrace position update */
int ipc3_dtrace_posn_update(struct snd_sof_dev *sdev,
			    struct sof_ipc_dma_trace_posn *posn);

/* dtrace platform callback wrappers */
static inline int sof_dtrace_host_init(struct snd_sof_dev *sdev,
				       struct sof_ipc_dma_trace_params_ext *dtrace_params)
{
	struct snd_sof_dsp_ops *dsp_ops = sdev->pdata->desc->ops;

	if (dsp_ops->trace_init)
		return dsp_ops->trace_init(sdev, dtrace_params);

	return 0;
}

static inline int sof_dtrace_host_release(struct snd_sof_dev *sdev)
{
	struct snd_sof_dsp_ops *dsp_ops = sdev->pdata->desc->ops;

	if (dsp_ops->trace_release)
		return dsp_ops->trace_release(sdev);

	return 0;
}

static inline int sof_dtrace_host_trigger(struct snd_sof_dev *sdev, int cmd)
{
	struct snd_sof_dsp_ops *dsp_ops = sdev->pdata->desc->ops;

	if (dsp_ops->trace_trigger)
		return dsp_ops->trace_trigger(sdev, cmd);

	return 0;
}

#endif
