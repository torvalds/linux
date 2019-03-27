/*-
 * Copyright (c) 2015 Justin Hibbits
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/smp.h>

#include <machine/platform.h>
#include <machine/platformvar.h>

#include <dev/ofw/openfirm.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "platform_if.h"

static int rb_probe(platform_t);
static int rb_attach(platform_t);

static platform_method_t rb_methods[] = {
	PLATFORMMETHOD(platform_probe,		rb_probe),
	PLATFORMMETHOD(platform_attach,		rb_attach),
	PLATFORMMETHOD_END
};

DEFINE_CLASS_1(rb, rb_platform, rb_methods, 0, mpc85xx_platform);

PLATFORM_DEF(rb_platform);

static int
rb_probe(platform_t plat)
{
	phandle_t rootnode;
	char model[32];

	rootnode = OF_finddevice("/");

	if (OF_getprop(rootnode, "model", model, sizeof(model)) > 0) {
		if (strcmp(model, "RB800") == 0)
			return (BUS_PROBE_SPECIFIC);
	}

	return (ENXIO);
}

static int
rb_attach(platform_t plat)
{
	int error;

	error = mpc85xx_attach(plat);
	if (error)
		return (error);

	return (0);
}
