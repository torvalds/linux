/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright 2021 NXP
 */

#ifndef __SOUND_SOC_SOF_OF_H
#define __SOUND_SOC_SOF_OF_H

extern const struct dev_pm_ops sof_of_pm;

int sof_of_probe(struct platform_device *pdev);
int sof_of_remove(struct platform_device *pdev);
void sof_of_shutdown(struct platform_device *pdev);

#endif
