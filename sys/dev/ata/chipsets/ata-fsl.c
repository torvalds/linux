/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ata.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/ata/ata-all.h>
#include <dev/ata/ata-pci.h>
#include <ata_if.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/fdt.h>

/* local prototypes */
static int imx_ata_ch_attach(device_t dev);
static int imx_ata_setmode(device_t dev, int target, int mode);

static int
imx_ata_probe(device_t dev)
{
	struct ata_pci_controller *ctrl;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,imx51-ata") &&
	    !ofw_bus_is_compatible(dev, "fsl,imx53-ata"))
		return (ENXIO);

	ctrl = device_get_softc(dev);

	device_set_desc(dev, "Freescale Integrated PATA Controller");
	return (BUS_PROBE_LOW_PRIORITY);
}

static void
imx_ata_intr(void *data)
{
	struct ata_pci_controller *ctrl = data;

	bus_write_2(ctrl->r_res1, 0x28, bus_read_2(ctrl->r_res1, 0x28));
	ctrl->interrupt[0].function(ctrl->interrupt[0].argument);
}

static int
imx_ata_attach(device_t dev)
{
	struct ata_pci_controller *ctrl;
	device_t child;
	int unit;

	ctrl = device_get_softc(dev);
	/* do chipset specific setups only needed once */
	ctrl->legacy = ata_legacy(dev);
	ctrl->channels = 1;
	ctrl->ichannels = -1;
	ctrl->ch_attach = ata_pci_ch_attach;
	ctrl->ch_detach = ata_pci_ch_detach;
	ctrl->dev = dev;

	ctrl->r_type1 = SYS_RES_MEMORY;
	ctrl->r_rid1 = 0;
	ctrl->r_res1 = bus_alloc_resource_any(dev, ctrl->r_type1,
	    &ctrl->r_rid1, RF_ACTIVE);

	if (ata_setup_interrupt(dev, imx_ata_intr)) {
		device_printf(dev, "failed to setup interrupt\n");
    		return ENXIO;
	}

	ctrl->channels = 1;

	ctrl->ch_attach = imx_ata_ch_attach;
	ctrl->setmode = imx_ata_setmode;

	/* attach all channels on this controller */
	unit = 0;
	child = device_add_child(dev, "ata", ((unit == 0) && ctrl->legacy) ?
		    unit : devclass_find_free_unit(ata_devclass, 2));
	if (child == NULL)
		device_printf(dev, "failed to add ata child device\n");
	else
		device_set_ivars(child, (void *)(intptr_t)unit);

	bus_generic_attach(dev);
	return 0;
}

static int
imx_ata_ch_attach(device_t dev)
{
	struct ata_pci_controller *ctrl;
	struct ata_channel *ch;
	int i;

	ctrl = device_get_softc(device_get_parent(dev));
	ch = device_get_softc(dev);
	for (i = ATA_DATA; i < ATA_MAX_RES; i++)
		ch->r_io[i].res = ctrl->r_res1;

	bus_write_2(ctrl->r_res1, 0x24, 0x80);
	DELAY(100);
	bus_write_2(ctrl->r_res1, 0x24, 0xc0);
	DELAY(100);


	/* Write TIME_OFF/ON/1/2W */
	bus_write_1(ctrl->r_res1, 0x00, 3);
	bus_write_1(ctrl->r_res1, 0x01, 3);
	bus_write_1(ctrl->r_res1, 0x02, (25 + 15) / 15);
	bus_write_1(ctrl->r_res1, 0x03, (70 + 15) / 15);

	/* Write TIME_2R/AX/RDX/4 */
	bus_write_1(ctrl->r_res1, 0x04, (70 + 15) / 15);
	bus_write_1(ctrl->r_res1, 0x05, (50 + 15) / 15 + 2);
	bus_write_1(ctrl->r_res1, 0x06, 1);
	bus_write_1(ctrl->r_res1, 0x07, (10 + 15) / 15);

	/* Write TIME_9 ; the rest of timing registers is irrelevant for PIO */
	bus_write_1(ctrl->r_res1, 0x08, (10 + 15) / 15);

	bus_write_2(ctrl->r_res1, 0x24, 0xc1);
	DELAY(30000);

	/* setup ATA registers */
	ch->r_io[ATA_DATA   ].offset = 0xa0;
	ch->r_io[ATA_FEATURE].offset = 0xa4;
	ch->r_io[ATA_ERROR  ].offset = 0xa4;
	ch->r_io[ATA_COUNT  ].offset = 0xa8;
	ch->r_io[ATA_SECTOR ].offset = 0xac;
	ch->r_io[ATA_CYL_LSB].offset = 0xb0;
	ch->r_io[ATA_CYL_MSB].offset = 0xb4;
	ch->r_io[ATA_DRIVE  ].offset = 0xb8;
	ch->r_io[ATA_COMMAND].offset = 0xbc;

	ch->r_io[ATA_STATUS ].offset = 0xbc;
	ch->r_io[ATA_ALTSTAT].offset = 0xd8;
	ch->r_io[ATA_CONTROL].offset = 0xd8;

	ata_pci_hw(dev);

	ch->flags |= ATA_NO_SLAVE;
	ch->flags |= ATA_USE_16BIT;
	ch->flags |= ATA_CHECKS_CABLE;
	ch->flags |= ATA_KNOWN_PRESENCE;

	/* Clear pending interrupts. */
	bus_write_2(ctrl->r_res1, 0x28, 0xf8);
	/* Enable all, but Idle interrupts. */
	bus_write_2(ctrl->r_res1, 0x2c, 0x88);

	return 0;
}

static int
imx_ata_setmode(device_t dev, int target, int mode)
{

	return (min(mode, ATA_PIO4));
}

static device_method_t imx_ata_methods[] = {
	DEVMETHOD(device_probe,		imx_ata_probe),
	DEVMETHOD(device_attach,	imx_ata_attach),
	DEVMETHOD(device_detach,	ata_pci_detach),
	DEVMETHOD(device_suspend,	ata_pci_suspend),
	DEVMETHOD(device_resume,	ata_pci_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(bus_read_ivar,	ata_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	ata_pci_write_ivar),
	DEVMETHOD(bus_alloc_resource,	ata_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	ata_pci_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	ata_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	ata_pci_teardown_intr),
	DEVMETHOD(pci_read_config,	ata_pci_read_config),
	DEVMETHOD(pci_write_config,	ata_pci_write_config),
	DEVMETHOD(bus_print_child,	ata_pci_print_child),
	DEVMETHOD(bus_child_location_str, ata_pci_child_location_str),
	DEVMETHOD_END
};
static driver_t imx_ata_driver = {
        "atapci",
        imx_ata_methods,
        sizeof(struct ata_pci_controller)
};
DRIVER_MODULE(imx_ata, simplebus, imx_ata_driver, ata_pci_devclass, NULL,
    NULL);
MODULE_VERSION(imx_ata, 1);
MODULE_DEPEND(imx_ata, ata, 1, 1, 1);
MODULE_DEPEND(imx_ata, atapci, 1, 1, 1);
