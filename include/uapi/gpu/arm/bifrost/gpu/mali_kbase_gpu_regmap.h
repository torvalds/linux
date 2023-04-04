/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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

#if MALI_USE_CSF
#include "backend/mali_kbase_gpu_regmap_csf.h"
#else
#include "backend/mali_kbase_gpu_regmap_jm.h"
#endif /* !MALI_USE_CSF */

/* Begin Register Offsets */
/* GPU control registers */

#define GPU_CONTROL_BASE        0x0000
#define GPU_CONTROL_REG(r)      (GPU_CONTROL_BASE + (r))

#define GPU_ID                  0x000   /* (RO) GPU and revision identifier */

#define GPU_IRQ_CLEAR           0x024   /* (WO) */
#define GPU_IRQ_STATUS          0x02C   /* (RO) */

#define SHADER_READY_LO         0x140   /* (RO) Shader core ready bitmap, low word */
#define SHADER_READY_HI         0x144   /* (RO) Shader core ready bitmap, high word */

#define TILER_READY_LO          0x150   /* (RO) Tiler core ready bitmap, low word */
#define TILER_READY_HI          0x154   /* (RO) Tiler core ready bitmap, high word */

#define L2_READY_LO             0x160   /* (RO) Level 2 cache ready bitmap, low word */
#define L2_READY_HI             0x164   /* (RO) Level 2 cache ready bitmap, high word */

#define SHADER_PWRON_LO         0x180   /* (WO) Shader core power on bitmap, low word */
#define SHADER_PWRON_HI         0x184   /* (WO) Shader core power on bitmap, high word */

#define TILER_PWRON_LO          0x190   /* (WO) Tiler core power on bitmap, low word */
#define TILER_PWRON_HI          0x194   /* (WO) Tiler core power on bitmap, high word */

#define L2_PWRON_LO             0x1A0   /* (WO) Level 2 cache power on bitmap, low word */
#define L2_PWRON_HI             0x1A4   /* (WO) Level 2 cache power on bitmap, high word */

/* Job control registers */

#define JOB_CONTROL_BASE        0x1000

#define JOB_CONTROL_REG(r)      (JOB_CONTROL_BASE + (r))

#define JOB_IRQ_CLEAR           0x004   /* Interrupt clear register */
#define JOB_IRQ_MASK            0x008   /* Interrupt mask register */
#define JOB_IRQ_STATUS          0x00C   /* Interrupt status register */

/* MMU control registers */

#define MEMORY_MANAGEMENT_BASE  0x2000

#define MMU_REG(r)              (MEMORY_MANAGEMENT_BASE + (r))

#define MMU_IRQ_RAWSTAT         0x000   /* (RW) Raw interrupt status register */
#define MMU_IRQ_CLEAR           0x004   /* (WO) Interrupt clear register */
#define MMU_IRQ_MASK            0x008   /* (RW) Interrupt mask register */
#define MMU_IRQ_STATUS          0x00C   /* (RO) Interrupt status register */

#define MMU_AS0                 0x400   /* Configuration registers for address space 0 */

/* MMU address space control registers */

#define MMU_AS_REG(n, r)        (MMU_REG(MMU_AS0 + ((n) << 6)) + (r))

#define AS_TRANSTAB_LO         0x00	/* (RW) Translation Table Base Address for address space n, low word */
#define AS_TRANSTAB_HI         0x04	/* (RW) Translation Table Base Address for address space n, high word */
#define AS_MEMATTR_LO          0x08	/* (RW) Memory attributes for address space n, low word. */
#define AS_MEMATTR_HI          0x0C	/* (RW) Memory attributes for address space n, high word. */
#define AS_COMMAND             0x18	/* (WO) MMU command register for address space n */

/* (RW) Translation table configuration for address space n, low word */
#define AS_TRANSCFG_LO         0x30
/* (RW) Translation table configuration for address space n, high word */
#define AS_TRANSCFG_HI         0x34

#endif /* _UAPI_KBASE_GPU_REGMAP_H_ */
