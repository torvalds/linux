/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_kernel_memory_backend_os.h
 */

#ifndef __UMP_KERNEL_MEMORY_BACKEND_OS_H__
#define __UMP_KERNEL_MEMORY_BACKEND_OS_H__

#include "ump_kernel_memory_backend.h"

ump_memory_backend * ump_os_memory_backend_create(const int max_allocation);

#endif /* __UMP_KERNEL_MEMORY_BACKEND_OS_H__ */

