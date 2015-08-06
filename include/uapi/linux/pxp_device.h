/*
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef _UAPI_PXP_DEVICE
#define _UAPI_PXP_DEVICE

#include <linux/pxp_dma.h>

struct pxp_chan_handle {
	unsigned int handle;
	int hist_status;
};

struct pxp_mem_desc {
	unsigned int handle;
	unsigned int size;
	dma_addr_t phys_addr;
	unsigned int virt_uaddr;		/* virtual user space address */
	unsigned int mtype;
};

struct pxp_mem_flush {
	unsigned int handle;
	unsigned int type;
};

#define PXP_IOC_MAGIC  'P'

#define PXP_IOC_GET_CHAN      _IOR(PXP_IOC_MAGIC, 0, struct pxp_mem_desc)
#define PXP_IOC_PUT_CHAN      _IOW(PXP_IOC_MAGIC, 1, struct pxp_mem_desc)
#define PXP_IOC_CONFIG_CHAN   _IOW(PXP_IOC_MAGIC, 2, struct pxp_mem_desc)
#define PXP_IOC_START_CHAN    _IOW(PXP_IOC_MAGIC, 3, struct pxp_mem_desc)
#define PXP_IOC_GET_PHYMEM    _IOWR(PXP_IOC_MAGIC, 4, struct pxp_mem_desc)
#define PXP_IOC_PUT_PHYMEM    _IOW(PXP_IOC_MAGIC, 5, struct pxp_mem_desc)
#define PXP_IOC_WAIT4CMPLT    _IOWR(PXP_IOC_MAGIC, 6, struct pxp_mem_desc)
#define PXP_IOC_FLUSH_PHYMEM   _IOR(PXP_IOC_MAGIC, 7, struct pxp_mem_flush)

/* Memory types supported*/
#define MEMORY_TYPE_UNCACHED 0x0
#define MEMORY_TYPE_WC	     0x1
#define MEMORY_TYPE_CACHED   0x2

/* Cache flush operations */
#define CACHE_CLEAN      0x1
#define CACHE_INVALIDATE 0x2
#define CACHE_FLUSH      0x4

#endif
