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

/*
 * set_rate return value
 *
 * see ACKMD/BPFMD on
 *     ACK_MD (FSI2)
 *     CKG1   (FSI)
 *
 * err		: return value <  0
 * no change	: return value == 0
 * change xMD	: return value >  0
 *
 * 0x-00000AB
 *
 * A:  ACKMD value
 * B:  BPFMD value
 */

#define SH_FSI_ACKMD_MASK	(0xF << 0)
#define SH_FSI_ACKMD_512	(1 << 0)
#define SH_FSI_ACKMD_256	(2 << 0)
#define SH_FSI_ACKMD_128	(3 << 0)
#define SH_FSI_ACKMD_64		(4 << 0)
#define SH_FSI_ACKMD_32		(5 << 0)

#define SH_FSI_BPFMD_MASK	(0xF << 4)
#define SH_FSI_BPFMD_512	(1 << 4)
#define SH_FSI_BPFMD_256	(2 << 4)
#define SH_FSI_BPFMD_128	(3 << 4)
#define SH_FSI_BPFMD_64		(4 << 4)
#define SH_FSI_BPFMD_32		(5 << 4)
#define SH_FSI_BPFMD_16		(6 << 4)

struct sh_fsi_port_info {
	unsigned long flags;
	int tx_id;
	int rx_id;
	int (*set_rate)(struct device *dev, int rate, int enable);
};

struct sh_fsi_platform_info {
	struct sh_fsi_port_info port_a;
	struct sh_fsi_port_info port_b;
};

#endif /* __SOUND_FSI_H */
