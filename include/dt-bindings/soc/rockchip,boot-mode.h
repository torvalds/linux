/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ROCKCHIP_BOOT_MODE_H
#define __ROCKCHIP_BOOT_MODE_H

/*high 24 bits is tag, low 8 bits is type*/
#define REBOOT_FLAG		0x5242C300
/* normal boot */
#define BOOT_NORMAL		(REBOOT_FLAG + 0)
/* enter bootloader rockusb mode */
#define BOOT_BL_DOWNLOAD	(REBOOT_FLAG + 1)
/* enter recovery */
#define BOOT_RECOVERY		(REBOOT_FLAG + 3)
/* reboot by panic */
#define BOOT_PANIC		(REBOOT_FLAG + 7)
/* reboot by watchdog */
#define BOOT_WATCHDOG		(REBOOT_FLAG + 8)
/* enter fastboot mode */
#define BOOT_FASTBOOT		(REBOOT_FLAG + 9)
/* enter charging mode */
#define BOOT_CHARGING		(REBOOT_FLAG + 11)
/* enter usb mass storage mode */
#define BOOT_UMS		(REBOOT_FLAG + 12)
/* reboot system quiescent */
#define BOOT_QUIESCENT		(REBOOT_FLAG + 14)
/* reboot by panic and capture ramdump in uboot through usb */
#define BOOT_WINUSB		(REBOOT_FLAG + 15)

#endif
