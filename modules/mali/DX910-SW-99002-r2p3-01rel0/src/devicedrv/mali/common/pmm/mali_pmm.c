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
 * @file mali_pmm.c
 * Implementation of the power management module for the kernel device driver
 */

#if USING_MALI_PMM

#include "mali_ukk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_subsystem.h"

#include "mali_pmm.h"
#include "mali_pmm_system.h"
#include "mali_pmm_state.h"
#include "mali_pmm_policy.h"
#include "mali_pmm_pmu.h"
#include "mali_platform.h"

/* Internal PMM subsystem state */
static _mali_pmm_internal_state_t *pmm_state = NULL;
/* Mali kernel subsystem id */
static mali_kernel_subsystem_identifier mali_subsystem_pmm_id = -1;

#define GET_PMM_STATE_PTR (pmm_state)

/* Internal functions */
static _mali_osk_errcode_t malipmm_create(_mali_osk_resource_t *resource);
static void pmm_event_process( void );
_mali_osk_errcode_t malipmm_irq_uhandler(void *data);
void malipmm_irq_bhandler(void *data);

/** @brief Start the PMM subsystem
 *
 * @param id Subsystem id to uniquely identify this subsystem
 * @return _MALI_OSK_ERR_OK if the system started successfully, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t malipmm_kernel_subsystem_start( mali_kernel_subsystem_identifier id );

/** @brief Perform post start up of the PMM subsystem
 *
 * Post start up includes initializing the current policy, now that the system is
 * completely started - to stop policies turning off hardware during the start up
 * 
 * @param id the unique subsystem id
 * @return _MALI_OSK_ERR_OK if the post startup was successful, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t malipmm_kernel_load_complete( mali_kernel_subsystem_identifier id );

/** @brief Terminate the PMM subsystem
 *
 * @param id the unique subsystem id
 */
void malipmm_kernel_subsystem_terminate( mali_kernel_subsystem_identifier id );

#if MALI_STATE_TRACKING
u32 malipmm_subsystem_dump_state( char *buf, u32 size );
#endif


/* This will be one of the subsystems in the array of subsystems:
	static struct mali_kernel_subsystem * subsystems[];
  found in file: mali_kernel_core.c
*/
struct mali_kernel_subsystem mali_subsystem_pmm=
{
	malipmm_kernel_subsystem_start,                     /* startup */
	malipmm_kernel_subsystem_terminate,                 /* shutdown */
	malipmm_kernel_load_complete,                       /* loaded all subsystems */
	NULL,
	NULL,
	NULL,
	NULL,
#if MALI_STATE_TRACKING
	malipmm_subsystem_dump_state,                       /* dump_state */
#endif
};

#if PMM_OS_TEST

u32 power_test_event = 0;
mali_bool power_test_flag = MALI_FALSE;
_mali_osk_timer_t *power_test_timer = NULL;

void _mali_osk_pmm_power_up_done(mali_pmm_message_data data)
{
	MALI_PRINT(("POWER TEST OS UP DONE\n"));
}

void _mali_osk_pmm_power_down_done(mali_pmm_message_data data)
{
	MALI_PRINT(("POWER TEST OS DOWN DONE\n"));
}

/**
 * Symbian OS Power Up call to the driver
 */
void power_test_callback( void *arg )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	power_test_flag = MALI_TRUE;
	_mali_osk_irq_schedulework( pmm->irq );
}

void power_test_start()
{
	power_test_timer = _mali_osk_timer_init();
	_mali_osk_timer_setcallback( power_test_timer, power_test_callback, NULL );
	
	/* First event is power down */
	power_test_event = MALI_PMM_EVENT_OS_POWER_DOWN;
	_mali_osk_timer_add( power_test_timer, 10000 );
}

mali_bool power_test_check()
{
	if( power_test_flag )
	{
		_mali_uk_pmm_message_s event = {
					NULL,
					0,
					1 };
		event.id = power_test_event;

		power_test_flag = MALI_FALSE;

		/* Send event */
		_mali_ukk_pmm_event_message( &event );

		/* Switch to next event to test */
		if( power_test_event == MALI_PMM_EVENT_OS_POWER_DOWN )
		{
			power_test_event = MALI_PMM_EVENT_OS_POWER_UP;
		}
		else
		{
			power_test_event = MALI_PMM_EVENT_OS_POWER_DOWN;
		}
		_mali_osk_timer_add( power_test_timer, 5000 );
		
		return MALI_TRUE;
	}
	
	return MALI_FALSE;
}

void power_test_end()
{
	_mali_osk_timer_del( power_test_timer );
	_mali_osk_timer_term( power_test_timer );
	power_test_timer = NULL;
}

#endif

void _mali_ukk_pmm_event_message( _mali_uk_pmm_message_s *args )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	_mali_osk_notification_t *msg;
	mali_pmm_message_t *event;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_DEBUG_ASSERT_POINTER(args);

	MALIPMM_DEBUG_PRINT( ("PMM: sending message\n") );

#if MALI_PMM_TRACE && MALI_PMM_TRACE_SENT_EVENTS
	_mali_pmm_trace_event_message( args, MALI_FALSE );
#endif

	msg = _mali_osk_notification_create( MALI_PMM_NOTIFICATION_TYPE, sizeof( mali_pmm_message_t ) );

	if( msg )
	{
		event = (mali_pmm_message_t *)msg->result_buffer;
		event->id = args->id;
		event->ts = _mali_osk_time_tickcount();
		event->data = args->data;
		
		_mali_osk_atomic_inc( &(pmm->messages_queued) );

		if( args->id > MALI_PMM_EVENT_INTERNALS )
		{
			/* Internal PMM message */
			_mali_osk_notification_queue_send( pmm->iqueue, msg );
			#if (MALI_PMM_TRACE || MALI_STATE_TRACKING)
				pmm->imessages_sent++;
			#endif
		}
		else
		{
			/* Real event */
			_mali_osk_notification_queue_send( pmm->queue, msg );
			#if (MALI_PMM_TRACE || MALI_STATE_TRACKING)
				pmm->messages_sent++;
			#endif
		}
	}
	else
	{
		MALI_PRINT_ERROR( ("PMM: Could not send message %d", args->id) );
		/* Make note of this OOM - which has caused a missed event */
		pmm->missed++;
	}
	
	/* Schedule time to look at the event or the fact we couldn't create an event */
	_mali_osk_irq_schedulework( pmm->irq );
}

mali_pmm_state _mali_pmm_state( void )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	if( pmm && (mali_subsystem_pmm_id != -1) )
	{
		return pmm->state;
	}

	/* No working subsystem yet */
	return MALI_PMM_STATE_UNAVAILABLE;
}


mali_pmm_core_mask _mali_pmm_cores_list( void )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	return pmm->cores_registered;
}

mali_pmm_core_mask _mali_pmm_cores_powered( void )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	return pmm->cores_powered;
}


_mali_osk_errcode_t _mali_pmm_list_policies(
		u32 policy_list_size,
		mali_pmm_policy *policy_list,
		u32 *policies_available )
{
	/* TBD - This is currently a stub function for basic power management */

	MALI_ERROR( _MALI_OSK_ERR_UNSUPPORTED );
}

_mali_osk_errcode_t _mali_pmm_set_policy( mali_pmm_policy policy )
{
	/* TBD - This is currently a stub function for basic power management */

/* TBD - When this is not a stub... include tracing...
#if MALI_PMM_TRACE
	_mali_pmm_trace_policy_change( old, newpolicy );
#endif
*/
	MALI_ERROR( _MALI_OSK_ERR_UNSUPPORTED );
}

_mali_osk_errcode_t _mali_pmm_get_policy( mali_pmm_policy *policy )
{
	if( policy )
	{
		_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
		MALI_DEBUG_ASSERT_POINTER(pmm);

		if( pmm )
		{
			*policy = pmm->policy;
			MALI_SUCCESS;
		}
		else
		{
			 *policy = MALI_PMM_POLICY_NONE;
			 MALI_ERROR( _MALI_OSK_ERR_FAULT );
		}
	}

	/* No return argument */
	MALI_ERROR( _MALI_OSK_ERR_INVALID_ARGS );
}

#if ( MALI_PMM_TRACE || MALI_STATE_TRACKING )

/* Event names - order must match mali_pmm_event_id enum */
static char *pmm_trace_events[] = {
	"OS_POWER_UP",
	"OS_POWER_DOWN",
	"JOB_SCHEDULED",
	"JOB_QUEUED",
	"JOB_FINISHED",
	"TIMEOUT",
};

/* State names - order must match mali_pmm_state enum */
static char *pmm_trace_state[] = {
	"UNAVAILABLE",
	"SYSTEM ON",
	"SYSTEM OFF",
	"SYSTEM TRANSITION",
};

/* Policy names - order must match mali_pmm_policy enum */
static char *pmm_trace_policy[] = {
	"NONE",
	"ALWAYS ON",
	"JOB CONTROL",
};

/* Status names - order must match mali_pmm_status enum */
static char *pmm_trace_status[] = {
	"MALI_PMM_STATUS_IDLE",                       /**< PMM is waiting next event */
	"MALI_PMM_STATUS_POLICY_POWER_DOWN",          /**< Policy initiated power down */
	"MALI_PMM_STATUS_POLICY_POWER_UP",            /**< Policy initiated power down */
	"MALI_PMM_STATUS_OS_WAITING",                 /**< PMM is waiting for OS power up */
	"MALI_PMM_STATUS_OS_POWER_DOWN",              /**< OS initiated power down */
	"MALI_PMM_STATUS_RUNTIME_IDLE_IN_PROGRESS",
	"MALI_PMM_STATUS_DVFS_PAUSE",                 /**< PMM DVFS Status Pause */
	"MALI_PMM_STATUS_OS_POWER_UP",                /**< OS initiated power up */
	"MALI_PMM_STATUS_OFF",                        /**< PMM is not active */
};

#endif /* MALI_PMM_TRACE || MALI_STATE_TRACKING */
#if MALI_PMM_TRACE

/* UK event names - order must match mali_pmm_event_id enum */
static char *pmm_trace_events_uk[] = {
	"UKS",
	"UK_EXAMPLE",
};

/* Internal event names - order must match mali_pmm_event_id enum */
static char *pmm_trace_events_internal[] = {
	"INTERNALS",
	"INTERNAL_POWER_UP_ACK",
	"INTERNAL_POWER_DOWN_ACK",
};

void _mali_pmm_trace_hardware_change( mali_pmm_core_mask old, mali_pmm_core_mask newstate )
{
	const char *dname;
	const char *cname;
	const char *ename;

	if( old != newstate )
	{
		if( newstate == 0 )
		{
			dname = "NO cores";
		}
		else
		{
			dname = pmm_trace_get_core_name( newstate );
		}
			
		/* These state checks only work if the assumption that only cores can be
		 * turned on or turned off in seperate actions is true. If core power states can
		 * be toggled (some one, some off) at the same time, this check does not work
		 */
		if( old > newstate )
		{
			/* Cores have turned off */
			cname = pmm_trace_get_core_name( old - newstate );
			ename = "OFF";
		}
		else
		{
			/* Cores have turned on */
			cname = pmm_trace_get_core_name( newstate - old );
			ename = "ON";
		}
		MALI_PRINT( ("PMM Trace: Hardware %s ON, %s just turned %s. { 0x%08x -> 0x%08x }", dname, cname, ename, old, newstate) );
	}
}

void _mali_pmm_trace_state_change( mali_pmm_state old, mali_pmm_state newstate )
{
	if( old != newstate )
	{
		MALI_PRINT( ("PMM Trace: State changed from %s to %s", pmm_trace_state[old], pmm_trace_state[newstate]) );
	}
}

void _mali_pmm_trace_policy_change( mali_pmm_policy old, mali_pmm_policy newpolicy )
{
	if( old != newpolicy )
	{
		MALI_PRINT( ("PMM Trace: Policy changed from %s to %s", pmm_trace_policy[old], pmm_trace_policy[newpolicy]) );
	}
}

void _mali_pmm_trace_event_message( mali_pmm_message_t *event, mali_bool received )
{
	const char *ename;
	const char *dname;
	const char *tname;
	const char *format = "PMM Trace: Event %s { (%d) %s, %d ticks, (0x%x) %s }";

	MALI_DEBUG_ASSERT_POINTER(event);

	tname = (received) ? "received" : "sent";

	if( event->id >= MALI_PMM_EVENT_INTERNALS )
	{
		ename = pmm_trace_events_internal[((int)event->id) - MALI_PMM_EVENT_INTERNALS];
	}
	else if( event->id >= MALI_PMM_EVENT_UKS )
	{
		ename = pmm_trace_events_uk[((int)event->id) - MALI_PMM_EVENT_UKS];
	}
	else
	{
		ename = pmm_trace_events[event->id];
	}

	switch( event->id )
	{
		case MALI_PMM_EVENT_OS_POWER_UP:
		case MALI_PMM_EVENT_OS_POWER_DOWN:
			dname = "os event";
			break;

		case MALI_PMM_EVENT_JOB_SCHEDULED:
		case MALI_PMM_EVENT_JOB_QUEUED:
		case MALI_PMM_EVENT_JOB_FINISHED:
		case MALI_PMM_EVENT_INTERNAL_POWER_UP_ACK:
		case MALI_PMM_EVENT_INTERNAL_POWER_DOWN_ACK:
			dname = pmm_trace_get_core_name( (mali_pmm_core_mask)event->data );
			break;
			
		case MALI_PMM_EVENT_TIMEOUT:
			dname = "timeout start";
			/* Print data with a different format */
			format = "PMM Trace: Event %s { (%d) %s, %d ticks, %d ticks %s }";
			break;
		default:
			dname = "unknown data";
	}

	MALI_PRINT( (format, tname, (u32)event->id, ename, event->ts, (u32)event->data, dname) );
}

#endif /* MALI_PMM_TRACE */


/****************** Mali Kernel API *****************/

_mali_osk_errcode_t malipmm_kernel_subsystem_start( mali_kernel_subsystem_identifier id )
{
	mali_subsystem_pmm_id = id;
	MALI_CHECK_NO_ERROR(_mali_kernel_core_register_resource_handler(PMU, malipmm_create));
	MALI_SUCCESS;
}

_mali_osk_errcode_t malipmm_create(_mali_osk_resource_t *resource)
{
	/* Create PMM state memory */
	MALI_DEBUG_ASSERT( pmm_state == NULL );
	pmm_state = (_mali_pmm_internal_state_t *) _mali_osk_malloc(sizeof(*pmm_state));
	MALI_CHECK_NON_NULL( pmm_state, _MALI_OSK_ERR_NOMEM );	

	/* All values get 0 as default */
	_mali_osk_memset(pmm_state, 0, sizeof(*pmm_state));

	/* Set up the initial PMM state */
	pmm_state->waiting = 0;
	pmm_state->status = MALI_PMM_STATUS_IDLE;
	pmm_state->state = MALI_PMM_STATE_UNAVAILABLE; /* Until a core registers */

	/* Set up policy via compile time option for the moment */
#if MALI_PMM_ALWAYS_ON
	pmm_state->policy = MALI_PMM_POLICY_ALWAYS_ON;
#else 
	pmm_state->policy = MALI_PMM_POLICY_JOB_CONTROL;
#endif
	
#if MALI_PMM_TRACE
	_mali_pmm_trace_policy_change( MALI_PMM_POLICY_NONE, pmm_state->policy );
#endif

	/* Set up assumes all values are initialized to NULL or MALI_FALSE, so
	 * we can exit halfway through set up and perform clean up
	 */

#if USING_MALI_PMU
        if( mali_pmm_pmu_init(resource) != _MALI_OSK_ERR_OK ) goto pmm_fail_cleanup;
        pmm_state->pmu_initialized = MALI_TRUE;
#endif
	pmm_state->queue = _mali_osk_notification_queue_init();
	if( !pmm_state->queue ) goto pmm_fail_cleanup;

	pmm_state->iqueue = _mali_osk_notification_queue_init();
	if( !pmm_state->iqueue ) goto pmm_fail_cleanup;

	/* We are creating an IRQ handler just for the worker thread it gives us */
	pmm_state->irq = _mali_osk_irq_init( _MALI_OSK_IRQ_NUMBER_PMM,
		malipmm_irq_uhandler,
		malipmm_irq_bhandler,
		NULL,
		NULL,
		(void *)pmm_state,            /* PMM state is passed to IRQ */
		"PMM handler" );

	if( !pmm_state->irq ) goto pmm_fail_cleanup;

	pmm_state->lock = _mali_osk_lock_init((_mali_osk_lock_flags_t)(_MALI_OSK_LOCKFLAG_READERWRITER | _MALI_OSK_LOCKFLAG_ORDERED), 0, 75);
	if( !pmm_state->lock ) goto pmm_fail_cleanup;

	if( _mali_osk_atomic_init( &(pmm_state->messages_queued), 0 ) != _MALI_OSK_ERR_OK )
	{
		goto pmm_fail_cleanup;
	}

	MALIPMM_DEBUG_PRINT( ("PMM: subsystem created, policy=%d\n", pmm_state->policy) );

	MALI_SUCCESS;

pmm_fail_cleanup:
	MALI_PRINT_ERROR( ("PMM: subsystem failed to be created\n") );
	if( pmm_state )
	{
		if( pmm_state->lock ) _mali_osk_lock_term( pmm_state->lock );
		if( pmm_state->irq ) _mali_osk_irq_term( pmm_state->irq );
		if( pmm_state->queue ) _mali_osk_notification_queue_term( pmm_state->queue );
		if( pmm_state->iqueue ) _mali_osk_notification_queue_term( pmm_state->iqueue );		
#if USING_MALI_PMU
                if( pmm_state->pmu_initialized )
                {
                        _mali_osk_resource_type_t t = PMU;
                        mali_pmm_pmu_deinit(&t);
                }
#endif /* USING_MALI_PMU */

		_mali_osk_free(pmm_state);
		pmm_state = NULL; 
	}
	MALI_ERROR( _MALI_OSK_ERR_FAULT );
}

_mali_osk_errcode_t malipmm_kernel_load_complete( mali_kernel_subsystem_identifier id )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	MALIPMM_DEBUG_PRINT( ("PMM: subsystem loaded, policy initializing\n") );

#if PMM_OS_TEST
	power_test_start();
#endif

	/* Initialize the profile now the system has loaded - so that cores are 
	 * not turned off during start up 
	 */
	return pmm_policy_init( pmm );
}

void malipmm_force_powerup( void )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_LOCK(pmm);
	pmm->status = MALI_PMM_STATUS_OFF;
	MALI_PMM_UNLOCK(pmm);
	
	/* flush PMM workqueue */
	_mali_osk_flush_workqueue( pmm->irq );

	if (pmm->cores_powered == 0)
	{
		malipmm_powerup(pmm->cores_registered);
	}
}

void malipmm_kernel_subsystem_terminate( mali_kernel_subsystem_identifier id )
{
	/* Check this is the right system */
	MALI_DEBUG_ASSERT( id == mali_subsystem_pmm_id );
	MALI_DEBUG_ASSERT_POINTER(pmm_state);

	if( pmm_state )
	{
#if PMM_OS_TEST
		power_test_end();
#endif
		/* Get the lock so we can shutdown */
		MALI_PMM_LOCK(pmm_state);
#if MALI_STATE_TRACKING
		pmm_state->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */
		pmm_state->status = MALI_PMM_STATUS_OFF;
#if MALI_STATE_TRACKING
		pmm_state->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */
		MALI_PMM_UNLOCK(pmm_state);
		_mali_osk_pmm_ospmm_cleanup();
		pmm_policy_term(pmm_state);
		_mali_osk_irq_term( pmm_state->irq );
		_mali_osk_notification_queue_term( pmm_state->queue );
		_mali_osk_notification_queue_term( pmm_state->iqueue );
		if (pmm_state->cores_registered) malipmm_powerdown(pmm_state->cores_registered,MALI_POWER_MODE_LIGHT_SLEEP);
#if USING_MALI_PMU
		if( pmm_state->pmu_initialized )
		{
			_mali_osk_resource_type_t t = PMU;
			mali_pmm_pmu_deinit(&t);
		}
#endif /* USING_MALI_PMU */

		_mali_osk_atomic_term( &(pmm_state->messages_queued) );
		MALI_PMM_LOCK_TERM(pmm_state);
		_mali_osk_free(pmm_state);
		pmm_state = NULL; 
	}

	MALIPMM_DEBUG_PRINT( ("PMM: subsystem terminated\n") );
}

_mali_osk_errcode_t malipmm_powerup( u32 cores )
{
        _mali_osk_errcode_t err = _MALI_OSK_ERR_OK;
        _mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;

	/* If all the cores are powered down, power up the MALI */
        if (pmm->cores_powered == 0)
        {
                mali_platform_power_mode_change(MALI_POWER_MODE_ON);
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
		/* Initiate the power up */
                _mali_osk_pmm_dev_activate();
#endif
        }

#if USING_MALI_PMU
        err = mali_pmm_pmu_powerup( cores );
#endif
        return err;
}

_mali_osk_errcode_t malipmm_powerdown( u32 cores, mali_power_mode power_mode )
{
        _mali_osk_errcode_t err = _MALI_OSK_ERR_OK;
        _mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
#if USING_MALI_PMU
        err = mali_pmm_pmu_powerdown( cores );
#endif

	/* If all cores are powered down, power off the MALI */
        if (pmm->cores_powered == 0)
        {
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
		/* Initiate the power down */
                _mali_osk_pmm_dev_idle();
#endif
                mali_platform_power_mode_change(power_mode);
        }
        return err;
}

_mali_osk_errcode_t malipmm_core_register( mali_pmm_core_id core )
{
	_mali_osk_errcode_t err;
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;

	if( pmm == NULL )
	{
		/* PMM state has not been created, this is because the PMU resource has not been 
		 * created yet.
		 * This probably means that the PMU resource has not been specfied as the first 
		 * resource in the config file
		 */
		MALI_PRINT_ERROR( ("PMM: Cannot register core %s because the PMU resource has not been\n initialized. Please make sure the PMU resource is the first resource in the\n resource configuration.\n", 
							pmm_trace_get_core_name(core)) );
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	MALI_PMM_LOCK(pmm);

#if MALI_STATE_TRACKING
	pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */


	/* Check if the core is registered more than once in PMM */
	MALI_DEBUG_ASSERT( (pmm->cores_registered & core) == 0 );

	MALIPMM_DEBUG_PRINT( ("PMM: core registered: (0x%x) %s\n", core, pmm_trace_get_core_name(core)) );

#if !MALI_PMM_NO_PMU
	/* Make sure the core is powered up */
	err = malipmm_powerup( core );
#else
	err = _MALI_OSK_ERR_OK;
#endif
	if( _MALI_OSK_ERR_OK == err )
	{
#if MALI_PMM_TRACE
		mali_pmm_core_mask old_power = pmm->cores_powered;
#endif
		/* Assume a registered core is now powered up and idle */
		pmm->cores_registered |= core;
		pmm->cores_idle |= core;
		pmm->cores_powered |= core;
		pmm_update_system_state( pmm );

#if MALI_PMM_TRACE
		_mali_pmm_trace_hardware_change( old_power, pmm->cores_powered );
#endif		
	}
	else
	{
		MALI_PRINT_ERROR( ("PMM: Error(%d) powering up registered core: (0x%x) %s\n", 
								err, core, pmm_trace_get_core_name(core)) );
	}

#if MALI_STATE_TRACKING
	pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

	MALI_PMM_UNLOCK(pmm);
	
	return err;
}

void malipmm_core_unregister( mali_pmm_core_id core )
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	MALI_PMM_LOCK(pmm);
#if MALI_STATE_TRACKING
	pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */


	/* Check if the core is registered in PMM */
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, core );

	MALIPMM_DEBUG_PRINT( ("PMM: core unregistered: (0x%x) %s\n", core, pmm_trace_get_core_name(core)) );

	{
#if MALI_PMM_TRACE
		mali_pmm_core_mask old_power = pmm->cores_powered;
#endif
		/* Remove the core from the system */
		pmm->cores_idle &= (~core);
		pmm->cores_powered &= (~core);
		pmm->cores_pend_down &= (~core);
		pmm->cores_pend_up &= (~core);
		pmm->cores_ack_down &= (~core);
		pmm->cores_ack_up &= (~core);

		pmm_update_system_state( pmm );

#if MALI_PMM_TRACE
		_mali_pmm_trace_hardware_change( old_power, pmm->cores_powered );
#endif		
	}

#if MALI_STATE_TRACKING
	pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

	MALI_PMM_UNLOCK(pmm);
}
void malipmm_core_power_down_okay( mali_pmm_core_id core )
{
	_mali_uk_pmm_message_s event = {
		NULL,
		MALI_PMM_EVENT_INTERNAL_POWER_DOWN_ACK,
		0 };

	event.data = core;

	_mali_ukk_pmm_event_message( &event );
}

void malipmm_set_policy_check()
{
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	pmm->check_policy = MALI_TRUE;

	/* To check the policy we need to schedule some work */
	_mali_osk_irq_schedulework( pmm->irq );
}

_mali_osk_errcode_t malipmm_irq_uhandler(void *data)
{
	MALIPMM_DEBUG_PRINT( ("PMM: uhandler - not expected to be used\n") );

	MALI_SUCCESS;
}

void malipmm_irq_bhandler(void *data)
{
	_mali_pmm_internal_state_t *pmm;
	pmm = (_mali_pmm_internal_state_t *)data;
	MALI_DEBUG_ASSERT_POINTER(pmm);

#if PMM_OS_TEST
	if( power_test_check() ) return;
#endif

	MALI_PMM_LOCK(pmm);
#if MALI_STATE_TRACKING
	pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */

	/* Quick out when we are shutting down */
	if( pmm->status == MALI_PMM_STATUS_OFF )
	{

	#if MALI_STATE_TRACKING
		pmm->mali_pmm_lock_acquired = 0;
	#endif /* MALI_STATE_TRACKING */

		MALI_PMM_UNLOCK(pmm);
		return;
	}

	MALIPMM_DEBUG_PRINT( ("PMM: bhandler - Processing event\n") );

	if( pmm->missed > 0 )
	{
		MALI_PRINT_ERROR( ("PMM: Failed to send %d events", pmm->missed) );
		pmm_fatal_reset( pmm );
	}
	
	if( pmm->check_policy )
	{
		pmm->check_policy = MALI_FALSE;
		pmm_policy_check_policy(pmm);
	}
	else
	{
		/* Perform event processing */
		pmm_event_process();
		if( pmm->fatal_power_err )
		{
			/* Try a reset */
			pmm_fatal_reset( pmm );
		}
	}

#if MALI_STATE_TRACKING
	pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

	MALI_PMM_UNLOCK(pmm);
}

static void pmm_event_process( void )
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;
	_mali_osk_notification_t *msg = NULL;
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;
	mali_pmm_message_t *event;
	u32 process_messages;

	MALI_DEBUG_ASSERT_POINTER(pmm);


	/* Max number of messages to process before exiting - as we shouldn't stay
	 * processing the messages for a long time
	 */
	process_messages = _mali_osk_atomic_read( &(pmm->messages_queued) );

	while( process_messages > 0 )
	{
		/* Check internal message queue first */
		err = _mali_osk_notification_queue_dequeue( pmm->iqueue, &msg );

		if( err != _MALI_OSK_ERR_OK )
		{
			if( pmm->status == MALI_PMM_STATUS_IDLE || pmm->status == MALI_PMM_STATUS_OS_WAITING || pmm->status == MALI_PMM_STATUS_DVFS_PAUSE) 	
			{
				if( pmm->waiting > 0 ) pmm->waiting--;

				/* We aren't busy changing state, so look at real events */
				err = _mali_osk_notification_queue_dequeue( pmm->queue, &msg );

				if( err != _MALI_OSK_ERR_OK )
				{
					pmm->no_events++;
					MALIPMM_DEBUG_PRINT( ("PMM: event_process - No message to process\n") );
					/* Nothing to do - so return */
					return;
				}
				else
				{
					#if (MALI_PMM_TRACE || MALI_STATE_TRACKING)
						pmm->messages_received++;
					#endif
				}
			}
			else
			{
				/* Waiting for an internal message */
				pmm->waiting++;
				MALIPMM_DEBUG_PRINT( ("PMM: event_process - Waiting for internal message, messages queued=%d\n", pmm->waiting) );
				return;
			}
		}
		else
		{
			#if (MALI_PMM_TRACE || MALI_STATE_TRACKING)
				pmm->imessages_received++;
			#endif
		}

		MALI_DEBUG_ASSERT_POINTER( msg );
		/* Check the message type matches */
		MALI_DEBUG_ASSERT( msg->notification_type == MALI_PMM_NOTIFICATION_TYPE );

		event = msg->result_buffer;

		_mali_osk_atomic_dec( &(pmm->messages_queued) );
		process_messages--;

		#if MALI_PMM_TRACE
			/* Trace before we process the event in case we have an error */
			_mali_pmm_trace_event_message( event, MALI_TRUE );
		#endif
		err = pmm_policy_process( pmm, event );

		
		if( err != _MALI_OSK_ERR_OK )
		{
			MALI_PRINT_ERROR( ("PMM: Error(%d) in policy %d when processing event message with id: %d", 
					err, pmm->policy, event->id) );
		}
		
		/* Delete notification */
		_mali_osk_notification_delete ( msg );

		if( pmm->fatal_power_err )
		{
			/* Nothing good has happened - exit */
			return;
		}

			
		#if MALI_PMM_TRACE
			MALI_PRINT( ("PMM Trace: Event processed, msgs (sent/read) = %d/%d, int msgs (sent/read) = %d/%d, no events = %d, waiting = %d\n", 
					pmm->messages_sent, pmm->messages_received, pmm->imessages_sent, pmm->imessages_received, pmm->no_events, pmm->waiting) );
		#endif
	}

	if( pmm->status == MALI_PMM_STATUS_IDLE && pmm->waiting > 0 )
	{
		/* For events we ignored whilst we were busy, add a new
		 * scheduled time to look at them */
		_mali_osk_irq_schedulework( pmm->irq );
	}
}

#if MALI_STATE_TRACKING
u32 malipmm_subsystem_dump_state(char *buf, u32 size)
{
	int len = 0;
	_mali_pmm_internal_state_t *pmm = GET_PMM_STATE_PTR;

	if( !pmm )
	{
		len += _mali_osk_snprintf(buf + len, size + len, "PMM: Null state\n");
	}
	else
	{
		len += _mali_osk_snprintf(buf+len, size+len, "Locks:\n  PMM lock acquired: %s\n",
				pmm->mali_pmm_lock_acquired ? "true" : "false");
		len += _mali_osk_snprintf(buf+len, size+len,
				"PMM state:\n  Previous status: %s\n  Status: %s\n  Current event: %s\n  Policy: %s\n  Check policy: %s\n  State: %s\n",
				pmm_trace_status[pmm->mali_last_pmm_status], pmm_trace_status[pmm->status],
				pmm_trace_events[pmm->mali_new_event_status], pmm_trace_policy[pmm->policy],
				pmm->check_policy ? "true" : "false", pmm_trace_state[pmm->state]);
		len += _mali_osk_snprintf(buf+len, size+len,
				"PMM cores:\n  Cores registered: %d\n  Cores powered: %d\n  Cores idle: %d\n"
				"  Cores pending down: %d\n  Cores pending up: %d\n  Cores ack down: %d\n  Cores ack up: %d\n",
				pmm->cores_registered, pmm->cores_powered, pmm->cores_idle, pmm->cores_pend_down,
				pmm->cores_pend_up, pmm->cores_ack_down, pmm->cores_ack_up);
		len += _mali_osk_snprintf(buf+len, size+len, "PMM misc:\n  PMU init: %s\n  Messages queued: %d\n"
				"  Waiting: %d\n  No events: %d\n  Missed events: %d\n  Fatal power error: %s\n",
				pmm->pmu_initialized ? "true" : "false", _mali_osk_atomic_read(&(pmm->messages_queued)),
				pmm->waiting, pmm->no_events, pmm->missed, pmm->fatal_power_err ? "true" : "false");
	}
	return len;
}
#endif /* MALI_STATE_TRACKING */

#endif /* USING_MALI_PMM */
