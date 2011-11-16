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
#include "mali_kernel_subsystem.h"
#include "regs/mali_gp_regs.h"
#include "mali_kernel_rendercore.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#if MALI_TIMELINE_PROFILING_ENABLED
#include "mali_kernel_profiling.h"
#endif
#if defined(USING_MALI400_L2_CACHE)
#include "mali_kernel_l2_cache.h"
#endif
#if USING_MMU
#include "mali_kernel_mem_mmu.h" /* Needed for mali_kernel_mmu_force_bus_reset() */
#endif

#if defined(USING_MALI200)
#define MALI_GP_SUBSYSTEM_NAME "MaliGP2"
#define MALI_GP_CORE_TYPE      _MALI_GP2
#elif defined(USING_MALI400)
#define MALI_GP_SUBSYSTEM_NAME "Mali-400 GP"
#define MALI_GP_CORE_TYPE      _MALI_400_GP
#else
#error "No supported mali core defined"
#endif

#define GET_JOB_EMBEDDED_PTR(job) (&((job)->embedded_core_job))
#define GET_JOBGP2_PTR(job_extern) _MALI_OSK_CONTAINER_OF(job_extern, maligp_job, embedded_core_job)

/* Initialized when this subsystem is initialized. This is determined by the
 * position in subsystems[], and so the value used to initialize this is
 * determined at compile time */
static mali_kernel_subsystem_identifier mali_subsystem_gp_id = -1;

static mali_core_renderunit * last_gp_core_cookie = NULL;

/* Describing a maligp job settings */
typedef struct maligp_job
{
	/* The general job struct common for all mali cores */
	mali_core_job embedded_core_job;
	_mali_uk_gp_start_job_s user_input;

	u32 irq_status;
	u32 status_reg_on_stop;
	u32 perf_counter0;
	u32 perf_counter1;
	u32 vscl_stop_addr;
	u32 plbcl_stop_addr;
	u32 heap_current_addr;

	/* The data we will return back to the user */
	_mali_osk_notification_t *notification_obj;

	int is_stalled_waiting_for_more_memory;

	u32 active_mask;
	/* progress checking */
	u32 last_vscl;
	u32 last_plbcl;
	/* extended progress checking, only enabled when we can use one of the performance counters */
	u32 have_extended_progress_checking;
	u32 vertices;

#if defined(USING_MALI400_L2_CACHE)
	u32 perf_counter_l2_src0;
	u32 perf_counter_l2_src1;
	u32 perf_counter_l2_val0;
	u32 perf_counter_l2_val1;
#endif

#if MALI_TIMELINE_PROFILING_ENABLED
	u32 pid;
	u32 tid;
#endif
} maligp_job;

/*Functions Exposed to the General External System through
  function pointers.*/

static _mali_osk_errcode_t maligp_subsystem_startup(mali_kernel_subsystem_identifier id);
#if USING_MMU
static _mali_osk_errcode_t maligp_subsystem_mmu_connect(mali_kernel_subsystem_identifier id);
#endif
static void maligp_subsystem_terminate(mali_kernel_subsystem_identifier id);
static _mali_osk_errcode_t maligp_subsystem_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue);
static void maligp_subsystem_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot);
static _mali_osk_errcode_t maligp_subsystem_core_system_info_fill(_mali_system_info* info);
static _mali_osk_errcode_t maligp_renderunit_create(_mali_osk_resource_t * resource);
#if USING_MMU
static void maligp_subsystem_broadcast_notification(mali_core_notification_message message, u32 data);
#endif
#if MALI_STATE_TRACKING
u32 maligp_subsystem_dump_state(char *buf, u32 size);
#endif

/* Internal support functions  */
static _mali_osk_errcode_t maligp_core_version_legal( mali_core_renderunit *core );
static void maligp_raw_reset( mali_core_renderunit *core);
static void maligp_reset_hard(struct mali_core_renderunit * core);
static void maligp_reset(mali_core_renderunit *core);
static void maligp_initialize_registers_mgmt(mali_core_renderunit *core );

#ifdef DEBUG
static void maligp_print_regs(int debug_level, mali_core_renderunit *core);
#endif

/* Functions exposed to mali_core system through functionpointers
   in the subsystem struct. */
static _mali_osk_errcode_t subsystem_maligp_start_job(mali_core_job * job, mali_core_renderunit * core);
static u32 subsystem_maligp_irq_handler_upper_half(mali_core_renderunit * core);
static int subsystem_maligp_irq_handler_bottom_half(mali_core_renderunit* core);
static _mali_osk_errcode_t subsystem_maligp_get_new_job_from_user(struct mali_core_session * session, void * argument);
static _mali_osk_errcode_t subsystem_maligp_suspend_response(struct mali_core_session * session, void * argument);
static void subsystem_maligp_return_job_to_user(mali_core_job * job, mali_subsystem_job_end_code end_status);
static void subsystem_maligp_renderunit_delete(mali_core_renderunit * core);
static void subsystem_maligp_renderunit_reset_core(struct mali_core_renderunit * core, mali_core_reset_style style );
static void subsystem_maligp_renderunit_probe_core_irq_trigger(struct mali_core_renderunit* core);
static _mali_osk_errcode_t subsystem_maligp_renderunit_probe_core_irq_finished(struct mali_core_renderunit* core);
static void subsystem_maligp_renderunit_stop_bus(struct mali_core_renderunit* core);

/* Variables */
static register_address_and_value default_mgmt_regs[] =
{
	{ MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED }
};


/* This will be one of the subsystems in the array of subsystems:
	static struct mali_kernel_subsystem * subsystems[];
  found in file: mali_kernel_core.c
*/

struct mali_kernel_subsystem mali_subsystem_gp2=
{
	maligp_subsystem_startup,                   /* startup */
	maligp_subsystem_terminate,                 /* shutdown */
#if USING_MMU
	maligp_subsystem_mmu_connect,               /* load_complete */
#else
    NULL,
#endif
	maligp_subsystem_core_system_info_fill,     /* system_info_fill */
	maligp_subsystem_session_begin,             /* session_begin */
	maligp_subsystem_session_end,               /* session_end */
#if USING_MMU
	maligp_subsystem_broadcast_notification,    /* broadcast_notification */
#else
    NULL,
#endif
#if MALI_STATE_TRACKING
	maligp_subsystem_dump_state,                /* dump_state */
#endif
} ;

static mali_core_subsystem subsystem_maligp ;

static _mali_osk_errcode_t maligp_subsystem_startup(mali_kernel_subsystem_identifier id)
{
	mali_core_subsystem * subsystem;

	MALI_DEBUG_PRINT(3, ("Mali GP: maligp_subsystem_startup\n") ) ;

    mali_subsystem_gp_id = id;

    /* All values get 0 as default */
	_mali_osk_memset(&subsystem_maligp, 0, sizeof(*subsystem));

	subsystem = &subsystem_maligp;
	subsystem->start_job = &subsystem_maligp_start_job;
	subsystem->irq_handler_upper_half = &subsystem_maligp_irq_handler_upper_half;
	subsystem->irq_handler_bottom_half = &subsystem_maligp_irq_handler_bottom_half;
	subsystem->get_new_job_from_user = &subsystem_maligp_get_new_job_from_user;
	subsystem->suspend_response = &subsystem_maligp_suspend_response;
	subsystem->return_job_to_user = &subsystem_maligp_return_job_to_user;
	subsystem->renderunit_delete = &subsystem_maligp_renderunit_delete;
	subsystem->reset_core = &subsystem_maligp_renderunit_reset_core;
	subsystem->stop_bus = &subsystem_maligp_renderunit_stop_bus;
	subsystem->probe_core_irq_trigger = &subsystem_maligp_renderunit_probe_core_irq_trigger;
	subsystem->probe_core_irq_acknowledge = &subsystem_maligp_renderunit_probe_core_irq_finished;

	/* Setting variables in the general core part of the subsystem.*/
	subsystem->name = MALI_GP_SUBSYSTEM_NAME;
	subsystem->core_type = MALI_GP_CORE_TYPE;
	subsystem->id = id;

	/* Initiates the rest of the general core part of the subsystem */
    MALI_CHECK_NO_ERROR(mali_core_subsystem_init( subsystem ));

	/* This will register the function for adding MALIGP2 cores to the subsystem */
#if defined(USING_MALI200)
    MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MALIGP2, maligp_renderunit_create));
#endif
#if defined(USING_MALI400)
	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(MALI400GP, maligp_renderunit_create));
#endif

	MALI_DEBUG_PRINT(6, ("Mali GP: maligp_subsystem_startup\n") ) ;

	MALI_SUCCESS;
}

#if USING_MMU
static _mali_osk_errcode_t maligp_subsystem_mmu_connect(mali_kernel_subsystem_identifier id)
{
       mali_core_subsystem_attach_mmu(&subsystem_maligp);
       MALI_SUCCESS; /* OK */
}
#endif

static void maligp_subsystem_terminate(mali_kernel_subsystem_identifier id)
{
	MALI_DEBUG_PRINT(3, ("Mali GP: maligp_subsystem_terminate\n") ) ;
	mali_core_subsystem_cleanup(&subsystem_maligp);
}

static _mali_osk_errcode_t maligp_subsystem_session_begin(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue)
{
	mali_core_session * session;

	MALI_DEBUG_PRINT(3, ("Mali GP: maligp_subsystem_session_begin\n") ) ;
    MALI_CHECK_NON_NULL(session = _mali_osk_malloc( sizeof(*session) ), _MALI_OSK_ERR_FAULT);

	_mali_osk_memset(session, 0, sizeof(*session) );
	*slot = (mali_kernel_subsystem_session_slot)session;

	session->subsystem = &subsystem_maligp;

	session->notification_queue = queue;

#if USING_MMU
	session->mmu_session = mali_session_data;
#endif

	mali_core_session_begin(session);

	MALI_DEBUG_PRINT(6, ("Mali GP: maligp_subsystem_session_begin\n") ) ;

    MALI_SUCCESS;
}

static void maligp_subsystem_session_end(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot)
{
	mali_core_session * session;
	/** @note mali_session_data not needed here */

	MALI_DEBUG_PRINT(3, ("Mali GP: maligp_subsystem_session_end\n") ) ;
	if ( NULL==slot || NULL==*slot)
	{
		MALI_PRINT_ERROR(("Input slot==NULL"));
		return;
	}
	session = (mali_core_session *)*slot;
	mali_core_session_close(session);

	_mali_osk_free(session);
	*slot = NULL;

	MALI_DEBUG_PRINT(6, ("Mali GP: maligp_subsystem_session_end\n") ) ;
}

/**
 * We fill in info about all the cores we have
 * @param info Pointer to system info struct to update
 * @return _MALI_OSK_ERR_OK on success, or another _mali_osk_errcode_t for errors.
 */
static _mali_osk_errcode_t maligp_subsystem_core_system_info_fill(_mali_system_info* info)
{
	return mali_core_subsystem_system_info_fill(&subsystem_maligp, info);
}

static _mali_osk_errcode_t maligp_renderunit_create(_mali_osk_resource_t * resource)
{
	mali_core_renderunit *core;
	int err;

	MALI_DEBUG_PRINT(3, ("Mali GP: maligp_renderunit_create\n") ) ;
	/* Checking that the resource settings are correct */
#if defined(USING_MALI200)
	if(MALIGP2 != resource->type)
	{
		MALI_PRINT_ERROR(("Can not register this resource as a " MALI_GP_SUBSYSTEM_NAME " core."));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
#elif defined(USING_MALI400)
	if(MALI400GP != resource->type)
	{
		MALI_PRINT_ERROR(("Can not register this resource as a " MALI_GP_SUBSYSTEM_NAME " core."));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
#endif
	if ( 0 != resource->size )
	{
		MALI_PRINT_ERROR(("Memory size set to " MALI_GP_SUBSYSTEM_NAME " core should be zero."));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	if ( NULL == resource->description )
	{
		MALI_PRINT_ERROR(("A " MALI_GP_SUBSYSTEM_NAME " core needs a unique description field"));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* Create a new core object */
	core = (mali_core_renderunit*) _mali_osk_malloc(sizeof(*core));
	if ( NULL == core )
	{
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* Variables set to be able to open and register the core */
	core->subsystem             = &subsystem_maligp ;
	core->registers_base_addr   = resource->base ;
	core->size                  = MALIGP2_REGISTER_ADDRESS_SPACE_SIZE ;
	core->description           = resource->description;
	core->irq_nr                = resource->irq ;
#if USING_MMU
	core->mmu_id                = resource->mmu_id;
	core->mmu                   = NULL;
#endif
#if USING_MALI_PMM
	/* Set up core's PMM id */
	core->pmm_id = MALI_PMM_CORE_GP;
#endif

	err = mali_core_renderunit_init( core );
    if (_MALI_OSK_ERR_OK != err)
    {
		MALI_DEBUG_PRINT(1, ("Failed to initialize renderunit\n"));
		goto exit_on_error0;
    }

	/* Map the new core object, setting: core->registers_mapped  */
	err = mali_core_renderunit_map_registers(core);
	if (_MALI_OSK_ERR_OK != err) goto exit_on_error1;

	/* Check that the register mapping of the core works.
	Return 0 if maligp core is present and accessible. */
	if (mali_benchmark) {
		core->core_version = MALI_GP_PRODUCT_ID << 16;
	} else {
		core->core_version = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_VERSION);
	}

	err = maligp_core_version_legal(core);
	if (_MALI_OSK_ERR_OK != err) goto exit_on_error2;

	/* Reset the core. Put the core into a state where it can start to render. */
	maligp_reset(core);

	/* Registering IRQ, init the work_queue_irq_handle */
	/* Adding this core as an available renderunit in the subsystem. */
	err = mali_core_subsystem_register_renderunit(&subsystem_maligp, core);
	if (_MALI_OSK_ERR_OK != err) goto exit_on_error2;

#ifdef DEBUG
	MALI_DEBUG_PRINT(4, ("Mali GP: Initial Register settings:\n"));
	maligp_print_regs(4, core);
#endif

	MALI_DEBUG_PRINT(6, ("Mali GP: maligp_renderunit_create\n") ) ;

	MALI_SUCCESS;

exit_on_error2:
	mali_core_renderunit_unmap_registers(core);
exit_on_error1:
    mali_core_renderunit_term(core);
exit_on_error0:
	_mali_osk_free( core ) ;
	MALI_PRINT_ERROR(("Renderunit NOT created."));
    MALI_ERROR((_mali_osk_errcode_t)err);
}

#if USING_MMU
/* Used currently only for signalling when MMU has a pagefault */
static void maligp_subsystem_broadcast_notification(mali_core_notification_message message, u32 data)
{
	mali_core_subsystem_broadcast_notification(&subsystem_maligp, message, data);
}
#endif

#ifdef DEBUG
static void maligp_print_regs(int debug_level, mali_core_renderunit *core)
{
	if (debug_level <= mali_debug_level)
	{
		MALI_DEBUG_PRINT(1, ("  VS 0x%08X 0x%08X, PLBU 0x%08X 0x%08X ALLOC 0x%08X 0x%08X\n",
			mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR),
			mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_VSCL_END_ADDR),
			mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBUCL_START_ADDR),
			mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBUCL_END_ADDR),
			mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR),
			mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR))
		);
		MALI_DEBUG_PRINT(1, ("  IntRaw 0x%08X  IntMask 0x%08X, Status 0x%02X  Ver: 0x%08X \n",
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_INT_MASK),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_STATUS),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_VERSION)));

		MALI_DEBUG_PRINT(1, ("  PERF_CNT Enbl:%d %d Src: %02d %02d  VAL: 0x%08X 0x%08X\n",
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_ENABLE),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_ENABLE),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_SRC),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_SRC),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_VALUE),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE)));

		MALI_DEBUG_PRINT(1, ("  VS_START 0x%08X PLBU_START 0x%08X  AXI_ERR 0x%08X\n",
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR_READ),
				mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBCL_START_ADDR_READ),
				mali_core_renderunit_register_read(core, MALIGP2_CONTR_AXI_BUS_ERROR_STAT)));
	}
}
#endif

static _mali_osk_errcode_t maligp_core_version_legal( mali_core_renderunit *core )
{
	u32 mali_type;

	mali_type = core->core_version >> 16;

#if defined(USING_MALI400)
	if (  MALI400_GP_PRODUCT_ID != mali_type && MALI300_GP_PRODUCT_ID != mali_type )
#else
	if (  MALI_GP_PRODUCT_ID != mali_type )
#endif
	{
		MALI_PRINT_ERROR(("Error: reading this from maligp version register: 0x%x\n", core->core_version));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
	MALI_DEBUG_PRINT(3, ("Mali GP: core_version_legal: Reads correct mali version: %d\n", core->core_version )) ;
    MALI_SUCCESS;
}

static void subsystem_maligp_renderunit_stop_bus(struct mali_core_renderunit* core)
{
	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_STOP_BUS);
}

static void maligp_reset( mali_core_renderunit *core )
{
	if (!mali_benchmark) {
		maligp_raw_reset(core);
		maligp_initialize_registers_mgmt(core);
	}
}


static void maligp_reset_hard( mali_core_renderunit *core )
{
	const int reset_finished_loop_count = 15;
	const u32 reset_wait_target_register = MALIGP2_REG_ADDR_MGMT_WRITE_BOUND_LOW;
	const u32 reset_invalid_value = 0xC0FFE000;
	const u32 reset_check_value = 0xC01A0000;
	const u32 reset_default_value = 0;
	int i;

	mali_core_renderunit_register_write(core, reset_wait_target_register, reset_invalid_value);

	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_RESET);

	for (i = 0; i < reset_finished_loop_count; i++)
	{
		mali_core_renderunit_register_write(core, reset_wait_target_register, reset_check_value);
		if (reset_check_value == mali_core_renderunit_register_read(core, reset_wait_target_register))
		{
			MALI_DEBUG_PRINT(5, ("Reset loop exiting after %d iterations\n", i));
			break;
		}
	}

	if (i == reset_finished_loop_count)
	{
		MALI_DEBUG_PRINT(1, ("The reset loop didn't work\n"));
	}

	mali_core_renderunit_register_write(core, reset_wait_target_register, reset_default_value); /* set it back to the default */
	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);
	

}

static void maligp_raw_reset( mali_core_renderunit *core )
{
	int i;
	const int request_loop_count = 20;

	MALI_DEBUG_PRINT(4, ("Mali GP: maligp_raw_reset: %s\n", core->description)) ;
	if (mali_benchmark) return;

	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_MASK, 0); /* disable the IRQs */

#if defined(USING_MALI200)

	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_STOP_BUS);

	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_STATUS) & MALIGP2_REG_VAL_STATUS_BUS_STOPPED) break;
		_mali_osk_time_ubusydelay(10);
	}

	MALI_DEBUG_PRINT_IF(1, request_loop_count == i, ("Mali GP: Bus was never stopped during core reset\n"));

	if (request_loop_count==i)
	{
		/* Could not stop bus connections from core, probably because some of the already pending
		bus request has had a page fault, and therefore can not complete before the MMU does PageFault
		handling. This can be treated as a heavier reset function - which unfortunately reset all
		the cores on this MMU in addition to the MMU itself */
#if USING_MMU
		if ((NULL!=core->mmu) && (MALI_FALSE == core->error_recovery))
		{
			MALI_DEBUG_PRINT(1, ("Mali GP: Forcing MMU bus reset\n"));
			mali_kernel_mmu_force_bus_reset(core->mmu);
			return;
		}
#endif
		MALI_PRINT(("A MMU reset did not allow GP to stop its bus, system failure, unable to recover\n"));
		return;
	}

	/* the bus was stopped OK, complete the reset */
	/* use the hard reset routine to do the actual reset */
	maligp_reset_hard(core);

#elif defined(USING_MALI400)

	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALI400GP_REG_VAL_IRQ_RESET_COMPLETED);
	mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_CMD, MALI400GP_REG_VAL_CMD_SOFT_RESET);

	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT) & /*Bitwise OR*/
				MALI400GP_REG_VAL_IRQ_RESET_COMPLETED) break;
		_mali_osk_time_ubusydelay(10);
	}

	if ( request_loop_count==i )
	{
#if USING_MMU
		/* Could not stop bus connections from core, probably because some of the already pending
		   bus request has had a page fault, and therefore can not complete before the MMU does PageFault
		   handling. This can be treated as a heavier reset function - which unfortunately reset all
		   the cores on this MMU in addition to the MMU itself */
		if ((NULL!=core->mmu) && (MALI_FALSE == core->error_recovery))
		{
			MALI_DEBUG_PRINT(1, ("Mali GP: Forcing Bus reset\n"));
			mali_kernel_mmu_force_bus_reset(core->mmu);
			return;
		}
#endif
		MALI_PRINT(("A MMU reset did not allow GP to stop its bus, system failure, unable to recover\n"));
	}
	else
	{
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);
	}

#else
#error "no supported mali core defined"
#endif
}

/* Sets the registers on maligp according to the const default_mgmt_regs array. */
static void maligp_initialize_registers_mgmt(mali_core_renderunit *core )
{
	int i;

	MALI_DEBUG_PRINT(6, ("Mali GP: maligp_initialize_registers_mgmt: %s\n", core->description)) ;
	for(i=0 ; i< (sizeof(default_mgmt_regs)/sizeof(*default_mgmt_regs)) ; ++i)
	{
		mali_core_renderunit_register_write(core, default_mgmt_regs[i].address, default_mgmt_regs[i].value);
	}
}


/* Start this job on this core. Return MALI_TRUE if the job was started. */
static _mali_osk_errcode_t subsystem_maligp_start_job(mali_core_job * job, mali_core_renderunit * core)
{
	maligp_job 	*jobgp;
	u32 startcmd;
	/* The local extended version of the general structs */
	jobgp  = _MALI_OSK_CONTAINER_OF(job, maligp_job, embedded_core_job);

	startcmd = 0;
	if ( jobgp->user_input.frame_registers[0] != jobgp->user_input.frame_registers[1] )
	{
		startcmd |= (u32) MALIGP2_REG_VAL_CMD_START_VS;
	}

	if ( jobgp->user_input.frame_registers[2] != jobgp->user_input.frame_registers[3] )
	{
		startcmd |= (u32) MALIGP2_REG_VAL_CMD_START_PLBU;
	}

	if(0 == startcmd)
	{
		MALI_DEBUG_PRINT(4, ("Mali GP: Job: 0x%08x  WILL NOT START SINCE JOB HAS ILLEGAL ADDRESSES\n",
				(u32)jobgp->user_input.user_job_ptr));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}


#ifdef DEBUG
	MALI_DEBUG_PRINT(4, ("Mali GP: Registers Start\n"));
	maligp_print_regs(4, core);
#endif


	mali_core_renderunit_register_write_array(
			core,
			MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR,
			&(jobgp->user_input.frame_registers[0]),
			sizeof(jobgp->user_input.frame_registers)/sizeof(jobgp->user_input.frame_registers[0]));

	/* This selects which performance counters we are reading */
	if ( 0 != jobgp->user_input.perf_counter_flag )
	{
		if ( jobgp->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE)
		{
			mali_core_renderunit_register_write(
					core,
					MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_SRC,
					jobgp->user_input.perf_counter_src0);

			mali_core_renderunit_register_write(
					core,
					MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_ENABLE,
					MALIGP2_REG_VAL_PERF_CNT_ENABLE);
		}

		if ( jobgp->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE)
		{
			mali_core_renderunit_register_write(
					core,
					MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_SRC,
					jobgp->user_input.perf_counter_src1);

			mali_core_renderunit_register_write(
					core,
					MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_ENABLE,
					MALIGP2_REG_VAL_PERF_CNT_ENABLE);
		}

#if defined(USING_MALI400_L2_CACHE)
		if ( jobgp->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE) )
		{
			int force_reset = ( jobgp->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_L2_RESET ) ? 1 : 0;
			u32 src0 = 0;
			u32 src1 = 0;

			if ( jobgp->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE )
			{
				src0 = jobgp->user_input.perf_counter_l2_src0;
			}
			if ( jobgp->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE )
			{
				src1 = jobgp->user_input.perf_counter_l2_src1;
			}

			mali_kernel_l2_cache_set_perf_counters(src0, src1, force_reset); /* will activate and possibly reset counters */

			/* Now, retrieve the current values, so we can substract them when the job has completed */
			mali_kernel_l2_cache_get_perf_counters(&jobgp->perf_counter_l2_src0,
			                                       &jobgp->perf_counter_l2_val0,
			                                       &jobgp->perf_counter_l2_src1,
			                                       &jobgp->perf_counter_l2_val1);
		}
#endif
	}

	if ( 0 == (jobgp->user_input.perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE))
	{
		/* extended progress checking can be enabled */

		jobgp->have_extended_progress_checking = 1;

		mali_core_renderunit_register_write(
				core,
		MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_SRC,
		MALIGP2_REG_VAL_PERF_CNT1_SRC_NUMBER_OF_VERTICES_PROCESSED
										   );

		mali_core_renderunit_register_write(
				core,
		MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_ENABLE,
		MALIGP2_REG_VAL_PERF_CNT_ENABLE);
	}

	subsystem_flush_mapped_mem_cache();

	MALI_DEBUG_PRINT(4, ("Mali GP: STARTING GP WITH CMD: 0x%x\n", startcmd));
#if MALI_STATE_TRACKING
	 _mali_osk_atomic_inc(&job->session->jobs_started);
#endif

	/* This is the command that starts the Core */
	mali_core_renderunit_register_write(core,
										MALIGP2_REG_ADDR_MGMT_CMD,
										startcmd);

#if MALI_TIMELINE_PROFILING_ENABLED
	_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), jobgp->pid, jobgp->tid, 0, 0, 0);
#endif

	MALI_SUCCESS;
}

/* Check if given core has an interrupt pending. Return MALI_TRUE and set mask to 0 if pending */

static u32 subsystem_maligp_irq_handler_upper_half(mali_core_renderunit * core)
{
	u32 irq_readout;

	if (mali_benchmark) {
		return (core->current_job ? 1 : 0); /* simulate irq is pending when a job is pending */
	}

	irq_readout = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_INT_STAT);

	MALI_DEBUG_PRINT(5, ("Mali GP: IRQ: %04x\n", irq_readout)) ;

	if ( MALIGP2_REG_VAL_IRQ_MASK_NONE != irq_readout )
	{
		/* Mask out all IRQs from this core until IRQ is handled */
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_MASK	, MALIGP2_REG_VAL_IRQ_MASK_NONE);
		/* We do need to handle this in a bottom half, return 1 */
		return 1;
	}
	return 0;
}

/* This function should check if the interrupt indicates that job was finished.
If so it should update the job-struct, reset the core registers, and return MALI_TRUE, .
If the job is still working after this function it should return MALI_FALSE.
The function must also enable the bits in the interrupt mask for the core.
Called by the bottom half interrupt function. */
static int subsystem_maligp_irq_handler_bottom_half(mali_core_renderunit* core)
{
	mali_core_job * job;
	maligp_job * jobgp;
	u32 irq_readout;
	u32 core_status;
	u32 vscl;
	u32 plbcl;

	job = core->current_job;

	if (mali_benchmark) {
		MALI_DEBUG_PRINT(3, ("MaliGP: Job: Benchmark\n") );
		irq_readout = MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST | MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST;
		core_status = 0;
	} else {
		irq_readout = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT) & MALIGP2_REG_VAL_IRQ_MASK_USED;
		core_status = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_STATUS);
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

	jobgp = GET_JOBGP2_PTR(job);

	jobgp->heap_current_addr = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR);

	vscl = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR);
	plbcl = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBUCL_START_ADDR);

	MALI_DEBUG_PRINT(3, ("Mali GP: Job: 0x%08x  IRQ RECEIVED  Rawstat: 0x%x Status: 0x%x\n",
			(u32)jobgp->user_input.user_job_ptr, irq_readout , core_status )) ;

	jobgp->irq_status |= irq_readout;
	jobgp->status_reg_on_stop = core_status;

	if ( 0 != jobgp->is_stalled_waiting_for_more_memory )
	{
#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status? */
#endif

		/* Readback the performance counters */
		if (jobgp->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE) )
		{
			jobgp->perf_counter0 = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_VALUE);
			jobgp->perf_counter1 = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
		}

#if defined(USING_MALI400_L2_CACHE)
		if (jobgp->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE) )
		{
			u32 src0;
			u32 val0;
			u32 src1;
			u32 val1;
			mali_kernel_l2_cache_get_perf_counters(&src0, &val0, &src1, &val1);

			if (jobgp->perf_counter_l2_src0 == src0)
			{
				jobgp->perf_counter_l2_val0 = val0 - jobgp->perf_counter_l2_val0;
			}
			else
			{
				jobgp->perf_counter_l2_val0 = 0;
			}

			if (jobgp->perf_counter_l2_src1 == src1)
			{
				jobgp->perf_counter_l2_val1 = val1 - jobgp->perf_counter_l2_val1;
			}
			else
			{
				jobgp->perf_counter_l2_val1 = 0;
			}
		}
#endif

		MALI_DEBUG_PRINT(2, ("Mali GP: Job aborted - userspace would not provide more heap memory.\n"));
#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif
		return JOB_STATUS_END_OOM; /* Core is ready for more jobs.*/
	}
	/* finished ? */
	else if (0 == (core_status & MALIGP2_REG_VAL_STATUS_MASK_ACTIVE))
	{
#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status? */
#endif

#ifdef DEBUG
		MALI_DEBUG_PRINT(4, ("Mali GP: Registers On job end:\n"));
		maligp_print_regs(4, core);
#endif
		MALI_DEBUG_PRINT_IF(5, irq_readout & 0x04, ("OOM when done, ignoring (reg.current = 0x%x, reg.end = 0x%x)\n",
		           (void*)mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR),
		           (void*)mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR))
		          );


		if (0 != jobgp->user_input.perf_counter_flag )
		{
			/* Readback the performance counters */
			if (jobgp->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE) )
			{
				jobgp->perf_counter0 = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_VALUE);
				jobgp->perf_counter1 = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
			}

#if defined(USING_MALI400_L2_CACHE)
			if (jobgp->user_input.perf_counter_flag & (_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC0_ENABLE|_MALI_PERFORMANCE_COUNTER_FLAG_L2_SRC1_ENABLE) )
			{
				u32 src0;
				u32 val0;
				u32 src1;
				u32 val1;
				mali_kernel_l2_cache_get_perf_counters(&src0, &val0, &src1, &val1);

				if (jobgp->perf_counter_l2_src0 == src0)
				{
					jobgp->perf_counter_l2_val0 = val0 - jobgp->perf_counter_l2_val0;
				}
				else
				{
					jobgp->perf_counter_l2_val0 = 0;
				}

				if (jobgp->perf_counter_l2_src1 == src1)
				{
					jobgp->perf_counter_l2_val1 = val1 - jobgp->perf_counter_l2_val1;
				}
				else
				{
					jobgp->perf_counter_l2_val1 = 0;
				}
			}
#endif
		}

		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);

#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif
		return JOB_STATUS_END_SUCCESS; /* core idle */
	}
	/* sw watchdog timeout handling or time to do hang checking ? */
	else if (
	         (CORE_WATCHDOG_TIMEOUT == core->state) ||
	         (
	          (CORE_HANG_CHECK_TIMEOUT == core->state) &&
              (
	           (jobgp->have_extended_progress_checking ? (mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE) == jobgp->vertices) : 1/*TRUE*/) &&
	           ((core_status & MALIGP2_REG_VAL_STATUS_VS_ACTIVE) ? (vscl == jobgp->last_vscl) : 1/*TRUE*/) &&
	           ((core_status & MALIGP2_REG_VAL_STATUS_PLBU_ACTIVE) ? (plbcl == jobgp->last_plbcl) : 1/*TRUE*/)
	          )
	         )
	        )
	{
		/* no progress detected, killed by the watchdog */

#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status? */
#endif

		MALI_DEBUG_PRINT(1, ("Mali GP: SW-Timeout. Regs:\n"));
		if (core_status & MALIGP2_REG_VAL_STATUS_VS_ACTIVE) MALI_DEBUG_PRINT(1, ("vscl current = 0x%x last = 0x%x\n", (void*)vscl, (void*)jobgp->last_vscl));
		if (core_status & MALIGP2_REG_VAL_STATUS_PLBU_ACTIVE) MALI_DEBUG_PRINT(1, ("plbcl current = 0x%x last = 0x%x\n", (void*)plbcl, (void*)jobgp->last_plbcl));
		if (jobgp->have_extended_progress_checking) MALI_DEBUG_PRINT(1, ("vertices processed = %d, last = %d\n", mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE),
		jobgp->vertices));
#ifdef DEBUG
		maligp_print_regs(2, core);
#endif

#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif

		return JOB_STATUS_END_HANG;
	}
	/* if hang timeout checking was enabled and we detected progress, will be fall down to this check */
	/* check for PLBU OOM before the hang check to avoid the race condition of the hw wd trigging while waiting for us to handle the OOM interrupt */
	else if ( 0 != (irq_readout & MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM))
	{
		mali_core_session *session;
		_mali_osk_notification_t *notific;
		_mali_uk_gp_job_suspended_s * suspended_job;

#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SUSPEND|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status? */
#endif

		session = job->session;

		MALI_DEBUG_PRINT(4, ("OOM, new heap requested by GP\n"));
		MALI_DEBUG_PRINT(4, ("Status when OOM: current = 0x%x, end = 0x%x\n",
		         (void*)mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR),
		         (void*)mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR))
		       );

		notific = _mali_osk_notification_create(

		                                       _MALI_NOTIFICATION_GP_STALLED,
		                                       sizeof( _mali_uk_gp_job_suspended_s )
		                                      );
        if ( NULL == notific)
		{
			MALI_PRINT_ERROR( ("Mali GP: Could not get notification object\n")) ;
			return JOB_STATUS_END_OOM; /* Core is ready for more jobs.*/
		}

		core->state = CORE_WORKING;
		jobgp->is_stalled_waiting_for_more_memory = 1;
		suspended_job = (_mali_uk_gp_job_suspended_s *)notific->result_buffer; /* this is ok - result_buffer was malloc'd */

		suspended_job->user_job_ptr = jobgp->user_input.user_job_ptr;
		suspended_job->reason = _MALIGP_JOB_SUSPENDED_OUT_OF_MEMORY ;
		suspended_job->cookie = (u32) core;
		last_gp_core_cookie = core;

		_mali_osk_notification_queue_send( session->notification_queue, notific);

#ifdef DEBUG
		maligp_print_regs(4, core);
#endif

		/* stop all active timers */
		_mali_osk_timer_del( core->timer);
		_mali_osk_timer_del( core->timer_hang_detection);
		MALI_DEBUG_PRINT(4, ("Mali GP: PLBU heap empty, sending memory request to userspace\n"));
		/* save to watchdog_jiffies what was remaining WD timeout value when OOM was triggered */
		job->watchdog_jiffies = (long)job->watchdog_jiffies - (long)_mali_osk_time_tickcount();
		/* reuse core->timer as the userspace response timeout handler */
		_mali_osk_timer_add( core->timer, _mali_osk_time_mstoticks(1000) ); /* wait max 1 sec for userspace to respond */
		return JOB_STATUS_CONTINUE_RUN; /* The core is NOT available for new jobs. */
	}
	/* hw watchdog is reporting a new hang or an existing progress-during-hang check passed? */
	else if	((CORE_HANG_CHECK_TIMEOUT == core->state) || (irq_readout & jobgp->active_mask & MALIGP2_REG_VAL_IRQ_HANG))
	{
		/* check interval in ms */
		u32 timeout = mali_core_hang_check_timeout_get();
		MALI_DEBUG_PRINT(3, ("Mali GP: HW/SW Watchdog triggered, checking for progress in %d ms\n", timeout));
		core->state = CORE_WORKING;

		/* save state for the progress checking */
		jobgp->last_vscl = vscl;
		jobgp->last_plbcl = plbcl;
		if (jobgp->have_extended_progress_checking)
		{
			jobgp->vertices = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
		}
        /* hw watchdog triggered, set up a progress checker every HANGCHECK ms */
        _mali_osk_timer_add( core->timer_hang_detection, _mali_osk_time_mstoticks(timeout));
		jobgp->active_mask &= ~MALIGP2_REG_VAL_IRQ_HANG; /* ignore the hw watchdog from now on */
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, irq_readout);
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_MASK, jobgp->active_mask);
		return JOB_STATUS_CONTINUE_RUN; /* not finihsed */ }
	/* no errors, but still working */
	else if ( ( 0 == (core_status & MALIGP2_REG_VAL_STATUS_MASK_ERROR)) &&
	          ( 0 != (core_status & MALIGP2_REG_VAL_STATUS_MASK_ACTIVE ))
	        )
	{
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, irq_readout);
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_MASK, jobgp->active_mask);
		return JOB_STATUS_CONTINUE_RUN;
	}
	/* Else there must be some error */
	else
	{
#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), 0, 0, 0, 0, 0); /* add GP and L2 counters and return status? */
#endif

		MALI_DEBUG_PRINT(1, ("Mali GP: Core crashed? *IRQ: 0x%x Status: 0x%x\n", irq_readout, core_status ));
		#ifdef DEBUG
		MALI_DEBUG_PRINT(1, ("Mali GP: Registers Before reset:\n"));
		maligp_print_regs(1, core);
		#endif
#if MALI_STATE_TRACKING
		_mali_osk_atomic_inc(&job->session->jobs_ended);
#endif
		return JOB_STATUS_END_UNKNOWN_ERR;
	}
}


/* This function is called from the ioctl function and should return a mali_core_job pointer
to a created mali_core_job object with the data given from userspace */
static _mali_osk_errcode_t subsystem_maligp_get_new_job_from_user(struct mali_core_session * session, void * argument)
{
	maligp_job *jobgp;
	mali_core_job *job = NULL;
	mali_core_job *previous_replaced_job;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;
	_mali_uk_gp_start_job_s * user_ptr_job_input;

	user_ptr_job_input = (_mali_uk_gp_start_job_s *)argument;

    MALI_CHECK_NON_NULL(jobgp = (maligp_job *) _mali_osk_calloc(1, sizeof(maligp_job)), _MALI_OSK_ERR_FAULT);

	/* Copy the job data from the U/K interface */
	if ( NULL == _mali_osk_memcpy(&jobgp->user_input, user_ptr_job_input, sizeof(_mali_uk_gp_start_job_s) ) )
	{
		MALI_PRINT_ERROR( ("Mali GP: Could not copy data from U/K interface.\n")) ;
        err = _MALI_OSK_ERR_FAULT;
		goto function_exit;
	}

	MALI_DEBUG_PRINT(3, ("Mali GP: subsystem_maligp_get_new_job_from_user 0x%x\n", (void*)jobgp->user_input.user_job_ptr));

	MALI_DEBUG_PRINT(3, ("Mali GP: Job Regs: 0x%08X 0x%08X, 0x%08X 0x%08X 0x%08X 0x%08X\n",
			jobgp->user_input.frame_registers[0],
			jobgp->user_input.frame_registers[1],
			jobgp->user_input.frame_registers[2],
			jobgp->user_input.frame_registers[3],
			jobgp->user_input.frame_registers[4],
			jobgp->user_input.frame_registers[5])  );


	job = GET_JOB_EMBEDDED_PTR(jobgp);

	job->session = session;
	job_priority_set(job, jobgp->user_input.priority);
	job_watchdog_set(job, jobgp->user_input.watchdog_msecs );
	jobgp->heap_current_addr = jobgp->user_input.frame_registers[4];

	job->abort_id = jobgp->user_input.abort_id;

	jobgp->is_stalled_waiting_for_more_memory = 0;

#if MALI_TIMELINE_PROFILING_ENABLED
	jobgp->pid = _mali_osk_get_pid();
	jobgp->tid = _mali_osk_get_tid();
#endif

	if (NULL != session->job_waiting_to_run)
	{
		/* IF NOT( newjow HAS HIGHER PRIORITY THAN waitingjob) EXIT_NOT_START new job */
		if(!job_has_higher_priority(job, session->job_waiting_to_run))
		{
			/* The job we try to add does NOT have higher pri than current */
			/* Cause jobgp to free: */
			user_ptr_job_input->status = _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE;
			goto function_exit;
		}
	}

	/* We now know that we have a job, and a slot to put it in */

	jobgp->active_mask = MALIGP2_REG_VAL_IRQ_MASK_USED;

	/* Allocating User Return Data */
	jobgp->notification_obj = _mali_osk_notification_create(
			_MALI_NOTIFICATION_GP_FINISHED,
			sizeof(_mali_uk_gp_job_finished_s) );

	if ( NULL == jobgp->notification_obj )
	{
		MALI_PRINT_ERROR( ("Mali GP: Could not get notification_obj.\n")) ;
		err = _MALI_OSK_ERR_NOMEM;
		goto function_exit;
	}

	_MALI_OSK_INIT_LIST_HEAD( &(job->list) ) ;

	MALI_DEBUG_PRINT(4, ("Mali GP: Job: 0x%08x INPUT from user.\n", (u32)jobgp->user_input.user_job_ptr)) ;

	/* This should not happen since we have the checking of priority above */
	err = mali_core_session_add_job(session, job, &previous_replaced_job);
	if ( _MALI_OSK_ERR_OK != err )
	{
		MALI_PRINT_ERROR( ("Mali GP: Internal error\n")) ;
		/* Cause jobgp to free: */
		user_ptr_job_input->status = _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE;
		_mali_osk_notification_delete( jobgp->notification_obj );
		goto function_exit;
	}

	/* If MALI_TRUE: This session had a job with lower priority which were removed.
	This replaced job is given back to userspace. */
	if ( NULL != previous_replaced_job )
	{
		maligp_job *previous_replaced_jobgp;

		previous_replaced_jobgp = GET_JOBGP2_PTR(previous_replaced_job);

		MALI_DEBUG_PRINT(4, ("Mali GP: Replacing job: 0x%08x\n", (u32)previous_replaced_jobgp->user_input.user_job_ptr)) ;

		/* Copy to the input data (which also is output data) the
		pointer to the job that were replaced, so that the userspace
		driver can put this job in the front of its job-queue */
		user_ptr_job_input->returned_user_job_ptr = previous_replaced_jobgp->user_input.user_job_ptr;

		/** @note failure to 'copy to user' at this point must not free jobgp,
		 * and so no transaction rollback required in the U/K interface */

		/* This does not cause jobgp to free: */
		user_ptr_job_input->status = _MALI_UK_START_JOB_STARTED_LOW_PRI_JOB_RETURNED;
		MALI_DEBUG_PRINT(5, ("subsystem_maligp_get_new_job_from_user: Job added, prev returned\n")) ;
	}
	else
	{
		/* This does not cause jobgp to free: */
		user_ptr_job_input->status = _MALI_UK_START_JOB_STARTED;
		MALI_DEBUG_PRINT(5, ("subsystem_maligp_get_new_job_from_user: Job added\n")) ;
	}

function_exit:
	if ( _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE == user_ptr_job_input->status
		|| _MALI_OSK_ERR_OK != err )
	{
		_mali_osk_free(jobgp);
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


static _mali_osk_errcode_t subsystem_maligp_suspend_response(struct mali_core_session * session, void * argument)
{
	mali_core_renderunit *core;
	maligp_job *jobgp;
	mali_core_job *job;

	_mali_uk_gp_suspend_response_s * suspend_response;

	MALI_DEBUG_PRINT(5, ("subsystem_maligp_suspend_response\n"));

	suspend_response = (_mali_uk_gp_suspend_response_s *)argument;

	/* We read job data from User */
	/* On a single mali_gp system we can only have one Stalled GP,
	and therefore one stalled request with a cookie. This checks
	that we get the correct cookie */
	if ( last_gp_core_cookie != (mali_core_renderunit *)suspend_response->cookie )
	{
		MALI_DEBUG_PRINT(2, ("Mali GP: Got an illegal cookie from Userspace.\n")) ;
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
	core = (mali_core_renderunit *)suspend_response->cookie;
	last_gp_core_cookie = NULL;
	job = core->current_job;
	jobgp = GET_JOBGP2_PTR(job);

	switch( suspend_response->code )
	{
		case _MALIGP_JOB_RESUME_WITH_NEW_HEAP :
			MALI_DEBUG_PRINT(5, ("MALIGP_JOB_RESUME_WITH_NEW_HEAP jiffies: %li\n", _mali_osk_time_tickcount()));
			MALI_DEBUG_PRINT(4, ("New Heap addr 0x%08x - 0x%08x\n", suspend_response->arguments[0], suspend_response->arguments[1]));

			jobgp->is_stalled_waiting_for_more_memory = 0;
			job->watchdog_jiffies += _mali_osk_time_tickcount(); /* convert to absolute time again */
			_mali_osk_timer_mod( core->timer, job->watchdog_jiffies); /* update the timer */


			mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, (MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM | MALIGP2_REG_VAL_IRQ_HANG));
			mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_MASK, jobgp->active_mask);
			mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR, suspend_response->arguments[0]);
			mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR, suspend_response->arguments[1]);
			mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_UPDATE_PLBU_ALLOC);

#if MALI_TIMELINE_PROFILING_ENABLED
			_mali_profiling_add_event(MALI_PROFILING_EVENT_TYPE_RESUME|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(core->core_number), 0, 0, 0, 0, 0);
#endif

			MALI_DEBUG_PRINT(4, ("GP resumed with new heap\n"));

			break;

		case _MALIGP_JOB_ABORT:
			MALI_DEBUG_PRINT(3, ("MALIGP_JOB_ABORT on heap extend request\n"));
			_mali_osk_irq_schedulework( core->irq );
			break;

		default:
			MALI_PRINT_ERROR(("Wrong Suspend response from userspace\n"));
	}
    MALI_SUCCESS;
}

/* This function is called from the ioctl function and should write the necessary data
to userspace telling which job was finished and the status and debuginfo for this job.
The function must also free and cleanup the input job object. */
static void subsystem_maligp_return_job_to_user( mali_core_job * job, mali_subsystem_job_end_code end_status )
{
	maligp_job 	*jobgp;
	_mali_uk_gp_job_finished_s * job_out;
	_mali_uk_gp_start_job_s* job_input;
	mali_core_session *session;


	jobgp  = _MALI_OSK_CONTAINER_OF(job, maligp_job, embedded_core_job);
	job_out =  (_mali_uk_gp_job_finished_s *)jobgp->notification_obj->result_buffer; /* OK - this should've been malloc'd */
	job_input= &(jobgp->user_input);
	session = job->session;

	MALI_DEBUG_PRINT(5, ("Mali GP: Job: 0x%08x OUTPUT to user. Runtime: %d ms, irq readout %x\n",
			(u32)jobgp->user_input.user_job_ptr,
			job->render_time_msecs,
		   	jobgp->irq_status)) ;

	_mali_osk_memset(job_out, 0 , sizeof(_mali_uk_gp_job_finished_s));

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

	job_out->irq_status = jobgp->irq_status;
	job_out->status_reg_on_stop = jobgp->status_reg_on_stop;
	job_out->vscl_stop_addr  = 0;
	job_out->plbcl_stop_addr = 0;
	job_out->heap_current_addr  = jobgp->heap_current_addr;
	job_out->perf_counter0 = jobgp->perf_counter0;
	job_out->perf_counter1 = jobgp->perf_counter1;
	job_out->perf_counter_src0 = jobgp->user_input.perf_counter_src0 ;
	job_out->perf_counter_src1 = jobgp->user_input.perf_counter_src1 ;
	job_out->render_time = job->render_time_msecs;
#if defined(USING_MALI400_L2_CACHE)
	job_out->perf_counter_l2_src0 = jobgp->perf_counter_l2_src0;
	job_out->perf_counter_l2_src1 = jobgp->perf_counter_l2_src1;
	job_out->perf_counter_l2_val0 = jobgp->perf_counter_l2_val0;
	job_out->perf_counter_l2_val1 = jobgp->perf_counter_l2_val1;
#endif

#if MALI_STATE_TRACKING
	_mali_osk_atomic_inc(&session->jobs_returned);
#endif
	_mali_osk_notification_queue_send( session->notification_queue, jobgp->notification_obj);
	jobgp->notification_obj = NULL;

	_mali_osk_free(jobgp);

	last_gp_core_cookie = NULL;
}

static void subsystem_maligp_renderunit_delete(mali_core_renderunit * core)
{
	MALI_DEBUG_PRINT(5, ("Mali GP: maligp_renderunit_delete\n"));
	_mali_osk_free(core);
}

static void subsystem_maligp_renderunit_reset_core(struct mali_core_renderunit * core, mali_core_reset_style style)
{
	MALI_DEBUG_PRINT(5, ("Mali GP: renderunit_reset_core\n"));

	switch (style)
	{
		case MALI_CORE_RESET_STYLE_RUNABLE:
			maligp_reset(core);
			break;
		case MALI_CORE_RESET_STYLE_DISABLE:
			maligp_raw_reset(core); /* do the raw reset */
			mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_MASK, 0); /* then disable the IRQs */
			break;
		case MALI_CORE_RESET_STYLE_HARD:
			maligp_reset_hard(core);
			maligp_initialize_registers_mgmt(core);
			break;
		default:
			MALI_DEBUG_PRINT(1, ("Unknown reset type %d\n", style));
			break;
	}
}

static void subsystem_maligp_renderunit_probe_core_irq_trigger(struct mali_core_renderunit* core)
{
    mali_core_renderunit_register_write(core , MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);
	mali_core_renderunit_register_write(core , MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT, MALIGP2_REG_VAL_CMD_FORCE_HANG );
	_mali_osk_mem_barrier();
}

static _mali_osk_errcode_t subsystem_maligp_renderunit_probe_core_irq_finished(struct mali_core_renderunit* core)
{
	u32 irq_readout;

	irq_readout = mali_core_renderunit_register_read(core, MALIGP2_REG_ADDR_MGMT_INT_STAT);

	if ( MALIGP2_REG_VAL_IRQ_FORCE_HANG & irq_readout )
	{
		mali_core_renderunit_register_write(core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_FORCE_HANG);
		_mali_osk_mem_barrier();
		MALI_SUCCESS;
	}

    MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

_mali_osk_errcode_t _mali_ukk_gp_start_job( _mali_uk_gp_start_job_s *args )
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_gp_id);
    MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
    return mali_core_subsystem_ioctl_start_job(session, args);
}

_mali_osk_errcode_t _mali_ukk_get_gp_number_of_cores( _mali_uk_get_gp_number_of_cores_s *args )
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_gp_id);
    MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
    return mali_core_subsystem_ioctl_number_of_cores_get(session, &args->number_of_cores);
}

_mali_osk_errcode_t _mali_ukk_get_gp_core_version( _mali_uk_get_gp_core_version_s *args )
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_gp_id);
    MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
    return mali_core_subsystem_ioctl_core_version_get(session, &args->version);
}

_mali_osk_errcode_t _mali_ukk_gp_suspend_response( _mali_uk_gp_suspend_response_s *args )
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_gp_id);
    MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_FAULT);
    return mali_core_subsystem_ioctl_suspend_response(session, args);
}

void _mali_ukk_gp_abort_job( _mali_uk_gp_abort_job_s * args)
{
	mali_core_session * session;
    MALI_DEBUG_ASSERT_POINTER(args);
    if (NULL == args->ctx) return;
    session = (mali_core_session *)mali_kernel_session_manager_slot_get(args->ctx, mali_subsystem_gp_id);
    if (NULL == session) return;
    mali_core_subsystem_ioctl_abort_job(session, args->abort_id);

}

#if USING_MALI_PMM

_mali_osk_errcode_t maligp_signal_power_up( mali_bool queue_only )
{
	MALI_DEBUG_PRINT(4, ("Mali GP: signal power up core - queue_only: %d\n", queue_only ));

	return( mali_core_subsystem_signal_power_up( &subsystem_maligp, 0, queue_only ) );
}
	
_mali_osk_errcode_t maligp_signal_power_down( mali_bool immediate_only )
{
	MALI_DEBUG_PRINT(4, ("Mali GP: signal power down core - immediate_only: %d\n", immediate_only ));

	return( mali_core_subsystem_signal_power_down( &subsystem_maligp, 0, immediate_only ) );
}

#endif

#if MALI_STATE_TRACKING
u32 maligp_subsystem_dump_state(char *buf, u32 size)
{
	return mali_core_renderunit_dump_state(&subsystem_maligp, buf, size);
}
#endif
