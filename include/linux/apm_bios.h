/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Include file for the interface to an APM BIOS
 * Copyright 1994-2001 Stephen Rothwell (sfr@canb.auug.org.au)
 */
#ifndef _LINUX_APM_H
#define _LINUX_APM_H

#include <uapi/linux/apm_bios.h>


#define APM_CS		(GDT_ENTRY_APMBIOS_BASE * 8)
#define APM_CS_16	(APM_CS + 8)
#define APM_DS		(APM_CS_16 + 8)

/* Results of APM Installation Check */
#define APM_16_BIT_SUPPORT	0x0001
#define APM_32_BIT_SUPPORT	0x0002
#define APM_IDLE_SLOWS_CLOCK	0x0004
#define APM_BIOS_DISABLED      	0x0008
#define APM_BIOS_DISENGAGED     0x0010

/*
 * Data for APM that is persistent across module unload/load
 */
struct apm_info {
	struct apm_bios_info	bios;
	unsigned short		connection_version;
	int			get_power_status_broken;
	int			get_power_status_swabinminutes;
	int			allow_ints;
	int			forbid_idle;
	int			realmode_power_off;
	int			disabled;
};

/*
 * The APM function codes
 */
#define	APM_FUNC_INST_CHECK	0x5300
#define	APM_FUNC_REAL_CONN	0x5301
#define	APM_FUNC_16BIT_CONN	0x5302
#define	APM_FUNC_32BIT_CONN	0x5303
#define	APM_FUNC_DISCONN	0x5304
#define	APM_FUNC_IDLE		0x5305
#define	APM_FUNC_BUSY		0x5306
#define	APM_FUNC_SET_STATE	0x5307
#define	APM_FUNC_ENABLE_PM	0x5308
#define	APM_FUNC_RESTORE_BIOS	0x5309
#define	APM_FUNC_GET_STATUS	0x530a
#define	APM_FUNC_GET_EVENT	0x530b
#define	APM_FUNC_GET_STATE	0x530c
#define	APM_FUNC_ENABLE_DEV_PM	0x530d
#define	APM_FUNC_VERSION	0x530e
#define	APM_FUNC_ENGAGE_PM	0x530f
#define	APM_FUNC_GET_CAP	0x5310
#define	APM_FUNC_RESUME_TIMER	0x5311
#define	APM_FUNC_RESUME_ON_RING	0x5312
#define	APM_FUNC_TIMER		0x5313

/*
 * Function code for APM_FUNC_RESUME_TIMER
 */
#define	APM_FUNC_DISABLE_TIMER	0
#define	APM_FUNC_GET_TIMER	1
#define	APM_FUNC_SET_TIMER	2

/*
 * Function code for APM_FUNC_RESUME_ON_RING
 */
#define	APM_FUNC_DISABLE_RING	0
#define	APM_FUNC_ENABLE_RING	1
#define	APM_FUNC_GET_RING	2

/*
 * Function code for APM_FUNC_TIMER_STATUS
 */
#define	APM_FUNC_TIMER_DISABLE	0
#define	APM_FUNC_TIMER_ENABLE	1
#define	APM_FUNC_TIMER_GET	2

/*
 * in arch/i386/kernel/setup.c
 */
extern struct apm_info	apm_info;

/*
 * This is the "All Devices" ID communicated to the BIOS
 */
#define APM_DEVICE_BALL		((apm_info.connection_version > 0x0100) ? \
				 APM_DEVICE_ALL : APM_DEVICE_OLD_ALL)
#endif	/* LINUX_APM_H */
