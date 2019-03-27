/***********************license start***************
 * Copyright (c) 2003-2012  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * cvmx-sso-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon sso.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SSO_DEFS_H__
#define __CVMX_SSO_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_ACTIVE_CYCLES CVMX_SSO_ACTIVE_CYCLES_FUNC()
static inline uint64_t CVMX_SSO_ACTIVE_CYCLES_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_ACTIVE_CYCLES not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010E8ull);
}
#else
#define CVMX_SSO_ACTIVE_CYCLES (CVMX_ADD_IO_SEG(0x00016700000010E8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_BIST_STAT CVMX_SSO_BIST_STAT_FUNC()
static inline uint64_t CVMX_SSO_BIST_STAT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_BIST_STAT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001078ull);
}
#else
#define CVMX_SSO_BIST_STAT (CVMX_ADD_IO_SEG(0x0001670000001078ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_CFG CVMX_SSO_CFG_FUNC()
static inline uint64_t CVMX_SSO_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001088ull);
}
#else
#define CVMX_SSO_CFG (CVMX_ADD_IO_SEG(0x0001670000001088ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_DS_PC CVMX_SSO_DS_PC_FUNC()
static inline uint64_t CVMX_SSO_DS_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_DS_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001070ull);
}
#else
#define CVMX_SSO_DS_PC (CVMX_ADD_IO_SEG(0x0001670000001070ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_ERR CVMX_SSO_ERR_FUNC()
static inline uint64_t CVMX_SSO_ERR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_ERR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001038ull);
}
#else
#define CVMX_SSO_ERR (CVMX_ADD_IO_SEG(0x0001670000001038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_ERR_ENB CVMX_SSO_ERR_ENB_FUNC()
static inline uint64_t CVMX_SSO_ERR_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_ERR_ENB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001030ull);
}
#else
#define CVMX_SSO_ERR_ENB (CVMX_ADD_IO_SEG(0x0001670000001030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_FIDX_ECC_CTL CVMX_SSO_FIDX_ECC_CTL_FUNC()
static inline uint64_t CVMX_SSO_FIDX_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_FIDX_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010D0ull);
}
#else
#define CVMX_SSO_FIDX_ECC_CTL (CVMX_ADD_IO_SEG(0x00016700000010D0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_FIDX_ECC_ST CVMX_SSO_FIDX_ECC_ST_FUNC()
static inline uint64_t CVMX_SSO_FIDX_ECC_ST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_FIDX_ECC_ST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010D8ull);
}
#else
#define CVMX_SSO_FIDX_ECC_ST (CVMX_ADD_IO_SEG(0x00016700000010D8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_FPAGE_CNT CVMX_SSO_FPAGE_CNT_FUNC()
static inline uint64_t CVMX_SSO_FPAGE_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_FPAGE_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001090ull);
}
#else
#define CVMX_SSO_FPAGE_CNT (CVMX_ADD_IO_SEG(0x0001670000001090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_GWE_CFG CVMX_SSO_GWE_CFG_FUNC()
static inline uint64_t CVMX_SSO_GWE_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_GWE_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001098ull);
}
#else
#define CVMX_SSO_GWE_CFG (CVMX_ADD_IO_SEG(0x0001670000001098ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_IDX_ECC_CTL CVMX_SSO_IDX_ECC_CTL_FUNC()
static inline uint64_t CVMX_SSO_IDX_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_IDX_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010C0ull);
}
#else
#define CVMX_SSO_IDX_ECC_CTL (CVMX_ADD_IO_SEG(0x00016700000010C0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_IDX_ECC_ST CVMX_SSO_IDX_ECC_ST_FUNC()
static inline uint64_t CVMX_SSO_IDX_ECC_ST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_IDX_ECC_ST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010C8ull);
}
#else
#define CVMX_SSO_IDX_ECC_ST (CVMX_ADD_IO_SEG(0x00016700000010C8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_IQ_CNTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_IQ_CNTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000009000ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_IQ_CNTX(offset) (CVMX_ADD_IO_SEG(0x0001670000009000ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_IQ_COM_CNT CVMX_SSO_IQ_COM_CNT_FUNC()
static inline uint64_t CVMX_SSO_IQ_COM_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_IQ_COM_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001058ull);
}
#else
#define CVMX_SSO_IQ_COM_CNT (CVMX_ADD_IO_SEG(0x0001670000001058ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_IQ_INT CVMX_SSO_IQ_INT_FUNC()
static inline uint64_t CVMX_SSO_IQ_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_IQ_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001048ull);
}
#else
#define CVMX_SSO_IQ_INT (CVMX_ADD_IO_SEG(0x0001670000001048ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_IQ_INT_EN CVMX_SSO_IQ_INT_EN_FUNC()
static inline uint64_t CVMX_SSO_IQ_INT_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_IQ_INT_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001050ull);
}
#else
#define CVMX_SSO_IQ_INT_EN (CVMX_ADD_IO_SEG(0x0001670000001050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_IQ_THRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_IQ_THRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x000167000000A000ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_IQ_THRX(offset) (CVMX_ADD_IO_SEG(0x000167000000A000ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_NOS_CNT CVMX_SSO_NOS_CNT_FUNC()
static inline uint64_t CVMX_SSO_NOS_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_NOS_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001040ull);
}
#else
#define CVMX_SSO_NOS_CNT (CVMX_ADD_IO_SEG(0x0001670000001040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_NW_TIM CVMX_SSO_NW_TIM_FUNC()
static inline uint64_t CVMX_SSO_NW_TIM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_NW_TIM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001028ull);
}
#else
#define CVMX_SSO_NW_TIM (CVMX_ADD_IO_SEG(0x0001670000001028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_OTH_ECC_CTL CVMX_SSO_OTH_ECC_CTL_FUNC()
static inline uint64_t CVMX_SSO_OTH_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_OTH_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010B0ull);
}
#else
#define CVMX_SSO_OTH_ECC_CTL (CVMX_ADD_IO_SEG(0x00016700000010B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_OTH_ECC_ST CVMX_SSO_OTH_ECC_ST_FUNC()
static inline uint64_t CVMX_SSO_OTH_ECC_ST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_OTH_ECC_ST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010B8ull);
}
#else
#define CVMX_SSO_OTH_ECC_ST (CVMX_ADD_IO_SEG(0x00016700000010B8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_PND_ECC_CTL CVMX_SSO_PND_ECC_CTL_FUNC()
static inline uint64_t CVMX_SSO_PND_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_PND_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010A0ull);
}
#else
#define CVMX_SSO_PND_ECC_CTL (CVMX_ADD_IO_SEG(0x00016700000010A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_PND_ECC_ST CVMX_SSO_PND_ECC_ST_FUNC()
static inline uint64_t CVMX_SSO_PND_ECC_ST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_PND_ECC_ST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010A8ull);
}
#else
#define CVMX_SSO_PND_ECC_ST (CVMX_ADD_IO_SEG(0x00016700000010A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_PPX_GRP_MSK(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_SSO_PPX_GRP_MSK(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000006000ull) + ((offset) & 31) * 8;
}
#else
#define CVMX_SSO_PPX_GRP_MSK(offset) (CVMX_ADD_IO_SEG(0x0001670000006000ull) + ((offset) & 31) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_PPX_QOS_PRI(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_SSO_PPX_QOS_PRI(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000003000ull) + ((offset) & 31) * 8;
}
#else
#define CVMX_SSO_PPX_QOS_PRI(offset) (CVMX_ADD_IO_SEG(0x0001670000003000ull) + ((offset) & 31) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_PP_STRICT CVMX_SSO_PP_STRICT_FUNC()
static inline uint64_t CVMX_SSO_PP_STRICT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_PP_STRICT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010E0ull);
}
#else
#define CVMX_SSO_PP_STRICT (CVMX_ADD_IO_SEG(0x00016700000010E0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_QOSX_RND(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_QOSX_RND(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000002000ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_QOSX_RND(offset) (CVMX_ADD_IO_SEG(0x0001670000002000ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_QOS_THRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_QOS_THRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x000167000000B000ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_QOS_THRX(offset) (CVMX_ADD_IO_SEG(0x000167000000B000ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_QOS_WE CVMX_SSO_QOS_WE_FUNC()
static inline uint64_t CVMX_SSO_QOS_WE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_QOS_WE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001080ull);
}
#else
#define CVMX_SSO_QOS_WE (CVMX_ADD_IO_SEG(0x0001670000001080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_RESET CVMX_SSO_RESET_FUNC()
static inline uint64_t CVMX_SSO_RESET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_RESET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000010F0ull);
}
#else
#define CVMX_SSO_RESET (CVMX_ADD_IO_SEG(0x00016700000010F0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_RWQ_HEAD_PTRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_RWQ_HEAD_PTRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x000167000000C000ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_RWQ_HEAD_PTRX(offset) (CVMX_ADD_IO_SEG(0x000167000000C000ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_RWQ_POP_FPTR CVMX_SSO_RWQ_POP_FPTR_FUNC()
static inline uint64_t CVMX_SSO_RWQ_POP_FPTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_RWQ_POP_FPTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x000167000000C408ull);
}
#else
#define CVMX_SSO_RWQ_POP_FPTR (CVMX_ADD_IO_SEG(0x000167000000C408ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_RWQ_PSH_FPTR CVMX_SSO_RWQ_PSH_FPTR_FUNC()
static inline uint64_t CVMX_SSO_RWQ_PSH_FPTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_RWQ_PSH_FPTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x000167000000C400ull);
}
#else
#define CVMX_SSO_RWQ_PSH_FPTR (CVMX_ADD_IO_SEG(0x000167000000C400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_RWQ_TAIL_PTRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_RWQ_TAIL_PTRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x000167000000C200ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_RWQ_TAIL_PTRX(offset) (CVMX_ADD_IO_SEG(0x000167000000C200ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_TS_PC CVMX_SSO_TS_PC_FUNC()
static inline uint64_t CVMX_SSO_TS_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_TS_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001068ull);
}
#else
#define CVMX_SSO_TS_PC (CVMX_ADD_IO_SEG(0x0001670000001068ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_WA_COM_PC CVMX_SSO_WA_COM_PC_FUNC()
static inline uint64_t CVMX_SSO_WA_COM_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_WA_COM_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001060ull);
}
#else
#define CVMX_SSO_WA_COM_PC (CVMX_ADD_IO_SEG(0x0001670000001060ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_WA_PCX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_SSO_WA_PCX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000005000ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_SSO_WA_PCX(offset) (CVMX_ADD_IO_SEG(0x0001670000005000ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_WQ_INT CVMX_SSO_WQ_INT_FUNC()
static inline uint64_t CVMX_SSO_WQ_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_WQ_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001000ull);
}
#else
#define CVMX_SSO_WQ_INT (CVMX_ADD_IO_SEG(0x0001670000001000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_WQ_INT_CNTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_SSO_WQ_INT_CNTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000008000ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_SSO_WQ_INT_CNTX(offset) (CVMX_ADD_IO_SEG(0x0001670000008000ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_WQ_INT_PC CVMX_SSO_WQ_INT_PC_FUNC()
static inline uint64_t CVMX_SSO_WQ_INT_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_WQ_INT_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001020ull);
}
#else
#define CVMX_SSO_WQ_INT_PC (CVMX_ADD_IO_SEG(0x0001670000001020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_WQ_INT_THRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_SSO_WQ_INT_THRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000007000ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_SSO_WQ_INT_THRX(offset) (CVMX_ADD_IO_SEG(0x0001670000007000ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SSO_WQ_IQ_DIS CVMX_SSO_WQ_IQ_DIS_FUNC()
static inline uint64_t CVMX_SSO_WQ_IQ_DIS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_SSO_WQ_IQ_DIS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000001010ull);
}
#else
#define CVMX_SSO_WQ_IQ_DIS (CVMX_ADD_IO_SEG(0x0001670000001010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SSO_WS_PCX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_SSO_WS_PCX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000004000ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_SSO_WS_PCX(offset) (CVMX_ADD_IO_SEG(0x0001670000004000ull) + ((offset) & 63) * 8)
#endif

/**
 * cvmx_sso_active_cycles
 *
 * SSO_ACTIVE_CYCLES = SSO cycles SSO active
 *
 * This register counts every sclk cycle that the SSO clocks are active.
 * **NOTE: Added in pass 2.0
 */
union cvmx_sso_active_cycles {
	uint64_t u64;
	struct cvmx_sso_active_cycles_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t act_cyc                      : 64; /**< Counts number of active cycles. */
#else
	uint64_t act_cyc                      : 64;
#endif
	} s;
	struct cvmx_sso_active_cycles_s       cn68xx;
};
typedef union cvmx_sso_active_cycles cvmx_sso_active_cycles_t;

/**
 * cvmx_sso_bist_stat
 *
 * SSO_BIST_STAT = SSO BIST Status Register
 *
 * Contains the BIST status for the SSO memories ('0' = pass, '1' = fail).
 * Note that PP BIST status is not reported here as it was in previous designs.
 *
 *   There may be more for DDR interface buffers.
 *   It's possible that a RAM will be used for SSO_PP_QOS_RND.
 */
union cvmx_sso_bist_stat {
	uint64_t u64;
	struct cvmx_sso_bist_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_62_63               : 2;
	uint64_t odu_pref                     : 2;  /**< ODU Prefetch memory BIST status */
	uint64_t reserved_54_59               : 6;
	uint64_t fptr                         : 2;  /**< FPTR memory BIST status */
	uint64_t reserved_45_51               : 7;
	uint64_t rwo_dat                      : 1;  /**< RWO_DAT memory BIST status */
	uint64_t rwo                          : 2;  /**< RWO memory BIST status */
	uint64_t reserved_35_41               : 7;
	uint64_t rwi_dat                      : 1;  /**< RWI_DAT memory BIST status */
	uint64_t reserved_32_33               : 2;
	uint64_t soc                          : 1;  /**< SSO CAM BIST status */
	uint64_t reserved_28_30               : 3;
	uint64_t ncbo                         : 4;  /**< NCBO transmitter memory BIST status */
	uint64_t reserved_21_23               : 3;
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t reserved_17_19               : 3;
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t reserved_10_15               : 6;
	uint64_t pend                         : 2;  /**< Pending switch memory BIST status */
	uint64_t reserved_2_7                 : 6;
	uint64_t oth                          : 2;  /**< WQP, GRP memory BIST status */
#else
	uint64_t oth                          : 2;
	uint64_t reserved_2_7                 : 6;
	uint64_t pend                         : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t fidx                         : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t index                        : 1;
	uint64_t reserved_21_23               : 3;
	uint64_t ncbo                         : 4;
	uint64_t reserved_28_30               : 3;
	uint64_t soc                          : 1;
	uint64_t reserved_32_33               : 2;
	uint64_t rwi_dat                      : 1;
	uint64_t reserved_35_41               : 7;
	uint64_t rwo                          : 2;
	uint64_t rwo_dat                      : 1;
	uint64_t reserved_45_51               : 7;
	uint64_t fptr                         : 2;
	uint64_t reserved_54_59               : 6;
	uint64_t odu_pref                     : 2;
	uint64_t reserved_62_63               : 2;
#endif
	} s;
	struct cvmx_sso_bist_stat_s           cn68xx;
	struct cvmx_sso_bist_stat_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t fptr                         : 2;  /**< FPTR memory BIST status */
	uint64_t reserved_45_51               : 7;
	uint64_t rwo_dat                      : 1;  /**< RWO_DAT memory BIST status */
	uint64_t rwo                          : 2;  /**< RWO memory BIST status */
	uint64_t reserved_35_41               : 7;
	uint64_t rwi_dat                      : 1;  /**< RWI_DAT memory BIST status */
	uint64_t reserved_32_33               : 2;
	uint64_t soc                          : 1;  /**< SSO CAM BIST status */
	uint64_t reserved_28_30               : 3;
	uint64_t ncbo                         : 4;  /**< NCBO transmitter memory BIST status */
	uint64_t reserved_21_23               : 3;
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t reserved_17_19               : 3;
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t reserved_10_15               : 6;
	uint64_t pend                         : 2;  /**< Pending switch memory BIST status */
	uint64_t reserved_2_7                 : 6;
	uint64_t oth                          : 2;  /**< WQP, GRP memory BIST status */
#else
	uint64_t oth                          : 2;
	uint64_t reserved_2_7                 : 6;
	uint64_t pend                         : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t fidx                         : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t index                        : 1;
	uint64_t reserved_21_23               : 3;
	uint64_t ncbo                         : 4;
	uint64_t reserved_28_30               : 3;
	uint64_t soc                          : 1;
	uint64_t reserved_32_33               : 2;
	uint64_t rwi_dat                      : 1;
	uint64_t reserved_35_41               : 7;
	uint64_t rwo                          : 2;
	uint64_t rwo_dat                      : 1;
	uint64_t reserved_45_51               : 7;
	uint64_t fptr                         : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} cn68xxp1;
};
typedef union cvmx_sso_bist_stat cvmx_sso_bist_stat_t;

/**
 * cvmx_sso_cfg
 *
 * SSO_CFG = SSO Config
 *
 * This register is an assortment of various SSO configuration bits.
 */
union cvmx_sso_cfg {
	uint64_t u64;
	struct cvmx_sso_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t qck_gw_rsp_adj               : 3;  /**< Fast GET_WORK response fine adjustment
                                                         Allowed values are 0, 1, and 2 (0 is quickest) */
	uint64_t qck_gw_rsp_dis               : 1;  /**< Disable faster response to GET_WORK */
	uint64_t qck_sw_dis                   : 1;  /**< Disable faster switch to UNSCHEDULED on GET_WORK */
	uint64_t rwq_alloc_dis                : 1;  /**< Disable FPA Alloc Requests when SSO_FPAGE_CNT < 16 */
	uint64_t soc_ccam_dis                 : 1;  /**< Disable power saving SOC conditional CAM
                                                         (**NOTE: Added in pass 2.0) */
	uint64_t sso_cclk_dis                 : 1;  /**< Disable power saving SSO conditional clocking
                                                         (**NOTE: Added in pass 2.0) */
	uint64_t rwo_flush                    : 1;  /**< Flush RWO engine
                                                         Allows outbound NCB entries to go immediately rather
                                                         than waiting for a complete fill packet. This register
                                                         is one-shot and clears itself each time it is set. */
	uint64_t wfe_thr                      : 1;  /**< Use 1 Work-fetch engine (instead of 4) */
	uint64_t rwio_byp_dis                 : 1;  /**< Disable Bypass path in RWI/RWO Engines */
	uint64_t rwq_byp_dis                  : 1;  /**< Disable Bypass path in RWQ Engine */
	uint64_t stt                          : 1;  /**< STT Setting for RW Stores */
	uint64_t ldt                          : 1;  /**< LDT Setting for RW Loads */
	uint64_t dwb                          : 1;  /**< DWB Setting for Return Page Requests
                                                         1 = 2 128B cache pages to issue DWB for
                                                         0 = 0 128B cache pages ro issue DWB for */
	uint64_t rwen                         : 1;  /**< Enable RWI/RWO operations
                                                         This bit should be set after SSO_RWQ_HEAD_PTRX and
                                                         SSO_RWQ_TAIL_PTRX have been programmed. */
#else
	uint64_t rwen                         : 1;
	uint64_t dwb                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stt                          : 1;
	uint64_t rwq_byp_dis                  : 1;
	uint64_t rwio_byp_dis                 : 1;
	uint64_t wfe_thr                      : 1;
	uint64_t rwo_flush                    : 1;
	uint64_t sso_cclk_dis                 : 1;
	uint64_t soc_ccam_dis                 : 1;
	uint64_t rwq_alloc_dis                : 1;
	uint64_t qck_sw_dis                   : 1;
	uint64_t qck_gw_rsp_dis               : 1;
	uint64_t qck_gw_rsp_adj               : 3;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_sso_cfg_s                 cn68xx;
	struct cvmx_sso_cfg_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t rwo_flush                    : 1;  /**< Flush RWO engine
                                                         Allows outbound NCB entries to go immediately rather
                                                         than waiting for a complete fill packet. This register
                                                         is one-shot and clears itself each time it is set. */
	uint64_t wfe_thr                      : 1;  /**< Use 1 Work-fetch engine (instead of 4) */
	uint64_t rwio_byp_dis                 : 1;  /**< Disable Bypass path in RWI/RWO Engines */
	uint64_t rwq_byp_dis                  : 1;  /**< Disable Bypass path in RWQ Engine */
	uint64_t stt                          : 1;  /**< STT Setting for RW Stores */
	uint64_t ldt                          : 1;  /**< LDT Setting for RW Loads */
	uint64_t dwb                          : 1;  /**< DWB Setting for Return Page Requests
                                                         1 = 2 128B cache pages to issue DWB for
                                                         0 = 0 128B cache pages ro issue DWB for */
	uint64_t rwen                         : 1;  /**< Enable RWI/RWO operations
                                                         This bit should be set after SSO_RWQ_HEAD_PTRX and
                                                         SSO_RWQ_TAIL_PTRX have been programmed. */
#else
	uint64_t rwen                         : 1;
	uint64_t dwb                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stt                          : 1;
	uint64_t rwq_byp_dis                  : 1;
	uint64_t rwio_byp_dis                 : 1;
	uint64_t wfe_thr                      : 1;
	uint64_t rwo_flush                    : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} cn68xxp1;
};
typedef union cvmx_sso_cfg cvmx_sso_cfg_t;

/**
 * cvmx_sso_ds_pc
 *
 * SSO_DS_PC = SSO De-Schedule Performance Counter
 *
 * Counts the number of de-schedule requests.
 * Counter rolls over through zero when max value exceeded.
 */
union cvmx_sso_ds_pc {
	uint64_t u64;
	struct cvmx_sso_ds_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ds_pc                        : 64; /**< De-schedule performance counter */
#else
	uint64_t ds_pc                        : 64;
#endif
	} s;
	struct cvmx_sso_ds_pc_s               cn68xx;
	struct cvmx_sso_ds_pc_s               cn68xxp1;
};
typedef union cvmx_sso_ds_pc cvmx_sso_ds_pc_t;

/**
 * cvmx_sso_err
 *
 * SSO_ERR = SSO Error Register
 *
 * Contains ECC and other misc error bits.
 *
 * <45> The free page error bit will assert when SSO_FPAGE_CNT <= 16 and
 *      SSO_CFG[RWEN] is 1.  Software will want to disable the interrupt
 *      associated with this error when recovering SSO pointers from the
 *      FPA and SSO.
 *
 * This register also contains the illegal operation error bits:
 *
 * <42> Received ADDWQ with tag specified as EMPTY
 * <41> Received illegal opcode
 * <40> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP/GET_WORK/ALLOC_WE
 *      from WS with CLR_NSCHED pending
 * <39> Received CLR_NSCHED
 *      from WS with SWTAG_DESCH/DESCH/CLR_NSCHED pending
 * <38> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP/GET_WORK/ALLOC_WE
 *      from WS with ALLOC_WE pending
 * <37> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP/GET_WORK/ALLOC_WE/CLR_NSCHED
 *      from WS with GET_WORK pending
 * <36> Received SWTAG_FULL/SWTAG_DESCH
 *      with tag specified as UNSCHEDULED
 * <35> Received SWTAG/SWTAG_FULL/SWTAG_DESCH
 *      with tag specified as EMPTY
 * <34> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/GET_WORK
 *      from WS with pending tag switch to ORDERED or ATOMIC
 * <33> Received SWTAG/SWTAG_DESCH/DESCH/UPD_WQP
 *      from WS in UNSCHEDULED state
 * <32> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP
 *      from WS in EMPTY state
 */
union cvmx_sso_err {
	uint64_t u64;
	struct cvmx_sso_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t bfp                          : 1;  /**< Bad Fill Packet error
                                                         Last byte of the fill packet did not match 8'h1a */
	uint64_t awe                          : 1;  /**< Out-of-memory error (ADDWQ Request is dropped) */
	uint64_t fpe                          : 1;  /**< Free page error */
	uint64_t reserved_43_44               : 2;
	uint64_t iop                          : 11; /**< Illegal operation errors */
	uint64_t reserved_12_31               : 20;
	uint64_t pnd_dbe0                     : 1;  /**< Double bit error for even PND RAM */
	uint64_t pnd_sbe0                     : 1;  /**< Single bit error for even PND RAM */
	uint64_t pnd_dbe1                     : 1;  /**< Double bit error for odd PND RAM */
	uint64_t pnd_sbe1                     : 1;  /**< Single bit error for odd PND RAM */
	uint64_t oth_dbe0                     : 1;  /**< Double bit error for even OTH RAM */
	uint64_t oth_sbe0                     : 1;  /**< Single bit error for even OTH RAM */
	uint64_t oth_dbe1                     : 1;  /**< Double bit error for odd OTH RAM */
	uint64_t oth_sbe1                     : 1;  /**< Single bit error for odd OTH RAM */
	uint64_t idx_dbe                      : 1;  /**< Double bit error for IDX RAM */
	uint64_t idx_sbe                      : 1;  /**< Single bit error for IDX RAM */
	uint64_t fidx_dbe                     : 1;  /**< Double bit error for FIDX RAM */
	uint64_t fidx_sbe                     : 1;  /**< Single bit error for FIDX RAM */
#else
	uint64_t fidx_sbe                     : 1;
	uint64_t fidx_dbe                     : 1;
	uint64_t idx_sbe                      : 1;
	uint64_t idx_dbe                      : 1;
	uint64_t oth_sbe1                     : 1;
	uint64_t oth_dbe1                     : 1;
	uint64_t oth_sbe0                     : 1;
	uint64_t oth_dbe0                     : 1;
	uint64_t pnd_sbe1                     : 1;
	uint64_t pnd_dbe1                     : 1;
	uint64_t pnd_sbe0                     : 1;
	uint64_t pnd_dbe0                     : 1;
	uint64_t reserved_12_31               : 20;
	uint64_t iop                          : 11;
	uint64_t reserved_43_44               : 2;
	uint64_t fpe                          : 1;
	uint64_t awe                          : 1;
	uint64_t bfp                          : 1;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_sso_err_s                 cn68xx;
	struct cvmx_sso_err_s                 cn68xxp1;
};
typedef union cvmx_sso_err cvmx_sso_err_t;

/**
 * cvmx_sso_err_enb
 *
 * SSO_ERR_ENB = SSO Error Enable Register
 *
 * Contains the interrupt enables corresponding to SSO_ERR.
 */
union cvmx_sso_err_enb {
	uint64_t u64;
	struct cvmx_sso_err_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t bfp_ie                       : 1;  /**< Bad Fill Packet error interrupt enable */
	uint64_t awe_ie                       : 1;  /**< Add-work error interrupt enable */
	uint64_t fpe_ie                       : 1;  /**< Free Page error interrupt enable */
	uint64_t reserved_43_44               : 2;
	uint64_t iop_ie                       : 11; /**< Illegal operation interrupt enables */
	uint64_t reserved_12_31               : 20;
	uint64_t pnd_dbe0_ie                  : 1;  /**< Double bit error interrupt enable for even PND RAM */
	uint64_t pnd_sbe0_ie                  : 1;  /**< Single bit error interrupt enable for even PND RAM */
	uint64_t pnd_dbe1_ie                  : 1;  /**< Double bit error interrupt enable for odd PND RAM */
	uint64_t pnd_sbe1_ie                  : 1;  /**< Single bit error interrupt enable for odd PND RAM */
	uint64_t oth_dbe0_ie                  : 1;  /**< Double bit error interrupt enable for even OTH RAM */
	uint64_t oth_sbe0_ie                  : 1;  /**< Single bit error interrupt enable for even OTH RAM */
	uint64_t oth_dbe1_ie                  : 1;  /**< Double bit error interrupt enable for odd OTH RAM */
	uint64_t oth_sbe1_ie                  : 1;  /**< Single bit error interrupt enable for odd OTH RAM */
	uint64_t idx_dbe_ie                   : 1;  /**< Double bit error interrupt enable for IDX RAM */
	uint64_t idx_sbe_ie                   : 1;  /**< Single bit error interrupt enable for IDX RAM */
	uint64_t fidx_dbe_ie                  : 1;  /**< Double bit error interrupt enable for FIDX RAM */
	uint64_t fidx_sbe_ie                  : 1;  /**< Single bit error interrupt enable for FIDX RAM */
#else
	uint64_t fidx_sbe_ie                  : 1;
	uint64_t fidx_dbe_ie                  : 1;
	uint64_t idx_sbe_ie                   : 1;
	uint64_t idx_dbe_ie                   : 1;
	uint64_t oth_sbe1_ie                  : 1;
	uint64_t oth_dbe1_ie                  : 1;
	uint64_t oth_sbe0_ie                  : 1;
	uint64_t oth_dbe0_ie                  : 1;
	uint64_t pnd_sbe1_ie                  : 1;
	uint64_t pnd_dbe1_ie                  : 1;
	uint64_t pnd_sbe0_ie                  : 1;
	uint64_t pnd_dbe0_ie                  : 1;
	uint64_t reserved_12_31               : 20;
	uint64_t iop_ie                       : 11;
	uint64_t reserved_43_44               : 2;
	uint64_t fpe_ie                       : 1;
	uint64_t awe_ie                       : 1;
	uint64_t bfp_ie                       : 1;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_sso_err_enb_s             cn68xx;
	struct cvmx_sso_err_enb_s             cn68xxp1;
};
typedef union cvmx_sso_err_enb cvmx_sso_err_enb_t;

/**
 * cvmx_sso_fidx_ecc_ctl
 *
 * SSO_FIDX_ECC_CTL = SSO FIDX ECC Control
 *
 */
union cvmx_sso_fidx_ecc_ctl {
	uint64_t u64;
	struct cvmx_sso_fidx_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t flip_synd                    : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error for the FIDX RAM. */
	uint64_t ecc_ena                      : 1;  /**< ECC Enable: When set will enable the 5 bit ECC
                                                         correct logic for the FIDX RAM. */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t flip_synd                    : 2;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_sso_fidx_ecc_ctl_s        cn68xx;
	struct cvmx_sso_fidx_ecc_ctl_s        cn68xxp1;
};
typedef union cvmx_sso_fidx_ecc_ctl cvmx_sso_fidx_ecc_ctl_t;

/**
 * cvmx_sso_fidx_ecc_st
 *
 * SSO_FIDX_ECC_ST = SSO FIDX ECC Status
 *
 */
union cvmx_sso_fidx_ecc_st {
	uint64_t u64;
	struct cvmx_sso_fidx_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t addr                         : 11; /**< Latch the address for latest sde/dbe occured
                                                         for the FIDX RAM */
	uint64_t reserved_9_15                : 7;
	uint64_t syndrom                      : 5;  /**< Report the latest error syndrom for the
                                                         FIDX RAM */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t syndrom                      : 5;
	uint64_t reserved_9_15                : 7;
	uint64_t addr                         : 11;
	uint64_t reserved_27_63               : 37;
#endif
	} s;
	struct cvmx_sso_fidx_ecc_st_s         cn68xx;
	struct cvmx_sso_fidx_ecc_st_s         cn68xxp1;
};
typedef union cvmx_sso_fidx_ecc_st cvmx_sso_fidx_ecc_st_t;

/**
 * cvmx_sso_fpage_cnt
 *
 * SSO_FPAGE_CNT = SSO Free Page Cnt
 *
 * This register keeps track of the number of free pages pointers available for use in external memory.
 */
union cvmx_sso_fpage_cnt {
	uint64_t u64;
	struct cvmx_sso_fpage_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t fpage_cnt                    : 32; /**< Free Page Cnt
                                                         HW updates this register. Writes to this register
                                                         are only for diagnostic purposes */
#else
	uint64_t fpage_cnt                    : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sso_fpage_cnt_s           cn68xx;
	struct cvmx_sso_fpage_cnt_s           cn68xxp1;
};
typedef union cvmx_sso_fpage_cnt cvmx_sso_fpage_cnt_t;

/**
 * cvmx_sso_gwe_cfg
 *
 * SSO_GWE_CFG = SSO Get-Work Examiner Configuration
 *
 * This register controls the operation of the Get-Work Examiner (GWE)
 */
union cvmx_sso_gwe_cfg {
	uint64_t u64;
	struct cvmx_sso_gwe_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t odu_ffpgw_dis                : 1;  /**< Disable flushing ODU on periodic restart of GWE */
	uint64_t gwe_rfpgw_dis                : 1;  /**< Disable periodic restart of GWE for pending get_work */
	uint64_t odu_prf_dis                  : 1;  /**< Disable ODU-initiated prefetches of WQEs into L2C
                                                         For diagnostic use only. */
	uint64_t odu_bmp_dis                  : 1;  /**< Disable ODU bumps.
                                                         If SSO_PP_STRICT is true, could
                                                         prevent forward progress under some circumstances.
                                                         For diagnostic use only. */
	uint64_t reserved_5_7                 : 3;
	uint64_t gwe_hvy_dis                  : 1;  /**< Disable GWE automatic, proportional weight-increase
                                                         mechanism and use SSO_QOSX_RND values as-is.
                                                         For diagnostic use only. */
	uint64_t gwe_poe                      : 1;  /**< Pause GWE on extracts
                                                         For diagnostic use only. */
	uint64_t gwe_fpor                     : 1;  /**< Flush GWE pipeline when restarting GWE.
                                                         For diagnostic use only. */
	uint64_t gwe_rah                      : 1;  /**< Begin at head of input queues when restarting GWE.
                                                         For diagnostic use only. */
	uint64_t gwe_dis                      : 1;  /**< Disable Get-Work Examiner */
#else
	uint64_t gwe_dis                      : 1;
	uint64_t gwe_rah                      : 1;
	uint64_t gwe_fpor                     : 1;
	uint64_t gwe_poe                      : 1;
	uint64_t gwe_hvy_dis                  : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t odu_bmp_dis                  : 1;
	uint64_t odu_prf_dis                  : 1;
	uint64_t gwe_rfpgw_dis                : 1;
	uint64_t odu_ffpgw_dis                : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_sso_gwe_cfg_s             cn68xx;
	struct cvmx_sso_gwe_cfg_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t gwe_poe                      : 1;  /**< Pause GWE on extracts
                                                         For diagnostic use only. */
	uint64_t gwe_fpor                     : 1;  /**< Flush GWE pipeline when restarting GWE.
                                                         For diagnostic use only. */
	uint64_t gwe_rah                      : 1;  /**< Begin at head of input queues when restarting GWE.
                                                         For diagnostic use only. */
	uint64_t gwe_dis                      : 1;  /**< Disable Get-Work Examiner */
#else
	uint64_t gwe_dis                      : 1;
	uint64_t gwe_rah                      : 1;
	uint64_t gwe_fpor                     : 1;
	uint64_t gwe_poe                      : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} cn68xxp1;
};
typedef union cvmx_sso_gwe_cfg cvmx_sso_gwe_cfg_t;

/**
 * cvmx_sso_idx_ecc_ctl
 *
 * SSO_IDX_ECC_CTL = SSO IDX ECC Control
 *
 */
union cvmx_sso_idx_ecc_ctl {
	uint64_t u64;
	struct cvmx_sso_idx_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t flip_synd                    : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error for the IDX RAM. */
	uint64_t ecc_ena                      : 1;  /**< ECC Enable: When set will enable the 5 bit ECC
                                                         correct logic for the IDX RAM. */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t flip_synd                    : 2;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_sso_idx_ecc_ctl_s         cn68xx;
	struct cvmx_sso_idx_ecc_ctl_s         cn68xxp1;
};
typedef union cvmx_sso_idx_ecc_ctl cvmx_sso_idx_ecc_ctl_t;

/**
 * cvmx_sso_idx_ecc_st
 *
 * SSO_IDX_ECC_ST = SSO IDX ECC Status
 *
 */
union cvmx_sso_idx_ecc_st {
	uint64_t u64;
	struct cvmx_sso_idx_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t addr                         : 11; /**< Latch the address for latest sde/dbe occured
                                                         for the IDX RAM */
	uint64_t reserved_9_15                : 7;
	uint64_t syndrom                      : 5;  /**< Report the latest error syndrom for the
                                                         IDX RAM */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t syndrom                      : 5;
	uint64_t reserved_9_15                : 7;
	uint64_t addr                         : 11;
	uint64_t reserved_27_63               : 37;
#endif
	} s;
	struct cvmx_sso_idx_ecc_st_s          cn68xx;
	struct cvmx_sso_idx_ecc_st_s          cn68xxp1;
};
typedef union cvmx_sso_idx_ecc_st cvmx_sso_idx_ecc_st_t;

/**
 * cvmx_sso_iq_cnt#
 *
 * CSR reserved addresses: (64): 0x8200..0x83f8
 * CSR align addresses: ===========================================================================================================
 * SSO_IQ_CNTX = SSO Input Queue Count Register
 *               (one per QOS level)
 *
 * Contains a read-only count of the number of work queue entries for each QOS
 * level. Counts both in-unit and in-memory entries.
 */
union cvmx_sso_iq_cntx {
	uint64_t u64;
	struct cvmx_sso_iq_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_cnt                       : 32; /**< Input queue count for QOS level X */
#else
	uint64_t iq_cnt                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sso_iq_cntx_s             cn68xx;
	struct cvmx_sso_iq_cntx_s             cn68xxp1;
};
typedef union cvmx_sso_iq_cntx cvmx_sso_iq_cntx_t;

/**
 * cvmx_sso_iq_com_cnt
 *
 * SSO_IQ_COM_CNT = SSO Input Queue Combined Count Register
 *
 * Contains a read-only count of the total number of work queue entries in all
 * QOS levels.  Counts both in-unit and in-memory entries.
 */
union cvmx_sso_iq_com_cnt {
	uint64_t u64;
	struct cvmx_sso_iq_com_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_cnt                       : 32; /**< Input queue combined count */
#else
	uint64_t iq_cnt                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sso_iq_com_cnt_s          cn68xx;
	struct cvmx_sso_iq_com_cnt_s          cn68xxp1;
};
typedef union cvmx_sso_iq_com_cnt cvmx_sso_iq_com_cnt_t;

/**
 * cvmx_sso_iq_int
 *
 * SSO_IQ_INT = SSO Input Queue Interrupt Register
 *
 * Contains the bits (one per QOS level) that can trigger the input queue
 * interrupt.  An IQ_INT bit will be set if SSO_IQ_CNT#QOS# changes and the
 * resulting value is equal to SSO_IQ_THR#QOS#.
 */
union cvmx_sso_iq_int {
	uint64_t u64;
	struct cvmx_sso_iq_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t iq_int                       : 8;  /**< Input queue interrupt bits */
#else
	uint64_t iq_int                       : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_sso_iq_int_s              cn68xx;
	struct cvmx_sso_iq_int_s              cn68xxp1;
};
typedef union cvmx_sso_iq_int cvmx_sso_iq_int_t;

/**
 * cvmx_sso_iq_int_en
 *
 * SSO_IQ_INT_EN = SSO Input Queue Interrupt Enable Register
 *
 * Contains the bits (one per QOS level) that enable the input queue interrupt.
 */
union cvmx_sso_iq_int_en {
	uint64_t u64;
	struct cvmx_sso_iq_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t int_en                       : 8;  /**< Input queue interrupt enable bits */
#else
	uint64_t int_en                       : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_sso_iq_int_en_s           cn68xx;
	struct cvmx_sso_iq_int_en_s           cn68xxp1;
};
typedef union cvmx_sso_iq_int_en cvmx_sso_iq_int_en_t;

/**
 * cvmx_sso_iq_thr#
 *
 * CSR reserved addresses: (24): 0x9040..0x90f8
 * CSR align addresses: ===========================================================================================================
 * SSO_IQ_THRX = SSO Input Queue Threshold Register
 *               (one per QOS level)
 *
 * Threshold value for triggering input queue interrupts.
 */
union cvmx_sso_iq_thrx {
	uint64_t u64;
	struct cvmx_sso_iq_thrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_thr                       : 32; /**< Input queue threshold for QOS level X */
#else
	uint64_t iq_thr                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sso_iq_thrx_s             cn68xx;
	struct cvmx_sso_iq_thrx_s             cn68xxp1;
};
typedef union cvmx_sso_iq_thrx cvmx_sso_iq_thrx_t;

/**
 * cvmx_sso_nos_cnt
 *
 * SSO_NOS_CNT = SSO No-schedule Count Register
 *
 * Contains the number of work queue entries on the no-schedule list.
 */
union cvmx_sso_nos_cnt {
	uint64_t u64;
	struct cvmx_sso_nos_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t nos_cnt                      : 12; /**< Number of work queue entries on the no-schedule list */
#else
	uint64_t nos_cnt                      : 12;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_sso_nos_cnt_s             cn68xx;
	struct cvmx_sso_nos_cnt_s             cn68xxp1;
};
typedef union cvmx_sso_nos_cnt cvmx_sso_nos_cnt_t;

/**
 * cvmx_sso_nw_tim
 *
 * SSO_NW_TIM = SSO New Work Timer Period Register
 *
 * Sets the minimum period for a new work request timeout.  Period is specified
 * in n-1 notation where the increment value is 1024 clock cycles.  Thus, a
 * value of 0x0 in this register translates to 1024 cycles, 0x1 translates to
 * 2048 cycles, 0x2 translates to 3072 cycles, etc...  Note: the maximum period
 * for a new work request timeout is 2 times the minimum period.  Note: the new
 * work request timeout counter is reset when this register is written.
 *
 * There are two new work request timeout cases:
 *
 * - WAIT bit clear.  The new work request can timeout if the timer expires
 *   before the pre-fetch engine has reached the end of all work queues.  This
 *   can occur if the executable work queue entry is deep in the queue and the
 *   pre-fetch engine is subject to many resets (i.e. high switch, de-schedule,
 *   or new work load from other PP's).  Thus, it is possible for a PP to
 *   receive a work response with the NO_WORK bit set even though there was at
 *   least one executable entry in the work queues.  The other (and typical)
 *   scenario for receiving a NO_WORK response with the WAIT bit clear is that
 *   the pre-fetch engine has reached the end of all work queues without
 *   finding executable work.
 *
 * - WAIT bit set.  The new work request can timeout if the timer expires
 *   before the pre-fetch engine has found executable work.  In this case, the
 *   only scenario where the PP will receive a work response with the NO_WORK
 *   bit set is if the timer expires.  Note: it is still possible for a PP to
 *   receive a NO_WORK response even though there was at least one executable
 *   entry in the work queues.
 *
 * In either case, it's important to note that switches and de-schedules are
 * higher priority operations that can cause the pre-fetch engine to reset.
 * Thus in a system with many switches or de-schedules occurring, it's possible
 * for the new work timer to expire (resulting in NO_WORK responses) before the
 * pre-fetch engine is able to get very deep into the work queues.
 */
union cvmx_sso_nw_tim {
	uint64_t u64;
	struct cvmx_sso_nw_tim_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t nw_tim                       : 10; /**< New work timer period */
#else
	uint64_t nw_tim                       : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_sso_nw_tim_s              cn68xx;
	struct cvmx_sso_nw_tim_s              cn68xxp1;
};
typedef union cvmx_sso_nw_tim cvmx_sso_nw_tim_t;

/**
 * cvmx_sso_oth_ecc_ctl
 *
 * SSO_OTH_ECC_CTL = SSO OTH ECC Control
 *
 */
union cvmx_sso_oth_ecc_ctl {
	uint64_t u64;
	struct cvmx_sso_oth_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t flip_synd1                   : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error for the odd OTH RAM. */
	uint64_t ecc_ena1                     : 1;  /**< ECC Enable: When set will enable the 7 bit ECC
                                                         correct logic for the odd OTH RAM. */
	uint64_t flip_synd0                   : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error for the even OTH RAM. */
	uint64_t ecc_ena0                     : 1;  /**< ECC Enable: When set will enable the 7 bit ECC
                                                         correct logic for the even OTH RAM. */
#else
	uint64_t ecc_ena0                     : 1;
	uint64_t flip_synd0                   : 2;
	uint64_t ecc_ena1                     : 1;
	uint64_t flip_synd1                   : 2;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_sso_oth_ecc_ctl_s         cn68xx;
	struct cvmx_sso_oth_ecc_ctl_s         cn68xxp1;
};
typedef union cvmx_sso_oth_ecc_ctl cvmx_sso_oth_ecc_ctl_t;

/**
 * cvmx_sso_oth_ecc_st
 *
 * SSO_OTH_ECC_ST = SSO OTH ECC Status
 *
 */
union cvmx_sso_oth_ecc_st {
	uint64_t u64;
	struct cvmx_sso_oth_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t addr1                        : 11; /**< Latch the address for latest sde/dbe occured
                                                         for the odd OTH RAM */
	uint64_t reserved_43_47               : 5;
	uint64_t syndrom1                     : 7;  /**< Report the latest error syndrom for the odd
                                                         OTH RAM */
	uint64_t reserved_27_35               : 9;
	uint64_t addr0                        : 11; /**< Latch the address for latest sde/dbe occured
                                                         for the even OTH RAM */
	uint64_t reserved_11_15               : 5;
	uint64_t syndrom0                     : 7;  /**< Report the latest error syndrom for the even
                                                         OTH RAM */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t syndrom0                     : 7;
	uint64_t reserved_11_15               : 5;
	uint64_t addr0                        : 11;
	uint64_t reserved_27_35               : 9;
	uint64_t syndrom1                     : 7;
	uint64_t reserved_43_47               : 5;
	uint64_t addr1                        : 11;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_sso_oth_ecc_st_s          cn68xx;
	struct cvmx_sso_oth_ecc_st_s          cn68xxp1;
};
typedef union cvmx_sso_oth_ecc_st cvmx_sso_oth_ecc_st_t;

/**
 * cvmx_sso_pnd_ecc_ctl
 *
 * SSO_PND_ECC_CTL = SSO PND ECC Control
 *
 */
union cvmx_sso_pnd_ecc_ctl {
	uint64_t u64;
	struct cvmx_sso_pnd_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t flip_synd1                   : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error for the odd PND RAM. */
	uint64_t ecc_ena1                     : 1;  /**< ECC Enable: When set will enable the 7 bit ECC
                                                         correct logic for the odd PND RAM. */
	uint64_t flip_synd0                   : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error for the even PND RAM. */
	uint64_t ecc_ena0                     : 1;  /**< ECC Enable: When set will enable the 7 bit ECC
                                                         correct logic for the even PND RAM. */
#else
	uint64_t ecc_ena0                     : 1;
	uint64_t flip_synd0                   : 2;
	uint64_t ecc_ena1                     : 1;
	uint64_t flip_synd1                   : 2;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_sso_pnd_ecc_ctl_s         cn68xx;
	struct cvmx_sso_pnd_ecc_ctl_s         cn68xxp1;
};
typedef union cvmx_sso_pnd_ecc_ctl cvmx_sso_pnd_ecc_ctl_t;

/**
 * cvmx_sso_pnd_ecc_st
 *
 * SSO_PND_ECC_ST = SSO PND ECC Status
 *
 */
union cvmx_sso_pnd_ecc_st {
	uint64_t u64;
	struct cvmx_sso_pnd_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t addr1                        : 11; /**< Latch the address for latest sde/dbe occured
                                                         for the odd PND RAM */
	uint64_t reserved_43_47               : 5;
	uint64_t syndrom1                     : 7;  /**< Report the latest error syndrom for the odd
                                                         PND RAM */
	uint64_t reserved_27_35               : 9;
	uint64_t addr0                        : 11; /**< Latch the address for latest sde/dbe occured
                                                         for the even PND RAM */
	uint64_t reserved_11_15               : 5;
	uint64_t syndrom0                     : 7;  /**< Report the latest error syndrom for the even
                                                         PND RAM */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t syndrom0                     : 7;
	uint64_t reserved_11_15               : 5;
	uint64_t addr0                        : 11;
	uint64_t reserved_27_35               : 9;
	uint64_t syndrom1                     : 7;
	uint64_t reserved_43_47               : 5;
	uint64_t addr1                        : 11;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_sso_pnd_ecc_st_s          cn68xx;
	struct cvmx_sso_pnd_ecc_st_s          cn68xxp1;
};
typedef union cvmx_sso_pnd_ecc_st cvmx_sso_pnd_ecc_st_t;

/**
 * cvmx_sso_pp#_grp_msk
 *
 * CSR reserved addresses: (24): 0x5040..0x50f8
 * CSR align addresses: ===========================================================================================================
 * SSO_PPX_GRP_MSK = SSO PP Group Mask Register
 *                   (one bit per group per PP)
 *
 * Selects which group(s) a PP belongs to.  A '1' in any bit position sets the
 * PP's membership in the corresponding group.  A value of 0x0 will prevent the
 * PP from receiving new work.
 *
 * Note that these do not contain QOS level priorities for each PP.  This is a
 * change from previous POW designs.
 */
union cvmx_sso_ppx_grp_msk {
	uint64_t u64;
	struct cvmx_sso_ppx_grp_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t grp_msk                      : 64; /**< PPX group mask */
#else
	uint64_t grp_msk                      : 64;
#endif
	} s;
	struct cvmx_sso_ppx_grp_msk_s         cn68xx;
	struct cvmx_sso_ppx_grp_msk_s         cn68xxp1;
};
typedef union cvmx_sso_ppx_grp_msk cvmx_sso_ppx_grp_msk_t;

/**
 * cvmx_sso_pp#_qos_pri
 *
 * CSR reserved addresses: (56): 0x2040..0x21f8
 * CSR align addresses: ===========================================================================================================
 * SSO_PP(0..31)_QOS_PRI = SSO PP QOS Priority Register
 *                                (one field per IQ per PP)
 *
 * Contains the QOS level priorities for each PP.
 *      0x0       is the highest priority
 *      0x7       is the lowest priority
 *      0xf       prevents the PP from receiving work from that QOS level
 *      0x8-0xe   Reserved
 *
 * For a given PP, priorities should begin at 0x0, and remain contiguous
 * throughout the range.  Failure to do so may result in severe
 * performance degradation.
 *
 *
 * Priorities for IQs 0..7
 */
union cvmx_sso_ppx_qos_pri {
	uint64_t u64;
	struct cvmx_sso_ppx_qos_pri_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t qos7_pri                     : 4;  /**< QOS7 priority for PPX */
	uint64_t reserved_52_55               : 4;
	uint64_t qos6_pri                     : 4;  /**< QOS6 priority for PPX */
	uint64_t reserved_44_47               : 4;
	uint64_t qos5_pri                     : 4;  /**< QOS5 priority for PPX */
	uint64_t reserved_36_39               : 4;
	uint64_t qos4_pri                     : 4;  /**< QOS4 priority for PPX */
	uint64_t reserved_28_31               : 4;
	uint64_t qos3_pri                     : 4;  /**< QOS3 priority for PPX */
	uint64_t reserved_20_23               : 4;
	uint64_t qos2_pri                     : 4;  /**< QOS2 priority for PPX */
	uint64_t reserved_12_15               : 4;
	uint64_t qos1_pri                     : 4;  /**< QOS1 priority for PPX */
	uint64_t reserved_4_7                 : 4;
	uint64_t qos0_pri                     : 4;  /**< QOS0 priority for PPX */
#else
	uint64_t qos0_pri                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t qos1_pri                     : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t qos2_pri                     : 4;
	uint64_t reserved_20_23               : 4;
	uint64_t qos3_pri                     : 4;
	uint64_t reserved_28_31               : 4;
	uint64_t qos4_pri                     : 4;
	uint64_t reserved_36_39               : 4;
	uint64_t qos5_pri                     : 4;
	uint64_t reserved_44_47               : 4;
	uint64_t qos6_pri                     : 4;
	uint64_t reserved_52_55               : 4;
	uint64_t qos7_pri                     : 4;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_sso_ppx_qos_pri_s         cn68xx;
	struct cvmx_sso_ppx_qos_pri_s         cn68xxp1;
};
typedef union cvmx_sso_ppx_qos_pri cvmx_sso_ppx_qos_pri_t;

/**
 * cvmx_sso_pp_strict
 *
 * SSO_PP_STRICT = SSO Strict Priority
 *
 * This register controls getting work from the input queues.  If the bit
 * corresponding to a PP is set, that PP will not take work off the input
 * queues until it is known that there is no higher-priority work available.
 *
 * Setting SSO_PP_STRICT may incur a performance penalty if highest-priority
 * work is not found early.
 *
 * It is possible to starve a PP of work with SSO_PP_STRICT.  If the
 * SSO_PPX_GRP_MSK for a PP masks-out much of the work added to the input
 * queues that are higher-priority for that PP, and if there is a constant
 * stream of work through one or more of those higher-priority input queues,
 * then that PP may not accept work from lower-priority input queues.  This can
 * be alleviated by ensuring that most or all the work added to the
 * higher-priority input queues for a PP with SSO_PP_STRICT set are in a group
 * acceptable to that PP.
 *
 * It is also possible to neglect work in an input queue if SSO_PP_STRICT is
 * used.  If an input queue is a lower-priority queue for all PPs, and if all
 * the PPs have their corresponding bit in SSO_PP_STRICT set, then work may
 * never be taken (or be seldom taken) from that queue.  This can be alleviated
 * by ensuring that work in all input queues can be serviced by one or more PPs
 * that do not have SSO_PP_STRICT set, or that the input queue is the
 * highest-priority input queue for one or more PPs that do have SSO_PP_STRICT
 * set.
 */
union cvmx_sso_pp_strict {
	uint64_t u64;
	struct cvmx_sso_pp_strict_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pp_strict                    : 32; /**< Corresponding PP operates in strict mode. */
#else
	uint64_t pp_strict                    : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sso_pp_strict_s           cn68xx;
	struct cvmx_sso_pp_strict_s           cn68xxp1;
};
typedef union cvmx_sso_pp_strict cvmx_sso_pp_strict_t;

/**
 * cvmx_sso_qos#_rnd
 *
 * CSR align addresses: ===========================================================================================================
 * SSO_QOS(0..7)_RND = SSO QOS Issue Round Register
 *                (one per IQ)
 *
 * The number of arbitration rounds each QOS level participates in.
 */
union cvmx_sso_qosx_rnd {
	uint64_t u64;
	struct cvmx_sso_qosx_rnd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t rnds_qos                     : 8;  /**< Number of rounds to participate in for IQ(X). */
#else
	uint64_t rnds_qos                     : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_sso_qosx_rnd_s            cn68xx;
	struct cvmx_sso_qosx_rnd_s            cn68xxp1;
};
typedef union cvmx_sso_qosx_rnd cvmx_sso_qosx_rnd_t;

/**
 * cvmx_sso_qos_thr#
 *
 * CSR reserved addresses: (24): 0xa040..0xa0f8
 * CSR align addresses: ===========================================================================================================
 * SSO_QOS_THRX = SSO QOS Threshold Register
 *                (one per QOS level)
 *
 * Contains the thresholds for allocating SSO internal storage buffers.  If the
 * number of remaining free buffers drops below the minimum threshold (MIN_THR)
 * or the number of allocated buffers for this QOS level rises above the
 * maximum threshold (MAX_THR), future incoming work queue entries will be
 * buffered externally rather than internally.  This register also contains the
 * number of internal buffers currently allocated to this QOS level (BUF_CNT).
 */
union cvmx_sso_qos_thrx {
	uint64_t u64;
	struct cvmx_sso_qos_thrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t buf_cnt                      : 12; /**< # of internal buffers allocated to QOS level X */
	uint64_t reserved_26_27               : 2;
	uint64_t max_thr                      : 12; /**< Max threshold for QOS level X
                                                         For performance reasons, MAX_THR can have a slop of 4
                                                         WQE for QOS level X. */
	uint64_t reserved_12_13               : 2;
	uint64_t min_thr                      : 12; /**< Min threshold for QOS level X
                                                         For performance reasons, MIN_THR can have a slop of 4
                                                         WQEs for QOS level X. */
#else
	uint64_t min_thr                      : 12;
	uint64_t reserved_12_13               : 2;
	uint64_t max_thr                      : 12;
	uint64_t reserved_26_27               : 2;
	uint64_t buf_cnt                      : 12;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_sso_qos_thrx_s            cn68xx;
	struct cvmx_sso_qos_thrx_s            cn68xxp1;
};
typedef union cvmx_sso_qos_thrx cvmx_sso_qos_thrx_t;

/**
 * cvmx_sso_qos_we
 *
 * SSO_QOS_WE = SSO WE Buffers
 *
 * This register contains a read-only count of the current number of free
 * buffers (FREE_CNT) and the total number of tag chain heads on the de-schedule list
 * (DES_CNT) (which is not the same as the total number of entries on all of the descheduled
 * tag chains.)
 */
union cvmx_sso_qos_we {
	uint64_t u64;
	struct cvmx_sso_qos_we_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_26_63               : 38;
	uint64_t des_cnt                      : 12; /**< Number of buffers on de-schedule list */
	uint64_t reserved_12_13               : 2;
	uint64_t free_cnt                     : 12; /**< Number of total free buffers */
#else
	uint64_t free_cnt                     : 12;
	uint64_t reserved_12_13               : 2;
	uint64_t des_cnt                      : 12;
	uint64_t reserved_26_63               : 38;
#endif
	} s;
	struct cvmx_sso_qos_we_s              cn68xx;
	struct cvmx_sso_qos_we_s              cn68xxp1;
};
typedef union cvmx_sso_qos_we cvmx_sso_qos_we_t;

/**
 * cvmx_sso_reset
 *
 * SSO_RESET = SSO Soft Reset
 *
 * Writing a one to SSO_RESET[RESET] will reset the SSO.  After receiving a
 * store to this CSR, the SSO must not be sent any other operations for 2500
 * sclk cycles.
 *
 * Note that the contents of this register are reset along with the rest of the
 * SSO.
 *
 * IMPLEMENTATION NOTES--NOT FOR SPEC:
 *      The SSO must return the bus credit associated with the CSR store used
 *      to write this register before reseting itself.  And the RSL tree
 *      that passes through the SSO must continue to work for RSL operations
 *      that do not target the SSO itself.
 */
union cvmx_sso_reset {
	uint64_t u64;
	struct cvmx_sso_reset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t reset                        : 1;  /**< Reset the SSO */
#else
	uint64_t reset                        : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_sso_reset_s               cn68xx;
};
typedef union cvmx_sso_reset cvmx_sso_reset_t;

/**
 * cvmx_sso_rwq_head_ptr#
 *
 * CSR reserved addresses: (24): 0xb040..0xb0f8
 * CSR align addresses: ===========================================================================================================
 * SSO_RWQ_HEAD_PTRX = SSO Remote Queue Head Register
 *                (one per QOS level)
 * Contains the ptr to the first entry of the remote linked list(s) for a particular
 * QoS level. SW should initialize the remote linked list(s) by programming
 * SSO_RWQ_HEAD_PTRX and SSO_RWQ_TAIL_PTRX to identical values.
 */
union cvmx_sso_rwq_head_ptrx {
	uint64_t u64;
	struct cvmx_sso_rwq_head_ptrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t ptr                          : 31; /**< Head Pointer */
	uint64_t reserved_5_6                 : 2;
	uint64_t rctr                         : 5;  /**< Index of next WQE entry in fill packet to be
                                                         processed (inbound queues) */
#else
	uint64_t rctr                         : 5;
	uint64_t reserved_5_6                 : 2;
	uint64_t ptr                          : 31;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_sso_rwq_head_ptrx_s       cn68xx;
	struct cvmx_sso_rwq_head_ptrx_s       cn68xxp1;
};
typedef union cvmx_sso_rwq_head_ptrx cvmx_sso_rwq_head_ptrx_t;

/**
 * cvmx_sso_rwq_pop_fptr
 *
 * SSO_RWQ_POP_FPTR = SSO Pop Free Pointer
 *
 * This register is used by SW to remove pointers for buffer-reallocation and diagnostics, and
 * should only be used when SSO is idle.
 *
 * To remove ALL pointers, software must insure that there are modulus 16
 * pointers in the FPA.  To do this, SSO_CFG.RWQ_BYP_DIS must be set, the FPA
 * pointer count read, and enough fake buffers pushed via SSO_RWQ_PSH_FPTR to
 * bring the FPA pointer count up to mod 16.
 */
union cvmx_sso_rwq_pop_fptr {
	uint64_t u64;
	struct cvmx_sso_rwq_pop_fptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t val                          : 1;  /**< Free Pointer Valid */
	uint64_t cnt                          : 6;  /**< fptr_in count */
	uint64_t reserved_38_56               : 19;
	uint64_t fptr                         : 31; /**< Free Pointer */
	uint64_t reserved_0_6                 : 7;
#else
	uint64_t reserved_0_6                 : 7;
	uint64_t fptr                         : 31;
	uint64_t reserved_38_56               : 19;
	uint64_t cnt                          : 6;
	uint64_t val                          : 1;
#endif
	} s;
	struct cvmx_sso_rwq_pop_fptr_s        cn68xx;
	struct cvmx_sso_rwq_pop_fptr_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t val                          : 1;  /**< Free Pointer Valid */
	uint64_t reserved_38_62               : 25;
	uint64_t fptr                         : 31; /**< Free Pointer */
	uint64_t reserved_0_6                 : 7;
#else
	uint64_t reserved_0_6                 : 7;
	uint64_t fptr                         : 31;
	uint64_t reserved_38_62               : 25;
	uint64_t val                          : 1;
#endif
	} cn68xxp1;
};
typedef union cvmx_sso_rwq_pop_fptr cvmx_sso_rwq_pop_fptr_t;

/**
 * cvmx_sso_rwq_psh_fptr
 *
 * CSR reserved addresses: (56): 0xc240..0xc3f8
 * SSO_RWQ_PSH_FPTR = SSO Free Pointer FIFO
 *
 * This register is used by SW to initialize the SSO with a pool of free
 * pointers by writing the FPTR field whenever FULL = 0. Free pointers are
 * fetched/released from/to the pool when accessing WQE entries stored remotely
 * (in remote linked lists).  Free pointers should be 128 byte aligned, each of
 * 256 bytes. This register should only be used when SSO is idle.
 *
 * Software needs to set aside buffering for
 *      8 + 48 + ROUNDUP(N/26)
 *
 * where as many as N DRAM work queue entries may be used.  The first 8 buffers
 * are used to setup the SSO_RWQ_HEAD_PTR and SSO_RWQ_TAIL_PTRs, and the
 * remainder are pushed via this register.
 *
 * IMPLEMENTATION NOTES--NOT FOR SPEC:
 *      48 avoids false out of buffer error due to (16) FPA and in-sso FPA buffering (32)
 *      26 is number of WAE's per 256B buffer
 */
union cvmx_sso_rwq_psh_fptr {
	uint64_t u64;
	struct cvmx_sso_rwq_psh_fptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t full                         : 1;  /**< FIFO Full.  When set, the FPA is busy writing entries
                                                         and software must wait before adding new entries. */
	uint64_t cnt                          : 4;  /**< fptr_out count */
	uint64_t reserved_38_58               : 21;
	uint64_t fptr                         : 31; /**< Free Pointer */
	uint64_t reserved_0_6                 : 7;
#else
	uint64_t reserved_0_6                 : 7;
	uint64_t fptr                         : 31;
	uint64_t reserved_38_58               : 21;
	uint64_t cnt                          : 4;
	uint64_t full                         : 1;
#endif
	} s;
	struct cvmx_sso_rwq_psh_fptr_s        cn68xx;
	struct cvmx_sso_rwq_psh_fptr_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t full                         : 1;  /**< FIFO Full.  When set, the FPA is busy writing entries
                                                         and software must wait before adding new entries. */
	uint64_t reserved_38_62               : 25;
	uint64_t fptr                         : 31; /**< Free Pointer */
	uint64_t reserved_0_6                 : 7;
#else
	uint64_t reserved_0_6                 : 7;
	uint64_t fptr                         : 31;
	uint64_t reserved_38_62               : 25;
	uint64_t full                         : 1;
#endif
	} cn68xxp1;
};
typedef union cvmx_sso_rwq_psh_fptr cvmx_sso_rwq_psh_fptr_t;

/**
 * cvmx_sso_rwq_tail_ptr#
 *
 * CSR reserved addresses: (56): 0xc040..0xc1f8
 * SSO_RWQ_TAIL_PTRX = SSO Remote Queue Tail Register
 *                (one per QOS level)
 * Contains the ptr to the last entry of the remote linked list(s) for a particular
 * QoS level. SW must initialize the remote linked list(s) by programming
 * SSO_RWQ_HEAD_PTRX and SSO_RWQ_TAIL_PTRX to identical values.
 */
union cvmx_sso_rwq_tail_ptrx {
	uint64_t u64;
	struct cvmx_sso_rwq_tail_ptrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t ptr                          : 31; /**< Tail Pointer */
	uint64_t reserved_5_6                 : 2;
	uint64_t rctr                         : 5;  /**< Number of entries waiting to be sent out to external
                                                         RAM (outbound queues) */
#else
	uint64_t rctr                         : 5;
	uint64_t reserved_5_6                 : 2;
	uint64_t ptr                          : 31;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_sso_rwq_tail_ptrx_s       cn68xx;
	struct cvmx_sso_rwq_tail_ptrx_s       cn68xxp1;
};
typedef union cvmx_sso_rwq_tail_ptrx cvmx_sso_rwq_tail_ptrx_t;

/**
 * cvmx_sso_ts_pc
 *
 * SSO_TS_PC = SSO Tag Switch Performance Counter
 *
 * Counts the number of tag switch requests.
 * Counter rolls over through zero when max value exceeded.
 */
union cvmx_sso_ts_pc {
	uint64_t u64;
	struct cvmx_sso_ts_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ts_pc                        : 64; /**< Tag switch performance counter */
#else
	uint64_t ts_pc                        : 64;
#endif
	} s;
	struct cvmx_sso_ts_pc_s               cn68xx;
	struct cvmx_sso_ts_pc_s               cn68xxp1;
};
typedef union cvmx_sso_ts_pc cvmx_sso_ts_pc_t;

/**
 * cvmx_sso_wa_com_pc
 *
 * SSO_WA_COM_PC = SSO Work Add Combined Performance Counter
 *
 * Counts the number of add new work requests for all QOS levels.
 * Counter rolls over through zero when max value exceeded.
 */
union cvmx_sso_wa_com_pc {
	uint64_t u64;
	struct cvmx_sso_wa_com_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wa_pc                        : 64; /**< Work add combined performance counter */
#else
	uint64_t wa_pc                        : 64;
#endif
	} s;
	struct cvmx_sso_wa_com_pc_s           cn68xx;
	struct cvmx_sso_wa_com_pc_s           cn68xxp1;
};
typedef union cvmx_sso_wa_com_pc cvmx_sso_wa_com_pc_t;

/**
 * cvmx_sso_wa_pc#
 *
 * CSR reserved addresses: (64): 0x4200..0x43f8
 * CSR align addresses: ===========================================================================================================
 * SSO_WA_PCX = SSO Work Add Performance Counter
 *             (one per QOS level)
 *
 * Counts the number of add new work requests for each QOS level.
 * Counter rolls over through zero when max value exceeded.
 */
union cvmx_sso_wa_pcx {
	uint64_t u64;
	struct cvmx_sso_wa_pcx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wa_pc                        : 64; /**< Work add performance counter for QOS level X */
#else
	uint64_t wa_pc                        : 64;
#endif
	} s;
	struct cvmx_sso_wa_pcx_s              cn68xx;
	struct cvmx_sso_wa_pcx_s              cn68xxp1;
};
typedef union cvmx_sso_wa_pcx cvmx_sso_wa_pcx_t;

/**
 * cvmx_sso_wq_int
 *
 * Note, the old POW offsets ran from 0x0 to 0x3f8, leaving the next available slot at 0x400.
 * To ensure no overlap, start on 4k boundary: 0x1000.
 * SSO_WQ_INT = SSO Work Queue Interrupt Register
 *
 * Contains the bits (one per group) that set work queue interrupts and are
 * used to clear these interrupts.  For more information regarding this
 * register, see the interrupt section of the SSO spec.
 */
union cvmx_sso_wq_int {
	uint64_t u64;
	struct cvmx_sso_wq_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wq_int                       : 64; /**< Work queue interrupt bits
                                                         Corresponding WQ_INT bit is set by HW whenever:
                                                           - SSO_WQ_INT_CNTX[IQ_CNT] >=
                                                             SSO_WQ_INT_THRX[IQ_THR] and the threshold
                                                             interrupt is not disabled.
                                                             SSO_WQ_IQ_DISX[IQ_DIS<X>]==1 disables the interrupt
                                                             SSO_WQ_INT_THRX[IQ_THR]==0 disables the int.
                                                           - SSO_WQ_INT_CNTX[DS_CNT] >=
                                                             SSO_WQ_INT_THRX[DS_THR] and the threshold
                                                             interrupt is not disabled
                                                             SSO_WQ_INT_THRX[DS_THR]==0 disables the int.
                                                           - SSO_WQ_INT_CNTX[TC_CNT]==1 when periodic
                                                             counter SSO_WQ_INT_PC[PC]==0 and
                                                             SSO_WQ_INT_THRX[TC_EN]==1 and at least one of:
                                                               - SSO_WQ_INT_CNTX[IQ_CNT] > 0
                                                               - SSO_WQ_INT_CNTX[DS_CNT] > 0 */
#else
	uint64_t wq_int                       : 64;
#endif
	} s;
	struct cvmx_sso_wq_int_s              cn68xx;
	struct cvmx_sso_wq_int_s              cn68xxp1;
};
typedef union cvmx_sso_wq_int cvmx_sso_wq_int_t;

/**
 * cvmx_sso_wq_int_cnt#
 *
 * CSR reserved addresses: (64): 0x7200..0x73f8
 * CSR align addresses: ===========================================================================================================
 * SSO_WQ_INT_CNTX = SSO Work Queue Interrupt Count Register
 *                   (one per group)
 *
 * Contains a read-only copy of the counts used to trigger work queue
 * interrupts.  For more information regarding this register, see the interrupt
 * section.
 */
union cvmx_sso_wq_int_cntx {
	uint64_t u64;
	struct cvmx_sso_wq_int_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t tc_cnt                       : 4;  /**< Time counter current value for group X
                                                         HW sets TC_CNT to SSO_WQ_INT_THRX[TC_THR] whenever:
                                                           - corresponding SSO_WQ_INT_CNTX[IQ_CNT]==0 and
                                                             corresponding SSO_WQ_INT_CNTX[DS_CNT]==0
                                                           - corresponding SSO_WQ_INT[WQ_INT<X>] is written
                                                             with a 1 by SW
                                                           - corresponding SSO_WQ_IQ_DIS[IQ_DIS<X>] is written
                                                             with a 1 by SW
                                                           - corresponding SSO_WQ_INT_THRX is written by SW
                                                           - TC_CNT==1 and periodic counter
                                                             SSO_WQ_INT_PC[PC]==0
                                                         Otherwise, HW decrements TC_CNT whenever the
                                                         periodic counter SSO_WQ_INT_PC[PC]==0.
                                                         TC_CNT is 0 whenever SSO_WQ_INT_THRX[TC_THR]==0. */
	uint64_t reserved_26_27               : 2;
	uint64_t ds_cnt                       : 12; /**< De-schedule executable count for group X */
	uint64_t reserved_12_13               : 2;
	uint64_t iq_cnt                       : 12; /**< Input queue executable count for group X */
#else
	uint64_t iq_cnt                       : 12;
	uint64_t reserved_12_13               : 2;
	uint64_t ds_cnt                       : 12;
	uint64_t reserved_26_27               : 2;
	uint64_t tc_cnt                       : 4;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sso_wq_int_cntx_s         cn68xx;
	struct cvmx_sso_wq_int_cntx_s         cn68xxp1;
};
typedef union cvmx_sso_wq_int_cntx cvmx_sso_wq_int_cntx_t;

/**
 * cvmx_sso_wq_int_pc
 *
 * CSR reserved addresses: (1): 0x1018..0x1018
 * SSO_WQ_INT_PC = SSO Work Queue Interrupt Periodic Counter Register
 *
 * Contains the threshold value for the work queue interrupt periodic counter
 * and also a read-only copy of the periodic counter.  For more information
 * regarding this register, see the interrupt section.
 */
union cvmx_sso_wq_int_pc {
	uint64_t u64;
	struct cvmx_sso_wq_int_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t pc                           : 28; /**< Work queue interrupt periodic counter */
	uint64_t reserved_28_31               : 4;
	uint64_t pc_thr                       : 20; /**< Work queue interrupt periodic counter threshold */
	uint64_t reserved_0_7                 : 8;
#else
	uint64_t reserved_0_7                 : 8;
	uint64_t pc_thr                       : 20;
	uint64_t reserved_28_31               : 4;
	uint64_t pc                           : 28;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_sso_wq_int_pc_s           cn68xx;
	struct cvmx_sso_wq_int_pc_s           cn68xxp1;
};
typedef union cvmx_sso_wq_int_pc cvmx_sso_wq_int_pc_t;

/**
 * cvmx_sso_wq_int_thr#
 *
 * CSR reserved addresses: (96): 0x6100..0x63f8
 * CSR align addresses: ===========================================================================================================
 * SSO_WQ_INT_THR(0..63) = SSO Work Queue Interrupt Threshold Registers
 *                         (one per group)
 *
 * Contains the thresholds for enabling and setting work queue interrupts.  For
 * more information, see the interrupt section.
 *
 * Note: Up to 16 of the SSO's internal storage buffers can be allocated
 * for hardware use and are therefore not available for incoming work queue
 * entries.  Additionally, any WS that is not in the EMPTY state consumes a
 * buffer.  Thus in a 32 PP system, it is not advisable to set either IQ_THR or
 * DS_THR to greater than 2048 - 16 - 32*2 = 1968.  Doing so may prevent the
 * interrupt from ever triggering.
 *
 * Priorities for QOS levels 0..7
 */
union cvmx_sso_wq_int_thrx {
	uint64_t u64;
	struct cvmx_sso_wq_int_thrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t tc_en                        : 1;  /**< Time counter interrupt enable for group X
                                                         TC_EN must be zero when TC_THR==0 */
	uint64_t tc_thr                       : 4;  /**< Time counter interrupt threshold for group X
                                                         When TC_THR==0, SSO_WQ_INT_CNTX[TC_CNT] is zero */
	uint64_t reserved_26_27               : 2;
	uint64_t ds_thr                       : 12; /**< De-schedule count threshold for group X
                                                         DS_THR==0 disables the threshold interrupt */
	uint64_t reserved_12_13               : 2;
	uint64_t iq_thr                       : 12; /**< Input queue count threshold for group X
                                                         IQ_THR==0 disables the threshold interrupt */
#else
	uint64_t iq_thr                       : 12;
	uint64_t reserved_12_13               : 2;
	uint64_t ds_thr                       : 12;
	uint64_t reserved_26_27               : 2;
	uint64_t tc_thr                       : 4;
	uint64_t tc_en                        : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_sso_wq_int_thrx_s         cn68xx;
	struct cvmx_sso_wq_int_thrx_s         cn68xxp1;
};
typedef union cvmx_sso_wq_int_thrx cvmx_sso_wq_int_thrx_t;

/**
 * cvmx_sso_wq_iq_dis
 *
 * CSR reserved addresses: (1): 0x1008..0x1008
 * SSO_WQ_IQ_DIS = SSO Input Queue Interrupt Temporary Disable Mask
 *
 * Contains the input queue interrupt temporary disable bits (one per group).
 * For more information regarding this register, see the interrupt section.
 */
union cvmx_sso_wq_iq_dis {
	uint64_t u64;
	struct cvmx_sso_wq_iq_dis_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t iq_dis                       : 64; /**< Input queue interrupt temporary disable mask
                                                         Corresponding SSO_WQ_INTX[WQ_INT<X>] bit cannot be
                                                         set due to IQ_CNT/IQ_THR check when this bit is set.
                                                         Corresponding IQ_DIS bit is cleared by HW whenever:
                                                          - SSO_WQ_INT_CNTX[IQ_CNT] is zero, or
                                                          - SSO_WQ_INT_CNTX[TC_CNT]==1 when periodic
                                                            counter SSO_WQ_INT_PC[PC]==0 */
#else
	uint64_t iq_dis                       : 64;
#endif
	} s;
	struct cvmx_sso_wq_iq_dis_s           cn68xx;
	struct cvmx_sso_wq_iq_dis_s           cn68xxp1;
};
typedef union cvmx_sso_wq_iq_dis cvmx_sso_wq_iq_dis_t;

/**
 * cvmx_sso_ws_pc#
 *
 * CSR reserved addresses: (225): 0x3100..0x3800
 * CSR align addresses: ===========================================================================================================
 * SSO_WS_PCX = SSO Work Schedule Performance Counter
 *              (one per group)
 *
 * Counts the number of work schedules for each group.
 * Counter rolls over through zero when max value exceeded.
 */
union cvmx_sso_ws_pcx {
	uint64_t u64;
	struct cvmx_sso_ws_pcx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ws_pc                        : 64; /**< Work schedule performance counter for group X */
#else
	uint64_t ws_pc                        : 64;
#endif
	} s;
	struct cvmx_sso_ws_pcx_s              cn68xx;
	struct cvmx_sso_ws_pcx_s              cn68xxp1;
};
typedef union cvmx_sso_ws_pcx cvmx_sso_ws_pcx_t;

#endif
