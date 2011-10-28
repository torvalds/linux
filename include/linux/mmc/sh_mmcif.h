/*
 * include/linux/mmc/sh_mmcif.h
 *
 * platform data for eMMC driver
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 */

#ifndef __SH_MMCIF_H__
#define __SH_MMCIF_H__

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/sh_dma.h>

/*
 * MMCIF : CE_CLK_CTRL [19:16]
 * 1000 : Peripheral clock / 512
 * 0111 : Peripheral clock / 256
 * 0110 : Peripheral clock / 128
 * 0101 : Peripheral clock / 64
 * 0100 : Peripheral clock / 32
 * 0011 : Peripheral clock / 16
 * 0010 : Peripheral clock / 8
 * 0001 : Peripheral clock / 4
 * 0000 : Peripheral clock / 2
 * 1111 : Peripheral clock (sup_pclk set '1')
 */

struct sh_mmcif_dma {
	struct sh_dmae_slave chan_priv_tx;
	struct sh_dmae_slave chan_priv_rx;
};

struct sh_mmcif_plat_data {
	void (*set_pwr)(struct platform_device *pdev, int state);
	void (*down_pwr)(struct platform_device *pdev);
	int (*get_cd)(struct platform_device *pdef);
	struct sh_mmcif_dma	*dma;
	u8			sup_pclk;	/* 1 :SH7757, 0: SH7724/SH7372 */
	unsigned long		caps;
	u32			ocr;
};

#define MMCIF_CE_CMD_SET	0x00000000
#define MMCIF_CE_ARG		0x00000008
#define MMCIF_CE_ARG_CMD12	0x0000000C
#define MMCIF_CE_CMD_CTRL	0x00000010
#define MMCIF_CE_BLOCK_SET	0x00000014
#define MMCIF_CE_CLK_CTRL	0x00000018
#define MMCIF_CE_BUF_ACC	0x0000001C
#define MMCIF_CE_RESP3		0x00000020
#define MMCIF_CE_RESP2		0x00000024
#define MMCIF_CE_RESP1		0x00000028
#define MMCIF_CE_RESP0		0x0000002C
#define MMCIF_CE_RESP_CMD12	0x00000030
#define MMCIF_CE_DATA		0x00000034
#define MMCIF_CE_INT		0x00000040
#define MMCIF_CE_INT_MASK	0x00000044
#define MMCIF_CE_HOST_STS1	0x00000048
#define MMCIF_CE_HOST_STS2	0x0000004C
#define MMCIF_CE_VERSION	0x0000007C

/* CE_BUF_ACC */
#define BUF_ACC_DMAWEN		(1 << 25)
#define BUF_ACC_DMAREN		(1 << 24)
#define BUF_ACC_BUSW_32		(0 << 17)
#define BUF_ACC_BUSW_16		(1 << 17)
#define BUF_ACC_ATYP		(1 << 16)

/* CE_CLK_CTRL */
#define CLK_ENABLE		(1 << 24) /* 1: output mmc clock */
#define CLK_CLEAR		((1 << 19) | (1 << 18) | (1 << 17) | (1 << 16))
#define CLK_SUP_PCLK		((1 << 19) | (1 << 18) | (1 << 17) | (1 << 16))
#define CLKDIV_4		(1<<16) /* mmc clock frequency.
					 * n: bus clock/(2^(n+1)) */
#define CLKDIV_256		(7<<16) /* mmc clock frequency. (see above) */
#define SRSPTO_256		((1 << 13) | (0 << 12)) /* resp timeout */
#define SRBSYTO_29		((1 << 11) | (1 << 10) |	\
				 (1 << 9) | (1 << 8)) /* resp busy timeout */
#define SRWDTO_29		((1 << 7) | (1 << 6) |		\
				 (1 << 5) | (1 << 4)) /* read/write timeout */
#define SCCSTO_29		((1 << 3) | (1 << 2) |		\
				 (1 << 1) | (1 << 0)) /* ccs timeout */

/* CE_VERSION */
#define SOFT_RST_ON		(1 << 31)
#define SOFT_RST_OFF		0

static inline u32 sh_mmcif_readl(void __iomem *addr, int reg)
{
	return __raw_readl(addr + reg);
}

static inline void sh_mmcif_writel(void __iomem *addr, int reg, u32 val)
{
	__raw_writel(val, addr + reg);
}

#define SH_MMCIF_BBS 512 /* boot block size */

static inline void sh_mmcif_boot_cmd_send(void __iomem *base,
					  unsigned long cmd, unsigned long arg)
{
	sh_mmcif_writel(base, MMCIF_CE_INT, 0);
	sh_mmcif_writel(base, MMCIF_CE_ARG, arg);
	sh_mmcif_writel(base, MMCIF_CE_CMD_SET, cmd);
}

static inline int sh_mmcif_boot_cmd_poll(void __iomem *base, unsigned long mask)
{
	unsigned long tmp;
	int cnt;

	for (cnt = 0; cnt < 1000000; cnt++) {
		tmp = sh_mmcif_readl(base, MMCIF_CE_INT);
		if (tmp & mask) {
			sh_mmcif_writel(base, MMCIF_CE_INT, tmp & ~mask);
			return 0;
		}
	}

	return -1;
}

static inline int sh_mmcif_boot_cmd(void __iomem *base,
				    unsigned long cmd, unsigned long arg)
{
	sh_mmcif_boot_cmd_send(base, cmd, arg);
	return sh_mmcif_boot_cmd_poll(base, 0x00010000);
}

static inline int sh_mmcif_boot_do_read_single(void __iomem *base,
					       unsigned int block_nr,
					       unsigned long *buf)
{
	int k;

	/* CMD13 - Status */
	sh_mmcif_boot_cmd(base, 0x0d400000, 0x00010000);

	if (sh_mmcif_readl(base, MMCIF_CE_RESP0) != 0x0900)
		return -1;

	/* CMD17 - Read */
	sh_mmcif_boot_cmd(base, 0x11480000, block_nr * SH_MMCIF_BBS);
	if (sh_mmcif_boot_cmd_poll(base, 0x00100000) < 0)
		return -1;

	for (k = 0; k < (SH_MMCIF_BBS / 4); k++)
		buf[k] = sh_mmcif_readl(base, MMCIF_CE_DATA);

	return 0;
}

static inline int sh_mmcif_boot_do_read(void __iomem *base,
					unsigned long first_block,
					unsigned long nr_blocks,
					void *buf)
{
	unsigned long k;
	int ret = 0;

	/* In data transfer mode: Set clock to Bus clock/4 (about 20Mhz) */
	sh_mmcif_writel(base, MMCIF_CE_CLK_CTRL,
			CLK_ENABLE | CLKDIV_4 | SRSPTO_256 |
			SRBSYTO_29 | SRWDTO_29 | SCCSTO_29);

	/* CMD9 - Get CSD */
	sh_mmcif_boot_cmd(base, 0x09806000, 0x00010000);

	/* CMD7 - Select the card */
	sh_mmcif_boot_cmd(base, 0x07400000, 0x00010000);

	/* CMD16 - Set the block size */
	sh_mmcif_boot_cmd(base, 0x10400000, SH_MMCIF_BBS);

	for (k = 0; !ret && k < nr_blocks; k++)
		ret = sh_mmcif_boot_do_read_single(base, first_block + k,
						   buf + (k * SH_MMCIF_BBS));

	return ret;
}

static inline void sh_mmcif_boot_init(void __iomem *base)
{
	/* reset */
	sh_mmcif_writel(base, MMCIF_CE_VERSION, SOFT_RST_ON);
	sh_mmcif_writel(base, MMCIF_CE_VERSION, SOFT_RST_OFF);

	/* byte swap */
	sh_mmcif_writel(base, MMCIF_CE_BUF_ACC, BUF_ACC_ATYP);

	/* Set block size in MMCIF hardware */
	sh_mmcif_writel(base, MMCIF_CE_BLOCK_SET, SH_MMCIF_BBS);

	/* Enable the clock, set it to Bus clock/256 (about 325Khz). */
	sh_mmcif_writel(base, MMCIF_CE_CLK_CTRL,
			CLK_ENABLE | CLKDIV_256 | SRSPTO_256 |
			SRBSYTO_29 | SRWDTO_29 | SCCSTO_29);

	/* CMD0 */
	sh_mmcif_boot_cmd(base, 0x00000040, 0);

	/* CMD1 - Get OCR */
	do {
		sh_mmcif_boot_cmd(base, 0x01405040, 0x40300000); /* CMD1 */
	} while ((sh_mmcif_readl(base, MMCIF_CE_RESP0) & 0x80000000)
		 != 0x80000000);

	/* CMD2 - Get CID */
	sh_mmcif_boot_cmd(base, 0x02806040, 0);

	/* CMD3 - Set card relative address */
	sh_mmcif_boot_cmd(base, 0x03400040, 0x00010000);
}

#endif /* __SH_MMCIF_H__ */
