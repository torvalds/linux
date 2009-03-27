#ifndef __DMI_H__
#define __DMI_H__

#include <linux/list.h>
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
};

struct dmi_header {
	u8 type;
	u8 length;
	u16 handle;
};

struct dmi_device {
	struct list_head list;
	int type;
	const char *name;
	void *device_data;	/* Type specific data */
};

#ifdef CONFIG_DMI

extern int dmi_check_system(const struct dmi_system_id *list);
const struct dmi_system_id *dmi_first_match(const struct dmi_system_id *list);
extern const char * dmi_get_system_info(int field);
extern const struct dmi_device * dmi_find_device(int type, const char *name,
	const struct dmi_device *from);
extern void dmi_scan_machine(void);
extern int dmi_get_year(int field);
extern int dmi_name_in_vendors(const char *str);
extern int dmi_name_in_serial(const char *str);
extern int dmi_available;
extern int dmi_walk(void (*decode)(const struct dmi_header *));
extern bool dmi_match(enum dmi_field f, const char *str);

#else

static inline int dmi_check_system(const struct dmi_system_id *list) { return 0; }
static inline const char * dmi_get_system_info(int field) { return NULL; }
static inline const struct dmi_device * dmi_find_device(int type, const char *name,
	const struct dmi_device *from) { return NULL; }
static inline void dmi_scan_machine(void) { return; }
static inline int dmi_get_year(int year) { return 0; }
static inline int dmi_name_in_vendors(const char *s) { return 0; }
static inline int dmi_name_in_serial(const char *s) { return 0; }
#define dmi_available 0
static inline int dmi_walk(void (*decode)(const struct dmi_header *))
	{ return -1; }
static inline bool dmi_match(enum dmi_field f, const char *str)
	{ return false; }
static inline const struct dmi_system_id *
	dmi_first_match(const struct dmi_system_id *list) { return NULL; }

#endif

#endif	/* __DMI_H__ */
