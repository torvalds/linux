/*
 *  acpi_bus.h - ACPI Bus Driver ($Revision: 22 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef __ACPI_BUS_H__
#define __ACPI_BUS_H__

#include <linux/device.h>
#include <linux/property.h>

/* TBD: Make dynamic */
#define ACPI_MAX_HANDLES	10
struct acpi_handle_list {
	u32 count;
	acpi_handle handles[ACPI_MAX_HANDLES];
};

/* acpi_utils.h */
acpi_status
acpi_extract_package(union acpi_object *package,
		     struct acpi_buffer *format, struct acpi_buffer *buffer);
acpi_status
acpi_evaluate_integer(acpi_handle handle,
		      acpi_string pathname,
		      struct acpi_object_list *arguments, unsigned long long *data);
acpi_status
acpi_evaluate_reference(acpi_handle handle,
			acpi_string pathname,
			struct acpi_object_list *arguments,
			struct acpi_handle_list *list);
acpi_status
acpi_evaluate_ost(acpi_handle handle, u32 source_event, u32 status_code,
		  struct acpi_buffer *status_buf);

acpi_status
acpi_get_physical_device_location(acpi_handle handle, struct acpi_pld_info **pld);

bool acpi_has_method(acpi_handle handle, char *name);
acpi_status acpi_execute_simple_method(acpi_handle handle, char *method,
				       u64 arg);
acpi_status acpi_evaluate_ej0(acpi_handle handle);
acpi_status acpi_evaluate_lck(acpi_handle handle, int lock);
bool acpi_ata_match(acpi_handle handle);
bool acpi_bay_match(acpi_handle handle);
bool acpi_dock_match(acpi_handle handle);

bool acpi_check_dsm(acpi_handle handle, const guid_t *guid, u64 rev, u64 funcs);
union acpi_object *acpi_evaluate_dsm(acpi_handle handle, const guid_t *guid,
			u64 rev, u64 func, union acpi_object *argv4);

static inline union acpi_object *
acpi_evaluate_dsm_typed(acpi_handle handle, const guid_t *guid, u64 rev,
			u64 func, union acpi_object *argv4,
			acpi_object_type type)
{
	union acpi_object *obj;

	obj = acpi_evaluate_dsm(handle, guid, rev, func, argv4);
	if (obj && obj->type != type) {
		ACPI_FREE(obj);
		obj = NULL;
	}

	return obj;
}

#define	ACPI_INIT_DSM_ARGV4(cnt, eles)			\
	{						\
	  .package.type = ACPI_TYPE_PACKAGE,		\
	  .package.count = (cnt),			\
	  .package.elements = (eles)			\
	}

bool acpi_dev_found(const char *hid);
bool acpi_dev_present(const char *hid, const char *uid, s64 hrv);

#ifdef CONFIG_ACPI

#include <linux/proc_fs.h>

#define ACPI_BUS_FILE_ROOT	"acpi"
extern struct proc_dir_entry *acpi_root_dir;

enum acpi_bus_device_type {
	ACPI_BUS_TYPE_DEVICE = 0,
	ACPI_BUS_TYPE_POWER,
	ACPI_BUS_TYPE_PROCESSOR,
	ACPI_BUS_TYPE_THERMAL,
	ACPI_BUS_TYPE_POWER_BUTTON,
	ACPI_BUS_TYPE_SLEEP_BUTTON,
	ACPI_BUS_DEVICE_TYPE_COUNT
};

struct acpi_driver;
struct acpi_device;

/*
 * ACPI Scan Handler
 * -----------------
 */

struct acpi_hotplug_profile {
	struct kobject kobj;
	int (*scan_dependent)(struct acpi_device *adev);
	void (*notify_online)(struct acpi_device *adev);
	bool enabled:1;
	bool demand_offline:1;
};

static inline struct acpi_hotplug_profile *to_acpi_hotplug_profile(
						struct kobject *kobj)
{
	return container_of(kobj, struct acpi_hotplug_profile, kobj);
}

struct acpi_scan_handler {
	const struct acpi_device_id *ids;
	struct list_head list_node;
	bool (*match)(const char *idstr, const struct acpi_device_id **matchid);
	int (*attach)(struct acpi_device *dev, const struct acpi_device_id *id);
	void (*detach)(struct acpi_device *dev);
	void (*bind)(struct device *phys_dev);
	void (*unbind)(struct device *phys_dev);
	struct acpi_hotplug_profile hotplug;
};

/*
 * ACPI Hotplug Context
 * --------------------
 */

struct acpi_hotplug_context {
	struct acpi_device *self;
	int (*notify)(struct acpi_device *, u32);
	void (*uevent)(struct acpi_device *, u32);
	void (*fixup)(struct acpi_device *);
};

/*
 * ACPI Driver
 * -----------
 */

typedef int (*acpi_op_add) (struct acpi_device * device);
typedef int (*acpi_op_remove) (struct acpi_device * device);
typedef void (*acpi_op_notify) (struct acpi_device * device, u32 event);

struct acpi_device_ops {
	acpi_op_add add;
	acpi_op_remove remove;
	acpi_op_notify notify;
};

#define ACPI_DRIVER_ALL_NOTIFY_EVENTS	0x1	/* system AND device events */

struct acpi_driver {
	char name[80];
	char class[80];
	const struct acpi_device_id *ids; /* Supported Hardware IDs */
	unsigned int flags;
	struct acpi_device_ops ops;
	struct device_driver drv;
	struct module *owner;
};

/*
 * ACPI Device
 * -----------
 */

/* Status (_STA) */

struct acpi_device_status {
	u32 present:1;
	u32 enabled:1;
	u32 show_in_ui:1;
	u32 functional:1;
	u32 battery_present:1;
	u32 reserved:27;
};

/* Flags */

struct acpi_device_flags {
	u32 dynamic_status:1;
	u32 removable:1;
	u32 ejectable:1;
	u32 power_manageable:1;
	u32 match_driver:1;
	u32 initialized:1;
	u32 visited:1;
	u32 hotplug_notify:1;
	u32 is_dock_station:1;
	u32 of_compatible_ok:1;
	u32 coherent_dma:1;
	u32 cca_seen:1;
	u32 spi_i2c_slave:1;
	u32 reserved:19;
};

/* File System */

struct acpi_device_dir {
	struct proc_dir_entry *entry;
};

#define acpi_device_dir(d)	((d)->dir.entry)

/* Plug and Play */

typedef char acpi_bus_id[8];
typedef unsigned long acpi_bus_address;
typedef char acpi_device_name[40];
typedef char acpi_device_class[20];

struct acpi_hardware_id {
	struct list_head list;
	const char *id;
};

struct acpi_pnp_type {
	u32 hardware_id:1;
	u32 bus_address:1;
	u32 platform_id:1;
	u32 reserved:29;
};

struct acpi_device_pnp {
	acpi_bus_id bus_id;		/* Object name */
	struct acpi_pnp_type type;	/* ID type */
	acpi_bus_address bus_address;	/* _ADR */
	char *unique_id;		/* _UID */
	struct list_head ids;		/* _HID and _CIDs */
	acpi_device_name device_name;	/* Driver-determined */
	acpi_device_class device_class;	/*        "          */
	union acpi_object *str_obj;	/* unicode string for _STR method */
};

#define acpi_device_bid(d)	((d)->pnp.bus_id)
#define acpi_device_adr(d)	((d)->pnp.bus_address)
const char *acpi_device_hid(struct acpi_device *device);
#define acpi_device_uid(d)	((d)->pnp.unique_id)
#define acpi_device_name(d)	((d)->pnp.device_name)
#define acpi_device_class(d)	((d)->pnp.device_class)

/* Power Management */

struct acpi_device_power_flags {
	u32 explicit_get:1;	/* _PSC present? */
	u32 power_resources:1;	/* Power resources */
	u32 inrush_current:1;	/* Serialize Dx->D0 */
	u32 power_removed:1;	/* Optimize Dx->D0 */
	u32 ignore_parent:1;	/* Power is independent of parent power state */
	u32 dsw_present:1;	/* _DSW present? */
	u32 reserved:26;
};

struct acpi_device_power_state {
	struct {
		u8 valid:1;
		u8 explicit_set:1;	/* _PSx present? */
		u8 reserved:6;
	} flags;
	int power;		/* % Power (compared to D0) */
	int latency;		/* Dx->D0 time (microseconds) */
	struct list_head resources;	/* Power resources referenced */
};

struct acpi_device_power {
	int state;		/* Current state */
	struct acpi_device_power_flags flags;
	struct acpi_device_power_state states[ACPI_D_STATE_COUNT];	/* Power states (D0-D3Cold) */
};

/* Performance Management */

struct acpi_device_perf_flags {
	u8 reserved:8;
};

struct acpi_device_perf_state {
	struct {
		u8 valid:1;
		u8 reserved:7;
	} flags;
	u8 power;		/* % Power (compared to P0) */
	u8 performance;		/* % Performance (    "   ) */
	int latency;		/* Px->P0 time (microseconds) */
};

struct acpi_device_perf {
	int state;
	struct acpi_device_perf_flags flags;
	int state_count;
	struct acpi_device_perf_state *states;
};

/* Wakeup Management */
struct acpi_device_wakeup_flags {
	u8 valid:1;		/* Can successfully enable wakeup? */
	u8 notifier_present:1;  /* Wake-up notify handler has been installed */
	u8 enabled:1;		/* Enabled for wakeup */
};

struct acpi_device_wakeup_context {
	void (*func)(struct acpi_device_wakeup_context *context);
	struct device *dev;
};

struct acpi_device_wakeup {
	acpi_handle gpe_device;
	u64 gpe_number;
	u64 sleep_state;
	struct list_head resources;
	struct acpi_device_wakeup_flags flags;
	struct acpi_device_wakeup_context context;
	struct wakeup_source *ws;
	int prepare_count;
};

struct acpi_device_physical_node {
	unsigned int node_id;
	struct list_head node;
	struct device *dev;
	bool put_online:1;
};

/* ACPI Device Specific Data (_DSD) */
struct acpi_device_data {
	const union acpi_object *pointer;
	const union acpi_object *properties;
	const union acpi_object *of_compatible;
	struct list_head subnodes;
};

struct acpi_gpio_mapping;

/* Device */
struct acpi_device {
	int device_type;
	acpi_handle handle;		/* no handle for fixed hardware */
	struct fwnode_handle fwnode;
	struct acpi_device *parent;
	struct list_head children;
	struct list_head node;
	struct list_head wakeup_list;
	struct list_head del_list;
	struct acpi_device_status status;
	struct acpi_device_flags flags;
	struct acpi_device_pnp pnp;
	struct acpi_device_power power;
	struct acpi_device_wakeup wakeup;
	struct acpi_device_perf performance;
	struct acpi_device_dir dir;
	struct acpi_device_data data;
	struct acpi_scan_handler *handler;
	struct acpi_hotplug_context *hp;
	struct acpi_driver *driver;
	const struct acpi_gpio_mapping *driver_gpios;
	void *driver_data;
	struct device dev;
	unsigned int physical_node_count;
	unsigned int dep_unmet;
	struct list_head physical_node_list;
	struct mutex physical_node_lock;
	void (*remove)(struct acpi_device *);
};

/* Non-device subnode */
struct acpi_data_node {
	const char *name;
	acpi_handle handle;
	struct fwnode_handle fwnode;
	struct fwnode_handle *parent;
	struct acpi_device_data data;
	struct list_head sibling;
	struct kobject kobj;
	struct completion kobj_done;
};

extern const struct fwnode_operations acpi_device_fwnode_ops;
extern const struct fwnode_operations acpi_data_fwnode_ops;
extern const struct fwnode_operations acpi_static_fwnode_ops;

static inline bool is_acpi_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) &&
		(fwnode->ops == &acpi_device_fwnode_ops
		 || fwnode->ops == &acpi_data_fwnode_ops);
}

static inline bool is_acpi_device_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) &&
		fwnode->ops == &acpi_device_fwnode_ops;
}

#define to_acpi_device_node(__fwnode)					\
	({								\
		typeof(__fwnode) __to_acpi_device_node_fwnode = __fwnode; \
									\
		is_acpi_device_node(__to_acpi_device_node_fwnode) ?	\
			container_of(__to_acpi_device_node_fwnode,	\
				     struct acpi_device, fwnode) :	\
			NULL;						\
	})

static inline bool is_acpi_data_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->ops == &acpi_data_fwnode_ops;
}

#define to_acpi_data_node(__fwnode)					\
	({								\
		typeof(__fwnode) __to_acpi_data_node_fwnode = __fwnode;	\
									\
		is_acpi_data_node(__to_acpi_data_node_fwnode) ?		\
			container_of(__to_acpi_data_node_fwnode,	\
				     struct acpi_data_node, fwnode) :	\
			NULL;						\
	})

static inline bool is_acpi_static_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) &&
		fwnode->ops == &acpi_static_fwnode_ops;
}

static inline bool acpi_data_node_match(const struct fwnode_handle *fwnode,
					const char *name)
{
	return is_acpi_data_node(fwnode) ?
		(!strcmp(to_acpi_data_node(fwnode)->name, name)) : false;
}

static inline struct fwnode_handle *acpi_fwnode_handle(struct acpi_device *adev)
{
	return &adev->fwnode;
}

static inline void *acpi_driver_data(struct acpi_device *d)
{
	return d->driver_data;
}

#define to_acpi_device(d)	container_of(d, struct acpi_device, dev)
#define to_acpi_driver(d)	container_of(d, struct acpi_driver, drv)

static inline void acpi_set_device_status(struct acpi_device *adev, u32 sta)
{
	*((u32 *)&adev->status) = sta;
}

static inline void acpi_set_hp_context(struct acpi_device *adev,
				       struct acpi_hotplug_context *hp)
{
	hp->self = adev;
	adev->hp = hp;
}

void acpi_initialize_hp_context(struct acpi_device *adev,
				struct acpi_hotplug_context *hp,
				int (*notify)(struct acpi_device *, u32),
				void (*uevent)(struct acpi_device *, u32));

/* acpi_device.dev.bus == &acpi_bus_type */
extern struct bus_type acpi_bus_type;

/*
 * Events
 * ------
 */

struct acpi_bus_event {
	struct list_head node;
	acpi_device_class device_class;
	acpi_bus_id bus_id;
	u32 type;
	u32 data;
};

extern struct kobject *acpi_kobj;
extern int acpi_bus_generate_netlink_event(const char*, const char*, u8, int);
void acpi_bus_private_data_handler(acpi_handle, void *);
int acpi_bus_get_private_data(acpi_handle, void **);
int acpi_bus_attach_private_data(acpi_handle, void *);
void acpi_bus_detach_private_data(acpi_handle);
extern int acpi_notifier_call_chain(struct acpi_device *, u32, u32);
extern int register_acpi_notifier(struct notifier_block *);
extern int unregister_acpi_notifier(struct notifier_block *);

/*
 * External Functions
 */

int acpi_bus_get_device(acpi_handle handle, struct acpi_device **device);
struct acpi_device *acpi_bus_get_acpi_device(acpi_handle handle);
void acpi_bus_put_acpi_device(struct acpi_device *adev);
acpi_status acpi_bus_get_status_handle(acpi_handle handle,
				       unsigned long long *sta);
int acpi_bus_get_status(struct acpi_device *device);

int acpi_bus_set_power(acpi_handle handle, int state);
const char *acpi_power_state_string(int state);
int acpi_device_get_power(struct acpi_device *device, int *state);
int acpi_device_set_power(struct acpi_device *device, int state);
int acpi_bus_init_power(struct acpi_device *device);
int acpi_device_fix_up_power(struct acpi_device *device);
int acpi_bus_update_power(acpi_handle handle, int *state_p);
int acpi_device_update_power(struct acpi_device *device, int *state_p);
bool acpi_bus_power_manageable(acpi_handle handle);

#ifdef CONFIG_PM
bool acpi_bus_can_wakeup(acpi_handle handle);
#else
static inline bool acpi_bus_can_wakeup(acpi_handle handle) { return false; }
#endif

void acpi_scan_lock_acquire(void);
void acpi_scan_lock_release(void);
void acpi_lock_hp_context(void);
void acpi_unlock_hp_context(void);
int acpi_scan_add_handler(struct acpi_scan_handler *handler);
int acpi_bus_register_driver(struct acpi_driver *driver);
void acpi_bus_unregister_driver(struct acpi_driver *driver);
int acpi_bus_scan(acpi_handle handle);
void acpi_bus_trim(struct acpi_device *start);
acpi_status acpi_bus_get_ejd(acpi_handle handle, acpi_handle * ejd);
int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids);
void acpi_set_modalias(struct acpi_device *adev, const char *default_id,
		       char *modalias, size_t len);
int acpi_create_dir(struct acpi_device *);
void acpi_remove_dir(struct acpi_device *);

static inline bool acpi_device_enumerated(struct acpi_device *adev)
{
	return adev && adev->flags.initialized && adev->flags.visited;
}

/**
 * module_acpi_driver(acpi_driver) - Helper macro for registering an ACPI driver
 * @__acpi_driver: acpi_driver struct
 *
 * Helper macro for ACPI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_acpi_driver(__acpi_driver) \
	module_driver(__acpi_driver, acpi_bus_register_driver, \
		      acpi_bus_unregister_driver)

/*
 * Bind physical devices with ACPI devices
 */
struct acpi_bus_type {
	struct list_head list;
	const char *name;
	bool (*match)(struct device *dev);
	struct acpi_device * (*find_companion)(struct device *);
	void (*setup)(struct device *);
	void (*cleanup)(struct device *);
};
int register_acpi_bus_type(struct acpi_bus_type *);
int unregister_acpi_bus_type(struct acpi_bus_type *);
int acpi_bind_one(struct device *dev, struct acpi_device *adev);
int acpi_unbind_one(struct device *dev);

struct acpi_pci_root {
	struct acpi_device * device;
	struct pci_bus *bus;
	u16 segment;
	struct resource secondary;	/* downstream bus range */

	u32 osc_support_set;	/* _OSC state of support bits */
	u32 osc_control_set;	/* _OSC state of control bits */
	phys_addr_t mcfg_addr;
};

/* helper */

bool acpi_dma_supported(struct acpi_device *adev);
enum dev_dma_attr acpi_get_dma_attr(struct acpi_device *adev);
int acpi_dma_configure(struct device *dev, enum dev_dma_attr attr);
void acpi_dma_deconfigure(struct device *dev);

struct acpi_device *acpi_find_child_device(struct acpi_device *parent,
					   u64 address, bool check_children);
int acpi_is_root_bridge(acpi_handle);
struct acpi_pci_root *acpi_pci_find_root(acpi_handle handle);

int acpi_enable_wakeup_device_power(struct acpi_device *dev, int state);
int acpi_disable_wakeup_device_power(struct acpi_device *dev);

#ifdef CONFIG_X86
bool acpi_device_always_present(struct acpi_device *adev);
#else
static inline bool acpi_device_always_present(struct acpi_device *adev)
{
	return false;
}
#endif

#ifdef CONFIG_PM
void acpi_pm_wakeup_event(struct device *dev);
acpi_status acpi_add_pm_notifier(struct acpi_device *adev, struct device *dev,
			void (*func)(struct acpi_device_wakeup_context *context));
acpi_status acpi_remove_pm_notifier(struct acpi_device *adev);
bool acpi_pm_device_can_wakeup(struct device *dev);
int acpi_pm_device_sleep_state(struct device *, int *, int);
int acpi_pm_set_device_wakeup(struct device *dev, bool enable);
#else
static inline void acpi_pm_wakeup_event(struct device *dev)
{
}
static inline acpi_status acpi_add_pm_notifier(struct acpi_device *adev,
					       struct device *dev,
					       void (*func)(struct acpi_device_wakeup_context *context))
{
	return AE_SUPPORT;
}
static inline acpi_status acpi_remove_pm_notifier(struct acpi_device *adev)
{
	return AE_SUPPORT;
}
static inline bool acpi_pm_device_can_wakeup(struct device *dev)
{
	return false;
}
static inline int acpi_pm_device_sleep_state(struct device *d, int *p, int m)
{
	if (p)
		*p = ACPI_STATE_D0;

	return (m >= ACPI_STATE_D0 && m <= ACPI_STATE_D3_COLD) ?
		m : ACPI_STATE_D0;
}
static inline int acpi_pm_set_device_wakeup(struct device *dev, bool enable)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_ACPI_SLEEP
u32 acpi_target_system_state(void);
#else
static inline u32 acpi_target_system_state(void) { return ACPI_STATE_S0; }
#endif

static inline bool acpi_device_power_manageable(struct acpi_device *adev)
{
	return adev->flags.power_manageable;
}

static inline bool acpi_device_can_wakeup(struct acpi_device *adev)
{
	return adev->wakeup.flags.valid;
}

static inline bool acpi_device_can_poweroff(struct acpi_device *adev)
{
	return adev->power.states[ACPI_STATE_D3_COLD].flags.valid ||
		((acpi_gbl_FADT.header.revision < 6) &&
		adev->power.states[ACPI_STATE_D3_HOT].flags.explicit_set);
}

#else	/* CONFIG_ACPI */

static inline int register_acpi_bus_type(void *bus) { return 0; }
static inline int unregister_acpi_bus_type(void *bus) { return 0; }

#endif				/* CONFIG_ACPI */

#endif /*__ACPI_BUS_H__*/
