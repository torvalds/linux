/*-
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
 * An ARM platform implementation is declared with a kernel object and
 * an associated method table, similar to a device driver.
 *
 * e.g.
 *
 * static platform_method_t bcm2835_methods[] = {
 *	PLATFORMMETHOD(platform_probe,		bcm2835_probe),
 *  ...
 *	PLATFORMMETHOD_END
 * };
 *
 * static platform_def_t bcm3835_platform = {
 * 	"bcm2835",
 *	bcm2835_methods,
 *	sizeof(bcm2835_platform_softc),	// or 0 if no softc
 * };
 *
 * PLATFORM_DEF(bcm2835_platform);
 */

#include <sys/kobj.h>
#include <sys/linker_set.h>

struct platform_class {
	KOBJ_CLASS_FIELDS;

	/* How many times to loop to delay approximately 1us */
	int delay_count;
};

struct platform_kobj {
	/*
	 * A platform instance is a kernel object
	 */
	KOBJ_FIELDS;

	/* Platform class, for access to class specific data */
	struct platform_class *cls;
};

typedef struct platform_kobj	*platform_t;
typedef struct platform_class	platform_def_t;
#define platform_method_t	kobj_method_t

#define PLATFORMMETHOD		KOBJMETHOD
#define	PLATFORMMETHOD_END	KOBJMETHOD_END

#define PLATFORM_DEF(name)	DATA_SET(platform_set, name)

#ifdef FDT
struct fdt_platform_class {
	KOBJ_CLASS_FIELDS;

	const char *fdt_compatible;
};

typedef struct fdt_platform_class fdt_platform_def_t;

extern platform_method_t fdt_platform_methods[];

#define FDT_PLATFORM_DEF2(NAME, VAR_NAME, NAME_STR, _size, _compatible,	\
    _delay)								\
CTASSERT(_delay > 0);							\
static fdt_platform_def_t VAR_NAME ## _fdt_platform = {			\
	.name = NAME_STR,						\
	.methods = fdt_platform_methods,				\
	.fdt_compatible = _compatible,					\
};									\
static kobj_class_t VAR_NAME ## _baseclasses[] =			\
	{ (kobj_class_t)&VAR_NAME ## _fdt_platform, NULL };		\
static platform_def_t VAR_NAME ## _platform = {				\
	.name = NAME_STR,						\
	.methods = NAME ## _methods,					\
	.size = _size,							\
	.baseclasses = VAR_NAME ## _baseclasses,			\
	.delay_count = _delay,						\
};									\
DATA_SET(platform_set, VAR_NAME ## _platform)

#define	FDT_PLATFORM_DEF(NAME, NAME_STR, size, compatible, delay)	\
    FDT_PLATFORM_DEF2(NAME, NAME, NAME_STR, size, compatible, delay)

#endif

/*
 * Helper to get the platform object
 */
platform_t platform_obj(void);

bool arm_tmr_timed_wait(platform_t, int);

#endif /* _MACHINE_PLATFORMVAR_H_ */
