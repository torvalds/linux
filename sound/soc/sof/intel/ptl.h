/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2025 Intel Corporation
 */

#ifndef __SOF_INTEL_PTL_H
#define __SOF_INTEL_PTL_H

#define PTL_MICPVCP_DDZE_FORCED		BIT(16)
#define PTL_MICPVCP_DDZE_ENABLED	BIT(17)
#define PTL_MICPVCP_DDZLS_SDW		GENMASK(26, 20)
#define PTL_MICPVCP_GET_SDW_MASK(x)	(((x) & PTL_MICPVCP_DDZLS_SDW) >> 20)

int sof_ptl_set_ops(struct snd_sof_dev *sdev, struct snd_sof_dsp_ops *dsp_ops);

#endif /* __SOF_INTEL_PTL_H */
