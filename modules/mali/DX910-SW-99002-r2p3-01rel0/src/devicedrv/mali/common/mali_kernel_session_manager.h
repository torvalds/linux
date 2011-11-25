/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_SESSION_MANAGER_H__
#define __MALI_KERNEL_SESSION_MANAGER_H__

/* Incomplete struct to pass around pointers to it */
struct mali_session_data;

void * mali_kernel_session_manager_slot_get(struct mali_session_data * session, int id);

#endif /* __MALI_KERNEL_SESSION_MANAGER_H__ */
