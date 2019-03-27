/*-
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@jp.freebsd.org>
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
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
 *
 * $FreeBSD$
 */

#ifndef _ACPIVAR_H_
#define _ACPIVAR_H_

#ifdef _KERNEL

#include "acpi_if.h"
#include "bus_if.h"
#include <sys/eventhandler.h>
#ifdef INTRNG
#include <sys/intr.h>
#endif
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/selinfo.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

struct apm_clone_data;
struct acpi_softc {
    device_t		acpi_dev;
    struct cdev		*acpi_dev_t;

    int			acpi_enabled;
    int			acpi_sstate;
    int			acpi_sleep_disabled;
    int			acpi_resources_reserved;

    struct sysctl_ctx_list acpi_sysctl_ctx;
    struct sysctl_oid	*acpi_sysctl_tree;
    int			acpi_power_button_sx;
    int			acpi_sleep_button_sx;
    int			acpi_lid_switch_sx;

    int			acpi_standby_sx;
    int			acpi_suspend_sx;

    int			acpi_sleep_delay;
    int			acpi_s4bios;
    int			acpi_do_disable;
    int			acpi_verbose;
    int			acpi_handle_reboot;

    vm_offset_t		acpi_wakeaddr;
    vm_paddr_t		acpi_wakephys;

    int			acpi_next_sstate;	/* Next suspend Sx state. */
    struct apm_clone_data *acpi_clone;		/* Pseudo-dev for devd(8). */
    STAILQ_HEAD(,apm_clone_data) apm_cdevs;	/* All apm/apmctl/acpi cdevs. */
    struct callout	susp_force_to;		/* Force suspend if no acks. */
};

struct acpi_device {
    /* ACPI ivars */
    ACPI_HANDLE			ad_handle;
    void			*ad_private;
    int				ad_flags;
    int				ad_cls_class;

    /* Resources */
    struct resource_list	ad_rl;
};

#ifdef INTRNG
struct intr_map_data_acpi {
	struct intr_map_data	hdr;
	u_int			irq;
	u_int			pol;
	u_int			trig;
};

#endif

/* Track device (/dev/{apm,apmctl} and /dev/acpi) notification status. */
struct apm_clone_data {
    STAILQ_ENTRY(apm_clone_data) entries;
    struct cdev 	*cdev;
    int			flags;
#define	ACPI_EVF_NONE	0	/* /dev/apm semantics */
#define	ACPI_EVF_DEVD	1	/* /dev/acpi is handled via devd(8) */
#define	ACPI_EVF_WRITE	2	/* Device instance is opened writable. */
    int			notify_status;
#define	APM_EV_NONE	0	/* Device not yet aware of pending sleep. */
#define	APM_EV_NOTIFIED	1	/* Device saw next sleep state. */
#define	APM_EV_ACKED	2	/* Device agreed sleep can occur. */
    struct acpi_softc	*acpi_sc;
    struct selinfo	sel_read;
};

#define ACPI_PRW_MAX_POWERRES	8

struct acpi_prw_data {
    ACPI_HANDLE		gpe_handle;
    int			gpe_bit;
    int			lowest_wake;
    ACPI_OBJECT		power_res[ACPI_PRW_MAX_POWERRES];
    int			power_res_count;
};

/* Flags for each device defined in the AML namespace. */
#define ACPI_FLAG_WAKE_ENABLED	0x1

/* Macros for extracting parts of a PCI address from an _ADR value. */
#define	ACPI_ADR_PCI_SLOT(adr)	(((adr) & 0xffff0000) >> 16)
#define	ACPI_ADR_PCI_FUNC(adr)	((adr) & 0xffff)

/*
 * Entry points to ACPI from above are global functions defined in this
 * file, sysctls, and I/O on the control device.  Entry points from below
 * are interrupts (the SCI), notifies, task queue threads, and the thermal
 * zone polling thread.
 *
 * ACPI tables and global shared data are protected by a global lock
 * (acpi_mutex).  
 *
 * Each ACPI device can have its own driver-specific mutex for protecting
 * shared access to local data.  The ACPI_LOCK macros handle mutexes.
 *
 * Drivers that need to serialize access to functions (e.g., to route
 * interrupts, get/set control paths, etc.) should use the sx lock macros
 * (ACPI_SERIAL).
 *
 * ACPI-CA handles its own locking and should not be called with locks held.
 *
 * The most complicated path is:
 *     GPE -> EC runs _Qxx -> _Qxx reads EC space -> GPE
 */
extern struct mtx			acpi_mutex;
#define ACPI_LOCK(sys)			mtx_lock(&sys##_mutex)
#define ACPI_UNLOCK(sys)		mtx_unlock(&sys##_mutex)
#define ACPI_LOCK_ASSERT(sys)		mtx_assert(&sys##_mutex, MA_OWNED);
#define ACPI_LOCK_DECL(sys, name)				\
	static struct mtx sys##_mutex;				\
	MTX_SYSINIT(sys##_mutex, &sys##_mutex, name, MTX_DEF)
#define ACPI_SERIAL_BEGIN(sys)		sx_xlock(&sys##_sxlock)
#define ACPI_SERIAL_END(sys)		sx_xunlock(&sys##_sxlock)
#define ACPI_SERIAL_ASSERT(sys)		sx_assert(&sys##_sxlock, SX_XLOCKED);
#define ACPI_SERIAL_DECL(sys, name)				\
	static struct sx sys##_sxlock;				\
	SX_SYSINIT(sys##_sxlock, &sys##_sxlock, name)

/*
 * ACPI CA does not define layers for non-ACPI CA drivers.
 * We define some here within the range provided.
 */
#define	ACPI_AC_ADAPTER		0x00010000
#define	ACPI_BATTERY		0x00020000
#define	ACPI_BUS		0x00040000
#define	ACPI_BUTTON		0x00080000
#define	ACPI_EC			0x00100000
#define	ACPI_FAN		0x00200000
#define	ACPI_POWERRES		0x00400000
#define	ACPI_PROCESSOR		0x00800000
#define	ACPI_THERMAL		0x01000000
#define	ACPI_TIMER		0x02000000
#define	ACPI_OEM		0x04000000

/*
 * Constants for different interrupt models used with acpi_SetIntrModel().
 */
#define	ACPI_INTR_PIC		0
#define	ACPI_INTR_APIC		1
#define	ACPI_INTR_SAPIC		2

/*
 * Various features and capabilities for the acpi_get_features() method.
 * In particular, these are used for the ACPI 3.0 _PDC and _OSC methods.
 * See the Intel document titled "Intel Processor Vendor-Specific ACPI",
 * number 302223-007.
 */
#define	ACPI_CAP_PERF_MSRS	(1 << 0)  /* Intel SpeedStep PERF_CTL MSRs */
#define	ACPI_CAP_C1_IO_HALT	(1 << 1)  /* Intel C1 "IO then halt" sequence */
#define	ACPI_CAP_THR_MSRS	(1 << 2)  /* Intel OnDemand throttling MSRs */
#define	ACPI_CAP_SMP_SAME	(1 << 3)  /* MP C1, Px, and Tx (all the same) */
#define	ACPI_CAP_SMP_SAME_C3	(1 << 4)  /* MP C2 and C3 (all the same) */
#define	ACPI_CAP_SMP_DIFF_PX	(1 << 5)  /* MP Px (different, using _PSD) */
#define	ACPI_CAP_SMP_DIFF_CX	(1 << 6)  /* MP Cx (different, using _CSD) */
#define	ACPI_CAP_SMP_DIFF_TX	(1 << 7)  /* MP Tx (different, using _TSD) */
#define	ACPI_CAP_SMP_C1_NATIVE	(1 << 8)  /* MP C1 support other than halt */
#define	ACPI_CAP_SMP_C3_NATIVE	(1 << 9)  /* MP C2 and C3 support */
#define	ACPI_CAP_PX_HW_COORD	(1 << 11) /* Intel P-state HW coordination */
#define	ACPI_CAP_INTR_CPPC	(1 << 12) /* Native Interrupt Handling for
	     Collaborative Processor Performance Control notifications */
#define	ACPI_CAP_HW_DUTY_C	(1 << 13) /* Hardware Duty Cycling */

/*
 * Quirk flags.
 *
 * ACPI_Q_BROKEN: Disables all ACPI support.
 * ACPI_Q_TIMER: Disables support for the ACPI timer.
 * ACPI_Q_MADT_IRQ0: Specifies that ISA IRQ 0 is wired up to pin 0 of the
 *	first APIC and that the MADT should force that by ignoring the PC-AT
 *	compatible flag and ignoring overrides that redirect IRQ 0 to pin 2.
 */
extern int	acpi_quirks;
#define ACPI_Q_OK		0
#define ACPI_Q_BROKEN		(1 << 0)
#define ACPI_Q_TIMER		(1 << 1)
#define ACPI_Q_MADT_IRQ0	(1 << 2)

/*
 * Note that the low ivar values are reserved to provide
 * interface compatibility with ISA drivers which can also
 * attach to ACPI.
 */
#define ACPI_IVAR_HANDLE	0x100
#define ACPI_IVAR_UNUSED	0x101	/* Unused/reserved. */
#define ACPI_IVAR_PRIVATE	0x102
#define ACPI_IVAR_FLAGS		0x103

/*
 * Accessor functions for our ivars.  Default value for BUS_READ_IVAR is
 * (type) 0.  The <sys/bus.h> accessor functions don't check return values.
 */
#define __ACPI_BUS_ACCESSOR(varp, var, ivarp, ivar, type)	\
								\
static __inline type varp ## _get_ ## var(device_t dev)		\
{								\
    uintptr_t v = 0;						\
    BUS_READ_IVAR(device_get_parent(dev), dev,			\
	ivarp ## _IVAR_ ## ivar, &v);				\
    return ((type) v);						\
}								\
								\
static __inline void varp ## _set_ ## var(device_t dev, type t)	\
{								\
    uintptr_t v = (uintptr_t) t;				\
    BUS_WRITE_IVAR(device_get_parent(dev), dev,			\
	ivarp ## _IVAR_ ## ivar, v);				\
}

__ACPI_BUS_ACCESSOR(acpi, handle, ACPI, HANDLE, ACPI_HANDLE)
__ACPI_BUS_ACCESSOR(acpi, private, ACPI, PRIVATE, void *)
__ACPI_BUS_ACCESSOR(acpi, flags, ACPI, FLAGS, int)

void acpi_fake_objhandler(ACPI_HANDLE h, void *data);
static __inline device_t
acpi_get_device(ACPI_HANDLE handle)
{
    void *dev = NULL;
    AcpiGetData(handle, acpi_fake_objhandler, &dev);
    return ((device_t)dev);
}

static __inline ACPI_OBJECT_TYPE
acpi_get_type(device_t dev)
{
    ACPI_HANDLE		h;
    ACPI_OBJECT_TYPE	t;

    if ((h = acpi_get_handle(dev)) == NULL)
	return (ACPI_TYPE_NOT_FOUND);
    if (ACPI_FAILURE(AcpiGetType(h, &t)))
	return (ACPI_TYPE_NOT_FOUND);
    return (t);
}

/* Find the difference between two PM tick counts. */
static __inline uint32_t
acpi_TimerDelta(uint32_t end, uint32_t start)
{

	if (end < start && (AcpiGbl_FADT.Flags & ACPI_FADT_32BIT_TIMER) == 0)
		end |= 0x01000000;
	return (end - start);
}

#ifdef ACPI_DEBUGGER
void		acpi_EnterDebugger(void);
#endif

#ifdef ACPI_DEBUG
#include <sys/cons.h>
#define STEP(x)		do {printf x, printf("\n"); cngetc();} while (0)
#else
#define STEP(x)
#endif

#define ACPI_VPRINT(dev, acpi_sc, x...) do {			\
    if (acpi_get_verbose(acpi_sc))				\
	device_printf(dev, x);					\
} while (0)

/* Values for the first status word returned by _OSC. */
#define	ACPI_OSC_FAILURE	(1 << 1)
#define	ACPI_OSC_BAD_UUID	(1 << 2)
#define	ACPI_OSC_BAD_REVISION	(1 << 3)
#define	ACPI_OSC_CAPS_MASKED	(1 << 4)

#define ACPI_DEVINFO_PRESENT(x, flags)					\
	(((x) & (flags)) == (flags))
#define ACPI_DEVICE_PRESENT(x)						\
	ACPI_DEVINFO_PRESENT(x, ACPI_STA_DEVICE_PRESENT |		\
	    ACPI_STA_DEVICE_FUNCTIONING)
#define ACPI_BATTERY_PRESENT(x)						\
	ACPI_DEVINFO_PRESENT(x, ACPI_STA_DEVICE_PRESENT |		\
	    ACPI_STA_DEVICE_FUNCTIONING | ACPI_STA_BATTERY_PRESENT)

/* Callback function type for walking subtables within a table. */
typedef void acpi_subtable_handler(ACPI_SUBTABLE_HEADER *, void *);

BOOLEAN		acpi_DeviceIsPresent(device_t dev);
BOOLEAN		acpi_BatteryIsPresent(device_t dev);
ACPI_STATUS	acpi_GetHandleInScope(ACPI_HANDLE parent, char *path,
		    ACPI_HANDLE *result);
ACPI_BUFFER	*acpi_AllocBuffer(int size);
ACPI_STATUS	acpi_ConvertBufferToInteger(ACPI_BUFFER *bufp,
		    UINT32 *number);
ACPI_STATUS	acpi_GetInteger(ACPI_HANDLE handle, char *path,
		    UINT32 *number);
ACPI_STATUS	acpi_SetInteger(ACPI_HANDLE handle, char *path,
		    UINT32 number);
ACPI_STATUS	acpi_ForeachPackageObject(ACPI_OBJECT *obj, 
		    void (*func)(ACPI_OBJECT *comp, void *arg), void *arg);
ACPI_STATUS	acpi_FindIndexedResource(ACPI_BUFFER *buf, int index,
		    ACPI_RESOURCE **resp);
ACPI_STATUS	acpi_AppendBufferResource(ACPI_BUFFER *buf,
		    ACPI_RESOURCE *res);
UINT8		acpi_DSMQuery(ACPI_HANDLE h, uint8_t *uuid, int revision);
ACPI_STATUS	acpi_EvaluateDSM(ACPI_HANDLE handle, uint8_t *uuid,
		    int revision, uint64_t function, union acpi_object *package,
		    ACPI_BUFFER *out_buf);
ACPI_STATUS	acpi_EvaluateOSC(ACPI_HANDLE handle, uint8_t *uuid,
		    int revision, int count, uint32_t *caps_in,
		    uint32_t *caps_out, bool query);
ACPI_STATUS	acpi_OverrideInterruptLevel(UINT32 InterruptNumber);
ACPI_STATUS	acpi_SetIntrModel(int model);
int		acpi_ReqSleepState(struct acpi_softc *sc, int state);
int		acpi_AckSleepState(struct apm_clone_data *clone, int error);
ACPI_STATUS	acpi_SetSleepState(struct acpi_softc *sc, int state);
int		acpi_wake_set_enable(device_t dev, int enable);
int		acpi_parse_prw(ACPI_HANDLE h, struct acpi_prw_data *prw);
ACPI_STATUS	acpi_Startup(void);
void		acpi_UserNotify(const char *subsystem, ACPI_HANDLE h,
		    uint8_t notify);
int		acpi_bus_alloc_gas(device_t dev, int *type, int *rid,
		    ACPI_GENERIC_ADDRESS *gas, struct resource **res,
		    u_int flags);
void		acpi_walk_subtables(void *first, void *end,
		    acpi_subtable_handler *handler, void *arg);
int		acpi_MatchHid(ACPI_HANDLE h, const char *hid);
#define ACPI_MATCHHID_NOMATCH 0
#define ACPI_MATCHHID_HID 1
#define ACPI_MATCHHID_CID 2


struct acpi_parse_resource_set {
    void	(*set_init)(device_t dev, void *arg, void **context);
    void	(*set_done)(device_t dev, void *context);
    void	(*set_ioport)(device_t dev, void *context, uint64_t base,
		    uint64_t length);
    void	(*set_iorange)(device_t dev, void *context, uint64_t low,
		    uint64_t high, uint64_t length, uint64_t align);
    void	(*set_memory)(device_t dev, void *context, uint64_t base,
		    uint64_t length);
    void	(*set_memoryrange)(device_t dev, void *context, uint64_t low,
		    uint64_t high, uint64_t length, uint64_t align);
    void	(*set_irq)(device_t dev, void *context, uint8_t *irq,
		    int count, int trig, int pol);
    void	(*set_ext_irq)(device_t dev, void *context, uint32_t *irq,
		    int count, int trig, int pol);
    void	(*set_drq)(device_t dev, void *context, uint8_t *drq,
		    int count);
    void	(*set_start_dependent)(device_t dev, void *context,
		    int preference);
    void	(*set_end_dependent)(device_t dev, void *context);
};

extern struct	acpi_parse_resource_set acpi_res_parse_set;

int		acpi_identify(void);
void		acpi_config_intr(device_t dev, ACPI_RESOURCE *res);
#ifdef INTRNG
int		acpi_map_intr(device_t dev, u_int irq, ACPI_HANDLE handle);
#endif
ACPI_STATUS	acpi_lookup_irq_resource(device_t dev, int rid,
		    struct resource *res, ACPI_RESOURCE *acpi_res);
ACPI_STATUS	acpi_parse_resources(device_t dev, ACPI_HANDLE handle,
		    struct acpi_parse_resource_set *set, void *arg);
struct resource *acpi_alloc_sysres(device_t child, int type, int *rid,
		    rman_res_t start, rman_res_t end, rman_res_t count,
		    u_int flags);

/* ACPI event handling */
UINT32		acpi_event_power_button_sleep(void *context);
UINT32		acpi_event_power_button_wake(void *context);
UINT32		acpi_event_sleep_button_sleep(void *context);
UINT32		acpi_event_sleep_button_wake(void *context);

#define ACPI_EVENT_PRI_FIRST      0
#define ACPI_EVENT_PRI_DEFAULT    10000
#define ACPI_EVENT_PRI_LAST       20000

typedef void (*acpi_event_handler_t)(void *, int);

EVENTHANDLER_DECLARE(acpi_sleep_event, acpi_event_handler_t);
EVENTHANDLER_DECLARE(acpi_wakeup_event, acpi_event_handler_t);

/* Device power control. */
ACPI_STATUS	acpi_pwr_wake_enable(ACPI_HANDLE consumer, int enable);
ACPI_STATUS	acpi_pwr_switch_consumer(ACPI_HANDLE consumer, int state);
int		acpi_device_pwr_for_sleep(device_t bus, device_t dev,
		    int *dstate);

/* APM emulation */
void		acpi_apm_init(struct acpi_softc *);

/* Misc. */
static __inline struct acpi_softc *
acpi_device_get_parent_softc(device_t child)
{
    device_t	parent;

    parent = device_get_parent(child);
    if (parent == NULL)
	return (NULL);
    return (device_get_softc(parent));
}

static __inline int
acpi_get_verbose(struct acpi_softc *sc)
{
    if (sc)
	return (sc->acpi_verbose);
    return (0);
}

char		*acpi_name(ACPI_HANDLE handle);
int		acpi_avoid(ACPI_HANDLE handle);
int		acpi_disabled(char *subsys);
int		acpi_machdep_init(device_t dev);
void		acpi_install_wakeup_handler(struct acpi_softc *sc);
int		acpi_sleep_machdep(struct acpi_softc *sc, int state);
int		acpi_wakeup_machdep(struct acpi_softc *sc, int state,
		    int sleep_result, int intr_enabled);
int		acpi_table_quirks(int *quirks);
int		acpi_machdep_quirks(int *quirks);

uint32_t	hpet_get_uid(device_t dev);

/* Battery Abstraction. */
struct acpi_battinfo;

int		acpi_battery_register(device_t dev);
int		acpi_battery_remove(device_t dev);
int		acpi_battery_get_units(void);
int		acpi_battery_get_info_expire(void);
int		acpi_battery_bst_valid(struct acpi_bst *bst);
int		acpi_battery_bif_valid(struct acpi_bif *bif);
int		acpi_battery_get_battinfo(device_t dev,
		    struct acpi_battinfo *info);

/* Embedded controller. */
void		acpi_ec_ecdt_probe(device_t);

/* AC adapter interface. */
int		acpi_acad_get_acline(int *);

/* Package manipulation convenience functions. */
#define ACPI_PKG_VALID(pkg, size)				\
    ((pkg) != NULL && (pkg)->Type == ACPI_TYPE_PACKAGE &&	\
     (pkg)->Package.Count >= (size))
int		acpi_PkgInt(ACPI_OBJECT *res, int idx, UINT64 *dst);
int		acpi_PkgInt32(ACPI_OBJECT *res, int idx, uint32_t *dst);
int		acpi_PkgStr(ACPI_OBJECT *res, int idx, void *dst, size_t size);
int		acpi_PkgGas(device_t dev, ACPI_OBJECT *res, int idx, int *type,
		    int *rid, struct resource **dst, u_int flags);
int		acpi_PkgFFH_IntelCpu(ACPI_OBJECT *res, int idx, int *vendor,
		    int *class, uint64_t *address, int *accsize);
ACPI_HANDLE	acpi_GetReference(ACPI_HANDLE scope, ACPI_OBJECT *obj);

/*
 * Base level for BUS_ADD_CHILD.  Special devices are added at orders less
 * than this, and normal devices at or above this level.  This keeps the
 * probe order sorted so that things like sysresource are available before
 * their children need them.
 */
#define	ACPI_DEV_BASE_ORDER	100

/* Default maximum number of tasks to enqueue. */
#ifndef ACPI_MAX_TASKS
#define	ACPI_MAX_TASKS		MAX(32, MAXCPU * 4)
#endif

/* Default number of task queue threads to start. */
#ifndef ACPI_MAX_THREADS
#define ACPI_MAX_THREADS	3
#endif

/* Use the device logging level for ktr(4). */
#define	KTR_ACPI		KTR_DEV

SYSCTL_DECL(_debug_acpi);

/*
 * Parse and use proximity information in SRAT and SLIT.
 */
int		acpi_pxm_init(int ncpus, vm_paddr_t maxphys);
void		acpi_pxm_parse_tables(void);
void		acpi_pxm_set_mem_locality(void);
void		acpi_pxm_set_cpu_locality(void);
void		acpi_pxm_free(void);

/*
 * Map a PXM to a VM domain.
 *
 * Returns the VM domain ID if found, or -1 if not found / invalid.
 */
int		acpi_map_pxm_to_vm_domainid(int pxm);
int		acpi_get_cpus(device_t dev, device_t child, enum cpu_sets op,
		    size_t setsize, cpuset_t *cpuset);
int		acpi_get_domain(device_t dev, device_t child, int *domain);

#ifdef __aarch64__
/*
 * ARM specific ACPI interfaces, relating to IORT table.
 */
int	acpi_iort_map_pci_msi(u_int seg, u_int rid, u_int *xref, u_int *devid);
int	acpi_iort_its_lookup(u_int its_id, u_int *xref, int *pxm);
#endif
#endif /* _KERNEL */
#endif /* !_ACPIVAR_H_ */
