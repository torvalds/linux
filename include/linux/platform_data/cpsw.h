/*
 * Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __CPSW_H__
#define __CPSW_H__

#include <linux/if_ether.h>

struct cpsw_slave_data {
	char		phy_id[MII_BUS_ID_SIZE];
	int		phy_if;
	u8		mac_addr[ETH_ALEN];
};

struct cpsw_platform_data {
	u32	ss_reg_ofs;	/* Subsystem control register offset */
	u32	channels;	/* number of cpdma channels (symmetric) */
	u32	slaves;		/* number of slave cpgmac ports */
	struct cpsw_slave_data	*slave_data;
	u32	cpts_active_slave; /* time stamping slave */
	u32	cpts_clock_mult;  /* convert input clock ticks to nanoseconds */
	u32	cpts_clock_shift; /* convert input clock ticks to nanoseconds */
	u32	ale_entries;	/* ale table size */
	u32	bd_ram_size;  /*buffer descriptor ram size */
	u32	rx_descs;	/* Number of Rx Descriptios */
	u32	mac_control;	/* Mac control register */
	u16	default_vlan;	/* Def VLAN for ALE lookup in VLAN aware mode*/
};

#endif /* __CPSW_H__ */
