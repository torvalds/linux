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
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#ifndef __NLM_BOARD_H__
#define __NLM_BOARD_H__

#define XLP_NAE_NBLOCKS		5
#define XLP_NAE_NPORTS		4

/*
 * EVP board EEPROM info
 */
#define	EEPROM_I2CBUS		1
#define	EEPROM_I2CADDR		0xAE
#define	EEPROM_SIZE		48
#define	EEPROM_MACADDR_OFFSET	2

/* used if there is no FDT */
#define	BOARD_CONSOLE_SPEED	115200
#define	BOARD_CONSOLE_UART	0

/*
 * EVP board CPLD chip select and daughter card info field
 */
#define XLP_EVB_CPLD_CHIPSELECT	2

#define DCARD_ILAKEN		0x0
#define DCARD_SGMII		0x1
#define DCARD_XAUI		0x2
#define DCARD_NOT_PRSNT		0x3

#if !defined(LOCORE) && !defined(__ASSEMBLY__)
/*
 * NAE configuration
 */

struct xlp_port_ivars {
	int	port;
	int	block;
	int	node;
	int	type;
	int	phy_addr;
	int	mdio_bus;
	int	loopback_mode;
	int	num_channels;
	int	free_desc_sizes;
	int	num_free_descs;
	int	pseq_fifo_size;
	int	iface_fifo_size;
	int	rxbuf_size;
	int	rx_slots_reqd;
	int	tx_slots_reqd;
	int	vlan_pri_en;
	int	stg2_fifo_size;
	int	eh_fifo_size;
	int	frout_fifo_size;
	int	ms_fifo_size;
	int	pkt_fifo_size;
	int	pktlen_fifo_size;
	int	max_stg2_offset;
	int	max_eh_offset;
	int	max_frout_offset;
	int	max_ms_offset;
	int	max_pmem_offset;
	int	stg1_2_credit;
	int	stg2_eh_credit;
	int	stg2_frout_credit;
	int	stg2_ms_credit;
	int	hw_parser_en;
	u_int	ieee1588_inc_intg;
	u_int	ieee1588_inc_den;
	u_int	ieee1588_inc_num;
	uint64_t ieee1588_userval;
	uint64_t ieee1588_ptpoff;
	uint64_t ieee1588_tmr1;
	uint64_t ieee1588_tmr2;
	uint64_t ieee1588_tmr3;
};

struct xlp_block_ivars {
	int	block;
	int	type;
	u_int	portmask;
	struct xlp_port_ivars	port_ivars[XLP_NAE_NPORTS];
};

struct xlp_nae_ivars {
	int	node;
	int	nblocks;
	u_int	blockmask;
	u_int	ilmask;
	u_int	xauimask;
	u_int	sgmiimask;
	int	freq;
	u_int	flow_crc_poly;
	u_int	hw_parser_en;
	u_int	prepad_en;
	u_int	prepad_size;	/* size in 16 byte units */
	u_int	ieee_1588_en;
	struct xlp_block_ivars	block_ivars[XLP_NAE_NBLOCKS];
};

struct xlp_board_info {
	u_int	nodemask;
	struct xlp_node_info {
		struct xlp_nae_ivars	nae_ivars;
	} nodes[XLP_MAX_NODES];
};

extern struct xlp_board_info xlp_board_info;

/* Network configuration */
int nlm_get_vfbid_mapping(int);
int nlm_get_poe_distvec(int vec, uint32_t *distvec);
void xlpge_get_macaddr(uint8_t *macaddr);

int nlm_board_info_setup(void);

/* EEPROM & CPLD */
int nlm_board_eeprom_read(int node, int i2cbus, int addr, int offs,
    uint8_t *buf,int sz);
uint64_t nlm_board_cpld_base(int node, int chipselect);
int nlm_board_cpld_majorversion(uint64_t cpldbase);
int nlm_board_cpld_minorversion(uint64_t cpldbase);
void nlm_board_cpld_reset(uint64_t cpldbase);
int nlm_board_cpld_dboard_type(uint64_t cpldbase, int slot);

#endif
#endif
