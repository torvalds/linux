/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TI pm33xx platform data
 *
 * Copyright (C) 2016-2018 Texas Instruments, Inc.
 *	Dave Gerlach <d-gerlach@ti.com>
 */

#ifndef _LINUX_PLATFORM_DATA_PM33XX_H
#define _LINUX_PLATFORM_DATA_PM33XX_H

#include <linux/kbuild.h>
#include <linux/types.h>

#ifndef __ASSEMBLER__
struct am33xx_pm_sram_addr {
	void (*do_wfi)(void);
	unsigned long *do_wfi_sz;
	unsigned long *resume_offset;
	unsigned long *emif_sram_table;
	unsigned long *ro_sram_data;
};

struct am33xx_pm_platform_data {
	int	(*init)(void);
	int	(*soc_suspend)(unsigned int state, int (*fn)(unsigned long));
	struct  am33xx_pm_sram_addr *(*get_sram_addrs)(void);
};

struct am33xx_pm_sram_data {
	u32 wfi_flags;
	u32 l2_aux_ctrl_val;
	u32 l2_prefetch_ctrl_val;
} __packed __aligned(8);

struct am33xx_pm_ro_sram_data {
	u32 amx3_pm_sram_data_virt;
	u32 amx3_pm_sram_data_phys;
} __packed __aligned(8);

#endif /* __ASSEMBLER__ */
#endif /* _LINUX_PLATFORM_DATA_PM33XX_H */
