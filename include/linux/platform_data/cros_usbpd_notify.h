// SPDX-License-Identifier: GPL-2.0-only
/*
 * ChromeOS EC Power Delivery Notifier Driver
 *
 * Copyright 2020 Google LLC
 */

#ifndef __LINUX_PLATFORM_DATA_CROS_USBPD_NOTIFY_H
#define __LINUX_PLATFORM_DATA_CROS_USBPD_NOTIFY_H

#include <linux/notifier.h>

int cros_usbpd_register_notify(struct notifier_block *nb);

void cros_usbpd_unregister_notify(struct notifier_block *nb);

#endif  /* __LINUX_PLATFORM_DATA_CROS_USBPD_NOTIFY_H */
