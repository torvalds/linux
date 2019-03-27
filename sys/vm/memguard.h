/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005,
 *     Bosko Milekic <bmilekic@FreeBSD.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VM_MEMGUARD_H_
#define	_VM_MEMGUARD_H_

#include "opt_vm.h"

struct malloc_type;
struct vm_map;
struct vmem;

#ifdef DEBUG_MEMGUARD
unsigned long	memguard_fudge(unsigned long, const struct vm_map *);
void	memguard_init(struct vmem *);
void 	*memguard_alloc(unsigned long, int);
void	*memguard_realloc(void *, unsigned long, struct malloc_type *, int);
void	memguard_free(void *);
int	memguard_cmp_mtp(struct malloc_type *, unsigned long);
int	memguard_cmp_zone(uma_zone_t);
int	is_memguard_addr(void *);
#else
#define	memguard_fudge(size, xxx)	(size)
#define	memguard_init(map)		do { } while (0)
#define	memguard_alloc(size, flags)	NULL
#define	memguard_realloc(a, s, mtp, f)	NULL
#define	memguard_free(addr)		do { } while (0)
#define	memguard_cmp_mtp(mtp, size)	0
#define	memguard_cmp_zone(zone)		0
#define	is_memguard_addr(addr)		0
#endif

#endif /* _VM_MEMGUARD_H_ */
