/*-
 * Copyright (c) 2015 Brian Fundakowski Feldman.  All rights reserved.
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

#include "opt_platform.h"
#include "opt_spi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/spigenio.h>
#include <sys/types.h>
 
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#ifdef FDT
#include <dev/ofw/ofw_bus_subr.h>

static struct ofw_compat_data compat_data[] = {
	{"freebsd,spigen", true},
	{NULL,             false}
};

#endif

#include "spibus_if.h"

struct spigen_softc {
	device_t sc_dev;
	struct cdev *sc_cdev;
#ifdef SPIGEN_LEGACY_CDEVNAME
	struct cdev *sc_adev;           /* alias device */
#endif
	struct mtx sc_mtx;
};

struct spigen_mmap {
	vm_object_t bufobj;
	vm_offset_t kvaddr;
	size_t      bufsize;
};

static int
spigen_probe(device_t dev)
{
	int rv;

	/*
	 * By default we only bid to attach if specifically added by our parent
	 * (usually via hint.spigen.#.at=busname).  On FDT systems we bid as the
	 * default driver based on being configured in the FDT data.
	 */
	rv = BUS_PROBE_NOWILDCARD;

#ifdef FDT
	if (ofw_bus_status_okay(dev) &&
	    ofw_bus_search_compatible(dev, compat_data)->ocd_data)
                rv = BUS_PROBE_DEFAULT;
#endif

	device_set_desc(dev, "SPI Generic IO");

	return (rv);
}

static int spigen_open(struct cdev *, int, int, struct thread *);
static int spigen_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);
static int spigen_close(struct cdev *, int, int, struct thread *);
static d_mmap_single_t spigen_mmap_single;

static struct cdevsw spigen_cdevsw = {
	.d_version =     D_VERSION,
	.d_name =        "spigen",
	.d_open =        spigen_open,
	.d_ioctl =       spigen_ioctl,
	.d_mmap_single = spigen_mmap_single,
	.d_close =       spigen_close
};

static int
spigen_attach(device_t dev)
{
	struct spigen_softc *sc;
	const int unit = device_get_unit(dev);
	int cs, res;
	struct make_dev_args mda;

	spibus_get_cs(dev, &cs);
	cs &= ~SPIBUS_CS_HIGH; /* trim 'cs high' bit */

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	make_dev_args_init(&mda);
	mda.mda_flags = MAKEDEV_WAITOK;
	mda.mda_devsw = &spigen_cdevsw;
	mda.mda_cr = NULL;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_OPERATOR;
	mda.mda_mode = 0660;
	mda.mda_unit = unit;
	mda.mda_si_drv1 = dev;

	res = make_dev_s(&mda, &(sc->sc_cdev), "spigen%d.%d",
	    device_get_unit(device_get_parent(dev)), cs);
	if (res) {
		return res;
	}

#ifdef SPIGEN_LEGACY_CDEVNAME
	res = make_dev_alias_p(0, &sc->sc_adev, sc->sc_cdev, "spigen%d", unit);
	if (res) {
		if (sc->sc_cdev) {
			destroy_dev(sc->sc_cdev);
			sc->sc_cdev = NULL;
		}
		return res;
	}
#endif

	return (0);
}

static int 
spigen_open(struct cdev *cdev, int oflags, int devtype, struct thread *td)
{
	device_t dev;
	struct spigen_softc *sc;

	dev = cdev->si_drv1;
	sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mtx);
	device_busy(sc->sc_dev);
	mtx_unlock(&sc->sc_mtx);

	return (0);
}

static int
spigen_transfer(struct cdev *cdev, struct spigen_transfer *st)
{
	struct spi_command transfer = SPI_COMMAND_INITIALIZER;
	device_t dev = cdev->si_drv1;
	int error = 0;

#if 0
	device_printf(dev, "cmd %p %u data %p %u\n", st->st_command.iov_base,
	    st->st_command.iov_len, st->st_data.iov_base, st->st_data.iov_len);
#endif

	if (st->st_command.iov_len == 0)
		return (EINVAL);

	transfer.tx_cmd = transfer.rx_cmd = malloc(st->st_command.iov_len,
	    M_DEVBUF, M_WAITOK);
	if (st->st_data.iov_len > 0) {
		transfer.tx_data = transfer.rx_data = malloc(st->st_data.iov_len,
		    M_DEVBUF, M_WAITOK);
	}
	else
		transfer.tx_data = transfer.rx_data = NULL;

	error = copyin(st->st_command.iov_base, transfer.tx_cmd,
	    transfer.tx_cmd_sz = transfer.rx_cmd_sz = st->st_command.iov_len);	
	if ((error == 0) && (st->st_data.iov_len > 0))
		error = copyin(st->st_data.iov_base, transfer.tx_data,
		    transfer.tx_data_sz = transfer.rx_data_sz =
		                          st->st_data.iov_len);	
	if (error == 0)
		error = SPIBUS_TRANSFER(device_get_parent(dev), dev, &transfer);
	if (error == 0) {
		error = copyout(transfer.rx_cmd, st->st_command.iov_base,
		    transfer.rx_cmd_sz);
		if ((error == 0) && (st->st_data.iov_len > 0))
			error = copyout(transfer.rx_data, st->st_data.iov_base,
			    transfer.rx_data_sz);
	}

	free(transfer.tx_cmd, M_DEVBUF);
	free(transfer.tx_data, M_DEVBUF);
	return (error);
}

static int
spigen_transfer_mmapped(struct cdev *cdev, struct spigen_transfer_mmapped *stm)
{
	struct spi_command transfer = SPI_COMMAND_INITIALIZER;
	device_t dev = cdev->si_drv1;
	struct spigen_mmap *mmap;
	int error;

	if ((error = devfs_get_cdevpriv((void **)&mmap)) != 0)
		return (error);

	if (mmap->bufsize < stm->stm_command_length + stm->stm_data_length)
		return (E2BIG);

	transfer.tx_cmd = transfer.rx_cmd = (void *)((uintptr_t)mmap->kvaddr);
	transfer.tx_cmd_sz = transfer.rx_cmd_sz = stm->stm_command_length;
	transfer.tx_data = transfer.rx_data =
	    (void *)((uintptr_t)mmap->kvaddr + stm->stm_command_length);
	transfer.tx_data_sz = transfer.rx_data_sz = stm->stm_data_length;
	error = SPIBUS_TRANSFER(device_get_parent(dev), dev, &transfer);

	return (error);
}

static int
spigen_ioctl(struct cdev *cdev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	device_t dev = cdev->si_drv1;
	int error;

	switch (cmd) {
	case SPIGENIOC_TRANSFER:
		error = spigen_transfer(cdev, (struct spigen_transfer *)data);
		break;
	case SPIGENIOC_TRANSFER_MMAPPED:
		error = spigen_transfer_mmapped(cdev, (struct spigen_transfer_mmapped *)data);
		break;
	case SPIGENIOC_GET_CLOCK_SPEED:
		error = spibus_get_clock(dev, (uint32_t *)data);
		break;
	case SPIGENIOC_SET_CLOCK_SPEED:
		error = spibus_set_clock(dev, *(uint32_t *)data);
		break;
	case SPIGENIOC_GET_SPI_MODE:
		error = spibus_get_mode(dev, (uint32_t *)data);
		break;
	case SPIGENIOC_SET_SPI_MODE:
		error = spibus_set_mode(dev, *(uint32_t *)data);
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

static void
spigen_mmap_cleanup(void *arg)
{
	struct spigen_mmap *mmap = arg;

	if (mmap->kvaddr != 0)
		pmap_qremove(mmap->kvaddr, mmap->bufsize / PAGE_SIZE);
	if (mmap->bufobj != NULL)
		vm_object_deallocate(mmap->bufobj);
	free(mmap, M_DEVBUF);
}

static int
spigen_mmap_single(struct cdev *cdev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **object, int nprot)
{
	struct spigen_mmap *mmap;
	vm_page_t *m;
	size_t n, pages;
	int error;

	if (size == 0 ||
	    (nprot & (PROT_EXEC | PROT_READ | PROT_WRITE))
	    != (PROT_READ | PROT_WRITE))
		return (EINVAL);
	size = roundup2(size, PAGE_SIZE);
	pages = size / PAGE_SIZE;

	if (devfs_get_cdevpriv((void **)&mmap) == 0)
		return (EBUSY);

	mmap = malloc(sizeof(*mmap), M_DEVBUF, M_ZERO | M_WAITOK);
	if ((mmap->kvaddr = kva_alloc(size)) == 0) {
		spigen_mmap_cleanup(mmap);
		return (ENOMEM);
	}
	mmap->bufsize = size;
	mmap->bufobj = vm_pager_allocate(OBJT_PHYS, 0, size, nprot, 0,
	    curthread->td_ucred);

	m = malloc(sizeof(*m) * pages, M_TEMP, M_WAITOK);
	VM_OBJECT_WLOCK(mmap->bufobj);
	vm_object_reference_locked(mmap->bufobj); // kernel and userland both
	for (n = 0; n < pages; n++) {
		m[n] = vm_page_grab(mmap->bufobj, n,
		    VM_ALLOC_NOBUSY | VM_ALLOC_ZERO | VM_ALLOC_WIRED);
		m[n]->valid = VM_PAGE_BITS_ALL;
	}
	VM_OBJECT_WUNLOCK(mmap->bufobj);
	pmap_qenter(mmap->kvaddr, m, pages);
	free(m, M_TEMP);

	if ((error = devfs_set_cdevpriv(mmap, spigen_mmap_cleanup)) != 0) {
		/* Two threads were racing through this code; we lost. */
		spigen_mmap_cleanup(mmap);
		return (error);
	}
	*offset = 0;
	*object = mmap->bufobj;

	return (0);
}

static int 
spigen_close(struct cdev *cdev, int fflag, int devtype, struct thread *td)
{
	device_t dev = cdev->si_drv1;
	struct spigen_softc *sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mtx);
	device_unbusy(sc->sc_dev);
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

static int
spigen_detach(device_t dev)
{
	struct spigen_softc *sc;

	sc = device_get_softc(dev);

#ifdef SPIGEN_LEGACY_CDEVNAME
	if (sc->sc_adev)
		destroy_dev(sc->sc_adev);
#endif

	if (sc->sc_cdev)
		destroy_dev(sc->sc_cdev);
	
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static devclass_t spigen_devclass;

static device_method_t spigen_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		spigen_probe),
	DEVMETHOD(device_attach,	spigen_attach),
	DEVMETHOD(device_detach,	spigen_detach),

	{ 0, 0 }
};

static driver_t spigen_driver = {
	"spigen",
	spigen_methods,
	sizeof(struct spigen_softc),
};

DRIVER_MODULE(spigen, spibus, spigen_driver, spigen_devclass, 0, 0);
MODULE_DEPEND(spigen, spibus, 1, 1, 1);
#ifdef FDT
SIMPLEBUS_PNP_INFO(compat_data);
#endif
