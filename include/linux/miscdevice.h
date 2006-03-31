#ifndef _LINUX_MISCDEVICE_H
#define _LINUX_MISCDEVICE_H
#include <linux/module.h>
#include <linux/major.h>

#define PSMOUSE_MINOR  1
#define MS_BUSMOUSE_MINOR 2
#define ATIXL_BUSMOUSE_MINOR 3
/*#define AMIGAMOUSE_MINOR 4	FIXME OBSOLETE */
#define ATARIMOUSE_MINOR 5
#define SUN_MOUSE_MINOR 6
#define APOLLO_MOUSE_MINOR 7
#define PC110PAD_MINOR 9
/*#define ADB_MOUSE_MINOR 10	FIXME OBSOLETE */
#define WATCHDOG_MINOR		130	/* Watchdog timer     */
#define TEMP_MINOR		131	/* Temperature Sensor */
#define RTC_MINOR 135
#define EFI_RTC_MINOR		136	/* EFI Time services */
#define SUN_OPENPROM_MINOR 139
#define DMAPI_MINOR		140	/* DMAPI */
#define NVRAM_MINOR 144
#define SGI_MMTIMER        153
#define STORE_QUEUE_MINOR	155
#define I2O_MINOR 166
#define MICROCODE_MINOR		184
#define MWAVE_MINOR	219		/* ACP/Mwave Modem */
#define MPT_MINOR	220
#define MISC_DYNAMIC_MINOR 255

#define TUN_MINOR	     200
#define	HPET_MINOR	     228

struct device;
struct class_device;

struct miscdevice  {
	int minor;
	const char *name;
	const struct file_operations *fops;
	struct list_head list;
	struct device *dev;
	struct class_device *class;
	char devfs_name[64];
};

extern int misc_register(struct miscdevice * misc);
extern int misc_deregister(struct miscdevice * misc);

#define MODULE_ALIAS_MISCDEV(minor)				\
	MODULE_ALIAS("char-major-" __stringify(MISC_MAJOR)	\
	"-" __stringify(minor))
#endif
