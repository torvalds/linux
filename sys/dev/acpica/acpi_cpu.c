/*-
 * Copyright (c) 2003-2005 Nate Lawson (SDG)
 * Copyright (c) 2001 Michael Smith
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
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pcpu.h>
#include <sys/power.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sbuf.h>
#include <sys/smp.h>

#include <dev/pci/pcivar.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#if defined(__amd64__) || defined(__i386__)
#include <machine/clock.h>
#include <machine/specialreg.h>
#include <machine/md_var.h>
#endif
#include <sys/rman.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>

/*
 * Support for ACPI Processor devices, including C[1-3] sleep states.
 */

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_PROCESSOR
ACPI_MODULE_NAME("PROCESSOR")

struct acpi_cx {
    struct resource	*p_lvlx;	/* Register to read to enter state. */
    uint32_t		 type;		/* C1-3 (C4 and up treated as C3). */
    uint32_t		 trans_lat;	/* Transition latency (usec). */
    uint32_t		 power;		/* Power consumed (mW). */
    int			 res_type;	/* Resource type for p_lvlx. */
    int			 res_rid;	/* Resource ID for p_lvlx. */
    bool		 do_mwait;
    uint32_t		 mwait_hint;
    bool		 mwait_hw_coord;
    bool		 mwait_bm_avoidance;
};
#define MAX_CX_STATES	 8

struct acpi_cpu_softc {
    device_t		 cpu_dev;
    ACPI_HANDLE		 cpu_handle;
    struct pcpu		*cpu_pcpu;
    uint32_t		 cpu_acpi_id;	/* ACPI processor id */
    uint32_t		 cpu_p_blk;	/* ACPI P_BLK location */
    uint32_t		 cpu_p_blk_len;	/* P_BLK length (must be 6). */
    struct acpi_cx	 cpu_cx_states[MAX_CX_STATES];
    int			 cpu_cx_count;	/* Number of valid Cx states. */
    int			 cpu_prev_sleep;/* Last idle sleep duration. */
    int			 cpu_features;	/* Child driver supported features. */
    /* Runtime state. */
    int			 cpu_non_c2;	/* Index of lowest non-C2 state. */
    int			 cpu_non_c3;	/* Index of lowest non-C3 state. */
    u_int		 cpu_cx_stats[MAX_CX_STATES];/* Cx usage history. */
    /* Values for sysctl. */
    struct sysctl_ctx_list cpu_sysctl_ctx;
    struct sysctl_oid	*cpu_sysctl_tree;
    int			 cpu_cx_lowest;
    int			 cpu_cx_lowest_lim;
    int			 cpu_disable_idle; /* Disable entry to idle function */
    char 		 cpu_cx_supported[64];
};

struct acpi_cpu_device {
    struct resource_list	ad_rl;
};

#define CPU_GET_REG(reg, width) 					\
    (bus_space_read_ ## width(rman_get_bustag((reg)), 			\
		      rman_get_bushandle((reg)), 0))
#define CPU_SET_REG(reg, width, val)					\
    (bus_space_write_ ## width(rman_get_bustag((reg)), 			\
		       rman_get_bushandle((reg)), 0, (val)))

#define PM_USEC(x)	 ((x) >> 2)	/* ~4 clocks per usec (3.57955 Mhz) */

#define ACPI_NOTIFY_CX_STATES	0x81	/* _CST changed. */

#define CPU_QUIRK_NO_C3		(1<<0)	/* C3-type states are not usable. */
#define CPU_QUIRK_NO_BM_CTRL	(1<<2)	/* No bus mastering control. */

#define PCI_VENDOR_INTEL	0x8086
#define PCI_DEVICE_82371AB_3	0x7113	/* PIIX4 chipset for quirks. */
#define PCI_REVISION_A_STEP	0
#define PCI_REVISION_B_STEP	1
#define PCI_REVISION_4E		2
#define PCI_REVISION_4M		3
#define PIIX4_DEVACTB_REG	0x58
#define PIIX4_BRLD_EN_IRQ0	(1<<0)
#define PIIX4_BRLD_EN_IRQ	(1<<1)
#define PIIX4_BRLD_EN_IRQ8	(1<<5)
#define PIIX4_STOP_BREAK_MASK	(PIIX4_BRLD_EN_IRQ0 | PIIX4_BRLD_EN_IRQ | PIIX4_BRLD_EN_IRQ8)
#define PIIX4_PCNTRL_BST_EN	(1<<10)

#define	CST_FFH_VENDOR_INTEL	1
#define	CST_FFH_INTEL_CL_C1IO	1
#define	CST_FFH_INTEL_CL_MWAIT	2
#define	CST_FFH_MWAIT_HW_COORD	0x0001
#define	CST_FFH_MWAIT_BM_AVOID	0x0002

#define	CPUDEV_DEVICE_ID	"ACPI0007"

/* Allow users to ignore processor orders in MADT. */
static int cpu_unordered;
SYSCTL_INT(_debug_acpi, OID_AUTO, cpu_unordered, CTLFLAG_RDTUN,
    &cpu_unordered, 0,
    "Do not use the MADT to match ACPI Processor objects to CPUs.");

/* Knob to disable acpi_cpu devices */
bool acpi_cpu_disabled = false;

/* Platform hardware resource information. */
static uint32_t		 cpu_smi_cmd;	/* Value to write to SMI_CMD. */
static uint8_t		 cpu_cst_cnt;	/* Indicate we are _CST aware. */
static int		 cpu_quirks;	/* Indicate any hardware bugs. */

/* Values for sysctl. */
static struct sysctl_ctx_list cpu_sysctl_ctx;
static struct sysctl_oid *cpu_sysctl_tree;
static int		 cpu_cx_generic;
static int		 cpu_cx_lowest_lim;

static device_t		*cpu_devices;
static int		 cpu_ndevices;
static struct acpi_cpu_softc **cpu_softc;
ACPI_SERIAL_DECL(cpu, "ACPI CPU");

static int	acpi_cpu_probe(device_t dev);
static int	acpi_cpu_attach(device_t dev);
static int	acpi_cpu_suspend(device_t dev);
static int	acpi_cpu_resume(device_t dev);
static int	acpi_pcpu_get_id(device_t dev, uint32_t *acpi_id,
		    uint32_t *cpu_id);
static struct resource_list *acpi_cpu_get_rlist(device_t dev, device_t child);
static device_t	acpi_cpu_add_child(device_t dev, u_int order, const char *name,
		    int unit);
static int	acpi_cpu_read_ivar(device_t dev, device_t child, int index,
		    uintptr_t *result);
static int	acpi_cpu_shutdown(device_t dev);
static void	acpi_cpu_cx_probe(struct acpi_cpu_softc *sc);
static void	acpi_cpu_generic_cx_probe(struct acpi_cpu_softc *sc);
static int	acpi_cpu_cx_cst(struct acpi_cpu_softc *sc);
static void	acpi_cpu_startup(void *arg);
static void	acpi_cpu_startup_cx(struct acpi_cpu_softc *sc);
static void	acpi_cpu_cx_list(struct acpi_cpu_softc *sc);
#if defined(__i386__) || defined(__amd64__)
static void	acpi_cpu_idle(sbintime_t sbt);
#endif
static void	acpi_cpu_notify(ACPI_HANDLE h, UINT32 notify, void *context);
static void	acpi_cpu_quirks(void);
static void	acpi_cpu_quirks_piix4(void);
static int	acpi_cpu_usage_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cpu_usage_counters_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cpu_set_cx_lowest(struct acpi_cpu_softc *sc);
static int	acpi_cpu_cx_lowest_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_cpu_global_cx_lowest_sysctl(SYSCTL_HANDLER_ARGS);
#if defined(__i386__) || defined(__amd64__)
static int	acpi_cpu_method_sysctl(SYSCTL_HANDLER_ARGS);
#endif

static device_method_t acpi_cpu_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	acpi_cpu_probe),
    DEVMETHOD(device_attach,	acpi_cpu_attach),
    DEVMETHOD(device_detach,	bus_generic_detach),
    DEVMETHOD(device_shutdown,	acpi_cpu_shutdown),
    DEVMETHOD(device_suspend,	acpi_cpu_suspend),
    DEVMETHOD(device_resume,	acpi_cpu_resume),

    /* Bus interface */
    DEVMETHOD(bus_add_child,	acpi_cpu_add_child),
    DEVMETHOD(bus_read_ivar,	acpi_cpu_read_ivar),
    DEVMETHOD(bus_get_resource_list, acpi_cpu_get_rlist),
    DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
    DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
    DEVMETHOD(bus_alloc_resource, bus_generic_rl_alloc_resource),
    DEVMETHOD(bus_release_resource, bus_generic_rl_release_resource),
    DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr, bus_generic_teardown_intr),

    DEVMETHOD_END
};

static driver_t acpi_cpu_driver = {
    "cpu",
    acpi_cpu_methods,
    sizeof(struct acpi_cpu_softc),
};

static devclass_t acpi_cpu_devclass;
DRIVER_MODULE(cpu, acpi, acpi_cpu_driver, acpi_cpu_devclass, 0, 0);
MODULE_DEPEND(cpu, acpi, 1, 1, 1);

static int
acpi_cpu_probe(device_t dev)
{
    static char		   *cpudev_ids[] = { CPUDEV_DEVICE_ID, NULL };
    int			   acpi_id, cpu_id;
    ACPI_BUFFER		   buf;
    ACPI_HANDLE		   handle;
    ACPI_OBJECT		   *obj;
    ACPI_STATUS		   status;
    ACPI_OBJECT_TYPE	   type;

    if (acpi_disabled("cpu") || acpi_cpu_disabled)
	return (ENXIO);
    type = acpi_get_type(dev);
    if (type != ACPI_TYPE_PROCESSOR && type != ACPI_TYPE_DEVICE)
	return (ENXIO);
    if (type == ACPI_TYPE_DEVICE &&
	ACPI_ID_PROBE(device_get_parent(dev), dev, cpudev_ids, NULL) >= 0)
	return (ENXIO);

    handle = acpi_get_handle(dev);
    if (cpu_softc == NULL)
	cpu_softc = malloc(sizeof(struct acpi_cpu_softc *) *
	    (mp_maxid + 1), M_TEMP /* XXX */, M_WAITOK | M_ZERO);

    if (type == ACPI_TYPE_PROCESSOR) {
	/* Get our Processor object. */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(handle, NULL, NULL, &buf);
	if (ACPI_FAILURE(status)) {
	    device_printf(dev, "probe failed to get Processor obj - %s\n",
		AcpiFormatException(status));
	    return (ENXIO);
	}
	obj = (ACPI_OBJECT *)buf.Pointer;
	if (obj->Type != ACPI_TYPE_PROCESSOR) {
	    device_printf(dev, "Processor object has bad type %d\n",
		obj->Type);
	    AcpiOsFree(obj);
	    return (ENXIO);
	}

	/*
	 * Find the processor associated with our unit.  We could use the
	 * ProcId as a key, however, some boxes do not have the same values
	 * in their Processor object as the ProcId values in the MADT.
	 */
	acpi_id = obj->Processor.ProcId;
	AcpiOsFree(obj);
    } else {
	status = acpi_GetInteger(handle, "_UID", &acpi_id);
	if (ACPI_FAILURE(status)) {
	    device_printf(dev, "Device object has bad value - %s\n",
		AcpiFormatException(status));
	    return (ENXIO);
	}
    }
    if (acpi_pcpu_get_id(dev, &acpi_id, &cpu_id) != 0)
	return (ENXIO);

    /*
     * Check if we already probed this processor.  We scan the bus twice
     * so it's possible we've already seen this one.
     */
    if (cpu_softc[cpu_id] != NULL)
	return (ENXIO);

    /* Mark this processor as in-use and save our derived id for attach. */
    cpu_softc[cpu_id] = (void *)1;
    acpi_set_private(dev, (void*)(intptr_t)cpu_id);
    device_set_desc(dev, "ACPI CPU");

    if (!bootverbose && device_get_unit(dev) != 0) {
	    device_quiet(dev);
	    device_quiet_children(dev);
    }

    return (0);
}

static int
acpi_cpu_attach(device_t dev)
{
    ACPI_BUFFER		   buf;
    ACPI_OBJECT		   arg, *obj;
    ACPI_OBJECT_LIST	   arglist;
    struct pcpu		   *pcpu_data;
    struct acpi_cpu_softc *sc;
    struct acpi_softc	  *acpi_sc;
    ACPI_STATUS		   status;
    u_int		   features;
    int			   cpu_id, drv_count, i;
    driver_t 		  **drivers;
    uint32_t		   cap_set[3];

    /* UUID needed by _OSC evaluation */
    static uint8_t cpu_oscuuid[16] = { 0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29,
				       0xBE, 0x47, 0x9E, 0xBD, 0xD8, 0x70,
				       0x58, 0x71, 0x39, 0x53 };

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    sc = device_get_softc(dev);
    sc->cpu_dev = dev;
    sc->cpu_handle = acpi_get_handle(dev);
    cpu_id = (int)(intptr_t)acpi_get_private(dev);
    cpu_softc[cpu_id] = sc;
    pcpu_data = pcpu_find(cpu_id);
    pcpu_data->pc_device = dev;
    sc->cpu_pcpu = pcpu_data;
    cpu_smi_cmd = AcpiGbl_FADT.SmiCommand;
    cpu_cst_cnt = AcpiGbl_FADT.CstControl;

    if (acpi_get_type(dev) == ACPI_TYPE_PROCESSOR) {
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->cpu_handle, NULL, NULL, &buf);
	if (ACPI_FAILURE(status)) {
	    device_printf(dev, "attach failed to get Processor obj - %s\n",
		AcpiFormatException(status));
	    return (ENXIO);
	}
	obj = (ACPI_OBJECT *)buf.Pointer;
	sc->cpu_p_blk = obj->Processor.PblkAddress;
	sc->cpu_p_blk_len = obj->Processor.PblkLength;
	sc->cpu_acpi_id = obj->Processor.ProcId;
	AcpiOsFree(obj);
    } else {
	KASSERT(acpi_get_type(dev) == ACPI_TYPE_DEVICE,
	    ("Unexpected ACPI object"));
	status = acpi_GetInteger(sc->cpu_handle, "_UID", &sc->cpu_acpi_id);
	if (ACPI_FAILURE(status)) {
	    device_printf(dev, "Device object has bad value - %s\n",
		AcpiFormatException(status));
	    return (ENXIO);
	}
	sc->cpu_p_blk = 0;
	sc->cpu_p_blk_len = 0;
    }
    ACPI_DEBUG_PRINT((ACPI_DB_INFO, "acpi_cpu%d: P_BLK at %#x/%d\n",
		     device_get_unit(dev), sc->cpu_p_blk, sc->cpu_p_blk_len));

    /*
     * If this is the first cpu we attach, create and initialize the generic
     * resources that will be used by all acpi cpu devices.
     */
    if (device_get_unit(dev) == 0) {
	/* Assume we won't be using generic Cx mode by default */
	cpu_cx_generic = FALSE;

	/* Install hw.acpi.cpu sysctl tree */
	acpi_sc = acpi_device_get_parent_softc(dev);
	sysctl_ctx_init(&cpu_sysctl_ctx);
	cpu_sysctl_tree = SYSCTL_ADD_NODE(&cpu_sysctl_ctx,
	    SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree), OID_AUTO, "cpu",
	    CTLFLAG_RD, 0, "node for CPU children");
    }

    /*
     * Before calling any CPU methods, collect child driver feature hints
     * and notify ACPI of them.  We support unified SMP power control
     * so advertise this ourselves.  Note this is not the same as independent
     * SMP control where each CPU can have different settings.
     */
    sc->cpu_features = ACPI_CAP_SMP_SAME | ACPI_CAP_SMP_SAME_C3 |
      ACPI_CAP_C1_IO_HALT;

#if defined(__i386__) || defined(__amd64__)
    /*
     * Ask for MWAIT modes if not disabled and interrupts work
     * reasonable with MWAIT.
     */
    if (!acpi_disabled("mwait") && cpu_mwait_usable())
	sc->cpu_features |= ACPI_CAP_SMP_C1_NATIVE | ACPI_CAP_SMP_C3_NATIVE;
#endif

    if (devclass_get_drivers(acpi_cpu_devclass, &drivers, &drv_count) == 0) {
	for (i = 0; i < drv_count; i++) {
	    if (ACPI_GET_FEATURES(drivers[i], &features) == 0)
		sc->cpu_features |= features;
	}
	free(drivers, M_TEMP);
    }

    /*
     * CPU capabilities are specified in
     * Intel Processor Vendor-Specific ACPI Interface Specification.
     */
    if (sc->cpu_features) {
	cap_set[1] = sc->cpu_features;
	status = acpi_EvaluateOSC(sc->cpu_handle, cpu_oscuuid, 1, 2, cap_set,
	    cap_set, false);
	if (ACPI_SUCCESS(status)) {
	    if (cap_set[0] != 0)
		device_printf(dev, "_OSC returned status %#x\n", cap_set[0]);
	}
	else {
	    arglist.Pointer = &arg;
	    arglist.Count = 1;
	    arg.Type = ACPI_TYPE_BUFFER;
	    arg.Buffer.Length = sizeof(cap_set);
	    arg.Buffer.Pointer = (uint8_t *)cap_set;
	    cap_set[0] = 1; /* revision */
	    cap_set[1] = 1; /* number of capabilities integers */
	    cap_set[2] = sc->cpu_features;
	    AcpiEvaluateObject(sc->cpu_handle, "_PDC", &arglist, NULL);
	}
    }

    /* Probe for Cx state support. */
    acpi_cpu_cx_probe(sc);

    return (0);
}

static void
acpi_cpu_postattach(void *unused __unused)
{
    device_t *devices;
    int err;
    int i, n;
    int attached;

    err = devclass_get_devices(acpi_cpu_devclass, &devices, &n);
    if (err != 0) {
	printf("devclass_get_devices(acpi_cpu_devclass) failed\n");
	return;
    }
    attached = 0;
    for (i = 0; i < n; i++)
	if (device_is_attached(devices[i]) &&
	    device_get_driver(devices[i]) == &acpi_cpu_driver)
	    attached = 1;
    for (i = 0; i < n; i++)
	bus_generic_probe(devices[i]);
    for (i = 0; i < n; i++)
	bus_generic_attach(devices[i]);
    free(devices, M_TEMP);

    if (attached) {
#ifdef EARLY_AP_STARTUP
	acpi_cpu_startup(NULL);
#else
	/* Queue post cpu-probing task handler */
	AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_cpu_startup, NULL);
#endif
    }
}

SYSINIT(acpi_cpu, SI_SUB_CONFIGURE, SI_ORDER_MIDDLE,
    acpi_cpu_postattach, NULL);

static void
disable_idle(struct acpi_cpu_softc *sc)
{
    cpuset_t cpuset;

    CPU_SETOF(sc->cpu_pcpu->pc_cpuid, &cpuset);
    sc->cpu_disable_idle = TRUE;

    /*
     * Ensure that the CPU is not in idle state or in acpi_cpu_idle().
     * Note that this code depends on the fact that the rendezvous IPI
     * can not penetrate context where interrupts are disabled and acpi_cpu_idle
     * is called and executed in such a context with interrupts being re-enabled
     * right before return.
     */
    smp_rendezvous_cpus(cpuset, smp_no_rendezvous_barrier, NULL,
	smp_no_rendezvous_barrier, NULL);
}

static void
enable_idle(struct acpi_cpu_softc *sc)
{

    sc->cpu_disable_idle = FALSE;
}

#if defined(__i386__) || defined(__amd64__)
static int
is_idle_disabled(struct acpi_cpu_softc *sc)
{

    return (sc->cpu_disable_idle);
}
#endif

/*
 * Disable any entry to the idle function during suspend and re-enable it
 * during resume.
 */
static int
acpi_cpu_suspend(device_t dev)
{
    int error;

    error = bus_generic_suspend(dev);
    if (error)
	return (error);
    disable_idle(device_get_softc(dev));
    return (0);
}

static int
acpi_cpu_resume(device_t dev)
{

    enable_idle(device_get_softc(dev));
    return (bus_generic_resume(dev));
}

/*
 * Find the processor associated with a given ACPI ID.  By default,
 * use the MADT to map ACPI IDs to APIC IDs and use that to locate a
 * processor.  Some systems have inconsistent ASL and MADT however.
 * For these systems the cpu_unordered tunable can be set in which
 * case we assume that Processor objects are listed in the same order
 * in both the MADT and ASL.
 */
static int
acpi_pcpu_get_id(device_t dev, uint32_t *acpi_id, uint32_t *cpu_id)
{
    struct pcpu	*pc;
    uint32_t	 i, idx;

    KASSERT(acpi_id != NULL, ("Null acpi_id"));
    KASSERT(cpu_id != NULL, ("Null cpu_id"));
    idx = device_get_unit(dev);

    /*
     * If pc_acpi_id for CPU 0 is not initialized (e.g. a non-APIC
     * UP box) use the ACPI ID from the first processor we find.
     */
    if (idx == 0 && mp_ncpus == 1) {
	pc = pcpu_find(0);
	if (pc->pc_acpi_id == 0xffffffff)
	    pc->pc_acpi_id = *acpi_id;
	*cpu_id = 0;
	return (0);
    }

    CPU_FOREACH(i) {
	pc = pcpu_find(i);
	KASSERT(pc != NULL, ("no pcpu data for %d", i));
	if (cpu_unordered) {
	    if (idx-- == 0) {
		/*
		 * If pc_acpi_id doesn't match the ACPI ID from the
		 * ASL, prefer the MADT-derived value.
		 */
		if (pc->pc_acpi_id != *acpi_id)
		    *acpi_id = pc->pc_acpi_id;
		*cpu_id = pc->pc_cpuid;
		return (0);
	    }
	} else {
	    if (pc->pc_acpi_id == *acpi_id) {
		if (bootverbose)
		    device_printf(dev,
			"Processor %s (ACPI ID %u) -> APIC ID %d\n",
			acpi_name(acpi_get_handle(dev)), *acpi_id,
			pc->pc_cpuid);
		*cpu_id = pc->pc_cpuid;
		return (0);
	    }
	}
    }

    if (bootverbose)
	printf("ACPI: Processor %s (ACPI ID %u) ignored\n",
	    acpi_name(acpi_get_handle(dev)), *acpi_id);

    return (ESRCH);
}

static struct resource_list *
acpi_cpu_get_rlist(device_t dev, device_t child)
{
    struct acpi_cpu_device *ad;

    ad = device_get_ivars(child);
    if (ad == NULL)
	return (NULL);
    return (&ad->ad_rl);
}

static device_t
acpi_cpu_add_child(device_t dev, u_int order, const char *name, int unit)
{
    struct acpi_cpu_device *ad;
    device_t child;

    if ((ad = malloc(sizeof(*ad), M_TEMP, M_NOWAIT | M_ZERO)) == NULL)
	return (NULL);

    resource_list_init(&ad->ad_rl);
    
    child = device_add_child_ordered(dev, order, name, unit);
    if (child != NULL)
	device_set_ivars(child, ad);
    else
	free(ad, M_TEMP);
    return (child);
}

static int
acpi_cpu_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
    struct acpi_cpu_softc *sc;

    sc = device_get_softc(dev);
    switch (index) {
    case ACPI_IVAR_HANDLE:
	*result = (uintptr_t)sc->cpu_handle;
	break;
    case CPU_IVAR_PCPU:
	*result = (uintptr_t)sc->cpu_pcpu;
	break;
#if defined(__amd64__) || defined(__i386__)
    case CPU_IVAR_NOMINAL_MHZ:
	if (tsc_is_invariant) {
	    *result = (uintptr_t)(atomic_load_acq_64(&tsc_freq) / 1000000);
	    break;
	}
	/* FALLTHROUGH */
#endif
    default:
	return (ENOENT);
    }
    return (0);
}

static int
acpi_cpu_shutdown(device_t dev)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /* Allow children to shutdown first. */
    bus_generic_shutdown(dev);

    /*
     * Disable any entry to the idle function.
     */
    disable_idle(device_get_softc(dev));

    /*
     * CPU devices are not truly detached and remain referenced,
     * so their resources are not freed.
     */

    return_VALUE (0);
}

static void
acpi_cpu_cx_probe(struct acpi_cpu_softc *sc)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /* Use initial sleep value of 1 sec. to start with lowest idle state. */
    sc->cpu_prev_sleep = 1000000;
    sc->cpu_cx_lowest = 0;
    sc->cpu_cx_lowest_lim = 0;

    /*
     * Check for the ACPI 2.0 _CST sleep states object. If we can't find
     * any, we'll revert to generic FADT/P_BLK Cx control method which will
     * be handled by acpi_cpu_startup. We need to defer to after having
     * probed all the cpus in the system before probing for generic Cx
     * states as we may already have found cpus with valid _CST packages
     */
    if (!cpu_cx_generic && acpi_cpu_cx_cst(sc) != 0) {
	/*
	 * We were unable to find a _CST package for this cpu or there
	 * was an error parsing it. Switch back to generic mode.
	 */
	cpu_cx_generic = TRUE;
	if (bootverbose)
	    device_printf(sc->cpu_dev, "switching to generic Cx mode\n");
    }

    /*
     * TODO: _CSD Package should be checked here.
     */
}

static void
acpi_cpu_generic_cx_probe(struct acpi_cpu_softc *sc)
{
    ACPI_GENERIC_ADDRESS	 gas;
    struct acpi_cx		*cx_ptr;

    sc->cpu_cx_count = 0;
    cx_ptr = sc->cpu_cx_states;

    /* Use initial sleep value of 1 sec. to start with lowest idle state. */
    sc->cpu_prev_sleep = 1000000;

    /* C1 has been required since just after ACPI 1.0 */
    cx_ptr->type = ACPI_STATE_C1;
    cx_ptr->trans_lat = 0;
    cx_ptr++;
    sc->cpu_non_c2 = sc->cpu_cx_count;
    sc->cpu_non_c3 = sc->cpu_cx_count;
    sc->cpu_cx_count++;

    /* 
     * The spec says P_BLK must be 6 bytes long.  However, some systems
     * use it to indicate a fractional set of features present so we
     * take 5 as C2.  Some may also have a value of 7 to indicate
     * another C3 but most use _CST for this (as required) and having
     * "only" C1-C3 is not a hardship.
     */
    if (sc->cpu_p_blk_len < 5)
	return; 

    /* Validate and allocate resources for C2 (P_LVL2). */
    gas.SpaceId = ACPI_ADR_SPACE_SYSTEM_IO;
    gas.BitWidth = 8;
    if (AcpiGbl_FADT.C2Latency <= 100) {
	gas.Address = sc->cpu_p_blk + 4;
	cx_ptr->res_rid = 0;
	acpi_bus_alloc_gas(sc->cpu_dev, &cx_ptr->res_type, &cx_ptr->res_rid,
	    &gas, &cx_ptr->p_lvlx, RF_SHAREABLE);
	if (cx_ptr->p_lvlx != NULL) {
	    cx_ptr->type = ACPI_STATE_C2;
	    cx_ptr->trans_lat = AcpiGbl_FADT.C2Latency;
	    cx_ptr++;
	    sc->cpu_non_c3 = sc->cpu_cx_count;
	    sc->cpu_cx_count++;
	}
    }
    if (sc->cpu_p_blk_len < 6)
	return;

    /* Validate and allocate resources for C3 (P_LVL3). */
    if (AcpiGbl_FADT.C3Latency <= 1000 && !(cpu_quirks & CPU_QUIRK_NO_C3)) {
	gas.Address = sc->cpu_p_blk + 5;
	cx_ptr->res_rid = 1;
	acpi_bus_alloc_gas(sc->cpu_dev, &cx_ptr->res_type, &cx_ptr->res_rid,
	    &gas, &cx_ptr->p_lvlx, RF_SHAREABLE);
	if (cx_ptr->p_lvlx != NULL) {
	    cx_ptr->type = ACPI_STATE_C3;
	    cx_ptr->trans_lat = AcpiGbl_FADT.C3Latency;
	    cx_ptr++;
	    sc->cpu_cx_count++;
	}
    }
}

#if defined(__i386__) || defined(__amd64__)
static void
acpi_cpu_cx_cst_mwait(struct acpi_cx *cx_ptr, uint64_t address, int accsize)
{

	cx_ptr->do_mwait = true;
	cx_ptr->mwait_hint = address & 0xffffffff;
	cx_ptr->mwait_hw_coord = (accsize & CST_FFH_MWAIT_HW_COORD) != 0;
	cx_ptr->mwait_bm_avoidance = (accsize & CST_FFH_MWAIT_BM_AVOID) != 0;
}
#endif

static void
acpi_cpu_cx_cst_free_plvlx(device_t cpu_dev, struct acpi_cx *cx_ptr)
{

	if (cx_ptr->p_lvlx == NULL)
		return;
	bus_release_resource(cpu_dev, cx_ptr->res_type, cx_ptr->res_rid,
	    cx_ptr->p_lvlx);
	cx_ptr->p_lvlx = NULL;
}

/*
 * Parse a _CST package and set up its Cx states.  Since the _CST object
 * can change dynamically, our notify handler may call this function
 * to clean up and probe the new _CST package.
 */
static int
acpi_cpu_cx_cst(struct acpi_cpu_softc *sc)
{
    struct	 acpi_cx *cx_ptr;
    ACPI_STATUS	 status;
    ACPI_BUFFER	 buf;
    ACPI_OBJECT	*top;
    ACPI_OBJECT	*pkg;
    uint32_t	 count;
    int		 i;
#if defined(__i386__) || defined(__amd64__)
    uint64_t	 address;
    int		 vendor, class, accsize;
#endif

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    buf.Pointer = NULL;
    buf.Length = ACPI_ALLOCATE_BUFFER;
    status = AcpiEvaluateObject(sc->cpu_handle, "_CST", NULL, &buf);
    if (ACPI_FAILURE(status))
	return (ENXIO);

    /* _CST is a package with a count and at least one Cx package. */
    top = (ACPI_OBJECT *)buf.Pointer;
    if (!ACPI_PKG_VALID(top, 2) || acpi_PkgInt32(top, 0, &count) != 0) {
	device_printf(sc->cpu_dev, "invalid _CST package\n");
	AcpiOsFree(buf.Pointer);
	return (ENXIO);
    }
    if (count != top->Package.Count - 1) {
	device_printf(sc->cpu_dev, "invalid _CST state count (%d != %d)\n",
	       count, top->Package.Count - 1);
	count = top->Package.Count - 1;
    }
    if (count > MAX_CX_STATES) {
	device_printf(sc->cpu_dev, "_CST has too many states (%d)\n", count);
	count = MAX_CX_STATES;
    }

    sc->cpu_non_c2 = 0;
    sc->cpu_non_c3 = 0;
    sc->cpu_cx_count = 0;
    cx_ptr = sc->cpu_cx_states;

    /*
     * C1 has been required since just after ACPI 1.0.
     * Reserve the first slot for it.
     */
    cx_ptr->type = ACPI_STATE_C0;
    cx_ptr++;
    sc->cpu_cx_count++;

    /* Set up all valid states. */
    for (i = 0; i < count; i++) {
	pkg = &top->Package.Elements[i + 1];
	if (!ACPI_PKG_VALID(pkg, 4) ||
	    acpi_PkgInt32(pkg, 1, &cx_ptr->type) != 0 ||
	    acpi_PkgInt32(pkg, 2, &cx_ptr->trans_lat) != 0 ||
	    acpi_PkgInt32(pkg, 3, &cx_ptr->power) != 0) {

	    device_printf(sc->cpu_dev, "skipping invalid Cx state package\n");
	    continue;
	}

	/* Validate the state to see if we should use it. */
	switch (cx_ptr->type) {
	case ACPI_STATE_C1:
	    acpi_cpu_cx_cst_free_plvlx(sc->cpu_dev, cx_ptr);
#if defined(__i386__) || defined(__amd64__)
	    if (acpi_PkgFFH_IntelCpu(pkg, 0, &vendor, &class, &address,
	      &accsize) == 0 && vendor == CST_FFH_VENDOR_INTEL) {
		if (class == CST_FFH_INTEL_CL_C1IO) {
		    /* C1 I/O then Halt */
		    cx_ptr->res_rid = sc->cpu_cx_count;
		    bus_set_resource(sc->cpu_dev, SYS_RES_IOPORT,
		      cx_ptr->res_rid, address, 1);
		    cx_ptr->p_lvlx = bus_alloc_resource_any(sc->cpu_dev,
		      SYS_RES_IOPORT, &cx_ptr->res_rid, RF_ACTIVE |
		      RF_SHAREABLE);
		    if (cx_ptr->p_lvlx == NULL) {
			bus_delete_resource(sc->cpu_dev, SYS_RES_IOPORT,
			  cx_ptr->res_rid);
			device_printf(sc->cpu_dev,
			  "C1 I/O failed to allocate port %d, "
			  "degrading to C1 Halt", (int)address);
		    }
		} else if (class == CST_FFH_INTEL_CL_MWAIT) {
		    acpi_cpu_cx_cst_mwait(cx_ptr, address, accsize);
		}
	    }
#endif
	    if (sc->cpu_cx_states[0].type == ACPI_STATE_C0) {
		/* This is the first C1 state.  Use the reserved slot. */
		sc->cpu_cx_states[0] = *cx_ptr;
	    } else {
		sc->cpu_non_c2 = sc->cpu_cx_count;
		sc->cpu_non_c3 = sc->cpu_cx_count;
		cx_ptr++;
		sc->cpu_cx_count++;
	    }
	    continue;
	case ACPI_STATE_C2:
	    sc->cpu_non_c3 = sc->cpu_cx_count;
	    break;
	case ACPI_STATE_C3:
	default:
	    if ((cpu_quirks & CPU_QUIRK_NO_C3) != 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				 "acpi_cpu%d: C3[%d] not available.\n",
				 device_get_unit(sc->cpu_dev), i));
		continue;
	    }
	    break;
	}

	/* Free up any previous register. */
	acpi_cpu_cx_cst_free_plvlx(sc->cpu_dev, cx_ptr);

	/* Allocate the control register for C2 or C3. */
#if defined(__i386__) || defined(__amd64__)
	if (acpi_PkgFFH_IntelCpu(pkg, 0, &vendor, &class, &address,
	  &accsize) == 0 && vendor == CST_FFH_VENDOR_INTEL &&
	  class == CST_FFH_INTEL_CL_MWAIT) {
	    /* Native C State Instruction use (mwait) */
	    acpi_cpu_cx_cst_mwait(cx_ptr, address, accsize);
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
	      "acpi_cpu%d: Got C%d/mwait - %d latency\n",
	      device_get_unit(sc->cpu_dev), cx_ptr->type, cx_ptr->trans_lat));
	    cx_ptr++;
	    sc->cpu_cx_count++;
	} else
#endif
	{
	    cx_ptr->res_rid = sc->cpu_cx_count;
	    acpi_PkgGas(sc->cpu_dev, pkg, 0, &cx_ptr->res_type,
		&cx_ptr->res_rid, &cx_ptr->p_lvlx, RF_SHAREABLE);
	    if (cx_ptr->p_lvlx) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		     "acpi_cpu%d: Got C%d - %d latency\n",
		     device_get_unit(sc->cpu_dev), cx_ptr->type,
		     cx_ptr->trans_lat));
		cx_ptr++;
		sc->cpu_cx_count++;
	    }
	}
    }
    AcpiOsFree(buf.Pointer);

    /* If C1 state was not found, we need one now. */
    cx_ptr = sc->cpu_cx_states;
    if (cx_ptr->type == ACPI_STATE_C0) {
	cx_ptr->type = ACPI_STATE_C1;
	cx_ptr->trans_lat = 0;
    }

    return (0);
}

/*
 * Call this *after* all CPUs have been attached.
 */
static void
acpi_cpu_startup(void *arg)
{
    struct acpi_cpu_softc *sc;
    int i;

    /* Get set of CPU devices */
    devclass_get_devices(acpi_cpu_devclass, &cpu_devices, &cpu_ndevices);

    /*
     * Setup any quirks that might necessary now that we have probed
     * all the CPUs
     */
    acpi_cpu_quirks();

    if (cpu_cx_generic) {
	/*
	 * We are using generic Cx mode, probe for available Cx states
	 * for all processors.
	 */
	for (i = 0; i < cpu_ndevices; i++) {
	    sc = device_get_softc(cpu_devices[i]);
	    acpi_cpu_generic_cx_probe(sc);
	}
    } else {
	/*
	 * We are using _CST mode, remove C3 state if necessary.
	 * As we now know for sure that we will be using _CST mode
	 * install our notify handler.
	 */
	for (i = 0; i < cpu_ndevices; i++) {
	    sc = device_get_softc(cpu_devices[i]);
	    if (cpu_quirks & CPU_QUIRK_NO_C3) {
		sc->cpu_cx_count = min(sc->cpu_cx_count, sc->cpu_non_c3 + 1);
	    }
	    AcpiInstallNotifyHandler(sc->cpu_handle, ACPI_DEVICE_NOTIFY,
		acpi_cpu_notify, sc);
	}
    }

    /* Perform Cx final initialization. */
    for (i = 0; i < cpu_ndevices; i++) {
	sc = device_get_softc(cpu_devices[i]);
	acpi_cpu_startup_cx(sc);
    }

    /* Add a sysctl handler to handle global Cx lowest setting */
    SYSCTL_ADD_PROC(&cpu_sysctl_ctx, SYSCTL_CHILDREN(cpu_sysctl_tree),
	OID_AUTO, "cx_lowest", CTLTYPE_STRING | CTLFLAG_RW,
	NULL, 0, acpi_cpu_global_cx_lowest_sysctl, "A",
	"Global lowest Cx sleep state to use");

    /* Take over idling from cpu_idle_default(). */
    cpu_cx_lowest_lim = 0;
    for (i = 0; i < cpu_ndevices; i++) {
	sc = device_get_softc(cpu_devices[i]);
	enable_idle(sc);
    }
#if defined(__i386__) || defined(__amd64__)
    cpu_idle_hook = acpi_cpu_idle;
#endif
}

static void
acpi_cpu_cx_list(struct acpi_cpu_softc *sc)
{
    struct sbuf sb;
    int i;

    /*
     * Set up the list of Cx states
     */
    sbuf_new(&sb, sc->cpu_cx_supported, sizeof(sc->cpu_cx_supported),
	SBUF_FIXEDLEN);
    for (i = 0; i < sc->cpu_cx_count; i++)
	sbuf_printf(&sb, "C%d/%d/%d ", i + 1, sc->cpu_cx_states[i].type,
	    sc->cpu_cx_states[i].trans_lat);
    sbuf_trim(&sb);
    sbuf_finish(&sb);
}	

static void
acpi_cpu_startup_cx(struct acpi_cpu_softc *sc)
{
    acpi_cpu_cx_list(sc);
    
    SYSCTL_ADD_STRING(&sc->cpu_sysctl_ctx,
		      SYSCTL_CHILDREN(device_get_sysctl_tree(sc->cpu_dev)),
		      OID_AUTO, "cx_supported", CTLFLAG_RD,
		      sc->cpu_cx_supported, 0,
		      "Cx/microsecond values for supported Cx states");
    SYSCTL_ADD_PROC(&sc->cpu_sysctl_ctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->cpu_dev)),
		    OID_AUTO, "cx_lowest", CTLTYPE_STRING | CTLFLAG_RW,
		    (void *)sc, 0, acpi_cpu_cx_lowest_sysctl, "A",
		    "lowest Cx sleep state to use");
    SYSCTL_ADD_PROC(&sc->cpu_sysctl_ctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->cpu_dev)),
		    OID_AUTO, "cx_usage", CTLTYPE_STRING | CTLFLAG_RD,
		    (void *)sc, 0, acpi_cpu_usage_sysctl, "A",
		    "percent usage for each Cx state");
    SYSCTL_ADD_PROC(&sc->cpu_sysctl_ctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->cpu_dev)),
		    OID_AUTO, "cx_usage_counters", CTLTYPE_STRING | CTLFLAG_RD,
		    (void *)sc, 0, acpi_cpu_usage_counters_sysctl, "A",
		    "Cx sleep state counters");
#if defined(__i386__) || defined(__amd64__)
    SYSCTL_ADD_PROC(&sc->cpu_sysctl_ctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->cpu_dev)),
		    OID_AUTO, "cx_method", CTLTYPE_STRING | CTLFLAG_RD,
		    (void *)sc, 0, acpi_cpu_method_sysctl, "A",
		    "Cx entrance methods");
#endif

    /* Signal platform that we can handle _CST notification. */
    if (!cpu_cx_generic && cpu_cst_cnt != 0) {
	ACPI_LOCK(acpi);
	AcpiOsWritePort(cpu_smi_cmd, cpu_cst_cnt, 8);
	ACPI_UNLOCK(acpi);
    }
}

#if defined(__i386__) || defined(__amd64__)
/*
 * Idle the CPU in the lowest state possible.  This function is called with
 * interrupts disabled.  Note that once it re-enables interrupts, a task
 * switch can occur so do not access shared data (i.e. the softc) after
 * interrupts are re-enabled.
 */
static void
acpi_cpu_idle(sbintime_t sbt)
{
    struct	acpi_cpu_softc *sc;
    struct	acpi_cx *cx_next;
    uint64_t	cputicks;
    uint32_t	start_time, end_time;
    ACPI_STATUS	status;
    int		bm_active, cx_next_idx, i, us;

    /*
     * Look up our CPU id to get our softc.  If it's NULL, we'll use C1
     * since there is no ACPI processor object for this CPU.  This occurs
     * for logical CPUs in the HTT case.
     */
    sc = cpu_softc[PCPU_GET(cpuid)];
    if (sc == NULL) {
	acpi_cpu_c1();
	return;
    }

    /* If disabled, take the safe path. */
    if (is_idle_disabled(sc)) {
	acpi_cpu_c1();
	return;
    }

    /* Find the lowest state that has small enough latency. */
    us = sc->cpu_prev_sleep;
    if (sbt >= 0 && us > (sbt >> 12))
	us = (sbt >> 12);
    cx_next_idx = 0;
    if (cpu_disable_c2_sleep)
	i = min(sc->cpu_cx_lowest, sc->cpu_non_c2);
    else if (cpu_disable_c3_sleep)
	i = min(sc->cpu_cx_lowest, sc->cpu_non_c3);
    else
	i = sc->cpu_cx_lowest;
    for (; i >= 0; i--) {
	if (sc->cpu_cx_states[i].trans_lat * 3 <= us) {
	    cx_next_idx = i;
	    break;
	}
    }

    /*
     * Check for bus master activity.  If there was activity, clear
     * the bit and use the lowest non-C3 state.  Note that the USB
     * driver polling for new devices keeps this bit set all the
     * time if USB is loaded.
     */
    if ((cpu_quirks & CPU_QUIRK_NO_BM_CTRL) == 0 &&
	cx_next_idx > sc->cpu_non_c3) {
	status = AcpiReadBitRegister(ACPI_BITREG_BUS_MASTER_STATUS, &bm_active);
	if (ACPI_SUCCESS(status) && bm_active != 0) {
	    AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_STATUS, 1);
	    cx_next_idx = sc->cpu_non_c3;
	}
    }

    /* Select the next state and update statistics. */
    cx_next = &sc->cpu_cx_states[cx_next_idx];
    sc->cpu_cx_stats[cx_next_idx]++;
    KASSERT(cx_next->type != ACPI_STATE_C0, ("acpi_cpu_idle: C0 sleep"));

    /*
     * Execute HLT (or equivalent) and wait for an interrupt.  We can't
     * precisely calculate the time spent in C1 since the place we wake up
     * is an ISR.  Assume we slept no more then half of quantum, unless
     * we are called inside critical section, delaying context switch.
     */
    if (cx_next->type == ACPI_STATE_C1) {
	cputicks = cpu_ticks();
	if (cx_next->p_lvlx != NULL) {
	    /* C1 I/O then Halt */
	    CPU_GET_REG(cx_next->p_lvlx, 1);
	}
	if (cx_next->do_mwait)
	    acpi_cpu_idle_mwait(cx_next->mwait_hint);
	else
	    acpi_cpu_c1();
	end_time = ((cpu_ticks() - cputicks) << 20) / cpu_tickrate();
	if (curthread->td_critnest == 0)
		end_time = min(end_time, 500000 / hz);
	/* acpi_cpu_c1() returns with interrupts enabled. */
	if (cx_next->do_mwait)
	    ACPI_ENABLE_IRQS();
	sc->cpu_prev_sleep = (sc->cpu_prev_sleep * 3 + end_time) / 4;
	return;
    }

    /*
     * For C3, disable bus master arbitration and enable bus master wake
     * if BM control is available, otherwise flush the CPU cache.
     */
    if (cx_next->type == ACPI_STATE_C3 || cx_next->mwait_bm_avoidance) {
	if ((cpu_quirks & CPU_QUIRK_NO_BM_CTRL) == 0) {
	    AcpiWriteBitRegister(ACPI_BITREG_ARB_DISABLE, 1);
	    AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_RLD, 1);
	} else
	    ACPI_FLUSH_CPU_CACHE();
    }

    /*
     * Read from P_LVLx to enter C2(+), checking time spent asleep.
     * Use the ACPI timer for measuring sleep time.  Since we need to
     * get the time very close to the CPU start/stop clock logic, this
     * is the only reliable time source.
     */
    if (cx_next->type == ACPI_STATE_C3) {
	AcpiGetTimer(&start_time);
	cputicks = 0;
    } else {
	start_time = 0;
	cputicks = cpu_ticks();
    }
    if (cx_next->do_mwait)
	acpi_cpu_idle_mwait(cx_next->mwait_hint);
    else
	CPU_GET_REG(cx_next->p_lvlx, 1);

    /*
     * Read the end time twice.  Since it may take an arbitrary time
     * to enter the idle state, the first read may be executed before
     * the processor has stopped.  Doing it again provides enough
     * margin that we are certain to have a correct value.
     */
    AcpiGetTimer(&end_time);
    if (cx_next->type == ACPI_STATE_C3) {
	AcpiGetTimer(&end_time);
	AcpiGetTimerDuration(start_time, end_time, &end_time);
    } else
	end_time = ((cpu_ticks() - cputicks) << 20) / cpu_tickrate();

    /* Enable bus master arbitration and disable bus master wakeup. */
    if ((cx_next->type == ACPI_STATE_C3 || cx_next->mwait_bm_avoidance) &&
      (cpu_quirks & CPU_QUIRK_NO_BM_CTRL) == 0) {
	AcpiWriteBitRegister(ACPI_BITREG_ARB_DISABLE, 0);
	AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_RLD, 0);
    }
    ACPI_ENABLE_IRQS();

    sc->cpu_prev_sleep = (sc->cpu_prev_sleep * 3 + PM_USEC(end_time)) / 4;
}
#endif

/*
 * Re-evaluate the _CST object when we are notified that it changed.
 */
static void
acpi_cpu_notify(ACPI_HANDLE h, UINT32 notify, void *context)
{
    struct acpi_cpu_softc *sc = (struct acpi_cpu_softc *)context;

    if (notify != ACPI_NOTIFY_CX_STATES)
	return;

    /*
     * C-state data for target CPU is going to be in flux while we execute
     * acpi_cpu_cx_cst, so disable entering acpi_cpu_idle.
     * Also, it may happen that multiple ACPI taskqueues may concurrently
     * execute notifications for the same CPU.  ACPI_SERIAL is used to
     * protect against that.
     */
    ACPI_SERIAL_BEGIN(cpu);
    disable_idle(sc);

    /* Update the list of Cx states. */
    acpi_cpu_cx_cst(sc);
    acpi_cpu_cx_list(sc);
    acpi_cpu_set_cx_lowest(sc);

    enable_idle(sc);
    ACPI_SERIAL_END(cpu);

    acpi_UserNotify("PROCESSOR", sc->cpu_handle, notify);
}

static void
acpi_cpu_quirks(void)
{
    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /*
     * Bus mastering arbitration control is needed to keep caches coherent
     * while sleeping in C3.  If it's not present but a working flush cache
     * instruction is present, flush the caches before entering C3 instead.
     * Otherwise, just disable C3 completely.
     */
    if (AcpiGbl_FADT.Pm2ControlBlock == 0 ||
	AcpiGbl_FADT.Pm2ControlLength == 0) {
	if ((AcpiGbl_FADT.Flags & ACPI_FADT_WBINVD) &&
	    (AcpiGbl_FADT.Flags & ACPI_FADT_WBINVD_FLUSH) == 0) {
	    cpu_quirks |= CPU_QUIRK_NO_BM_CTRL;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"acpi_cpu: no BM control, using flush cache method\n"));
	} else {
	    cpu_quirks |= CPU_QUIRK_NO_C3;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"acpi_cpu: no BM control, C3 not available\n"));
	}
    }

    /*
     * If we are using generic Cx mode, C3 on multiple CPUs requires using
     * the expensive flush cache instruction.
     */
    if (cpu_cx_generic && mp_ncpus > 1) {
	cpu_quirks |= CPU_QUIRK_NO_BM_CTRL;
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
	    "acpi_cpu: SMP, using flush cache mode for C3\n"));
    }

    /* Look for various quirks of the PIIX4 part. */
    acpi_cpu_quirks_piix4();
}

static void
acpi_cpu_quirks_piix4(void)
{
#ifdef __i386__
    device_t acpi_dev;
    uint32_t val;
    ACPI_STATUS status;

    acpi_dev = pci_find_device(PCI_VENDOR_INTEL, PCI_DEVICE_82371AB_3);
    if (acpi_dev != NULL) {
	switch (pci_get_revid(acpi_dev)) {
	/*
	 * Disable C3 support for all PIIX4 chipsets.  Some of these parts
	 * do not report the BMIDE status to the BM status register and
	 * others have a livelock bug if Type-F DMA is enabled.  Linux
	 * works around the BMIDE bug by reading the BM status directly
	 * but we take the simpler approach of disabling C3 for these
	 * parts.
	 *
	 * See erratum #18 ("C3 Power State/BMIDE and Type-F DMA
	 * Livelock") from the January 2002 PIIX4 specification update.
	 * Applies to all PIIX4 models.
	 *
	 * Also, make sure that all interrupts cause a "Stop Break"
	 * event to exit from C2 state.
	 * Also, BRLD_EN_BM (ACPI_BITREG_BUS_MASTER_RLD in ACPI-speak)
	 * should be set to zero, otherwise it causes C2 to short-sleep.
	 * PIIX4 doesn't properly support C3 and bus master activity
	 * need not break out of C2.
	 */
	case PCI_REVISION_A_STEP:
	case PCI_REVISION_B_STEP:
	case PCI_REVISION_4E:
	case PCI_REVISION_4M:
	    cpu_quirks |= CPU_QUIRK_NO_C3;
	    ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"acpi_cpu: working around PIIX4 bug, disabling C3\n"));

	    val = pci_read_config(acpi_dev, PIIX4_DEVACTB_REG, 4);
	    if ((val & PIIX4_STOP_BREAK_MASK) != PIIX4_STOP_BREAK_MASK) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		    "acpi_cpu: PIIX4: enabling IRQs to generate Stop Break\n"));
	    	val |= PIIX4_STOP_BREAK_MASK;
		pci_write_config(acpi_dev, PIIX4_DEVACTB_REG, val, 4);
	    }
	    status = AcpiReadBitRegister(ACPI_BITREG_BUS_MASTER_RLD, &val);
	    if (ACPI_SUCCESS(status) && val != 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		    "acpi_cpu: PIIX4: reset BRLD_EN_BM\n"));
		AcpiWriteBitRegister(ACPI_BITREG_BUS_MASTER_RLD, 0);
	    }
	    break;
	default:
	    break;
	}
    }
#endif
}

static int
acpi_cpu_usage_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_cpu_softc *sc;
    struct sbuf	 sb;
    char	 buf[128];
    int		 i;
    uintmax_t	 fract, sum, whole;

    sc = (struct acpi_cpu_softc *) arg1;
    sum = 0;
    for (i = 0; i < sc->cpu_cx_count; i++)
	sum += sc->cpu_cx_stats[i];
    sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
    for (i = 0; i < sc->cpu_cx_count; i++) {
	if (sum > 0) {
	    whole = (uintmax_t)sc->cpu_cx_stats[i] * 100;
	    fract = (whole % sum) * 100;
	    sbuf_printf(&sb, "%u.%02u%% ", (u_int)(whole / sum),
		(u_int)(fract / sum));
	} else
	    sbuf_printf(&sb, "0.00%% ");
    }
    sbuf_printf(&sb, "last %dus", sc->cpu_prev_sleep);
    sbuf_trim(&sb);
    sbuf_finish(&sb);
    sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
    sbuf_delete(&sb);

    return (0);
}

/*
 * XXX TODO: actually add support to count each entry/exit
 * from the Cx states.
 */
static int
acpi_cpu_usage_counters_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct acpi_cpu_softc *sc;
    struct sbuf	 sb;
    char	 buf[128];
    int		 i;

    sc = (struct acpi_cpu_softc *) arg1;

    /* Print out the raw counters */
    sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);

    for (i = 0; i < sc->cpu_cx_count; i++) {
        sbuf_printf(&sb, "%u ", sc->cpu_cx_stats[i]);
    }

    sbuf_trim(&sb);
    sbuf_finish(&sb);
    sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
    sbuf_delete(&sb);

    return (0);
}

#if defined(__i386__) || defined(__amd64__)
static int
acpi_cpu_method_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_cpu_softc *sc;
	struct acpi_cx *cx;
	struct sbuf sb;
	char buf[128];
	int i;

	sc = (struct acpi_cpu_softc *)arg1;
	sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
	for (i = 0; i < sc->cpu_cx_count; i++) {
		cx = &sc->cpu_cx_states[i];
		sbuf_printf(&sb, "C%d/", i + 1);
		if (cx->do_mwait) {
			sbuf_cat(&sb, "mwait");
			if (cx->mwait_hw_coord)
				sbuf_cat(&sb, "/hwc");
			if (cx->mwait_bm_avoidance)
				sbuf_cat(&sb, "/bma");
		} else if (cx->type == ACPI_STATE_C1) {
			sbuf_cat(&sb, "hlt");
		} else {
			sbuf_cat(&sb, "io");
		}
		if (cx->type == ACPI_STATE_C1 && cx->p_lvlx != NULL)
			sbuf_cat(&sb, "/iohlt");
		sbuf_putc(&sb, ' ');
	}
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
	sbuf_delete(&sb);
	return (0);
}
#endif

static int
acpi_cpu_set_cx_lowest(struct acpi_cpu_softc *sc)
{
    int i;

    ACPI_SERIAL_ASSERT(cpu);
    sc->cpu_cx_lowest = min(sc->cpu_cx_lowest_lim, sc->cpu_cx_count - 1);

    /* If not disabling, cache the new lowest non-C3 state. */
    sc->cpu_non_c3 = 0;
    for (i = sc->cpu_cx_lowest; i >= 0; i--) {
	if (sc->cpu_cx_states[i].type < ACPI_STATE_C3) {
	    sc->cpu_non_c3 = i;
	    break;
	}
    }

    /* Reset the statistics counters. */
    bzero(sc->cpu_cx_stats, sizeof(sc->cpu_cx_stats));
    return (0);
}

static int
acpi_cpu_cx_lowest_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct	 acpi_cpu_softc *sc;
    char	 state[8];
    int		 val, error;

    sc = (struct acpi_cpu_softc *) arg1;
    snprintf(state, sizeof(state), "C%d", sc->cpu_cx_lowest_lim + 1);
    error = sysctl_handle_string(oidp, state, sizeof(state), req);
    if (error != 0 || req->newptr == NULL)
	return (error);
    if (strlen(state) < 2 || toupper(state[0]) != 'C')
	return (EINVAL);
    if (strcasecmp(state, "Cmax") == 0)
	val = MAX_CX_STATES;
    else {
	val = (int) strtol(state + 1, NULL, 10);
	if (val < 1 || val > MAX_CX_STATES)
	    return (EINVAL);
    }

    ACPI_SERIAL_BEGIN(cpu);
    sc->cpu_cx_lowest_lim = val - 1;
    acpi_cpu_set_cx_lowest(sc);
    ACPI_SERIAL_END(cpu);

    return (0);
}

static int
acpi_cpu_global_cx_lowest_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct	acpi_cpu_softc *sc;
    char	state[8];
    int		val, error, i;

    snprintf(state, sizeof(state), "C%d", cpu_cx_lowest_lim + 1);
    error = sysctl_handle_string(oidp, state, sizeof(state), req);
    if (error != 0 || req->newptr == NULL)
	return (error);
    if (strlen(state) < 2 || toupper(state[0]) != 'C')
	return (EINVAL);
    if (strcasecmp(state, "Cmax") == 0)
	val = MAX_CX_STATES;
    else {
	val = (int) strtol(state + 1, NULL, 10);
	if (val < 1 || val > MAX_CX_STATES)
	    return (EINVAL);
    }

    /* Update the new lowest useable Cx state for all CPUs. */
    ACPI_SERIAL_BEGIN(cpu);
    cpu_cx_lowest_lim = val - 1;
    for (i = 0; i < cpu_ndevices; i++) {
	sc = device_get_softc(cpu_devices[i]);
	sc->cpu_cx_lowest_lim = cpu_cx_lowest_lim;
	acpi_cpu_set_cx_lowest(sc);
    }
    ACPI_SERIAL_END(cpu);

    return (0);
}
