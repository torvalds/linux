/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Copyright (C) 2025 Amazon.com Inc. or its affiliates.
 * Pratyush Yadav <ptyadav@amazon.de>
 */

#ifndef _LINUX_KHO_ABI_MEMFD_H
#define _LINUX_KHO_ABI_MEMFD_H

#include <linux/types.h>
#include <linux/kho/abi/kexec_handover.h>

/**
 * DOC: memfd Live Update ABI
 *
 * memfd uses the ABI defined below for preserving its state across a kexec
 * reboot using the LUO.
 *
 * The state is serialized into a packed structure `struct memfd_luo_ser`
 * which is handed over to the next kernel via the KHO mechanism.
 *
 * This interface is a contract. Any modification to the structure layout
 * constitutes a breaking change. Such changes require incrementing the
 * version number in the MEMFD_LUO_FH_COMPATIBLE string.
 */

/**
 * MEMFD_LUO_FOLIO_DIRTY - The folio is dirty.
 *
 * This flag indicates the folio contains data from user. A non-dirty folio is
 * one that was allocated (say using fallocate(2)) but not written to.
 */
#define MEMFD_LUO_FOLIO_DIRTY		BIT(0)

/**
 * MEMFD_LUO_FOLIO_UPTODATE - The folio is up-to-date.
 *
 * An up-to-date folio has been zeroed out. shmem zeroes out folios on first
 * use. This flag tracks which folios need zeroing.
 */
#define MEMFD_LUO_FOLIO_UPTODATE	BIT(1)

/**
 * struct memfd_luo_folio_ser - Serialized state of a single folio.
 * @pfn:       The page frame number of the folio.
 * @flags:     Flags to describe the state of the folio.
 * @index:     The page offset (pgoff_t) of the folio within the original file.
 */
struct memfd_luo_folio_ser {
	u64 pfn:52;
	u64 flags:12;
	u64 index;
} __packed;

/**
 * struct memfd_luo_ser - Main serialization structure for a memfd.
 * @pos:       The file's current position (f_pos).
 * @size:      The total size of the file in bytes (i_size).
 * @nr_folios: Number of folios in the folios array.
 * @folios:    KHO vmalloc descriptor pointing to the array of
 *             struct memfd_luo_folio_ser.
 */
struct memfd_luo_ser {
	u64 pos;
	u64 size;
	u64 nr_folios;
	struct kho_vmalloc folios;
} __packed;

/* The compatibility string for memfd file handler */
#define MEMFD_LUO_FH_COMPATIBLE	"memfd-v1"

#endif /* _LINUX_KHO_ABI_MEMFD_H */
