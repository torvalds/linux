/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vga.h"
#include "opt_fb.h"
#include "opt_syscons.h"	/* should be removed in the future, XXX */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/fbio.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#ifdef __i386__
#include <machine/pc/bios.h>
#endif

#include <dev/fb/fbreg.h>
#include <dev/fb/vgareg.h>

#include <isa/isareg.h>
#include <isa/isavar.h>

#define VGA_ID		0x0009d041	/* PNP0900 */

static struct isa_pnp_id vga_ids[] = {
	{ VGA_ID,	NULL },		/* PNP0900 */
	{ 0,		NULL },
};

static void
vga_suspend(device_t dev)
{
	vga_softc_t *sc;
	int nbytes;

	sc = device_get_softc(dev);

	/* Save the video state across the suspend. */
	if (sc->state_buf != NULL)
		goto save_palette;
	nbytes = vidd_save_state(sc->adp, NULL, 0);
	if (nbytes <= 0)
		goto save_palette;
	sc->state_buf = malloc(nbytes, M_TEMP, M_NOWAIT);
	if (sc->state_buf == NULL)
		goto save_palette;
	if (bootverbose)
		device_printf(dev, "saving %d bytes of video state\n", nbytes);
	if (vidd_save_state(sc->adp, sc->state_buf, nbytes) != 0) {
		device_printf(dev, "failed to save state (nbytes=%d)\n",
		    nbytes);
		free(sc->state_buf, M_TEMP);
		sc->state_buf = NULL;
	}

save_palette:
	/* Save the color palette across the suspend. */
	if (sc->pal_buf != NULL)
		return;
	sc->pal_buf = malloc(256 * 3, M_TEMP, M_NOWAIT);
	if (sc->pal_buf == NULL)
		return;
	if (bootverbose)
		device_printf(dev, "saving color palette\n");
	if (vidd_save_palette(sc->adp, sc->pal_buf) != 0) {
		device_printf(dev, "failed to save palette\n");
		free(sc->pal_buf, M_TEMP);
		sc->pal_buf = NULL;
	}
}

static void
vga_resume(device_t dev)
{
	vga_softc_t *sc;

	sc = device_get_softc(dev);

	if (sc->state_buf != NULL) {
		if (vidd_load_state(sc->adp, sc->state_buf) != 0)
			device_printf(dev, "failed to reload state\n");
		free(sc->state_buf, M_TEMP);
		sc->state_buf = NULL;
	}
	if (sc->pal_buf != NULL) {
		if (vidd_load_palette(sc->adp, sc->pal_buf) != 0)
			device_printf(dev, "failed to reload palette\n");
		free(sc->pal_buf, M_TEMP);
		sc->pal_buf = NULL;
	}
}

#define VGA_SOFTC(unit)		\
	((vga_softc_t *)devclass_get_softc(isavga_devclass, unit))

static devclass_t	isavga_devclass;

#ifdef FB_INSTALL_CDEV

static d_open_t		isavga_open;
static d_close_t	isavga_close;
static d_read_t		isavga_read;
static d_write_t	isavga_write;
static d_ioctl_t	isavga_ioctl;
static d_mmap_t		isavga_mmap;

static struct cdevsw isavga_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	isavga_open,
	.d_close =	isavga_close,
	.d_read =	isavga_read,
	.d_write =	isavga_write,
	.d_ioctl =	isavga_ioctl,
	.d_mmap =	isavga_mmap,
	.d_name =	VGA_DRIVER_NAME,
};

#endif /* FB_INSTALL_CDEV */

static void
isavga_identify(driver_t *driver, device_t parent)
{
	BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, VGA_DRIVER_NAME, 0);
}

static int
isavga_probe(device_t dev)
{
	video_adapter_t adp;
	int error;

	/* No pnp support */
	if (isa_get_vendorid(dev))
		return (ENXIO);

	error = vga_probe_unit(device_get_unit(dev), &adp, device_get_flags(dev));
	if (error == 0) {
		device_set_desc(dev, "Generic ISA VGA");
		bus_set_resource(dev, SYS_RES_IOPORT, 0,
				 adp.va_io_base, adp.va_io_size);
		bus_set_resource(dev, SYS_RES_MEMORY, 0,
				 adp.va_mem_base, adp.va_mem_size);
		isa_set_vendorid(dev, VGA_ID);
		isa_set_logicalid(dev, VGA_ID);
#if 0
		isa_set_port(dev, adp.va_io_base);
		isa_set_portsize(dev, adp.va_io_size);
		isa_set_maddr(dev, adp.va_mem_base);
		isa_set_msize(dev, adp.va_mem_size);
#endif
	}
	return (error);
}

static int
isavga_attach(device_t dev)
{
	vga_softc_t *sc;
	int unit;
	int rid;
	int error;

	unit = device_get_unit(dev);
	sc = device_get_softc(dev);

	rid = 0;
	bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
				  RF_ACTIVE | RF_SHAREABLE);
	rid = 0;
	bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
				 RF_ACTIVE | RF_SHAREABLE);

	error = vga_attach_unit(unit, sc, device_get_flags(dev));
	if (error)
		return (error);

#ifdef FB_INSTALL_CDEV
	/* attach a virtual frame buffer device */
	error = fb_attach(VGA_MKMINOR(unit), sc->adp, &isavga_cdevsw);
	if (error)
		return (error);
#endif /* FB_INSTALL_CDEV */

	if (0 && bootverbose)
		vidd_diag(sc->adp, bootverbose);

#if 0 /* experimental */
	device_add_child(dev, "fb", -1);
	bus_generic_attach(dev);
#endif

	return (0);
}

static int
isavga_suspend(device_t dev)
{
	int error;

	error = bus_generic_suspend(dev);
	if (error != 0)
		return (error);
	vga_suspend(dev);

	return (error);
}

static int
isavga_resume(device_t dev)
{

	vga_resume(dev);

	return (bus_generic_resume(dev));
}

#ifdef FB_INSTALL_CDEV

static int
isavga_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	return (vga_open(dev, VGA_SOFTC(VGA_UNIT(dev)), flag, mode, td));
}

static int
isavga_close(struct cdev *dev, int flag, int mode, struct thread *td)
{
	return (vga_close(dev, VGA_SOFTC(VGA_UNIT(dev)), flag, mode, td));
}

static int
isavga_read(struct cdev *dev, struct uio *uio, int flag)
{
	return (vga_read(dev, VGA_SOFTC(VGA_UNIT(dev)), uio, flag));
}

static int
isavga_write(struct cdev *dev, struct uio *uio, int flag)
{
	return (vga_write(dev, VGA_SOFTC(VGA_UNIT(dev)), uio, flag));
}

static int
isavga_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{
	return (vga_ioctl(dev, VGA_SOFTC(VGA_UNIT(dev)), cmd, arg, flag, td));
}

static int
isavga_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	return (vga_mmap(dev, VGA_SOFTC(VGA_UNIT(dev)), offset, paddr, prot,
	    memattr));
}

#endif /* FB_INSTALL_CDEV */

static device_method_t isavga_methods[] = {
	DEVMETHOD(device_identify,	isavga_identify),
	DEVMETHOD(device_probe,		isavga_probe),
	DEVMETHOD(device_attach,	isavga_attach),
	DEVMETHOD(device_suspend,	isavga_suspend),
	DEVMETHOD(device_resume,	isavga_resume),

	DEVMETHOD_END
};

static driver_t isavga_driver = {
	VGA_DRIVER_NAME,
	isavga_methods,
	sizeof(vga_softc_t),
};

DRIVER_MODULE(vga, isa, isavga_driver, isavga_devclass, 0, 0);

static devclass_t	vgapm_devclass;

static void
vgapm_identify(driver_t *driver, device_t parent)
{

	if (device_get_flags(parent) != 0)
		device_add_child(parent, "vgapm", 0);
}

static int
vgapm_probe(device_t dev)
{

	device_set_desc(dev, "VGA suspend/resume");
	device_quiet(dev);

	return (BUS_PROBE_DEFAULT);
}

static int
vgapm_attach(device_t dev)
{

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
vgapm_suspend(device_t dev)
{
	device_t vga_dev;
	int error;

	error = bus_generic_suspend(dev);
	if (error != 0)
		return (error);
	vga_dev = devclass_get_device(isavga_devclass, 0);
	if (vga_dev == NULL)
		return (0);
	vga_suspend(vga_dev);

	return (0);
}

static int
vgapm_resume(device_t dev)
{
	device_t vga_dev;

	vga_dev = devclass_get_device(isavga_devclass, 0);
	if (vga_dev != NULL)
		vga_resume(vga_dev);

	return (bus_generic_resume(dev));
}

static device_method_t vgapm_methods[] = {
	DEVMETHOD(device_identify,	vgapm_identify),
	DEVMETHOD(device_probe,		vgapm_probe),
	DEVMETHOD(device_attach,	vgapm_attach),
	DEVMETHOD(device_suspend,	vgapm_suspend),
	DEVMETHOD(device_resume,	vgapm_resume),
	{ 0, 0 }
};

static driver_t vgapm_driver = {
	"vgapm",
	vgapm_methods,
	0
};

DRIVER_MODULE(vgapm, vgapci, vgapm_driver, vgapm_devclass, 0, 0);
ISA_PNP_INFO(vga_ids);
