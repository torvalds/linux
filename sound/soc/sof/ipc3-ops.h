/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
 */

#ifndef __SOUND_SOC_SOF_IPC3_OPS_H
#define __SOUND_SOC_SOF_IPC3_OPS_H

#include "sof-priv.h"

extern const struct sof_ipc_tplg_ops ipc3_tplg_ops;
extern const struct sof_ipc_ops ipc3_ops;
extern const struct sof_ipc_tplg_control_ops tplg_ipc3_control_ops;
extern const struct sof_ipc_pcm_ops ipc3_pcm_ops;

#endif
