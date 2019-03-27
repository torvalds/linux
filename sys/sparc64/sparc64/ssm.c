/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Marius Strobl <marius@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Glue allowing devices beneath the scalable shared memory node to be
 * treated like nexus(4) children
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/nexusvar.h>

static device_probe_t ssm_probe;

static device_method_t ssm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ssm_probe),

	/* Bus interface */

	/* ofw_bus interface */

	DEVMETHOD_END
};

static devclass_t ssm_devclass;

DEFINE_CLASS_1(ssm, ssm_driver, ssm_methods, 1 /* no softc */, nexus_driver);
EARLY_DRIVER_MODULE(ssm, nexus, ssm_driver, ssm_devclass, 0, 0, BUS_PASS_BUS);
MODULE_DEPEND(ssm, nexus, 1, 1, 1);
MODULE_VERSION(ssm, 1);

static int
ssm_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "ssm") == 0) {
		device_set_desc(dev, "Scalable Shared Memory");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}
