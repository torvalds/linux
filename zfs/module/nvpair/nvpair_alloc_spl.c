/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at * usr/src/OPENSOLARIS.LICENSE
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/nvpair.h>
#include <sys/kmem.h>
#include <sys/vmem.h>

static void *
nv_alloc_sleep_spl(nv_alloc_t *nva, size_t size)
{
	return (vmem_alloc(size, KM_SLEEP));
}

static void *
nv_alloc_pushpage_spl(nv_alloc_t *nva, size_t size)
{
	return (vmem_alloc(size, KM_PUSHPAGE));
}

static void *
nv_alloc_nosleep_spl(nv_alloc_t *nva, size_t size)
{
	return (kmem_alloc(size, KM_NOSLEEP));
}

static void
nv_free_spl(nv_alloc_t *nva, void *buf, size_t size)
{
	kmem_free(buf, size);
}

const nv_alloc_ops_t spl_sleep_ops_def = {
	NULL,			/* nv_ao_init() */
	NULL,			/* nv_ao_fini() */
	nv_alloc_sleep_spl,	/* nv_ao_alloc() */
	nv_free_spl,		/* nv_ao_free() */
	NULL			/* nv_ao_reset() */
};

const nv_alloc_ops_t spl_pushpage_ops_def = {
	NULL,			/* nv_ao_init() */
	NULL,			/* nv_ao_fini() */
	nv_alloc_pushpage_spl,	/* nv_ao_alloc() */
	nv_free_spl,		/* nv_ao_free() */
	NULL			/* nv_ao_reset() */
};

const nv_alloc_ops_t spl_nosleep_ops_def = {
	NULL,			/* nv_ao_init() */
	NULL,			/* nv_ao_fini() */
	nv_alloc_nosleep_spl,	/* nv_ao_alloc() */
	nv_free_spl,		/* nv_ao_free() */
	NULL			/* nv_ao_reset() */
};

nv_alloc_t nv_alloc_sleep_def = {
	&spl_sleep_ops_def,
	NULL
};

nv_alloc_t nv_alloc_pushpage_def = {
	&spl_pushpage_ops_def,
	NULL
};

nv_alloc_t nv_alloc_nosleep_def = {
	&spl_nosleep_ops_def,
	NULL
};

nv_alloc_t *nv_alloc_sleep = &nv_alloc_sleep_def;
nv_alloc_t *nv_alloc_pushpage = &nv_alloc_pushpage_def;
nv_alloc_t *nv_alloc_nosleep = &nv_alloc_nosleep_def;
