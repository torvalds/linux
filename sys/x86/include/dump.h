/*-
 * Copyright (c) 2014 EMC Corp.
 * Author: Conrad Meyer <conrad.meyer@isilon.com>
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

#ifndef _MACHINE_DUMP_H_
#define	_MACHINE_DUMP_H_

#ifdef __amd64__
#define	KERNELDUMP_ARCH_VERSION	KERNELDUMP_AMD64_VERSION
#define	EM_VALUE		EM_X86_64
#else
#define	KERNELDUMP_ARCH_VERSION	KERNELDUMP_I386_VERSION
#define	EM_VALUE		EM_386
#endif

/* 20 phys_avail entry pairs correspond to 10 pa's */
#define	DUMPSYS_MD_PA_NPAIRS	10
#define	DUMPSYS_NUM_AUX_HDRS	0

static inline void
dumpsys_pa_init(void)
{

	dumpsys_gen_pa_init();
}

static inline struct dump_pa *
dumpsys_pa_next(struct dump_pa *p)
{

	return (dumpsys_gen_pa_next(p));
}

static inline void
dumpsys_wbinv_all(void)
{

	dumpsys_gen_wbinv_all();
}

static inline void
dumpsys_unmap_chunk(vm_paddr_t pa, size_t s, void *va)
{

	dumpsys_gen_unmap_chunk(pa, s, va);
}

static inline int
dumpsys_write_aux_headers(struct dumperinfo *di)
{

	return (dumpsys_gen_write_aux_headers(di));
}

static inline int
dumpsys(struct dumperinfo *di)
{

	return (dumpsys_generic(di));
}

#endif  /* !_MACHINE_DUMP_H_ */
