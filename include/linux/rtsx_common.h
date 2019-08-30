/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Driver for Realtek driver-based card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 */

#ifndef __RTSX_COMMON_H
#define __RTSX_COMMON_H

#define DRV_NAME_RTSX_PCI		"rtsx_pci"
#define DRV_NAME_RTSX_PCI_SDMMC		"rtsx_pci_sdmmc"
#define DRV_NAME_RTSX_PCI_MS		"rtsx_pci_ms"

#define RTSX_REG_PAIR(addr, val)	(((u32)(addr) << 16) | (u8)(val))

#define RTSX_SSC_DEPTH_4M		0x01
#define RTSX_SSC_DEPTH_2M		0x02
#define RTSX_SSC_DEPTH_1M		0x03
#define RTSX_SSC_DEPTH_500K		0x04
#define RTSX_SSC_DEPTH_250K		0x05

#define RTSX_SD_CARD			0
#define RTSX_MS_CARD			1

#define CLK_TO_DIV_N			0
#define DIV_N_TO_CLK			1

struct platform_device;

struct rtsx_slot {
	struct platform_device	*p_dev;
	void			(*card_event)(struct platform_device *p_dev);
};

#endif
