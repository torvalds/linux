/*-
 * Copyright (c) 1999, 2000 John D. Polstra.
 * Copyright (c) 2014 the FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef RTLD_MACHDEP_H
#define	RTLD_MACHDEP_H	1

#include <sys/types.h>
#include <machine/atomic.h>

struct Struct_Obj_Entry;

/* Return the address of the .dynamic section in the dynamic linker. */
#define	rtld_dynamic(obj)						\
({									\
	Elf_Addr _dynamic_addr;						\
	asm volatile("adr	%0, _DYNAMIC" : "=&r"(_dynamic_addr));	\
	(const Elf_Dyn *)_dynamic_addr;					\
})

Elf_Addr reloc_jmpslot(Elf_Addr *where, Elf_Addr target,
    const struct Struct_Obj_Entry *defobj, const struct Struct_Obj_Entry *obj,
    const Elf_Rel *rel);

#define	make_function_pointer(def, defobj) \
	((defobj)->relocbase + (def)->st_value)

#define	call_initfini_pointer(obj, target) \
	(((InitFunc)(target))())

#define	call_init_pointer(obj, target) \
	(((InitArrFunc)(target))(main_argc, main_argv, environ))

/*
 * Pass zeros into the ifunc resolver so we can change them later. The first
 * 8 arguments on arm64 are passed in registers so make them known values
 * if we decide to use them later. Because of this ifunc resolvers can assume
 * no arguments are passeed in, and if this changes later will be able to
 * compare the argument with 0 to see if it is set.
 */
#define	call_ifunc_resolver(ptr) \
	(((Elf_Addr (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, \
	    uint64_t, uint64_t, uint64_t))ptr)(0, 0, 0, 0, 0, 0, 0, 0))

#define	round(size, align)				\
	(((size) + (align) - 1) & ~((align) - 1))
#define	calculate_first_tls_offset(size, align) \
	round(16, align)
#define	calculate_tls_offset(prev_offset, prev_size, size, align) \
	round(prev_offset + prev_size, align)
#define	calculate_tls_end(off, size) 	((off) + (size))
#define calculate_tls_post_size(align) \
	round(TLS_TCB_SIZE, align) - TLS_TCB_SIZE

#define	TLS_TCB_SIZE	16
typedef struct {
    unsigned long ti_module;
    unsigned long ti_offset;
} tls_index;

extern void *__tls_get_addr(tls_index *ti);

#define	RTLD_DEFAULT_STACK_PF_EXEC	PF_X
#define	RTLD_DEFAULT_STACK_EXEC		PROT_EXEC

#define md_abi_variant_hook(x)

#endif
