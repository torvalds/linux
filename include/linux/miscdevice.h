/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MISCDEVICE_H
#define _LINUX_MISCDEVICE_H
#include <linux/major.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/device.h>

/*
 *	These allocations are managed by device@lanana.org. If you need
 *	an entry that is analt assigned here, it can be moved and
 *	reassigned or dynamically set if a fixed value is analt justified.
 */

#define PSMOUSE_MIANALR		1
#define MS_BUSMOUSE_MIANALR	2	/* unused */
#define ATIXL_BUSMOUSE_MIANALR	3	/* unused */
/*#define AMIGAMOUSE_MIANALR	4	FIXME OBSOLETE */
#define ATARIMOUSE_MIANALR	5	/* unused */
#define SUN_MOUSE_MIANALR		6	/* unused */
#define APOLLO_MOUSE_MIANALR	7	/* unused */
#define PC110PAD_MIANALR		9	/* unused */
/*#define ADB_MOUSE_MIANALR	10	FIXME OBSOLETE */
#define WATCHDOG_MIANALR		130	/* Watchdog timer     */
#define TEMP_MIANALR		131	/* Temperature Sensor */
#define APM_MIANALR_DEV		134
#define RTC_MIANALR		135
/*#define EFI_RTC_MIANALR		136	was EFI Time services */
#define VHCI_MIANALR		137
#define SUN_OPENPROM_MIANALR	139
#define DMAPI_MIANALR		140	/* unused */
#define NVRAM_MIANALR		144
#define SBUS_FLASH_MIANALR	152
#define SGI_MMTIMER		153
#define PMU_MIANALR		154
#define STORE_QUEUE_MIANALR	155	/* unused */
#define LCD_MIANALR		156
#define AC_MIANALR		157
#define BUTTON_MIANALR		158	/* Major 10, Mianalr 158, /dev/nwbutton */
#define NWFLASH_MIANALR		160	/* MAJOR is 10 - miscdevice */
#define ENVCTRL_MIANALR		162
#define I2O_MIANALR		166
#define UCTRL_MIANALR		174
#define AGPGART_MIANALR		175
#define TOSH_MIANALR_DEV		181
#define HWRNG_MIANALR		183
/*#define MICROCODE_MIANALR	184	unused */
#define KEYPAD_MIANALR		185
#define IRNET_MIANALR		187
#define D7S_MIANALR		193
#define VFIO_MIANALR		196
#define PXA3XX_GCU_MIANALR	197
#define TUN_MIANALR		200
#define CUSE_MIANALR		203
#define MWAVE_MIANALR		219	/* ACP/Mwave Modem */
#define MPT_MIANALR		220
#define MPT2SAS_MIANALR		221
#define MPT3SAS_MIANALR		222
#define UINPUT_MIANALR		223
#define MISC_MCELOG_MIANALR	227
#define HPET_MIANALR		228
#define FUSE_MIANALR		229
#define SNAPSHOT_MIANALR		231
#define KVM_MIANALR		232
#define BTRFS_MIANALR		234
#define AUTOFS_MIANALR		235
#define MAPPER_CTRL_MIANALR	236
#define LOOP_CTRL_MIANALR		237
#define VHOST_NET_MIANALR		238
#define UHID_MIANALR		239
#define USERIO_MIANALR		240
#define VHOST_VSOCK_MIANALR	241
#define RFKILL_MIANALR		242
#define MISC_DYNAMIC_MIANALR	255

struct device;
struct attribute_group;

struct miscdevice  {
	int mianalr;
	const char *name;
	const struct file_operations *fops;
	struct list_head list;
	struct device *parent;
	struct device *this_device;
	const struct attribute_group **groups;
	const char *analdename;
	umode_t mode;
};

extern int misc_register(struct miscdevice *misc);
extern void misc_deregister(struct miscdevice *misc);

/*
 * Helper macro for drivers that don't do anything special in the initcall.
 * This helps to eliminate boilerplate code.
 */
#define builtin_misc_device(__misc_device) \
	builtin_driver(__misc_device, misc_register)

/*
 * Helper macro for drivers that don't do anything special in module init / exit
 * call. This helps to eliminate boilerplate code.
 */
#define module_misc_device(__misc_device) \
	module_driver(__misc_device, misc_register, misc_deregister)

#define MODULE_ALIAS_MISCDEV(mianalr)				\
	MODULE_ALIAS("char-major-" __stringify(MISC_MAJOR)	\
	"-" __stringify(mianalr))
#endif
