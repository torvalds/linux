#ifndef __LINUX_CMA_H
#define __LINUX_CMA_H

/* linux/include/linux/cma.h
 *
 * Contiguous Memory Allocator framework
 * Copyright (c) 2010 by Samsung Electronics.
 * Written by Michal Nazarewicz (m.nazarewicz@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

/*
 * See Documentation/contiguous-memory.txt for details.
 */

#include <linux/ioctl.h>
#include <linux/types.h>


#define CMA_MAGIC (('c' << 24) | ('M' << 16) | ('a' << 8) | 0x42)

enum {
	CMA_REQ_DEV_KIND,
	CMA_REQ_FROM_REG
};

/**
 * An information about area exportable to user space.
 * @magic:	must always be CMA_MAGIC.
 * @type:	type of the request.
 * @spec:	either "dev/kind\0" or "regions\0" depending on @type.
 *		In any case, the string must be NUL terminated.
 *		additionally, in the latter case scanning stops at
 *		semicolon (';').
 * @size:	size of the chunk to allocate.
 * @alignment:	desired alignment of the chunk (must be power of two or zero).
 * @start:	when ioctl() finishes this stores physical address of the chunk.
 */
struct cma_alloc_request {
	__u32 magic;
	__u32 type;

	/* __u64 to be compatible accross 32 and 64 bit systems. */
	__u64 size;
	__u64 alignment;
	__u64 start;

	char spec[32];
};

#define IOCTL_CMA_ALLOC    _IOWR('p', 0, struct cma_alloc_request)


/***************************** Kernel level API *****************************/

#ifdef __KERNEL__

#include <linux/rbtree.h>
#include <linux/list.h>
#if defined CONFIG_CMA_SYSFS
#  include <linux/kobject.h>
#endif


struct device;
struct cma_info;

/*
 * Don't call it directly, use cma_alloc(), cma_alloc_from() or
 * cma_alloc_from_region().
 */
dma_addr_t __must_check
__cma_alloc(const struct device *dev, const char *type,
	    size_t size, dma_addr_t alignment);

/* Don't call it directly, use cma_info() or cma_info_about(). */
int
__cma_info(struct cma_info *info, const struct device *dev, const char *type);


/**
 * cma_alloc - allocates contiguous chunk of memory.
 * @dev:	The device to perform allocation for.
 * @type:	A type of memory to allocate.  Platform may define
 *		several different types of memory and device drivers
 *		can then request chunks of different types.  Usually it's
 *		safe to pass NULL here which is the same as passing
 *		"common".
 * @size:	Size of the memory to allocate in bytes.
 * @alignment:	Desired alignment in bytes.  Must be a power of two or
 *		zero.  If alignment is less then a page size it will be
 *		set to page size. If unsure, pass zero here.
 *
 * On error returns a negative error cast to dma_addr_t.  Use
 * IS_ERR_VALUE() to check if returned value is indeed an error.
 * Otherwise bus address of the chunk is returned.
 */
static inline dma_addr_t __must_check
cma_alloc(const struct device *dev, const char *type,
	  size_t size, dma_addr_t alignment)
{
	return dev ? __cma_alloc(dev, type, size, alignment) : -EINVAL;
}


/**
 * struct cma_info - information about regions returned by cma_info().
 * @lower_bound:	The smallest address that is possible to be
 *			allocated for given (dev, type) pair.
 * @upper_bound:	The one byte after the biggest address that is
 *			possible to be allocated for given (dev, type)
 *			pair.
 * @total_size:	Total size of regions mapped to (dev, type) pair.
 * @free_size:	Total free size in all of the regions mapped to (dev, type)
 *		pair.  Because of possible race conditions, it is not
 *		guaranteed that the value will be correct -- it gives only
 *		an approximation.
 * @count:	Number of regions mapped to (dev, type) pair.
 */
struct cma_info {
	dma_addr_t lower_bound, upper_bound;
	size_t total_size, free_size;
	unsigned count;
};

/**
 * cma_info - queries information about regions.
 * @info:	Pointer to a structure where to save the information.
 * @dev:	The device to query information for.
 * @type:	A type of memory to query information for.
 *		If unsure, pass NULL here which is equal to passing
 *		"common".
 *
 * On error returns a negative error, zero otherwise.
 */
static inline int
cma_info(struct cma_info *info, const struct device *dev, const char *type)
{
	return dev ? __cma_info(info, dev, type) : -EINVAL;
}


/**
 * cma_free - frees a chunk of memory.
 * @addr:	Beginning of the chunk.
 *
 * Returns -ENOENT if there is no chunk at given location; otherwise
 * zero.  In the former case issues a warning.
 */
int cma_free(dma_addr_t addr);

/**
 * cma_get_virt - frees virtual address of cma memory.
 * @phys:	physical addrress
 * @size:		size of memory
 * @noncached :	0 is cached, 1 is non-cached.
 *
 * Returns -ENOENT if there is no chunk at given location; otherwise
 * zero.  In the former case issues a warning.
 */
void *cma_get_virt(dma_addr_t phys, dma_addr_t size, int noncached);


/****************************** Lower lever API *****************************/

/**
 * cma_alloc_from - allocates contiguous chunk of memory from named regions.
 * @regions:	Comma separated list of region names.  Terminated by NUL
 *		byte or a semicolon.
 * @size:	Size of the memory to allocate in bytes.
 * @alignment:	Desired alignment in bytes.  Must be a power of two or
 *		zero.  If alignment is less then a page size it will be
 *		set to page size. If unsure, pass zero here.
 *
 * On error returns a negative error cast to dma_addr_t.  Use
 * IS_ERR_VALUE() to check if returned value is indeed an error.
 * Otherwise bus address of the chunk is returned.
 */
static inline dma_addr_t __must_check
cma_alloc_from(const char *regions, size_t size, dma_addr_t alignment)
{
	return __cma_alloc(NULL, regions, size, alignment);
}

/**
 * cma_info_about - queries information about named regions.
 * @info:	Pointer to a structure where to save the information.
 * @regions:	Comma separated list of region names.  Terminated by NUL
 *		byte or a semicolon.
 *
 * On error returns a negative error, zero otherwise.
 */
static inline int
cma_info_about(struct cma_info *info, const const char *regions)
{
	return __cma_info(info, NULL, regions);
}



struct cma_allocator;

/**
 * struct cma_region - a region reserved for CMA allocations.
 * @name:	Unique name of the region.  Read only.
 * @start:	Bus address of the region in bytes.  Always aligned at
 *		least to a full page.  Read only.
 * @size:	Size of the region in bytes.  Multiply of a page size.
 *		Read only.
 * @free_space:	Free space in the region.  Read only.
 * @alignment:	Desired alignment of the region in bytes.  A power of two,
 *		always at least page size.  Early.
 * @alloc:	Allocator used with this region.  NULL means allocator is
 *		not attached.  Private.
 * @alloc_name:	Allocator name read from cmdline.  Private.  This may be
 *		different from @alloc->name.
 * @private_data:	Allocator's private data.
 * @users:	Number of chunks allocated in this region.
 * @list:	Entry in list of regions.  Private.
 * @used:	Whether region was already used, ie. there was at least
 *		one allocation request for.  Private.
 * @registered:	Whether this region has been registered.  Read only.
 * @reserved:	Whether this region has been reserved.  Early.  Read only.
 * @copy_name:	Whether @name and @alloc_name needs to be copied when
 *		this region is converted from early to normal.  Early.
 *		Private.
 * @free_alloc_name:	Whether @alloc_name was kmalloced().  Private.
 *
 * Regions come in two types: an early region and normal region.  The
 * former can be reserved or not-reserved.  Fields marked as "early"
 * are only meaningful in early regions.
 *
 * Early regions are important only during initialisation.  The list
 * of early regions is built from the "cma" command line argument or
 * platform defaults.  Platform initialisation code is responsible for
 * reserving space for unreserved regions that are placed on
 * cma_early_regions list.
 *
 * Later, during CMA initialisation all reserved regions from the
 * cma_early_regions list are registered as normal regions and can be
 * used using standard mechanisms.
 */
struct cma_region {
	const char *name;
	dma_addr_t start;
	size_t size;
	union {
		size_t free_space;	/* Normal region */
		dma_addr_t alignment;	/* Early region */
	};

	struct cma_allocator *alloc;
	const char *alloc_name;
	void *private_data;

	unsigned users;
	struct list_head list;

#if defined CONFIG_CMA_SYSFS
	struct kobject kobj;
#endif

	unsigned used:1;
	unsigned registered:1;
	unsigned reserved:1;
	unsigned copy_name:1;
	unsigned free_alloc_name:1;
};


/**
 * cma_region_register() - registers a region.
 * @reg:	Region to region.
 *
 * Region's start and size must be set.
 *
 * If name is set the region will be accessible using normal mechanism
 * like mapping or cma_alloc_from() function otherwise it will be
 * a private region and accessible only using the
 * cma_alloc_from_region() function.
 *
 * If alloc is set function will try to initialise given allocator
 * (and will return error if it failes).  Otherwise alloc_name may
 * point to a name of an allocator to use (if not set, the default
 * will be used).
 *
 * All other fields are ignored and/or overwritten.
 *
 * Returns zero or negative error.  In particular, -EADDRINUSE if
 * region overlap with already existing region.
 */
int __must_check cma_region_register(struct cma_region *reg);

/**
 * cma_region_unregister() - unregisters a region.
 * @reg:	Region to unregister.
 *
 * Region is unregistered only if there are no chunks allocated for
 * it.  Otherwise, function returns -EBUSY.
 *
 * On success returs zero.
 */
int __must_check cma_region_unregister(struct cma_region *reg);


/**
 * cma_alloc_from_region() - allocates contiguous chunk of memory from region.
 * @reg:	Region to allocate chunk from.
 * @size:	Size of the memory to allocate in bytes.
 * @alignment:	Desired alignment in bytes.  Must be a power of two or
 *		zero.  If alignment is less then a page size it will be
 *		set to page size. If unsure, pass zero here.
 *
 * On error returns a negative error cast to dma_addr_t.  Use
 * IS_ERR_VALUE() to check if returned value is indeed an error.
 * Otherwise bus address of the chunk is returned.
 */
dma_addr_t __must_check
cma_alloc_from_region(struct cma_region *reg,
		      size_t size, dma_addr_t alignment);



/****************************** Allocators API ******************************/

/**
 * struct cma_chunk - an allocated contiguous chunk of memory.
 * @start:	Bus address in bytes.
 * @size:	Size in bytes.
 * @free_space:	Free space in region in bytes.  Read only.
 * @reg:	Region this chunk belongs to.
 * @by_start:	A node in an red-black tree with all chunks sorted by
 *		start address.
 *
 * The cma_allocator::alloc() operation need to set only the @start
 * and @size fields.  The rest is handled by the caller (ie. CMA
 * glue).
 */
struct cma_chunk {
	dma_addr_t start;
	size_t size;

	struct cma_region *reg;
	struct rb_node by_start;
};


/**
 * struct cma_allocator - a CMA allocator.
 * @name:	Allocator's unique name
 * @init:	Initialises an allocator on given region.
 * @cleanup:	Cleans up after init.  May assume that there are no chunks
 *		allocated in given region.
 * @alloc:	Allocates a chunk of memory of given size in bytes and
 *		with given alignment.  Alignment is a power of
 *		two (thus non-zero) and callback does not need to check it.
 *		May also assume that it is the only call that uses given
 *		region (ie. access to the region is synchronised with
 *		a mutex).  This has to allocate the chunk object (it may be
 *		contained in a bigger structure with allocator-specific data.
 *		Required.
 * @free:	Frees allocated chunk.  May also assume that it is the only
 *		call that uses given region.  This has to free() the chunk
 *		object as well.  Required.
 * @list:	Entry in list of allocators.  Private.
 */
struct cma_allocator {
	const char *name;

	int (*init)(struct cma_region *reg);
	void (*cleanup)(struct cma_region *reg);
	struct cma_chunk *(*alloc)(struct cma_region *reg, size_t size,
				   dma_addr_t alignment);
	void (*free)(struct cma_chunk *chunk);

	struct list_head list;
};


/**
 * cma_allocator_register() - Registers an allocator.
 * @alloc:	Allocator to register.
 *
 * Adds allocator to the list of allocators managed by CMA.
 *
 * All of the fields of cma_allocator structure must be set except for
 * the optional name and the list's head which will be overriden
 * anyway.
 *
 * Returns zero or negative error code.
 */
int cma_allocator_register(struct cma_allocator *alloc);


/**************************** Initialisation API ****************************/

/**
 * cma_set_defaults() - specifies default command line parameters.
 * @regions:	A zero-sized entry terminated list of early regions.
 *		This array must not be placed in __initdata section.
 * @map:	Map attribute.
 *
 * This function should be called prior to cma_early_regions_reserve()
 * and after early parameters have been parsed.
 *
 * Returns zero or negative error.
 */
int __init cma_set_defaults(struct cma_region *regions, const char *map);


/**
 * cma_early_regions - a list of early regions.
 *
 * Platform needs to allocate space for each of the region before
 * initcalls are executed.  If space is reserved, the reserved flag
 * must be set.  Platform initialisation code may choose to use
 * cma_early_regions_allocate().
 *
 * Later, during CMA initialisation all reserved regions from the
 * cma_early_regions list are registered as normal regions and can be
 * used using standard mechanisms.
 */
extern struct list_head cma_early_regions __initdata;


/**
 * cma_early_region_register() - registers an early region.
 * @reg:	Region to add.
 *
 * Region's size, start and alignment must be set (however the last
 * two can be zero).  If name is set the region will be accessible
 * using normal mechanism like mapping or cma_alloc_from() function
 * otherwise it will be a private region accessible only using the
 * cma_alloc_from_region().
 *
 * During platform initialisation, space is reserved for early
 * regions.  Later, when CMA initialises, the early regions are
 * "converted" into normal regions.  If cma_region::alloc is set, CMA
 * will then try to setup given allocator on the region.  Failure to
 * do so will result in the region not being registered even though
 * the space for it will still be reserved.  If cma_region::alloc is
 * not set, allocator will be attached to the region on first use and
 * the value of cma_region::alloc_name will be taken into account if
 * set.
 *
 * All other fields are ignored and/or overwritten.
 *
 * Returns zero or negative error.  No checking if regions overlap is
 * performed.
 */
int __init __must_check cma_early_region_register(struct cma_region *reg);


/**
 * cma_early_region_reserve() - reserves a physically contiguous memory region.
 * @reg:	Early region to reserve memory for.
 *
 * If platform supports bootmem this is the first allocator this
 * function tries to use.  If that failes (or bootmem is not
 * supported) function tries to use memblec if it is available.
 *
 * On success sets reg->reserved flag.
 *
 * Returns zero or negative error.
 */
int __init cma_early_region_reserve(struct cma_region *reg);

/**
 * cma_early_regions_reserve() - helper function for reserving early regions.
 * @reserve:	Callbac function used to reserve space for region.  Needs
 *		to return non-negative if allocation succeeded, negative
 *		error otherwise.  NULL means cma_early_region_alloc() will
 *		be used.
 *
 * This function traverses the %cma_early_regions list and tries to
 * reserve memory for each early region.  It uses the @reserve
 * callback function for that purpose.  The reserved flag of each
 * region is updated accordingly.
 */
void __init cma_early_regions_reserve(int (*reserve)(struct cma_region *reg));

#else

#define cma_set_defaults(regions, map)     ((int)0)
#define cma_early_region_reserve(region)   ((int)-EOPNOTSUPP)
#define cma_early_regions_reserve(reserve) do { } while (0)

#endif

#endif
