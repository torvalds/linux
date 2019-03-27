/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/ethernet.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/fmn.h>
#include <mips/nlm/hal/pic.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/nae.h>
#include <mips/nlm/hal/uart.h>
#include <mips/nlm/hal/poe.h>

#include <mips/nlm/xlp.h>
#include <mips/nlm/board.h>
#include <mips/nlm/msgring.h>

static uint8_t board_eeprom_buf[EEPROM_SIZE];
static int board_eeprom_set;

struct xlp_board_info xlp_board_info;

struct vfbid_tbl {
	int vfbid;
	int dest_vc;
};

/* XXXJC : this should be derived from msg thread mask */
static struct vfbid_tbl nlm_vfbid[] = {
	/* NULL FBID should map to cpu0 to detect NAE send msg errors */
	{127,   0}, /* NAE <-> NAE mappings */
	{51, 1019}, {50, 1018}, {49, 1017}, {48, 1016},
	{47, 1015}, {46, 1014}, {45, 1013}, {44, 1012},
	{43, 1011}, {42, 1010}, {41, 1009}, {40, 1008},
	{39, 1007}, {38, 1006}, {37, 1005}, {36, 1004},
	{35, 1003}, {34, 1002}, {33, 1001}, {32, 1000},
	/* NAE <-> CPU mappings, freeback got to vc 3 of each thread */
	{31,  127}, {30,  123}, {29,  119}, {28,  115},
	{27,  111}, {26,  107}, {25,  103}, {24,   99},
	{23,   95}, {22,   91}, {21,   87}, {20,   83},
	{19,   79}, {18,   75}, {17,   71}, {16,   67},
	{15,   63}, {14,   59}, {13,   55}, {12,   51},
	{11,   47}, {10,   43}, { 9,   39}, { 8,   35},
	{ 7,   31}, { 6,   27}, { 5,   23}, { 4,   19},
	{ 3,   15}, { 2,   11}, { 1,    7}, { 0,    3},
};

static struct vfbid_tbl nlm3xx_vfbid[] = {
	/* NULL FBID should map to cpu0 to detect NAE send msg errors */
	{127,   0}, /* NAE <-> NAE mappings */
	{39,  503}, {38,  502}, {37,  501}, {36,  500},
	{35,  499}, {34,  498}, {33,  497}, {32,  496},
	/* NAE <-> CPU mappings, freeback got to vc 3 of each thread */
	{31,  127}, {30,  123}, {29,  119}, {28,  115},
	{27,  111}, {26,  107}, {25,  103}, {24,   99},
	{23,   95}, {22,   91}, {21,   87}, {20,   83},
	{19,   79}, {18,   75}, {17,   71}, {16,   67},
	{15,   63}, {14,   59}, {13,   55}, {12,   51},
	{11,   47}, {10,   43}, { 9,   39}, { 8,   35},
	{ 7,   31}, { 6,   27}, { 5,   23}, { 4,   19},
	{ 3,   15}, { 2,   11}, { 1,    7}, { 0,    3},
};

int
nlm_get_vfbid_mapping(int vfbid)
{
	int i, nentries;
	struct vfbid_tbl *p;

	if (nlm_is_xlp3xx()) {
		nentries = nitems(nlm3xx_vfbid);
		p = nlm3xx_vfbid;
	} else {
		nentries = nitems(nlm_vfbid);
		p = nlm_vfbid;
	}

	for (i = 0; i < nentries; i++) {
		if (p[i].vfbid == vfbid)
		    return (p[i].dest_vc);
	}

	return (-1);
}

int
nlm_get_poe_distvec(int vec, uint32_t *distvec)
{

	if (vec != 0)
		return (-1);  /* we support just vec 0 */
	nlm_calc_poe_distvec(xlp_msg_thread_mask, 0, 0, 0,
	    0x1 << XLPGE_RX_VC, distvec);
	return (0);
}

/*
 * All our knowledge of chip and board that cannot be detected by probing
 * at run-time goes here
 */

void
xlpge_get_macaddr(uint8_t *macaddr)
{

	if (board_eeprom_set == 0) {
		/* No luck, take some reasonable value */
		macaddr[0] = 0x00; macaddr[1] = 0x0f; macaddr[2] = 0x30;
		macaddr[3] = 0x20; macaddr[4] = 0x0d; macaddr[5] = 0x5b;
	} else
		memcpy(macaddr, &board_eeprom_buf[EEPROM_MACADDR_OFFSET],
		    ETHER_ADDR_LEN);
}

static void
nlm_setup_port_defaults(struct xlp_port_ivars *p)
{
	p->loopback_mode = 0;
	p->num_channels = 1;
	p->free_desc_sizes = 2048;
	p->vlan_pri_en = 0;
	p->hw_parser_en = 1;
	p->ieee1588_userval = 0;
	p->ieee1588_ptpoff = 0;
	p->ieee1588_tmr1 = 0;
	p->ieee1588_tmr2 = 0;
	p->ieee1588_tmr3 = 0;
	p->ieee1588_inc_intg = 0;
	p->ieee1588_inc_den = 1;
	p->ieee1588_inc_num = 1;

	if (nlm_is_xlp3xx()) {
		p->stg2_fifo_size = XLP3XX_STG2_FIFO_SZ;
		p->eh_fifo_size = XLP3XX_EH_FIFO_SZ;
		p->frout_fifo_size = XLP3XX_FROUT_FIFO_SZ;
		p->ms_fifo_size = XLP3XX_MS_FIFO_SZ;
		p->pkt_fifo_size = XLP3XX_PKT_FIFO_SZ;
		p->pktlen_fifo_size = XLP3XX_PKTLEN_FIFO_SZ;
		p->max_stg2_offset = XLP3XX_MAX_STG2_OFFSET;
		p->max_eh_offset = XLP3XX_MAX_EH_OFFSET;
		p->max_frout_offset = XLP3XX_MAX_FREE_OUT_OFFSET;
		p->max_ms_offset = XLP3XX_MAX_MS_OFFSET;
		p->max_pmem_offset = XLP3XX_MAX_PMEM_OFFSET;
		p->stg1_2_credit = XLP3XX_STG1_2_CREDIT;
		p->stg2_eh_credit = XLP3XX_STG2_EH_CREDIT;
		p->stg2_frout_credit = XLP3XX_STG2_FROUT_CREDIT;
		p->stg2_ms_credit = XLP3XX_STG2_MS_CREDIT;
	} else {
		p->stg2_fifo_size = XLP8XX_STG2_FIFO_SZ;
		p->eh_fifo_size = XLP8XX_EH_FIFO_SZ;
		p->frout_fifo_size = XLP8XX_FROUT_FIFO_SZ;
		p->ms_fifo_size = XLP8XX_MS_FIFO_SZ;
		p->pkt_fifo_size = XLP8XX_PKT_FIFO_SZ;
		p->pktlen_fifo_size = XLP8XX_PKTLEN_FIFO_SZ;
		p->max_stg2_offset = XLP8XX_MAX_STG2_OFFSET;
		p->max_eh_offset = XLP8XX_MAX_EH_OFFSET;
		p->max_frout_offset = XLP8XX_MAX_FREE_OUT_OFFSET;
		p->max_ms_offset = XLP8XX_MAX_MS_OFFSET;
		p->max_pmem_offset = XLP8XX_MAX_PMEM_OFFSET;
		p->stg1_2_credit = XLP8XX_STG1_2_CREDIT;
		p->stg2_eh_credit = XLP8XX_STG2_EH_CREDIT;
		p->stg2_frout_credit = XLP8XX_STG2_FROUT_CREDIT;
		p->stg2_ms_credit = XLP8XX_STG2_MS_CREDIT;
	}

	switch (p->type) {
	case SGMIIC:
		p->num_free_descs = 52;
		p->iface_fifo_size = 13;
		p->rxbuf_size = 128;
		p->rx_slots_reqd = SGMII_CAL_SLOTS;
		p->tx_slots_reqd = SGMII_CAL_SLOTS;
		if (nlm_is_xlp3xx())
		    p->pseq_fifo_size = 30;
		else
		    p->pseq_fifo_size = 62;
		break;
	case ILC:
		p->num_free_descs = 150;
		p->rxbuf_size = 944;
		p->rx_slots_reqd = IL8_CAL_SLOTS;
		p->tx_slots_reqd = IL8_CAL_SLOTS;
		p->pseq_fifo_size = 225;
		p->iface_fifo_size = 55;
		break;
	case XAUIC:
	default:
		p->num_free_descs = 150;
		p->rxbuf_size = 944;
		p->rx_slots_reqd = XAUI_CAL_SLOTS;
		p->tx_slots_reqd = XAUI_CAL_SLOTS;
		if (nlm_is_xlp3xx()) {
		    p->pseq_fifo_size = 120;
		    p->iface_fifo_size = 52;
		} else {
		    p->pseq_fifo_size = 225;
		    p->iface_fifo_size = 55;
		}
		break;
	}
}

/* XLP 8XX evaluation boards have the following phy-addr
 * assignment. There are two external mdio buses in XLP --
 * bus 0 and bus 1. The management ports (16 and 17) are
 * on mdio bus 0 while blocks/complexes[0 to 3] are all
 * on mdio bus 1. The phy_addr on bus 0 (mgmt ports 16
 * and 17) match the port numbers.
 * These are the details:
 * block  port   phy_addr   mdio_bus
 * ====================================
 * 0         0     4          1
 * 0         1     7          1
 * 0         2     6          1
 * 0         3     5          1
 * 1         0     8          1
 * 1         1     11         1
 * 1         2     10         1
 * 1         3     9          1
 * 2         0     0          1
 * 2         1     3          1
 * 2         2     2          1
 * 2         3     1          1
 * 3         0     12         1
 * 3         1     15         1
 * 3         2     14         1
 * 3         3     13         1
 *
 * 4         0     16         0
 * 4         1     17         0
 *
 * The XLP 3XX evaluation boards have the following phy-addr
 * assignments.
 * block  port   phy_addr   mdio_bus
 * ====================================
 * 0         0     4          0
 * 0         1     7          0
 * 0         2     6          0
 * 0         3     5          0
 * 1         0     8          0
 * 1         1     11         0
 * 1         2     10         0
 * 1         3     9          0
 */
static void
nlm_board_get_phyaddr(int block, int port, int *phyaddr)
{
	switch (block) {
	case 0: switch (port) {
		case 0: *phyaddr = 4; break;
		case 1: *phyaddr = 7; break;
		case 2: *phyaddr = 6; break;
		case 3: *phyaddr = 5; break;
		}
		break;
	case 1: switch (port) {
		case 0: *phyaddr = 8; break;
		case 1: *phyaddr = 11; break;
		case 2: *phyaddr = 10; break;
		case 3: *phyaddr = 9; break;
		}
		break;
	case 2: switch (port) {
		case 0: *phyaddr = 0; break;
		case 1: *phyaddr = 3; break;
		case 2: *phyaddr = 2; break;
		case 3: *phyaddr = 1; break;
		}
		break;
	case 3: switch (port) {
		case 0: *phyaddr = 12; break;
		case 1: *phyaddr = 15; break;
		case 2: *phyaddr = 14; break;
		case 3: *phyaddr = 13; break;
		}
		break;
	case 4: switch (port) { /* management SGMII */
		case 0: *phyaddr = 16; break;
		case 1: *phyaddr = 17; break;
		}
		break;
	}
}


static void
nlm_print_processor_info(void)
{
	uint32_t procid;
	int prid, rev;
	char *chip, *revstr;

	procid = mips_rd_prid();
	prid = (procid >> 8) & 0xff;
	rev = procid & 0xff;

	switch (prid) {
	case CHIP_PROCESSOR_ID_XLP_8XX:
		chip = "XLP 832";
		break;
	case CHIP_PROCESSOR_ID_XLP_3XX:
		chip = "XLP 3xx";
		break;
	case CHIP_PROCESSOR_ID_XLP_432:
	case CHIP_PROCESSOR_ID_XLP_416:
		chip = "XLP 4xx";
		break;
	default:
		chip = "XLP ?xx";
		break;
	}
	switch (rev) {
	case 0:
		revstr = "A0"; break;
	case 1:
		revstr = "A1"; break;
	case 2:
		revstr = "A2"; break;
	case 3:
		revstr = "B0"; break;
	case 4:
		revstr = "B1"; break;
	default:
		revstr = "??"; break;
	}

	printf("Processor info:\n");
	printf("  Netlogic %s %s [%x]\n", chip, revstr, procid);
}

/*
 * All our knowledge of chip and board that cannot be detected by probing
 * at run-time goes here
 */
static int
nlm_setup_xlp_board(int node)
{
	struct xlp_board_info	*boardp;
	struct xlp_node_info	*nodep;
	struct xlp_nae_ivars	*naep;
	struct xlp_block_ivars	*blockp;
	struct xlp_port_ivars	*portp;
	uint64_t cpldbase, nae_pcibase;
	int	block, port, rv, dbtype, usecpld = 0, evp = 0, svp = 0;
	uint8_t *b;

	/* start with a clean slate */
	boardp = &xlp_board_info;
	if (boardp->nodemask == 0)
		memset(boardp, 0, sizeof(xlp_board_info));
	boardp->nodemask |= (1 << node);
	nlm_print_processor_info();

	b =  board_eeprom_buf;
	rv = nlm_board_eeprom_read(node, EEPROM_I2CBUS, EEPROM_I2CADDR, 0, b,
	    EEPROM_SIZE);
	if (rv == 0) {
		board_eeprom_set = 1;
		printf("Board info (EEPROM on i2c@%d at %#X):\n",
		    EEPROM_I2CBUS, EEPROM_I2CADDR);
		printf("  Model:      %7.7s %2.2s\n", &b[16], &b[24]);
		printf("  Serial #:   %3.3s-%2.2s\n", &b[27], &b[31]);
		printf("  MAC addr:   %02x:%02x:%02x:%02x:%02x:%02x\n",
		    b[2], b[3], b[4], b[5], b[6], b[7]);
	} else
		printf("Board Info: Error on EEPROM read (i2c@%d %#X).\n",
		    EEPROM_I2CBUS, EEPROM_I2CADDR);

	nae_pcibase = nlm_get_nae_pcibase(node);
	nodep = &boardp->nodes[node];
	naep = &nodep->nae_ivars;
	naep->node = node;

	/* frequency at which network block runs */
	naep->freq = 500;

	/* CRC16 polynomial used for flow table generation */
	naep->flow_crc_poly = 0xffff;
	naep->hw_parser_en = 1;
	naep->prepad_en = 1;
	naep->prepad_size = 3; /* size in 16 byte units */
	naep->ieee_1588_en = 1;

	naep->ilmask = 0x0;	/* set this based on daughter card */
	naep->xauimask = 0x0;	/* set this based on daughter card */
	naep->sgmiimask = 0x0;	/* set this based on daughter card */
	naep->nblocks = nae_num_complex(nae_pcibase);
	if (strncmp(&b[16], "PCIE", 4) == 0) {
		usecpld = 0; /* XLP PCIe card */
		/* Broadcom's XLP PCIe card has the following
		 * blocks fixed.
		 * blk 0-XAUI, 1-XAUI, 4-SGMII(one port) */
		naep->blockmask = 0x13;
	} else if (strncmp(&b[16], "MB-EVP", 6) == 0) {
		usecpld = 1; /* XLP non-PCIe card which has CPLD */
		evp = 1;
		naep->blockmask = (1 << naep->nblocks) - 1;
	} else if ((strncmp(&b[16], "MB-S", 4) == 0) ||
	    (strncmp(&b[16], "MB_S", 4) == 0)) {
		usecpld = 1; /* XLP non-PCIe card which has CPLD */
		svp = 1;
		/* 3xx chip reports one block extra which is a bug */
		naep->nblocks = naep->nblocks - 1;
		naep->blockmask = (1 << naep->nblocks) - 1;
	} else {
		printf("ERROR!!! Board type:%7s didn't match any board"
		    " type we support\n", &b[16]);
		return (-1);
	}
	cpldbase = nlm_board_cpld_base(node, XLP_EVB_CPLD_CHIPSELECT);

	/* pretty print network config */
	printf("Network config");
	if (usecpld)
		printf("(from CPLD@%d):\n", XLP_EVB_CPLD_CHIPSELECT);
	else
		printf("(defaults):\n");
	printf("  NAE@%d Blocks: ", node);
	for (block = 0; block < naep->nblocks; block++) {
		char *s = "???";

		if ((naep->blockmask & (1 << block)) == 0)
			continue;
		blockp = &naep->block_ivars[block];
		blockp->block = block;
		if (usecpld)
			dbtype = nlm_board_cpld_dboard_type(cpldbase, block);
		else
			dbtype = DCARD_XAUI;  /* default XAUI */

		/* XLP PCIe cards */
		if ((!evp && !svp) && ((block == 2) || (block == 3)))
			dbtype = DCARD_NOT_PRSNT;

		if (block == 4) {
			/* management block 4 on 8xx or XLP PCIe */
			blockp->type = SGMIIC;
			if (evp)
				blockp->portmask = 0x3;
			else
				blockp->portmask = 0x1;
			naep->sgmiimask |= (1 << block);
		} else {
			switch (dbtype) {
			case DCARD_ILAKEN:
				blockp->type = ILC;
				blockp->portmask = 0x1;
				naep->ilmask |= (1 << block);
				break;
			case DCARD_SGMII:
				blockp->type = SGMIIC;
				blockp->portmask = 0xf;
				naep->sgmiimask |= (1 << block);
				break;
			case DCARD_XAUI:
				blockp->type = XAUIC;
				blockp->portmask = 0x1;
				naep->xauimask |= (1 << block);
				break;
			default: /* DCARD_NOT_PRSNT */
				blockp->type = UNKNOWN;
				blockp->portmask = 0;
				break;
			}
		}
		if (blockp->type != UNKNOWN) {
			for (port = 0; port < PORTS_PER_CMPLX; port++) {
				if ((blockp->portmask & (1 << port)) == 0)
					continue;
				portp = &blockp->port_ivars[port];
				nlm_board_get_phyaddr(block, port,
				    &portp->phy_addr);
				if (svp || (block == 4))
					portp->mdio_bus = 0;
				else
					portp->mdio_bus = 1;
				portp->port = port;
				portp->block = block;
				portp->node = node;
				portp->type = blockp->type;
				nlm_setup_port_defaults(portp);
			}
		}
		switch (blockp->type) {
		case SGMIIC : s = "SGMII"; break;
		case XAUIC  : s = "XAUI"; break;
		case ILC    : s = "IL"; break;
		}
		printf(" [%d %s]", block, s);
	}
	printf("\n");
	return (0);
}

int nlm_board_info_setup(void)
{
	if (nlm_setup_xlp_board(0) != 0)
		return (-1);
	return (0);
}
