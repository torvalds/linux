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
 *
 * $FreeBSD$
 */

#ifndef __NLM_POE_H__
#define	__NLM_POE_H__

/**
* @file_name poe.h
* @author Netlogic Microsystems
* @brief Basic definitions of XLP Packet Order Engine
*/

/* POE specific registers */
#define	POE_CL0_ENQ_SPILL_BASE_LO	0x0
#define	POE_CL1_ENQ_SPILL_BASE_LO	0x2
#define	POE_CL2_ENQ_SPILL_BASE_LO	0x4
#define	POE_CL3_ENQ_SPILL_BASE_LO	0x6
#define	POE_CL4_ENQ_SPILL_BASE_LO	0x8
#define	POE_CL5_ENQ_SPILL_BASE_LO	0xa
#define	POE_CL6_ENQ_SPILL_BASE_LO	0xc
#define	POE_CL7_ENQ_SPILL_BASE_LO	0xe
#define	POE_CL0_ENQ_SPILL_BASE_HI	0x1
#define	POE_CL1_ENQ_SPILL_BASE_HI	0x3
#define	POE_CL2_ENQ_SPILL_BASE_HI	0x5
#define	POE_CL3_ENQ_SPILL_BASE_HI	0x7
#define	POE_CL4_ENQ_SPILL_BASE_HI	0x9
#define	POE_CL5_ENQ_SPILL_BASE_HI	0xb
#define	POE_CL6_ENQ_SPILL_BASE_HI	0xd
#define	POE_CL7_ENQ_SPILL_BASE_HI	0xf
#define	POE_CL0_DEQ_SPILL_BASE_LO	0x10
#define	POE_CL1_DEQ_SPILL_BASE_LO	0x12
#define	POE_CL2_DEQ_SPILL_BASE_LO	0x14
#define	POE_CL3_DEQ_SPILL_BASE_LO	0x16
#define	POE_CL4_DEQ_SPILL_BASE_LO	0x18
#define	POE_CL5_DEQ_SPILL_BASE_LO	0x1a
#define	POE_CL6_DEQ_SPILL_BASE_LO	0x1c
#define	POE_CL7_DEQ_SPILL_BASE_LO	0x1e
#define	POE_CL0_DEQ_SPILL_BASE_HI	0x11
#define	POE_CL1_DEQ_SPILL_BASE_HI	0x13
#define	POE_CL2_DEQ_SPILL_BASE_HI	0x15
#define	POE_CL3_DEQ_SPILL_BASE_HI	0x17
#define	POE_CL4_DEQ_SPILL_BASE_HI	0x19
#define	POE_CL5_DEQ_SPILL_BASE_HI	0x1b
#define	POE_CL6_DEQ_SPILL_BASE_HI	0x1d
#define	POE_CL7_DEQ_SPILL_BASE_HI	0x1f
#define	POE_MSG_STORAGE_BASE_ADDR_LO	0x20
#define	POE_MSG_STORAGE_BASE_ADDR_HI	0x21
#define	POE_FBP_BASE_ADDR_LO		0x22
#define	POE_FBP_BASE_ADDR_HI		0x23
#define	POE_CL0_ENQ_SPILL_MAXLINE_LO	0x24
#define	POE_CL1_ENQ_SPILL_MAXLINE_LO	0x25
#define	POE_CL2_ENQ_SPILL_MAXLINE_LO	0x26
#define	POE_CL3_ENQ_SPILL_MAXLINE_LO	0x27
#define	POE_CL4_ENQ_SPILL_MAXLINE_LO	0x28
#define	POE_CL5_ENQ_SPILL_MAXLINE_LO	0x29
#define	POE_CL6_ENQ_SPILL_MAXLINE_LO	0x2a
#define	POE_CL7_ENQ_SPILL_MAXLINE_LO	0x2b
#define	POE_CL0_ENQ_SPILL_MAXLINE_HI	0x2c
#define	POE_CL1_ENQ_SPILL_MAXLINE_HI	0x2d
#define	POE_CL2_ENQ_SPILL_MAXLINE_HI	0x2e
#define	POE_CL3_ENQ_SPILL_MAXLINE_HI	0x2f
#define	POE_CL4_ENQ_SPILL_MAXLINE_HI	0x30
#define	POE_CL5_ENQ_SPILL_MAXLINE_HI	0x31
#define	POE_CL6_ENQ_SPILL_MAXLINE_HI	0x32
#define	POE_CL7_ENQ_SPILL_MAXLINE_HI	0x33
#define	POE_MAX_FLOW_MSG0		0x40
#define	POE_MAX_FLOW_MSG1		0x41
#define	POE_MAX_FLOW_MSG2		0x42
#define	POE_MAX_FLOW_MSG3		0x43
#define	POE_MAX_FLOW_MSG4		0x44
#define	POE_MAX_FLOW_MSG5		0x45
#define	POE_MAX_FLOW_MSG6		0x46
#define	POE_MAX_FLOW_MSG7		0x47
#define	POE_MAX_MSG_CL0			0x48
#define	POE_MAX_MSG_CL1			0x49
#define	POE_MAX_MSG_CL2			0x4a
#define	POE_MAX_MSG_CL3			0x4b
#define	POE_MAX_MSG_CL4			0x4c
#define	POE_MAX_MSG_CL5			0x4d
#define	POE_MAX_MSG_CL6			0x4e
#define	POE_MAX_MSG_CL7			0x4f
#define	POE_MAX_LOC_BUF_STG_CL0		0x50
#define	POE_MAX_LOC_BUF_STG_CL1		0x51
#define	POE_MAX_LOC_BUF_STG_CL2		0x52
#define	POE_MAX_LOC_BUF_STG_CL3		0x53
#define	POE_MAX_LOC_BUF_STG_CL4		0x54
#define	POE_MAX_LOC_BUF_STG_CL5		0x55
#define	POE_MAX_LOC_BUF_STG_CL6		0x56
#define	POE_MAX_LOC_BUF_STG_CL7		0x57
#define	POE_ENQ_MSG_COUNT0_SIZE		0x58
#define	POE_ENQ_MSG_COUNT1_SIZE		0x59
#define	POE_ENQ_MSG_COUNT2_SIZE		0x5a
#define	POE_ENQ_MSG_COUNT3_SIZE		0x5b
#define	POE_ENQ_MSG_COUNT4_SIZE		0x5c
#define	POE_ENQ_MSG_COUNT5_SIZE		0x5d
#define	POE_ENQ_MSG_COUNT6_SIZE		0x5e
#define	POE_ENQ_MSG_COUNT7_SIZE		0x5f
#define	POE_ERR_MSG_DESCRIP_LO0		0x60
#define	POE_ERR_MSG_DESCRIP_LO1		0x62
#define	POE_ERR_MSG_DESCRIP_LO2		0x64
#define	POE_ERR_MSG_DESCRIP_LO3		0x66
#define	POE_ERR_MSG_DESCRIP_HI0		0x61
#define	POE_ERR_MSG_DESCRIP_HI1		0x63
#define	POE_ERR_MSG_DESCRIP_HI2		0x65
#define	POE_ERR_MSG_DESCRIP_HI3		0x67
#define	POE_OOO_MSG_CNT_LO		0x68
#define	POE_IN_ORDER_MSG_CNT_LO		0x69
#define	POE_LOC_BUF_STOR_CNT_LO		0x6a
#define	POE_EXT_BUF_STOR_CNT_LO		0x6b
#define	POE_LOC_BUF_ALLOC_CNT_LO	0x6c
#define	POE_EXT_BUF_ALLOC_CNT_LO	0x6d
#define	POE_OOO_MSG_CNT_HI		0x6e
#define	POE_IN_ORDER_MSG_CNT_HI		0x6f
#define	POE_LOC_BUF_STOR_CNT_HI		0x70
#define	POE_EXT_BUF_STOR_CNT_HI		0x71
#define	POE_LOC_BUF_ALLOC_CNT_HI	0x72
#define	POE_EXT_BUF_ALLOC_CNT_HI	0x73
#define	POE_MODE_ERR_FLOW_ID		0x74
#define	POE_STATISTICS_ENABLE		0x75
#define	POE_MAX_SIZE_FLOW		0x76
#define	POE_MAX_SIZE			0x77
#define	POE_FBP_SP			0x78
#define	POE_FBP_SP_EN			0x79
#define	POE_LOC_ALLOC_EN		0x7a
#define	POE_EXT_ALLOC_EN		0x7b
#define	POE_DISTR_0_DROP_CNT		0xc0
#define	POE_DISTR_1_DROP_CNT		0xc1
#define	POE_DISTR_2_DROP_CNT		0xc2
#define	POE_DISTR_3_DROP_CNT		0xc3
#define	POE_DISTR_4_DROP_CNT		0xc4
#define	POE_DISTR_5_DROP_CNT		0xc5
#define	POE_DISTR_6_DROP_CNT		0xc6
#define	POE_DISTR_7_DROP_CNT		0xc7
#define	POE_DISTR_8_DROP_CNT		0xc8
#define	POE_DISTR_9_DROP_CNT		0xc9
#define	POE_DISTR_10_DROP_CNT		0xca
#define	POE_DISTR_11_DROP_CNT		0xcb
#define	POE_DISTR_12_DROP_CNT		0xcc
#define	POE_DISTR_13_DROP_CNT		0xcd
#define	POE_DISTR_14_DROP_CNT		0xce
#define	POE_DISTR_15_DROP_CNT		0xcf
#define	POE_CLASS_0_DROP_CNT		0xd0
#define	POE_CLASS_1_DROP_CNT		0xd1
#define	POE_CLASS_2_DROP_CNT		0xd2
#define	POE_CLASS_3_DROP_CNT		0xd3
#define	POE_CLASS_4_DROP_CNT		0xd4
#define	POE_CLASS_5_DROP_CNT		0xd5
#define	POE_CLASS_6_DROP_CNT		0xd6
#define	POE_CLASS_7_DROP_CNT		0xd7
#define	POE_DISTR_C0_DROP_CNT		0xd8
#define	POE_DISTR_C1_DROP_CNT		0xd9
#define	POE_DISTR_C2_DROP_CNT		0xda
#define	POE_DISTR_C3_DROP_CNT		0xdb
#define	POE_DISTR_C4_DROP_CNT		0xdc
#define	POE_DISTR_C5_DROP_CNT		0xdd
#define	POE_DISTR_C6_DROP_CNT		0xde
#define	POE_DISTR_C7_DROP_CNT		0xdf
#define	POE_CPU_DROP_CNT		0xe0
#define	POE_MAX_FLOW_DROP_CNT		0xe1
#define	POE_INTERRUPT_VEC		0x140
#define	POE_INTERRUPT_MASK		0x141
#define	POE_FATALERR_MASK		0x142
#define	POE_IDI_CFG			0x143
#define	POE_TIMEOUT_VALUE		0x144
#define	POE_CACHE_ALLOC_EN		0x145
#define	POE_FBP_ECC_ERR_CNT		0x146
#define	POE_MSG_STRG_ECC_ERR_CNT	0x147
#define	POE_FID_INFO_ECC_ERR_CNT	0x148
#define	POE_MSG_INFO_ECC_ERR_CNT	0x149
#define	POE_LL_ECC_ERR_CNT		0x14a
#define	POE_SIZE_ECC_ERR_CNT		0x14b
#define	POE_FMN_TXCR_ECC_ERR_CNT	0x14c
#define	POE_ENQ_INSPIL_ECC_ERR_CNT	0x14d
#define	POE_ENQ_OUTSPIL_ECC_ERR_CNT	0x14e
#define	POE_DEQ_OUTSPIL_ECC_ERR_CNT	0x14f
#define	POE_ENQ_MSG_SENT		0x150
#define	POE_ENQ_MSG_CNT			0x151
#define	POE_FID_RDATA			0x152
#define	POE_FID_WDATA			0x153
#define	POE_FID_CMD			0x154
#define	POE_FID_ADDR			0x155
#define	POE_MSG_INFO_CMD		0x156
#define	POE_MSG_INFO_ADDR		0x157
#define	POE_MSG_INFO_RDATA		0x158
#define	POE_LL_CMD			0x159
#define	POE_LL_ADDR			0x15a
#define	POE_LL_RDATA			0x15b
#define	POE_MSG_STG_CMD			0x15c
#define	POE_MSG_STG_ADDR		0x15d
#define	POE_MSG_STG_RDATA		0x15e
#define	POE_DISTR_THRESHOLD_0		0x1c0
#define	POE_DISTR_THRESHOLD_1		0x1c1
#define	POE_DISTR_THRESHOLD_2		0x1c2
#define	POE_DISTR_THRESHOLD_3		0x1c3
#define	POE_DISTR_THRESHOLD_4		0x1c4
#define	POE_DISTR_THRESHOLD(i)		(0x1c0 + (i))
#define	POE_DISTR_EN			0x1c5
#define	POE_ENQ_SPILL_THOLD		0x1c8
#define	POE_DEQ_SPILL_THOLD		0x1c9
#define	POE_DEQ_SPILL_TIMER		0x1ca
#define	POE_DISTR_CLASS_DROP_EN		0x1cb
#define	POE_DISTR_VEC_DROP_EN		0x1cc
#define	POE_DISTR_DROP_TIMER		0x1cd
#define	POE_ERROR_LOG_W0		0x1ce
#define	POE_ERROR_LOG_W1		0x1cf
#define	POE_ERROR_LOG_W2		0x1d0
#define	POE_ERR_INJ_CTRL0		0x1d1
#define	POE_TX_TIMER			0x1d4

#define	NUM_DIST_VEC			16
#define	NUM_WORDS_PER_DV		16
#define	MAX_DV_TBL_ENTRIES		(NUM_DIST_VEC * NUM_WORDS_PER_DV)
#define	POE_DIST_THRESHOLD_VAL		0xa

/*
 * POE distribution vectors
 *
 * Each vector is 512 bit with msb indicating vc 512 and lsb indicating vc 0
 * 512-bit-vector is specified as 16 32-bit words.
 * Left most word has the vc range 511-479 right most word has vc range 31 - 0
 * Each word has the MSB select higer vc number and LSB select lower vc num
 */
#define	POE_DISTVECT_BASE		0x100
#define	POE_DISTVECT(vec)		(POE_DISTVECT_BASE + 16 * (vec))
#define	POE_DISTVECT_OFFSET(node,cpu)	(4 * (3 - (node)) + (3 - (cpu)/8))
#define	POE_DISTVECT_SHIFT(node,cpu)	(((cpu) % 8 ) * 4)

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

#define	nlm_read_poe_reg(b, r)		nlm_read_reg(b, r)
#define	nlm_write_poe_reg(b, r, v)	nlm_write_reg(b, r, v)
#define	nlm_read_poedv_reg(b, r)	nlm_read_reg_xkphys(b, r)
#define	nlm_write_poedv_reg(b, r, v)	nlm_write_reg_xkphys(b, r, v)
#define	nlm_get_poe_pcibase(node)	\
				nlm_pcicfg_base(XLP_IO_POE_OFFSET(node))
#define	nlm_get_poe_regbase(node)	\
			(nlm_get_poe_pcibase(node) + XLP_IO_PCI_HDRSZ)
#define	nlm_get_poedv_regbase(node)	\
			nlm_xkphys_map_pcibar0(nlm_get_poe_pcibase(node))

static __inline int
nlm_poe_max_flows(uint64_t poe_pcibase)
{
	return (nlm_read_reg(poe_pcibase, XLP_PCI_DEVINFO_REG0));
}

/*
 * Helper function, calculate the distribution vector
 * cm0, cm1, cm2, cm3 : CPU masks for nodes 0..3
 * thr_vcmask: destination VCs for a thread
 */
static __inline void
nlm_calc_poe_distvec(uint32_t cm0, uint32_t cm1, uint32_t cm2, uint32_t cm3,
    uint32_t thr_vcmask, uint32_t *distvec)
{
	uint32_t cpumask = 0, val;
	int i, cpu, node, startcpu, index;

	thr_vcmask &= 0xf;
	for (node = 0; node < XLP_MAX_NODES; node++) {
		switch (node) {
		case 0: cpumask = cm0; break;
		case 1: cpumask = cm1; break;
		case 2: cpumask = cm2; break;
		case 3: cpumask = cm3; break;
		}

		for (i = 0; i < 4; i++) {
			val = 0;
			startcpu = 31 - i * 8;
			for (cpu = startcpu; cpu >= startcpu - 7; cpu--) {
				val <<= 4;
				if (cpumask & (1U << cpu))
				    val |= thr_vcmask;
			}
			index = POE_DISTVECT_OFFSET(node, startcpu);
			distvec[index] = val;
		}
	}
}

static __inline int
nlm_write_poe_distvec(uint64_t poedv_base, int vec, uint32_t *distvec)
{
	uint32_t reg;
	int i;

	if (vec < 0 || vec >= NUM_DIST_VEC)
		return (-1);

	for (i = 0; i < NUM_WORDS_PER_DV; i++) {
		reg = POE_DISTVECT(vec) + i;
		nlm_write_poedv_reg(poedv_base, reg, distvec[i]);
	}

	return (0);
}

static __inline void
nlm_config_poe(uint64_t poe_base, uint64_t poedv_base)
{
	uint32_t zerodv[NUM_WORDS_PER_DV];
	int i;

	/* First disable distribution vector logic */
	nlm_write_poe_reg(poe_base, POE_DISTR_EN, 0);

	memset(zerodv, 0, sizeof(zerodv));
	for (i = 0; i < NUM_DIST_VEC; i++)
		nlm_write_poe_distvec(poedv_base, i, zerodv);

	/* set the threshold */
	for (i = 0; i < 5; i++)
		nlm_write_poe_reg(poe_base, POE_DISTR_THRESHOLD(i),
		    POE_DIST_THRESHOLD_VAL);

	nlm_write_poe_reg(poe_base, POE_DISTR_EN, 1);

	/* always enable local message store */
	nlm_write_poe_reg(poe_base, POE_LOC_ALLOC_EN, 1);

	nlm_write_poe_reg(poe_base, POE_TX_TIMER, 0x3);
}
#endif /* !(LOCORE) && !(__ASSEMBLY__) */
#endif
