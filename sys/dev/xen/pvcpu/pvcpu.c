/*
 * Copyright (c) 2013 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/pcpu.h>
#include <sys/smp.h>

#include <xen/xen-os.h>

/*
 * Dummy Xen cpu device
 *
 * Since there's no ACPI on PVH guests, we need to create a dummy
 * CPU device in order to fill the pcpu->pc_device field.
 */

static void
xenpvcpu_identify(driver_t *driver, device_t parent)
{
	int i;

	/* Only attach in case the per-CPU device is not set. */
	if (!xen_domain() || PCPU_GET(device) != NULL)
		return;

	CPU_FOREACH(i) {
		if (BUS_ADD_CHILD(parent, 0, "pvcpu", i) == NULL)
			panic("Unable to add Xen PV CPU device.");
	}
}

static int
xenpvcpu_probe(device_t dev)
{

	device_set_desc(dev, "Xen PV CPU");
	return (BUS_PROBE_NOWILDCARD);
}

static int
xenpvcpu_attach(device_t dev)
{
	struct pcpu *pc;
	int cpu;

	cpu = device_get_unit(dev);
	pc = pcpu_find(cpu);
	pc->pc_device = dev;
	return (0);
}

static device_method_t xenpvcpu_methods[] = {
	DEVMETHOD(device_identify, xenpvcpu_identify),
	DEVMETHOD(device_probe, xenpvcpu_probe),
	DEVMETHOD(device_attach, xenpvcpu_attach),

	DEVMETHOD_END
};

static driver_t xenpvcpu_driver = {
	"pvcpu",
	xenpvcpu_methods,
	0,
};

devclass_t xenpvcpu_devclass;

DRIVER_MODULE(xenpvcpu, xenpv, xenpvcpu_driver, xenpvcpu_devclass, 0, 0);
MODULE_DEPEND(xenpvcpu, xenpv, 1, 1, 1);
