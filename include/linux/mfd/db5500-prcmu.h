/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * U5500 PRCMU API.
 */
#ifndef __MACH_PRCMU_U5500_H
#define __MACH_PRCMU_U5500_H

#ifdef CONFIG_UX500_SOC_DB5500

void db5500_prcmu_early_init(void);

int db5500_prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size);
int db5500_prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size);

#else /* !CONFIG_UX500_SOC_DB5500 */

static inline void db5500_prcmu_early_init(void)
{
}

static inline int db5500_prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int db5500_prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

#endif /* CONFIG_UX500_SOC_DB5500 */

static inline int db5500_prcmu_config_abb_event_readout(u32 abb_events)
{
#ifdef CONFIG_MACH_U5500_SIMULATOR
	return 0;
#else
	return -1;
#endif
}

#endif /* __MACH_PRCMU_U5500_H */
