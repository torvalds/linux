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
extern const struct sof_ipc_fw_loader_ops ipc3_loader_ops;

/* helpers for fw_ready and ext_manifest parsing */
int sof_ipc3_get_ext_windows(struct snd_sof_dev *sdev,
			     const struct sof_ipc_ext_data_hdr *ext_hdr);
int sof_ipc3_get_cc_info(struct snd_sof_dev *sdev,
			 const struct sof_ipc_ext_data_hdr *ext_hdr);
int sof_ipc3_validate_fw_version(struct snd_sof_dev *sdev);

#endif
