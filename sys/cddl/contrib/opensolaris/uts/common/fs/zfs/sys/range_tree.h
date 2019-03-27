/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_RANGE_TREE_H
#define	_SYS_RANGE_TREE_H

#include <sys/avl.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	RANGE_TREE_HISTOGRAM_SIZE	64

typedef struct range_tree_ops range_tree_ops_t;

/*
 * Note: the range_tree may not be accessed concurrently; consumers
 * must provide external locking if required.
 */
typedef struct range_tree {
	avl_tree_t	rt_root;	/* offset-ordered segment AVL tree */
	uint64_t	rt_space;	/* sum of all segments in the map */
	range_tree_ops_t *rt_ops;
	void		*rt_arg;

	/* rt_avl_compare should only be set it rt_arg is an AVL tree */
	uint64_t	rt_gap;		/* allowable inter-segment gap */
	int (*rt_avl_compare)(const void *, const void *);
	/*
	 * The rt_histogram maintains a histogram of ranges. Each bucket,
	 * rt_histogram[i], contains the number of ranges whose size is:
	 * 2^i <= size of range in bytes < 2^(i+1)
	 */
	uint64_t	rt_histogram[RANGE_TREE_HISTOGRAM_SIZE];
} range_tree_t;

typedef struct range_seg {
	avl_node_t	rs_node;	/* AVL node */
	avl_node_t	rs_pp_node;	/* AVL picker-private node */
	uint64_t	rs_start;	/* starting offset of this segment */
	uint64_t	rs_end;		/* ending offset (non-inclusive) */
	uint64_t	rs_fill;	/* actual fill if gap mode is on */
} range_seg_t;

struct range_tree_ops {
	void    (*rtop_create)(range_tree_t *rt, void *arg);
	void    (*rtop_destroy)(range_tree_t *rt, void *arg);
	void	(*rtop_add)(range_tree_t *rt, range_seg_t *rs, void *arg);
	void    (*rtop_remove)(range_tree_t *rt, range_seg_t *rs, void *arg);
	void	(*rtop_vacate)(range_tree_t *rt, void *arg);
};

typedef void range_tree_func_t(void *arg, uint64_t start, uint64_t size);

void range_tree_init(void);
void range_tree_fini(void);
range_tree_t *range_tree_create_impl(range_tree_ops_t *ops, void *arg,
    int (*avl_compare)(const void*, const void*), uint64_t gap);
	range_tree_t *range_tree_create(range_tree_ops_t *ops, void *arg);
void range_tree_destroy(range_tree_t *rt);
boolean_t range_tree_contains(range_tree_t *rt, uint64_t start, uint64_t size);
range_seg_t *range_tree_find(range_tree_t *rt, uint64_t start, uint64_t size);
void range_tree_resize_segment(range_tree_t *rt, range_seg_t *rs,
    uint64_t newstart, uint64_t newsize);
uint64_t range_tree_space(range_tree_t *rt);
boolean_t range_tree_is_empty(range_tree_t *rt);
void range_tree_verify(range_tree_t *rt, uint64_t start, uint64_t size);
void range_tree_swap(range_tree_t **rtsrc, range_tree_t **rtdst);
void range_tree_stat_verify(range_tree_t *rt);
uint64_t range_tree_min(range_tree_t *rt);
uint64_t range_tree_max(range_tree_t *rt);
uint64_t range_tree_span(range_tree_t *rt);

void range_tree_add(void *arg, uint64_t start, uint64_t size);
void range_tree_remove(void *arg, uint64_t start, uint64_t size);
void range_tree_remove_fill(range_tree_t *rt, uint64_t start, uint64_t size);
void range_tree_adjust_fill(range_tree_t *rt, range_seg_t *rs, int64_t delta);
void range_tree_clear(range_tree_t *rt, uint64_t start, uint64_t size);

void range_tree_vacate(range_tree_t *rt, range_tree_func_t *func, void *arg);
void range_tree_walk(range_tree_t *rt, range_tree_func_t *func, void *arg);
range_seg_t *range_tree_first(range_tree_t *rt);

void rt_avl_create(range_tree_t *rt, void *arg);
void rt_avl_destroy(range_tree_t *rt, void *arg);
void rt_avl_add(range_tree_t *rt, range_seg_t *rs, void *arg);
void rt_avl_remove(range_tree_t *rt, range_seg_t *rs, void *arg);
void rt_avl_vacate(range_tree_t *rt, void *arg);
extern struct range_tree_ops rt_avl_ops;

void rt_avl_create(range_tree_t *rt, void *arg);
void rt_avl_destroy(range_tree_t *rt, void *arg);
void rt_avl_add(range_tree_t *rt, range_seg_t *rs, void *arg);
void rt_avl_remove(range_tree_t *rt, range_seg_t *rs, void *arg);
void rt_avl_vacate(range_tree_t *rt, void *arg);
extern struct range_tree_ops rt_avl_ops;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RANGE_TREE_H */
