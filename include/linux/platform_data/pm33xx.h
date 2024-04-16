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

/*
 * WFI Flags for sleep code control
 *
 * These flags allow PM code to exclude certain operations from happening
 * in the low level ASM code found in sleep33xx.S and sleep43xx.S
 *
 * WFI_FLAG_FLUSH_CACHE: Flush the ARM caches and disable caching. Only
 *			 needed when MPU will lose context.
 * WFI_FLAG_SELF_REFRESH: Let EMIF place DDR memory into self-refresh and
 *			  disable EMIF.
 * WFI_FLAG_SAVE_EMIF: Save context of all EMIF registers and restore in
 *		       resume path. Only needed if PER domain loses context
 *		       and must also have WFI_FLAG_SELF_REFRESH set.
 * WFI_FLAG_WAKE_M3: Disable MPU clock or clockdomain to cause wkup_m3 to
 *		     execute when WFI instruction executes.
 * WFI_FLAG_RTC_ONLY: Configure the RTC to enter RTC+DDR mode.
 */
#define WFI_FLAG_FLUSH_CACHE		BIT(0)
#define WFI_FLAG_SELF_REFRESH		BIT(1)
#define WFI_FLAG_SAVE_EMIF		BIT(2)
#define WFI_FLAG_WAKE_M3		BIT(3)
#define WFI_FLAG_RTC_ONLY		BIT(4)

#ifndef __ASSEMBLER__
struct am33xx_pm_sram_addr {
	void (*do_wfi)(void);
	unsigned long *do_wfi_sz;
	unsigned long *resume_offset;
	unsigned long *emif_sram_table;
	unsigned long *ro_sram_data;
	unsigned long resume_address;
};

struct am33xx_pm_platform_data {
	int     (*init)(int (*idle)(u32 wfi_flags));
	int     (*deinit)(void);
	int	(*soc_suspend)(unsigned int state, int (*fn)(unsigned long),
			       unsigned long args);
	int	(*cpu_suspend)(int (*fn)(unsigned long), unsigned long args);
	void    (*begin_suspend)(void);
	void    (*finish_suspend)(void);
	struct  am33xx_pm_sram_addr *(*get_sram_addrs)(void);
	void (*save_context)(void);
	void (*restore_context)(void);
	int (*check_off_mode_enable)(void);
};

struct am33xx_pm_sram_data {
	u32 wfi_flags;
	u32 l2_aux_ctrl_val;
	u32 l2_prefetch_ctrl_val;
} __packed __aligned(8);

struct am33xx_pm_ro_sram_data {
	u32 amx3_pm_sram_data_virt;
	u32 amx3_pm_sram_data_phys;
	void __iomem *rtc_base_virt;
} __packed __aligned(8);

#endif /* __ASSEMBLER__ */
#endif /* _LINUX_PLATFORM_DATA_PM33XX_H */
