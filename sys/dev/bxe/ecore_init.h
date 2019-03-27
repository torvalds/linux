/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2017 QLogic Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef ECORE_INIT_H
#define ECORE_INIT_H

/* Init operation types and structures */
enum {
	OP_RD = 0x1,	/* read a single register */
	OP_WR,		/* write a single register */
	OP_SW,		/* copy a string to the device */
	OP_ZR,		/* clear memory */
	OP_ZP,		/* unzip then copy with DMAE */
	OP_WR_64,	/* write 64 bit pattern */
	OP_WB,		/* copy a string using DMAE */
#ifndef FW_ZIP_SUPPORT
	OP_FW,		/* copy an array from fw data (only used with unzipped FW) */
#endif
	OP_WB_ZR,	/* Clear a string using DMAE or indirect-wr */
	OP_IF_MODE_OR,  /* Skip the following ops if all init modes don't match */
	OP_IF_MODE_AND, /* Skip the following ops if any init modes don't match */
	OP_IF_PHASE,
	OP_RT,
	OP_DELAY,
	OP_VERIFY,
	OP_MAX
};

enum {
	STAGE_START,
	STAGE_END,
};

/* Returns the index of start or end of a specific block stage in ops array*/
#define BLOCK_OPS_IDX(block, stage, end) \
	(2*(((block)*NUM_OF_INIT_PHASES) + (stage)) + (end))


/* structs for the various opcodes */
struct raw_op {
	uint32_t op:8;
	uint32_t offset:24;
	uint32_t raw_data;
};

struct op_read {
	uint32_t op:8;
	uint32_t offset:24;
	uint32_t val;
};

struct op_write {
	uint32_t op:8;
	uint32_t offset:24;
	uint32_t val;
};

struct op_arr_write {
	uint32_t op:8;
	uint32_t offset:24;
#ifdef __BIG_ENDIAN
	uint16_t data_len;
	uint16_t data_off;
#else /* __LITTLE_ENDIAN */
	uint16_t data_off;
	uint16_t data_len;
#endif
};

struct op_zero {
	uint32_t op:8;
	uint32_t offset:24;
	uint32_t len;
};

struct op_if_mode {
	uint32_t op:8;
	uint32_t cmd_offset:24;
	uint32_t mode_bit_map;
};

struct op_if_phase {
	uint32_t op:8;
	uint32_t cmd_offset:24;
	uint32_t phase_bit_map;
};

struct op_delay {
	uint32_t op:8;
	uint32_t reserved:24;
	uint32_t delay;
};

union init_op {
	struct op_read		read;
	struct op_write		write;
	struct op_arr_write	arr_wr;
	struct op_zero		zero;
	struct raw_op		raw;
	struct op_if_mode	if_mode;
	struct op_if_phase	if_phase;
	struct op_delay		delay;
};


/* Init Phases */
enum {
	PHASE_COMMON,
	PHASE_PORT0,
	PHASE_PORT1,
	PHASE_PF0,
	PHASE_PF1,
	PHASE_PF2,
	PHASE_PF3,
	PHASE_PF4,
	PHASE_PF5,
	PHASE_PF6,
	PHASE_PF7,
	NUM_OF_INIT_PHASES
};

/* Init Modes */
enum {
	MODE_ASIC                      = 0x00000001,
	MODE_FPGA                      = 0x00000002,
	MODE_EMUL                      = 0x00000004,
	MODE_E2                        = 0x00000008,
	MODE_E3                        = 0x00000010,
	MODE_PORT2                     = 0x00000020,
	MODE_PORT4                     = 0x00000040,
	MODE_SF                        = 0x00000080,
	MODE_MF                        = 0x00000100,
	MODE_MF_SD                     = 0x00000200,
	MODE_MF_SI                     = 0x00000400,
	MODE_MF_AFEX                   = 0x00000800,
	MODE_E3_A0                     = 0x00001000,
	MODE_E3_B0                     = 0x00002000,
	MODE_COS3                      = 0x00004000,
	MODE_COS6                      = 0x00008000,
	MODE_LITTLE_ENDIAN             = 0x00010000,
	MODE_BIG_ENDIAN                = 0x00020000,
};

/* Init Blocks */
enum {
	BLOCK_ATC,
	BLOCK_BRB1,
	BLOCK_CCM,
	BLOCK_CDU,
	BLOCK_CFC,
	BLOCK_CSDM,
	BLOCK_CSEM,
	BLOCK_DBG,
	BLOCK_DMAE,
	BLOCK_DORQ,
	BLOCK_HC,
	BLOCK_IGU,
	BLOCK_MISC,
	BLOCK_NIG,
	BLOCK_PBF,
	BLOCK_PGLUE_B,
	BLOCK_PRS,
	BLOCK_PXP2,
	BLOCK_PXP,
	BLOCK_QM,
	BLOCK_SRC,
	BLOCK_TCM,
	BLOCK_TM,
	BLOCK_TSDM,
	BLOCK_TSEM,
	BLOCK_UCM,
	BLOCK_UPB,
	BLOCK_USDM,
	BLOCK_USEM,
	BLOCK_XCM,
	BLOCK_XPB,
	BLOCK_XSDM,
	BLOCK_XSEM,
	BLOCK_MISC_AEU,
	NUM_OF_INIT_BLOCKS
};








/* Vnics per mode */
#define ECORE_PORT2_MODE_NUM_VNICS 4


/* QM queue numbers */
#define ECORE_ETH_Q		0
#define ECORE_TOE_Q		3
#define ECORE_TOE_ACK_Q		6
#define ECORE_ISCSI_Q		9
#define ECORE_ISCSI_ACK_Q	11
#define ECORE_FCOE_Q		10

/* Vnics per mode */
#define ECORE_PORT4_MODE_NUM_VNICS 2

/* COS offset for port1 in E3 B0 4port mode */
#define ECORE_E3B0_PORT1_COS_OFFSET 3

/* QM Register addresses */
#define ECORE_Q_VOQ_REG_ADDR(pf_q_num)\
	(QM_REG_QVOQIDX_0 + 4 * (pf_q_num))
#define ECORE_VOQ_Q_REG_ADDR(cos, pf_q_num)\
	(QM_REG_VOQQMASK_0_LSB + 4 * ((cos) * 2 + ((pf_q_num) >> 5)))
#define ECORE_Q_CMDQ_REG_ADDR(pf_q_num)\
	(QM_REG_BYTECRDCMDQ_0 + 4 * ((pf_q_num) >> 4))

/* extracts the QM queue number for the specified port and vnic */
#define ECORE_PF_Q_NUM(q_num, port, vnic)\
	((((port) << 1) | (vnic)) * 16 + (q_num))


/* Maps the specified queue to the specified COS */
static inline void ecore_map_q_cos(struct bxe_softc *sc, uint32_t q_num, uint32_t new_cos)
{
	/* find current COS mapping */
	uint32_t curr_cos = REG_RD(sc, QM_REG_QVOQIDX_0 + q_num * 4);

	/* check if queue->COS mapping has changed */
	if (curr_cos != new_cos) {
		uint32_t num_vnics = ECORE_PORT2_MODE_NUM_VNICS;
		uint32_t reg_addr, reg_bit_map, vnic;

		/* update parameters for 4port mode */
		if (INIT_MODE_FLAGS(sc) & MODE_PORT4) {
			num_vnics = ECORE_PORT4_MODE_NUM_VNICS;
			if (PORT_ID(sc)) {
				curr_cos += ECORE_E3B0_PORT1_COS_OFFSET;
				new_cos += ECORE_E3B0_PORT1_COS_OFFSET;
			}
		}

		/* change queue mapping for each VNIC */
		for (vnic = 0; vnic < num_vnics; vnic++) {
			uint32_t pf_q_num =
				ECORE_PF_Q_NUM(q_num, PORT_ID(sc), vnic);
			uint32_t q_bit_map = 1 << (pf_q_num & 0x1f);

			/* overwrite queue->VOQ mapping */
			REG_WR(sc, ECORE_Q_VOQ_REG_ADDR(pf_q_num), new_cos);

			/* clear queue bit from current COS bit map */
			reg_addr = ECORE_VOQ_Q_REG_ADDR(curr_cos, pf_q_num);
			reg_bit_map = REG_RD(sc, reg_addr);
			REG_WR(sc, reg_addr, reg_bit_map & (~q_bit_map));

			/* set queue bit in new COS bit map */
			reg_addr = ECORE_VOQ_Q_REG_ADDR(new_cos, pf_q_num);
			reg_bit_map = REG_RD(sc, reg_addr);
			REG_WR(sc, reg_addr, reg_bit_map | q_bit_map);

			/* set/clear queue bit in command-queue bit map
			(E2/E3A0 only, valid COS values are 0/1) */
			if (!(INIT_MODE_FLAGS(sc) & MODE_E3_B0)) {
				reg_addr = ECORE_Q_CMDQ_REG_ADDR(pf_q_num);
				reg_bit_map = REG_RD(sc, reg_addr);
				q_bit_map = 1 << (2 * (pf_q_num & 0xf));
				reg_bit_map = new_cos ?
					      (reg_bit_map | q_bit_map) :
					      (reg_bit_map & (~q_bit_map));
				REG_WR(sc, reg_addr, reg_bit_map);
			}
		}
	}
}

/* Configures the QM according to the specified per-traffic-type COSes */
static inline void ecore_dcb_config_qm(struct bxe_softc *sc, enum cos_mode mode,
				       struct priority_cos *traffic_cos)
{
	ecore_map_q_cos(sc, ECORE_FCOE_Q,
			traffic_cos[LLFC_TRAFFIC_TYPE_FCOE].cos);
	ecore_map_q_cos(sc, ECORE_ISCSI_Q,
			traffic_cos[LLFC_TRAFFIC_TYPE_ISCSI].cos);
	ecore_map_q_cos(sc, ECORE_ISCSI_ACK_Q,
		traffic_cos[LLFC_TRAFFIC_TYPE_ISCSI].cos);
	if (mode != STATIC_COS) {
		/* required only in OVERRIDE_COS mode */
		ecore_map_q_cos(sc, ECORE_ETH_Q,
				traffic_cos[LLFC_TRAFFIC_TYPE_NW].cos);
		ecore_map_q_cos(sc, ECORE_TOE_Q,
				traffic_cos[LLFC_TRAFFIC_TYPE_NW].cos);
		ecore_map_q_cos(sc, ECORE_TOE_ACK_Q,
				traffic_cos[LLFC_TRAFFIC_TYPE_NW].cos);
	}
}


/*
 * congestion management port init api description
 * the api works as follows:
 * the driver should pass the cmng_init_input struct, the port_init function
 * will prepare the required internal ram structure which will be passed back
 * to the driver (cmng_init) that will write it into the internal ram.
 *
 * IMPORTANT REMARKS:
 * 1. the cmng_init struct does not represent the contiguous internal ram
 *    structure. the driver should use the XSTORM_CMNG_PERPORT_VARS_OFFSET
 *    offset in order to write the port sub struct and the
 *    PFID_FROM_PORT_AND_VNIC offset for writing the vnic sub struct (in other
 *    words - don't use memcpy!).
 * 2. although the cmng_init struct is filled for the maximal vnic number
 *    possible, the driver should only write the valid vnics into the internal
 *    ram according to the appropriate port mode.
 */
#define BITS_TO_BYTES(x) ((x)/8)

/* CMNG constants, as derived from system spec calculations */

/* default MIN rate in case VNIC min rate is configured to zero- 100Mbps */
#define DEF_MIN_RATE 100

/* resolution of the rate shaping timer - 400 usec */
#define RS_PERIODIC_TIMEOUT_USEC 400

/*
 *  number of bytes in single QM arbitration cycle -
 *  coefficient for calculating the fairness timer
 */
#define QM_ARB_BYTES 160000

/* resolution of Min algorithm 1:100 */
#define MIN_RES 100

/*
 *  how many bytes above threshold for
 *  the minimal credit of Min algorithm
 */
#define MIN_ABOVE_THRESH 32768

/*
 *  Fairness algorithm integration time coefficient -
 *  for calculating the actual Tfair
 */
#define T_FAIR_COEF ((MIN_ABOVE_THRESH + QM_ARB_BYTES) * 8 * MIN_RES)

/* Memory of fairness algorithm - 2 cycles */
#define FAIR_MEM 2
#define SAFC_TIMEOUT_USEC 52

#define SDM_TICKS 4


static inline void ecore_init_max(const struct cmng_init_input *input_data,
				  uint32_t r_param, struct cmng_init *ram_data)
{
	uint32_t vnic;
	struct cmng_vnic *vdata = &ram_data->vnic;
	struct cmng_struct_per_port *pdata = &ram_data->port;
	/*
	 * rate shaping per-port variables
	 *  100 micro seconds in SDM ticks = 25
	 *  since each tick is 4 microSeconds
	 */

	pdata->rs_vars.rs_periodic_timeout =
	RS_PERIODIC_TIMEOUT_USEC / SDM_TICKS;

	/* this is the threshold below which no timer arming will occur.
	 *  1.25 coefficient is for the threshold to be a little bigger
	 *  then the real time to compensate for timer in-accuracy
	 */
	pdata->rs_vars.rs_threshold =
	(5 * RS_PERIODIC_TIMEOUT_USEC * r_param)/4;

	/* rate shaping per-vnic variables */
	for (vnic = 0; vnic < ECORE_PORT2_MODE_NUM_VNICS; vnic++) {
		/* global vnic counter */
		vdata->vnic_max_rate[vnic].vn_counter.rate =
		input_data->vnic_max_rate[vnic];
		/*
		 * maximal Mbps for this vnic
		 * the quota in each timer period - number of bytes
		 * transmitted in this period
		 */
		vdata->vnic_max_rate[vnic].vn_counter.quota =
			RS_PERIODIC_TIMEOUT_USEC *
			(uint32_t)vdata->vnic_max_rate[vnic].vn_counter.rate / 8;
	}

}

static inline void ecore_init_max_per_vn(uint16_t vnic_max_rate,
				  struct rate_shaping_vars_per_vn *ram_data)
{
	/* global vnic counter */
	ram_data->vn_counter.rate = vnic_max_rate;

	/*
	* maximal Mbps for this vnic
	* the quota in each timer period - number of bytes
	* transmitted in this period
	*/
	ram_data->vn_counter.quota =
		RS_PERIODIC_TIMEOUT_USEC * (uint32_t)vnic_max_rate / 8;
}

static inline void ecore_init_min(const struct cmng_init_input *input_data,
				  uint32_t r_param, struct cmng_init *ram_data)
{
	uint32_t vnic, fair_periodic_timeout_usec, vnicWeightSum, tFair;
	struct cmng_vnic *vdata = &ram_data->vnic;
	struct cmng_struct_per_port *pdata = &ram_data->port;

	/* this is the resolution of the fairness timer */
	fair_periodic_timeout_usec = QM_ARB_BYTES / r_param;

	/*
	 * fairness per-port variables
	 * for 10G it is 1000usec. for 1G it is 10000usec.
	 */
	tFair = T_FAIR_COEF / input_data->port_rate;

	/* this is the threshold below which we won't arm the timer anymore */
	pdata->fair_vars.fair_threshold = QM_ARB_BYTES;

	/*
	 *  we multiply by 1e3/8 to get bytes/msec. We don't want the credits
	 *  to pass a credit of the T_FAIR*FAIR_MEM (algorithm resolution)
	 */
	pdata->fair_vars.upper_bound = r_param * tFair * FAIR_MEM;

	/* since each tick is 4 microSeconds */
	pdata->fair_vars.fairness_timeout =
				fair_periodic_timeout_usec / SDM_TICKS;

	/* calculate sum of weights */
	vnicWeightSum = 0;

	for (vnic = 0; vnic < ECORE_PORT2_MODE_NUM_VNICS; vnic++)
		vnicWeightSum += input_data->vnic_min_rate[vnic];

	/* global vnic counter */
	if (vnicWeightSum > 0) {
		/* fairness per-vnic variables */
		for (vnic = 0; vnic < ECORE_PORT2_MODE_NUM_VNICS; vnic++) {
			/*
			 *  this is the credit for each period of the fairness
			 *  algorithm - number of bytes in T_FAIR (this vnic
			 *  share of the port rate)
			 */
			vdata->vnic_min_rate[vnic].vn_credit_delta =
				((uint32_t)(input_data->vnic_min_rate[vnic]) * 100 *
				(T_FAIR_COEF / (8 * 100 * vnicWeightSum)));
			if (vdata->vnic_min_rate[vnic].vn_credit_delta <
			    pdata->fair_vars.fair_threshold +
			    MIN_ABOVE_THRESH) {
				vdata->vnic_min_rate[vnic].vn_credit_delta =
					pdata->fair_vars.fair_threshold +
					MIN_ABOVE_THRESH;
			}
		}
	}
}

static inline void ecore_init_fw_wrr(const struct cmng_init_input *input_data,
				     uint32_t r_param, struct cmng_init *ram_data)
{
	uint32_t vnic, cos;
	uint32_t cosWeightSum = 0;
	struct cmng_vnic *vdata = &ram_data->vnic;
	struct cmng_struct_per_port *pdata = &ram_data->port;

	for (cos = 0; cos < MAX_COS_NUMBER; cos++)
		cosWeightSum += input_data->cos_min_rate[cos];

	if (cosWeightSum > 0) {

		for (vnic = 0; vnic < ECORE_PORT2_MODE_NUM_VNICS; vnic++) {
			/*
			 *  Since cos and vnic shouldn't work together the rate
			 *  to divide between the coses is the port rate.
			 */
			uint32_t *ccd = vdata->vnic_min_rate[vnic].cos_credit_delta;
			for (cos = 0; cos < MAX_COS_NUMBER; cos++) {
				/*
				 * this is the credit for each period of
				 * the fairness algorithm - number of bytes
				 * in T_FAIR (this cos share of the vnic rate)
				 */
				ccd[cos] =
				    ((uint32_t)input_data->cos_min_rate[cos] * 100 *
				    (T_FAIR_COEF / (8 * 100 * cosWeightSum)));
				 if (ccd[cos] < pdata->fair_vars.fair_threshold
						+ MIN_ABOVE_THRESH) {
					ccd[cos] =
					    pdata->fair_vars.fair_threshold +
					    MIN_ABOVE_THRESH;
				}
			}
		}
	}
}

static inline void ecore_init_safc(const struct cmng_init_input *input_data,
				   struct cmng_init *ram_data)
{
	/* in microSeconds */
	ram_data->port.safc_vars.safc_timeout_usec = SAFC_TIMEOUT_USEC;
}

/* Congestion management port init */
static inline void ecore_init_cmng(const struct cmng_init_input *input_data,
				   struct cmng_init *ram_data)
{
	uint32_t r_param;
	ECORE_MEMSET(ram_data, 0,sizeof(struct cmng_init));

	ram_data->port.flags = input_data->flags;

	/*
	 *  number of bytes transmitted in a rate of 10Gbps
	 *  in one usec = 1.25KB.
	 */
	r_param = BITS_TO_BYTES(input_data->port_rate);
	ecore_init_max(input_data, r_param, ram_data);
	ecore_init_min(input_data, r_param, ram_data);
	ecore_init_fw_wrr(input_data, r_param, ram_data);
	ecore_init_safc(input_data, ram_data);
}




/* Returns the index of start or end of a specific block stage in ops array*/
#define BLOCK_OPS_IDX(block, stage, end) \
			(2*(((block)*NUM_OF_INIT_PHASES) + (stage)) + (end))


#define INITOP_SET		0	/* set the HW directly */
#define INITOP_CLEAR		1	/* clear the HW directly */
#define INITOP_INIT		2	/* set the init-value array */

/****************************************************************************
* ILT management
****************************************************************************/
struct ilt_line {
	ecore_dma_addr_t page_mapping;
	void *page;
	uint32_t size;
};

struct ilt_client_info {
	uint32_t page_size;
	uint16_t start;
	uint16_t end;
	uint16_t client_num;
	uint16_t flags;
#define ILT_CLIENT_SKIP_INIT	0x1
#define ILT_CLIENT_SKIP_MEM	0x2
};

struct ecore_ilt {
	uint32_t start_line;
	struct ilt_line		*lines;
	struct ilt_client_info	clients[4];
#define ILT_CLIENT_CDU	0
#define ILT_CLIENT_QM	1
#define ILT_CLIENT_SRC	2
#define ILT_CLIENT_TM	3
};

/****************************************************************************
* SRC configuration
****************************************************************************/
struct src_ent {
	uint8_t opaque[56];
	uint64_t next;
};

/****************************************************************************
* Parity configuration
****************************************************************************/
#define BLOCK_PRTY_INFO(block, en_mask, m1, m1h, m2, m3) \
{ \
	block##_REG_##block##_PRTY_MASK, \
	block##_REG_##block##_PRTY_STS_CLR, \
	en_mask, {m1, m1h, m2, m3}, #block \
}

#define BLOCK_PRTY_INFO_0(block, en_mask, m1, m1h, m2, m3) \
{ \
	block##_REG_##block##_PRTY_MASK_0, \
	block##_REG_##block##_PRTY_STS_CLR_0, \
	en_mask, {m1, m1h, m2, m3}, #block"_0" \
}

#define BLOCK_PRTY_INFO_1(block, en_mask, m1, m1h, m2, m3) \
{ \
	block##_REG_##block##_PRTY_MASK_1, \
	block##_REG_##block##_PRTY_STS_CLR_1, \
	en_mask, {m1, m1h, m2, m3}, #block"_1" \
}

static const struct {
	uint32_t mask_addr;
	uint32_t sts_clr_addr;
	uint32_t en_mask;		/* Mask to enable parity attentions */
	struct {
		uint32_t e1;		/* 57710 */
		uint32_t e1h;	/* 57711 */
		uint32_t e2;		/* 57712 */
		uint32_t e3;		/* 578xx */
	} reg_mask;		/* Register mask (all valid bits) */
	char name[8];		/* Block's longest name is 7 characters long
				 * (name + suffix)
				 */
} ecore_blocks_parity_data[] = {
	/* bit 19 masked */
	/* REG_WR(bp, PXP_REG_PXP_PRTY_MASK, 0x80000); */
	/* bit 5,18,20-31 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_0, 0xfff40020); */
	/* bit 5 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_1, 0x20);	*/
	/* REG_WR(bp, HC_REG_HC_PRTY_MASK, 0x0); */
	/* REG_WR(bp, MISC_REG_MISC_PRTY_MASK, 0x0); */

	/* Block IGU, MISC, PXP and PXP2 parity errors as long as we don't
	 * want to handle "system kill" flow at the moment.
	 */
	BLOCK_PRTY_INFO(PXP, 0x7ffffff, 0x3ffffff, 0x3ffffff, 0x7ffffff,
			0x7ffffff),
	BLOCK_PRTY_INFO_0(PXP2,	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(PXP2,	0x1ffffff, 0x7f, 0x7f, 0x7ff, 0x1ffffff),
	BLOCK_PRTY_INFO(HC, 0x7, 0x7, 0x7, 0, 0),
	BLOCK_PRTY_INFO(NIG, 0xffffffff, 0x3fffffff, 0xffffffff, 0, 0),
	BLOCK_PRTY_INFO_0(NIG,	0xffffffff, 0, 0, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(NIG,	0xffff, 0, 0, 0xff, 0xffff),
	BLOCK_PRTY_INFO(IGU, 0x7ff, 0, 0, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(MISC, 0x1, 0x1, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(QM, 0, 0x1ff, 0xfff, 0xfff, 0xfff),
	BLOCK_PRTY_INFO(ATC, 0x1f, 0, 0, 0x1f, 0x1f),
	BLOCK_PRTY_INFO(PGLUE_B, 0x3, 0, 0, 0x3, 0x3),
	BLOCK_PRTY_INFO(DORQ, 0, 0x3, 0x3, 0x3, 0x3),
	{GRCBASE_UPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_UPB + PB_REG_PB_PRTY_STS_CLR, 0xf,
		{0xf, 0xf, 0xf, 0xf}, "UPB"},
	{GRCBASE_XPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_XPB + PB_REG_PB_PRTY_STS_CLR, 0,
		{0xf, 0xf, 0xf, 0xf}, "XPB"},
	BLOCK_PRTY_INFO(SRC, 0x4, 0x7, 0x7, 0x7, 0x7),
	BLOCK_PRTY_INFO(CDU, 0, 0x1f, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO(CFC, 0, 0xf, 0xf, 0xf, 0x3f),
	BLOCK_PRTY_INFO(DBG, 0, 0x1, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(DMAE, 0, 0xf, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(BRB1, 0, 0xf, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(PRS, (1<<6), 0xff, 0xff, 0xff, 0xff),
	BLOCK_PRTY_INFO(PBF, 0, 0, 0x3ffff, 0xfffff, 0xfffffff),
	BLOCK_PRTY_INFO(TM, 0, 0, 0x7f, 0x7f, 0x7f),
	BLOCK_PRTY_INFO(TSDM, 0x18, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(CSDM, 0x8, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(USDM, 0x38, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(XSDM, 0x8, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(TCM, 0, 0, 0x7ffffff, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(CCM, 0, 0, 0x7ffffff, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(UCM, 0, 0, 0x7ffffff, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(XCM, 0, 0, 0x3fffffff, 0x3fffffff, 0x3fffffff),
	BLOCK_PRTY_INFO_0(TSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(TSEM, 0, 0x3, 0x1f, 0x3f, 0x3f),
	BLOCK_PRTY_INFO_0(USEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(USEM, 0, 0x3, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(CSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(CSEM, 0, 0x3, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(XSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(XSEM, 0, 0x3, 0x1f, 0x3f, 0x3f),
};


/* [28] MCP Latched rom_parity
 * [29] MCP Latched ump_rx_parity
 * [30] MCP Latched ump_tx_parity
 * [31] MCP Latched scpad_parity
 */
#define MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS	\
	(AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY)

#define MISC_AEU_ENABLE_MCP_PRTY_BITS	\
	(MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY)

/* Below registers control the MCP parity attention output. When
 * MISC_AEU_ENABLE_MCP_PRTY_BITS are set - attentions are
 * enabled, when cleared - disabled.
 */
static const struct {
	uint32_t addr;
	uint32_t bits;
} mcp_attn_ctl_regs[] = {
	{ MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0,
		MISC_AEU_ENABLE_MCP_PRTY_BITS },
	{ MISC_REG_AEU_ENABLE4_NIG_0,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS },
	{ MISC_REG_AEU_ENABLE4_PXP_0,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS },
	{ MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0,
		MISC_AEU_ENABLE_MCP_PRTY_BITS },
	{ MISC_REG_AEU_ENABLE4_NIG_1,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS },
	{ MISC_REG_AEU_ENABLE4_PXP_1,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS }
};

static inline void ecore_set_mcp_parity(struct bxe_softc *sc, uint8_t enable)
{
	int i;
	uint32_t reg_val;

	for (i = 0; i < ARRSIZE(mcp_attn_ctl_regs); i++) {
		reg_val = REG_RD(sc, mcp_attn_ctl_regs[i].addr);

		if (enable)
			reg_val |= MISC_AEU_ENABLE_MCP_PRTY_BITS; /* Linux is using mcp_attn_ctl_regs[i].bits */
		else
			reg_val &= ~MISC_AEU_ENABLE_MCP_PRTY_BITS; /* Linux is using mcp_attn_ctl_regs[i].bits */

		REG_WR(sc, mcp_attn_ctl_regs[i].addr, reg_val);
	}
}

static inline uint32_t ecore_parity_reg_mask(struct bxe_softc *sc, int idx)
{
	if (CHIP_IS_E1(sc))
		return ecore_blocks_parity_data[idx].reg_mask.e1;
	else if (CHIP_IS_E1H(sc))
		return ecore_blocks_parity_data[idx].reg_mask.e1h;
	else if (CHIP_IS_E2(sc))
		return ecore_blocks_parity_data[idx].reg_mask.e2;
	else /* CHIP_IS_E3 */
		return ecore_blocks_parity_data[idx].reg_mask.e3;
}

static inline void ecore_disable_blocks_parity(struct bxe_softc *sc)
{
	int i;

	for (i = 0; i < ARRSIZE(ecore_blocks_parity_data); i++) {
		uint32_t dis_mask = ecore_parity_reg_mask(sc, i);

		if (dis_mask) {
			REG_WR(sc, ecore_blocks_parity_data[i].mask_addr,
			       dis_mask);
			ECORE_MSG(sc, "Setting parity mask "
						 "for %s to\t\t0x%x\n",
				    ecore_blocks_parity_data[i].name, dis_mask);
		}
	}

	/* Disable MCP parity attentions */
	ecore_set_mcp_parity(sc, FALSE);
}

/**
 * Clear the parity error status registers.
 */
static inline void ecore_clear_blocks_parity(struct bxe_softc *sc)
{
	int i;
	uint32_t reg_val, mcp_aeu_bits =
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY;

	/* Clear SEM_FAST parities */
	REG_WR(sc, XSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(sc, TSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(sc, USEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(sc, CSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);

	for (i = 0; i < ARRSIZE(ecore_blocks_parity_data); i++) {
		uint32_t reg_mask = ecore_parity_reg_mask(sc, i);

		if (reg_mask) {
			reg_val = REG_RD(sc, ecore_blocks_parity_data[i].
					 sts_clr_addr);
			if (reg_val & reg_mask)
				ECORE_MSG(sc,
					   "Parity errors in %s: 0x%x\n",
					   ecore_blocks_parity_data[i].name,
					   reg_val & reg_mask);
		}
	}

	/* Check if there were parity attentions in MCP */
	reg_val = REG_RD(sc, MISC_REG_AEU_AFTER_INVERT_4_MCP);
	if (reg_val & mcp_aeu_bits)
		ECORE_MSG(sc, "Parity error in MCP: 0x%x\n",
			   reg_val & mcp_aeu_bits);

	/* Clear parity attentions in MCP:
	 * [7]  clears Latched rom_parity
	 * [8]  clears Latched ump_rx_parity
	 * [9]  clears Latched ump_tx_parity
	 * [10] clears Latched scpad_parity (both ports)
	 */
	REG_WR(sc, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x780);
}

static inline void ecore_enable_blocks_parity(struct bxe_softc *sc)
{
	int i;

	for (i = 0; i < ARRSIZE(ecore_blocks_parity_data); i++) {
		uint32_t reg_mask = ecore_parity_reg_mask(sc, i);

		if (reg_mask)
			REG_WR(sc, ecore_blocks_parity_data[i].mask_addr,
				ecore_blocks_parity_data[i].en_mask & reg_mask);
	}

	/* Enable MCP parity attentions */
	ecore_set_mcp_parity(sc, TRUE);
}


#endif /* ECORE_INIT_H */

