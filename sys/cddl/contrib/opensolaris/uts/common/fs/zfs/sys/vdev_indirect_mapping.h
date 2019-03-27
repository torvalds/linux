/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2015 by Delphix. All rights reserved.
 */

#ifndef	_SYS_VDEV_INDIRECT_MAPPING_H
#define	_SYS_VDEV_INDIRECT_MAPPING_H

#include <sys/dmu.h>
#include <sys/list.h>
#include <sys/spa.h>
#include <sys/space_map.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct vdev_indirect_mapping_entry_phys {
	/*
	 * Decode with DVA_MAPPING_* macros.
	 * Contains:
	 *   the source offset (low 63 bits)
	 *   the one-bit "mark", used for garbage collection (by zdb)
	 */
	uint64_t vimep_src;

	/*
	 * Note: the DVA's asize is 24 bits, and can thus store ranges
	 * up to 8GB.
	 */
	dva_t	vimep_dst;
} vdev_indirect_mapping_entry_phys_t;

#define	DVA_MAPPING_GET_SRC_OFFSET(vimep)	\
	BF64_GET_SB((vimep)->vimep_src, 0, 63, SPA_MINBLOCKSHIFT, 0)
#define	DVA_MAPPING_SET_SRC_OFFSET(vimep, x)	\
	BF64_SET_SB((vimep)->vimep_src, 0, 63, SPA_MINBLOCKSHIFT, 0, x)

typedef struct vdev_indirect_mapping_entry {
	vdev_indirect_mapping_entry_phys_t	vime_mapping;
	uint32_t				vime_obsolete_count;
	list_node_t				vime_node;
} vdev_indirect_mapping_entry_t;

/*
 * This is stored in the bonus buffer of the mapping object, see comment of
 * vdev_indirect_config for more details.
 */
typedef struct vdev_indirect_mapping_phys {
	uint64_t	vimp_max_offset;
	uint64_t	vimp_bytes_mapped;
	uint64_t	vimp_num_entries; /* number of v_i_m_entry_phys_t's */

	/*
	 * For each entry in the mapping object, this object contains an
	 * entry representing the number of bytes of that mapping entry
	 * that were no longer in use by the pool at the time this indirect
	 * vdev was last condensed.
	 */
	uint64_t	vimp_counts_object;
} vdev_indirect_mapping_phys_t;

#define	VDEV_INDIRECT_MAPPING_SIZE_V0	(3 * sizeof (uint64_t))

typedef struct vdev_indirect_mapping {
	uint64_t	vim_object;
	boolean_t	vim_havecounts;

	/*
	 * An ordered array of all mapping entries, sorted by source offset.
	 * Note that vim_entries is needed during a removal (and contains
	 * mappings that have been synced to disk so far) to handle frees
	 * from the removing device.
	 */
	vdev_indirect_mapping_entry_phys_t *vim_entries;

	objset_t	*vim_objset;

	dmu_buf_t	*vim_dbuf;
	vdev_indirect_mapping_phys_t	*vim_phys;
} vdev_indirect_mapping_t;

extern vdev_indirect_mapping_t *vdev_indirect_mapping_open(objset_t *os,
    uint64_t object);
extern void vdev_indirect_mapping_close(vdev_indirect_mapping_t *vim);
extern uint64_t vdev_indirect_mapping_alloc(objset_t *os, dmu_tx_t *tx);
extern void vdev_indirect_mapping_free(objset_t *os, uint64_t obj,
    dmu_tx_t *tx);

extern uint64_t vdev_indirect_mapping_num_entries(vdev_indirect_mapping_t *vim);
extern uint64_t vdev_indirect_mapping_max_offset(vdev_indirect_mapping_t *vim);
extern uint64_t vdev_indirect_mapping_object(vdev_indirect_mapping_t *vim);
extern uint64_t vdev_indirect_mapping_bytes_mapped(
    vdev_indirect_mapping_t *vim);
extern uint64_t vdev_indirect_mapping_size(vdev_indirect_mapping_t *vim);

/*
 * Writes the given list of vdev_indirect_mapping_entry_t to the mapping
 * then updates internal state.
 */
extern void vdev_indirect_mapping_add_entries(vdev_indirect_mapping_t *vim,
    list_t *vime_list, dmu_tx_t *tx);

extern vdev_indirect_mapping_entry_phys_t *
    vdev_indirect_mapping_entry_for_offset(vdev_indirect_mapping_t *vim,
    uint64_t offset);

extern vdev_indirect_mapping_entry_phys_t *
    vdev_indirect_mapping_entry_for_offset_or_next(vdev_indirect_mapping_t *vim,
    uint64_t offset);

extern uint32_t *vdev_indirect_mapping_load_obsolete_counts(
    vdev_indirect_mapping_t *vim);
extern void vdev_indirect_mapping_load_obsolete_spacemap(
    vdev_indirect_mapping_t *vim,
    uint32_t *counts, space_map_t *obsolete_space_sm);
extern void vdev_indirect_mapping_increment_obsolete_count(
    vdev_indirect_mapping_t *vim,
    uint64_t offset, uint64_t asize, uint32_t *counts);
extern void vdev_indirect_mapping_free_obsolete_counts(
    vdev_indirect_mapping_t *vim, uint32_t *counts);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_INDIRECT_MAPPING_H */
