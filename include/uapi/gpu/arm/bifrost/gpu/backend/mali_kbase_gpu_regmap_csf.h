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

#ifndef _UAPI_KBASE_GPU_REGMAP_CSF_H_
#define _UAPI_KBASE_GPU_REGMAP_CSF_H_

#include <linux/types.h>

#if !MALI_USE_CSF && defined(__KERNEL__)
#error "Cannot be compiled with JM"
#endif

/* IPA control registers */

#define IPA_CONTROL_BASE       0x40000
#define IPA_CONTROL_REG(r)     (IPA_CONTROL_BASE+(r))
#define COMMAND                0x000 /* (WO) Command register */
#define STATUS                 0x004 /* (RO) Status register */
#define TIMER                  0x008 /* (RW) Timer control register */

#define SELECT_CSHW_LO         0x010 /* (RW) Counter select for CS hardware, low word */
#define SELECT_CSHW_HI         0x014 /* (RW) Counter select for CS hardware, high word */
#define SELECT_MEMSYS_LO       0x018 /* (RW) Counter select for Memory system, low word */
#define SELECT_MEMSYS_HI       0x01C /* (RW) Counter select for Memory system, high word */
#define SELECT_TILER_LO        0x020 /* (RW) Counter select for Tiler cores, low word */
#define SELECT_TILER_HI        0x024 /* (RW) Counter select for Tiler cores, high word */
#define SELECT_SHADER_LO       0x028 /* (RW) Counter select for Shader cores, low word */
#define SELECT_SHADER_HI       0x02C /* (RW) Counter select for Shader cores, high word */

/* Accumulated counter values for CS hardware */
#define VALUE_CSHW_BASE        0x100
#define VALUE_CSHW_REG_LO(n)   (VALUE_CSHW_BASE + ((n) << 3))       /* (RO) Counter value #n, low word */
#define VALUE_CSHW_REG_HI(n)   (VALUE_CSHW_BASE + ((n) << 3) + 4)   /* (RO) Counter value #n, high word */

/* Accumulated counter values for memory system */
#define VALUE_MEMSYS_BASE      0x140
#define VALUE_MEMSYS_REG_LO(n) (VALUE_MEMSYS_BASE + ((n) << 3))     /* (RO) Counter value #n, low word */
#define VALUE_MEMSYS_REG_HI(n) (VALUE_MEMSYS_BASE + ((n) << 3) + 4) /* (RO) Counter value #n, high word */

#define VALUE_TILER_BASE       0x180
#define VALUE_TILER_REG_LO(n)  (VALUE_TILER_BASE + ((n) << 3))      /* (RO) Counter value #n, low word */
#define VALUE_TILER_REG_HI(n)  (VALUE_TILER_BASE + ((n) << 3) + 4)  /* (RO) Counter value #n, high word */

#define VALUE_SHADER_BASE      0x1C0
#define VALUE_SHADER_REG_LO(n) (VALUE_SHADER_BASE + ((n) << 3))     /* (RO) Counter value #n, low word */
#define VALUE_SHADER_REG_HI(n) (VALUE_SHADER_BASE + ((n) << 3) + 4) /* (RO) Counter value #n, high word */

#include "../../csf/mali_gpu_csf_control_registers.h"

/* Set to implementation defined, outer caching */
#define AS_MEMATTR_AARCH64_OUTER_IMPL_DEF 0x88ull
/* Set to write back memory, outer caching */
#define AS_MEMATTR_AARCH64_OUTER_WA       0x8Dull
/* Set to inner non-cacheable, outer-non-cacheable
 * Setting defined by the alloc bits is ignored, but set to a valid encoding:
 * - no-alloc on read
 * - no alloc on write
 */
#define AS_MEMATTR_AARCH64_NON_CACHEABLE  0x4Cull
/* Set to shared memory, that is inner cacheable on ACE and inner or outer
 * shared, otherwise inner non-cacheable.
 * Outer cacheable if inner or outer shared, otherwise outer non-cacheable.
 */
#define AS_MEMATTR_AARCH64_SHARED         0x8ull

/* Symbols for default MEMATTR to use
 * Default is - HW implementation defined caching
 */
#define AS_MEMATTR_INDEX_DEFAULT               0
#define AS_MEMATTR_INDEX_DEFAULT_ACE           3

/* HW implementation defined caching */
#define AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY 0
/* Force cache on */
#define AS_MEMATTR_INDEX_FORCE_TO_CACHE_ALL    1
/* Write-alloc */
#define AS_MEMATTR_INDEX_WRITE_ALLOC           2
/* Outer coherent, inner implementation defined policy */
#define AS_MEMATTR_INDEX_OUTER_IMPL_DEF        3
/* Outer coherent, write alloc inner */
#define AS_MEMATTR_INDEX_OUTER_WA              4
/* Normal memory, inner non-cacheable, outer non-cacheable (ARMv8 mode only) */
#define AS_MEMATTR_INDEX_NON_CACHEABLE         5
/* Normal memory, shared between MCU and Host */
#define AS_MEMATTR_INDEX_SHARED                6

/* Configuration bits for the CSF. */
#define CSF_CONFIG 0xF00

/* CSF_CONFIG register */
#define CSF_CONFIG_FORCE_COHERENCY_FEATURES_SHIFT 2

/* GPU control registers */
#define CORE_FEATURES           0x008   /* () Shader Core Features */
#define MCU_CONTROL             0x700
#define MCU_STATUS              0x704

#define MCU_CNTRL_ENABLE        (1 << 0)
#define MCU_CNTRL_AUTO          (1 << 1)
#define MCU_CNTRL_DISABLE       (0)

#define MCU_STATUS_HALTED        (1 << 1)

#define PRFCNT_BASE_LO   0x060  /* (RW) Performance counter memory
				 * region base address, low word
				 */
#define PRFCNT_BASE_HI   0x064  /* (RW) Performance counter memory
				 * region base address, high word
				 */
#define PRFCNT_CONFIG    0x068  /* (RW) Performance counter
				 * configuration
				 */

#define PRFCNT_CSHW_EN   0x06C  /* (RW) Performance counter
				 * enable for CS Hardware
				 */

#define PRFCNT_SHADER_EN 0x070  /* (RW) Performance counter enable
				 * flags for shader cores
				 */
#define PRFCNT_TILER_EN  0x074  /* (RW) Performance counter enable
				 * flags for tiler
				 */
#define PRFCNT_MMU_L2_EN 0x07C  /* (RW) Performance counter enable
				 * flags for MMU/L2 cache
				 */

/* JOB IRQ flags */
#define JOB_IRQ_GLOBAL_IF       (1 << 31)   /* Global interface interrupt received */

/* GPU_COMMAND codes */
#define GPU_COMMAND_CODE_NOP                0x00 /* No operation, nothing happens */
#define GPU_COMMAND_CODE_RESET              0x01 /* Reset the GPU */
#define GPU_COMMAND_CODE_PRFCNT             0x02 /* Clear or sample performance counters */
#define GPU_COMMAND_CODE_TIME               0x03 /* Configure time sources */
#define GPU_COMMAND_CODE_FLUSH_CACHES       0x04 /* Flush caches */
#define GPU_COMMAND_CODE_SET_PROTECTED_MODE 0x05 /* Places the GPU in protected mode */
#define GPU_COMMAND_CODE_FINISH_HALT        0x06 /* Halt CSF */
#define GPU_COMMAND_CODE_CLEAR_FAULT        0x07 /* Clear GPU_FAULTSTATUS and GPU_FAULTADDRESS, TODX */

/* GPU_COMMAND_RESET payloads */

/* This will leave the state of active jobs UNDEFINED, but will leave the external bus in a defined and idle state.
 * Power domains will remain powered on.
 */
#define GPU_COMMAND_RESET_PAYLOAD_FAST_RESET 0x00

/* This will leave the state of active CSs UNDEFINED, but will leave the external bus in a defined and
 * idle state.
 */
#define GPU_COMMAND_RESET_PAYLOAD_SOFT_RESET 0x01

/* This reset will leave the state of currently active streams UNDEFINED, will likely lose data, and may leave
 * the system bus in an inconsistent state. Use only as a last resort when nothing else works.
 */
#define GPU_COMMAND_RESET_PAYLOAD_HARD_RESET 0x02

/* GPU_COMMAND_PRFCNT payloads */
#define GPU_COMMAND_PRFCNT_PAYLOAD_SAMPLE 0x01 /* Sample performance counters */
#define GPU_COMMAND_PRFCNT_PAYLOAD_CLEAR  0x02 /* Clear performance counters */

/* GPU_COMMAND_TIME payloads */
#define GPU_COMMAND_TIME_DISABLE 0x00 /* Disable cycle counter */
#define GPU_COMMAND_TIME_ENABLE  0x01 /* Enable cycle counter */

/* GPU_COMMAND_FLUSH_CACHES payloads */
#define GPU_COMMAND_FLUSH_PAYLOAD_NONE             0x00 /* No flush */
#define GPU_COMMAND_FLUSH_PAYLOAD_CLEAN            0x01 /* Clean the caches */
#define GPU_COMMAND_FLUSH_PAYLOAD_INVALIDATE       0x02 /* Invalidate the caches */
#define GPU_COMMAND_FLUSH_PAYLOAD_CLEAN_INVALIDATE 0x03 /* Clean and invalidate the caches */

/* GPU_COMMAND command + payload */
#define GPU_COMMAND_CODE_PAYLOAD(opcode, payload) \
	((__u32)opcode | ((__u32)payload << 8))

/* Final GPU_COMMAND form */
/* No operation, nothing happens */
#define GPU_COMMAND_NOP \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_NOP, 0)

/* Stop all external bus interfaces, and then reset the entire GPU. */
#define GPU_COMMAND_SOFT_RESET \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_RESET, GPU_COMMAND_RESET_PAYLOAD_SOFT_RESET)

/* Immediately reset the entire GPU. */
#define GPU_COMMAND_HARD_RESET \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_RESET, GPU_COMMAND_RESET_PAYLOAD_HARD_RESET)

/* Clear all performance counters, setting them all to zero. */
#define GPU_COMMAND_PRFCNT_CLEAR \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_PRFCNT, GPU_COMMAND_PRFCNT_PAYLOAD_CLEAR)

/* Sample all performance counters, writing them out to memory */
#define GPU_COMMAND_PRFCNT_SAMPLE \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_PRFCNT, GPU_COMMAND_PRFCNT_PAYLOAD_SAMPLE)

/* Starts the cycle counter, and system timestamp propagation */
#define GPU_COMMAND_CYCLE_COUNT_START \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_TIME, GPU_COMMAND_TIME_ENABLE)

/* Stops the cycle counter, and system timestamp propagation */
#define GPU_COMMAND_CYCLE_COUNT_STOP \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_TIME, GPU_COMMAND_TIME_DISABLE)

/* Clean all caches */
#define GPU_COMMAND_CLEAN_CACHES \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_FLUSH_CACHES, GPU_COMMAND_FLUSH_PAYLOAD_CLEAN)

/* Clean and invalidate all caches */
#define GPU_COMMAND_CLEAN_INV_CACHES \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_FLUSH_CACHES, GPU_COMMAND_FLUSH_PAYLOAD_CLEAN_INVALIDATE)

/* Places the GPU in protected mode */
#define GPU_COMMAND_SET_PROTECTED_MODE \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_SET_PROTECTED_MODE, 0)

/* Halt CSF */
#define GPU_COMMAND_FINISH_HALT \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_FINISH_HALT, 0)

/* Clear GPU faults */
#define GPU_COMMAND_CLEAR_FAULT \
	GPU_COMMAND_CODE_PAYLOAD(GPU_COMMAND_CODE_CLEAR_FAULT, 0)

/* End Command Values */

/* GPU_FAULTSTATUS register */
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_SHIFT 0
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_MASK (0xFFul)
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_GET(reg_val) \
	(((reg_val)&GPU_FAULTSTATUS_EXCEPTION_TYPE_MASK) \
	 >> GPU_FAULTSTATUS_EXCEPTION_TYPE_SHIFT)
#define GPU_FAULTSTATUS_ACCESS_TYPE_SHIFT 8
#define GPU_FAULTSTATUS_ACCESS_TYPE_MASK \
	(0x3ul << GPU_FAULTSTATUS_ACCESS_TYPE_SHIFT)

#define GPU_FAULTSTATUS_ADDR_VALID_SHIFT 10
#define GPU_FAULTSTATUS_ADDR_VALID_FLAG \
	(1ul << GPU_FAULTSTATUS_ADDR_VALID_SHIFT)

#define GPU_FAULTSTATUS_JASID_VALID_SHIFT 11
#define GPU_FAULTSTATUS_JASID_VALID_FLAG \
	(1ul << GPU_FAULTSTATUS_JASID_VALID_SHIFT)

#define GPU_FAULTSTATUS_JASID_SHIFT 12
#define GPU_FAULTSTATUS_JASID_MASK (0xF << GPU_FAULTSTATUS_JASID_SHIFT)
#define GPU_FAULTSTATUS_JASID_GET(reg_val) \
	(((reg_val)&GPU_FAULTSTATUS_JASID_MASK) >> GPU_FAULTSTATUS_JASID_SHIFT)
#define GPU_FAULTSTATUS_JASID_SET(reg_val, value) \
	(((reg_val) & ~GPU_FAULTSTATUS_JASID_MASK) |  \
	(((value) << GPU_FAULTSTATUS_JASID_SHIFT) & GPU_FAULTSTATUS_JASID_MASK))

#define GPU_FAULTSTATUS_SOURCE_ID_SHIFT 16
#define GPU_FAULTSTATUS_SOURCE_ID_MASK \
	(0xFFFFul << GPU_FAULTSTATUS_SOURCE_ID_SHIFT)
/* End GPU_FAULTSTATUS register */

/* GPU_FAULTSTATUS_ACCESS_TYPE values */
#define GPU_FAULTSTATUS_ACCESS_TYPE_ATOMIC 0x0
#define GPU_FAULTSTATUS_ACCESS_TYPE_EXECUTE 0x1
#define GPU_FAULTSTATUS_ACCESS_TYPE_READ 0x2
#define GPU_FAULTSTATUS_ACCESS_TYPE_WRITE 0x3
/* End of GPU_FAULTSTATUS_ACCESS_TYPE values */

/* Implementation-dependent exception codes used to indicate CSG
 * and CS errors that are not specified in the specs.
 */
#define GPU_EXCEPTION_TYPE_SW_FAULT_0 ((__u8)0x70)
#define GPU_EXCEPTION_TYPE_SW_FAULT_1 ((__u8)0x71)
#define GPU_EXCEPTION_TYPE_SW_FAULT_2 ((__u8)0x72)

/* GPU_FAULTSTATUS_EXCEPTION_TYPE values */
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_OK 0x00
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_BUS_FAULT 0x80
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_SHAREABILITY_FAULT 0x88
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_SYSTEM_SHAREABILITY_FAULT 0x89
#define GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_CACHEABILITY_FAULT 0x8A
/* End of GPU_FAULTSTATUS_EXCEPTION_TYPE values */

#define GPU_FAULTSTATUS_ADDRESS_VALID_SHIFT GPU_U(10)
#define GPU_FAULTSTATUS_ADDRESS_VALID_MASK (GPU_U(0x1) << GPU_FAULTSTATUS_ADDRESS_VALID_SHIFT)
#define GPU_FAULTSTATUS_ADDRESS_VALID_GET(reg_val) \
	(((reg_val)&GPU_FAULTSTATUS_ADDRESS_VALID_MASK) >> GPU_FAULTSTATUS_ADDRESS_VALID_SHIFT)
#define GPU_FAULTSTATUS_ADDRESS_VALID_SET(reg_val, value) \
	(((reg_val) & ~GPU_FAULTSTATUS_ADDRESS_VALID_MASK) |  \
	(((value) << GPU_FAULTSTATUS_ADDRESS_VALID_SHIFT) & GPU_FAULTSTATUS_ADDRESS_VALID_MASK))

/* IRQ flags */
#define GPU_FAULT               (1 << 0)    /* A GPU Fault has occurred */
#define GPU_PROTECTED_FAULT     (1 << 1)    /* A GPU fault has occurred in protected mode */
#define RESET_COMPLETED         (1 << 8)    /* Set when a reset has completed.  */
#define POWER_CHANGED_SINGLE    (1 << 9)    /* Set when a single core has finished powering up or down. */
#define POWER_CHANGED_ALL       (1 << 10)   /* Set when all cores have finished powering up or down. */
#define CLEAN_CACHES_COMPLETED  (1 << 17)   /* Set when a cache clean operation has completed. */
#define DOORBELL_MIRROR         (1 << 18)   /* Mirrors the doorbell interrupt line to the CPU */
#define MCU_STATUS_GPU_IRQ      (1 << 19)   /* MCU requires attention */

/*
 * In Debug build,
 * GPU_IRQ_REG_COMMON | POWER_CHANGED_SINGLE is used to clear and unmask interupts sources of GPU_IRQ
 * by writing it onto GPU_IRQ_CLEAR/MASK registers.
 *
 * In Release build,
 * GPU_IRQ_REG_COMMON is used.
 *
 * Note:
 * CLEAN_CACHES_COMPLETED - Used separately for cache operation.
 * DOORBELL_MIRROR - Do not have it included for GPU_IRQ_REG_COMMON
 *                   as it can't be cleared by GPU_IRQ_CLEAR, thus interrupt storm might happen
 */
#define GPU_IRQ_REG_COMMON (GPU_FAULT | GPU_PROTECTED_FAULT | RESET_COMPLETED \
			| POWER_CHANGED_ALL | MCU_STATUS_GPU_IRQ)

/* GPU_CONTROL_MCU.GPU_IRQ_RAWSTAT */
#define PRFCNT_SAMPLE_COMPLETED (1 << 16)   /* Set when performance count sample has completed */

#endif /* _UAPI_KBASE_GPU_REGMAP_CSF_H_ */
