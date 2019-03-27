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
 *
 * Portions Copyright 2006-2008 John Birrell jb@freebsd.org
 *
 * $FreeBSD$
 *
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _FBT_H_
#define _FBT_H_

#include "fbt_isa.h"

/*
 * fbt_probe is a bit of a misnomer.  One of these structures is created for
 * each trace point of an FBT probe.  A probe might have multiple trace points
 * (e.g., a function with multiple return instructions), and different probes
 * might have a trace point at the same address (e.g., GNU ifuncs).
 */
typedef struct fbt_probe {
	struct fbt_probe *fbtp_hashnext;	/* global hash table linkage */
	struct fbt_probe *fbtp_tracenext;	/* next probe for tracepoint */
	struct fbt_probe *fbtp_probenext;	/* next tracepoint for probe */
	int		fbtp_enabled;
	fbt_patchval_t  *fbtp_patchpoint;
	int8_t		fbtp_rval;
	fbt_patchval_t	fbtp_patchval;
	fbt_patchval_t	fbtp_savedval;
	uintptr_t	fbtp_roffset;
	dtrace_id_t	fbtp_id;
	const char	*fbtp_name;
	modctl_t	*fbtp_ctl;
	int		fbtp_loadcnt;
	int		fbtp_symindx;
} fbt_probe_t;

struct linker_file;
struct linker_symval;
struct trapframe;

int	fbt_invop(uintptr_t, struct trapframe *, uintptr_t);
void	fbt_patch_tracepoint(fbt_probe_t *, fbt_patchval_t);
int	fbt_provide_module_function(struct linker_file *, int,
	    struct linker_symval *, void *);
int	fbt_excluded(const char *name);

extern dtrace_provider_id_t	fbt_id;
extern fbt_probe_t		**fbt_probetab;
extern int			fbt_probetab_mask;

#define	FBT_ADDR2NDX(addr)	((((uintptr_t)(addr)) >> 4) & fbt_probetab_mask)
#define	FBT_PROBETAB_SIZE	0x8000		/* 32k entries -- 128K total */

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_FBT);
#endif

#endif
