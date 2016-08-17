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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPACE_REFTREE_H
#define	_SYS_SPACE_REFTREE_H

#include <sys/range_tree.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct space_ref {
	avl_node_t	sr_node;	/* AVL node */
	uint64_t	sr_offset;	/* range offset (start or end) */
	int64_t		sr_refcnt;	/* associated reference count */
} space_ref_t;

void space_reftree_create(avl_tree_t *t);
void space_reftree_destroy(avl_tree_t *t);
void space_reftree_add_seg(avl_tree_t *t, uint64_t start, uint64_t end,
    int64_t refcnt);
void space_reftree_add_map(avl_tree_t *t, range_tree_t *rt, int64_t refcnt);
void space_reftree_generate_map(avl_tree_t *t, range_tree_t *rt,
    int64_t minref);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPACE_REFTREE_H */
