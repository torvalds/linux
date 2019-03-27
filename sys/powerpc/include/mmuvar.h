/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Peter Grehan
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

#ifndef _MACHINE_MMUVAR_H_
#define _MACHINE_MMUVAR_H_

/*
 * A PowerPC MMU implementation is declared with a kernel object and
 * an associated method table. The MMU_DEF macro is used to declare
 * the class, and also links it to the global MMU class list.
 *
 * e.g.
 *
 * static mmu_method_t ppc8xx_methods[] = {
 *	MMUMETHOD(mmu_change_wiring,		ppc8xx_mmu_change_wiring),
 *	MMUMETHOD(mmu_clear_modify,		ppc8xx_mmu_clear_modify),
 *	MMUMETHOD(mmu_clear_reference,		ppc8xx_mmu_clear_reference),
 *  ...
 *	MMUMETHOD(mmu_dev_direct_mapped,	ppc8xx_mmu_dev_direct_mapped),
 *	{ 0, 0 }
 * };
 *
 * MMU_DEF(ppc8xx, MMU_TYPE_8xx, ppc8xx_methods, sizeof(ppc8xx_mmu_softc));
 *
 * A single level of inheritance is supported in a similar fashion to
 * kobj inheritance e.g.
 *
 * MMU_DEF_1(ppc860c, MMU_TYPE_860c, ppc860c_methods, 0, ppc8xx);
 */

#include <sys/kobj.h>

struct mmu_kobj {
	/*
	 * An MMU instance is a kernel object
	 */
	KOBJ_FIELDS;

	/*
	 * Utility elements that an instance may use
	 */
	struct mtx	mmu_mtx;	/* available for instance use */
	void		*mmu_iptr;	/* instance data pointer */

	/*
	 * Opaque data that can be overlaid with an instance-private
	 * structure. MMU code can test that this is large enough at
	 * compile time with a sizeof() test againt it's softc. There
	 * is also a run-time test when the MMU kernel object is
	 * registered.
	 */
#define MMU_OPAQUESZ	64
	u_int		mmu_opaque[MMU_OPAQUESZ];
};

typedef struct mmu_kobj		*mmu_t;
typedef struct kobj_class	mmu_def_t;
#define mmu_method_t		kobj_method_t

#define MMUMETHOD	KOBJMETHOD

#define MMU_DEF(name, ident, methods, size)	\
						\
mmu_def_t name = {				\
	ident, methods, size, NULL		\
};						\
DATA_SET(mmu_set, name)

#define MMU_DEF_INHERIT(name, ident, methods, size, base1)	\
						\
static kobj_class_t name ## _baseclasses[] =	\
       	{ &base1, NULL };			\
mmu_def_t name = {                              \
	ident, methods, size, name ## _baseclasses	\
};                                              \
DATA_SET(mmu_set, name)


#if 0
mmu_def_t name = {				\
	ident, methods, size, name ## _baseclasses	\
};						
DATA_SET(mmu_set, name)
#endif

/*
 * Known MMU names
 */
#define MMU_TYPE_BOOKE	"mmu_booke"	/* Book-E MMU specification */
#define MMU_TYPE_OEA	"mmu_oea"	/* 32-bit OEA */
#define MMU_TYPE_G5	"mmu_g5"	/* 64-bit bridge (ibm 970) */
#define MMU_TYPE_P9H	"mmu_p9h"	/* 64-bit native ISA 3.0 (POWER9) hash */
#define MMU_TYPE_8xx	"mmu_8xx"	/* 8xx quicc TLB */

#endif /* _MACHINE_MMUVAR_H_ */
