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
#include "mali_osk_mali.h"
#include "mali_ukk.h"
#include "mali_timestamp.h"

#define MALI_PROFILING_MAX_BUFFER_ENTRIES 1048576

typedef struct mali_profiling_entry
{
	u64 timestamp;
	u32 event_id;
	u32 data[5];
} mali_profiling_entry;


typedef enum mali_profiling_state
{
	MALI_PROFILING_STATE_UNINITIALIZED,
	MALI_PROFILING_STATE_IDLE,
	MALI_PROFILING_STATE_RUNNING,
	MALI_PROFILING_STATE_RETURN,
} mali_profiling_state;


static _mali_osk_lock_t *lock = NULL;
static mali_profiling_state prof_state = MALI_PROFILING_STATE_UNINITIALIZED;
static mali_profiling_entry* profile_entries = NULL;
static u32 profile_entry_count = 0;
static _mali_osk_atomic_t profile_insert_index;
static _mali_osk_atomic_t profile_entries_written;


_mali_osk_errcode_t _mali_profiling_init(void)
{
	profile_entries = NULL;
	profile_entry_count = 0;
	_mali_osk_atomic_init(&profile_insert_index, 0);
	_mali_osk_atomic_init(&profile_entries_written, 0);

	lock = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, 0 );
	if (NULL == lock)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	prof_state = MALI_PROFILING_STATE_IDLE;

	return _MALI_OSK_ERR_OK;
}

void _mali_profiling_term(void)
{
	prof_state = MALI_PROFILING_STATE_UNINITIALIZED;

	/* wait for all elements to be completely inserted into array */
	while (_mali_osk_atomic_read(&profile_insert_index) != _mali_osk_atomic_read(&profile_entries_written))
	{
		/* do nothing */;
	}

	if (NULL != profile_entries)
	{
		_mali_osk_free(profile_entries);
		profile_entries = NULL;
	}

	if (NULL != lock)
	{
		_mali_osk_lock_term(lock);
		lock = NULL;
	}
}

inline _mali_osk_errcode_t _mali_profiling_add_event(u32 event_id, u32 data0, u32 data1, u32 data2, u32 data3, u32 data4)
{
	u32 cur_index = _mali_osk_atomic_inc_return(&profile_insert_index) - 1;

	if (prof_state != MALI_PROFILING_STATE_RUNNING || cur_index >= profile_entry_count)
	{
		/*
		 * Not in recording mode, or buffer is full
		 * Decrement index again, and early out
		 */
		_mali_osk_atomic_dec(&profile_insert_index);
		return _MALI_OSK_ERR_FAULT;
	}

	profile_entries[cur_index].timestamp = _mali_timestamp_get();
	profile_entries[cur_index].event_id = event_id;
	profile_entries[cur_index].data[0] = data0;
	profile_entries[cur_index].data[1] = data1;
	profile_entries[cur_index].data[2] = data2;
	profile_entries[cur_index].data[3] = data3;
	profile_entries[cur_index].data[4] = data4;

	_mali_osk_atomic_inc(&profile_entries_written);

	return _MALI_OSK_ERR_OK;
}


_mali_osk_errcode_t _mali_ukk_profiling_start(_mali_uk_profiling_start_s *args)
{
	_mali_osk_errcode_t ret;

	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (prof_state != MALI_PROFILING_STATE_IDLE)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	if (args->limit > MALI_PROFILING_MAX_BUFFER_ENTRIES)
	{
		args->limit = MALI_PROFILING_MAX_BUFFER_ENTRIES;
	}

	profile_entries = _mali_osk_malloc(args->limit * sizeof(mali_profiling_entry));
	profile_entry_count = args->limit;
	if (NULL == profile_entries)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_NOMEM;
	}

	ret = _mali_timestamp_reset();

	if (ret == _MALI_OSK_ERR_OK)
	{
		prof_state = MALI_PROFILING_STATE_RUNNING;
	}
	else
	{
		_mali_osk_free(profile_entries);
		profile_entries = NULL;
	}

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return ret;
}

_mali_osk_errcode_t _mali_ukk_profiling_add_event(_mali_uk_profiling_add_event_s *args)
{
	/* Always add process and thread identificator in the first two data elements for events from user space */
	return _mali_profiling_add_event(args->event_id, _mali_osk_get_pid(), _mali_osk_get_tid(), args->data[2], args->data[3], args->data[4]);
}

_mali_osk_errcode_t _mali_ukk_profiling_stop(_mali_uk_profiling_stop_s *args)
{
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (prof_state != MALI_PROFILING_STATE_RUNNING)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	/* go into return state (user to retreive events), no more events will be added after this */
	prof_state = MALI_PROFILING_STATE_RETURN;

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);

	/* wait for all elements to be completely inserted into array */
	while (_mali_osk_atomic_read(&profile_insert_index) != _mali_osk_atomic_read(&profile_entries_written))
	{
		/* do nothing */;
	}

	args->count = _mali_osk_atomic_read(&profile_insert_index);

	return _MALI_OSK_ERR_OK;
}


_mali_osk_errcode_t _mali_ukk_profiling_get_event(_mali_uk_profiling_get_event_s *args)
{
	u32 index = args->index;

	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (prof_state != MALI_PROFILING_STATE_RETURN)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	if (index >= _mali_osk_atomic_read(&profile_entries_written))
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_FAULT;
	}

	args->timestamp = profile_entries[index].timestamp;
	args->event_id = profile_entries[index].event_id;
	args->data[0] = profile_entries[index].data[0];
	args->data[1] = profile_entries[index].data[1];
	args->data[2] = profile_entries[index].data[2];
	args->data[3] = profile_entries[index].data[3];
	args->data[4] = profile_entries[index].data[4];

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_profiling_clear(_mali_uk_profiling_clear_s *args)
{
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (prof_state != MALI_PROFILING_STATE_RETURN)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	prof_state = MALI_PROFILING_STATE_IDLE;
	profile_entry_count = 0;
	_mali_osk_atomic_init(&profile_insert_index, 0);
	_mali_osk_atomic_init(&profile_entries_written, 0);
	if (NULL != profile_entries)
	{
		_mali_osk_free(profile_entries);
		profile_entries = NULL;
	}

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return _MALI_OSK_ERR_OK;
}

