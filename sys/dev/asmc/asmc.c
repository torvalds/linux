/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007, 2008 Rui Paulo <rpaulo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Driver for Apple's System Management Console (SMC).
 * SMC can be found on the MacBook, MacBook Pro and Mac Mini.
 *
 * Inspired by the Linux applesmc driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/rman.h>

#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>
#include <dev/asmc/asmcvar.h>

/*
 * Device interface.
 */
static int 	asmc_probe(device_t dev);
static int 	asmc_attach(device_t dev);
static int 	asmc_detach(device_t dev);
static int 	asmc_resume(device_t dev);

/*
 * SMC functions.
 */
static int 	asmc_init(device_t dev);
static int 	asmc_command(device_t dev, uint8_t command);
static int 	asmc_wait(device_t dev, uint8_t val);
static int 	asmc_wait_ack(device_t dev, uint8_t val, int amount);
static int 	asmc_key_write(device_t dev, const char *key, uint8_t *buf,
    uint8_t len);
static int 	asmc_key_read(device_t dev, const char *key, uint8_t *buf,
    uint8_t);
static int 	asmc_fan_count(device_t dev);
static int 	asmc_fan_getvalue(device_t dev, const char *key, int fan);
static int 	asmc_fan_setvalue(device_t dev, const char *key, int fan, int speed);
static int 	asmc_temp_getvalue(device_t dev, const char *key);
static int 	asmc_sms_read(device_t, const char *key, int16_t *val);
static void 	asmc_sms_calibrate(device_t dev);
static int 	asmc_sms_intrfast(void *arg);
static void 	asmc_sms_printintr(device_t dev, uint8_t);
static void 	asmc_sms_task(void *arg, int pending);
#ifdef DEBUG
void		asmc_dumpall(device_t);
static int	asmc_key_dump(device_t, int);
#endif

/*
 * Model functions.
 */
static int 	asmc_mb_sysctl_fanid(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_fanspeed(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_fansafespeed(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_fanminspeed(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_fanmaxspeed(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_fantargetspeed(SYSCTL_HANDLER_ARGS);
static int 	asmc_temp_sysctl(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_sms_x(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_sms_y(SYSCTL_HANDLER_ARGS);
static int 	asmc_mb_sysctl_sms_z(SYSCTL_HANDLER_ARGS);
static int 	asmc_mbp_sysctl_light_left(SYSCTL_HANDLER_ARGS);
static int 	asmc_mbp_sysctl_light_right(SYSCTL_HANDLER_ARGS);
static int 	asmc_mbp_sysctl_light_control(SYSCTL_HANDLER_ARGS);

struct asmc_model {
	const char 	 *smc_model;	/* smbios.system.product env var. */
	const char 	 *smc_desc;	/* driver description */

	/* Helper functions */
	int (*smc_sms_x)(SYSCTL_HANDLER_ARGS);
	int (*smc_sms_y)(SYSCTL_HANDLER_ARGS);
	int (*smc_sms_z)(SYSCTL_HANDLER_ARGS);
	int (*smc_fan_id)(SYSCTL_HANDLER_ARGS);
	int (*smc_fan_speed)(SYSCTL_HANDLER_ARGS);
	int (*smc_fan_safespeed)(SYSCTL_HANDLER_ARGS);
	int (*smc_fan_minspeed)(SYSCTL_HANDLER_ARGS);
	int (*smc_fan_maxspeed)(SYSCTL_HANDLER_ARGS);
	int (*smc_fan_targetspeed)(SYSCTL_HANDLER_ARGS);
	int (*smc_light_left)(SYSCTL_HANDLER_ARGS);
	int (*smc_light_right)(SYSCTL_HANDLER_ARGS);
	int (*smc_light_control)(SYSCTL_HANDLER_ARGS);

	const char 	*smc_temps[ASMC_TEMP_MAX];
	const char 	*smc_tempnames[ASMC_TEMP_MAX];
	const char 	*smc_tempdescs[ASMC_TEMP_MAX];
};

static struct asmc_model *asmc_match(device_t dev);

#define ASMC_SMS_FUNCS	asmc_mb_sysctl_sms_x, asmc_mb_sysctl_sms_y, \
			asmc_mb_sysctl_sms_z

#define ASMC_SMS_FUNCS_DISABLED NULL,NULL,NULL

#define ASMC_FAN_FUNCS	asmc_mb_sysctl_fanid, asmc_mb_sysctl_fanspeed, asmc_mb_sysctl_fansafespeed, \
			asmc_mb_sysctl_fanminspeed, \
			asmc_mb_sysctl_fanmaxspeed, \
			asmc_mb_sysctl_fantargetspeed

#define ASMC_FAN_FUNCS2	asmc_mb_sysctl_fanid, asmc_mb_sysctl_fanspeed, NULL, \
			asmc_mb_sysctl_fanminspeed, \
			asmc_mb_sysctl_fanmaxspeed, \
			asmc_mb_sysctl_fantargetspeed

#define ASMC_LIGHT_FUNCS asmc_mbp_sysctl_light_left, \
			 asmc_mbp_sysctl_light_right, \
			 asmc_mbp_sysctl_light_control

#define ASMC_LIGHT_FUNCS_DISABLED NULL, NULL, NULL

struct asmc_model asmc_models[] = {
	{
	  "MacBook1,1", "Apple SMC MacBook Core Duo",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, NULL, NULL, NULL,
	  ASMC_MB_TEMPS, ASMC_MB_TEMPNAMES, ASMC_MB_TEMPDESCS
	},

	{
	  "MacBook2,1", "Apple SMC MacBook Core 2 Duo",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, NULL, NULL, NULL,
	  ASMC_MB_TEMPS, ASMC_MB_TEMPNAMES, ASMC_MB_TEMPDESCS
	},

	{
	  "MacBook3,1", "Apple SMC MacBook Core 2 Duo",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, NULL, NULL, NULL,
	  ASMC_MB31_TEMPS, ASMC_MB31_TEMPNAMES, ASMC_MB31_TEMPDESCS
	},

	{
	  "MacBookPro1,1", "Apple SMC MacBook Pro Core Duo (15-inch)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP_TEMPS, ASMC_MBP_TEMPNAMES, ASMC_MBP_TEMPDESCS
	},

	{
	  "MacBookPro1,2", "Apple SMC MacBook Pro Core Duo (17-inch)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP_TEMPS, ASMC_MBP_TEMPNAMES, ASMC_MBP_TEMPDESCS
	},

	{
	  "MacBookPro2,1", "Apple SMC MacBook Pro Core 2 Duo (17-inch)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP_TEMPS, ASMC_MBP_TEMPNAMES, ASMC_MBP_TEMPDESCS
	},

	{
	  "MacBookPro2,2", "Apple SMC MacBook Pro Core 2 Duo (15-inch)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP_TEMPS, ASMC_MBP_TEMPNAMES, ASMC_MBP_TEMPDESCS
	},

	{
	  "MacBookPro3,1", "Apple SMC MacBook Pro Core 2 Duo (15-inch LED)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP_TEMPS, ASMC_MBP_TEMPNAMES, ASMC_MBP_TEMPDESCS
	},

	{
	  "MacBookPro3,2", "Apple SMC MacBook Pro Core 2 Duo (17-inch HD)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP_TEMPS, ASMC_MBP_TEMPNAMES, ASMC_MBP_TEMPDESCS
	},

	{
	  "MacBookPro4,1", "Apple SMC MacBook Pro Core 2 Duo (Penryn)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP4_TEMPS, ASMC_MBP4_TEMPNAMES, ASMC_MBP4_TEMPDESCS
	},

	{
	  "MacBookPro5,1", "Apple SMC MacBook Pro Core 2 Duo (2008/2009)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP5_TEMPS, ASMC_MBP5_TEMPNAMES, ASMC_MBP5_TEMPDESCS
	},

	{
	  "MacBookPro8,1", "Apple SMC MacBook Pro (early 2011, 13-inch)",
	  ASMC_SMS_FUNCS_DISABLED, ASMC_FAN_FUNCS2, ASMC_LIGHT_FUNCS,
	  ASMC_MBP81_TEMPS, ASMC_MBP81_TEMPNAMES, ASMC_MBP81_TEMPDESCS
	},

	{
	  "MacBookPro8,2", "Apple SMC MacBook Pro (early 2011)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP82_TEMPS, ASMC_MBP82_TEMPNAMES, ASMC_MBP82_TEMPDESCS
	},

	{
	 "MacBookPro9,2", "Apple SMC MacBook Pro (mid 2012)",
	  ASMC_SMS_FUNCS_DISABLED, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP9_TEMPS, ASMC_MBP9_TEMPNAMES, ASMC_MBP9_TEMPDESCS
	},

	{
	  "MacBookPro11,2", "Apple SMC MacBook Pro Retina Core i7 (2013/2014)",
	  ASMC_SMS_FUNCS_DISABLED, ASMC_FAN_FUNCS2, ASMC_LIGHT_FUNCS,
	  ASMC_MBP112_TEMPS, ASMC_MBP112_TEMPNAMES, ASMC_MBP112_TEMPDESCS
	},

	{
	  "MacBookPro11,3", "Apple SMC MacBook Pro Retina Core i7 (2013/2014)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, ASMC_LIGHT_FUNCS,
	  ASMC_MBP113_TEMPS, ASMC_MBP113_TEMPNAMES, ASMC_MBP113_TEMPDESCS
	},

	/* The Mac Mini has no SMS */
	{
	  "Macmini1,1", "Apple SMC Mac Mini",
	  NULL, NULL, NULL,
	  ASMC_FAN_FUNCS,
	  NULL, NULL, NULL,
	  ASMC_MM_TEMPS, ASMC_MM_TEMPNAMES, ASMC_MM_TEMPDESCS
	},

        /* The Mac Mini 2,1 has no SMS */
        {
          "Macmini2,1", "Apple SMC Mac Mini 2,1",
          ASMC_SMS_FUNCS_DISABLED,
          ASMC_FAN_FUNCS,
          ASMC_LIGHT_FUNCS_DISABLED,
          ASMC_MM21_TEMPS, ASMC_MM21_TEMPNAMES, ASMC_MM21_TEMPDESCS
        },

	/* The Mac Mini 3,1 has no SMS */
	{
	  "Macmini3,1", "Apple SMC Mac Mini 3,1",
	  NULL, NULL, NULL,
	  ASMC_FAN_FUNCS,
	  NULL, NULL, NULL,
	  ASMC_MM31_TEMPS, ASMC_MM31_TEMPNAMES, ASMC_MM31_TEMPDESCS
	},

	/* The Mac Mini 4,1 (Mid-2010) has no SMS */
	{ 
	  "Macmini4,1", "Apple SMC Mac mini 4,1 (Mid-2010)",
	  ASMC_SMS_FUNCS_DISABLED,
	  ASMC_FAN_FUNCS,
	  ASMC_LIGHT_FUNCS_DISABLED,
	  ASMC_MM41_TEMPS, ASMC_MM41_TEMPNAMES, ASMC_MM41_TEMPDESCS
	},

	/* The Mac Mini 5,2 has no SMS */
	{ 
	  "Macmini5,2", "Apple SMC Mac Mini 5,2",
	  NULL, NULL, NULL,
	  ASMC_FAN_FUNCS2,
	  NULL, NULL, NULL,
	  ASMC_MM52_TEMPS, ASMC_MM52_TEMPNAMES, ASMC_MM52_TEMPDESCS
	},

	/* Idem for the Mac Pro "Quad Core" (original) */
	{
	  "MacPro1,1", "Apple SMC Mac Pro (Quad Core)",
	  NULL, NULL, NULL,
	  ASMC_FAN_FUNCS,
	  NULL, NULL, NULL,
	  ASMC_MP1_TEMPS, ASMC_MP1_TEMPNAMES, ASMC_MP1_TEMPDESCS
	},

	/* Idem for the Mac Pro (8-core) */
	{
	  "MacPro2", "Apple SMC Mac Pro (8-core)",
	  NULL, NULL, NULL,
	  ASMC_FAN_FUNCS,
	  NULL, NULL, NULL,
	  ASMC_MP2_TEMPS, ASMC_MP2_TEMPNAMES, ASMC_MP2_TEMPDESCS
	},

	/* Idem for the MacPro  2010*/
	{
	  "MacPro5,1", "Apple SMC MacPro (2010)",
	  NULL, NULL, NULL,
	  ASMC_FAN_FUNCS,
	  NULL, NULL, NULL,
	  ASMC_MP5_TEMPS, ASMC_MP5_TEMPNAMES, ASMC_MP5_TEMPDESCS
	},

	{
	  "MacBookAir1,1", "Apple SMC MacBook Air",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, NULL, NULL, NULL,
	  ASMC_MBA_TEMPS, ASMC_MBA_TEMPNAMES, ASMC_MBA_TEMPDESCS
	},

	{
	  "MacBookAir3,1", "Apple SMC MacBook Air Core 2 Duo (Late 2010)",
	  ASMC_SMS_FUNCS, ASMC_FAN_FUNCS, NULL, NULL, NULL,
	  ASMC_MBA3_TEMPS, ASMC_MBA3_TEMPNAMES, ASMC_MBA3_TEMPDESCS
	},

	{
	  "MacBookAir5,1", "Apple SMC MacBook Air 11-inch (Mid 2012)",
	  ASMC_SMS_FUNCS_DISABLED,
	  ASMC_FAN_FUNCS2,
	  ASMC_LIGHT_FUNCS,
	  ASMC_MBA5_TEMPS, ASMC_MBA5_TEMPNAMES, ASMC_MBA5_TEMPDESCS
	},

	{
	  "MacBookAir5,2", "Apple SMC MacBook Air 13-inch (Mid 2012)",
	  ASMC_SMS_FUNCS_DISABLED,
	  ASMC_FAN_FUNCS2,
	  ASMC_LIGHT_FUNCS,
	  ASMC_MBA5_TEMPS, ASMC_MBA5_TEMPNAMES, ASMC_MBA5_TEMPDESCS
	},

	{
	  "MacBookAir7,1", "Apple SMC MacBook Air 11-inch (Early 2015)",
	  ASMC_SMS_FUNCS_DISABLED,
	  ASMC_FAN_FUNCS2,
	  ASMC_LIGHT_FUNCS,
	  ASMC_MBA7_TEMPS, ASMC_MBA7_TEMPNAMES, ASMC_MBA7_TEMPDESCS
	},

	{
	  "MacBookAir7,2", "Apple SMC MacBook Air 13-inch (Early 2015)",
	  ASMC_SMS_FUNCS_DISABLED,
	  ASMC_FAN_FUNCS2,
	  ASMC_LIGHT_FUNCS,
	  ASMC_MBA7_TEMPS, ASMC_MBA7_TEMPNAMES, ASMC_MBA7_TEMPDESCS
	},

	{ NULL, NULL }
};

#undef ASMC_SMS_FUNCS
#undef ASMC_SMS_FUNCS_DISABLED
#undef ASMC_FAN_FUNCS
#undef ASMC_FAN_FUNCS2
#undef ASMC_LIGHT_FUNCS

/*
 * Driver methods.
 */
static device_method_t	asmc_methods[] = {
	DEVMETHOD(device_probe,		asmc_probe),
	DEVMETHOD(device_attach,	asmc_attach),
	DEVMETHOD(device_detach,	asmc_detach),
	DEVMETHOD(device_resume,	asmc_resume),

	{ 0, 0 }
};

static driver_t	asmc_driver = {
	"asmc",
	asmc_methods,
	sizeof(struct asmc_softc)
};

/*
 * Debugging
 */
#define	_COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("ASMC")
#ifdef DEBUG
#define ASMC_DPRINTF(str)	device_printf(dev, str)
#else
#define ASMC_DPRINTF(str)
#endif

/* NB: can't be const */
static char *asmc_ids[] = { "APP0001", NULL };

static devclass_t asmc_devclass;

static unsigned int light_control = 0;

DRIVER_MODULE(asmc, acpi, asmc_driver, asmc_devclass, NULL, NULL);
MODULE_DEPEND(asmc, acpi, 1, 1, 1);

static struct asmc_model *
asmc_match(device_t dev)
{
	int i;
	char *model;

	model = kern_getenv("smbios.system.product");
	if (model == NULL)
		return (NULL);

	for (i = 0; asmc_models[i].smc_model; i++) {
		if (!strncmp(model, asmc_models[i].smc_model, strlen(model))) {
			freeenv(model);
			return (&asmc_models[i]);
		}
	}
	freeenv(model);

	return (NULL);
}

static int
asmc_probe(device_t dev)
{
	struct asmc_model *model;
	int rv;

	if (resource_disabled("asmc", 0))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, asmc_ids, NULL);
	if (rv > 0)
		return (rv);

	model = asmc_match(dev);
	if (!model) {
		device_printf(dev, "model not recognized\n");
		return (ENXIO);
	}
	device_set_desc(dev, model->smc_desc);

	return (rv);
}

static int
asmc_attach(device_t dev)
{
	int i, j;
	int ret;
	char name[2];
	struct asmc_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *sysctlctx;
	struct sysctl_oid *sysctlnode;
	struct asmc_model *model;

	sc->sc_ioport = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->sc_rid_port, RF_ACTIVE);
	if (sc->sc_ioport == NULL) {
		device_printf(dev, "unable to allocate IO port\n");
		return (ENOMEM);
	}

	sysctlctx  = device_get_sysctl_ctx(dev);
	sysctlnode = device_get_sysctl_tree(dev);

	model = asmc_match(dev);

	mtx_init(&sc->sc_mtx, "asmc", NULL, MTX_SPIN);

	sc->sc_model = model;
	asmc_init(dev);

	/*
	 * dev.asmc.n.fan.* tree.
	 */
	sc->sc_fan_tree[0] = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "fan",
	    CTLFLAG_RD, 0, "Fan Root Tree");

	for (i = 1; i <= sc->sc_nfan; i++) {
		j = i - 1;
		name[0] = '0' + j;
		name[1] = 0;
		sc->sc_fan_tree[i] = SYSCTL_ADD_NODE(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[0]),
		    OID_AUTO, name, CTLFLAG_RD, 0,
		    "Fan Subtree");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
		    OID_AUTO, "id", CTLTYPE_STRING | CTLFLAG_RD,
		    dev, j, model->smc_fan_id, "I",
		    "Fan ID");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
		    OID_AUTO, "speed", CTLTYPE_INT | CTLFLAG_RD,
		    dev, j, model->smc_fan_speed, "I",
		    "Fan speed in RPM");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
		    OID_AUTO, "safespeed",
		    CTLTYPE_INT | CTLFLAG_RD,
		    dev, j, model->smc_fan_safespeed, "I",
		    "Fan safe speed in RPM");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
		    OID_AUTO, "minspeed",
		    CTLTYPE_INT | CTLFLAG_RW,
		    dev, j, model->smc_fan_minspeed, "I",
		    "Fan minimum speed in RPM");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
		    OID_AUTO, "maxspeed",
		    CTLTYPE_INT | CTLFLAG_RW,
		    dev, j, model->smc_fan_maxspeed, "I",
		    "Fan maximum speed in RPM");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_fan_tree[i]),
		    OID_AUTO, "targetspeed",
		    CTLTYPE_INT | CTLFLAG_RW,
		    dev, j, model->smc_fan_targetspeed, "I",
		    "Fan target speed in RPM");
	}

	/*
	 * dev.asmc.n.temp tree.
	 */
	sc->sc_temp_tree = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "temp",
	    CTLFLAG_RD, 0, "Temperature sensors");

	for (i = 0; model->smc_temps[i]; i++) {
		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_temp_tree),
		    OID_AUTO, model->smc_tempnames[i],
		    CTLTYPE_INT | CTLFLAG_RD,
		    dev, i, asmc_temp_sysctl, "I",
		    model->smc_tempdescs[i]);
	}

	/*
	 * dev.asmc.n.light
	 */
	if (model->smc_light_left) {
		sc->sc_light_tree = SYSCTL_ADD_NODE(sysctlctx,
		    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "light",
		    CTLFLAG_RD, 0, "Keyboard backlight sensors");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_light_tree),
		    OID_AUTO, "left", CTLTYPE_INT | CTLFLAG_RD,
		    dev, 0, model->smc_light_left, "I",
		    "Keyboard backlight left sensor");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_light_tree),
		    OID_AUTO, "right", CTLTYPE_INT | CTLFLAG_RD,
		    dev, 0, model->smc_light_right, "I",
		    "Keyboard backlight right sensor");

		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sc->sc_light_tree),
		    OID_AUTO, "control",
		    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY,
		    dev, 0, model->smc_light_control, "I",
		    "Keyboard backlight brightness control");
	}

	if (model->smc_sms_x == NULL)
		goto nosms;

	/*
	 * dev.asmc.n.sms tree.
	 */
	sc->sc_sms_tree = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode), OID_AUTO, "sms",
	    CTLFLAG_RD, 0, "Sudden Motion Sensor");

	SYSCTL_ADD_PROC(sysctlctx,
	    SYSCTL_CHILDREN(sc->sc_sms_tree),
	    OID_AUTO, "x", CTLTYPE_INT | CTLFLAG_RD,
	    dev, 0, model->smc_sms_x, "I",
	    "Sudden Motion Sensor X value");

	SYSCTL_ADD_PROC(sysctlctx,
	    SYSCTL_CHILDREN(sc->sc_sms_tree),
	    OID_AUTO, "y", CTLTYPE_INT | CTLFLAG_RD,
	    dev, 0, model->smc_sms_y, "I",
	    "Sudden Motion Sensor Y value");

	SYSCTL_ADD_PROC(sysctlctx,
	    SYSCTL_CHILDREN(sc->sc_sms_tree),
	    OID_AUTO, "z", CTLTYPE_INT | CTLFLAG_RD,
	    dev, 0, model->smc_sms_z, "I",
	    "Sudden Motion Sensor Z value");

	/*
	 * Need a taskqueue to send devctl_notify() events
	 * when the SMS interrupt us.
	 *
	 * PI_REALTIME is used due to the sensitivity of the
	 * interrupt. An interrupt from the SMS means that the
	 * disk heads should be turned off as quickly as possible.
	 *
	 * We only need to do this for the non INTR_FILTER case.
	 */
	sc->sc_sms_tq = NULL;
	TASK_INIT(&sc->sc_sms_task, 0, asmc_sms_task, sc);
	sc->sc_sms_tq = taskqueue_create_fast("asmc_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->sc_sms_tq);
	taskqueue_start_threads(&sc->sc_sms_tq, 1, PI_REALTIME, "%s sms taskq",
	    device_get_nameunit(dev));
	/*
	 * Allocate an IRQ for the SMS.
	 */
	sc->sc_rid_irq = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_rid_irq, RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "unable to allocate IRQ resource\n");
		ret = ENXIO;
		goto err2;
	}

	ret = bus_setup_intr(dev, sc->sc_irq,
	          INTR_TYPE_MISC | INTR_MPSAFE,
	    asmc_sms_intrfast, NULL,
	    dev, &sc->sc_cookie);

	if (ret) {
		device_printf(dev, "unable to setup SMS IRQ\n");
		goto err1;
	}
nosms:
	return (0);
err1:
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_rid_irq, sc->sc_irq);
err2:
	bus_release_resource(dev, SYS_RES_IOPORT, sc->sc_rid_port,
	    sc->sc_ioport);
	mtx_destroy(&sc->sc_mtx);
	if (sc->sc_sms_tq)
		taskqueue_free(sc->sc_sms_tq);

	return (ret);
}

static int
asmc_detach(device_t dev)
{
	struct asmc_softc *sc = device_get_softc(dev);

	if (sc->sc_sms_tq) {
		taskqueue_drain(sc->sc_sms_tq, &sc->sc_sms_task);
		taskqueue_free(sc->sc_sms_tq);
	}
	if (sc->sc_cookie)
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_cookie);
	if (sc->sc_irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_rid_irq,
		    sc->sc_irq);
	if (sc->sc_ioport)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->sc_rid_port,
		    sc->sc_ioport);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
asmc_resume(device_t dev)
{
    uint8_t buf[2];
    buf[0] = light_control;
    buf[1] = 0x00;
    asmc_key_write(dev, ASMC_KEY_LIGHTVALUE, buf, sizeof buf);
    return (0);
}


#ifdef DEBUG
void asmc_dumpall(device_t dev)
{
	int i;

	/* XXX magic number */
	for (i=0; i < 0x100; i++)
		asmc_key_dump(dev, i);
}
#endif

static int
asmc_init(device_t dev)
{
	struct asmc_softc *sc = device_get_softc(dev);
	int i, error = 1;
	uint8_t buf[4];

	if (sc->sc_model->smc_sms_x == NULL)
		goto nosms;

	/*
	 * We are ready to receive interrupts from the SMS.
	 */
	buf[0] = 0x01;
	ASMC_DPRINTF(("intok key\n"));
	asmc_key_write(dev, ASMC_KEY_INTOK, buf, 1);
	DELAY(50);

	/*
	 * Initiate the polling intervals.
	 */
	buf[0] = 20; /* msecs */
	ASMC_DPRINTF(("low int key\n"));
	asmc_key_write(dev, ASMC_KEY_SMS_LOW_INT, buf, 1);
	DELAY(200);

	buf[0] = 20; /* msecs */
	ASMC_DPRINTF(("high int key\n"));
	asmc_key_write(dev, ASMC_KEY_SMS_HIGH_INT, buf, 1);
	DELAY(200);

	buf[0] = 0x00;
	buf[1] = 0x60;
	ASMC_DPRINTF(("sms low key\n"));
	asmc_key_write(dev, ASMC_KEY_SMS_LOW, buf, 2);
	DELAY(200);

	buf[0] = 0x01;
	buf[1] = 0xc0;
	ASMC_DPRINTF(("sms high key\n"));
	asmc_key_write(dev, ASMC_KEY_SMS_HIGH, buf, 2);
	DELAY(200);

	/*
	 * I'm not sure what this key does, but it seems to be
	 * required.
	 */
	buf[0] = 0x01;
	ASMC_DPRINTF(("sms flag key\n"));
	asmc_key_write(dev, ASMC_KEY_SMS_FLAG, buf, 1);
	DELAY(100);

	sc->sc_sms_intr_works = 0;

	/*
	 * Retry SMS initialization 1000 times
	 * (takes approx. 2 seconds in worst case)
	 */
	for (i = 0; i < 1000; i++) {
		if (asmc_key_read(dev, ASMC_KEY_SMS, buf, 2) == 0 &&
		    (buf[0] == ASMC_SMS_INIT1 && buf[1] == ASMC_SMS_INIT2)) {
			error = 0;
			sc->sc_sms_intr_works = 1;
			goto out;
		}
		buf[0] = ASMC_SMS_INIT1;
		buf[1] = ASMC_SMS_INIT2;
		ASMC_DPRINTF(("sms key\n"));
		asmc_key_write(dev, ASMC_KEY_SMS, buf, 2);
		DELAY(50);
	}
	device_printf(dev, "WARNING: Sudden Motion Sensor not initialized!\n");

out:
	asmc_sms_calibrate(dev);
nosms:
	sc->sc_nfan = asmc_fan_count(dev);
	if (sc->sc_nfan > ASMC_MAXFANS) {
		device_printf(dev, "more than %d fans were detected. Please "
		    "report this.\n", ASMC_MAXFANS);
		sc->sc_nfan = ASMC_MAXFANS;
	}

	if (bootverbose) {
		/*
		 * The number of keys is a 32 bit buffer
		 */
		asmc_key_read(dev, ASMC_NKEYS, buf, 4);
		device_printf(dev, "number of keys: %d\n", ntohl(*(uint32_t*)buf));
	}

#ifdef DEBUG
	asmc_dumpall(dev);
#endif

	return (error);
}

/*
 * We need to make sure that the SMC acks the byte sent.
 * Just wait up to (amount * 10)  ms.
 */
static int
asmc_wait_ack(device_t dev, uint8_t val, int amount)
{
	struct asmc_softc *sc = device_get_softc(dev);
	u_int i;

	val = val & ASMC_STATUS_MASK;

	for (i = 0; i < amount; i++) {
		if ((ASMC_CMDPORT_READ(sc) & ASMC_STATUS_MASK) == val)
			return (0);
		DELAY(10);
	}

	return (1);
}

/*
 * We need to make sure that the SMC acks the byte sent.
 * Just wait up to 100 ms.
 */
static int
asmc_wait(device_t dev, uint8_t val)
{
	struct asmc_softc *sc;

	if (asmc_wait_ack(dev, val, 1000) == 0)
		return (0);

	sc = device_get_softc(dev);
	val = val & ASMC_STATUS_MASK;

#ifdef DEBUG
	device_printf(dev, "%s failed: 0x%x, 0x%x\n", __func__, val,
	    ASMC_CMDPORT_READ(sc));
#endif
	return (1);
}

/*
 * Send the given command, retrying up to 10 times if
 * the acknowledgement fails.
 */
static int
asmc_command(device_t dev, uint8_t command) {

	int i;
	struct asmc_softc *sc = device_get_softc(dev);

	for (i=0; i < 10; i++) {
		ASMC_CMDPORT_WRITE(sc, command);
		if (asmc_wait_ack(dev, 0x0c, 100) == 0) {
			return (0);
		}
	}

#ifdef DEBUG
	device_printf(dev, "%s failed: 0x%x, 0x%x\n", __func__, command,
	    ASMC_CMDPORT_READ(sc));
#endif
	return (1);
}

static int
asmc_key_read(device_t dev, const char *key, uint8_t *buf, uint8_t len)
{
	int i, error = 1, try = 0;
	struct asmc_softc *sc = device_get_softc(dev);

	mtx_lock_spin(&sc->sc_mtx);

begin:
	if (asmc_command(dev, ASMC_CMDREAD))
		goto out;

	for (i = 0; i < 4; i++) {
		ASMC_DATAPORT_WRITE(sc, key[i]);
		if (asmc_wait(dev, 0x04))
			goto out;
	}

	ASMC_DATAPORT_WRITE(sc, len);

	for (i = 0; i < len; i++) {
		if (asmc_wait(dev, 0x05))
			goto out;
		buf[i] = ASMC_DATAPORT_READ(sc);
	}

	error = 0;
out:
	if (error) {
		if (++try < 10) goto begin;
		device_printf(dev,"%s for key %s failed %d times, giving up\n",
			__func__, key, try);
	}

	mtx_unlock_spin(&sc->sc_mtx);

	return (error);
}

#ifdef DEBUG
static int
asmc_key_dump(device_t dev, int number)
{
	struct asmc_softc *sc = device_get_softc(dev);
	char key[5] = { 0 };
	char type[7] = { 0 };
	uint8_t index[4];
	uint8_t v[32];
	uint8_t maxlen;
	int i, error = 1, try = 0;

	mtx_lock_spin(&sc->sc_mtx);

	index[0] = (number >> 24) & 0xff;
	index[1] = (number >> 16) & 0xff;
	index[2] = (number >> 8) & 0xff;
	index[3] = (number) & 0xff;

begin:
	if (asmc_command(dev, 0x12))
		goto out;

	for (i = 0; i < 4; i++) {
		ASMC_DATAPORT_WRITE(sc, index[i]);
		if (asmc_wait(dev, 0x04))
			goto out;
	}

	ASMC_DATAPORT_WRITE(sc, 4);

	for (i = 0; i < 4; i++) {
		if (asmc_wait(dev, 0x05))
			goto out;
		key[i] = ASMC_DATAPORT_READ(sc);
	}

	/* get type */
	if (asmc_command(dev, 0x13))
		goto out;

	for (i = 0; i < 4; i++) {
		ASMC_DATAPORT_WRITE(sc, key[i]);
		if (asmc_wait(dev, 0x04))
			goto out;
	}

	ASMC_DATAPORT_WRITE(sc, 6);

	for (i = 0; i < 6; i++) {
		if (asmc_wait(dev, 0x05))
			goto out;
		type[i] = ASMC_DATAPORT_READ(sc);
	}

	error = 0;
out:
	if (error) {
		if (++try < 10) goto begin;
		device_printf(dev,"%s for key %s failed %d times, giving up\n",
			__func__, key, try);
		mtx_unlock_spin(&sc->sc_mtx);
	}
	else {
		char buf[1024];
		char buf2[8];
		mtx_unlock_spin(&sc->sc_mtx);
		maxlen = type[0];
		type[0] = ' ';
		type[5] = 0;
		if (maxlen > sizeof(v)) {
			device_printf(dev,
			    "WARNING: cropping maxlen from %d to %zu\n",
			    maxlen, sizeof(v));
			maxlen = sizeof(v);
		}
		for (i = 0; i < sizeof(v); i++) {
			v[i] = 0;
		}
		asmc_key_read(dev, key, v, maxlen);
		snprintf(buf, sizeof(buf), "key %d is: %s, type %s "
		    "(len %d), data", number, key, type, maxlen);
		for (i = 0; i < maxlen; i++) {
			snprintf(buf2, sizeof(buf2), " %02x", v[i]);
			strlcat(buf, buf2, sizeof(buf));
		}
		strlcat(buf, " \n", sizeof(buf));
		device_printf(dev, "%s", buf);
	}

	return (error);
}
#endif

static int
asmc_key_write(device_t dev, const char *key, uint8_t *buf, uint8_t len)
{
	int i, error = -1, try = 0;
	struct asmc_softc *sc = device_get_softc(dev);

	mtx_lock_spin(&sc->sc_mtx);

begin:
	ASMC_DPRINTF(("cmd port: cmd write\n"));
	if (asmc_command(dev, ASMC_CMDWRITE))
		goto out;

	ASMC_DPRINTF(("data port: key\n"));
	for (i = 0; i < 4; i++) {
		ASMC_DATAPORT_WRITE(sc, key[i]);
		if (asmc_wait(dev, 0x04))
			goto out;
	}
	ASMC_DPRINTF(("data port: length\n"));
	ASMC_DATAPORT_WRITE(sc, len);

	ASMC_DPRINTF(("data port: buffer\n"));
	for (i = 0; i < len; i++) {
		if (asmc_wait(dev, 0x04))
			goto out;
		ASMC_DATAPORT_WRITE(sc, buf[i]);
	}

	error = 0;
out:
	if (error) {
		if (++try < 10) goto begin;
		device_printf(dev,"%s for key %s failed %d times, giving up\n",
			__func__, key, try);
	}

	mtx_unlock_spin(&sc->sc_mtx);

	return (error);

}

/*
 * Fan control functions.
 */
static int
asmc_fan_count(device_t dev)
{
	uint8_t buf[1];

	if (asmc_key_read(dev, ASMC_KEY_FANCOUNT, buf, sizeof buf) < 0)
		return (-1);

	return (buf[0]);
}

static int
asmc_fan_getvalue(device_t dev, const char *key, int fan)
{
	int speed;
	uint8_t buf[2];
	char fankey[5];

	snprintf(fankey, sizeof(fankey), key, fan);
	if (asmc_key_read(dev, fankey, buf, sizeof buf) < 0)
		return (-1);
	speed = (buf[0] << 6) | (buf[1] >> 2);

	return (speed);
}

static char*
asmc_fan_getstring(device_t dev, const char *key, int fan, uint8_t *buf, uint8_t buflen)
{
	char fankey[5];
	char* desc;

	snprintf(fankey, sizeof(fankey), key, fan);
	if (asmc_key_read(dev, fankey, buf, buflen) < 0)
		return (NULL);
	desc = buf+4;

	return (desc);
}

static int
asmc_fan_setvalue(device_t dev, const char *key, int fan, int speed)
{
	uint8_t buf[2];
	char fankey[5];

	speed *= 4;

	buf[0] = speed>>8;
	buf[1] = speed;

	snprintf(fankey, sizeof(fankey), key, fan);
	if (asmc_key_write(dev, fankey, buf, sizeof buf) < 0)
		return (-1);

	return (0);
}

static int
asmc_mb_sysctl_fanspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = asmc_fan_getvalue(dev, ASMC_KEY_FANSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}

static int
asmc_mb_sysctl_fanid(SYSCTL_HANDLER_ARGS)
{
	uint8_t buf[16];
	device_t dev = (device_t) arg1;
	int fan = arg2;
	int error = true;
	char* desc;

	desc = asmc_fan_getstring(dev, ASMC_KEY_FANID, fan, buf, sizeof(buf));

	if (desc != NULL)
		error = sysctl_handle_string(oidp, desc, 0, req);

	return (error);
}

static int
asmc_mb_sysctl_fansafespeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = asmc_fan_getvalue(dev, ASMC_KEY_FANSAFESPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}


static int
asmc_mb_sysctl_fanminspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = asmc_fan_getvalue(dev, ASMC_KEY_FANMINSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);

	if (error == 0 && req->newptr != NULL) {
		unsigned int newspeed = v;
		asmc_fan_setvalue(dev, ASMC_KEY_FANMINSPEED, fan, newspeed);
	}

	return (error);
}

static int
asmc_mb_sysctl_fanmaxspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = asmc_fan_getvalue(dev, ASMC_KEY_FANMAXSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);

	if (error == 0 && req->newptr != NULL) {
		unsigned int newspeed = v;
		asmc_fan_setvalue(dev, ASMC_KEY_FANMAXSPEED, fan, newspeed);
	}

	return (error);
}

static int
asmc_mb_sysctl_fantargetspeed(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int fan = arg2;
	int error;
	int32_t v;

	v = asmc_fan_getvalue(dev, ASMC_KEY_FANTARGETSPEED, fan);
	error = sysctl_handle_int(oidp, &v, 0, req);

	if (error == 0 && req->newptr != NULL) {
		unsigned int newspeed = v;
		asmc_fan_setvalue(dev, ASMC_KEY_FANTARGETSPEED, fan, newspeed);
	}

	return (error);
}

/*
 * Temperature functions.
 */
static int
asmc_temp_getvalue(device_t dev, const char *key)
{
	uint8_t buf[2];

	/*
	 * Check for invalid temperatures.
	 */
	if (asmc_key_read(dev, key, buf, sizeof buf) < 0)
		return (-1);

	return (buf[0]);
}

static int
asmc_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	struct asmc_softc *sc = device_get_softc(dev);
	int error, val;

	val = asmc_temp_getvalue(dev, sc->sc_model->smc_temps[arg2]);
	error = sysctl_handle_int(oidp, &val, 0, req);

	return (error);
}

/*
 * Sudden Motion Sensor functions.
 */
static int
asmc_sms_read(device_t dev, const char *key, int16_t *val)
{
	uint8_t buf[2];
	int error;

	/* no need to do locking here as asmc_key_read() already does it */
	switch (key[3]) {
	case 'X':
	case 'Y':
	case 'Z':
		error =	asmc_key_read(dev, key, buf, sizeof buf);
		break;
	default:
		device_printf(dev, "%s called with invalid argument %s\n",
			      __func__, key);
		error = 1;
		goto out;
	}
	*val = ((int16_t)buf[0] << 8) | buf[1];
out:
	return (error);
}

static void
asmc_sms_calibrate(device_t dev)
{
	struct asmc_softc *sc = device_get_softc(dev);

	asmc_sms_read(dev, ASMC_KEY_SMS_X, &sc->sms_rest_x);
	asmc_sms_read(dev, ASMC_KEY_SMS_Y, &sc->sms_rest_y);
	asmc_sms_read(dev, ASMC_KEY_SMS_Z, &sc->sms_rest_z);
}

static int
asmc_sms_intrfast(void *arg)
{
	uint8_t type;
	device_t dev = (device_t) arg;
	struct asmc_softc *sc = device_get_softc(dev);
	if (!sc->sc_sms_intr_works)
		return (FILTER_HANDLED);

	mtx_lock_spin(&sc->sc_mtx);
	type = ASMC_INTPORT_READ(sc);
	mtx_unlock_spin(&sc->sc_mtx);

	sc->sc_sms_intrtype = type;
	asmc_sms_printintr(dev, type);

	taskqueue_enqueue(sc->sc_sms_tq, &sc->sc_sms_task);
	return (FILTER_HANDLED);
}



static void
asmc_sms_printintr(device_t dev, uint8_t type)
{

	switch (type) {
	case ASMC_SMS_INTFF:
		device_printf(dev, "WARNING: possible free fall!\n");
		break;
	case ASMC_SMS_INTHA:
		device_printf(dev, "WARNING: high acceleration detected!\n");
		break;
	case ASMC_SMS_INTSH:
		device_printf(dev, "WARNING: possible shock!\n");
		break;
	default:
		device_printf(dev, "%s unknown interrupt\n", __func__);
	}
}

static void
asmc_sms_task(void *arg, int pending)
{
	struct asmc_softc *sc = (struct asmc_softc *)arg;
	char notify[16];
	int type;

	switch (sc->sc_sms_intrtype) {
	case ASMC_SMS_INTFF:
		type = 2;
		break;
	case ASMC_SMS_INTHA:
		type = 1;
		break;
	case ASMC_SMS_INTSH:
		type = 0;
		break;
	default:
		type = 255;
	}

	snprintf(notify, sizeof(notify), " notify=0x%x", type);
	devctl_notify("ACPI", "asmc", "SMS", notify);
}

static int
asmc_mb_sysctl_sms_x(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int error;
	int16_t val;
	int32_t v;

	asmc_sms_read(dev, ASMC_KEY_SMS_X, &val);
	v = (int32_t) val;
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}

static int
asmc_mb_sysctl_sms_y(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int error;
	int16_t val;
	int32_t v;

	asmc_sms_read(dev, ASMC_KEY_SMS_Y, &val);
	v = (int32_t) val;
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}

static int
asmc_mb_sysctl_sms_z(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	int error;
	int16_t val;
	int32_t v;

	asmc_sms_read(dev, ASMC_KEY_SMS_Z, &val);
	v = (int32_t) val;
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}

static int
asmc_mbp_sysctl_light_left(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	uint8_t buf[6];
	int error;
	int32_t v;

	asmc_key_read(dev, ASMC_KEY_LIGHTLEFT, buf, sizeof buf);
	v = buf[2];
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}

static int
asmc_mbp_sysctl_light_right(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	uint8_t buf[6];
	int error;
	int32_t v;

	asmc_key_read(dev, ASMC_KEY_LIGHTRIGHT, buf, sizeof buf);
	v = buf[2];
	error = sysctl_handle_int(oidp, &v, 0, req);

	return (error);
}

static int
asmc_mbp_sysctl_light_control(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t) arg1;
	uint8_t buf[2];
	int error;
	int v;

	v = light_control;
	error = sysctl_handle_int(oidp, &v, 0, req);

	if (error == 0 && req->newptr != NULL) {
		if (v < 0 || v > 255)
			return (EINVAL);
		light_control = v;
		buf[0] = light_control;
		buf[1] = 0x00;
		asmc_key_write(dev, ASMC_KEY_LIGHTVALUE, buf, sizeof buf);
	}
	return (error);
}
