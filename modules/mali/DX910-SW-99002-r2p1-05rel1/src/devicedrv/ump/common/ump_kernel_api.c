/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "ump_osk.h"
#include "ump_uk_types.h"
#include "ump_kernel_interface.h"
#include "ump_kernel_common.h"



/* ---------------- UMP kernel space API functions follows ---------------- */



UMP_KERNEL_API_EXPORT ump_secure_id ump_dd_secure_id_get(ump_dd_handle memh)
{
	ump_dd_mem * mem = (ump_dd_mem *)memh;

	DEBUG_ASSERT_POINTER(mem);

	DBG_MSG(5, ("Returning secure ID. ID: %u\n", mem->secure_id));

	return mem->secure_id;
}



UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_secure_id(ump_secure_id secure_id)
{
	ump_dd_mem * mem;

	_mali_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	DBG_MSG(5, ("Getting handle from secure ID. ID: %u\n", secure_id));
	if (0 != ump_descriptor_mapping_get(device.secure_id_map, (int)secure_id, (void**)&mem))
	{
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		DBG_MSG(1, ("Secure ID not found. ID: %u\n", secure_id));
		return UMP_DD_HANDLE_INVALID;
	}

	ump_dd_reference_add(mem);

	_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	return (ump_dd_handle)mem;
}



UMP_KERNEL_API_EXPORT unsigned long ump_dd_phys_block_count_get(ump_dd_handle memh)
{
	ump_dd_mem * mem = (ump_dd_mem*) memh;

	DEBUG_ASSERT_POINTER(mem);

	return mem->nr_blocks;
}



UMP_KERNEL_API_EXPORT ump_dd_status_code ump_dd_phys_blocks_get(ump_dd_handle memh, ump_dd_physical_block * blocks, unsigned long num_blocks)
{
	ump_dd_mem * mem = (ump_dd_mem *)memh;

	DEBUG_ASSERT_POINTER(mem);

	if (blocks == NULL)
	{
		DBG_MSG(1, ("NULL parameter in ump_dd_phys_blocks_get()\n"));
		return UMP_DD_INVALID;
	}

	if (mem->nr_blocks != num_blocks)
	{
		DBG_MSG(1, ("Specified number of blocks do not match actual number of blocks\n"));
		return UMP_DD_INVALID;
	}

	DBG_MSG(5, ("Returning physical block information. ID: %u\n", mem->secure_id));

	_mali_osk_memcpy(blocks, mem->block_array, sizeof(ump_dd_physical_block) * mem->nr_blocks);

	return UMP_DD_SUCCESS;
}



UMP_KERNEL_API_EXPORT ump_dd_status_code ump_dd_phys_block_get(ump_dd_handle memh, unsigned long index, ump_dd_physical_block * block)
{
	ump_dd_mem * mem = (ump_dd_mem *)memh;

	DEBUG_ASSERT_POINTER(mem);

	if (block == NULL)
	{
		DBG_MSG(1, ("NULL parameter in ump_dd_phys_block_get()\n"));
		return UMP_DD_INVALID;
	}

	if (index >= mem->nr_blocks)
	{
		DBG_MSG(5, ("Invalid index specified in ump_dd_phys_block_get()\n"));
		return UMP_DD_INVALID;
	}

	DBG_MSG(5, ("Returning physical block information. ID: %u, index: %lu\n", mem->secure_id, index));

	*block = mem->block_array[index];

	return UMP_DD_SUCCESS;
}



UMP_KERNEL_API_EXPORT unsigned long ump_dd_size_get(ump_dd_handle memh)
{
	ump_dd_mem * mem = (ump_dd_mem*)memh;

	DEBUG_ASSERT_POINTER(mem);

	DBG_MSG(5, ("Returning size. ID: %u, size: %lu\n", mem->secure_id, mem->size_bytes));

	return mem->size_bytes;
}



UMP_KERNEL_API_EXPORT void ump_dd_reference_add(ump_dd_handle memh)
{
	ump_dd_mem * mem = (ump_dd_mem*)memh;
	int new_ref;

	DEBUG_ASSERT_POINTER(mem);

	new_ref = _ump_osk_atomic_inc_and_read(&mem->ref_count);

	DBG_MSG(4, ("Memory reference incremented. ID: %u, new value: %d\n", mem->secure_id, new_ref));
}



UMP_KERNEL_API_EXPORT void ump_dd_reference_release(ump_dd_handle memh)
{
	int new_ref;
	ump_dd_mem * mem = (ump_dd_mem*)memh;

	DEBUG_ASSERT_POINTER(mem);

	/* We must hold this mutex while doing the atomic_dec_and_read, to protect
	that elements in the ump_descriptor_mapping table is always valid.  If they
	are not, userspace may accidently map in this secure_ids right before its freed
	giving a mapped backdoor into unallocated memory.*/
	_mali_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	new_ref = _ump_osk_atomic_dec_and_read(&mem->ref_count);

	DBG_MSG(4, ("Memory reference decremented. ID: %u, new value: %d\n", mem->secure_id, new_ref));

	if (0 == new_ref)
	{
		DBG_MSG(3, ("Final release of memory. ID: %u\n", mem->secure_id));

		ump_descriptor_mapping_free(device.secure_id_map, (int)mem->secure_id);

		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		mem->release_func(mem->ctx, mem);
		_mali_osk_free(mem);
	}
	else
	{
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	}
}



/* --------------- Handling of user space requests follows --------------- */


_mali_osk_errcode_t _ump_uku_get_api_version( _ump_uk_api_version_s *args )
{
	ump_session_data * session_data;

	DEBUG_ASSERT_POINTER( args );
	DEBUG_ASSERT_POINTER( args->ctx );

	session_data = (ump_session_data *)args->ctx;

	/* check compatability */
	if (args->version == UMP_IOCTL_API_VERSION)
	{
		DBG_MSG(3, ("API version set to newest %d (compatible)\n", GET_VERSION(args->version)));
		args->compatible = 1;
		session_data->api_version = args->version;
	}
	else if (args->version == MAKE_VERSION_ID(1))
	{
		DBG_MSG(2, ("API version set to depricated: %d (compatible)\n", GET_VERSION(args->version)));
		args->compatible = 1;
		session_data->api_version = args->version;
	}
	else
	{
		DBG_MSG(2, ("API version set to %d (incompatible with client version %d)\n", GET_VERSION(UMP_IOCTL_API_VERSION), GET_VERSION(args->version)));
		args->compatible = 0;
		args->version = UMP_IOCTL_API_VERSION; /* report our version */
	}

	return _MALI_OSK_ERR_OK;
}


_mali_osk_errcode_t _ump_ukk_release( _ump_uk_release_s *release_info )
{
	ump_session_memory_list_element * session_memory_element;
	ump_session_memory_list_element * tmp;
	ump_session_data * session_data;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_INVALID_FUNC;
	int secure_id;

	DEBUG_ASSERT_POINTER( release_info );
	DEBUG_ASSERT_POINTER( release_info->ctx );

	/* Retreive the session data */
	session_data = (ump_session_data*)release_info->ctx;

	/* If there are many items in the memory session list we
	 * could be de-referencing this pointer a lot so keep a local copy
	 */
	secure_id = release_info->secure_id;

	DBG_MSG(4, ("Releasing memory with IOCTL, ID: %u\n", secure_id));

	/* Iterate through the memory list looking for the requested secure ID */
	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_MALI_OSK_LIST_FOREACHENTRY(session_memory_element, tmp, &session_data->list_head_session_memory_list, ump_session_memory_list_element, list)
	{
		if ( session_memory_element->mem->secure_id == secure_id)
		{
			ump_dd_mem *release_mem;

			release_mem = session_memory_element->mem;
			_mali_osk_list_del(&session_memory_element->list);
			ump_dd_reference_release(release_mem);
			_mali_osk_free(session_memory_element);

			ret = _MALI_OSK_ERR_OK;
			break;
		}
	}

	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
 	DBG_MSG_IF(1, _MALI_OSK_ERR_OK != ret, ("UMP memory with ID %u does not belong to this session.\n", secure_id));

	DBG_MSG(4, ("_ump_ukk_release() returning 0x%x\n", ret));
	return ret;
}

_mali_osk_errcode_t _ump_ukk_size_get( _ump_uk_size_get_s *user_interaction )
{
	ump_dd_mem * mem;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;

	DEBUG_ASSERT_POINTER( user_interaction );

	/* We lock the mappings so things don't get removed while we are looking for the memory */
	_mali_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	if (0 == ump_descriptor_mapping_get(device.secure_id_map, (int)user_interaction->secure_id, (void**)&mem))
	{
		user_interaction->size = mem->size_bytes;
		DBG_MSG(4, ("Returning size. ID: %u, size: %lu ", (ump_secure_id)user_interaction->secure_id, (unsigned long)user_interaction->size));
		ret = _MALI_OSK_ERR_OK;
	}
	else
	{
		 user_interaction->size = 0;
		DBG_MSG(1, ("Failed to look up mapping in ump_ioctl_size_get(). ID: %u\n", (ump_secure_id)user_interaction->secure_id));
	}

	_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	return ret;
}



void _ump_ukk_msync( _ump_uk_msync_s *args )
{
	ump_dd_mem * mem = NULL;
	void *virtual = NULL;
	u32 size = 0;
	u32 offset = 0;
	_mali_osk_lock_wait(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
	ump_descriptor_mapping_get(device.secure_id_map, (int)args->secure_id, (void**)&mem);

	if (NULL == mem)
	{
		_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);
		DBG_MSG(1, ("Failed to look up mapping in _ump_ukk_msync(). ID: %u\n", (ump_secure_id)args->secure_id));
		return;
	}
	/* Ensure the memory doesn't dissapear when we are flushing it. */
	ump_dd_reference_add(mem);
	_mali_osk_lock_signal(device.secure_id_map_lock, _MALI_OSK_LOCKMODE_RW);

	/* Returns the cache settings back to Userspace */
	args->is_cached=mem->is_cached;

	/* If this flag is the only one set, we should not do the actual flush, only the readout */
	if ( _UMP_UK_MSYNC_READOUT_CACHE_ENABLED==args->op )
	{
		DBG_MSG(3, ("_ump_ukk_msync READOUT  ID: %u Enabled: %d\n", (ump_secure_id)args->secure_id, mem->is_cached));
		goto msync_release_and_return;
	}

	/* Nothing to do if the memory is not caches */
	if ( 0==mem->is_cached )
	{
		DBG_MSG(3, ("_ump_ukk_msync IGNORING ID: %u Enabled: %d  OP: %d\n", (ump_secure_id)args->secure_id, mem->is_cached, args->op));
		goto msync_release_and_return;
	}
	DBG_MSG(3, ("_ump_ukk_msync FLUSHING ID: %u Enabled: %d  OP: %d Address: 0x%08x Mapping: 0x%08x\n",
	            (ump_secure_id)args->secure_id, mem->is_cached, args->op, args->address, args->mapping));

	if ( args->address )
	{
		virtual = ((u32)args->address);
		offset = (u32)((args->address) - (args->mapping));
	} else {
		/* Flush entire mapping when no address is specified. */
		virtual = args->mapping;
	}
	if ( args->size )
	{
		size = args->size;
	} else {
		/* Flush entire mapping when no size is specified. */
		size = mem->size_bytes - offset;
	}

	if ( (offset + size) > mem->size_bytes )
	{
		DBG_MSG(1, ("Trying to flush more than the entire UMP allocation: offset: %u + size: %u > %u\n", offset, size, mem->size_bytes));
		goto msync_release_and_return;
	}

	/* The actual cache flush - Implemented for each OS*/
	_ump_osk_msync( mem, virtual, offset, size, args->op);

msync_release_and_return:
	ump_dd_reference_release(mem);
	return;
}
