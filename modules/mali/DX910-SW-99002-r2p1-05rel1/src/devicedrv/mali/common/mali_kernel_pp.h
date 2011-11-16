/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_PP_H__
#define __MALI_KERNEL_PP_H__

extern struct mali_kernel_subsystem mali_subsystem_mali200;

#if USING_MALI_PMM
_mali_osk_errcode_t malipp_signal_power_up( u32 core_num, mali_bool queue_only );
_mali_osk_errcode_t malipp_signal_power_down( u32 core_num, mali_bool immediate_only );
#endif

#endif /* __MALI_KERNEL_PP_H__ */
