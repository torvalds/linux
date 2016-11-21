/*
 * Copyright 2012-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BUSFREQ_H__
#define __ASM_ARCH_MXC_BUSFREQ_H__

#include <linux/notifier.h>
#include <linux/regulator/consumer.h>

/*
 * This enumerates busfreq low power mode entry and exit.
 */
enum busfreq_event {
	LOW_BUSFREQ_ENTER,
	LOW_BUSFREQ_EXIT,
};

/*
  * This enumerates the system bus and ddr frequencies in various modes.
  * BUS_FREQ_HIGH - DDR @ 528MHz, AHB @ 132MHz.
  * BUS_FREQ_MED - DDR @ 400MHz, AHB @ 132MHz
  * BUS_FREQ_AUDIO - DDR @ 50MHz/100MHz, AHB @ 24MHz.
  * BUS_FREQ_LOW  - DDR @ 24MHz, AHB @ 24MHz.
  * BUS_FREQ_ULTRA_LOW - DDR @ 1MHz, AHB - 3MHz.
  *
  * Drivers need to request/release the bus/ddr frequencies based on
  * their performance requirements. Drivers cannot request/release
  * BUS_FREQ_ULTRA_LOW mode as this mode is automatically entered from
  * either BUS_FREQ_AUDIO or BUS_FREQ_LOW
  * modes.
  */
enum bus_freq_mode {
	BUS_FREQ_HIGH,
	BUS_FREQ_MED,
	BUS_FREQ_AUDIO,
	BUS_FREQ_LOW,
	BUS_FREQ_ULTRA_LOW,
};

#ifdef CONFIG_CPU_FREQ
extern struct regulator *arm_reg;
extern struct regulator *soc_reg;
void request_bus_freq(enum bus_freq_mode mode);
void release_bus_freq(enum bus_freq_mode mode);
int register_busfreq_notifier(struct notifier_block *nb);
int unregister_busfreq_notifier(struct notifier_block *nb);
int get_bus_freq_mode(void);
#else
static inline void request_bus_freq(enum bus_freq_mode mode)
{
}
static inline void release_bus_freq(enum bus_freq_mode mode)
{
}
static inline int register_busfreq_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline int unregister_busfreq_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline int get_bus_freq_mode(void)
{
	return BUS_FREQ_HIGH;
}
#endif
#endif
