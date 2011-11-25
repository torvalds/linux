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
 * @file mali_pmm_policy.c
 * Implementation of the common routines for power management module 
 * policies
 */

#if USING_MALI_PMM

#include "mali_ukk.h"
#include "mali_kernel_common.h"

#include "mali_pmm.h"
#include "mali_pmm_system.h"
#include "mali_pmm_state.h"
#include "mali_pmm_policy.h"

#include "mali_pmm_policy_alwayson.h"
#include "mali_pmm_policy_jobcontrol.h"

/* Call back function for timer expiration */
static void pmm_policy_timer_callback( void *arg );

_mali_osk_errcode_t pmm_policy_timer_init( _pmm_policy_timer_t *pptimer, u32 timeout, mali_pmm_event_id id )
{
	MALI_DEBUG_ASSERT_POINTER(pptimer);
	
	/* All values get 0 as default */
	_mali_osk_memset(pptimer, 0, sizeof(*pptimer));
	
	pptimer->timer = _mali_osk_timer_init();
	if( pptimer->timer )
	{
		_mali_osk_timer_setcallback( pptimer->timer, pmm_policy_timer_callback, (void *)pptimer );
		pptimer->timeout = timeout;
		pptimer->event_id = id;
		MALI_SUCCESS;
	}
	
	return _MALI_OSK_ERR_FAULT;
}

static void pmm_policy_timer_callback( void *arg )
{
	_pmm_policy_timer_t *pptimer = (_pmm_policy_timer_t *)arg;
	
	MALI_DEBUG_ASSERT_POINTER(pptimer);
	MALI_DEBUG_ASSERT( pptimer->set );

	/* Set timer expired and flag there is a policy to check */
	pptimer->expired = MALI_TRUE;
	malipmm_set_policy_check();
}


void pmm_policy_timer_term( _pmm_policy_timer_t *pptimer )
{
	MALI_DEBUG_ASSERT_POINTER(pptimer);

	_mali_osk_timer_del( pptimer->timer );
	_mali_osk_timer_term( pptimer->timer );
	pptimer->timer = NULL;
}

mali_bool pmm_policy_timer_start( _pmm_policy_timer_t *pptimer )
{
	MALI_DEBUG_ASSERT_POINTER(pptimer);
	MALI_DEBUG_ASSERT_POINTER(pptimer->timer);

	if( !(pptimer->set) )
	{
		pptimer->set = MALI_TRUE;
		pptimer->expired = MALI_FALSE;
		pptimer->start = _mali_osk_time_tickcount();
		_mali_osk_timer_add( pptimer->timer, pptimer->timeout );
		return MALI_TRUE;
	}
	
	return MALI_FALSE;
}

mali_bool pmm_policy_timer_stop( _pmm_policy_timer_t *pptimer )
{
	MALI_DEBUG_ASSERT_POINTER(pptimer);
	MALI_DEBUG_ASSERT_POINTER(pptimer->timer);

	if( pptimer->set )
	{
		_mali_osk_timer_del( pptimer->timer );
		pptimer->set = MALI_FALSE;
		pptimer->expired = MALI_FALSE;
		return MALI_TRUE;
	}
	
	return MALI_FALSE;
}

mali_bool pmm_policy_timer_raise_event( _pmm_policy_timer_t *pptimer )
{
	MALI_DEBUG_ASSERT_POINTER(pptimer);

	if( pptimer->expired )
	{
		_mali_uk_pmm_message_s event = {
			NULL,
			MALI_PMM_EVENT_TIMEOUT, /* Assume timeout id, but set it below */
			0 };

		event.id = pptimer->event_id;
		event.data = (mali_pmm_message_data)pptimer->start;

		/* Don't need to do any other notification with this timer */
		pptimer->expired = MALI_FALSE;
		/* Unset timer so it is free to be set again */
		pptimer->set = MALI_FALSE;
		
		_mali_ukk_pmm_event_message( &event );
				
		return MALI_TRUE;
	}
	
	return MALI_FALSE;
}

mali_bool pmm_policy_timer_valid( u32 timer_start, u32 other_start )
{
	return (_mali_osk_time_after( other_start, timer_start ) == 0);
}


_mali_osk_errcode_t pmm_policy_init(_mali_pmm_internal_state_t *pmm)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmm);

	switch( pmm->policy )
	{
		case MALI_PMM_POLICY_ALWAYS_ON:
		{
			err = pmm_policy_init_always_on();
		}
		break;

		case MALI_PMM_POLICY_JOB_CONTROL:
		{
			err = pmm_policy_init_job_control(pmm);
		}
		break;

		case MALI_PMM_POLICY_NONE:
		default:
			err = _MALI_OSK_ERR_FAULT;
	}
	
	return err;
}

void pmm_policy_term(_mali_pmm_internal_state_t *pmm)
{
	MALI_DEBUG_ASSERT_POINTER(pmm);

	switch( pmm->policy )
	{
		case MALI_PMM_POLICY_ALWAYS_ON:
		{
			pmm_policy_term_always_on();
		}
		break;

		case MALI_PMM_POLICY_JOB_CONTROL:
		{
			pmm_policy_term_job_control();
		}
		break;

		case MALI_PMM_POLICY_NONE:
		default:
			MALI_PRINT_ERROR( ("PMM: Invalid policy terminated %d\n", pmm->policy) );
	}
}


_mali_osk_errcode_t pmm_policy_process(_mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_DEBUG_ASSERT_POINTER(event);

	switch( pmm->policy )
	{
		case MALI_PMM_POLICY_ALWAYS_ON:
		{
			err = pmm_policy_process_always_on( pmm, event );
		}
		break;

		case MALI_PMM_POLICY_JOB_CONTROL:
		{
			err = pmm_policy_process_job_control( pmm, event );
		}
		break;

		case MALI_PMM_POLICY_NONE:
		default:
			err = _MALI_OSK_ERR_FAULT;
	}
	
	return err;
}


void pmm_policy_check_policy( _mali_pmm_internal_state_t *pmm )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);

	switch( pmm->policy )
	{
		case MALI_PMM_POLICY_JOB_CONTROL:
		{
			pmm_policy_check_job_control();
		}
		break;

		default:
			/* Nothing needs to be done */
			break;
	}
}


#endif /* USING_MALI_PMM */

