/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 * Copyright (C) 2025 Google LLC, Jason Miu <jasonmiu@google.com>
 */

#ifndef _LINUX_KHO_ABI_KEXEC_HANDOVER_H
#define _LINUX_KHO_ABI_KEXEC_HANDOVER_H

#include <linux/types.h>

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

/**
 * DOC: Kexec Handover ABI for vmalloc Preservation
 *
 * The Kexec Handover ABI for preserving vmalloc'ed memory is defined by
 * a set of structures and helper macros. The layout of these structures is a
 * stable contract between kernels and is versioned by the KHO_FDT_COMPATIBLE
 * string.
 *
 * The preservation is managed through a main descriptor &struct kho_vmalloc,
 * which points to a linked list of &struct kho_vmalloc_chunk structures. These
 * chunks contain the physical addresses of the preserved pages, allowing the
 * next kernel to reconstruct the vmalloc area with the same content and layout.
 * Helper macros are also defined for storing and loading pointers within
 * these structures.
 */

/* Helper macro to define a union for a serializable pointer. */
#define DECLARE_KHOSER_PTR(name, type)	\
	union {                        \
		u64 phys;              \
		type ptr;              \
	} name

/* Stores the physical address of a serializable pointer. */
#define KHOSER_STORE_PTR(dest, val)               \
	({                                        \
		typeof(val) v = val;              \
		typecheck(typeof((dest).ptr), v); \
		(dest).phys = virt_to_phys(v);    \
	})

/* Loads the stored physical address back to a pointer. */
#define KHOSER_LOAD_PTR(src)						\
	({                                                                   \
		typeof(src) s = src;                                         \
		(typeof((s).ptr))((s).phys ? phys_to_virt((s).phys) : NULL); \
	})

/*
 * This header is embedded at the beginning of each `kho_vmalloc_chunk`
 * and contains a pointer to the next chunk in the linked list,
 * stored as a physical address for handover.
 */
struct kho_vmalloc_hdr {
	DECLARE_KHOSER_PTR(next, struct kho_vmalloc_chunk *);
};

#define KHO_VMALLOC_SIZE				\
	((PAGE_SIZE - sizeof(struct kho_vmalloc_hdr)) / \
	 sizeof(u64))

/*
 * Each chunk is a single page and is part of a linked list that describes
 * a preserved vmalloc area. It contains the header with the link to the next
 * chunk and a zero terminated array of physical addresses of the pages that
 * make up the preserved vmalloc area.
 */
struct kho_vmalloc_chunk {
	struct kho_vmalloc_hdr hdr;
	u64 phys[KHO_VMALLOC_SIZE];
};

static_assert(sizeof(struct kho_vmalloc_chunk) == PAGE_SIZE);

/*
 * Describes a preserved vmalloc memory area, including the
 * total number of pages, allocation flags, page order, and a pointer to the
 * first chunk of physical page addresses.
 */
struct kho_vmalloc {
	DECLARE_KHOSER_PTR(first, struct kho_vmalloc_chunk *);
	unsigned int total_pages;
	unsigned short flags;
	unsigned short order;
};

#endif	/* _LINUX_KHO_ABI_KEXEC_HANDOVER_H */
