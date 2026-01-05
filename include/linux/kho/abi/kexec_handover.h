/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 * Copyright (C) 2025 Google LLC, Jason Miu <jasonmiu@google.com>
 */

#ifndef _LINUX_KHO_ABI_KEXEC_HANDOVER_H
#define _LINUX_KHO_ABI_KEXEC_HANDOVER_H

/**
 * DOC: Kexec Handover ABI
 *
 * Kexec Handover uses the ABI defined below for passing preserved data from
 * one kernel to the next.
 * The ABI uses Flattened Device Tree (FDT) format. The first kernel creates an
 * FDT which is then passed to the next kernel during a kexec handover.
 *
 * This interface is a contract. Any modification to the FDT structure, node
 * properties, compatible string, or the layout of the data structures
 * referenced here constitutes a breaking change. Such changes require
 * incrementing the version number in KHO_FDT_COMPATIBLE to prevent a new kernel
 * from misinterpreting data from an older kernel. Changes are allowed provided
 * the compatibility version is incremented. However, backward/forward
 * compatibility is only guaranteed for kernels supporting the same ABI version.
 *
 * FDT Structure Overview:
 *   The FDT serves as a central registry for physical
 *   addresses of preserved data structures and sub-FDTs. The first kernel
 *   populates this FDT with references to memory regions and other FDTs that
 *   need to persist across the kexec transition. The subsequent kernel then
 *   parses this FDT to locate and restore the preserved data.::
 *
 *     / {
 *         compatible = "kho-v1";
 *
 *         preserved-memory-map = <0x...>;
 *
 *         <subnode-name-1> {
 *             fdt = <0x...>;
 *         };
 *
 *         <subnode-name-2> {
 *             fdt = <0x...>;
 *         };
 *               ... ...
 *         <subnode-name-N> {
 *             fdt = <0x...>;
 *         };
 *     };
 *
 *   Root KHO Node (/):
 *     - compatible: "kho-v1"
 *
 *       Indentifies the overall KHO ABI version.
 *
 *     - preserved-memory-map: u64
 *
 *       Physical memory address pointing to the root of the
 *       preserved memory map data structure.
 *
 *   Subnodes (<subnode-name-N>):
 *     Subnodes can also be added to the root node to
 *     describe other preserved data blobs. The <subnode-name-N>
 *     is provided by the subsystem that uses KHO for preserving its
 *     data.
 *
 *     - fdt: u64
 *
 *       Physical address pointing to a subnode FDT blob that is also
 *       being preserved.
 */

/* The compatible string for the KHO FDT root node. */
#define KHO_FDT_COMPATIBLE "kho-v1"

/* The FDT property for the preserved memory map. */
#define KHO_FDT_MEMORY_MAP_PROP_NAME "preserved-memory-map"

/* The FDT property for sub-FDTs. */
#define KHO_FDT_SUB_TREE_PROP_NAME "fdt"

#endif	/* _LINUX_KHO_ABI_KEXEC_HANDOVER_H */
