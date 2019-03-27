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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

/* And all this for BIOS_PADDRTOVADDR() */
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <machine/md_var.h>
#include <machine/pc/bios.h>

#include <machine/smapi.h>

#define	SMAPI_START	0xf0000
#define	SMAPI_STEP	0x10
#define	SMAPI_OFF	0
#define	SMAPI_LEN	4
#define	SMAPI_SIG	"$SMB"

#define	RES2HDR(res)	((struct smapi_bios_header *)rman_get_virtual(res))
#define	ADDR2HDR(addr)	((struct smapi_bios_header *)BIOS_PADDRTOVADDR(addr))

struct smapi_softc {
	struct cdev *		cdev;
	device_t		dev;
	struct resource *	res;
	int			rid;

	u_int32_t		smapi32_entry;

	struct smapi_bios_header *header;
};

extern u_long smapi32_offset;
extern u_short smapi32_segment;

devclass_t smapi_devclass;

static d_ioctl_t smapi_ioctl;

static struct cdevsw smapi_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	smapi_ioctl,
	.d_name =	"smapi",
	.d_flags =	D_NEEDGIANT,
};

static void	smapi_identify(driver_t *, device_t);
static int	smapi_probe(device_t);
static int	smapi_attach(device_t);
static int	smapi_detach(device_t);
static int	smapi_modevent(module_t, int, void *);
                
static int	smapi_header_cksum(struct smapi_bios_header *);

extern int	smapi32(struct smapi_bios_parameter *,
		    struct smapi_bios_parameter *);
extern int	smapi32_new(u_long, u_short,
		    struct smapi_bios_parameter *,
		    struct smapi_bios_parameter *);

static int
smapi_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct smapi_softc *sc;
	int error;

	error = 0;
	sc = devclass_get_softc(smapi_devclass, dev2unit(dev)); 
        if (sc == NULL) {
                error = ENXIO;
                goto fail;
        }

	switch (cmd) {
	case SMAPIOGHEADER:
		bcopy((caddr_t)sc->header, data,
				sizeof(struct smapi_bios_header)); 
		error = 0;
		break;
	case SMAPIOCGFUNCTION:
		smapi32_offset = sc->smapi32_entry;
		error = smapi32((struct smapi_bios_parameter *)data,
				(struct smapi_bios_parameter *)data);
		break;
	default:
		error = ENOTTY;
	}

fail:
	return (error);
}

static int
smapi_header_cksum (struct smapi_bios_header *header)
{
	u_int8_t *ptr;
	u_int8_t cksum;
	int i;

	ptr = (u_int8_t *)header;
	cksum = 0;
	for (i = 0; i < header->length; i++) {
		cksum += ptr[i];	
	}

	return (cksum);
}

static void
smapi_identify (driver_t *driver, device_t parent)
{
	device_t child;
	u_int32_t addr;
	int length;
	int rid;

	if (!device_is_alive(parent))
		return;

	addr = bios_sigsearch(SMAPI_START, SMAPI_SIG, SMAPI_LEN,
                              SMAPI_STEP, SMAPI_OFF);
	if (addr != 0) {
		rid = 0;
		length = ADDR2HDR(addr)->length;

		child = BUS_ADD_CHILD(parent, 5, "smapi", -1);
		device_set_driver(child, driver);
		bus_set_resource(child, SYS_RES_MEMORY, rid, addr, length);
		device_set_desc(child, "SMAPI BIOS");
	}

	return;
}

static int
smapi_probe (device_t dev)
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

	if (smapi_header_cksum(RES2HDR(res))) {
		device_printf(dev, "SMAPI header checksum failed.\n");
		error = ENXIO;
		goto bad;
	}

bad:
	if (res)
		bus_release_resource(dev, SYS_RES_MEMORY, rid, res);
	return (error);
}

static int
smapi_attach (device_t dev)
{
	struct smapi_softc *sc;
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
	sc->header = (struct smapi_bios_header *)rman_get_virtual(sc->res);
	sc->smapi32_entry = (u_int32_t)BIOS_PADDRTOVADDR(
					sc->header->prot32_segment +
					sc->header->prot32_offset);

        sc->cdev = make_dev(&smapi_cdevsw,
			device_get_unit(sc->dev),
			UID_ROOT, GID_WHEEL, 0600,
			"%s%d",
			smapi_cdevsw.d_name,
			device_get_unit(sc->dev));

	device_printf(dev, "Version: %d.%02d, Length: %d, Checksum: 0x%02x\n",
		bcd2bin(sc->header->version_major),
		bcd2bin(sc->header->version_minor),
		sc->header->length,
		sc->header->checksum);
	device_printf(dev, "Information=0x%b\n",
		sc->header->information,
		"\020"
		"\001REAL_VM86"
		"\002PROTECTED_16"
		"\003PROTECTED_32");

	if (bootverbose) {
		if (sc->header->information & SMAPI_REAL_VM86)
			device_printf(dev, "Real/VM86 mode: Segment 0x%04x, Offset 0x%04x\n",
				sc->header->real16_segment,
				sc->header->real16_offset);
		if (sc->header->information & SMAPI_PROT_16BIT)
			device_printf(dev, "16-bit Protected mode: Segment 0x%08x, Offset 0x%04x\n",
				sc->header->prot16_segment,
				sc->header->prot16_offset);
		if (sc->header->information & SMAPI_PROT_32BIT)
			device_printf(dev, "32-bit Protected mode: Segment 0x%08x, Offset 0x%08x\n",
				sc->header->prot32_segment,
				sc->header->prot32_offset);
	}

	return (0);
bad:
	if (sc->res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
	return (error);
}

static int
smapi_detach (device_t dev)
{
	struct smapi_softc *sc;

	sc = device_get_softc(dev);

	destroy_dev(sc->cdev);

	if (sc->res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);

	return (0);
}

static int
smapi_modevent (module_t mod, int what, void *arg)
{
	device_t *	devs;
	int		count;
	int		i;

	switch (what) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		devclass_get_devices(smapi_devclass, &devs, &count);
		for (i = 0; i < count; i++) {
			device_delete_child(device_get_parent(devs[i]), devs[i]);
		}
		free(devs, M_TEMP);
		break;
	default:
		break;
	}

	return (0);
}

static device_method_t smapi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,      smapi_identify),
	DEVMETHOD(device_probe,         smapi_probe),
	DEVMETHOD(device_attach,        smapi_attach),
	DEVMETHOD(device_detach,        smapi_detach),
	{ 0, 0 }
};

static driver_t smapi_driver = {
	"smapi",
	smapi_methods,
	sizeof(struct smapi_softc),
};

DRIVER_MODULE(smapi, nexus, smapi_driver, smapi_devclass, smapi_modevent, 0);
MODULE_VERSION(smapi, 1);
