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

#ifndef _MACHINE_PLATFORMVAR_H_
#define _MACHINE_PLATFORMVAR_H_

/*
 * A PowerPC platform implementation is declared with a kernel object and
 * an associated method table, similar to a device driver.
 *
 * e.g.
 *
 * static platform_method_t chrp_methods[] = {
 *	PLATFORMMETHOD(platform_probe,		chrp_probe),
 *	PLATFORMMETHOD(platform_mem_regions,	ofw_mem_regions),
 *  ...
 *	PLATFORMMETHOD(platform_smp_first_cpu,	chrp_smp_first_cpu),
 *	{ 0, 0 }
 * };
 *
 * static platform_def_t chrp_platform = {
 * 	"chrp",
 *	chrp_methods,
 *	sizeof(chrp_platform_softc),	// or 0 if no softc
 * };
 *
 * PLATFORM_DEF(chrp_platform);
 */

#include <sys/kobj.h>

struct platform_kobj {
	/*
	 * A platform instance is a kernel object
	 */
	KOBJ_FIELDS;

	/*
	 * Utility elements that an instance may use
	 */
	struct mtx	platform_mtx;	/* available for instance use */
	void		*platform_iptr;	/* instance data pointer */

	/*
	 * Opaque data that can be overlaid with an instance-private
	 * structure. Platform code can test that this is large enough at
	 * compile time with a sizeof() test againt it's softc. There
	 * is also a run-time test when the platform kernel object is
	 * registered.
	 */
#define PLATFORM_OPAQUESZ	64
	u_int		platform_opaque[PLATFORM_OPAQUESZ];
};

typedef struct platform_kobj	*platform_t;
typedef struct kobj_class	platform_def_t;
#define platform_method_t	kobj_method_t

#define PLATFORMMETHOD		KOBJMETHOD
#define	PLATFORMMETHOD_END	KOBJMETHOD_END

#define PLATFORM_DEF(name)	DATA_SET(platform_set, name)

#endif /* _MACHINE_PLATFORMVAR_H_ */
