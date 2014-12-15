#ifndef _UAPI_LINUX_REBOOT_H
#define _UAPI_LINUX_REBOOT_H

/*
 * Magic values required to use _reboot() system call.
 */

#define	LINUX_REBOOT_MAGIC1	0xfee1dead
#define	LINUX_REBOOT_MAGIC2	672274793
#define	LINUX_REBOOT_MAGIC2A	85072278
#define	LINUX_REBOOT_MAGIC2B	369367448
#define	LINUX_REBOOT_MAGIC2C	537993216


/*
 * Commands accepted by the _reboot() system call.
 *
 * RESTART     Restart system using default command and mode.
 * HALT        Stop OS and give system control to ROM monitor, if any.
 * CAD_ON      Ctrl-Alt-Del sequence causes RESTART command.
 * CAD_OFF     Ctrl-Alt-Del sequence sends SIGINT to init task.
 * POWER_OFF   Stop OS and remove all power from system, if possible.
 * RESTART2    Restart system using given command string.
 * SW_SUSPEND  Suspend system using software suspend if compiled in.
 * KEXEC       Restart system using a previously loaded Linux kernel
 */

#define	LINUX_REBOOT_CMD_RESTART	0x01234567
#define	LINUX_REBOOT_CMD_HALT		0xCDEF0123
#define	LINUX_REBOOT_CMD_CAD_ON		0x89ABCDEF
#define	LINUX_REBOOT_CMD_CAD_OFF	0x00000000
#define	LINUX_REBOOT_CMD_POWER_OFF	0x4321FEDC
#define	LINUX_REBOOT_CMD_RESTART2	0xA1B2C3D4
#define	LINUX_REBOOT_CMD_SW_SUSPEND	0xD000FCE2
#define	LINUX_REBOOT_CMD_KEXEC		0x45584543

#if defined (CONFIG_PLAT_MESON)
/*
 * Commands accepted by the arm_machine_restart() system call.
 *
 * MESON_NORMAL_BOOT     			Restart system normally.
 * MESON_FACTORY_RESET_REBOOT      Restart system into recovery factory reset.
 * MESON_UPDATE_REBOOT			Restart system into recovery update.
 * MESON_CHARGING_REBOOT     		Restart system into charging.
 * MESON_CRASH_REBOOT   			Restart system with system crach.
 * MESON_FACTORY_TEST_REBOOT    	Restart system into factory test.
 * MESON_SYSTEM_SWITCH_REBOOT  	Restart system for switch other OS.
 * MESON_SAFE_REBOOT       			Restart system into safe mode.
 * MESON_LOCK_REBOOT  			Restart system into lock mode.
 * elvis.yu---elvis.yu@amlogic.com
 */
#define	MESON_CHARGING_REBOOT					0x0
#define	MESON_NORMAL_BOOT				0x01010101
#define	MESON_FACTORY_RESET_REBOOT		0x02020202
#define	MESON_UPDATE_REBOOT				0x03030303
#define	MESON_CRASH_REBOOT				0x04040404
#define	MESON_FACTORY_TEST_REBOOT		0x05050505
#define	MESON_SYSTEM_SWITCH_REBOOT		0x06060606
#define	MESON_SAFE_REBOOT				0x07070707
#define	MESON_LOCK_REBOOT				0x08080808
#define	MESON_USB_BURNER_REBOOT			0x09090909
#define	MESON_REBOOT_CLEAR				0xdeaddead
#endif /* CONFIG_PLAT_MESON */

#endif /* _UAPI_LINUX_REBOOT_H */
