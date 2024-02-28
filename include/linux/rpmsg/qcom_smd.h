/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_RPMSG_QCOM_SMD_H
#define _LINUX_RPMSG_QCOM_SMD_H

#include <linux/device.h>
#include <linux/rpmsg.h>

struct qcom_smd_edge;

#if IS_ENABLED(CONFIG_RPMSG_QCOM_SMD)

struct qcom_smd_edge *qcom_smd_register_edge(struct device *parent,
					     struct device_node *node);
int qcom_smd_unregister_edge(struct qcom_smd_edge *edge);

#else

static inline struct qcom_smd_edge *
qcom_smd_register_edge(struct device *parent,
		       struct device_node *node)
{
	return NULL;
}

static inline int qcom_smd_unregister_edge(struct qcom_smd_edge *edge)
{
	return 0;
}

#endif

/* These operations are temporarily exposing signal interfaces */
int qcom_smd_get_sigs(struct rpmsg_endpoint *ept);
int qcom_smd_set_sigs(struct rpmsg_endpoint *ept, u32 set, u32 clear);
int qcom_smd_register_signals_cb(struct rpmsg_endpoint *ept,
	int (*signals_cb)(struct rpmsg_device *dev, void *priv, u32 old, u32 new));
#endif

