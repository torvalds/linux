/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Authors: Joe Kloss; Ravi Pokala (rpokala@freebsd.org)
 *
 * Copyright (c) 2017-2018 Panasas
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/atomic.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/smbus/smbconf.h>

#include "imcsmb_reg.h"
#include "imcsmb_var.h"

/* (Sandy,Ivy)bridge-Xeon and (Has,Broad)well-Xeon CPUs contain one or two
 * "Integrated Memory Controllers" (iMCs), and each iMC contains two separate
 * SMBus controllers. These are used for reading SPD data from the DIMMs, and
 * for reading the "Thermal Sensor on DIMM" (TSODs). The iMC SMBus controllers
 * are very simple devices, and have limited functionality compared to
 * full-fledged SMBus controllers, like the one in Intel ICHs and PCHs.
 *
 * The publicly available documentation for the iMC SMBus controllers can be
 * found in the CPU datasheets for (Sandy,Ivy)bridge-Xeon and
 * (Has,broad)well-Xeon, respectively:
 *
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/
 *      Sandybridge     xeon-e5-1600-2600-vol-2-datasheet.pdf
 *      Ivybridge       xeon-e5-v2-datasheet-vol-2.pdf
 *      Haswell         xeon-e5-v3-datasheet-vol-2.pdf
 *      Broadwell       xeon-e5-v4-datasheet-vol-2.pdf
 *
 * Another useful resource is the Linux driver. It is not in the main tree.
 *
 * https://www.mail-archive.com/linux-kernel@vger.kernel.org/msg840043.html
 *
 * The iMC SMBus controllers do not support interrupts (thus, they must be
 * polled for IO completion). All of the iMC registers are in PCI configuration
 * space; there is no support for PIO or MMIO. As a result, this driver does
 * not need to perform and newbus resource manipulation.
 *
 * Because there are multiple SMBus controllers sharing the same PCI device,
 * this driver is actually *two* drivers:
 *
 * - "imcsmb" is an smbus(4)-compliant SMBus controller driver
 *
 * - "imcsmb_pci" recognizes the PCI device and assigns the appropriate set of
 *    PCI config registers to a specific "imcsmb" instance.
 */

/* Depending on the motherboard and firmware, the TSODs might be polled by
 * firmware. Therefore, when this driver accesses these SMBus controllers, the
 * firmware polling must be disabled as part of requesting the bus, and
 * re-enabled when releasing the bus. Unfortunately, the details of how to do
 * this are vendor-specific. Contact your motherboard vendor to get the
 * information you need to do proper implementations.
 *
 * For NVDIMMs which conform to the ACPI "NFIT" standard, the ACPI firmware
 * manages the NVDIMM; for those which pre-date the standard, the operating
 * system interacts with the NVDIMM controller using a vendor-proprietary API
 * over the SMBus. In that case, the NVDIMM driver would be an SMBus slave
 * device driver, and would interface with the hardware via an SMBus controller
 * driver such as this one.
 */

/* PCIe device IDs for (Sandy,Ivy)bridge)-Xeon and (Has,Broad)well-Xeon */
#define PCI_VENDOR_INTEL		0x8086
#define IMCSMB_PCI_DEV_ID_IMC0_SBX	0x3ca8
#define IMCSMB_PCI_DEV_ID_IMC0_IBX	0x0ea8
#define IMCSMB_PCI_DEV_ID_IMC0_HSX	0x2fa8
#define IMCSMB_PCI_DEV_ID_IMC0_BDX	0x6fa8
/* (Sandy,Ivy)bridge-Xeon only have a single memory controller per socket */
#define IMCSMB_PCI_DEV_ID_IMC1_HSX	0x2f68
#define IMCSMB_PCI_DEV_ID_IMC1_BDX	0x6f68

/* There are two SMBus controllers in each device. These define the registers
 * for each of these devices.
 */
static struct imcsmb_reg_set imcsmb_regs[] = {
	{
		.smb_stat = IMCSMB_REG_STATUS0,
		.smb_cmd = IMCSMB_REG_COMMAND0,
		.smb_cntl = IMCSMB_REG_CONTROL0
	},
	{
		.smb_stat = IMCSMB_REG_STATUS1,
		.smb_cmd = IMCSMB_REG_COMMAND1,
		.smb_cntl = IMCSMB_REG_CONTROL1
	},
};

static struct imcsmb_pci_device {
	uint16_t	id;
	char		*name;
} imcsmb_pci_devices[] = {
	{IMCSMB_PCI_DEV_ID_IMC0_SBX,
	    "Intel Sandybridge Xeon iMC 0 SMBus controllers"	},
	{IMCSMB_PCI_DEV_ID_IMC0_IBX,
	    "Intel Ivybridge Xeon iMC 0 SMBus controllers"	},
	{IMCSMB_PCI_DEV_ID_IMC0_HSX,
	    "Intel Haswell Xeon iMC 0 SMBus controllers"	},
	{IMCSMB_PCI_DEV_ID_IMC1_HSX,
	    "Intel Haswell Xeon iMC 1 SMBus controllers"	},
	{IMCSMB_PCI_DEV_ID_IMC0_BDX,
	    "Intel Broadwell Xeon iMC 0 SMBus controllers"	},
	{IMCSMB_PCI_DEV_ID_IMC1_BDX,
	    "Intel Broadwell Xeon iMC 1 SMBus controllers"	},
	{0, NULL},
};

/* Device methods. */
static int imcsmb_pci_attach(device_t dev);
static int imcsmb_pci_detach(device_t dev);
static int imcsmb_pci_probe(device_t dev);

/**
 * device_attach() method. Set up the PCI device's softc, then explicitly create
 * children for the actual imcsmbX controllers. Set up the child's ivars to
 * point to the proper set of the PCI device's config registers.
 *
 * @author Joe Kloss, rpokala
 *
 * @param[in,out] dev
 *      Device being attached.
 */
static int
imcsmb_pci_attach(device_t dev)
{
	struct imcsmb_pci_softc *sc;
	device_t child;
	int rc;
	int unit;

	/* Initialize private state */
	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->semaphore = 0;

	/* Create the imcsmbX children */
	for (unit = 0; unit < 2; unit++) {
		child = device_add_child(dev, "imcsmb", -1);
		if (child == NULL) {
			/* Nothing has been allocated, so there's no cleanup. */
			device_printf(dev, "Child imcsmb not added\n");
			rc = ENXIO;
			goto out;
		}
		/* Set the child's ivars to point to the appropriate set of
		 * the PCI device's registers.
		 */
		device_set_ivars(child, &imcsmb_regs[unit]);
	}

	/* Attach the imcsmbX children. */
	if ((rc = bus_generic_attach(dev)) != 0) {
		device_printf(dev, "failed to attach children: %d\n", rc);
		goto out;
	}

out:
	return (rc);
}

/**
 * device_detach() method. attach() didn't do any allocations, so all that's
 * needed here is to free up any downstream drivers and children.
 *
 * @author Joe Kloss
 *
 * @param[in] dev
 *      Device being detached.
 */
static int
imcsmb_pci_detach(device_t dev)
{
	int rc;

	/* Detach any attached drivers */
	rc = bus_generic_detach(dev);
	if (rc == 0) {
		/* Remove all children */
		rc = device_delete_children(dev);
	}

	return (rc);
}

/**
 * device_probe() method. Look for the right PCI vendor/device IDs.
 *
 * @author Joe Kloss, rpokala
 *
 * @param[in,out] dev
 *      Device being probed.
 */
static int
imcsmb_pci_probe(device_t dev)
{
	struct imcsmb_pci_device *pci_device;
	int rc;
	uint16_t pci_dev_id;

	rc = ENXIO;

	if (pci_get_vendor(dev) != PCI_VENDOR_INTEL) {
		goto out;
	}

	pci_dev_id = pci_get_device(dev);
	for (pci_device = imcsmb_pci_devices;
	    pci_device->name != NULL;
	    pci_device++) {
		if (pci_dev_id == pci_device->id) {
			device_set_desc(dev, pci_device->name);
			rc = BUS_PROBE_DEFAULT;
			goto out;
		}
	}

out:
	return (rc);
}

/**
 * Invoked via smbus_callback() -> imcsmb_callback(); clear the semaphore, and
 * re-enable motherboard-specific DIMM temperature monitoring if needed. This
 * gets called after the transaction completes.
 *
 * @author Joe Kloss
 *
 * @param[in,out] dev
 *      The device whose busses to release.
 */
void
imcsmb_pci_release_bus(device_t dev)
{
	struct imcsmb_pci_softc *sc;

	sc = device_get_softc(dev);

	/*
	 * IF NEEDED, INSERT MOTHERBOARD-SPECIFIC CODE TO RE-ENABLE DIMM
	 * TEMPERATURE MONITORING HERE.
	 */

	atomic_store_rel_int(&sc->semaphore, 0);
}

/**
 * Invoked via smbus_callback() -> imcsmb_callback(); set the semaphore, and
 * disable motherboard-specific DIMM temperature monitoring if needed. This gets
 * called before the transaction starts.
 *
 * @author Joe Kloss
 *
 * @param[in,out] dev
 *      The device whose busses to request.
 */
int
imcsmb_pci_request_bus(device_t dev)
{
	struct imcsmb_pci_softc *sc;
	int rc;

	sc = device_get_softc(dev);
	rc = 0;

	/* We don't want to block. Use a simple test-and-set semaphore to
	 * protect the bus.
	 */
	if (atomic_cmpset_acq_int(&sc->semaphore, 0, 1) == 0) {
		rc = EWOULDBLOCK;
	}

	/*
	 * IF NEEDED, INSERT MOTHERBOARD-SPECIFIC CODE TO DISABLE DIMM
	 * TEMPERATURE MONITORING HERE.
	 */

	return (rc);
}

/* Our device class */
static devclass_t imcsmb_pci_devclass;

/* Device methods */
static device_method_t imcsmb_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_attach,	imcsmb_pci_attach),
	DEVMETHOD(device_detach,	imcsmb_pci_detach),
	DEVMETHOD(device_probe,		imcsmb_pci_probe),

	DEVMETHOD_END
};

static driver_t imcsmb_pci_driver = {
	.name = "imcsmb_pci",
	.methods = imcsmb_pci_methods,
	.size = sizeof(struct imcsmb_pci_softc),
};

DRIVER_MODULE(imcsmb_pci, pci, imcsmb_pci_driver, imcsmb_pci_devclass, 0, 0);
MODULE_DEPEND(imcsmb_pci, pci, 1, 1, 1);
MODULE_VERSION(imcsmb_pci, 1);

/* vi: set ts=8 sw=4 sts=8 noet: */
