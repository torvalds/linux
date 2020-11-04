/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __UCSI_GLINK_H__
#define __UCSI_GLINK_H__

#include <linux/errno.h>
#include <linux/usb/typec.h>

struct ucsi_glink_constat_info {
	enum typec_accessory acc;
	bool partner_usb;
	bool partner_alternate_mode;
	bool partner_change;
	bool connect;
};

struct notifier_block;

#if IS_ENABLED(CONFIG_UCSI_QTI_GLINK)

int register_ucsi_glink_notifier(struct notifier_block *nb);
int unregister_ucsi_glink_notifier(struct notifier_block *nb);

#else

static inline int register_ucsi_glink_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int unregister_ucsi_glink_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

#endif
#endif
