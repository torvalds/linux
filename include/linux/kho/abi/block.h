/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: KHO Serialization Blocks ABI
 *
 * Subsystems using the KHO Serialization Blocks framework rely on the stable
 * Application Binary Interface defined below to pass serialized state from a
 * pre-update kernel to a post-update kernel.
 *
 * This interface is a contract. Any modification to the structure fields,
 * compatible strings, or the layout of the `__packed` serialization
 * structures defined here constitutes a breaking change. Such changes require
 * incrementing the version number in the `KHO_FDT_COMPATIBLE` string to
 * prevent a new kernel from misinterpreting data from an old kernel.
 *
 * Changes are allowed provided the compatibility version is incremented;
 * however, backward/forward compatibility is only guaranteed for kernels
 * supporting the same ABI version.
 */

#ifndef _LINUX_KHO_ABI_BLOCK_H
#define _LINUX_KHO_ABI_BLOCK_H

#include <asm/page.h>
#include <linux/types.h>

/**
 * KHO_BLOCK_SIZE - The size of each serialization block.
 *
 * This is defined as PAGE_SIZE. PAGE_SIZE is ABI compliant because live
 * update between kernels with different page sizes is not supported by KHO.
 */
#define KHO_BLOCK_SIZE			PAGE_SIZE

/**
 * struct kho_block_header_ser - Header for the serialized data block.
 * @next:  Physical address of the next struct kho_block_header_ser.
 * @count: The number of entries that immediately follow this header in the
 *         memory block.
 *
 * This structure is located at the beginning of a block of physical memory
 * preserved across a kexec. It provides the necessary metadata to interpret
 * the array of entries that follow.
 */
struct kho_block_header_ser {
	u64 next;
	u64 count;
} __packed;

#endif /* _LINUX_KHO_ABI_BLOCK_H */
