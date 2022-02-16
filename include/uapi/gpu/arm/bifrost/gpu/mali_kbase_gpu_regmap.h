/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _UAPI_KBASE_GPU_REGMAP_H_
#define _UAPI_KBASE_GPU_REGMAP_H_

#if !MALI_USE_CSF
#include "backend/mali_kbase_gpu_regmap_jm.h"
#endif /* !MALI_USE_CSF */

/* MMU control registers */
#define MEMORY_MANAGEMENT_BASE  0x2000
#define MMU_REG(r)              (MEMORY_MANAGEMENT_BASE + (r))
#define MMU_IRQ_RAWSTAT         0x000   /* (RW) Raw interrupt status register */

#endif /* _UAPI_KBASE_GPU_REGMAP_H_ */
