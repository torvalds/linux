/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __GENERIC_PT_COMMON_H
#define __GENERIC_PT_COMMON_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/bits.h>

/**
 * DOC: Generic Radix Page Table
 *
 * Generic Radix Page Table is a set of functions and helpers to efficiently
 * parse radix style page tables typically seen in HW implementations. The
 * interface is built to deliver similar code generation as the mm's pte/pmd/etc
 * system by fully inlining the exact code required to handle each table level.
 *
 * Like the mm subsystem each format contributes its parsing implementation
 * under common names and the common code implements the required algorithms.
 *
 * The system is divided into three logical levels:
 *
 *  - The page table format and its manipulation functions
 *  - Generic helpers to give a consistent API regardless of underlying format
 *  - An algorithm implementation (e.g. IOMMU/DRM/KVM/MM)
 *
 * Multiple implementations are supported. The intention is to have the generic
 * format code be re-usable for whatever specialized implementation is required.
 * The generic code is solely about the format of the radix tree; it does not
 * include memory allocation or higher level decisions that are left for the
 * implementation.
 *
 * The generic framework supports a superset of functions across many HW
 * implementations:
 *
 *  - Entries comprised of contiguous blocks of IO PTEs for larger page sizes
 *  - Multi-level tables, up to 6 levels. Runtime selected top level
 *  - Runtime variable table level size (ARM's concatenated tables)
 *  - Expandable top level allowing dynamic sizing of table levels
 *  - Optional leaf entries at any level
 *  - 32-bit/64-bit virtual and output addresses, using every address bit
 *  - Dirty tracking
 *  - Sign extended addressing
 */

/**
 * struct pt_common - struct for all page table implementations
 */
struct pt_common {
	/**
	 * @top_of_table: Encodes the table top pointer and the top level in a
	 * single value. Must use READ_ONCE/WRITE_ONCE to access it. The lower
	 * bits of the aligned table pointer are used for the level.
	 */
	uintptr_t top_of_table;
	/**
	 * @max_oasz_lg2: Maximum number of bits the OA can contain. Upper bits
	 * must be zero. This may be less than what the page table format
	 * supports, but must not be more.
	 */
	u8 max_oasz_lg2;
	/**
	 * @max_vasz_lg2: Maximum number of bits the VA can contain. Upper bits
	 * are 0 or 1 depending on pt_full_va_prefix(). This may be less than
	 * what the page table format supports, but must not be more. When
	 * PT_FEAT_DYNAMIC_TOP is set this reflects the maximum VA capability.
	 */
	u8 max_vasz_lg2;
	/**
	 * @features: Bitmap of `enum pt_features`
	 */
	unsigned int features;
};

/* Encoding parameters for top_of_table */
enum {
	PT_TOP_LEVEL_BITS = 3,
	PT_TOP_LEVEL_MASK = GENMASK(PT_TOP_LEVEL_BITS - 1, 0),
};

/**
 * enum pt_features - Features turned on in the table. Each symbol is a bit
 * position.
 */
enum pt_features {
	/**
	 * @PT_FEAT_DMA_INCOHERENT: Cache flush page table memory before
	 * assuming the HW can read it. Otherwise a SMP release is sufficient
	 * for HW to read it.
	 */
	PT_FEAT_DMA_INCOHERENT,
	/**
	 * @PT_FEAT_FULL_VA: The table can span the full VA range from 0 to
	 * PT_VADDR_MAX.
	 */
	PT_FEAT_FULL_VA,
	/**
	 * @PT_FEAT_DYNAMIC_TOP: The table's top level can be increased
	 * dynamically during map. This requires HW support for atomically
	 * setting both the table top pointer and the starting table level.
	 */
	PT_FEAT_DYNAMIC_TOP,
	/**
	 * @PT_FEAT_SIGN_EXTEND: The top most bit of the valid VA range sign
	 * extends up to the full pt_vaddr_t. This divides the page table into
	 * three VA ranges::
	 *
	 *   0         -> 2^N - 1             Lower
	 *   2^N       -> (MAX - 2^N - 1)     Non-Canonical
	 *   MAX - 2^N -> MAX                 Upper
	 *
	 * In this mode pt_common::max_vasz_lg2 includes the sign bit and the
	 * upper bits that don't fall within the translation are just validated.
	 *
	 * If not set there is no sign extension and valid VA goes from 0 to 2^N
	 * - 1.
	 */
	PT_FEAT_SIGN_EXTEND,
	/**
	 * @PT_FEAT_FLUSH_RANGE: IOTLB maintenance is done by flushing IOVA
	 * ranges which will clean out any walk cache or any IOPTE fully
	 * contained by the range. The optimization objective is to minimize the
	 * number of flushes even if ranges include IOVA gaps that do not need
	 * to be flushed.
	 */
	PT_FEAT_FLUSH_RANGE,
	/**
	 * @PT_FEAT_FLUSH_RANGE_NO_GAPS: Like PT_FEAT_FLUSH_RANGE except that
	 * the optimization objective is to only flush IOVA that has been
	 * changed. This mode is suitable for cases like hypervisor shadowing
	 * where flushing unchanged ranges may cause the hypervisor to reparse
	 * significant amount of page table.
	 */
	PT_FEAT_FLUSH_RANGE_NO_GAPS,
	/* private: */
	PT_FEAT_FMT_START,
};

struct pt_amdv1 {
	struct pt_common common;
};

enum {
	/*
	 * The memory backing the tables is encrypted. Use __sme_set() to adjust
	 * the page table pointers in the tree. This only works with
	 * CONFIG_AMD_MEM_ENCRYPT.
	 */
	PT_FEAT_AMDV1_ENCRYPT_TABLES = PT_FEAT_FMT_START,
	/*
	 * The PTEs are set to prevent cache incoherent traffic, such as PCI no
	 * snoop. This is set either at creation time or before the first map
	 * operation.
	 */
	PT_FEAT_AMDV1_FORCE_COHERENCE,
};

struct pt_vtdss {
	struct pt_common common;
};

enum {
	/*
	 * The PTEs are set to prevent cache incoherent traffic, such as PCI no
	 * snoop. This is set either at creation time or before the first map
	 * operation.
	 */
	PT_FEAT_VTDSS_FORCE_COHERENCE = PT_FEAT_FMT_START,
	/*
	 * Prevent creating read-only PTEs. Used to work around HW errata
	 * ERRATA_772415_SPR17.
	 */
	PT_FEAT_VTDSS_FORCE_WRITEABLE,
};

struct pt_x86_64 {
	struct pt_common common;
};

enum {
	/*
	 * The memory backing the tables is encrypted. Use __sme_set() to adjust
	 * the page table pointers in the tree. This only works with
	 * CONFIG_AMD_MEM_ENCRYPT.
	 */
	PT_FEAT_X86_64_AMD_ENCRYPT_TABLES = PT_FEAT_FMT_START,
};

#endif
