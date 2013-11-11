/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_CLK_MXS_H
#define __LINUX_CLK_MXS_H

int mx23_clocks_init(void);
int mx28_clocks_init(void);
int mxs_saif_clkmux_select(unsigned int clkmux);

#endif
