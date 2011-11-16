/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_subsystem.h"
#include "mali_kernel_mem.h"
#include "mali_kernel_session_manager.h"
#include "mali_kernel_pp.h"
#include "mali_kernel_gp.h"
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_ukk.h"
#include "mali_kernel_core.h"
#include "mali_kernel_rendercore.h"
#if defined USING_MALI400_L2_CACHE
#include "mali_kernel_l2_cache.h"
#endif
#if USING_MALI_PMM
#include "mali_pmm.h"
#endif /* USING_MALI_PMM */

/* platform specific set up */
#include "mali_platform.h"

/* Initialized when this subsystem is initialized. This is determined by the
 * position in subsystems[], and so the value used to initialize this is
 * determined at compile time */
static mali_kernel_subsystem_identifier mali_subsystem_core_id = -1;

/** Pointer to table of resource definitions available to the Mali driver.
 *  _mali_osk_resources_init() sets up the pointer to this table.
 */
static _mali_osk_resource_t *arch_configuration = NULL;

/** Number of resources initialized by _mali_osk_resources_init() */
static u32 num_resources;

static _mali_osk_errcode_t register_resources( _mali_osk_resource_t **arch_configuration, u32 num_resources );

static _mali_osk_errcode_t initialize_subsystems(void);
static void terminate_subsystems(void);

static _mali_osk_errcode_t mali_kernel_subsystem_core_setup(mali_kernel_subsystem_identifier id);
static void mali_kernel_subsystem_core_cleanup(mali_kernel_subsystem_identifier id);
static _mali_osk_errcode_t mali_kernel_subsystem_core_system_info_fill(_mali_system_info* info);
static _mali_osk_errcode_t mali_kernel_subsystem_core_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue);

static _mali_osk_errcode_t build_system_info(void);
static void cleanup_system_info(_mali_system_info *cleanup);

/**
 * @brief handler for MEM_VALIDATION resources
 *
 * This resource handler is common to all memory systems. It provides a default
 * means for validating requests to map in external memory via
 * _mali_ukk_map_external_mem. In addition, if _mali_ukk_va_to_pa is
 * implemented, then _mali_ukk_va_to_pa can make use of this MEM_VALIDATION
 * resource.
 *
 * MEM_VALIDATION also provide a CPU physical to Mali physical address
 * translation, for use by _mali_ukk_map_external_mem.
 *
 * @note MEM_VALIDATION resources are only to handle simple cases where a
 * certain physical address range is allowed to be mapped in by any process,
 * e.g. a framebuffer at a fixed location. If the implementor has more complex
 * mapping requirements, then they must either:
 * - implement their own memory validation function
 * - or, integrate with UMP.
 *
 * @param resource The resource to handle (type MEM_VALIDATION)
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
static _mali_osk_errcode_t mali_kernel_core_resource_mem_validation(_mali_osk_resource_t * resource);

/* MEM_VALIDATION handler state */
typedef struct
{
	u32 phys_base;        /**< Mali physical base of the memory, page aligned */
	u32 size;             /**< size in bytes of the memory, multiple of page size */
	s32 cpu_usage_adjust; /**< Offset to add to Mali Physical address to obtain CPU physical address */
} _mali_mem_validation_t;

#define INVALID_MEM 0xffffffff

static _mali_mem_validation_t mem_validator = { INVALID_MEM, INVALID_MEM, -1 };

static struct mali_kernel_subsystem mali_subsystem_core =
{
	mali_kernel_subsystem_core_setup,               /* startup */
    mali_kernel_subsystem_core_cleanup,             /* shutdown */
    NULL,                                           /* load_complete */
    mali_kernel_subsystem_core_system_info_fill,    /* system_info_fill */
	mali_kernel_subsystem_core_session_begin,       /* session_begin */
    NULL,                                           /* session_end */
    NULL,                                           /* broadcast_notification */
#if MALI_STATE_TRACKING
	NULL,                                           /* dump_state */
#endif
};

static struct mali_kernel_subsystem * subsystems[] =
{
	/* always initialize the hw subsystems first */
	/* always included */
	&mali_subsystem_memory,

#if USING_MALI_PMM
	/* The PMM must be initialized before any cores - including L2 cache */
	&mali_subsystem_pmm,
#endif

	/* The rendercore subsystem must be initialized before any subsystem based on the
	 * rendercores is started e.g. mali_subsystem_mali200 and mali_subsystem_gp2 */
	&mali_subsystem_rendercore,

 	/* add reference to the subsystem */
	&mali_subsystem_mali200,

 	/* add reference to the subsystem */
	&mali_subsystem_gp2,

#if defined USING_MALI400_L2_CACHE
	&mali_subsystem_l2_cache,
#endif

	/* always included */
	/* NOTE Keep the core entry at the tail of the list */
	&mali_subsystem_core
};

#define SUBSYSTEMS_COUNT ( sizeof(subsystems) / sizeof(subsystems[0]) )

/* Pointers to this type available as incomplete struct in mali_kernel_session_manager.h */
struct mali_session_data
{
	void * subsystem_data[SUBSYSTEMS_COUNT];
	_mali_osk_notification_queue_t * ioctl_queue;
};

static mali_kernel_resource_registrator resource_handler[RESOURCE_TYPE_COUNT] = { NULL, };

/* system info variables */
static _mali_osk_lock_t *system_info_lock = NULL;
static _mali_system_info * system_info = NULL;
static u32 system_info_size = 0;

/* is called from OS specific driver entry point */
_mali_osk_errcode_t mali_kernel_constructor( void )
{
    _mali_osk_errcode_t err;

	err = mali_platform_init(NULL);
	if (_MALI_OSK_ERR_OK != err) goto error1;

    err = _mali_osk_init();
    if (_MALI_OSK_ERR_OK != err) goto error2;

	MALI_DEBUG_PRINT(2, ("\n"));
	MALI_DEBUG_PRINT(2, ("Inserting Mali v%d device driver. \n",_MALI_API_VERSION));
	MALI_DEBUG_PRINT(2, ("Compiled: %s, time: %s.\n", __DATE__, __TIME__));
	MALI_DEBUG_PRINT(2, ("Svn revision: %s\n", SVN_REV_STRING));

    err  = initialize_subsystems();
    if (_MALI_OSK_ERR_OK != err) goto error3;

    MALI_PRINT(("Mali device driver %s loaded\n", SVN_REV_STRING));

	MALI_SUCCESS;

error3:
	MALI_PRINT(("Mali subsystems failed\n"));
    _mali_osk_term();
error2:
	MALI_PRINT(("Mali device driver init failed\n"));
	if (_MALI_OSK_ERR_OK != mali_platform_deinit(NULL))
	{
		MALI_PRINT(("Failed to deinit platform\n"));
	}
error1:
	MALI_PRINT(("Failed to init platform\n"));
	MALI_ERROR(err);
}

/* is called from OS specific driver exit point */
void mali_kernel_destructor( void )
{
	MALI_DEBUG_PRINT(2, ("\n"));
	MALI_DEBUG_PRINT(2, ("Unloading Mali v%d device driver.\n",_MALI_API_VERSION));
	terminate_subsystems(); /* subsystems are responsible for their registered resources */
    _mali_osk_term();

	if (_MALI_OSK_ERR_OK != mali_platform_deinit(NULL))
	{
		MALI_PRINT(("Failed to deinit platform\n"));
	}
	MALI_DEBUG_PRINT(2, ("Module unloaded.\n"));
}

_mali_osk_errcode_t register_resources( _mali_osk_resource_t **arch_configuration, u32 num_resources )
{
	_mali_osk_resource_t *arch_resource = *arch_configuration;
	u32 i;
#if USING_MALI_PMM
	u32 is_pmu_first_resource = 1;
#endif /* USING_MALI_PMM */

	/* loop over arch configuration */
	for (i = 0; i < num_resources; ++i, arch_resource++)
	{
		if (  (arch_resource->type >= RESOURCE_TYPE_FIRST) &&
		      (arch_resource->type < RESOURCE_TYPE_COUNT) &&
		      (NULL != resource_handler[arch_resource->type])
		   )
		{	
#if USING_MALI_PMM
			if((arch_resource->type != PMU) && (is_pmu_first_resource == 1))
			{
				_mali_osk_resource_t mali_pmu_virtual_resource;
				mali_pmu_virtual_resource.type = PMU;
				mali_pmu_virtual_resource.description = "Virtual PMU";
				mali_pmu_virtual_resource.base = 0x00000000;
				mali_pmu_virtual_resource.cpu_usage_adjust = 0;
				mali_pmu_virtual_resource.size = 0;
				mali_pmu_virtual_resource.irq = 0;
				mali_pmu_virtual_resource.flags = 0;
				mali_pmu_virtual_resource.mmu_id = 0;
				mali_pmu_virtual_resource.alloc_order = 0;
				MALI_CHECK_NO_ERROR(resource_handler[mali_pmu_virtual_resource.type](&mali_pmu_virtual_resource));
			}
			is_pmu_first_resource = 0;
#endif /* USING_MALI_PMM */

			MALI_CHECK_NO_ERROR(resource_handler[arch_resource->type](arch_resource));
			/* the subsystem shutdown process will release all the resources already registered */
		}
		else
		{
			MALI_DEBUG_PRINT(1, ("No handler installed for resource %s, type %d\n", arch_resource->description, arch_resource->type));
			MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
		}
	}

	MALI_SUCCESS;
}

static _mali_osk_errcode_t initialize_subsystems(void)
{
	int i, j;
    _mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT; /* default error code */

    MALI_CHECK_NON_NULL(system_info_lock = _mali_osk_lock_init( (_mali_osk_lock_flags_t)(_MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE), 0, 0 ), _MALI_OSK_ERR_FAULT);

	for (i = 0; i < (int)SUBSYSTEMS_COUNT; ++i)
	{
		if (NULL != subsystems[i]->startup)
		{
			/* the subsystem has a startup function defined */
			err = subsystems[i]->startup(i); /* the subsystem identifier is the offset in our subsystems array */
			if (_MALI_OSK_ERR_OK != err) goto cleanup;
		}
	}

	for (j = 0; j < (int)SUBSYSTEMS_COUNT; ++j)
	{
		if (NULL != subsystems[j]->load_complete)
		{
			/* the subsystem has a load_complete function defined */
			err = subsystems[j]->load_complete(j);
			if (_MALI_OSK_ERR_OK != err) goto cleanup;
		}
	}

	/* All systems loaded and resources registered */
	/* Build system info */
	if (_MALI_OSK_ERR_OK != build_system_info()) goto cleanup;

	MALI_SUCCESS; /* all ok */

cleanup:
	/* i is index of subsystem which failed to start, all indices before that has to be shut down */
	for (i = i - 1; i >= 0; --i)
	{
		/* the subsystem identifier is the offset in our subsystems array */
		/* Call possible shutdown notficiation functions */
		if (NULL != subsystems[i]->shutdown) subsystems[i]->shutdown(i);
	}

    _mali_osk_lock_term( system_info_lock );
    MALI_ERROR(err); /* err is what the module which failed its startup returned, or the default */
}

static void terminate_subsystems(void)
{
	int i;
	/* shut down subsystems in reverse order from startup */
	for (i = SUBSYSTEMS_COUNT - 1; i >= 0; --i)
	{
		/* the subsystem identifier is the offset in our subsystems array */
		if (NULL != subsystems[i]->shutdown) subsystems[i]->shutdown(i);
	}
    if (system_info_lock) _mali_osk_lock_term( system_info_lock );

	/* Free _mali_system_info struct */
	cleanup_system_info(system_info);
}

void _mali_kernel_core_broadcast_subsystem_message(mali_core_notification_message message, u32 data)
{
	int i;

	for (i = 0; i < (int)SUBSYSTEMS_COUNT; ++i)
	{
		if (NULL != subsystems[i]->broadcast_notification)
		{
			subsystems[i]->broadcast_notification(message, data);
		}
	}
}

static _mali_osk_errcode_t mali_kernel_subsystem_core_setup(mali_kernel_subsystem_identifier id)
{
    mali_subsystem_core_id = id;

	/* Register our own resources */
	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MEM_VALIDATION, mali_kernel_core_resource_mem_validation));

    /* parse the arch resource definition and tell all the subsystems */
	/* this is why the core subsystem has to be specified last in the subsystem array */
    MALI_CHECK_NO_ERROR(_mali_osk_resources_init(&arch_configuration, &num_resources));

    MALI_CHECK_NO_ERROR(register_resources(&arch_configuration, num_resources));

    /* resource parsing succeeded and the subsystem have corretly accepted their resources */
	MALI_SUCCESS;
}

static void mali_kernel_subsystem_core_cleanup(mali_kernel_subsystem_identifier id)
{
    _mali_osk_resources_term(&arch_configuration, num_resources);
}

static void cleanup_system_info(_mali_system_info *cleanup)
{
	_mali_core_info * current_core;
	_mali_mem_info * current_mem;

	/* delete all the core info structs */
	while (NULL != cleanup->core_info)
	{
		current_core = cleanup->core_info;
		cleanup->core_info = cleanup->core_info->next;
		_mali_osk_free(current_core);
	}

	/* delete all the mem info struct */
	while (NULL != cleanup->mem_info)
	{
		current_mem = cleanup->mem_info;
		cleanup->mem_info = cleanup->mem_info->next;
		_mali_osk_free(current_mem);
	}

	/* delete the system info struct itself */
	_mali_osk_free(cleanup);
}

static _mali_osk_errcode_t build_system_info(void)
{
	unsigned int i;
	int err = _MALI_OSK_ERR_FAULT;
	_mali_system_info * new_info, * cleanup;
	_mali_core_info * current_core;
	_mali_mem_info * current_mem;
	u32 new_size = 0;

	/* create a new system info struct */
	MALI_CHECK_NON_NULL(new_info = (_mali_system_info *)_mali_osk_malloc(sizeof(_mali_system_info)), _MALI_OSK_ERR_NOMEM);

	_mali_osk_memset(new_info, 0, sizeof(_mali_system_info));

	/* if an error happens during any of the system_info_fill calls cleanup the new info structs */
	cleanup = new_info;

	/* ask each subsystems to fill in their info */
	for (i = 0; i < SUBSYSTEMS_COUNT; ++i)
	{
		if (NULL != subsystems[i]->system_info_fill)
		{
			err = subsystems[i]->system_info_fill(new_info);
			if (_MALI_OSK_ERR_OK != err) goto error_exit;
		}
	}

	/* building succeeded, calculate the size */

	/* size needed of the system info struct itself */
	new_size = sizeof(_mali_system_info);

	/* size needed for the cores */
	for (current_core = new_info->core_info; NULL != current_core; current_core = current_core->next)
	{
		new_size += sizeof(_mali_core_info);
	}

	/* size needed for the memory banks */
	for (current_mem = new_info->mem_info; NULL != current_mem; current_mem = current_mem->next)
	{
		new_size += sizeof(_mali_mem_info);
	}

	/* lock system info access so a user wont't get a corrupted version */
	_mali_osk_lock_wait( system_info_lock, _MALI_OSK_LOCKMODE_RW );

	/* cleanup the old one */
	cleanup = system_info;
	/* set new info */
	system_info = new_info;
	system_info_size = new_size;

	/* we're safe */
	_mali_osk_lock_signal( system_info_lock, _MALI_OSK_LOCKMODE_RW );

	/* ok result */
	err = _MALI_OSK_ERR_OK;

	/* we share the cleanup routine with the error case */
error_exit:
	if (NULL == cleanup) MALI_ERROR((_mali_osk_errcode_t)err); /* no cleanup needed, return what err contains */

	/* cleanup */
	cleanup_system_info(cleanup);

	/* return whatever err is, we could end up here in both the error and success cases */
	MALI_ERROR((_mali_osk_errcode_t)err);
}

_mali_osk_errcode_t _mali_ukk_get_api_version( _mali_uk_get_api_version_s *args )
{
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    /* check compatability */
	if ( args->version == _MALI_UK_API_VERSION )
	{
		args->compatible = 1;
	}
	else
	{
		args->compatible = 0;
	}

	args->version = _MALI_UK_API_VERSION; /* report our version */

	/* success regardless of being compatible or not */
    MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_get_system_info_size(_mali_uk_get_system_info_size_s *args)
{
    MALI_DEBUG_ASSERT_POINTER(args);
    args->size = system_info_size;
    MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_get_system_info( _mali_uk_get_system_info_s *args )
{
	_mali_core_info * current_core;
	_mali_mem_info * current_mem;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	void * current_write_pos, ** current_patch_pos;
    u32 adjust_ptr_base;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    MALI_CHECK_NON_NULL(args->system_info, _MALI_OSK_ERR_INVALID_ARGS);

	/* lock the system info */
	_mali_osk_lock_wait( system_info_lock, _MALI_OSK_LOCKMODE_RW );

	/* first check size */
	if (args->size < system_info_size) goto exit_when_locked;

	/* we build a copy of system_info in the user space buffer specified by the user and
     * patch up the pointers. The ukk_private members of _mali_uk_get_system_info_s may
     * indicate a different base address for patching the pointers (normally the
     * address of the provided system_info buffer would be used). This is helpful when
     * the system_info buffer needs to get copied to user space and the pointers need
     * to be in user space.
     */
    if (0 == args->ukk_private)
    {
        adjust_ptr_base = (u32)args->system_info;
    }
    else
    {
        adjust_ptr_base = args->ukk_private;
    }

	/* copy each struct into the buffer, and update its pointers */
	current_write_pos = (void *)args->system_info;

	/* first, the master struct */
	_mali_osk_memcpy(current_write_pos, system_info, sizeof(_mali_system_info));

	/* advance write pointer */
	current_write_pos = (void *)((u32)current_write_pos + sizeof(_mali_system_info));

	/* first we write the core info structs, patch starts at master's core_info pointer */
	current_patch_pos = (void **)((u32)args->system_info + offsetof(_mali_system_info, core_info));

	for (current_core = system_info->core_info; NULL != current_core; current_core = current_core->next)
	{

		/* patch the pointer pointing to this core */
		*current_patch_pos = (void*)(adjust_ptr_base + ((u32)current_write_pos - (u32)args->system_info));

		/* copy the core info */
		_mali_osk_memcpy(current_write_pos, current_core, sizeof(_mali_core_info));

		/* update patch pos */
		current_patch_pos = (void **)((u32)current_write_pos + offsetof(_mali_core_info, next));

		/* advance write pos in memory */
		current_write_pos = (void *)((u32)current_write_pos + sizeof(_mali_core_info));
	}
	/* patching of last patch pos is not needed, since we wrote NULL there in the first place */

	/* then we write the mem info structs, patch starts at master's mem_info pointer */
	current_patch_pos = (void **)((u32)args->system_info + offsetof(_mali_system_info, mem_info));

	for (current_mem = system_info->mem_info; NULL != current_mem; current_mem = current_mem->next)
	{
		/* patch the pointer pointing to this core */
		*current_patch_pos = (void*)(adjust_ptr_base + ((u32)current_write_pos - (u32)args->system_info));

		/* copy the core info */
		_mali_osk_memcpy(current_write_pos, current_mem, sizeof(_mali_mem_info));

		/* update patch pos */
		current_patch_pos = (void **)((u32)current_write_pos + offsetof(_mali_mem_info, next));

		/* advance write pos in memory */
		current_write_pos = (void *)((u32)current_write_pos + sizeof(_mali_mem_info));
	}
	/* patching of last patch pos is not needed, since we wrote NULL there in the first place */

	err = _MALI_OSK_ERR_OK;
exit_when_locked:
	_mali_osk_lock_signal( system_info_lock, _MALI_OSK_LOCKMODE_RW );
    MALI_ERROR(err);
}

_mali_osk_errcode_t _mali_ukk_wait_for_notification( _mali_uk_wait_for_notification_s *args )
{
	_mali_osk_errcode_t err;
	_mali_osk_notification_t * notification;
    _mali_osk_notification_queue_t *queue;

    /* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    queue = (_mali_osk_notification_queue_t *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_core_id);

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue)
	{
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
        args->type = _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS;
		MALI_SUCCESS;
	}

    /* receive a notification, might sleep */
	err = _mali_osk_notification_queue_receive(queue, &notification);
	if (_MALI_OSK_ERR_OK != err)
	{
        MALI_ERROR(err); /* errcode returned, pass on to caller */
    }

	/* copy the buffer to the user */
    args->type = (_mali_uk_notification_type)notification->notification_type;
    _mali_osk_memcpy(&args->data, notification->result_buffer, notification->result_buffer_size);

	/* finished with the notification */
	_mali_osk_notification_delete( notification );

    MALI_SUCCESS; /* all ok */
}

_mali_osk_errcode_t _mali_ukk_post_notification( _mali_uk_post_notification_s *args )
{
	_mali_osk_notification_t * notification;
    _mali_osk_notification_queue_t *queue;

    /* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

    queue = (_mali_osk_notification_queue_t *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_core_id);

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue)
	{
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		MALI_SUCCESS;
	}

	notification = _mali_osk_notification_create(args->type, 0);
	if ( NULL == notification)
	{
		MALI_PRINT_ERROR( ("Failed to create notification object\n")) ;
		return _MALI_OSK_ERR_NOMEM;
	}

	_mali_osk_notification_queue_send(queue, notification);

    MALI_SUCCESS; /* all ok */
}

static _mali_osk_errcode_t mali_kernel_subsystem_core_system_info_fill(_mali_system_info* info)
{
    MALI_CHECK_NON_NULL(info, _MALI_OSK_ERR_INVALID_ARGS);

	info->drivermode = _MALI_DRIVER_MODE_NORMAL;

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_kernel_subsystem_core_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue)
{
    MALI_CHECK_NON_NULL(slot, _MALI_OSK_ERR_INVALID_ARGS);
	*slot = queue;
	MALI_SUCCESS;
}

/* MEM_VALIDATION resource handler */
static _mali_osk_errcode_t mali_kernel_core_resource_mem_validation(_mali_osk_resource_t * resource)
{
	/* Check that no other MEM_VALIDATION resources exist */
	MALI_CHECK( ((u32)-1) == mem_validator.phys_base, _MALI_OSK_ERR_FAULT );

	/* Check restrictions on page alignment */
	MALI_CHECK( 0 == (resource->base & (~_MALI_OSK_CPU_PAGE_MASK)), _MALI_OSK_ERR_FAULT );
	MALI_CHECK( 0 == (resource->size & (~_MALI_OSK_CPU_PAGE_MASK)), _MALI_OSK_ERR_FAULT );
	MALI_CHECK( 0 == (resource->cpu_usage_adjust & (~_MALI_OSK_CPU_PAGE_MASK)), _MALI_OSK_ERR_FAULT );

	mem_validator.phys_base = resource->base;
	mem_validator.size = resource->size;
	mem_validator.cpu_usage_adjust = resource->cpu_usage_adjust;
	MALI_DEBUG_PRINT( 2, ("Memory Validator '%s' installed for Mali physical address base==0x%08X, size==0x%08X, cpu_adjust==0x%08X\n",
						  resource->description, mem_validator.phys_base, mem_validator.size, mem_validator.cpu_usage_adjust ));
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_kernel_core_translate_cpu_to_mali_phys_range( u32 *phys_base, u32 size )
{
	u32 mali_phys_base;

	mali_phys_base = *phys_base - mem_validator.cpu_usage_adjust;

	MALI_CHECK( 0 == ( mali_phys_base & (~_MALI_OSK_CPU_PAGE_MASK)), _MALI_OSK_ERR_FAULT );
	MALI_CHECK( 0 == ( size & (~_MALI_OSK_CPU_PAGE_MASK)), _MALI_OSK_ERR_FAULT );

	MALI_CHECK_NO_ERROR( mali_kernel_core_validate_mali_phys_range( mali_phys_base, size ) );

	*phys_base = mali_phys_base;
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_kernel_core_validate_mali_phys_range( u32 phys_base, u32 size )
{
	MALI_CHECK_GOTO( 0 == ( phys_base & (~_MALI_OSK_CPU_PAGE_MASK)), failure );
	MALI_CHECK_GOTO( 0 == ( size & (~_MALI_OSK_CPU_PAGE_MASK)), failure );

	if ( phys_base             >= mem_validator.phys_base
		 && (phys_base + size) >= mem_validator.phys_base
		 && phys_base          <= (mem_validator.phys_base + mem_validator.size)
		 && (phys_base + size) <= (mem_validator.phys_base + mem_validator.size) )
	{
		MALI_SUCCESS;
	}

 failure:
	MALI_PRINTF( ("*******************************************************************************\n") );
	MALI_PRINTF( ("MALI PHYSICAL RANGE VALIDATION ERROR!\n") );
	MALI_PRINTF( ("\n") );
	MALI_PRINTF( ("We failed to validate a Mali-Physical range that the user-side wished to map in\n") );
	MALI_PRINTF( ("\n") );
	MALI_PRINTF( ("It is likely that the user-side wished to do Direct Rendering, but a suitable\n") );
	MALI_PRINTF( ("address range validation mechanism has not been correctly setup\n") );
	MALI_PRINTF( ("\n") );
	MALI_PRINTF( ("The range supplied was: phys_base=0x%08X, size=0x%08X\n", phys_base, size) );
	MALI_PRINTF( ("\n") );
	MALI_PRINTF( ("Please refer to the ARM Mali Software Integration Guide for more information.\n") );
	MALI_PRINTF( ("\n") );
	MALI_PRINTF( ("*******************************************************************************\n") );

	MALI_ERROR( _MALI_OSK_ERR_FAULT );
}


_mali_osk_errcode_t _mali_kernel_core_register_resource_handler(_mali_osk_resource_type_t type, mali_kernel_resource_registrator handler)
{
	MALI_CHECK(type < RESOURCE_TYPE_COUNT, _MALI_OSK_ERR_INVALID_ARGS);
	MALI_DEBUG_ASSERT(NULL == resource_handler[type]); /* A handler for resource already exists */
	resource_handler[type] = handler;
	MALI_SUCCESS;
}

void * mali_kernel_session_manager_slot_get(struct mali_session_data * session_data, int id)
{
	MALI_DEBUG_ASSERT_POINTER(session_data);
	if(id >= SUBSYSTEMS_COUNT) { MALI_DEBUG_PRINT(3, ("mali_kernel_session_manager_slot_get: id %d out of range\n", id)); return NULL; }

	if (NULL == session_data) { MALI_DEBUG_PRINT(3, ("mali_kernel_session_manager_slot_get: got NULL session data\n")); return NULL; }
	return session_data->subsystem_data[id];
}

_mali_osk_errcode_t _mali_ukk_open(void **context)
{
	int i;
    _mali_osk_errcode_t err;
	struct mali_session_data * session_data;

	/* allocated struct to track this session */
	session_data = (struct mali_session_data *)_mali_osk_malloc(sizeof(struct mali_session_data));
    MALI_CHECK_NON_NULL(session_data, _MALI_OSK_ERR_NOMEM);

	_mali_osk_memset(session_data->subsystem_data, 0, sizeof(session_data->subsystem_data));

	/* create a response queue for this session */
	session_data->ioctl_queue = _mali_osk_notification_queue_init();
	if (NULL == session_data->ioctl_queue)
	{
		_mali_osk_free(session_data);
        MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	MALI_DEBUG_PRINT(3, ("Session starting\n"));

	/* call session_begin on all subsystems */
	for (i = 0; i < (int)SUBSYSTEMS_COUNT; ++i)
	{
		if (NULL != subsystems[i]->session_begin)
		{
			/* subsystem has a session_begin */
			err = subsystems[i]->session_begin(session_data, &session_data->subsystem_data[i], session_data->ioctl_queue);
            MALI_CHECK_GOTO(err == _MALI_OSK_ERR_OK, cleanup);
		}
	}

    *context = (void*)session_data;

	MALI_DEBUG_PRINT(3, ("Session started\n"));
	MALI_SUCCESS;

cleanup:
	MALI_DEBUG_PRINT(2, ("Session startup failed\n"));
	/* i is index of subsystem which failed session begin, all indices before that has to be ended */
	/* end subsystem sessions in the reverse order they where started in */
	for (i = i - 1; i >= 0; --i)
	{
		if (NULL != subsystems[i]->session_end) subsystems[i]->session_end(session_data, &session_data->subsystem_data[i]);
	}

	_mali_osk_notification_queue_term(session_data->ioctl_queue);
	_mali_osk_free(session_data);

	/* return what the subsystem which failed session start returned */
    MALI_ERROR(err);
}

_mali_osk_errcode_t _mali_ukk_close(void **context)
{
    int i;
	struct mali_session_data * session_data;

    MALI_CHECK_NON_NULL(context, _MALI_OSK_ERR_INVALID_ARGS);

	session_data = (struct mali_session_data *)*context;

	MALI_DEBUG_PRINT(2, ("Session ending\n"));

	/* end subsystem sessions in the reverse order they where started in */
	for (i = SUBSYSTEMS_COUNT - 1; i >= 0; --i)
    {
		if (NULL != subsystems[i]->session_end) subsystems[i]->session_end(session_data, &session_data->subsystem_data[i]);
	}

	_mali_osk_notification_queue_term(session_data->ioctl_queue);
	_mali_osk_free(session_data);

    *context = NULL;

	MALI_DEBUG_PRINT(2, ("Session has ended\n"));

    MALI_SUCCESS;
}

#if USING_MALI_PMM

_mali_osk_errcode_t mali_core_signal_power_up( mali_pmm_core_id core, mali_bool queue_only )
{
	switch( core )
	{
	case MALI_PMM_CORE_GP:
		MALI_CHECK_NO_ERROR(maligp_signal_power_up(queue_only));
		break;
#if defined USING_MALI400_L2_CACHE
	case MALI_PMM_CORE_L2:
		if( !queue_only )
		{
			/* Enable L2 cache due to power up */			
			mali_kernel_l2_cache_do_enable();

			/* Invalidate the cache on power up */
			MALI_DEBUG_PRINT(5, ("L2 Cache: Invalidate all\n"));
			MALI_CHECK_NO_ERROR(mali_kernel_l2_cache_invalidate_all());
		}
		break;
#endif
	case MALI_PMM_CORE_PP0:
		MALI_CHECK_NO_ERROR(malipp_signal_power_up(0, queue_only));
		break;
	case MALI_PMM_CORE_PP1:
		MALI_CHECK_NO_ERROR(malipp_signal_power_up(1, queue_only));
		break;
	case MALI_PMM_CORE_PP2:
		MALI_CHECK_NO_ERROR(malipp_signal_power_up(2, queue_only));
		break;
	case MALI_PMM_CORE_PP3:
		MALI_CHECK_NO_ERROR(malipp_signal_power_up(3, queue_only));
		break;
	default:
		/* Unknown core */
		MALI_DEBUG_PRINT_ERROR( ("Unknown core signalled with power up: %d\n", core) );
		MALI_ERROR( _MALI_OSK_ERR_INVALID_ARGS );
	}
	
	MALI_SUCCESS;
}
	
_mali_osk_errcode_t mali_core_signal_power_down( mali_pmm_core_id core, mali_bool immediate_only )
{
	switch( core )
	{
	case MALI_PMM_CORE_GP:
		MALI_CHECK_NO_ERROR(maligp_signal_power_down(immediate_only));
		break;
#if defined USING_MALI400_L2_CACHE
	case MALI_PMM_CORE_L2:
		/* Nothing to do */
		break;
#endif
	case MALI_PMM_CORE_PP0:
		MALI_CHECK_NO_ERROR(malipp_signal_power_down(0, immediate_only));
		break;
	case MALI_PMM_CORE_PP1:
		MALI_CHECK_NO_ERROR(malipp_signal_power_down(1, immediate_only));
		break;
	case MALI_PMM_CORE_PP2:
		MALI_CHECK_NO_ERROR(malipp_signal_power_down(2, immediate_only));
		break;
	case MALI_PMM_CORE_PP3:
		MALI_CHECK_NO_ERROR(malipp_signal_power_down(3, immediate_only));
		break;
	default:
		/* Unknown core */
		MALI_DEBUG_PRINT_ERROR( ("Unknown core signalled with power down: %d\n", core) );
		MALI_ERROR( _MALI_OSK_ERR_INVALID_ARGS );
	}
	
	MALI_SUCCESS;
}

#endif


#if MALI_STATE_TRACKING
u32 _mali_kernel_core_dump_state(char* buf, u32 size)
{
	int i, n;
	char *original_buf = buf;
	for (i = 0; i < SUBSYSTEMS_COUNT; ++i)
	{
		if (NULL != subsystems[i]->dump_state)
		{
			n = subsystems[i]->dump_state(buf, size);
			size -= n;
			buf += n;
		}
	}
#if USING_MALI_PMM
	n = mali_pmm_dump_os_thread_state(buf, size);
	size -= n;
	buf += n;
#endif
	/* Return number of bytes written to buf */
	return (u32)(buf - original_buf);
}
#endif
