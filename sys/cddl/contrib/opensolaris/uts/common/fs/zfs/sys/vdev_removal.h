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
 * Copyright (c) 2014, 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_VDEV_REMOVAL_H
#define	_SYS_VDEV_REMOVAL_H

#include <sys/spa.h>
#include <sys/bpobj.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/vdev_indirect_births.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct spa_vdev_removal {
	uint64_t	svr_vdev_id;
	uint64_t	svr_max_offset_to_sync[TXG_SIZE];
	/* Thread performing a vdev removal. */
	kthread_t	*svr_thread;
	/* Segments left to copy from the current metaslab. */
	range_tree_t	*svr_allocd_segs;
	kmutex_t	svr_lock;
	kcondvar_t	svr_cv;
	boolean_t	svr_thread_exit;

	/*
	 * New mappings to write out each txg.
	 */
	list_t		svr_new_segments[TXG_SIZE];

	/*
	 * Ranges that were freed while a mapping was in flight.  This is
	 * a subset of the ranges covered by vdev_im_new_segments.
	 */
	range_tree_t	*svr_frees[TXG_SIZE];

	/*
	 * Number of bytes which we have finished our work for
	 * in each txg.  This could be data copied (which will be part of
	 * the mappings in vdev_im_new_segments), or data freed before
	 * we got around to copying it.
	 */
	uint64_t	svr_bytes_done[TXG_SIZE];

	/* List of leaf zap objects to be unlinked */
	nvlist_t	*svr_zaplist;
} spa_vdev_removal_t;

typedef struct spa_condensing_indirect {
	/*
	 * New mappings to write out each txg.
	 */
	list_t		sci_new_mapping_entries[TXG_SIZE];

	vdev_indirect_mapping_t *sci_new_mapping;
} spa_condensing_indirect_t;

extern int spa_remove_init(spa_t *);
extern void spa_restart_removal(spa_t *);
extern int spa_condense_init(spa_t *);
extern void spa_condense_fini(spa_t *);
extern void spa_start_indirect_condensing_thread(spa_t *);
extern void spa_vdev_condense_suspend(spa_t *);
extern int spa_vdev_remove(spa_t *, uint64_t, boolean_t);
extern void free_from_removing_vdev(vdev_t *, uint64_t, uint64_t);
extern int spa_removal_get_stats(spa_t *, pool_removal_stat_t *);
extern void svr_sync(spa_t *spa, dmu_tx_t *tx);
extern void spa_vdev_remove_suspend(spa_t *);
extern int spa_vdev_remove_cancel(spa_t *);
extern void spa_vdev_removal_destroy(spa_vdev_removal_t *svr);

extern int vdev_removal_max_span;
extern int zfs_remove_max_segment;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_REMOVAL_H */
