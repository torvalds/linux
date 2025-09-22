/*
 * region-allocator.h -- region based memory allocator.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef REGION_ALLOCATOR_H
#define REGION_ALLOCATOR_H

#include <stdio.h>

typedef struct region region_type;

#define DEFAULT_CHUNK_SIZE         4096
#define DEFAULT_LARGE_OBJECT_SIZE  (DEFAULT_CHUNK_SIZE / 8)
#define DEFAULT_INITIAL_CLEANUP_SIZE 16


/*
 * mmap allocator constants
 *
 */
#ifdef USE_MMAP_ALLOC

/* header starts with size_t containing allocated size info and has at least 16 bytes to align the returned memory */
#define MMAP_ALLOC_HEADER_SIZE (sizeof(size_t) >= 16 ? (sizeof(size_t)) : 16)

/* mmap allocator uses chunks of 32 4kB pages */
#define MMAP_ALLOC_CHUNK_SIZE		((32 * 4096) - MMAP_ALLOC_HEADER_SIZE)
#define MMAP_ALLOC_LARGE_OBJECT_SIZE	(MMAP_ALLOC_CHUNK_SIZE / 8)
#define MMAP_ALLOC_INITIAL_CLEANUP_SIZE	16

#endif /* USE_MMAP_ALLOC */

/*
 * Create a new region.
 */
region_type *region_create(void *(*allocator)(size_t),
			   void (*deallocator)(void *));


/*
 * Create a new region, with chunk size and large object size.
 * Note that large_object_size must be <= chunk_size.
 * Anything larger than the large object size is individually alloced.
 * large_object_size = chunk_size/8 is reasonable;
 * initial_cleanup_size is the number of preallocated ptrs for cleanups.
 * The cleanups are in a growing array, and it must start larger than zero.
 * If recycle is true, environmentally friendly memory recycling is be enabled.
 */
region_type *region_create_custom(void *(*allocator)(size_t),
				  void (*deallocator)(void *),
				  size_t chunk_size,
				  size_t large_object_size,
				  size_t initial_cleanup_size,
				  int recycle);


/*
 * Destroy REGION.  All memory associated with REGION is freed as if
 * region_free_all was called.
 */
void region_destroy(region_type *region);


/*
 * Add a cleanup to REGION.  ACTION will be called with DATA as
 * parameter when the region is freed or destroyed.
 *
 * Returns 0 on failure.
 */
size_t region_add_cleanup(region_type *region,
			  void (*action)(void *),
			  void *data);

/* 
 * Remove cleanup, both action and data must match exactly.
 */
void region_remove_cleanup(region_type *region,
        void (*action)(void *), void *data);

/*
 * Allocate SIZE bytes of memory inside REGION.  The memory is
 * deallocated when region_free_all is called for this region.
 */
void *region_alloc(region_type *region, size_t size);

/** Allocate array with integer overflow checks, in region */
void *region_alloc_array(region_type *region, size_t num, size_t size);

/*
 * Allocate SIZE bytes of memory inside REGION and copy INIT into it.
 * The memory is deallocated when region_free_all is called for this
 * region.
 */
void *region_alloc_init(region_type *region, const void *init, size_t size);

/** 
 * Allocate array (with integer overflow check on sizes), and init with
 * the given array copied into it.  Allocated in the region
 */
void *region_alloc_array_init(region_type *region, const void *init,
	size_t num, size_t size);

/*
 * Allocate SIZE bytes of memory inside REGION that are initialized to
 * 0.  The memory is deallocated when region_free_all is called for
 * this region.
 */
void *region_alloc_zero(region_type *region, size_t size);

/** 
 * Allocate array (with integer overflow check on sizes), and zero it.
 * Allocated in the region.
 */
void *region_alloc_array_zero(region_type *region, size_t num, size_t size);

/*
 * Run the cleanup actions and free all memory associated with REGION.
 */
void region_free_all(region_type *region);


/*
 * Duplicate STRING and allocate the result in REGION.
 */
char *region_strdup(region_type *region, const char *string);

/*
 * Replace a string on the to_replace location, if string is different
 */
void region_str_replace(region_type* region, char **to_replace,
		const char *string);
/*
 * Recycle an allocated memory block. Pass size used to alloc it.
 * Does nothing if recycling is not enabled for the region.
 */
void region_recycle(region_type *region, void *block, size_t size);

/*
 * Print some REGION statistics to OUT.
 */
void region_dump_stats(region_type *region, FILE *out);

/* get size of recyclebin */
size_t region_get_recycle_size(region_type* region);
/* get size of region memory in use */
size_t region_get_mem(region_type* region);
/* get size of region memory unused */
size_t region_get_mem_unused(region_type* region);

/* Debug print REGION statistics to LOG. */
void region_log_stats(region_type *region);

#endif /* REGION_ALLOCATOR_H */
