/*-
 * Copyright (c) 2002-2003 Taku YAMAMOTO <taku@cent.saitama-u.ac.jp>
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
 *	$Id: acpi_vid.c,v 1.4 2003/10/13 10:07:36 taku Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/power.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

/* ACPI video extension driver. */
struct acpi_video_output {
	ACPI_HANDLE	handle;
	UINT32		adr;
	STAILQ_ENTRY(acpi_video_output) vo_next;
	struct {
		int	num;
		STAILQ_ENTRY(acpi_video_output) next;
	} vo_unit;
	int		vo_brightness;
	int		vo_fullpower;
	int		vo_economy;
	int		vo_numlevels;
	int		*vo_levels;
	struct sysctl_ctx_list vo_sysctl_ctx;
	struct sysctl_oid *vo_sysctl_tree;
};

STAILQ_HEAD(acpi_video_output_queue, acpi_video_output);

struct acpi_video_softc {
	device_t		device;
	ACPI_HANDLE		handle;
	struct acpi_video_output_queue vid_outputs;
	eventhandler_tag	vid_pwr_evh;
};

/* interfaces */
static int	acpi_video_modevent(struct module*, int, void *);
static void	acpi_video_identify(driver_t *driver, device_t parent);
static int	acpi_video_probe(device_t);
static int	acpi_video_attach(device_t);
static int	acpi_video_detach(device_t);
static int	acpi_video_resume(device_t);
static int	acpi_video_shutdown(device_t);
static void	acpi_video_notify_handler(ACPI_HANDLE, UINT32, void *);
static void	acpi_video_power_profile(void *);
static void	acpi_video_bind_outputs(struct acpi_video_softc *);
static struct acpi_video_output *acpi_video_vo_init(UINT32);
static void	acpi_video_vo_bind(struct acpi_video_output *, ACPI_HANDLE);
static void	acpi_video_vo_destroy(struct acpi_video_output *);
static int	acpi_video_vo_check_level(struct acpi_video_output *, int);
static void	acpi_video_vo_notify_handler(ACPI_HANDLE, UINT32, void *);
static int	acpi_video_vo_active_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_video_vo_bright_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_video_vo_presets_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_video_vo_levels_sysctl(SYSCTL_HANDLER_ARGS);

/* operations */
static void	vid_set_switch_policy(ACPI_HANDLE, UINT32);
static int	vid_enum_outputs(ACPI_HANDLE,
		    void(*)(ACPI_HANDLE, UINT32, void *), void *);
static int	vo_get_brightness_levels(ACPI_HANDLE, int **);
static int	vo_get_brightness(ACPI_HANDLE);
static void	vo_set_brightness(ACPI_HANDLE, int);
static UINT32	vo_get_device_status(ACPI_HANDLE);
static UINT32	vo_get_graphics_state(ACPI_HANDLE);
static void	vo_set_device_state(ACPI_HANDLE, UINT32);

/* events */
#define	VID_NOTIFY_SWITCHED	0x80
#define	VID_NOTIFY_REPROBE	0x81
#define	VID_NOTIFY_CYCLE_BRN	0x85
#define	VID_NOTIFY_INC_BRN	0x86
#define	VID_NOTIFY_DEC_BRN	0x87
#define	VID_NOTIFY_ZERO_BRN	0x88

/* _DOS (Enable/Disable Output Switching) argument bits */
#define	DOS_SWITCH_MASK		3
#define	DOS_SWITCH_BY_OSPM	0
#define	DOS_SWITCH_BY_BIOS	1
#define	DOS_SWITCH_LOCKED	2
#define	DOS_BRIGHTNESS_BY_OSPM	(1 << 2)

/* _DOD and subdev's _ADR */
#define	DOD_DEVID_MASK		0x0f00
#define	DOD_DEVID_MASK_FULL	0xffff
#define	DOD_DEVID_MASK_DISPIDX	0x000f
#define	DOD_DEVID_MASK_DISPPORT	0x00f0
#define	DOD_DEVID_MONITOR	0x0100
#define	DOD_DEVID_LCD		0x0110
#define	DOD_DEVID_TV		0x0200
#define	DOD_DEVID_EXT		0x0300
#define	DOD_DEVID_INTDFP	0x0400
#define	DOD_BIOS		(1 << 16)
#define	DOD_NONVGA		(1 << 17)
#define	DOD_HEAD_ID_SHIFT	18
#define	DOD_HEAD_ID_BITS	3
#define	DOD_HEAD_ID_MASK \
		(((1 << DOD_HEAD_ID_BITS) - 1) << DOD_HEAD_ID_SHIFT)
#define	DOD_DEVID_SCHEME_STD	(1U << 31)

/* _BCL related constants */
#define	BCL_FULLPOWER		0
#define	BCL_ECONOMY		1

/* _DCS (Device Currrent Status) value bits and masks. */
#define	DCS_EXISTS		(1 << 0)
#define	DCS_ACTIVE		(1 << 1)
#define	DCS_READY		(1 << 2)
#define	DCS_FUNCTIONAL		(1 << 3)
#define	DCS_ATTACHED		(1 << 4)

/* _DSS (Device Set Status) argument bits and masks. */
#define	DSS_INACTIVE		0
#define	DSS_ACTIVE		(1 << 0)
#define	DSS_SETNEXT		(1 << 30)
#define	DSS_COMMIT		(1U << 31)

static device_method_t acpi_video_methods[] = {
	DEVMETHOD(device_identify, acpi_video_identify),
	DEVMETHOD(device_probe, acpi_video_probe),
	DEVMETHOD(device_attach, acpi_video_attach),
	DEVMETHOD(device_detach, acpi_video_detach),
	DEVMETHOD(device_resume, acpi_video_resume),
	DEVMETHOD(device_shutdown, acpi_video_shutdown),
	{ 0, 0 }
};

static driver_t acpi_video_driver = {
	"acpi_video",
	acpi_video_methods,
	sizeof(struct acpi_video_softc),
};

static devclass_t acpi_video_devclass;

DRIVER_MODULE(acpi_video, vgapci, acpi_video_driver, acpi_video_devclass,
	      acpi_video_modevent, NULL);
MODULE_DEPEND(acpi_video, acpi, 1, 1, 1);

static struct sysctl_ctx_list	acpi_video_sysctl_ctx;
static struct sysctl_oid	*acpi_video_sysctl_tree;
static struct acpi_video_output_queue crt_units, tv_units,
    ext_units, lcd_units, other_units;

/*
 * The 'video' lock protects the hierarchy of video output devices
 * (the video "bus").  The 'video_output' lock protects per-output
 * data is equivalent to a softc lock for each video output.
 */
ACPI_SERIAL_DECL(video, "ACPI video");
ACPI_SERIAL_DECL(video_output, "ACPI video output");
static MALLOC_DEFINE(M_ACPIVIDEO, "acpivideo", "ACPI video extension");

static int
acpi_video_modevent(struct module *mod __unused, int evt, void *cookie __unused)
{
	int err;

	err = 0;
	switch (evt) {
	case MOD_LOAD:
		sysctl_ctx_init(&acpi_video_sysctl_ctx);
		STAILQ_INIT(&crt_units);
		STAILQ_INIT(&tv_units);
		STAILQ_INIT(&ext_units);
		STAILQ_INIT(&lcd_units);
		STAILQ_INIT(&other_units);
		break;
	case MOD_UNLOAD:
		sysctl_ctx_free(&acpi_video_sysctl_ctx);
		acpi_video_sysctl_tree = NULL;
		break;
	default:
		err = EINVAL;
	}

	return (err);
}

static void
acpi_video_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "acpi_video", -1) == NULL)
		device_add_child(parent, "acpi_video", -1);
}

static int
acpi_video_probe(device_t dev)
{
	ACPI_HANDLE devh, h;
	ACPI_OBJECT_TYPE t_dos;

	devh = acpi_get_handle(dev);
	if (acpi_disabled("video") ||
	    ACPI_FAILURE(AcpiGetHandle(devh, "_DOD", &h)) ||
	    ACPI_FAILURE(AcpiGetHandle(devh, "_DOS", &h)) ||
	    ACPI_FAILURE(AcpiGetType(h, &t_dos)) ||
	    t_dos != ACPI_TYPE_METHOD)
		return (ENXIO);

	device_set_desc(dev, "ACPI video extension");
	return (0);
}

static int
acpi_video_attach(device_t dev)
{
	struct acpi_softc *acpi_sc;
	struct acpi_video_softc *sc;

	sc = device_get_softc(dev);

	acpi_sc = devclass_get_softc(devclass_find("acpi"), 0);
	if (acpi_sc == NULL)
		return (ENXIO);
	ACPI_SERIAL_BEGIN(video);
	if (acpi_video_sysctl_tree == NULL) {
		acpi_video_sysctl_tree = SYSCTL_ADD_NODE(&acpi_video_sysctl_ctx,
				    SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree),
				    OID_AUTO, "video", CTLFLAG_RD, 0,
				    "video extension control");
	}
	ACPI_SERIAL_END(video);

	sc->device = dev;
	sc->handle = acpi_get_handle(dev);
	STAILQ_INIT(&sc->vid_outputs);

	AcpiInstallNotifyHandler(sc->handle, ACPI_DEVICE_NOTIFY,
				 acpi_video_notify_handler, sc);
	sc->vid_pwr_evh = EVENTHANDLER_REGISTER(power_profile_change,
				 acpi_video_power_profile, sc, 0);

	ACPI_SERIAL_BEGIN(video);
	acpi_video_bind_outputs(sc);
	ACPI_SERIAL_END(video);

	/*
	 * Notify the BIOS that we want to switch both active outputs and
	 * brightness levels.
	 */
	vid_set_switch_policy(sc->handle, DOS_SWITCH_BY_OSPM |
	    DOS_BRIGHTNESS_BY_OSPM);

	acpi_video_power_profile(sc);

	return (0);
}

static int
acpi_video_detach(device_t dev)
{
	struct acpi_video_softc *sc;
	struct acpi_video_output *vo, *vn;

	sc = device_get_softc(dev);

	vid_set_switch_policy(sc->handle, DOS_SWITCH_BY_BIOS);
	EVENTHANDLER_DEREGISTER(power_profile_change, sc->vid_pwr_evh);
	AcpiRemoveNotifyHandler(sc->handle, ACPI_DEVICE_NOTIFY,
				acpi_video_notify_handler);

	ACPI_SERIAL_BEGIN(video);
	STAILQ_FOREACH_SAFE(vo, &sc->vid_outputs, vo_next, vn) {
		acpi_video_vo_destroy(vo);
	}
	ACPI_SERIAL_END(video);

	return (0);
}

static int
acpi_video_resume(device_t dev)
{
	struct acpi_video_softc *sc;
	struct acpi_video_output *vo, *vn;
	int level;

	sc = device_get_softc(dev);

	/* Restore brightness level */
	ACPI_SERIAL_BEGIN(video);
	ACPI_SERIAL_BEGIN(video_output);
	STAILQ_FOREACH_SAFE(vo, &sc->vid_outputs, vo_next, vn) {
		if ((vo->adr & DOD_DEVID_MASK_FULL) != DOD_DEVID_LCD &&
		    (vo->adr & DOD_DEVID_MASK) != DOD_DEVID_INTDFP)
			continue;

		if ((vo_get_device_status(vo->handle) & DCS_ACTIVE) == 0)
			continue;

		level = vo_get_brightness(vo->handle);
		if (level != -1)
			vo_set_brightness(vo->handle, level);
	}
	ACPI_SERIAL_END(video_output);
	ACPI_SERIAL_END(video);

	return (0);
}

static int
acpi_video_shutdown(device_t dev)
{
	struct acpi_video_softc *sc;

	sc = device_get_softc(dev);
	vid_set_switch_policy(sc->handle, DOS_SWITCH_BY_BIOS);

	return (0);
}

static void
acpi_video_notify_handler(ACPI_HANDLE handle, UINT32 notify, void *context)
{
	struct acpi_video_softc *sc;
	struct acpi_video_output *vo, *vo_tmp;
	ACPI_HANDLE lasthand;
	UINT32 dcs, dss, dss_p;

	sc = (struct acpi_video_softc *)context;

	switch (notify) {
	case VID_NOTIFY_SWITCHED:
		dss_p = 0;
		lasthand = NULL;
		ACPI_SERIAL_BEGIN(video);
		ACPI_SERIAL_BEGIN(video_output);
		STAILQ_FOREACH(vo, &sc->vid_outputs, vo_next) {
			dss = vo_get_graphics_state(vo->handle);
			dcs = vo_get_device_status(vo->handle);
			if (!(dcs & DCS_READY))
				dss = DSS_INACTIVE;
			if (((dcs & DCS_ACTIVE) && dss == DSS_INACTIVE) ||
			    (!(dcs & DCS_ACTIVE) && dss == DSS_ACTIVE)) {
				if (lasthand != NULL)
					vo_set_device_state(lasthand, dss_p);
				dss_p = dss;
				lasthand = vo->handle;
			}
		}
		if (lasthand != NULL)
			vo_set_device_state(lasthand, dss_p|DSS_COMMIT);
		ACPI_SERIAL_END(video_output);
		ACPI_SERIAL_END(video);
		break;
	case VID_NOTIFY_REPROBE:
		ACPI_SERIAL_BEGIN(video);
		STAILQ_FOREACH(vo, &sc->vid_outputs, vo_next)
			vo->handle = NULL;
		acpi_video_bind_outputs(sc);
		STAILQ_FOREACH_SAFE(vo, &sc->vid_outputs, vo_next, vo_tmp) {
			if (vo->handle == NULL) {
				STAILQ_REMOVE(&sc->vid_outputs, vo,
				    acpi_video_output, vo_next);
				acpi_video_vo_destroy(vo);
			}
		}
		ACPI_SERIAL_END(video);
		break;
	default:
		device_printf(sc->device, "unknown notify event 0x%x\n",
		    notify);
	}
}

static void
acpi_video_power_profile(void *context)
{
	int state;
	struct acpi_video_softc *sc;
	struct acpi_video_output *vo;

	sc = context;
	state = power_profile_get_state();
	if (state != POWER_PROFILE_PERFORMANCE &&
	    state != POWER_PROFILE_ECONOMY)
		return;

	ACPI_SERIAL_BEGIN(video);
	ACPI_SERIAL_BEGIN(video_output);
	STAILQ_FOREACH(vo, &sc->vid_outputs, vo_next) {
		if (vo->vo_levels != NULL && vo->vo_brightness == -1)
			vo_set_brightness(vo->handle,
			    state == POWER_PROFILE_ECONOMY ?
			    vo->vo_economy : vo->vo_fullpower);
	}
	ACPI_SERIAL_END(video_output);
	ACPI_SERIAL_END(video);
}

static void
acpi_video_bind_outputs_subr(ACPI_HANDLE handle, UINT32 adr, void *context)
{
	struct acpi_video_softc *sc;
	struct acpi_video_output *vo;

	ACPI_SERIAL_ASSERT(video);
	sc = context;

	STAILQ_FOREACH(vo, &sc->vid_outputs, vo_next) {
		if (vo->adr == adr) {
			acpi_video_vo_bind(vo, handle);
			return;
		}
	}
	vo = acpi_video_vo_init(adr);
	if (vo != NULL) {
		acpi_video_vo_bind(vo, handle);
		STAILQ_INSERT_TAIL(&sc->vid_outputs, vo, vo_next);
	}
}

static void
acpi_video_bind_outputs(struct acpi_video_softc *sc)
{

	ACPI_SERIAL_ASSERT(video);
	vid_enum_outputs(sc->handle, acpi_video_bind_outputs_subr, sc);
}

static struct acpi_video_output *
acpi_video_vo_init(UINT32 adr)
{
	struct acpi_video_output *vn, *vo, *vp;
	int n, x;
	char name[8], env[32];
	const char *type, *desc;
	struct acpi_video_output_queue *voqh;

	ACPI_SERIAL_ASSERT(video);

	switch (adr & DOD_DEVID_MASK) {
	case DOD_DEVID_MONITOR:
		if ((adr & DOD_DEVID_MASK_FULL) == DOD_DEVID_LCD) {
			/* DOD_DEVID_LCD is a common, backward compatible ID */
			desc = "Internal/Integrated Digital Flat Panel";
			type = "lcd";
			voqh = &lcd_units;
		} else {
			desc = "VGA CRT or VESA Compatible Analog Monitor";
			type = "crt";
			voqh = &crt_units;
		}
		break;
	case DOD_DEVID_TV:
		desc = "TV/HDTV or Analog-Video Monitor";
		type = "tv";
		voqh = &tv_units;
		break;
	case DOD_DEVID_EXT:
		desc = "External Digital Monitor";
		type = "ext";
		voqh = &ext_units;
		break;
	case DOD_DEVID_INTDFP:
		desc = "Internal/Integrated Digital Flat Panel";
		type = "lcd";
		voqh = &lcd_units;
		break;
	default:
		desc = "unknown output";
		type = "out";
		voqh = &other_units;
	}

	n = 0;
	vp = NULL;
	STAILQ_FOREACH(vn, voqh, vo_unit.next) {
		if (vn->vo_unit.num != n)
			break;
		vp = vn;
		n++;
	}

	snprintf(name, sizeof(name), "%s%d", type, n);

	vo = malloc(sizeof(*vo), M_ACPIVIDEO, M_NOWAIT);
	if (vo != NULL) {
		vo->handle = NULL;
		vo->adr = adr;
		vo->vo_unit.num = n;
		vo->vo_brightness = -1;
		vo->vo_fullpower = -1;	/* TODO: override with tunables */
		vo->vo_economy = -1;
		vo->vo_numlevels = 0;
		vo->vo_levels = NULL;
		snprintf(env, sizeof(env), "hw.acpi.video.%s.fullpower", name);
		if (getenv_int(env, &x))
			vo->vo_fullpower = x;
		snprintf(env, sizeof(env), "hw.acpi.video.%s.economy", name);
		if (getenv_int(env, &x))
			vo->vo_economy = x;

		sysctl_ctx_init(&vo->vo_sysctl_ctx);
		if (vp != NULL)
			STAILQ_INSERT_AFTER(voqh, vp, vo, vo_unit.next);
		else
			STAILQ_INSERT_TAIL(voqh, vo, vo_unit.next);
		if (acpi_video_sysctl_tree != NULL)
			vo->vo_sysctl_tree =
			    SYSCTL_ADD_NODE(&vo->vo_sysctl_ctx,
				SYSCTL_CHILDREN(acpi_video_sysctl_tree),
				OID_AUTO, name, CTLFLAG_RD, 0, desc);
		if (vo->vo_sysctl_tree != NULL) {
			SYSCTL_ADD_PROC(&vo->vo_sysctl_ctx,
			    SYSCTL_CHILDREN(vo->vo_sysctl_tree),
			    OID_AUTO, "active",
			    CTLTYPE_INT|CTLFLAG_RW, vo, 0,
			    acpi_video_vo_active_sysctl, "I",
			    "current activity of this device");
			SYSCTL_ADD_PROC(&vo->vo_sysctl_ctx,
			    SYSCTL_CHILDREN(vo->vo_sysctl_tree),
			    OID_AUTO, "brightness",
			    CTLTYPE_INT|CTLFLAG_RW, vo, 0,
			    acpi_video_vo_bright_sysctl, "I",
			    "current brightness level");
			SYSCTL_ADD_PROC(&vo->vo_sysctl_ctx,
			    SYSCTL_CHILDREN(vo->vo_sysctl_tree),
			    OID_AUTO, "fullpower",
			    CTLTYPE_INT|CTLFLAG_RW, vo,
			    POWER_PROFILE_PERFORMANCE,
			    acpi_video_vo_presets_sysctl, "I",
			    "preset level for full power mode");
			SYSCTL_ADD_PROC(&vo->vo_sysctl_ctx,
			    SYSCTL_CHILDREN(vo->vo_sysctl_tree),
			    OID_AUTO, "economy",
			    CTLTYPE_INT|CTLFLAG_RW, vo,
			    POWER_PROFILE_ECONOMY,
			    acpi_video_vo_presets_sysctl, "I",
			    "preset level for economy mode");
			SYSCTL_ADD_PROC(&vo->vo_sysctl_ctx,
			    SYSCTL_CHILDREN(vo->vo_sysctl_tree),
			    OID_AUTO, "levels",
			    CTLTYPE_INT | CTLFLAG_RD, vo, 0,
			    acpi_video_vo_levels_sysctl, "I",
			    "supported brightness levels");
		} else
			printf("%s: sysctl node creation failed\n", type);
	} else
		printf("%s: softc allocation failed\n", type);

	if (bootverbose) {
		printf("found %s(%x)", desc, adr & DOD_DEVID_MASK_FULL);
		printf(", idx#%x", adr & DOD_DEVID_MASK_DISPIDX);
		printf(", port#%x", (adr & DOD_DEVID_MASK_DISPPORT) >> 4);
		if (adr & DOD_BIOS)
			printf(", detectable by BIOS");
		if (adr & DOD_NONVGA)
			printf(" (Non-VGA output device whose power "
			    "is related to the VGA device)");
		printf(", head #%d\n",
			(adr & DOD_HEAD_ID_MASK) >> DOD_HEAD_ID_SHIFT);
	}
	return (vo);
}

static void
acpi_video_vo_bind(struct acpi_video_output *vo, ACPI_HANDLE handle)
{

	ACPI_SERIAL_BEGIN(video_output);
	if (vo->vo_levels != NULL) {
		AcpiRemoveNotifyHandler(vo->handle, ACPI_DEVICE_NOTIFY,
		    acpi_video_vo_notify_handler);
		AcpiOsFree(vo->vo_levels);
		vo->vo_levels = NULL;
	}
	vo->handle = handle;
	vo->vo_numlevels = vo_get_brightness_levels(handle, &vo->vo_levels);
	if (vo->vo_numlevels >= 2) {
		if (vo->vo_fullpower == -1 ||
		    acpi_video_vo_check_level(vo, vo->vo_fullpower) != 0) {
			/* XXX - can't deal with rebinding... */
			vo->vo_fullpower = vo->vo_levels[BCL_FULLPOWER];
		}
		if (vo->vo_economy == -1 ||
		    acpi_video_vo_check_level(vo, vo->vo_economy) != 0) {
			/* XXX - see above. */
			vo->vo_economy = vo->vo_levels[BCL_ECONOMY];
		}
		AcpiInstallNotifyHandler(handle, ACPI_DEVICE_NOTIFY,
		    acpi_video_vo_notify_handler, vo);
	}
	ACPI_SERIAL_END(video_output);
}

static void
acpi_video_vo_destroy(struct acpi_video_output *vo)
{
	struct acpi_video_output_queue *voqh;

	ACPI_SERIAL_ASSERT(video);
	if (vo->vo_sysctl_tree != NULL) {
		vo->vo_sysctl_tree = NULL;
		sysctl_ctx_free(&vo->vo_sysctl_ctx);
	}
	if (vo->vo_levels != NULL) {
		AcpiRemoveNotifyHandler(vo->handle, ACPI_DEVICE_NOTIFY,
		    acpi_video_vo_notify_handler);
		AcpiOsFree(vo->vo_levels);
	}

	switch (vo->adr & DOD_DEVID_MASK) {
	case DOD_DEVID_MONITOR:
		voqh = &crt_units;
		break;
	case DOD_DEVID_TV:
		voqh = &tv_units;
		break;
	case DOD_DEVID_EXT:
		voqh = &ext_units;
		break;
	case DOD_DEVID_INTDFP:
		voqh = &lcd_units;
		break;
	default:
		voqh = &other_units;
	}
	STAILQ_REMOVE(voqh, vo, acpi_video_output, vo_unit.next);
	free(vo, M_ACPIVIDEO);
}

static int
acpi_video_vo_check_level(struct acpi_video_output *vo, int level)
{
	int i;

	ACPI_SERIAL_ASSERT(video_output);
	if (vo->vo_levels == NULL)
		return (ENODEV);
	for (i = 0; i < vo->vo_numlevels; i++)
		if (vo->vo_levels[i] == level)
			return (0);
	return (EINVAL);
}

static void
acpi_video_vo_notify_handler(ACPI_HANDLE handle, UINT32 notify, void *context)
{
	struct acpi_video_output *vo;
	int i, j, level, new_level;

	vo = context;
	ACPI_SERIAL_BEGIN(video_output);
	if (vo->handle != handle)
		goto out;

	switch (notify) {
	case VID_NOTIFY_CYCLE_BRN:
		if (vo->vo_numlevels <= 3)
			goto out;
		/* FALLTHROUGH */
	case VID_NOTIFY_INC_BRN:
	case VID_NOTIFY_DEC_BRN:
	case VID_NOTIFY_ZERO_BRN:
		if (vo->vo_levels == NULL)
			goto out;
		level = vo_get_brightness(handle);
		if (level < 0)
			goto out;
		break;
	default:
		printf("unknown notify event 0x%x from %s\n",
		    notify, acpi_name(handle));
		goto out;
	}

	new_level = level;
	switch (notify) {
	case VID_NOTIFY_CYCLE_BRN:
		for (i = 2; i < vo->vo_numlevels; i++)
			if (vo->vo_levels[i] == level) {
				new_level = vo->vo_numlevels > i + 1 ?
				     vo->vo_levels[i + 1] : vo->vo_levels[2];
				break;
			}
		break;
	case VID_NOTIFY_INC_BRN:
	case VID_NOTIFY_DEC_BRN:
		for (i = 0; i < vo->vo_numlevels; i++) {
			j = vo->vo_levels[i];
			if (notify == VID_NOTIFY_INC_BRN) {
				if (j > level &&
				    (j < new_level || level == new_level))
					new_level = j;
			} else {
				if (j < level &&
				    (j > new_level || level == new_level))
					new_level = j;
			}
		}
		break;
	case VID_NOTIFY_ZERO_BRN:
		for (i = 0; i < vo->vo_numlevels; i++)
			if (vo->vo_levels[i] == 0) {
				new_level = 0;
				break;
			}
		break;
	}
	if (new_level != level) {
		vo_set_brightness(handle, new_level);
		vo->vo_brightness = new_level;
	}

out:
	ACPI_SERIAL_END(video_output);
}

/* ARGSUSED */
static int
acpi_video_vo_active_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_video_output *vo;
	int state, err;

	vo = (struct acpi_video_output *)arg1;
	if (vo->handle == NULL)
		return (ENXIO);
	ACPI_SERIAL_BEGIN(video_output);
	state = (vo_get_device_status(vo->handle) & DCS_ACTIVE) ? 1 : 0;
	err = sysctl_handle_int(oidp, &state, 0, req);
	if (err != 0 || req->newptr == NULL)
		goto out;
	vo_set_device_state(vo->handle,
	    DSS_COMMIT | (state ? DSS_ACTIVE : DSS_INACTIVE));
out:
	ACPI_SERIAL_END(video_output);
	return (err);
}

/* ARGSUSED */
static int
acpi_video_vo_bright_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_video_output *vo;
	int level, preset, err;

	vo = (struct acpi_video_output *)arg1;
	ACPI_SERIAL_BEGIN(video_output);
	if (vo->handle == NULL) {
		err = ENXIO;
		goto out;
	}
	if (vo->vo_levels == NULL) {
		err = ENODEV;
		goto out;
	}

	preset = (power_profile_get_state() == POWER_PROFILE_ECONOMY) ?
		  vo->vo_economy : vo->vo_fullpower;
	level = vo->vo_brightness;
	if (level == -1)
		level = preset;

	err = sysctl_handle_int(oidp, &level, 0, req);
	if (err != 0 || req->newptr == NULL)
		goto out;
	if (level < -1 || level > 100) {
		err = EINVAL;
		goto out;
	}

	if (level != -1 && (err = acpi_video_vo_check_level(vo, level)))
		goto out;
	vo->vo_brightness = level;
	vo_set_brightness(vo->handle, (level == -1) ? preset : level);

out:
	ACPI_SERIAL_END(video_output);
	return (err);
}

static int
acpi_video_vo_presets_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_video_output *vo;
	int i, level, *preset, err;

	vo = (struct acpi_video_output *)arg1;
	ACPI_SERIAL_BEGIN(video_output);
	if (vo->handle == NULL) {
		err = ENXIO;
		goto out;
	}
	if (vo->vo_levels == NULL) {
		err = ENODEV;
		goto out;
	}
	preset = (arg2 == POWER_PROFILE_ECONOMY) ?
		  &vo->vo_economy : &vo->vo_fullpower;
	level = *preset;
	err = sysctl_handle_int(oidp, &level, 0, req);
	if (err != 0 || req->newptr == NULL)
		goto out;
	if (level < -1 || level > 100) {
		err = EINVAL;
		goto out;
	}
	if (level == -1) {
		i = (arg2 == POWER_PROFILE_ECONOMY) ?
		    BCL_ECONOMY : BCL_FULLPOWER;
		level = vo->vo_levels[i];
	} else if ((err = acpi_video_vo_check_level(vo, level)) != 0)
		goto out;

	if (vo->vo_brightness == -1 && (power_profile_get_state() == arg2))
		vo_set_brightness(vo->handle, level);
	*preset = level;

out:
	ACPI_SERIAL_END(video_output);
	return (err);
}

/* ARGSUSED */
static int
acpi_video_vo_levels_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_video_output *vo;
	int err;

	vo = (struct acpi_video_output *)arg1;
	ACPI_SERIAL_BEGIN(video_output);
	if (vo->vo_levels == NULL) {
		err = ENODEV;
		goto out;
	}
	if (req->newptr != NULL) {
		err = EPERM;
		goto out;
	}
	err = sysctl_handle_opaque(oidp, vo->vo_levels,
	    vo->vo_numlevels * sizeof(*vo->vo_levels), req);

out:
	ACPI_SERIAL_END(video_output);
	return (err);
}

static void
vid_set_switch_policy(ACPI_HANDLE handle, UINT32 policy)
{
	ACPI_STATUS status;

	status = acpi_SetInteger(handle, "_DOS", policy);
	if (ACPI_FAILURE(status))
		printf("can't evaluate %s._DOS - %s\n",
		       acpi_name(handle), AcpiFormatException(status));
}

struct enum_callback_arg {
	void (*callback)(ACPI_HANDLE, UINT32, void *);
	void *context;
	ACPI_OBJECT *dod_pkg;
	int count;
};

static ACPI_STATUS
vid_enum_outputs_subr(ACPI_HANDLE handle, UINT32 level __unused,
		      void *context, void **retp __unused)
{
	ACPI_STATUS status;
	UINT32 adr, val;
	struct enum_callback_arg *argset;
	size_t i;

	ACPI_SERIAL_ASSERT(video);
	argset = context;
	status = acpi_GetInteger(handle, "_ADR", &adr);
	if (ACPI_FAILURE(status))
		return (AE_OK);

	for (i = 0; i < argset->dod_pkg->Package.Count; i++) {
		if (acpi_PkgInt32(argset->dod_pkg, i, &val) == 0 &&
		    (val & DOD_DEVID_MASK_FULL) ==
		    (adr & DOD_DEVID_MASK_FULL)) {
			argset->callback(handle, val, argset->context);
			argset->count++;
		}
	}

	return (AE_OK);
}

static int
vid_enum_outputs(ACPI_HANDLE handle,
		 void (*callback)(ACPI_HANDLE, UINT32, void *), void *context)
{
	ACPI_STATUS status;
	ACPI_BUFFER dod_buf;
	ACPI_OBJECT *res;
	struct enum_callback_arg argset;

	ACPI_SERIAL_ASSERT(video);
	dod_buf.Length = ACPI_ALLOCATE_BUFFER;
	dod_buf.Pointer = NULL;
	status = AcpiEvaluateObject(handle, "_DOD", NULL, &dod_buf);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			printf("can't evaluate %s._DOD - %s\n",
			       acpi_name(handle), AcpiFormatException(status));
		argset.count = -1;
		goto out;
	}
	res = (ACPI_OBJECT *)dod_buf.Pointer;
	if (!ACPI_PKG_VALID(res, 1)) {
		printf("evaluation of %s._DOD makes no sense\n",
		       acpi_name(handle));
		argset.count = -1;
		goto out;
	}
	if (callback == NULL) {
		argset.count = res->Package.Count;
		goto out;
	}
	argset.callback = callback;
	argset.context  = context;
	argset.dod_pkg  = res;
	argset.count    = 0;
	status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, handle, 1,
	    vid_enum_outputs_subr, NULL, &argset, NULL);
	if (ACPI_FAILURE(status))
		printf("failed walking down %s - %s\n",
		       acpi_name(handle), AcpiFormatException(status));
out:
	if (dod_buf.Pointer != NULL)
		AcpiOsFree(dod_buf.Pointer);
	return (argset.count);
}

static int
vo_get_brightness_levels(ACPI_HANDLE handle, int **levelp)
{
	ACPI_STATUS status;
	ACPI_BUFFER bcl_buf;
	ACPI_OBJECT *res;
	int num, i, n, *levels;

	bcl_buf.Length = ACPI_ALLOCATE_BUFFER;
	bcl_buf.Pointer = NULL;
	status = AcpiEvaluateObject(handle, "_BCL", NULL, &bcl_buf);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			printf("can't evaluate %s._BCL - %s\n",
			       acpi_name(handle), AcpiFormatException(status));
		goto out;
	}
	res = (ACPI_OBJECT *)bcl_buf.Pointer;
	if (!ACPI_PKG_VALID(res, 2)) {
		printf("evaluation of %s._BCL makes no sense\n",
		       acpi_name(handle));
		goto out;
	}
	num = res->Package.Count;
	if (num < 2 || levelp == NULL)
		goto out;
	levels = AcpiOsAllocate(num * sizeof(*levels));
	if (levels == NULL)
		goto out;
	for (i = 0, n = 0; i < num; i++)
		if (acpi_PkgInt32(res, i, &levels[n]) == 0)
			n++;
	if (n < 2) {
		AcpiOsFree(levels);
		goto out;
	}
	*levelp = levels;
	return (n);

out:
	if (bcl_buf.Pointer != NULL)
		AcpiOsFree(bcl_buf.Pointer);
	return (0);
}

static int
vo_get_brightness(ACPI_HANDLE handle)
{
	UINT32 level;
	ACPI_STATUS status;

	ACPI_SERIAL_ASSERT(video_output);
	status = acpi_GetInteger(handle, "_BQC", &level);
	if (ACPI_FAILURE(status)) {
		printf("can't evaluate %s._BQC - %s\n", acpi_name(handle),
		    AcpiFormatException(status));
		return (-1);
	}
	if (level > 100)
		return (-1);

	return (level);
}

static void
vo_set_brightness(ACPI_HANDLE handle, int level)
{
	ACPI_STATUS status;

	ACPI_SERIAL_ASSERT(video_output);
	status = acpi_SetInteger(handle, "_BCM", level);
	if (ACPI_FAILURE(status))
		printf("can't evaluate %s._BCM - %s\n",
		       acpi_name(handle), AcpiFormatException(status));
}

static UINT32
vo_get_device_status(ACPI_HANDLE handle)
{
	UINT32 dcs;
	ACPI_STATUS status;

	ACPI_SERIAL_ASSERT(video_output);
	dcs = 0;
	status = acpi_GetInteger(handle, "_DCS", &dcs);
	if (ACPI_FAILURE(status))
		printf("can't evaluate %s._DCS - %s\n",
		       acpi_name(handle), AcpiFormatException(status));

	return (dcs);
}

static UINT32
vo_get_graphics_state(ACPI_HANDLE handle)
{
	UINT32 dgs;
	ACPI_STATUS status;

	dgs = 0;
	status = acpi_GetInteger(handle, "_DGS", &dgs);
	if (ACPI_FAILURE(status))
		printf("can't evaluate %s._DGS - %s\n",
		       acpi_name(handle), AcpiFormatException(status));

	return (dgs);
}

static void
vo_set_device_state(ACPI_HANDLE handle, UINT32 state)
{
	ACPI_STATUS status;

	ACPI_SERIAL_ASSERT(video_output);
	status = acpi_SetInteger(handle, "_DSS", state);
	if (ACPI_FAILURE(status))
		printf("can't evaluate %s._DSS - %s\n",
		       acpi_name(handle), AcpiFormatException(status));
}
