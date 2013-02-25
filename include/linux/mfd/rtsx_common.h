/* Driver for Realtek driver-based card reader
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
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

struct platform_device;

struct rtsx_slot {
	struct platform_device	*p_dev;
	void			(*card_event)(struct platform_device *p_dev);
};

#endif
