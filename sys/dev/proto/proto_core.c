/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/reboot.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/uio.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <dev/pci/pcivar.h>

#include <dev/proto/proto.h>
#include <dev/proto/proto_dev.h>
#include <dev/proto/proto_busdma.h>

CTASSERT(SYS_RES_IRQ != PROTO_RES_UNUSED &&
    SYS_RES_DRQ != PROTO_RES_UNUSED &&
    SYS_RES_MEMORY != PROTO_RES_UNUSED &&
    SYS_RES_IOPORT != PROTO_RES_UNUSED);
CTASSERT(SYS_RES_IRQ != PROTO_RES_PCICFG &&
    SYS_RES_DRQ != PROTO_RES_PCICFG &&
    SYS_RES_MEMORY != PROTO_RES_PCICFG &&
    SYS_RES_IOPORT != PROTO_RES_PCICFG);
CTASSERT(SYS_RES_IRQ != PROTO_RES_BUSDMA &&
    SYS_RES_DRQ != PROTO_RES_BUSDMA &&
    SYS_RES_MEMORY != PROTO_RES_BUSDMA &&
    SYS_RES_IOPORT != PROTO_RES_BUSDMA);

devclass_t proto_devclass;
char proto_driver_name[] = "proto";

static d_open_t proto_open;
static d_close_t proto_close;
static d_read_t proto_read;
static d_write_t proto_write;
static d_ioctl_t proto_ioctl;
static d_mmap_t proto_mmap;

struct cdevsw proto_devsw = {
	.d_version = D_VERSION,
	.d_flags = 0,
	.d_name = proto_driver_name,
	.d_open = proto_open,
	.d_close = proto_close,
	.d_read = proto_read,
	.d_write = proto_write,
	.d_ioctl = proto_ioctl,
	.d_mmap = proto_mmap,
};

static MALLOC_DEFINE(M_PROTO, "PROTO", "PROTO driver");

int
proto_add_resource(struct proto_softc *sc, int type, int rid,
    struct resource *res)
{
	struct proto_res *r;

	if (type == PROTO_RES_UNUSED)
		return (EINVAL);
	if (sc->sc_rescnt == PROTO_RES_MAX)
		return (ENOSPC);

	r = sc->sc_res + sc->sc_rescnt++;
	r->r_type = type;
	r->r_rid = rid;
	r->r_d.res = res;
	return (0);
}

#ifdef notyet
static int
proto_intr(void *arg)
{
	struct proto_softc *sc = arg;

	/* XXX TODO */
	return (FILTER_HANDLED);
}
#endif

int
proto_probe(device_t dev, const char *prefix, char ***devnamesp)
{
	char **devnames = *devnamesp;
	const char *dn, *ep, *ev;
	size_t pfxlen;
	int idx, names;

	if (devnames == NULL) {
		pfxlen = strlen(prefix);
		names = 1;	/* NULL pointer */
		ev = kern_getenv("hw.proto.attach");
		if (ev != NULL) {
			dn = ev;
			while (*dn != '\0') {
				ep = dn;
				while (*ep != ',' && *ep != '\0')
					ep++;
				if ((ep - dn) > pfxlen &&
				    strncmp(dn, prefix, pfxlen) == 0)
					names++;
				dn = (*ep == ',') ? ep + 1 : ep;
			}
		}
		devnames = malloc(names * sizeof(caddr_t), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		*devnamesp = devnames;
		if (ev != NULL) {
			dn = ev;
			idx = 0;
			while (*dn != '\0') {
				ep = dn;
				while (*ep != ',' && *ep != '\0')
					ep++;
				if ((ep - dn) > pfxlen &&
				    strncmp(dn, prefix, pfxlen) == 0) {
					devnames[idx] = malloc(ep - dn + 1,
					    M_DEVBUF, M_WAITOK | M_ZERO);
					memcpy(devnames[idx], dn, ep - dn);
					idx++;
				}
				dn = (*ep == ',') ? ep + 1 : ep;
			}
			freeenv(__DECONST(char *, ev));
		}
	}

	dn = device_get_desc(dev);
	while (*devnames != NULL) {
		if (strcmp(dn, *devnames) == 0)
			return (BUS_PROBE_SPECIFIC);
		devnames++;
	}
	return (BUS_PROBE_HOOVER);
}

int
proto_attach(device_t dev)
{
	struct proto_softc *sc;
	struct proto_res *r;
	u_int res;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	for (res = 0; res < sc->sc_rescnt; res++) {
		r = sc->sc_res + res;
		switch (r->r_type) {
		case SYS_RES_IRQ:
			/* XXX TODO */
			break;
		case SYS_RES_DRQ:
			break;
		case SYS_RES_MEMORY:
		case SYS_RES_IOPORT:
			r->r_size = rman_get_size(r->r_d.res);
			r->r_u.cdev = make_dev(&proto_devsw, res, 0, 0, 0600,
			    "proto/%s/%02x.%s", device_get_desc(dev), r->r_rid,
			    (r->r_type == SYS_RES_IOPORT) ? "io" : "mem");
			r->r_u.cdev->si_drv1 = sc;
			r->r_u.cdev->si_drv2 = r;
			break;
		case PROTO_RES_PCICFG:
			r->r_size = 4096;
			r->r_u.cdev = make_dev(&proto_devsw, res, 0, 0, 0600,
			    "proto/%s/pcicfg", device_get_desc(dev));
			r->r_u.cdev->si_drv1 = sc;
			r->r_u.cdev->si_drv2 = r;
			break;
		case PROTO_RES_BUSDMA:
			r->r_d.busdma = proto_busdma_attach(sc);
			r->r_size = 0;	/* no read(2) nor write(2) */
			r->r_u.cdev = make_dev(&proto_devsw, res, 0, 0, 0600,
			    "proto/%s/busdma", device_get_desc(dev));
			r->r_u.cdev->si_drv1 = sc;
			r->r_u.cdev->si_drv2 = r;
			break;
		}
	}
	return (0);
}

int
proto_detach(device_t dev)
{
	struct proto_softc *sc;
	struct proto_res *r;
	u_int res;

	sc = device_get_softc(dev);

	/* Don't detach if we have open device files. */
	for (res = 0; res < sc->sc_rescnt; res++) {
		r = sc->sc_res + res;
		if (r->r_opened)
			return (EBUSY);
	}

	for (res = 0; res < sc->sc_rescnt; res++) {
		r = sc->sc_res + res;
		switch (r->r_type) {
		case SYS_RES_IRQ:
			/* XXX TODO */
			bus_release_resource(dev, r->r_type, r->r_rid,
			    r->r_d.res);
			break;
		case SYS_RES_DRQ:
			bus_release_resource(dev, r->r_type, r->r_rid,
			    r->r_d.res);
			break;
		case SYS_RES_MEMORY:
		case SYS_RES_IOPORT:
			bus_release_resource(dev, r->r_type, r->r_rid,
			    r->r_d.res);
			destroy_dev(r->r_u.cdev);
			break;
		case PROTO_RES_PCICFG:
			destroy_dev(r->r_u.cdev);
			break;
		case PROTO_RES_BUSDMA:
			proto_busdma_detach(sc, r->r_d.busdma);
			destroy_dev(r->r_u.cdev);
			break;
		}
		r->r_type = PROTO_RES_UNUSED;
	}
	sc->sc_rescnt = 0;
	return (0);
}

/*
 * Device functions
 */

static int
proto_open(struct cdev *cdev, int oflags, int devtype, struct thread *td)
{
	struct proto_res *r;

	r = cdev->si_drv2;
	if (!atomic_cmpset_acq_ptr(&r->r_opened, 0UL, (uintptr_t)td->td_proc))
		return (EBUSY);
	return (0);
}

static int
proto_close(struct cdev *cdev, int fflag, int devtype, struct thread *td)
{
	struct proto_res *r;
	struct proto_softc *sc;

	sc = cdev->si_drv1;
	r = cdev->si_drv2;
	if (!atomic_cmpset_acq_ptr(&r->r_opened, (uintptr_t)td->td_proc, 0UL))
		return (ENXIO);
	if (r->r_type == PROTO_RES_BUSDMA)
		proto_busdma_cleanup(sc, r->r_d.busdma);
	return (0);
}

static int
proto_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
	union {
		uint8_t	x1[8];
		uint16_t x2[4];
		uint32_t x4[2];
		uint64_t x8[1];
	} buf;
	struct proto_softc *sc;
	struct proto_res *r;
	device_t dev;
	off_t ofs;
	u_long width;
	int error;

	sc = cdev->si_drv1;
	dev = sc->sc_dev;
	r = cdev->si_drv2;

	width = uio->uio_resid;
	if (width < 1 || width > 8 || bitcount16(width) > 1)
		return (EIO);
	ofs = uio->uio_offset;
	if (ofs + width > r->r_size)
		return (EIO);

	switch (width) {
	case 1:
		buf.x1[0] = (r->r_type == PROTO_RES_PCICFG) ?
		    pci_read_config(dev, ofs, 1) : bus_read_1(r->r_d.res, ofs);
		break;
	case 2:
		buf.x2[0] = (r->r_type == PROTO_RES_PCICFG) ?
		    pci_read_config(dev, ofs, 2) : bus_read_2(r->r_d.res, ofs);
		break;
	case 4:
		buf.x4[0] = (r->r_type == PROTO_RES_PCICFG) ?
		    pci_read_config(dev, ofs, 4) : bus_read_4(r->r_d.res, ofs);
		break;
#ifndef __i386__
	case 8:
		if (r->r_type == PROTO_RES_PCICFG)
			return (EINVAL);
		buf.x8[0] = bus_read_8(r->r_d.res, ofs);
		break;
#endif
	default:
		return (EIO);
	}

	error = uiomove(&buf, width, uio);
	return (error);
}

static int
proto_write(struct cdev *cdev, struct uio *uio, int ioflag)
{
	union {
		uint8_t	x1[8];
		uint16_t x2[4];
		uint32_t x4[2];
		uint64_t x8[1];
	} buf;
	struct proto_softc *sc;
	struct proto_res *r;
	device_t dev;
	off_t ofs;
	u_long width;
	int error;

	sc = cdev->si_drv1;
	dev = sc->sc_dev;
	r = cdev->si_drv2;

	width = uio->uio_resid;
	if (width < 1 || width > 8 || bitcount16(width) > 1)
		return (EIO);
	ofs = uio->uio_offset;
	if (ofs + width > r->r_size)
		return (EIO);

	error = uiomove(&buf, width, uio);
	if (error)
		return (error);

	switch (width) {
	case 1:
		if (r->r_type == PROTO_RES_PCICFG)
			pci_write_config(dev, ofs, buf.x1[0], 1);
		else
			bus_write_1(r->r_d.res, ofs, buf.x1[0]);
		break;
	case 2:
		if (r->r_type == PROTO_RES_PCICFG)
			pci_write_config(dev, ofs, buf.x2[0], 2);
		else
			bus_write_2(r->r_d.res, ofs, buf.x2[0]);
		break;
	case 4:
		if (r->r_type == PROTO_RES_PCICFG)
			pci_write_config(dev, ofs, buf.x4[0], 4);
		else
			bus_write_4(r->r_d.res, ofs, buf.x4[0]);
		break;
#ifndef __i386__
	case 8:
		if (r->r_type == PROTO_RES_PCICFG)
			return (EINVAL);
		bus_write_8(r->r_d.res, ofs, buf.x8[0]);
		break;
#endif
	default:
		return (EIO);
	}

	return (0);
}

static int
proto_ioctl(struct cdev *cdev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct proto_ioc_region *region;
	struct proto_ioc_busdma *busdma;
	struct proto_res *r;
	struct proto_softc *sc;
	int error;

	sc = cdev->si_drv1;
	r = cdev->si_drv2;

	error = 0;
	switch (cmd) {
	case PROTO_IOC_REGION:
		if (r->r_type == PROTO_RES_BUSDMA) {
			error = EINVAL;
			break;
		}
		region = (struct proto_ioc_region *)data;
		region->size = r->r_size;
		if (r->r_type == PROTO_RES_PCICFG)
			region->address = 0;
		else
			region->address = rman_get_start(r->r_d.res);
		break;
	case PROTO_IOC_BUSDMA:
		if (r->r_type != PROTO_RES_BUSDMA) {
			error = EINVAL;
			break;
		}
		busdma = (struct proto_ioc_busdma *)data;
		error = proto_busdma_ioctl(sc, r->r_d.busdma, busdma, td);
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}

static int
proto_mmap(struct cdev *cdev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct proto_res *r;

	if (offset & PAGE_MASK)
		return (EINVAL);
	if (prot & PROT_EXEC)
		return (EACCES);

	r = cdev->si_drv2;

	switch (r->r_type) {
	case SYS_RES_MEMORY:
		if (offset >= r->r_size)
			return (EINVAL);
		*paddr = rman_get_start(r->r_d.res) + offset;
#ifndef __sparc64__
		*memattr = VM_MEMATTR_UNCACHEABLE;
#endif
		break;
	case PROTO_RES_BUSDMA:
		if (!proto_busdma_mmap_allowed(r->r_d.busdma, offset))
			return (EINVAL);
		*paddr = offset;
		break;
	default:
		return (ENXIO);
	}
	return (0);
}
