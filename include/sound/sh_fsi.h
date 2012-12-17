#ifndef __SOUND_FSI_H
#define __SOUND_FSI_H

/*
 * Fifo-attached Serial Interface (FSI) support for SH7724
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define FSI_PORT_A	0
#define FSI_PORT_B	1

#include <linux/clk.h>
#include <sound/soc.h>

/*
 * flags format
 *
 * 0x00000CBA
 *
 * A:  inversion
 * B:  format mode
 * C:  chip specific
 * D:  clock selecter if master mode
 */

/* A: clock inversion */
#define SH_FSI_INVERSION_MASK	0x0000000F
#define SH_FSI_LRM_INV		(1 << 0)
#define SH_FSI_BRM_INV		(1 << 1)
#define SH_FSI_LRS_INV		(1 << 2)
#define SH_FSI_BRS_INV		(1 << 3)

/* B: format mode */
#define SH_FSI_FMT_MASK		0x000000F0
#define SH_FSI_FMT_DAI		(0 << 4)
#define SH_FSI_FMT_SPDIF	(1 << 4)

/* C: chip specific */
#define SH_FSI_OPTION_MASK	0x00000F00
#define SH_FSI_ENABLE_STREAM_MODE	(1 << 8) /* for 16bit data */

/* D:  clock selecter if master mode */
#define SH_FSI_CLK_MASK		0x0000F000
#define SH_FSI_CLK_EXTERNAL	(0 << 12)
#define SH_FSI_CLK_CPG		(1 << 12) /* FSIxCK + FSI-DIV */

struct sh_fsi_port_info {
	unsigned long flags;
	int tx_id;
	int rx_id;
};

struct sh_fsi_platform_info {
	struct sh_fsi_port_info port_a;
	struct sh_fsi_port_info port_b;
};

#endif /* __SOUND_FSI_H */
