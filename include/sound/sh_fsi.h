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

#define SH_FSI_FMT_MONO		0
#define SH_FSI_FMT_MONO_DELAY	1
#define SH_FSI_FMT_PCM		2
#define SH_FSI_FMT_I2S		3
#define SH_FSI_FMT_TDM		4
#define SH_FSI_FMT_TDM_DELAY	5
#define SH_FSI_FMT_SPDIF	6


#define SH_FSI_IFMT_TDM_CH(x) \
	(SH_FSI_IFMT(TDM)	| SH_FSI_SET_CH_I(x))
#define SH_FSI_IFMT_TDM_DELAY_CH(x) \
	(SH_FSI_IFMT(TDM_DELAY)	| SH_FSI_SET_CH_I(x))

#define SH_FSI_OFMT_TDM_CH(x) \
	(SH_FSI_OFMT(TDM)	| SH_FSI_SET_CH_O(x))
#define SH_FSI_OFMT_TDM_DELAY_CH(x) \
	(SH_FSI_OFMT(TDM_DELAY)	| SH_FSI_SET_CH_O(x))


/*
 * set_rate return value
 *
 * see ACKMD/BPFMD on
 *     ACK_MD (FSI2)
 *     CKG1   (FSI)
 *
 * err:  return value < 0
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

struct sh_fsi_platform_info {
	unsigned long porta_flags;
	unsigned long portb_flags;
	int (*set_rate)(int is_porta, int rate); /* for master mode */
};

extern struct snd_soc_dai fsi_soc_dai[2];
extern struct snd_soc_platform fsi_soc_platform;

#endif /* __SOUND_FSI_H */
