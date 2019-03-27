/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>

#include "common/common.h"
#include "common/t4_regs.h"
#include "cudbg.h"
#include "cudbg_lib_common.h"
#include "cudbg_entity.h"

int collect_wtp_data(struct cudbg_init *pdbg_init,
		     struct cudbg_buffer *dbg_buff,
		     struct cudbg_error *cudbg_err);
/*SGE_DEBUG Registers.*/
#define TP_MIB_SIZE	    0x5e

struct sge_debug_reg_data {
	/*indx0*/
	u32 reserved1:4;
	u32 reserved2:4;
	u32 debug_uP_SOP_cnt:4;
	u32 debug_uP_EOP_cnt:4;
	u32 debug_CIM_SOP1_cnt:4;
	u32 debug_CIM_EOP1_cnt:4;
	u32 debug_CIM_SOP0_cnt:4;
	u32 debug_CIM_EOP0_cnt:4;

	/*indx1*/
	u32 reserved3:32;

	/*indx2*/
	u32 debug_T_Rx_SOP1_cnt:4;
	u32 debug_T_Rx_EOP1_cnt:4;
	u32 debug_T_Rx_SOP0_cnt:4;
	u32 debug_T_Rx_EOP0_cnt:4;
	u32 debug_U_Rx_SOP1_cnt:4;
	u32 debug_U_Rx_EOP1_cnt:4;
	u32 debug_U_Rx_SOP0_cnt:4;
	u32 debug_U_Rx_EOP0_cnt:4;

	/*indx3*/
	u32 reserved4:32;

	/*indx4*/
	u32 debug_UD_Rx_SOP3_cnt:4;
	u32 debug_UD_Rx_EOP3_cnt:4;
	u32 debug_UD_Rx_SOP2_cnt:4;
	u32 debug_UD_Rx_EOP2_cnt:4;
	u32 debug_UD_Rx_SOP1_cnt:4;
	u32 debug_UD_Rx_EOP1_cnt:4;
	u32 debug_UD_Rx_SOP0_cnt:4;
	u32 debug_UD_Rx_EOP0_cnt:4;

	/*indx5*/
	u32 reserved5:32;

	/*indx6*/
	u32 debug_U_Tx_SOP3_cnt:4;
	u32 debug_U_Tx_EOP3_cnt:4;
	u32 debug_U_Tx_SOP2_cnt:4;
	u32 debug_U_Tx_EOP2_cnt:4;
	u32 debug_U_Tx_SOP1_cnt:4;
	u32 debug_U_Tx_EOP1_cnt:4;
	u32 debug_U_Tx_SOP0_cnt:4;
	u32 debug_U_Tx_EOP0_cnt:4;

	/*indx7*/
	u32 reserved6:32;

	/*indx8*/
	u32  debug_PC_Rsp_SOP1_cnt:4;
	u32  debug_PC_Rsp_EOP1_cnt:4;
	u32  debug_PC_Rsp_SOP0_cnt:4;
	u32  debug_PC_Rsp_EOP0_cnt:4;
	u32  debug_PC_Req_SOP1_cnt:4;
	u32  debug_PC_Req_EOP1_cnt:4;
	u32  debug_PC_Req_SOP0_cnt:4;
	u32  debug_PC_Req_EOP0_cnt:4;

	/*indx9*/
	u32 reserved7:32;

	/*indx10*/
	u32  debug_PD_Req_SOP3_cnt:4;
	u32  debug_PD_Req_EOP3_cnt:4;
	u32  debug_PD_Req_SOP2_cnt:4;
	u32  debug_PD_Req_EOP2_cnt:4;
	u32  debug_PD_Req_SOP1_cnt:4;
	u32  debug_PD_Req_EOP1_cnt:4;
	u32  debug_PD_Req_SOP0_cnt:4;
	u32  debug_PD_Req_EOP0_cnt:4;

	/*indx11*/
	u32 reserved8:32;

	/*indx12*/
	u32  debug_PD_Rsp_SOP3_cnt:4;
	u32  debug_PD_Rsp_EOP3_cnt:4;
	u32  debug_PD_Rsp_SOP2_cnt:4;
	u32  debug_PD_Rsp_EOP2_cnt:4;
	u32  debug_PD_Rsp_SOP1_cnt:4;
	u32  debug_PD_Rsp_EOP1_cnt:4;
	u32  debug_PD_Rsp_SOP0_cnt:4;
	u32  debug_PD_Rsp_EOP0_cnt:4;

	/*indx13*/
	u32 reserved9:32;

	/*indx14*/
	u32  debug_CPLSW_TP_Rx_SOP1_cnt:4;
	u32  debug_CPLSW_TP_Rx_EOP1_cnt:4;
	u32  debug_CPLSW_TP_Rx_SOP0_cnt:4;
	u32  debug_CPLSW_TP_Rx_EOP0_cnt:4;
	u32  debug_CPLSW_CIM_SOP1_cnt:4;
	u32  debug_CPLSW_CIM_EOP1_cnt:4;
	u32  debug_CPLSW_CIM_SOP0_cnt:4;
	u32  debug_CPLSW_CIM_EOP0_cnt:4;

	/*indx15*/
	u32 reserved10:32;

	/*indx16*/
	u32  debug_PD_Req_Rd3_cnt:4;
	u32  debug_PD_Req_Rd2_cnt:4;
	u32  debug_PD_Req_Rd1_cnt:4;
	u32  debug_PD_Req_Rd0_cnt:4;
	u32  debug_PD_Req_Int3_cnt:4;
	u32  debug_PD_Req_Int2_cnt:4;
	u32  debug_PD_Req_Int1_cnt:4;
	u32  debug_PD_Req_Int0_cnt:4;

};

struct tp_mib_type tp_mib[] = {
	{"tp_mib_mac_in_err_0", 0x0},
	{"tp_mib_mac_in_err_1", 0x1},
	{"tp_mib_mac_in_err_2", 0x2},
	{"tp_mib_mac_in_err_3", 0x3},
	{"tp_mib_hdr_in_err_0", 0x4},
	{"tp_mib_hdr_in_err_1", 0x5},
	{"tp_mib_hdr_in_err_2", 0x6},
	{"tp_mib_hdr_in_err_3", 0x7},
	{"tp_mib_tcp_in_err_0", 0x8},
	{"tp_mib_tcp_in_err_1", 0x9},
	{"tp_mib_tcp_in_err_2", 0xa},
	{"tp_mib_tcp_in_err_3", 0xb},
	{"tp_mib_tcp_out_rst", 0xc},
	{"tp_mib_tcp_in_seg_hi", 0x10},
	{"tp_mib_tcp_in_seg_lo", 0x11},
	{"tp_mib_tcp_out_seg_hi", 0x12},
	{"tp_mib_tcp_out_seg_lo", 0x13},
	{"tp_mib_tcp_rxt_seg_hi", 0x14},
	{"tp_mib_tcp_rxt_seg_lo", 0x15},
	{"tp_mib_tnl_cng_drop_0", 0x18},
	{"tp_mib_tnl_cng_drop_1", 0x19},
	{"tp_mib_tnl_cng_drop_2", 0x1a},
	{"tp_mib_tnl_cng_drop_3", 0x1b},
	{"tp_mib_ofd_chn_drop_0", 0x1c},
	{"tp_mib_ofd_chn_drop_1", 0x1d},
	{"tp_mib_ofd_chn_drop_2", 0x1e},
	{"tp_mib_ofd_chn_drop_3", 0x1f},
	{"tp_mib_tnl_out_pkt_0", 0x20},
	{"tp_mib_tnl_out_pkt_1", 0x21},
	{"tp_mib_tnl_out_pkt_2", 0x22},
	{"tp_mib_tnl_out_pkt_3", 0x23},
	{"tp_mib_tnl_in_pkt_0", 0x24},
	{"tp_mib_tnl_in_pkt_1", 0x25},
	{"tp_mib_tnl_in_pkt_2", 0x26},
	{"tp_mib_tnl_in_pkt_3", 0x27},
	{"tp_mib_tcp_v6in_err_0", 0x28},
	{"tp_mib_tcp_v6in_err_1", 0x29},
	{"tp_mib_tcp_v6in_err_2", 0x2a},
	{"tp_mib_tcp_v6in_err_3", 0x2b},
	{"tp_mib_tcp_v6out_rst", 0x2c},
	{"tp_mib_tcp_v6in_seg_hi", 0x30},
	{"tp_mib_tcp_v6in_seg_lo", 0x31},
	{"tp_mib_tcp_v6out_seg_hi", 0x32},
	{"tp_mib_tcp_v6out_seg_lo", 0x33},
	{"tp_mib_tcp_v6rxt_seg_hi", 0x34},
	{"tp_mib_tcp_v6rxt_seg_lo", 0x35},
	{"tp_mib_ofd_arp_drop", 0x36},
	{"tp_mib_ofd_dfr_drop", 0x37},
	{"tp_mib_cpl_in_req_0", 0x38},
	{"tp_mib_cpl_in_req_1", 0x39},
	{"tp_mib_cpl_in_req_2", 0x3a},
	{"tp_mib_cpl_in_req_3", 0x3b},
	{"tp_mib_cpl_out_rsp_0", 0x3c},
	{"tp_mib_cpl_out_rsp_1", 0x3d},
	{"tp_mib_cpl_out_rsp_2", 0x3e},
	{"tp_mib_cpl_out_rsp_3", 0x3f},
	{"tp_mib_tnl_lpbk_0", 0x40},
	{"tp_mib_tnl_lpbk_1", 0x41},
	{"tp_mib_tnl_lpbk_2", 0x42},
	{"tp_mib_tnl_lpbk_3", 0x43},
	{"tp_mib_tnl_drop_0", 0x44},
	{"tp_mib_tnl_drop_1", 0x45},
	{"tp_mib_tnl_drop_2", 0x46},
	{"tp_mib_tnl_drop_3", 0x47},
	{"tp_mib_fcoe_ddp_0", 0x48},
	{"tp_mib_fcoe_ddp_1", 0x49},
	{"tp_mib_fcoe_ddp_2", 0x4a},
	{"tp_mib_fcoe_ddp_3", 0x4b},
	{"tp_mib_fcoe_drop_0", 0x4c},
	{"tp_mib_fcoe_drop_1", 0x4d},
	{"tp_mib_fcoe_drop_2", 0x4e},
	{"tp_mib_fcoe_drop_3", 0x4f},
	{"tp_mib_fcoe_byte_0_hi", 0x50},
	{"tp_mib_fcoe_byte_0_lo", 0x51},
	{"tp_mib_fcoe_byte_1_hi", 0x52},
	{"tp_mib_fcoe_byte_1_lo", 0x53},
	{"tp_mib_fcoe_byte_2_hi", 0x54},
	{"tp_mib_fcoe_byte_2_lo", 0x55},
	{"tp_mib_fcoe_byte_3_hi", 0x56},
	{"tp_mib_fcoe_byte_3_lo", 0x57},
	{"tp_mib_ofd_vln_drop_0", 0x58},
	{"tp_mib_ofd_vln_drop_1", 0x59},
	{"tp_mib_ofd_vln_drop_2", 0x5a},
	{"tp_mib_ofd_vln_drop_3", 0x5b},
	{"tp_mib_usm_pkts", 0x5c},
	{"tp_mib_usm_drop", 0x5d},
	{"tp_mib_usm_bytes_hi", 0x5e},
	{"tp_mib_usm_bytes_lo", 0x5f},
	{"tp_mib_tid_del", 0x60},
	{"tp_mib_tid_inv", 0x61},
	{"tp_mib_tid_act", 0x62},
	{"tp_mib_tid_pas", 0x63},
	{"tp_mib_rqe_dfr_mod", 0x64},
	{"tp_mib_rqe_dfr_pkt", 0x65}
};

static u32 read_sge_debug_data(struct cudbg_init *pdbg_init, u32 *sge_dbg_reg)
{
	struct adapter *padap = pdbg_init->adap;
	u32 value;
	int i = 0;

	for (i = 0; i <= 15; i++) {
		t4_write_reg(padap, A_SGE_DEBUG_INDEX, (u32)i);
		value = t4_read_reg(padap, A_SGE_DEBUG_DATA_LOW);
		/*printf("LOW	 0x%08x\n", value);*/
		sge_dbg_reg[(i << 1) | 1] = HTONL_NIBBLE(value);
		value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH);
		/*printf("HIGH	 0x%08x\n", value);*/
		sge_dbg_reg[(i << 1)] = HTONL_NIBBLE(value);
	}
	return 0;
}

static u32 read_tp_mib_data(struct cudbg_init *pdbg_init,
			    struct tp_mib_data **ppTp_Mib)
{
	struct adapter *padap = pdbg_init->adap;
	u32 i = 0;

	for (i = 0; i < TP_MIB_SIZE; i++) {
		t4_tp_mib_read(padap, &tp_mib[i].value, 1,
				  (u32)tp_mib[i].addr, true);
	}
	*ppTp_Mib = (struct tp_mib_data *)&tp_mib[0];

	return 0;
}

static int t5_wtp_data(struct cudbg_init *pdbg_init,
		       struct cudbg_buffer *dbg_buff,
		       struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct sge_debug_reg_data *sge_dbg_reg = NULL;
	struct cudbg_buffer scratch_buff;
	struct tp_mib_data *ptp_mib = NULL;
	struct wtp_data *wtp;
	u32 Sge_Dbg[32] = {0};
	u32 value = 0;
	u32 i = 0;
	u32 drop = 0;
	u32 err = 0;
	u32 offset;
	int rc = 0;

	rc = get_scratch_buff(dbg_buff, sizeof(struct wtp_data), &scratch_buff);

	if (rc)
		goto err;

	offset = scratch_buff.offset;
	wtp = (struct wtp_data *)((char *)scratch_buff.data + offset);

	read_sge_debug_data(pdbg_init, Sge_Dbg);
	read_tp_mib_data(pdbg_init, &ptp_mib);

	sge_dbg_reg = (struct sge_debug_reg_data *) &Sge_Dbg[0];

	/*#######################################################################*/
	/*# TX PATH, starting from pcie*/
	/*#######################################################################*/

	/* Get Reqests of commmands from SGE to PCIE*/

	wtp->sge_pcie_cmd_req.sop[0] =	sge_dbg_reg->debug_PC_Req_SOP0_cnt;
	wtp->sge_pcie_cmd_req.sop[1] =	sge_dbg_reg->debug_PC_Req_SOP1_cnt;

	wtp->sge_pcie_cmd_req.eop[0] =	sge_dbg_reg->debug_PC_Req_EOP0_cnt;
	wtp->sge_pcie_cmd_req.eop[1] =	sge_dbg_reg->debug_PC_Req_EOP1_cnt;

	/* Get Reqests of commmands from PCIE to core*/
	value = t4_read_reg(padap, A_PCIE_CMDR_REQ_CNT);

	wtp->pcie_core_cmd_req.sop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_cmd_req.sop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	/* there is no EOP for this, so we fake it.*/
	wtp->pcie_core_cmd_req.eop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_cmd_req.eop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/

	/* Get DMA stats*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, A_PCIE_T5_DMA_STAT3 + (i * 0x10));
		wtp->pcie_t5_dma_stat3.sop[i] = value & 0xFF;
		wtp->pcie_t5_dma_stat3.eop[i] = ((value >> 16) & 0xFF);
	}

	/* Get SGE debug data high index 6*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_6);
	wtp->sge_debug_data_high_index_6.sop[0] = ((value >> 4) & 0x0F);
	wtp->sge_debug_data_high_index_6.eop[0] = ((value >> 0) & 0x0F);
	wtp->sge_debug_data_high_index_6.sop[1] = ((value >> 12) & 0x0F);
	wtp->sge_debug_data_high_index_6.eop[1] = ((value >> 8) & 0x0F);
	wtp->sge_debug_data_high_index_6.sop[2] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_index_6.eop[2] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_index_6.sop[3] = ((value >> 28) & 0x0F);
	wtp->sge_debug_data_high_index_6.eop[3] = ((value >> 24) & 0x0F);

	/* Get SGE debug data high index 3*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_3);
	wtp->sge_debug_data_high_index_3.sop[0] = ((value >> 4) & 0x0F);
	wtp->sge_debug_data_high_index_3.eop[0] = ((value >> 0) & 0x0F);
	wtp->sge_debug_data_high_index_3.sop[1] = ((value >> 12) & 0x0F);
	wtp->sge_debug_data_high_index_3.eop[1] = ((value >> 8) & 0x0F);
	wtp->sge_debug_data_high_index_3.sop[2] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_index_3.eop[2] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_index_3.sop[3] = ((value >> 28) & 0x0F);
	wtp->sge_debug_data_high_index_3.eop[3] = ((value >> 24) & 0x0F);

	/* Get ULP SE CNT CHx*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, A_ULP_TX_SE_CNT_CH0 + (i * 4));
		wtp->ulp_se_cnt_chx.sop[i] = ((value >> 28) & 0x0F);
		wtp->ulp_se_cnt_chx.eop[i] = ((value >> 24) & 0x0F);
	}

	/* Get MAC PORTx PKT COUNT*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, 0x3081c + ((i * 4) << 12));
		wtp->mac_portx_pkt_count.sop[i] = ((value >> 24) & 0xFF);
		wtp->mac_portx_pkt_count.eop[i] = ((value >> 16) & 0xFF);
		wtp->mac_porrx_pkt_count.sop[i] = ((value >> 8) & 0xFF);
		wtp->mac_porrx_pkt_count.eop[i] = ((value >> 0) & 0xFF);
	}

	/* Get mac portx aFramesTransmittedok*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, 0x30a80 + ((i * 4) << 12));
		wtp->mac_portx_aframestra_ok.sop[i] = (value & 0xFF);
		wtp->mac_portx_aframestra_ok.eop[i] = (value & 0xFF);
	}

	/* Get command respones from core to PCIE*/
	value = t4_read_reg(padap, A_PCIE_CMDR_RSP_CNT);

	wtp->core_pcie_cmd_rsp.sop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->core_pcie_cmd_rsp.sop[1] = ((value >> 16) & 0xFF); /*bit 16:23*/

	wtp->core_pcie_cmd_rsp.eop[0] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->core_pcie_cmd_rsp.eop[1] = ((value >> 24) & 0xFF); /*bit 24:31*/

	/*Get command Resposes from PCIE to SGE*/
	wtp->pcie_sge_cmd_rsp.sop[0] = sge_dbg_reg->debug_PC_Rsp_SOP0_cnt;
	wtp->pcie_sge_cmd_rsp.sop[1] = sge_dbg_reg->debug_PC_Rsp_SOP1_cnt;

	wtp->pcie_sge_cmd_rsp.eop[0] = sge_dbg_reg->debug_PC_Rsp_EOP0_cnt;
	wtp->pcie_sge_cmd_rsp.eop[1] = sge_dbg_reg->debug_PC_Rsp_EOP1_cnt;

	/* Get commands sent from SGE to CIM/uP*/
	wtp->sge_cim.sop[0] = sge_dbg_reg->debug_CIM_SOP0_cnt;
	wtp->sge_cim.sop[1] = sge_dbg_reg->debug_CIM_SOP1_cnt;

	wtp->sge_cim.eop[0] = sge_dbg_reg->debug_CIM_EOP0_cnt;
	wtp->sge_cim.eop[1] = sge_dbg_reg->debug_CIM_EOP1_cnt;

	/* Get Reqests of data from PCIE by SGE*/
	wtp->utx_sge_dma_req.sop[0] = sge_dbg_reg->debug_UD_Rx_SOP0_cnt;
	wtp->utx_sge_dma_req.sop[1] = sge_dbg_reg->debug_UD_Rx_SOP1_cnt;
	wtp->utx_sge_dma_req.sop[2] = sge_dbg_reg->debug_UD_Rx_SOP2_cnt;
	wtp->utx_sge_dma_req.sop[3] = sge_dbg_reg->debug_UD_Rx_SOP3_cnt;

	wtp->utx_sge_dma_req.eop[0] = sge_dbg_reg->debug_UD_Rx_EOP0_cnt;
	wtp->utx_sge_dma_req.eop[1] = sge_dbg_reg->debug_UD_Rx_EOP1_cnt;
	wtp->utx_sge_dma_req.eop[2] = sge_dbg_reg->debug_UD_Rx_EOP2_cnt;
	wtp->utx_sge_dma_req.eop[3] = sge_dbg_reg->debug_UD_Rx_EOP3_cnt;

	/* Get Reqests of data from PCIE by SGE*/
	wtp->sge_pcie_dma_req.sop[0] = sge_dbg_reg->debug_PD_Req_Rd0_cnt;
	wtp->sge_pcie_dma_req.sop[1] = sge_dbg_reg->debug_PD_Req_Rd1_cnt;
	wtp->sge_pcie_dma_req.sop[2] = sge_dbg_reg->debug_PD_Req_Rd2_cnt;
	wtp->sge_pcie_dma_req.sop[3] = sge_dbg_reg->debug_PD_Req_Rd3_cnt;
	/*no EOP's, so fake it.*/
	wtp->sge_pcie_dma_req.eop[0] = sge_dbg_reg->debug_PD_Req_Rd0_cnt;
	wtp->sge_pcie_dma_req.eop[1] = sge_dbg_reg->debug_PD_Req_Rd1_cnt;
	wtp->sge_pcie_dma_req.eop[2] = sge_dbg_reg->debug_PD_Req_Rd2_cnt;
	wtp->sge_pcie_dma_req.eop[3] = sge_dbg_reg->debug_PD_Req_Rd3_cnt;

	/* Get Reqests of data from PCIE to core*/
	value = t4_read_reg(padap, A_PCIE_DMAR_REQ_CNT);

	wtp->pcie_core_dma_req.sop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_dma_req.sop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->pcie_core_dma_req.sop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->pcie_core_dma_req.sop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/
	/* There is no eop so fake it.*/
	wtp->pcie_core_dma_req.eop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_dma_req.eop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->pcie_core_dma_req.eop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->pcie_core_dma_req.eop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/

	/* Get data responses from core to PCIE*/
	value = t4_read_reg(padap, A_PCIE_DMAR_RSP_SOP_CNT);

	wtp->core_pcie_dma_rsp.sop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->core_pcie_dma_rsp.sop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->core_pcie_dma_rsp.sop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->core_pcie_dma_rsp.sop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/

	value = t4_read_reg(padap, A_PCIE_DMAR_RSP_EOP_CNT);

	wtp->core_pcie_dma_rsp.eop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->core_pcie_dma_rsp.eop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->core_pcie_dma_rsp.eop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->core_pcie_dma_rsp.eop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/

	/* Get PCIE_DATA to SGE*/
	wtp->pcie_sge_dma_rsp.sop[0] = sge_dbg_reg->debug_PD_Rsp_SOP0_cnt;
	wtp->pcie_sge_dma_rsp.sop[1] = sge_dbg_reg->debug_PD_Rsp_SOP1_cnt;
	wtp->pcie_sge_dma_rsp.sop[2] = sge_dbg_reg->debug_PD_Rsp_SOP2_cnt;
	wtp->pcie_sge_dma_rsp.sop[3] = sge_dbg_reg->debug_PD_Rsp_SOP3_cnt;

	wtp->pcie_sge_dma_rsp.eop[0] = sge_dbg_reg->debug_PD_Rsp_EOP0_cnt;
	wtp->pcie_sge_dma_rsp.eop[1] = sge_dbg_reg->debug_PD_Rsp_EOP1_cnt;
	wtp->pcie_sge_dma_rsp.eop[2] = sge_dbg_reg->debug_PD_Rsp_EOP2_cnt;
	wtp->pcie_sge_dma_rsp.eop[3] = sge_dbg_reg->debug_PD_Rsp_EOP3_cnt;

	/*Get SGE to ULP_TX*/
	wtp->sge_utx.sop[0] = sge_dbg_reg->debug_U_Tx_SOP0_cnt;
	wtp->sge_utx.sop[1] = sge_dbg_reg->debug_U_Tx_SOP1_cnt;
	wtp->sge_utx.sop[2] = sge_dbg_reg->debug_U_Tx_SOP2_cnt;
	wtp->sge_utx.sop[3] = sge_dbg_reg->debug_U_Tx_SOP3_cnt;

	wtp->sge_utx.eop[0] = sge_dbg_reg->debug_U_Tx_EOP0_cnt;
	wtp->sge_utx.eop[1] = sge_dbg_reg->debug_U_Tx_EOP1_cnt;
	wtp->sge_utx.eop[2] = sge_dbg_reg->debug_U_Tx_EOP2_cnt;
	wtp->sge_utx.eop[3] = sge_dbg_reg->debug_U_Tx_EOP3_cnt;

	/* Get ULP_TX to TP*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, (A_ULP_TX_SE_CNT_CH0 + (i*4)));

		wtp->utx_tp.sop[i] = ((value >> 28) & 0xF); /*bits 28:31*/
		wtp->utx_tp.eop[i] = ((value >> 24) & 0xF); /*bits 24:27*/
	}

	/* Get TP_DBG_CSIDE registers*/
	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_CSIDE_RX0 + i),
			       true);

		wtp->utx_tpcside.sop[i]   = ((value >> 28) & 0xF);/*bits 28:31*/
		wtp->utx_tpcside.eop[i]   = ((value >> 24) & 0xF);/*bits 24:27*/
		wtp->tpcside_rxpld.sop[i] = ((value >> 20) & 0xF);/*bits 20:23*/
		wtp->tpcside_rxpld.eop[i] = ((value >> 16) & 0xF);/*bits 16:19*/
		wtp->tpcside_rxarb.sop[i] = ((value >> 12) & 0xF);/*bits 12:15*/
		wtp->tpcside_rxarb.eop[i] = ((value >> 8) & 0xF); /*bits 8:11*/
		wtp->tpcside_rxcpl.sop[i] = ((value >> 4) & 0xF); /*bits 4:7*/
		wtp->tpcside_rxcpl.eop[i] = ((value >> 0) & 0xF); /*bits 0:3*/
	}

	/* TP_DBG_ESIDE*/
	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_ESIDE_PKT0 + i),
			       true);

		wtp->tpeside_mps.sop[i] = ((value >> 28) & 0xF); /*bits 28:31*/
		wtp->tpeside_mps.eop[i] = ((value >> 24) & 0xF); /*bits 24:27*/
		wtp->tpeside_pm.sop[i]	= ((value >> 20) & 0xF); /*bits 20:23*/
		wtp->tpeside_pm.eop[i]	= ((value >> 16) & 0xF); /*bits 16:19*/
		wtp->mps_tpeside.sop[i] = ((value >> 12) & 0xF); /*bits 12:15*/
		wtp->mps_tpeside.eop[i] = ((value >> 8) & 0xF); /*bits 8:11*/
		wtp->tpeside_pld.sop[i] = ((value >> 4) & 0xF); /*bits 4:7*/
		wtp->tpeside_pld.eop[i] = ((value >> 0) & 0xF); /*bits 0:3*/

	}

	/*PCIE CMD STAT2*/
	for (i = 0; i < 3; i++) {
		value = t4_read_reg(padap, 0x5988 + (i * 0x10));
		wtp->pcie_cmd_stat2.sop[i] = value & 0xFF;
		wtp->pcie_cmd_stat2.eop[i] = value & 0xFF;
	}

	/*PCIE cmd stat3*/
	for (i = 0; i < 3; i++) {
		value = t4_read_reg(padap, 0x598c + (i * 0x10));
		wtp->pcie_cmd_stat3.sop[i] = value & 0xFF;
		wtp->pcie_cmd_stat3.eop[i] = value & 0xFF;
	}

	/* ULP_RX input/output*/
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_ULP_RX_SE_CNT_CH0 + (i*4)));

		wtp->pmrx_ulprx.sop[i]	  = ((value >> 4) & 0xF); /*bits 4:7*/
		wtp->pmrx_ulprx.eop[i]	  = ((value >> 0) & 0xF); /*bits 0:3*/
		wtp->ulprx_tpcside.sop[i] = ((value >> 28) & 0xF);/*bits 28:31*/
		wtp->ulprx_tpcside.eop[i] = ((value >> 24) & 0xF);/*bits 24:27*/
	}

	/* Get the MPS input from TP*/
	drop = 0;
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_MPS_TX_SE_CNT_TP01 + (i << 2)));
		wtp->tp_mps.sop[(i*2)]	   = ((value >> 8) & 0xFF); /*bit 8:15*/
		wtp->tp_mps.eop[(i*2)]	   = ((value >> 0) & 0xFF); /*bit 0:7*/
		wtp->tp_mps.sop[(i*2) + 1] = ((value >> 24) & 0xFF);/*bit 24:31
								    */
		wtp->tp_mps.eop[(i*2) + 1] = ((value >> 16) & 0xFF);/*bit 16:23
								    */
	}
	drop  = ptp_mib->TP_MIB_OFD_ARP_DROP.value;
	drop += ptp_mib->TP_MIB_OFD_DFR_DROP.value;

	drop += ptp_mib->TP_MIB_TNL_DROP_0.value;
	drop += ptp_mib->TP_MIB_TNL_DROP_1.value;
	drop += ptp_mib->TP_MIB_TNL_DROP_2.value;
	drop += ptp_mib->TP_MIB_TNL_DROP_3.value;

	wtp->tp_mps.drops = drop;

	/* Get the MPS output to the MAC's*/
	drop = 0;
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_MPS_TX_SE_CNT_MAC01 + (i << 2)));
		wtp->mps_xgm.sop[(i*2)]     = ((value >> 8) & 0xFF);/*bit 8:15*/
		wtp->mps_xgm.eop[(i*2)]     = ((value >> 0) & 0xFF);/*bit 0:7*/
		wtp->mps_xgm.sop[(i*2) + 1] = ((value >> 24) & 0xFF);/*bit 24:31
								     */
		wtp->mps_xgm.eop[(i*2) + 1] = ((value >> 16) & 0xFF);/*bit 16:23
								     */
	}
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap,
				(T5_PORT0_REG(A_MPS_PORT_STAT_TX_PORT_DROP_L) +
				(i * T5_PORT_STRIDE)));
		drop += value;
	}
	wtp->mps_xgm.drops = (drop & 0xFF);

	/* Get the SOP/EOP counters into and out of MAC. [JHANEL] I think this
	 * is*/
	/* clear on read, so you have to read both TX and RX path at same
	 * time.*/
	drop = 0;
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap,
				(T5_PORT0_REG(A_MAC_PORT_PKT_COUNT) +
				(i * T5_PORT_STRIDE)));

		wtp->tx_xgm_xgm.sop[i] = ((value >> 24) & 0xFF); /*bit 24:31*/
		wtp->tx_xgm_xgm.eop[i] = ((value >> 16) & 0xFF); /*bit 16:23*/
		wtp->rx_xgm_xgm.sop[i] = ((value >> 8) & 0xFF); /*bit 8:15*/
		wtp->rx_xgm_xgm.eop[i] = ((value >> 0) & 0xFF); /*bit 0:7*/
	}

	/* Get the MAC's output to the wire*/
	drop = 0;
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap,
				(T5_PORT0_REG(A_MAC_PORT_AFRAMESTRANSMITTEDOK) +
				(i * T5_PORT_STRIDE)));
		wtp->xgm_wire.sop[i] = (value);
		wtp->xgm_wire.eop[i] = (value); /* No EOP for XGMAC, so fake
						   it.*/
	}

	/*########################################################################*/
	/*# RX PATH, starting from wire*/
	/*########################################################################*/

	/* Add up the wire input to the MAC*/
	drop = 0;
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap,
				(T5_PORT0_REG(A_MAC_PORT_AFRAMESRECEIVEDOK) +
				(i * T5_PORT_STRIDE)));

		wtp->wire_xgm.sop[i] = (value);
		wtp->wire_xgm.eop[i] = (value); /* No EOP for XGMAC, so fake
						   it.*/
	}

	/* Already read the rx_xgm_xgm when reading TX path.*/

	/* Add up SOP/EOP's on all 8 MPS buffer channels*/
	drop = 0;
	for (i = 0; i < 8; i++) {
		value = t4_read_reg(padap, (A_MPS_RX_SE_CNT_IN0 + (i << 2)));

		wtp->xgm_mps.sop[i] = ((value >> 8) & 0xFF); /*bits 8:15*/
		wtp->xgm_mps.eop[i] = ((value >> 0) & 0xFF); /*bits 0:7*/
	}
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, (A_MPS_RX_CLS_DROP_CNT0 + (i << 2)));
		/* typo in JHANEL's code.*/
		drop += (value & 0xFFFF) + ((value >> 16) & 0xFFFF);
	}
	wtp->xgm_mps.cls_drop = drop & 0xFF;

	/* Add up the overflow drops on all 4 ports.*/
	drop = 0;
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L +
				     (i << 2)));
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_LB_DROP_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L +
				     (i << 2)));

		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L +
				     (i << 3)));
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_LB_TRUNC_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L +
				     (i << 3)));

		value = t4_read_reg(padap,
			T5_PORT0_REG(A_MPS_PORT_STAT_LB_PORT_DROP_FRAMES) +
			(i * T5_PORT_STRIDE));
		drop += value;
	}
	wtp->xgm_mps.drop = (drop & 0xFF);

	/* Add up the MPS errors that should result in dropped packets*/
	err = 0;
	for (i = 0; i < 4; i++) {

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LEN_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LEN_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_SYM_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_SYM_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LESS_64B_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG((A_MPS_PORT_STAT_RX_PORT_LESS_64B_L) +
			(i * T5_PORT_STRIDE) + 4)));
	}
	wtp->xgm_mps.err = (err & 0xFF);

	drop = 0;
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_MPS_RX_SE_CNT_OUT01 + (i << 2)));

		wtp->mps_tp.sop[(i*2)]	   = ((value >> 8) & 0xFF); /*bit 8:15*/
		wtp->mps_tp.eop[(i*2)]	   = ((value >> 0) & 0xFF); /*bit 0:7*/
		wtp->mps_tp.sop[(i*2) + 1] = ((value >> 24) & 0xFF);/*bit 24:31
								    */
		wtp->mps_tp.eop[(i*2) + 1] = ((value >> 16) & 0xFF);/*bit 16:23
								    */
	}
	drop = ptp_mib->TP_MIB_TNL_CNG_DROP_0.value;
	drop += ptp_mib->TP_MIB_TNL_CNG_DROP_1.value;
	drop += ptp_mib->TP_MIB_TNL_CNG_DROP_2.value;
	drop += ptp_mib->TP_MIB_TNL_CNG_DROP_3.value;
	drop += ptp_mib->TP_MIB_OFD_CHN_DROP_0.value;
	drop += ptp_mib->TP_MIB_OFD_CHN_DROP_1.value;
	drop += ptp_mib->TP_MIB_OFD_CHN_DROP_2.value;
	drop += ptp_mib->TP_MIB_OFD_CHN_DROP_3.value;
	drop += ptp_mib->TP_MIB_FCOE_DROP_0.value;
	drop += ptp_mib->TP_MIB_FCOE_DROP_1.value;
	drop += ptp_mib->TP_MIB_FCOE_DROP_2.value;
	drop += ptp_mib->TP_MIB_FCOE_DROP_3.value;
	drop += ptp_mib->TP_MIB_OFD_VLN_DROP_0.value;
	drop += ptp_mib->TP_MIB_OFD_VLN_DROP_1.value;
	drop += ptp_mib->TP_MIB_OFD_VLN_DROP_2.value;
	drop += ptp_mib->TP_MIB_OFD_VLN_DROP_3.value;
	drop += ptp_mib->TP_MIB_USM_DROP.value;

	wtp->mps_tp.drops = drop;

	/* Get TP_DBG_CSIDE_TX registers*/
	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_CSIDE_RX0 + i),
			       true);

		wtp->tpcside_csw.sop[i]   = ((value >> 28) & 0xF);/*bits 28:31*/
		wtp->tpcside_csw.eop[i]   = ((value >> 24) & 0xF);/*bits 24:27*/
		wtp->tpcside_pm.sop[i]	  = ((value >> 20) & 0xF);/*bits 20:23*/
		wtp->tpcside_pm.eop[i]	  = ((value >> 16) & 0xF);/*bits 16:19*/
		wtp->tpcside_uturn.sop[i] = ((value >> 12) & 0xF);/*bits 12:15*/
		wtp->tpcside_uturn.eop[i] = ((value >> 8) & 0xF); /*bits 8:11*/
		wtp->tpcside_txcpl.sop[i] = ((value >> 4) & 0xF); /*bits 4:7*/
		wtp->tpcside_txcpl.eop[i] = ((value >> 0) & 0xF); /*bits 0:3*/
	}

	/* TP to CPL_SWITCH*/
	wtp->tp_csw.sop[0] = sge_dbg_reg->debug_CPLSW_TP_Rx_SOP0_cnt;
	wtp->tp_csw.sop[1] = sge_dbg_reg->debug_CPLSW_TP_Rx_SOP1_cnt;

	wtp->tp_csw.eop[0] = sge_dbg_reg->debug_CPLSW_TP_Rx_EOP0_cnt;
	wtp->tp_csw.eop[1] = sge_dbg_reg->debug_CPLSW_TP_Rx_EOP1_cnt;

	/* TP/CPL_SWITCH to SGE*/
	wtp->csw_sge.sop[0] = sge_dbg_reg->debug_T_Rx_SOP0_cnt;
	wtp->csw_sge.sop[1] = sge_dbg_reg->debug_T_Rx_SOP1_cnt;

	wtp->csw_sge.eop[0] = sge_dbg_reg->debug_T_Rx_EOP0_cnt;
	wtp->csw_sge.eop[1] = sge_dbg_reg->debug_T_Rx_EOP1_cnt;

	wtp->sge_pcie.sop[0] = sge_dbg_reg->debug_PD_Req_SOP0_cnt;
	wtp->sge_pcie.sop[1] = sge_dbg_reg->debug_PD_Req_SOP1_cnt;
	wtp->sge_pcie.sop[2] = sge_dbg_reg->debug_PD_Req_SOP2_cnt;
	wtp->sge_pcie.sop[3] = sge_dbg_reg->debug_PD_Req_SOP3_cnt;

	wtp->sge_pcie.eop[0] = sge_dbg_reg->debug_PD_Req_EOP0_cnt;
	wtp->sge_pcie.eop[1] = sge_dbg_reg->debug_PD_Req_EOP1_cnt;
	wtp->sge_pcie.eop[2] = sge_dbg_reg->debug_PD_Req_EOP2_cnt;
	wtp->sge_pcie.eop[3] = sge_dbg_reg->debug_PD_Req_EOP3_cnt;

	wtp->sge_pcie_ints.sop[0] = sge_dbg_reg->debug_PD_Req_Int0_cnt;
	wtp->sge_pcie_ints.sop[1] = sge_dbg_reg->debug_PD_Req_Int1_cnt;
	wtp->sge_pcie_ints.sop[2] = sge_dbg_reg->debug_PD_Req_Int2_cnt;
	wtp->sge_pcie_ints.sop[3] = sge_dbg_reg->debug_PD_Req_Int3_cnt;
	/* NO EOP, so fake it.*/
	wtp->sge_pcie_ints.eop[0] = sge_dbg_reg->debug_PD_Req_Int0_cnt;
	wtp->sge_pcie_ints.eop[1] = sge_dbg_reg->debug_PD_Req_Int1_cnt;
	wtp->sge_pcie_ints.eop[2] = sge_dbg_reg->debug_PD_Req_Int2_cnt;
	wtp->sge_pcie_ints.eop[3] = sge_dbg_reg->debug_PD_Req_Int3_cnt;

	/*Get PCIE DMA1 STAT2*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, A_PCIE_T5_DMA_STAT2 + (i * 0x10));
		wtp->pcie_dma1_stat2.sop[i] = ((value >> 8) & 0x0F);
		wtp->pcie_dma1_stat2.eop[i] = ((value >> 8) & 0x0F);
		wtp->pcie_dma1_stat2_core.sop[i] += value & 0x0F;
		wtp->pcie_dma1_stat2_core.eop[i] += value & 0x0F;
	}

	/* Get mac porrx aFramesTransmittedok*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, 0x30a88 + ((i * 4) << 12));
		wtp->mac_porrx_aframestra_ok.sop[i] = (value & 0xFF);
		wtp->mac_porrx_aframestra_ok.eop[i] = (value & 0xFF);
	}

	/*Get SGE debug data high index 7*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_7);
	wtp->sge_debug_data_high_indx7.sop[0] = ((value >> 4) & 0x0F);
	wtp->sge_debug_data_high_indx7.eop[0] = ((value >> 0) & 0x0F);
	wtp->sge_debug_data_high_indx7.sop[1] = ((value >> 12) & 0x0F);
	wtp->sge_debug_data_high_indx7.eop[1] = ((value >> 8) & 0x0F);
	wtp->sge_debug_data_high_indx7.sop[2] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_indx7.eop[2] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_indx7.sop[3] = ((value >> 28) & 0x0F);
	wtp->sge_debug_data_high_indx7.eop[3] = ((value >> 24) & 0x0F);

	/*Get SGE debug data high index 1*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_1);
	wtp->sge_debug_data_high_indx1.sop[0] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_indx1.eop[0] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_indx1.sop[1] = ((value >> 28) & 0x0F);
	wtp->sge_debug_data_high_indx1.eop[1] = ((value >> 24) & 0x0F);

	/*Get TP debug CSIDE Tx registers*/
	for (i = 0; i < 2; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_CSIDE_TX0 + i),
			       true);

		wtp->utx_tpcside_tx.sop[i] = ((value >> 28) & 0xF);/*bits 28:31
								   */
		wtp->utx_tpcside_tx.eop[i] = ((value >> 24) & 0xF);
	}

	/*Get SGE debug data high index 9*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_9);
	wtp->sge_debug_data_high_indx9.sop[0] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_indx9.sop[1] = ((value >> 28) & 0x0F);
	wtp->sge_debug_data_high_indx9.eop[0] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_indx9.eop[1] = ((value >> 24) & 0x0F);
	wtp->sge_work_req_pkt.sop[0] = ((value >> 4) & 0x0F);
	wtp->sge_work_req_pkt.sop[1] = ((value >> 12) & 0x0F);

	/*Get LE DB response count*/
	value = t4_read_reg(padap, A_LE_DB_REQ_RSP_CNT);
	wtp->le_db_rsp_cnt.sop = value & 0xF;
	wtp->le_db_rsp_cnt.eop = (value >> 16) & 0xF;

	/*Get TP debug Eside PKTx*/
	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_ESIDE_PKT0 + i),
			       true);

		wtp->tp_dbg_eside_pktx.sop[i] = ((value >> 12) & 0xF);
		wtp->tp_dbg_eside_pktx.eop[i] = ((value >> 8) & 0xF);
	}

	/* Get data responses from core to PCIE*/
	value = t4_read_reg(padap, A_PCIE_DMAW_SOP_CNT);

	wtp->pcie_core_dmaw.sop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_dmaw.sop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->pcie_core_dmaw.sop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->pcie_core_dmaw.sop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/

	value = t4_read_reg(padap, A_PCIE_DMAW_EOP_CNT);

	wtp->pcie_core_dmaw.eop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_dmaw.eop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->pcie_core_dmaw.eop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->pcie_core_dmaw.eop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/

	value = t4_read_reg(padap, A_PCIE_DMAI_CNT);

	wtp->pcie_core_dmai.sop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_dmai.sop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->pcie_core_dmai.sop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->pcie_core_dmai.sop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/
	/* no eop for interrups, just fake it.*/
	wtp->pcie_core_dmai.eop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->pcie_core_dmai.eop[1] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->pcie_core_dmai.eop[2] = ((value >> 16) & 0xFF); /*bit 16:23*/
	wtp->pcie_core_dmai.eop[3] = ((value >> 24) & 0xFF); /*bit 24:31*/

	rc = write_compression_hdr(&scratch_buff, dbg_buff);

	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int t6_wtp_data(struct cudbg_init *pdbg_init,
		       struct cudbg_buffer *dbg_buff,
		       struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct sge_debug_reg_data *sge_dbg_reg = NULL;
	struct cudbg_buffer scratch_buff;
	struct tp_mib_data *ptp_mib = NULL;
	struct wtp_data *wtp;
	u32 Sge_Dbg[32] = {0};
	u32 value = 0;
	u32 i = 0;
	u32 drop = 0;
	u32 err = 0;
	u32 offset;
	int rc = 0;

	rc = get_scratch_buff(dbg_buff, sizeof(struct wtp_data), &scratch_buff);

	if (rc)
		goto err;

	offset = scratch_buff.offset;
	wtp = (struct wtp_data *)((char *)scratch_buff.data + offset);

	read_sge_debug_data(pdbg_init, Sge_Dbg);
	read_tp_mib_data(pdbg_init, &ptp_mib);

	sge_dbg_reg = (struct sge_debug_reg_data *) &Sge_Dbg[0];

	/*# TX PATH*/

	/*PCIE CMD STAT2*/
	value = t4_read_reg(padap, A_PCIE_T5_CMD_STAT2);
	wtp->pcie_cmd_stat2.sop[0] = value & 0xFF;
	wtp->pcie_cmd_stat2.eop[0] = value & 0xFF;

	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_7);
	wtp->sge_pcie_cmd_req.sop[0] = ((value >> 20) & 0x0F);
	wtp->sge_pcie_cmd_req.eop[0] = ((value >> 16) & 0x0F);
	wtp->sge_pcie_cmd_req.sop[1] = ((value >> 28) & 0x0F);
	wtp->sge_pcie_cmd_req.eop[1] = ((value >> 24) & 0x0F);

	value = t4_read_reg(padap, A_PCIE_T5_CMD_STAT3);
	wtp->pcie_cmd_stat3.sop[0] = value & 0xFF;
	wtp->pcie_cmd_stat3.eop[0] = value & 0xFF;

	/*Get command Resposes from PCIE to SGE*/
	wtp->pcie_sge_cmd_rsp.sop[0] = sge_dbg_reg->debug_PC_Rsp_SOP0_cnt;
	wtp->pcie_sge_cmd_rsp.eop[0] = sge_dbg_reg->debug_PC_Rsp_EOP0_cnt;
	wtp->pcie_sge_cmd_rsp.sop[1] = sge_dbg_reg->debug_PC_Rsp_SOP1_cnt;
	wtp->pcie_sge_cmd_rsp.eop[1] = sge_dbg_reg->debug_PC_Rsp_EOP0_cnt;

	/* Get commands sent from SGE to CIM/uP*/
	wtp->sge_cim.sop[0] = sge_dbg_reg->debug_CIM_SOP0_cnt;
	wtp->sge_cim.sop[1] = sge_dbg_reg->debug_CIM_SOP1_cnt;

	wtp->sge_cim.eop[0] = sge_dbg_reg->debug_CIM_EOP0_cnt;
	wtp->sge_cim.eop[1] = sge_dbg_reg->debug_CIM_EOP1_cnt;

	/*Get SGE debug data high index 9*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_9);
	wtp->sge_work_req_pkt.sop[0] = ((value >> 4) & 0x0F);
	wtp->sge_work_req_pkt.eop[0] = ((value >> 0) & 0x0F);

	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, A_PCIE_T5_DMA_STAT2 + (i * 0x10));
		wtp->pcie_dma1_stat2.sop[i] = ((value >> 8) & 0x0F);
		wtp->pcie_dma1_stat2.eop[i] = ((value >> 8) & 0x0F);
		wtp->pcie_dma1_stat2_core.sop[i] = value & 0x0F;
		wtp->pcie_dma1_stat2_core.eop[i] = value & 0x0F;
	}

	/* Get DMA0 stats3*/
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, A_PCIE_T5_DMA_STAT3 + (i * 0x10));
		wtp->pcie_t5_dma_stat3.sop[i] = value & 0xFF;
		wtp->pcie_t5_dma_stat3.eop[i] = ((value >> 16) & 0xFF);
	}

	/* Get ULP SE CNT CHx*/
	for (i = 0; i < 4; i++) {
		value = t4_read_reg(padap, A_ULP_TX_SE_CNT_CH0 + (i * 4));
		wtp->ulp_se_cnt_chx.sop[i] = ((value >> 28) & 0x0F);
		wtp->ulp_se_cnt_chx.eop[i] = ((value >> 24) & 0x0F);
	}

	/* Get TP_DBG_CSIDE registers*/
	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_CSIDE_RX0 + i),
			       true);

		wtp->utx_tpcside.sop[i]   = ((value >> 28) & 0xF);/*bits 28:31*/
		wtp->utx_tpcside.eop[i]   = ((value >> 24) & 0xF);/*bits 24:27*/
		wtp->tpcside_rxarb.sop[i] = ((value >> 12) & 0xF);/*bits 12:15*/
		wtp->tpcside_rxarb.eop[i] = ((value >> 8) & 0xF); /*bits 8:11*/
	}

	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_ESIDE_PKT0 + i),
			       true);


		wtp->tpeside_mps.sop[i] = ((value >> 28) & 0xF); /*bits 28:31*/
		wtp->tpeside_mps.eop[i] = ((value >> 24) & 0xF); /*bits 24:27*/
	}

	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_MPS_TX_SE_CNT_TP01 + (i << 2)));
		wtp->tp_mps.sop[(i*2)]	   = ((value >> 8) & 0xFF); /*bit 8:15*/
		wtp->tp_mps.eop[(i*2)]	   = ((value >> 0) & 0xFF); /*bit 0:7*/
		wtp->tp_mps.sop[(i*2) + 1] = ((value >> 24) & 0xFF);/*bit 24:31
								    */
		wtp->tp_mps.eop[(i*2) + 1] = ((value >> 16) & 0xFF);/*bit 16:23
								    */
	}

	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_MPS_TX_SE_CNT_MAC01 + (i << 2)));
		wtp->mps_xgm.sop[(i*2)]     = ((value >> 8) & 0xFF);/*bit 8:15*/
		wtp->mps_xgm.eop[(i*2)]     = ((value >> 0) & 0xFF); /*bit 0:7*/
		wtp->mps_xgm.sop[(i*2) + 1] = ((value >> 24) & 0xFF);/*bit 24:31
								     */
		wtp->mps_xgm.eop[(i*2) + 1] = ((value >> 16) & 0xFF);/*bit 16:23
								     */
	}

	/* Get MAC PORTx PKT COUNT*/
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, 0x3081c + ((i * 4) << 12));
		wtp->mac_portx_pkt_count.sop[i] = ((value >> 24) & 0xFF);
		wtp->mac_portx_pkt_count.eop[i] = ((value >> 16) & 0xFF);
		wtp->mac_porrx_pkt_count.sop[i] = ((value >> 8) & 0xFF);
		wtp->mac_porrx_pkt_count.eop[i] = ((value >> 0) & 0xFF);
	}

	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, 0x30f20 + ((i * 4) << 12));
		wtp->mac_portx_aframestra_ok.sop[i] = value & 0xff;
		wtp->mac_portx_aframestra_ok.eop[i] = value & 0xff;
	}

	/*MAC_PORT_MTIP_1G10G_TX_etherStatsPkts*/

	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, 0x30f60 + ((i * 4) << 12));
		wtp->mac_portx_etherstatspkts.sop[i] = value & 0xff;
		wtp->mac_portx_etherstatspkts.eop[i] = value & 0xff;
	}

	/*RX path*/

	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_7);
	wtp->sge_debug_data_high_indx7.sop[0] = ((value >> 4) & 0x0F);
	wtp->sge_debug_data_high_indx7.eop[0] = ((value >> 0) & 0x0F);
	wtp->sge_debug_data_high_indx7.sop[1] = ((value >> 12) & 0x0F);
	wtp->sge_debug_data_high_indx7.eop[1] = ((value >> 8) & 0x0F);

	/*Get SGE debug data high index 1*/
	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_1);
	wtp->sge_debug_data_high_indx1.sop[0] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_indx1.eop[0] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_indx1.sop[1] = ((value >> 28) & 0x0F);
	wtp->sge_debug_data_high_indx1.eop[1] = ((value >> 24) & 0x0F);

	value = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_9);
	wtp->sge_debug_data_high_indx9.sop[0] = ((value >> 20) & 0x0F);
	wtp->sge_debug_data_high_indx9.sop[1] = ((value >> 28) & 0x0F);

	wtp->sge_debug_data_high_indx9.eop[0] = ((value >> 16) & 0x0F);
	wtp->sge_debug_data_high_indx9.eop[1] = ((value >> 24) & 0x0F);

	for (i = 0; i < 2; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_CSIDE_TX0 + i),
			       true);

		wtp->utx_tpcside_tx.sop[i] = ((value >> 28) & 0xF);/*bits 28:31
								   */
		wtp->utx_tpcside_tx.eop[i]   = ((value >> 24) & 0xF);
	}

	/*ULP_RX input/output*/
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_ULP_RX_SE_CNT_CH0 + (i*4)));

		wtp->pmrx_ulprx.sop[i]	  = ((value >> 4) & 0xF); /*bits 4:7*/
		wtp->pmrx_ulprx.eop[i]	  = ((value >> 0) & 0xF); /*bits 0:3*/
		wtp->ulprx_tpcside.sop[i] = ((value >> 28) & 0xF);/*bits 28:31*/
		wtp->ulprx_tpcside.eop[i] = ((value >> 24) & 0xF);/*bits 24:27*/
	}

	/*Get LE DB response count*/
	value = t4_read_reg(padap, A_LE_DB_REQ_RSP_CNT);
	wtp->le_db_rsp_cnt.sop = value & 0xF;
	wtp->le_db_rsp_cnt.eop = (value >> 16) & 0xF;

	/*Get TP debug Eside PKTx*/
	for (i = 0; i < 4; i++) {
		t4_tp_pio_read(padap, &value, 1, (u32)(A_TP_DBG_ESIDE_PKT0 + i),
			       true);

		wtp->tp_dbg_eside_pktx.sop[i] = ((value >> 12) & 0xF);
		wtp->tp_dbg_eside_pktx.eop[i] = ((value >> 8) & 0xF);
	}

	drop = 0;
	/*MPS_RX_SE_CNT_OUT01*/
	value = t4_read_reg(padap, (A_MPS_RX_SE_CNT_OUT01 + (i << 2)));
	wtp->mps_tp.sop[0] = ((value >> 8) & 0xFF); /*bit 8:15*/
	wtp->mps_tp.eop[0] = ((value >> 0) & 0xFF); /*bit 0:7*/
	wtp->mps_tp.sop[1] = ((value >> 24) & 0xFF); /*bit 24:31*/
	wtp->mps_tp.eop[1] = ((value >> 16) & 0xFF); /*bit 16:23*/

	drop = ptp_mib->TP_MIB_TNL_CNG_DROP_0.value;
	drop += ptp_mib->TP_MIB_TNL_CNG_DROP_1.value;
	drop += ptp_mib->TP_MIB_OFD_CHN_DROP_0.value;
	drop += ptp_mib->TP_MIB_OFD_CHN_DROP_1.value;
	drop += ptp_mib->TP_MIB_FCOE_DROP_0.value;
	drop += ptp_mib->TP_MIB_FCOE_DROP_1.value;
	drop += ptp_mib->TP_MIB_OFD_VLN_DROP_0.value;
	drop += ptp_mib->TP_MIB_OFD_VLN_DROP_1.value;
	drop += ptp_mib->TP_MIB_USM_DROP.value;

	wtp->mps_tp.drops = drop;

	drop = 0;
	for (i = 0; i < 8; i++) {
		value = t4_read_reg(padap, (A_MPS_RX_SE_CNT_IN0 + (i << 2)));

		wtp->xgm_mps.sop[i] = ((value >> 8) & 0xFF); /*bits 8:15*/
		wtp->xgm_mps.eop[i] = ((value >> 0) & 0xFF); /*bits 0:7*/
	}
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, (A_MPS_RX_CLS_DROP_CNT0 + (i << 2)));
		drop += (value & 0xFFFF) + ((value >> 16) & 0xFFFF);
	}
	wtp->xgm_mps.cls_drop = drop & 0xFF;

	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, 0x30e20 + ((i * 4) << 12));
		wtp->mac_porrx_aframestra_ok.sop[i] = value & 0xff;
		wtp->mac_porrx_aframestra_ok.eop[i] = value & 0xff;
	}

	/*MAC_PORT_MTIP_1G10G_RX_etherStatsPkts*/
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap, 0x30e60 + ((i * 4) << 12));
		wtp->mac_porrx_etherstatspkts.sop[i] = value & 0xff;
		wtp->mac_porrx_etherstatspkts.eop[i] = value & 0xff;
	}

	wtp->sge_pcie_ints.sop[0] = sge_dbg_reg->debug_PD_Req_Int0_cnt;
	wtp->sge_pcie_ints.sop[1] = sge_dbg_reg->debug_PD_Req_Int1_cnt;
	wtp->sge_pcie_ints.sop[2] = sge_dbg_reg->debug_PD_Req_Int2_cnt;
	wtp->sge_pcie_ints.sop[3] = sge_dbg_reg->debug_PD_Req_Int3_cnt;

	/* Add up the overflow drops on all 4 ports.*/
	drop = 0;
	for (i = 0; i < 2; i++) {
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L +
				     (i << 2)));
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_LB_DROP_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_DROP_FRAME_L +
				     (i << 2)));

		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L +
				     (i << 3)));
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_LB_TRUNC_FRAME_L +
				     (i << 3)));
		drop += value;
		value = t4_read_reg(padap,
				    (A_MPS_STAT_RX_BG_0_MAC_TRUNC_FRAME_L +
				     (i << 3)));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_LB_PORT_DROP_FRAMES) +
			(i * T5_PORT_STRIDE)));
		drop += value;
	}
	wtp->xgm_mps.drop = (drop & 0xFF);

	/* Add up the MPS errors that should result in dropped packets*/
	err = 0;
	for (i = 0; i < 2; i++) {

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_MTU_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_CRC_ERROR_L) +
				     (i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_CRC_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LEN_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LEN_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_SYM_ERROR_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_SYM_ERROR_L) +
			(i * T5_PORT_STRIDE) + 4));

		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LESS_64B_L) +
			(i * T5_PORT_STRIDE)));
		err += value;
		value = t4_read_reg(padap,
			(T5_PORT0_REG(A_MPS_PORT_STAT_RX_PORT_LESS_64B_L) +
			(i * T5_PORT_STRIDE) + 4));
	}
	wtp->xgm_mps.err = (err & 0xFF);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);

	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

int collect_wtp_data(struct cudbg_init *pdbg_init,
		     struct cudbg_buffer *dbg_buff,
		     struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	int rc = -1;

	if (is_t5(padap))
		rc = t5_wtp_data(pdbg_init, dbg_buff, cudbg_err);
	else if (is_t6(padap))
		rc = t6_wtp_data(pdbg_init, dbg_buff, cudbg_err);

	return rc;
}
