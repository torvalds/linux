/*
 *  PS3 platform declarations.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(_ASM_POWERPC_PS3_H)
#define _ASM_POWERPC_PS3_H

#include <linux/compiler.h> /* for __deprecated */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>

/**
 * struct ps3_device_id - HV bus device identifier from the system repository
 * @bus_id: HV bus id, {1..} (zero invalid)
 * @dev_id: HV device id, {0..}
 */

struct ps3_device_id {
	unsigned int bus_id;
	unsigned int dev_id;
};


/* dma routines */

enum ps3_dma_page_size {
	PS3_DMA_4K = 12U,
	PS3_DMA_64K = 16U,
	PS3_DMA_1M = 20U,
	PS3_DMA_16M = 24U,
};

enum ps3_dma_region_type {
	PS3_DMA_OTHER = 0,
	PS3_DMA_INTERNAL = 2,
};

/**
 * struct ps3_dma_region - A per device dma state variables structure
 * @did: The HV device id.
 * @page_size: The ioc pagesize.
 * @region_type: The HV region type.
 * @bus_addr: The 'translated' bus address of the region.
 * @len: The length in bytes of the region.
 * @chunk_list: Opaque variable used by the ioc page manager.
 */

struct ps3_dma_region {
	struct ps3_device_id did;
	enum ps3_dma_page_size page_size;
	enum ps3_dma_region_type region_type;
	unsigned long bus_addr;
	unsigned long len;
	struct {
		spinlock_t lock;
		struct list_head head;
	} chunk_list;
};

/**
 * struct ps3_dma_region_init - Helper to initialize structure variables
 *
 * Helper to properly initialize variables prior to calling
 * ps3_system_bus_device_register.
 */

static inline void ps3_dma_region_init(struct ps3_dma_region *r,
	const struct ps3_device_id* did, enum ps3_dma_page_size page_size,
	enum ps3_dma_region_type region_type)
{
	r->did = *did;
	r->page_size = page_size;
	r->region_type = region_type;
}
int ps3_dma_region_create(struct ps3_dma_region *r);
int ps3_dma_region_free(struct ps3_dma_region *r);
int ps3_dma_map(struct ps3_dma_region *r, unsigned long virt_addr,
	unsigned long len, unsigned long *bus_addr);
int ps3_dma_unmap(struct ps3_dma_region *r, unsigned long bus_addr,
	unsigned long len);

/* mmio routines */

enum ps3_mmio_page_size {
	PS3_MMIO_4K = 12U,
	PS3_MMIO_64K = 16U
};

/**
 * struct ps3_mmio_region - a per device mmio state variables structure
 *
 * Current systems can be supported with a single region per device.
 */

struct ps3_mmio_region {
	struct ps3_device_id did;
	unsigned long bus_addr;
	unsigned long len;
	enum ps3_mmio_page_size page_size;
	unsigned long lpar_addr;
};

/**
 * struct ps3_mmio_region_init - Helper to initialize structure variables
 *
 * Helper to properly initialize variables prior to calling
 * ps3_system_bus_device_register.
 */

static inline void ps3_mmio_region_init(struct ps3_mmio_region *r,
	const struct ps3_device_id* did, unsigned long bus_addr,
	unsigned long len, enum ps3_mmio_page_size page_size)
{
	r->did = *did;
	r->bus_addr = bus_addr;
	r->len = len;
	r->page_size = page_size;
}
int ps3_mmio_region_create(struct ps3_mmio_region *r);
int ps3_free_mmio_region(struct ps3_mmio_region *r);
unsigned long ps3_mm_phys_to_lpar(unsigned long phys_addr);

/* inrerrupt routines */

int ps3_alloc_io_irq(unsigned int interrupt_id, unsigned int *virq);
int ps3_free_io_irq(unsigned int virq);
int ps3_alloc_event_irq(unsigned int *virq);
int ps3_free_event_irq(unsigned int virq);
int ps3_send_event_locally(unsigned int virq);
int ps3_connect_event_irq(const struct ps3_device_id *did,
	unsigned int interrupt_id, unsigned int *virq);
int ps3_disconnect_event_irq(const struct ps3_device_id *did,
	unsigned int interrupt_id, unsigned int virq);
int ps3_alloc_vuart_irq(void* virt_addr_bmp, unsigned int *virq);
int ps3_free_vuart_irq(unsigned int virq);
int ps3_alloc_spe_irq(unsigned long spe_id, unsigned int class,
	unsigned int *virq);
int ps3_free_spe_irq(unsigned int virq);

/* lv1 result codes */

enum lv1_result {
	LV1_SUCCESS                     = 0,
	/* not used                       -1 */
	LV1_RESOURCE_SHORTAGE           = -2,
	LV1_NO_PRIVILEGE                = -3,
	LV1_DENIED_BY_POLICY            = -4,
	LV1_ACCESS_VIOLATION            = -5,
	LV1_NO_ENTRY                    = -6,
	LV1_DUPLICATE_ENTRY             = -7,
	LV1_TYPE_MISMATCH               = -8,
	LV1_BUSY                        = -9,
	LV1_EMPTY                       = -10,
	LV1_WRONG_STATE                 = -11,
	/* not used                       -12 */
	LV1_NO_MATCH                    = -13,
	LV1_ALREADY_CONNECTED           = -14,
	LV1_UNSUPPORTED_PARAMETER_VALUE = -15,
	LV1_CONDITION_NOT_SATISFIED     = -16,
	LV1_ILLEGAL_PARAMETER_VALUE     = -17,
	LV1_BAD_OPTION                  = -18,
	LV1_IMPLEMENTATION_LIMITATION   = -19,
	LV1_NOT_IMPLEMENTED             = -20,
	LV1_INVALID_CLASS_ID            = -21,
	LV1_CONSTRAINT_NOT_SATISFIED    = -22,
	LV1_ALIGNMENT_ERROR             = -23,
	LV1_INTERNAL_ERROR              = -32768,
};

static inline const char* ps3_result(int result)
{
#if defined(DEBUG)
	switch (result) {
	case LV1_SUCCESS:
		return "LV1_SUCCESS (0)";
	case -1:
		return "** unknown result ** (-1)";
	case LV1_RESOURCE_SHORTAGE:
		return "LV1_RESOURCE_SHORTAGE (-2)";
	case LV1_NO_PRIVILEGE:
		return "LV1_NO_PRIVILEGE (-3)";
	case LV1_DENIED_BY_POLICY:
		return "LV1_DENIED_BY_POLICY (-4)";
	case LV1_ACCESS_VIOLATION:
		return "LV1_ACCESS_VIOLATION (-5)";
	case LV1_NO_ENTRY:
		return "LV1_NO_ENTRY (-6)";
	case LV1_DUPLICATE_ENTRY:
		return "LV1_DUPLICATE_ENTRY (-7)";
	case LV1_TYPE_MISMATCH:
		return "LV1_TYPE_MISMATCH (-8)";
	case LV1_BUSY:
		return "LV1_BUSY (-9)";
	case LV1_EMPTY:
		return "LV1_EMPTY (-10)";
	case LV1_WRONG_STATE:
		return "LV1_WRONG_STATE (-11)";
	case -12:
		return "** unknown result ** (-12)";
	case LV1_NO_MATCH:
		return "LV1_NO_MATCH (-13)";
	case LV1_ALREADY_CONNECTED:
		return "LV1_ALREADY_CONNECTED (-14)";
	case LV1_UNSUPPORTED_PARAMETER_VALUE:
		return "LV1_UNSUPPORTED_PARAMETER_VALUE (-15)";
	case LV1_CONDITION_NOT_SATISFIED:
		return "LV1_CONDITION_NOT_SATISFIED (-16)";
	case LV1_ILLEGAL_PARAMETER_VALUE:
		return "LV1_ILLEGAL_PARAMETER_VALUE (-17)";
	case LV1_BAD_OPTION:
		return "LV1_BAD_OPTION (-18)";
	case LV1_IMPLEMENTATION_LIMITATION:
		return "LV1_IMPLEMENTATION_LIMITATION (-19)";
	case LV1_NOT_IMPLEMENTED:
		return "LV1_NOT_IMPLEMENTED (-20)";
	case LV1_INVALID_CLASS_ID:
		return "LV1_INVALID_CLASS_ID (-21)";
	case LV1_CONSTRAINT_NOT_SATISFIED:
		return "LV1_CONSTRAINT_NOT_SATISFIED (-22)";
	case LV1_ALIGNMENT_ERROR:
		return "LV1_ALIGNMENT_ERROR (-23)";
	case LV1_INTERNAL_ERROR:
		return "LV1_INTERNAL_ERROR (-32768)";
	default:
		BUG();
		return "** unknown result **";
	};
#else
	return "";
#endif
}

#endif
