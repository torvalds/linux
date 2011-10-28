/*
 *  intel_mid_dma.h - Intel MID DMA Drivers
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Author: Vinod Koul <vinod.koul@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *
 */
#ifndef __INTEL_MID_DMA_H__
#define __INTEL_MID_DMA_H__

#include <linux/dmaengine.h>

#define DMA_PREP_CIRCULAR_LIST		(1 << 10)

/*DMA mode configurations*/
enum intel_mid_dma_mode {
	LNW_DMA_PER_TO_MEM = 0, /*periphral to memory configuration*/
	LNW_DMA_MEM_TO_PER,	/*memory to periphral configuration*/
	LNW_DMA_MEM_TO_MEM,	/*mem to mem confg (testing only)*/
};

/*DMA handshaking*/
enum intel_mid_dma_hs_mode {
	LNW_DMA_HW_HS = 0,	/*HW Handshaking only*/
	LNW_DMA_SW_HS = 1,	/*SW Handshaking not recommended*/
};

/*Burst size configuration*/
enum intel_mid_dma_msize {
	LNW_DMA_MSIZE_1 = 0x0,
	LNW_DMA_MSIZE_4 = 0x1,
	LNW_DMA_MSIZE_8 = 0x2,
	LNW_DMA_MSIZE_16 = 0x3,
	LNW_DMA_MSIZE_32 = 0x4,
	LNW_DMA_MSIZE_64 = 0x5,
};

/**
 * struct intel_mid_dma_slave - DMA slave structure
 *
 * @dirn: DMA trf direction
 * @src_width: tx register width
 * @dst_width: rx register width
 * @hs_mode: HW/SW handshaking mode
 * @cfg_mode: DMA data transfer mode (per-per/mem-per/mem-mem)
 * @src_msize: Source DMA burst size
 * @dst_msize: Dst DMA burst size
 * @per_addr: Periphral address
 * @device_instance: DMA peripheral device instance, we can have multiple
 *		peripheral device connected to single DMAC
 */
struct intel_mid_dma_slave {
	enum intel_mid_dma_hs_mode	hs_mode;  /*handshaking*/
	enum intel_mid_dma_mode		cfg_mode; /*mode configuration*/
	unsigned int		device_instance; /*0, 1 for periphral instance*/
	struct dma_slave_config		dma_slave;
};

#endif /*__INTEL_MID_DMA_H__*/
