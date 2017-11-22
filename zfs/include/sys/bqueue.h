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
 * Copyright (c) 2014 by Delphix. All rights reserved.
 */

#ifndef	_BQUEUE_H
#define	_BQUEUE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include	<sys/zfs_context.h>

typedef struct bqueue {
	list_t bq_list;
	kmutex_t bq_lock;
	kcondvar_t bq_add_cv;
	kcondvar_t bq_pop_cv;
	uint64_t bq_size;
	uint64_t bq_maxsize;
	size_t bq_node_offset;
} bqueue_t;

typedef struct bqueue_node {
	list_node_t bqn_node;
	uint64_t bqn_size;
} bqueue_node_t;


int bqueue_init(bqueue_t *, uint64_t, size_t);
void bqueue_destroy(bqueue_t *);
void bqueue_enqueue(bqueue_t *, void *, uint64_t);
void *bqueue_dequeue(bqueue_t *);
boolean_t bqueue_empty(bqueue_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _BQUEUE_H */
