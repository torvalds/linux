/*-
 * Copyright (c) 2010 Aleksandr Rybalko.
 * Copyright (c) 2016 Michael Zhilin.
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
 */

/*
 * Implementation of pseudo driver for MIPS to copy the NVRAM settings
 * from various sources into the kernel environment.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>

#include "nvram2env.h"

static int
nvram2env_mips_probe(device_t dev)
{
	struct nvram2env_softc	*sc;

	sc = device_get_softc(dev);
	sc->bst = mips_bus_space_generic;

	return (nvram2env_probe(dev));
}

static device_method_t nvram2env_mips_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nvram2env_mips_probe),

	DEVMETHOD_END
};

DEFINE_CLASS_1(nvram2env, nvram2env_mips_driver, nvram2env_mips_methods,
		sizeof(struct nvram2env_softc), nvram2env_driver);
DRIVER_MODULE(nvram2env_mips, nexus, nvram2env_mips_driver, nvram2env_devclass,
    NULL, NULL);

MODULE_VERSION(nvram2env_mips, 1);
MODULE_DEPEND(nvram2env_mips, nvram2env, 1, 1, 1);
