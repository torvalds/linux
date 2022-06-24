/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014, 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __ESOC_CLIENT_H_
#define __ESOC_CLIENT_H_

#include <linux/device.h>
#include <linux/esoc_ctrl.h>
#include <linux/notifier.h>

/* Flag values used with the power_on and power_off hooks */
#define ESOC_HOOK_MDM_CRASH	0x0001 /* In crash handling path */
#define ESOC_HOOK_MDM_DOWN	0x0002 /* MDM about to go down */

struct esoc_client_hook {
	char *name;
	void *priv;
	enum esoc_client_hook_prio prio;
	int (*esoc_link_power_on)(void *priv, unsigned int flags);
	void (*esoc_link_power_off)(void *priv, unsigned int flags);
	u64 (*esoc_link_get_id)(void *priv);
	void (*esoc_link_mdm_crash)(void *priv);
};

/*
 * struct esoc_desc: Describes an external soc
 * @name: external soc name
 * @priv: private data for external soc
 */
struct esoc_desc {
	const char *name;
	const char *link;
	const char *link_info;
	void *priv;
};

#if IS_ENABLED(CONFIG_QCOM_ESOC_CLIENT)
/* Can return probe deferral */
struct esoc_desc *devm_register_esoc_client(struct device *dev,
							const char *name);
void devm_unregister_esoc_client(struct device *dev,
						struct esoc_desc *esoc_desc);
int esoc_register_client_notifier(struct notifier_block *nb);
int esoc_register_client_hook(struct esoc_desc *desc,
				struct esoc_client_hook *client_hook);
int esoc_unregister_client_hook(struct esoc_desc *desc,
				struct esoc_client_hook *client_hook);

#else
struct esoc_desc *devm_register_esoc_client(struct device *dev,
							const char *name)
{
	return NULL;
}

void devm_unregister_esoc_client(struct device *dev,
						struct esoc_desc *esoc_desc)
{
}

int esoc_register_client_notifier(struct notifier_block *nb)
{
	return -EPERM;
}

int esoc_register_client_hook(struct esoc_desc *desc,
				struct esoc_client_hook *client_hook)
{
	return -EPERM;
}

int esoc_unregister_client_hook(struct esoc_desc *desc,
				struct esoc_client_hook *client_hook)
{
	return -EPERM;
}
#endif
#endif
