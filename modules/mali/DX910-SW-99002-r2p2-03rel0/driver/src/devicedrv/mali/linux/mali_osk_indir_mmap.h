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
 * @file mali_osk_specific.h
 * Defines per-OS Kernel level specifics, such as unusual workarounds for
 * certain OSs.
 */

#ifndef __MALI_OSK_INDIR_MMAP_H__
#define __MALI_OSK_INDIR_MMAP_H__

#include "mali_uk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Linux specific means for calling _mali_ukk_mem_mmap/munmap
 *
 * The presence of _MALI_OSK_SPECIFIC_INDIRECT_MMAP indicates that
 * _mali_osk_specific_indirect_mmap and _mali_osk_specific_indirect_munmap
 * should be used instead of _mali_ukk_mem_mmap/_mali_ukk_mem_munmap.
 *
 * The arguments are the same as _mali_ukk_mem_mmap/_mali_ukk_mem_munmap.
 *
 * In ALL operating system other than Linux, it is expected that common code
 * should be able to call _mali_ukk_mem_mmap/_mali_ukk_mem_munmap directly.
 * Such systems should NOT define _MALI_OSK_SPECIFIC_INDIRECT_MMAP.
 */
_mali_osk_errcode_t _mali_osk_specific_indirect_mmap( _mali_uk_mem_mmap_s *args );
_mali_osk_errcode_t _mali_osk_specific_indirect_munmap( _mali_uk_mem_munmap_s *args );


#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_INDIR_MMAP_H__ */
