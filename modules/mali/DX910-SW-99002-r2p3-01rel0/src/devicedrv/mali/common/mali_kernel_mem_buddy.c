/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_core.h"
#include "mali_kernel_subsystem.h"
#include "mali_kernel_mem.h"
#include "mali_kernel_descriptor_mapping.h"
#include "mali_kernel_session_manager.h"

/* kernel side OS functions and user-kernel interface */
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_osk_list.h"
#include "mali_ukk.h"

#ifdef _MALI_OSK_SPECIFIC_INDIRECT_MMAP
#include "mali_osk_indir_mmap.h"
#endif

#error Support for non-MMU builds is no longer supported and is planned for removal.

/**
 * Minimum memory allocation size
 */
#define MIN_BLOCK_SIZE (1024*1024UL)

/**
 * Per-session memory descriptor mapping table sizes
 */
#define MALI_MEM_DESCRIPTORS_INIT 64
#define MALI_MEM_DESCRIPTORS_MAX 4096

/**
 * Enum uses to store multiple fields in one u32 to keep the memory block struct small
 */
enum MISC_SHIFT { MISC_SHIFT_FREE = 0, MISC_SHIFT_ORDER = 1, MISC_SHIFT_TOPLEVEL = 6 };
enum MISC_MASK { MISC_MASK_FREE = 0x01, MISC_MASK_ORDER = 0x1F, MISC_MASK_TOPLEVEL = 0x1F };

/* forward declaration of the block struct */
struct mali_memory_block;

/**
 * Definition of memory bank type.
 * Represents a memory bank (separate address space)
 * Each bank keeps track of its block usage.
 * A buddy system used to track the usage
*/
typedef struct mali_memory_bank
{
	_mali_osk_list_t list; /* links multiple banks together */
	_mali_osk_lock_t *lock;
	u32 base_addr; /* Mali seen address of bank */
	u32 cpu_usage_adjust; /* Adjustment factor for what the CPU sees */
	u32 size; /* the effective size */
	u32 real_size; /* the real size of the bank, as given by to the subsystem */
	int min_order;
	int max_order;
	struct mali_memory_block * blocklist;
	_mali_osk_list_t *freelist;
	_mali_osk_atomic_t num_active_allocations;
	u32 used_for_flags;
	u32 alloc_order; /**< Order in which the bank will be used for allocations */
	const char *name; /**< Descriptive name of the bank */
} mali_memory_bank;

/**
 * Definition of the memory block type
 * Represents a memory block, which is the smallest memory unit operated on.
 * A block keeps info about its mapping, if in use by a user process
 */
typedef struct mali_memory_block
{
	_mali_osk_list_t link; /* used for freelist and process usage list*/
	mali_memory_bank * bank; /* the bank it belongs to */
	void __user * mapping; /* possible user space mapping of this block */
	u32 misc; /* used while a block is free to track the number blocks it represents */
	int descriptor;
	u32 mmap_cookie; /**< necessary for interaction with _mali_ukk_mem_mmap/munmap */
} mali_memory_block;

/**
 * Defintion of the type used to represent memory used by a session.
 * Containts the head of the list of memory currently in use by a session.
 */
typedef struct memory_session
{
	_mali_osk_lock_t *lock;
	_mali_osk_list_t memory_head; /* List of the memory blocks used by this session. */
	mali_descriptor_mapping * descriptor_mapping; /**< Mapping between userspace descriptors and our pointers */
} memory_session;

/*
	Subsystem interface implementation
*/
/**
 * Buddy block memory subsystem startup function
 * Called by the driver core when the driver is loaded.
 * Registers the memory systems ioctl handler, resource handlers and memory map function with the core.
 *
 * @param id Identifier assigned by the core to the memory subsystem
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_initialize(mali_kernel_subsystem_identifier id);

/**
 * Buddy block memory subsystem shutdown function
 * Called by the driver core when the driver is unloaded.
 * Cleans up
 * @param id Identifier assigned by the core to the memory subsystem
 */
static void mali_memory_core_terminate(mali_kernel_subsystem_identifier id);

/**
 * Buddy block memory load complete notification function.
 * Called by the driver core when all drivers have loaded and all resources has been registered
 * Reports on the memory resources registered
 * @param id Identifier assigned by the core to the memory subsystem
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_load_complete(mali_kernel_subsystem_identifier id);


/**
 * Buddy block memory subsystem session begin notification
 * Called by the core when a new session to the driver is started.
 * Creates a memory session object and sets it as the subsystem slot data for this session
 * @param slot Pointer to the slot to use for storing per-session data
 * @param queue The user space event sink
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue);

/**
 * Buddy block memory subsystem session end notification
 * Called by the core when a session to the driver has ended.
 * Cleans up per session data, which includes checking and fixing memory leaks
 *
 * @param slot Pointer to the slot to use for storing per-session data
 */
static void mali_memory_core_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot);

/**
 * Buddy block memory subsystem system info filler
 * Called by the core when a system info update is needed
 * We fill in info about all the memory types we have
 * @param info Pointer to system info struct to update
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_system_info_fill(_mali_system_info* info);

/* our registered resource handlers */
/**
 * Buddy block memory subsystem's notification handler for MEMORY resource instances.
 * Registered with the core during startup.
 * Called by the core for each memory bank described in the active architecture's config.h file.
 * Requests memory region ownership and calls backend.
 * @param resource The resource to handle (type MEMORY)
 * @return 0 if the memory was claimed and accepted, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_resource_memory(_mali_osk_resource_t * resource);

/**
 * Buddy block memory subsystem's notification handler for MMU resource instances.
 * Registered with the core during startup.
 * Called by the core for each mmu described in the active architecture's config.h file.
 * @param resource The resource to handle (type MMU)
 * @return 0 if the MMU was found and initialized, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_resource_mmu(_mali_osk_resource_t * resource);

/**
 * Buddy block memory subsystem's notification handler for FPGA_FRAMEWORK resource instances.
 * Registered with the core during startup.
 * Called by the core for each fpga framework described in the active architecture's config.h file.
 * @param resource The resource to handle (type FPGA_FRAMEWORK)
 * @return 0 if the FPGA framework was found and initialized, negative on error
 */
static _mali_osk_errcode_t mali_memory_core_resource_fpga(_mali_osk_resource_t * resource);

/* ioctl command implementations */
/**
 * Buddy block memory subsystem's handler for MALI_IOC_MEM_GET_BIG_BLOCK ioctl
 * Called by the generic ioctl handler when the MALI_IOC_MEM_GET_BIG_BLOCK command is received.
 * Finds an available memory block and maps into the current process' address space.
 * @param ukk_private private word for use by the User/Kernel interface
 * @param session_data Pointer to the per-session object which will track the memory usage
 * @param argument The argument from the user. A pointer to an struct mali_dd_get_big_block in user space
 * @return Zero if successful, a standard Linux error value value on error (a negative value)
 */
_mali_osk_errcode_t _mali_ukk_get_big_block( _mali_uk_get_big_block_s *args );

/**
 * Buddy block memory subsystem's handler for MALI_IOC_MEM_FREE_BIG_BLOCK ioctl
 * Called by the generic ioctl handler when the MALI_IOC_MEM_FREE_BIG_BLOCK command is received.
 * Unmaps the memory from the process' address space and marks the block as free.
 * @param session_data Pointer to the per-session object which tracks the memory usage
 * @param argument The argument from the user. A pointer to an struct mali_dd_get_big_block in user space
 * @return Zero if successful, a standard Linux error value value on error (a negative value)
 */

/* this static version allows us to make use of it while holding the memory_session lock.
 * This is required  for the session_end code */
static _mali_osk_errcode_t _mali_ukk_free_big_block_internal( struct mali_session_data * mali_session_data, memory_session * session_data, _mali_uk_free_big_block_s *args);

_mali_osk_errcode_t _mali_ukk_free_big_block( _mali_uk_free_big_block_s *args );

/**
 * Buddy block memory subsystem's memory bank registration routine
 * Called when a MEMORY resource has been found.
 * The memory region has already been reserved for use by this driver.
 * Create a bank object to represent this region and initialize its slots.
 * @note Can only be called in an module atomic scope, i.e. during module init since no locking is performed
 * @param phys_base Physical base address of this bank
 * @param cpu_usage_adjust Adjustment factor for CPU seen address
 * @param size Size of the bank in bytes
 * @param flags Memory type bits
 * @param alloc_order Order in which the bank will be used for allocations
 * @param name descriptive name of the bank
 * @return Zero on success, negative on error
 */
static int mali_memory_bank_register(u32 phys_base, u32 cpu_usage_adjust, u32 size, u32 flags, u32 alloc_order, const char *name);

/**
 * Get a block of mali memory of at least the given size and of the given type
 * This is the backend for get_big_block.
 * @param type_id The type id of memory requested.
 * @param minimum_size The size requested
 * @return Pointer to a block on success, NULL on failure
 */
static mali_memory_block * mali_memory_block_get(u32 type_id, u32 minimum_size);

/**
 * Get the mali seen address of the memory described by the block
 * @param block The memory block to return the address of
 * @return The mali seen address of the memory block
 */
MALI_STATIC_INLINE u32 block_mali_addr_get(mali_memory_block * block);

/**
 * Get the cpu seen address of the memory described by the block
 * The cpu_usage_adjust will be used to change the mali seen phys address
 * @param block The memory block to return the address of
 * @return The mali seen address of the memory block
 */
MALI_STATIC_INLINE u32 block_cpu_addr_get(mali_memory_block * block);

/**
 * Get the size of the memory described by the given block
 * @param block The memory block to return the size of
 * @return The size of the memory block described by the object
 */
MALI_STATIC_INLINE u32 block_size_get(mali_memory_block * block);

/**
 * Get the user space accessible mapping the memory described by the given memory block
 * Returns a pointer in user space to the memory, if one has been created.
 * @param block The memory block to return the mapping of
 * @return User space pointer to cpu accessible memory or NULL if not mapped
 */
MALI_STATIC_INLINE void __user * block_mapping_get(mali_memory_block * block);

/**
 * Set the user space accessible mapping the memory described by the given memory block.
 * Sets the stored pointer to user space for the memory described by this block.
 * @param block The memory block to set mapping info for
 * @param ptr User space pointer to cpu accessible memory or NULL if not mapped
 */
MALI_STATIC_INLINE void block_mapping_set(mali_memory_block * block, void __user * ptr);

/**
 * Get the cookie for use with _mali_ukk_mem_munmap().
 * @param block The memory block to get the cookie from
 * @return the cookie. A return of 0 is still a valid cookie.
 */
MALI_STATIC_INLINE u32 block_mmap_cookie_get(mali_memory_block * block);

/**
 * Set the cookie returned via _mali_ukk_mem_mmap().
 * @param block The memory block to set the cookie for
 * @param cookie the cookie
 */
MALI_STATIC_INLINE void block_mmap_cookie_set(mali_memory_block * block, u32 cookie);


/**
 * Get a memory block's free status
 * @param block The block to get the state of
 */
MALI_STATIC_INLINE u32 get_block_free(mali_memory_block * block);

/**
 * Set a memory block's free status
 * @param block The block to set the state for
 * @param state The state to set
 */
MALI_STATIC_INLINE void set_block_free(mali_memory_block * block, int state);

/**
 * Set a memory block's order
 * @param block The block to set the order for
 * @param order The order to set
 */
MALI_STATIC_INLINE void set_block_order(mali_memory_block * block, u32 order);

/**
 * Get a memory block's order
 * @param block The block to get the order for
 * @return The order this block exists on
 */
MALI_STATIC_INLINE u32 get_block_order(mali_memory_block * block);

/**
 * Tag a block as being a toplevel block.
 * A toplevel block has no buddy and no parent
 * @param block The block to tag as being toplevel
 */
MALI_STATIC_INLINE void set_block_toplevel(mali_memory_block * block, u32 level);

/**
 * Check if a block is a toplevel block
 * @param block The block to check
 * @return 1 if toplevel, 0 else
 */
MALI_STATIC_INLINE u32 get_block_toplevel(mali_memory_block * block);

/**
 * Checks if the given block is a buddy at the given order and that it's free
 * @param block The block to check
 * @param order The order to check against
 * @return 0 if not valid, else 1
 */
MALI_STATIC_INLINE int block_is_valid_buddy(mali_memory_block * block, int order);

/*
 The buddy system uses the following rules to quickly find a blocks buddy
 and parent (block representing this block at a higher order level):
 - Given a block with index i the blocks buddy is at index i ^ ( 1 << order)
 - Given a block with index i the blocks parent is at i & ~(1 << order)
*/

/**
 * Get a blocks buddy
 * @param block The block to find the buddy for
 * @param order The order to operate on
 * @return Pointer to the buddy block
 */
MALI_STATIC_INLINE mali_memory_block * block_get_buddy(mali_memory_block * block, u32 order);

/**
 * Get a blocks parent
 * @param block The block to find the parent for
 * @param order The order to operate on
 * @return Pointer to the parent block
 */
MALI_STATIC_INLINE mali_memory_block * block_get_parent(mali_memory_block * block, u32 order);

/**
 * Release mali memory
 * Backend for free_big_block.
 * Will release the mali memory described by the given block struct.
 * @param block Memory block to free
 */
static void block_release(mali_memory_block * block);

/* end interface implementation */

/**
 * List of all the memory banks registerd with the subsystem.
 * Access to this list is NOT synchronized since it's only
 * written to during module init and termination.
 */
static _MALI_OSK_LIST_HEAD(memory_banks_list);

/*
	The buddy memory system's mali subsystem interface implementation.
	We currently handle module and session life-time management.
*/
struct mali_kernel_subsystem mali_subsystem_memory =
{
	mali_memory_core_initialize,                /* startup */
	mali_memory_core_terminate,                 /* shutdown */
    mali_memory_core_load_complete,             /* load_complete */
	mali_memory_core_system_info_fill,          /* system_info_fill */
    mali_memory_core_session_begin,             /* session_begin */
	mali_memory_core_session_end,               /* session_end */
    NULL,                                       /* broadcast_notification */
#if MALI_STATE_TRACKING
	NULL,                                       /* dump_state */
#endif
};

/* Initialized when this subsystem is initialized. This is determined by the
 * position in subsystems[], and so the value used to initialize this is
 * determined at compile time */
static mali_kernel_subsystem_identifier mali_subsystem_memory_id = -1;

/* called during module init */
static _mali_osk_errcode_t mali_memory_core_initialize(mali_kernel_subsystem_identifier id)
{
    _MALI_OSK_INIT_LIST_HEAD(&memory_banks_list);

	mali_subsystem_memory_id = id;

	/* register our handlers */
	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MEMORY, mali_memory_core_resource_memory));

	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MMU, mali_memory_core_resource_mmu));

	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(FPGA_FRAMEWORK, mali_memory_core_resource_fpga));

	MALI_SUCCESS;
}

/* called if/when our module is unloaded */
static void mali_memory_core_terminate(mali_kernel_subsystem_identifier id)
{
	mali_memory_bank * bank, *temp;

	/* loop over all memory banks to free them */
	/* we use the safe version since we delete the current bank in the body */
	_MALI_OSK_LIST_FOREACHENTRY(bank, temp, &memory_banks_list, mali_memory_bank, list)
	{
		MALI_DEBUG_CODE(int usage_count = _mali_osk_atomic_read(&bank->num_active_allocations));
 		/*
			Report leaked memory
			If this happens we have a bug in our session cleanup code.
		*/
		MALI_DEBUG_PRINT_IF(1, 0 != usage_count,  ("%d allocation(s) from memory bank at 0x%X still in use\n", usage_count, bank->base_addr));

        _mali_osk_atomic_term(&bank->num_active_allocations);

	    _mali_osk_lock_term(bank->lock);

        /* unlink from bank list */
		_mali_osk_list_del(&bank->list);

		/* release kernel resources used by the bank */
		_mali_osk_mem_unreqregion(bank->base_addr, bank->real_size);

		/* remove all resources used to represent this bank*/
		_mali_osk_free(bank->freelist);
		_mali_osk_free(bank->blocklist);

		/* destroy the bank object itself */
		_mali_osk_free(bank);
	}

	/* No need to de-initialize mali_subsystem_memory_id - it could only be
	 * re-initialized to the same value */
}

/* load_complete handler */
static _mali_osk_errcode_t mali_memory_core_load_complete(mali_kernel_subsystem_identifier id)
{
	mali_memory_bank * bank, *temp;

	MALI_DEBUG_PRINT( 1, ("Mali memory allocators will be used in this order of preference (lowest number first) :\n"));

	_MALI_OSK_LIST_FOREACHENTRY(bank, temp, &memory_banks_list, mali_memory_bank, list)
	{
		if ( NULL != bank->name )
		{
			MALI_DEBUG_PRINT( 1, ("\t%d: %s\n", bank->alloc_order, bank->name) );
		}
		else
		{
			MALI_DEBUG_PRINT( 1, ("\t%d: (UNNAMED ALLOCATOR)\n", bank->alloc_order ) );
		}
	}
	MALI_SUCCESS;
}

MALI_STATIC_INLINE u32 order_needed_for_size(u32 size, struct mali_memory_bank * bank)
{
    u32 order = 0;

    if (0 < size)
    {
        for ( order = sizeof(u32)*8 - 1; ((1UL<<order) & size) == 0; --order)
            /* nothing */;

        /* check if size is pow2, if not we need increment order by one */
        if (0 != (size & ((1UL<<order)-1))) ++order;
    }

    if ((NULL != bank) && (order < bank->min_order)) order = bank->min_order;
    /* Not capped to max order, that doesn't make sense */

    return order;
}

MALI_STATIC_INLINE u32 maximum_order_which_fits(u32 size)
{
	u32 order = 0;
	u32 powsize = 1;
	while (powsize < size)
	{
		powsize <<= 1;
		if (powsize > size) break;
		order++;
	}

	return order;
}

/* called for new MEMORY resources */
static _mali_osk_errcode_t mali_memory_bank_register(u32 phys_base, u32 cpu_usage_adjust, u32 size, u32 flags, u32 alloc_order, const char *name)
{
	/* no locking performed due to function contract */
	int i;
	u32 left, offset;
	mali_memory_bank * bank;
	mali_memory_bank * bank_enum, *temp;

    _mali_osk_errcode_t err;

	/* Only a multiple of MIN_BLOCK_SIZE is usable */
	u32 usable_size = size & ~(MIN_BLOCK_SIZE - 1);

	/* handle zero sized banks and bank smaller than the fixed block size */
	if (0 == usable_size)
	{
		MALI_PRINT(("Usable size == 0\n"));
        MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	/* warn for banks not a muliple of the block size  */
	MALI_DEBUG_PRINT_IF(1, usable_size != size, ("Memory bank @ 0x%X not a multiple of minimum block size. %d bytes wasted\n", phys_base, size - usable_size));

	/* check against previous registrations */
	MALI_DEBUG_CODE(
        {
		    _MALI_OSK_LIST_FOREACHENTRY(bank, temp, &memory_banks_list, mali_memory_bank, list)
		    {
			    /* duplicate ? */
			    if (bank->base_addr == phys_base)
			    {
				    MALI_PRINT(("Duplicate registration of a memory bank at 0x%X detected\n", phys_base));
				    MALI_ERROR(_MALI_OSK_ERR_FAULT);
			    }
			    /* overlapping ? */
			    else if (
						    ( (phys_base > bank->base_addr) && (phys_base < (bank->base_addr + bank->real_size)) ) ||
						    ( (phys_base + size) > bank->base_addr && ((phys_base + size) < (bank->base_addr + bank->real_size)) )
					    )
			    {
				    MALI_PRINT(("Overlapping memory blocks found. Memory at 0x%X overlaps with memory at 0x%X size 0x%X\n", bank->base_addr, phys_base, size));
				    MALI_ERROR(_MALI_OSK_ERR_FAULT);
			    }
		    }
        }
	);

	/* create an object to represent this memory bank */
	MALI_CHECK_NON_NULL(bank = (mali_memory_bank*)_mali_osk_malloc(sizeof(mali_memory_bank)), _MALI_OSK_ERR_NOMEM);

	/* init the fields */
	_MALI_OSK_INIT_LIST_HEAD(&bank->list);
	bank->base_addr = phys_base;
	bank->cpu_usage_adjust = cpu_usage_adjust;
	bank->size = usable_size;
	bank->real_size = size;
	bank->alloc_order = alloc_order;
	bank->name = name;

    err = _mali_osk_atomic_init(&bank->num_active_allocations, 0);
    if (err != _MALI_OSK_ERR_OK)
    {
		_mali_osk_free(bank);
        MALI_ERROR(err);
    }

	bank->used_for_flags = flags;
	bank->min_order = order_needed_for_size(MIN_BLOCK_SIZE, NULL);
	bank->max_order = maximum_order_which_fits(usable_size);
	bank->lock = _mali_osk_lock_init((_mali_osk_lock_flags_t)(_MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE), 0, 0);
    if (NULL == bank->lock)
    {
        _mali_osk_atomic_term(&bank->num_active_allocations);
		_mali_osk_free(bank);
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
    }

	bank->blocklist = _mali_osk_calloc(1, sizeof(struct mali_memory_block) * (usable_size / MIN_BLOCK_SIZE));
	if (NULL == bank->blocklist)
	{
        _mali_osk_lock_term(bank->lock);
        _mali_osk_atomic_term(&bank->num_active_allocations);
		_mali_osk_free(bank);
        MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	for (i = 0; i < (usable_size / MIN_BLOCK_SIZE); i++)
	{
		bank->blocklist[i].bank = bank;
	}

	bank->freelist = _mali_osk_calloc(1, sizeof(_mali_osk_list_t) * (bank->max_order - bank->min_order + 1));
	if (NULL == bank->freelist)
	{
        _mali_osk_lock_term(bank->lock);
		_mali_osk_free(bank->blocklist);
        _mali_osk_atomic_term(&bank->num_active_allocations);
		_mali_osk_free(bank);
        MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	for (i = 0; i < (bank->max_order - bank->min_order + 1); i++) _MALI_OSK_INIT_LIST_HEAD(&bank->freelist[i]);

	/* init slot info */
	for (offset = 0, left = usable_size; offset < (usable_size / MIN_BLOCK_SIZE); /* updated inside the body */)
	{
		u32 block_order;
		mali_memory_block * block;

		/* the maximum order which fits in the remaining area */
		block_order = maximum_order_which_fits(left);

		/* find the block pointer */
		block = &bank->blocklist[offset];

		/* tag the block as being toplevel */
		set_block_toplevel(block, block_order);

		/* tag it as being free */
		set_block_free(block, 1);

		/* set the order */
		set_block_order(block, block_order);

		_mali_osk_list_addtail(&block->link, bank->freelist + (block_order - bank->min_order));

		left -= (1 << block_order);
		offset += ((1 << block_order) / MIN_BLOCK_SIZE);
	}

	/* add bank to list of banks on the system */
	_MALI_OSK_LIST_FOREACHENTRY( bank_enum, temp, &memory_banks_list, mali_memory_bank, list )
	{
		if ( bank_enum->alloc_order >= alloc_order )
		{
			/* Found insertion point - our item must go before this one */
			break;
		}
	}
    _mali_osk_list_addtail(&bank->list, &bank_enum->list);

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_memory_mmu_register(u32 type, u32 phys_base)
{
	/* not supported */
	return _MALI_OSK_ERR_INVALID_FUNC;
}

void mali_memory_mmu_unregister(u32 phys_base)
{
	/* not supported */
	return;
}

static mali_memory_block * mali_memory_block_get(u32 type_id, u32 minimum_size)
{
	mali_memory_bank * bank;
	mali_memory_block * block = NULL;
	u32 requested_order, current_order;

	/* input validation */
	if (0 == minimum_size)
	{
		/* bad size */
		MALI_DEBUG_PRINT(2, ("Zero size block requested by mali_memory_block_get\n"));
		return NULL;
	}

	bank = (mali_memory_bank*)type_id;

	requested_order = order_needed_for_size(minimum_size, bank);

	MALI_DEBUG_PRINT(4, ("For size %d we need order %d (%d)\n", minimum_size, requested_order, 1 << requested_order));

	_mali_osk_lock_wait(bank->lock, _MALI_OSK_LOCKMODE_RW);
	/* ! critical section begin */

	MALI_DEBUG_PRINT(7, ("Bank 0x%x locked\n", bank));

	for (current_order = requested_order; current_order <= bank->max_order; ++current_order)
	{
		_mali_osk_list_t * list = bank->freelist + (current_order - bank->min_order);
		MALI_DEBUG_PRINT(7, ("Checking freelist 0x%x for order %d\n", list, current_order));
		if (0 != _mali_osk_list_empty(list)) continue; /* empty list */

		MALI_DEBUG_PRINT(7, ("Found an entry on the freelist for order %d\n", current_order));


		block = _MALI_OSK_LIST_ENTRY(list->next, mali_memory_block, link);
		_mali_osk_list_delinit(&block->link);

		while (current_order > requested_order)
		{
			mali_memory_block * buddy_block;
			MALI_DEBUG_PRINT(7, ("Splitting block 0x%x\n", block));
			current_order--;
			list--;
			buddy_block = block_get_buddy(block, current_order - bank->min_order);
			set_block_order(buddy_block, current_order);
			set_block_free(buddy_block, 1);
			_mali_osk_list_add(&buddy_block->link, list);
		}

		set_block_order(block, current_order);
		set_block_free(block, 0);

		/* update usage count */
		_mali_osk_atomic_inc(&bank->num_active_allocations);

		break;
	}

	/* ! critical section end */
	_mali_osk_lock_signal(bank->lock, _MALI_OSK_LOCKMODE_RW);

	MALI_DEBUG_PRINT(7, ("Lock released for bank 0x%x\n", bank));

	MALI_DEBUG_PRINT_IF(7, NULL != block, ("Block 0x%x allocated\n", block));

	return block;
}


static void block_release(mali_memory_block * block)
{
	mali_memory_bank * bank;
	u32 current_order;

	if (NULL == block) return;

	bank = block->bank;

	/* we're manipulating the free list, so we need to lock it */
	_mali_osk_lock_wait(bank->lock, _MALI_OSK_LOCKMODE_RW);
	/* ! critical section begin */

	set_block_free(block, 1);
	current_order = get_block_order(block);

	while (current_order <= bank->max_order)
	{
		mali_memory_block * buddy_block;
		buddy_block = block_get_buddy(block, current_order - bank->min_order);
		if (!block_is_valid_buddy(buddy_block, current_order)) break;
		_mali_osk_list_delinit(&buddy_block->link); /* remove from free list */
		/* clear tracked data in both blocks */
		set_block_order(block, 0);
		set_block_free(block, 0);
		set_block_order(buddy_block, 0);
		set_block_free(buddy_block, 0);
		/* make the parent control the new state */
		block = block_get_parent(block, current_order - bank->min_order);
		set_block_order(block, current_order + 1); /* merged has a higher order */
		set_block_free(block, 1); /* mark it as free */
		current_order++;
 		if (get_block_toplevel(block) == current_order) break; /* stop the merge if we've arrived at a toplevel block */
	}

	_mali_osk_list_add(&block->link, &bank->freelist[current_order - bank->min_order]);

	/* update bank usage statistics */
	_mali_osk_atomic_dec(&block->bank->num_active_allocations);

	/* !critical section end */
	_mali_osk_lock_signal(bank->lock, _MALI_OSK_LOCKMODE_RW);

	return;
}

MALI_STATIC_INLINE u32 block_get_offset(mali_memory_block * block)
{
	return block - block->bank->blocklist;
}

MALI_STATIC_INLINE u32 block_mali_addr_get(mali_memory_block * block)
{
	if (NULL != block) return block->bank->base_addr + MIN_BLOCK_SIZE * block_get_offset(block);
	else return 0;
}

MALI_STATIC_INLINE u32 block_cpu_addr_get(mali_memory_block * block)
{
	if (NULL != block) return (block->bank->base_addr + MIN_BLOCK_SIZE * block_get_offset(block)) + block->bank->cpu_usage_adjust;
	else return 0;
}

MALI_STATIC_INLINE u32 block_size_get(mali_memory_block * block)
{
	if (NULL != block) return 1 << get_block_order(block);
	else return 0;
}

MALI_STATIC_INLINE void __user * block_mapping_get(mali_memory_block * block)
{
	if (NULL != block) return block->mapping;
	else return NULL;
}

MALI_STATIC_INLINE void block_mapping_set(mali_memory_block * block, void __user * ptr)
{
	if (NULL != block) block->mapping = ptr;
}

MALI_STATIC_INLINE u32 block_mmap_cookie_get(mali_memory_block * block)
{
	if (NULL != block) return block->mmap_cookie;
	else return 0;
}

/**
 * Set the cookie returned via _mali_ukk_mem_mmap().
 * @param block The memory block to set the cookie for
 * @param cookie the cookie
 */
MALI_STATIC_INLINE void block_mmap_cookie_set(mali_memory_block * block, u32 cookie)
{
	if (NULL != block) block->mmap_cookie = cookie;
}


static _mali_osk_errcode_t mali_memory_core_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue)
{
	memory_session * session_data;

	/* validate input */
	if (NULL == slot)
	{
		MALI_DEBUG_PRINT(1, ("NULL slot given to memory session begin\n"));
        MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	if (NULL != *slot)
	{
		MALI_DEBUG_PRINT(1, ("The slot given to memory session begin already contains data"));
        MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	/* create the session data object */
	MALI_CHECK_NON_NULL(session_data = _mali_osk_malloc(sizeof(memory_session)), _MALI_OSK_ERR_NOMEM);

	/* create descriptor mapping table */
	session_data->descriptor_mapping = mali_descriptor_mapping_create(MALI_MEM_DESCRIPTORS_INIT, MALI_MEM_DESCRIPTORS_MAX);

    if (NULL == session_data->descriptor_mapping)
	{
		_mali_osk_free(session_data);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	_MALI_OSK_INIT_LIST_HEAD(&session_data->memory_head); /* no memory in use */
	session_data->lock = _mali_osk_lock_init((_mali_osk_lock_flags_t)(_MALI_OSK_LOCKFLAG_ONELOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE), 0, 0);
    if (NULL == session_data->lock)
    {
		_mali_osk_free(session_data);
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
    }

	*slot = session_data; /* slot will point to our data object */

	MALI_SUCCESS;
}

static void mali_memory_core_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot)
{
	memory_session * session_data;

	/* validate input */
	if (NULL == slot)
	{
		MALI_DEBUG_PRINT(1, ("NULL slot given to memory session begin\n"));
		return;
	}

	if (NULL == *slot)
	{
		MALI_DEBUG_PRINT(1, ("NULL memory_session found in current session object"));
		return;
	}

	_mali_osk_lock_wait(((memory_session*)*slot)->lock, _MALI_OSK_LOCKMODE_RW);
	session_data = (memory_session *)*slot;
	 /* clear our slot */
        *slot = NULL;

	/*
		First free all memory still being used.
		This can happen if the caller has leaked memory or
		the application has crashed forcing an auto-session end.
	*/
	if (0 == _mali_osk_list_empty(&session_data->memory_head))
	{
		mali_memory_block * block, * temp;
		MALI_DEBUG_PRINT(1, ("Memory found on session usage list during session termination\n"));

		/* use the _safe version since fre_big_block removes the active block from the list we're iterating */
		_MALI_OSK_LIST_FOREACHENTRY(block, temp, &session_data->memory_head, mali_memory_block, link)
		{
			_mali_osk_errcode_t err;
			_mali_uk_free_big_block_s uk_args;

			MALI_DEBUG_PRINT(4, ("Freeing block 0x%x with mali address 0x%x size %d mapped in user space at 0x%x\n",
						block,
						(void*)block_mali_addr_get(block),
						block_size_get(block),
						block_mapping_get(block))
				   	);

			/* free the block */
			/** @note manual type safety check-point */
    		uk_args.ctx = mali_session_data;
			uk_args.cookie = (u32)block->descriptor;
			err = _mali_ukk_free_big_block_internal( mali_session_data, session_data, &uk_args );

			if ( _MALI_OSK_ERR_OK != err )
			{
				MALI_DEBUG_PRINT_ERROR(("_mali_ukk_free_big_block_internal() failed during session termination on block with cookie==0x%X\n",
										uk_args.cookie)
									   );
			}
		}
	}

	if (NULL != session_data->descriptor_mapping)
	{
		mali_descriptor_mapping_destroy(session_data->descriptor_mapping);
		session_data->descriptor_mapping = NULL;
	}

	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
	_mali_osk_lock_term(session_data->lock);

    /* free the session data object */
	_mali_osk_free(session_data);

	return;
}

static _mali_osk_errcode_t mali_memory_core_system_info_fill(_mali_system_info* info)
{
	mali_memory_bank * bank, *temp;
	_mali_mem_info **mem_info_tail;

	/* check input */
    MALI_CHECK_NON_NULL(info, _MALI_OSK_ERR_INVALID_ARGS);

	/* make sure we won't leak any memory. It could also be that it's an uninitialized variable, but that would be a bug in the caller */
	MALI_DEBUG_ASSERT(NULL == info->mem_info);

	mem_info_tail = &info->mem_info;

	_MALI_OSK_LIST_FOREACHENTRY(bank, temp, &memory_banks_list, mali_memory_bank, list)
	{
		_mali_mem_info * mem_info;

        mem_info = (_mali_mem_info *)_mali_osk_calloc(1, sizeof(_mali_mem_info));
		if (NULL == mem_info) return _MALI_OSK_ERR_NOMEM; /* memory already allocated will be freed by the caller */

		/* set info */
		mem_info->size = bank->size;
		mem_info->flags = (_mali_bus_usage)bank->used_for_flags;
		mem_info->maximum_order_supported = bank->max_order;
		mem_info->identifier = (u32)bank;

		/* add to system info linked list */
		(*mem_info_tail) = mem_info;
		mem_info_tail = &mem_info->next;
	}

	/* all OK */
    MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_resource_memory(_mali_osk_resource_t * resource)
{
	_mali_osk_errcode_t err;

    /* Request ownership of the memory */
	if (_MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(resource->base, resource->size, resource->description))
	{
		MALI_DEBUG_PRINT(1, ("Failed to request memory region %s (0x%08X - 0x%08X)\n", resource->description, resource->base, resource->base + resource->size - 1));
        MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	/* call backend */
	err = mali_memory_bank_register(resource->base, resource->cpu_usage_adjust, resource->size, resource->flags, resource->alloc_order, resource->description);
	if (_MALI_OSK_ERR_OK != err)
	{
		/* if backend refused the memory we have to release the region again */
		MALI_DEBUG_PRINT(1, ("Memory bank registration failed\n"));
		_mali_osk_mem_unreqregion(resource->base, resource->size);
        MALI_ERROR(err);
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_memory_core_resource_mmu(_mali_osk_resource_t * resource)
{
	/* Not supported by the fixed block memory system */
	MALI_DEBUG_PRINT(1, ("MMU resource not supported by non-MMU driver!\n"));
	MALI_ERROR(_MALI_OSK_ERR_INVALID_FUNC);
}

static _mali_osk_errcode_t mali_memory_core_resource_fpga(_mali_osk_resource_t * resource)
{
	mali_io_address mapping;

	MALI_DEBUG_PRINT(5, ("FPGA framework '%s' @ (0x%08X - 0x%08X)\n",
			resource->description, resource->base, resource->base + sizeof(u32) * 2 - 1
		   ));

	mapping = _mali_osk_mem_mapioregion(resource->base + 0x1000, sizeof(u32) * 2, "fpga framework");
	if (mapping)
	{
		u32 data;
		data = _mali_osk_mem_ioread32(mapping, 0);
		MALI_DEBUG_PRINT(2, ("FPGA framwork '%s' @ 0x%08X:\n", resource->description, resource->base));
		MALI_DEBUG_PRINT(2, ("\tBitfile date: %d%02d%02d_%02d%02d\n",
					  (data >> 20),
					  (data >> 16) & 0xF,
					  (data >> 11) & 0x1F,
					  (data >> 6)  & 0x1F,
					  (data >> 0)  & 0x3F));
		data = _mali_osk_mem_ioread32(mapping, sizeof(u32));
		MALI_DEBUG_PRINT(2, ("\tBitfile SCCS rev: %d\n", data));

		_mali_osk_mem_unmapioregion(resource->base + 0x1000, sizeof(u32) *2, mapping);
	}
	else MALI_DEBUG_PRINT(1, ("Failed to access FPGA framwork '%s' @ 0x%08X\n", resource->description, resource->base));

	MALI_SUCCESS;
}

/* static _mali_osk_errcode_t get_big_block(void * ukk_private, struct mali_session_data * mali_session_data, void __user * argument) */
_mali_osk_errcode_t _mali_ukk_get_big_block( _mali_uk_get_big_block_s *args )
{
	_mali_uk_mem_mmap_s args_mmap = {0, };
	int md;
	mali_memory_block * block;
	_mali_osk_errcode_t err;
	memory_session * session_data;

	MALI_DEBUG_ASSERT_POINTER( args );

	MALI_DEBUG_ASSERT_POINTER( args->ctx );

	/** @note manual type safety check-point */
    session_data = (memory_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_memory_id);

    MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_INVALID_ARGS);

	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);

    if (!args->type_id)
	{
		_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* at least min block size */
	if (MIN_BLOCK_SIZE > args->minimum_size_requested) args->minimum_size_requested = MIN_BLOCK_SIZE;

	/* perform the actual allocation */
	block = mali_memory_block_get(args->type_id, args->minimum_size_requested);
	if ( NULL == block )
	{
		/* no memory available with requested type_id */
		_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

    if (_MALI_OSK_ERR_OK != mali_descriptor_mapping_allocate_mapping(session_data->descriptor_mapping, block, &md))
    {
		block_release(block);
		_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
       MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}
	block->descriptor = md;


	/* fill in response */
	args->mali_address = block_mali_addr_get(block);
	args->block_size = block_size_get(block);
	args->cookie = (u32)md;
	args->flags = block->bank->used_for_flags;

	/* map the block into the process' address space */

	/** @note manual type safety check-point */
	args_mmap.ukk_private = (void *)args->ukk_private;
	args_mmap.ctx = args->ctx;
	args_mmap.size = args->block_size;
	args_mmap.phys_addr = block_cpu_addr_get(block);

#ifndef _MALI_OSK_SPECIFIC_INDIRECT_MMAP
	err = _mali_ukk_mem_mmap( &args_mmap );
#else
	err = _mali_osk_specific_indirect_mmap( &args_mmap );
#endif

	/* check if the mapping failed */
	if ( _MALI_OSK_ERR_OK != err )
	{
		MALI_DEBUG_PRINT(1, ("Memory mapping failed 0x%x\n", args->cpuptr));
		/* mapping failed */

		/* remove descriptor entry */
		mali_descriptor_mapping_free(session_data->descriptor_mapping, md);

		/* free the mali memory */
		block_release(block);

		_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);
        return err;
	}

	args->cpuptr = args_mmap.mapping;
	block_mmap_cookie_set(block, args_mmap.cookie);
	block_mapping_set(block, args->cpuptr);

	MALI_DEBUG_PRINT(2, ("Mali memory 0x%x (size %d) mapped in process memory space at 0x%x\n", (void*)args->mali_address, args->block_size, args->cpuptr));

	/* track memory in use for the session */
	_mali_osk_list_addtail(&block->link, &session_data->memory_head);

	/* memory assigned to the session, memory mapped into the process' view */
	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	MALI_SUCCESS;
}

/* Internal code that assumes the memory session lock is held */
static _mali_osk_errcode_t _mali_ukk_free_big_block_internal( struct mali_session_data * mali_session_data, memory_session * session_data, _mali_uk_free_big_block_s *args)
{
	mali_memory_block * block = NULL;
	_mali_osk_errcode_t err;
	_mali_uk_mem_munmap_s args_munmap = {0,};

	MALI_DEBUG_ASSERT_POINTER( mali_session_data );
	MALI_DEBUG_ASSERT_POINTER( session_data );
	MALI_DEBUG_ASSERT_POINTER( args );

	err = mali_descriptor_mapping_get(session_data->descriptor_mapping, (int)args->cookie, (void**)&block);
	if (_MALI_OSK_ERR_OK != err)
	{
		MALI_DEBUG_PRINT(1, ("Invalid memory descriptor %d used to release memory pages\n", (int)args->cookie));
		MALI_ERROR(err);
	}

    MALI_DEBUG_ASSERT_POINTER(block);

	MALI_DEBUG_PRINT(4, ("Asked to free block 0x%x with mali address 0x%x size %d mapped in user space at 0x%x\n",
			block,
			(void*)block_mali_addr_get(block),
			block_size_get(block),
			block_mapping_get(block))
		   );

	/** @note manual type safety check-point */
	args_munmap.ctx = (void*)mali_session_data;
	args_munmap.mapping = block_mapping_get( block );
	args_munmap.size = block_size_get( block );
	args_munmap.cookie = block_mmap_cookie_get( block );

#ifndef _MALI_OSK_SPECIFIC_INDIRECT_MMAP
		_mali_ukk_mem_munmap( &args_munmap );
#else
		_mali_osk_specific_indirect_munmap( &args_munmap );
#endif

	MALI_DEBUG_PRINT(6, ("Session data 0x%x, lock 0x%x\n", session_data, &session_data->lock));

	/* unlink from session usage list */
	MALI_DEBUG_PRINT(5, ("unlink from session usage list\n"));
	_mali_osk_list_delinit(&block->link);

	/* remove descriptor entry */
	mali_descriptor_mapping_free(session_data->descriptor_mapping, (int)args->cookie);

	/* free the mali memory */
	block_release(block);
	MALI_DEBUG_PRINT(5, ("Block freed\n"));

	MALI_SUCCESS;
}

/* static _mali_osk_errcode_t free_big_block( struct mali_session_data * mali_session_data, void __user * argument) */
_mali_osk_errcode_t _mali_ukk_free_big_block( _mali_uk_free_big_block_s *args )
{
	_mali_osk_errcode_t err;
	struct mali_session_data * mali_session_data;
	memory_session * session_data;

	MALI_DEBUG_ASSERT_POINTER( args );

	MALI_DEBUG_ASSERT_POINTER( args->ctx );

	/** @note manual type safety check-point */
	mali_session_data = (struct mali_session_data *)args->ctx;

	/* Must always verify this, since these are provided by the user */
    MALI_CHECK_NON_NULL(mali_session_data, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = mali_kernel_session_manager_slot_get(mali_session_data, mali_subsystem_memory_id);

    MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_INVALID_ARGS);

	_mali_osk_lock_wait(session_data->lock, _MALI_OSK_LOCKMODE_RW);

	/** @note this has been separated out so that the session_end handler can call this while it has the memory_session lock held */
	err = _mali_ukk_free_big_block_internal( mali_session_data, session_data, args );

	_mali_osk_lock_signal(session_data->lock, _MALI_OSK_LOCKMODE_RW);

    return err;
}

MALI_STATIC_INLINE u32 get_block_free(mali_memory_block * block)
{
	return (block->misc >> MISC_SHIFT_FREE) & MISC_MASK_FREE;
}

MALI_STATIC_INLINE void set_block_free(mali_memory_block * block, int state)
{
	if (state) block->misc |= (MISC_MASK_FREE << MISC_SHIFT_FREE);
	else block->misc &= ~(MISC_MASK_FREE << MISC_SHIFT_FREE);
}

MALI_STATIC_INLINE void set_block_order(mali_memory_block * block, u32 order)
{
	block->misc &= ~(MISC_MASK_ORDER << MISC_SHIFT_ORDER);
	block->misc |= ((order & MISC_MASK_ORDER) << MISC_SHIFT_ORDER);
}

MALI_STATIC_INLINE u32 get_block_order(mali_memory_block * block)
{
	return (block->misc >> MISC_SHIFT_ORDER) & MISC_MASK_ORDER;
}

MALI_STATIC_INLINE void set_block_toplevel(mali_memory_block * block, u32 level)
{
	block->misc |= ((level & MISC_MASK_TOPLEVEL) << MISC_SHIFT_TOPLEVEL);
}

MALI_STATIC_INLINE u32 get_block_toplevel(mali_memory_block * block)
{
	return (block->misc >> MISC_SHIFT_TOPLEVEL) & MISC_MASK_TOPLEVEL;
}

MALI_STATIC_INLINE int block_is_valid_buddy(mali_memory_block * block, int order)
{
	if (get_block_free(block) && (get_block_order(block) == order)) return 1;
	else return 0;
}

MALI_STATIC_INLINE mali_memory_block * block_get_buddy(mali_memory_block * block, u32 order)
{
	return block + ( (block_get_offset(block) ^ (1 << order)) - block_get_offset(block));
}

MALI_STATIC_INLINE mali_memory_block * block_get_parent(mali_memory_block * block, u32 order)
{
	return block + ((block_get_offset(block) & ~(1 << order)) - block_get_offset(block));
}

/* This handler registered to mali_mmap for non-MMU builds */
_mali_osk_errcode_t _mali_ukk_mem_mmap( _mali_uk_mem_mmap_s *args )
{
	_mali_osk_errcode_t ret;
	struct mali_session_data * mali_session_data;
	mali_memory_allocation * descriptor;
	memory_session * session_data;

	/* validate input */
	if (NULL == args) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: args was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS); }

	/* Unpack arguments */
	mali_session_data = (struct mali_session_data *)args->ctx;

	if (NULL == mali_session_data) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: mali_session data was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS); }

	MALI_DEBUG_ASSERT( mali_subsystem_memory_id >= 0 );

	session_data = mali_kernel_session_manager_slot_get(mali_session_data, mali_subsystem_memory_id);
	/* validate input */
	if (NULL == session_data) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: session data was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_FAULT); }

	descriptor = (mali_memory_allocation*) _mali_osk_calloc( 1, sizeof(mali_memory_allocation) );
	if (NULL == descriptor) { MALI_DEBUG_PRINT(3,("mali_ukk_mem_mmap: descriptor was NULL\n")); MALI_ERROR(_MALI_OSK_ERR_NOMEM); }

	descriptor->size = args->size;
	descriptor->mali_address = args->phys_addr;
	descriptor->mali_addr_mapping_info = (void*)session_data;
	descriptor->process_addr_mapping_info = args->ukk_private; /* save to be used during physical manager callback */
	descriptor->flags = MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE;

	ret = _mali_osk_mem_mapregion_init( descriptor );
	if ( _MALI_OSK_ERR_OK != ret )
	{
		MALI_DEBUG_PRINT(3, ("_mali_osk_mem_mapregion_init() failed\n"));
		_mali_osk_free(descriptor);
        MALI_ERROR(ret);
	}

	ret = _mali_osk_mem_mapregion_map( descriptor, 0, &descriptor->mali_address, descriptor->size );
	if ( _MALI_OSK_ERR_OK != ret )
	{
		MALI_DEBUG_PRINT(3, ("_mali_osk_mem_mapregion_map() failed\n"));
		_mali_osk_mem_mapregion_term( descriptor );
		_mali_osk_free(descriptor);
        MALI_ERROR(ret);
	}

	args->mapping = descriptor->mapping;

	/**
	 * @note we do not require use of mali_descriptor_mapping here:
	 * the cookie gets stored in the mali_memory_block struct, which itself is
	 * protected by mali_descriptor_mapping, and so this cookie never leaves
	 * kernel space (on any OS).
	 *
	 * In the MMU case, we must use a mali_descriptor_mapping, since on _some_
	 * OSs, the cookie leaves kernel space.
	 */
	args->cookie = (u32)descriptor;
    MALI_SUCCESS;
}

/* This handler registered to mali_munmap for non-MMU builds */
_mali_osk_errcode_t _mali_ukk_mem_munmap( _mali_uk_mem_munmap_s *args )
{
	mali_memory_allocation * descriptor;

	/** see note in _mali_ukk_mem_mmap() - no need to use descriptor mapping */
	descriptor = (mali_memory_allocation *)args->cookie;
	MALI_DEBUG_ASSERT_POINTER(descriptor);

	/* args->mapping and args->size are also discarded. They are only necessary for certain do_munmap implementations. However, they could be used to check the descriptor at this point. */
	_mali_osk_mem_mapregion_unmap( descriptor, 0, descriptor->size, (_mali_osk_mem_mapregion_flags_t)0 );

	_mali_osk_mem_mapregion_term( descriptor );

	_mali_osk_free(descriptor);

	return _MALI_OSK_ERR_OK;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_init_mem( _mali_uk_init_mem_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_term_mem( _mali_uk_term_mem_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_map_external_mem( _mali_uk_map_external_mem_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_unmap_external_mem( _mali_uk_unmap_external_mem_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_query_mmu_page_table_dump_size( _mali_uk_query_mmu_page_table_dump_size_s *args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}

/**
 * Stub function to satisfy UDD interface exclusion requirement.
 * This is because the Base code compiles in \b both MMU and non-MMU calls,
 * so both sets must be declared (but the 'unused' set may be stub)
 */
_mali_osk_errcode_t _mali_ukk_dump_mmu_page_table( _mali_uk_dump_mmu_page_table_s * args )
{
	MALI_IGNORE( args );
	return _MALI_OSK_ERR_FAULT;
}
