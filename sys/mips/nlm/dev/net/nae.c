/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/systm.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/nae.h>
#include <mips/nlm/hal/mdio.h>
#include <mips/nlm/hal/sgmii.h>
#include <mips/nlm/hal/xaui.h>

#include <mips/nlm/board.h>
#include <mips/nlm/xlp.h>

void
nlm_nae_flush_free_fifo(uint64_t nae_base, int nblocks)
{
	uint32_t data, fifo_mask;

	fifo_mask = (1 << (4 * nblocks)) - 1;

	nlm_write_nae_reg(nae_base, NAE_RX_FREE_FIFO_POP, fifo_mask);
	do {
		data = nlm_read_nae_reg(nae_base, NAE_RX_FREE_FIFO_POP);
	} while (data != fifo_mask);

	nlm_write_nae_reg(nae_base, NAE_RX_FREE_FIFO_POP, 0);
}

void
nlm_program_nae_parser_seq_fifo(uint64_t nae_base, int maxports,
    struct nae_port_config *cfg)
{
	uint32_t val;
	int start = 0, size, i;

	for (i = 0; i < maxports; i++) {
		size = cfg[i].pseq_fifo_size;
		val = (((size & 0x1fff) << 17) |
		    ((start & 0xfff) << 5) |
		    (i & 0x1f));
		nlm_write_nae_reg(nae_base, NAE_PARSER_SEQ_FIFO_CFG, val);
		start += size;
	}
}

void
nlm_setup_rx_cal_cfg(uint64_t nae_base, int total_num_ports,
    struct nae_port_config *cfg)
{
	int rx_slots = 0, port;
	int cal_len, cal = 0, last_free = 0;
	uint32_t val;

	for (port = 0; port < total_num_ports; port++) {
		if (cfg[port].rx_slots_reqd)
		    rx_slots += cfg[port].rx_slots_reqd;
		if (rx_slots > MAX_CAL_SLOTS) {
			rx_slots = MAX_CAL_SLOTS;
			break;
		}
	}

	cal_len = rx_slots - 1;

	do {
		if (cal >= MAX_CAL_SLOTS)
			break;
		last_free = cal;
		for (port = 0; port < total_num_ports; port++) {
			if (cfg[port].rx_slots_reqd > 0) {
				val = (cal_len << 16) | (port << 8) | cal;
				nlm_write_nae_reg(nae_base,
				    NAE_RX_IF_SLOT_CAL, val);
				cal++;
				cfg[port].rx_slots_reqd--;
			}
		}
		if (last_free == cal)
			break;
	} while (1);
}

void
nlm_setup_tx_cal_cfg(uint64_t nae_base, int total_num_ports,
    struct nae_port_config *cfg)
{
	int tx_slots = 0, port;
	int cal = 0, last_free = 0;
	uint32_t val;

	for (port = 0; port < total_num_ports; port++) {
		if (cfg[port].tx_slots_reqd)
			tx_slots += cfg[port].tx_slots_reqd;
		if (tx_slots > MAX_CAL_SLOTS) {
			tx_slots = MAX_CAL_SLOTS;
			break;
		}
	}

	nlm_write_nae_reg(nae_base, NAE_EGR_NIOR_CAL_LEN_REG, tx_slots - 1);
	do {
		if (cal >= MAX_CAL_SLOTS)
			break;
		last_free = cal;
		for (port = 0; port < total_num_ports; port++) {
			if (cfg[port].tx_slots_reqd > 0) {
				val = (port << 7) | (cal << 1) | 1;
				nlm_write_nae_reg(nae_base,
				    NAE_EGR_NIOR_CRDT_CAL_PROG, val);
				cal++;
				cfg[port].tx_slots_reqd--;
			}
		}
		if (last_free == cal)
			break;
	} while (1);
}

void
nlm_deflate_frin_fifo_carving(uint64_t nae_base, int total_num_ports)
{
	const int minimum_size = 8;
	uint32_t value;
	int intf, start;

	for (intf = 0; intf < total_num_ports; intf++) {
		start = minimum_size * intf;
		value = (minimum_size << 20) | (start << 8) | (intf);
		nlm_write_nae_reg(nae_base, NAE_FREE_IN_FIFO_CFG, value);
	}
}

void
nlm_reset_nae(int node)
{
	uint64_t sysbase;
	uint64_t nae_base;
	uint64_t nae_pcibase;
	uint32_t rx_config;
	uint32_t bar0;
	int reset_bit;

	sysbase  = nlm_get_sys_regbase(node);
	nae_base = nlm_get_nae_regbase(node);
	nae_pcibase = nlm_get_nae_pcibase(node);

	bar0 = nlm_read_pci_reg(nae_pcibase, XLP_PCI_CFGREG4);

#if BYTE_ORDER == LITTLE_ENDIAN
	if (nlm_is_xlp8xx_ax()) {
		uint8_t	val;
		/* membar fixup */
		val = (bar0 >> 24) & 0xff;
		bar0 = (val << 24) | (val << 16) | (val << 8) | val;
	}
#endif

	if (nlm_is_xlp3xx())
		reset_bit = 6;
	else
		reset_bit = 9;

	/* Reset NAE */
	nlm_write_sys_reg(sysbase, SYS_RESET, (1 << reset_bit));

	/* XXXJC - 1s delay here may be too high */
	DELAY(1000000);
	nlm_write_sys_reg(sysbase, SYS_RESET, (0 << reset_bit));
	DELAY(1000000);

	rx_config = nlm_read_nae_reg(nae_base, NAE_RX_CONFIG);
	nlm_write_pci_reg(nae_pcibase, XLP_PCI_CFGREG4, bar0);
}

void
nlm_setup_poe_class_config(uint64_t nae_base, int max_poe_classes,
    int num_contexts, int *poe_cl_tbl)
{
	uint32_t val;
	int i, max_poe_class_ctxt_tbl_sz;

	max_poe_class_ctxt_tbl_sz = num_contexts/max_poe_classes;
	for (i = 0; i < max_poe_class_ctxt_tbl_sz; i++) {
		val = (poe_cl_tbl[(i/max_poe_classes) & 0x7] << 8) | i;
		nlm_write_nae_reg(nae_base, NAE_POE_CLASS_SETUP_CFG, val);
	}
}

void
nlm_setup_vfbid_mapping(uint64_t nae_base)
{
	uint32_t val;
	int dest_vc, vfbid;

	/* 127 is max vfbid */
	for (vfbid = 127; vfbid >= 0; vfbid--) {
		dest_vc = nlm_get_vfbid_mapping(vfbid);
		if (dest_vc < 0)
			continue;
		val = (dest_vc << 16) | (vfbid << 4) | 1;
		nlm_write_nae_reg(nae_base, NAE_VFBID_DESTMAP_CMD, val);
	}
}

void
nlm_setup_flow_crc_poly(uint64_t nae_base, uint32_t poly)
{
	nlm_write_nae_reg(nae_base, NAE_FLOW_CRC16_POLY_CFG, poly);
}

void
nlm_setup_iface_fifo_cfg(uint64_t nae_base, int maxports,
    struct nae_port_config *cfg)
{
	uint32_t reg;
	int fifo_xoff_thresh = 12;
	int i, size;
	int cur_iface_start = 0;

	for (i = 0; i < maxports; i++) {
		size = cfg[i].iface_fifo_size;
		reg = ((fifo_xoff_thresh << 25) |
		    ((size & 0x1ff) << 16) |
		    ((cur_iface_start & 0xff) << 8) |
		    (i & 0x1f));
		nlm_write_nae_reg(nae_base, NAE_IFACE_FIFO_CFG, reg);
		cur_iface_start += size;
	}
}

void
nlm_setup_rx_base_config(uint64_t nae_base, int maxports,
    struct nae_port_config *cfg)
{
	int base = 0;
	uint32_t val;
	int i;
	int id;

	for (i = 0; i < (maxports/2); i++) {
		id = 0x12 + i; /* RX_IF_BASE_CONFIG0 */

		val = (base & 0x3ff);
		base += cfg[(i * 2)].num_channels;

		val |= ((base & 0x3ff) << 16);
		base += cfg[(i * 2) + 1].num_channels;

		nlm_write_nae_reg(nae_base, NAE_REG(7, 0, id), val);
	}
}

void
nlm_setup_rx_buf_config(uint64_t nae_base, int maxports,
    struct nae_port_config *cfg)
{
	uint32_t val;
	int i, sz, k;
	int context = 0;
	int base = 0;

	for (i = 0; i < maxports; i++) {
		if (cfg[i].type == UNKNOWN)
			continue;
		for (k = 0; k < cfg[i].num_channels; k++) {
			/* write index (context num) */
			nlm_write_nae_reg(nae_base, NAE_RXBUF_BASE_DPTH_ADDR,
			    (context+k));

			/* write value (rx buf sizes) */
			sz = cfg[i].rxbuf_size;
			val = 0x80000000 | ((base << 2) & 0x3fff); /* base */
			val |= (((sz << 2)  & 0x3fff) << 16); /* size */

			nlm_write_nae_reg(nae_base, NAE_RXBUF_BASE_DPTH, val);
			nlm_write_nae_reg(nae_base, NAE_RXBUF_BASE_DPTH,
			    (0x7fffffff & val));
			base += sz;
		}
		context += cfg[i].num_channels;
	}
}

void
nlm_setup_freein_fifo_cfg(uint64_t nae_base, struct nae_port_config *cfg)
{
	int size, i;
	uint32_t reg;
	int start = 0, maxbufpool;

	if (nlm_is_xlp8xx())
		maxbufpool = MAX_FREE_FIFO_POOL_8XX;
	else
		maxbufpool = MAX_FREE_FIFO_POOL_3XX;
	for (i = 0; i < maxbufpool; i++) {
		/* Each entry represents 2 descs; hence division by 2 */
		size = (cfg[i].num_free_descs / 2);
		if (size == 0)
			size = 8;
		reg = ((size  & 0x3ff ) << 20) | /* fcSize */
		    ((start & 0x1ff)  << 8) | /* fcStart */
		    (i & 0x1f);

		nlm_write_nae_reg(nae_base, NAE_FREE_IN_FIFO_CFG, reg);
		start += size;
	}
}

/* XXX function name */
int
nlm_get_flow_mask(int num_ports)
{
	const int max_bits = 5; /* upto 32 ports */
	int i;

	/* Compute the number of bits to needed to
	 * represent all the ports */
	for (i = 0; i < max_bits; i++) {
		if (num_ports <= (2 << i))
			return (i + 1);
	}
	return (max_bits);
}

void
nlm_program_flow_cfg(uint64_t nae_base, int port,
    uint32_t cur_flow_base, uint32_t flow_mask)
{
	uint32_t val;

	val = (cur_flow_base << 16) | port;
	val |= ((flow_mask & 0x1f) << 8);
	nlm_write_nae_reg(nae_base, NAE_FLOW_BASEMASK_CFG, val);
}

void
xlp_ax_nae_lane_reset_txpll(uint64_t nae_base, int block, int lane_ctrl,
    int mode)
{
	uint32_t val = 0, saved_data;
	int rext_sel = 0;

	val = PHY_LANE_CTRL_RST |
	    PHY_LANE_CTRL_PWRDOWN |
	    (mode << PHY_LANE_CTRL_PHYMODE_POS);

	/* set comma bypass for XAUI */
	if (mode != PHYMODE_SGMII)
		val |= PHY_LANE_CTRL_BPC_XAUI;

	nlm_write_nae_reg(nae_base, NAE_REG(block, PHY, lane_ctrl), val);

	if (lane_ctrl != 4) {
		rext_sel = (1 << 23);
		if (mode != PHYMODE_SGMII)
			rext_sel |= PHY_LANE_CTRL_BPC_XAUI;

		val = nlm_read_nae_reg(nae_base,
		    NAE_REG(block, PHY, lane_ctrl));
		val &= ~PHY_LANE_CTRL_RST;
		val |= rext_sel;

		/* Resetting PMA for non-zero lanes */
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, PHY, lane_ctrl), val);

		DELAY(20000);	/* 20 ms delay, XXXJC: needed? */

		val |= PHY_LANE_CTRL_RST;
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, PHY, lane_ctrl), val);

		val = 0;
	}

	/* Come out of reset for TXPLL */
	saved_data = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl)) & 0xFFC00000;

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl),
	    (0x66 << PHY_LANE_CTRL_ADDR_POS)
	    | PHY_LANE_CTRL_CMD_READ
	    | PHY_LANE_CTRL_CMD_START
	    | PHY_LANE_CTRL_RST
	    | rext_sel
	    | val );

	while (((val = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl))) &
	    PHY_LANE_CTRL_CMD_PENDING));

	val &= 0xFF;
	/* set bit[4] to 0 */
	val &= ~(1 << 4);
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl),
	    (0x66 << PHY_LANE_CTRL_ADDR_POS)
	    | PHY_LANE_CTRL_CMD_WRITE
	    | PHY_LANE_CTRL_CMD_START
	    | (0x0 << 19) /* (0x4 << 19) */
	    | rext_sel
	    | saved_data
	    | val );

	/* re-do */
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl),
	    (0x66 << PHY_LANE_CTRL_ADDR_POS)
	    | PHY_LANE_CTRL_CMD_WRITE
	    | PHY_LANE_CTRL_CMD_START
	    | (0x0 << 19) /* (0x4 << 19) */
	    | rext_sel
	    | saved_data
	    | val );

	while (!((val = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, PHY, (lane_ctrl - PHY_LANE_0_CTRL)))) &
	    PHY_LANE_STAT_PCR));

	/* Clear the Power Down bit */
	val = nlm_read_nae_reg(nae_base, NAE_REG(block, PHY, lane_ctrl));
	val &= ~((1 << 29) | (0x7ffff));
	nlm_write_nae_reg(nae_base, NAE_REG(block, PHY, lane_ctrl),
	    (rext_sel | val));
}

void
xlp_nae_lane_reset_txpll(uint64_t nae_base, int block, int lane_ctrl,
    int mode)
{
	uint32_t val = 0;
	int rext_sel = 0;

	if (lane_ctrl != 4)
		rext_sel = (1 << 23);

	val = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl));

	/* set comma bypass for XAUI */
	if (mode != PHYMODE_SGMII)
		val |= PHY_LANE_CTRL_BPC_XAUI;
	val |= 0x100000;
	val |= (mode << PHY_LANE_CTRL_PHYMODE_POS);
	val &= ~(0x20000);
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl), val);

	val = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl));
	val |= 0x40000000;
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl), val);

	/* clear the power down bit */
	val = nlm_read_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl));
	val &= ~( (1 << 29) | (0x7ffff));
	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, PHY, lane_ctrl), rext_sel | val);
}

void
xlp_nae_config_lane_gmac(uint64_t nae_base, int cplx_mask)
{
	int block, lane_ctrl;
	int cplx_lane_enable;
	int lane_enable = 0;

	cplx_lane_enable = LM_SGMII |
	    (LM_SGMII << 4) |
	    (LM_SGMII << 8) |
	    (LM_SGMII << 12);

	/*  Lane mode progamming */
	block = 7;

	/* Complexes 0, 1 */
	if (cplx_mask & 0x1)
		lane_enable |= cplx_lane_enable;

	if (cplx_mask & 0x2)
		lane_enable |= (cplx_lane_enable << 16);

	if (lane_enable) {
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_0_1),
		    lane_enable);
		lane_enable = 0;
	}
	/* Complexes 2 3 */
	if (cplx_mask & 0x4)
		lane_enable |= cplx_lane_enable;

	if (cplx_mask & 0x8)
		lane_enable |= (cplx_lane_enable << 16);

	nlm_write_nae_reg(nae_base,
	    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_2_3),
	    lane_enable);

	/* complex 4 */
	/* XXXJC : fix duplicate code */
	if (cplx_mask & 0x10) {
		nlm_write_nae_reg(nae_base,
		    NAE_REG(block, LANE_CFG, LANE_CFG_CPLX_4),
		    ((LM_SGMII << 4) | LM_SGMII));
		for (lane_ctrl = PHY_LANE_0_CTRL;
		    lane_ctrl <= PHY_LANE_1_CTRL; lane_ctrl++) {
			if (!nlm_is_xlp8xx_ax())
				xlp_nae_lane_reset_txpll(nae_base,
				    4, lane_ctrl, PHYMODE_SGMII);
			else
				xlp_ax_nae_lane_reset_txpll(nae_base, 4,
				    lane_ctrl, PHYMODE_SGMII);
		}
	}

	for (block = 0; block < 4; block++) {
		if ((cplx_mask & (1 << block)) == 0)
			continue;

		for (lane_ctrl = PHY_LANE_0_CTRL;
		    lane_ctrl <= PHY_LANE_3_CTRL; lane_ctrl++) {
			if (!nlm_is_xlp8xx_ax())
				xlp_nae_lane_reset_txpll(nae_base,
				    block, lane_ctrl, PHYMODE_SGMII);
			else
				xlp_ax_nae_lane_reset_txpll(nae_base, block,
				    lane_ctrl, PHYMODE_SGMII);
		}
	}
}

void
config_egress_fifo_carvings(uint64_t nae_base, int hwport, int start_ctxt,
    int num_ctxts, int max_ctxts, struct nae_port_config *cfg)
{
	static uint32_t cur_start[6] = {0, 0, 0, 0, 0, 0};
	uint32_t data = 0;
	uint32_t start = 0, size, offset;
	int i, limit;

	limit = start_ctxt + num_ctxts;
	/* Stage 2 FIFO */
	start = cur_start[0];
	for (i = start_ctxt; i < limit; i++) {
		size = cfg[hwport].stg2_fifo_size / max_ctxts;
		if (size)
			offset = size - 1;
		else
			offset = size;
		if (offset > cfg[hwport].max_stg2_offset)
			offset = cfg[hwport].max_stg2_offset;
		data = offset << 23  |
		    start << 11 |
		    i << 1      |
		    1;
		nlm_write_nae_reg(nae_base, NAE_STG2_PMEM_PROG, data);
		start += size;
	}
	cur_start[0] = start;

	/* EH FIFO */
	start  = cur_start[1];
	for (i = start_ctxt; i < limit; i++) {
		size = cfg[hwport].eh_fifo_size / max_ctxts;
		if (size)
			offset = size - 1;
		else
			offset = size ;
		if (offset > cfg[hwport].max_eh_offset)
		    offset = cfg[hwport].max_eh_offset;
		data = offset << 23  |
		    start << 11 |
		    i << 1      |
		    1;
		nlm_write_nae_reg(nae_base, NAE_EH_PMEM_PROG, data);
		start += size;
	}
	cur_start[1] = start;

	/* FROUT FIFO */
	start  = cur_start[2];
	for (i = start_ctxt; i < limit; i++) {
		size = cfg[hwport].frout_fifo_size / max_ctxts;
		if (size)
			offset = size - 1;
		else
			offset = size ;
		if (offset > cfg[hwport].max_frout_offset)
			offset = cfg[hwport].max_frout_offset;
		data = offset << 23  |
		    start << 11 |
		    i << 1      |
		    1;
		nlm_write_nae_reg(nae_base, NAE_FREE_PMEM_PROG, data);
		start += size;
	}
	cur_start[2] = start;

	/* MS FIFO */
	start = cur_start[3];
	for (i = start_ctxt; i < limit; i++) {
		size = cfg[hwport].ms_fifo_size / max_ctxts;
		if (size)
			offset = size - 1;
		else
			offset = size ;
		if (offset > cfg[hwport].max_ms_offset)
			offset = cfg[hwport].max_ms_offset;
		data = offset << 22  |	/* FIXME in PRM */
		    start << 11 |
		    i << 1      |
		    1;
		nlm_write_nae_reg(nae_base, NAE_STR_PMEM_CMD, data);
		start += size;
	}
	cur_start[3] = start;

	/* PKT FIFO */
	start  = cur_start[4];
	for (i = start_ctxt; i < limit; i++) {
		size = cfg[hwport].pkt_fifo_size / max_ctxts;
		if (size)
			offset = size - 1;
		else
			offset = size ;
		if (offset > cfg[hwport].max_pmem_offset)
			offset = cfg[hwport].max_pmem_offset;
		nlm_write_nae_reg(nae_base, NAE_TX_PKT_PMEM_CMD1, offset);

		data = start << 11	|
		    i << 1		|
		    1;
		nlm_write_nae_reg(nae_base, NAE_TX_PKT_PMEM_CMD0, data);
		start += size;
	}
	cur_start[4] = start;

	/* PKT LEN FIFO */
	start  = cur_start[5];
	for (i = start_ctxt; i < limit; i++) {
		size = cfg[hwport].pktlen_fifo_size / max_ctxts;
		if (size)
			offset = size - 1;
		else
			offset = size ;
		data = offset  << 22	|
		    start << 11		|
		    i << 1		|
		    1;
		nlm_write_nae_reg(nae_base, NAE_TX_PKTLEN_PMEM_CMD, data);
		start += size;
	}
	cur_start[5] = start;
}

void
config_egress_fifo_credits(uint64_t nae_base, int hwport, int start_ctxt,
    int num_ctxts, int max_ctxts, struct nae_port_config *cfg)
{
	uint32_t data, credit, max_credit;
	int i, limit;

	limit = start_ctxt + num_ctxts;
	/* Stage1 -> Stage2 */
	max_credit = cfg[hwport].max_stg2_offset + 1;
	for (i = start_ctxt; i < limit; i++) {
		credit = cfg[hwport].stg1_2_credit / max_ctxts;
		if (credit > max_credit)
		    credit = max_credit;
		data = credit << 16	|
		    i << 4		|
		    1;
		nlm_write_nae_reg(nae_base, NAE_STG1_STG2CRDT_CMD, data);
	}

	/* Stage2 -> EH */
	max_credit = cfg[hwport].max_eh_offset + 1;
	for (i = start_ctxt; i < limit; i++) {
		credit = cfg[hwport].stg2_eh_credit / max_ctxts;
		if (credit > max_credit)
			credit = max_credit;
		data = credit << 16	|
		    i << 4		|
		    1;
		nlm_write_nae_reg(nae_base, NAE_STG2_EHCRDT_CMD, data);
	}

	/* Stage2 -> Frout */
	max_credit = cfg[hwport].max_frout_offset + 1;
	for (i = start_ctxt; i < limit; i++) {
		credit = cfg[hwport].stg2_frout_credit / max_ctxts;
		if (credit > max_credit)
			credit = max_credit;
		data = credit << 16	|
		    i << 4		|
		    1;
		nlm_write_nae_reg(nae_base, NAE_EH_FREECRDT_CMD, data);
	}

	/* Stage2 -> MS */
	max_credit = cfg[hwport].max_ms_offset + 1;
	for (i = start_ctxt; i < limit; i++) {
		credit = cfg[hwport].stg2_ms_credit / max_ctxts;
		if (credit > max_credit)
			credit = max_credit;
		data = credit << 16	|
		    i << 4		|
		    1;
		nlm_write_nae_reg(nae_base, NAE_STG2_STRCRDT_CMD, data);
	}
}

void
nlm_config_freein_fifo_uniq_cfg(uint64_t nae_base, int port,
    int nblock_free_desc)
{
	uint32_t val;
	int size_in_clines;

	size_in_clines = (nblock_free_desc / NAE_CACHELINE_SIZE);
	val = (size_in_clines << 8) | (port & 0x1f);
	nlm_write_nae_reg(nae_base, NAE_FREEIN_FIFO_UNIQ_SZ_CFG, val);
}

/* XXXJC: redundant, see ucore_spray_config() */
void
nlm_config_ucore_iface_mask_cfg(uint64_t nae_base, int port,
    int nblock_ucore_mask)
{
	uint32_t val;

	val = ( 0x1U << 31) | ((nblock_ucore_mask & 0xffff) << 8) |
	    (port & 0x1f);
	nlm_write_nae_reg(nae_base, NAE_UCORE_IFACEMASK_CFG, val);
}

int
nlm_nae_init_netior(uint64_t nae_base, int nblocks)
{
	uint32_t ctrl1, ctrl2, ctrl3;

	if (nblocks == 5)
		ctrl3 = 0x07 << 18;
	else
		ctrl3 = 0;

	switch (nblocks) {
	case 2:
		ctrl1 = 0xff;
		ctrl2 = 0x0707;
		break;
	case 4:
	case 5:
		ctrl1 = 0xfffff;
		ctrl2 = 0x07070707;
		break;
	default:
		printf("WARNING: unsupported blocks %d\n", nblocks);
		return (-1);
	}

	nlm_write_nae_reg(nae_base, NAE_LANE_CFG_SOFTRESET, 0);
	nlm_write_nae_reg(nae_base, NAE_NETIOR_MISC_CTRL3, ctrl3);
	nlm_write_nae_reg(nae_base, NAE_NETIOR_MISC_CTRL2, ctrl2);
	nlm_write_nae_reg(nae_base, NAE_NETIOR_MISC_CTRL1, ctrl1);
	nlm_write_nae_reg(nae_base, NAE_NETIOR_MISC_CTRL1, 0x0);
	return (0);
}

void
nlm_nae_init_ingress(uint64_t nae_base, uint32_t desc_size)
{
	uint32_t rx_cfg;
	uint32_t parser_threshold = 384;

	rx_cfg = nlm_read_nae_reg(nae_base, NAE_RX_CONFIG);
	rx_cfg &= ~(0x3 << 1);		/* reset max message size */
	rx_cfg &= ~(0xff << 4);		/* clear freein desc cluster size */
	rx_cfg &= ~(0x3f << 24);	/* reset rx status mask */ /*XXX: why not 7f */

	rx_cfg |= 1;			/* rx enable */
	rx_cfg |= (0x0 << 1);		/* max message size */
	rx_cfg |= (0x43 & 0x7f) << 24;	/* rx status mask */
	rx_cfg |= ((desc_size / 64) & 0xff) << 4; /* freein desc cluster size */
	nlm_write_nae_reg(nae_base, NAE_RX_CONFIG, rx_cfg);
	nlm_write_nae_reg(nae_base, NAE_PARSER_CONFIG,
	    (parser_threshold & 0x3ff) |
	    (((parser_threshold / desc_size) + 1) & 0xff) << 12 |
	    (((parser_threshold / 64) % desc_size) & 0xff) << 20);

	/*nlm_write_nae_reg(nae_base, NAE_RX_FREE_FIFO_THRESH, 33);*/
}

void
nlm_nae_init_egress(uint64_t nae_base)
{
	uint32_t tx_cfg;

	tx_cfg = nlm_read_nae_reg(nae_base, NAE_TX_CONFIG);
	if (!nlm_is_xlp8xx_ax()) {
		nlm_write_nae_reg(nae_base, NAE_TX_CONFIG,
		    tx_cfg	|
		    0x1		|	/* tx enable */
		    0x2		|	/* tx ace */
		    0x4		|	/* tx compatible */
		    (1 << 3));
	} else {
		nlm_write_nae_reg(nae_base, NAE_TX_CONFIG,
		    tx_cfg	|
		    0x1		|	/* tx enable */
		    0x2);		/* tx ace */
	}
}

uint32_t
ucore_spray_config(uint32_t interface, uint32_t ucore_mask, int cmd)
{
	return ((cmd & 0x1) << 31) | ((ucore_mask & 0xffff) << 8) |
	    (interface & 0x1f);
}

void
nlm_nae_init_ucore(uint64_t nae_base, int if_num, u_int ucore_mask)
{
	uint32_t ucfg;

	ucfg = ucore_spray_config(if_num, ucore_mask, 1); /* 1 : write */
	nlm_write_nae_reg(nae_base, NAE_UCORE_IFACEMASK_CFG, ucfg);
}

uint64_t
nae_tx_desc(u_int type, u_int rdex, u_int fbid, u_int len, uint64_t addr)
{
	return ((uint64_t)type  << 62) |
		((uint64_t)rdex << 61) |
		((uint64_t)fbid << 54) |
		((uint64_t)len  << 40) | addr;
}

void
nlm_setup_l2type(uint64_t nae_base, int hwport, uint32_t l2extlen,
    uint32_t l2extoff, uint32_t extra_hdrsize, uint32_t proto_offset,
    uint32_t fixed_hdroff, uint32_t l2proto)
{
	uint32_t val;

	val = ((l2extlen & 0x3f) << 26)		|
	    ((l2extoff & 0x3f) << 20)		|
	    ((extra_hdrsize & 0x3f) << 14)	|
	    ((proto_offset & 0x3f) << 8)	|
	    ((fixed_hdroff & 0x3f) << 2)	|
	    (l2proto & 0x3);
	nlm_write_nae_reg(nae_base, (NAE_L2_TYPE_PORT0 + hwport), val);
}

void
nlm_setup_l3ctable_mask(uint64_t nae_base, int hwport, uint32_t ptmask,
    uint32_t l3portmask)
{
	uint32_t val;

	val = ((ptmask & 0x1) << 6)	|
	    ((l3portmask & 0x1) << 5)	|
	    (hwport & 0x1f);
	nlm_write_nae_reg(nae_base, NAE_L3_CTABLE_MASK0, val);
}

void
nlm_setup_l3ctable_even(uint64_t nae_base, int entry, uint32_t l3hdroff,
    uint32_t ipcsum_en, uint32_t l4protooff,
    uint32_t l2proto, uint32_t eth_type)
{
	uint32_t val;

	val = ((l3hdroff & 0x3f) << 26)	|
	    ((l4protooff & 0x3f) << 20)	|
	    ((ipcsum_en & 0x1) << 18)	|
	    ((l2proto & 0x3) << 16)	|
	    (eth_type & 0xffff);
	nlm_write_nae_reg(nae_base, (NAE_L3CTABLE0 + (entry * 2)), val);
}

void
nlm_setup_l3ctable_odd(uint64_t nae_base, int entry, uint32_t l3off0,
    uint32_t l3len0, uint32_t l3off1, uint32_t l3len1,
    uint32_t l3off2, uint32_t l3len2)
{
	uint32_t val;

	val = ((l3off0 & 0x3f) << 26)	|
	    ((l3len0 & 0x1f) << 21)	|
	    ((l3off1 & 0x3f) << 15)	|
	    ((l3len1 & 0x1f) << 10)	|
	    ((l3off2 & 0x3f) << 4)	|
	    (l3len2 & 0xf);
	nlm_write_nae_reg(nae_base, (NAE_L3CTABLE0 + ((entry * 2) + 1)), val);
}

void
nlm_setup_l4ctable_even(uint64_t nae_base, int entry, uint32_t im,
    uint32_t l3cm, uint32_t l4pm, uint32_t port,
    uint32_t l3camaddr, uint32_t l4proto)
{
	uint32_t val;

	val = ((im & 0x1) << 19)	|
	    ((l3cm & 0x1) << 18)	|
	    ((l4pm & 0x1) << 17)	|
	    ((port & 0x1f) << 12)	|
	    ((l3camaddr & 0xf) << 8)	|
	    (l4proto & 0xff);
	nlm_write_nae_reg(nae_base, (NAE_L4CTABLE0 + (entry * 2)), val);
}

void
nlm_setup_l4ctable_odd(uint64_t nae_base, int entry, uint32_t l4off0,
    uint32_t l4len0, uint32_t l4off1, uint32_t l4len1)
{
	uint32_t val;

	val = ((l4off0 & 0x3f) << 21)	|
	    ((l4len0 & 0xf) << 17)	|
	    ((l4off1 & 0x3f) << 11)	|
	    (l4len1 & 0xf);
	nlm_write_nae_reg(nae_base, (NAE_L4CTABLE0 + ((entry * 2) + 1)), val);
}

void
nlm_enable_hardware_parser(uint64_t nae_base)
{
	uint32_t val;

	val = nlm_read_nae_reg(nae_base, NAE_RX_CONFIG);
	val |= (1 << 12); /* hardware parser enable */
	nlm_write_nae_reg(nae_base, NAE_RX_CONFIG, val);

	/***********************************************
	 * program L3 CAM table
	 ***********************************************/

	/*
	 *  entry-0 is ipv4 MPLS type 1 label
	 */
	 /* l3hdroff = 4 bytes, ether_type = 0x8847 for MPLS_type1 */
	nlm_setup_l3ctable_even(nae_base, 0, 4, 1, 9, 1, 0x8847);
	/* l3off0 (8 bytes) -> l3len0 (1 byte) := ip proto
	 * l3off1 (12 bytes) -> l3len1 (4 bytes) := src ip
	 * l3off2 (16 bytes) -> l3len2 (4 bytes) := dst ip
	 */
	nlm_setup_l3ctable_odd(nae_base, 0, 9, 1, 12, 4, 16, 4);

	/*
	 * entry-1 is for ethernet IPv4 packets
	 */
	nlm_setup_l3ctable_even(nae_base, 1, 0, 1, 9, 1, 0x0800);
	/* l3off0 (8 bytes) -> l3len0 (1 byte) := ip proto
	 * l3off1 (12 bytes) -> l3len1 (4 bytes) := src ip
	 * l3off2 (16 bytes) -> l3len2 (4 bytes) := dst ip
	 */
	nlm_setup_l3ctable_odd(nae_base, 1, 9, 1, 12, 4, 16, 4);

	/*
	 * entry-2 is for ethernet IPv6 packets
	 */
	nlm_setup_l3ctable_even(nae_base, 2, 0, 1, 6, 1, 0x86dd);
	/* l3off0 (6 bytes) -> l3len0 (1 byte) := next header (ip proto)
	 * l3off1 (8 bytes) -> l3len1 (16 bytes) := src ip
	 * l3off2 (24 bytes) -> l3len2 (16 bytes) := dst ip
	 */
	nlm_setup_l3ctable_odd(nae_base, 2, 6, 1, 8, 16, 24, 16);

	/*
	 * entry-3 is for ethernet ARP packets
	 */
	nlm_setup_l3ctable_even(nae_base, 3, 0, 0, 9, 1, 0x0806);
	/* extract 30 bytes from packet start */
	nlm_setup_l3ctable_odd(nae_base, 3, 0, 30, 0, 0, 0, 0);

	/*
	 * entry-4 is for ethernet FCoE packets
	 */
	nlm_setup_l3ctable_even(nae_base, 4, 0, 0, 9, 1, 0x8906);
	/* FCoE packet consists of 4 byte start-of-frame,
	 * and 24 bytes of frame header, followed by
	 * 64 bytes of optional-header (ESP, network..),
	 * 2048 bytes of payload, 36 bytes of optional
	 * "fill bytes" or ESP trailer, 4 bytes of CRC,
	 * and 4 bytes of end-of-frame
	 * We extract the first 4 + 24 = 28 bytes
	 */
	nlm_setup_l3ctable_odd(nae_base, 4, 0, 28, 0, 0, 0, 0);

	/*
	 * entry-5 is for vlan tagged frames (0x8100)
	 */
	nlm_setup_l3ctable_even(nae_base, 5, 0, 0, 9, 1, 0x8100);
	/* we extract 31 bytes from the payload */
	nlm_setup_l3ctable_odd(nae_base, 5, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-6 is for ieee 802.1ad provider bridging
	 * tagged frames (0x88a8)
	 */
	nlm_setup_l3ctable_even(nae_base, 6, 0, 0, 9, 1, 0x88a8);
	/* we extract 31 bytes from the payload */
	nlm_setup_l3ctable_odd(nae_base, 6, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-7 is for Cisco's Q-in-Q tagged frames (0x9100)
	 */
	nlm_setup_l3ctable_even(nae_base, 7, 0, 0, 9, 1, 0x9100);
	/* we extract 31 bytes from the payload */
	nlm_setup_l3ctable_odd(nae_base, 7, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-8 is for Ethernet Jumbo frames (0x8870)
	 */
	nlm_setup_l3ctable_even(nae_base, 8, 0, 0, 9, 1, 0x8870);
	/* we extract 31 bytes from the payload */
	nlm_setup_l3ctable_odd(nae_base, 8, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-9 is for MPLS Multicast frames (0x8848)
	 */
	nlm_setup_l3ctable_even(nae_base, 9, 0, 0, 9, 1, 0x8848);
	/* we extract 31 bytes from the payload */
	nlm_setup_l3ctable_odd(nae_base, 9, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-10 is for IEEE 802.1ae MAC Security frames (0x88e5)
	 */
	nlm_setup_l3ctable_even(nae_base, 10, 0, 0, 9, 1, 0x88e5);
	/* we extract 31 bytes from the payload */
	nlm_setup_l3ctable_odd(nae_base, 10, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-11 is for PTP frames (0x88f7)
	 */
	nlm_setup_l3ctable_even(nae_base, 11, 0, 0, 9, 1, 0x88f7);
	/* PTP messages can be sent as UDP messages over
	 * IPv4 or IPv6; and as a raw ethernet message
	 * with ethertype 0x88f7. The message contents
	 * are the same for UDP or ethernet based encapsulations
	 * The header is 34 bytes long, and we extract
	 * it all out.
	 */
	nlm_setup_l3ctable_odd(nae_base, 11, 0, 31, 31, 2, 0, 0);

	/*
	 * entry-12 is for ethernet Link Control Protocol (LCP)
	 * used with PPPoE
	 */
	nlm_setup_l3ctable_even(nae_base, 12, 0, 0, 9, 1, 0xc021);
	/* LCP packet consists of 1 byte of code, 1 byte of
	 * identifier and two bytes of length followed by
	 * data (upto length bytes).
	 * We extract 4 bytes from start of packet
	 */
	nlm_setup_l3ctable_odd(nae_base, 12, 0, 4, 0, 0, 0, 0);

	/*
	 * entry-13 is for ethernet Link Quality Report (0xc025)
	 * used with PPPoE
	 */
	nlm_setup_l3ctable_even(nae_base, 13, 0, 0, 9, 1, 0xc025);
	/* We extract 31 bytes from packet start */
	nlm_setup_l3ctable_odd(nae_base, 13, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-14 is for PPPoE Session (0x8864)
	 */
	nlm_setup_l3ctable_even(nae_base, 14, 0, 0, 9, 1, 0x8864);
	/* We extract 31 bytes from packet start */
	nlm_setup_l3ctable_odd(nae_base, 14, 0, 31, 0, 0, 0, 0);

	/*
	 * entry-15 - default entry
	 */
	nlm_setup_l3ctable_even(nae_base, 15, 0, 0, 0, 0, 0x0000);
	/* We extract 31 bytes from packet start */
	nlm_setup_l3ctable_odd(nae_base, 15, 0, 31, 0, 0, 0, 0);

	/***********************************************
	 * program L4 CAM table
	 ***********************************************/

	/*
	 * entry-0 - tcp packets (0x6)
	 */
	nlm_setup_l4ctable_even(nae_base, 0, 0, 0, 1, 0, 0, 0x6);
	/* tcp header is 20 bytes without tcp options
	 * We extract 20 bytes from tcp start */
	nlm_setup_l4ctable_odd(nae_base, 0, 0, 15, 15, 5);

	/*
	 * entry-1 - udp packets (0x11)
	 */
	nlm_setup_l4ctable_even(nae_base, 1, 0, 0, 1, 0, 0, 0x11);
	/* udp header is 8 bytes in size.
	 * We extract 8 bytes from udp start */
	nlm_setup_l4ctable_odd(nae_base, 1, 0, 8, 0, 0);

	/*
	 * entry-2 - sctp packets (0x84)
	 */
	nlm_setup_l4ctable_even(nae_base, 2, 0, 0, 1, 0, 0, 0x84);
	/* sctp packets have a 12 byte generic header
	 * and various chunks.
	 * We extract 12 bytes from sctp start */
	nlm_setup_l4ctable_odd(nae_base, 2, 0, 12, 0, 0);

	/*
	 * entry-3 - RDP packets (0x1b)
	 */
	nlm_setup_l4ctable_even(nae_base, 3, 0, 0, 1, 0, 0, 0x1b);
	/* RDP packets have 18 bytes of generic header
	 * before variable header starts.
	 * We extract 18 bytes from rdp start */
	nlm_setup_l4ctable_odd(nae_base, 3, 0, 15, 15, 3);

	/*
	 * entry-4 - DCCP packets (0x21)
	 */
	nlm_setup_l4ctable_even(nae_base, 4, 0, 0, 1, 0, 0, 0x21);
	/* DCCP has two types of generic headers of
	 * sizes 16 bytes and 12 bytes if X = 1.
	 * We extract 16 bytes from dccp start */
	nlm_setup_l4ctable_odd(nae_base, 4, 0, 15, 15, 1);

	/*
	 * entry-5 - ipv6 encapsulated in ipv4 packets (0x29)
	 */
	nlm_setup_l4ctable_even(nae_base, 5, 0, 0, 1, 0, 0, 0x29);
	/* ipv4 header is 20 bytes excluding IP options.
	 * We extract 20 bytes from IPv4 start */
	nlm_setup_l4ctable_odd(nae_base, 5, 0, 15, 15, 5);

	/*
	 * entry-6 - ip in ip encapsulation packets (0x04)
	 */
	nlm_setup_l4ctable_even(nae_base, 6, 0, 0, 1, 0, 0, 0x04);
	/* ipv4 header is 20 bytes excluding IP options.
	 * We extract 20 bytes from ipv4 start */
	nlm_setup_l4ctable_odd(nae_base, 6, 0, 15, 15, 5);

	/*
	 * entry-7 - default entry (0x0)
	 */
	nlm_setup_l4ctable_even(nae_base, 7, 0, 0, 1, 0, 0, 0x0);
	/* We extract 20 bytes from packet start */
	nlm_setup_l4ctable_odd(nae_base, 7, 0, 15, 15, 5);
}

void
nlm_enable_hardware_parser_per_port(uint64_t nae_base, int block, int port)
{
	int hwport = (block * 4) + (port & 0x3);

	/* program L2 and L3 header extraction for each port */
	/* enable ethernet L2 mode on port */
	nlm_setup_l2type(nae_base, hwport, 0, 0, 0, 0, 0, 1);

	/* l2proto and ethtype included in l3cam */
	nlm_setup_l3ctable_mask(nae_base, hwport, 1, 0);
}

void
nlm_prepad_enable(uint64_t nae_base, int size)
{
	uint32_t val;

	val = nlm_read_nae_reg(nae_base, NAE_RX_CONFIG);
	val |= (1 << 13); /* prepad enable */
	val |= ((size & 0x3) << 22); /* prepad size */
	nlm_write_nae_reg(nae_base, NAE_RX_CONFIG, val);
}

void
nlm_setup_1588_timer(uint64_t nae_base, struct nae_port_config *cfg)
{
	uint32_t hi, lo, val;

	hi = cfg[0].ieee1588_userval >> 32;
	lo = cfg[0].ieee1588_userval & 0xffffffff;
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_USER_VALUE_HI, hi);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_USER_VALUE_LO, lo);

	hi = cfg[0].ieee1588_ptpoff >> 32;
	lo = cfg[0].ieee1588_ptpoff & 0xffffffff;
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_OFFSET_HI, hi);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_OFFSET_LO, lo);

	hi = cfg[0].ieee1588_tmr1 >> 32;
	lo = cfg[0].ieee1588_tmr1 & 0xffffffff;
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_TMR1_HI, hi);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_TMR1_LO, lo);

	hi = cfg[0].ieee1588_tmr2 >> 32;
	lo = cfg[0].ieee1588_tmr2 & 0xffffffff;
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_TMR2_HI, hi);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_TMR2_LO, lo);

	hi = cfg[0].ieee1588_tmr3 >> 32;
	lo = cfg[0].ieee1588_tmr3 & 0xffffffff;
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_TMR3_HI, hi);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_TMR3_LO, lo);

	nlm_write_nae_reg(nae_base, NAE_1588_PTP_INC_INTG,
	    cfg[0].ieee1588_inc_intg);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_INC_NUM,
	    cfg[0].ieee1588_inc_num);
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_INC_DEN,
	    cfg[0].ieee1588_inc_den);

	val = nlm_read_nae_reg(nae_base, NAE_1588_PTP_CONTROL);
	/* set and clear freq_mul = 1 */
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_CONTROL, val | (0x1 << 1));
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_CONTROL, val);
	/* set and clear load_user_val = 1 */
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_CONTROL, val | (0x1 << 6));
	nlm_write_nae_reg(nae_base, NAE_1588_PTP_CONTROL, val);
}

void
nlm_mac_enable(uint64_t nae_base, int nblock, int port_type, int port)
{
	uint32_t mac_cfg1, xaui_cfg;
	uint32_t netwk_inf;
	int iface = port & 0x3;

	switch(port_type) {
	case SGMIIC:
		netwk_inf = nlm_read_nae_reg(nae_base,
		    SGMII_NET_IFACE_CTRL(nblock, iface));
		nlm_write_nae_reg(nae_base,
		    SGMII_NET_IFACE_CTRL(nblock, iface),
		    netwk_inf		|
		    (1 << 2));			/* enable tx */
		mac_cfg1 = nlm_read_nae_reg(nae_base,
		    SGMII_MAC_CONF1(nblock, iface));
		nlm_write_nae_reg(nae_base,
		    SGMII_MAC_CONF1(nblock, iface),
		    mac_cfg1		|
		    (1 << 2)		|	/* rx enable */
		    1);				/* tx enable */
		break;
	case XAUIC:
		xaui_cfg = nlm_read_nae_reg(nae_base,
		    XAUI_CONFIG1(nblock));
		nlm_write_nae_reg(nae_base,
		    XAUI_CONFIG1(nblock),
		    xaui_cfg		|
		    XAUI_CONFIG_TFEN	|
		    XAUI_CONFIG_RFEN);
		break;
	case ILC:
		break;
	}
}

void
nlm_mac_disable(uint64_t nae_base, int nblock, int port_type, int port)
{
	uint32_t mac_cfg1, xaui_cfg;
	uint32_t netwk_inf;
	int iface = port & 0x3;

	switch(port_type) {
	case SGMIIC:
		mac_cfg1 = nlm_read_nae_reg(nae_base,
		    SGMII_MAC_CONF1(nblock, iface));
		nlm_write_nae_reg(nae_base,
		    SGMII_MAC_CONF1(nblock, iface),
		    mac_cfg1		&
		    ~((1 << 2)		|	/* rx enable */
		    1));			/* tx enable */
		netwk_inf = nlm_read_nae_reg(nae_base,
		    SGMII_NET_IFACE_CTRL(nblock, iface));
		nlm_write_nae_reg(nae_base,
		    SGMII_NET_IFACE_CTRL(nblock, iface),
		    netwk_inf		&
		    ~(1 << 2));			/* enable tx */
		break;
	case XAUIC:
		xaui_cfg = nlm_read_nae_reg(nae_base,
		    XAUI_CONFIG1(nblock));
		nlm_write_nae_reg(nae_base,
		    XAUI_CONFIG1(nblock),
		    xaui_cfg		&
		    ~(XAUI_CONFIG_TFEN	|
		    XAUI_CONFIG_RFEN));
		break;
	case ILC:
		break;
	}
}

/*
 * Set IOR credits for the ports in ifmask to valmask
 */
static void
nlm_nae_set_ior_credit(uint64_t nae_base, uint32_t ifmask, uint32_t valmask)
{
	uint32_t tx_config, tx_ior_credit;

	tx_ior_credit = nlm_read_nae_reg(nae_base, NAE_TX_IORCRDT_INIT);
	tx_ior_credit &= ~ifmask;
	tx_ior_credit |= valmask;
	nlm_write_nae_reg(nae_base, NAE_TX_IORCRDT_INIT, tx_ior_credit);

	tx_config = nlm_read_nae_reg(nae_base, NAE_TX_CONFIG);
	/* need to toggle these bits for credits to be loaded */
	nlm_write_nae_reg(nae_base, NAE_TX_CONFIG,
	    tx_config | (TXINITIORCR(ifmask)));
	nlm_write_nae_reg(nae_base, NAE_TX_CONFIG,
	    tx_config & ~(TXINITIORCR(ifmask)));
}

int
nlm_nae_open_if(uint64_t nae_base, int nblock, int port_type,
    int port, uint32_t desc_size)
{
	uint32_t netwk_inf;
	uint32_t mac_cfg1, netior_ctrl3;
	int iface, iface_ctrl_reg, iface_ctrl3_reg, conf1_reg, conf2_reg;

	switch (port_type) {
	case XAUIC:
		netwk_inf = nlm_read_nae_reg(nae_base,
		    XAUI_NETIOR_XGMAC_CTRL1(nblock));
		netwk_inf |= (1 << NETIOR_XGMAC_STATS_CLR_POS);
		nlm_write_nae_reg(nae_base,
		    XAUI_NETIOR_XGMAC_CTRL1(nblock), netwk_inf);

		nlm_nae_set_ior_credit(nae_base, 0xf << port, 0xf << port);
		break;

	case ILC:
		nlm_nae_set_ior_credit(nae_base, 0xff << port, 0xff << port);
		break;

	case SGMIIC:
		nlm_nae_set_ior_credit(nae_base, 0x1 << port, 0);

		/*
		 * XXXJC: split this and merge to sgmii.c
		 * some of this is duplicated from there.
		 */
		/* init phy id to access internal PCS */
		iface = port & 0x3;
		iface_ctrl_reg = SGMII_NET_IFACE_CTRL(nblock, iface);
		conf1_reg = SGMII_MAC_CONF1(nblock, iface);
		conf2_reg = SGMII_MAC_CONF2(nblock, iface);

		netwk_inf = nlm_read_nae_reg(nae_base, iface_ctrl_reg);
		netwk_inf &= 0x7ffffff;
		netwk_inf |= (port << 27);
		nlm_write_nae_reg(nae_base, iface_ctrl_reg, netwk_inf);

		/* Sofreset sgmii port - set bit 11 to 0  */
		netwk_inf &= 0xfffff7ff;
		nlm_write_nae_reg(nae_base, iface_ctrl_reg, netwk_inf);

		/* Reset Gmac */
		mac_cfg1 = nlm_read_nae_reg(nae_base, conf1_reg);
		nlm_write_nae_reg(nae_base, conf1_reg,
		    mac_cfg1	|
		    (1U << 31)	|	/* soft reset */
		    (1 << 2)	|	/* rx enable */
		    (1));		/* tx enable */

		/* default to 1G */
		nlm_write_nae_reg(nae_base,
		    conf2_reg,
		    (0x7 << 12)	|	/* interface preamble length */
		    (0x2 << 8)	|	/* interface mode */
		    (0x1 << 2)	|	/* pad crc enable */
		    (0x1));		/* full duplex */

		/* clear gmac reset */
		mac_cfg1 = nlm_read_nae_reg(nae_base, conf1_reg);
		nlm_write_nae_reg(nae_base, conf1_reg, mac_cfg1 & ~(1U << 31));

		/* clear speed debug bit */
		iface_ctrl3_reg = SGMII_NET_IFACE_CTRL3(nblock, iface);
		netior_ctrl3 = nlm_read_nae_reg(nae_base, iface_ctrl3_reg);
		nlm_write_nae_reg(nae_base, iface_ctrl3_reg,
		    netior_ctrl3 & ~(1 << 6));

		/* disable TX, RX for now */
		mac_cfg1 = nlm_read_nae_reg(nae_base, conf1_reg);
		nlm_write_nae_reg(nae_base, conf1_reg, mac_cfg1 & ~(0x5));
		netwk_inf = nlm_read_nae_reg(nae_base, iface_ctrl_reg);
		nlm_write_nae_reg(nae_base, iface_ctrl_reg,
		    netwk_inf & ~(0x1 << 2));

		/* clear stats counters */
		netwk_inf = nlm_read_nae_reg(nae_base, iface_ctrl_reg);
		nlm_write_nae_reg(nae_base, iface_ctrl_reg,
		    netwk_inf | (1 << 15));

		/* enable stats counters */
		netwk_inf = nlm_read_nae_reg(nae_base, iface_ctrl_reg);
		nlm_write_nae_reg(nae_base, iface_ctrl_reg,
		    (netwk_inf & ~(1 << 15)) | (1 << 16));

		/* flow control? */
		mac_cfg1 = nlm_read_nae_reg(nae_base, conf1_reg);
		nlm_write_nae_reg(nae_base, conf1_reg,
		    mac_cfg1 | (0x3 << 4));
		break;
	}

	nlm_nae_init_ingress(nae_base, desc_size);
	nlm_nae_init_egress(nae_base);

	return (0);
}
