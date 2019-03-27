/*-
 * Copyright (c) 2013 Dmitry Chagin
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

#ifndef _LINUX_VDSO_H_
#define	_LINUX_VDSO_H_

#include <sys/types.h>

struct linux_vdso_sym {
	SLIST_ENTRY(linux_vdso_sym) sym;
	uint32_t	size;
	uintptr_t *	ptr;
	char		symname[];
};

vm_object_t __elfN(linux_shared_page_init)(char **);
void	__elfN(linux_shared_page_fini)(vm_object_t);
void	__elfN(linux_vdso_fixup)(struct sysentvec *);
void	__elfN(linux_vdso_reloc)(struct sysentvec *);
void	__elfN(linux_vdso_sym_init)(struct linux_vdso_sym *);

#define	LINUX_VDSO_SYM_INTPTR(name)				\
uintptr_t name;							\
LINUX_VDSO_SYM_DEFINE(name)

#define	LINUX_VDSO_SYM_CHAR(name)				\
const char * name;						\
LINUX_VDSO_SYM_DEFINE(name)

#define	LINUX_VDSO_SYM_DEFINE(name)				\
static struct linux_vdso_sym name ## sym = {			\
	.symname	= #name,				\
	.size		= sizeof(#name),			\
	.ptr		= (uintptr_t *)&name			\
};								\
SYSINIT(__elfN(name ## _sym_init), SI_SUB_EXEC,			\
    SI_ORDER_FIRST, __elfN(linux_vdso_sym_init), &name ## sym);	\
struct __hack

#endif	/* _LINUX_VDSO_H_ */
