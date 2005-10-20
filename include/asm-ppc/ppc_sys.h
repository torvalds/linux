/*
 * include/asm-ppc/ppc_sys.h
 *
 * PPC system definitions and library functions
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2005 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_PPC_SYS_H
#define __ASM_PPC_SYS_H

#include <linux/init.h>
#include <linux/device.h>
#include <linux/types.h>

#if defined(CONFIG_8260)
#include <asm/mpc8260.h>
#elif defined(CONFIG_83xx)
#include <asm/mpc83xx.h>
#elif defined(CONFIG_85xx)
#include <asm/mpc85xx.h>
#elif defined(CONFIG_8xx)
#include <asm/mpc8xx.h>
#elif defined(CONFIG_PPC_MPC52xx)
#include <asm/mpc52xx.h>
#elif defined(CONFIG_MPC10X_BRIDGE)
#include <asm/mpc10x.h>
#else
#error "need definition of ppc_sys_devices"
#endif

struct ppc_sys_spec {
	/* PPC sys is matched via (ID & mask) == value, id could be
	 * PVR, SVR, IMMR, * etc. */
	u32 			mask;
	u32 			value;
	u32 			num_devices;
	char 			*ppc_sys_name;
	enum ppc_sys_devices 	*device_list;
};

/* describes all specific chips and which devices they have on them */
extern struct ppc_sys_spec ppc_sys_specs[];
extern struct ppc_sys_spec *cur_ppc_sys_spec;

/* determine which specific SOC we are */
extern void identify_ppc_sys_by_id(u32 id) __init;
extern void identify_ppc_sys_by_name(char *name) __init;
extern void identify_ppc_sys_by_name_and_id(char *name, u32 id) __init;

/* describes all devices that may exist in a given family of processors */
extern struct platform_device ppc_sys_platform_devices[];

/* allow any platform_device fixup to occur before device is registered */
extern int (*ppc_sys_device_fixup) (struct platform_device * pdev);

/* Update all memory resources by paddr, call before platform_device_register */
extern void ppc_sys_fixup_mem_resource(struct platform_device *pdev,
				       phys_addr_t paddr) __init;

/* Get platform_data pointer out of platform device, call before platform_device_register */
extern void *ppc_sys_get_pdata(enum ppc_sys_devices dev) __init;

/* remove a device from the system */
extern void ppc_sys_device_remove(enum ppc_sys_devices dev);

#endif				/* __ASM_PPC_SYS_H */
#endif				/* __KERNEL__ */
