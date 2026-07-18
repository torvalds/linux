/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: Live Update Orchestrator ABI
 *
 * Live Update Orchestrator uses the stable Application Binary Interface
 * defined below to pass state from a pre-update kernel to a post-update
 * kernel. The ABI is built upon the Kexec HandOver framework and registers
 * the central `struct luo_ser` via the KHO raw subtree API.
 *
 * This interface is a contract. Any modification to the structure fields,
 * compatible strings, or the layout of the `__packed` serialization
 * structures defined here constitutes a breaking change. Such changes require
 * incrementing the version number in the relevant `_COMPATIBLE` string to
 * prevent a new kernel from misinterpreting data from an old kernel.
 *
 * Changes are allowed provided the compatibility version is incremented;
 * however, backward/forward compatibility is only guaranteed for kernels
 * supporting the same ABI version.
 *
 * KHO Structure Overview:
 *   The entire LUO state is encapsulated within a single KHO entry named "LUO".
 *   This entry contains the `struct luo_ser` structure.
 *
 * Serialization Structures:
 *   - struct luo_ser:
 *     The central ABI structure that contains the overall state of the LUO.
 *     It includes the compatibility string, the liveupdate-number, and pointers
 *     to sessions and FLBs.
 *
 *   - struct luo_session_ser:
 *     Metadata for a single session, including its name and a physical pointer
 *     to the first `struct kho_block_header_ser` for all files in that session.
 *     Multiple blocks are linked via the `next` field in the header.
 *
 *   - struct luo_file_ser:
 *     Metadata for a single preserved file. Contains the `compatible` string to
 *     find the correct handler in the new kernel, a user-provided `token` for
 *     identification, and an opaque `data` handle for the handler to use.
 *
 *   - struct luo_flb_header_ser:
 *     Header for the FLB array. Contains the total page count of the
 *     preserved memory block and the number of `struct luo_flb_ser` entries
 *     that follow.
 *
 *   - struct luo_flb_ser:
 *     Metadata for a single preserved global object. Contains its `name`
 *     (compatible string), an opaque `data` handle, and the `count`
 *     number of files depending on it.
 */

#ifndef _LINUX_KHO_ABI_LUO_H
#define _LINUX_KHO_ABI_LUO_H

#include <linux/align.h>
#include <linux/kho/abi/block.h>
#include <uapi/linux/liveupdate.h>

/*
 * The LUO state is registered under this KHO entry name.
 */
#define LUO_KHO_ENTRY_NAME	"LUO"
#define LUO_ABI_COMPATIBLE	"luo-v5"
#define LUO_ABI_COMPAT_LEN	ALIGN(sizeof(LUO_ABI_COMPATIBLE), 8)

/**
 * struct luo_ser - Centralized LUO ABI header.
 * @compatible:     Compatibility string identifying the LUO ABI version.
 * @liveupdate_num: A counter tracking the number of successful live updates.
 * @sessions_pa:    Physical address of the first session block header.
 * @flbs_pa:        Physical address of the FLB header.
 *
 * This structure is the root of all preserved LUO state.
 */
struct luo_ser {
	char compatible[LUO_ABI_COMPAT_LEN];
	u64 liveupdate_num;
	u64 sessions_pa;
	u64 flbs_pa;
} __packed;

#define LIVEUPDATE_HNDL_COMPAT_LENGTH	48

/**
 * struct luo_file_ser - Represents the serialized preserves files.
 * @compatible:  File handler compatible string.
 * @data:        Private data
 * @token:       User provided token for this file
 *
 * If this structure is modified, `LUO_ABI_COMPATIBLE` must be updated.
 */
struct luo_file_ser {
	char compatible[LIVEUPDATE_HNDL_COMPAT_LENGTH];
	u64 data;
	u64 token;
} __packed;

/**
 * struct luo_file_set_ser - Represents the serialized metadata for file set
 * @files:   The physical address of the first `struct kho_block_header_ser`.
 *           This structure is the header for a block of memory containing
 *           an array of `struct luo_file_ser` entries. Multiple blocks are
 *           linked via the `next` field in the header.
 * @count:   The total number of files that were part of this session during
 *           serialization. Used for iteration and validation during
 *           restoration.
 */
struct luo_file_set_ser {
	u64 files;
	u64 count;
} __packed;

/**
 * struct luo_session_ser - Represents the serialized metadata for a LUO session.
 * @name:         The unique name of the session, provided by the userspace at
 *                the time of session creation.
 * @file_set_ser: Serialized files belonging to this session,
 *
 * This structure is used to package session-specific metadata for transfer
 * between kernels via Kexec Handover. An array of these structures (one per
 * session) is created and passed to the new kernel, allowing it to reconstruct
 * the session context.
 *
 * If this structure is modified, `LUO_ABI_COMPATIBLE` must be updated.
 */
struct luo_session_ser {
	char name[LIVEUPDATE_SESSION_NAME_LENGTH];
	struct luo_file_set_ser file_set_ser;
} __packed;

/* The max size is set so it can be reliably used during in serialization */
#define LIVEUPDATE_FLB_COMPAT_LENGTH	48

/**
 * struct luo_flb_header_ser - Header for the serialized FLB data block.
 * @pgcnt: The total number of pages occupied by the entire preserved memory
 *         region, including this header and the subsequent array of
 *         &struct luo_flb_ser entries.
 * @count: The number of &struct luo_flb_ser entries that follow this header
 *         in the memory block.
 *
 * This structure is located at the physical address specified by the
 * flbs_pa in luo_ser.
 *
 * If this structure is modified, `LUO_ABI_COMPATIBLE` must be updated.
 */
struct luo_flb_header_ser {
	u64 pgcnt;
	u64 count;
} __packed;

/**
 * struct luo_flb_ser - Represents the serialized state of a single FLB object.
 * @name:    The unique compatibility string of the FLB object, used to find the
 *           corresponding &struct liveupdate_flb handler in the new kernel.
 * @data:    The opaque u64 handle returned by the FLB's .preserve() operation
 *           in the old kernel. This handle encapsulates the entire state needed
 *           for restoration.
 * @count:   The reference count at the time of serialization; i.e., the number
 *           of preserved files that depended on this FLB. This is used by the
 *           new kernel to correctly manage the FLB's lifecycle.
 *
 * An array of these structures is created in a preserved memory region and
 * passed to the new kernel. Each entry allows the LUO core to restore one
 * global, shared object.
 *
 * If this structure is modified, `LUO_ABI_COMPATIBLE` must be updated.
 */
struct luo_flb_ser {
	char name[LIVEUPDATE_FLB_COMPAT_LENGTH];
	u64 data;
	u64 count;
} __packed;

/* Kernel Live Update Test ABI */
#ifdef CONFIG_LIVEUPDATE_TEST
#define LIVEUPDATE_TEST_FLB_COMPATIBLE(i)	"liveupdate-test-flb-v" #i
#endif

#endif /* _LINUX_KHO_ABI_LUO_H */
