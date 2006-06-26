#ifndef __DMI_H__
#define __DMI_H__

#include <linux/list.h>

enum dmi_field {
	DMI_NONE,
	DMI_BIOS_VENDOR,
	DMI_BIOS_VERSION,
	DMI_BIOS_DATE,
	DMI_SYS_VENDOR,
	DMI_PRODUCT_NAME,
	DMI_PRODUCT_VERSION,
	DMI_PRODUCT_SERIAL,
	DMI_BOARD_VENDOR,
	DMI_BOARD_NAME,
	DMI_BOARD_VERSION,
	DMI_STRING_MAX,
};

enum dmi_device_type {
	DMI_DEV_TYPE_ANY = 0,
	DMI_DEV_TYPE_OTHER,
	DMI_DEV_TYPE_UNKNOWN,
	DMI_DEV_TYPE_VIDEO,
	DMI_DEV_TYPE_SCSI,
	DMI_DEV_TYPE_ETHERNET,
	DMI_DEV_TYPE_TOKENRING,
	DMI_DEV_TYPE_SOUND,
	DMI_DEV_TYPE_IPMI = -1
};

struct dmi_header {
	u8 type;
	u8 length;
	u16 handle;
};

/*
 *	DMI callbacks for problem boards
 */
struct dmi_strmatch {
	u8 slot;
	char *substr;
};

struct dmi_system_id {
	int (*callback)(struct dmi_system_id *);
	const char *ident;
	struct dmi_strmatch matches[4];
	void *driver_data;
};

#define DMI_MATCH(a, b)	{ a, b }

struct dmi_device {
	struct list_head list;
	int type;
	const char *name;
	void *device_data;	/* Type specific data */
};

#ifdef CONFIG_DMI

extern int dmi_check_system(struct dmi_system_id *list);
extern char * dmi_get_system_info(int field);
extern struct dmi_device * dmi_find_device(int type, const char *name,
	struct dmi_device *from);
extern void dmi_scan_machine(void);
extern int dmi_get_year(int field);

#else

static inline int dmi_check_system(struct dmi_system_id *list) { return 0; }
static inline char * dmi_get_system_info(int field) { return NULL; }
static inline struct dmi_device * dmi_find_device(int type, const char *name,
	struct dmi_device *from) { return NULL; }
static inline int dmi_get_year(int year) { return 0; }

#endif

#endif	/* __DMI_H__ */
