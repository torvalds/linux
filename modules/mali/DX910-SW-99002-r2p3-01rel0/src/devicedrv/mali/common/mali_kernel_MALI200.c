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
#include "mali_osk.h"
#include "mali_kernel_pp.h"
#include "mali_kernel_subsystem.h"
#include "mali_kernel_core.h"
#include "regs/mali_200_regs.h"
#include "mali_kernel_rendercore.h"
#if MALI_TIMELINE_PROFILING_ENABLED
#include "mali_kernel_profiling.h"
#endif
#ifdef USING_MALI400_L2_CACHE
#include "mali_kernel_l2_cache.h"
#endif
#if USING_MMU
#include "mali_kernel_mem_mmu.h" /* Needed for mali_kernel_mmu_force_bus_reset() */
#endif

#include "mali_osk_list.h"

#if defined(USING_MALI200)
#define MALI_PP_SUBSYSTEM_NAME "Mali200"
#define MALI_PP_CORE_TYPE      _MALI_200
#elif defined(USING_MALI400)
#define MALI_PP_SUBSYSTEM_NAME "Mali-400 PP"
#define MALI_PP_CORE_TYPE      _MALI_400_PP
#else
#error "No supported mali core defined"
#endif

#define GET_JOB_EMBEDDED_PTR(job) (&((job)->embedded_core_job))
#define GET_JOB200_PTR(job_extern) _MALI_OSK_CONTAINER_OF(job_extern, mali200_job, embedded_core_job)

/* Initialized when this subsystem is initialized. This is determined by the
 * position in subsystems[], and so the value used to initialize this is
 * determined at compile time */
static mali_kernel_subsystem_identifier mali_subsystem_mali200_id = -1;

/* Describing a mali200 job settings */
typedef struct mali200_job
{
	/* The general job struct common for all mali cores */
	mali_core_job embedded_core_job;
	_mali_uk_pp_start_job_s user_input;

	u32 irq_status;
	u32 perf_counter0;
	u32 perf_counter1;
	u32 last_tile_list_addr; /* Neccessary to continue a stopped job */

	u32 active_mask;

	/* The data we will return back to the user */
	_mali_osk_notification_t *notification_obj;

#if defined(USING_MALI400_L2_CACHE)
	u32 perf_counter_l2_src0;
	u32 perf_counter_l2_src1;
	u32 perf_counter_l2_val0;
	u32 perf_counter_l2_val1;
	u32 perf_counter_l2_val0_raw;
	u32 perf_counter_l2_val1_raw;
#endif

#if MALI_TIMELINE_PROFILING_ENABLED
	u32 pid;
	u32 tid;
#endif
} mali200_job;


/*Functions Exposed to the General External System through
  funciont pointers.*/

static _mali_osk_errcode_t mali200_subsystem_startup(mali_kernel_subsystem_identifier id);
#if USING_MMU
static _mali_osk_errcode_t mali200_subsystem_mmu_connect(mali_kernel_subsystem_identifier id);
#endif
static void mali200_subsystem_terminate(mali_kernel_subsystem_identifier id);
static _mali_osk_errcode_t mali200_subsystem_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue);
static void mali200_subsystem_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot);
static _mali_osk_errcode_t mali200_subsystem_core_system_info_fill(_mali_system_info* info);
static _mali_osk_errcode_t mali200_renderunit_create(_mali_osk_resource_t * resource);
#if USING_MMU
static void mali200_subsystem_broadcast_notification(mali_core_notification_message message, u32 data);
#endif
#if MALI_STATE_TRACKING
u32 mali200_subsystem_dump_state(char *buf, u32 size);
#endif

/* Internal support functions  */
static _mali_osk_errcode_t mali200_core_version_legal( mali_core_renderunit *core );
static void mali200_reset(mali_core_renderunit *core);
static void mali200_reset_hard(struct mali_core_renderunit * core);
static void mali200_raw_reset(mali_core_renderunit * core);
static void mali200_initialize_registers_mgmt(mali_core_renderunit *core );

/* Functions exposed to mali_core system through functionpointers
   in the subsystem struct. */
static _mali_osk_errcode_t subsystem_mali200_start_job(mali_core_job * job, mali_core_renderunit * core);
static _mali_osk_errcode_t subsystem_mali200_get_new_job_from_user(struct mali_core_session * session, void * argument);
static void subsystem_mali200_return_job_to_user( mali_core_job * job, mali_subsystem_job_end_code end_status);
static void subsystem_mali200_renderunit_delete(mali_core_renderunit * core);
static void subsystem_mali200_renderunit_reset_core(struct mali_core_renderunit * core, mali_core_reset_style style);
static void subsystem_mali200_renderunit_probe_core_irq_trigger(struct mali_core_renderunit* core);
static _mali_osk_errcode_t subsystem_mali200_renderunit_probe_core_irq_finished(struct mali_core_renderunit* core);

static void subsystem_mali200_renderunit_stop_bus(struct mali_core_renderunit* core);
static u32 subsystem_mali200_irq_handler_upper_half(struct mali_core_renderunit * core);
static int subsystem_mali200_irq_handler_bottom_half(struct mali_core_renderunit* core);

/* This will be one of the subsystems in the array of subsystems:
	static struct mali_kernel_subsystem * subsystems[];
  found in file: mali_kernel_core.c
*/

struct mali_kernel_subsystem mali_subsystem_mali200=
{
	mali200_subsystem_startup,                  /* startup */
	mali200_subsystem_terminate,                /* shutdown */
#if USING_MMU
	mali200_subsystem_mmu_connect,              /* load_complete */
#else
    NULL,
#endif
	mali200_subsystem_core_system_info_fill,    /* system_info_fill */
    mali200_subsystem_session_begin,            /* session_begin */
	mali200_subsystem_session_end,              /* session_end */
#if USING_MMU
	mali200_subsystem_broadcast_notification,   /* broadcast_notification */
#else
    NULL,
#endif
#if MALI_STATE_TRACKING
	mali200_subsystem_dump_state,               /* dump_state */
#endif
} ;

static mali_core_subsystem subsystem_mali200 ;

static _mali_osk_errcode_t mali200_subsystem_startup(mali_kernel_subsystem_identifier id)
{
	mali_core_subsystem * subsystem;

	MALI_DEBUG_PRINT(3, ("Mali PP: mali200_subsystem_startup\n") ) ;

    mali_subsystem_mali200_id = id;

	/* All values get 0 as default */
	_mali_osk_memset(&subsystem_mali200, 0, sizeof(subsystem_mali200));

	subsystem = &subsystem_mali200;
	subsystem->start_job = &subsystem_mali200_start_job;
	subsystem->irq_handler_upper_half = &subsystem_mali200_irq_handler_upper_half;
	subsystem->irq_handler_bottom_half = &subsystem_mali200_irq_handler_bottom_half;
	subsystem->get_new_job_from_user = &subsystem_mali200_get_new_job_from_user;
	subsystem->return_job_to_user = &subsystem_mali200_return_job_to_user;
	subsystem->renderunit_delete = &subsystem_mali200_renderunit_delete;
	subsystem->reset_core = &subsystem_mali200_renderunit_reset_core;
	subsystem->stop_bus = &subsystem_mali200_renderunit_stop_bus;
	subsystem->probe_core_irq_trigger = &subsystem_mali200_renderunit_probe_core_irq_trigger;
	subsystem->probe_core_irq_acknowledge = &subsystem_mali200_renderunit_probe_core_irq_finished;

	/* Setting variables in the general core part of the subsystem.*/
	subsystem->name = MALI_PP_SUBSYSTEM_NAME;
	subsystem->core_type = MALI_PP_CORE_TYPE;
	subsystem->id = id;

	/* Initiates the rest of the general core part of the subsystem */
    MALI_CHECK_NO_ERROR(mali_core_subsystem_init( subsystem ));

	/* This will register the function for adding MALI200 cores to the subsystem */
#if defined(USING_MALI200)
	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MALI200, mali200_renderunit_create));
#endif
#if defined(USING_MALI400)
	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MALI400PP, mali200_renderunit_create));
#endif

	MALI_DEBUG_PRINT(6, ("Mali PP: mali200_subsystem_startup\n") ) ;

	MALI_SUCCESS;
}

#if USING_MMU
static _mali_osk_errcode_t mali200_subsystem_mmu_connect(mali_kernel_subsystem_identifier id)
{
	mali_core_subsystem_attach_mmu(&subsystem_mali200);
    MALI_SUCCESS; /* OK */
}
#endif

static void mali200_subsystem_terminate(mali_kernel_subsystem_identifier id)
{
	MALI_DEBUG_PRINT(3, ("Mali PP: mali200_subsystem_terminate\n") ) ;
	mali_core_subsystem_cleanup(&subsystem_mali200);
}

static _mali_osk_errcode_t mali200_subsystem_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue)
{
	mali_core_session * session;

	MALI_DEBUG_PRINT(3, ("Mali PP: mali200_subsystem_session_begin\n") ) ;
    MALI_CHECK_NON_NULL(session = _mali_osk_malloc( sizeof(mali_core_session) ), _MALI_OSK_ERR_NOMEM);

	_mali_osk_memset(session, 0, sizeof(*session) );
	*slot = (mali_kernel_subsystem_session_slot)session;

	session->subsystem = &subsystem_mali200;

	session->notification_queue = queue;

#if USING_MMU
	session->mmu_session = mali_session_data;
#endif

	mali_core_session_begin(session);

	MALI_DEBUG_PRINT(6, ("Mali PP: mali200_subsystem_session_begin\n") ) ;

	MALI_SUCCESS;
}

static void mali200_subsystem_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot)
{
	mali_core_session * session;

	MALI_DEBUG_PRINT(3, ("Mali PP: mali200_subsystem_session_end\n") ) ;
	if ( NULL==slot || NULL==*slot)
	{
		MALI_PRINT_ERROR(("Input slot==NULL"));
		return;
	}
	session = (mali_core_session*) *slot;
	mali_core_session_close(session);

	_mali_osk_free(session);
	*slot = NULL;

	MALI_DEBUG_PRINT(6, ("Mali PP: mali200_subsystem_session_end\n") ) ;
}

/**
 * We fill in info about all the cores we have
 * @param info Pointer to system info struct to update
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali200_subsystem_core_system_info_fill(_mali_system_info* info)
{
	return mali_core_subsystem_system_info_fill(&subsystem_mali200, info);
}


static _mali_osk_errcode_t mali200_renderunit_create(_mali_osk_resource_t * resource)
{
	mali_core_renderunit *core;
	_mali_osk_errcode_t err;

	MALI_DEBUG_PRINT(3, ("Mali PP: mali200_renderunit_create\n") ) ;
	/* Checking that the resource settings are correct */
#if defined(USING_MALI200)
	if(MALI200 != resource->type)
	{
		MALI_PRINT_ERROR(("Can not register this resource as a " MALI_PP_SUBSYSTEM_NAME " core."));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
#elif defined(USING_MALI400)
	if(MALI400PP != resource->type)
	{
		MALI_PRINT_ERROR(("Can not register this resource as a " MALI_PP_SUBSYSTEM_NAME " core."));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
#endif
	if ( 0 != resource->size )
	{
		MALI_PRINT_ERROR(("Memory size set to " MALI_PP_SUBSYSTEM_NAME " core should be zero."));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	if ( NULL == resource->description )
	{
		MALI_PRINT_ERROR(("A " MALI_PP_SUBSYSTEM_NAME " core needs a unique description field"));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* Create a new core object */
	core = (mali_core_renderunit*) _mali_osk_malloc(sizeof(*core));
	if ( NULL == core )
	{
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	/* Variables set to be able to open and register the core */
	core->subsystem = &subsystem_mali200 ;
	core->registers_base_addr 	= resource->base ;
	core->size 					= MALI200_REG_SIZEOF_REGISTER_BANK ;
	core->irq_nr 				= resource->irq ;
	core->description 			= resource->description;
#if USING_MMU
	core->mmu_id                = resource->mmu_id;
	core->mmu                   = NULL;
#endif
#if USING_MALI_PMM
	/* Set up core's PMM id */
	switch( subsystem_mali200.number_of_cores )
	{
	case 0:
		core->pmm_id = MALI_PMM_CORE_PP0;
		break;
	case 1:
		core->pmm_id = MALI_PMM_CORE_PP1;
		break;
	case 2:
		core->pmm_id = MALI_PMM_CORE_PP2;
		break;
	case 3:
		core->pmm_id = MALI_PMM_CORE_PP3;
		break;
	default:
		MALI_DEBUG_PRINT(1, ("Unknown supported core for PMM\n"));
		err = _MALI_OSK_ERR_FAULT;
		goto exit_on_error0;
	}
#endif

	err = mali_core_renderunit_init( core );
    if (_MALI_OSK_ERR_OK != err)
    {
		MALI_DEBUG_PRINT(1, ("Failed to initialize renderunit\n"));
		goto exit_on_error0;
    }

	/* Map the new core object, setting: core->registers_mapped  */
	err = mali_core_renderunit_map_registers(core);
	if (_MALI_OSK_ERR_OK != err)
	{
		MALI_DEBUG_PRINT(1, ("Failed to map register\n"));
		goto exit_on_error1;
	}

	/* Check that the register mapping of the core works.
	Return 0 if Mali PP core is present and accessible. */
	if (mali_benchmark) {
#if defined(USING_MALI200)
		core->core_version = (((u32)MALI_PP_PRODUCT_ID) << 16) | 5 /* Fake Mali200-r0p5 */;
#elif defined(USING_MALI400)
		core->core_version = (((u32)MALI_PP_PRODUCT_ID) << 16) | 0x0101 /* Fake Mali400-r1p1 */;
#else
#error "No supported mali core defined"
#endif
	} else {
		core->core_version = mali_core_renderunit_register_read(
		                     core,
		                     MALI200_REG_ADDR_MGMT_VERSION);
	}

	err = mali200_core_version_legal(core);
	if (_MALI_OSK_ERR_OK != err)
	{
		MALI_DEBUG_PRINT(1, ("Invalid core\n"));
		goto exit_on_error2;
	}

	/* Reset the core. Put the core into a state where it can start to render. */
	mali200_reset(core);

	/* Registering IRQ, init the work_queue_irq_handle */
	/* Adding this core as an available renderunit in the subsystem. */
	err = mali_core_subsystem_register_renderunit(&subsystem_mali200, core);
	if (_MALI_OSK_ERR_OK != err)
	{
		MALI_DEBUG_PRINT(1, ("Failed to register with core\n"));
		goto exit_on_error2;
	}
	MALI_DEBUG_PRINT(6, ("Mali PP: mali200_renderunit_create\n") ) ;

	MALI_SUCCESS;

exit_on_error2:
	mali_core_renderunit_unmap_registers(core);
exit_on_error1:
    mali_core_renderunit_term(core);
exit_on_error0:
	_mali_osk_free( core ) ;
	MALI_PRINT_ERROR(("Renderunit NOT created."));
    MALI_ERROR(err);
}

#if USING_MMU
/* Used currently only for signalling when MMU has a pagefault */
static void mali200_subsystem_broadcast_notification(mali_core_notification_message message, u32 data)
{
	mali_core_subsystem_broadcast_notification(&subsystem_mali200, message, data);
}
#endif

static _mali_osk_errcode_t mali200_core_version_legal( mali_core_renderunit *core )
{
	u32 mali_type;

	mali_type = core->core_version >> 16;
#if defined(USING_MALI400)
	/* Mali300 and Mali400 is compatible, accept either core. */
	if (MALI400_PP_PRODUCT_ID != mali_type && MALI300_PP_PRODUCT_ID != mali_type)
#else
	if (MALI_PP_PRODUCT_ID != mali_type)
#endif
	{
		MALI_PRINT_ERROR(("Error: reading this from " MALI_PP_SUBSYSTEM_NAME " version register: 0x%x\n", core->core_version));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
	MALI_DEBUG_PRINT(3, ("Mali PP: core_version_legal: Reads correct mali version: %d\n", mali_type) ) ;
	MALI_SUCCESS;
}

static void subsystem_mali200_renderunit_stop_bus(struct mali_core_renderunit* core)
{
	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_STOP_BUS);
}

static void mali200_raw_reset( mali_core_renderunit *core )
{
	int i;
	const int request_loop_count = 20;

	MALI_DEBUG_PRINT(4, ("Mali PP: mali200_raw_reset: %s\n", core->description));
	if (mali_benchmark) return;

	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, 0); /* disable IRQs */

#if defined(USING_MALI200)

    mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_STOP_BUS);

	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_STATUS) & MALI200_REG_VAL_STATUS_BUS_STOPPED) break;
		_mali_osk_time_ubusydelay(10);
	}

	MALI_DEBUG_PRINT_IF(1, request_loop_count == i, ("Mali PP: Bus was never stopped during core reset\n"));


	if (request_loop_count==i)
	{
#if USING_MMU
		if ((NULL!=core->mmu) && (MALI_FALSE == core->error_recovery))
		{
			/* Could not stop bus connections from core, probably because some of the already pending
			   bus request has had a page fault, and therefore can not complete before the MMU does PageFault
			   handling. This can be treated as a heavier reset function - which unfortunately reset all
			   the cores on this MMU in addition to the MMU itself */
			MALI_DEBUG_PRINT(1, ("Mali PP: Forcing Bus reset\n"));
			mali_kernel_mmu_force_bus_reset(core->mmu);
			return;
		}
#endif
		MALI_PRINT(("A MMU reset did not allow PP  to stop its bus, system failure, unable to recover\n"));
		return;
	}

	/* use the hard reset routine to do the actual reset */
	mali200_reset_hard(core);

#elif defined(USING_MALI400)

	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI400PP_REG_VAL_IRQ_RESET_COMPLETED);
	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI400PP_REG_VAL_CTRL_MGMT_SOFT_RESET);

	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT) & MALI400PP_REG_VAL_IRQ_RESET_COMPLETED) break;
		_mali_osk_time_ubusydelay(10);
	}

	if (request_loop_count==i)
	{
#if USING_MMU
		if ((NULL!=core->mmu) && (MALI_FALSE == core->error_recovery))
		{
			/* Could not stop bus connections from core, probably because some of the already pending
			   bus request has had a page fault, and therefore can not complete before the MMU does PageFault
			   handling. This can be treated as a heavier reset function - which unfortunately reset all
			   the cores on this MMU in addition to the MMU itself */
			MALI_DEBUG_PRINT(1, ("Mali PP: Forcing Bus reset\n"));
			mali_kernel_mmu_force_bus_reset(core->mmu);
			return;
		}
#endif
		MALI_PRINT(("A MMU reset did not allow PP  to stop its bus, system failure, unable to recover\n"));
		return;
	}
	else
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_MASK_ALL);

#else
#error "no supported mali core defined"
#endif
}

static void mali200_reset( mali_core_renderunit *core )
{
	if (!mali_benchmark) {
		mali200_raw_reset(core);
		mali200_initialize_registers_mgmt(core);
	}
}

/* Sets the registers on mali200 according to the const default_mgmt_regs array. */
static void mali200_initialize_registers_mgmt(mali_core_renderunit *core )
{
	MALI_DEBUG_PRINT(6, ("Mali PP: mali200_initialize_registers_mgmt: %s\n", core->description)) ;
	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);
}

/* Start this job on this core. Return MALI_TRUE if the job was started. */
static _mali_osk_errcode_t subsystem_mali200_start_job(mali_core_job * job, mali_core_renderunit * core)
{
	mali200_job 	*job200;

	/* The local extended version of the general structs */
	job200  = _MALI_OSK_CONTAINER_OF(job, mali200_job, embedded_core_job);

	if ( (0 == job200->user_input.frame_registers[0]) ||
	     (0 == job200->user_input.frame_registers[1]) )
	{
		MALI_DEBUG_PRINT(4, ("Mali PP: Job: 0x%08x  WILL NOT START SINCE JOB HAS ILLEGAL ADDRESSES\n",
				(u32)job200->user_input.user_job_ptr));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	MALI_DEBUG_PRINT(4, ("Mali PP: Job: 0x%08x  START_RENDER  Tile_list: 0x%08x\n",
			(u32)job200->user_input.user_job_ptr,
			job200->user_input.frame_registers[0]));
	MALI_DEBUG_PRINT(6, ("Mali PP:      RSW base addr: 0x%08x  Vertex base addr: 0x%08x\n",
			job200->user_input.frame_registers[1], job200->user_input.frame_registers[2]));

	/* Frame registers. Copy from mem to physical registers */
	mali_core_renderunit_register_write_array(
			core,
			MALI200_REG_ADDR_FRAME,
			&(job200->user_input.frame_registers[0]),
			MALI200_NUM_REGS_FRAME);

	/* Write Back unit 0. Copy from mem to physical registers*/
	mali_core_renderunit_register_write_array(
				core,
				MALI200_REG_ADDR_WB0,
				&(job200->user_input.wb0_registers[0]),
				MALI200_NUM_REGS_WBx);

	/* Write Back unit 1. Copy from mem to physical registers */
	mali_core_renderunit_register_write_array(
				core,
				MALI200_REG_ADDR_WB1,
				&(job200->user_input.wb1_registers[0]),
				MALI200_NUM_REGS_WBx);

	/* Write Back unit 2. Copy from mem to physical registers */
	mali_core_renderunit_register_write_array(
			core,
			MALI200_REG_ADDR_WB2,
			&(job200->user_input.wb2_registers[0]),
			MALI200_NUM_REGS_WBx);


	/* This selects which performance counters we are reading */
	if ( 0 != job200->user_input.perf_counter_flag )
	{
		if ( job200->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE)
		{
			mali_core_renderunit_register_write_relaxed(
					core,
					MALI200_REG_ADDR_MGMT_PERF_CNT_0_ENABLE,
					MALI200_REG_VAL_PERF_CNT_ENABLE);
			mali_core_renderunit_register_write_relaxed(
					core,
					MALI200_REG_ADDR_MGMT_PERF_CNT_0_SRC,
					job200->user_input.perf_counter_src0);

		}

		if ( job200->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE)
		{
			mali_core_renderunit_register_write_relaxed(
					core,
					MALI200_REG_ADDR_MGMT_PERF_CNT_1_ENABLE,
					MALI200_REG_VAL_PERF_CNT_ENABLE);
			mali_core_renderunit_register_write_relaxed(
					core,
					MALI200_REG_ADDR_MGMT_PERF_CNT_1_SRC,
					job200->user_input.perf_counter_src1);

		}

#if defined(USING_MALI400_L2_CACHE)
		if ( job200->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE) )
		{
			int force_reset = ( job200->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_L2_RESET ) ? 1 : 0;
			u32 src0 = 0;
			u32 src1 = 0;

			if ( job200->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE )
			{
				src0 = job200->user_input.perf_counter_l2_src0;
			}
			if ( job200->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE )
			{
				src1 = job200->user_input.perf_counter_l2_src1;
			}

			mali_kernel_l2_cache_set_perf_counters(src0, src1, force_reset); /* will activate and possibly reset counters */

			/* Now, retrieve the current values, so we can substract them when the job has completed */
			mali_kernel_l2_cache_get_perf_counters(&job200->perf_counter_l2_src0,
			                                       &job200->perf_counter_l2_val0,
			                                       &job200->perf_counter_l2_src1,
			                                       &job200->perf_counter_l2_val1);
		}
#endif
	}

	subsystem_flush_mapped_mem_cache();

#if MALI_STATE_TRACKING
	_mali_osk_atomic_inc(&job->session->jobs_started);
#endif

	/* This is the command that starts the Core */
	mali_core_renderunit_register_write(
			core,
			MALI200_REG_ADDR_MGMT_CTRL_MGMT,
			MALI200_REG_VAL_CTRL_MGMT_START_RENDERING);
	_mali_osk_write_mem_barrier();

#if MALI_TIMELINE_PROFILING_ENABLED
	_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE | MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_number) | MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH, job200->user_input.frame_builder_id, job200->user_input.flush_id, 0, 0, 0); 
	_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_number), job200->pid, job200->tid, 0, 0, 0);
#endif

    MALI_SUCCESS;
}

static u32 subsystem_mali200_irq_handler_upper_half(mali_core_renderunit * core)
{
	u32 irq_readout;

	if (mali_benchmark) {
		return (core->current_job ? 1 : 0); /* simulate irq is pending when a job is pending */
	}

	irq_readout = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_INT_STATUS);

	if ( MALI200_REG_VAL_IRQ_MASK_NONE != irq_readout )
	{
		/* Mask out all IRQs from this core until IRQ is handled */
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_NONE);

#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_number)|MALI_PROFILING_EVENT_REASON_SINGLE_HW_INTERRUPT, irq_readout, 0, 0, 0, 0);
#endif

		return 1;
	}
	return 0;
}

static int subsystem_mali200_irq_handler_bottom_half(struct mali_core_renderunit* core)
{
	u32 irq_readout;
	u32 current_tile_addr;
	u32 core_status;
	mali_core_job * job;
	mali200_job * job200;

	job = core->current_job;
	job200 = GET_JOB200_PTR(job);


	if (mali_benchmark) {
		irq_readout = MALI200_REG_VAL_IRQ_END_OF_FRAME;
		current_tile_addr = 0;
		core_status = 0;
	} else {
		irq_readout = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT) & MALI200_REG_VAL_IRQ_MASK_USED;
		current_tile_addr = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_CURRENT_REND_LIST_ADDR);
		core_status = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_STATUS);
	}

	if (NULL == job)
	{
		MALI_DEBUG_ASSERT(CORE_IDLE==core->state);
		if ( 0 != irq_readout )
		{
			MALI_PRINT_ERROR(("Interrupt from a core not running a job. IRQ: 0x%04x Status: 0x%04x", irq_readout, core_status));
		}
		return JOB_STATUS_END_UNKNOWN_ERR;
	}
	MALI_DEBUG_ASSERT(CORE_IDLE!=core->state);

	job200->irq_status |= irq_readout;

	MALI_DEBUG_PRINT_IF( 3, ( 0 != irq_readout ),
	            ("Mali PP: Job: 0x%08x  IRQ RECEIVED  Rawstat: 0x%x Tile_addr: 0x%x Status: 0x%x\n",
	            (u32)job200->user_input.user_job_ptr, irq_readout ,current_tile_addr ,core_status));

	if ( MALI200_REG_VAL_IRQ_END_OF_FRAME & irq_readout)
	{
#if defined(USING_MALI200)
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_FLUSH_CACHES);
#endif

		if (0 != job200->user_input.perf_counter_flag )
		{
			if (job200->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE) )
			{
				job200->perf_counter0 = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_VALUE);
				job200->perf_counter1 = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
			}

#if defined(USING_MALI400_L2_CACHE)
			if (job200->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE) )
			{
				u32 src0;
				u32 val0;
				u32 src1;
				u32 val1;
				mali_kernel_l2_cache_get_perf_counters(&src0, &val0, &src1, &val1);

				if (job200->perf_counter_l2_src0 == src0)
				{
					job200->perf_counter_l2_val0_raw = val0;
					job200->perf_counter_l2_val0 = val0 - job200->perf_counter_l2_val0;
				}
				else
				{
					job200->perf_counter_l2_val0_raw = 0;
					job200->perf_counter_l2_val0 = 0;
				}

				if (job200->perf_counter_l2_src1 == src1)
				{
					job200->perf_counter_l2_val1_raw = val1;
					job200->perf_counter_l2_val1 = val1 - job200->perf_counter_l2_val1;
				}
				else
				{
					job200->perf_counter_l2_val1_raw = 0;
					job200->perf_counter_l2_val1 = 0;
				}
			}
#endif

		}

#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_number),
				job200->perf_counter0, job200->perf_counter1,
				job200->user_input.perf_counter_src0 | (job200->user_input.perf_counter_src1 << 8)
#if defined(USING_MALI400_L2_CACHE)
				| (job200->user_input.perf_counter_l2_src0 << 16) | (job200->user_input.perf_counter_l2_src1 << 24),
				job200->perf_counter_l2_val0, job200->perf_counter_l2_val1
#else
				, 0, 0
#endif
				);
#endif


#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif
		return JOB_STATUS_END_SUCCESS; /* reschedule */
	}
	/* Overall SW watchdog timeout or (time to do hang checking and progress detected)? */
	else if (
	         (CORE_WATCHDOG_TIMEOUT == core->state) ||
	         ((CORE_HANG_CHECK_TIMEOUT == core->state) && (current_tile_addr == job200->last_tile_list_addr))
	        )
	{
#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status */
#endif
		/* no progress detected, killed by the watchdog */
		MALI_DEBUG_PRINT(2, ("M200: SW-Timeout Rawstat: 0x%x Tile_addr: 0x%x Status: 0x%x.\n", irq_readout ,current_tile_addr ,core_status) );
		/* In this case will the system outside cleanup and reset the core */

#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif

		return JOB_STATUS_END_HANG;
   	}
	/* HW watchdog triggered or an existing hang check passed? */
	else if	((CORE_HANG_CHECK_TIMEOUT == core->state) || (irq_readout & job200->active_mask & MALI200_REG_VAL_IRQ_HANG))
	{
		/* check interval in ms */
		u32 timeout = mali_core_hang_check_timeout_get();
		MALI_DEBUG_PRINT(3, ("M200: HW/SW Watchdog triggered, checking for progress in %d ms\n", timeout));
		job200->last_tile_list_addr = current_tile_addr;
		/* hw watchdog triggered, set up a progress checker every HANGCHECK ms */
		_mali_osk_timer_add(core->timer_hang_detection, _mali_osk_time_mstoticks(timeout));
		job200->active_mask &= ~MALI200_REG_VAL_IRQ_HANG; /* ignore the hw watchdoig from now on */
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_CLEAR, irq_readout & ~MALI200_REG_VAL_IRQ_HANG);
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, job200->active_mask);
		return JOB_STATUS_CONTINUE_RUN; /* not finished */
	}
	/* No irq pending, core still busy */
	else if ((0 == (irq_readout & MALI200_REG_VAL_IRQ_MASK_USED)) && ( 0 != (core_status & MALI200_REG_VAL_STATUS_RENDERING_ACTIVE)))
	{
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_CLEAR, irq_readout);
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, job200->active_mask);
		return JOB_STATUS_CONTINUE_RUN; /* Not finished */
	}
	else
	{
#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status */
#endif

		MALI_DEBUG_PRINT(1, ("Mali PP: Job: 0x%08x  CRASH?  Rawstat: 0x%x Tile_addr: 0x%x Status: 0x%x\n",
				(u32)job200->user_input.user_job_ptr, irq_readout ,current_tile_addr ,core_status) ) ;

		if (irq_readout & MALI200_REG_VAL_IRQ_BUS_ERROR)
		{
			u32 bus_error = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_BUS_ERROR_STATUS);

			MALI_DEBUG_PRINT(1, ("Bus error status: 0x%08X\n", bus_error));
			MALI_DEBUG_PRINT_IF(1, (bus_error & 0x01), ("Bus write error from id 0x%02x\n", (bus_error>>2) & 0x0F));
			MALI_DEBUG_PRINT_IF(1, (bus_error & 0x02), ("Bus read error from id 0x%02x\n", (bus_error>>6) & 0x0F));
			MALI_DEBUG_PRINT_IF(1, (0 == (bus_error & 0x03)), ("Bus error but neither read or write was set as the error reason\n"));
			(void)bus_error;
		}

#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif
		return JOB_STATUS_END_UNKNOWN_ERR; /* reschedule */
	}
}


/* This function is called from the ioctl function and should return a mali_core_job pointer
to a created mali_core_job object with the data given from userspace */
static _mali_osk_errcode_t subsystem_mali200_get_new_job_from_user(struct mali_core_session * session, void * argument)
{
	mali200_job *job200;
	mali_core_job *job = NULL;
	mali_core_job *previous_replaced_job;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;
	_mali_uk_pp_start_job_s * user_ptr_job_input;

    user_ptr_job_input = (_mali_uk_pp_start_job_s *)argument;

    MALI_CHECK_NON_NULL(job200 = (mali200_job *) _mali_osk_malloc(sizeof(mali200_job)), _MALI_OSK_ERR_NOMEM);
	_mali_osk_memset(job200, 0 , sizeof(mali200_job) );

	/* We read job data from Userspace pointer */
    if ( NULL == _mali_osk_memcpy((void*)&job200->user_input, user_ptr_job_input, sizeof(job200->user_input)) )
	{
		MALI_PRINT_ERROR( ("Mali PP: Could not copy data from U/K interface.\n")) ;
        err = _MALI_OSK_ERR_FAULT;
		goto function_exit;
	}

	MALI_DEBUG_PRINT(5, ("Mali PP: subsystem_mali200_get_new_job_from_user 0x%x\n", (void*)job200->user_input.user_job_ptr));

	MALI_DEBUG_PRINT(5, ("Mali PP: Frameregs: 0x%x 0x%x 0x%x Writeback[1] 0x%x, Pri:%d; Watchd:%d\n",
			job200->user_input.frame_registers[0], job200->user_input.frame_registers[1], job200->user_input.frame_registers[2],
			job200->user_input.wb0_registers[1], job200->user_input.priority,
			job200->user_input.watchdog_msecs));

	if ( job200->user_input.perf_counter_flag)
	{
#if defined(USING_MALI400_L2_CACHE)
		MALI_DEBUG_PRINT(5, ("Mali PP: Performance counters: flag:0x%x src0:0x%x src1:0x%x l2_src0:0x%x l2_src1:0x%x\n",
				job200->user_input.perf_counter_flag,
				job200->user_input.perf_counter_src0,
				job200->user_input.perf_counter_src1,
				job200->user_input.perf_counter_l2_src0,
				job200->user_input.perf_counter_l2_src1));
#else
		MALI_DEBUG_PRINT(5, ("Mali PP: Performance counters: flag:0x%x src0:0x%x src1:0x%x\n",
				job200->user_input.perf_counter_flag,
				job200->user_input.perf_counter_src0,
				job200->user_input.perf_counter_src1));
#endif
	}

	job = GET_JOB_EMBEDDED_PTR(job200);

	job->session = session;
	job_priority_set(job, job200->user_input.priority);
	job_watchdog_set(job, job200->user_input.watchdog_msecs );

#if MALI_TIMELINE_PROFILING_ENABLED
	job200->pid = _mali_osk_get_pid();
	job200->tid = _mali_osk_get_tid();
#endif

	job->abort_id = job200->user_input.abort_id;
	if (NULL != session->job_waiting_to_run)
	{
		/* IF NOT( newjow HAS HIGHER PRIORITY THAN waitingjob) EXIT_NOT_START newjob */
		if(!job_has_higher_priority(job, session->job_waiting_to_run))
		{
			/* The job we try to add does NOT have higher pri than current */
			user_ptr_job_input->status = _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE;
			goto function_exit;
		}
	}

	/* We now know that we has a job, and a empty session slot to put it in */

	job200->active_mask = MALI200_REG_VAL_IRQ_MASK_USED;

	/* Allocating User Return Data */
	job200->notification_obj = _mali_osk_notification_create(
			_MALI_NOTIFICATION_PP_FINISHED,
			sizeof(_mali_uk_pp_job_finished_s) );

	if ( NULL == job200->notification_obj )
	{
		MALI_PRINT_ERROR( ("Mali PP: Could not get notification_obj.\n")) ;
		err = _MALI_OSK_ERR_NOMEM;
		goto function_exit;
	}

	_MALI_OSK_INIT_LIST_HEAD( &(job->list) ) ;

	MALI_DEBUG_PRINT(4, ("Mali PP: Job: 0x%08x INPUT from user.\n", (u32)job200->user_input.user_job_ptr)) ;

	/* This should not happen since we have the checking of priority above */
	if ( _MALI_OSK_ERR_OK != mali_core_session_add_job(session, job, &previous_replaced_job))
	{
		MALI_PRINT_ERROR( ("Mali PP: Internal error\n")) ;
		user_ptr_job_input->status = _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE;
		_mali_osk_notification_delete( job200->notification_obj );
		goto function_exit;
	}

	/* If MALI_TRUE: This session had a job with lower priority which were removed.
	This replaced job is given back to userspace. */
	if ( NULL != previous_replaced_job )
	{
		mali200_job *previous_replaced_job200;

		previous_replaced_job200 = GET_JOB200_PTR(previous_replaced_job);

		MALI_DEBUG_PRINT(4, ("Mali PP: Replacing job: 0x%08x\n", (u32)previous_replaced_job200->user_input.user_job_ptr)) ;

		/* Copy to the input data (which also is output data) the
		pointer to the job that were replaced, so that the userspace
		driver can put this job in the front of its job-queue */

		user_ptr_job_input->returned_user_job_ptr = previous_replaced_job200->user_input.user_job_ptr;

		/** @note failure to 'copy to user' at this point must not free job200,
		 * and so no transaction rollback required in the U/K interface */

		/* This does not cause job200 to free: */
		user_ptr_job_input->status = _MALI_UK_START_JOB_STARTED_LOW_PRI_JOB_RETURNED;
		MALI_DEBUG_PRINT(5, ("subsystem_mali200_get_new_job_from_user: Job added, prev returned\n")) ;
	}
	else
	{
		/* This does not cause job200 to free: */
		user_ptr_job_input->status = _MALI_UK_START_JOB_STARTED;
		MALI_DEBUG_PRINT(5, ("subsystem_mali200_get_new_job_from_user: Job added\n")) ;
	}

function_exit:
	if (_MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE == user_ptr_job_input->status
		|| _MALI_OSK_ERR_OK != err )
	{
		_mali_osk_free(job200);
	}
#if MALI_STATE_TRACKING
	if (_MALI_UK_START_JOB_STARTED==user_ptr_job_input->status)
	{
		if(job)
		{
			job->job_nr=_mali_osk_atomic_inc_return(&session->jobs_received);
		}
	}
#endif

	MALI_ERROR(err);
}

/* This function is called from the ioctl function and should write the necessary data
to userspace telling which job was finished and the status and debuginfo for this job.
The function must also free and cleanup the input job object. */
static void subsystem_mali200_return_job_to_user( mali_core_job * job, mali_subsystem_job_end_code end_status)
{
	mali200_job 	*job200;
	_mali_uk_pp_job_finished_s * job_out;
	_mali_uk_pp_start_job_s * job_input;
	mali_core_session *session;

	if (NULL == job)
	{
		MALI_DEBUG_PRINT(1, ("subsystem_mali200_return_job_to_user received a NULL ptr\n"));
		return;
	}

	job200  = _MALI_OSK_CONTAINER_OF(job, mali200_job, embedded_core_job);

	if (NULL == job200->notification_obj)
	{
		MALI_DEBUG_PRINT(1, ("Found job200 with NULL notification object, abandoning userspace sending\n"));
		return;
	}

	job_out =  job200->notification_obj->result_buffer;
	job_input= &(job200->user_input);
	session = job->session;

	MALI_DEBUG_PRINT(4, ("Mali PP: Job: 0x%08x OUTPUT to user. Runtime: %dms\n",
			(u32)job200->user_input.user_job_ptr,
			job->render_time_msecs)) ;

	_mali_osk_memset(job_out, 0 , sizeof(_mali_uk_pp_job_finished_s));

	job_out->user_job_ptr = job_input->user_job_ptr;

	switch( end_status )
	{
		case JOB_STATUS_CONTINUE_RUN:
		case JOB_STATUS_END_SUCCESS:
		case JOB_STATUS_END_OOM:
		case JOB_STATUS_END_ABORT:
		case JOB_STATUS_END_TIMEOUT_SW:
		case JOB_STATUS_END_HANG:
		case JOB_STATUS_END_SEG_FAULT:
		case JOB_STATUS_END_ILLEGAL_JOB:
		case JOB_STATUS_END_UNKNOWN_ERR:
		case JOB_STATUS_END_SHUTDOWN:
		case JOB_STATUS_END_SYSTEM_UNUSABLE:
			job_out->status = (mali_subsystem_job_end_code) end_status;
			break;

		default:
			job_out->status = JOB_STATUS_END_UNKNOWN_ERR ;
	}
	job_out->irq_status = job200->irq_status;
	job_out->perf_counter0 = job200->perf_counter0;
	job_out->perf_counter1 = job200->perf_counter1;
	job_out->render_time = job->render_time_msecs;

#if defined(USING_MALI400_L2_CACHE)
	job_out->perf_counter_l2_src0 = job200->perf_counter_l2_src0;
	job_out->perf_counter_l2_src1 = job200->perf_counter_l2_src1;
	job_out->perf_counter_l2_val0 = job200->perf_counter_l2_val0;
	job_out->perf_counter_l2_val1 = job200->perf_counter_l2_val1;
	job_out->perf_counter_l2_val0_raw = job200->perf_counter_l2_val0_raw;
	job_out->perf_counter_l2_val1_raw = job200->perf_counter_l2_val1_raw;
#endif

#if MALI_STATE_TRACKING
	_mali_osk_atomic_inc(&session->jobs_returned);
#endif
	_mali_osk_notification_queue_send( session->notification_queue, job200->notification_obj);
	job200->notification_obj = NULL;

	_mali_osk_free(job200);
}

static void subsystem_mali200_renderunit_delete(mali_core_renderunit * core)
{
	MALI_DEBUG_PRINT(5, ("Mali PP: mali200_renderunit_delete\n"));
	_mali_osk_free(core);
}

static void mali200_reset_hard(struct mali_core_renderunit * core)
{
	const int reset_finished_loop_count = 15;
	const u32 reset_wait_target_register = MALI200_REG_ADDR_MGMT_WRITE_BOUNDARY_LOW;
	const u32 reset_invalid_value = 0xC0FFE000;
	const u32 reset_check_value = 0xC01A0000;
	const u32 reset_default_value = 0;
	int i;

	MALI_DEBUG_PRINT(5, ("subsystem_mali200_renderunit_reset_core_hard called for core %s\n", core->description));

	mali_core_renderunit_register_write(core, reset_wait_target_register, reset_invalid_value);

	mali_core_renderunit_register_write(
			core,
			MALI200_REG_ADDR_MGMT_CTRL_MGMT,
			MALI200_REG_VAL_CTRL_MGMT_FORCE_RESET);

	for (i = 0; i < reset_finished_loop_count; i++)
	{
		mali_core_renderunit_register_write(core, reset_wait_target_register, reset_check_value);
		if (reset_check_value == mali_core_renderunit_register_read(core, reset_wait_target_register))
		{
			MALI_DEBUG_PRINT(5, ("Reset loop exiting after %d iterations\n", i));
			break;
		}
		_mali_osk_time_ubusydelay(10);
	}

	if (i == reset_finished_loop_count)
	{
		MALI_DEBUG_PRINT(1, ("The reset loop didn't work\n"));
	}

	mali_core_renderunit_register_write(core, reset_wait_target_register, reset_default_value); /* set it back to the default */
	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_MASK_ALL);
}

static void subsystem_mali200_renderunit_reset_core(struct mali_core_renderunit * core, mali_core_reset_style style)
{
	MALI_DEBUG_PRINT(5, ("Mali PP: renderunit_reset_core\n"));

	switch (style)
	{
		case MALI_CORE_RESET_STYLE_RUNABLE:
			mali200_reset(core);
			break;
		case MALI_CORE_RESET_STYLE_DISABLE:
			mali200_raw_reset(core); /* do the raw reset */
			mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, 0); /* then disable the IRQs */
			break;
		case MALI_CORE_RESET_STYLE_HARD:
			mali200_reset_hard(core);
			break;
		default:
			MALI_DEBUG_PRINT(1, ("Unknown reset type %d\n", style));
	}
}

static void subsystem_mali200_renderunit_probe_core_irq_trigger(struct mali_core_renderunit* core)
{
	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);
	mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT, MALI200_REG_VAL_IRQ_FORCE_HANG);
	_mali_osk_mem_barrier();
}

static _mali_osk_errcode_t subsystem_mali200_renderunit_probe_core_irq_finished(struct mali_core_renderunit* core)
{
	u32 irq_readout;

	irq_readout = mali_core_renderunit_register_read(core, MALI200_REG_ADDR_MGMT_INT_STATUS);

	if ( MALI200_REG_VAL_IRQ_FORCE_HANG & irq_readout )
	{
		mali_core_renderunit_register_write(core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_FORCE_HANG);
		_mali_osk_mem_barrier();
		MALI_SUCCESS;
	}

    MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

_mali_osk_errcode_t _mali_ukk_pp_start_job( _mali_uk_pp_start_job_s *args )
{
	mali_core_session * session;
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
	session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_mali200_id);
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
	return mali_core_subsystem_ioctl_start_job(session, args);
}

_mali_osk_errcode_t _mali_ukk_get_pp_number_of_cores( _mali_uk_get_pp_number_of_cores_s *args )
{
	mali_core_session * session;
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
	session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_mali200_id);
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
	return mali_core_subsystem_ioctl_number_of_cores_get(session, &args->number_of_cores);
}

_mali_osk_errcode_t _mali_ukk_get_pp_core_version( _mali_uk_get_pp_core_version_s *args )
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_mali200_id);
    MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
    return mali_core_subsystem_ioctl_core_version_get(session, &args->version);
}

void _mali_ukk_pp_abort_job( _mali_uk_pp_abort_job_s * args)
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    if (NULL == args->ctx) return;
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_mali200_id);
    if (NULL == session) return;
    mali_core_subsystem_ioctl_abort_job(session, args->abort_id);

}

#if USING_MALI_PMM

_mali_osk_errcode_t malipp_signal_power_up( u32 core_num, mali_bool queue_only )
{
	MALI_DEBUG_PRINT(4, ("Mali PP: signal power up core: %d - queue_only: %d\n", core_num, queue_only ));

	return( mali_core_subsystem_signal_power_up( &subsystem_mali200, core_num, queue_only ) );
}
	
_mali_osk_errcode_t malipp_signal_power_down( u32 core_num, mali_bool immediate_only )
{
	MALI_DEBUG_PRINT(4, ("Mali PP: signal power down core: %d - immediate_only: %d\n", core_num, immediate_only ));

	return( mali_core_subsystem_signal_power_down( &subsystem_mali200, core_num, immediate_only ) );
}

#endif

#if MALI_STATE_TRACKING
u32 mali200_subsystem_dump_state(char *buf, u32 size)
{
	return mali_core_renderunit_dump_state(&subsystem_mali200, buf, size);
}
#endif
