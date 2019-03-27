/*-
 * Copyright (c) 2000, 2001 Michael Smith
 * Copyright (c) 2000 BSDi
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

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/power.h>

#include "cpufreq_if.h"

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_THERMAL
ACPI_MODULE_NAME("THERMAL")

#define TZ_ZEROC	2731
#define TZ_KELVTOC(x)	(((x) - TZ_ZEROC) / 10), abs(((x) - TZ_ZEROC) % 10)

#define TZ_NOTIFY_TEMPERATURE	0x80 /* Temperature changed. */
#define TZ_NOTIFY_LEVELS	0x81 /* Cooling levels changed. */
#define TZ_NOTIFY_DEVICES	0x82 /* Device lists changed. */
#define TZ_NOTIFY_CRITICAL	0xcc /* Fake notify that _CRT/_HOT reached. */

/* Check for temperature changes every 10 seconds by default */
#define TZ_POLLRATE	10

/* Make sure the reported temperature is valid for this number of polls. */
#define TZ_VALIDCHECKS	3

/* Notify the user we will be shutting down in one more poll cycle. */
#define TZ_NOTIFYCOUNT	(TZ_VALIDCHECKS - 1)

/* ACPI spec defines this */
#define TZ_NUMLEVELS	10
struct acpi_tz_zone {
    int		ac[TZ_NUMLEVELS];
    ACPI_BUFFER	al[TZ_NUMLEVELS];
    int		crt;
    int		hot;
    ACPI_BUFFER	psl;
    int		psv;
    int		tc1;
    int		tc2;
    int		tsp;
    int		tzp;
};

struct acpi_tz_softc {
    device_t			tz_dev;
    ACPI_HANDLE			tz_handle;	/*Thermal zone handle*/
    int				tz_temperature;	/*Current temperature*/
    int				tz_active;	/*Current active cooling*/
#define TZ_ACTIVE_NONE		-1
#define TZ_ACTIVE_UNKNOWN	-2
    int				tz_requested;	/*Minimum active cooling*/
    int				tz_thflags;	/*Current temp-related flags*/
#define TZ_THFLAG_NONE		0
#define TZ_THFLAG_PSV		(1<<0)
#define TZ_THFLAG_HOT		(1<<2)
#define TZ_THFLAG_CRT		(1<<3)
    int				tz_flags;
#define TZ_FLAG_NO_SCP		(1<<0)		/*No _SCP method*/
#define TZ_FLAG_GETPROFILE	(1<<1)		/*Get power_profile in timeout*/
#define TZ_FLAG_GETSETTINGS	(1<<2)		/*Get devs/setpoints*/
    struct timespec		tz_cooling_started;
					/*Current cooling starting time*/

    struct sysctl_ctx_list	tz_sysctl_ctx;
    struct sysctl_oid		*tz_sysctl_tree;
    eventhandler_tag		tz_event;

    struct acpi_tz_zone 	tz_zone;	/*Thermal zone parameters*/
    int				tz_validchecks;
    int				tz_insane_tmp_notified;

    /* passive cooling */
    struct proc			*tz_cooling_proc;
    int				tz_cooling_proc_running;
    int				tz_cooling_enabled;
    int				tz_cooling_active;
    int				tz_cooling_updated;
    int				tz_cooling_saved_freq;
};

#define	TZ_ACTIVE_LEVEL(act)	((act) >= 0 ? (act) : TZ_NUMLEVELS)

#define CPUFREQ_MAX_LEVELS	64 /* XXX cpufreq should export this */

static int	acpi_tz_probe(device_t dev);
static int	acpi_tz_attach(device_t dev);
static int	acpi_tz_establish(struct acpi_tz_softc *sc);
static void	acpi_tz_monitor(void *Context);
static void	acpi_tz_switch_cooler_off(ACPI_OBJECT *obj, void *arg);
static void	acpi_tz_switch_cooler_on(ACPI_OBJECT *obj, void *arg);
static void	acpi_tz_getparam(struct acpi_tz_softc *sc, char *node,
				 int *data);
static void	acpi_tz_sanity(struct acpi_tz_softc *sc, int *val, char *what);
static int	acpi_tz_active_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_tz_cooling_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_tz_temp_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_tz_passive_sysctl(SYSCTL_HANDLER_ARGS);
static void	acpi_tz_notify_handler(ACPI_HANDLE h, UINT32 notify,
				       void *context);
static void	acpi_tz_signal(struct acpi_tz_softc *sc, int flags);
static void	acpi_tz_timeout(struct acpi_tz_softc *sc, int flags);
static void	acpi_tz_power_profile(void *arg);
static void	acpi_tz_thread(void *arg);
static int	acpi_tz_cooling_is_available(struct acpi_tz_softc *sc);
static int	acpi_tz_cooling_thread_start(struct acpi_tz_softc *sc);

static device_method_t acpi_tz_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	acpi_tz_probe),
    DEVMETHOD(device_attach,	acpi_tz_attach),

    DEVMETHOD_END
};

static driver_t acpi_tz_driver = {
    "acpi_tz",
    acpi_tz_methods,
    sizeof(struct acpi_tz_softc),
};

static char *acpi_tz_tmp_name = "_TMP";

static devclass_t acpi_tz_devclass;
DRIVER_MODULE(acpi_tz, acpi, acpi_tz_driver, acpi_tz_devclass, 0, 0);
MODULE_DEPEND(acpi_tz, acpi, 1, 1, 1);

static struct sysctl_ctx_list	acpi_tz_sysctl_ctx;
static struct sysctl_oid	*acpi_tz_sysctl_tree;

/* Minimum cooling run time */
static int			acpi_tz_min_runtime;
static int			acpi_tz_polling_rate = TZ_POLLRATE;
static int			acpi_tz_override;

/* Timezone polling thread */
static struct proc		*acpi_tz_proc;
ACPI_LOCK_DECL(thermal, "ACPI thermal zone");

static int			acpi_tz_cooling_unit = -1;

static int
acpi_tz_probe(device_t dev)
{
    int		result;

    if (acpi_get_type(dev) == ACPI_TYPE_THERMAL && !acpi_disabled("thermal")) {
	device_set_desc(dev, "Thermal Zone");
	result = -10;
    } else
	result = ENXIO;
    return (result);
}

static int
acpi_tz_attach(device_t dev)
{
    struct acpi_tz_softc	*sc;
    struct acpi_softc		*acpi_sc;
    int				error;
    char			oidname[8];

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    sc = device_get_softc(dev);
    sc->tz_dev = dev;
    sc->tz_handle = acpi_get_handle(dev);
    sc->tz_requested = TZ_ACTIVE_NONE;
    sc->tz_active = TZ_ACTIVE_UNKNOWN;
    sc->tz_thflags = TZ_THFLAG_NONE;
    sc->tz_cooling_proc = NULL;
    sc->tz_cooling_proc_running = FALSE;
    sc->tz_cooling_active = FALSE;
    sc->tz_cooling_updated = FALSE;
    sc->tz_cooling_enabled = FALSE;

    /*
     * Parse the current state of the thermal zone and build control
     * structures.  We don't need to worry about interference with the
     * control thread since we haven't fully attached this device yet.
     */
    if ((error = acpi_tz_establish(sc)) != 0)
	return (error);

    /*
     * Register for any Notify events sent to this zone.
     */
    AcpiInstallNotifyHandler(sc->tz_handle, ACPI_DEVICE_NOTIFY,
			     acpi_tz_notify_handler, sc);

    /*
     * Create our sysctl nodes.
     *
     * XXX we need a mechanism for adding nodes under ACPI.
     */
    if (device_get_unit(dev) == 0) {
	acpi_sc = acpi_device_get_parent_softc(dev);
	sysctl_ctx_init(&acpi_tz_sysctl_ctx);
	acpi_tz_sysctl_tree = SYSCTL_ADD_NODE(&acpi_tz_sysctl_ctx,
			      SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree),
			      OID_AUTO, "thermal", CTLFLAG_RD, 0, "");
	SYSCTL_ADD_INT(&acpi_tz_sysctl_ctx,
		       SYSCTL_CHILDREN(acpi_tz_sysctl_tree),
		       OID_AUTO, "min_runtime", CTLFLAG_RW,
		       &acpi_tz_min_runtime, 0,
		       "minimum cooling run time in sec");
	SYSCTL_ADD_INT(&acpi_tz_sysctl_ctx,
		       SYSCTL_CHILDREN(acpi_tz_sysctl_tree),
		       OID_AUTO, "polling_rate", CTLFLAG_RW,
		       &acpi_tz_polling_rate, 0, "monitor polling interval in seconds");
	SYSCTL_ADD_INT(&acpi_tz_sysctl_ctx,
		       SYSCTL_CHILDREN(acpi_tz_sysctl_tree), OID_AUTO,
		       "user_override", CTLFLAG_RW, &acpi_tz_override, 0,
		       "allow override of thermal settings");
    }
    sysctl_ctx_init(&sc->tz_sysctl_ctx);
    sprintf(oidname, "tz%d", device_get_unit(dev));
    sc->tz_sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&sc->tz_sysctl_ctx,
			 SYSCTL_CHILDREN(acpi_tz_sysctl_tree),
			 OID_AUTO, oidname, CTLFLAG_RD, 0, "", "thermal_zone");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD,
		    &sc->tz_temperature, 0, sysctl_handle_int,
		    "IK", "current thermal zone temperature");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "active", CTLTYPE_INT | CTLFLAG_RW,
		    sc, 0, acpi_tz_active_sysctl, "I", "cooling is active");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "passive_cooling", CTLTYPE_INT | CTLFLAG_RW,
		    sc, 0, acpi_tz_cooling_sysctl, "I",
		    "enable passive (speed reduction) cooling");

    SYSCTL_ADD_INT(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		   OID_AUTO, "thermal_flags", CTLFLAG_RD,
		   &sc->tz_thflags, 0, "thermal zone flags");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_PSV", CTLTYPE_INT | CTLFLAG_RW,
		    sc, offsetof(struct acpi_tz_softc, tz_zone.psv),
		    acpi_tz_temp_sysctl, "IK", "passive cooling temp setpoint");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_HOT", CTLTYPE_INT | CTLFLAG_RW,
		    sc, offsetof(struct acpi_tz_softc, tz_zone.hot),
		    acpi_tz_temp_sysctl, "IK",
		    "too hot temp setpoint (suspend now)");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_CRT", CTLTYPE_INT | CTLFLAG_RW,
		    sc, offsetof(struct acpi_tz_softc, tz_zone.crt),
		    acpi_tz_temp_sysctl, "IK",
		    "critical temp setpoint (shutdown now)");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_ACx", CTLTYPE_INT | CTLFLAG_RD,
		    &sc->tz_zone.ac, sizeof(sc->tz_zone.ac),
		    sysctl_handle_opaque, "IK", "");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_TC1", CTLTYPE_INT | CTLFLAG_RW,
		    sc, offsetof(struct acpi_tz_softc, tz_zone.tc1),
		    acpi_tz_passive_sysctl, "I",
		    "thermal constant 1 for passive cooling");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_TC2", CTLTYPE_INT | CTLFLAG_RW,
		    sc, offsetof(struct acpi_tz_softc, tz_zone.tc2),
		    acpi_tz_passive_sysctl, "I",
		    "thermal constant 2 for passive cooling");
    SYSCTL_ADD_PROC(&sc->tz_sysctl_ctx, SYSCTL_CHILDREN(sc->tz_sysctl_tree),
		    OID_AUTO, "_TSP", CTLTYPE_INT | CTLFLAG_RW,
		    sc, offsetof(struct acpi_tz_softc, tz_zone.tsp),
		    acpi_tz_passive_sysctl, "I",
		    "thermal sampling period for passive cooling");

    /*
     * Register our power profile event handler.
     */
    sc->tz_event = EVENTHANDLER_REGISTER(power_profile_change,
	acpi_tz_power_profile, sc, 0);

    /*
     * Flag the event handler for a manual invocation by our timeout.
     * We defer it like this so that the rest of the subsystem has time
     * to come up.  Don't bother evaluating/printing the temperature at
     * this point; on many systems it'll be bogus until the EC is running.
     */
    sc->tz_flags |= TZ_FLAG_GETPROFILE;

    return_VALUE (0);
}

static void
acpi_tz_startup(void *arg __unused)
{
    struct acpi_tz_softc *sc;
    device_t *devs;
    int devcount, error, i;

    devclass_get_devices(acpi_tz_devclass, &devs, &devcount);
    if (devcount == 0) {
	free(devs, M_TEMP);
	return;
    }

    /*
     * Create thread to service all of the thermal zones.
     */
    error = kproc_create(acpi_tz_thread, NULL, &acpi_tz_proc, RFHIGHPID, 0,
	"acpi_thermal");
    if (error != 0)
	printf("acpi_tz: could not create thread - %d", error);

    /*
     * Create a thread to handle passive cooling for 1st zone which
     * has _PSV, _TSP, _TC1 and _TC2.  Users can enable it for other
     * zones manually for now.
     *
     * XXX We enable only one zone to avoid multiple zones conflict
     * with each other since cpufreq currently sets all CPUs to the
     * given frequency whereas it's possible for different thermal
     * zones to specify independent settings for multiple CPUs.
     */
    for (i = 0; i < devcount; i++) {
	sc = device_get_softc(devs[i]);
	if (acpi_tz_cooling_is_available(sc)) {
	    sc->tz_cooling_enabled = TRUE;
	    error = acpi_tz_cooling_thread_start(sc);
	    if (error != 0) {
		sc->tz_cooling_enabled = FALSE;
		break;
	    }
	    acpi_tz_cooling_unit = device_get_unit(devs[i]);
	    break;
	}
    }
    free(devs, M_TEMP);
}
SYSINIT(acpi_tz, SI_SUB_KICK_SCHEDULER, SI_ORDER_ANY, acpi_tz_startup, NULL);

/*
 * Parse the current state of this thermal zone and set up to use it.
 *
 * Note that we may have previous state, which will have to be discarded.
 */
static int
acpi_tz_establish(struct acpi_tz_softc *sc)
{
    ACPI_OBJECT	*obj;
    int		i;
    char	nbuf[8];

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /* Erase any existing state. */
    for (i = 0; i < TZ_NUMLEVELS; i++)
	if (sc->tz_zone.al[i].Pointer != NULL)
	    AcpiOsFree(sc->tz_zone.al[i].Pointer);
    if (sc->tz_zone.psl.Pointer != NULL)
	AcpiOsFree(sc->tz_zone.psl.Pointer);

    /*
     * XXX: We initialize only ACPI_BUFFER to avoid race condition
     * with passive cooling thread which refers psv, tc1, tc2 and tsp.
     */
    bzero(sc->tz_zone.ac, sizeof(sc->tz_zone.ac));
    bzero(sc->tz_zone.al, sizeof(sc->tz_zone.al));
    bzero(&sc->tz_zone.psl, sizeof(sc->tz_zone.psl));

    /* Evaluate thermal zone parameters. */
    for (i = 0; i < TZ_NUMLEVELS; i++) {
	sprintf(nbuf, "_AC%d", i);
	acpi_tz_getparam(sc, nbuf, &sc->tz_zone.ac[i]);
	sprintf(nbuf, "_AL%d", i);
	sc->tz_zone.al[i].Length = ACPI_ALLOCATE_BUFFER;
	sc->tz_zone.al[i].Pointer = NULL;
	AcpiEvaluateObject(sc->tz_handle, nbuf, NULL, &sc->tz_zone.al[i]);
	obj = (ACPI_OBJECT *)sc->tz_zone.al[i].Pointer;
	if (obj != NULL) {
	    /* Should be a package containing a list of power objects */
	    if (obj->Type != ACPI_TYPE_PACKAGE) {
		device_printf(sc->tz_dev, "%s has unknown type %d, rejecting\n",
			      nbuf, obj->Type);
		return_VALUE (ENXIO);
	    }
	}
    }
    acpi_tz_getparam(sc, "_CRT", &sc->tz_zone.crt);
    acpi_tz_getparam(sc, "_HOT", &sc->tz_zone.hot);
    sc->tz_zone.psl.Length = ACPI_ALLOCATE_BUFFER;
    sc->tz_zone.psl.Pointer = NULL;
    AcpiEvaluateObject(sc->tz_handle, "_PSL", NULL, &sc->tz_zone.psl);
    acpi_tz_getparam(sc, "_PSV", &sc->tz_zone.psv);
    acpi_tz_getparam(sc, "_TC1", &sc->tz_zone.tc1);
    acpi_tz_getparam(sc, "_TC2", &sc->tz_zone.tc2);
    acpi_tz_getparam(sc, "_TSP", &sc->tz_zone.tsp);
    acpi_tz_getparam(sc, "_TZP", &sc->tz_zone.tzp);

    /*
     * Sanity-check the values we've been given.
     *
     * XXX what do we do about systems that give us the same value for
     *     more than one of these setpoints?
     */
    acpi_tz_sanity(sc, &sc->tz_zone.crt, "_CRT");
    acpi_tz_sanity(sc, &sc->tz_zone.hot, "_HOT");
    acpi_tz_sanity(sc, &sc->tz_zone.psv, "_PSV");
    for (i = 0; i < TZ_NUMLEVELS; i++)
	acpi_tz_sanity(sc, &sc->tz_zone.ac[i], "_ACx");

    return_VALUE (0);
}

static char *aclevel_string[] = {
    "NONE", "_AC0", "_AC1", "_AC2", "_AC3", "_AC4",
    "_AC5", "_AC6", "_AC7", "_AC8", "_AC9"
};

static __inline const char *
acpi_tz_aclevel_string(int active)
{
    if (active < -1 || active >= TZ_NUMLEVELS)
	return (aclevel_string[0]);

    return (aclevel_string[active + 1]);
}

/*
 * Get the current temperature.
 */
static int
acpi_tz_get_temperature(struct acpi_tz_softc *sc)
{
    int		temp;
    ACPI_STATUS	status;

    ACPI_FUNCTION_NAME ("acpi_tz_get_temperature");

    /* Evaluate the thermal zone's _TMP method. */
    status = acpi_GetInteger(sc->tz_handle, acpi_tz_tmp_name, &temp);
    if (ACPI_FAILURE(status)) {
	ACPI_VPRINT(sc->tz_dev, acpi_device_get_parent_softc(sc->tz_dev),
	    "error fetching current temperature -- %s\n",
	     AcpiFormatException(status));
	return (FALSE);
    }

    /* Check it for validity. */
    acpi_tz_sanity(sc, &temp, acpi_tz_tmp_name);
    if (temp == -1)
	return (FALSE);

    ACPI_DEBUG_PRINT((ACPI_DB_VALUES, "got %d.%dC\n", TZ_KELVTOC(temp)));
    sc->tz_temperature = temp;
    return (TRUE);
}

/*
 * Evaluate the condition of a thermal zone, take appropriate actions.
 */
static void
acpi_tz_monitor(void *Context)
{
    struct acpi_tz_softc *sc;
    struct	timespec curtime;
    int		temp;
    int		i;
    int		newactive, newflags;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    sc = (struct acpi_tz_softc *)Context;

    /* Get the current temperature. */
    if (!acpi_tz_get_temperature(sc)) {
	/* XXX disable zone? go to max cooling? */
	return_VOID;
    }
    temp = sc->tz_temperature;

    /*
     * Work out what we ought to be doing right now.
     *
     * Note that the _ACx levels sort from hot to cold.
     */
    newactive = TZ_ACTIVE_NONE;
    for (i = TZ_NUMLEVELS - 1; i >= 0; i--) {
	if (sc->tz_zone.ac[i] != -1 && temp >= sc->tz_zone.ac[i])
	    newactive = i;
    }

    /*
     * We are going to get _ACx level down (colder side), but give a guaranteed
     * minimum cooling run time if requested.
     */
    if (acpi_tz_min_runtime > 0 && sc->tz_active != TZ_ACTIVE_NONE &&
	sc->tz_active != TZ_ACTIVE_UNKNOWN &&
	(newactive == TZ_ACTIVE_NONE || newactive > sc->tz_active)) {

	getnanotime(&curtime);
	timespecsub(&curtime, &sc->tz_cooling_started, &curtime);
	if (curtime.tv_sec < acpi_tz_min_runtime)
	    newactive = sc->tz_active;
    }

    /* Handle user override of active mode */
    if (sc->tz_requested != TZ_ACTIVE_NONE && (newactive == TZ_ACTIVE_NONE
        || sc->tz_requested < newactive))
	newactive = sc->tz_requested;

    /* update temperature-related flags */
    newflags = TZ_THFLAG_NONE;
    if (sc->tz_zone.psv != -1 && temp >= sc->tz_zone.psv)
	newflags |= TZ_THFLAG_PSV;
    if (sc->tz_zone.hot != -1 && temp >= sc->tz_zone.hot)
	newflags |= TZ_THFLAG_HOT;
    if (sc->tz_zone.crt != -1 && temp >= sc->tz_zone.crt)
	newflags |= TZ_THFLAG_CRT;

    /* If the active cooling state has changed, we have to switch things. */
    if (sc->tz_active == TZ_ACTIVE_UNKNOWN) {
	/*
	 * We don't know which cooling device is on or off,
	 * so stop them all, because we now know which
	 * should be on (if any).
	 */
	for (i = 0; i < TZ_NUMLEVELS; i++) {
	    if (sc->tz_zone.al[i].Pointer != NULL) {
		acpi_ForeachPackageObject(
		    (ACPI_OBJECT *)sc->tz_zone.al[i].Pointer,
		    acpi_tz_switch_cooler_off, sc);
	    }
	}
	/* now we know that all devices are off */
	sc->tz_active = TZ_ACTIVE_NONE;
    }

    if (newactive != sc->tz_active) {
	/* Turn off unneeded cooling devices that are on, if any are */
	for (i = TZ_ACTIVE_LEVEL(sc->tz_active);
	     i < TZ_ACTIVE_LEVEL(newactive); i++) {
	    acpi_ForeachPackageObject(
		(ACPI_OBJECT *)sc->tz_zone.al[i].Pointer,
		acpi_tz_switch_cooler_off, sc);
	}
	/* Turn on cooling devices that are required, if any are */
	for (i = TZ_ACTIVE_LEVEL(sc->tz_active) - 1;
	     i >= TZ_ACTIVE_LEVEL(newactive); i--) {
	    acpi_ForeachPackageObject(
		(ACPI_OBJECT *)sc->tz_zone.al[i].Pointer,
		acpi_tz_switch_cooler_on, sc);
	}

	ACPI_VPRINT(sc->tz_dev, acpi_device_get_parent_softc(sc->tz_dev),
		    "switched from %s to %s: %d.%dC\n",
		    acpi_tz_aclevel_string(sc->tz_active),
		    acpi_tz_aclevel_string(newactive), TZ_KELVTOC(temp));
	sc->tz_active = newactive;
	getnanotime(&sc->tz_cooling_started);
    }

    /* XXX (de)activate any passive cooling that may be required. */

    /*
     * If the temperature is at _HOT or _CRT, increment our event count.
     * If it has occurred enough times, shutdown the system.  This is
     * needed because some systems will report an invalid high temperature
     * for one poll cycle.  It is suspected this is due to the embedded
     * controller timing out.  A typical value is 138C for one cycle on
     * a system that is otherwise 65C.
     *
     * If we're almost at that threshold, notify the user through devd(8).
     */
    if ((newflags & (TZ_THFLAG_HOT | TZ_THFLAG_CRT)) != 0) {
	sc->tz_validchecks++;
	if (sc->tz_validchecks == TZ_VALIDCHECKS) {
	    device_printf(sc->tz_dev,
		"WARNING - current temperature (%d.%dC) exceeds safe limits\n",
		TZ_KELVTOC(sc->tz_temperature));
	    shutdown_nice(RB_POWEROFF);
	} else if (sc->tz_validchecks == TZ_NOTIFYCOUNT)
	    acpi_UserNotify("Thermal", sc->tz_handle, TZ_NOTIFY_CRITICAL);
    } else {
	sc->tz_validchecks = 0;
    }
    sc->tz_thflags = newflags;

    return_VOID;
}

/*
 * Given an object, verify that it's a reference to a device of some sort,
 * and try to switch it off.
 */
static void
acpi_tz_switch_cooler_off(ACPI_OBJECT *obj, void *arg)
{
    ACPI_HANDLE			cooler;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    cooler = acpi_GetReference(NULL, obj);
    if (cooler == NULL) {
	ACPI_DEBUG_PRINT((ACPI_DB_OBJECTS, "can't get handle\n"));
	return_VOID;
    }

    ACPI_DEBUG_PRINT((ACPI_DB_OBJECTS, "called to turn %s off\n",
		     acpi_name(cooler)));
    acpi_pwr_switch_consumer(cooler, ACPI_STATE_D3);

    return_VOID;
}

/*
 * Given an object, verify that it's a reference to a device of some sort,
 * and try to switch it on.
 *
 * XXX replication of off/on function code is bad.
 */
static void
acpi_tz_switch_cooler_on(ACPI_OBJECT *obj, void *arg)
{
    struct acpi_tz_softc	*sc = (struct acpi_tz_softc *)arg;
    ACPI_HANDLE			cooler;
    ACPI_STATUS			status;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    cooler = acpi_GetReference(NULL, obj);
    if (cooler == NULL) {
	ACPI_DEBUG_PRINT((ACPI_DB_OBJECTS, "can't get handle\n"));
	return_VOID;
    }

    ACPI_DEBUG_PRINT((ACPI_DB_OBJECTS, "called to turn %s on\n",
		     acpi_name(cooler)));
    status = acpi_pwr_switch_consumer(cooler, ACPI_STATE_D0);
    if (ACPI_FAILURE(status)) {
	ACPI_VPRINT(sc->tz_dev, acpi_device_get_parent_softc(sc->tz_dev),
		    "failed to activate %s - %s\n", acpi_name(cooler),
		    AcpiFormatException(status));
    }

    return_VOID;
}

/*
 * Read/debug-print a parameter, default it to -1.
 */
static void
acpi_tz_getparam(struct acpi_tz_softc *sc, char *node, int *data)
{

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (ACPI_FAILURE(acpi_GetInteger(sc->tz_handle, node, data))) {
	*data = -1;
    } else {
	ACPI_DEBUG_PRINT((ACPI_DB_VALUES, "%s.%s = %d\n",
			 acpi_name(sc->tz_handle), node, *data));
    }

    return_VOID;
}

/*
 * Sanity-check a temperature value.  Assume that setpoints
 * should be between 0C and 200C.
 */
static void
acpi_tz_sanity(struct acpi_tz_softc *sc, int *val, char *what)
{
    if (*val != -1 && (*val < TZ_ZEROC || *val > TZ_ZEROC + 2000)) {
	/*
	 * If the value we are checking is _TMP, warn the user only
	 * once. This avoids spamming messages if, for instance, the
	 * sensor is broken and always returns an invalid temperature.
	 *
	 * This is only done for _TMP; other values always emit a
	 * warning.
	 */
	if (what != acpi_tz_tmp_name || !sc->tz_insane_tmp_notified) {
	    device_printf(sc->tz_dev, "%s value is absurd, ignored (%d.%dC)\n",
			  what, TZ_KELVTOC(*val));

	    /* Don't warn the user again if the read value doesn't improve. */
	    if (what == acpi_tz_tmp_name)
		sc->tz_insane_tmp_notified = 1;
	}
	*val = -1;
	return;
    }

    /* This value is correct. Warn if it's incorrect again. */
    if (what == acpi_tz_tmp_name)
	sc->tz_insane_tmp_notified = 0;
}

/*
 * Respond to a sysctl on the active state node.
 */
static int
acpi_tz_active_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_tz_softc	*sc;
    int				active;
    int		 		error;

    sc = (struct acpi_tz_softc *)oidp->oid_arg1;
    active = sc->tz_active;
    error = sysctl_handle_int(oidp, &active, 0, req);

    /* Error or no new value */
    if (error != 0 || req->newptr == NULL)
	return (error);
    if (active < -1 || active >= TZ_NUMLEVELS)
	return (EINVAL);

    /* Set new preferred level and re-switch */
    sc->tz_requested = active;
    acpi_tz_signal(sc, 0);
    return (0);
}

static int
acpi_tz_cooling_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_tz_softc *sc;
    int enabled, error;

    sc = (struct acpi_tz_softc *)oidp->oid_arg1;
    enabled = sc->tz_cooling_enabled;
    error = sysctl_handle_int(oidp, &enabled, 0, req);

    /* Error or no new value */
    if (error != 0 || req->newptr == NULL)
	return (error);
    if (enabled != TRUE && enabled != FALSE)
	return (EINVAL);

    if (enabled) {
	if (acpi_tz_cooling_is_available(sc))
	    error = acpi_tz_cooling_thread_start(sc);
	else
	    error = ENODEV;
	if (error)
	    enabled = FALSE;
    }
    sc->tz_cooling_enabled = enabled;
    return (error);
}

static int
acpi_tz_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_tz_softc	*sc;
    int				temp, *temp_ptr;
    int		 		error;

    sc = oidp->oid_arg1;
    temp_ptr = (int *)(void *)(uintptr_t)((uintptr_t)sc + oidp->oid_arg2);
    temp = *temp_ptr;
    error = sysctl_handle_int(oidp, &temp, 0, req);

    /* Error or no new value */
    if (error != 0 || req->newptr == NULL)
	return (error);

    /* Only allow changing settings if override is set. */
    if (!acpi_tz_override)
	return (EPERM);

    /* Check user-supplied value for sanity. */
    acpi_tz_sanity(sc, &temp, "user-supplied temp");
    if (temp == -1)
	return (EINVAL);

    *temp_ptr = temp;
    return (0);
}

static int
acpi_tz_passive_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_tz_softc	*sc;
    int				val, *val_ptr;
    int				error;

    sc = oidp->oid_arg1;
    val_ptr = (int *)(void *)(uintptr_t)((uintptr_t)sc + oidp->oid_arg2);
    val = *val_ptr;
    error = sysctl_handle_int(oidp, &val, 0, req);

    /* Error or no new value */
    if (error != 0 || req->newptr == NULL)
	return (error);

    /* Only allow changing settings if override is set. */
    if (!acpi_tz_override)
	return (EPERM);

    *val_ptr = val;
    return (0);
}

static void
acpi_tz_notify_handler(ACPI_HANDLE h, UINT32 notify, void *context)
{
    struct acpi_tz_softc	*sc = (struct acpi_tz_softc *)context;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    switch (notify) {
    case TZ_NOTIFY_TEMPERATURE:
	/* Temperature change occurred */
	acpi_tz_signal(sc, 0);
	break;
    case TZ_NOTIFY_DEVICES:
    case TZ_NOTIFY_LEVELS:
	/* Zone devices/setpoints changed */
	acpi_tz_signal(sc, TZ_FLAG_GETSETTINGS);
	break;
    default:
	ACPI_VPRINT(sc->tz_dev, acpi_device_get_parent_softc(sc->tz_dev),
		    "unknown Notify event 0x%x\n", notify);
	break;
    }

    acpi_UserNotify("Thermal", h, notify);

    return_VOID;
}

static void
acpi_tz_signal(struct acpi_tz_softc *sc, int flags)
{
    ACPI_LOCK(thermal);
    sc->tz_flags |= flags;
    ACPI_UNLOCK(thermal);
    wakeup(&acpi_tz_proc);
}

/*
 * Notifies can be generated asynchronously but have also been seen to be
 * triggered by other thermal methods.  One system generates a notify of
 * 0x81 when the fan is turned on or off.  Another generates it when _SCP
 * is called.  To handle these situations, we check the zone via
 * acpi_tz_monitor() before evaluating changes to setpoints or the cooling
 * policy.
 */
static void
acpi_tz_timeout(struct acpi_tz_softc *sc, int flags)
{

    /* Check the current temperature and take action based on it */
    acpi_tz_monitor(sc);

    /* If requested, get the power profile settings. */
    if (flags & TZ_FLAG_GETPROFILE)
	acpi_tz_power_profile(sc);

    /*
     * If requested, check for new devices/setpoints.  After finding them,
     * check if we need to switch fans based on the new values.
     */
    if (flags & TZ_FLAG_GETSETTINGS) {
	acpi_tz_establish(sc);
	acpi_tz_monitor(sc);
    }

    /* XXX passive cooling actions? */
}

/*
 * System power profile may have changed; fetch and notify the
 * thermal zone accordingly.
 *
 * Since this can be called from an arbitrary eventhandler, it needs
 * to get the ACPI lock itself.
 */
static void
acpi_tz_power_profile(void *arg)
{
    ACPI_STATUS			status;
    struct acpi_tz_softc	*sc = (struct acpi_tz_softc *)arg;
    int				state;

    state = power_profile_get_state();
    if (state != POWER_PROFILE_PERFORMANCE && state != POWER_PROFILE_ECONOMY)
	return;

    /* check that we haven't decided there's no _SCP method */
    if ((sc->tz_flags & TZ_FLAG_NO_SCP) == 0) {

	/* Call _SCP to set the new profile */
	status = acpi_SetInteger(sc->tz_handle, "_SCP",
	    (state == POWER_PROFILE_PERFORMANCE) ? 0 : 1);
	if (ACPI_FAILURE(status)) {
	    if (status != AE_NOT_FOUND)
		ACPI_VPRINT(sc->tz_dev,
			    acpi_device_get_parent_softc(sc->tz_dev),
			    "can't evaluate %s._SCP - %s\n",
			    acpi_name(sc->tz_handle),
			    AcpiFormatException(status));
	    sc->tz_flags |= TZ_FLAG_NO_SCP;
	} else {
	    /* We have to re-evaluate the entire zone now */
	    acpi_tz_signal(sc, TZ_FLAG_GETSETTINGS);
	}
    }
}

/*
 * Thermal zone monitor thread.
 */
static void
acpi_tz_thread(void *arg)
{
    device_t	*devs;
    int		devcount, i;
    int		flags;
    struct acpi_tz_softc **sc;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    devs = NULL;
    devcount = 0;
    sc = NULL;

    for (;;) {
	/* If the number of devices has changed, re-evaluate. */
	if (devclass_get_count(acpi_tz_devclass) != devcount) {
	    if (devs != NULL) {
		free(devs, M_TEMP);
		free(sc, M_TEMP);
	    }
	    devclass_get_devices(acpi_tz_devclass, &devs, &devcount);
	    sc = malloc(sizeof(struct acpi_tz_softc *) * devcount, M_TEMP,
			M_WAITOK | M_ZERO);
	    for (i = 0; i < devcount; i++)
		sc[i] = device_get_softc(devs[i]);
	}

	/* Check for temperature events and act on them. */
	for (i = 0; i < devcount; i++) {
	    ACPI_LOCK(thermal);
	    flags = sc[i]->tz_flags;
	    sc[i]->tz_flags &= TZ_FLAG_NO_SCP;
	    ACPI_UNLOCK(thermal);
	    acpi_tz_timeout(sc[i], flags);
	}

	/* If more work to do, don't go to sleep yet. */
	ACPI_LOCK(thermal);
	for (i = 0; i < devcount; i++) {
	    if (sc[i]->tz_flags & ~TZ_FLAG_NO_SCP)
		break;
	}

	/*
	 * If we have no more work, sleep for a while, setting PDROP so that
	 * the mutex will not be reacquired.  Otherwise, drop the mutex and
	 * loop to handle more events.
	 */
	if (i == devcount)
	    msleep(&acpi_tz_proc, &thermal_mutex, PZERO | PDROP, "tzpoll",
		hz * acpi_tz_polling_rate);
	else
	    ACPI_UNLOCK(thermal);
    }
}

static int
acpi_tz_cpufreq_restore(struct acpi_tz_softc *sc)
{
    device_t dev;
    int error;

    if (!sc->tz_cooling_updated)
	return (0);
    if ((dev = devclass_get_device(devclass_find("cpufreq"), 0)) == NULL)
	return (ENXIO);
    ACPI_VPRINT(sc->tz_dev, acpi_device_get_parent_softc(sc->tz_dev),
	"temperature %d.%dC: resuming previous clock speed (%d MHz)\n",
	TZ_KELVTOC(sc->tz_temperature), sc->tz_cooling_saved_freq);
    error = CPUFREQ_SET(dev, NULL, CPUFREQ_PRIO_KERN);
    if (error == 0)
	sc->tz_cooling_updated = FALSE;
    return (error);
}

static int
acpi_tz_cpufreq_update(struct acpi_tz_softc *sc, int req)
{
    device_t dev;
    struct cf_level *levels;
    int num_levels, error, freq, desired_freq, perf, i;

    levels = malloc(CPUFREQ_MAX_LEVELS * sizeof(*levels), M_TEMP, M_NOWAIT);
    if (levels == NULL)
	return (ENOMEM);

    /*
     * Find the main device, cpufreq0.  We don't yet support independent
     * CPU frequency control on SMP.
     */
    if ((dev = devclass_get_device(devclass_find("cpufreq"), 0)) == NULL) {
	error = ENXIO;
	goto out;
    }

    /* Get the current frequency. */
    error = CPUFREQ_GET(dev, &levels[0]);
    if (error)
	goto out;
    freq = levels[0].total_set.freq;

    /* Get the current available frequency levels. */
    num_levels = CPUFREQ_MAX_LEVELS;
    error = CPUFREQ_LEVELS(dev, levels, &num_levels);
    if (error) {
	if (error == E2BIG)
	    printf("cpufreq: need to increase CPUFREQ_MAX_LEVELS\n");
	goto out;
    }

    /* Calculate the desired frequency as a percent of the max frequency. */
    perf = 100 * freq / levels[0].total_set.freq - req;
    if (perf < 0)
	perf = 0;
    else if (perf > 100)
	perf = 100;
    desired_freq = levels[0].total_set.freq * perf / 100;

    if (desired_freq < freq) {
	/* Find the closest available frequency, rounding down. */
	for (i = 0; i < num_levels; i++)
	    if (levels[i].total_set.freq <= desired_freq)
		break;

	/* If we didn't find a relevant setting, use the lowest. */
	if (i == num_levels)
	    i--;
    } else {
	/* If we didn't decrease frequency yet, don't increase it. */
	if (!sc->tz_cooling_updated) {
	    sc->tz_cooling_active = FALSE;
	    goto out;
	}

	/* Use saved cpu frequency as maximum value. */
	if (desired_freq > sc->tz_cooling_saved_freq)
	    desired_freq = sc->tz_cooling_saved_freq;

	/* Find the closest available frequency, rounding up. */
	for (i = num_levels - 1; i >= 0; i--)
	    if (levels[i].total_set.freq >= desired_freq)
		break;

	/* If we didn't find a relevant setting, use the highest. */
	if (i == -1)
	    i++;

	/* If we're going to the highest frequency, restore the old setting. */
	if (i == 0 || desired_freq == sc->tz_cooling_saved_freq) {
	    error = acpi_tz_cpufreq_restore(sc);
	    if (error == 0)
		sc->tz_cooling_active = FALSE;
	    goto out;
	}
    }

    /* If we are going to a new frequency, activate it. */
    if (levels[i].total_set.freq != freq) {
	ACPI_VPRINT(sc->tz_dev, acpi_device_get_parent_softc(sc->tz_dev),
	    "temperature %d.%dC: %screasing clock speed "
	    "from %d MHz to %d MHz\n",
	    TZ_KELVTOC(sc->tz_temperature),
	    (freq > levels[i].total_set.freq) ? "de" : "in",
	    freq, levels[i].total_set.freq);
	error = CPUFREQ_SET(dev, &levels[i], CPUFREQ_PRIO_KERN);
	if (error == 0 && !sc->tz_cooling_updated) {
	    sc->tz_cooling_saved_freq = freq;
	    sc->tz_cooling_updated = TRUE;
	}
    }

out:
    if (levels)
	free(levels, M_TEMP);
    return (error);
}

/*
 * Passive cooling thread; monitors current temperature according to the
 * cooling interval and calculates whether to scale back CPU frequency.
 */
static void
acpi_tz_cooling_thread(void *arg)
{
    struct acpi_tz_softc *sc;
    int error, perf, curr_temp, prev_temp;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    sc = (struct acpi_tz_softc *)arg;

    prev_temp = sc->tz_temperature;
    while (sc->tz_cooling_enabled) {
	if (sc->tz_cooling_active)
	    (void)acpi_tz_get_temperature(sc);
	curr_temp = sc->tz_temperature;
	if (curr_temp >= sc->tz_zone.psv)
	    sc->tz_cooling_active = TRUE;
	if (sc->tz_cooling_active) {
	    perf = sc->tz_zone.tc1 * (curr_temp - prev_temp) +
		   sc->tz_zone.tc2 * (curr_temp - sc->tz_zone.psv);
	    perf /= 10;

	    if (perf != 0) {
		error = acpi_tz_cpufreq_update(sc, perf);

		/*
		 * If error and not simply a higher priority setting was
		 * active, disable cooling.
		 */
		if (error != 0 && error != EPERM) {
		    device_printf(sc->tz_dev,
			"failed to set new freq, disabling passive cooling\n");
		    sc->tz_cooling_enabled = FALSE;
		}
	    }
	}
	prev_temp = curr_temp;
	tsleep(&sc->tz_cooling_proc, PZERO, "cooling",
	    hz * sc->tz_zone.tsp / 10);
    }
    if (sc->tz_cooling_active) {
	acpi_tz_cpufreq_restore(sc);
	sc->tz_cooling_active = FALSE;
    }
    sc->tz_cooling_proc = NULL;
    ACPI_LOCK(thermal);
    sc->tz_cooling_proc_running = FALSE;
    ACPI_UNLOCK(thermal);
    kproc_exit(0);
}

/*
 * TODO: We ignore _PSL (list of cooling devices) since cpufreq enumerates
 * all CPUs for us.  However, it's possible in the future _PSL will
 * reference non-CPU devices so we may want to support it then.
 */
static int
acpi_tz_cooling_is_available(struct acpi_tz_softc *sc)
{
    return (sc->tz_zone.tc1 != -1 && sc->tz_zone.tc2 != -1 &&
	sc->tz_zone.tsp != -1 && sc->tz_zone.tsp != 0 &&
	sc->tz_zone.psv != -1);
}

static int
acpi_tz_cooling_thread_start(struct acpi_tz_softc *sc)
{
    int error;

    ACPI_LOCK(thermal);
    if (sc->tz_cooling_proc_running) {
	ACPI_UNLOCK(thermal);
	return (0);
    }
    sc->tz_cooling_proc_running = TRUE;
    ACPI_UNLOCK(thermal);
    error = 0;
    if (sc->tz_cooling_proc == NULL) {
	error = kproc_create(acpi_tz_cooling_thread, sc,
	    &sc->tz_cooling_proc, RFHIGHPID, 0, "acpi_cooling%d",
	    device_get_unit(sc->tz_dev));
	if (error != 0) {
	    device_printf(sc->tz_dev, "could not create thread - %d", error);
	    ACPI_LOCK(thermal);
	    sc->tz_cooling_proc_running = FALSE;
	    ACPI_UNLOCK(thermal);
	}
    }
    return (error);
}
