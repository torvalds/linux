/*
 * Copyright 2013 Ideas On Board SPRL
 * Copyright 2013, 2014 Horms Solutions Ltd.
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Contact: Simon Horman <horms@verge.net.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_CLK_SHMOBILE_H_
#define __LINUX_CLK_SHMOBILE_H_

#include <linux/types.h>

void r8a7779_clocks_init(u32 mode);
void rcar_gen2_clocks_init(u32 mode);

#endif
