/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Mark R V Murray
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/rwlock.h>
#include <sys/systm.h>

static struct rwlock	mr_lock;

/*
 * Implementation-neutral, kernel-callable functions for manipulating
 * memory range attributes.
 */
void
mem_range_init(void)
{

	if (mem_range_softc.mr_op == NULL)
		return;
	rw_init(&mr_lock, "memrange");
	mem_range_softc.mr_op->init(&mem_range_softc);
}

void
mem_range_destroy(void)
{

	if (mem_range_softc.mr_op == NULL)
		return;
	rw_destroy(&mr_lock);
}

int
mem_range_attr_get(struct mem_range_desc *mrd, int *arg)
{
	int nd;

	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);
	nd = *arg;
	rw_rlock(&mr_lock);
	if (nd == 0)
		*arg = mem_range_softc.mr_ndesc;
	else
		bcopy(mem_range_softc.mr_desc, mrd, nd * sizeof(*mrd));
	rw_runlock(&mr_lock);
	return (0);
}

int
mem_range_attr_set(struct mem_range_desc *mrd, int *arg)
{
	int ret;

	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);
	rw_wlock(&mr_lock);
	ret = mem_range_softc.mr_op->set(&mem_range_softc, mrd, arg);
	rw_wunlock(&mr_lock);
	return (ret);
}
