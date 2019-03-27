/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003-2004 Poul-Henning Kamp
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 * This is a device driver or the Adlink 9812 and 9810 ADC cards, mainly
 * intended to support Software Defined Radio reception of timesignals
 * in the VLF band.  See http://phk.freebsd.dk/loran-c
 *
 * The driver is configured with ioctls which define a ringbuffer with
 * a given number of chunks in it.  The a control structure and the
 * ringbuffer can then be mmap(2)'ed into userland and the application
 * can operate on the data directly.
 *
 * Tested with 10MHz external clock, divisor of 2 (ie: 5MHz sampling),
 * One channel active (ie: 2 bytes per sample = 10MB/sec) on a 660MHz
 * Celeron PC.
 *
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/kthread.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <pci_if.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#endif /* _KERNEL */

#include <sys/ioccom.h>

#define ADLINK_SETDIVISOR	_IOWR('A', 255, u_int)	/* divisor */
#define ADLINK_SETCHUNKSIZE	_IOWR('A', 254, u_int)	/* bytes */
#define ADLINK_SETRINGSIZE	_IOWR('A', 253, u_int)	/* bytes */
#define ADLINK_START		_IOWR('A', 252, u_int)	/* dummy */
#define ADLINK_STOP		_IOWR('A', 251, u_int)	/* dummy */
#define ADLINK_RESET		_IOWR('A', 250, u_int)	/* dummy */

struct page0 {
	u_int			version;
	int			state;
#	  define STATE_RESET	-1
#	  define STATE_RUN	0
	u_int			divisor;	/* int */
	u_int			chunksize;	/* bytes */
	u_int			ringsize;	/* chunks */
	u_int			o_sample;	/*
						 * offset of ring generation
						 * array
						 */
	u_int			o_ring;		/* offset of ring */
};

#define PAGE0VERSION	20050219

#ifdef _KERNEL

struct pgstat {
	uint64_t		*sample;
	vm_paddr_t		phys;
	void			*virt;
	struct pgstat		*next;
};

struct softc {
	device_t		device;
	void			*intrhand;
	struct resource		*res[3];
	struct cdev		*dev;
	off_t			mapvir;
	int			error;
	struct page0		*p0;
	u_int			nchunks;
	struct pgstat		*chunks;
	struct pgstat		*next;
	uint64_t		sample;
};

static d_ioctl_t adlink_ioctl;
static d_mmap_t	adlink_mmap;
static int adlink_intr(void *arg);

static struct cdevsw adlink_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_ioctl =	adlink_ioctl,
	.d_mmap =	adlink_mmap,
	.d_name =	"adlink",
};

static int
adlink_intr(void *arg)
{
	struct softc *sc;
	struct pgstat *pg;
	uint32_t u;

	sc = arg;
	u = bus_read_4(sc->res[0], 0x38);
	if (!(u & 0x00800000))
		return (FILTER_STRAY);
	bus_write_4(sc->res[0], 0x38, u | 0x003f4000);

	sc->sample += sc->p0->chunksize / 2;
	pg = sc->next;
	*(pg->sample) = sc->sample;

	u = bus_read_4(sc->res[1], 0x18);
	if (u & 1)
		sc->p0->state = EIO;

	if (sc->p0->state != STATE_RUN) {
		printf("adlink: stopping %d\n", sc->p0->state);
		return (FILTER_STRAY);
	}

	pg = pg->next;
	sc->next = pg;
	*(pg->sample) = 0;
	bus_write_4(sc->res[0], 0x24, pg->phys);
	bus_write_4(sc->res[0], 0x28, sc->p0->chunksize);
	wakeup(sc);
	return (FILTER_HANDLED);
}

static int
adlink_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct softc *sc;
	vm_offset_t o;
	int i;
	struct pgstat *pg;

	sc = dev->si_drv1;
	if (nprot != VM_PROT_READ)
		return (-1);
	if (offset == 0) {
		*paddr = vtophys(sc->p0);
		return (0);
	}
	o = PAGE_SIZE;
	pg = sc->chunks;
	for (i = 0; i < sc->nchunks; i++, pg++) {
		if (offset - o >= sc->p0->chunksize) {
			o += sc->p0->chunksize;
			continue;
		}
		*paddr = pg->phys + (offset - o);
		return (0);
	}
	return (-1);
}

static int
adlink_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct softc *sc;
	int i, error;
	u_int u;
	struct pgstat *pg;
	uint64_t *sample;
	
	sc = dev->si_drv1;
	u = *(u_int*)data;
	error = 0;
	switch (cmd) {
	case ADLINK_SETDIVISOR:
		if (sc->p0->state == STATE_RUN)
			return (EBUSY);
		if (u & 1)
			return (EINVAL);
		sc->p0->divisor = u;
		break;
	case ADLINK_SETCHUNKSIZE:
		if (sc->p0->state != STATE_RESET)
			return (EBUSY);
		if (u % PAGE_SIZE)
			return (EINVAL);
		if (sc->p0->ringsize != 0 && sc->p0->ringsize % u)
			return (EINVAL);
		sc->p0->chunksize = u;
		break;
	case ADLINK_SETRINGSIZE:
		if (sc->p0->state != STATE_RESET)
			return (EBUSY);
		if (u % PAGE_SIZE)
			return (EINVAL);
		if (sc->p0->chunksize != 0 && u % sc->p0->chunksize)
			return (EINVAL);
		sc->p0->ringsize = u;
		break;
	case ADLINK_START:
		if (sc->p0->state == STATE_RUN)
			return (EBUSY);
		if (sc->p0->state == STATE_RESET) {
			
			if (sc->p0->chunksize == 0)
				sc->p0->chunksize = 4 * PAGE_SIZE;
			if (sc->p0->ringsize == 0)
				sc->p0->ringsize = 16 * sc->p0->chunksize;
			if (sc->p0->divisor == 0)
				sc->p0->divisor = 4;

			sc->nchunks = sc->p0->ringsize / sc->p0->chunksize;
			if (sc->nchunks * sizeof (*pg->sample) +
			    sizeof *sc->p0 > PAGE_SIZE)
				return (EINVAL);
			sc->p0->o_ring = PAGE_SIZE;
			sample = (uint64_t *)(sc->p0 + 1);
			sc->p0->o_sample =
			    (uintptr_t)sample - (uintptr_t)(sc->p0);
			pg = malloc(sizeof *pg * sc->nchunks,
			    M_DEVBUF, M_WAITOK | M_ZERO);
			sc->chunks = pg;
			for (i = 0; i < sc->nchunks; i++) {
				pg->sample = sample;
				*pg->sample = 0;
				sample++;
				pg->virt = contigmalloc(sc->p0->chunksize,
				    M_DEVBUF, M_WAITOK,
				    0ul, 0xfffffffful,
				    PAGE_SIZE, 0);
				pg->phys = vtophys(pg->virt);
				if (i == sc->nchunks - 1)
					pg->next = sc->chunks;
				else
					pg->next = pg + 1;
				pg++;
			}
			sc->next = sc->chunks;
		}

		/* Reset generation numbers */
		pg = sc->chunks;
		for (i = 0; i < sc->nchunks; i++) {
			*pg->sample = 0;
			pg++;
		}

		/* Enable interrupts on write complete */
		bus_write_4(sc->res[0], 0x38, 0x00004000);

		/* Sample CH0 only */
		bus_write_4(sc->res[1], 0x00, 1);

		/* Divide clock by four */
		bus_write_4(sc->res[1], 0x04, sc->p0->divisor);

		/* Software trigger mode: software */
		bus_write_4(sc->res[1], 0x08, 0);

		/* Trigger level zero */
		bus_write_4(sc->res[1], 0x0c, 0);

		/* Trigger source CH0 (not used) */
		bus_write_4(sc->res[1], 0x10, 0);

		/* Fifo control/status: flush */
		bus_write_4(sc->res[1], 0x18, 3);

		/* Clock source: external sine */
		bus_write_4(sc->res[1], 0x20, 2);

		/* Chipmunks are go! */
		sc->p0->state = STATE_RUN;

		/* Set up Write DMA */
		pg = sc->next = sc->chunks;
		*(pg->sample) = 0;
		bus_write_4(sc->res[0], 0x24, pg->phys);
		bus_write_4(sc->res[0], 0x28, sc->p0->chunksize);
		u = bus_read_4(sc->res[0], 0x3c);
		bus_write_4(sc->res[0], 0x3c, u | 0x00000600);

		/* Acquisition Enable Register: go! */
		bus_write_4(sc->res[1], 0x1c, 1);

		break;
	case ADLINK_STOP:
		if (sc->p0->state == STATE_RESET)
			break;
		sc->p0->state = EINTR;	
		while (*(sc->next->sample) == 0)
			tsleep(sc, PUSER | PCATCH, "adstop", 1);
		break;
#ifdef notyet
	/*
	 * I'm not sure we can actually do this.  How do we revoke
	 * the mmap'ed pages from any process having them mmapped ?
	 */
	case ADLINK_RESET:
		if (sc->p0->state == STATE_RESET)
			break;
		sc->p0->state = EINTR;	
		while (*(sc->next->samp) == 0)
			tsleep(sc, PUSER | PCATCH, "adreset", 1);
		/* deallocate ring buffer */
		break;
#endif
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}

static devclass_t adlink_devclass;

struct pci_id
{
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
} adlink_id[] = {
	{ .vendor = 0x10e8, .device = 0x80da,
	  .desc ="Adlink PCI-9812 4 ch 12 bit 20 msps" }
};

static int
adlink_probe(device_t self)
{
	int i;
	uint16_t vendor, device;

	vendor = pci_get_vendor(self);
	device = pci_get_device(self);
	for (i = 0; i < nitems(adlink_id); i++) {
		if (adlink_id[i].vendor == vendor &&
		    adlink_id[i].device == device) {
			device_set_desc(self, adlink_id[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static struct resource_spec adlink_res_spec[] = {
	{ SYS_RES_IOPORT,	PCIR_BAR(0),	RF_ACTIVE},
	{ SYS_RES_IOPORT,	PCIR_BAR(1),	RF_ACTIVE},
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE},
	{ -1, 0, 0 }
};

static int
adlink_attach(device_t self)
{
	struct softc *sc;
	int i, error;

	sc = device_get_softc(self);
	bzero(sc, sizeof *sc);
	sc->device = self;

	error = bus_alloc_resources(self, adlink_res_spec, sc->res);
	if (error)
		return (error);

	i = bus_setup_intr(self, sc->res[2], INTR_TYPE_MISC,
	    adlink_intr, NULL, sc, &sc->intrhand);
	if (i) {
		printf("adlink: Couldn't get FAST intr\n");
		i = bus_setup_intr(self, sc->res[2],
		    INTR_MPSAFE | INTR_TYPE_MISC,
		    NULL, (driver_intr_t *)adlink_intr, sc, &sc->intrhand);
	}

	if (i) {
		bus_release_resources(self, adlink_res_spec, sc->res);
		return (ENODEV);
	}

	sc->p0 = malloc(PAGE_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->p0->version = PAGE0VERSION;
	sc->p0->state = STATE_RESET;

	sc->dev = make_dev(&adlink_cdevsw, device_get_unit(self),
	    UID_ROOT, GID_WHEEL, 0444, "adlink%d", device_get_unit(self));
	sc->dev->si_drv1 = sc;

	return (0);
}

static device_method_t adlink_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		adlink_probe),
	DEVMETHOD(device_attach,	adlink_attach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	DEVMETHOD_END
};
 
static driver_t adlink_driver = {
	"adlink",
	adlink_methods,
	sizeof(struct softc)
};

DRIVER_MODULE(adlink, pci, adlink_driver, adlink_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, adlink, adlink_id,
    nitems(adlink_id));
#endif /* _KERNEL */
