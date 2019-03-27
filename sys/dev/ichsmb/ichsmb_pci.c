/*-
 * ichsmb_pci.c
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 2000 Whistle Communications, Inc.
 * All rights reserved.
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 *
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Support for the SMBus controller logical device which is part of the
 * Intel 81801AA/AB/BA/CA/DC/EB (ICH/ICH[02345]) I/O controller hub chips.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/smbus/smbconf.h>

#include <dev/ichsmb/ichsmb_var.h>
#include <dev/ichsmb/ichsmb_reg.h>

/* PCI unique identifiers */
#define	PCI_VENDOR_INTEL		0x8086
#define	ID_82801AA			0x2413
#define	ID_82801AB			0x2423
#define	ID_82801BA			0x2443
#define	ID_82801CA			0x2483
#define	ID_82801DC			0x24C3
#define	ID_82801EB			0x24D3
#define	ID_82801FB			0x266A
#define	ID_82801GB			0x27da
#define	ID_82801H			0x283e
#define	ID_82801I			0x2930
#define	ID_EP80579			0x5032
#define	ID_82801JI			0x3a30
#define	ID_82801JD			0x3a60
#define	ID_PCH				0x3b30
#define	ID_6300ESB			0x25a4
#define	ID_631xESB			0x269b
#define	ID_DH89XXCC			0x2330
#define	ID_PATSBURG			0x1d22
#define	ID_CPT				0x1c22
#define	ID_PPT				0x1e22
#define	ID_AVOTON			0x1f3c
#define	ID_COLETOCRK			0x23B0
#define	ID_LPT				0x8c22
#define	ID_LPTLP			0x9c22
#define	ID_WCPT				0x8ca2
#define	ID_WCPTLP			0x9ca2
#define	ID_BAYTRAIL			0x0f12
#define	ID_BRASWELL			0x2292
#define	ID_WELLSBURG			0x8d22
#define	ID_SRPT				0xa123
#define	ID_SRPTLP			0x9d23
#define	ID_DENVERTON			0x19df
#define	ID_BROXTON			0x5ad4
#define	ID_LEWISBURG			0xa1a3
#define	ID_LEWISBURG2			0xa223
#define	ID_KABYLAKE			0xa2a3
#define	ID_CANNONLAKE			0xa323

static const struct ichsmb_device {
	uint16_t	id;
	const char	*name;
} ichsmb_devices[] = {
	{ ID_82801AA,	"Intel 82801AA (ICH) SMBus controller"		},
	{ ID_82801AB,	"Intel 82801AB (ICH0) SMBus controller"		},
	{ ID_82801BA,	"Intel 82801BA (ICH2) SMBus controller"		},
	{ ID_82801CA,	"Intel 82801CA (ICH3) SMBus controller"		},
	{ ID_82801DC,	"Intel 82801DC (ICH4) SMBus controller"		},
	{ ID_82801EB,	"Intel 82801EB (ICH5) SMBus controller"		},
	{ ID_82801FB,	"Intel 82801FB (ICH6) SMBus controller"		},
	{ ID_82801GB,	"Intel 82801GB (ICH7) SMBus controller"		},
	{ ID_82801H,	"Intel 82801H (ICH8) SMBus controller"		},
	{ ID_82801I,	"Intel 82801I (ICH9) SMBus controller"		},
	{ ID_82801GB,	"Intel 82801GB (ICH7) SMBus controller"		},
	{ ID_82801H,	"Intel 82801H (ICH8) SMBus controller"		},
	{ ID_82801I,	"Intel 82801I (ICH9) SMBus controller"		},
	{ ID_EP80579,	"Intel EP80579 SMBus controller"		},
	{ ID_82801JI,	"Intel 82801JI (ICH10) SMBus controller"	},
	{ ID_82801JD,	"Intel 82801JD (ICH10) SMBus controller"	},
	{ ID_PCH,	"Intel PCH SMBus controller"			},
	{ ID_6300ESB,	"Intel 6300ESB (ICH) SMBus controller"		},
	{ ID_631xESB,	"Intel 631xESB/6321ESB (ESB2) SMBus controller"	},
	{ ID_DH89XXCC,	"Intel DH89xxCC SMBus controller"		},
	{ ID_PATSBURG,	"Intel Patsburg SMBus controller"		},
	{ ID_CPT,	"Intel Cougar Point SMBus controller"		},
	{ ID_PPT,	"Intel Panther Point SMBus controller"		},
	{ ID_AVOTON,	"Intel Avoton SMBus controller"			},
	{ ID_LPT,	"Intel Lynx Point SMBus controller"		},
	{ ID_LPTLP,	"Intel Lynx Point-LP SMBus controller"		},
	{ ID_WCPT,	"Intel Wildcat Point SMBus controller"		},
	{ ID_WCPTLP,	"Intel Wildcat Point-LP SMBus controller"	},
	{ ID_BAYTRAIL,	"Intel Baytrail SMBus controller"		},
	{ ID_BRASWELL,	"Intel Braswell SMBus controller"		},
	{ ID_COLETOCRK,	"Intel Coleto Creek SMBus controller"		},
	{ ID_WELLSBURG,	"Intel Wellsburg SMBus controller"		},
	{ ID_SRPT,	"Intel Sunrise Point-H SMBus controller"	},
	{ ID_SRPTLP,	"Intel Sunrise Point-LP SMBus controller"	},
	{ ID_DENVERTON,	"Intel Denverton SMBus controller"		},
	{ ID_BROXTON,	"Intel Broxton SMBus controller"		},
	{ ID_LEWISBURG,	"Intel Lewisburg SMBus controller"		},
	{ ID_LEWISBURG2,"Intel Lewisburg SMBus controller"		},
	{ ID_KABYLAKE,	"Intel Kaby Lake SMBus controller"		},
	{ ID_CANNONLAKE,"Intel Cannon Lake SMBus controller"		},
	{ 0, NULL },
};

/* Internal functions */
static int	ichsmb_pci_probe(device_t dev);
static int	ichsmb_pci_attach(device_t dev);
/*Use generic one for now*/
#if 0
static int	ichsmb_pci_detach(device_t dev);
#endif

/* Device methods */
static device_method_t ichsmb_pci_methods[] = {
	/* Device interface */
        DEVMETHOD(device_probe, ichsmb_pci_probe),
        DEVMETHOD(device_attach, ichsmb_pci_attach),
        DEVMETHOD(device_detach, ichsmb_detach),

	/* SMBus methods */
        DEVMETHOD(smbus_callback, ichsmb_callback),
        DEVMETHOD(smbus_quick, ichsmb_quick),
        DEVMETHOD(smbus_sendb, ichsmb_sendb),
        DEVMETHOD(smbus_recvb, ichsmb_recvb),
        DEVMETHOD(smbus_writeb, ichsmb_writeb),
        DEVMETHOD(smbus_writew, ichsmb_writew),
        DEVMETHOD(smbus_readb, ichsmb_readb),
        DEVMETHOD(smbus_readw, ichsmb_readw),
        DEVMETHOD(smbus_pcall, ichsmb_pcall),
        DEVMETHOD(smbus_bwrite, ichsmb_bwrite),
        DEVMETHOD(smbus_bread, ichsmb_bread),

	DEVMETHOD_END
};

static driver_t ichsmb_pci_driver = {
	"ichsmb",
	ichsmb_pci_methods,
	sizeof(struct ichsmb_softc)
};

static devclass_t ichsmb_pci_devclass;

DRIVER_MODULE(ichsmb, pci, ichsmb_pci_driver, ichsmb_pci_devclass, 0, 0);

static int
ichsmb_pci_probe(device_t dev)
{
	const struct ichsmb_device *device;

	if (pci_get_vendor(dev) != PCI_VENDOR_INTEL)
		return (ENXIO);

	for (device = ichsmb_devices; device->name != NULL; device++) {
		if (pci_get_device(dev) == device->id) {
			device_set_desc(dev, device->name);
			return (ichsmb_probe(dev));
		}
	}

	return (ENXIO);
}

static int
ichsmb_pci_attach(device_t dev)
{
	const sc_p sc = device_get_softc(dev);
	int error;

	/* Initialize private state */
	bzero(sc, sizeof(*sc));
	sc->ich_cmd = -1;
	sc->dev = dev;

	/* Allocate an I/O range */
	sc->io_rid = ICH_SMB_BASE;
	sc->io_res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
	    &sc->io_rid, 16, RF_ACTIVE);
	if (sc->io_res == NULL)
		sc->io_res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
		    &sc->io_rid, 32, RF_ACTIVE);
	if (sc->io_res == NULL) {
		device_printf(dev, "can't map I/O\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate interrupt */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "can't get IRQ\n");
		error = ENXIO;
		goto fail;
	}

	/* Enable device */
	pci_write_config(dev, ICH_HOSTC, ICH_HOSTC_HST_EN, 1);

	/* Done */
	error = ichsmb_attach(dev);
	if (error)
		goto fail;
	return (0);

fail:
	/* Attach failed, release resources */
	ichsmb_release_resources(sc);
	return (error);
}


MODULE_DEPEND(ichsmb, pci, 1, 1, 1);
MODULE_DEPEND(ichsmb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(ichsmb, 1);
