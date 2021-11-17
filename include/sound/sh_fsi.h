/* SPDX-License-Identifier: GPL-2.0
 *
 * Fifo-attached Serial Interface (FSI) support for SH7724
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 */
#ifndef __SOUND_FSI_H
#define __SOUND_FSI_H

#include <linux/clk.h>
#include <sound/soc.h>

/*
 * flags
 */
#define SH_FSI_FMT_SPDIF		(1 << 0) /* spdif for HDMI */
#define SH_FSI_ENABLE_STREAM_MODE	(1 << 1) /* for 16bit data */
#define SH_FSI_CLK_CPG			(1 << 2) /* FSIxCK + FSI-DIV */

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
