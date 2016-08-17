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
 * Copyright (c) 2013, 2014 by Delphix. All rights reserved.
 */

#ifndef	_SYS_MULTILIST_H
#define	_SYS_MULTILIST_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef list_node_t multilist_node_t;
typedef struct multilist multilist_t;
typedef struct multilist_sublist multilist_sublist_t;
typedef unsigned int multilist_sublist_index_func_t(multilist_t *, void *);

struct multilist_sublist {
	/*
	 * The mutex used internally to implement thread safe insertions
	 * and removals to this individual sublist. It can also be locked
	 * by a consumer using multilist_sublist_{lock,unlock}, which is
	 * useful if a consumer needs to traverse the list in a thread
	 * safe manner.
	 */
	kmutex_t	mls_lock;
	/*
	 * The actual list object containing all objects in this sublist.
	 */
	list_t		mls_list;
	/*
	 * Pad to cache line, in an effort to try and prevent cache line
	 * contention.
	 */
} ____cacheline_aligned;

struct multilist {
	/*
	 * This is used to get to the multilist_node_t structure given
	 * the void *object contained on the list.
	 */
	size_t				ml_offset;
	/*
	 * The number of sublists used internally by this multilist.
	 */
	uint64_t			ml_num_sublists;
	/*
	 * The array of pointers to the actual sublists.
	 */
	multilist_sublist_t		*ml_sublists;
	/*
	 * Pointer to function which determines the sublist to use
	 * when inserting and removing objects from this multilist.
	 * Please see the comment above multilist_create for details.
	 */
	multilist_sublist_index_func_t	*ml_index_func;
};

void multilist_destroy(multilist_t *);
void multilist_create(multilist_t *, size_t, size_t, unsigned int,
    multilist_sublist_index_func_t *);

void multilist_insert(multilist_t *, void *);
void multilist_remove(multilist_t *, void *);
int  multilist_is_empty(multilist_t *);

unsigned int multilist_get_num_sublists(multilist_t *);
unsigned int multilist_get_random_index(multilist_t *);

multilist_sublist_t *multilist_sublist_lock(multilist_t *, unsigned int);
void multilist_sublist_unlock(multilist_sublist_t *);

void multilist_sublist_insert_head(multilist_sublist_t *, void *);
void multilist_sublist_insert_tail(multilist_sublist_t *, void *);
void multilist_sublist_move_forward(multilist_sublist_t *mls, void *obj);
void multilist_sublist_remove(multilist_sublist_t *, void *);

void *multilist_sublist_head(multilist_sublist_t *);
void *multilist_sublist_tail(multilist_sublist_t *);
void *multilist_sublist_next(multilist_sublist_t *, void *);
void *multilist_sublist_prev(multilist_sublist_t *, void *);

void multilist_link_init(multilist_node_t *);
int  multilist_link_active(multilist_node_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MULTILIST_H */
