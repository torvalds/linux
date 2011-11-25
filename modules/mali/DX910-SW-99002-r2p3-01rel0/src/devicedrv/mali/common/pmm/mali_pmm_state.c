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
 * @file mali_pmm_state.c
 * Implementation of the power management module internal state
 */

#if USING_MALI_PMM

#include "mali_ukk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_subsystem.h"

#include "mali_pmm.h"
#include "mali_pmm_state.h"
#include "mali_pmm_system.h"

#include "mali_kernel_core.h"
#include "mali_platform.h"

#define SIZEOF_CORES_LIST 6

/* NOTE: L2 *MUST* be first on the list so that it
 * is correctly powered on first and powered off last
 */
static mali_pmm_core_id cores_list[] = { MALI_PMM_CORE_L2,
										MALI_PMM_CORE_GP,
										MALI_PMM_CORE_PP0,
										MALI_PMM_CORE_PP1,
										MALI_PMM_CORE_PP2,
										MALI_PMM_CORE_PP3 };



void pmm_update_system_state( _mali_pmm_internal_state_t *pmm )
{
	mali_pmm_state state;

	MALI_DEBUG_ASSERT_POINTER(pmm);

	if( pmm->cores_registered == 0 )
	{
		state = MALI_PMM_STATE_UNAVAILABLE;
	}
	else if( pmm->cores_powered == 0 )
	{
		state = MALI_PMM_STATE_SYSTEM_OFF;
	}
	else if( pmm->cores_powered == pmm->cores_registered )
	{
		state = MALI_PMM_STATE_SYSTEM_ON;
	}
	else
	{
		/* Some other state where not everything is on or off */
		state = MALI_PMM_STATE_SYSTEM_TRANSITION;
	}

#if MALI_PMM_TRACE
	_mali_pmm_trace_state_change( pmm->state, state );
#endif
	pmm->state = state;
}

mali_pmm_core_mask pmm_cores_from_event_data( _mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event )
{
	mali_pmm_core_mask cores;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_DEBUG_ASSERT_POINTER(event);

	switch( event->id )
	{
	case MALI_PMM_EVENT_OS_POWER_UP:
	case MALI_PMM_EVENT_OS_POWER_DOWN:
		/* All cores - the system */
		cores = pmm->cores_registered;
		break;

	case MALI_PMM_EVENT_JOB_SCHEDULED:
	case MALI_PMM_EVENT_JOB_QUEUED:
	case MALI_PMM_EVENT_JOB_FINISHED:
	case MALI_PMM_EVENT_INTERNAL_POWER_UP_ACK:
	case MALI_PMM_EVENT_INTERNAL_POWER_DOWN_ACK:
		/* Currently the main event data is only the cores
		 * for these messages
		 */
		cores = (mali_pmm_core_mask)event->data;
		if( cores == MALI_PMM_CORE_SYSTEM )
		{
			cores = pmm->cores_registered;
		}
		else if( cores == MALI_PMM_CORE_PP_ALL )
		{
			/* Get the subset of registered PP cores */
			cores = (pmm->cores_registered & MALI_PMM_CORE_PP_ALL);
		}
		MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );
		break;

	default:
		/* Assume timeout messages - report cores still powered */
		cores = pmm->cores_powered;
		break;
	}

	return cores;
}

mali_pmm_core_mask pmm_cores_to_power_up( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores )
{
	mali_pmm_core_mask cores_subset;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );

	/* Check that cores aren't pending power down when asked for power up */
	MALI_DEBUG_ASSERT( pmm->cores_pend_down == 0 );

	cores_subset = (~(pmm->cores_powered) & cores);
	if( cores_subset != 0 )
	{
		/* There are some cores that need powering up */
		pmm->cores_pend_up = cores_subset;
	}

	return cores_subset;
}

mali_pmm_core_mask pmm_cores_to_power_down( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores, mali_bool immediate_only )
{
	mali_pmm_core_mask cores_subset;
	_mali_osk_errcode_t err;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );

	/* Check that cores aren't pending power up when asked for power down */
	MALI_DEBUG_ASSERT( pmm->cores_pend_up == 0 );

	cores_subset = (pmm->cores_powered & cores);
	if( cores_subset != 0 )
	{
		int n;
		volatile mali_pmm_core_mask *ppowered = &(pmm->cores_powered);

		/* There are some cores that need powering up, but we may
		 * need to wait until they are idle
		 */
		for( n = SIZEOF_CORES_LIST-1; n >= 0; n-- )
		{
			if( (cores_list[n] & cores_subset) != 0 )
			{
				/* Core is to be powered down */
				pmm->cores_pend_down |= cores_list[n];

				/* Can't hold the power lock, when acessing subsystem mutex via
				 * the core power call.
				 * Due to terminatation of driver requiring a subsystem mutex
				 * and then power lock held to unregister a core.
				 * This does mean that the following function could fail
				 * as the core is unregistered before we tell it to power
				 * down, but it does not matter as we are terminating
				 */
#if MALI_STATE_TRACKING
                pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

				MALI_PMM_UNLOCK(pmm);
				/* Signal the core to power down
				 * If it is busy (not idle) it will set a pending power down flag 
				 * (as long as we don't want to only immediately power down). 
				 * If it isn't busy it will move out of the idle queue right
				 * away
				 */
				err = mali_core_signal_power_down( cores_list[n], immediate_only );
				MALI_PMM_LOCK(pmm);

#if MALI_STATE_TRACKING
                pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */
			

				/* Re-read cores_subset in case it has changed */
				cores_subset = (*ppowered & cores);

				if( err == _MALI_OSK_ERR_OK )
				{
					/* We moved an idle core to the power down queue
					 * which means it is now acknowledged (if it is still 
					 * registered)
					 */
					pmm->cores_ack_down |= (cores_list[n] & cores_subset);
				}
				else
				{
					MALI_DEBUG_PRINT(1,("The error in PMM is ...%x...%x",err,*ppowered));
					MALI_DEBUG_ASSERT( err == _MALI_OSK_ERR_BUSY ||
										(err == _MALI_OSK_ERR_FAULT &&
										(*ppowered & cores_list[n]) == 0) );
					/* If we didn't move a core - it must be active, so
					 * leave it pending, so we get an acknowledgement (when
					 * not in immediate only mode)
					 * Alternatively we are shutting down and the core has
					 * been unregistered
					 */
				}
			}
		}
	}

	return cores_subset;
}

void pmm_power_down_cancel( _mali_pmm_internal_state_t *pmm )
{
	int n;
	mali_pmm_core_mask pd, ad;
	_mali_osk_errcode_t err;
	volatile mali_pmm_core_mask *pregistered;

	MALI_DEBUG_ASSERT_POINTER(pmm);

	MALIPMM_DEBUG_PRINT( ("PMM: Cancelling power down\n") );

	pd = pmm->cores_pend_down;
	ad = pmm->cores_ack_down;
	/* Clear the pending cores so that they don't move to the off
	 * queue if they haven't already
	 */
	pmm->cores_pend_down = 0;
	pmm->cores_ack_down = 0;
	pregistered = &(pmm->cores_registered);

	/* Power up all the pending power down cores - just so
	 * we make sure the system is in a known state, as a
	 * pending core might have sent an acknowledged message
	 * which hasn't been read yet.
	 */
	for( n = 0; n < SIZEOF_CORES_LIST; n++ )
	{
		if( (cores_list[n] & pd) != 0 )
		{
			/* Can't hold the power lock, when acessing subsystem mutex via
			 * the core power call.
			 * Due to terminatation of driver requiring a subsystem mutex
			 * and then power lock held to unregister a core.
			 * This does mean that the following power up function could fail
			 * as the core is unregistered before we tell it to power
			 * up, but it does not matter as we are terminating
			 */
#if MALI_STATE_TRACKING
			pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

			MALI_PMM_UNLOCK(pmm);
			/* As we are cancelling - only move the cores back to the queue - 
			 * no reset needed
			 */
			err = mali_core_signal_power_up( cores_list[n], MALI_TRUE );
			MALI_PMM_LOCK(pmm);
#if MALI_STATE_TRACKING
			pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */

			/* Update pending list with the current registered cores */
			pd &= (*pregistered);

			if( err != _MALI_OSK_ERR_OK )
			{
				MALI_DEBUG_ASSERT( (err == _MALI_OSK_ERR_BUSY && 
										((cores_list[n] & ad) == 0))  ||
										(err == _MALI_OSK_ERR_FAULT &&
										(*pregistered & cores_list[n]) == 0) );
				/* If we didn't power up a core - it must be active and 
				 * hasn't actually tried to power down - this is expected
				 * for cores that haven't acknowledged
				 * Alternatively we are shutting down and the core has
				 * been unregistered
				 */
			}
		}
	}
	/* Only used in debug builds */
	MALI_IGNORE(ad);
}


mali_bool pmm_power_down_okay( _mali_pmm_internal_state_t *pmm )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);

	return ( pmm->cores_pend_down == pmm->cores_ack_down ? MALI_TRUE : MALI_FALSE );
}

mali_bool pmm_invoke_power_down( _mali_pmm_internal_state_t *pmm, mali_power_mode power_mode )
{
	_mali_osk_errcode_t err;
	MALI_DEBUG_ASSERT_POINTER(pmm);

	/* Check that cores are pending power down during power down invoke */
	MALI_DEBUG_ASSERT( pmm->cores_pend_down != 0 );
	/* Check that cores are not pending power up during power down invoke */
	MALI_DEBUG_ASSERT( pmm->cores_pend_up == 0 );

	if( !pmm_power_down_okay( pmm ) )
	{
		MALIPMM_DEBUG_PRINT( ("PMM: Waiting for cores to go idle for power off - 0x%08x / 0x%08x\n", 
				pmm->cores_pend_down, pmm->cores_ack_down) );
		return MALI_FALSE;
	}
	else
	{
		pmm->cores_powered &= ~(pmm->cores_pend_down);
#if !MALI_PMM_NO_PMU
		err = malipmm_powerdown( pmm->cores_pend_down, power_mode);
#else
		err = _MALI_OSK_ERR_OK;
#endif
		
		if( err == _MALI_OSK_ERR_OK )
		{
#if MALI_PMM_TRACE
			mali_pmm_core_mask old_power = pmm->cores_powered;
#endif
			/* Remove powered down cores from idle and powered list */
			pmm->cores_idle &= ~(pmm->cores_pend_down);
			/* Reset pending/acknowledged status */
			pmm->cores_pend_down = 0;
			pmm->cores_ack_down = 0;
#if MALI_PMM_TRACE
			_mali_pmm_trace_hardware_change( old_power, pmm->cores_powered );
#endif
		}
		else
		{
			pmm->cores_powered |= pmm->cores_pend_down;
			MALI_PRINT_ERROR( ("PMM: Failed to get PMU to power down cores - (0x%x) %s", 
					pmm->cores_pend_down, pmm_trace_get_core_name(pmm->cores_pend_down)) );
			pmm->fatal_power_err = MALI_TRUE;
		}
	}

	return MALI_TRUE;
}


mali_bool pmm_power_up_okay( _mali_pmm_internal_state_t *pmm )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);

	return ( pmm->cores_pend_up == pmm->cores_ack_up ? MALI_TRUE : MALI_FALSE );
}


mali_bool pmm_invoke_power_up( _mali_pmm_internal_state_t *pmm )
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmm);

	/* Check that cores are pending power up during power up invoke */
	MALI_DEBUG_ASSERT( pmm->cores_pend_up != 0 );
	/* Check that cores are not pending power down during power up invoke */
	MALI_DEBUG_ASSERT( pmm->cores_pend_down == 0 );

	if( pmm_power_up_okay( pmm ) )
	{
		/* Power up has completed - sort out subsystem core status */
		
		int n;
		/* Use volatile to access, so that it is updated if any cores are unregistered */
		volatile mali_pmm_core_mask *ppendup = &(pmm->cores_pend_up);
#if MALI_PMM_TRACE
		mali_pmm_core_mask old_power = pmm->cores_powered;
#endif
		/* Move cores into idle queues */
		for( n = 0; n < SIZEOF_CORES_LIST; n++ )
		{
			if( (cores_list[n] & (*ppendup)) != 0 )
			{
				/* Can't hold the power lock, when acessing subsystem mutex via
				 * the core power call.
				 * Due to terminatation of driver requiring a subsystem mutex
				 * and then power lock held to unregister a core.
				 * This does mean that the following function could fail
				 * as the core is unregistered before we tell it to power
				 * up, but it does not matter as we are terminating
				 */
#if MALI_STATE_TRACKING
				pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

				MALI_PMM_UNLOCK(pmm);
				err = mali_core_signal_power_up( cores_list[n], MALI_FALSE );
				MALI_PMM_LOCK(pmm);

#if MALI_STATE_TRACKING
				pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */


				if( err != _MALI_OSK_ERR_OK )
				{
					MALI_DEBUG_ASSERT( (err == _MALI_OSK_ERR_FAULT &&
										(*ppendup & cores_list[n]) == 0) );
					/* We only expect this to fail when we are shutting down 
					 * and the core has been unregistered
					 */
				}
			}
		}
		/* Finished power up - add cores to idle and powered list */
		pmm->cores_powered |= (*ppendup);
		pmm->cores_idle |= (*ppendup);
		/* Reset pending/acknowledge status */
		pmm->cores_pend_up = 0;
		pmm->cores_ack_up = 0;

#if MALI_PMM_TRACE
		_mali_pmm_trace_hardware_change( old_power, pmm->cores_powered );
#endif
		return MALI_TRUE;
	}
	else
	{
#if !MALI_PMM_NO_PMU
		/* Power up must now be done */
		err = malipmm_powerup( pmm->cores_pend_up );
#else
		err = _MALI_OSK_ERR_OK;
#endif
		if( err != _MALI_OSK_ERR_OK )
		{
			MALI_PRINT_ERROR( ("PMM: Failed to get PMU to power up cores - (0x%x) %s", 
					pmm->cores_pend_up, pmm_trace_get_core_name(pmm->cores_pend_up)) );
			pmm->fatal_power_err = MALI_TRUE;
		}
		else
		{
			/* TBD - Update core status immediately rather than use event message */
			_mali_uk_pmm_message_s event = {
				NULL,
				MALI_PMM_EVENT_INTERNAL_POWER_UP_ACK,
				0 };
			/* All the cores that were pending power up, have now completed power up */
			event.data = pmm->cores_pend_up;
			_mali_ukk_pmm_event_message( &event );
			MALIPMM_DEBUG_PRINT( ("PMM: Sending ACK to power up") );
		}
	}

	/* Always return false, as we need an interrupt to acknowledge
	 * when power up is complete
	 */
	return MALI_FALSE;
}

mali_pmm_core_mask pmm_cores_set_active( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );

	pmm->cores_idle &= (~cores);
	return pmm->cores_idle;
}

mali_pmm_core_mask pmm_cores_set_idle( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );

	pmm->cores_idle |= (cores);
	return pmm->cores_idle;
}

mali_pmm_core_mask pmm_cores_set_down_ack( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );

	/* Check core is not pending a power down */
	MALI_DEBUG_ASSERT( (pmm->cores_pend_down & cores) != 0 );
	/* Check core has not acknowledged power down more than once */
	MALI_DEBUG_ASSERT( (pmm->cores_ack_down & cores) == 0 );

	pmm->cores_ack_down |= (cores);

	return pmm->cores_ack_down;
}

void pmm_fatal_reset( _mali_pmm_internal_state_t *pmm )
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;
	_mali_osk_notification_t *msg = NULL;
	mali_pmm_status status;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALIPMM_DEBUG_PRINT( ("PMM: Fatal Reset called") ); 

	MALI_DEBUG_ASSERT( pmm->status != MALI_PMM_STATUS_OFF );

	/* Reset the common status */
	pmm->waiting = 0;
	pmm->missed = 0;
	pmm->fatal_power_err = MALI_FALSE;
	pmm->no_events = 0;
	pmm->check_policy = MALI_FALSE;
	pmm->cores_pend_down = 0;
	pmm->cores_pend_up = 0;
	pmm->cores_ack_down = 0;
	pmm->cores_ack_up = 0;
	pmm->is_dvfs_active = 0; 
#if MALI_PMM_TRACE
	pmm->messages_sent = 0;
	pmm->messages_received = 0;
	pmm->imessages_sent = 0;
	pmm->imessages_received = 0;
	MALI_PRINT( ("PMM Trace: *** Fatal reset occurred ***") );
#endif

	/* Set that we are unavailable whilst resetting */
	pmm->state = MALI_PMM_STATE_UNAVAILABLE;
	status = pmm->status;
	pmm->status = MALI_PMM_STATUS_OFF;

	/* We want all cores powered */
	pmm->cores_powered = pmm->cores_registered;
	/* The cores may not be idle, but this state will be rectified later */
	pmm->cores_idle = pmm->cores_registered;
	
	/* So power on any cores that are registered */
	if( pmm->cores_registered != 0 )
	{
		int n;
		volatile mali_pmm_core_mask *pregistered = &(pmm->cores_registered);
#if !MALI_PMM_NO_PMU
		err = malipmm_powerup( pmm->cores_registered );
#endif
		if( err != _MALI_OSK_ERR_OK )
		{
			/* This is very bad as we can't even be certain the cores are now 
			 * powered up
			 */
			MALI_PRINT_ERROR( ("PMM: Failed to perform PMM reset!\n") );
			/* TBD driver exit? */
		}

		for( n = SIZEOF_CORES_LIST-1; n >= 0; n-- )
		{
			if( (cores_list[n] & (*pregistered)) != 0 )
			{
#if MALI_STATE_TRACKING
				pmm->mali_pmm_lock_acquired = 0;
#endif /* MALI_STATE_TRACKING */

				MALI_PMM_UNLOCK(pmm);
				/* Core is now active - so try putting it in the idle queue */
				err = mali_core_signal_power_up( cores_list[n], MALI_FALSE );
				MALI_PMM_LOCK(pmm);
#if MALI_STATE_TRACKING
                pmm->mali_pmm_lock_acquired = 1;
#endif /* MALI_STATE_TRACKING */

				/* We either succeeded, or we were not off anyway, or we have
				 * just be deregistered 
				 */
				MALI_DEBUG_ASSERT( (err == _MALI_OSK_ERR_OK) ||
									(err == _MALI_OSK_ERR_BUSY) ||
									(err == _MALI_OSK_ERR_FAULT && 
									(*pregistered & cores_list[n]) == 0) );
			}
		}
	}

	/* Unblock any pending OS event */
	if( status == MALI_PMM_STATUS_OS_POWER_UP )
	{
		/* Get the OS data and respond to the power up */
		_mali_osk_pmm_power_up_done( pmm_retrieve_os_event_data( pmm ) );
	}
	if( status == MALI_PMM_STATUS_OS_POWER_DOWN )
	{
		/* Get the OS data and respond to the power down 
		 * NOTE: We are not powered down at this point due to power problems,
		 * so we are lying to the system, but something bad has already 
		 * happened and we are trying unstick things
		 * TBD - Add busy loop to power down cores?
		 */
		_mali_osk_pmm_power_down_done( pmm_retrieve_os_event_data( pmm ) );
	}
		
	/* Purge the event queues */
	do
	{
		if( _mali_osk_notification_queue_dequeue( pmm->iqueue, &msg ) == _MALI_OSK_ERR_OK )
		{
			_mali_osk_notification_delete ( msg );
			break;
		}
	} while (MALI_TRUE);

	do
	{
		if( _mali_osk_notification_queue_dequeue( pmm->queue, &msg ) == _MALI_OSK_ERR_OK )
		{
			_mali_osk_notification_delete ( msg );
			break;
		}
	} while (MALI_TRUE);

	/* Return status/state to normal */
	pmm->status = MALI_PMM_STATUS_IDLE;
	pmm_update_system_state(pmm);
}

mali_pmm_core_mask pmm_cores_set_up_ack( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( pmm->cores_registered, cores );

	/* Check core is not pending a power up */
	MALI_DEBUG_ASSERT( (pmm->cores_pend_up & cores) != 0 );
	/* Check core has not acknowledged power up more than once */
	MALI_DEBUG_ASSERT( (pmm->cores_ack_up & cores) == 0 );

	pmm->cores_ack_up |= (cores);

	return pmm->cores_ack_up;
}

void pmm_save_os_event_data(_mali_pmm_internal_state_t *pmm, mali_pmm_message_data data)
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	/* Check that there is no saved data */
	MALI_DEBUG_ASSERT( pmm->os_data == 0 );
	/* Can't store zero data - as retrieve check will fail */
	MALI_DEBUG_ASSERT( data != 0 );

	pmm->os_data = data;
}

mali_pmm_message_data pmm_retrieve_os_event_data(_mali_pmm_internal_state_t *pmm)
{
	mali_pmm_message_data data;

	MALI_DEBUG_ASSERT_POINTER(pmm);
	/* Check that there is saved data */
	MALI_DEBUG_ASSERT( pmm->os_data != 0 );

	/* Get data, and clear the saved version */
	data = pmm->os_data;
	pmm->os_data = 0;

	return data;
}

/* Create list of core names to look up
 * We are doing it this way to overcome the need for
 * either string allocation, or stack space, so we
 * use constant strings instead
 */
typedef struct pmm_trace_corelist
{
	mali_pmm_core_mask id;
	const char *name;
} pmm_trace_corelist_t;

static pmm_trace_corelist_t pmm_trace_cores[] = {
	{ MALI_PMM_CORE_SYSTEM,  "SYSTEM" },
	{ MALI_PMM_CORE_GP,      "GP" },
	{ MALI_PMM_CORE_L2,      "L2" },
	{ MALI_PMM_CORE_PP0,     "PP0" },
	{ MALI_PMM_CORE_PP1,     "PP1" },
	{ MALI_PMM_CORE_PP2,     "PP2" },
	{ MALI_PMM_CORE_PP3,     "PP3" },
	{ MALI_PMM_CORE_PP_ALL,  "PP (all)" },
	{ (MALI_PMM_CORE_GP | MALI_PMM_CORE_L2 | MALI_PMM_CORE_PP0),
		"GP+L2+PP0" },
	{ (MALI_PMM_CORE_GP | MALI_PMM_CORE_PP0),
		"GP+PP0" },	
	{ (MALI_PMM_CORE_GP | MALI_PMM_CORE_L2 | MALI_PMM_CORE_PP0 | MALI_PMM_CORE_PP1),
		"GP+L2+PP0+PP1" },
	{ (MALI_PMM_CORE_GP | MALI_PMM_CORE_PP0 | MALI_PMM_CORE_PP1),
		"GP+PP0+PP1" },
	{ 0, NULL } /* Terminator of list */
};

const char *pmm_trace_get_core_name( mali_pmm_core_mask cores )
{
	const char *dname = NULL;
	int cl;
	
	/* Look up name in corelist */
	cl = 0;
	while( pmm_trace_cores[cl].name != NULL )
	{
		if( pmm_trace_cores[cl].id == cores )
		{
			dname = pmm_trace_cores[cl].name;
			break;
		}
		cl++;
	}
	
	if( dname == NULL )
	{
		/* We don't know a good short-hand for the configuration */
		dname = "[multi-core]";
	}

	return dname;
}

#endif /* USING_MALI_PMM */

