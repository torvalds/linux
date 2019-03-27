/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Matthew N. Dodd <winter@jurai.net>
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
 * VPD decoder for IBM systems (Thinkpads)
 * http://www-1.ibm.com/support/docview.wss?uid=psg1MIGR-45120
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <machine/md_var.h>
#include <machine/pc/bios.h>

/*
 * Vital Product Data
 */
struct vpd {
	u_int16_t	Header;			/* 0x55AA */
	u_int8_t	Signature[3];		/* Always 'VPD' */
	u_int8_t	Length;			/* Sructure Length */

	u_int8_t	Reserved[7];		/* Reserved */

	u_int8_t	BuildID[9];		/* BIOS Build ID */
	u_int8_t	BoxSerial[7];		/* Box Serial Number */
	u_int8_t	PlanarSerial[11];	/* Motherboard Serial Number */
	u_int8_t	MachType[7];		/* Machine Type/Model */
	u_int8_t	Checksum;		/* Checksum */
} __packed;

struct vpd_softc {
	device_t		dev;
	struct resource *	res;
	int			rid;

	struct vpd *		vpd;

	struct sysctl_ctx_list  ctx;

	char		BuildID[10];
	char		BoxSerial[8];
	char		PlanarSerial[12];
	char		MachineType[5];
	char		MachineModel[4];
};

#define	VPD_START	0xf0000
#define	VPD_STEP	0x10
#define	VPD_OFF		2
#define	VPD_LEN		3
#define	VPD_SIG		"VPD"

#define	RES2VPD(res)	((struct vpd *)rman_get_virtual(res))
#define	ADDR2VPD(addr)	((struct vpd *)BIOS_PADDRTOVADDR(addr))

static devclass_t vpd_devclass;

static void	vpd_identify	(driver_t *, device_t);
static int	vpd_probe	(device_t);
static int	vpd_attach	(device_t);
static int	vpd_detach	(device_t);
static int	vpd_modevent	(module_t, int, void *);

static int	vpd_cksum	(struct vpd *);

static SYSCTL_NODE(_hw, OID_AUTO, vpd, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd, OID_AUTO, machine, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd_machine, OID_AUTO, type, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd_machine, OID_AUTO, model, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd, OID_AUTO, build_id, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd, OID_AUTO, serial, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd_serial, OID_AUTO, box, CTLFLAG_RD, NULL, NULL);
static SYSCTL_NODE(_hw_vpd_serial, OID_AUTO, planar, CTLFLAG_RD, NULL, NULL);

static void
vpd_identify (driver_t *driver, device_t parent)
{
	device_t child;
	u_int32_t addr;
	int length;
	int rid;

	if (!device_is_alive(parent))
		return;

	addr = bios_sigsearch(VPD_START, VPD_SIG, VPD_LEN, VPD_STEP, VPD_OFF);
	if (addr != 0) {
		rid = 0;
		length = ADDR2VPD(addr)->Length;

		child = BUS_ADD_CHILD(parent, 5, "vpd", -1);
		device_set_driver(child, driver);
		bus_set_resource(child, SYS_RES_MEMORY, rid, addr, length);
		device_set_desc(child, "Vital Product Data Area");
	}
		
	return;
}

static int
vpd_probe (device_t dev)
{
	struct resource *res;
	int rid;
	int error;

	error = 0;
	rid = 0;
	res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (res == NULL) {
		device_printf(dev, "Unable to allocate memory resource.\n");
		error = ENOMEM;
		goto bad;
	}

	if (vpd_cksum(RES2VPD(res)))
		device_printf(dev, "VPD checksum failed.  BIOS update may be required.\n");

bad:
	if (res)
		bus_release_resource(dev, SYS_RES_MEMORY, rid, res);
	return (error);
}

static int
vpd_attach (device_t dev)
{
	struct vpd_softc *sc;
	char unit[4];
	int error;

	sc = device_get_softc(dev);
	error = 0;

	sc->dev = dev;
	sc->rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
		RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "Unable to allocate memory resource.\n");
		error = ENOMEM;
		goto bad;
	}
	sc->vpd = RES2VPD(sc->res);

	snprintf(unit, sizeof(unit), "%d", device_get_unit(sc->dev));
	snprintf(sc->MachineType, 5, "%.4s", sc->vpd->MachType);
	snprintf(sc->MachineModel, 4, "%.3s", sc->vpd->MachType+4);
	snprintf(sc->BuildID, 10, "%.9s", sc->vpd->BuildID);
	snprintf(sc->BoxSerial, 8, "%.7s", sc->vpd->BoxSerial);
	snprintf(sc->PlanarSerial, 12, "%.11s", sc->vpd->PlanarSerial);

	sysctl_ctx_init(&sc->ctx);
	SYSCTL_ADD_STRING(&sc->ctx,
		SYSCTL_STATIC_CHILDREN(_hw_vpd_machine_type), OID_AUTO,
		unit, CTLFLAG_RD|CTLFLAG_DYN, sc->MachineType, 0, NULL);
	SYSCTL_ADD_STRING(&sc->ctx,
		SYSCTL_STATIC_CHILDREN(_hw_vpd_machine_model), OID_AUTO,
		unit, CTLFLAG_RD|CTLFLAG_DYN, sc->MachineModel, 0, NULL);
	SYSCTL_ADD_STRING(&sc->ctx,
		SYSCTL_STATIC_CHILDREN(_hw_vpd_build_id), OID_AUTO,
		unit, CTLFLAG_RD|CTLFLAG_DYN, sc->BuildID, 0, NULL);
	SYSCTL_ADD_STRING(&sc->ctx,
		SYSCTL_STATIC_CHILDREN(_hw_vpd_serial_box), OID_AUTO,
		unit, CTLFLAG_RD|CTLFLAG_DYN, sc->BoxSerial, 0, NULL);
	SYSCTL_ADD_STRING(&sc->ctx,
		SYSCTL_STATIC_CHILDREN(_hw_vpd_serial_planar), OID_AUTO,
		unit, CTLFLAG_RD|CTLFLAG_DYN, sc->PlanarSerial, 0, NULL);

	device_printf(dev, "Machine Type: %.4s, Model: %.3s, Build ID: %.9s\n",
		sc->MachineType, sc->MachineModel, sc->BuildID);
	device_printf(dev, "Box Serial: %.7s, Planar Serial: %.11s\n",
		sc->BoxSerial, sc->PlanarSerial);
		
	return (0);
bad:
	if (sc->res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
	return (error);
}

static int
vpd_detach (device_t dev)
{
	struct vpd_softc *sc;

	sc = device_get_softc(dev);

	if (sc->res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);

	sysctl_ctx_free(&sc->ctx);

	return (0);
}

static int
vpd_modevent (mod, what, arg)
        module_t        mod;
        int             what;
        void *          arg;
{
	device_t *	devs;
	int		count;
	int		i;

	switch (what) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		devclass_get_devices(vpd_devclass, &devs, &count);
		for (i = 0; i < count; i++) {
			device_delete_child(device_get_parent(devs[i]), devs[i]);
		}
		break;
	default:
		break;
	}

	return (0);
}

static device_method_t vpd_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,      vpd_identify),
	DEVMETHOD(device_probe,         vpd_probe),
	DEVMETHOD(device_attach,        vpd_attach),
	DEVMETHOD(device_detach,        vpd_detach),
	{ 0, 0 }
};

static driver_t vpd_driver = {
	"vpd",
	vpd_methods,
	sizeof(struct vpd_softc),
};

DRIVER_MODULE(vpd, nexus, vpd_driver, vpd_devclass, vpd_modevent, 0);
MODULE_VERSION(vpd, 1);

/*
 * Perform a checksum over the VPD structure, starting with
 * the BuildID.  (Jean Delvare <khali@linux-fr.org>)
 */
static int
vpd_cksum (struct vpd *v)
{
	u_int8_t *ptr;
	u_int8_t cksum;
	int i;

	ptr = (u_int8_t *)v;
	cksum = 0;
	for (i = offsetof(struct vpd, BuildID); i < v->Length ; i++)
		cksum += ptr[i];
	return (cksum);
}
