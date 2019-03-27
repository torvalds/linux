/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/*
 * SRI-Cambridge BERI soft processor <-> ARM core ring buffer.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/event.h>
#include <sys/selinfo.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#define READ4(_sc, _reg) \
	bus_read_4((_sc)->res[0], _reg)
#define WRITE4(_sc, _reg, _val) \
	bus_write_4((_sc)->res[0], _reg, _val)

#define CDES_INT_EN		(1 << 15)
#define CDES_CAUSE_MASK		0x3
#define CDES_CAUSE_SHIFT	13
#define DEVNAME_MAXLEN		256

typedef struct
{
	uint16_t cdes;
	uint16_t interrupt_level;
	uint16_t in;
	uint16_t out;
} control_reg_t;

struct beri_softc {
	struct resource		*res[3];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	struct cdev		*cdev;
	device_t		dev;
	void			*read_ih;
	void			*write_ih;
	struct selinfo		beri_rsel;
	struct mtx		beri_mtx;
	int			opened;

	char			devname[DEVNAME_MAXLEN];
	int			control_read;
	int			control_write;
	int			data_read;
	int			data_write;
	int			data_size;
};

static struct resource_spec beri_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ -1, 0 }
};

static control_reg_t
get_control_reg(struct beri_softc *sc, int dir)
{
	uint32_t offset;
	uint16_t dst[4];
	control_reg_t c;
	uint16_t *cp;
	int i;

	cp = (uint16_t *)&c;

	offset = dir ? sc->control_write : sc->control_read;
	((uint32_t *)dst)[0] = READ4(sc, offset);
	((uint32_t *)dst)[1] = READ4(sc, offset + 4);

	for (i = 0; i < 4; i++)
		cp[i] = dst[3 - i];

	return (c);
}

static void
set_control_reg(struct beri_softc *sc, int dir, control_reg_t *c)
{
	uint32_t offset;
	uint16_t src[4];
	uint16_t *cp;
	int i;

	cp = (uint16_t *)c;

	for (i = 0; i < 4; i++)
		src[3 - i] = cp[i];

	offset = dir ? sc->control_write : sc->control_read;
	WRITE4(sc, offset + 0, ((uint32_t *)src)[0]);
	WRITE4(sc, offset + 4, ((uint32_t *)src)[1]);
}

static int
get_stock(struct beri_softc *sc, int dir, control_reg_t *c)
{
	uint32_t fill;

	fill = (c->in - c->out + sc->data_size) % sc->data_size;

	if (dir)
		return (sc->data_size - fill - 1);
	else
		return (fill);
}

static void
beri_intr_write(void *arg)
{
	struct beri_softc *sc;
	control_reg_t c;

	sc = arg;

	c = get_control_reg(sc, 1);
	if (c.cdes & CDES_INT_EN) {
		c.cdes &= ~(CDES_INT_EN);
		set_control_reg(sc, 1, &c);
	}

	mtx_lock(&sc->beri_mtx);
	selwakeuppri(&sc->beri_rsel, PZERO + 1);
	KNOTE_LOCKED(&sc->beri_rsel.si_note, 0);
	mtx_unlock(&sc->beri_mtx);
}

static void
beri_intr_read(void *arg)
{
	struct beri_softc *sc;
	control_reg_t c;

	sc = arg;

	c = get_control_reg(sc, 0);
	if (c.cdes & CDES_INT_EN) {
		c.cdes &= ~(CDES_INT_EN);
		set_control_reg(sc, 0, &c);
	}

	mtx_lock(&sc->beri_mtx);
	selwakeuppri(&sc->beri_rsel, PZERO + 1);
	KNOTE_LOCKED(&sc->beri_rsel.si_note, 0);
	mtx_unlock(&sc->beri_mtx);
}

static int
beri_open(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct beri_softc *sc;
	control_reg_t c;

	sc = dev->si_drv1;

	if (sc->opened)
		return (1);

	/* Setup interrupt handlers */
	if (bus_setup_intr(sc->dev, sc->res[1], INTR_TYPE_BIO | INTR_MPSAFE,
		NULL, beri_intr_read, sc, &sc->read_ih)) {
		device_printf(sc->dev, "Unable to setup read intr\n");
		return (1);
	}
	if (bus_setup_intr(sc->dev, sc->res[2], INTR_TYPE_BIO | INTR_MPSAFE,
		NULL, beri_intr_write, sc, &sc->write_ih)) {
		device_printf(sc->dev, "Unable to setup write intr\n");
		return (1);
	}

	sc->opened = 1;

	/* Clear write buffer */
	c = get_control_reg(sc, 1);
	c.in = c.out;
	c.cdes = 0;
	set_control_reg(sc, 1, &c);

	/* Clear read buffer */
	c = get_control_reg(sc, 0);
	c.out = c.in;
	c.cdes = 0;
	set_control_reg(sc, 0, &c);

	return (0);
}

static int
beri_close(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct beri_softc *sc;

	sc = dev->si_drv1;

	if (sc->opened) {
		sc->opened = 0;

		/* Unsetup interrupt handlers */
		bus_teardown_intr(sc->dev, sc->res[1], sc->read_ih);
		bus_teardown_intr(sc->dev, sc->res[2], sc->write_ih);
	}

	return (0);
}

static int
beri_rdwr(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct beri_softc *sc;
	uint32_t offset;
	control_reg_t c;
	uint16_t *ptr;
	uint8_t *dst;
	int stock;
	int dir;
	int amount;
	int count;

	sc = dev->si_drv1;

	dir = uio->uio_rw ? 1 : 0;

	c = get_control_reg(sc, dir);
	stock = get_stock(sc, dir, &c);
	if (stock < uio->uio_resid) {
		device_printf(sc->dev, "Err: no data/space available\n");
		return (1);
	}

	amount = uio->uio_resid;
	ptr = dir ? &c.in : &c.out;
	count = (sc->data_size - *ptr);

	offset = dir ? sc->data_write : sc->data_read;
	dst = (uint8_t *)(sc->bsh + offset);

	if (amount <= count) {
		uiomove(dst + *ptr, amount, uio);
	} else {
		uiomove(dst + *ptr, count, uio);
		uiomove(dst, (amount - count), uio);
	}

	*ptr = (*ptr + amount) % sc->data_size;
	set_control_reg(sc, dir, &c);

	return (0);
}

static int
beri_kqread(struct knote *kn, long hint)
{
	struct beri_softc *sc;
	control_reg_t c;
	int stock;

	sc = kn->kn_hook;

	c = get_control_reg(sc, 0);
	stock = get_stock(sc, 0, &c);
	if (stock) {
		kn->kn_data = stock;
		return (1);
	}

	kn->kn_data = 0;

	/* Wait at least one new byte in buffer */
	c.interrupt_level = 1;

	/* Enable interrupts */
	c.cdes |= (CDES_INT_EN);
	set_control_reg(sc, 0, &c);

	return (0);
}

static int
beri_kqwrite(struct knote *kn, long hint)
{
	struct beri_softc *sc;
	control_reg_t c;
	int stock;

	sc = kn->kn_hook;

	c = get_control_reg(sc, 1);
	stock = get_stock(sc, 1, &c);
	if (stock) {
		kn->kn_data = stock;
		return (1);
	}

	kn->kn_data = 0;

	/* Wait at least one free position in buffer */
	c.interrupt_level = sc->data_size - 2;

	/* Enable interrupts */
	c.cdes |= (CDES_INT_EN);
	set_control_reg(sc, 1, &c);

	return (0);
}

static void
beri_kqdetach(struct knote *kn)
{
	struct beri_softc *sc;

	sc = kn->kn_hook;

	knlist_remove(&sc->beri_rsel.si_note, kn, 0);
}

static struct filterops beri_read_filterops = {
	.f_isfd =       1,
	.f_attach =     NULL,
	.f_detach =     beri_kqdetach,
	.f_event =      beri_kqread,
};

static struct filterops beri_write_filterops = {
	.f_isfd =       1,
	.f_attach =     NULL,
	.f_detach =     beri_kqdetach,
	.f_event =      beri_kqwrite,
};

static int
beri_kqfilter(struct cdev *dev, struct knote *kn)
{
	struct beri_softc *sc;

	sc = dev->si_drv1;

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &beri_read_filterops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &beri_write_filterops;
		break;
	default:
		return(EINVAL);
	}

	kn->kn_hook = sc;
	knlist_add(&sc->beri_rsel.si_note, kn, 0);

	return (0);
}

static struct cdevsw beri_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	beri_open,
	.d_close =	beri_close,
	.d_write =	beri_rdwr,
	.d_read =	beri_rdwr,
	.d_kqfilter =	beri_kqfilter,
	.d_name =	"beri ring buffer",
};

static int
parse_fdt(struct beri_softc *sc)
{
	pcell_t dts_value[0];
	phandle_t node;
	int len;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	/* get device name */
	if (OF_getprop(ofw_bus_get_node(sc->dev), "device_name",
		&sc->devname, sizeof(sc->devname)) <= 0) {
		device_printf(sc->dev, "Can't get device_name\n");
		return (ENXIO);
	}

	if ((len = OF_getproplen(node, "data_size")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "data_size", dts_value, len);
	sc->data_size = dts_value[0];

	if ((len = OF_getproplen(node, "data_read")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "data_read", dts_value, len);
	sc->data_read = dts_value[0];

	if ((len = OF_getproplen(node, "data_write")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "data_write", dts_value, len);
	sc->data_write = dts_value[0];

	if ((len = OF_getproplen(node, "control_read")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "control_read", dts_value, len);
	sc->control_read = dts_value[0];

	if ((len = OF_getproplen(node, "control_write")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "control_write", dts_value, len);
	sc->control_write = dts_value[0];

	return (0);
}

static int
beri_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "sri-cambridge,beri-ring"))
		return (ENXIO);

	device_set_desc(dev, "SRI-Cambridge BERI ring buffer");
	return (BUS_PROBE_DEFAULT);
}

static int
beri_attach(device_t dev)
{
	struct beri_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, beri_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	if (parse_fdt(sc)) {
		device_printf(sc->dev, "Can't get FDT values\n");
		return (ENXIO);
	}

	sc->cdev = make_dev(&beri_cdevsw, 0, UID_ROOT, GID_WHEEL,
	    S_IRWXU, "%s", sc->devname);
	if (sc->cdev == NULL) {
		device_printf(dev, "Failed to create character device.\n");
		return (ENXIO);
	}

	sc->cdev->si_drv1 = sc;

	mtx_init(&sc->beri_mtx, "beri_mtx", NULL, MTX_DEF);
	knlist_init_mtx(&sc->beri_rsel.si_note, &sc->beri_mtx);

	return (0);
}

static device_method_t beri_methods[] = {
	DEVMETHOD(device_probe,		beri_probe),
	DEVMETHOD(device_attach,	beri_attach),
	{ 0, 0 }
};

static driver_t beri_driver = {
	"beri_ring",
	beri_methods,
	sizeof(struct beri_softc),
};

static devclass_t beri_devclass;

DRIVER_MODULE(beri_ring, simplebus, beri_driver, beri_devclass, 0, 0);
