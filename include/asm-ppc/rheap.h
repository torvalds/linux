/*
 * include/asm-ppc/rheap.c
 *
 * Header file for the implementation of a remote heap.
 *
 * Author: Pantelis Antoniou <panto@intracom.gr>
 *
 * 2004 (c) INTRACOM S.A. Greece. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASM_PPC_RHEAP_H__
#define __ASM_PPC_RHEAP_H__

#include <linux/list.h>

typedef struct _rh_block {
	struct list_head list;
	void *start;
	int size;
	const char *owner;
} rh_block_t;

typedef struct _rh_info {
	unsigned int alignment;
	int max_blocks;
	int empty_slots;
	rh_block_t *block;
	struct list_head empty_list;
	struct list_head free_list;
	struct list_head taken_list;
	unsigned int flags;
} rh_info_t;

#define RHIF_STATIC_INFO	0x1
#define RHIF_STATIC_BLOCK	0x2

typedef struct rh_stats_t {
	void *start;
	int size;
	const char *owner;
} rh_stats_t;

#define RHGS_FREE	0
#define RHGS_TAKEN	1

/* Create a remote heap dynamically */
extern rh_info_t *rh_create(unsigned int alignment);

/* Destroy a remote heap, created by rh_create() */
extern void rh_destroy(rh_info_t * info);

/* Initialize in place a remote info block */
extern void rh_init(rh_info_t * info, unsigned int alignment, int max_blocks,
		    rh_block_t * block);

/* Attach a free region to manage */
extern int rh_attach_region(rh_info_t * info, void *start, int size);

/* Detach a free region */
extern void *rh_detach_region(rh_info_t * info, void *start, int size);

/* Allocate the given size from the remote heap */
extern void *rh_alloc(rh_info_t * info, int size, const char *owner);

/* Allocate the given size from the given address */
extern void *rh_alloc_fixed(rh_info_t * info, void *start, int size,
			    const char *owner);

/* Free the allocated area */
extern int rh_free(rh_info_t * info, void *start);

/* Get stats for debugging purposes */
extern int rh_get_stats(rh_info_t * info, int what, int max_stats,
			rh_stats_t * stats);

/* Simple dump of remote heap info */
extern void rh_dump(rh_info_t * info);

/* Set owner of taken block */
extern int rh_set_owner(rh_info_t * info, void *start, const char *owner);

#endif				/* __ASM_PPC_RHEAP_H__ */
