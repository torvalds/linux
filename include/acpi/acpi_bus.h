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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef __ACPI_BUS_H__
#define __ACPI_BUS_H__

#include <linux/kobject.h>

#include <acpi/acpi.h>

#define PREFIX			"ACPI: "

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
		      struct acpi_object_list *arguments, unsigned long *data);
acpi_status
acpi_evaluate_reference(acpi_handle handle,
			acpi_string pathname,
			struct acpi_object_list *arguments,
			struct acpi_handle_list *list);

#ifdef CONFIG_ACPI

#include <linux/proc_fs.h>

#define ACPI_BUS_FILE_ROOT	"acpi"
extern struct proc_dir_entry *acpi_root_dir;
extern struct fadt_descriptor acpi_fadt;

enum acpi_bus_removal_type {
	ACPI_BUS_REMOVAL_NORMAL = 0,
	ACPI_BUS_REMOVAL_EJECT,
	ACPI_BUS_REMOVAL_SUPRISE,
	ACPI_BUS_REMOVAL_TYPE_COUNT
};

enum acpi_bus_device_type {
	ACPI_BUS_TYPE_DEVICE = 0,
	ACPI_BUS_TYPE_POWER,
	ACPI_BUS_TYPE_PROCESSOR,
	ACPI_BUS_TYPE_THERMAL,
	ACPI_BUS_TYPE_SYSTEM,
	ACPI_BUS_TYPE_POWER_BUTTON,
	ACPI_BUS_TYPE_SLEEP_BUTTON,
	ACPI_BUS_DEVICE_TYPE_COUNT
};

struct acpi_driver;
struct acpi_device;

/*
 * ACPI Driver
 * -----------
 */

typedef int (*acpi_op_add) (struct acpi_device * device);
typedef int (*acpi_op_remove) (struct acpi_device * device, int type);
typedef int (*acpi_op_lock) (struct acpi_device * device, int type);
typedef int (*acpi_op_start) (struct acpi_device * device);
typedef int (*acpi_op_stop) (struct acpi_device * device, int type);
typedef int (*acpi_op_suspend) (struct acpi_device * device, int state);
typedef int (*acpi_op_resume) (struct acpi_device * device, int state);
typedef int (*acpi_op_scan) (struct acpi_device * device);
typedef int (*acpi_op_bind) (struct acpi_device * device);
typedef int (*acpi_op_unbind) (struct acpi_device * device);
typedef int (*acpi_op_match) (struct acpi_device * device,
			      struct acpi_driver * driver);

struct acpi_bus_ops {
	u32 acpi_op_add:1;
	u32 acpi_op_remove:1;
	u32 acpi_op_lock:1;
	u32 acpi_op_start:1;
	u32 acpi_op_stop:1;
	u32 acpi_op_suspend:1;
	u32 acpi_op_resume:1;
	u32 acpi_op_scan:1;
	u32 acpi_op_bind:1;
	u32 acpi_op_unbind:1;
	u32 acpi_op_match:1;
	u32 reserved:21;
};

struct acpi_device_ops {
	acpi_op_add add;
	acpi_op_remove remove;
	acpi_op_lock lock;
	acpi_op_start start;
	acpi_op_stop stop;
	acpi_op_suspend suspend;
	acpi_op_resume resume;
	acpi_op_scan scan;
	acpi_op_bind bind;
	acpi_op_unbind unbind;
	acpi_op_match match;
};

struct acpi_driver {
	struct list_head node;
	char name[80];
	char class[80];
	atomic_t references;
	char *ids;		/* Supported Hardware IDs */
	struct acpi_device_ops ops;
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
	u32 hardware_id:1;
	u32 compatible_ids:1;
	u32 bus_address:1;
	u32 unique_id:1;
	u32 removable:1;
	u32 ejectable:1;
	u32 lockable:1;
	u32 suprise_removal_ok:1;
	u32 power_manageable:1;
	u32 performance_manageable:1;
	u32 wake_capable:1;	/* Wakeup(_PRW) supported? */
	u32 reserved:20;
};

/* File System */

struct acpi_device_dir {
	struct proc_dir_entry *entry;
};

#define acpi_device_dir(d)	((d)->dir.entry)

/* Plug and Play */

typedef char acpi_bus_id[5];
typedef unsigned long acpi_bus_address;
typedef char acpi_hardware_id[9];
typedef char acpi_unique_id[9];
typedef char acpi_device_name[40];
typedef char acpi_device_class[20];

struct acpi_device_pnp {
	acpi_bus_id bus_id;	/* Object name */
	acpi_bus_address bus_address;	/* _ADR */
	acpi_hardware_id hardware_id;	/* _HID */
	struct acpi_compatible_id_list *cid_list;	/* _CIDs */
	acpi_unique_id unique_id;	/* _UID */
	acpi_device_name device_name;	/* Driver-determined */
	acpi_device_class device_class;	/*        "          */
};

#define acpi_device_bid(d)	((d)->pnp.bus_id)
#define acpi_device_adr(d)	((d)->pnp.bus_address)
#define acpi_device_hid(d)	((d)->pnp.hardware_id)
#define acpi_device_uid(d)	((d)->pnp.unique_id)
#define acpi_device_name(d)	((d)->pnp.device_name)
#define acpi_device_class(d)	((d)->pnp.device_class)

/* Power Management */

struct acpi_device_power_flags {
	u32 explicit_get:1;	/* _PSC present? */
	u32 power_resources:1;	/* Power resources */
	u32 inrush_current:1;	/* Serialize Dx->D0 */
	u32 power_removed:1;	/* Optimize Dx->D0 */
	u32 reserved:28;
};

struct acpi_device_power_state {
	struct {
		u8 valid:1;
		u8 explicit_set:1;	/* _PSx present? */
		u8 reserved:6;
	} flags;
	int power;		/* % Power (compared to D0) */
	int latency;		/* Dx->D0 time (microseconds) */
	struct acpi_handle_list resources;	/* Power resources referenced */
};

struct acpi_device_power {
	int state;		/* Current state */
	struct acpi_device_power_flags flags;
	struct acpi_device_power_state states[4];	/* Power states (D0-D3) */
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
	u8 run_wake:1;		/* Run-Wake GPE devices */
};

struct acpi_device_wakeup_state {
	u8 enabled:1;
	u8 active:1;
};

struct acpi_device_wakeup {
	acpi_handle gpe_device;
	acpi_integer gpe_number;
	acpi_integer sleep_state;
	struct acpi_handle_list resources;
	struct acpi_device_wakeup_state state;
	struct acpi_device_wakeup_flags flags;
};

/* Device */

struct acpi_device {
	acpi_handle handle;
	struct acpi_device *parent;
	struct list_head children;
	struct list_head node;
	struct list_head wakeup_list;
	struct list_head g_list;
	struct acpi_device_status status;
	struct acpi_device_flags flags;
	struct acpi_device_pnp pnp;
	struct acpi_device_power power;
	struct acpi_device_wakeup wakeup;
	struct acpi_device_perf performance;
	struct acpi_device_dir dir;
	struct acpi_device_ops ops;
	struct acpi_driver *driver;
	void *driver_data;
	struct kobject kobj;
};

#define acpi_driver_data(d)	((d)->driver_data)

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

extern struct subsystem acpi_subsys;

/*
 * External Functions
 */

int acpi_bus_get_device(acpi_handle handle, struct acpi_device **device);
void acpi_bus_data_handler(acpi_handle handle, u32 function, void *context);
int acpi_bus_get_status(struct acpi_device *device);
int acpi_bus_get_power(acpi_handle handle, int *state);
int acpi_bus_set_power(acpi_handle handle, int state);
int acpi_bus_generate_event(struct acpi_device *device, u8 type, int data);
int acpi_bus_receive_event(struct acpi_bus_event *event);
int acpi_bus_register_driver(struct acpi_driver *driver);
void acpi_bus_unregister_driver(struct acpi_driver *driver);
int acpi_bus_add(struct acpi_device **child, struct acpi_device *parent,
		 acpi_handle handle, int type);
int acpi_bus_trim(struct acpi_device *start, int rmdevice);
int acpi_bus_start(struct acpi_device *device);

int acpi_match_ids(struct acpi_device *device, char *ids);
int acpi_create_dir(struct acpi_device *);
void acpi_remove_dir(struct acpi_device *);

/*
 * Bind physical devices with ACPI devices
 */
#include <linux/device.h>
struct acpi_bus_type {
	struct list_head list;
	struct bus_type *bus;
	/* For general devices under the bus */
	int (*find_device) (struct device *, acpi_handle *);
	/* For bridges, such as PCI root bridge, IDE controller */
	int (*find_bridge) (struct device *, acpi_handle *);
};
int register_acpi_bus_type(struct acpi_bus_type *);
int unregister_acpi_bus_type(struct acpi_bus_type *);
struct device *acpi_get_physical_device(acpi_handle);
/* helper */
acpi_handle acpi_get_child(acpi_handle, acpi_integer);
acpi_handle acpi_get_pci_rootbridge_handle(unsigned int, unsigned int);
#define DEVICE_ACPI_HANDLE(dev) ((acpi_handle)((dev)->firmware_data))

#endif /* CONFIG_ACPI */

#endif /*__ACPI_BUS_H__*/
