/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_PROFILING_H__
#define __MALI_KERNEL_PROFILING_H__

#if MALI_TIMELINE_PROFILING_ENABLED

#include <../../../include/cinstr/mali_cinstr_profiling_events_m200.h>

/**
 * Initialize the profiling module.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_init(void);

/*
 * Terminate the profiling module.
 */
void _mali_profiling_term(void);

/**
 * Add an profiling event
 *
 * @param event_id The event identificator.
 * @param data0 - First data parameter, depending on event_id specified.
 * @param data1 - Second data parameter, depending on event_id specified.
 * @param data2 - Third data parameter, depending on event_id specified.
 * @param data3 - Fourth data parameter, depending on event_id specified.
 * @param data4 - Fifth data parameter, depending on event_id specified.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_add_event(u32 event_id, u32 data0, u32 data1, u32 data2, u32 data3, u32 data4);

#endif /* MALI_TIMELINE_PROFILING_ENABLED */

#endif /* __MALI_KERNEL_PROFILING_H__ */


