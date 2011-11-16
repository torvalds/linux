/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#include "mali_kernel_memory_engine.h"
#include "mali_block_allocator.h"
#include "mali_osk.h"

#define MALI_BLOCK_SIZE (256UL * 1024UL)  /* 256 kB, remember to keep the ()s */

typedef struct block_info
{
	struct block_info * next;
} block_info;

/* The structure used as the handle produced by block_allocator_allocate,
 * and removed by block_allocator_release */
typedef struct block_allocator_allocation
{
	/* The list will be released in reverse order */
	block_info *last_allocated;
	mali_allocation_engine * engine;
	mali_memory_allocation * descriptor;
	u32 start_offset;
	u32 mapping_length;
} block_allocator_allocation;


typedef struct block_allocator
{
    _mali_osk_lock_t *mutex;
	block_info * all_blocks;
	block_info * first_free;
	u32 base;
	u32 cpu_usage_adjust;
	u32 num_blocks;
} block_allocator;

MALI_STATIC_INLINE u32 get_phys(block_allocator * info, block_info * block);
static mali_physical_memory_allocation_result block_allocator_allocate(void* ctx, mali_allocation_engine * engine,  mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info);
static void block_allocator_release(void * ctx, void * handle);
static mali_physical_memory_allocation_result block_allocator_allocate_page_table_block(void * ctx, mali_page_table_block * block);
static void block_allocator_release_page_table_block( mali_page_table_block *page_table_block );
static void block_allocator_destroy(mali_physical_memory_allocator * allocator);

mali_physical_memory_allocator * mali_block_allocator_create(u32 base_address, u32 cpu_usage_adjust, u32 size, const char *name)
{
	mali_physical_memory_allocator * allocator;
	block_allocator * info;
	u32 usable_size;
	u32 num_blocks;

	usable_size = size & ~(MALI_BLOCK_SIZE - 1);
	MALI_DEBUG_PRINT(3, ("Mali block allocator create for region starting at 0x%08X length 0x%08X\n", base_address, size));
	MALI_DEBUG_PRINT(4, ("%d usable bytes\n", usable_size));
	num_blocks = usable_size / MALI_BLOCK_SIZE;
	MALI_DEBUG_PRINT(4, ("which becomes %d blocks\n", num_blocks));

	if (usable_size == 0)
	{
		MALI_DEBUG_PRINT(1, ("Memory block of size %d is unusable\n", size));
		return NULL;
	}

	allocator = _mali_osk_malloc(sizeof(mali_physical_memory_allocator));
	if (NULL != allocator)
	{
		info = _mali_osk_malloc(sizeof(block_allocator));
		if (NULL != info)
		{
            info->mutex = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_ORDERED, 0, 105);
            if (NULL != info->mutex)
            {
        		info->all_blocks = _mali_osk_malloc(sizeof(block_info) * num_blocks);
			    if (NULL != info->all_blocks)
			    {
				    u32 i;
				    info->first_free = NULL;
				    info->num_blocks = num_blocks;

				    info->base = base_address;
				    info->cpu_usage_adjust = cpu_usage_adjust;

				    for ( i = 0; i < num_blocks; i++)
				    {
					    info->all_blocks[i].next = info->first_free;
					    info->first_free = &info->all_blocks[i];
				    }

				    allocator->allocate = block_allocator_allocate;
				    allocator->allocate_page_table_block = block_allocator_allocate_page_table_block;
				    allocator->destroy = block_allocator_destroy;
				    allocator->ctx = info;
					allocator->name = name;

				    return allocator;
			    }
                _mali_osk_lock_term(info->mutex);
            }
			_mali_osk_free(info);
		}
		_mali_osk_free(allocator);
	}

	return NULL;
}

static void block_allocator_destroy(mali_physical_memory_allocator * allocator)
{
	block_allocator * info;
	MALI_DEBUG_ASSERT_POINTER(allocator);
	MALI_DEBUG_ASSERT_POINTER(allocator->ctx);
	info = (block_allocator*)allocator->ctx;

	_mali_osk_free(info->all_blocks);
    _mali_osk_lock_term(info->mutex);
	_mali_osk_free(info);
	_mali_osk_free(allocator);
}

MALI_STATIC_INLINE u32 get_phys(block_allocator * info, block_info * block)
{
	return info->base + ((block - info->all_blocks) * MALI_BLOCK_SIZE);
}

static mali_physical_memory_allocation_result block_allocator_allocate(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info)
{
	block_allocator * info;
	u32 left;
	block_info * last_allocated = NULL;
	mali_physical_memory_allocation_result result = MALI_MEM_ALLOC_NONE;
	block_allocator_allocation *ret_allocation;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT_POINTER(offset);
	MALI_DEBUG_ASSERT_POINTER(alloc_info);

	info = (block_allocator*)ctx;
	left = descriptor->size - *offset;
	MALI_DEBUG_ASSERT(0 != left);

	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW)) return MALI_MEM_ALLOC_INTERNAL_FAILURE;

	ret_allocation = _mali_osk_malloc( sizeof(block_allocator_allocation) );

	if ( NULL == ret_allocation )
	{
		/* Failure; try another allocator by returning MALI_MEM_ALLOC_NONE */
		_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);
		return result;
	}

	ret_allocation->start_offset = *offset;
	ret_allocation->mapping_length = 0;

	while ((left > 0) && (info->first_free))
	{
		block_info * block;
		u32 phys_addr;
		u32 padding;
		u32 current_mapping_size;

		block = info->first_free;
		info->first_free = info->first_free->next;
		block->next = last_allocated;
		last_allocated = block;

		phys_addr = get_phys(info, block);

		padding = *offset & (MALI_BLOCK_SIZE-1);

 		if (MALI_BLOCK_SIZE - padding < left)
		{
			current_mapping_size = MALI_BLOCK_SIZE - padding;
		}
		else
		{
			current_mapping_size = left;
		}

		if (_MALI_OSK_ERR_OK != mali_allocation_engine_map_physical(engine, descriptor, *offset, phys_addr + padding, info->cpu_usage_adjust, current_mapping_size))
		{
			MALI_DEBUG_PRINT(1, ("Mapping of physical memory  failed\n"));
			result = MALI_MEM_ALLOC_INTERNAL_FAILURE;
			mali_allocation_engine_unmap_physical(engine, descriptor, ret_allocation->start_offset, ret_allocation->mapping_length, (_mali_osk_mem_mapregion_flags_t)0);

			/* release all memory back to the pool */
			while (last_allocated)
			{
				/* This relinks every block we've just allocated back into the free-list */
				block = last_allocated->next;
				last_allocated->next = info->first_free;
				info->first_free = last_allocated;
				last_allocated = block;
			}

			break;
		}

		*offset += current_mapping_size;
		left -= current_mapping_size;
		ret_allocation->mapping_length += current_mapping_size;
	}

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);

	if (last_allocated)
	{
		if (left) result = MALI_MEM_ALLOC_PARTIAL;
		else result = MALI_MEM_ALLOC_FINISHED;

		/* Record all the information about this allocation */
		ret_allocation->last_allocated = last_allocated;
		ret_allocation->engine = engine;
		ret_allocation->descriptor = descriptor;

		alloc_info->ctx = info;
		alloc_info->handle = ret_allocation;
		alloc_info->release = block_allocator_release;
	}
	else
	{
		/* Free the allocation information - nothing to be passed back */
		_mali_osk_free( ret_allocation );
	}

	return result;
}

static void block_allocator_release(void * ctx, void * handle)
{
	block_allocator * info;
	block_info * block, * next;
	block_allocator_allocation *allocation;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(handle);

	info = (block_allocator*)ctx;
	allocation = (block_allocator_allocation*)handle;
	block = allocation->last_allocated;

	MALI_DEBUG_ASSERT_POINTER(block);

	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW))
	{
		MALI_DEBUG_PRINT(1, ("allocator release: Failed to get mutex\n"));
		return;
	}

	/* unmap */
	mali_allocation_engine_unmap_physical(allocation->engine, allocation->descriptor, allocation->start_offset, allocation->mapping_length, (_mali_osk_mem_mapregion_flags_t)0);

	while (block)
	{
		MALI_DEBUG_ASSERT(!((block < info->all_blocks) || (block > (info->all_blocks + info->num_blocks))));

		next = block->next;

		/* relink into free-list */
		block->next = info->first_free;
		info->first_free = block;

		/* advance the loop */
		block = next;
	}

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);

	_mali_osk_free( allocation );}


static mali_physical_memory_allocation_result block_allocator_allocate_page_table_block(void * ctx, mali_page_table_block * block)
{
	block_allocator * info;
	mali_physical_memory_allocation_result result = MALI_MEM_ALLOC_INTERNAL_FAILURE;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(block);
	info = (block_allocator*)ctx;

	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW)) return MALI_MEM_ALLOC_INTERNAL_FAILURE;

	if (NULL != info->first_free)
	{
		void * virt;
		u32 phys;
		u32 size;
		block_info * alloc;
		alloc = info->first_free;

		phys = get_phys(info, alloc); /* Does not modify info or alloc */
		size = MALI_BLOCK_SIZE; /* Must be multiple of MALI_MMU_PAGE_SIZE */
		virt = _mali_osk_mem_mapioregion( phys, size, "Mali block allocator page tables" );

		/* Failure of _mali_osk_mem_mapioregion will result in MALI_MEM_ALLOC_INTERNAL_FAILURE,
		 * because it's unlikely another allocator will be able to map in. */

		if ( NULL != virt )
		{
			block->ctx = info; /* same as incoming ctx */
			block->handle = alloc;
			block->phys_base = phys;
			block->size = size;
			block->release = block_allocator_release_page_table_block;
			block->mapping = virt;

			info->first_free = alloc->next;

			alloc->next = NULL; /* Could potentially link many blocks together instead */

			result = MALI_MEM_ALLOC_FINISHED;
		}
	}
	else result = MALI_MEM_ALLOC_NONE;

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);

	return result;
}


static void block_allocator_release_page_table_block( mali_page_table_block *page_table_block )
{
	block_allocator * info;
	block_info * block, * next;

	MALI_DEBUG_ASSERT_POINTER( page_table_block );

	info = (block_allocator*)page_table_block->ctx;
	block = (block_info*)page_table_block->handle;

	MALI_DEBUG_ASSERT_POINTER(info);
	MALI_DEBUG_ASSERT_POINTER(block);


	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW))
	{
		MALI_DEBUG_PRINT(1, ("allocator release: Failed to get mutex\n"));
		return;
	}

	/* Unmap all the physical memory at once */
	_mali_osk_mem_unmapioregion( page_table_block->phys_base, page_table_block->size, page_table_block->mapping );

	/** @note This loop handles the case where more than one block_info was linked.
	 * Probably unnecssary for page table block releasing. */
	while (block)
	{
		next = block->next;

		MALI_DEBUG_ASSERT(!((block < info->all_blocks) || (block > (info->all_blocks + info->num_blocks))));

		block->next = info->first_free;
		info->first_free = block;

		block = next;
	}

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);
}

