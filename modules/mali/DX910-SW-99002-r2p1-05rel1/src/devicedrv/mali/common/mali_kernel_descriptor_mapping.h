/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_kernel_descriptor_mapping.h
 */

#ifndef __MALI_KERNEL_DESCRIPTOR_MAPPING_H__
#define __MALI_KERNEL_DESCRIPTOR_MAPPING_H__

#include "mali_osk.h"

/**
 * The actual descriptor mapping table, never directly accessed by clients
 */
typedef struct mali_descriptor_table
{
	u32 * usage; /**< Pointer to bitpattern indicating if a descriptor is valid/used or not */
	void** mappings; /**< Array of the pointers the descriptors map to */
} mali_descriptor_table;

/**
 * The descriptor mapping object
 * Provides a separate namespace where we can map an integer to a pointer
 */
typedef struct mali_descriptor_mapping
{
    _mali_osk_lock_t *lock; /**< Lock protecting access to the mapping object */
	int max_nr_mappings_allowed; /**< Max number of mappings to support in this namespace */
	int current_nr_mappings; /**< Current number of possible mappings */
	mali_descriptor_table * table; /**< Pointer to the current mapping table */
} mali_descriptor_mapping;

/**
 * Create a descriptor mapping object
 * Create a descriptor mapping capable of holding init_entries growable to max_entries
 * @param init_entries Number of entries to preallocate memory for
 * @param max_entries Number of entries to max support
 * @return Pointer to a descriptor mapping object, NULL on failure
 */
mali_descriptor_mapping * mali_descriptor_mapping_create(int init_entries, int max_entries);

/**
 * Destroy a descriptor mapping object
 * @param map The map to free
 */
void mali_descriptor_mapping_destroy(mali_descriptor_mapping * map);

/**
 * Allocate a new mapping entry (descriptor ID)
 * Allocates a new entry in the map.
 * @param map The map to allocate a new entry in
 * @param target The value to map to
 * @return The descriptor allocated, a negative value on error
 */
_mali_osk_errcode_t mali_descriptor_mapping_allocate_mapping(mali_descriptor_mapping * map, void * target, int *descriptor);

/**
 * Get the value mapped to by a descriptor ID
 * @param map The map to lookup the descriptor id in
 * @param descriptor The descriptor ID to lookup
 * @param target Pointer to a pointer which will receive the stored value
 * @return 0 on successful lookup, negative on error
 */
_mali_osk_errcode_t mali_descriptor_mapping_get(mali_descriptor_mapping * map, int descriptor, void** target);

/**
 * Set the value mapped to by a descriptor ID
 * @param map The map to lookup the descriptor id in
 * @param descriptor The descriptor ID to lookup
 * @param target Pointer to replace the current value with
 * @return 0 on successful lookup, negative on error
 */
_mali_osk_errcode_t mali_descriptor_mapping_set(mali_descriptor_mapping * map, int descriptor, void * target);

/**
 * Call the specified callback function for each descriptor in map.
 * Entire function is mutex protected.
 * @param map The map to do callbacks for
 * @param callback A callback function which will be calle for each entry in map
 */
void mali_descriptor_mapping_call_for_each(mali_descriptor_mapping * map, void (*callback)(int, void*));

/**
 * Free the descriptor ID
 * For the descriptor to be reused it has to be freed
 * @param map The map to free the descriptor from
 * @param descriptor The descriptor ID to free
 */
void mali_descriptor_mapping_free(mali_descriptor_mapping * map, int descriptor);

#endif /* __MALI_KERNEL_DESCRIPTOR_MAPPING_H__ */
