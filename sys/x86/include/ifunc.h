/*-
 * Copyright (c) 2015-2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __X86_IFUNC_H
#define	__X86_IFUNC_H

#define	DEFINE_IFUNC(qual, ret_type, name, args, resolver_qual)		\
    resolver_qual ret_type (*name##_resolver(void))args __used;		\
    qual ret_type name args __attribute__((ifunc(#name "_resolver")));	\
    resolver_qual ret_type (*name##_resolver(void))args

#define	DEFINE_UIFUNC(qual, ret_type, name, args, resolver_qual)	\
    resolver_qual ret_type (*name##_resolver(uint32_t, uint32_t,	\
	uint32_t, uint32_t))args __used;				\
    qual ret_type name args __attribute__((ifunc(#name "_resolver")));	\
    resolver_qual ret_type (*name##_resolver(				\
	uint32_t cpu_feature __unused,					\
	uint32_t cpu_feature2 __unused,					\
	uint32_t cpu_stdext_feature __unused,				\
	uint32_t cpu_stdext_feature2 __unused))args

#endif
