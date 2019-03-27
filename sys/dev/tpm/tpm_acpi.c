/*
 * Copyright (c) 2008, 2009 Michael Shalayeff
 * Copyright (c) 2009, 2010 Hans-Joerg Hoexer
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <machine/md_var.h>

#include <isa/isareg.h>
#include <isa/isavar.h>
#include "tpmvar.h"

#include "opt_acpi.h"
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>



char *tpm_ids[] = {"ATM1200",  "BCM0102", "INTC0102", "SNO3504", "WEC1000",
    "PNP0C31", NULL};

static int
tpm_acpi_probe(device_t dev)
{
	int rv;
	
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, tpm_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "Trusted Platform Module");
		
	return (rv);
}

static device_method_t tpm_acpi_methods[] = {
#if 0
	/*In some case, TPM existence is found only in TPCA header*/
	DEVMETHOD(device_identify,	tpm_acpi_identify),
#endif

	DEVMETHOD(device_probe,		tpm_acpi_probe),
	DEVMETHOD(device_attach,	tpm_attach),
	DEVMETHOD(device_detach,	tpm_detach),
	DEVMETHOD(device_suspend,	tpm_suspend),
	DEVMETHOD(device_resume,	tpm_resume),
	{ 0, 0 }
};
static driver_t tpm_acpi_driver = {
	"tpm", tpm_acpi_methods, sizeof(struct tpm_softc),
};

devclass_t tpm_devclass;
DRIVER_MODULE(tpm, acpi, tpm_acpi_driver, tpm_devclass, 0, 0);
