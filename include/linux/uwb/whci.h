/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Wireless Host Controller Interface for Ultra-Wide-Band and Wireless USB
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * References:
 *   [WHCI] Wireless Host Controller Interface Specification for
 *          Certified Wireless Universal Serial Bus, revision 0.95.
 */
#ifndef _LINUX_UWB_WHCI_H_
#define _LINUX_UWB_WHCI_H_

#include <linux/pci.h>

/*
 * UWB interface capability registers (offsets from UWBBASE)
 *
 * [WHCI] section 2.2
 */
#define UWBCAPINFO	0x00 /* == UWBCAPDATA(0) */
#  define UWBCAPINFO_TO_N_CAPS(c)	(((c) >> 0)  & 0xFull)
#define UWBCAPDATA(n)	(8*(n))
#  define UWBCAPDATA_TO_VERSION(c)	(((c) >> 32) & 0xFFFFull)
#  define UWBCAPDATA_TO_OFFSET(c)	(((c) >> 18) & 0x3FFFull)
#  define UWBCAPDATA_TO_BAR(c)		(((c) >> 16) & 0x3ull)
#  define UWBCAPDATA_TO_SIZE(c)		((((c) >> 8) & 0xFFull) * sizeof(u32))
#  define UWBCAPDATA_TO_CAP_ID(c)	(((c) >> 0)  & 0xFFull)

/* Size of the WHCI capability data (including the RC capability) for
   a device with n capabilities. */
#define UWBCAPDATA_SIZE(n) (8 + 8*(n))


/*
 * URC registers (offsets from URCBASE)
 *
 * [WHCI] section 2.3
 */
#define URCCMD		0x00
#  define URCCMD_RESET		(1 << 31)  /* UMC Hardware reset */
#  define URCCMD_RS		(1 << 30)  /* Run/Stop */
#  define URCCMD_EARV		(1 << 29)  /* Event Address Register Valid */
#  define URCCMD_ACTIVE		(1 << 15)  /* Command is active */
#  define URCCMD_IWR		(1 << 14)  /* Interrupt When Ready */
#  define URCCMD_SIZE_MASK	0x00000fff /* Command size mask */
#define URCSTS		0x04
#  define URCSTS_EPS		(1 << 17)  /* Event Processing Status */
#  define URCSTS_HALTED		(1 << 16)  /* RC halted */
#  define URCSTS_HSE		(1 << 10)  /* Host System Error...fried */
#  define URCSTS_ER		(1 <<  9)  /* Event Ready */
#  define URCSTS_RCI		(1 <<  8)  /* Ready for Command Interrupt */
#  define URCSTS_INT_MASK	0x00000700 /* URC interrupt sources */
#  define URCSTS_ISI		0x000000ff /* Interrupt Source Identification */
#define URCINTR		0x08
#  define URCINTR_EN_ALL	0x000007ff /* Enable all interrupt sources */
#define URCCMDADDR	0x10
#define URCEVTADDR	0x18
#  define URCEVTADDR_OFFSET_MASK 0xfff    /* Event pointer offset mask */


/** Write 32 bit @value to little endian register at @addr */
static inline
void le_writel(u32 value, void __iomem *addr)
{
	iowrite32(value, addr);
}


/** Read from 32 bit little endian register at @addr */
static inline
u32 le_readl(void __iomem *addr)
{
	return ioread32(addr);
}


/** Write 64 bit @value to little endian register at @addr */
static inline
void le_writeq(u64 value, void __iomem *addr)
{
	iowrite32(value, addr);
	iowrite32(value >> 32, addr + 4);
}


/** Read from 64 bit little endian register at @addr */
static inline
u64 le_readq(void __iomem *addr)
{
	u64 value;
	value  = ioread32(addr);
	value |= (u64)ioread32(addr + 4) << 32;
	return value;
}

extern int whci_wait_for(struct device *dev, u32 __iomem *reg,
			 u32 mask, u32 result,
			 unsigned long max_ms,  const char *tag);

#endif /* #ifndef _LINUX_UWB_WHCI_H_ */
