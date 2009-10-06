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

/* flags format

 * 0xABCDEEFF
 *
 * A:  channel size for TDM (input)
 * B:  channel size for TDM (ooutput)
 * C:  inversion
 * D:  mode
 * E:  input format
 * F:  output format
 */

#include <linux/clk.h>
#include <sound/soc.h>

/* TDM channel */
#define SH_FSI_SET_CH_I(x)	((x & 0xF) << 28)
#define SH_FSI_SET_CH_O(x)	((x & 0xF) << 24)

#define SH_FSI_CH_IMASK		0xF0000000
#define SH_FSI_CH_OMASK		0x0F000000
#define SH_FSI_GET_CH_I(x)	((x & SH_FSI_CH_IMASK) >> 28)
#define SH_FSI_GET_CH_O(x)	((x & SH_FSI_CH_OMASK) >> 24)

/* clock inversion */
#define SH_FSI_INVERSION_MASK	0x00F00000
#define SH_FSI_LRM_INV		(1 << 20)
#define SH_FSI_BRM_INV		(1 << 21)
#define SH_FSI_LRS_INV		(1 << 22)
#define SH_FSI_BRS_INV		(1 << 23)

/* mode */
#define SH_FSI_MODE_MASK	0x000F0000
#define SH_FSI_IN_SLAVE_MODE	(1 << 16)  /* default master mode */
#define SH_FSI_OUT_SLAVE_MODE	(1 << 17)  /* default master mode */

/* DI format */
#define SH_FSI_FMT_MASK		0x000000FF
#define SH_FSI_IFMT(x)		(((SH_FSI_FMT_ ## x) & SH_FSI_FMT_MASK) << 8)
#define SH_FSI_OFMT(x)		(((SH_FSI_FMT_ ## x) & SH_FSI_FMT_MASK) << 0)
#define SH_FSI_GET_IFMT(x)	((x >> 8) & SH_FSI_FMT_MASK)
#define SH_FSI_GET_OFMT(x)	((x >> 0) & SH_FSI_FMT_MASK)

#define SH_FSI_FMT_MONO		(1 << 0)
#define SH_FSI_FMT_MONO_DELAY	(1 << 1)
#define SH_FSI_FMT_PCM		(1 << 2)
#define SH_FSI_FMT_I2S		(1 << 3)
#define SH_FSI_FMT_TDM		(1 << 4)
#define SH_FSI_FMT_TDM_DELAY	(1 << 5)

#define SH_FSI_IFMT_TDM_CH(x) \
	(SH_FSI_IFMT(TDM)	| SH_FSI_SET_CH_I(x))
#define SH_FSI_IFMT_TDM_DELAY_CH(x) \
	(SH_FSI_IFMT(TDM_DELAY)	| SH_FSI_SET_CH_I(x))

#define SH_FSI_OFMT_TDM_CH(x) \
	(SH_FSI_OFMT(TDM)	| SH_FSI_SET_CH_O(x))
#define SH_FSI_OFMT_TDM_DELAY_CH(x) \
	(SH_FSI_OFMT(TDM_DELAY)	| SH_FSI_SET_CH_O(x))

struct sh_fsi_platform_info {
	unsigned long porta_flags;
	unsigned long portb_flags;
};

extern struct snd_soc_dai fsi_soc_dai[2];
extern struct snd_soc_platform fsi_soc_platform;

#endif /* __SOUND_FSI_H */
