/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
 *         for ST-Ericsson.
 */
#ifndef UX500_PCM_H
#define UX500_PCM_H

#include <asm/page.h>

#include <linux/workqueue.h>

int ux500_pcm_register_platform(struct platform_device *pdev);
int ux500_pcm_unregister_platform(struct platform_device *pdev);

#endif
