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
	u32		slave_reg_ofs;
	u32		sliver_reg_ofs;
	const char	*phy_id;
	int		phy_if;
	u8		mac_addr[ETH_ALEN];
};

struct cpsw_platform_data {
	u32	ss_reg_ofs;	/* Subsystem control register offset */
	u32	channels;	/* number of cpdma channels (symmetric) */
	u32	cpdma_reg_ofs;	/* cpdma register offset */
	u32	cpdma_sram_ofs;	/* cpdma sram offset */

	u32	slaves;		/* number of slave cpgmac ports */
	struct cpsw_slave_data	*slave_data;

	u32	ale_reg_ofs;	/* address lookup engine reg offset */
	u32	ale_entries;	/* ale table size */

	u32	host_port_reg_ofs; /* cpsw cpdma host port registers */
	u32     host_port_num; /* The port number for the host port */

	u32	hw_stats_reg_ofs;  /* cpsw hardware statistics counters */

	u32	bd_ram_ofs;   /* embedded buffer descriptor RAM offset*/
	u32	bd_ram_size;  /*buffer descriptor ram size */
	u32	hw_ram_addr; /*if the HW address for BD RAM is different */
	bool	no_bd_ram; /* no embedded BD ram*/

	u32	rx_descs;	/* Number of Rx Descriptios */

	u32	mac_control;	/* Mac control register */
};

#endif /* __CPSW_H__ */
