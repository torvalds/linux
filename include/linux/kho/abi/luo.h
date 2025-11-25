/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: Live Update Orchestrator ABI
 *
 * This header defines the stable Application Binary Interface used by the
 * Live Update Orchestrator to pass state from a pre-update kernel to a
 * post-update kernel. The ABI is built upon the Kexec HandOver framework
 * and uses a Flattened Device Tree to describe the preserved data.
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
 *     };
 *
 * Main LUO Node (/):
 *
 *   - compatible: "luo-v1"
 *     Identifies the overall LUO ABI version.
 *   - liveupdate-number: u64
 *     A counter tracking the number of successful live updates performed.
 */

#ifndef _LINUX_KHO_ABI_LUO_H
#define _LINUX_KHO_ABI_LUO_H

/*
 * The LUO FDT hooks all LUO state for sessions, fds, etc.
 * In the root it also carries "liveupdate-number" 64-bit property that
 * corresponds to the number of live-updates performed on this machine.
 */
#define LUO_FDT_SIZE		PAGE_SIZE
#define LUO_FDT_KHO_ENTRY_NAME	"LUO"
#define LUO_FDT_COMPATIBLE	"luo-v1"
#define LUO_FDT_LIVEUPDATE_NUM	"liveupdate-number"

#endif /* _LINUX_KHO_ABI_LUO_H */
