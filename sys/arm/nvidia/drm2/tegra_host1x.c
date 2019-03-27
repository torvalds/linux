/*-
 * Copyright (c) 2015 Michal Meloun
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
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>

#include <sys/module.h>
#include <sys/resource.h>
#include <sys/sx.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc_helper.h>
#include <dev/drm2/drm_fb_helper.h>
#include <dev/fdt/simplebus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/drm2/tegra_drm.h>

#include "fb_if.h"
#include "tegra_drm_if.h"

#define	WR4(_sc, _r, _v)	bus_rite_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))

#define	LOCK(_sc)		sx_xlock(&(_sc)->lock)
#define	UNLOCK(_sc)		sx_xunlock(&(_sc)->lock)
#define	SLEEP(_sc, timeout)	sx_sleep(sc, &sc->lock, 0, "host1x", timeout);
#define	LOCK_INIT(_sc)		sx_init(&_sc->lock, "host1x")
#define	LOCK_DESTROY(_sc)	sx_destroy(&_sc->lock)
#define	ASSERT_LOCKED(_sc)	sx_assert(&_sc->lock, SA_LOCKED)
#define	ASSERT_UNLOCKED(_sc)	sx_assert(&_sc->lock, SA_UNLOCKED)

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-host1x",	1},
	{NULL,				0}
};

#define DRIVER_NAME "tegra"
#define DRIVER_DESC "NVIDIA Tegra TK1"
#define DRIVER_DATE "20151101"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

struct client_info;
TAILQ_HEAD(client_list, client_info);
typedef struct client_list client_list_t;

struct client_info {
	TAILQ_ENTRY(client_info) list_e;
	device_t client;
	int 	activated;
};

struct host1x_softc {
	struct simplebus_softc	simplebus_sc;	/* must be first */
	device_t		dev;
	struct sx		lock;
	int 			attach_done;

	struct resource		*mem_res;
	struct resource		*syncpt_irq_res;
	void			*syncpt_irq_h;
	struct resource		*gen_irq_res;
	void			*gen_irq_h;

	clk_t			clk;
	hwreset_t			reset;
	struct intr_config_hook	irq_hook;

	int			drm_inited;
	client_list_t		clients;

	struct tegra_drm 	*tegra_drm;
};


static void
host1x_output_poll_changed(struct drm_device *drm_dev)
{
	struct tegra_drm *drm;

	drm = container_of(drm_dev, struct tegra_drm, drm_dev);
	if (drm->fb != NULL)
		drm_fb_helper_hotplug_event(&drm->fb->fb_helper);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = tegra_drm_fb_create,
	.output_poll_changed = host1x_output_poll_changed,
};


static int
host1x_drm_init(struct host1x_softc *sc)
{
	struct client_info *entry;
	int rv;

	LOCK(sc);

	TAILQ_FOREACH(entry, &sc->clients, list_e) {
		if (entry->activated)
			continue;
		rv = TEGRA_DRM_INIT_CLIENT(entry->client, sc->dev,
		    sc->tegra_drm);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot init DRM client %s: %d\n",
			    device_get_name(entry->client), rv);
			return (rv);
		}
		entry->activated = 1;
	}
	UNLOCK(sc);

	return (0);
}

static int
host1x_drm_exit(struct host1x_softc *sc)
{
	struct client_info *entry;
	int rv;
#ifdef FREEBSD_NOTYET
	struct drm_device *dev, *tmp;
#endif
	LOCK(sc);
	if (!sc->drm_inited) {
		UNLOCK(sc);
		return (0);
	}
	TAILQ_FOREACH_REVERSE(entry, &sc->clients, client_list, list_e) {
		if (!entry->activated)
			continue;
		rv = TEGRA_DRM_EXIT_CLIENT(entry->client, sc->dev,
		    sc->tegra_drm);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot exit DRM client %s: %d\n",
			    device_get_name(entry->client), rv);
		}
		entry->activated = 0;
	}

#ifdef FREEBSD_NOTYET
	list_for_each_entry_safe(dev, tmp, &driver->device_list, driver_item)
		drm_put_dev(dev);
#endif
	sc->drm_inited = 0;
	UNLOCK(sc);

	return (0);
}

static int
host1x_drm_load(struct drm_device *drm_dev, unsigned long flags)
{
	struct host1x_softc *sc;
	int rv;

	sc = device_get_softc(drm_dev->dev);

	drm_mode_config_init(drm_dev);
	drm_dev->mode_config.min_width = 32;
	drm_dev->mode_config.min_height = 32;
	drm_dev->mode_config.max_width = 4096;
	drm_dev->mode_config.max_height = 4096;
	drm_dev->mode_config.funcs = &mode_config_funcs;

	rv = host1x_drm_init(sc);
	if (rv != 0)
		goto fail_host1x;

	drm_dev->irq_enabled = true;
	drm_dev->max_vblank_count = 0xffffffff;
	drm_dev->vblank_disable_allowed = true;

	rv = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (rv != 0)
		goto fail_vblank;

	drm_mode_config_reset(drm_dev);

	rv = tegra_drm_fb_init(drm_dev);
	if (rv != 0)
		goto fail_fb;
	drm_kms_helper_poll_init(drm_dev);

	return (0);

fail_fb:
	tegra_drm_fb_destroy(drm_dev);
	drm_vblank_cleanup(drm_dev);
fail_vblank:
	host1x_drm_exit(sc);
fail_host1x:
	drm_mode_config_cleanup(drm_dev);

	return (rv);
}

static int
host1x_drm_unload(struct drm_device *drm_dev)
{
	struct host1x_softc *sc;
	int rv;

	sc = device_get_softc(drm_dev->dev);

	drm_kms_helper_poll_fini(drm_dev);
	tegra_drm_fb_destroy(drm_dev);
	drm_mode_config_cleanup(drm_dev);

	rv = host1x_drm_exit(sc);
	if (rv < 0)
		return (rv);
	return (0);
}

static int
host1x_drm_open(struct drm_device *drm_dev, struct drm_file *filp)
{

	return (0);
}

static void
tegra_drm_preclose(struct drm_device *drm, struct drm_file *file)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &drm->mode_config.crtc_list, head)
		tegra_dc_cancel_page_flip(crtc, file);
}

static void
host1x_drm_lastclose(struct drm_device *drm_dev)
{

	struct tegra_drm *drm;

	drm = container_of(drm_dev, struct tegra_drm, drm_dev);
	if (drm->fb  != NULL)
		drm_fb_helper_restore_fbdev_mode(&drm->fb->fb_helper);
}

static int
host1x_drm_enable_vblank(struct drm_device *drm_dev, int pipe)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head) {
		if (pipe == tegra_dc_get_pipe(crtc)) {
			tegra_dc_enable_vblank(crtc);
			return (0);
		}
	}
	return (-ENODEV);
}

static void
host1x_drm_disable_vblank(struct drm_device *drm_dev, int pipe)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head) {
		if (pipe == tegra_dc_get_pipe(crtc)) {
			tegra_dc_disable_vblank(crtc);
			return;
		}
	}
}


static struct drm_ioctl_desc host1x_drm_ioctls[] = {
};


struct drm_driver tegra_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load = host1x_drm_load,
	.unload = host1x_drm_unload,
	.open = host1x_drm_open,
	.preclose = tegra_drm_preclose,
	.lastclose = host1x_drm_lastclose,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = host1x_drm_enable_vblank,
	.disable_vblank = host1x_drm_disable_vblank,

	/* Fields filled by tegra_bo_driver_register()
	.gem_free_object
	.gem_pager_ops
	.dumb_create
	.dumb_map_offset
	.dumb_destroy
	*/
	.ioctls = host1x_drm_ioctls,
	.num_ioctls = nitems(host1x_drm_ioctls),

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

/*
 * ----------------- Device methods -------------------------
 */
static void
host1x_irq_hook(void *arg)
{
	struct host1x_softc *sc;
	int rv;

	sc = arg;
	config_intrhook_disestablish(&sc->irq_hook);

	tegra_bo_driver_register(&tegra_drm_driver);
	rv = drm_get_platform_dev(sc->dev, &sc->tegra_drm->drm_dev,
	    &tegra_drm_driver);
	if (rv != 0) {
		device_printf(sc->dev, "drm_get_platform_dev(): %d\n", rv);
		return;
	}

	sc->drm_inited = 1;
}

static struct fb_info *
host1x_fb_helper_getinfo(device_t dev)
{
	struct host1x_softc *sc;

	sc = device_get_softc(dev);
	if (sc->tegra_drm == NULL)
		return (NULL);
	return (tegra_drm_fb_getinfo(&sc->tegra_drm->drm_dev));
}

static int
host1x_register_client(device_t dev, device_t client)
{
	struct host1x_softc *sc;
	struct client_info *entry;

	sc = device_get_softc(dev);

	entry = malloc(sizeof(struct client_info), M_DEVBUF, M_WAITOK | M_ZERO);
	entry->client = client;
	entry->activated = 0;

	LOCK(sc);
	TAILQ_INSERT_TAIL(&sc->clients, entry, list_e);
	UNLOCK(sc);

	return (0);
}

static int
host1x_deregister_client(device_t dev, device_t client)
{
	struct host1x_softc *sc;
	struct client_info *entry;

	sc = device_get_softc(dev);

	LOCK(sc);
	TAILQ_FOREACH(entry, &sc->clients, list_e) {
		if (entry->client == client) {
			if (entry->activated)
				panic("Tegra DRM: Attempt to deregister "
				    "activated client");
			TAILQ_REMOVE(&sc->clients, entry, list_e);
			free(entry, M_DEVBUF);
			UNLOCK(sc);
			return (0);
		}
	}
	UNLOCK(sc);

	return (0);
}

static void
host1x_gen_intr(void *arg)
{
	struct host1x_softc *sc;

	sc = (struct host1x_softc *)arg;
	LOCK(sc);
	UNLOCK(sc);
}

static void
host1x_syncpt_intr(void *arg)
{
	struct host1x_softc *sc;

	sc = (struct host1x_softc *)arg;
	LOCK(sc);
	UNLOCK(sc);
}

static void
host1x_new_pass(device_t dev)
{
	struct host1x_softc *sc;
	int rv, rid;
	phandle_t node;

	/*
	 * We attach during BUS_PASS_BUS (because we must overcome simplebus),
	 * but some of our FDT resources are not ready until BUS_PASS_DEFAULT
	 */
	sc = device_get_softc(dev);
	if (sc->attach_done || bus_current_pass < BUS_PASS_DEFAULT) {
		bus_generic_new_pass(dev);
		return;
	}

	sc->attach_done = 1;
	node = ofw_bus_get_node(dev);

	/* Allocate our IRQ resource. */
	rid = 0;
	sc->syncpt_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->syncpt_irq_res == NULL) {
		device_printf(dev, "Cannot allocate interrupt.\n");
		rv = ENXIO;
		goto fail;
	}
	rid = 1;
	sc->gen_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->gen_irq_res == NULL) {
		device_printf(dev, "Cannot allocate interrupt.\n");
		rv = ENXIO;
		goto fail;
	}

	/* FDT resources */
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "host1x", &sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot get fuse reset\n");
		goto fail;
	}
	rv = clk_get_by_ofw_index(sc->dev, 0, 0, &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get i2c clock: %d\n", rv);
		goto fail;
	}

	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock: %d\n", rv);
		goto fail;
	}
	rv = hwreset_deassert(sc->reset);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot clear reset\n");
		goto fail;
	}

	/* Setup  interrupts */
	rv = bus_setup_intr(dev, sc->gen_irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, host1x_gen_intr,
	    sc, &sc->gen_irq_h);
	if (rv) {
		device_printf(dev, "Cannot setup gen interrupt.\n");
		goto fail;
	}

	rv = bus_setup_intr(dev, sc->syncpt_irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, host1x_syncpt_intr,
	    sc, &sc->syncpt_irq_h);
	if (rv) {
		device_printf(dev, "Cannot setup syncpt interrupt.\n");
		goto fail;
	}

	simplebus_init(dev, 0);
	for (node = OF_child(node); node > 0; node = OF_peer(node))
	    simplebus_add_device(dev, node, 0, NULL, -1, NULL);

	sc->irq_hook.ich_func = host1x_irq_hook;
	sc->irq_hook.ich_arg = sc;
	config_intrhook_establish(&sc->irq_hook);
	bus_generic_new_pass(dev);
	return;

fail:
	device_detach(dev);
	return;
}

static int
host1x_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
host1x_attach(device_t dev)
{
	int rv, rid;
	struct host1x_softc *sc;

	sc = device_get_softc(dev);
	sc->tegra_drm = malloc(sizeof(struct tegra_drm), DRM_MEM_DRIVER,
	    M_WAITOK | M_ZERO);

	/* crosslink together all worlds */
	sc->dev = dev;
	sc->tegra_drm->drm_dev.dev_private = &sc->tegra_drm;
	sc->tegra_drm->drm_dev.dev = dev;

	TAILQ_INIT(&sc->clients);

	LOCK_INIT(sc);

	/* Get the memory resource for the register mapping. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot map registers.\n");
		rv = ENXIO;
		goto fail;
	}

	return (bus_generic_attach(dev));

fail:
	if (sc->tegra_drm != NULL)
		free(sc->tegra_drm, DRM_MEM_DRIVER);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);
	return (rv);
}

static int
host1x_detach(device_t dev)
{
	struct host1x_softc *sc;

	sc = device_get_softc(dev);

	host1x_drm_exit(sc);

	if (sc->gen_irq_h != NULL)
		bus_teardown_intr(dev, sc->gen_irq_res, sc->gen_irq_h);
	if (sc->tegra_drm != NULL)
		free(sc->tegra_drm, DRM_MEM_DRIVER);
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (sc->reset != NULL)
		hwreset_release(sc->reset);
	if (sc->syncpt_irq_h != NULL)
		bus_teardown_intr(dev, sc->syncpt_irq_res, sc->syncpt_irq_h);
	if (sc->gen_irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 1, sc->gen_irq_res);
	if (sc->syncpt_irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->syncpt_irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);
	return (bus_generic_detach(dev));
}

static device_method_t host1x_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		host1x_probe),
	DEVMETHOD(device_attach,	host1x_attach),
	DEVMETHOD(device_detach,	host1x_detach),

	/* Bus interface */
	DEVMETHOD(bus_new_pass,		host1x_new_pass),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,           host1x_fb_helper_getinfo),

	/* tegra drm interface */
	DEVMETHOD(tegra_drm_register_client,	host1x_register_client),
	DEVMETHOD(tegra_drm_deregister_client,	host1x_deregister_client),

	DEVMETHOD_END
};

static devclass_t host1x_devclass;
DEFINE_CLASS_1(host1x, host1x_driver, host1x_methods,
    sizeof(struct host1x_softc), simplebus_driver);
EARLY_DRIVER_MODULE(host1x, simplebus, host1x_driver,
    host1x_devclass, 0, 0,  BUS_PASS_BUS);

/* Bindings for fbd device. */
extern devclass_t fbd_devclass;
extern driver_t fbd_driver;
DRIVER_MODULE(fbd, host1x, fbd_driver, fbd_devclass, 0, 0);

