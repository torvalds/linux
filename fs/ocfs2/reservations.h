/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * reservations.h
 *
 * Allocation reservations function prototypes and structures.
 *
 * Copyright (C) 2010 Novell.  All rights reserved.
 */

#ifndef	OCFS2_RESERVATIONS_H
#define	OCFS2_RESERVATIONS_H

#include <linux/rbtree.h>

#define OCFS2_DEFAULT_RESV_LEVEL	2
#define OCFS2_MAX_RESV_LEVEL	9
#define OCFS2_MIN_RESV_LEVEL	0

struct ocfs2_alloc_reservation {
	struct rb_node	r_node;

	unsigned int	r_start;	/* Beginning of current window */
	unsigned int	r_len;		/* Length of the window */

	unsigned int	r_last_len;	/* Length of most recent alloc */
	unsigned int	r_last_start;	/* Start of most recent alloc */
	struct list_head	r_lru;	/* LRU list head */

	unsigned int	r_flags;
};

#define	OCFS2_RESV_FLAG_INUSE	0x01	/* Set when r_node is part of a btree */
#define	OCFS2_RESV_FLAG_TMP	0x02	/* Temporary reservation, will be
					 * destroyed immediately after use */
#define	OCFS2_RESV_FLAG_DIR	0x04	/* Reservation is for an unindexed
					 * directory btree */

struct ocfs2_reservation_map {
	struct rb_root		m_reservations;
	char			*m_disk_bitmap;

	struct ocfs2_super	*m_osb;

	/* The following are not initialized to meaningful values until a disk
	 * bitmap is provided. */
	u32			m_bitmap_len;	/* Number of valid
						 * bits available */

	struct list_head	m_lru;		/* LRU of reservations
						 * structures. */

};

void ocfs2_resv_init_once(struct ocfs2_alloc_reservation *resv);

#define OCFS2_RESV_TYPES	(OCFS2_RESV_FLAG_TMP|OCFS2_RESV_FLAG_DIR)
void ocfs2_resv_set_type(struct ocfs2_alloc_reservation *resv,
			 unsigned int flags);

int ocfs2_dir_resv_allowed(struct ocfs2_super *osb);

/**
 * ocfs2_resv_discard() - truncate a reservation
 * @resmap:
 * @resv: the reservation to truncate.
 *
 * After this function is called, the reservation will be empty, and
 * unlinked from the rbtree.
 */
void ocfs2_resv_discard(struct ocfs2_reservation_map *resmap,
			struct ocfs2_alloc_reservation *resv);


/**
 * ocfs2_resmap_init() - Initialize fields of a reservations bitmap
 * @osb: struct ocfs2_super to be saved in resmap
 * @resmap: struct ocfs2_reservation_map to initialize
 */
void ocfs2_resmap_init(struct ocfs2_super *osb,
		      struct ocfs2_reservation_map *resmap);

/**
 * ocfs2_resmap_restart() - "restart" a reservation bitmap
 * @resmap: reservations bitmap
 * @clen: Number of valid bits in the bitmap
 * @disk_bitmap: the disk bitmap this resmap should refer to.
 *
 * Re-initialize the parameters of a reservation bitmap. This is
 * useful for local alloc window slides.
 *
 * This function will call ocfs2_trunc_resv against all existing
 * reservations. A future version will recalculate existing
 * reservations based on the new bitmap.
 */
void ocfs2_resmap_restart(struct ocfs2_reservation_map *resmap,
			  unsigned int clen, char *disk_bitmap);

/**
 * ocfs2_resmap_uninit() - uninitialize a reservation bitmap structure
 * @resmap: the struct ocfs2_reservation_map to uninitialize
 */
void ocfs2_resmap_uninit(struct ocfs2_reservation_map *resmap);

/**
 * ocfs2_resmap_resv_bits() - Return still-valid reservation bits
 * @resmap: reservations bitmap
 * @resv: reservation to base search from
 * @cstart: start of proposed allocation
 * @clen: length (in clusters) of proposed allocation
 *
 * Using the reservation data from resv, this function will compare
 * resmap and resmap->m_disk_bitmap to determine what part (if any) of
 * the reservation window is still clear to use. If resv is empty,
 * this function will try to allocate a window for it.
 *
 * On success, zero is returned and the valid allocation area is set in cstart
 * and clen.
 *
 * Returns -ENOSPC if reservations are disabled.
 */
int ocfs2_resmap_resv_bits(struct ocfs2_reservation_map *resmap,
			   struct ocfs2_alloc_reservation *resv,
			   int *cstart, int *clen);

/**
 * ocfs2_resmap_claimed_bits() - Tell the reservation code that bits were used.
 * @resmap: reservations bitmap
 * @resv: optional reservation to recalculate based on new bitmap
 * @cstart: start of allocation in clusters
 * @clen: end of allocation in clusters.
 *
 * Tell the reservation code that bits were used to fulfill allocation in
 * resmap. The bits don't have to have been part of any existing
 * reservation. But we must always call this function when bits are claimed.
 * Internally, the reservations code will use this information to mark the
 * reservations bitmap. If resv is passed, it's next allocation window will be
 * calculated. It also expects that 'cstart' is the same as we passed back
 * from ocfs2_resmap_resv_bits().
 */
void ocfs2_resmap_claimed_bits(struct ocfs2_reservation_map *resmap,
			       struct ocfs2_alloc_reservation *resv,
			       u32 cstart, u32 clen);

#endif	/* OCFS2_RESERVATIONS_H */
