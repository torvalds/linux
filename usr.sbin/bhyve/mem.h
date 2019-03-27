/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MEM_H_
#define	_MEM_H_

#include <sys/linker_set.h>

struct vmctx;

typedef int (*mem_func_t)(struct vmctx *ctx, int vcpu, int dir, uint64_t addr,
			  int size, uint64_t *val, void *arg1, long arg2);

struct mem_range {
	const char 	*name;
	int		flags;
	mem_func_t	handler;
	void		*arg1;
	long		arg2;
	uint64_t  	base;
	uint64_t  	size;
};
#define	MEM_F_READ		0x1
#define	MEM_F_WRITE		0x2
#define	MEM_F_RW		0x3
#define	MEM_F_IMMUTABLE		0x4	/* mem_range cannot be unregistered */

void	init_mem(void);
int     emulate_mem(struct vmctx *, int vcpu, uint64_t paddr, struct vie *vie,
		    struct vm_guest_paging *paging);

int	read_mem(struct vmctx *ctx, int vcpu, uint64_t gpa, uint64_t *rval,
		 int size);
int	register_mem(struct mem_range *memp);
int	register_mem_fallback(struct mem_range *memp);
int	unregister_mem(struct mem_range *memp);

#endif	/* _MEM_H_ */
