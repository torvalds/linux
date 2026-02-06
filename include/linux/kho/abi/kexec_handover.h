/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (C) 2023 Alexander Graf <graf@amazon.com>
 * Copyright (C) 2025 Microsoft Corporation, Mike Rapoport <rppt@kernel.org>
 * Copyright (C) 2025 Google LLC, Changyuan Lyu <changyuanl@google.com>
 * Copyright (C) 2025 Google LLC, Jason Miu <jasonmiu@google.com>
 */

#ifndef _LINUX_KHO_ABI_KEXEC_HANDOVER_H
#define _LINUX_KHO_ABI_KEXEC_HANDOVER_H

#include <linux/bits.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/types.h>

#include <asm/page.h>

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
 *   The FDT serves as a central registry for physical addresses of preserved
 *   data structures. The first kernel populates this FDT with references to
 *   memory regions and other metadata that need to persist across the kexec
 *   transition. The subsequent kernel then parses this FDT to locate and
 *   restore the preserved data.::
 *
 *     / {
 *         compatible = "kho-v2";
 *
 *         preserved-memory-map = <0x...>;
 *
 *         <subnode-name-1> {
 *             preserved-data = <0x...>;
 *         };
 *
 *         <subnode-name-2> {
 *             preserved-data = <0x...>;
 *         };
 *               ... ...
 *         <subnode-name-N> {
 *             preserved-data = <0x...>;
 *         };
 *     };
 *
 *   Root KHO Node (/):
 *     - compatible: "kho-v2"
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
 *     - preserved-data: u64
 *
 *       Physical address pointing to a subnode data blob that is also
 *       being preserved.
 */

/* The compatible string for the KHO FDT root node. */
#define KHO_FDT_COMPATIBLE "kho-v2"

/* The FDT property for the preserved memory map. */
#define KHO_FDT_MEMORY_MAP_PROP_NAME "preserved-memory-map"

/* The FDT property for preserved data blobs. */
#define KHO_FDT_SUB_TREE_PROP_NAME "preserved-data"

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

/**
 * DOC: KHO persistent memory tracker
 *
 * KHO tracks preserved memory using a radix tree data structure. Each node of
 * the tree is exactly a single page. The leaf nodes are bitmaps where each set
 * bit is a preserved page of any order. The intermediate nodes are tables of
 * physical addresses that point to a lower level node.
 *
 * The tree hierarchy is shown below::
 *
 *   root
 *   +-------------------+
 *   |     Level 5       | (struct kho_radix_node)
 *   +-------------------+
 *     |
 *     v
 *   +-------------------+
 *   |     Level 4       | (struct kho_radix_node)
 *   +-------------------+
 *     |
 *     | ... (intermediate levels)
 *     |
 *     v
 *   +-------------------+
 *   |      Level 0      | (struct kho_radix_leaf)
 *   +-------------------+
 *
 * The tree is traversed using a key that encodes the page's physical address
 * (pa) and its order into a single unsigned long value. The encoded key value
 * is composed of two parts: the 'order bit' in the upper part and the
 * 'shifted physical address' in the lower part.::
 *
 *   +------------+-----------------------------+--------------------------+
 *   | Page Order | Order Bit                   | Shifted Physical Address |
 *   +------------+-----------------------------+--------------------------+
 *   | 0          | ...000100 ... (at bit 52)   | pa >> (PAGE_SHIFT + 0)   |
 *   | 1          | ...000010 ... (at bit 51)   | pa >> (PAGE_SHIFT + 1)   |
 *   | 2          | ...000001 ... (at bit 50)   | pa >> (PAGE_SHIFT + 2)   |
 *   | ...        | ...                         | ...                      |
 *   +------------+-----------------------------+--------------------------+
 *
 * Shifted Physical Address:
 * The 'shifted physical address' is the physical address normalized for its
 * order. It effectively represents the PFN shifted right by the order.
 *
 * Order Bit:
 * The 'order bit' encodes the page order by setting a single bit at a
 * specific position. The position of this bit itself represents the order.
 *
 * For instance, on a 64-bit system with 4KB pages (PAGE_SHIFT = 12), the
 * maximum range for the shifted physical address (for order 0) is 52 bits
 * (64 - 12). This address occupies bits [0-51]. For order 0, the order bit is
 * set at position 52.
 *
 * The following diagram illustrates how the encoded key value is split into
 * indices for the tree levels, with PAGE_SIZE of 4KB::
 *
 *        63:60   59:51    50:42    41:33    32:24    23:15         14:0
 *   +---------+--------+--------+--------+--------+--------+-----------------+
 *   |    0    |  Lv 5  |  Lv 4  |  Lv 3  |  Lv 2  |  Lv 1  |  Lv 0 (bitmap)  |
 *   +---------+--------+--------+--------+--------+--------+-----------------+
 *
 * The radix tree stores pages of all orders in a single 6-level hierarchy. It
 * efficiently shares higher tree levels, especially due to common zero top
 * address bits, allowing a single, efficient algorithm to manage all
 * pages. This bitmap approach also offers memory efficiency; for example, a
 * 512KB bitmap can cover a 16GB memory range for 0-order pages with PAGE_SIZE =
 * 4KB.
 *
 * The data structures defined here are part of the KHO ABI. Any modification
 * to these structures that breaks backward compatibility must be accompanied by
 * an update to the "compatible" string. This ensures that a newer kernel can
 * correctly interpret the data passed by an older kernel.
 */

/*
 * Defines constants for the KHO radix tree structure, used to track preserved
 * memory. These constants govern the indexing, sizing, and depth of the tree.
 */
enum kho_radix_consts {
	/*
	 * The bit position of the order bit (and also the length of the
	 * shifted physical address) for an order-0 page.
	 */
	KHO_ORDER_0_LOG2 = 64 - PAGE_SHIFT,

	/* Size of the table in kho_radix_node, in log2 */
	KHO_TABLE_SIZE_LOG2 = const_ilog2(PAGE_SIZE / sizeof(phys_addr_t)),

	/* Number of bits in the kho_radix_leaf bitmap, in log2 */
	KHO_BITMAP_SIZE_LOG2 = PAGE_SHIFT + const_ilog2(BITS_PER_BYTE),

	/*
	 * The total tree depth is the number of intermediate levels
	 * and 1 bitmap level.
	 */
	KHO_TREE_MAX_DEPTH =
		DIV_ROUND_UP(KHO_ORDER_0_LOG2 - KHO_BITMAP_SIZE_LOG2,
			     KHO_TABLE_SIZE_LOG2) + 1,
};

struct kho_radix_node {
	u64 table[1 << KHO_TABLE_SIZE_LOG2];
};

struct kho_radix_leaf {
	DECLARE_BITMAP(bitmap, 1 << KHO_BITMAP_SIZE_LOG2);
};

#endif	/* _LINUX_KHO_ABI_KEXEC_HANDOVER_H */
