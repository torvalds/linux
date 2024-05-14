/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_AOSS_H__
#define __QCOM_AOSS_H__

#include <linux/err.h>
#include <linux/device.h>

struct qmp;

#if IS_ENABLED(CONFIG_QCOM_AOSS_QMP)

int qmp_send(struct qmp *qmp, const void *data, size_t len);
struct qmp *qmp_get(struct device *dev);
void qmp_put(struct qmp *qmp);

#else

static inline int qmp_send(struct qmp *qmp, const void *data, size_t len)
{
	return -ENODEV;
}

static inline struct qmp *qmp_get(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

static inline void qmp_put(struct qmp *qmp)
{
}

#endif

#endif
