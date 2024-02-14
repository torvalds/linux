/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ALSA SoC Audio Layer - Rockchip SPDIF transceiver driver
 *
 * Copyright (c) 2015 Collabora Ltd.
 * Author: Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 */

#ifndef _ROCKCHIP_SPDIF_H
#define _ROCKCHIP_SPDIF_H

/*
 * CFGR
 * transfer configuration register
*/
#define SPDIF_CFGR_CLK_DIV_SHIFT	(16)
#define SPDIF_CFGR_CLK_DIV_MASK		(0xff << SPDIF_CFGR_CLK_DIV_SHIFT)
#define SPDIF_CFGR_CLK_DIV(x)		(x << SPDIF_CFGR_CLK_DIV_SHIFT)

#define SPDIF_CFGR_HALFWORD_SHIFT	2
#define SPDIF_CFGR_HALFWORD_DISABLE	(0 << SPDIF_CFGR_HALFWORD_SHIFT)
#define SPDIF_CFGR_HALFWORD_ENABLE	(1 << SPDIF_CFGR_HALFWORD_SHIFT)

#define SPDIF_CFGR_VDW_SHIFT	0
#define SPDIF_CFGR_VDW(x)	(x << SPDIF_CFGR_VDW_SHIFT)
#define SDPIF_CFGR_VDW_MASK	(0xf << SPDIF_CFGR_VDW_SHIFT)

#define SPDIF_CFGR_VDW_16	SPDIF_CFGR_VDW(0x0)
#define SPDIF_CFGR_VDW_20	SPDIF_CFGR_VDW(0x1)
#define SPDIF_CFGR_VDW_24	SPDIF_CFGR_VDW(0x2)

/*
 * DMACR
 * DMA control register
*/
#define SPDIF_DMACR_TDE_SHIFT	5
#define SPDIF_DMACR_TDE_DISABLE	(0 << SPDIF_DMACR_TDE_SHIFT)
#define SPDIF_DMACR_TDE_ENABLE	(1 << SPDIF_DMACR_TDE_SHIFT)

#define SPDIF_DMACR_TDL_SHIFT	0
#define SPDIF_DMACR_TDL(x)	((x) << SPDIF_DMACR_TDL_SHIFT)
#define SPDIF_DMACR_TDL_MASK	(0x1f << SPDIF_DMACR_TDL_SHIFT)

/*
 * XFER
 * Transfer control register
*/
#define SPDIF_XFER_TXS_SHIFT	0
#define SPDIF_XFER_TXS_STOP	(0 << SPDIF_XFER_TXS_SHIFT)
#define SPDIF_XFER_TXS_START	(1 << SPDIF_XFER_TXS_SHIFT)

#define SPDIF_CFGR	(0x0000)
#define SPDIF_SDBLR	(0x0004)
#define SPDIF_DMACR	(0x0008)
#define SPDIF_INTCR	(0x000c)
#define SPDIF_INTSR	(0x0010)
#define SPDIF_XFER	(0x0018)
#define SPDIF_SMPDR	(0x0020)

#endif /* _ROCKCHIP_SPDIF_H */
