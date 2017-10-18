
#ifndef _LINUX_RPMSG_QCOM_SMD_H
#define _LINUX_RPMSG_QCOM_SMD_H

#include <linux/device.h>

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

#endif
