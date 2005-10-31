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

#include <linux/version.h>
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

struct fs_mii_bus_info {
	int method;		/* mii method                  */
	int id;			/* the id of the mii_bus       */
	int disable_aneg;	/* if the controller needs to negothiate speed & duplex */
	int lpa; 		/* the default board-specific vallues will be applied otherwise */

	union {
		struct {
			int duplex;
			int speed;
		} fixed;

		struct {
			/* nothing */
		} fec;
		
		struct {
			/* nothing */
		} scc;

		struct {
			int mdio_port;	/* port & bit for MDIO */
			int mdio_bit;
			int mdc_port;	/* port & bit for MDC  */
			int mdc_bit;
			int delay;	/* delay in us         */
		} bitbang;
	} i;
};

struct fs_platform_info {
	
	void(*init_ioports)(void);
	/* device specific information */
	int fs_no;		/* controller index            */

	u32 cp_page;		/* CPM page */
	u32 cp_block;		/* CPM sblock */
	
	u32 clk_trx;		/* some stuff for pins & mux configuration*/
	u32 clk_route;
	u32 clk_mask;
	
	u32 mem_offset;
	u32 dpram_offset;
	u32 fcc_regs_c;
	
	u32 device_flags;

	int phy_addr;		/* the phy address (-1 no phy) */
	int phy_irq;		/* the phy irq (if it exists)  */

	const struct fs_mii_bus_info *bus_info;

	int rx_ring, tx_ring;	/* number of buffers on rx     */
	__u8 macaddr[6];	/* mac address                 */
	int rx_copybreak;	/* limit we copy small frames  */
	int use_napi;		/* use NAPI                    */
	int napi_weight;	/* NAPI weight                 */

	int use_rmii;		/* use RMII mode 	       */
};

#endif
