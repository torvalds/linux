/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2006-2008 John Birrell <jb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Statically Defined Tracing (SDT) definitions.
 *
 */

#ifndef _SYS_SDT_H
#define	_SYS_SDT_H

#ifndef _KERNEL

#define	_DTRACE_VERSION	1

#define	DTRACE_PROBE(prov, name) {				\
	extern void __dtrace_##prov##___##name(void);		\
	__dtrace_##prov##___##name();				\
}

#define	DTRACE_PROBE1(prov, name, arg1) {			\
	extern void __dtrace_##prov##___##name(unsigned long);	\
	__dtrace_##prov##___##name((unsigned long)arg1);	\
}

#define	DTRACE_PROBE2(prov, name, arg1, arg2) {			\
	extern void __dtrace_##prov##___##name(unsigned long,	\
	    unsigned long);					\
	__dtrace_##prov##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2);				\
}

#define	DTRACE_PROBE3(prov, name, arg1, arg2, arg3) {		\
	extern void __dtrace_##prov##___##name(unsigned long,	\
	    unsigned long, unsigned long);			\
	__dtrace_##prov##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2, (unsigned long)arg3);		\
}

#define	DTRACE_PROBE4(prov, name, arg1, arg2, arg3, arg4) {	\
	extern void __dtrace_##prov##___##name(unsigned long,	\
	    unsigned long, unsigned long, unsigned long);	\
	__dtrace_##prov##___##name((unsigned long)arg1,		\
	    (unsigned long)arg2, (unsigned long)arg3,		\
	    (unsigned long)arg4);				\
}

#define	DTRACE_PROBE5(prov, name, arg1, arg2, arg3, arg4, arg5) {	\
	extern void __dtrace_##prov##___##name(unsigned long,		\
	    unsigned long, unsigned long, unsigned long, unsigned long);\
	__dtrace_##prov##___##name((unsigned long)arg1,			\
	    (unsigned long)arg2, (unsigned long)arg3,			\
	    (unsigned long)arg4, (unsigned long)arg5);			\
}

#else /* _KERNEL */

#include <sys/cdefs.h>
#include <sys/linker_set.h>

extern volatile bool sdt_probes_enabled;

#ifndef KDTRACE_HOOKS

#define SDT_PROVIDER_DEFINE(prov)
#define SDT_PROVIDER_DECLARE(prov)
#define SDT_PROBE_DEFINE(prov, mod, func, name)
#define SDT_PROBE_DECLARE(prov, mod, func, name)
#define SDT_PROBES_ENABLED()	0
#define SDT_PROBE(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4)
#define SDT_PROBE_ARGTYPE(prov, mod, func, name, num, type, xtype)

#define	SDT_PROBE_DEFINE0(prov, mod, func, name)
#define	SDT_PROBE_DEFINE1(prov, mod, func, name, arg0)
#define	SDT_PROBE_DEFINE2(prov, mod, func, name, arg0, arg1)
#define	SDT_PROBE_DEFINE3(prov, mod, func, name, arg0, arg1, arg2)
#define	SDT_PROBE_DEFINE4(prov, mod, func, name, arg0, arg1, arg2, arg3)
#define	SDT_PROBE_DEFINE5(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4)
#define	SDT_PROBE_DEFINE6(prov, mod, func, name, arg0, arg1, arg2,      \
    arg3, arg4, arg5)
#define	SDT_PROBE_DEFINE7(prov, mod, func, name, arg0, arg1, arg2,      \
    arg3, arg4, arg5, arg6)

#define	SDT_PROBE0(prov, mod, func, name)
#define	SDT_PROBE1(prov, mod, func, name, arg0)
#define	SDT_PROBE2(prov, mod, func, name, arg0, arg1)
#define	SDT_PROBE3(prov, mod, func, name, arg0, arg1, arg2)
#define	SDT_PROBE4(prov, mod, func, name, arg0, arg1, arg2, arg3)
#define	SDT_PROBE5(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4)
#define	SDT_PROBE6(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4, arg5)
#define	SDT_PROBE7(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4, arg5,  \
    arg6)

#define	SDT_PROBE_DEFINE0_XLATE(prov, mod, func, name)
#define	SDT_PROBE_DEFINE1_XLATE(prov, mod, func, name, arg0, xarg0)
#define	SDT_PROBE_DEFINE2_XLATE(prov, mod, func, name, arg0, xarg0,     \
    arg1, xarg1)
#define	SDT_PROBE_DEFINE3_XLATE(prov, mod, func, name, arg0, xarg0,     \
    arg1, xarg1, arg2, xarg2)
#define SDT_PROBE_DEFINE4_XLATE(prov, mod, func, name, arg0, xarg0,     \
    arg1, xarg1, arg2, xarg2, arg3, xarg3)
#define	SDT_PROBE_DEFINE5_XLATE(prov, mod, func, name, arg0, xarg0,     \
    arg1, xarg1, arg2, xarg2, arg3, xarg3, arg4, xarg4)
#define	SDT_PROBE_DEFINE6_XLATE(prov, mod, func, name, arg0, xarg0,     \
    arg1,  xarg1, arg2, xarg2, arg3, xarg3, arg4, xarg4, arg5, xarg5)
#define	SDT_PROBE_DEFINE7_XLATE(prov, mod, func, name, arg0, xarg0,     \
    arg1, xarg1, arg2, xarg2, arg3, xarg3, arg4, xarg4, arg5, xarg5, arg6,     \
    xarg6)

#define	DTRACE_PROBE(name)
#define	DTRACE_PROBE1(name, type0, arg0)
#define	DTRACE_PROBE2(name, type0, arg0, type1, arg1)
#define	DTRACE_PROBE3(name, type0, arg0, type1, arg1, type2, arg2)
#define	DTRACE_PROBE4(name, type0, arg0, type1, arg1, type2, arg2, type3, arg3)
#define	DTRACE_PROBE5(name, type0, arg0, type1, arg1, type2, arg2, type3, arg3,\
    type4, arg4)

#else

SET_DECLARE(sdt_providers_set, struct sdt_provider);
SET_DECLARE(sdt_probes_set, struct sdt_probe);
SET_DECLARE(sdt_argtypes_set, struct sdt_argtype);

#define SDT_PROVIDER_DEFINE(prov)						\
	struct sdt_provider sdt_provider_##prov[1] = {				\
		{ #prov, { NULL, NULL }, 0, 0 }					\
	};									\
	DATA_SET(sdt_providers_set, sdt_provider_##prov);

#define SDT_PROVIDER_DECLARE(prov)						\
	extern struct sdt_provider sdt_provider_##prov[1]

#define SDT_PROBE_DEFINE(prov, mod, func, name)					\
	struct sdt_probe sdt_##prov##_##mod##_##func##_##name[1] = {		\
		{ sizeof(struct sdt_probe), sdt_provider_##prov,		\
		    { NULL, NULL }, { NULL, NULL }, #mod, #func, #name, 0, 0,	\
		    NULL }							\
	};									\
	DATA_SET(sdt_probes_set, sdt_##prov##_##mod##_##func##_##name);

#define SDT_PROBE_DECLARE(prov, mod, func, name)				\
	extern struct sdt_probe sdt_##prov##_##mod##_##func##_##name[1]

#define	SDT_PROBES_ENABLED()	__predict_false(sdt_probes_enabled)

#define SDT_PROBE(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4)	do {	\
	if (SDT_PROBES_ENABLED()) {						\
		if (__predict_false(sdt_##prov##_##mod##_##func##_##name->id))	\
		(*sdt_probe_func)(sdt_##prov##_##mod##_##func##_##name->id,	\
		    (uintptr_t) arg0, (uintptr_t) arg1, (uintptr_t) arg2,	\
		    (uintptr_t) arg3, (uintptr_t) arg4);			\
	} \
} while (0)

#define SDT_PROBE_ARGTYPE(prov, mod, func, name, num, type, xtype)		\
	static struct sdt_argtype sdta_##prov##_##mod##_##func##_##name##num[1]	\
	    = { { num, type, xtype, { NULL, NULL },				\
	    sdt_##prov##_##mod##_##func##_##name }				\
	};									\
	DATA_SET(sdt_argtypes_set, sdta_##prov##_##mod##_##func##_##name##num);

#define	SDT_PROBE_DEFINE0(prov, mod, func, name)			\
	SDT_PROBE_DEFINE(prov, mod, func, name)

#define	SDT_PROBE_DEFINE1(prov, mod, func, name, arg0)			\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL)

#define	SDT_PROBE_DEFINE2(prov, mod, func, name, arg0, arg1)		\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, NULL)

#define	SDT_PROBE_DEFINE3(prov, mod, func, name, arg0, arg1, arg2)\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, NULL)

#define	SDT_PROBE_DEFINE4(prov, mod, func, name, arg0, arg1, arg2, arg3) \
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, NULL)

#define	SDT_PROBE_DEFINE5(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4) \
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 4, arg4, NULL)

#define	SDT_PROBE_DEFINE6(prov, mod, func, name, arg0, arg1, arg2, arg3,\
    arg4, arg5) \
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 4, arg4, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 5, arg5, NULL)

#define	SDT_PROBE_DEFINE7(prov, mod, func, name, arg0, arg1, arg2, arg3,\
    arg4, arg5, arg6) \
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 4, arg4, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 5, arg5, NULL);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 6, arg6, NULL)

#define	SDT_PROBE_DEFINE0_XLATE(prov, mod, func, name)		\
	SDT_PROBE_DEFINE(prov, mod, func, name)

#define	SDT_PROBE_DEFINE1_XLATE(prov, mod, func, name, arg0, xarg0) \
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0)

#define	SDT_PROBE_DEFINE2_XLATE(prov, mod, func, name, arg0, xarg0, \
    arg1,  xarg1)							\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, xarg1)

#define	SDT_PROBE_DEFINE3_XLATE(prov, mod, func, name, arg0, xarg0, \
    arg1, xarg1, arg2, xarg2)						\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, xarg1);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, xarg2)

#define	SDT_PROBE_DEFINE4_XLATE(prov, mod, func, name, arg0, xarg0, \
    arg1, xarg1, arg2, xarg2, arg3, xarg3)				\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, xarg1);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, xarg2);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, xarg3)

#define	SDT_PROBE_DEFINE5_XLATE(prov, mod, func, name, arg0, xarg0, \
    arg1, xarg1, arg2, xarg2, arg3, xarg3, arg4, xarg4)			\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, xarg1);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, xarg2);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, xarg3);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 4, arg4, xarg4)

#define	SDT_PROBE_DEFINE6_XLATE(prov, mod, func, name, arg0, xarg0, \
    arg1, xarg1, arg2, xarg2, arg3, xarg3, arg4, xarg4, arg5, xarg5)	\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, xarg1);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, xarg2);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, xarg3);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 4, arg4, xarg4);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 5, arg5, xarg5)

#define	SDT_PROBE_DEFINE7_XLATE(prov, mod, func, name, arg0, xarg0, \
    arg1, xarg1, arg2, xarg2, arg3, xarg3, arg4, xarg4, arg5, xarg5, arg6, \
    xarg6)								\
	SDT_PROBE_DEFINE(prov, mod, func, name);			\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 0, arg0, xarg0);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 1, arg1, xarg1);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 2, arg2, xarg2);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 3, arg3, xarg3);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 4, arg4, xarg4);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 5, arg5, xarg5);	\
	SDT_PROBE_ARGTYPE(prov, mod, func, name, 6, arg6, xarg6)

#define	SDT_PROBE0(prov, mod, func, name)				\
	SDT_PROBE(prov, mod, func, name, 0, 0, 0, 0, 0)
#define	SDT_PROBE1(prov, mod, func, name, arg0)				\
	SDT_PROBE(prov, mod, func, name, arg0, 0, 0, 0, 0)
#define	SDT_PROBE2(prov, mod, func, name, arg0, arg1)			\
	SDT_PROBE(prov, mod, func, name, arg0, arg1, 0, 0, 0)
#define	SDT_PROBE3(prov, mod, func, name, arg0, arg1, arg2)		\
	SDT_PROBE(prov, mod, func, name, arg0, arg1, arg2,  0, 0)
#define	SDT_PROBE4(prov, mod, func, name, arg0, arg1, arg2, arg3)	\
	SDT_PROBE(prov, mod, func, name, arg0, arg1, arg2, arg3, 0)
#define	SDT_PROBE5(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4) \
	SDT_PROBE(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4)
#define	SDT_PROBE6(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4, arg5)  \
	do {								       \
		if (sdt_##prov##_##mod##_##func##_##name->id)		       \
			(*(void (*)(uint32_t, uintptr_t, uintptr_t, uintptr_t, \
			    uintptr_t, uintptr_t, uintptr_t))sdt_probe_func)(  \
			    sdt_##prov##_##mod##_##func##_##name->id,	       \
			    (uintptr_t)arg0, (uintptr_t)arg1, (uintptr_t)arg2, \
			    (uintptr_t)arg3, (uintptr_t)arg4, (uintptr_t)arg5);\
	} while (0)
#define	SDT_PROBE7(prov, mod, func, name, arg0, arg1, arg2, arg3, arg4, arg5,  \
    arg6)								       \
	do {								       \
		if (sdt_##prov##_##mod##_##func##_##name->id)		       \
			(*(void (*)(uint32_t, uintptr_t, uintptr_t, uintptr_t, \
			    uintptr_t, uintptr_t, uintptr_t, uintptr_t))       \
			    sdt_probe_func)(				       \
			    sdt_##prov##_##mod##_##func##_##name->id,	       \
			    (uintptr_t)arg0, (uintptr_t)arg1, (uintptr_t)arg2, \
			    (uintptr_t)arg3, (uintptr_t)arg4, (uintptr_t)arg5, \
			    (uintptr_t)arg6);				       \
	} while (0)

#define	DTRACE_PROBE_IMPL_START(name, arg0, arg1, arg2, arg3, arg4)	do { \
	static SDT_PROBE_DEFINE(sdt, , , name);				     \
	SDT_PROBE(sdt, , , name, arg0, arg1, arg2, arg3, arg4);
#define DTRACE_PROBE_IMPL_END	} while (0)

#define DTRACE_PROBE(name)						\
	DTRACE_PROBE_IMPL_START(name, 0, 0, 0, 0, 0)			\
	DTRACE_PROBE_IMPL_END

#define DTRACE_PROBE1(name, type0, arg0)				\
	DTRACE_PROBE_IMPL_START(name, arg0, 0, 0, 0, 0) 		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 0, #type0, NULL);		\
	DTRACE_PROBE_IMPL_END

#define DTRACE_PROBE2(name, type0, arg0, type1, arg1)			\
	DTRACE_PROBE_IMPL_START(name, arg0, arg1, 0, 0, 0) 		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 0, #type0, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 1, #type1, NULL);		\
	DTRACE_PROBE_IMPL_END

#define DTRACE_PROBE3(name, type0, arg0, type1, arg1, type2, arg2)	\
	DTRACE_PROBE_IMPL_START(name, arg0, arg1, arg2, 0, 0)	 	\
	SDT_PROBE_ARGTYPE(sdt, , , name, 0, #type0, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 1, #type1, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 2, #type2, NULL);		\
	DTRACE_PROBE_IMPL_END

#define DTRACE_PROBE4(name, type0, arg0, type1, arg1, type2, arg2, type3, arg3)	\
	DTRACE_PROBE_IMPL_START(name, arg0, arg1, arg2, arg3, 0) 	\
	SDT_PROBE_ARGTYPE(sdt, , , name, 0, #type0, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 1, #type1, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 2, #type2, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 3, #type3, NULL);		\
	DTRACE_PROBE_IMPL_END

#define DTRACE_PROBE5(name, type0, arg0, type1, arg1, type2, arg2, type3, arg3,	\
    type4, arg4)								\
	DTRACE_PROBE_IMPL_START(name, arg0, arg1, arg2, arg3, arg4) 	\
	SDT_PROBE_ARGTYPE(sdt, , , name, 0, #type0, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 1, #type1, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 2, #type2, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 3, #type3, NULL);		\
	SDT_PROBE_ARGTYPE(sdt, , , name, 4, #type4, NULL);		\
	DTRACE_PROBE_IMPL_END

#endif /* KDTRACE_HOOKS */

/*
 * This type definition must match that of dtrace_probe. It is defined this
 * way to avoid having to rely on CDDL code.
 */
typedef	void (*sdt_probe_func_t)(uint32_t, uintptr_t arg0, uintptr_t arg1,
    uintptr_t arg2, uintptr_t arg3, uintptr_t arg4);

/*
 * The 'sdt' provider will set it to dtrace_probe when it loads.
 */
extern sdt_probe_func_t	sdt_probe_func;

struct sdt_probe;
struct sdt_provider;
struct linker_file;

struct sdt_argtype {
	int		ndx;		/* Argument index. */
	const char	*type;		/* Argument type string. */
	const char	*xtype;		/* Translated argument type. */
	TAILQ_ENTRY(sdt_argtype)
			argtype_entry;	/* Argument type list entry. */
	struct sdt_probe *probe;	/* Ptr to the probe structure. */
};

struct sdt_probe {
	int		version;	/* Set to sizeof(struct sdt_probe). */
	struct sdt_provider *prov;	/* Ptr to the provider structure. */
	TAILQ_ENTRY(sdt_probe)
			probe_entry;	/* SDT probe list entry. */
	TAILQ_HEAD(, sdt_argtype) argtype_list;
	const char	*mod;
	const char	*func;
	const char	*name;
	id_t		id;		/* DTrace probe ID. */
	int		n_args;		/* Number of arguments. */
	struct linker_file *sdtp_lf;	/* Module in which we're defined. */
};

struct sdt_provider {
	char *name;			/* Provider name. */
	TAILQ_ENTRY(sdt_provider)
			prov_entry;	/* SDT provider list entry. */
	uintptr_t	id;		/* DTrace provider ID. */
	int		sdt_refs;	/* Number of module references. */
};

void sdt_probe_stub(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t);

SDT_PROVIDER_DECLARE(sdt);

#endif /* _KERNEL */

#endif /* _SYS_SDT_H */
