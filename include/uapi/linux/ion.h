/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * drivers/staging/android/uapi/ion.h
 *
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef _UAPI_LINUX_ION_H
#define _UAPI_LINUX_ION_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * ion_heap_types - list of all possible types of heaps that Android can use
 *
 * @ION_HEAP_TYPE_SYSTEM:        Reserved heap id for ion heap that allocates
 *				 memory using alloc_page(). Also, supports
 *				 deferred free and allocation pools.
 * @ION_HEAP_TYPE_SYSTEM_CONTIG: Reserved heap id for ion heap that is the same
 *				 as SYSTEM_HEAP, except doesn't support
 *				 allocation pools.
 * @ION_HEAP_TYPE_CARVEOUT:      Reserved heap id for ion heap that allocates
 *				 memory from a pre-reserved memory region
 *				 aka 'carveout'.
 * @ION_HEAP_TYPE_DMA:		 Reserved heap id for ion heap that manages
 * 				 single CMA (contiguous memory allocator)
 * 				 region. Uses standard DMA APIs for
 *				 managing memory within the CMA region.
 */
enum ion_heap_type {
	ION_HEAP_TYPE_SYSTEM = 0,
	ION_HEAP_TYPE_SYSTEM_CONTIG = 1,
	ION_HEAP_TYPE_CARVEOUT = 2,
	ION_HEAP_TYPE_CHUNK = 3,
	ION_HEAP_TYPE_DMA = 4,
	/* reserved range for future standard heap types */
	ION_HEAP_TYPE_CUSTOM = 16,
	ION_HEAP_TYPE_MAX = 31,
};

/**
 * ion_heap_id - list of standard heap ids that Android can use
 *
 * @ION_HEAP_SYSTEM		Id for the ION_HEAP_TYPE_SYSTEM
 * @ION_HEAP_SYSTEM_CONTIG	Id for the ION_HEAP_TYPE_SYSTEM_CONTIG
 * @ION_HEAP_CHUNK		Id for the ION_HEAP_TYPE_CHUNK
 * @ION_HEAP_CARVEOUT_START	Start of reserved id range for heaps of type
 *				ION_HEAP_TYPE_CARVEOUT
 * @ION_HEAP_CARVEOUT_END	End of reserved id range for heaps of type
 *				ION_HEAP_TYPE_CARVEOUT
 * @ION_HEAP_DMA_START 		Start of reserved id range for heaps of type
 *				ION_HEAP_TYPE_DMA
 * @ION_HEAP_DMA_END		End of reserved id range for heaps of type
 *				ION_HEAP_TYPE_DMA
 * @ION_HEAP_CUSTOM_START	Start of reserved id range for heaps of custom
 *				type
 * @ION_HEAP_CUSTOM_END		End of reserved id range for heaps of custom
 *				type
 */
enum ion_heap_id {
	ION_HEAP_SYSTEM = (1 << ION_HEAP_TYPE_SYSTEM),
	ION_HEAP_SYSTEM_CONTIG = (ION_HEAP_SYSTEM << 1),
	ION_HEAP_CARVEOUT_START = (ION_HEAP_SYSTEM_CONTIG << 1),
	ION_HEAP_CARVEOUT_END = (ION_HEAP_CARVEOUT_START << 4),
	ION_HEAP_CHUNK = (ION_HEAP_CARVEOUT_END << 1),
	ION_HEAP_DMA_START = (ION_HEAP_CHUNK << 1),
	ION_HEAP_DMA_END = (ION_HEAP_DMA_START << 7),
	ION_HEAP_CUSTOM_START = (ION_HEAP_DMA_END << 1),
	ION_HEAP_CUSTOM_END = (ION_HEAP_CUSTOM_START << 15),
};

#define ION_NUM_MAX_HEAPS	(32)

/**
 * allocation flags - the lower 16 bits are used by core ion, the upper 16
 * bits are reserved for use by the heaps themselves.
 */

/*
 * mappings of this buffer should be cached, ion will do cache maintenance
 * when the buffer is mapped for dma
 */
#define ION_FLAG_CACHED		1

/**
 * DOC: Ion Userspace API
 *
 * create a client by opening /dev/ion
 * most operations handled via following ioctls
 *
 */

/**
 * struct ion_allocation_data - metadata passed from userspace for allocations
 * @len:		size of the allocation
 * @heap_id_mask:	mask of heap ids to allocate from
 * @flags:		flags passed to heap
 * @handle:		pointer that will be populated with a cookie to use to
 *			refer to this allocation
 *
 * Provided by userspace as an argument to the ioctl
 */
struct ion_allocation_data {
	__u64 len;
	__u32 heap_id_mask;
	__u32 flags;
	__u32 fd;
	__u32 unused;
};

#define MAX_HEAP_NAME			32

/**
 * struct ion_heap_data - data about a heap
 * @name - first 32 characters of the heap name
 * @type - heap type
 * @heap_id - heap id for the heap
 */
struct ion_heap_data {
	char name[MAX_HEAP_NAME];
	__u32 type;
	__u32 heap_id;
	__u32 reserved0;
	__u32 reserved1;
	__u32 reserved2;
};

/**
 * struct ion_heap_query - collection of data about all heaps
 * @cnt - total number of heaps to be copied
 * @heaps - buffer to copy heap data
 */
struct ion_heap_query {
	__u32 cnt; /* Total number of heaps to be copied */
	__u32 reserved0; /* align to 64bits */
	__u64 heaps; /* buffer to be populated */
	__u32 reserved1;
	__u32 reserved2;
};

#define ION_IOC_MAGIC		'I'

/**
 * DOC: ION_IOC_ALLOC - allocate memory
 *
 * Takes an ion_allocation_data struct and returns it with the handle field
 * populated with the opaque handle for the allocation.
 */
#define ION_IOC_ALLOC		_IOWR(ION_IOC_MAGIC, 0, \
				      struct ion_allocation_data)

/**
 * DOC: ION_IOC_HEAP_QUERY - information about available heaps
 *
 * Takes an ion_heap_query structure and populates information about
 * available Ion heaps.
 */
#define ION_IOC_HEAP_QUERY     _IOWR(ION_IOC_MAGIC, 8, \
					struct ion_heap_query)

#endif /* _UAPI_LINUX_ION_H */
