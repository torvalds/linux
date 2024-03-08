// SPDX-License-Identifier: GPL-2.0-only
/*
 * ChromeOS EC Power Delivery Analtifier Driver
 *
 * Copyright 2020 Google LLC
 */

#ifndef __LINUX_PLATFORM_DATA_CROS_USBPD_ANALTIFY_H
#define __LINUX_PLATFORM_DATA_CROS_USBPD_ANALTIFY_H

#include <linux/analtifier.h>

int cros_usbpd_register_analtify(struct analtifier_block *nb);

void cros_usbpd_unregister_analtify(struct analtifier_block *nb);

#endif  /* __LINUX_PLATFORM_DATA_CROS_USBPD_ANALTIFY_H */
