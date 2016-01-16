#ifndef __DMI_H__
#define __DMI_H__

#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/mod_devicetable.h>

/* enum dmi_field is in mod_devicetable.h */

enum dmi_device_type {
	DMI_DEV_TYPE_ANY = 0,
	DMI_DEV_TYPE_OTHER,
	DMI_DEV_TYPE_UNKNOWN,
	DMI_DEV_TYPE_VIDEO,
	DMI_DEV_TYPE_SCSI,
	DMI_DEV_TYPE_ETHERNET,
	DMI_DEV_TYPE_TOKENRING,
	DMI_DEV_TYPE_SOUND,
	DMI_DEV_TYPE_PATA,
	DMI_DEV_TYPE_SATA,
	DMI_DEV_TYPE_SAS,
	DMI_DEV_TYPE_IPMI = -1,
	DMI_DEV_TYPE_OEM_STRING = -2,
	DMI_DEV_TYPE_DEV_ONBOARD = -3,
	DMI_DEV_TYPE_DEV_SLOT = -4,
};

enum dmi_entry_type {
	DMI_ENTRY_BIOS = 0,
	DMI_ENTRY_SYSTEM,
	DMI_ENTRY_BASEBOARD,
	DMI_ENTRY_CHASSIS,
	DMI_ENTRY_PROCESSOR,
	DMI_ENTRY_MEM_CONTROLLER,
	DMI_ENTRY_MEM_MODULE,
	DMI_ENTRY_CACHE,
	DMI_ENTRY_PORT_CONNECTOR,
	DMI_ENTRY_SYSTEM_SLOT,
	DMI_ENTRY_ONBOARD_DEVICE,
	DMI_ENTRY_OEMSTRINGS,
	DMI_ENTRY_SYSCONF,
	DMI_ENTRY_BIOS_LANG,
	DMI_ENTRY_GROUP_ASSOC,
	DMI_ENTRY_SYSTEM_EVENT_LOG,
	DMI_ENTRY_PHYS_MEM_ARRAY,
	DMI_ENTRY_MEM_DEVICE,
	DMI_ENTRY_32_MEM_ERROR,
	DMI_ENTRY_MEM_ARRAY_MAPPED_ADDR,
	DMI_ENTRY_MEM_DEV_MAPPED_ADDR,
	DMI_ENTRY_BUILTIN_POINTING_DEV,
	DMI_ENTRY_PORTABLE_BATTERY,
	DMI_ENTRY_SYSTEM_RESET,
	DMI_ENTRY_HW_SECURITY,
	DMI_ENTRY_SYSTEM_POWER_CONTROLS,
	DMI_ENTRY_VOLTAGE_PROBE,
	DMI_ENTRY_COOLING_DEV,
	DMI_ENTRY_TEMP_PROBE,
	DMI_ENTRY_ELECTRICAL_CURRENT_PROBE,
	DMI_ENTRY_OOB_REMOTE_ACCESS,
	DMI_ENTRY_BIS_ENTRY,
	DMI_ENTRY_SYSTEM_BOOT,
	DMI_ENTRY_MGMT_DEV,
	DMI_ENTRY_MGMT_DEV_COMPONENT,
	DMI_ENTRY_MGMT_DEV_THRES,
	DMI_ENTRY_MEM_CHANNEL,
	DMI_ENTRY_IPMI_DEV,
	DMI_ENTRY_SYS_POWER_SUPPLY,
	DMI_ENTRY_ADDITIONAL,
	DMI_ENTRY_ONBOARD_DEV_EXT,
	DMI_ENTRY_MGMT_CONTROLLER_HOST,
	DMI_ENTRY_INACTIVE = 126,
	DMI_ENTRY_END_OF_TABLE = 127,
};

struct dmi_header {
	u8 type;
	u8 length;
	u16 handle;
} __packed;

struct dmi_device {
	struct list_head list;
	int type;
	const char *name;
	void *device_data;	/* Type specific data */
};

#ifdef CONFIG_DMI

struct dmi_dev_onboard {
	struct dmi_device dev;
	int instance;
	int segment;
	int bus;
	int devfn;
};

extern struct kobject *dmi_kobj;
extern int dmi_check_system(const struct dmi_system_id *list);
const struct dmi_system_id *dmi_first_match(const struct dmi_system_id *list);
extern const char * dmi_get_system_info(int field);
extern const struct dmi_device * dmi_find_device(int type, const char *name,
	const struct dmi_device *from);
extern void dmi_scan_machine(void);
extern void dmi_memdev_walk(void);
extern void dmi_set_dump_stack_arch_desc(void);
extern bool dmi_get_date(int field, int *yearp, int *monthp, int *dayp);
extern int dmi_name_in_vendors(const char *str);
extern int dmi_name_in_serial(const char *str);
extern int dmi_available;
extern int dmi_walk(void (*decode)(const struct dmi_header *, void *),
	void *private_data);
extern bool dmi_match(enum dmi_field f, const char *str);
extern void dmi_memdev_name(u16 handle, const char **bank, const char **device);

#else

static inline int dmi_check_system(const struct dmi_system_id *list) { return 0; }
static inline const char * dmi_get_system_info(int field) { return NULL; }
static inline const struct dmi_device * dmi_find_device(int type, const char *name,
	const struct dmi_device *from) { return NULL; }
static inline void dmi_scan_machine(void) { return; }
static inline void dmi_memdev_walk(void) { }
static inline void dmi_set_dump_stack_arch_desc(void) { }
static inline bool dmi_get_date(int field, int *yearp, int *monthp, int *dayp)
{
	if (yearp)
		*yearp = 0;
	if (monthp)
		*monthp = 0;
	if (dayp)
		*dayp = 0;
	return false;
}
static inline int dmi_name_in_vendors(const char *s) { return 0; }
static inline int dmi_name_in_serial(const char *s) { return 0; }
#define dmi_available 0
static inline int dmi_walk(void (*decode)(const struct dmi_header *, void *),
	void *private_data) { return -1; }
static inline bool dmi_match(enum dmi_field f, const char *str)
	{ return false; }
static inline void dmi_memdev_name(u16 handle, const char **bank,
		const char **device) { }
static inline const struct dmi_system_id *
	dmi_first_match(const struct dmi_system_id *list) { return NULL; }

#endif

#endif	/* __DMI_H__ */
