/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_PBS_H
#define _QCOM_PBS_H

#include <linux/errno.h>
#include <linux/types.h>

struct device_node;
struct pbs_dev;

#if IS_ENABLED(CONFIG_QCOM_PBS)
int qcom_pbs_trigger_event(struct pbs_dev *pbs, u8 bitmap);
struct pbs_dev *get_pbs_client_device(struct device *client_dev);
#else
static inline int qcom_pbs_trigger_event(struct pbs_dev *pbs, u8 bitmap)
{
	return -ENODEV;
}

static inline struct pbs_dev *get_pbs_client_device(struct device *client_dev)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif
