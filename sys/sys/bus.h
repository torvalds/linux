/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997,1998,2003 Doug Rabson
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

#ifndef _SYS_BUS_H_
#define _SYS_BUS_H_

#include <machine/_limits.h>
#include <machine/_bus.h>
#include <sys/_bus_dma.h>
#include <sys/ioccom.h>

/**
 * @defgroup NEWBUS newbus - a generic framework for managing devices
 * @{
 */

/**
 * @brief Interface information structure.
 */
struct u_businfo {
	int	ub_version;		/**< @brief interface version */
#define BUS_USER_VERSION	2
	int	ub_generation;		/**< @brief generation count */
};

/**
 * @brief State of the device.
 */
typedef enum device_state {
	DS_NOTPRESENT = 10,		/**< @brief not probed or probe failed */
	DS_ALIVE = 20,			/**< @brief probe succeeded */
	DS_ATTACHING = 25,		/**< @brief currently attaching */
	DS_ATTACHED = 30,		/**< @brief attach method called */
	DS_BUSY = 40			/**< @brief device is open */
} device_state_t;

/**
 * @brief Device information exported to userspace.
 * The strings are placed one after the other, separated by NUL characters.
 * Fields should be added after the last one and order maintained for compatibility
 */
#define BUS_USER_BUFFER		(3*1024)
struct u_device {
	uintptr_t	dv_handle;
	uintptr_t	dv_parent;
	uint32_t	dv_devflags;		/**< @brief API Flags for device */
	uint16_t	dv_flags;		/**< @brief flags for dev state */
	device_state_t	dv_state;		/**< @brief State of attachment */
	char		dv_fields[BUS_USER_BUFFER]; /**< @brief NUL terminated fields */
	/* name (name of the device in tree) */
	/* desc (driver description) */
	/* drivername (Name of driver without unit number) */
	/* pnpinfo (Plug and play information from bus) */
	/* location (Location of device on parent */
	/* NUL */
};

/* Flags exported via dv_flags. */
#define	DF_ENABLED	0x01		/* device should be probed/attached */
#define	DF_FIXEDCLASS	0x02		/* devclass specified at create time */
#define	DF_WILDCARD	0x04		/* unit was originally wildcard */
#define	DF_DESCMALLOCED	0x08		/* description was malloced */
#define	DF_QUIET	0x10		/* don't print verbose attach message */
#define	DF_DONENOMATCH	0x20		/* don't execute DEVICE_NOMATCH again */
#define	DF_EXTERNALSOFTC 0x40		/* softc not allocated by us */
#define	DF_REBID	0x80		/* Can rebid after attach */
#define	DF_SUSPENDED	0x100		/* Device is suspended. */
#define	DF_QUIET_CHILDREN 0x200		/* Default to quiet for all my children */
#define	DF_ATTACHED_ONCE 0x400		/* Has been attached at least once */
#define	DF_NEEDNOMATCH	0x800		/* Has a pending NOMATCH event */

/**
 * @brief Device request structure used for ioctl's.
 *
 * Used for ioctl's on /dev/devctl2.  All device ioctl's
 * must have parameter definitions which begin with dr_name.
 */
struct devreq_buffer {
	void	*buffer;
	size_t	length;
};

struct devreq {
	char		dr_name[128];
	int		dr_flags;		/* request-specific flags */
	union {
		struct devreq_buffer dru_buffer;
		void	*dru_data;
	} dr_dru;
#define	dr_buffer	dr_dru.dru_buffer	/* variable-sized buffer */
#define	dr_data		dr_dru.dru_data		/* fixed-size buffer */
};

#define	DEV_ATTACH	_IOW('D', 1, struct devreq)
#define	DEV_DETACH	_IOW('D', 2, struct devreq)
#define	DEV_ENABLE	_IOW('D', 3, struct devreq)
#define	DEV_DISABLE	_IOW('D', 4, struct devreq)
#define	DEV_SUSPEND	_IOW('D', 5, struct devreq)
#define	DEV_RESUME	_IOW('D', 6, struct devreq)
#define	DEV_SET_DRIVER	_IOW('D', 7, struct devreq)
#define	DEV_CLEAR_DRIVER _IOW('D', 8, struct devreq)
#define	DEV_RESCAN	_IOW('D', 9, struct devreq)
#define	DEV_DELETE	_IOW('D', 10, struct devreq)
#define	DEV_FREEZE	_IOW('D', 11, struct devreq)
#define	DEV_THAW	_IOW('D', 12, struct devreq)

/* Flags for DEV_DETACH and DEV_DISABLE. */
#define	DEVF_FORCE_DETACH	0x0000001

/* Flags for DEV_SET_DRIVER. */
#define	DEVF_SET_DRIVER_DETACH	0x0000001	/* Detach existing driver. */

/* Flags for DEV_CLEAR_DRIVER. */
#define	DEVF_CLEAR_DRIVER_DETACH 0x0000001	/* Detach existing driver. */

/* Flags for DEV_DELETE. */
#define	DEVF_FORCE_DELETE	0x0000001

#ifdef _KERNEL

#include <sys/eventhandler.h>
#include <sys/kobj.h>

/**
 * devctl hooks.  Typically one should use the devctl_notify
 * hook to send the message.  However, devctl_queue_data is also
 * included in case devctl_notify isn't sufficiently general.
 */
boolean_t devctl_process_running(void);
void devctl_notify_f(const char *__system, const char *__subsystem,
    const char *__type, const char *__data, int __flags);
void devctl_notify(const char *__system, const char *__subsystem,
    const char *__type, const char *__data);
void devctl_queue_data_f(char *__data, int __flags);
void devctl_queue_data(char *__data);
struct sbuf;
void devctl_safe_quote_sb(struct sbuf *__sb, const char *__src);

/**
 * Device name parsers.  Hook to allow device enumerators to map
 * scheme-specific names to a device.
 */
typedef void (*dev_lookup_fn)(void *arg, const char *name,
    device_t *result);
EVENTHANDLER_DECLARE(dev_lookup, dev_lookup_fn);

/**
 * @brief A device driver (included mainly for compatibility with
 * FreeBSD 4.x).
 */
typedef struct kobj_class	driver_t;

/**
 * @brief A device class
 *
 * The devclass object has two main functions in the system. The first
 * is to manage the allocation of unit numbers for device instances
 * and the second is to hold the list of device drivers for a
 * particular bus type. Each devclass has a name and there cannot be
 * two devclasses with the same name. This ensures that unique unit
 * numbers are allocated to device instances.
 *
 * Drivers that support several different bus attachments (e.g. isa,
 * pci, pccard) should all use the same devclass to ensure that unit
 * numbers do not conflict.
 *
 * Each devclass may also have a parent devclass. This is used when
 * searching for device drivers to allow a form of inheritance. When
 * matching drivers with devices, first the driver list of the parent
 * device's devclass is searched. If no driver is found in that list,
 * the search continues in the parent devclass (if any).
 */
typedef struct devclass		*devclass_t;

/**
 * @brief A device method
 */
#define device_method_t		kobj_method_t

/**
 * @brief Driver interrupt filter return values
 *
 * If a driver provides an interrupt filter routine it must return an
 * integer consisting of oring together zero or more of the following
 * flags:
 *
 *	FILTER_STRAY	- this device did not trigger the interrupt
 *	FILTER_HANDLED	- the interrupt has been fully handled and can be EOId
 *	FILTER_SCHEDULE_THREAD - the threaded interrupt handler should be
 *			  scheduled to execute
 *
 * If the driver does not provide a filter, then the interrupt code will
 * act is if the filter had returned FILTER_SCHEDULE_THREAD.  Note that it
 * is illegal to specify any other flag with FILTER_STRAY and that it is
 * illegal to not specify either of FILTER_HANDLED or FILTER_SCHEDULE_THREAD
 * if FILTER_STRAY is not specified.
 */
#define	FILTER_STRAY		0x01
#define	FILTER_HANDLED		0x02
#define	FILTER_SCHEDULE_THREAD	0x04

/**
 * @brief Driver interrupt service routines
 *
 * The filter routine is run in primary interrupt context and may not
 * block or use regular mutexes.  It may only use spin mutexes for
 * synchronization.  The filter may either completely handle the
 * interrupt or it may perform some of the work and defer more
 * expensive work to the regular interrupt handler.  If a filter
 * routine is not registered by the driver, then the regular interrupt
 * handler is always used to handle interrupts from this device.
 *
 * The regular interrupt handler executes in its own thread context
 * and may use regular mutexes.  However, it is prohibited from
 * sleeping on a sleep queue.
 */
typedef int driver_filter_t(void*);
typedef void driver_intr_t(void*);

/**
 * @brief Interrupt type bits.
 * 
 * These flags are used both by newbus interrupt
 * registration (nexus.c) and also in struct intrec, which defines
 * interrupt properties.
 *
 * XXX We should probably revisit this and remove the vestiges of the
 * spls implicit in names like INTR_TYPE_TTY. In the meantime, don't
 * confuse things by renaming them (Grog, 18 July 2000).
 *
 * Buses which do interrupt remapping will want to change their type
 * to reflect what sort of devices are underneath.
 */
enum intr_type {
	INTR_TYPE_TTY = 1,
	INTR_TYPE_BIO = 2,
	INTR_TYPE_NET = 4,
	INTR_TYPE_CAM = 8,
	INTR_TYPE_MISC = 16,
	INTR_TYPE_CLK = 32,
	INTR_TYPE_AV = 64,
	INTR_EXCL = 256,		/* exclusive interrupt */
	INTR_MPSAFE = 512,		/* this interrupt is SMP safe */
	INTR_ENTROPY = 1024,		/* this interrupt provides entropy */
	INTR_MD1 = 4096,		/* flag reserved for MD use */
	INTR_MD2 = 8192,		/* flag reserved for MD use */
	INTR_MD3 = 16384,		/* flag reserved for MD use */
	INTR_MD4 = 32768		/* flag reserved for MD use */
};

enum intr_trigger {
	INTR_TRIGGER_INVALID = -1,
	INTR_TRIGGER_CONFORM = 0,
	INTR_TRIGGER_EDGE = 1,
	INTR_TRIGGER_LEVEL = 2
};

enum intr_polarity {
	INTR_POLARITY_CONFORM = 0,
	INTR_POLARITY_HIGH = 1,
	INTR_POLARITY_LOW = 2
};

/**
 * CPU sets supported by bus_get_cpus().  Note that not all sets may be
 * supported for a given device.  If a request is not supported by a
 * device (or its parents), then bus_get_cpus() will fail with EINVAL.
 */
enum cpu_sets {
	LOCAL_CPUS = 0,
	INTR_CPUS
};

typedef int (*devop_t)(void);

/**
 * @brief This structure is deprecated.
 *
 * Use the kobj(9) macro DEFINE_CLASS to
 * declare classes which implement device drivers.
 */
struct driver {
	KOBJ_CLASS_FIELDS;
};

/**
 * @brief A resource mapping.
 */
struct resource_map {
	bus_space_tag_t r_bustag;
	bus_space_handle_t r_bushandle;
	bus_size_t r_size;
	void	*r_vaddr;
};
	
/**
 * @brief Optional properties of a resource mapping request.
 */
struct resource_map_request {
	size_t	size;
	rman_res_t offset;
	rman_res_t length;
	vm_memattr_t memattr;
};

void	resource_init_map_request_impl(struct resource_map_request *_args,
	    size_t _sz);
#define	resource_init_map_request(rmr) 					\
	resource_init_map_request_impl((rmr), sizeof(*(rmr)))

/*
 * Definitions for drivers which need to keep simple lists of resources
 * for their child devices.
 */
struct	resource;

/**
 * @brief An entry for a single resource in a resource list.
 */
struct resource_list_entry {
	STAILQ_ENTRY(resource_list_entry) link;
	int	type;			/**< @brief type argument to alloc_resource */
	int	rid;			/**< @brief resource identifier */
	int	flags;			/**< @brief resource flags */
	struct	resource *res;		/**< @brief the real resource when allocated */
	rman_res_t	start;		/**< @brief start of resource range */
	rman_res_t	end;		/**< @brief end of resource range */
	rman_res_t	count;			/**< @brief count within range */
};
STAILQ_HEAD(resource_list, resource_list_entry);

#define	RLE_RESERVED		0x0001	/* Reserved by the parent bus. */
#define	RLE_ALLOCATED		0x0002	/* Reserved resource is allocated. */
#define	RLE_PREFETCH		0x0004	/* Resource is a prefetch range. */

void	resource_list_init(struct resource_list *rl);
void	resource_list_free(struct resource_list *rl);
struct resource_list_entry *
	resource_list_add(struct resource_list *rl,
			  int type, int rid,
			  rman_res_t start, rman_res_t end, rman_res_t count);
int	resource_list_add_next(struct resource_list *rl,
			  int type,
			  rman_res_t start, rman_res_t end, rman_res_t count);
int	resource_list_busy(struct resource_list *rl,
			   int type, int rid);
int	resource_list_reserved(struct resource_list *rl, int type, int rid);
struct resource_list_entry*
	resource_list_find(struct resource_list *rl,
			   int type, int rid);
void	resource_list_delete(struct resource_list *rl,
			     int type, int rid);
struct resource *
	resource_list_alloc(struct resource_list *rl,
			    device_t bus, device_t child,
			    int type, int *rid,
			    rman_res_t start, rman_res_t end,
			    rman_res_t count, u_int flags);
int	resource_list_release(struct resource_list *rl,
			      device_t bus, device_t child,
			      int type, int rid, struct resource *res);
int	resource_list_release_active(struct resource_list *rl,
				     device_t bus, device_t child,
				     int type);
struct resource *
	resource_list_reserve(struct resource_list *rl,
			      device_t bus, device_t child,
			      int type, int *rid,
			      rman_res_t start, rman_res_t end,
			      rman_res_t count, u_int flags);
int	resource_list_unreserve(struct resource_list *rl,
				device_t bus, device_t child,
				int type, int rid);
void	resource_list_purge(struct resource_list *rl);
int	resource_list_print_type(struct resource_list *rl,
				 const char *name, int type,
				 const char *format);

/*
 * The root bus, to which all top-level buses are attached.
 */
extern device_t root_bus;
extern devclass_t root_devclass;
void	root_bus_configure(void);

/*
 * Useful functions for implementing buses.
 */

int	bus_generic_activate_resource(device_t dev, device_t child, int type,
				      int rid, struct resource *r);
device_t
	bus_generic_add_child(device_t dev, u_int order, const char *name,
			      int unit);
int	bus_generic_adjust_resource(device_t bus, device_t child, int type,
				    struct resource *r, rman_res_t start,
				    rman_res_t end);
struct resource *
	bus_generic_alloc_resource(device_t bus, device_t child, int type,
				   int *rid, rman_res_t start, rman_res_t end,
				   rman_res_t count, u_int flags);
int	bus_generic_attach(device_t dev);
int	bus_generic_bind_intr(device_t dev, device_t child,
			      struct resource *irq, int cpu);
int	bus_generic_child_present(device_t dev, device_t child);
int	bus_generic_config_intr(device_t, int, enum intr_trigger,
				enum intr_polarity);
int	bus_generic_describe_intr(device_t dev, device_t child,
				  struct resource *irq, void *cookie,
				  const char *descr);
int	bus_generic_deactivate_resource(device_t dev, device_t child, int type,
					int rid, struct resource *r);
int	bus_generic_detach(device_t dev);
void	bus_generic_driver_added(device_t dev, driver_t *driver);
int	bus_generic_get_cpus(device_t dev, device_t child, enum cpu_sets op,
			     size_t setsize, struct _cpuset *cpuset);
bus_dma_tag_t
	bus_generic_get_dma_tag(device_t dev, device_t child);
bus_space_tag_t
	bus_generic_get_bus_tag(device_t dev, device_t child);
int	bus_generic_get_domain(device_t dev, device_t child, int *domain);
struct resource_list *
	bus_generic_get_resource_list (device_t, device_t);
int	bus_generic_map_resource(device_t dev, device_t child, int type,
				 struct resource *r,
				 struct resource_map_request *args,
				 struct resource_map *map);
void	bus_generic_new_pass(device_t dev);
int	bus_print_child_header(device_t dev, device_t child);
int	bus_print_child_domain(device_t dev, device_t child);
int	bus_print_child_footer(device_t dev, device_t child);
int	bus_generic_print_child(device_t dev, device_t child);
int	bus_generic_probe(device_t dev);
int	bus_generic_read_ivar(device_t dev, device_t child, int which,
			      uintptr_t *result);
int	bus_generic_release_resource(device_t bus, device_t child,
				     int type, int rid, struct resource *r);
int	bus_generic_resume(device_t dev);
int	bus_generic_resume_child(device_t dev, device_t child);
int	bus_generic_setup_intr(device_t dev, device_t child,
			       struct resource *irq, int flags,
			       driver_filter_t *filter, driver_intr_t *intr, 
			       void *arg, void **cookiep);

struct resource *
	bus_generic_rl_alloc_resource (device_t, device_t, int, int *,
				       rman_res_t, rman_res_t, rman_res_t, u_int);
void	bus_generic_rl_delete_resource (device_t, device_t, int, int);
int	bus_generic_rl_get_resource (device_t, device_t, int, int, rman_res_t *,
				     rman_res_t *);
int	bus_generic_rl_set_resource (device_t, device_t, int, int, rman_res_t,
				     rman_res_t);
int	bus_generic_rl_release_resource (device_t, device_t, int, int,
					 struct resource *);

int	bus_generic_shutdown(device_t dev);
int	bus_generic_suspend(device_t dev);
int	bus_generic_suspend_child(device_t dev, device_t child);
int	bus_generic_teardown_intr(device_t dev, device_t child,
				  struct resource *irq, void *cookie);
int	bus_generic_suspend_intr(device_t dev, device_t child,
				  struct resource *irq);
int	bus_generic_resume_intr(device_t dev, device_t child,
				  struct resource *irq);
int	bus_generic_unmap_resource(device_t dev, device_t child, int type,
				   struct resource *r,
				   struct resource_map *map);
int	bus_generic_write_ivar(device_t dev, device_t child, int which,
			       uintptr_t value);
int	bus_null_rescan(device_t dev);

/*
 * Wrapper functions for the BUS_*_RESOURCE methods to make client code
 * a little simpler.
 */

struct resource_spec {
	int	type;
	int	rid;
	int	flags;
};
#define	RESOURCE_SPEC_END	{-1, 0, 0}

int	bus_alloc_resources(device_t dev, struct resource_spec *rs,
			    struct resource **res);
void	bus_release_resources(device_t dev, const struct resource_spec *rs,
			      struct resource **res);

int	bus_adjust_resource(device_t child, int type, struct resource *r,
			    rman_res_t start, rman_res_t end);
struct	resource *bus_alloc_resource(device_t dev, int type, int *rid,
				     rman_res_t start, rman_res_t end,
				     rman_res_t count, u_int flags);
int	bus_activate_resource(device_t dev, int type, int rid,
			      struct resource *r);
int	bus_deactivate_resource(device_t dev, int type, int rid,
				struct resource *r);
int	bus_map_resource(device_t dev, int type, struct resource *r,
			 struct resource_map_request *args,
			 struct resource_map *map);
int	bus_unmap_resource(device_t dev, int type, struct resource *r,
			   struct resource_map *map);
int	bus_get_cpus(device_t dev, enum cpu_sets op, size_t setsize,
		     struct _cpuset *cpuset);
bus_dma_tag_t bus_get_dma_tag(device_t dev);
bus_space_tag_t bus_get_bus_tag(device_t dev);
int	bus_get_domain(device_t dev, int *domain);
int	bus_release_resource(device_t dev, int type, int rid,
			     struct resource *r);
int	bus_free_resource(device_t dev, int type, struct resource *r);
int	bus_setup_intr(device_t dev, struct resource *r, int flags,
		       driver_filter_t filter, driver_intr_t handler, 
		       void *arg, void **cookiep);
int	bus_teardown_intr(device_t dev, struct resource *r, void *cookie);
int	bus_suspend_intr(device_t dev, struct resource *r);
int	bus_resume_intr(device_t dev, struct resource *r);
int	bus_bind_intr(device_t dev, struct resource *r, int cpu);
int	bus_describe_intr(device_t dev, struct resource *irq, void *cookie,
			  const char *fmt, ...) __printflike(4, 5);
int	bus_set_resource(device_t dev, int type, int rid,
			 rman_res_t start, rman_res_t count);
int	bus_get_resource(device_t dev, int type, int rid,
			 rman_res_t *startp, rman_res_t *countp);
rman_res_t	bus_get_resource_start(device_t dev, int type, int rid);
rman_res_t	bus_get_resource_count(device_t dev, int type, int rid);
void	bus_delete_resource(device_t dev, int type, int rid);
int	bus_child_present(device_t child);
int	bus_child_pnpinfo_str(device_t child, char *buf, size_t buflen);
int	bus_child_location_str(device_t child, char *buf, size_t buflen);
void	bus_enumerate_hinted_children(device_t bus);

static __inline struct resource *
bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags)
{
	return (bus_alloc_resource(dev, type, rid, 0, ~0, 1, flags));
}

static __inline struct resource *
bus_alloc_resource_anywhere(device_t dev, int type, int *rid,
    rman_res_t count, u_int flags)
{
	return (bus_alloc_resource(dev, type, rid, 0, ~0, count, flags));
}

/*
 * Access functions for device.
 */
device_t	device_add_child(device_t dev, const char *name, int unit);
device_t	device_add_child_ordered(device_t dev, u_int order,
					 const char *name, int unit);
void	device_busy(device_t dev);
int	device_delete_child(device_t dev, device_t child);
int	device_delete_children(device_t dev);
int	device_attach(device_t dev);
int	device_detach(device_t dev);
void	device_disable(device_t dev);
void	device_enable(device_t dev);
device_t	device_find_child(device_t dev, const char *classname,
				  int unit);
const char	*device_get_desc(device_t dev);
devclass_t	device_get_devclass(device_t dev);
driver_t	*device_get_driver(device_t dev);
u_int32_t	device_get_flags(device_t dev);
device_t	device_get_parent(device_t dev);
int	device_get_children(device_t dev, device_t **listp, int *countp);
void	*device_get_ivars(device_t dev);
void	device_set_ivars(device_t dev, void *ivars);
const	char *device_get_name(device_t dev);
const	char *device_get_nameunit(device_t dev);
void	*device_get_softc(device_t dev);
device_state_t	device_get_state(device_t dev);
int	device_get_unit(device_t dev);
struct sysctl_ctx_list *device_get_sysctl_ctx(device_t dev);
struct sysctl_oid *device_get_sysctl_tree(device_t dev);
int	device_has_quiet_children(device_t dev);
int	device_is_alive(device_t dev);	/* did probe succeed? */
int	device_is_attached(device_t dev);	/* did attach succeed? */
int	device_is_enabled(device_t dev);
int	device_is_suspended(device_t dev);
int	device_is_quiet(device_t dev);
device_t device_lookup_by_name(const char *name);
int	device_print_prettyname(device_t dev);
int	device_printf(device_t dev, const char *, ...) __printflike(2, 3);
int	device_probe(device_t dev);
int	device_probe_and_attach(device_t dev);
int	device_probe_child(device_t bus, device_t dev);
int	device_quiesce(device_t dev);
void	device_quiet(device_t dev);
void	device_quiet_children(device_t dev);
void	device_set_desc(device_t dev, const char* desc);
void	device_set_desc_copy(device_t dev, const char* desc);
int	device_set_devclass(device_t dev, const char *classname);
int	device_set_devclass_fixed(device_t dev, const char *classname);
bool	device_is_devclass_fixed(device_t dev);
int	device_set_driver(device_t dev, driver_t *driver);
void	device_set_flags(device_t dev, u_int32_t flags);
void	device_set_softc(device_t dev, void *softc);
void	device_free_softc(void *softc);
void	device_claim_softc(device_t dev);
int	device_set_unit(device_t dev, int unit);	/* XXX DONT USE XXX */
int	device_shutdown(device_t dev);
void	device_unbusy(device_t dev);
void	device_verbose(device_t dev);

/*
 * Access functions for devclass.
 */
int		devclass_add_driver(devclass_t dc, driver_t *driver,
				    int pass, devclass_t *dcp);
devclass_t	devclass_create(const char *classname);
int		devclass_delete_driver(devclass_t busclass, driver_t *driver);
devclass_t	devclass_find(const char *classname);
const char	*devclass_get_name(devclass_t dc);
device_t	devclass_get_device(devclass_t dc, int unit);
void	*devclass_get_softc(devclass_t dc, int unit);
int	devclass_get_devices(devclass_t dc, device_t **listp, int *countp);
int	devclass_get_drivers(devclass_t dc, driver_t ***listp, int *countp);
int	devclass_get_count(devclass_t dc);
int	devclass_get_maxunit(devclass_t dc);
int	devclass_find_free_unit(devclass_t dc, int unit);
void	devclass_set_parent(devclass_t dc, devclass_t pdc);
devclass_t	devclass_get_parent(devclass_t dc);
struct sysctl_ctx_list *devclass_get_sysctl_ctx(devclass_t dc);
struct sysctl_oid *devclass_get_sysctl_tree(devclass_t dc);

/*
 * Access functions for device resources.
 */
int	resource_int_value(const char *name, int unit, const char *resname,
			   int *result);
int	resource_long_value(const char *name, int unit, const char *resname,
			    long *result);
int	resource_string_value(const char *name, int unit, const char *resname,
			      const char **result);
int	resource_disabled(const char *name, int unit);
int	resource_find_match(int *anchor, const char **name, int *unit,
			    const char *resname, const char *value);
int	resource_find_dev(int *anchor, const char *name, int *unit,
			  const char *resname, const char *value);
int	resource_unset_value(const char *name, int unit, const char *resname);

/*
 * Functions for maintaining and checking consistency of
 * bus information exported to userspace.
 */
int	bus_data_generation_check(int generation);
void	bus_data_generation_update(void);

/**
 * Some convenience defines for probe routines to return.  These are just
 * suggested values, and there's nothing magical about them.
 * BUS_PROBE_SPECIFIC is for devices that cannot be reprobed, and that no
 * possible other driver may exist (typically legacy drivers who don't follow
 * all the rules, or special needs drivers).  BUS_PROBE_VENDOR is the
 * suggested value that vendor supplied drivers use.  This is for source or
 * binary drivers that are not yet integrated into the FreeBSD tree.  Its use
 * in the base OS is prohibited.  BUS_PROBE_DEFAULT is the normal return value
 * for drivers to use.  It is intended that nearly all of the drivers in the
 * tree should return this value.  BUS_PROBE_LOW_PRIORITY are for drivers that
 * have special requirements like when there are two drivers that support
 * overlapping series of hardware devices.  In this case the one that supports
 * the older part of the line would return this value, while the one that
 * supports the newer ones would return BUS_PROBE_DEFAULT.  BUS_PROBE_GENERIC
 * is for drivers that wish to have a generic form and a specialized form,
 * like is done with the pci bus and the acpi pci bus.  BUS_PROBE_HOOVER is
 * for those buses that implement a generic device placeholder for devices on
 * the bus that have no more specific driver for them (aka ugen).
 * BUS_PROBE_NOWILDCARD or lower means that the device isn't really bidding
 * for a device node, but accepts only devices that its parent has told it
 * use this driver.
 */
#define BUS_PROBE_SPECIFIC	0	/* Only I can use this device */
#define BUS_PROBE_VENDOR	(-10)	/* Vendor supplied driver */
#define BUS_PROBE_DEFAULT	(-20)	/* Base OS default driver */
#define BUS_PROBE_LOW_PRIORITY	(-40)	/* Older, less desirable drivers */
#define BUS_PROBE_GENERIC	(-100)	/* generic driver for dev */
#define BUS_PROBE_HOOVER	(-1000000) /* Driver for any dev on bus */
#define BUS_PROBE_NOWILDCARD	(-2000000000) /* No wildcard device matches */

/**
 * During boot, the device tree is scanned multiple times.  Each scan,
 * or pass, drivers may be attached to devices.  Each driver
 * attachment is assigned a pass number.  Drivers may only probe and
 * attach to devices if their pass number is less than or equal to the
 * current system-wide pass number.  The default pass is the last pass
 * and is used by most drivers.  Drivers needed by the scheduler are
 * probed in earlier passes.
 */
#define	BUS_PASS_ROOT		0	/* Used to attach root0. */
#define	BUS_PASS_BUS		10	/* Buses and bridges. */
#define	BUS_PASS_CPU		20	/* CPU devices. */
#define	BUS_PASS_RESOURCE	30	/* Resource discovery. */
#define	BUS_PASS_INTERRUPT	40	/* Interrupt controllers. */
#define	BUS_PASS_TIMER		50	/* Timers and clocks. */
#define	BUS_PASS_SCHEDULER	60	/* Start scheduler. */
#define	BUS_PASS_SUPPORTDEV	100000	/* Drivers which support DEFAULT drivers. */
#define	BUS_PASS_DEFAULT	__INT_MAX /* Everything else. */

#define	BUS_PASS_ORDER_FIRST	0
#define	BUS_PASS_ORDER_EARLY	2
#define	BUS_PASS_ORDER_MIDDLE	5
#define	BUS_PASS_ORDER_LATE	7
#define	BUS_PASS_ORDER_LAST	9

extern int bus_current_pass;

void	bus_set_pass(int pass);

/**
 * Shorthands for constructing method tables.
 */
#define	DEVMETHOD	KOBJMETHOD
#define	DEVMETHOD_END	KOBJMETHOD_END

/*
 * Some common device interfaces.
 */
#include "device_if.h"
#include "bus_if.h"

struct	module;

int	driver_module_handler(struct module *, int, void *);

/**
 * Module support for automatically adding drivers to buses.
 */
struct driver_module_data {
	int		(*dmd_chainevh)(struct module *, int, void *);
	void		*dmd_chainarg;
	const char	*dmd_busname;
	kobj_class_t	dmd_driver;
	devclass_t	*dmd_devclass;
	int		dmd_pass;
};

#define	EARLY_DRIVER_MODULE_ORDERED(name, busname, driver, devclass,	\
    evh, arg, order, pass)						\
									\
static struct driver_module_data name##_##busname##_driver_mod = {	\
	evh, arg,							\
	#busname,							\
	(kobj_class_t) &driver,						\
	&devclass,							\
	pass								\
};									\
									\
static moduledata_t name##_##busname##_mod = {				\
	#busname "/" #name,						\
	driver_module_handler,						\
	&name##_##busname##_driver_mod					\
};									\
DECLARE_MODULE(name##_##busname, name##_##busname##_mod,		\
	       SI_SUB_DRIVERS, order)

#define	EARLY_DRIVER_MODULE(name, busname, driver, devclass, evh, arg, pass) \
	EARLY_DRIVER_MODULE_ORDERED(name, busname, driver, devclass,	\
	    evh, arg, SI_ORDER_MIDDLE, pass)

#define	DRIVER_MODULE_ORDERED(name, busname, driver, devclass, evh, arg,\
    order)								\
	EARLY_DRIVER_MODULE_ORDERED(name, busname, driver, devclass,	\
	    evh, arg, order, BUS_PASS_DEFAULT)

#define	DRIVER_MODULE(name, busname, driver, devclass, evh, arg)	\
	EARLY_DRIVER_MODULE(name, busname, driver, devclass, evh, arg,	\
	    BUS_PASS_DEFAULT)

/**
 * Generic ivar accessor generation macros for bus drivers
 */
#define __BUS_ACCESSOR(varp, var, ivarp, ivar, type)			\
									\
static __inline type varp ## _get_ ## var(device_t dev)			\
{									\
	uintptr_t v;							\
	BUS_READ_IVAR(device_get_parent(dev), dev,			\
	    ivarp ## _IVAR_ ## ivar, &v);				\
	return ((type) v);						\
}									\
									\
static __inline void varp ## _set_ ## var(device_t dev, type t)		\
{									\
	uintptr_t v = (uintptr_t) t;					\
	BUS_WRITE_IVAR(device_get_parent(dev), dev,			\
	    ivarp ## _IVAR_ ## ivar, v);				\
}

/**
 * Shorthand macros, taking resource argument
 * Generated with sys/tools/bus_macro.sh
 */

#define bus_barrier(r, o, l, f) \
	bus_space_barrier((r)->r_bustag, (r)->r_bushandle, (o), (l), (f))
#define bus_read_1(r, o) \
	bus_space_read_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_1(r, o, d, c) \
	bus_space_read_multi_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_1(r, o, d, c) \
	bus_space_read_region_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_1(r, o, v, c) \
	bus_space_set_multi_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_1(r, o, v, c) \
	bus_space_set_region_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_1(r, o, v) \
	bus_space_write_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_1(r, o, d, c) \
	bus_space_write_multi_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_1(r, o, d, c) \
	bus_space_write_region_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_1(r, o) \
	bus_space_read_stream_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_1(r, o, d, c) \
	bus_space_read_multi_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_1(r, o, d, c) \
	bus_space_read_region_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_1(r, o, v, c) \
	bus_space_set_multi_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_1(r, o, v, c) \
	bus_space_set_region_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_1(r, o, v) \
	bus_space_write_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_1(r, o, d, c) \
	bus_space_write_multi_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_1(r, o, d, c) \
	bus_space_write_region_stream_1((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_2(r, o) \
	bus_space_read_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_2(r, o, d, c) \
	bus_space_read_multi_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_2(r, o, d, c) \
	bus_space_read_region_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_2(r, o, v, c) \
	bus_space_set_multi_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_2(r, o, v, c) \
	bus_space_set_region_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_2(r, o, v) \
	bus_space_write_2((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_2(r, o, d, c) \
	bus_space_write_multi_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_2(r, o, d, c) \
	bus_space_write_region_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_2(r, o) \
	bus_space_read_stream_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_2(r, o, d, c) \
	bus_space_read_multi_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_2(r, o, d, c) \
	bus_space_read_region_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_2(r, o, v, c) \
	bus_space_set_multi_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_2(r, o, v, c) \
	bus_space_set_region_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_2(r, o, v) \
	bus_space_write_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_2(r, o, d, c) \
	bus_space_write_multi_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_2(r, o, d, c) \
	bus_space_write_region_stream_2((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_4(r, o) \
	bus_space_read_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_4(r, o, d, c) \
	bus_space_read_multi_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_4(r, o, d, c) \
	bus_space_read_region_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_4(r, o, v, c) \
	bus_space_set_multi_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_4(r, o, v, c) \
	bus_space_set_region_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_4(r, o, v) \
	bus_space_write_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_4(r, o, d, c) \
	bus_space_write_multi_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_4(r, o, d, c) \
	bus_space_write_region_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_4(r, o) \
	bus_space_read_stream_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_4(r, o, d, c) \
	bus_space_read_multi_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_4(r, o, d, c) \
	bus_space_read_region_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_4(r, o, v, c) \
	bus_space_set_multi_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_4(r, o, v, c) \
	bus_space_set_region_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_4(r, o, v) \
	bus_space_write_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_4(r, o, d, c) \
	bus_space_write_multi_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_4(r, o, d, c) \
	bus_space_write_region_stream_4((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_8(r, o) \
	bus_space_read_8((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_8(r, o, d, c) \
	bus_space_read_multi_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_8(r, o, d, c) \
	bus_space_read_region_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_8(r, o, v, c) \
	bus_space_set_multi_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_8(r, o, v, c) \
	bus_space_set_region_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_8(r, o, v) \
	bus_space_write_8((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_8(r, o, d, c) \
	bus_space_write_multi_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_8(r, o, d, c) \
	bus_space_write_region_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_stream_8(r, o) \
	bus_space_read_stream_8((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_multi_stream_8(r, o, d, c) \
	bus_space_read_multi_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_read_region_stream_8(r, o, d, c) \
	bus_space_read_region_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_set_multi_stream_8(r, o, v, c) \
	bus_space_set_multi_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_set_region_stream_8(r, o, v, c) \
	bus_space_set_region_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (v), (c))
#define bus_write_stream_8(r, o, v) \
	bus_space_write_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_multi_stream_8(r, o, d, c) \
	bus_space_write_multi_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#define bus_write_region_stream_8(r, o, d, c) \
	bus_space_write_region_stream_8((r)->r_bustag, (r)->r_bushandle, (o), (d), (c))
#endif /* _KERNEL */

#endif /* !_SYS_BUS_H_ */
