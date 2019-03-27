/*-
 * Copyright (C) 2016 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/conf.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/broadcom/bcm2835/bcm2835_mbox.h>
#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>
#include <arm/broadcom/bcm2835/bcm2835_vcbus.h>

#include "mbox_if.h"

#ifdef DEBUG
#define DPRINTF(fmt, ...) do {			\
	printf("%s:%u: ", __func__, __LINE__);	\
	printf(fmt, ##__VA_ARGS__);		\
} while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define	FT5406_LOCK(_sc)		\
	mtx_lock(&(_sc)->sc_mtx)
#define	FT5406_UNLOCK(_sc)		\
	mtx_unlock(&(_sc)->sc_mtx)
#define	FT5406_LOCK_INIT(_sc)	\
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), \
	    "ft5406", MTX_DEF)
#define	FT5406_LOCK_DESTROY(_sc)	\
	mtx_destroy(&_sc->sc_mtx);
#define	FT5406_LOCK_ASSERT(_sc)	\
	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	FT5406_DEVICE_MODE	0
#define	FT5406_GESTURE_ID	1
#define	FT5406_NUM_POINTS	2
#define	FT5406_POINT_XH(n)	(0 + 3 + (n)*6)
#define	FT5406_POINT_XL(n)	(1 + 3 + (n)*6)
#define	FT5406_POINT_YH(n)	(2 + 3 + (n)*6)
#define	FT5406_POINT_YL(n)	(3 + 3 + (n)*6)
#define	FT5406_WINDOW_SIZE	64

#define	GET_NUM_POINTS(buf)	(buf[FT5406_NUM_POINTS])
#define	GET_X(buf, n)		(((buf[FT5406_POINT_XH(n)] & 0xf) << 8) | \
				    (buf[FT5406_POINT_XL(n)]))
#define	GET_Y(buf, n)		(((buf[FT5406_POINT_YH(n)] & 0xf) << 8) | \
				    (buf[FT5406_POINT_YL(n)]))
#define	GET_TOUCH_ID(buf, n)	((buf[FT5406_POINT_YH(n)] >> 4) & 0xf)

#define	NO_POINTS		99
#define	SCREEN_WIDTH		800
#define	SCREEN_HEIGHT		480
#define	SCREEN_WIDTH_MM		155
#define	SCREEN_HEIGHT_MM	86
#define	SCREEN_RES_X	(SCREEN_WIDTH / SCREEN_WIDTH_MM)
#define	SCREEN_RES_Y	(SCREEN_HEIGHT / SCREEN_HEIGHT_MM)
#define	MAX_TOUCH_ID	(10 - 1)

struct ft5406ts_softc {
	device_t		sc_dev;
	struct mtx		sc_mtx;
	int			sc_tick;
	struct callout		sc_callout;

	/* mbox buffer (mapped to KVA) */
	uint8_t			*touch_buf;

	/* initial hook for waiting mbox intr */
	struct intr_config_hook	sc_init_hook;

	struct evdev_dev	*sc_evdev;

	uint8_t			sc_window[FT5406_WINDOW_SIZE];
};

static evdev_open_t ft5406ts_ev_open;
static evdev_close_t ft5406ts_ev_close;

static const struct evdev_methods ft5406ts_evdev_methods = {
	.ev_open = &ft5406ts_ev_open,
	.ev_close = &ft5406ts_ev_close,
};

static void
ft5406ts_callout(void *data)
{
	struct ft5406ts_softc *sc = (struct ft5406ts_softc *)data;
	int points;
	int id, i, x, y;

	FT5406_LOCK_ASSERT(sc);

	memcpy(sc->sc_window, sc->touch_buf, FT5406_WINDOW_SIZE);
	sc->touch_buf[FT5406_NUM_POINTS] = NO_POINTS;

	points = GET_NUM_POINTS(sc->sc_window);
	/*
	 * No update from VC - do nothing.
	 */
	if (points == NO_POINTS)
		goto out;

	for (i = 0; i < points; i++) {
		id = GET_TOUCH_ID(sc->sc_window, i);
		x = GET_X(sc->sc_window, i);
		y = GET_Y(sc->sc_window, i);

		if (id > MAX_TOUCH_ID) {
			device_printf(sc->sc_dev, "bad touch id: %d", id);
			continue;
		}
		evdev_push_event(sc->sc_evdev, EV_ABS, ABS_MT_SLOT, id);
		evdev_push_event(sc->sc_evdev, EV_ABS, ABS_MT_TRACKING_ID, id);
		evdev_push_event(sc->sc_evdev, EV_ABS, ABS_MT_POSITION_X, x);
		evdev_push_event(sc->sc_evdev, EV_ABS, ABS_MT_POSITION_Y, y);
	}
	evdev_sync(sc->sc_evdev);
out:
	callout_reset(&sc->sc_callout, sc->sc_tick, ft5406ts_callout, sc);
}

static int
ft5406ts_ev_close(struct evdev_dev *evdev)
{
	struct ft5406ts_softc *sc = evdev_get_softc(evdev);

	FT5406_LOCK_ASSERT(sc);

	callout_stop(&sc->sc_callout);

	return (0);
}

static int
ft5406ts_ev_open(struct evdev_dev *evdev)
{
	struct ft5406ts_softc *sc = evdev_get_softc(evdev);

	FT5406_LOCK_ASSERT(sc);

	callout_reset(&sc->sc_callout, sc->sc_tick, ft5406ts_callout, sc);

	return (0);
}

static void
ft5406ts_init(void *arg)
{
	struct ft5406ts_softc *sc = arg;
	struct bcm2835_mbox_tag_touchbuf msg;
	uint32_t touchbuf;
	int err;

	/* release this hook (continue boot) */
	config_intrhook_disestablish(&sc->sc_init_hook);

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_TOUCHBUF;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body);
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->sc_dev, "failed to get touchbuf address\n");
		return;
	}

	if (msg.body.resp.address == 0) {
		device_printf(sc->sc_dev, "touchscreen not detected\n");
		return;
	}

	touchbuf = VCBUS_TO_PHYS(msg.body.resp.address);
	sc->touch_buf = (uint8_t*)pmap_mapdev(touchbuf, FT5406_WINDOW_SIZE);

	/* 60Hz */
	sc->sc_tick = hz * 17 / 1000;
	if (sc->sc_tick == 0)
		sc->sc_tick = 1;

	sc->sc_evdev = evdev_alloc();
	evdev_set_name(sc->sc_evdev, device_get_desc(sc->sc_dev));
	evdev_set_phys(sc->sc_evdev, device_get_nameunit(sc->sc_dev));
	evdev_set_id(sc->sc_evdev, BUS_HOST, 0, 0, 0);
	evdev_set_methods(sc->sc_evdev, sc, &ft5406ts_evdev_methods);
	evdev_set_flag(sc->sc_evdev, EVDEV_FLAG_MT_STCOMPAT);
	evdev_set_flag(sc->sc_evdev, EVDEV_FLAG_MT_AUTOREL);
	evdev_support_prop(sc->sc_evdev, INPUT_PROP_DIRECT);
	evdev_support_event(sc->sc_evdev, EV_SYN);
	evdev_support_event(sc->sc_evdev, EV_ABS);

	evdev_support_abs(sc->sc_evdev, ABS_MT_SLOT, 0, 0,
	    MAX_TOUCH_ID, 0, 0, 0);
	evdev_support_abs(sc->sc_evdev, ABS_MT_TRACKING_ID, 0, -1,
	    MAX_TOUCH_ID, 0, 0, 0);
	evdev_support_abs(sc->sc_evdev, ABS_MT_POSITION_X, 0, 0,
	    SCREEN_WIDTH, 0, 0, SCREEN_RES_X);
	evdev_support_abs(sc->sc_evdev, ABS_MT_POSITION_Y, 0, 0,
	    SCREEN_HEIGHT, 0, 0, SCREEN_RES_Y);

	err = evdev_register_mtx(sc->sc_evdev, &sc->sc_mtx);
	if (err) {
		evdev_free(sc->sc_evdev);
		sc->sc_evdev = NULL;	/* Avoid double free */
		return;
	}

	sc->touch_buf[FT5406_NUM_POINTS] = NO_POINTS;
	callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
}

static int
ft5406ts_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "rpi,rpi-ft5406"))
		return (ENXIO);

	device_set_desc(dev, "FT5406 touchscreen (VC memory interface)");

	return (BUS_PROBE_DEFAULT);
}

static int
ft5406ts_attach(device_t dev)
{
	struct ft5406ts_softc *sc;

	/* set self dev */
	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	/* register callback for using mbox when interrupts are enabled */
	sc->sc_init_hook.ich_func = ft5406ts_init;
	sc->sc_init_hook.ich_arg = sc;

	FT5406_LOCK_INIT(sc);

	if (config_intrhook_establish(&sc->sc_init_hook) != 0) {
		device_printf(dev, "config_intrhook_establish failed\n");
		FT5406_LOCK_DESTROY(sc);
		return (ENOMEM);
	}

	return (0);
}

static int
ft5406ts_detach(device_t dev)
{
	struct ft5406ts_softc *sc;

	sc = device_get_softc(dev);

	evdev_free(sc->sc_evdev);

	FT5406_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t ft5406ts_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ft5406ts_probe),
	DEVMETHOD(device_attach,	ft5406ts_attach),
	DEVMETHOD(device_detach,	ft5406ts_detach),

	DEVMETHOD_END
};

static devclass_t ft5406ts_devclass;
static driver_t ft5406ts_driver = {
	"ft5406ts",
	ft5406ts_methods,
	sizeof(struct ft5406ts_softc),
};

DRIVER_MODULE(ft5406ts, ofwbus, ft5406ts_driver, ft5406ts_devclass, 0, 0);
MODULE_DEPEND(ft5406ts, evdev, 1, 1, 1);
