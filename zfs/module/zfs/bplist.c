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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/bplist.h>
#include <sys/zfs_context.h>


void
bplist_create(bplist_t *bpl)
{
	mutex_init(&bpl->bpl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&bpl->bpl_list, sizeof (bplist_entry_t),
	    offsetof(bplist_entry_t, bpe_node));
}

void
bplist_destroy(bplist_t *bpl)
{
	list_destroy(&bpl->bpl_list);
	mutex_destroy(&bpl->bpl_lock);
}

void
bplist_append(bplist_t *bpl, const blkptr_t *bp)
{
	bplist_entry_t *bpe = kmem_alloc(sizeof (*bpe), KM_SLEEP);

	mutex_enter(&bpl->bpl_lock);
	bpe->bpe_blk = *bp;
	list_insert_tail(&bpl->bpl_list, bpe);
	mutex_exit(&bpl->bpl_lock);
}

/*
 * To aid debugging, we keep the most recently removed entry.  This way if
 * we are in the callback, we can easily locate the entry.
 */
static bplist_entry_t *bplist_iterate_last_removed;

void
bplist_iterate(bplist_t *bpl, bplist_itor_t *func, void *arg, dmu_tx_t *tx)
{
	bplist_entry_t *bpe;

	mutex_enter(&bpl->bpl_lock);
	while ((bpe = list_head(&bpl->bpl_list))) {
		bplist_iterate_last_removed = bpe;
		list_remove(&bpl->bpl_list, bpe);
		mutex_exit(&bpl->bpl_lock);
		func(arg, &bpe->bpe_blk, tx);
		kmem_free(bpe, sizeof (*bpe));
		mutex_enter(&bpl->bpl_lock);
	}
	mutex_exit(&bpl->bpl_lock);
}
