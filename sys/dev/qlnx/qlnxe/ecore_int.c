/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * File : ecore_int.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"
#include "ecore.h"
#include "ecore_spq.h"
#include "reg_addr.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore_init_ops.h"
#include "ecore_rt_defs.h"
#include "ecore_int.h"
#include "reg_addr.h"
#include "ecore_hw.h"
#include "ecore_sriov.h"
#include "ecore_vf.h"
#include "ecore_hw_defs.h"
#include "ecore_hsi_common.h"
#include "ecore_mcp.h"
#include "ecore_dbg_fw_funcs.h"

#ifdef DIAG
/* This is nasty, but diag is using the drv_dbg_fw_funcs.c [non-ecore flavor],
 * and so the functions are lacking ecore prefix.
 * If there would be other clients needing this [or if the content that isn't
 * really optional there would increase], we'll need to re-think this.
 */
enum dbg_status dbg_read_attn(struct ecore_hwfn *dev,
							  struct ecore_ptt *ptt,
							  enum block_id block,
							  enum dbg_attn_type attn_type,
							  bool clear_status,
							  struct dbg_attn_block_result *results);

enum dbg_status dbg_parse_attn(struct ecore_hwfn *dev,
							   struct dbg_attn_block_result *results);

const char* dbg_get_status_str(enum dbg_status status);

#define ecore_dbg_read_attn(hwfn, ptt, id, type, clear, results) \
	dbg_read_attn(hwfn, ptt, id, type, clear, results)
#define ecore_dbg_parse_attn(hwfn, results) \
	dbg_parse_attn(hwfn, results)
#define ecore_dbg_get_status_str(status) \
	dbg_get_status_str(status)
#endif

struct ecore_pi_info {
	ecore_int_comp_cb_t comp_cb;
	void *cookie; /* Will be sent to the completion callback function */
};

struct ecore_sb_sp_info {
	struct ecore_sb_info sb_info;
	/* per protocol index data */
	struct ecore_pi_info pi_info_arr[PIS_PER_SB_E4];
};

enum ecore_attention_type {
	ECORE_ATTN_TYPE_ATTN,
	ECORE_ATTN_TYPE_PARITY,
};

#define SB_ATTN_ALIGNED_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(struct atten_status_block, p_hwfn)

struct aeu_invert_reg_bit {
	char bit_name[30];

#define ATTENTION_PARITY		(1 << 0)

#define ATTENTION_LENGTH_MASK		(0x00000ff0)
#define ATTENTION_LENGTH_SHIFT		(4)
#define ATTENTION_LENGTH(flags)		(((flags) & ATTENTION_LENGTH_MASK) >> \
					 ATTENTION_LENGTH_SHIFT)
#define ATTENTION_SINGLE		(1 << ATTENTION_LENGTH_SHIFT)
#define ATTENTION_PAR			(ATTENTION_SINGLE | ATTENTION_PARITY)
#define ATTENTION_PAR_INT		((2 << ATTENTION_LENGTH_SHIFT) | \
					 ATTENTION_PARITY)

/* Multiple bits start with this offset */
#define ATTENTION_OFFSET_MASK		(0x000ff000)
#define ATTENTION_OFFSET_SHIFT		(12)

#define ATTENTION_BB_MASK		(0x00700000)
#define ATTENTION_BB_SHIFT		(20)
#define ATTENTION_BB(value)		(value << ATTENTION_BB_SHIFT)
#define ATTENTION_BB_DIFFERENT		(1 << 23)

#define	ATTENTION_CLEAR_ENABLE		(1 << 28)
	unsigned int flags;

	/* Callback to call if attention will be triggered */
	enum _ecore_status_t (*cb)(struct ecore_hwfn *p_hwfn);

	enum block_id block_index;
};

struct aeu_invert_reg {
	struct aeu_invert_reg_bit bits[32];
};

#define MAX_ATTN_GRPS		(8)
#define NUM_ATTN_REGS		(9)

static enum _ecore_status_t ecore_mcp_attn_cb(struct ecore_hwfn *p_hwfn)
{
	u32 tmp = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, MCP_REG_CPU_STATE);

	DP_INFO(p_hwfn->p_dev, "MCP_REG_CPU_STATE: %08x - Masking...\n",
		tmp);
	ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt, MCP_REG_CPU_EVENT_MASK,
		 0xffffffff);

	return ECORE_SUCCESS;
}

#define ECORE_PSWHST_ATTENTION_DISABLED_PF_MASK		(0x3c000)
#define ECORE_PSWHST_ATTENTION_DISABLED_PF_SHIFT	(14)
#define ECORE_PSWHST_ATTENTION_DISABLED_VF_MASK		(0x03fc0)
#define ECORE_PSWHST_ATTENTION_DISABLED_VF_SHIFT	(6)
#define ECORE_PSWHST_ATTENTION_DISABLED_VALID_MASK	(0x00020)
#define ECORE_PSWHST_ATTENTION_DISABLED_VALID_SHIFT	(5)
#define ECORE_PSWHST_ATTENTION_DISABLED_CLIENT_MASK	(0x0001e)
#define ECORE_PSWHST_ATTENTION_DISABLED_CLIENT_SHIFT	(1)
#define ECORE_PSWHST_ATTENTION_DISABLED_WRITE_MASK	(0x1)
#define ECORE_PSWHST_ATTNETION_DISABLED_WRITE_SHIFT	(0)
#define ECORE_PSWHST_ATTENTION_VF_DISABLED		(0x1)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS		(0x1)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_WR_MASK 	(0x1)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_WR_SHIFT	(0)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_CLIENT_MASK	(0x1e)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_CLIENT_SHIFT	(1)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_VALID_MASK	(0x20)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_VALID_SHIFT	(5)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_ID_MASK	(0x3fc0)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_ID_SHIFT	(6)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_PF_ID_MASK	(0x3c000)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_PF_ID_SHIFT	(14)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_BYTE_EN_MASK	(0x3fc0000)
#define ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_BYTE_EN_SHIFT	(18)
static enum _ecore_status_t ecore_pswhst_attn_cb(struct ecore_hwfn *p_hwfn)
{
	u32 tmp = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, PSWHST_REG_VF_DISABLED_ERROR_VALID);

	/* Disabled VF access */
	if (tmp & ECORE_PSWHST_ATTENTION_VF_DISABLED) {
		u32 addr, data;

		addr = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				PSWHST_REG_VF_DISABLED_ERROR_ADDRESS);
		data = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				PSWHST_REG_VF_DISABLED_ERROR_DATA);
		DP_INFO(p_hwfn->p_dev, "PF[0x%02x] VF [0x%02x] [Valid 0x%02x] Client [0x%02x] Write [0x%02x] Addr [0x%08x]\n",
			(u8)((data & ECORE_PSWHST_ATTENTION_DISABLED_PF_MASK) >>
			     ECORE_PSWHST_ATTENTION_DISABLED_PF_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_DISABLED_VF_MASK) >>
			     ECORE_PSWHST_ATTENTION_DISABLED_VF_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_DISABLED_VALID_MASK) >>
			     ECORE_PSWHST_ATTENTION_DISABLED_VALID_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_DISABLED_CLIENT_MASK) >>
			     ECORE_PSWHST_ATTENTION_DISABLED_CLIENT_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_DISABLED_WRITE_MASK) >>
			     ECORE_PSWHST_ATTNETION_DISABLED_WRITE_SHIFT),
			addr);
	}

	tmp = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		       PSWHST_REG_INCORRECT_ACCESS_VALID);
	if (tmp & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS) {
		u32 addr, data, length;

		addr = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				PSWHST_REG_INCORRECT_ACCESS_ADDRESS);
		data = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				PSWHST_REG_INCORRECT_ACCESS_DATA);
		length = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				  PSWHST_REG_INCORRECT_ACCESS_LENGTH);

		DP_INFO(p_hwfn->p_dev, "Incorrect access to %08x of length %08x - PF [%02x] VF [%04x] [valid %02x] client [%02x] write [%02x] Byte-Enable [%04x] [%08x]\n",
			addr, length,
			(u8)((data & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_PF_ID_MASK) >>
			     ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_PF_ID_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_ID_MASK) >>
			     ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_ID_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_VALID_MASK) >>
			     ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_VF_VALID_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_CLIENT_MASK) >>
			     ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_CLIENT_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_WR_MASK) >>
			     ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_WR_SHIFT),
			(u8)((data & ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_BYTE_EN_MASK) >>
			     ECORE_PSWHST_ATTENTION_INCORRECT_ACCESS_BYTE_EN_SHIFT),
			data);
	}

	/* TODO - We know 'some' of these are legal due to virtualization,
	 * but is it true for all of them?
	 */
	return ECORE_SUCCESS;
}

#define ECORE_GRC_ATTENTION_VALID_BIT		(1 << 0)
#define ECORE_GRC_ATTENTION_ADDRESS_MASK	(0x7fffff << 0)
#define ECORE_GRC_ATTENTION_RDWR_BIT		(1 << 23)
#define ECORE_GRC_ATTENTION_MASTER_MASK		(0xf << 24)
#define ECORE_GRC_ATTENTION_MASTER_SHIFT	(24)
#define ECORE_GRC_ATTENTION_PF_MASK		(0xf)
#define ECORE_GRC_ATTENTION_VF_MASK		(0xff << 4)
#define ECORE_GRC_ATTENTION_VF_SHIFT		(4)
#define ECORE_GRC_ATTENTION_PRIV_MASK		(0x3 << 14)
#define ECORE_GRC_ATTENTION_PRIV_SHIFT		(14)
#define ECORE_GRC_ATTENTION_PRIV_VF		(0)
static const char* grc_timeout_attn_master_to_str(u8 master)
{
	switch(master) {
	case 1: return "PXP";
	case 2: return "MCP";
	case 3: return "MSDM";
	case 4: return "PSDM";
	case 5: return "YSDM";
	case 6: return "USDM";
	case 7: return "TSDM";
	case 8: return "XSDM";
	case 9: return "DBU";
	case 10: return "DMAE";
	default:
		return "Unkown";
	}
}

static enum _ecore_status_t ecore_grc_attn_cb(struct ecore_hwfn *p_hwfn)
{
	u32 tmp, tmp2;

	/* We've already cleared the timeout interrupt register, so we learn
	 * of interrupts via the validity register
	 */
	tmp = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		       GRC_REG_TIMEOUT_ATTN_ACCESS_VALID);
	if (!(tmp & ECORE_GRC_ATTENTION_VALID_BIT))
		goto out;

	/* Read the GRC timeout information */
	tmp = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
		       GRC_REG_TIMEOUT_ATTN_ACCESS_DATA_0);
	tmp2 = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			GRC_REG_TIMEOUT_ATTN_ACCESS_DATA_1);

	DP_NOTICE(p_hwfn->p_dev, false,
		  "GRC timeout [%08x:%08x] - %s Address [%08x] [Master %s] [PF: %02x %s %02x]\n",
		  tmp2, tmp,
		  (tmp & ECORE_GRC_ATTENTION_RDWR_BIT) ? "Write to"
						       : "Read from",
		  (tmp & ECORE_GRC_ATTENTION_ADDRESS_MASK) << 2,
		  grc_timeout_attn_master_to_str((tmp & ECORE_GRC_ATTENTION_MASTER_MASK) >>
						 ECORE_GRC_ATTENTION_MASTER_SHIFT),
		  (tmp2 & ECORE_GRC_ATTENTION_PF_MASK),
		  (((tmp2 & ECORE_GRC_ATTENTION_PRIV_MASK) >>
		  ECORE_GRC_ATTENTION_PRIV_SHIFT) ==
		  ECORE_GRC_ATTENTION_PRIV_VF) ? "VF" : "(Irrelevant:)",
		  (tmp2 & ECORE_GRC_ATTENTION_VF_MASK) >>
		  ECORE_GRC_ATTENTION_VF_SHIFT);

out:
	/* Regardles of anything else, clean the validity bit */
	ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt,
		 GRC_REG_TIMEOUT_ATTN_ACCESS_VALID, 0);
	return ECORE_SUCCESS;
}

#define ECORE_PGLUE_ATTENTION_VALID (1 << 29)
#define ECORE_PGLUE_ATTENTION_RD_VALID (1 << 26)
#define ECORE_PGLUE_ATTENTION_DETAILS_PFID_MASK (0xf << 20)
#define ECORE_PGLUE_ATTENTION_DETAILS_PFID_SHIFT (20)
#define ECORE_PGLUE_ATTENTION_DETAILS_VF_VALID (1 << 19)
#define ECORE_PGLUE_ATTENTION_DETAILS_VFID_MASK (0xff << 24)
#define ECORE_PGLUE_ATTENTION_DETAILS_VFID_SHIFT (24)
#define ECORE_PGLUE_ATTENTION_DETAILS2_WAS_ERR (1 << 21)
#define ECORE_PGLUE_ATTENTION_DETAILS2_BME	(1 << 22)
#define ECORE_PGLUE_ATTENTION_DETAILS2_FID_EN (1 << 23)
#define ECORE_PGLUE_ATTENTION_ICPL_VALID (1 << 23)
#define ECORE_PGLUE_ATTENTION_ZLR_VALID (1 << 25)
#define ECORE_PGLUE_ATTENTION_ILT_VALID (1 << 23)

enum _ecore_status_t ecore_pglueb_rbc_attn_handler(struct ecore_hwfn *p_hwfn,
						   struct ecore_ptt *p_ptt)
{
	u32 tmp;

	tmp = ecore_rd(p_hwfn, p_ptt, PGLUE_B_REG_TX_ERR_WR_DETAILS2);
	if (tmp & ECORE_PGLUE_ATTENTION_VALID) {
		u32 addr_lo, addr_hi, details;

		addr_lo = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_TX_ERR_WR_ADD_31_0);
		addr_hi = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_TX_ERR_WR_ADD_63_32);
		details = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_TX_ERR_WR_DETAILS);

		DP_NOTICE(p_hwfn, false,
			  "Illegal write by chip to [%08x:%08x] blocked. Details: %08x [PFID %02x, VFID %02x, VF_VALID %02x] Details2 %08x [Was_error %02x BME deassert %02x FID_enable deassert %02x]\n",
			  addr_hi, addr_lo, details,
			  (u8)((details & ECORE_PGLUE_ATTENTION_DETAILS_PFID_MASK) >> ECORE_PGLUE_ATTENTION_DETAILS_PFID_SHIFT),
			  (u8)((details & ECORE_PGLUE_ATTENTION_DETAILS_VFID_MASK) >> ECORE_PGLUE_ATTENTION_DETAILS_VFID_SHIFT),
			  (u8)((details & ECORE_PGLUE_ATTENTION_DETAILS_VF_VALID) ? 1 : 0),
			  tmp,
			  (u8)((tmp & ECORE_PGLUE_ATTENTION_DETAILS2_WAS_ERR) ? 1 : 0),
			  (u8)((tmp & ECORE_PGLUE_ATTENTION_DETAILS2_BME) ? 1 : 0),
			  (u8)((tmp & ECORE_PGLUE_ATTENTION_DETAILS2_FID_EN) ? 1 : 0));
	}

	tmp = ecore_rd(p_hwfn, p_ptt, PGLUE_B_REG_TX_ERR_RD_DETAILS2);
	if (tmp & ECORE_PGLUE_ATTENTION_RD_VALID) {
		u32 addr_lo, addr_hi, details;

		addr_lo = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_TX_ERR_RD_ADD_31_0);
		addr_hi = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_TX_ERR_RD_ADD_63_32);
		details = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_TX_ERR_RD_DETAILS);

		DP_NOTICE(p_hwfn, false,
			  "Illegal read by chip from [%08x:%08x] blocked. Details: %08x [PFID %02x, VFID %02x, VF_VALID %02x] Details2 %08x [Was_error %02x BME deassert %02x FID_enable deassert %02x]\n",
			  addr_hi, addr_lo, details,
			  (u8)((details & ECORE_PGLUE_ATTENTION_DETAILS_PFID_MASK) >> ECORE_PGLUE_ATTENTION_DETAILS_PFID_SHIFT),
			  (u8)((details & ECORE_PGLUE_ATTENTION_DETAILS_VFID_MASK) >> ECORE_PGLUE_ATTENTION_DETAILS_VFID_SHIFT),
			  (u8)((details & ECORE_PGLUE_ATTENTION_DETAILS_VF_VALID) ? 1 : 0),
			  tmp,
			  (u8)((tmp & ECORE_PGLUE_ATTENTION_DETAILS2_WAS_ERR) ? 1 : 0),
			  (u8)((tmp & ECORE_PGLUE_ATTENTION_DETAILS2_BME) ? 1 : 0),
			  (u8)((tmp & ECORE_PGLUE_ATTENTION_DETAILS2_FID_EN) ? 1 : 0));
	}

	tmp = ecore_rd(p_hwfn, p_ptt, PGLUE_B_REG_TX_ERR_WR_DETAILS_ICPL);
	if (tmp & ECORE_PGLUE_ATTENTION_ICPL_VALID)
		DP_NOTICE(p_hwfn, false, "ICPL eror - %08x\n", tmp);

	tmp = ecore_rd(p_hwfn, p_ptt, PGLUE_B_REG_MASTER_ZLR_ERR_DETAILS);
	if (tmp & ECORE_PGLUE_ATTENTION_ZLR_VALID) {
		u32 addr_hi, addr_lo;

		addr_lo = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_MASTER_ZLR_ERR_ADD_31_0);
		addr_hi = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_MASTER_ZLR_ERR_ADD_63_32);

		DP_NOTICE(p_hwfn, false,
			  "ICPL eror - %08x [Address %08x:%08x]\n",
			  tmp, addr_hi, addr_lo);
	}

	tmp = ecore_rd(p_hwfn, p_ptt, PGLUE_B_REG_VF_ILT_ERR_DETAILS2);
	if (tmp & ECORE_PGLUE_ATTENTION_ILT_VALID) {
		u32 addr_hi, addr_lo, details;

		addr_lo = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_VF_ILT_ERR_ADD_31_0);
		addr_hi = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_VF_ILT_ERR_ADD_63_32);
		details = ecore_rd(p_hwfn, p_ptt,
				   PGLUE_B_REG_VF_ILT_ERR_DETAILS);

		DP_NOTICE(p_hwfn, false,
			  "ILT error - Details %08x Details2 %08x [Address %08x:%08x]\n",
			  details, tmp, addr_hi, addr_lo);
	}

	/* Clear the indications */
	ecore_wr(p_hwfn, p_ptt, PGLUE_B_REG_LATCHED_ERRORS_CLR, (1 << 2));

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_pglueb_rbc_attn_cb(struct ecore_hwfn *p_hwfn)
{
	return ecore_pglueb_rbc_attn_handler(p_hwfn, p_hwfn->p_dpc_ptt);
}

static enum _ecore_status_t ecore_fw_assertion(struct ecore_hwfn *p_hwfn)
{
	DP_NOTICE(p_hwfn, false, "FW assertion!\n");

	ecore_hw_err_notify(p_hwfn, ECORE_HW_ERR_FW_ASSERT);

	return ECORE_INVAL;
}

static enum _ecore_status_t
ecore_general_attention_35(struct ecore_hwfn *p_hwfn)
{
	DP_INFO(p_hwfn, "General attention 35!\n");

	return ECORE_SUCCESS;
}

#define ECORE_DORQ_ATTENTION_REASON_MASK	(0xfffff)
#define ECORE_DORQ_ATTENTION_OPAQUE_MASK	(0xffff)
#define ECORE_DORQ_ATTENTION_OPAQUE_SHIFT	(0x0)
#define ECORE_DORQ_ATTENTION_SIZE_MASK		(0x7f)
#define ECORE_DORQ_ATTENTION_SIZE_SHIFT		(16)

#define ECORE_DB_REC_COUNT			10
#define ECORE_DB_REC_INTERVAL			100

/* assumes sticky overflow indication was set for this PF */
static enum _ecore_status_t ecore_db_rec_attn(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt)
{
	u8 count = ECORE_DB_REC_COUNT;
	u32 usage = 1;

	/* wait for usage to zero or count to run out. This is necessary since
	 * EDPM doorbell transactions can take multiple 64b cycles, and as such
	 * can "split" over the pci. Possibly, the doorbell drop can happen with
	 * half an EDPM in the queue and other half dropped. Another EDPM
	 * doorbell to the same address (from doorbell recovery mechanism or
	 * from the doorbelling entity) could have first half dropped and second
	 * half interperted as continuation of the first. To prevent such
	 * malformed doorbells from reaching the device, flush the queue before
	 * releaseing the overflow sticky indication.
	 */
	while (count-- && usage) {
		usage = ecore_rd(p_hwfn, p_ptt, DORQ_REG_PF_USAGE_CNT);
		OSAL_UDELAY(ECORE_DB_REC_INTERVAL);
	}

	/* should have been depleted by now */
	if (usage) {
		DP_NOTICE(p_hwfn->p_dev, false,
			  "DB recovery: doorbell usage failed to zero after %d usec. usage was %x\n",
			  ECORE_DB_REC_INTERVAL * ECORE_DB_REC_COUNT, usage);
		return ECORE_TIMEOUT;
	}

	/* flush any pedning (e)dpm as they may never arrive */
	ecore_wr(p_hwfn, p_ptt, DORQ_REG_DPM_FORCE_ABORT, 0x1);

	/* release overflow sticky indication (stop silently dropping everything) */
	ecore_wr(p_hwfn, p_ptt, DORQ_REG_PF_OVFL_STICKY, 0x0);

	/* repeat all last doorbells (doorbell drop recovery) */
	ecore_db_recovery_execute(p_hwfn, DB_REC_REAL_DEAL);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_dorq_attn_cb(struct ecore_hwfn *p_hwfn)
{
	u32 int_sts, first_drop_reason, details, address, overflow,
		all_drops_reason;
	struct ecore_ptt *p_ptt = p_hwfn->p_dpc_ptt;
	enum _ecore_status_t rc;

	int_sts = ecore_rd(p_hwfn, p_ptt, DORQ_REG_INT_STS);
	DP_NOTICE(p_hwfn->p_dev, false, "DORQ attention. int_sts was %x\n",
		  int_sts);

	/* int_sts may be zero since all PFs were interrupted for doorbell
	 * overflow but another one already handled it. Can abort here. If
	 * This PF also requires overflow recovery we will be interrupted again.
	 * The masked almost full indication may also be set. Ignoring.
	 */
	if (!(int_sts & ~DORQ_REG_INT_STS_DORQ_FIFO_AFULL))
		return ECORE_SUCCESS;

	/* check if db_drop or overflow happened */
	if (int_sts & (DORQ_REG_INT_STS_DB_DROP |
		       DORQ_REG_INT_STS_DORQ_FIFO_OVFL_ERR)) {
	
		/* obtain data about db drop/overflow */
		first_drop_reason = ecore_rd(p_hwfn, p_ptt,
				  DORQ_REG_DB_DROP_REASON) &
				  ECORE_DORQ_ATTENTION_REASON_MASK;
		details = ecore_rd(p_hwfn, p_ptt,
				   DORQ_REG_DB_DROP_DETAILS);
		address = ecore_rd(p_hwfn, p_ptt,
				   DORQ_REG_DB_DROP_DETAILS_ADDRESS);
		overflow = ecore_rd(p_hwfn, p_ptt,
				    DORQ_REG_PF_OVFL_STICKY);
		all_drops_reason = ecore_rd(p_hwfn, p_ptt,
					    DORQ_REG_DB_DROP_DETAILS_REASON);

		/* log info */
		DP_NOTICE(p_hwfn->p_dev, false,
			  "Doorbell drop occurred\n"
			  "Address\t\t0x%08x\t(second BAR address)\n"
			  "FID\t\t0x%04x\t\t(Opaque FID)\n"
			  "Size\t\t0x%04x\t\t(in bytes)\n"
			  "1st drop reason\t0x%08x\t(details on first drop since last handling)\n"
			  "Sticky reasons\t0x%08x\t(all drop reasons since last handling)\n"
			  "Overflow\t0x%x\t\t(a per PF indication)\n",
			  address, GET_FIELD(details, ECORE_DORQ_ATTENTION_OPAQUE),
			  GET_FIELD(details, ECORE_DORQ_ATTENTION_SIZE) * 4,
			  first_drop_reason, all_drops_reason, overflow);

		/* if this PF caused overflow, initiate recovery */
		if (overflow) {
			rc = ecore_db_rec_attn(p_hwfn, p_ptt);
			if (rc != ECORE_SUCCESS)
				return rc;
		}

		/* clear the doorbell drop details and prepare for next drop */
		ecore_wr(p_hwfn, p_ptt, DORQ_REG_DB_DROP_DETAILS_REL, 0);

		/* mark interrupt as handeld (note: even if drop was due to a diffrent
		 * reason than overflow we mark as handled)
		 */
		ecore_wr(p_hwfn, p_ptt, DORQ_REG_INT_STS_WR,
			 DORQ_REG_INT_STS_DB_DROP | DORQ_REG_INT_STS_DORQ_FIFO_OVFL_ERR);

		/* if there are no indications otherthan drop indications, success */
		if ((int_sts & ~(DORQ_REG_INT_STS_DB_DROP |
				 DORQ_REG_INT_STS_DORQ_FIFO_OVFL_ERR |
				 DORQ_REG_INT_STS_DORQ_FIFO_AFULL)) == 0)
			return ECORE_SUCCESS;
	}

	/* some other indication was present - non recoverable */
	DP_INFO(p_hwfn, "DORQ fatal attention\n");

	return ECORE_INVAL;
}

static enum _ecore_status_t ecore_tm_attn_cb(struct ecore_hwfn *p_hwfn)
{
#ifndef ASIC_ONLY
	if (CHIP_REV_IS_EMUL_B0(p_hwfn->p_dev)) {
		u32 val = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
				   TM_REG_INT_STS_1);

		if (val & ~(TM_REG_INT_STS_1_PEND_TASK_SCAN |
			    TM_REG_INT_STS_1_PEND_CONN_SCAN))
			return ECORE_INVAL;

		if (val & (TM_REG_INT_STS_1_PEND_TASK_SCAN |
			   TM_REG_INT_STS_1_PEND_CONN_SCAN))
			DP_INFO(p_hwfn, "TM attention on emulation - most likely results of clock-ratios\n");
		val = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, TM_REG_INT_MASK_1);
		val |= TM_REG_INT_MASK_1_PEND_CONN_SCAN |
		       TM_REG_INT_MASK_1_PEND_TASK_SCAN;
		ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt, TM_REG_INT_MASK_1, val);

		return ECORE_SUCCESS;
	}
#endif

	return ECORE_INVAL;
}

/* Instead of major changes to the data-structure, we have a some 'special'
 * identifiers for sources that changed meaning between adapters.
 */
enum aeu_invert_reg_special_type {
	AEU_INVERT_REG_SPECIAL_CNIG_0,
	AEU_INVERT_REG_SPECIAL_CNIG_1,
	AEU_INVERT_REG_SPECIAL_CNIG_2,
	AEU_INVERT_REG_SPECIAL_CNIG_3,
	AEU_INVERT_REG_SPECIAL_MAX,
};

static struct aeu_invert_reg_bit
aeu_descs_special[AEU_INVERT_REG_SPECIAL_MAX] = {
	{"CNIG port 0", ATTENTION_SINGLE, OSAL_NULL, BLOCK_CNIG},
	{"CNIG port 1", ATTENTION_SINGLE, OSAL_NULL, BLOCK_CNIG},
	{"CNIG port 2", ATTENTION_SINGLE, OSAL_NULL, BLOCK_CNIG},
	{"CNIG port 3", ATTENTION_SINGLE, OSAL_NULL, BLOCK_CNIG},
};

/* Notice aeu_invert_reg must be defined in the same order of bits as HW; */
static struct aeu_invert_reg aeu_descs[NUM_ATTN_REGS] =
{
	{
		{	/* After Invert 1 */
			{"GPIO0 function%d", (32 << ATTENTION_LENGTH_SHIFT), OSAL_NULL, MAX_BLOCK_ID},
		}
	},

	{
		{	/* After Invert 2 */
			{"PGLUE config_space", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"PGLUE misc_flr", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"PGLUE B RBC", ATTENTION_PAR_INT, ecore_pglueb_rbc_attn_cb, BLOCK_PGLUE_B},
			{"PGLUE misc_mctp", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"Flash event", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"SMB event", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"Main Power", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"SW timers #%d", (8 << ATTENTION_LENGTH_SHIFT) | (1 << ATTENTION_OFFSET_SHIFT), OSAL_NULL, MAX_BLOCK_ID},
			{"PCIE glue/PXP VPD %d", (16 << ATTENTION_LENGTH_SHIFT), OSAL_NULL, BLOCK_PGLCS},
		}
	},

	{
		{	/* After Invert 3 */
			{"General Attention %d", (32 << ATTENTION_LENGTH_SHIFT), OSAL_NULL, MAX_BLOCK_ID},
		}
	},

	{
		{	/* After Invert 4 */
			{"General Attention 32", ATTENTION_SINGLE | ATTENTION_CLEAR_ENABLE, ecore_fw_assertion, MAX_BLOCK_ID},
			{"General Attention %d", (2 << ATTENTION_LENGTH_SHIFT) | (33 << ATTENTION_OFFSET_SHIFT), OSAL_NULL, MAX_BLOCK_ID},
			{"General Attention 35", ATTENTION_SINGLE | ATTENTION_CLEAR_ENABLE, ecore_general_attention_35, MAX_BLOCK_ID},
			{"NWS Parity", ATTENTION_PAR | ATTENTION_BB_DIFFERENT |
				       ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_0) , OSAL_NULL, BLOCK_NWS},
			{"NWS Interrupt", ATTENTION_SINGLE | ATTENTION_BB_DIFFERENT |
					  ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_1), OSAL_NULL, BLOCK_NWS},
			{"NWM Parity", ATTENTION_PAR | ATTENTION_BB_DIFFERENT |
				       ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_2), OSAL_NULL, BLOCK_NWM},
			{"NWM Interrupt", ATTENTION_SINGLE | ATTENTION_BB_DIFFERENT |
					  ATTENTION_BB(AEU_INVERT_REG_SPECIAL_CNIG_3), OSAL_NULL, BLOCK_NWM},
			{"MCP CPU", ATTENTION_SINGLE, ecore_mcp_attn_cb, MAX_BLOCK_ID},
			{"MCP Watchdog timer", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"MCP M2P", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"AVS stop status ready", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"MSTAT", ATTENTION_PAR_INT, OSAL_NULL, MAX_BLOCK_ID},
			{"MSTAT per-path", ATTENTION_PAR_INT, OSAL_NULL, MAX_BLOCK_ID},
			{"Reserved %d", (6 << ATTENTION_LENGTH_SHIFT), OSAL_NULL, MAX_BLOCK_ID },
			{"NIG", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_NIG},
			{"BMB/OPTE/MCP", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_BMB},
			{"BTB", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_BTB},
			{"BRB", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_BRB},
			{"PRS", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PRS},
		}
	},

	{
		{	/* After Invert 5 */
			{"SRC", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_SRC},
			{"PB Client1", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PBF_PB1},
			{"PB Client2", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PBF_PB2},
			{"RPB", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_RPB},
			{"PBF", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PBF},
			{"QM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_QM},
			{"TM", ATTENTION_PAR_INT, ecore_tm_attn_cb, BLOCK_TM},
			{"MCM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_MCM},
			{"MSDM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_MSDM},
			{"MSEM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_MSEM},
			{"PCM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PCM},
			{"PSDM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSDM},
			{"PSEM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSEM},
			{"TCM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_TCM},
			{"TSDM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_TSDM},
			{"TSEM", ATTENTION_PAR_INT,  OSAL_NULL, BLOCK_TSEM},
		}
	},

	{
		{	/* After Invert 6 */
			{"UCM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_UCM},
			{"USDM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_USDM},
			{"USEM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_USEM},
			{"XCM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_XCM},
			{"XSDM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_XSDM},
			{"XSEM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_XSEM},
			{"YCM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_YCM},
			{"YSDM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_YSDM},
			{"YSEM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_YSEM},
			{"XYLD", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_XYLD},
			{"TMLD", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_TMLD},
			{"MYLD", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_MULD},
			{"YULD", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_YULD},
			{"DORQ", ATTENTION_PAR_INT, ecore_dorq_attn_cb, BLOCK_DORQ},
			{"DBG", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_DBG},
			{"IPC", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_IPC},
		}
	},

	{
		{	/* After Invert 7 */
			{"CCFC", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_CCFC},
			{"CDU", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_CDU},
			{"DMAE", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_DMAE},
			{"IGU", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_IGU},
			{"ATC", ATTENTION_PAR_INT, OSAL_NULL, MAX_BLOCK_ID},
			{"CAU", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_CAU},
			{"PTU", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PTU},
			{"PRM", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PRM},
			{"TCFC", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_TCFC},
			{"RDIF", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_RDIF},
			{"TDIF", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_TDIF},
			{"RSS", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_RSS},
			{"MISC", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_MISC},
			{"MISCS", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_MISCS},
			{"PCIE", ATTENTION_PAR, OSAL_NULL, BLOCK_PCIE},
			{"Vaux PCI core", ATTENTION_SINGLE, OSAL_NULL, BLOCK_PGLCS},
			{"PSWRQ", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWRQ},
		}
	},

	{
		{	/* After Invert 8 */
			{"PSWRQ (pci_clk)", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWRQ2},
			{"PSWWR", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWWR},
			{"PSWWR (pci_clk)", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWWR2},
			{"PSWRD", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWRD},
			{"PSWRD (pci_clk)", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWRD2},
			{"PSWHST", ATTENTION_PAR_INT, ecore_pswhst_attn_cb, BLOCK_PSWHST},
			{"PSWHST (pci_clk)", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_PSWHST2},
			{"GRC", ATTENTION_PAR_INT, ecore_grc_attn_cb, BLOCK_GRC},
			{"CPMU", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_CPMU},
			{"NCSI", ATTENTION_PAR_INT, OSAL_NULL, BLOCK_NCSI},
			{"MSEM PRAM", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"PSEM PRAM", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"TSEM PRAM", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"USEM PRAM", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"XSEM PRAM", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"YSEM PRAM", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"pxp_misc_mps", ATTENTION_PAR, OSAL_NULL, BLOCK_PGLCS},
			{"PCIE glue/PXP Exp. ROM", ATTENTION_SINGLE, OSAL_NULL, BLOCK_PGLCS},
			{"PERST_B assertion", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"PERST_B deassertion", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"Reserved %d", (2 << ATTENTION_LENGTH_SHIFT), OSAL_NULL, MAX_BLOCK_ID },
		}
	},

	{
		{	/* After Invert 9 */
			{"MCP Latched memory", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"MCP Latched scratchpad cache", ATTENTION_SINGLE, OSAL_NULL, MAX_BLOCK_ID},
			{"MCP Latched ump_tx", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"MCP Latched scratchpad", ATTENTION_PAR, OSAL_NULL, MAX_BLOCK_ID},
			{"Reserved %d", (28 << ATTENTION_LENGTH_SHIFT), OSAL_NULL, MAX_BLOCK_ID },
		}
	},

};

static struct aeu_invert_reg_bit *
ecore_int_aeu_translate(struct ecore_hwfn *p_hwfn,
			struct aeu_invert_reg_bit *p_bit)
{
	if (!ECORE_IS_BB(p_hwfn->p_dev))
		return p_bit;

	if (!(p_bit->flags & ATTENTION_BB_DIFFERENT))
		return p_bit;

	return &aeu_descs_special[(p_bit->flags & ATTENTION_BB_MASK) >>
				  ATTENTION_BB_SHIFT];
}

static bool ecore_int_is_parity_flag(struct ecore_hwfn *p_hwfn,
				     struct aeu_invert_reg_bit *p_bit)
{
	return !!(ecore_int_aeu_translate(p_hwfn, p_bit)->flags &
		  ATTENTION_PARITY);
}

#define ATTN_STATE_BITS		(0xfff)
#define ATTN_BITS_MASKABLE	(0x3ff)
struct ecore_sb_attn_info {
	/* Virtual & Physical address of the SB */
	struct atten_status_block	*sb_attn;
	dma_addr_t			sb_phys;

	/* Last seen running index */
	u16				index;

	/* A mask of the AEU bits resulting in a parity error */
	u32				parity_mask[NUM_ATTN_REGS];

	/* A pointer to the attention description structure */
	struct aeu_invert_reg		*p_aeu_desc;

	/* Previously asserted attentions, which are still unasserted */
	u16				known_attn;

	/* Cleanup address for the link's general hw attention */
	u32				mfw_attn_addr;
};

static u16 ecore_attn_update_idx(struct ecore_hwfn *p_hwfn,
				 struct ecore_sb_attn_info *p_sb_desc)
{
	u16 rc = 0, index;

	OSAL_MMIOWB(p_hwfn->p_dev);

	index = OSAL_LE16_TO_CPU(p_sb_desc->sb_attn->sb_index);
	if (p_sb_desc->index != index) {
		p_sb_desc->index = index;
		rc = ECORE_SB_ATT_IDX;
	}

	OSAL_MMIOWB(p_hwfn->p_dev);

	return rc;
}

/**
 * @brief ecore_int_assertion - handles asserted attention bits
 *
 * @param p_hwfn
 * @param asserted_bits newly asserted bits
 * @return enum _ecore_status_t
 */
static enum _ecore_status_t ecore_int_assertion(struct ecore_hwfn *p_hwfn,
						u16 asserted_bits)
{
	struct ecore_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 igu_mask;

	/* Mask the source of the attention in the IGU */
	igu_mask = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			    IGU_REG_ATTENTION_ENABLE);
	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR, "IGU mask: 0x%08x --> 0x%08x\n",
		   igu_mask, igu_mask & ~(asserted_bits & ATTN_BITS_MASKABLE));
	igu_mask &= ~(asserted_bits & ATTN_BITS_MASKABLE);
	ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, igu_mask);

	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
		   "inner known ATTN state: 0x%04x --> 0x%04x\n",
		   sb_attn_sw->known_attn,
		   sb_attn_sw->known_attn | asserted_bits);
	sb_attn_sw->known_attn |= asserted_bits;

	/* Handle MCP events */
	if (asserted_bits & 0x100) {
		ecore_mcp_handle_events(p_hwfn, p_hwfn->p_dpc_ptt);
		/* Clean the MCP attention */
		ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt,
			 sb_attn_sw->mfw_attn_addr, 0);
	}

	/* FIXME - this will change once we'll have GOOD gtt definitions */
	DIRECT_REG_WR(p_hwfn,
		      (u8 OSAL_IOMEM*)p_hwfn->regview +
		      GTT_BAR0_MAP_REG_IGU_CMD +
		      ((IGU_CMD_ATTN_BIT_SET_UPPER -
			IGU_CMD_INT_ACK_BASE) << 3), (u32)asserted_bits);

	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR, "set cmd IGU: 0x%04x\n",
		   asserted_bits);

	return ECORE_SUCCESS;
}

static void ecore_int_attn_print(struct ecore_hwfn *p_hwfn,
				 enum block_id id, enum dbg_attn_type type,
				 bool b_clear)
{
	struct dbg_attn_block_result attn_results;
	enum dbg_status status;

	OSAL_MEMSET(&attn_results, 0, sizeof(attn_results));

	status = ecore_dbg_read_attn(p_hwfn, p_hwfn->p_dpc_ptt, id, type,
				     b_clear, &attn_results);
#ifdef ATTN_DESC
	if (status != DBG_STATUS_OK)
		DP_NOTICE(p_hwfn, true,
			  "Failed to parse attention information [status: %s]\n",
			  ecore_dbg_get_status_str(status));
	else
		ecore_dbg_parse_attn(p_hwfn, &attn_results);
#else
	if (status != DBG_STATUS_OK)
		DP_NOTICE(p_hwfn, true,
			  "Failed to parse attention information [status: %d]\n",
			  status);
	else
		ecore_dbg_print_attn(p_hwfn, &attn_results);
#endif
}

/**
 * @brief ecore_int_deassertion_aeu_bit - handles the effects of a single
 * cause of the attention
 *
 * @param p_hwfn
 * @param p_aeu - descriptor of an AEU bit which caused the attention
 * @param aeu_en_reg - register offset of the AEU enable reg. which configured
 *  this bit to this group.
 * @param bit_index - index of this bit in the aeu_en_reg
 *
 * @return enum _ecore_status_t
 */
static enum _ecore_status_t
ecore_int_deassertion_aeu_bit(struct ecore_hwfn *p_hwfn,
			      struct aeu_invert_reg_bit *p_aeu,
			      u32 aeu_en_reg,
			      const char *p_bit_name,
			      u32 bitmask)
{
	enum _ecore_status_t rc = ECORE_INVAL;
	bool b_fatal = false;

	DP_INFO(p_hwfn, "Deasserted attention `%s'[%08x]\n",
		p_bit_name, bitmask);

	/* Call callback before clearing the interrupt status */
	if (p_aeu->cb) {
		DP_INFO(p_hwfn, "`%s (attention)': Calling Callback function\n",
			p_bit_name);
		rc = p_aeu->cb(p_hwfn);
	}

	if (rc != ECORE_SUCCESS)
		b_fatal = true;

	/* Print HW block interrupt registers */
	if (p_aeu->block_index != MAX_BLOCK_ID)
		ecore_int_attn_print(p_hwfn, p_aeu->block_index,
				     ATTN_TYPE_INTERRUPT, !b_fatal);

	/* Reach assertion if attention is fatal */
	if (b_fatal) {
		DP_NOTICE(p_hwfn, true, "`%s': Fatal attention\n",
			  p_bit_name);

		ecore_hw_err_notify(p_hwfn, ECORE_HW_ERR_HW_ATTN);
	}

	/* Prevent this Attention from being asserted in the future */
	if (p_aeu->flags & ATTENTION_CLEAR_ENABLE ||
	    p_hwfn->p_dev->attn_clr_en) {
		u32 val;
		u32 mask = ~bitmask;
		val = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg);
		ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg, (val & mask));
		DP_INFO(p_hwfn, "`%s' - Disabled future attentions\n",
			p_bit_name);
	}

	return rc;
}

/**
 * @brief ecore_int_deassertion_parity - handle a single parity AEU source
 *
 * @param p_hwfn
 * @param p_aeu - descriptor of an AEU bit which caused the parity
 * @param aeu_en_reg - address of the AEU enable register
 * @param bit_index
 */
static void ecore_int_deassertion_parity(struct ecore_hwfn *p_hwfn,
					 struct aeu_invert_reg_bit *p_aeu,
					 u32 aeu_en_reg, u8 bit_index)
{
	u32 block_id = p_aeu->block_index, mask, val;

	DP_NOTICE(p_hwfn->p_dev, false,
		  "%s parity attention is set [address 0x%08x, bit %d]\n",
		  p_aeu->bit_name, aeu_en_reg, bit_index);

	if (block_id != MAX_BLOCK_ID) {
		ecore_int_attn_print(p_hwfn, block_id, ATTN_TYPE_PARITY, false);

		/* In A0, there's a single parity bit for several blocks */
		if (block_id == BLOCK_BTB) {
			ecore_int_attn_print(p_hwfn, BLOCK_OPTE,
					     ATTN_TYPE_PARITY, false);
			ecore_int_attn_print(p_hwfn, BLOCK_MCP,
					     ATTN_TYPE_PARITY, false);
		}
	}

	/* Prevent this parity error from being re-asserted */
	mask = ~(0x1 << bit_index);
	val = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg);
	ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en_reg, val & mask);
	DP_INFO(p_hwfn, "`%s' - Disabled future parity errors\n",
		p_aeu->bit_name);
}

/**
 * @brief - handles deassertion of previously asserted attentions.
 *
 * @param p_hwfn
 * @param deasserted_bits - newly deasserted bits
 * @return enum _ecore_status_t
 *
 */
static enum _ecore_status_t ecore_int_deassertion(struct ecore_hwfn *p_hwfn,
						  u16 deasserted_bits)
{
	struct ecore_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 aeu_inv_arr[NUM_ATTN_REGS], aeu_mask, aeu_en, en;
	u8 i, j, k, bit_idx;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	/* Read the attention registers in the AEU */
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		aeu_inv_arr[i] = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
					  MISC_REG_AEU_AFTER_INVERT_1_IGU +
					  i * 0x4);
		DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
			   "Deasserted bits [%d]: %08x\n",
			   i, aeu_inv_arr[i]);
	}

	/* Handle parity attentions first */
	for (i = 0; i < NUM_ATTN_REGS; i++)
	{
		struct aeu_invert_reg *p_aeu = &sb_attn_sw->p_aeu_desc[i];
		u32 parities;

		aeu_en = MISC_REG_AEU_ENABLE1_IGU_OUT_0 + i * sizeof(u32);
		en = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en);
		parities = sb_attn_sw->parity_mask[i] & aeu_inv_arr[i] & en;

		/* Skip register in which no parity bit is currently set */
		if (!parities)
			continue;

		for (j = 0, bit_idx = 0; bit_idx < 32; j++) {
			struct aeu_invert_reg_bit *p_bit = &p_aeu->bits[j];

			if (ecore_int_is_parity_flag(p_hwfn, p_bit) &&
			    !!(parities & (1 << bit_idx)))
				ecore_int_deassertion_parity(p_hwfn, p_bit,
							     aeu_en, bit_idx);

			bit_idx += ATTENTION_LENGTH(p_bit->flags);
		}
	}

	/* Find non-parity cause for attention and act */
	for (k = 0; k < MAX_ATTN_GRPS; k++) {
		struct aeu_invert_reg_bit *p_aeu;

		/* Handle only groups whose attention is currently deasserted */
		if (!(deasserted_bits & (1 << k)))
			continue;

		for (i = 0; i < NUM_ATTN_REGS; i++) {
			u32 bits;

			aeu_en = MISC_REG_AEU_ENABLE1_IGU_OUT_0 +
				 i * sizeof(u32) +
				 k * sizeof(u32) * NUM_ATTN_REGS;
			en = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt, aeu_en);
			bits = aeu_inv_arr[i] & en;

			/* Skip if no bit from this group is currently set */
			if (!bits)
				continue;

			/* Find all set bits from current register which belong
			 * to current group, making them responsible for the
			 * previous assertion.
			 */
			for (j = 0, bit_idx = 0; bit_idx < 32; j++)
			{
				long unsigned int bitmask;
				u8 bit, bit_len;

				/* Need to account bits with changed meaning */
				p_aeu = &sb_attn_sw->p_aeu_desc[i].bits[j];
				p_aeu = ecore_int_aeu_translate(p_hwfn, p_aeu);

				bit = bit_idx;
				bit_len = ATTENTION_LENGTH(p_aeu->flags);
				if (ecore_int_is_parity_flag(p_hwfn, p_aeu)) {
					/* Skip Parity */
					bit++;
					bit_len--;
				}

				/* Find the bits relating to HW-block, then
				 * shift so they'll become LSB.
				 */
				bitmask = bits & (((1 << bit_len) - 1) << bit);
				bitmask >>= bit;

				if (bitmask) {
					u32 flags = p_aeu->flags;
					char bit_name[30];
					u8 num;

					num = (u8)OSAL_FIND_FIRST_BIT(&bitmask,
								bit_len);

					/* Some bits represent more than a
					 * a single interrupt. Correctly print
					 * their name.
					 */
					if (ATTENTION_LENGTH(flags) > 2 ||
					    ((flags & ATTENTION_PAR_INT) &&
					    ATTENTION_LENGTH(flags) > 1))
						OSAL_SNPRINTF(bit_name, 30,
							      p_aeu->bit_name,
							      num);
					else
						OSAL_STRNCPY(bit_name,
							     p_aeu->bit_name,
							     30);

					/* We now need to pass bitmask in its
					 * correct position.
					 */
					bitmask <<= bit;

					/* Handle source of the attention */
					ecore_int_deassertion_aeu_bit(p_hwfn,
								      p_aeu,
								      aeu_en,
								      bit_name,
								      bitmask);
				}

				bit_idx += ATTENTION_LENGTH(p_aeu->flags);
			}
		}
	}

	/* Clear IGU indication for the deasserted bits */
	/* FIXME - this will change once we'll have GOOD gtt definitions */
	DIRECT_REG_WR(p_hwfn,
		      (u8 OSAL_IOMEM*)p_hwfn->regview +
				      GTT_BAR0_MAP_REG_IGU_CMD +
				      ((IGU_CMD_ATTN_BIT_CLR_UPPER -
					IGU_CMD_INT_ACK_BASE) << 3),
		      ~((u32)deasserted_bits));

	/* Unmask deasserted attentions in IGU */
	aeu_mask = ecore_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			    IGU_REG_ATTENTION_ENABLE);
	aeu_mask |= (deasserted_bits & ATTN_BITS_MASKABLE);
	ecore_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, aeu_mask);

	/* Clear deassertion from inner state */
	sb_attn_sw->known_attn &= ~deasserted_bits;

	return rc;
}

static enum _ecore_status_t ecore_int_attentions(struct ecore_hwfn *p_hwfn)
{
	struct ecore_sb_attn_info *p_sb_attn_sw = p_hwfn->p_sb_attn;
	struct atten_status_block *p_sb_attn = p_sb_attn_sw->sb_attn;
	u16 index = 0, asserted_bits, deasserted_bits;
	u32 attn_bits = 0, attn_acks = 0;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	/* Read current attention bits/acks - safeguard against attentions
	 * by guaranting work on a synchronized timeframe
	 */
	do {
		index = OSAL_LE16_TO_CPU(p_sb_attn->sb_index);
		attn_bits = OSAL_LE32_TO_CPU(p_sb_attn->atten_bits);
		attn_acks = OSAL_LE32_TO_CPU(p_sb_attn->atten_ack);
	} while (index != OSAL_LE16_TO_CPU(p_sb_attn->sb_index));
	p_sb_attn->sb_index = index;

	/* Attention / Deassertion are meaningful (and in correct state)
	 * only when they differ and consistent with known state - deassertion
	 * when previous attention & current ack, and assertion when current
	 * attention with no previous attention
	 */
	asserted_bits = (attn_bits & ~attn_acks & ATTN_STATE_BITS) &
			~p_sb_attn_sw->known_attn;
	deasserted_bits = (~attn_bits & attn_acks & ATTN_STATE_BITS) &
			  p_sb_attn_sw->known_attn;

	if ((asserted_bits & ~0x100) || (deasserted_bits & ~0x100))
		DP_INFO(p_hwfn,
			"Attention: Index: 0x%04x, Bits: 0x%08x, Acks: 0x%08x, asserted: 0x%04x, De-asserted 0x%04x [Prev. known: 0x%04x]\n",
			index, attn_bits, attn_acks, asserted_bits,
			deasserted_bits, p_sb_attn_sw->known_attn);
	else if (asserted_bits == 0x100)
		DP_INFO(p_hwfn,
			"MFW indication via attention\n");
	else
		DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
			   "MFW indication [deassertion]\n");

	if (asserted_bits) {
		rc = ecore_int_assertion(p_hwfn, asserted_bits);
		if (rc)
			return rc;
	}

	if (deasserted_bits)
		rc = ecore_int_deassertion(p_hwfn, deasserted_bits);

	return rc;
}

static void ecore_sb_ack_attn(struct ecore_hwfn *p_hwfn,
			      void OSAL_IOMEM *igu_addr, u32 ack_cons)
{
	struct igu_prod_cons_update igu_ack = { 0 };

	igu_ack.sb_id_and_flags =
		((ack_cons << IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (1 << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (IGU_INT_NOP << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_ATTN <<
		  IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

	DIRECT_REG_WR(p_hwfn, igu_addr, igu_ack.sb_id_and_flags);

	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	OSAL_MMIOWB(p_hwfn->p_dev);
	OSAL_BARRIER(p_hwfn->p_dev);
}

void ecore_int_sp_dpc(osal_int_ptr_t hwfn_cookie)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)hwfn_cookie;
	struct ecore_pi_info *pi_info = OSAL_NULL;
	struct ecore_sb_attn_info *sb_attn;
	struct ecore_sb_info *sb_info;
	int arr_size;
	u16 rc = 0;

	if (!p_hwfn)
		return;

	if (!p_hwfn->p_sp_sb) {
		DP_ERR(p_hwfn->p_dev, "DPC called - no p_sp_sb\n");
		return;
	}

	sb_info = &p_hwfn->p_sp_sb->sb_info;
	arr_size = OSAL_ARRAY_SIZE(p_hwfn->p_sp_sb->pi_info_arr);
	if (!sb_info) {
		DP_ERR(p_hwfn->p_dev, "Status block is NULL - cannot ack interrupts\n");
		return;
	}

	if (!p_hwfn->p_sb_attn) {
		DP_ERR(p_hwfn->p_dev, "DPC called - no p_sb_attn");
		return;
	}
	sb_attn =  p_hwfn->p_sb_attn;

	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR, "DPC Called! (hwfn %p %d)\n",
		   p_hwfn, p_hwfn->my_id);

	/* Disable ack for def status block. Required both for msix +
	 * inta in non-mask mode, in inta does no harm.
	 */
	ecore_sb_ack(sb_info, IGU_INT_DISABLE, 0);

	/* Gather Interrupts/Attentions information */
	if (!sb_info->sb_virt) {
		DP_ERR(p_hwfn->p_dev, "Interrupt Status block is NULL - cannot check for new interrupts!\n");
	} else {
		u32 tmp_index = sb_info->sb_ack;
		rc = ecore_sb_update_sb_idx(sb_info);
		DP_VERBOSE(p_hwfn->p_dev, ECORE_MSG_INTR,
			   "Interrupt indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_info->sb_ack);
	}

	if (!sb_attn || !sb_attn->sb_attn) {
		DP_ERR(p_hwfn->p_dev, "Attentions Status block is NULL - cannot check for new attentions!\n");
	} else {
		u16 tmp_index = sb_attn->index;

		rc |= ecore_attn_update_idx(p_hwfn, sb_attn);
		DP_VERBOSE(p_hwfn->p_dev, ECORE_MSG_INTR,
			   "Attention indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_attn->index);
	}

	/* Check if we expect interrupts at this time. if not just ack them */
	if (!(rc & ECORE_SB_EVENT_MASK)) {
		ecore_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	/* Check the validity of the DPC ptt. If not ack interrupts and fail */
	if (!p_hwfn->p_dpc_ptt) {
		DP_NOTICE(p_hwfn->p_dev, true, "Failed to allocate PTT\n");
		ecore_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	if (rc & ECORE_SB_ATT_IDX)
		ecore_int_attentions(p_hwfn);

	if (rc & ECORE_SB_IDX) {
		int pi;

		/* Since we only looked at the SB index, it's possible more
		 * than a single protocol-index on the SB incremented.
		 * Iterate over all configured protocol indices and check
		 * whether something happened for each.
		 */
		for (pi = 0; pi < arr_size; pi++) {
			pi_info = &p_hwfn->p_sp_sb->pi_info_arr[pi];
			if (pi_info->comp_cb != OSAL_NULL)
				pi_info->comp_cb(p_hwfn, pi_info->cookie);
		}
	}

	if (sb_attn && (rc & ECORE_SB_ATT_IDX)) {
		/* This should be done before the interrupts are enabled,
		 * since otherwise a new attention will be generated.
		 */
		ecore_sb_ack_attn(p_hwfn, sb_info->igu_addr, sb_attn->index);
	}

	ecore_sb_ack(sb_info, IGU_INT_ENABLE, 1);
}

static void ecore_int_sb_attn_free(struct ecore_hwfn *p_hwfn)
{
	struct ecore_sb_attn_info *p_sb = p_hwfn->p_sb_attn;

	if (!p_sb)
		return;

	if (p_sb->sb_attn) {
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_sb->sb_attn,
				       p_sb->sb_phys,
				       SB_ATTN_ALIGNED_SIZE(p_hwfn));
	}

	OSAL_FREE(p_hwfn->p_dev, p_sb);
	p_hwfn->p_sb_attn = OSAL_NULL;
}

static void ecore_int_sb_attn_setup(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt *p_ptt)
{
	struct ecore_sb_attn_info *sb_info = p_hwfn->p_sb_attn;

	OSAL_MEMSET(sb_info->sb_attn, 0, sizeof(*sb_info->sb_attn));

	sb_info->index = 0;
	sb_info->known_attn = 0;

	/* Configure Attention Status Block in IGU */
	ecore_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_L,
		 DMA_LO(p_hwfn->p_sb_attn->sb_phys));
	ecore_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_H,
		 DMA_HI(p_hwfn->p_sb_attn->sb_phys));
}

static void ecore_int_sb_attn_init(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt,
				   void *sb_virt_addr,
				   dma_addr_t sb_phy_addr)
{
	struct ecore_sb_attn_info *sb_info = p_hwfn->p_sb_attn;
	int i, j, k;

	sb_info->sb_attn = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	/* Set the pointer to the AEU descriptors */
	sb_info->p_aeu_desc = aeu_descs;

	/* Calculate Parity Masks */
	OSAL_MEMSET(sb_info->parity_mask, 0, sizeof(u32) * NUM_ATTN_REGS);
	for (i = 0; i < NUM_ATTN_REGS; i++) {
		/* j is array index, k is bit index */
		for (j = 0, k = 0; k < 32; j++) {
			struct aeu_invert_reg_bit *p_aeu;

			p_aeu = &aeu_descs[i].bits[j];
			if (ecore_int_is_parity_flag(p_hwfn, p_aeu))
				sb_info->parity_mask[i] |= 1 << k;

			k += ATTENTION_LENGTH(p_aeu->flags);
		}
		DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
			   "Attn Mask [Reg %d]: 0x%08x\n",
			   i, sb_info->parity_mask[i]);
	}

	/* Set the address of cleanup for the mcp attention */
	sb_info->mfw_attn_addr = (p_hwfn->rel_pf_id << 3) +
				 MISC_REG_AEU_GENERAL_ATTN_0;

	ecore_int_sb_attn_setup(p_hwfn, p_ptt);
}

static enum _ecore_status_t ecore_int_sb_attn_alloc(struct ecore_hwfn *p_hwfn,
						    struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	struct ecore_sb_attn_info *p_sb;
	dma_addr_t p_phys = 0;
	void *p_virt;

	/* SB struct */
	p_sb = OSAL_ALLOC(p_dev, GFP_KERNEL, sizeof(*p_sb));
	if (!p_sb) {
		DP_NOTICE(p_dev, false, "Failed to allocate `struct ecore_sb_attn_info'\n");
		return ECORE_NOMEM;
	}

	/* SB ring  */
	p_virt = OSAL_DMA_ALLOC_COHERENT(p_dev, &p_phys,
					 SB_ATTN_ALIGNED_SIZE(p_hwfn));
	if (!p_virt) {
		DP_NOTICE(p_dev, false, "Failed to allocate status block (attentions)\n");
		OSAL_FREE(p_dev, p_sb);
		return ECORE_NOMEM;
	}

	/* Attention setup */
	p_hwfn->p_sb_attn = p_sb;
	ecore_int_sb_attn_init(p_hwfn, p_ptt, p_virt, p_phys);

	return ECORE_SUCCESS;
}

/* coalescing timeout = timeset << (timer_res + 1) */
#define ECORE_CAU_DEF_RX_USECS 24
#define ECORE_CAU_DEF_TX_USECS 48

void ecore_init_cau_sb_entry(struct ecore_hwfn *p_hwfn,
			     struct cau_sb_entry *p_sb_entry,
			     u8 pf_id, u16 vf_number, u8 vf_valid)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u32 cau_state;
	u8 timer_res;

	OSAL_MEMSET(p_sb_entry, 0, sizeof(*p_sb_entry));

	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_PF_NUMBER, pf_id);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_NUMBER, vf_number);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_VALID, vf_valid);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET0, 0x7F);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET1, 0x7F);

	cau_state = CAU_HC_DISABLE_STATE;

	if (p_dev->int_coalescing_mode == ECORE_COAL_MODE_ENABLE) {
		cau_state = CAU_HC_ENABLE_STATE;
		if (!p_dev->rx_coalesce_usecs)
			p_dev->rx_coalesce_usecs = ECORE_CAU_DEF_RX_USECS;
		if (!p_dev->tx_coalesce_usecs)
			p_dev->tx_coalesce_usecs = ECORE_CAU_DEF_TX_USECS;
	}

	/* Coalesce = (timeset << timer-res), timeset is 7bit wide */
	if (p_dev->rx_coalesce_usecs <= 0x7F)
		timer_res = 0;
	else if (p_dev->rx_coalesce_usecs <= 0xFF)
		timer_res = 1;
	else
		timer_res = 2;
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES0, timer_res);

	if (p_dev->tx_coalesce_usecs <= 0x7F)
		timer_res = 0;
	else if (p_dev->tx_coalesce_usecs <= 0xFF)
		timer_res = 1;
	else
		timer_res = 2;
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES1, timer_res);

	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE0, cau_state);
	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE1, cau_state);
}

static void _ecore_int_cau_conf_pi(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt,
				   u16 igu_sb_id, u32 pi_index,
				   enum ecore_coalescing_fsm coalescing_fsm,
				   u8 timeset)
{
	struct cau_pi_entry pi_entry;
	u32 sb_offset, pi_offset;

	if (IS_VF(p_hwfn->p_dev))
		return;/* @@@TBD MichalK- VF CAU... */

	sb_offset = igu_sb_id * PIS_PER_SB_E4;
	OSAL_MEMSET(&pi_entry, 0, sizeof(struct cau_pi_entry));

	SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_PI_TIMESET, timeset);
	if (coalescing_fsm == ECORE_COAL_RX_STATE_MACHINE)
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 0);
	else
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 1);

	pi_offset = sb_offset + pi_index;
	if (p_hwfn->hw_init_done) {
		ecore_wr(p_hwfn, p_ptt,
			 CAU_REG_PI_MEMORY + pi_offset * sizeof(u32),
			 *((u32 *)&(pi_entry)));
	} else {
		STORE_RT_REG(p_hwfn,
			     CAU_REG_PI_MEMORY_RT_OFFSET + pi_offset,
			     *((u32 *)&(pi_entry)));
	}
}

void ecore_int_cau_conf_pi(struct ecore_hwfn *p_hwfn,
			   struct ecore_ptt *p_ptt,
			   struct ecore_sb_info *p_sb, u32 pi_index,
			   enum ecore_coalescing_fsm coalescing_fsm,
			   u8 timeset)
{
	_ecore_int_cau_conf_pi(p_hwfn, p_ptt, p_sb->igu_sb_id,
			       pi_index, coalescing_fsm, timeset);
}

void ecore_int_cau_conf_sb(struct ecore_hwfn *p_hwfn,
			   struct ecore_ptt *p_ptt,
			   dma_addr_t sb_phys, u16 igu_sb_id,
			   u16 vf_number, u8 vf_valid)
{
	struct cau_sb_entry sb_entry;

	ecore_init_cau_sb_entry(p_hwfn, &sb_entry, p_hwfn->rel_pf_id,
				vf_number, vf_valid);

	if (p_hwfn->hw_init_done) {
		/* Wide-bus, initialize via DMAE */
		u64 phys_addr = (u64)sb_phys;

		ecore_dmae_host2grc(p_hwfn, p_ptt, (u64)(osal_uintptr_t)&phys_addr,
				    CAU_REG_SB_ADDR_MEMORY +
				    igu_sb_id * sizeof(u64), 2,
				    OSAL_NULL /* default parameters */);
		ecore_dmae_host2grc(p_hwfn, p_ptt, (u64)(osal_uintptr_t)&sb_entry,
				    CAU_REG_SB_VAR_MEMORY +
				    igu_sb_id * sizeof(u64), 2,
				    OSAL_NULL /* default parameters */);
	} else {
		/* Initialize Status Block Address */
		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_ADDR_MEMORY_RT_OFFSET+igu_sb_id*2,
				 sb_phys);

		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_VAR_MEMORY_RT_OFFSET+igu_sb_id*2,
				 sb_entry);
	}

	/* Configure pi coalescing if set */
	if (p_hwfn->p_dev->int_coalescing_mode == ECORE_COAL_MODE_ENABLE) {
		/* eth will open queues for all tcs, so configure all of them
		 * properly, rather than just the active ones
		 */
		u8 num_tc = p_hwfn->hw_info.num_hw_tc;

		u8 timeset, timer_res;
		u8 i;

		/* timeset = (coalesce >> timer-res), timeset is 7bit wide */
		if (p_hwfn->p_dev->rx_coalesce_usecs <= 0x7F)
			timer_res = 0;
		else if (p_hwfn->p_dev->rx_coalesce_usecs <= 0xFF)
			timer_res = 1;
		else
			timer_res = 2;
		timeset = (u8)(p_hwfn->p_dev->rx_coalesce_usecs >> timer_res);
		_ecore_int_cau_conf_pi(p_hwfn, p_ptt, igu_sb_id, RX_PI,
				       ECORE_COAL_RX_STATE_MACHINE,
				       timeset);

		if (p_hwfn->p_dev->tx_coalesce_usecs <= 0x7F)
			timer_res = 0;
		else if (p_hwfn->p_dev->tx_coalesce_usecs <= 0xFF)
			timer_res = 1;
		else
			timer_res = 2;
		timeset = (u8)(p_hwfn->p_dev->tx_coalesce_usecs >> timer_res);
		for (i = 0; i < num_tc; i++) {
			_ecore_int_cau_conf_pi(p_hwfn, p_ptt,
					       igu_sb_id, TX_PI(i),
					       ECORE_COAL_TX_STATE_MACHINE,
					       timeset);
		}
	}
}

void ecore_int_sb_setup(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt *p_ptt,
			       struct ecore_sb_info *sb_info)
{
	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	OSAL_MEMSET(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	if (IS_PF(p_hwfn->p_dev))
		ecore_int_cau_conf_sb(p_hwfn, p_ptt, sb_info->sb_phys,
				      sb_info->igu_sb_id, 0, 0);
}

struct ecore_igu_block *
ecore_get_igu_free_sb(struct ecore_hwfn *p_hwfn, bool b_is_pf)
{
	struct ecore_igu_block *p_block;
	u16 igu_id;

	for (igu_id = 0; igu_id < ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev);
	     igu_id++) {
		p_block = &p_hwfn->hw_info.p_igu_info->entry[igu_id];

		if (!(p_block->status & ECORE_IGU_STATUS_VALID) ||
		    !(p_block->status & ECORE_IGU_STATUS_FREE))
			continue;

		if (!!(p_block->status & ECORE_IGU_STATUS_PF) ==
		    b_is_pf)
			return p_block;
	}

	return OSAL_NULL;
}

static u16 ecore_get_pf_igu_sb_id(struct ecore_hwfn *p_hwfn,
				  u16 vector_id)
{
	struct ecore_igu_block *p_block;
	u16 igu_id;

	for (igu_id = 0; igu_id < ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev);
	     igu_id++) {
		p_block = &p_hwfn->hw_info.p_igu_info->entry[igu_id];

		if (!(p_block->status & ECORE_IGU_STATUS_VALID) ||
		    !p_block->is_pf ||
		    p_block->vector_number != vector_id)
			continue;

		return igu_id;
	}

	return ECORE_SB_INVALID_IDX;
}

u16 ecore_get_igu_sb_id(struct ecore_hwfn *p_hwfn, u16 sb_id)
{
	u16 igu_sb_id;

	/* Assuming continuous set of IGU SBs dedicated for given PF */
	if (sb_id == ECORE_SP_SB_ID)
		igu_sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
	else if (IS_PF(p_hwfn->p_dev))
		igu_sb_id = ecore_get_pf_igu_sb_id(p_hwfn, sb_id + 1);
	else
		igu_sb_id = ecore_vf_get_igu_sb_id(p_hwfn, sb_id);

	if (igu_sb_id == ECORE_SB_INVALID_IDX)
		DP_NOTICE(p_hwfn, true,
			  "Slowpath SB vector %04x doesn't exist\n",
			  sb_id);
	else if (sb_id == ECORE_SP_SB_ID)
		DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
			   "Slowpath SB index in IGU is 0x%04x\n", igu_sb_id);
	else
		DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
			   "SB [%04x] <--> IGU SB [%04x]\n", sb_id, igu_sb_id);

	return igu_sb_id;
}

enum _ecore_status_t ecore_int_sb_init(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       struct ecore_sb_info *sb_info,
				       void *sb_virt_addr,
				       dma_addr_t sb_phy_addr,
				       u16 sb_id)
{
	sb_info->sb_virt = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	sb_info->igu_sb_id = ecore_get_igu_sb_id(p_hwfn, sb_id);

	if (sb_info->igu_sb_id == ECORE_SB_INVALID_IDX)
		return ECORE_INVAL;

	/* Let the igu info reference the client's SB info */
	if (sb_id != ECORE_SP_SB_ID) {
		if (IS_PF(p_hwfn->p_dev)) {
			struct ecore_igu_info *p_info;
			struct ecore_igu_block *p_block;

			p_info = p_hwfn->hw_info.p_igu_info;
			p_block = &p_info->entry[sb_info->igu_sb_id];

			p_block->sb_info = sb_info;
			p_block->status &= ~ECORE_IGU_STATUS_FREE;
			p_info->usage.free_cnt--;
		} else {
			ecore_vf_set_sb_info(p_hwfn, sb_id, sb_info);
		}
	}

#ifdef ECORE_CONFIG_DIRECT_HWFN
	sb_info->p_hwfn = p_hwfn;
#endif
	sb_info->p_dev = p_hwfn->p_dev;

	/* The igu address will hold the absolute address that needs to be
	 * written to for a specific status block
	 */
	if (IS_PF(p_hwfn->p_dev)) {
		sb_info->igu_addr = (u8 OSAL_IOMEM*)p_hwfn->regview +
				    GTT_BAR0_MAP_REG_IGU_CMD +
				    (sb_info->igu_sb_id << 3);

	} else {
		sb_info->igu_addr =
			(u8 OSAL_IOMEM*)p_hwfn->regview +
			PXP_VF_BAR0_START_IGU +
			((IGU_CMD_INT_ACK_BASE + sb_info->igu_sb_id) << 3);
	}

	sb_info->flags |= ECORE_SB_INFO_INIT;

	ecore_int_sb_setup(p_hwfn, p_ptt, sb_info);

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_int_sb_release(struct ecore_hwfn *p_hwfn,
					  struct ecore_sb_info *sb_info,
					  u16 sb_id)
{
	struct ecore_igu_info *p_info;
	struct ecore_igu_block *p_block;

	if (sb_info == OSAL_NULL)
		return ECORE_SUCCESS;

	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	OSAL_MEMSET(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	if (IS_VF(p_hwfn->p_dev)) {
		ecore_vf_set_sb_info(p_hwfn, sb_id, OSAL_NULL);
		return ECORE_SUCCESS;
	}

	p_info = p_hwfn->hw_info.p_igu_info;
	p_block = &p_info->entry[sb_info->igu_sb_id];

	/* Vector 0 is reserved to Default SB */
	if (p_block->vector_number == 0) {
		DP_ERR(p_hwfn, "Do Not free sp sb using this function");
		return ECORE_INVAL;
	}

	/* Lose reference to client's SB info, and fix counters */
	p_block->sb_info = OSAL_NULL;
	p_block->status |= ECORE_IGU_STATUS_FREE;
	p_info->usage.free_cnt++;

	return ECORE_SUCCESS;
}

static void ecore_int_sp_sb_free(struct ecore_hwfn *p_hwfn)
{
	struct ecore_sb_sp_info *p_sb = p_hwfn->p_sp_sb;

	if (!p_sb)
		return;

	if (p_sb->sb_info.sb_virt) {
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_sb->sb_info.sb_virt,
				       p_sb->sb_info.sb_phys,
				       SB_ALIGNED_SIZE(p_hwfn));
	}

	OSAL_FREE(p_hwfn->p_dev, p_sb);
	p_hwfn->p_sp_sb = OSAL_NULL;
}

static enum _ecore_status_t ecore_int_sp_sb_alloc(struct ecore_hwfn *p_hwfn,
						  struct ecore_ptt *p_ptt)
{
	struct ecore_sb_sp_info *p_sb;
	dma_addr_t p_phys = 0;
	void *p_virt;

	/* SB struct */
	p_sb = OSAL_ALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*p_sb));
	if (!p_sb) {
		DP_NOTICE(p_hwfn, false, "Failed to allocate `struct ecore_sb_info'\n");
		return ECORE_NOMEM;
	}

	/* SB ring  */
	p_virt = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
					 &p_phys,
					 SB_ALIGNED_SIZE(p_hwfn));
	if (!p_virt) {
		DP_NOTICE(p_hwfn, false, "Failed to allocate status block\n");
		OSAL_FREE(p_hwfn->p_dev, p_sb);
		return ECORE_NOMEM;
	}


	/* Status Block setup */
	p_hwfn->p_sp_sb = p_sb;
	ecore_int_sb_init(p_hwfn, p_ptt, &p_sb->sb_info,
			  p_virt, p_phys, ECORE_SP_SB_ID);

	OSAL_MEMSET(p_sb->pi_info_arr, 0, sizeof(p_sb->pi_info_arr));

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_int_register_cb(struct ecore_hwfn *p_hwfn,
					   ecore_int_comp_cb_t comp_cb,
					   void *cookie,
					   u8 *sb_idx,
					   __le16 **p_fw_cons)
{
	struct ecore_sb_sp_info *p_sp_sb  = p_hwfn->p_sp_sb;
	enum _ecore_status_t rc = ECORE_NOMEM;
	u8 pi;

	/* Look for a free index */
	for (pi = 0; pi < OSAL_ARRAY_SIZE(p_sp_sb->pi_info_arr); pi++) {
		if (p_sp_sb->pi_info_arr[pi].comp_cb != OSAL_NULL)
			continue;

		p_sp_sb->pi_info_arr[pi].comp_cb = comp_cb;
		p_sp_sb->pi_info_arr[pi].cookie = cookie;
		*sb_idx = pi;
		*p_fw_cons = &p_sp_sb->sb_info.sb_virt->pi_array[pi];
		rc = ECORE_SUCCESS;
		break;
	}

	return rc;
}

enum _ecore_status_t ecore_int_unregister_cb(struct ecore_hwfn *p_hwfn,
					     u8 pi)
{
	struct ecore_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;

	if (p_sp_sb->pi_info_arr[pi].comp_cb == OSAL_NULL)
		return ECORE_NOMEM;

	p_sp_sb->pi_info_arr[pi].comp_cb = OSAL_NULL;
	p_sp_sb->pi_info_arr[pi].cookie = OSAL_NULL;

	return ECORE_SUCCESS;
}

u16 ecore_int_get_sp_sb_id(struct ecore_hwfn *p_hwfn)
{
	return p_hwfn->p_sp_sb->sb_info.igu_sb_id;
}

void ecore_int_igu_enable_int(struct ecore_hwfn *p_hwfn,
			      struct ecore_ptt	*p_ptt,
			      enum ecore_int_mode int_mode)
{
	u32 igu_pf_conf = IGU_PF_CONF_FUNC_EN | IGU_PF_CONF_ATTN_BIT_EN;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_FPGA(p_hwfn->p_dev)) {
		DP_INFO(p_hwfn, "FPGA - don't enable ATTN generation in IGU\n");
		igu_pf_conf &= ~IGU_PF_CONF_ATTN_BIT_EN;
	}
#endif

	p_hwfn->p_dev->int_mode = int_mode;
	switch (p_hwfn->p_dev->int_mode) {
	case ECORE_INT_MODE_INTA:
		igu_pf_conf |= IGU_PF_CONF_INT_LINE_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case ECORE_INT_MODE_MSI:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case ECORE_INT_MODE_MSIX:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		break;
	case ECORE_INT_MODE_POLL:
		break;
	}

	ecore_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, igu_pf_conf);
}

static void ecore_int_igu_enable_attn(struct ecore_hwfn *p_hwfn,
				      struct ecore_ptt *p_ptt)
{
#ifndef ASIC_ONLY
	if (CHIP_REV_IS_FPGA(p_hwfn->p_dev)) {
		DP_INFO(p_hwfn, "FPGA - Don't enable Attentions in IGU and MISC\n");
		return;
	}
#endif

	/* Configure AEU signal change to produce attentions */
	ecore_wr(p_hwfn, p_ptt, IGU_REG_ATTENTION_ENABLE, 0);
	ecore_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0xfff);
	ecore_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0xfff);
	ecore_wr(p_hwfn, p_ptt, IGU_REG_ATTENTION_ENABLE, 0xfff);

	/* Flush the writes to IGU */
	OSAL_MMIOWB(p_hwfn->p_dev);

	/* Unmask AEU signals toward IGU */
	ecore_wr(p_hwfn, p_ptt, MISC_REG_AEU_MASK_ATTN_IGU, 0xff);
}

enum _ecore_status_t
ecore_int_igu_enable(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			  enum ecore_int_mode int_mode)
{
	enum _ecore_status_t rc = ECORE_SUCCESS;

	ecore_int_igu_enable_attn(p_hwfn, p_ptt);

	if ((int_mode != ECORE_INT_MODE_INTA) || IS_LEAD_HWFN(p_hwfn)) {
		rc = OSAL_SLOWPATH_IRQ_REQ(p_hwfn);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, true, "Slowpath IRQ request failed\n");
			return ECORE_NORESOURCES;
		}
		p_hwfn->b_int_requested = true;
	}

	/* Enable interrupt Generation */
	ecore_int_igu_enable_int(p_hwfn, p_ptt, int_mode);

	p_hwfn->b_int_enabled = 1;

	return rc;
}

void ecore_int_igu_disable_int(struct ecore_hwfn	*p_hwfn,
			       struct ecore_ptt		*p_ptt)
{
	p_hwfn->b_int_enabled = 0;

	if (IS_VF(p_hwfn->p_dev))
		return;

	ecore_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, 0);
}

#define IGU_CLEANUP_SLEEP_LENGTH		(1000)
static void ecore_int_igu_cleanup_sb(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt,
				     u16 igu_sb_id,
				     bool cleanup_set,
				     u16 opaque_fid)
{
	u32 cmd_ctrl = 0, val = 0, sb_bit = 0, sb_bit_addr = 0, data = 0;
	u32 pxp_addr = IGU_CMD_INT_ACK_BASE + igu_sb_id;
	u32 sleep_cnt = IGU_CLEANUP_SLEEP_LENGTH;
	u8  type = 0; /* FIXME MichalS type??? */

	OSAL_BUILD_BUG_ON((IGU_REG_CLEANUP_STATUS_4 -
			   IGU_REG_CLEANUP_STATUS_0) != 0x200);

	/* USE Control Command Register to perform cleanup. There is an
	 * option to do this using IGU bar, but then it can't be used for VFs.
	 */

	/* Set the data field */
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_SET, cleanup_set ? 1 : 0);
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_TYPE, type);
	SET_FIELD(data, IGU_CLEANUP_COMMAND_TYPE, IGU_COMMAND_TYPE_SET);

	/* Set the control register */
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_PXP_ADDR, pxp_addr);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_FID, opaque_fid);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_TYPE, IGU_CTRL_CMD_TYPE_WR);

	ecore_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_32LSB_DATA, data);

	OSAL_BARRIER(p_hwfn->p_dev);

	ecore_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl);

	/* Flush the write to IGU */
	OSAL_MMIOWB(p_hwfn->p_dev);

	/* calculate where to read the status bit from */
	sb_bit = 1 << (igu_sb_id % 32);
	sb_bit_addr = igu_sb_id / 32 * sizeof(u32);

	sb_bit_addr += IGU_REG_CLEANUP_STATUS_0 + (0x80 * type);

	/* Now wait for the command to complete */
	while (--sleep_cnt) {
		val = ecore_rd(p_hwfn, p_ptt, sb_bit_addr);
		if ((val & sb_bit) == (cleanup_set ? sb_bit : 0))
			break;
		OSAL_MSLEEP(5);
	}

	if (!sleep_cnt)
		DP_NOTICE(p_hwfn, true,
			  "Timeout waiting for clear status 0x%08x [for sb %d]\n",
			  val, igu_sb_id);
}

void ecore_int_igu_init_pure_rt_single(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       u16 igu_sb_id, u16 opaque, bool b_set)
{
	struct ecore_igu_block *p_block;
	int pi, i;

	p_block = &p_hwfn->hw_info.p_igu_info->entry[igu_sb_id];
	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
		   "Cleaning SB [%04x]: func_id= %d is_pf = %d vector_num = 0x%0x\n",
		   igu_sb_id, p_block->function_id, p_block->is_pf,
		   p_block->vector_number);

	/* Set */
	if (b_set)
		ecore_int_igu_cleanup_sb(p_hwfn, p_ptt, igu_sb_id, 1, opaque);

	/* Clear */
	ecore_int_igu_cleanup_sb(p_hwfn, p_ptt, igu_sb_id, 0, opaque);

	/* Wait for the IGU SB to cleanup */
	for (i = 0; i < IGU_CLEANUP_SLEEP_LENGTH; i++) {
		u32 val;

		val = ecore_rd(p_hwfn, p_ptt,
			       IGU_REG_WRITE_DONE_PENDING +
			       ((igu_sb_id / 32) * 4));
		if (val & (1 << (igu_sb_id % 32)))
			OSAL_UDELAY(10);
		else
			break;
	}
	if (i == IGU_CLEANUP_SLEEP_LENGTH)
		DP_NOTICE(p_hwfn, true,
			  "Failed SB[0x%08x] still appearing in WRITE_DONE_PENDING\n",
			  igu_sb_id);

	/* Clear the CAU for the SB */
	for (pi = 0; pi < 12; pi++)
		ecore_wr(p_hwfn, p_ptt,
			 CAU_REG_PI_MEMORY + (igu_sb_id * 12 + pi) * 4, 0);
}

void ecore_int_igu_init_pure_rt(struct ecore_hwfn *p_hwfn,
				 struct ecore_ptt *p_ptt,
				 bool b_set,
				 bool b_slowpath)
{
	struct ecore_igu_info *p_info = p_hwfn->hw_info.p_igu_info;
	struct ecore_igu_block *p_block;
	u16 igu_sb_id = 0;
	u32 val = 0;

	/* @@@TBD MichalK temporary... should be moved to init-tool... */
	val = ecore_rd(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION);
	val |= IGU_REG_BLOCK_CONFIGURATION_VF_CLEANUP_EN;
	val &= ~IGU_REG_BLOCK_CONFIGURATION_PXP_TPH_INTERFACE_EN;
	ecore_wr(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION, val);
	/* end temporary */

	for (igu_sb_id = 0;
	     igu_sb_id < ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev);
	     igu_sb_id++) {
		p_block = &p_info->entry[igu_sb_id];

		if (!(p_block->status & ECORE_IGU_STATUS_VALID) ||
		    !p_block->is_pf ||
		    (p_block->status & ECORE_IGU_STATUS_DSB))
			continue;

		ecore_int_igu_init_pure_rt_single(p_hwfn, p_ptt, igu_sb_id,
						  p_hwfn->hw_info.opaque_fid,
						  b_set);
	}

	if (b_slowpath)
		ecore_int_igu_init_pure_rt_single(p_hwfn, p_ptt,
						  p_info->igu_dsb_id,
						  p_hwfn->hw_info.opaque_fid,
						  b_set);
}

int ecore_int_igu_reset_cam(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt)
{
	struct ecore_igu_info *p_info = p_hwfn->hw_info.p_igu_info;
	struct ecore_igu_block *p_block;
	int pf_sbs, vf_sbs;
	u16 igu_sb_id;
	u32 val, rval;

	if (!RESC_NUM(p_hwfn, ECORE_SB)) {
		/* We're using an old MFW - have to prevent any switching
		 * of SBs between PF and VFs as later driver wouldn't be
		 * able to tell which belongs to which.
		 */
		p_info->b_allow_pf_vf_change = false;
	} else {
		/* Use the numbers the MFW have provided -
		 * don't forget MFW accounts for the default SB as well.
		 */
		p_info->b_allow_pf_vf_change = true;

		if (p_info->usage.cnt != RESC_NUM(p_hwfn, ECORE_SB) - 1) {
			DP_INFO(p_hwfn,
				"MFW notifies of 0x%04x PF SBs; IGU indicates of only 0x%04x\n",
				RESC_NUM(p_hwfn, ECORE_SB) - 1,
				p_info->usage.cnt);
			p_info->usage.cnt = RESC_NUM(p_hwfn, ECORE_SB) - 1;
		}

		/* TODO - how do we learn about VF SBs from MFW? */
		if (IS_PF_SRIOV(p_hwfn)) {
			u16 vfs = p_hwfn->p_dev->p_iov_info->total_vfs;

			if (vfs != p_info->usage.iov_cnt)
				DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
					   "0x%04x VF SBs in IGU CAM != PCI configuration 0x%04x\n",
					   p_info->usage.iov_cnt, vfs);

			/* At this point we know how many SBs we have totally
			 * in IGU + number of PF SBs. So we can validate that
			 * we'd have sufficient for VF.
			 */
			if (vfs > p_info->usage.free_cnt +
				  p_info->usage.free_cnt_iov -
				  p_info->usage.cnt) {
				DP_NOTICE(p_hwfn, true,
					  "Not enough SBs for VFs - 0x%04x SBs, from which %04x PFs and %04x are required\n",
					  p_info->usage.free_cnt +
					  p_info->usage.free_cnt_iov,
					  p_info->usage.cnt, vfs);
				return ECORE_INVAL;
			}
		}
	}

	/* Cap the number of VFs SBs by the number of VFs */
	if (IS_PF_SRIOV(p_hwfn))
		p_info->usage.iov_cnt = p_hwfn->p_dev->p_iov_info->total_vfs;

	/* Mark all SBs as free, now in the right PF/VFs division */
	p_info->usage.free_cnt = p_info->usage.cnt;
	p_info->usage.free_cnt_iov = p_info->usage.iov_cnt;
	p_info->usage.orig = p_info->usage.cnt;
	p_info->usage.iov_orig = p_info->usage.iov_cnt;

	/* We now proceed to re-configure the IGU cam to reflect the initial
	 * configuration. We can start with the Default SB.
	 */
	pf_sbs = p_info->usage.cnt;
	vf_sbs = p_info->usage.iov_cnt;

	for (igu_sb_id = p_info->igu_dsb_id;
	     igu_sb_id < ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev);
	     igu_sb_id++) {
		p_block = &p_info->entry[igu_sb_id];
		val = 0;

		if (!(p_block->status & ECORE_IGU_STATUS_VALID))
			continue;

		if (p_block->status & ECORE_IGU_STATUS_DSB) {
			p_block->function_id = p_hwfn->rel_pf_id;
			p_block->is_pf = 1;
			p_block->vector_number = 0;
			p_block->status = ECORE_IGU_STATUS_VALID |
					  ECORE_IGU_STATUS_PF |
					  ECORE_IGU_STATUS_DSB;
		} else if (pf_sbs) {
			pf_sbs--;
			p_block->function_id = p_hwfn->rel_pf_id;
			p_block->is_pf = 1;
			p_block->vector_number = p_info->usage.cnt - pf_sbs;
			p_block->status = ECORE_IGU_STATUS_VALID |
					  ECORE_IGU_STATUS_PF |
					  ECORE_IGU_STATUS_FREE;
		} else if (vf_sbs) {
			p_block->function_id =
				p_hwfn->p_dev->p_iov_info->first_vf_in_pf +
				p_info->usage.iov_cnt - vf_sbs;
			p_block->is_pf = 0;
			p_block->vector_number = 0;
			p_block->status = ECORE_IGU_STATUS_VALID |
					  ECORE_IGU_STATUS_FREE;
			vf_sbs--;
		} else {
			p_block->function_id = 0;
			p_block->is_pf = 0;
			p_block->vector_number = 0;
		}

		SET_FIELD(val, IGU_MAPPING_LINE_FUNCTION_NUMBER,
			  p_block->function_id);
		SET_FIELD(val, IGU_MAPPING_LINE_PF_VALID, p_block->is_pf);
		SET_FIELD(val, IGU_MAPPING_LINE_VECTOR_NUMBER,
			  p_block->vector_number);

		/* VF entries would be enabled when VF is initializaed */
		SET_FIELD(val, IGU_MAPPING_LINE_VALID, p_block->is_pf);

		rval = ecore_rd(p_hwfn, p_ptt,
				IGU_REG_MAPPING_MEMORY +
				sizeof(u32) * igu_sb_id);

		if (rval != val) {
			ecore_wr(p_hwfn, p_ptt,
				 IGU_REG_MAPPING_MEMORY +
				 sizeof(u32) * igu_sb_id,
				 val);

			DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
				   "IGU reset: [SB 0x%04x] func_id = %d is_pf = %d vector_num = 0x%x [%08x -> %08x]\n",
				   igu_sb_id, p_block->function_id,
				   p_block->is_pf, p_block->vector_number,
				   rval, val);
		}
	}

	return 0;
}

int ecore_int_igu_reset_cam_default(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt *p_ptt)
{
	struct ecore_sb_cnt_info *p_cnt = &p_hwfn->hw_info.p_igu_info->usage;

	/* Return all the usage indications to default prior to the reset;
	 * The reset expects the !orig to reflect the initial status of the
	 * SBs, and would re-calculate the originals based on those.
	 */
	p_cnt->cnt = p_cnt->orig;
	p_cnt->free_cnt = p_cnt->orig;
	p_cnt->iov_cnt = p_cnt->iov_orig;
	p_cnt->free_cnt_iov = p_cnt->iov_orig;
	p_cnt->orig = 0;
	p_cnt->iov_orig = 0;

	/* TODO - we probably need to re-configure the CAU as well... */
	return ecore_int_igu_reset_cam(p_hwfn, p_ptt);
}

static void ecore_int_igu_read_cam_block(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u16 igu_sb_id)
{
	u32 val = ecore_rd(p_hwfn, p_ptt,
			   IGU_REG_MAPPING_MEMORY + sizeof(u32) * igu_sb_id);
	struct ecore_igu_block *p_block;

	p_block = &p_hwfn->hw_info.p_igu_info->entry[igu_sb_id];

	/* Fill the block information */
	p_block->function_id = GET_FIELD(val,
					 IGU_MAPPING_LINE_FUNCTION_NUMBER);
	p_block->is_pf = GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID);
	p_block->vector_number = GET_FIELD(val,
					   IGU_MAPPING_LINE_VECTOR_NUMBER);
	p_block->igu_sb_id = igu_sb_id;
}

enum _ecore_status_t ecore_int_igu_read_cam(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt)
{
	struct ecore_igu_info *p_igu_info;
	struct ecore_igu_block *p_block;
	u32 min_vf = 0, max_vf = 0;
	u16 igu_sb_id;

	p_hwfn->hw_info.p_igu_info = OSAL_ZALLOC(p_hwfn->p_dev,
						 GFP_KERNEL,
						 sizeof(*p_igu_info));
	if (!p_hwfn->hw_info.p_igu_info)
		return ECORE_NOMEM;
	p_igu_info = p_hwfn->hw_info.p_igu_info;

	/* Distinguish between existent and onn-existent default SB */
	p_igu_info->igu_dsb_id = ECORE_SB_INVALID_IDX;

	/* Find the range of VF ids whose SB belong to this PF */
	if (p_hwfn->p_dev->p_iov_info) {
		struct ecore_hw_sriov_info *p_iov = p_hwfn->p_dev->p_iov_info;

		min_vf = p_iov->first_vf_in_pf;
		max_vf = p_iov->first_vf_in_pf + p_iov->total_vfs;
	}

	for (igu_sb_id = 0;
	     igu_sb_id < ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev);
	     igu_sb_id++) {
		/* Read current entry; Notice it might not belong to this PF */
		ecore_int_igu_read_cam_block(p_hwfn, p_ptt, igu_sb_id);
		p_block = &p_igu_info->entry[igu_sb_id];

		if ((p_block->is_pf) &&
		    (p_block->function_id == p_hwfn->rel_pf_id)) {
			p_block->status = ECORE_IGU_STATUS_PF |
					  ECORE_IGU_STATUS_VALID |
					  ECORE_IGU_STATUS_FREE;

			if (p_igu_info->igu_dsb_id != ECORE_SB_INVALID_IDX)
				p_igu_info->usage.cnt++;
		} else if (!(p_block->is_pf) &&
			   (p_block->function_id >= min_vf) &&
			   (p_block->function_id < max_vf)) {
			/* Available for VFs of this PF */
			p_block->status = ECORE_IGU_STATUS_VALID |
					  ECORE_IGU_STATUS_FREE;

			if (p_igu_info->igu_dsb_id != ECORE_SB_INVALID_IDX)
				p_igu_info->usage.iov_cnt++;
		}

		/* Mark the First entry belonging to the PF or its VFs
		 * as the default SB [we'll reset IGU prior to first usage].
		 */
		if ((p_block->status & ECORE_IGU_STATUS_VALID) &&
		    (p_igu_info->igu_dsb_id == ECORE_SB_INVALID_IDX)) {
			p_igu_info->igu_dsb_id = igu_sb_id;
			p_block->status |= ECORE_IGU_STATUS_DSB;
		}

		/* While this isn't suitable for all clients, limit number
		 * of prints by having each PF print only its entries with the
		 * exception of PF0 which would print everything.
		 */
		if ((p_block->status & ECORE_IGU_STATUS_VALID) ||
		    (p_hwfn->abs_pf_id == 0))
			DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
				   "IGU_BLOCK: [SB 0x%04x] func_id = %d is_pf = %d vector_num = 0x%x\n",
				   igu_sb_id, p_block->function_id,
				   p_block->is_pf, p_block->vector_number);
	}

	if (p_igu_info->igu_dsb_id == ECORE_SB_INVALID_IDX) {
		DP_NOTICE(p_hwfn, true,
			  "IGU CAM returned invalid values igu_dsb_id=0x%x\n",
			  p_igu_info->igu_dsb_id);
		return ECORE_INVAL;
	}

	/* All non default SB are considered free at this point */
	p_igu_info->usage.free_cnt = p_igu_info->usage.cnt;
	p_igu_info->usage.free_cnt_iov = p_igu_info->usage.iov_cnt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
		   "igu_dsb_id=0x%x, num Free SBs - PF: %04x VF: %04x [might change after resource allocation]\n",
		   p_igu_info->igu_dsb_id, p_igu_info->usage.cnt,
		   p_igu_info->usage.iov_cnt);

	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_int_igu_relocate_sb(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			  u16 sb_id, bool b_to_vf)
{
	struct ecore_igu_info *p_info = p_hwfn->hw_info.p_igu_info;
	struct ecore_igu_block *p_block = OSAL_NULL;
	u16 igu_sb_id = 0, vf_num = 0;
	u32 val = 0;

	if (IS_VF(p_hwfn->p_dev) || !IS_PF_SRIOV(p_hwfn))
		return ECORE_INVAL;

	if (sb_id == ECORE_SP_SB_ID)
		return ECORE_INVAL;

	if (!p_info->b_allow_pf_vf_change) {
		DP_INFO(p_hwfn, "Can't relocate SBs as MFW is too old.\n");
		return ECORE_INVAL;
	}

	/* If we're moving a SB from PF to VF, the client had to specify
	 * which vector it wants to move.
	 */
	if (b_to_vf) {
		igu_sb_id = ecore_get_pf_igu_sb_id(p_hwfn, sb_id + 1);
		if (igu_sb_id == ECORE_SB_INVALID_IDX)
			return ECORE_INVAL;
	}

	/* If we're moving a SB from VF to PF, need to validate there isn't
	 * already a line configured for that vector.
	 */
	if (!b_to_vf) {
		if (ecore_get_pf_igu_sb_id(p_hwfn, sb_id + 1) !=
		    ECORE_SB_INVALID_IDX)
			return ECORE_INVAL;
	}

	/* We need to validate that the SB can actually be relocated.
	 * This would also handle the previous case where we've explicitly
	 * stated which IGU SB needs to move.
	 */
	for (; igu_sb_id < ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev);
	     igu_sb_id++) {
		p_block = &p_info->entry[igu_sb_id];

		if (!(p_block->status & ECORE_IGU_STATUS_VALID) ||
		    !(p_block->status & ECORE_IGU_STATUS_FREE) ||
		    (!!(p_block->status & ECORE_IGU_STATUS_PF) != b_to_vf)) {
			if (b_to_vf)
				return ECORE_INVAL;
			else
				continue;
		}

		break;
	}

	if (igu_sb_id == ECORE_MAPPING_MEMORY_SIZE(p_hwfn->p_dev)) {
		DP_VERBOSE(p_hwfn, (ECORE_MSG_INTR | ECORE_MSG_IOV),
			   "Failed to find a free SB to move\n");
		return ECORE_INVAL;
	}

	if (p_block == OSAL_NULL) {
		DP_VERBOSE(p_hwfn, (ECORE_MSG_INTR | ECORE_MSG_IOV),
			   "SB address (p_block) is NULL\n");
		return ECORE_INVAL;
	}

	/* At this point, p_block points to the SB we want to relocate */
	if (b_to_vf) {
		p_block->status &= ~ECORE_IGU_STATUS_PF;

		/* It doesn't matter which VF number we choose, since we're
		 * going to disable the line; But let's keep it in range.
		 */
		vf_num = (u16)p_hwfn->p_dev->p_iov_info->first_vf_in_pf;

		p_block->function_id = (u8)vf_num;
		p_block->is_pf = 0;
		p_block->vector_number = 0;

		p_info->usage.cnt--;
		p_info->usage.free_cnt--;
		p_info->usage.iov_cnt++;
		p_info->usage.free_cnt_iov++;

		/* TODO - if SBs aren't really the limiting factor,
		 * then it might not be accurate [in the since that
		 * we might not need decrement the feature].
		 */
		p_hwfn->hw_info.feat_num[ECORE_PF_L2_QUE]--;
		p_hwfn->hw_info.feat_num[ECORE_VF_L2_QUE]++;
	} else {
		p_block->status |= ECORE_IGU_STATUS_PF;
		p_block->function_id = p_hwfn->rel_pf_id;
		p_block->is_pf = 1;
		p_block->vector_number = sb_id + 1;

		p_info->usage.cnt++;
		p_info->usage.free_cnt++;
		p_info->usage.iov_cnt--;
		p_info->usage.free_cnt_iov--;

		p_hwfn->hw_info.feat_num[ECORE_PF_L2_QUE]++;
		p_hwfn->hw_info.feat_num[ECORE_VF_L2_QUE]--;
	}

	/* Update the IGU and CAU with the new configuration */
	SET_FIELD(val, IGU_MAPPING_LINE_FUNCTION_NUMBER,
		  p_block->function_id);
	SET_FIELD(val, IGU_MAPPING_LINE_PF_VALID, p_block->is_pf);
	SET_FIELD(val, IGU_MAPPING_LINE_VALID, p_block->is_pf);
	SET_FIELD(val, IGU_MAPPING_LINE_VECTOR_NUMBER,
		  p_block->vector_number);

	ecore_wr(p_hwfn, p_ptt,
		 IGU_REG_MAPPING_MEMORY + sizeof(u32) * igu_sb_id,
		 val);

	ecore_int_cau_conf_sb(p_hwfn, p_ptt, 0,
			      igu_sb_id, vf_num,
			      p_block->is_pf ? 0 : 1);

	DP_VERBOSE(p_hwfn, ECORE_MSG_INTR,
		   "Relocation: [SB 0x%04x] func_id = %d is_pf = %d vector_num = 0x%x\n",
		   igu_sb_id, p_block->function_id,
		   p_block->is_pf, p_block->vector_number);

	return ECORE_SUCCESS;
}

/**
 * @brief Initialize igu runtime registers
 *
 * @param p_hwfn
 */
void ecore_int_igu_init_rt(struct ecore_hwfn *p_hwfn)
{
	u32 igu_pf_conf = IGU_PF_CONF_FUNC_EN;

	STORE_RT_REG(p_hwfn, IGU_REG_PF_CONFIGURATION_RT_OFFSET, igu_pf_conf);
}

#define LSB_IGU_CMD_ADDR (IGU_REG_SISR_MDPC_WMASK_LSB_UPPER - \
			  IGU_CMD_INT_ACK_BASE)
#define MSB_IGU_CMD_ADDR (IGU_REG_SISR_MDPC_WMASK_MSB_UPPER - \
			  IGU_CMD_INT_ACK_BASE)
u64 ecore_int_igu_read_sisr_reg(struct ecore_hwfn *p_hwfn)
{
	u32 intr_status_hi = 0, intr_status_lo = 0;
	u64 intr_status = 0;

	intr_status_lo = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				LSB_IGU_CMD_ADDR * 8);
	intr_status_hi = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				MSB_IGU_CMD_ADDR * 8);
	intr_status = ((u64)intr_status_hi << 32) + (u64)intr_status_lo;

	return intr_status;
}

static void ecore_int_sp_dpc_setup(struct ecore_hwfn *p_hwfn)
{
	OSAL_DPC_INIT(p_hwfn->sp_dpc, p_hwfn);
	p_hwfn->b_sp_dpc_enabled = true;
}

static enum _ecore_status_t ecore_int_sp_dpc_alloc(struct ecore_hwfn *p_hwfn)
{
	p_hwfn->sp_dpc = OSAL_DPC_ALLOC(p_hwfn);
	if (!p_hwfn->sp_dpc)
		return ECORE_NOMEM;

	return ECORE_SUCCESS;
}

static void ecore_int_sp_dpc_free(struct ecore_hwfn *p_hwfn)
{
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->sp_dpc);
	p_hwfn->sp_dpc = OSAL_NULL;
}

enum _ecore_status_t ecore_int_alloc(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt)
{
	enum _ecore_status_t rc = ECORE_SUCCESS;

	rc = ecore_int_sp_dpc_alloc(p_hwfn);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn->p_dev, "Failed to allocate sp dpc mem\n");
		return rc;
	}

	rc = ecore_int_sp_sb_alloc(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn->p_dev, "Failed to allocate sp sb mem\n");
		return rc;
	}

	rc = ecore_int_sb_attn_alloc(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS)
		DP_ERR(p_hwfn->p_dev, "Failed to allocate sb attn mem\n");

	return rc;
}

void ecore_int_free(struct ecore_hwfn *p_hwfn)
{
	ecore_int_sp_sb_free(p_hwfn);
	ecore_int_sb_attn_free(p_hwfn);
	ecore_int_sp_dpc_free(p_hwfn);
}

void ecore_int_setup(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt)
{
	if (!p_hwfn || !p_hwfn->p_sp_sb || !p_hwfn->p_sb_attn)
		return;

	ecore_int_sb_setup(p_hwfn, p_ptt, &p_hwfn->p_sp_sb->sb_info);
	ecore_int_sb_attn_setup(p_hwfn, p_ptt);
	ecore_int_sp_dpc_setup(p_hwfn);
}

void ecore_int_get_num_sbs(struct ecore_hwfn *p_hwfn,
			   struct ecore_sb_cnt_info *p_sb_cnt_info)
{
	struct ecore_igu_info *p_igu_info = p_hwfn->hw_info.p_igu_info;

	if (!p_igu_info || !p_sb_cnt_info)
		return;

	OSAL_MEMCPY(p_sb_cnt_info, &p_igu_info->usage,
		    sizeof(*p_sb_cnt_info));
}

void ecore_int_disable_post_isr_release(struct ecore_dev *p_dev)
{
	int i;

	for_each_hwfn(p_dev, i)
		p_dev->hwfns[i].b_int_requested = false;
}

void ecore_int_attn_clr_enable(struct ecore_dev *p_dev, bool clr_enable)
{
	p_dev->attn_clr_en = clr_enable;
}

enum _ecore_status_t ecore_int_set_timer_res(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt,
					     u8 timer_res, u16 sb_id, bool tx)
{
	struct cau_sb_entry sb_entry;
	enum _ecore_status_t rc;

	if (!p_hwfn->hw_init_done) {
		DP_ERR(p_hwfn, "hardware not initialized yet\n");
		return ECORE_INVAL;
	}

	rc = ecore_dmae_grc2host(p_hwfn, p_ptt, CAU_REG_SB_VAR_MEMORY +
				 sb_id * sizeof(u64),
				 (u64)(osal_uintptr_t)&sb_entry, 2,
				 OSAL_NULL /* default parameters */);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn, "dmae_grc2host failed %d\n", rc);
		return rc;
	}

	if (tx)
		SET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES1, timer_res);
	else
		SET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES0, timer_res);

	rc = ecore_dmae_host2grc(p_hwfn, p_ptt,
				 (u64)(osal_uintptr_t)&sb_entry,
				 CAU_REG_SB_VAR_MEMORY + sb_id * sizeof(u64), 2,
				 OSAL_NULL /* default parameters */);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn, "dmae_host2grc failed %d\n", rc);
		return rc;
	}

	return rc;
}

enum _ecore_status_t ecore_int_get_sb_dbg(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  struct ecore_sb_info *p_sb,
					  struct ecore_sb_info_dbg *p_info)
{
	u16 sbid = p_sb->igu_sb_id;
	int i;

	if (IS_VF(p_hwfn->p_dev))
		return ECORE_INVAL;

	if (sbid > NUM_OF_SBS(p_hwfn->p_dev))
		return ECORE_INVAL;

	p_info->igu_prod = ecore_rd(p_hwfn, p_ptt,
				    IGU_REG_PRODUCER_MEMORY + sbid * 4);
	p_info->igu_cons = ecore_rd(p_hwfn, p_ptt,
				    IGU_REG_CONSUMER_MEM + sbid * 4);

	for (i = 0; i < PIS_PER_SB_E4; i++)
		p_info->pi[i] = (u16)ecore_rd(p_hwfn, p_ptt,
					      CAU_REG_PI_MEMORY +
					      sbid * 4 * PIS_PER_SB_E4 + i * 4);

	return ECORE_SUCCESS;
}
