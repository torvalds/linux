/*
 * Platform information definitions for the
 * universal Freescale Ethernet driver.
 *
 * Copyright (c) 2003 Intracom S.A. 
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * 2005 (c) MontaVista Software, Inc. 
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License 
 * version 2. This program is licensed "as is" without any warranty of any 
 * kind, whether express or implied.
 */

#ifndef FS_ENET_PD_H
#define FS_ENET_PD_H

#include <linux/clk.h>
#include <linux/string.h>
#include <linux/of_mdio.h>
#include <linux/if_ether.h>
#include <asm/types.h>

#define FS_ENET_NAME	"fs_enet"

enum fs_id {
	fsid_fec1,
	fsid_fec2,
	fsid_fcc1,
	fsid_fcc2,
	fsid_fcc3,
	fsid_scc1,
	fsid_scc2,
	fsid_scc3,
	fsid_scc4,
};

#define FS_MAX_INDEX	9

static inline int fs_get_fec_index(enum fs_id id)
{
	if (id >= fsid_fec1 && id <= fsid_fec2)
		return id - fsid_fec1;
	return -1;
}

static inline int fs_get_fcc_index(enum fs_id id)
{
	if (id >= fsid_fcc1 && id <= fsid_fcc3)
		return id - fsid_fcc1;
	return -1;
}

static inline int fs_get_scc_index(enum fs_id id)
{
	if (id >= fsid_scc1 && id <= fsid_scc4)
		return id - fsid_scc1;
	return -1;
}

static inline int fs_fec_index2id(int index)
{
	int id = fsid_fec1 + index - 1;
	if (id >= fsid_fec1 && id <= fsid_fec2)
		return id;
	return FS_MAX_INDEX;
		}

static inline int fs_fcc_index2id(int index)
{
	int id = fsid_fcc1 + index - 1;
	if (id >= fsid_fcc1 && id <= fsid_fcc3)
		return id;
	return FS_MAX_INDEX;
}

static inline int fs_scc_index2id(int index)
{
	int id = fsid_scc1 + index - 1;
	if (id >= fsid_scc1 && id <= fsid_scc4)
		return id;
	return FS_MAX_INDEX;
}

enum fs_mii_method {
	fsmii_fixed,
	fsmii_fec,
	fsmii_bitbang,
};

enum fs_ioport {
	fsiop_porta,
	fsiop_portb,
	fsiop_portc,
	fsiop_portd,
	fsiop_porte,
};

struct fs_mii_bit {
	u32 offset;
	u8 bit;
	u8 polarity;
};
struct fs_mii_bb_platform_info {
	struct fs_mii_bit 	mdio_dir;
	struct fs_mii_bit 	mdio_dat;
	struct fs_mii_bit	mdc_dat;
	int delay;	/* delay in us         */
	int irq[32]; 	/* irqs per phy's */
};

struct fs_platform_info {
	/* device specific information */
	u32 cp_command;		/* CPM page/sblock/mcn */

	u32 dpram_offset;
	
	struct device_node *phy_node;

	int rx_ring, tx_ring;	/* number of buffers on rx     */
	int rx_copybreak;	/* limit we copy small frames  */
	int napi_weight;	/* NAPI weight                 */

	int use_rmii;		/* use RMII mode 	       */

	struct clk *clk_per;	/* 'per' clock for register access */
};
struct fs_mii_fec_platform_info {
	u32 irq[32];
	u32 mii_speed;
};

#endif
