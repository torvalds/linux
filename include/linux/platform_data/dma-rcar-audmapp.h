/*
 * This is for Renesas R-Car Audio-DMAC-peri-peri.
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 * Copyright (C) 2014 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This file is based on the include/linux/sh_dma.h
 *
 * Header for the new SH dmaengine driver
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef SH_AUDMAPP_H
#define SH_AUDMAPP_H

#include <linux/dmaengine.h>

struct audmapp_slave_config {
	int		slave_id;
	dma_addr_t	src;
	dma_addr_t	dst;
	u32		chcr;
};

struct audmapp_pdata {
	struct audmapp_slave_config *slave;
	int slave_num;
};

#endif /* SH_AUDMAPP_H */
