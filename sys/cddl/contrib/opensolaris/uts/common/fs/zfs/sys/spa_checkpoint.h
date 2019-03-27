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
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPA_CHECKPOINT_H
#define	_SYS_SPA_CHECKPOINT_H

#include <sys/zthr.h>

typedef struct spa_checkpoint_info {
	uint64_t sci_timestamp; /* when checkpointed uberblock was synced  */
	uint64_t sci_dspace;    /* disk space used by checkpoint in bytes */
} spa_checkpoint_info_t;

int spa_checkpoint(const char *);
int spa_checkpoint_discard(const char *);

boolean_t spa_checkpoint_discard_thread_check(void *, zthr_t *);
int spa_checkpoint_discard_thread(void *, zthr_t *);

int spa_checkpoint_get_stats(spa_t *, pool_checkpoint_stat_t *);

#endif /* _SYS_SPA_CHECKPOINT_H */
