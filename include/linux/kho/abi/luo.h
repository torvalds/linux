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
 * kernel. The ABI is built upon the Kexec HandOver framework and uses a
 * Flattened Device Tree to describe the preserved data.
 *
 * This interface is a contract. Any modification to the FDT structure, node
 * properties, compatible strings, or the layout of the `__packed` serialization
 * structures defined here constitutes a breaking change. Such changes require
 * incrementing the version number in the relevant `_COMPATIBLE` string to
 * prevent a new kernel from misinterpreting data from an old kernel.
 *
 * Changes are allowed provided the compatibility version is incremented;
 * however, backward/forward compatibility is only guaranteed for kernels
 * supporting the same ABI version.
 *
 * FDT Structure Overview:
 *   The entire LUO state is encapsulated within a single KHO entry named "LUO".
 *   This entry contains an FDT with the following layout:
 *
 *   .. code-block:: none
 *
 *     / {
 *         compatible = "luo-v1";
 *         liveupdate-number = <...>;
 *
 *         luo-session {
 *             compatible = "luo-session-v1";
 *             luo-session-header = <phys_addr_of_session_header_ser>;
 *         };
 *
 *         luo-flb {
 *             compatible = "luo-flb-v1";
 *             luo-flb-header = <phys_addr_of_flb_header_ser>;
 *         };
 *     };
 *
 * Main LUO Node (/):
 *
 *   - compatible: "luo-v1"
 *     Identifies the overall LUO ABI version.
 *   - liveupdate-number: u64
 *     A counter tracking the number of successful live updates performed.
 *
 * Session Node (luo-session):
 *   This node describes all preserved user-space sessions.
 *
 *   - compatible: "luo-session-v1"
 *     Identifies the session ABI version.
 *   - luo-session-header: u64
 *     The physical address of a `struct luo_session_header_ser`. This structure
 *     is the header for a contiguous block of memory containing an array of
 *     `struct luo_session_ser`, one for each preserved session.
 *
 * File-Lifecycle-Bound Node (luo-flb):
 *   This node describes all preserved global objects whose lifecycle is bound
 *   to that of the preserved files (e.g., shared IOMMU state).
 *
 *   - compatible: "luo-flb-v1"
 *     Identifies the FLB ABI version.
 *   - luo-flb-header: u64
 *     The physical address of a `struct luo_flb_header_ser`. This structure is
 *     the header for a contiguous block of memory containing an array of
 *     `struct luo_flb_ser`, one for each preserved global object.
 *
 * Serialization Structures:
 *   The FDT properties point to memory regions containing arrays of simple,
 *   `__packed` structures. These structures contain the actual preserved state.
 *
 *   - struct luo_session_header_ser:
 *     Header for the session array. Contains the total page count of the
 *     preserved memory block and the number of `struct luo_session_ser`
 *     entries that follow.
 *
 *   - struct luo_session_ser:
 *     Metadata for a single session, including its name and a physical pointer
 *     to another preserved memory block containing an array of
 *     `struct luo_file_ser` for all files in that session.
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

#include <uapi/linux/liveupdate.h>

/*
 * The LUO FDT hooks all LUO state for sessions, fds, etc.
 * In the root it also carries "liveupdate-number" 64-bit property that
 * corresponds to the number of live-updates performed on this machine.
 */
#define LUO_FDT_SIZE		PAGE_SIZE
#define LUO_FDT_KHO_ENTRY_NAME	"LUO"
#define LUO_FDT_COMPATIBLE	"luo-v1"
#define LUO_FDT_LIVEUPDATE_NUM	"liveupdate-number"

#define LIVEUPDATE_HNDL_COMPAT_LENGTH	48

/**
 * struct luo_file_ser - Represents the serialized preserves files.
 * @compatible:  File handler compatible string.
 * @data:        Private data
 * @token:       User provided token for this file
 *
 * If this structure is modified, LUO_SESSION_COMPATIBLE must be updated.
 */
struct luo_file_ser {
	char compatible[LIVEUPDATE_HNDL_COMPAT_LENGTH];
	u64 data;
	u64 token;
} __packed;

/**
 * struct luo_file_set_ser - Represents the serialized metadata for file set
 * @files:   The physical address of a contiguous memory block that holds
 *           the serialized state of files (array of luo_file_ser) in this file
 *           set.
 * @count:   The total number of files that were part of this session during
 *           serialization. Used for iteration and validation during
 *           restoration.
 */
struct luo_file_set_ser {
	u64 files;
	u64 count;
} __packed;

/*
 * LUO FDT session node
 * LUO_FDT_SESSION_HEADER:  is a u64 physical address of struct
 *                          luo_session_header_ser
 */
#define LUO_FDT_SESSION_NODE_NAME	"luo-session"
#define LUO_FDT_SESSION_COMPATIBLE	"luo-session-v2"
#define LUO_FDT_SESSION_HEADER		"luo-session-header"

/**
 * struct luo_session_header_ser - Header for the serialized session data block.
 * @count: The number of `struct luo_session_ser` entries that immediately
 *         follow this header in the memory block.
 *
 * This structure is located at the beginning of a contiguous block of
 * physical memory preserved across the kexec. It provides the necessary
 * metadata to interpret the array of session entries that follow.
 *
 * If this structure is modified, `LUO_FDT_SESSION_COMPATIBLE` must be updated.
 */
struct luo_session_header_ser {
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
 * If this structure is modified, `LUO_FDT_SESSION_COMPATIBLE` must be updated.
 */
struct luo_session_ser {
	char name[LIVEUPDATE_SESSION_NAME_LENGTH];
	struct luo_file_set_ser file_set_ser;
} __packed;

/* The max size is set so it can be reliably used during in serialization */
#define LIVEUPDATE_FLB_COMPAT_LENGTH	48

#define LUO_FDT_FLB_NODE_NAME	"luo-flb"
#define LUO_FDT_FLB_COMPATIBLE	"luo-flb-v1"
#define LUO_FDT_FLB_HEADER	"luo-flb-header"

/**
 * struct luo_flb_header_ser - Header for the serialized FLB data block.
 * @pgcnt: The total number of pages occupied by the entire preserved memory
 *         region, including this header and the subsequent array of
 *         &struct luo_flb_ser entries.
 * @count: The number of &struct luo_flb_ser entries that follow this header
 *         in the memory block.
 *
 * This structure is located at the physical address specified by the
 * `LUO_FDT_FLB_HEADER` FDT property. It provides the new kernel with the
 * necessary information to find and iterate over the array of preserved
 * File-Lifecycle-Bound objects and to manage the underlying memory.
 *
 * If this structure is modified, LUO_FDT_FLB_COMPATIBLE must be updated.
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
 * If this structure is modified, LUO_FDT_FLB_COMPATIBLE must be updated.
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
