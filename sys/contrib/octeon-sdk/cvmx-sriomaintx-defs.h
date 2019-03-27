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
 * cvmx-sriomaintx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon sriomaintx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SRIOMAINTX_DEFS_H__
#define __CVMX_SRIOMAINTX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ASMBLY_ID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ASMBLY_ID(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000008ull;
}
#else
#define CVMX_SRIOMAINTX_ASMBLY_ID(block_id) (0x0000000000000008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ASMBLY_INFO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ASMBLY_INFO(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000000Cull;
}
#else
#define CVMX_SRIOMAINTX_ASMBLY_INFO(block_id) (0x000000000000000Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_BAR1_IDXX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 15)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 15)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_BAR1_IDXX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return 0x0000000000200010ull + (((offset) & 15) + ((block_id) & 3) * 0x0ull) * 4;
}
#else
#define CVMX_SRIOMAINTX_BAR1_IDXX(offset, block_id) (0x0000000000200010ull + (((offset) & 15) + ((block_id) & 3) * 0x0ull) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_BELL_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_BELL_STATUS(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200080ull;
}
#else
#define CVMX_SRIOMAINTX_BELL_STATUS(block_id) (0x0000000000200080ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_COMP_TAG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_COMP_TAG(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000006Cull;
}
#else
#define CVMX_SRIOMAINTX_COMP_TAG(block_id) (0x000000000000006Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_CORE_ENABLES(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_CORE_ENABLES(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200070ull;
}
#else
#define CVMX_SRIOMAINTX_CORE_ENABLES(block_id) (0x0000000000200070ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_DEV_ID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_DEV_ID(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000000ull;
}
#else
#define CVMX_SRIOMAINTX_DEV_ID(block_id) (0x0000000000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_DEV_REV(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_DEV_REV(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000004ull;
}
#else
#define CVMX_SRIOMAINTX_DEV_REV(block_id) (0x0000000000000004ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_DST_OPS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_DST_OPS(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000001Cull;
}
#else
#define CVMX_SRIOMAINTX_DST_OPS(block_id) (0x000000000000001Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_ATTR_CAPT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_ATTR_CAPT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002048ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_ATTR_CAPT(block_id) (0x0000000000002048ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_ERR_DET(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_ERR_DET(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002040ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_ERR_DET(block_id) (0x0000000000002040ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_ERR_RATE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_ERR_RATE(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002068ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_ERR_RATE(block_id) (0x0000000000002068ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_ERR_RATE_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_ERR_RATE_EN(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002044ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_ERR_RATE_EN(block_id) (0x0000000000002044ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_ERR_RATE_THR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_ERR_RATE_THR(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000206Cull;
}
#else
#define CVMX_SRIOMAINTX_ERB_ERR_RATE_THR(block_id) (0x000000000000206Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_HDR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_HDR(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002000ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_HDR(block_id) (0x0000000000002000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_ADDR_CAPT_H(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_ADDR_CAPT_H(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002010ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_ADDR_CAPT_H(block_id) (0x0000000000002010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_ADDR_CAPT_L(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_ADDR_CAPT_L(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002014ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_ADDR_CAPT_L(block_id) (0x0000000000002014ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_CTRL_CAPT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_CTRL_CAPT(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000201Cull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_CTRL_CAPT(block_id) (0x000000000000201Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_DEV_ID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_DEV_ID(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002028ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_DEV_ID(block_id) (0x0000000000002028ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_DEV_ID_CAPT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_DEV_ID_CAPT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002018ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_DEV_ID_CAPT(block_id) (0x0000000000002018ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_ERR_DET(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_ERR_DET(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002008ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_ERR_DET(block_id) (0x0000000000002008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_LT_ERR_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_LT_ERR_EN(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000200Cull;
}
#else
#define CVMX_SRIOMAINTX_ERB_LT_ERR_EN(block_id) (0x000000000000200Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_PACK_CAPT_1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_PACK_CAPT_1(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002050ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_PACK_CAPT_1(block_id) (0x0000000000002050ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_PACK_CAPT_2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_PACK_CAPT_2(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002054ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_PACK_CAPT_2(block_id) (0x0000000000002054ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_PACK_CAPT_3(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_PACK_CAPT_3(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000002058ull;
}
#else
#define CVMX_SRIOMAINTX_ERB_PACK_CAPT_3(block_id) (0x0000000000002058ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_ERB_PACK_SYM_CAPT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_ERB_PACK_SYM_CAPT(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000204Cull;
}
#else
#define CVMX_SRIOMAINTX_ERB_PACK_SYM_CAPT(block_id) (0x000000000000204Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_HB_DEV_ID_LOCK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_HB_DEV_ID_LOCK(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000068ull;
}
#else
#define CVMX_SRIOMAINTX_HB_DEV_ID_LOCK(block_id) (0x0000000000000068ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_BUFFER_CONFIG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_BUFFER_CONFIG(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000102000ull;
}
#else
#define CVMX_SRIOMAINTX_IR_BUFFER_CONFIG(block_id) (0x0000000000102000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_BUFFER_CONFIG2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_BUFFER_CONFIG2(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000102004ull;
}
#else
#define CVMX_SRIOMAINTX_IR_BUFFER_CONFIG2(block_id) (0x0000000000102004ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_PD_PHY_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_PD_PHY_CTRL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107028ull;
}
#else
#define CVMX_SRIOMAINTX_IR_PD_PHY_CTRL(block_id) (0x0000000000107028ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_PD_PHY_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_PD_PHY_STAT(%lu) is invalid on this chip\n", block_id);
	return 0x000000000010702Cull;
}
#else
#define CVMX_SRIOMAINTX_IR_PD_PHY_STAT(block_id) (0x000000000010702Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_PI_PHY_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_PI_PHY_CTRL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107020ull;
}
#else
#define CVMX_SRIOMAINTX_IR_PI_PHY_CTRL(block_id) (0x0000000000107020ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_PI_PHY_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_PI_PHY_STAT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107024ull;
}
#else
#define CVMX_SRIOMAINTX_IR_PI_PHY_STAT(block_id) (0x0000000000107024ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_SP_RX_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_SP_RX_CTRL(%lu) is invalid on this chip\n", block_id);
	return 0x000000000010700Cull;
}
#else
#define CVMX_SRIOMAINTX_IR_SP_RX_CTRL(block_id) (0x000000000010700Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_SP_RX_DATA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_SP_RX_DATA(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107014ull;
}
#else
#define CVMX_SRIOMAINTX_IR_SP_RX_DATA(block_id) (0x0000000000107014ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_SP_RX_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_SP_RX_STAT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107010ull;
}
#else
#define CVMX_SRIOMAINTX_IR_SP_RX_STAT(block_id) (0x0000000000107010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_SP_TX_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_SP_TX_CTRL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107000ull;
}
#else
#define CVMX_SRIOMAINTX_IR_SP_TX_CTRL(block_id) (0x0000000000107000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_SP_TX_DATA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_SP_TX_DATA(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107008ull;
}
#else
#define CVMX_SRIOMAINTX_IR_SP_TX_DATA(block_id) (0x0000000000107008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_IR_SP_TX_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_IR_SP_TX_STAT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000107004ull;
}
#else
#define CVMX_SRIOMAINTX_IR_SP_TX_STAT(block_id) (0x0000000000107004ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_LANE_X_STATUS_0(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 3)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_LANE_X_STATUS_0(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return 0x0000000000001010ull + (((offset) & 3) + ((block_id) & 3) * 0x0ull) * 32;
}
#else
#define CVMX_SRIOMAINTX_LANE_X_STATUS_0(offset, block_id) (0x0000000000001010ull + (((offset) & 3) + ((block_id) & 3) * 0x0ull) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_LCS_BA0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_LCS_BA0(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000058ull;
}
#else
#define CVMX_SRIOMAINTX_LCS_BA0(block_id) (0x0000000000000058ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_LCS_BA1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_LCS_BA1(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000005Cull;
}
#else
#define CVMX_SRIOMAINTX_LCS_BA1(block_id) (0x000000000000005Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_M2S_BAR0_START0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_M2S_BAR0_START0(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200000ull;
}
#else
#define CVMX_SRIOMAINTX_M2S_BAR0_START0(block_id) (0x0000000000200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_M2S_BAR0_START1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_M2S_BAR0_START1(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200004ull;
}
#else
#define CVMX_SRIOMAINTX_M2S_BAR0_START1(block_id) (0x0000000000200004ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_M2S_BAR1_START0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_M2S_BAR1_START0(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200008ull;
}
#else
#define CVMX_SRIOMAINTX_M2S_BAR1_START0(block_id) (0x0000000000200008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_M2S_BAR1_START1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_M2S_BAR1_START1(%lu) is invalid on this chip\n", block_id);
	return 0x000000000020000Cull;
}
#else
#define CVMX_SRIOMAINTX_M2S_BAR1_START1(block_id) (0x000000000020000Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_M2S_BAR2_START(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_M2S_BAR2_START(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200050ull;
}
#else
#define CVMX_SRIOMAINTX_M2S_BAR2_START(block_id) (0x0000000000200050ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_MAC_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_MAC_CTRL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200068ull;
}
#else
#define CVMX_SRIOMAINTX_MAC_CTRL(block_id) (0x0000000000200068ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PE_FEAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PE_FEAT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000010ull;
}
#else
#define CVMX_SRIOMAINTX_PE_FEAT(block_id) (0x0000000000000010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PE_LLC(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PE_LLC(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000004Cull;
}
#else
#define CVMX_SRIOMAINTX_PE_LLC(block_id) (0x000000000000004Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_0_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_0_CTL(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000015Cull;
}
#else
#define CVMX_SRIOMAINTX_PORT_0_CTL(block_id) (0x000000000000015Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_0_CTL2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_0_CTL2(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000154ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_0_CTL2(block_id) (0x0000000000000154ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_0_ERR_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_0_ERR_STAT(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000158ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_0_ERR_STAT(block_id) (0x0000000000000158ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_0_LINK_REQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_0_LINK_REQ(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000140ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_0_LINK_REQ(block_id) (0x0000000000000140ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_0_LINK_RESP(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_0_LINK_RESP(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000144ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_0_LINK_RESP(block_id) (0x0000000000000144ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_0_LOCAL_ACKID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_0_LOCAL_ACKID(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000148ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_0_LOCAL_ACKID(block_id) (0x0000000000000148ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_GEN_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_GEN_CTL(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000013Cull;
}
#else
#define CVMX_SRIOMAINTX_PORT_GEN_CTL(block_id) (0x000000000000013Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_LT_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_LT_CTL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000120ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_LT_CTL(block_id) (0x0000000000000120ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_MBH0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_MBH0(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000100ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_MBH0(block_id) (0x0000000000000100ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_RT_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_RT_CTL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000124ull;
}
#else
#define CVMX_SRIOMAINTX_PORT_RT_CTL(block_id) (0x0000000000000124ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PORT_TTL_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PORT_TTL_CTL(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000012Cull;
}
#else
#define CVMX_SRIOMAINTX_PORT_TTL_CTL(block_id) (0x000000000000012Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_PRI_DEV_ID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_PRI_DEV_ID(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000060ull;
}
#else
#define CVMX_SRIOMAINTX_PRI_DEV_ID(block_id) (0x0000000000000060ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_SEC_DEV_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_SEC_DEV_CTRL(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200064ull;
}
#else
#define CVMX_SRIOMAINTX_SEC_DEV_CTRL(block_id) (0x0000000000200064ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_SEC_DEV_ID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_SEC_DEV_ID(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000200060ull;
}
#else
#define CVMX_SRIOMAINTX_SEC_DEV_ID(block_id) (0x0000000000200060ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_SERIAL_LANE_HDR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_SERIAL_LANE_HDR(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000001000ull;
}
#else
#define CVMX_SRIOMAINTX_SERIAL_LANE_HDR(block_id) (0x0000000000001000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_SRC_OPS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_SRC_OPS(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000018ull;
}
#else
#define CVMX_SRIOMAINTX_SRC_OPS(block_id) (0x0000000000000018ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOMAINTX_TX_DROP(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOMAINTX_TX_DROP(%lu) is invalid on this chip\n", block_id);
	return 0x000000000020006Cull;
}
#else
#define CVMX_SRIOMAINTX_TX_DROP(block_id) (0x000000000020006Cull)
#endif

/**
 * cvmx_sriomaint#_asmbly_id
 *
 * SRIOMAINT_ASMBLY_ID = SRIO Assembly ID
 *
 * The Assembly ID register shows the Assembly ID and Vendor
 *
 * Notes:
 * The Assembly ID register shows the Assembly ID and Vendor specified in $SRIO_ASMBLY_ID.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ASMBLY_ID     hclk    hrst_n
 */
union cvmx_sriomaintx_asmbly_id {
	uint32_t u32;
	struct cvmx_sriomaintx_asmbly_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t assy_id                      : 16; /**< Assembly Identifer */
	uint32_t assy_ven                     : 16; /**< Assembly Vendor Identifer */
#else
	uint32_t assy_ven                     : 16;
	uint32_t assy_id                      : 16;
#endif
	} s;
	struct cvmx_sriomaintx_asmbly_id_s    cn63xx;
	struct cvmx_sriomaintx_asmbly_id_s    cn63xxp1;
	struct cvmx_sriomaintx_asmbly_id_s    cn66xx;
};
typedef union cvmx_sriomaintx_asmbly_id cvmx_sriomaintx_asmbly_id_t;

/**
 * cvmx_sriomaint#_asmbly_info
 *
 * SRIOMAINT_ASMBLY_INFO = SRIO Assembly Information
 *
 * The Assembly Info register shows the Assembly Revision specified in $SRIO_ASMBLY_INFO
 *
 * Notes:
 * The Assembly Info register shows the Assembly Revision specified in $SRIO_ASMBLY_INFO and Extended
 *  Feature Pointer.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ASMBLY_INFO   hclk    hrst_n
 */
union cvmx_sriomaintx_asmbly_info {
	uint32_t u32;
	struct cvmx_sriomaintx_asmbly_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t assy_rev                     : 16; /**< Assembly Revision */
	uint32_t ext_fptr                     : 16; /**< Pointer to the first entry in the extended feature
                                                         list. */
#else
	uint32_t ext_fptr                     : 16;
	uint32_t assy_rev                     : 16;
#endif
	} s;
	struct cvmx_sriomaintx_asmbly_info_s  cn63xx;
	struct cvmx_sriomaintx_asmbly_info_s  cn63xxp1;
	struct cvmx_sriomaintx_asmbly_info_s  cn66xx;
};
typedef union cvmx_sriomaintx_asmbly_info cvmx_sriomaintx_asmbly_info_t;

/**
 * cvmx_sriomaint#_bar1_idx#
 *
 * SRIOMAINT_BAR1_IDXX = SRIO BAR1 IndexX Register
 *
 * Contains address index and control bits for access to memory ranges of BAR1.
 *
 * Notes:
 * This register specifies the Octeon address, endian swap and cache status associated with each of
 *  the 16 BAR1 entries.  The local address bits used are based on the BARSIZE field located in the
 *  SRIOMAINT(0,2..3)_M2S_BAR1_START0 register.  This register is only writeable over SRIO if the
 *  SRIO(0,2..3)_ACC_CTRL.DENY_BAR1 bit is zero.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_BAR1_IDX[0:15]        hclk    hrst_n
 */
union cvmx_sriomaintx_bar1_idxx {
	uint32_t u32;
	struct cvmx_sriomaintx_bar1_idxx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_30_31               : 2;
	uint32_t la                           : 22; /**< L2/DRAM Address bits [37:16]
                                                         Not all LA[21:0] bits are used by SRIO hardware,
                                                         depending on SRIOMAINT(0,2..3)_M2S_BAR1_START1[BARSIZE].

                                                                                 Become
                                                                                 L2/DRAM
                                                                                 Address  Entry
                                                         BARSIZE   LA Bits Used   Bits    Size
                                                            0        LA[21:0]    [37:16]   64KB
                                                            1        LA[21:1]    [37:17]  128KB
                                                            2        LA[21:2]    [37:18]  256KB
                                                            3        LA[21:3]    [37:19]  512KB
                                                            4        LA[21:4]    [37:20]    1MB
                                                            5        LA[21:5]    [37:21]    2MB
                                                            6        LA[21:6]    [37:22]    4MB
                                                            7        LA[21:7]    [37:23]    8MB
                                                            8        LA[21:8]    [37:24]   16MB
                                                            9        LA[21:9]    [37:25]   32MB
                                                           10        LA[21:10]   [37:26]   64MB
                                                           11        LA[21:11]   [37:27]  128MB
                                                           12        LA[21:12]   [37:28]  256MB
                                                           13        LA[21:13]   [37:29]  512MB */
	uint32_t reserved_6_7                 : 2;
	uint32_t es                           : 2;  /**< Endian Swap Mode.
                                                         0 = No Swap
                                                         1 = 64-bit Swap Bytes [ABCD_EFGH] -> [HGFE_DCBA]
                                                         2 = 32-bit Swap Words [ABCD_EFGH] -> [DCBA_HGFE]
                                                         3 = 32-bit Word Exch  [ABCD_EFGH] -> [EFGH_ABCD] */
	uint32_t nca                          : 1;  /**< Non-Cacheable Access Mode.  When set, transfers
                                                         through this window are not cacheable. */
	uint32_t reserved_1_2                 : 2;
	uint32_t enable                       : 1;  /**< When set the selected index address is valid. */
#else
	uint32_t enable                       : 1;
	uint32_t reserved_1_2                 : 2;
	uint32_t nca                          : 1;
	uint32_t es                           : 2;
	uint32_t reserved_6_7                 : 2;
	uint32_t la                           : 22;
	uint32_t reserved_30_31               : 2;
#endif
	} s;
	struct cvmx_sriomaintx_bar1_idxx_s    cn63xx;
	struct cvmx_sriomaintx_bar1_idxx_s    cn63xxp1;
	struct cvmx_sriomaintx_bar1_idxx_s    cn66xx;
};
typedef union cvmx_sriomaintx_bar1_idxx cvmx_sriomaintx_bar1_idxx_t;

/**
 * cvmx_sriomaint#_bell_status
 *
 * SRIOMAINT_BELL_STATUS = SRIO Incoming Doorbell Status
 *
 * The SRIO Incoming (RX) Doorbell Status
 *
 * Notes:
 * This register displays the status of the doorbells received.  If FULL is set the SRIO device will
 *  retry incoming transactions.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_BELL_STATUS   hclk    hrst_n
 */
union cvmx_sriomaintx_bell_status {
	uint32_t u32;
	struct cvmx_sriomaintx_bell_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t full                         : 1;  /**< Not able to receive Doorbell Transactions */
#else
	uint32_t full                         : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_sriomaintx_bell_status_s  cn63xx;
	struct cvmx_sriomaintx_bell_status_s  cn63xxp1;
	struct cvmx_sriomaintx_bell_status_s  cn66xx;
};
typedef union cvmx_sriomaintx_bell_status cvmx_sriomaintx_bell_status_t;

/**
 * cvmx_sriomaint#_comp_tag
 *
 * SRIOMAINT_COMP_TAG = SRIO Component Tag
 *
 * Component Tag
 *
 * Notes:
 * This register contains a component tag value for the processing element and the value can be
 *  assigned by software when the device is initialized.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_COMP_TAG      hclk    hrst_n
 */
union cvmx_sriomaintx_comp_tag {
	uint32_t u32;
	struct cvmx_sriomaintx_comp_tag_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t comp_tag                     : 32; /**< Component Tag for Firmware Use */
#else
	uint32_t comp_tag                     : 32;
#endif
	} s;
	struct cvmx_sriomaintx_comp_tag_s     cn63xx;
	struct cvmx_sriomaintx_comp_tag_s     cn63xxp1;
	struct cvmx_sriomaintx_comp_tag_s     cn66xx;
};
typedef union cvmx_sriomaintx_comp_tag cvmx_sriomaintx_comp_tag_t;

/**
 * cvmx_sriomaint#_core_enables
 *
 * SRIOMAINT_CORE_ENABLES = SRIO Core Control
 *
 * Core Control
 *
 * Notes:
 * This register displays the reset state of the Octeon Core Logic while the SRIO Link is running.
 *  The bit should be set after the software has initialized the chip to allow memory operations.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_CORE_ENABLES  hclk    hrst_n, srst_n
 */
union cvmx_sriomaintx_core_enables {
	uint32_t u32;
	struct cvmx_sriomaintx_core_enables_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_5_31                : 27;
	uint32_t halt                         : 1;  /**< OCTEON currently in Reset
                                                         0 = All OCTEON resources are available.
                                                         1 = The OCTEON is in reset. When this bit is set,
                                                             SRIO maintenance registers can be accessed,
                                                             but BAR0, BAR1, and BAR2 cannot be. */
	uint32_t imsg1                        : 1;  /**< Allow Incoming Message Unit 1 Operations
                                                         Note: This bit is cleared when the C63XX is reset
                                                          0 = SRIO Incoming Messages to Unit 1 ignored and
                                                              return error response
                                                          1 = SRIO Incoming Messages to Unit 1 */
	uint32_t imsg0                        : 1;  /**< Allow Incoming Message Unit 0 Operations
                                                         Note: This bit is cleared when the C63XX is reset
                                                          0 = SRIO Incoming Messages to Unit 0 ignored and
                                                              return error response
                                                          1 = SRIO Incoming Messages to Unit 0 */
	uint32_t doorbell                     : 1;  /**< Allow Inbound Doorbell Operations
                                                         Note: This bit is cleared when the C63XX is reset
                                                          0 = SRIO Doorbell OPs ignored and return error
                                                              response
                                                          1 = SRIO Doorbell OPs Allowed */
	uint32_t memory                       : 1;  /**< Allow Inbound/Outbound Memory Operations
                                                         Note: This bit is cleared when the C63XX is reset
                                                          0 = SRIO Incoming Nwrites and Swrites are
                                                              dropped.  Incoming Nreads, Atomics and
                                                              NwriteRs return responses with ERROR status.
                                                              SRIO Incoming Maintenance BAR Memory Accesses
                                                              are processed normally.
                                                              Outgoing Store Operations are Dropped
                                                              Outgoing Load Operations are not issued and
                                                              return all 1's with an ERROR status.
                                                              In Flight Operations started while the bit is
                                                              set in both directions will complete normally.
                                                          1 = SRIO Memory Read/Write OPs Allowed */
#else
	uint32_t memory                       : 1;
	uint32_t doorbell                     : 1;
	uint32_t imsg0                        : 1;
	uint32_t imsg1                        : 1;
	uint32_t halt                         : 1;
	uint32_t reserved_5_31                : 27;
#endif
	} s;
	struct cvmx_sriomaintx_core_enables_s cn63xx;
	struct cvmx_sriomaintx_core_enables_s cn63xxp1;
	struct cvmx_sriomaintx_core_enables_s cn66xx;
};
typedef union cvmx_sriomaintx_core_enables cvmx_sriomaintx_core_enables_t;

/**
 * cvmx_sriomaint#_dev_id
 *
 * SRIOMAINT_DEV_ID = SRIO Device ID
 *
 * The DeviceVendor Identity field identifies the vendor that manufactured the device
 *
 * Notes:
 * This register identifies Cavium Inc. and the Product ID.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_DEV_ID        hclk    hrst_n
 */
union cvmx_sriomaintx_dev_id {
	uint32_t u32;
	struct cvmx_sriomaintx_dev_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t device                       : 16; /**< Product Identity */
	uint32_t vendor                       : 16; /**< Cavium Vendor Identity */
#else
	uint32_t vendor                       : 16;
	uint32_t device                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_dev_id_s       cn63xx;
	struct cvmx_sriomaintx_dev_id_s       cn63xxp1;
	struct cvmx_sriomaintx_dev_id_s       cn66xx;
};
typedef union cvmx_sriomaintx_dev_id cvmx_sriomaintx_dev_id_t;

/**
 * cvmx_sriomaint#_dev_rev
 *
 * SRIOMAINT_DEV_REV = SRIO Device Revision
 *
 * The Device Revision register identifies the chip pass and revision
 *
 * Notes:
 * This register identifies the chip pass and revision derived from the fuses.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_DEV_REV       hclk    hrst_n
 */
union cvmx_sriomaintx_dev_rev {
	uint32_t u32;
	struct cvmx_sriomaintx_dev_rev_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t revision                     : 8;  /**< Chip Pass/Revision */
#else
	uint32_t revision                     : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_sriomaintx_dev_rev_s      cn63xx;
	struct cvmx_sriomaintx_dev_rev_s      cn63xxp1;
	struct cvmx_sriomaintx_dev_rev_s      cn66xx;
};
typedef union cvmx_sriomaintx_dev_rev cvmx_sriomaintx_dev_rev_t;

/**
 * cvmx_sriomaint#_dst_ops
 *
 * SRIOMAINT_DST_OPS = SRIO Source Operations
 *
 * The logical operations supported from external devices.
 *
 * Notes:
 * The logical operations supported from external devices.   The Destination OPs register shows the
 *  operations specified in the SRIO(0,2..3)_IP_FEATURE.OPS register.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_DST_OPS       hclk    hrst_n
 */
union cvmx_sriomaintx_dst_ops {
	uint32_t u32;
	struct cvmx_sriomaintx_dst_ops_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t gsm_read                     : 1;  /**< PE does not support Read Home operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<31>] */
	uint32_t i_read                       : 1;  /**< PE does not support Instruction Read.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<30>] */
	uint32_t rd_own                       : 1;  /**< PE does not support Read for Ownership.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<29>] */
	uint32_t d_invald                     : 1;  /**< PE does not support Data Cache Invalidate.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<28>] */
	uint32_t castout                      : 1;  /**< PE does not support Castout Operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<27>] */
	uint32_t d_flush                      : 1;  /**< PE does not support Data Cache Flush.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<26>] */
	uint32_t io_read                      : 1;  /**< PE does not support IO Read.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<25>] */
	uint32_t i_invald                     : 1;  /**< PE does not support Instruction Cache Invalidate.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<24>] */
	uint32_t tlb_inv                      : 1;  /**< PE does not support TLB Entry Invalidate.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<23>] */
	uint32_t tlb_invs                     : 1;  /**< PE does not support TLB Entry Invalidate Sync.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<22>] */
	uint32_t reserved_16_21               : 6;
	uint32_t read                         : 1;  /**< PE can support Nread operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<15>] */
	uint32_t write                        : 1;  /**< PE can support Nwrite operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<14>] */
	uint32_t swrite                       : 1;  /**< PE can support Swrite operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<13>] */
	uint32_t write_r                      : 1;  /**< PE can support Write with Response operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<12>] */
	uint32_t msg                          : 1;  /**< PE can support Data Message operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<11>] */
	uint32_t doorbell                     : 1;  /**< PE can support Doorbell operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<10>] */
	uint32_t compswap                     : 1;  /**< PE does not support Atomic Compare and Swap.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<9>] */
	uint32_t testswap                     : 1;  /**< PE does not support Atomic Test and Swap.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<8>] */
	uint32_t atom_inc                     : 1;  /**< PE can support Atomic increment operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<7>] */
	uint32_t atom_dec                     : 1;  /**< PE can support Atomic decrement operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<6>] */
	uint32_t atom_set                     : 1;  /**< PE can support Atomic set operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<5>] */
	uint32_t atom_clr                     : 1;  /**< PE can support Atomic clear operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<4>] */
	uint32_t atom_swp                     : 1;  /**< PE does not support Atomic Swap.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<3>] */
	uint32_t port_wr                      : 1;  /**< PE can Port Write operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<2>] */
	uint32_t reserved_0_1                 : 2;
#else
	uint32_t reserved_0_1                 : 2;
	uint32_t port_wr                      : 1;
	uint32_t atom_swp                     : 1;
	uint32_t atom_clr                     : 1;
	uint32_t atom_set                     : 1;
	uint32_t atom_dec                     : 1;
	uint32_t atom_inc                     : 1;
	uint32_t testswap                     : 1;
	uint32_t compswap                     : 1;
	uint32_t doorbell                     : 1;
	uint32_t msg                          : 1;
	uint32_t write_r                      : 1;
	uint32_t swrite                       : 1;
	uint32_t write                        : 1;
	uint32_t read                         : 1;
	uint32_t reserved_16_21               : 6;
	uint32_t tlb_invs                     : 1;
	uint32_t tlb_inv                      : 1;
	uint32_t i_invald                     : 1;
	uint32_t io_read                      : 1;
	uint32_t d_flush                      : 1;
	uint32_t castout                      : 1;
	uint32_t d_invald                     : 1;
	uint32_t rd_own                       : 1;
	uint32_t i_read                       : 1;
	uint32_t gsm_read                     : 1;
#endif
	} s;
	struct cvmx_sriomaintx_dst_ops_s      cn63xx;
	struct cvmx_sriomaintx_dst_ops_s      cn63xxp1;
	struct cvmx_sriomaintx_dst_ops_s      cn66xx;
};
typedef union cvmx_sriomaintx_dst_ops cvmx_sriomaintx_dst_ops_t;

/**
 * cvmx_sriomaint#_erb_attr_capt
 *
 * SRIOMAINT_ERB_ATTR_CAPT = SRIO Attributes Capture
 *
 * Attributes Capture
 *
 * Notes:
 * This register contains the information captured during the error.
 *  The HW will not update this register (i.e. this register is locked) while
 *  VALID is set in this CSR.
 *  The HW sets SRIO_INT_REG[PHY_ERB] every time it sets VALID in this CSR.
 *  To handle the interrupt, the following procedure may be best:
 *       (1) clear SRIO_INT_REG[PHY_ERB],
 *       (2) read this CSR, corresponding SRIOMAINT*_ERB_ERR_DET, SRIOMAINT*_ERB_PACK_SYM_CAPT,
 *           SRIOMAINT*_ERB_PACK_CAPT_1, SRIOMAINT*_ERB_PACK_CAPT_2, and SRIOMAINT*_ERB_PACK_CAPT_3
 *       (3) Write VALID in this CSR to 0.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_ATTR_CAPT hclk    hrst_n
 */
union cvmx_sriomaintx_erb_attr_capt {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_attr_capt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t inf_type                     : 3;  /**< Type of Information Logged.
                                                         000 - Packet
                                                         010 - Short Control Symbol
                                                               (use only first capture register)
                                                         100 - Implementation Specific Error Reporting
                                                         All Others Reserved */
	uint32_t err_type                     : 5;  /**< The encoded value of the 31 minus the bit in
                                                         SRIOMAINT(0,2..3)_ERB_ERR_DET that describes the error
                                                         captured in SRIOMAINT(0,2..3)_ERB_*CAPT Registers.
                                                         (For example a value of 5 indicates 31-5 = bit 26) */
	uint32_t err_info                     : 20; /**< Error Info.
                                                         ERR_TYPE Bits   Description
                                                            0     23     TX Protocol Error
                                                                  22     RX Protocol Error
                                                                  21     TX Link Response Timeout
                                                                  20     TX ACKID Timeout
                                                                  - 19:16  Reserved
                                                                  - 15:12  TX Protocol ID
                                                                         1 = Rcvd Unexpected Link Response
                                                                         2 = Rcvd Link Response before Req
                                                                         3 = Rcvd NACK servicing NACK
                                                                         4 = Rcvd NACK
                                                                         5 = Rcvd RETRY servicing RETRY
                                                                         6 = Rcvd RETRY servicing NACK
                                                                         7 = Rcvd ACK servicing RETRY
                                                                         8 = Rcvd ACK servicing NACK
                                                                         9 = Unexp ACKID on ACK or RETRY
                                                                        10 = Unexp ACK or RETRY
                                                                  - 11:8   Reserved
                                                                  - 7:4   RX Protocol ID
                                                                         1 = Rcvd EOP w/o Prev SOP
                                                                         2 = Rcvd STOMP w/o Prev SOP
                                                                         3 = Unexp RESTART
                                                                         4 = Redundant Status from LinkReq
                                                          9-16    23:20  RX K Bits
                                                                  - 19:0   Reserved
                                                           26     23:20  RX K Bits
                                                                  - 19:0   Reserved
                                                           27     23:12  Type
                                                                           0x000 TX
                                                                           0x010 RX
                                                                  - 11:8   RX or TX Protocol ID (see above)
                                                                  - 7:4   Reserved
                                                           30     23:20  RX K Bits
                                                                  - 19:0   Reserved
                                                           31     23:16  ACKID Timeout 0x2
                                                                  - 15:14  Reserved
                                                                  - 13:8   AckID
                                                                  - 7:4   Reserved
                                                           All others ERR_TYPEs are reserved. */
	uint32_t reserved_1_3                 : 3;
	uint32_t valid                        : 1;  /**< This bit is set by hardware to indicate that the
                                                         Packet/control symbol capture registers contain
                                                         valid information. For control symbols, only
                                                         capture register 0 will contain meaningful
                                                         information.  This bit must be cleared by software
                                                         to allow capture of other errors. */
#else
	uint32_t valid                        : 1;
	uint32_t reserved_1_3                 : 3;
	uint32_t err_info                     : 20;
	uint32_t err_type                     : 5;
	uint32_t inf_type                     : 3;
#endif
	} s;
	struct cvmx_sriomaintx_erb_attr_capt_s cn63xx;
	struct cvmx_sriomaintx_erb_attr_capt_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t inf_type                     : 3;  /**< Type of Information Logged.
                                                         000 - Packet
                                                         010 - Short Control Symbol
                                                               (use only first capture register)
                                                         All Others Reserved */
	uint32_t err_type                     : 5;  /**< The encoded value of the 31 minus the bit in
                                                         SRIOMAINT(0..1)_ERB_ERR_DET that describes the error
                                                         captured in SRIOMAINT(0..1)_ERB_*CAPT Registers.
                                                         (For example a value of 5 indicates 31-5 = bit 26) */
	uint32_t reserved_1_23                : 23;
	uint32_t valid                        : 1;  /**< This bit is set by hardware to indicate that the
                                                         Packet/control symbol capture registers contain
                                                         valid information. For control symbols, only
                                                         capture register 0 will contain meaningful
                                                         information.  This bit must be cleared by software
                                                         to allow capture of other errors. */
#else
	uint32_t valid                        : 1;
	uint32_t reserved_1_23                : 23;
	uint32_t err_type                     : 5;
	uint32_t inf_type                     : 3;
#endif
	} cn63xxp1;
	struct cvmx_sriomaintx_erb_attr_capt_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_attr_capt cvmx_sriomaintx_erb_attr_capt_t;

/**
 * cvmx_sriomaint#_erb_err_det
 *
 * SRIOMAINT_ERB_ERR_DET = SRIO Error Detect
 *
 * Error Detect
 *
 * Notes:
 * The Error Detect Register indicates physical layer transmission errors detected by the hardware.
 *  The HW will not update this register (i.e. this register is locked) while
 *  SRIOMAINT*_ERB_ATTR_CAPT[VALID] is set.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_ERR_DET   hclk    hrst_n
 */
union cvmx_sriomaintx_erb_err_det {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_err_det_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t imp_err                      : 1;  /**< Implementation Specific Error. */
	uint32_t reserved_23_30               : 8;
	uint32_t ctl_crc                      : 1;  /**< Received a control symbol with a bad CRC value
                                                         Complete Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t uns_id                       : 1;  /**< Received an acknowledge control symbol with an
                                                         unexpected ackID (packet-accepted or packet_retry)
                                                         Partial Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t nack                         : 1;  /**< Received packet-not-accepted acknowledge control
                                                         symbols.
                                                         Partial Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t out_ack                      : 1;  /**< Received packet with unexpected ackID value
                                                         Header in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t pkt_crc                      : 1;  /**< Received a packet with a bad CRC value
                                                         Header in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t size                         : 1;  /**< Received packet which exceeds the maximum allowed
                                                         size of 276 bytes.
                                                         Header in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t inv_char                     : 1;  /**< Received illegal, 8B/10B error  or undefined
                                                         codegroup within a packet.
                                                         Header in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t inv_data                     : 1;  /**< Received data codegroup or 8B/10B error within an
                                                         IDLE sequence.
                                                         Header in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t reserved_6_14                : 9;
	uint32_t bad_ack                      : 1;  /**< Link_response received with an ackID that is not
                                                         outstanding.
                                                         Partial Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t proterr                      : 1;  /**< An unexpected packet or control symbol was
                                                         received.
                                                         Partial Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t f_toggle                     : 1;  /**< Reserved. */
	uint32_t del_err                      : 1;  /**< Received illegal or undefined codegroup.
                                                         (either INV_DATA or INV_CHAR)
                                                         Complete Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t uns_ack                      : 1;  /**< An unexpected acknowledge control symbol was
                                                         received.
                                                         Partial Symbol in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
	uint32_t lnk_tout                     : 1;  /**< An acknowledge or link-response control symbol is
                                                         not received within the specified timeout interval
                                                         Partial Header in SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT */
#else
	uint32_t lnk_tout                     : 1;
	uint32_t uns_ack                      : 1;
	uint32_t del_err                      : 1;
	uint32_t f_toggle                     : 1;
	uint32_t proterr                      : 1;
	uint32_t bad_ack                      : 1;
	uint32_t reserved_6_14                : 9;
	uint32_t inv_data                     : 1;
	uint32_t inv_char                     : 1;
	uint32_t size                         : 1;
	uint32_t pkt_crc                      : 1;
	uint32_t out_ack                      : 1;
	uint32_t nack                         : 1;
	uint32_t uns_id                       : 1;
	uint32_t ctl_crc                      : 1;
	uint32_t reserved_23_30               : 8;
	uint32_t imp_err                      : 1;
#endif
	} s;
	struct cvmx_sriomaintx_erb_err_det_s  cn63xx;
	struct cvmx_sriomaintx_erb_err_det_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t ctl_crc                      : 1;  /**< Received a control symbol with a bad CRC value
                                                         Complete Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t uns_id                       : 1;  /**< Received an acknowledge control symbol with an
                                                         unexpected ackID (packet-accepted or packet_retry)
                                                         Partial Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t nack                         : 1;  /**< Received packet-not-accepted acknowledge control
                                                         symbols.
                                                         Partial Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t out_ack                      : 1;  /**< Received packet with unexpected ackID value
                                                         Header in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t pkt_crc                      : 1;  /**< Received a packet with a bad CRC value
                                                         Header in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t size                         : 1;  /**< Received packet which exceeds the maximum allowed
                                                         size of 276 bytes.
                                                         Header in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t reserved_6_16                : 11;
	uint32_t bad_ack                      : 1;  /**< Link_response received with an ackID that is not
                                                         outstanding.
                                                         Partial Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t proterr                      : 1;  /**< An unexpected packet or control symbol was
                                                         received.
                                                         Partial Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t f_toggle                     : 1;  /**< Reserved. */
	uint32_t del_err                      : 1;  /**< Received illegal or undefined codegroup.
                                                         (either INV_DATA or INV_CHAR) (Pass 2)
                                                         Complete Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t uns_ack                      : 1;  /**< An unexpected acknowledge control symbol was
                                                         received.
                                                         Partial Symbol in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
	uint32_t lnk_tout                     : 1;  /**< An acknowledge or link-response control symbol is
                                                         not received within the specified timeout interval
                                                         Partial Header in SRIOMAINT(0..1)_ERB_PACK_SYM_CAPT */
#else
	uint32_t lnk_tout                     : 1;
	uint32_t uns_ack                      : 1;
	uint32_t del_err                      : 1;
	uint32_t f_toggle                     : 1;
	uint32_t proterr                      : 1;
	uint32_t bad_ack                      : 1;
	uint32_t reserved_6_16                : 11;
	uint32_t size                         : 1;
	uint32_t pkt_crc                      : 1;
	uint32_t out_ack                      : 1;
	uint32_t nack                         : 1;
	uint32_t uns_id                       : 1;
	uint32_t ctl_crc                      : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} cn63xxp1;
	struct cvmx_sriomaintx_erb_err_det_s  cn66xx;
};
typedef union cvmx_sriomaintx_erb_err_det cvmx_sriomaintx_erb_err_det_t;

/**
 * cvmx_sriomaint#_erb_err_rate
 *
 * SRIOMAINT_ERB_ERR_RATE = SRIO Error Rate
 *
 * Error Rate
 *
 * Notes:
 * The Error Rate register is used with the Error Rate Threshold register to monitor and control the
 *  reporting of transmission errors.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_ERR_RATE  hclk    hrst_n
 */
union cvmx_sriomaintx_erb_err_rate {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_err_rate_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t err_bias                     : 8;  /**< These bits provide the error rate bias value.
                                                         0x00 - do not decrement the error rate counter
                                                         0x01 - decrement every 1ms (+/-34%)
                                                         0x02 - decrement every 10ms (+/-34%)
                                                         0x04 - decrement every 100ms (+/-34%)
                                                         0x08 - decrement every 1s (+/-34%)
                                                         0x10 - decrement every 10s (+/-34%)
                                                         0x20 - decrement every 100s (+/-34%)
                                                         0x40 - decrement every 1000s (+/-34%)
                                                         0x80 - decrement every 10000s (+/-34%)
                                                         All other values are reserved */
	uint32_t reserved_18_23               : 6;
	uint32_t rate_lim                     : 2;  /**< These bits limit the incrementing of the error
                                                         rate counter above the failed threshold trigger.
                                                           00 - only count 2 errors above
                                                           01 - only count 4 errors above
                                                           10 - only count 16 error above
                                                           11 - do not limit incrementing the error rate ct */
	uint32_t pk_rate                      : 8;  /**< Peak Value attainted by the error rate counter */
	uint32_t rate_cnt                     : 8;  /**< These bits maintain a count of the number of
                                                         transmission errors that have been detected by the
                                                         port, decremented by the Error Rate Bias
                                                         mechanism, to create an indication of the link
                                                         error rate. */
#else
	uint32_t rate_cnt                     : 8;
	uint32_t pk_rate                      : 8;
	uint32_t rate_lim                     : 2;
	uint32_t reserved_18_23               : 6;
	uint32_t err_bias                     : 8;
#endif
	} s;
	struct cvmx_sriomaintx_erb_err_rate_s cn63xx;
	struct cvmx_sriomaintx_erb_err_rate_s cn63xxp1;
	struct cvmx_sriomaintx_erb_err_rate_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_err_rate cvmx_sriomaintx_erb_err_rate_t;

/**
 * cvmx_sriomaint#_erb_err_rate_en
 *
 * SRIOMAINT_ERB_ERR_RATE_EN = SRIO Error Rate Enable
 *
 * Error Rate Enable
 *
 * Notes:
 * This register contains the bits that control when an error condition is allowed to increment the
 *  error rate counter in the Error Rate Threshold Register and lock the Error Capture registers.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_ERR_RATE_EN       hclk    hrst_n
 */
union cvmx_sriomaintx_erb_err_rate_en {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_err_rate_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t imp_err                      : 1;  /**< Enable Implementation Specific Error. */
	uint32_t reserved_23_30               : 8;
	uint32_t ctl_crc                      : 1;  /**< Enable error rate counting of control symbols with
                                                         bad CRC values */
	uint32_t uns_id                       : 1;  /**< Enable error rate counting of acknowledge control
                                                         symbol with unexpected ackIDs
                                                         (packet-accepted or packet_retry) */
	uint32_t nack                         : 1;  /**< Enable error rate counting of packet-not-accepted
                                                         acknowledge control symbols. */
	uint32_t out_ack                      : 1;  /**< Enable error rate counting of received packet with
                                                         unexpected ackID value */
	uint32_t pkt_crc                      : 1;  /**< Enable error rate counting of received a packet
                                                         with a bad CRC value */
	uint32_t size                         : 1;  /**< Enable error rate counting of received packet
                                                         which exceeds the maximum size of 276 bytes. */
	uint32_t inv_char                     : 1;  /**< Enable error rate counting of received illegal
                                                         illegal, 8B/10B error or undefined codegroup
                                                         within a packet. */
	uint32_t inv_data                     : 1;  /**< Enable error rate counting of received data
                                                         codegroup or 8B/10B error within IDLE sequence. */
	uint32_t reserved_6_14                : 9;
	uint32_t bad_ack                      : 1;  /**< Enable error rate counting of link_responses with
                                                         an ackID that is not outstanding. */
	uint32_t proterr                      : 1;  /**< Enable error rate counting of unexpected packet or
                                                         control symbols received. */
	uint32_t f_toggle                     : 1;  /**< Reserved. */
	uint32_t del_err                      : 1;  /**< Enable error rate counting of illegal or undefined
                                                         codegroups (either INV_DATA or INV_CHAR). */
	uint32_t uns_ack                      : 1;  /**< Enable error rate counting of unexpected
                                                         acknowledge control symbols received. */
	uint32_t lnk_tout                     : 1;  /**< Enable error rate counting of acknowledge or
                                                         link-response control symbols not received within
                                                         the specified timeout interval */
#else
	uint32_t lnk_tout                     : 1;
	uint32_t uns_ack                      : 1;
	uint32_t del_err                      : 1;
	uint32_t f_toggle                     : 1;
	uint32_t proterr                      : 1;
	uint32_t bad_ack                      : 1;
	uint32_t reserved_6_14                : 9;
	uint32_t inv_data                     : 1;
	uint32_t inv_char                     : 1;
	uint32_t size                         : 1;
	uint32_t pkt_crc                      : 1;
	uint32_t out_ack                      : 1;
	uint32_t nack                         : 1;
	uint32_t uns_id                       : 1;
	uint32_t ctl_crc                      : 1;
	uint32_t reserved_23_30               : 8;
	uint32_t imp_err                      : 1;
#endif
	} s;
	struct cvmx_sriomaintx_erb_err_rate_en_s cn63xx;
	struct cvmx_sriomaintx_erb_err_rate_en_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t ctl_crc                      : 1;  /**< Enable error rate counting of control symbols with
                                                         bad CRC values */
	uint32_t uns_id                       : 1;  /**< Enable error rate counting of acknowledge control
                                                         symbol with unexpected ackIDs
                                                         (packet-accepted or packet_retry) */
	uint32_t nack                         : 1;  /**< Enable error rate counting of packet-not-accepted
                                                         acknowledge control symbols. */
	uint32_t out_ack                      : 1;  /**< Enable error rate counting of received packet with
                                                         unexpected ackID value */
	uint32_t pkt_crc                      : 1;  /**< Enable error rate counting of received a packet
                                                         with a bad CRC value */
	uint32_t size                         : 1;  /**< Enable error rate counting of received packet
                                                         which exceeds the maximum size of 276 bytes. */
	uint32_t reserved_6_16                : 11;
	uint32_t bad_ack                      : 1;  /**< Enable error rate counting of link_responses with
                                                         an ackID that is not outstanding. */
	uint32_t proterr                      : 1;  /**< Enable error rate counting of unexpected packet or
                                                         control symbols received. */
	uint32_t f_toggle                     : 1;  /**< Reserved. */
	uint32_t del_err                      : 1;  /**< Enable error rate counting of illegal or undefined
                                                         codegroups (either INV_DATA or INV_CHAR). (Pass 2) */
	uint32_t uns_ack                      : 1;  /**< Enable error rate counting of unexpected
                                                         acknowledge control symbols received. */
	uint32_t lnk_tout                     : 1;  /**< Enable error rate counting of acknowledge or
                                                         link-response control symbols not received within
                                                         the specified timeout interval */
#else
	uint32_t lnk_tout                     : 1;
	uint32_t uns_ack                      : 1;
	uint32_t del_err                      : 1;
	uint32_t f_toggle                     : 1;
	uint32_t proterr                      : 1;
	uint32_t bad_ack                      : 1;
	uint32_t reserved_6_16                : 11;
	uint32_t size                         : 1;
	uint32_t pkt_crc                      : 1;
	uint32_t out_ack                      : 1;
	uint32_t nack                         : 1;
	uint32_t uns_id                       : 1;
	uint32_t ctl_crc                      : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} cn63xxp1;
	struct cvmx_sriomaintx_erb_err_rate_en_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_err_rate_en cvmx_sriomaintx_erb_err_rate_en_t;

/**
 * cvmx_sriomaint#_erb_err_rate_thr
 *
 * SRIOMAINT_ERB_ERR_RATE_THR = SRIO Error Rate Threshold
 *
 * Error Rate Threshold
 *
 * Notes:
 * The Error Rate Threshold register is used to control the reporting of errors to the link status.
 *  Typically the Degraded Threshold is less than the Fail Threshold.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_ERR_RATE_THR      hclk    hrst_n
 */
union cvmx_sriomaintx_erb_err_rate_thr {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_err_rate_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t fail_th                      : 8;  /**< These bits provide the threshold value for
                                                         reporting an error condition due to a possibly
                                                         broken link.
                                                           0x00 - Disable the Error Rate Failed Threshold
                                                                  Trigger
                                                           0x01 - Set the error reporting threshold to 1
                                                           0x02 - Set the error reporting threshold to 2
                                                           - ...
                                                           0xFF - Set the error reporting threshold to 255 */
	uint32_t dgrad_th                     : 8;  /**< These bits provide the threshold value for
                                                         reporting an error condition due to a possibly
                                                         degrading link.
                                                           0x00 - Disable the Degrade Rate Failed Threshold
                                                                  Trigger
                                                           0x01 - Set the error reporting threshold to 1
                                                           0x02 - Set the error reporting threshold to 2
                                                           - ...
                                                           0xFF - Set the error reporting threshold to 255 */
	uint32_t reserved_0_15                : 16;
#else
	uint32_t reserved_0_15                : 16;
	uint32_t dgrad_th                     : 8;
	uint32_t fail_th                      : 8;
#endif
	} s;
	struct cvmx_sriomaintx_erb_err_rate_thr_s cn63xx;
	struct cvmx_sriomaintx_erb_err_rate_thr_s cn63xxp1;
	struct cvmx_sriomaintx_erb_err_rate_thr_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_err_rate_thr cvmx_sriomaintx_erb_err_rate_thr_t;

/**
 * cvmx_sriomaint#_erb_hdr
 *
 * SRIOMAINT_ERB_HDR = SRIO Error Reporting Block Header
 *
 * Error Reporting Block Header
 *
 * Notes:
 * The error management extensions block header register contains the EF_PTR to the next EF_BLK and
 *  the EF_ID that identifies this as the error management extensions block header. In this
 *  implementation this is the last block and therefore the EF_PTR is a NULL pointer.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_HDR       hclk    hrst_n
 */
union cvmx_sriomaintx_erb_hdr {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_hdr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ef_ptr                       : 16; /**< Pointer to the next block in the extended features
                                                         data structure. */
	uint32_t ef_id                        : 16; /**< Single Port ID */
#else
	uint32_t ef_id                        : 16;
	uint32_t ef_ptr                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_erb_hdr_s      cn63xx;
	struct cvmx_sriomaintx_erb_hdr_s      cn63xxp1;
	struct cvmx_sriomaintx_erb_hdr_s      cn66xx;
};
typedef union cvmx_sriomaintx_erb_hdr cvmx_sriomaintx_erb_hdr_t;

/**
 * cvmx_sriomaint#_erb_lt_addr_capt_h
 *
 * SRIOMAINT_ERB_LT_ADDR_CAPT_H = SRIO Logical/Transport Layer High Address Capture
 *
 * Logical/Transport Layer High Address Capture
 *
 * Notes:
 * This register contains error information. It is locked when a Logical/Transport error is detected
 *  and unlocked when the SRIOMAINT(0,2..3)_ERB_LT_ERR_DET is written to zero. This register should be
 *  written only when error detection is disabled.  This register is only required for end point
 *  transactions of 50 or 66 bits.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_LT_ADDR_CAPT_H    hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_addr_capt_h {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_addr_capt_h_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr                         : 32; /**< Most significant 32 bits of the address associated
                                                         with the error. Information supplied for requests
                                                         and responses if available. */
#else
	uint32_t addr                         : 32;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_addr_capt_h_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_addr_capt_h_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_addr_capt_h_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_addr_capt_h cvmx_sriomaintx_erb_lt_addr_capt_h_t;

/**
 * cvmx_sriomaint#_erb_lt_addr_capt_l
 *
 * SRIOMAINT_ERB_LT_ADDR_CAPT_L = SRIO Logical/Transport Layer Low Address Capture
 *
 * Logical/Transport Layer Low Address Capture
 *
 * Notes:
 * This register contains error information. It is locked when a Logical/Transport error is detected
 *  and unlocked when the SRIOMAINT(0,2..3)_ERB_LT_ERR_DET is written to zero.  This register should be
 *  written only when error detection is disabled.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_LT_ADDR_CAPT_L    hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_addr_capt_l {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_addr_capt_l_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr                         : 29; /**< Least significant 29 bits of the address
                                                         associated with the error.  Bits 31:24 specify the
                                                         request HOP count for Maintenance Operations.
                                                         Information supplied for requests and responses if
                                                         available. */
	uint32_t reserved_2_2                 : 1;
	uint32_t xaddr                        : 2;  /**< Extended address bits of the address associated
                                                         with the error.  Information supplied for requests
                                                         and responses if available. */
#else
	uint32_t xaddr                        : 2;
	uint32_t reserved_2_2                 : 1;
	uint32_t addr                         : 29;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_addr_capt_l_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_addr_capt_l_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_addr_capt_l_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_addr_capt_l cvmx_sriomaintx_erb_lt_addr_capt_l_t;

/**
 * cvmx_sriomaint#_erb_lt_ctrl_capt
 *
 * SRIOMAINT_ERB_LT_CTRL_CAPT = SRIO Logical/Transport Layer Control Capture
 *
 * Logical/Transport Layer Control Capture
 *
 * Notes:
 * This register contains error information. It is locked when a Logical/Transport error is detected
 *  and unlocked when the SRIOMAINT(0,2..3)_ERB_LT_ERR_DET is written to zero.  This register should be
 *  written only when error detection is disabled.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_LT_CTRL_CAPT      hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_ctrl_capt {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_ctrl_capt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ftype                        : 4;  /**< Format Type associated with the error */
	uint32_t ttype                        : 4;  /**< Transaction Type associated with the error
                                                         (For Messages)
                                                         Message Length */
	uint32_t extra                        : 8;  /**< Additional Information
                                                         (For Messages)
                                                         - 23:22 Letter
                                                         - 21:20 Mbox
                                                         - 19:16 Msgseg/xmbox
                                                         Information for the last message request sent
                                                         for the mailbox that had an error
                                                         (For Responses)
                                                         - 23:20 Response Request FTYPE
                                                         - 19:16 Response Request TTYPE
                                                         (For all other types)
                                                         Reserved. */
	uint32_t status                       : 4;  /**< Response Status.
                                                         (For all other Requests)
                                                         Reserved. */
	uint32_t size                         : 4;  /**< Size associated with the transaction. */
	uint32_t tt                           : 1;  /**< Transfer Type 0=ID8, 1=ID16. */
	uint32_t wdptr                        : 1;  /**< Word Pointer associated with the error. */
	uint32_t reserved_5_5                 : 1;
	uint32_t capt_idx                     : 5;  /**< Capture Index. 31 - Bit set in
                                                         SRIOMAINT(0,2..3)_ERB_LT_ERR_DET. */
#else
	uint32_t capt_idx                     : 5;
	uint32_t reserved_5_5                 : 1;
	uint32_t wdptr                        : 1;
	uint32_t tt                           : 1;
	uint32_t size                         : 4;
	uint32_t status                       : 4;
	uint32_t extra                        : 8;
	uint32_t ttype                        : 4;
	uint32_t ftype                        : 4;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_ctrl_capt_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_ctrl_capt_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_ctrl_capt_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_ctrl_capt cvmx_sriomaintx_erb_lt_ctrl_capt_t;

/**
 * cvmx_sriomaint#_erb_lt_dev_id
 *
 * SRIOMAINT_ERB_LT_DEV_ID = SRIO Port-write Target deviceID
 *
 * Port-write Target deviceID
 *
 * Notes:
 * This SRIO interface does not support generating Port-Writes based on ERB Errors.  This register is
 *  currently unused and should be treated as reserved.
 *
 * Clk_Rst:        SRIOMAINT_ERB_LT_DEV_ID hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_dev_id {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_dev_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t id16                         : 8;  /**< This is the most significant byte of the
                                                         port-write destination deviceID (large transport
                                                         systems only)
                                                         destination ID used for Port Write errors */
	uint32_t id8                          : 8;  /**< This is the port-write destination deviceID */
	uint32_t tt                           : 1;  /**< Transport Type used for Port Write
                                                         0 = Small Transport, ID8 Only
                                                         1 = Large Transport, ID16 and ID8 */
	uint32_t reserved_0_14                : 15;
#else
	uint32_t reserved_0_14                : 15;
	uint32_t tt                           : 1;
	uint32_t id8                          : 8;
	uint32_t id16                         : 8;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_dev_id_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_dev_id_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_dev_id_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_dev_id cvmx_sriomaintx_erb_lt_dev_id_t;

/**
 * cvmx_sriomaint#_erb_lt_dev_id_capt
 *
 * SRIOMAINT_ERB_LT_DEV_ID_CAPT = SRIO Logical/Transport Layer Device ID Capture
 *
 * Logical/Transport Layer Device ID Capture
 *
 * Notes:
 * This register contains error information. It is locked when a Logical/Transport error is detected
 *  and unlocked when the SRIOMAINT(0,2..3)_ERB_LT_ERR_DET is written to zero.  This register should be
 *  written only when error detection is disabled.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_LT_DEV_ID_CAPT    hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_dev_id_capt {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_dev_id_capt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dst_id16                     : 8;  /**< Most significant byte of the large transport
                                                         destination ID associated with the error */
	uint32_t dst_id8                      : 8;  /**< Least significant byte of the large transport
                                                         destination ID or the 8-bit small transport
                                                         destination ID associated with the error */
	uint32_t src_id16                     : 8;  /**< Most significant byte of the large transport
                                                         source ID associated with the error */
	uint32_t src_id8                      : 8;  /**< Least significant byte of the large transport
                                                         source ID or the 8-bit small transport source ID
                                                         associated with the error */
#else
	uint32_t src_id8                      : 8;
	uint32_t src_id16                     : 8;
	uint32_t dst_id8                      : 8;
	uint32_t dst_id16                     : 8;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_dev_id_capt_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_dev_id_capt_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_dev_id_capt_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_dev_id_capt cvmx_sriomaintx_erb_lt_dev_id_capt_t;

/**
 * cvmx_sriomaint#_erb_lt_err_det
 *
 * SRIOMAINT_ERB_LT_ERR_DET = SRIO Logical/Transport Layer Error Detect
 *
 * SRIO Logical/Transport Layer Error Detect
 *
 * Notes:
 * This register indicates the error that was detected by the Logical or Transport logic layer.
 *  Once a bit is set in this CSR, HW will lock the register until SW writes a zero to clear all the
 *  fields.  The HW sets SRIO_INT_REG[LOG_ERB] every time it sets one of the bits.
 *  To handle the interrupt, the following procedure may be best:
 *       (1) clear SRIO_INT_REG[LOG_ERB],
 *       (2) read this CSR, corresponding SRIOMAINT*_ERB_LT_ADDR_CAPT_H, SRIOMAINT*_ERB_LT_ADDR_CAPT_L,
 *           SRIOMAINT*_ERB_LT_DEV_ID_CAPT, and SRIOMAINT*_ERB_LT_CTRL_CAPT
 *       (3) Write this CSR to 0.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_LT_ERR_DET        hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_err_det {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_err_det_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t io_err                       : 1;  /**< Received a response of ERROR for an IO Logical
                                                         Layer Request.  This includes all Maintenance and
                                                         Memory Responses not destined for the RX Soft
                                                         Packet FIFO. When SRIO receives an ERROR response
                                                         for a read, the issuing core or DPI DMA engine
                                                         receives result bytes with all bits set. In the
                                                         case of writes with response, this bit is the only
                                                         indication of failure. */
	uint32_t msg_err                      : 1;  /**< Received a response of ERROR for an outgoing
                                                         message segment. This bit is the only direct
                                                         indication of a MSG_ERR. When a MSG_ERR occurs,
                                                         SRIO drops the message segment and will not set
                                                         SRIO*_INT_REG[OMSG*] after the message
                                                         "transfer". NOTE: SRIO can continue to send or
                                                         retry other segments from the same message after
                                                         a MSG_ERR. */
	uint32_t gsm_err                      : 1;  /**< Received a response of ERROR for an GSM Logical
                                                         Request.  SRIO hardware never sets this bit. GSM
                                                         operations are not supported (outside of the Soft
                                                         Packet FIFO). */
	uint32_t msg_fmt                      : 1;  /**< Received an incoming Message Segment with a
                                                         formating error.  A MSG_FMT error occurs when SRIO
                                                         receives a message segment with a reserved SSIZE,
                                                         or a illegal data payload size, or a MSGSEG greater
                                                         than MSGLEN, or a MSGSEG that is the duplicate of
                                                         one already received by an inflight message.
                                                         When a non-duplicate MSG_FMT error occurs, SRIO
                                                         drops the segment and sends an ERROR response.
                                                         When a duplicate MSG_FMT error occurs, SRIO
                                                         (internally) terminates the currently-inflight
                                                         message with an error and processes the duplicate,
                                                         which may result in a new message being generated
                                                         internally for the duplicate. */
	uint32_t ill_tran                     : 1;  /**< Received illegal fields in the request/response
                                                         packet for a supported transaction or any packet
                                                         with a reserved transaction type. When an ILL_TRAN
                                                         error occurs, SRIO ignores the packet. ILL_TRAN
                                                         errors are 2nd priority after ILL_TGT and may mask
                                                         other problems. Packets with ILL_TRAN errors cannot
                                                         enter the RX Soft Packet FIFO.
                                                         There are two things that can set ILL_TRAN:
                                                         (1) SRIO received a packet with a tt value is not
                                                         0 or 1, or (2) SRIO received a response to an
                                                         outstanding message segment whose status was not
                                                         DONE, RETRY, or ERROR. */
	uint32_t ill_tgt                      : 1;  /**< Received a packet that contained a destination ID
                                                         other than SRIOMAINT*_PRI_DEV_ID or
                                                         SRIOMAINT*_SEC_DEV_ID. When an ILL_TGT error
                                                         occurs, SRIO drops the packet. ILL_TGT errors are
                                                         highest priority, so may mask other problems.
                                                         Packets with ILL_TGT errors cannot enter the RX
                                                         soft packet fifo. */
	uint32_t msg_tout                     : 1;  /**< An expected incoming message request has not been
                                                         received within the time-out interval specified in
                                                         SRIOMAINT(0,2..3)_PORT_RT_CTL. When a MSG_TOUT occurs,
                                                         SRIO (internally) terminates the inflight message
                                                         with an error. */
	uint32_t pkt_tout                     : 1;  /**< A required response has not been received to an
                                                         outgoing memory, maintenance or message request
                                                         before the time-out interval specified in
                                                         SRIOMAINT(0,2..3)_PORT_RT_CTL.  When an IO or maintenance
                                                         read request operation has a PKT_TOUT, the issuing
                                                         core load or DPI DMA engine receive all ones for
                                                         the result. When an IO NWRITE_R has a PKT_TOUT,
                                                         this bit is the only indication of failure. When a
                                                         message request operation has a PKT_TOUT, SRIO
                                                         discards the the outgoing message segment,  and
                                                         this bit is the only direct indication of failure.
                                                         NOTE: SRIO may continue to send or retry other
                                                         segments from the same message. When one or more of
                                                         the segments in an outgoing message have a
                                                         PKT_TOUT, SRIO will not set SRIO*_INT_REG[OMSG*]
                                                         after the message "transfer". */
	uint32_t uns_resp                     : 1;  /**< An unsolicited/unexpected memory, maintenance or
                                                         message response packet was received that was not
                                                         destined for the RX Soft Packet FIFO.  When this
                                                         condition is detected, the packet is dropped. */
	uint32_t uns_tran                     : 1;  /**< A transaction is received that is not supported.
                                                         SRIO HW will never set this bit - SRIO routes all
                                                         unsupported transactions to the RX soft packet
                                                         FIFO. */
	uint32_t reserved_1_21                : 21;
	uint32_t resp_sz                      : 1;  /**< Received an incoming Memory or Maintenance
                                                         Read response packet with a DONE status and less
                                                         data then expected.  This condition causes the
                                                         Read to be completed and an error response to be
                                                         returned with all the data bits set to the issuing
                                                         Core or DMA Engine. */
#else
	uint32_t resp_sz                      : 1;
	uint32_t reserved_1_21                : 21;
	uint32_t uns_tran                     : 1;
	uint32_t uns_resp                     : 1;
	uint32_t pkt_tout                     : 1;
	uint32_t msg_tout                     : 1;
	uint32_t ill_tgt                      : 1;
	uint32_t ill_tran                     : 1;
	uint32_t msg_fmt                      : 1;
	uint32_t gsm_err                      : 1;
	uint32_t msg_err                      : 1;
	uint32_t io_err                       : 1;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_err_det_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_err_det_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_err_det_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_err_det cvmx_sriomaintx_erb_lt_err_det_t;

/**
 * cvmx_sriomaint#_erb_lt_err_en
 *
 * SRIOMAINT_ERB_LT_ERR_EN = SRIO Logical/Transport Layer Error Enable
 *
 * SRIO Logical/Transport Layer Error Enable
 *
 * Notes:
 * This register contains the bits that control if an error condition locks the Logical/Transport
 *  Layer Error Detect and Capture registers and is reported to the system host.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_LT_ERR_EN hclk    hrst_n
 */
union cvmx_sriomaintx_erb_lt_err_en {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_lt_err_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t io_err                       : 1;  /**< Enable reporting of an IO error response. Save and
                                                         lock original request transaction information in
                                                         all Logical/Transport Layer Capture CSRs. */
	uint32_t msg_err                      : 1;  /**< Enable reporting of a Message error response. Save
                                                         and lock original request transaction information
                                                         in all Logical/Transport Layer Capture CSRs. */
	uint32_t gsm_err                      : 1;  /**< Enable reporting of a GSM error response. Save and
                                                         lock original request transaction capture
                                                         information in all Logical/Transport Layer Capture
                                                         CSRs. */
	uint32_t msg_fmt                      : 1;  /**< Enable reporting of a message format error. Save
                                                         and lock transaction capture information in
                                                         Logical/Transport Layer Device ID and Control
                                                         Capture CSRs. */
	uint32_t ill_tran                     : 1;  /**< Enable reporting of an illegal transaction decode
                                                         error Save and lock transaction capture
                                                         information in Logical/Transport Layer Device ID
                                                         and Control Capture CSRs. */
	uint32_t ill_tgt                      : 1;  /**< Enable reporting of an illegal transaction target
                                                         error. Save and lock transaction capture
                                                         information in Logical/Transport Layer Device ID
                                                         and Control Capture CSRs. */
	uint32_t msg_tout                     : 1;  /**< Enable reporting of a Message Request time-out
                                                         error. Save and lock transaction capture
                                                         information in Logical/Transport Layer Device ID
                                                         and Control Capture CSRs for the last Message
                                                         request segment packet received. */
	uint32_t pkt_tout                     : 1;  /**< Enable reporting of a packet response time-out
                                                         error.  Save and lock original request address in
                                                         Logical/Transport Layer Address Capture CSRs.
                                                         Save and lock original request Destination ID in
                                                         Logical/Transport Layer Device ID Capture CSR. */
	uint32_t uns_resp                     : 1;  /**< Enable reporting of an unsolicited response error.
                                                         Save and lock transaction capture information in
                                                         Logical/Transport Layer Device ID and Control
                                                         Capture CSRs. */
	uint32_t uns_tran                     : 1;  /**< Enable reporting of an unsupported transaction
                                                         error.  Save and lock transaction capture
                                                         information in Logical/Transport Layer Device ID
                                                         and Control Capture CSRs. */
	uint32_t reserved_1_21                : 21;
	uint32_t resp_sz                      : 1;  /**< Enable reporting of an incoming response with
                                                         unexpected data size */
#else
	uint32_t resp_sz                      : 1;
	uint32_t reserved_1_21                : 21;
	uint32_t uns_tran                     : 1;
	uint32_t uns_resp                     : 1;
	uint32_t pkt_tout                     : 1;
	uint32_t msg_tout                     : 1;
	uint32_t ill_tgt                      : 1;
	uint32_t ill_tran                     : 1;
	uint32_t msg_fmt                      : 1;
	uint32_t gsm_err                      : 1;
	uint32_t msg_err                      : 1;
	uint32_t io_err                       : 1;
#endif
	} s;
	struct cvmx_sriomaintx_erb_lt_err_en_s cn63xx;
	struct cvmx_sriomaintx_erb_lt_err_en_s cn63xxp1;
	struct cvmx_sriomaintx_erb_lt_err_en_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_lt_err_en cvmx_sriomaintx_erb_lt_err_en_t;

/**
 * cvmx_sriomaint#_erb_pack_capt_1
 *
 * SRIOMAINT_ERB_PACK_CAPT_1 = SRIO Packet Capture 1
 *
 * Packet Capture 1
 *
 * Notes:
 * Error capture register 1 contains either long symbol capture information or bytes 4 through 7 of
 *  the packet header.
 *  The HW will not update this register (i.e. this register is locked) while
 *  SRIOMAINT*_ERB_ATTR_CAPT[VALID] is set.  This register should only be read while this bit is set.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_PACK_CAPT_1       hclk    hrst_n
 */
union cvmx_sriomaintx_erb_pack_capt_1 {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_pack_capt_1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t capture                      : 32; /**< Bytes 4 thru 7 of the packet header. */
#else
	uint32_t capture                      : 32;
#endif
	} s;
	struct cvmx_sriomaintx_erb_pack_capt_1_s cn63xx;
	struct cvmx_sriomaintx_erb_pack_capt_1_s cn63xxp1;
	struct cvmx_sriomaintx_erb_pack_capt_1_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_pack_capt_1 cvmx_sriomaintx_erb_pack_capt_1_t;

/**
 * cvmx_sriomaint#_erb_pack_capt_2
 *
 * SRIOMAINT_ERB_PACK_CAPT_2 = SRIO Packet Capture 2
 *
 * Packet Capture 2
 *
 * Notes:
 * Error capture register 2 contains bytes 8 through 11 of the packet header.
 *  The HW will not update this register (i.e. this register is locked) while
 *  SRIOMAINT*_ERB_ATTR_CAPT[VALID] is set.  This register should only be read while this bit is set.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_PACK_CAPT_2       hclk    hrst_n
 */
union cvmx_sriomaintx_erb_pack_capt_2 {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_pack_capt_2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t capture                      : 32; /**< Bytes 8 thru 11 of the packet header. */
#else
	uint32_t capture                      : 32;
#endif
	} s;
	struct cvmx_sriomaintx_erb_pack_capt_2_s cn63xx;
	struct cvmx_sriomaintx_erb_pack_capt_2_s cn63xxp1;
	struct cvmx_sriomaintx_erb_pack_capt_2_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_pack_capt_2 cvmx_sriomaintx_erb_pack_capt_2_t;

/**
 * cvmx_sriomaint#_erb_pack_capt_3
 *
 * SRIOMAINT_ERB_PACK_CAPT_3 = SRIO Packet Capture 3
 *
 * Packet Capture 3
 *
 * Notes:
 * Error capture register 3 contains bytes 12 through 15 of the packet header.
 *  The HW will not update this register (i.e. this register is locked) while
 *  SRIOMAINT*_ERB_ATTR_CAPT[VALID] is set.  This register should only be read while this bit is set.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_PACK_CAPT_3       hclk    hrst_n
 */
union cvmx_sriomaintx_erb_pack_capt_3 {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_pack_capt_3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t capture                      : 32; /**< Bytes 12 thru 15 of the packet header. */
#else
	uint32_t capture                      : 32;
#endif
	} s;
	struct cvmx_sriomaintx_erb_pack_capt_3_s cn63xx;
	struct cvmx_sriomaintx_erb_pack_capt_3_s cn63xxp1;
	struct cvmx_sriomaintx_erb_pack_capt_3_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_pack_capt_3 cvmx_sriomaintx_erb_pack_capt_3_t;

/**
 * cvmx_sriomaint#_erb_pack_sym_capt
 *
 * SRIOMAINT_ERB_PACK_SYM_CAPT = SRIO Packet/Control Symbol Capture
 *
 * Packet/Control Symbol Capture
 *
 * Notes:
 * This register contains either captured control symbol information or the first 4 bytes of captured
 *  packet information.  The Errors that generate Partial Control Symbols can be found in
 *  SRIOMAINT*_ERB_ERR_DET.  The HW will not update this register (i.e. this register is locked) while
 *  SRIOMAINT*_ERB_ATTR_CAPT[VALID] is set.  This register should only be read while this bit is set.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_ERB_PACK_SYM_CAPT     hclk    hrst_n
 */
union cvmx_sriomaintx_erb_pack_sym_capt {
	uint32_t u32;
	struct cvmx_sriomaintx_erb_pack_sym_capt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t capture                      : 32; /**< Control Character and Control Symbol or Bytes 0 to
                                                         3 of Packet Header
                                                         The Control Symbol consists of
                                                           - 31:24 - SC Character (0 in Partial Symbol)
                                                           - 23:21 - Stype 0
                                                           - 20:16 - Parameter 0
                                                           - 15:11 - Parameter 1
                                                           - 10: 8 - Stype 1 (0 in Partial Symbol)
                                                           - 7: 5 - Command (0 in Partial Symbol)
                                                           - 4: 0 - CRC5    (0 in Partial Symbol) */
#else
	uint32_t capture                      : 32;
#endif
	} s;
	struct cvmx_sriomaintx_erb_pack_sym_capt_s cn63xx;
	struct cvmx_sriomaintx_erb_pack_sym_capt_s cn63xxp1;
	struct cvmx_sriomaintx_erb_pack_sym_capt_s cn66xx;
};
typedef union cvmx_sriomaintx_erb_pack_sym_capt cvmx_sriomaintx_erb_pack_sym_capt_t;

/**
 * cvmx_sriomaint#_hb_dev_id_lock
 *
 * SRIOMAINT_HB_DEV_ID_LOCK = SRIO Host Device ID Lock
 *
 * The Host Base Device ID
 *
 * Notes:
 * This register contains the Device ID of the Host responsible for initializing this SRIO device.
 *  The register contains a special write once function that captures the first HOSTID written to it
 *  after reset.  The function allows several potential hosts to write to this register and then read
 *  it to see if they have responsibility for initialization.  The register can be unlocked by
 *  rewriting the current host value.  This will reset the lock and restore the value to 0xFFFF.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_HB_DEV_ID_LOCK        hclk    hrst_n
 */
union cvmx_sriomaintx_hb_dev_id_lock {
	uint32_t u32;
	struct cvmx_sriomaintx_hb_dev_id_lock_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t hostid                       : 16; /**< Primary 16-bit Device ID */
#else
	uint32_t hostid                       : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_sriomaintx_hb_dev_id_lock_s cn63xx;
	struct cvmx_sriomaintx_hb_dev_id_lock_s cn63xxp1;
	struct cvmx_sriomaintx_hb_dev_id_lock_s cn66xx;
};
typedef union cvmx_sriomaintx_hb_dev_id_lock cvmx_sriomaintx_hb_dev_id_lock_t;

/**
 * cvmx_sriomaint#_ir_buffer_config
 *
 * SRIOMAINT_IR_BUFFER_CONFIG = SRIO Buffer Configuration
 *
 * Buffer Configuration
 *
 * Notes:
 * This register controls the operation of the SRIO Core buffer mux logic.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG      hclk    hrst_n
 */
union cvmx_sriomaintx_ir_buffer_config {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_buffer_config_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t tx_wm0                       : 4;  /**< Reserved. (See SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2) */
	uint32_t tx_wm1                       : 4;  /**< Reserved. (See SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2) */
	uint32_t tx_wm2                       : 4;  /**< Reserved. (See SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2) */
	uint32_t reserved_3_19                : 17;
	uint32_t tx_flow                      : 1;  /**< Controls whether Transmitter Flow Control is
                                                         permitted on this device.
                                                           0 - Disabled
                                                           1 - Permitted
                                                         The reset value of this field is
                                                         SRIO*_IP_FEATURE[TX_FLOW]. */
	uint32_t tx_sync                      : 1;  /**< Reserved. */
	uint32_t rx_sync                      : 1;  /**< Reserved. */
#else
	uint32_t rx_sync                      : 1;
	uint32_t tx_sync                      : 1;
	uint32_t tx_flow                      : 1;
	uint32_t reserved_3_19                : 17;
	uint32_t tx_wm2                       : 4;
	uint32_t tx_wm1                       : 4;
	uint32_t tx_wm0                       : 4;
#endif
	} s;
	struct cvmx_sriomaintx_ir_buffer_config_s cn63xx;
	struct cvmx_sriomaintx_ir_buffer_config_s cn63xxp1;
	struct cvmx_sriomaintx_ir_buffer_config_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_buffer_config cvmx_sriomaintx_ir_buffer_config_t;

/**
 * cvmx_sriomaint#_ir_buffer_config2
 *
 * SRIOMAINT_IR_BUFFER_CONFIG2 = SRIO Buffer Configuration 2
 *
 * Buffer Configuration 2
 *
 * Notes:
 * This register controls the RX and TX Buffer availablility by priority.  The typical values are
 *  optimized for normal operation.  Care must be taken when changing these values to avoid values
 *  which can result in deadlocks.  Disabling a priority is not recommended and can result in system
 *  level failures.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2     hclk    hrst_n
 */
union cvmx_sriomaintx_ir_buffer_config2 {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_buffer_config2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t tx_wm3                       : 4;  /**< Number of buffers free before a priority 3 packet
                                                         will be transmitted.  A value of 9 will disable
                                                         this priority. */
	uint32_t tx_wm2                       : 4;  /**< Number of buffers free before a priority 2 packet
                                                         will be transmitted.  A value of 9 will disable
                                                         this priority. */
	uint32_t tx_wm1                       : 4;  /**< Number of buffers free before a priority 1 packet
                                                         will be transmitted.  A value of 9 will disable
                                                         this priority. */
	uint32_t tx_wm0                       : 4;  /**< Number of buffers free before a priority 0 packet
                                                         will be transmitted.  A value of 9 will disable
                                                         this priority. */
	uint32_t rx_wm3                       : 4;  /**< Number of buffers free before a priority 3 packet
                                                         will be accepted.  A value of 9 will disable this
                                                         priority and always cause a physical layer RETRY. */
	uint32_t rx_wm2                       : 4;  /**< Number of buffers free before a priority 2 packet
                                                         will be accepted.  A value of 9 will disable this
                                                         priority and always cause a physical layer RETRY. */
	uint32_t rx_wm1                       : 4;  /**< Number of buffers free before a priority 1 packet
                                                         will be accepted.  A value of 9 will disable this
                                                         priority and always cause a physical layer RETRY. */
	uint32_t rx_wm0                       : 4;  /**< Number of buffers free before a priority 0 packet
                                                         will be accepted.  A value of 9 will disable this
                                                         priority and always cause a physical layer RETRY. */
#else
	uint32_t rx_wm0                       : 4;
	uint32_t rx_wm1                       : 4;
	uint32_t rx_wm2                       : 4;
	uint32_t rx_wm3                       : 4;
	uint32_t tx_wm0                       : 4;
	uint32_t tx_wm1                       : 4;
	uint32_t tx_wm2                       : 4;
	uint32_t tx_wm3                       : 4;
#endif
	} s;
	struct cvmx_sriomaintx_ir_buffer_config2_s cn63xx;
	struct cvmx_sriomaintx_ir_buffer_config2_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_buffer_config2 cvmx_sriomaintx_ir_buffer_config2_t;

/**
 * cvmx_sriomaint#_ir_pd_phy_ctrl
 *
 * SRIOMAINT_IR_PD_PHY_CTRL = SRIO Platform Dependent PHY Control
 *
 * Platform Dependent PHY Control
 *
 * Notes:
 * This register can be used for testing.  The register is otherwise unused by the hardware.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_PD_PHY_CTRL        hclk    hrst_n
 */
union cvmx_sriomaintx_ir_pd_phy_ctrl {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_pd_phy_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pd_ctrl                      : 32; /**< Unused Register available for testing */
#else
	uint32_t pd_ctrl                      : 32;
#endif
	} s;
	struct cvmx_sriomaintx_ir_pd_phy_ctrl_s cn63xx;
	struct cvmx_sriomaintx_ir_pd_phy_ctrl_s cn63xxp1;
	struct cvmx_sriomaintx_ir_pd_phy_ctrl_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_pd_phy_ctrl cvmx_sriomaintx_ir_pd_phy_ctrl_t;

/**
 * cvmx_sriomaint#_ir_pd_phy_stat
 *
 * SRIOMAINT_IR_PD_PHY_STAT = SRIO Platform Dependent PHY Status
 *
 * Platform Dependent PHY Status
 *
 * Notes:
 * This register is used to monitor PHY status on each lane.  They are documented here to assist in
 *  debugging only.  The lane numbers take into account the lane swap pin.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_PD_PHY_STAT        hclk    hrst_n
 */
union cvmx_sriomaintx_ir_pd_phy_stat {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_pd_phy_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t ln3_rx                       : 3;  /**< Phy Lane 3 RX Status
                                                         0XX = Normal Operation
                                                         100 = 8B/10B Error
                                                         101 = Elastic Buffer Overflow (Data Lost)
                                                         110 = Elastic Buffer Underflow (Data Corrupted)
                                                         111 = Disparity Error */
	uint32_t ln3_dis                      : 1;  /**< Lane 3 Phy Clock Disabled
                                                         0 = Phy Clock Valid
                                                         1 = Phy Clock InValid */
	uint32_t ln2_rx                       : 3;  /**< Phy Lane 2 RX Status
                                                         0XX = Normal Operation
                                                         100 = 8B/10B Error
                                                         101 = Elastic Buffer Overflow (Data Lost)
                                                         110 = Elastic Buffer Underflow (Data Corrupted)
                                                         111 = Disparity Error */
	uint32_t ln2_dis                      : 1;  /**< Lane 2 Phy Clock Disabled
                                                         0 = Phy Clock Valid
                                                         1 = Phy Clock InValid */
	uint32_t ln1_rx                       : 3;  /**< Phy Lane 1 RX Status
                                                         0XX = Normal Operation
                                                         100 = 8B/10B Error
                                                         101 = Elastic Buffer Overflow (Data Lost)
                                                         110 = Elastic Buffer Underflow (Data Corrupted)
                                                         111 = Disparity Error */
	uint32_t ln1_dis                      : 1;  /**< Lane 1 Phy Clock Disabled
                                                         0 = Phy Clock Valid
                                                         1 = Phy Clock InValid */
	uint32_t ln0_rx                       : 3;  /**< Phy Lane 0 RX Status
                                                         0XX = Normal Operation
                                                         100 = 8B/10B Error
                                                         101 = Elastic Buffer Overflow (Data Lost)
                                                         110 = Elastic Buffer Underflow (Data Corrupted)
                                                         111 = Disparity Error */
	uint32_t ln0_dis                      : 1;  /**< Lane 0 Phy Clock Disabled
                                                         0 = Phy Clock Valid
                                                         1 = Phy Clock InValid */
#else
	uint32_t ln0_dis                      : 1;
	uint32_t ln0_rx                       : 3;
	uint32_t ln1_dis                      : 1;
	uint32_t ln1_rx                       : 3;
	uint32_t ln2_dis                      : 1;
	uint32_t ln2_rx                       : 3;
	uint32_t ln3_dis                      : 1;
	uint32_t ln3_rx                       : 3;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_sriomaintx_ir_pd_phy_stat_s cn63xx;
	struct cvmx_sriomaintx_ir_pd_phy_stat_s cn63xxp1;
	struct cvmx_sriomaintx_ir_pd_phy_stat_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_pd_phy_stat cvmx_sriomaintx_ir_pd_phy_stat_t;

/**
 * cvmx_sriomaint#_ir_pi_phy_ctrl
 *
 * SRIOMAINT_IR_PI_PHY_CTRL = SRIO Platform Independent PHY Control
 *
 * Platform Independent PHY Control
 *
 * Notes:
 * This register is used to control platform independent operating modes of the transceivers. These
 *  control bits are uniform across all platforms.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_PI_PHY_CTRL        hclk    hrst_n
 */
union cvmx_sriomaintx_ir_pi_phy_ctrl {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_pi_phy_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t tx_reset                     : 1;  /**< Outgoing PHY Logic Reset.  0=Reset, 1=Normal Op */
	uint32_t rx_reset                     : 1;  /**< Incoming PHY Logic Reset.  0=Reset, 1=Normal Op */
	uint32_t reserved_29_29               : 1;
	uint32_t loopback                     : 2;  /**< These bits control the state of the loopback
                                                         control vector on the transceiver interface.  The
                                                         loopback modes are enumerated as follows:
                                                           00 - No Loopback
                                                           01 - Near End PCS Loopback
                                                           10 - Far End PCS Loopback
                                                           11 - Both Near and Far End PCS Loopback */
	uint32_t reserved_0_26                : 27;
#else
	uint32_t reserved_0_26                : 27;
	uint32_t loopback                     : 2;
	uint32_t reserved_29_29               : 1;
	uint32_t rx_reset                     : 1;
	uint32_t tx_reset                     : 1;
#endif
	} s;
	struct cvmx_sriomaintx_ir_pi_phy_ctrl_s cn63xx;
	struct cvmx_sriomaintx_ir_pi_phy_ctrl_s cn63xxp1;
	struct cvmx_sriomaintx_ir_pi_phy_ctrl_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_pi_phy_ctrl cvmx_sriomaintx_ir_pi_phy_ctrl_t;

/**
 * cvmx_sriomaint#_ir_pi_phy_stat
 *
 * SRIOMAINT_IR_PI_PHY_STAT = SRIO Platform Independent PHY Status
 *
 * Platform Independent PHY Status
 *
 * Notes:
 * This register displays the status of the link initialization state machine.  Changes to this state
 *  cause the SRIO(0,2..3)_INT_REG.LINK_UP or SRIO(0,2..3)_INT_REG.LINK_DOWN interrupts.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_PI_PHY_STAT        hclk    hrst_n
 */
union cvmx_sriomaintx_ir_pi_phy_stat {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_pi_phy_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_12_31               : 20;
	uint32_t tx_rdy                       : 1;  /**< Minimum number of Status Transmitted */
	uint32_t rx_rdy                       : 1;  /**< Minimum number of Good Status Received */
	uint32_t init_sm                      : 10; /**< Initialization State Machine
                                                         001 - Silent
                                                         002 - Seek
                                                         004 - Discovery
                                                         008 - 1x_Mode_Lane0
                                                         010 - 1x_Mode_Lane1
                                                         020 - 1x_Mode_Lane2
                                                         040 - 1x_Recovery
                                                         080 - 2x_Mode
                                                         100 - 2x_Recovery
                                                         200 - 4x_Mode
                                                         All others are reserved */
#else
	uint32_t init_sm                      : 10;
	uint32_t rx_rdy                       : 1;
	uint32_t tx_rdy                       : 1;
	uint32_t reserved_12_31               : 20;
#endif
	} s;
	struct cvmx_sriomaintx_ir_pi_phy_stat_s cn63xx;
	struct cvmx_sriomaintx_ir_pi_phy_stat_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_10_31               : 22;
	uint32_t init_sm                      : 10; /**< Initialization State Machine
                                                         001 - Silent
                                                         002 - Seek
                                                         004 - Discovery
                                                         008 - 1x_Mode_Lane0
                                                         010 - 1x_Mode_Lane1
                                                         020 - 1x_Mode_Lane2
                                                         040 - 1x_Recovery
                                                         080 - 2x_Mode
                                                         100 - 2x_Recovery
                                                         200 - 4x_Mode
                                                         All others are reserved */
#else
	uint32_t init_sm                      : 10;
	uint32_t reserved_10_31               : 22;
#endif
	} cn63xxp1;
	struct cvmx_sriomaintx_ir_pi_phy_stat_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_pi_phy_stat cvmx_sriomaintx_ir_pi_phy_stat_t;

/**
 * cvmx_sriomaint#_ir_sp_rx_ctrl
 *
 * SRIOMAINT_IR_SP_RX_CTRL = SRIO Soft Packet FIFO Receive Control
 *
 * Soft Packet FIFO Receive Control
 *
 * Notes:
 * This register is used to configure events generated by the reception of packets using the soft
 * packet FIFO.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_SP_RX_CTRL hclk    hrst_n
 */
union cvmx_sriomaintx_ir_sp_rx_ctrl {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_sp_rx_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t overwrt                      : 1;  /**< When clear, SRIO drops received packets that should
                                                         enter the soft packet FIFO when the FIFO is full.
                                                         In this case, SRIO also increments
                                                         SRIOMAINT(0,2..3)_IR_SP_RX_STAT.DROP_CNT. When set, SRIO
                                                         stalls received packets that should enter the soft
                                                         packet FIFO when the FIFO is full. SRIO may stop
                                                         receiving any packets in this stall case if
                                                         software does not drain the receive soft packet
                                                         FIFO. */
#else
	uint32_t overwrt                      : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_sriomaintx_ir_sp_rx_ctrl_s cn63xx;
	struct cvmx_sriomaintx_ir_sp_rx_ctrl_s cn63xxp1;
	struct cvmx_sriomaintx_ir_sp_rx_ctrl_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_sp_rx_ctrl cvmx_sriomaintx_ir_sp_rx_ctrl_t;

/**
 * cvmx_sriomaint#_ir_sp_rx_data
 *
 * SRIOMAINT_IR_SP_RX_DATA = SRIO Soft Packet FIFO Receive Data
 *
 * Soft Packet FIFO Receive Data
 *
 * Notes:
 * This register is used to read data from the soft packet FIFO.  The Soft Packet FIFO contains the
 *  majority of the packet data received from the SRIO link.  The packet does not include the Control
 *  Symbols or the initial byte containing AckId, 2 Reserved Bits and the CRF.  In the case of packets
 *  with less than 80 bytes (including AckId byte) both the trailing CRC and Pad (if present) are
 *  included in the FIFO and Octet Count.  In the case of a packet with exactly 80 bytes (including
 *  the AckId byte) the CRC is removed and the Pad is maintained so the Octet Count will read 81 bytes
 *  instead of the expected 83.  In cases over 80 bytes the CRC at 80 bytes is removed but the
 *  trailing CRC and Pad (if necessary) are present.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_SP_RX_DATA hclk    hrst_n
 */
union cvmx_sriomaintx_ir_sp_rx_data {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_sp_rx_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_data                     : 32; /**< This register is used to read packet data from the
                                                         RX FIFO. */
#else
	uint32_t pkt_data                     : 32;
#endif
	} s;
	struct cvmx_sriomaintx_ir_sp_rx_data_s cn63xx;
	struct cvmx_sriomaintx_ir_sp_rx_data_s cn63xxp1;
	struct cvmx_sriomaintx_ir_sp_rx_data_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_sp_rx_data cvmx_sriomaintx_ir_sp_rx_data_t;

/**
 * cvmx_sriomaint#_ir_sp_rx_stat
 *
 * SRIOMAINT_IR_SP_RX_STAT = SRIO Soft Packet FIFO Receive Status
 *
 * Soft Packet FIFO Receive Status
 *
 * Notes:
 * This register is used to monitor the reception of packets using the soft packet FIFO.
 *  The HW sets SRIO_INT_REG[SOFT_RX] every time a packet arrives in the soft packet FIFO. To read
 *  out (one or more) packets, the following procedure may be best:
 *       (1) clear SRIO_INT_REG[SOFT_RX],
 *       (2) read this CSR to determine how many packets there are,
 *       (3) read the packets out (via SRIOMAINT*_IR_SP_RX_DATA).
 *  This procedure could lead to situations where SOFT_RX will be set even though there are currently
 *  no packets - the SW interrupt handler would need to properly handle this case
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_SP_RX_STAT hclk    hrst_n
 */
union cvmx_sriomaintx_ir_sp_rx_stat {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_sp_rx_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t octets                       : 16; /**< This field shows how many octets are remaining
                                                         in the current packet in the RX FIFO. */
	uint32_t buffers                      : 4;  /**< This field indicates how many complete packets are
                                                         stored in the Rx FIFO. */
	uint32_t drop_cnt                     : 7;  /**< Number of Packets Received when the RX FIFO was
                                                         full and then discarded. */
	uint32_t full                         : 1;  /**< This bit is set when the value of Buffers Filled
                                                         equals the number of available reception buffers. */
	uint32_t fifo_st                      : 4;  /**< These bits display the state of the state machine
                                                         that controls loading of packet data into the RX
                                                         FIFO. The enumeration of states are as follows:
                                                           0000 - Idle
                                                           0001 - Armed
                                                           0010 - Active
                                                           All other states are reserved. */
#else
	uint32_t fifo_st                      : 4;
	uint32_t full                         : 1;
	uint32_t drop_cnt                     : 7;
	uint32_t buffers                      : 4;
	uint32_t octets                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_ir_sp_rx_stat_s cn63xx;
	struct cvmx_sriomaintx_ir_sp_rx_stat_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t octets                       : 16; /**< This field shows how many octets are remaining
                                                         in the current packet in the RX FIFO. */
	uint32_t buffers                      : 4;  /**< This field indicates how many complete packets are
                                                         stored in the Rx FIFO. */
	uint32_t reserved_5_11                : 7;
	uint32_t full                         : 1;  /**< This bit is set when the value of Buffers Filled
                                                         equals the number of available reception buffers.
                                                         This bit always reads zero in Pass 1 */
	uint32_t fifo_st                      : 4;  /**< These bits display the state of the state machine
                                                         that controls loading of packet data into the RX
                                                         FIFO. The enumeration of states are as follows:
                                                           0000 - Idle
                                                           0001 - Armed
                                                           0010 - Active
                                                           All other states are reserved. */
#else
	uint32_t fifo_st                      : 4;
	uint32_t full                         : 1;
	uint32_t reserved_5_11                : 7;
	uint32_t buffers                      : 4;
	uint32_t octets                       : 16;
#endif
	} cn63xxp1;
	struct cvmx_sriomaintx_ir_sp_rx_stat_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_sp_rx_stat cvmx_sriomaintx_ir_sp_rx_stat_t;

/**
 * cvmx_sriomaint#_ir_sp_tx_ctrl
 *
 * SRIOMAINT_IR_SP_TX_CTRL = SRIO Soft Packet FIFO Transmit Control
 *
 * Soft Packet FIFO Transmit Control
 *
 * Notes:
 * This register is used to configure and control the transmission of packets using the soft packet
 *  FIFO.
 *
 * Clk_Rst:        SRIOMAINT_IR_SP_TX_CTRL hclk    hrst_n
 */
union cvmx_sriomaintx_ir_sp_tx_ctrl {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_sp_tx_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t octets                       : 16; /**< Writing a non-zero value (N) to this field arms
                                                         the packet FIFO for packet transmission. The FIFO
                                                         control logic will transmit the next N bytes
                                                         written 4-bytes at a time to the
                                                         SRIOMAINT(0,2..3)_IR_SP_TX_DATA Register and create a
                                                         single RapidIO packet. */
	uint32_t reserved_0_15                : 16;
#else
	uint32_t reserved_0_15                : 16;
	uint32_t octets                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_ir_sp_tx_ctrl_s cn63xx;
	struct cvmx_sriomaintx_ir_sp_tx_ctrl_s cn63xxp1;
	struct cvmx_sriomaintx_ir_sp_tx_ctrl_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_sp_tx_ctrl cvmx_sriomaintx_ir_sp_tx_ctrl_t;

/**
 * cvmx_sriomaint#_ir_sp_tx_data
 *
 * SRIOMAINT_IR_SP_TX_DATA = SRIO Soft Packet FIFO Transmit Data
 *
 * Soft Packet FIFO Transmit Data
 *
 * Notes:
 * This register is used to write data to the soft packet FIFO.  The format of the packet follows the
 * Internal Packet Format (add link here).  Care must be taken on creating TIDs for the packets which
 * generate a response.  Bits [7:6] of the 8 bit TID must be set for all Soft Packet FIFO generated
 * packets.  TID values of 0x00 - 0xBF are reserved for hardware generated Tags.  The remainer of the
 * TID[5:0] must be unique for each packet in flight and cannot be reused until a response is received
 * in the SRIOMAINT(0,2..3)_IR_SP_RX_DATA register.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_SP_TX_DATA hclk    hrst_n
 */
union cvmx_sriomaintx_ir_sp_tx_data {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_sp_tx_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_data                     : 32; /**< This register is used to write packet data to the
                                                         Tx FIFO. Reads of this register will return zero. */
#else
	uint32_t pkt_data                     : 32;
#endif
	} s;
	struct cvmx_sriomaintx_ir_sp_tx_data_s cn63xx;
	struct cvmx_sriomaintx_ir_sp_tx_data_s cn63xxp1;
	struct cvmx_sriomaintx_ir_sp_tx_data_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_sp_tx_data cvmx_sriomaintx_ir_sp_tx_data_t;

/**
 * cvmx_sriomaint#_ir_sp_tx_stat
 *
 * SRIOMAINT_IR_SP_TX_STAT = SRIO Soft Packet FIFO Transmit Status
 *
 * Soft Packet FIFO Transmit Status
 *
 * Notes:
 * This register is used to monitor the transmission of packets using the soft packet FIFO.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_IR_SP_TX_STAT hclk    hrst_n
 */
union cvmx_sriomaintx_ir_sp_tx_stat {
	uint32_t u32;
	struct cvmx_sriomaintx_ir_sp_tx_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t octets                       : 16; /**< This field shows how many octets are still to be
                                                         loaded in the current packet. */
	uint32_t buffers                      : 4;  /**< This field indicates how many complete packets are
                                                         stored in the Tx FIFO.  The field always reads
                                                         zero in the current hardware. */
	uint32_t reserved_5_11                : 7;
	uint32_t full                         : 1;  /**< This bit is set when the value of Buffers Filled
                                                         equals the number of available transmission
                                                         buffers. */
	uint32_t fifo_st                      : 4;  /**< These bits display the state of the state machine
                                                         that controls loading of packet data into the TX
                                                         FIFO. The enumeration of states are as follows:
                                                           0000 - Idle
                                                           0001 - Armed
                                                           0010 - Active
                                                           All other states are reserved. */
#else
	uint32_t fifo_st                      : 4;
	uint32_t full                         : 1;
	uint32_t reserved_5_11                : 7;
	uint32_t buffers                      : 4;
	uint32_t octets                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_ir_sp_tx_stat_s cn63xx;
	struct cvmx_sriomaintx_ir_sp_tx_stat_s cn63xxp1;
	struct cvmx_sriomaintx_ir_sp_tx_stat_s cn66xx;
};
typedef union cvmx_sriomaintx_ir_sp_tx_stat cvmx_sriomaintx_ir_sp_tx_stat_t;

/**
 * cvmx_sriomaint#_lane_#_status_0
 *
 * SRIOMAINT_LANE_X_STATUS_0 = SRIO Lane X Status 0
 *
 * SRIO Lane Status 0
 *
 * Notes:
 * This register contains status information about the local lane transceiver.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_LANE_[0:3]_STATUS_0   hclk    hrst_n
 */
union cvmx_sriomaintx_lane_x_status_0 {
	uint32_t u32;
	struct cvmx_sriomaintx_lane_x_status_0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t port                         : 8;  /**< The number of the port within the device to which
                                                         the lane is assigned. */
	uint32_t lane                         : 4;  /**< Lane Number within the port. */
	uint32_t tx_type                      : 1;  /**< Transmitter Type
                                                         0 = Short Run
                                                         1 = Long Run */
	uint32_t tx_mode                      : 1;  /**< Transmitter Operating Mode
                                                         0 = Short Run
                                                         1 = Long Run */
	uint32_t rx_type                      : 2;  /**< Receiver Type
                                                         0 = Short Run
                                                         1 = Medium Run
                                                         2 = Long Run
                                                         3 = Reserved */
	uint32_t rx_inv                       : 1;  /**< Receiver Input Inverted
                                                         0 = No Inversion
                                                         1 = Input Inverted */
	uint32_t rx_adapt                     : 1;  /**< Receiver Trained
                                                         0 = One or more adaptive equalizers are
                                                             controlled by the lane receiver and at least
                                                             one is not trained.
                                                         1 = The lane receiver controls no adaptive
                                                             equalizers or all the equalizers are trained. */
	uint32_t rx_sync                      : 1;  /**< Receiver Lane Sync'd */
	uint32_t rx_train                     : 1;  /**< Receiver Lane Trained */
	uint32_t dec_err                      : 4;  /**< 8Bit/10Bit Decoding Errors
                                                         0    = No Errors since last read
                                                         1-14 = Number of Errors since last read
                                                         15   = Fifteen or more Errors since last read */
	uint32_t xsync                        : 1;  /**< Receiver Lane Sync Change
                                                         0 = Lane Sync has not changed since last read
                                                         1 = Lane Sync has changed since last read */
	uint32_t xtrain                       : 1;  /**< Receiver Training Change
                                                         0 = Training has not changed since last read
                                                         1 = Training has changed since last read */
	uint32_t reserved_4_5                 : 2;
	uint32_t status1                      : 1;  /**< Status 1 CSR Implemented */
	uint32_t statusn                      : 3;  /**< Status 2-7 Not Implemented */
#else
	uint32_t statusn                      : 3;
	uint32_t status1                      : 1;
	uint32_t reserved_4_5                 : 2;
	uint32_t xtrain                       : 1;
	uint32_t xsync                        : 1;
	uint32_t dec_err                      : 4;
	uint32_t rx_train                     : 1;
	uint32_t rx_sync                      : 1;
	uint32_t rx_adapt                     : 1;
	uint32_t rx_inv                       : 1;
	uint32_t rx_type                      : 2;
	uint32_t tx_mode                      : 1;
	uint32_t tx_type                      : 1;
	uint32_t lane                         : 4;
	uint32_t port                         : 8;
#endif
	} s;
	struct cvmx_sriomaintx_lane_x_status_0_s cn63xx;
	struct cvmx_sriomaintx_lane_x_status_0_s cn63xxp1;
	struct cvmx_sriomaintx_lane_x_status_0_s cn66xx;
};
typedef union cvmx_sriomaintx_lane_x_status_0 cvmx_sriomaintx_lane_x_status_0_t;

/**
 * cvmx_sriomaint#_lcs_ba0
 *
 * SRIOMAINT_LCS_BA0 = SRIO Local Configuration Space MSB Base Address
 *
 * MSBs of SRIO Address Space mapped to Maintenance BAR.
 *
 * Notes:
 * The double word aligned SRIO address window mapped to the SRIO Maintenance BAR.  This window has
 *  the highest priority and eclipses matches to the BAR0, BAR1 and BAR2 windows.  Note:  Address bits
 *  not supplied in the transfer are considered zero.  For example, SRIO Address 65:35 must be set to
 *  zero to match in a 34-bit access.  SRIO Address 65:50 must be set to zero to match in a 50-bit
 *  access.  This coding allows the Maintenance Bar window to appear in specific address spaces. The
 *  remaining bits are located in SRIOMAINT(0,2..3)_LCS_BA1. This SRIO maintenance BAR is effectively
 *  disabled when LCSBA[30] is set with 34 or 50-bit addressing.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_LCS_BA0       hclk    hrst_n
 */
union cvmx_sriomaintx_lcs_ba0 {
	uint32_t u32;
	struct cvmx_sriomaintx_lcs_ba0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_31_31               : 1;
	uint32_t lcsba                        : 31; /**< SRIO Address 65:35 */
#else
	uint32_t lcsba                        : 31;
	uint32_t reserved_31_31               : 1;
#endif
	} s;
	struct cvmx_sriomaintx_lcs_ba0_s      cn63xx;
	struct cvmx_sriomaintx_lcs_ba0_s      cn63xxp1;
	struct cvmx_sriomaintx_lcs_ba0_s      cn66xx;
};
typedef union cvmx_sriomaintx_lcs_ba0 cvmx_sriomaintx_lcs_ba0_t;

/**
 * cvmx_sriomaint#_lcs_ba1
 *
 * SRIOMAINT_LCS_BA1 = SRIO Local Configuration Space LSB Base Address
 *
 * LSBs of SRIO Address Space mapped to Maintenance BAR.
 *
 * Notes:
 * The double word aligned SRIO address window mapped to the SRIO Maintenance BAR.  This window has
 *  the highest priority and eclipses matches to the BAR0, BAR1 and BAR2 windows. Address bits not
 *  supplied in the transfer are considered zero.  For example, SRIO Address 65:35 must be set to zero
 *  to match in a 34-bit access and SRIO Address 65:50 must be set to zero to match in a 50-bit access.
 *  This coding allows the Maintenance Bar window to appear in specific address spaces. Accesses
 *  through this BAR are limited to single word (32-bit) aligned transfers of one to four bytes.
 *  Accesses which violate this rule will return an error response if possible and be otherwise
 *  ignored.  The remaining bits are located in SRIOMAINT(0,2..3)_LCS_BA0.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_LCS_BA1       hclk    hrst_n
 */
union cvmx_sriomaintx_lcs_ba1 {
	uint32_t u32;
	struct cvmx_sriomaintx_lcs_ba1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lcsba                        : 11; /**< SRIO Address 34:24 */
	uint32_t reserved_0_20                : 21;
#else
	uint32_t reserved_0_20                : 21;
	uint32_t lcsba                        : 11;
#endif
	} s;
	struct cvmx_sriomaintx_lcs_ba1_s      cn63xx;
	struct cvmx_sriomaintx_lcs_ba1_s      cn63xxp1;
	struct cvmx_sriomaintx_lcs_ba1_s      cn66xx;
};
typedef union cvmx_sriomaintx_lcs_ba1 cvmx_sriomaintx_lcs_ba1_t;

/**
 * cvmx_sriomaint#_m2s_bar0_start0
 *
 * SRIOMAINT_M2S_BAR0_START0 = SRIO Device Access BAR0 MSB Start
 *
 * The starting SRIO address to forwarded to the NPEI Configuration Space.
 *
 * Notes:
 * This register specifies the 50-bit and 66-bit SRIO Address mapped to the BAR0 Space.  See
 *  SRIOMAINT(0,2..3)_M2S_BAR0_START1 for more details. This register is only writeable over SRIO if the
 *  SRIO(0,2..3)_ACC_CTRL.DENY_BAR0 bit is zero.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_M2S_BAR0_START0       hclk    hrst_n
 */
union cvmx_sriomaintx_m2s_bar0_start0 {
	uint32_t u32;
	struct cvmx_sriomaintx_m2s_bar0_start0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr64                       : 16; /**< SRIO Address 63:48 */
	uint32_t addr48                       : 16; /**< SRIO Address 47:32 */
#else
	uint32_t addr48                       : 16;
	uint32_t addr64                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_m2s_bar0_start0_s cn63xx;
	struct cvmx_sriomaintx_m2s_bar0_start0_s cn63xxp1;
	struct cvmx_sriomaintx_m2s_bar0_start0_s cn66xx;
};
typedef union cvmx_sriomaintx_m2s_bar0_start0 cvmx_sriomaintx_m2s_bar0_start0_t;

/**
 * cvmx_sriomaint#_m2s_bar0_start1
 *
 * SRIOMAINT_M2S_BAR0_START1 = SRIO Device Access BAR0 LSB Start
 *
 * The starting SRIO address to forwarded to the NPEI Configuration Space.
 *
 * Notes:
 * This register specifies the SRIO Address mapped to the BAR0 RSL Space.  If the transaction has not
 *  already been mapped to SRIO Maintenance Space through the SRIOMAINT_LCS_BA[1:0] registers, if
 *  ENABLE is set and the address bits match then the SRIO Memory transactions will map to Octeon SLI
 *  Registers.  34-bit address transactions require a match in SRIO Address 33:14 and require all the
 *  other bits in ADDR48, ADDR64 and ADDR66 fields to be zero.  50-bit address transactions a match of
 *  SRIO Address 49:14 and require all the other bits of ADDR64 and ADDR66 to be zero.  66-bit address
 *  transactions require matches of all valid address field bits.  Reads and  Writes through Bar0
 *  have a size limit of 8 bytes and cannot cross a 64-bit boundry.  All accesses with sizes greater
 *  than this limit will be ignored and return an error on any SRIO responses.  Note: ADDR48 and
 *  ADDR64 fields are located in SRIOMAINT(0,2..3)_M2S_BAR0_START0.  The ADDR32/66 fields of this register
 *  are writeable over SRIO if the SRIO(0,2..3)_ACC_CTRL.DENY_ADR0 bit is zero.  The ENABLE field is
 *  writeable over SRIO if the SRIO(0,2..3)_ACC_CTRL.DENY_BAR0 bit is zero.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_M2S_BAR0_START1       hclk    hrst_n
 */
union cvmx_sriomaintx_m2s_bar0_start1 {
	uint32_t u32;
	struct cvmx_sriomaintx_m2s_bar0_start1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr32                       : 18; /**< SRIO Address 31:14 */
	uint32_t reserved_3_13                : 11;
	uint32_t addr66                       : 2;  /**< SRIO Address 65:64 */
	uint32_t enable                       : 1;  /**< Enable BAR0 Access */
#else
	uint32_t enable                       : 1;
	uint32_t addr66                       : 2;
	uint32_t reserved_3_13                : 11;
	uint32_t addr32                       : 18;
#endif
	} s;
	struct cvmx_sriomaintx_m2s_bar0_start1_s cn63xx;
	struct cvmx_sriomaintx_m2s_bar0_start1_s cn63xxp1;
	struct cvmx_sriomaintx_m2s_bar0_start1_s cn66xx;
};
typedef union cvmx_sriomaintx_m2s_bar0_start1 cvmx_sriomaintx_m2s_bar0_start1_t;

/**
 * cvmx_sriomaint#_m2s_bar1_start0
 *
 * SRIOMAINT_M2S_BAR1_START0 = SRIO Device Access BAR1 MSB Start
 *
 * The starting SRIO address to forwarded to the BAR1 Memory Space.
 *
 * Notes:
 * This register specifies the 50-bit and 66-bit SRIO Address mapped to the BAR1 Space.  See
 *  SRIOMAINT(0,2..3)_M2S_BAR1_START1 for more details.  This register is only writeable over SRIO if the
 *  SRIO(0,2..3)_ACC_CTRL.DENY_ADR1 bit is zero.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_M2S_BAR1_START0       hclk    hrst_n
 */
union cvmx_sriomaintx_m2s_bar1_start0 {
	uint32_t u32;
	struct cvmx_sriomaintx_m2s_bar1_start0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr64                       : 16; /**< SRIO Address 63:48 */
	uint32_t addr48                       : 16; /**< SRIO Address 47:32
                                                         The SRIO hardware does not use the low order
                                                         one or two bits of this field when BARSIZE is 12
                                                         or 13, respectively.
                                                         (BARSIZE is SRIOMAINT(0,2..3)_M2S_BAR1_START1[BARSIZE].) */
#else
	uint32_t addr48                       : 16;
	uint32_t addr64                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_m2s_bar1_start0_s cn63xx;
	struct cvmx_sriomaintx_m2s_bar1_start0_s cn63xxp1;
	struct cvmx_sriomaintx_m2s_bar1_start0_s cn66xx;
};
typedef union cvmx_sriomaintx_m2s_bar1_start0 cvmx_sriomaintx_m2s_bar1_start0_t;

/**
 * cvmx_sriomaint#_m2s_bar1_start1
 *
 * SRIOMAINT_M2S_BAR1_START1 = SRIO Device to BAR1 Start
 *
 * The starting SRIO address to forwarded to the BAR1 Memory Space.
 *
 * Notes:
 * This register specifies the SRIO Address mapped to the BAR1 Space.  If the transaction has not
 *  already been mapped to SRIO Maintenance Space through the SRIOMAINT_LCS_BA[1:0] registers and the
 *  address bits do not match enabled BAR0 addresses and if ENABLE is set and the addresses match the
 *  BAR1 addresses then SRIO Memory transactions will map to Octeon Memory Space specified by
 *  SRIOMAINT(0,2..3)_BAR1_IDX[31:0] registers.  The BARSIZE field determines the size of BAR1, the entry
 *  select bits, and the size of each entry. A 34-bit address matches BAR1 when it matches
 *  SRIO_Address[33:20+BARSIZE] while all the other bits in ADDR48, ADDR64 and ADDR66 are zero.
 *  A 50-bit address matches BAR1 when it matches SRIO_Address[49:20+BARSIZE] while all the
 *  other bits of ADDR64 and ADDR66 are zero.  A 66-bit address matches BAR1 when all of
 *  SRIO_Address[65:20+BARSIZE] match all corresponding address CSR field bits.  Note: ADDR48 and
 *  ADDR64 fields are located in SRIOMAINT(0,2..3)_M2S_BAR1_START0. The ADDR32/66 fields of this register
 *  are writeable over SRIO if the SRIO(0,2..3)_ACC_CTRL.DENY_ADR1 bit is zero.  The remaining fields are
 *  writeable over SRIO if the SRIO(0,2..3)_ACC_CTRL.DENY_BAR1 bit is zero.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_M2S_BAR1_START1       hclk    hrst_n
 */
union cvmx_sriomaintx_m2s_bar1_start1 {
	uint32_t u32;
	struct cvmx_sriomaintx_m2s_bar1_start1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr32                       : 12; /**< SRIO Address 31:20
                                                         This field is not used by the SRIO hardware for
                                                         BARSIZE values 12 or 13.
                                                         With BARSIZE < 12, the upper 12-BARSIZE
                                                         bits of this field are used, and the lower BARSIZE
                                                         bits of this field are unused by the SRIO hardware. */
	uint32_t reserved_7_19                : 13;
	uint32_t barsize                      : 4;  /**< Bar Size.
                                                                              SRIO_Address*
                                                                         ---------------------
                                                                        /                     \
                                                         BARSIZE         BAR     Entry   Entry    Entry
                                                         Value   BAR    compare  Select  Offset   Size
                                                                 Size    bits    bits    bits
                                                          0       1MB    65:20   19:16   15:0     64KB
                                                          1       2MB    65:21   20:17   16:0    128KB
                                                          2       4MB    65:22   21:18   17:0    256KB
                                                          3       8MB    65:23   22:19   18:0    512KB
                                                          4      16MB    65:24   23:20   19:0      1MB
                                                          5      32MB    65:25   24:21   20:0      2MB
                                                          6      64MB    65:26   25:22   21:0      4MB
                                                          7     128MB    65:27   26:23   22:0      8MB
                                                          8     256MB    65:28   27:24   23:0     16MB
                                                          9     512MB    65:29   28:25   24:0     32MB
                                                         10    1024MB    65:30   29:26   25:0     64MB
                                                         11    2048MB    65:31   30:27   26:0    128MB
                                                         12    4096MB    65:32   31:28   27:0    256MB
                                                         13    8192MB    65:33   32:29   28:0    512MB

                                                         *The SRIO Transaction Address
                                                         The entry select bits is the X that  select an
                                                         SRIOMAINT(0,2..3)_BAR1_IDXX entry. */
	uint32_t addr66                       : 2;  /**< SRIO Address 65:64 */
	uint32_t enable                       : 1;  /**< Enable BAR1 Access */
#else
	uint32_t enable                       : 1;
	uint32_t addr66                       : 2;
	uint32_t barsize                      : 4;
	uint32_t reserved_7_19                : 13;
	uint32_t addr32                       : 12;
#endif
	} s;
	struct cvmx_sriomaintx_m2s_bar1_start1_s cn63xx;
	struct cvmx_sriomaintx_m2s_bar1_start1_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr32                       : 12; /**< SRIO Address 31:20
                                                         With BARSIZE < 12, the upper 12-BARSIZE
                                                         bits of this field are used, and the lower BARSIZE
                                                         bits of this field are unused by the SRIO hardware. */
	uint32_t reserved_6_19                : 14;
	uint32_t barsize                      : 3;  /**< Bar Size.
                                                                              SRIO_Address*
                                                                         ---------------------
                                                                        /                     \
                                                         BARSIZE         BAR     Entry   Entry    Entry
                                                         Value   BAR    compare  Select  Offset   Size
                                                                 Size    bits    bits    bits
                                                          0       1MB    65:20   19:16   15:0     64KB
                                                          1       2MB    65:21   20:17   16:0    128KB
                                                          2       4MB    65:22   21:18   17:0    256KB
                                                          3       8MB    65:23   22:19   18:0    512KB
                                                          4      16MB    65:24   23:20   19:0      1MB
                                                          5      32MB    65:25   24:21   20:0      2MB
                                                          6      64MB    65:26   25:22   21:0      4MB
                                                          7     128MB    65:27   26:23   22:0      8MB
                                                          8     256MB  ** not in pass 1
                                                          9     512MB  ** not in pass 1
                                                         10       1GB  ** not in pass 1
                                                         11       2GB  ** not in pass 1
                                                         12       4GB  ** not in pass 1
                                                         13       8GB  ** not in pass 1

                                                         *The SRIO Transaction Address
                                                         The entry select bits is the X that  select an
                                                         SRIOMAINT(0..1)_BAR1_IDXX entry.

                                                         In O63 pass 2, BARSIZE is 4 bits (6:3 in this
                                                         CSR), and BARSIZE values 8-13 are implemented,
                                                         providing a total possible BAR1 size range from
                                                         1MB up to 8GB. */
	uint32_t addr66                       : 2;  /**< SRIO Address 65:64 */
	uint32_t enable                       : 1;  /**< Enable BAR1 Access */
#else
	uint32_t enable                       : 1;
	uint32_t addr66                       : 2;
	uint32_t barsize                      : 3;
	uint32_t reserved_6_19                : 14;
	uint32_t addr32                       : 12;
#endif
	} cn63xxp1;
	struct cvmx_sriomaintx_m2s_bar1_start1_s cn66xx;
};
typedef union cvmx_sriomaintx_m2s_bar1_start1 cvmx_sriomaintx_m2s_bar1_start1_t;

/**
 * cvmx_sriomaint#_m2s_bar2_start
 *
 * SRIOMAINT_M2S_BAR2_START = SRIO Device to BAR2 Start
 *
 * The starting SRIO address to forwarded to the BAR2 Memory Space.
 *
 * Notes:
 * This register specifies the SRIO Address mapped to the BAR2 Space.  If ENABLE is set and the
 *  address bits do not match and other enabled BAR address and match the BAR2 addresses then the SRIO
 *  Memory transactions will map to Octeon BAR2 Memory Space.  34-bit address transactions require
 *  ADDR66, ADDR64 and ADDR48 fields set to zero and supplies zeros for unused addresses 40:34.
 *  50-bit address transactions a match of SRIO Address 49:41 and require all the other bits of ADDR64
 *  and ADDR66 to be zero.  66-bit address transactions require matches of all valid address field
 *  bits.  The ADDR32/48/64/66 fields of this register are writeable over SRIO if the
 *  SRIO(0,2..3)_ACC_CTRL.DENY_ADR2 bit is zero.  The remaining fields are writeable over SRIO if the
 *  SRIO(0,2..3)_ACC_CTRL.DENY_BAR2 bit is zero.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_M2S_BAR2_START        hclk    hrst_n
 */
union cvmx_sriomaintx_m2s_bar2_start {
	uint32_t u32;
	struct cvmx_sriomaintx_m2s_bar2_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t addr64                       : 16; /**< SRIO Address 63:48 */
	uint32_t addr48                       : 7;  /**< SRIO Address 47:41 */
	uint32_t reserved_6_8                 : 3;
	uint32_t esx                          : 2;  /**< Endian Swap Mode used for SRIO 34-bit access.
                                                         For 50/66-bit assesses Endian Swap is determine
                                                         by ESX XOR'd with SRIO Addr 39:38.
                                                         0 = No Swap
                                                         1 = 64-bit Swap Bytes [ABCD_EFGH] -> [HGFE_DCBA]
                                                         2 = 32-bit Swap Words [ABCD_EFGH] -> [DCBA_HGFE]
                                                         3 = 32-bit Word Exch  [ABCD_EFGH] -> [EFGH_ABCD] */
	uint32_t cax                          : 1;  /**< Cacheable Access Mode.  When set transfer is
                                                         cached.  This bit is used for SRIO 34-bit access.
                                                         For 50/66-bit accessas NCA is determine by CAX
                                                         XOR'd with SRIO Addr 40. */
	uint32_t addr66                       : 2;  /**< SRIO Address 65:64 */
	uint32_t enable                       : 1;  /**< Enable BAR2 Access */
#else
	uint32_t enable                       : 1;
	uint32_t addr66                       : 2;
	uint32_t cax                          : 1;
	uint32_t esx                          : 2;
	uint32_t reserved_6_8                 : 3;
	uint32_t addr48                       : 7;
	uint32_t addr64                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_m2s_bar2_start_s cn63xx;
	struct cvmx_sriomaintx_m2s_bar2_start_s cn63xxp1;
	struct cvmx_sriomaintx_m2s_bar2_start_s cn66xx;
};
typedef union cvmx_sriomaintx_m2s_bar2_start cvmx_sriomaintx_m2s_bar2_start_t;

/**
 * cvmx_sriomaint#_mac_ctrl
 *
 * SRIOMAINT_MAC_CTRL = SRIO MAC Control
 *
 * Control for MAC Features
 *
 * Notes:
 * This register enables MAC optimizations that may not be supported by all SRIO devices.  The
 *  default values should be supported.  This register can be changed at any time while the MAC is
 *  out of reset.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_MAC_CTRL      hclk    hrst_n
 */
union cvmx_sriomaintx_mac_ctrl {
	uint32_t u32;
	struct cvmx_sriomaintx_mac_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_21_31               : 11;
	uint32_t sec_spf                      : 1;  /**< Send all Incoming Packets matching Secondary ID to
                                                         RX Soft Packet FIFO.  This bit is ignored if
                                                         RX_SPF is set. */
	uint32_t ack_zero                     : 1;  /**< Generate ACKs for all incoming Zero Byte packets.
                                                         Default behavior is to issue a NACK.  Regardless
                                                         of this setting the SRIO(0,2..3)_INT_REG.ZERO_PKT
                                                         interrupt is generated.
                                                         SRIO(0,2..3)_INT_REG. */
	uint32_t rx_spf                       : 1;  /**< Route all received packets to RX Soft Packet FIFO.
                                                         No logical layer ERB Errors will be reported.
                                                         Used for Diagnostics Only. */
	uint32_t eop_mrg                      : 1;  /**< Transmitted Packets can eliminate EOP Symbol on
                                                         back to back packets. */
	uint32_t type_mrg                     : 1;  /**< Allow STYPE Merging on Transmit. */
	uint32_t lnk_rtry                     : 16; /**< Number of times MAC will reissue Link Request
                                                         after timeout.  If retry count is exceeded Fatal
                                                         Port Error will occur (see SRIO(0,2..3)_INT_REG.F_ERROR) */
#else
	uint32_t lnk_rtry                     : 16;
	uint32_t type_mrg                     : 1;
	uint32_t eop_mrg                      : 1;
	uint32_t rx_spf                       : 1;
	uint32_t ack_zero                     : 1;
	uint32_t sec_spf                      : 1;
	uint32_t reserved_21_31               : 11;
#endif
	} s;
	struct cvmx_sriomaintx_mac_ctrl_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t ack_zero                     : 1;  /**< Generate ACKs for all incoming Zero Byte packets.
                                                         Default behavior is to issue a NACK.  Regardless
                                                         of this setting the SRIO(0..1)_INT_REG.ZERO_PKT
                                                         interrupt is generated.
                                                         SRIO(0..1)_INT_REG. */
	uint32_t rx_spf                       : 1;  /**< Route all received packets to RX Soft Packet FIFO.
                                                         No logical layer ERB Errors will be reported.
                                                         Used for Diagnostics Only. */
	uint32_t eop_mrg                      : 1;  /**< Transmitted Packets can eliminate EOP Symbol on
                                                         back to back packets. */
	uint32_t type_mrg                     : 1;  /**< Allow STYPE Merging on Transmit. */
	uint32_t lnk_rtry                     : 16; /**< Number of times MAC will reissue Link Request
                                                         after timeout.  If retry count is exceeded Fatal
                                                         Port Error will occur (see SRIO(0..1)_INT_REG.F_ERROR) */
#else
	uint32_t lnk_rtry                     : 16;
	uint32_t type_mrg                     : 1;
	uint32_t eop_mrg                      : 1;
	uint32_t rx_spf                       : 1;
	uint32_t ack_zero                     : 1;
	uint32_t reserved_20_31               : 12;
#endif
	} cn63xx;
	struct cvmx_sriomaintx_mac_ctrl_s     cn66xx;
};
typedef union cvmx_sriomaintx_mac_ctrl cvmx_sriomaintx_mac_ctrl_t;

/**
 * cvmx_sriomaint#_pe_feat
 *
 * SRIOMAINT_PE_FEAT = SRIO Processing Element Features
 *
 * The Supported Processing Element Features.
 *
 * Notes:
 * The Processing Element Feature register describes the major functionality provided by the SRIO
 *  device.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PE_FEAT       hclk    hrst_n
 */
union cvmx_sriomaintx_pe_feat {
	uint32_t u32;
	struct cvmx_sriomaintx_pe_feat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bridge                       : 1;  /**< Bridge Functions not supported. */
	uint32_t memory                       : 1;  /**< PE contains addressable memory. */
	uint32_t proc                         : 1;  /**< PE contains a local processor. */
	uint32_t switchf                      : 1;  /**< Switch Functions not supported. */
	uint32_t mult_prt                     : 1;  /**< Multiport Functions not supported. */
	uint32_t reserved_7_26                : 20;
	uint32_t suppress                     : 1;  /**< Error Recovery Suppression not supported. */
	uint32_t crf                          : 1;  /**< Critical Request Flow not supported. */
	uint32_t lg_tran                      : 1;  /**< Large Transport (16-bit Device IDs) supported. */
	uint32_t ex_feat                      : 1;  /**< Extended Feature Pointer is valid. */
	uint32_t ex_addr                      : 3;  /**< PE supports 66, 50 and 34-bit addresses.
                                                         [2:1] are a RO copy of SRIO*_IP_FEATURE[A66,A50]. */
#else
	uint32_t ex_addr                      : 3;
	uint32_t ex_feat                      : 1;
	uint32_t lg_tran                      : 1;
	uint32_t crf                          : 1;
	uint32_t suppress                     : 1;
	uint32_t reserved_7_26                : 20;
	uint32_t mult_prt                     : 1;
	uint32_t switchf                      : 1;
	uint32_t proc                         : 1;
	uint32_t memory                       : 1;
	uint32_t bridge                       : 1;
#endif
	} s;
	struct cvmx_sriomaintx_pe_feat_s      cn63xx;
	struct cvmx_sriomaintx_pe_feat_s      cn63xxp1;
	struct cvmx_sriomaintx_pe_feat_s      cn66xx;
};
typedef union cvmx_sriomaintx_pe_feat cvmx_sriomaintx_pe_feat_t;

/**
 * cvmx_sriomaint#_pe_llc
 *
 * SRIOMAINT_PE_LLC = SRIO Processing Element Logical Layer Control
 *
 * Addresses supported by the SRIO Device.
 *
 * Notes:
 * The Processing Element Logical Layer is used for general configuration for the logical interface.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PE_LLC        hclk    hrst_n
 */
union cvmx_sriomaintx_pe_llc {
	uint32_t u32;
	struct cvmx_sriomaintx_pe_llc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_3_31                : 29;
	uint32_t ex_addr                      : 3;  /**< Controls the number of address bits generated by
                                                         PE as a source and processed by the PE as a
                                                         target of an operation.
                                                          001 = 34-bit Addresses
                                                          010 = 50-bit Addresses
                                                          100 = 66-bit Addresses
                                                          All other encodings are reserved. */
#else
	uint32_t ex_addr                      : 3;
	uint32_t reserved_3_31                : 29;
#endif
	} s;
	struct cvmx_sriomaintx_pe_llc_s       cn63xx;
	struct cvmx_sriomaintx_pe_llc_s       cn63xxp1;
	struct cvmx_sriomaintx_pe_llc_s       cn66xx;
};
typedef union cvmx_sriomaintx_pe_llc cvmx_sriomaintx_pe_llc_t;

/**
 * cvmx_sriomaint#_port_0_ctl
 *
 * SRIOMAINT_PORT_0_CTL = SRIO Port 0 Control
 *
 * Port 0 Control
 *
 * Notes:
 * This register contains assorted control bits.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_0_CTL    hclk    hrst_n
 */
union cvmx_sriomaintx_port_0_ctl {
	uint32_t u32;
	struct cvmx_sriomaintx_port_0_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pt_width                     : 2;  /**< Hardware Port Width.
                                                         00 = One Lane supported.
                                                         01 = One/Four Lanes supported.
                                                         10 = One/Two Lanes supported.
                                                         11 = One/Two/Four Lanes supported.
                                                         This value is a copy of SRIO*_IP_FEATURE[PT_WIDTH]
                                                         limited by the number of lanes the MAC has. */
	uint32_t it_width                     : 3;  /**< Initialized Port Width
                                                         000 = Single-lane, Lane 0
                                                         001 = Single-lane, Lane 1 or 2
                                                         010 = Four-lane
                                                         011 = Two-lane
                                                         111 = Link Uninitialized
                                                         Others = Reserved */
	uint32_t ov_width                     : 3;  /**< Override Port Width.  Writing this register causes
                                                         the port to reinitialize.
                                                         000 = No Override all lanes possible
                                                         001 = Reserved
                                                         010 = Force Single-lane, Lane 0
                                                               If Ln 0 is unavailable try Ln 2 then Ln 1
                                                         011 = Force Single-lane, Lane 2
                                                               If Ln 2 is unavailable try Ln 1 then Ln 0
                                                         100 = Reserved
                                                         101 = Enable Two-lane, Disable Four-Lane
                                                         110 = Enable Four-lane, Disable Two-Lane
                                                         111 = All lanes sizes enabled */
	uint32_t disable                      : 1;  /**< Port Disable.  Setting this bit disables both
                                                         drivers and receivers. */
	uint32_t o_enable                     : 1;  /**< Port Output Enable.  When cleared, port will
                                                         generate control symbols and respond to
                                                         maintenance transactions only.  When set, all
                                                         transactions are allowed. */
	uint32_t i_enable                     : 1;  /**< Port Input Enable.  When cleared, port will
                                                         generate control symbols and respond to
                                                         maintenance packets only.  All other packets will
                                                         not be accepted. */
	uint32_t dis_err                      : 1;  /**< Disable Error Checking.  Diagnostic Only. */
	uint32_t mcast                        : 1;  /**< Reserved. */
	uint32_t reserved_18_18               : 1;
	uint32_t enumb                        : 1;  /**< Enumeration Boundry. SW can use this bit to
                                                         determine port enumeration. */
	uint32_t reserved_16_16               : 1;
	uint32_t ex_width                     : 2;  /**< Extended Port Width not supported. */
	uint32_t ex_stat                      : 2;  /**< Extended Port Width Status. 00 = not supported */
	uint32_t suppress                     : 8;  /**< Retransmit Suppression Mask.  CRF not Supported. */
	uint32_t stp_port                     : 1;  /**< Stop on Failed Port.  This bit is used with the
                                                         DROP_PKT bit to force certain behavior when the
                                                         Error Rate Failed Threshold has been met or
                                                         exceeded. */
	uint32_t drop_pkt                     : 1;  /**< Drop on Failed Port.  This bit is used with the
                                                         STP_PORT bit to force certain behavior when the
                                                         Error Rate Failed Threshold has been met or
                                                         exceeded. */
	uint32_t prt_lock                     : 1;  /**< When this bit is cleared, the packets that may be
                                                         received and issued are controlled by the state of
                                                         the O_ENABLE and I_ENABLE bits.  When this bit is
                                                         set, this port is stopped and is not enabled to
                                                         issue or receive any packets; the input port can
                                                         still follow the training procedure and can still
                                                         send and respond to link-requests; all received
                                                         packets return packet-not-accepted control symbols
                                                         to force an error condition to be signaled by the
                                                         sending device. */
	uint32_t pt_type                      : 1;  /**< Port Type.  1 = Serial port. */
#else
	uint32_t pt_type                      : 1;
	uint32_t prt_lock                     : 1;
	uint32_t drop_pkt                     : 1;
	uint32_t stp_port                     : 1;
	uint32_t suppress                     : 8;
	uint32_t ex_stat                      : 2;
	uint32_t ex_width                     : 2;
	uint32_t reserved_16_16               : 1;
	uint32_t enumb                        : 1;
	uint32_t reserved_18_18               : 1;
	uint32_t mcast                        : 1;
	uint32_t dis_err                      : 1;
	uint32_t i_enable                     : 1;
	uint32_t o_enable                     : 1;
	uint32_t disable                      : 1;
	uint32_t ov_width                     : 3;
	uint32_t it_width                     : 3;
	uint32_t pt_width                     : 2;
#endif
	} s;
	struct cvmx_sriomaintx_port_0_ctl_s   cn63xx;
	struct cvmx_sriomaintx_port_0_ctl_s   cn63xxp1;
	struct cvmx_sriomaintx_port_0_ctl_s   cn66xx;
};
typedef union cvmx_sriomaintx_port_0_ctl cvmx_sriomaintx_port_0_ctl_t;

/**
 * cvmx_sriomaint#_port_0_ctl2
 *
 * SRIOMAINT_PORT_0_CTL2 = SRIO Port 0 Control 2
 *
 * Port 0 Control 2
 *
 * Notes:
 * These registers are accessed when a local processor or an external device wishes to examine the
 *  port baudrate information.  The Automatic Baud Rate Feature is not available on this device.  The
 *  SUP_* and ENB_* fields are set directly by the QLM_SPD bits as a reference but otherwise have
 *  no effect.  WARNING:  Writes to this register will reinitialize the SRIO link.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_0_CTL2   hclk    hrst_n
 */
union cvmx_sriomaintx_port_0_ctl2 {
	uint32_t u32;
	struct cvmx_sriomaintx_port_0_ctl2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t sel_baud                     : 4;  /**< Link Baud Rate Selected.
                                                           0000 - No rate selected
                                                           0001 - 1.25 GBaud
                                                           0010 - 2.5 GBaud
                                                           0011 - 3.125 GBaud
                                                           0100 - 5.0 GBaud
                                                           0101 - 6.25 GBaud (reserved)
                                                           0110 - 0b1111 - Reserved
                                                         Indicates the speed of the interface SERDES lanes
                                                         (selected by the QLM*_SPD straps). */
	uint32_t baud_sup                     : 1;  /**< Automatic Baud Rate Discovery not supported. */
	uint32_t baud_enb                     : 1;  /**< Auto Baud Rate Discovery Enable. */
	uint32_t sup_125g                     : 1;  /**< 1.25GB Rate Operation supported.
                                                         Set when the interface SERDES lanes are operating
                                                         at 1.25 Gbaud (as selected by QLM*_SPD straps). */
	uint32_t enb_125g                     : 1;  /**< 1.25GB Rate Operation enable.
                                                         Reset to 1 when the interface SERDES lanes are
                                                         operating at 1.25 Gbaud (as selected by QLM*_SPD
                                                         straps). Reset to 0 otherwise. */
	uint32_t sup_250g                     : 1;  /**< 2.50GB Rate Operation supported.
                                                         Set when the interface SERDES lanes are operating
                                                         at 2.5 Gbaud (as selected by QLM*_SPD straps). */
	uint32_t enb_250g                     : 1;  /**< 2.50GB Rate Operation enable.
                                                         Reset to 1 when the interface SERDES lanes are
                                                         operating at 2.5 Gbaud (as selected by QLM*_SPD
                                                         straps). Reset to 0 otherwise. */
	uint32_t sup_312g                     : 1;  /**< 3.125GB Rate Operation supported.
                                                         Set when the interface SERDES lanes are operating
                                                         at 3.125 Gbaud (as selected by QLM*_SPD straps). */
	uint32_t enb_312g                     : 1;  /**< 3.125GB Rate Operation enable.
                                                         Reset to 1 when the interface SERDES lanes are
                                                         operating at 3.125 Gbaud (as selected by QLM*_SPD
                                                         straps). Reset to 0 otherwise. */
	uint32_t sub_500g                     : 1;  /**< 5.0GB Rate Operation supported.
                                                         Set when the interface SERDES lanes are operating
                                                         at 5.0 Gbaud (as selected by QLM*_SPD straps). */
	uint32_t enb_500g                     : 1;  /**< 5.0GB Rate Operation enable.
                                                         Reset to 1 when the interface SERDES lanes are
                                                         operating at 5.0 Gbaud (as selected by QLM*_SPD
                                                         straps). Reset to 0 otherwise. */
	uint32_t sup_625g                     : 1;  /**< 6.25GB Rate Operation (not supported). */
	uint32_t enb_625g                     : 1;  /**< 6.25GB Rate Operation enable. */
	uint32_t reserved_2_15                : 14;
	uint32_t tx_emph                      : 1;  /**< Indicates whether is port is able to transmit
                                                         commands to control the transmit emphasis in the
                                                         connected port. */
	uint32_t emph_en                      : 1;  /**< Controls whether a port may adjust the
                                                         transmit emphasis in the connected port.  This bit
                                                         should be cleared for normal operation. */
#else
	uint32_t emph_en                      : 1;
	uint32_t tx_emph                      : 1;
	uint32_t reserved_2_15                : 14;
	uint32_t enb_625g                     : 1;
	uint32_t sup_625g                     : 1;
	uint32_t enb_500g                     : 1;
	uint32_t sub_500g                     : 1;
	uint32_t enb_312g                     : 1;
	uint32_t sup_312g                     : 1;
	uint32_t enb_250g                     : 1;
	uint32_t sup_250g                     : 1;
	uint32_t enb_125g                     : 1;
	uint32_t sup_125g                     : 1;
	uint32_t baud_enb                     : 1;
	uint32_t baud_sup                     : 1;
	uint32_t sel_baud                     : 4;
#endif
	} s;
	struct cvmx_sriomaintx_port_0_ctl2_s  cn63xx;
	struct cvmx_sriomaintx_port_0_ctl2_s  cn63xxp1;
	struct cvmx_sriomaintx_port_0_ctl2_s  cn66xx;
};
typedef union cvmx_sriomaintx_port_0_ctl2 cvmx_sriomaintx_port_0_ctl2_t;

/**
 * cvmx_sriomaint#_port_0_err_stat
 *
 * SRIOMAINT_PORT_0_ERR_STAT = SRIO Port 0 Error and Status
 *
 * Port 0 Error and Status
 *
 * Notes:
 * This register displays port error and status information.  Several port error conditions are
 *  captured here and must be cleared by writing 1's to the individual bits.
 *  Bits are R/W on 65/66xx pass 1 and R/W1C on pass 1.2
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_0_ERR_STAT       hclk    hrst_n
 */
union cvmx_sriomaintx_port_0_err_stat {
	uint32_t u32;
	struct cvmx_sriomaintx_port_0_err_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_27_31               : 5;
	uint32_t pkt_drop                     : 1;  /**< Output Packet Dropped. */
	uint32_t o_fail                       : 1;  /**< Output Port has encountered a failure condition,
                                                         meaning the port's failed error threshold has
                                                         reached SRIOMAINT(0,2..3)_ERB_ERR_RATE_THR.ER_FAIL value. */
	uint32_t o_dgrad                      : 1;  /**< Output Port has encountered a degraded condition,
                                                         meaning the port's degraded threshold has
                                                         reached SRIOMAINT(0,2..3)_ERB_ERR_RATE_THR.ER_DGRAD
                                                         value. */
	uint32_t reserved_21_23               : 3;
	uint32_t o_retry                      : 1;  /**< Output Retry Encountered.  This bit is set when
                                                         bit 18 is set. */
	uint32_t o_rtried                     : 1;  /**< Output Port has received a packet-retry condition
                                                         and cannot make forward progress.  This bit is set
                                                         when  bit 18 is set and is cleared when a packet-
                                                         accepted or a packet-not-accepted control symbol
                                                         is received. */
	uint32_t o_sm_ret                     : 1;  /**< Output Port State Machine has received a
                                                         packet-retry control symbol and is retrying the
                                                         packet. */
	uint32_t o_error                      : 1;  /**< Output Error Encountered and possibly recovered
                                                         from.  This sticky bit is set with bit 16. */
	uint32_t o_sm_err                     : 1;  /**< Output Port State Machine has encountered an
                                                         error. */
	uint32_t reserved_11_15               : 5;
	uint32_t i_sm_ret                     : 1;  /**< Input Port State Machine has received a
                                                         packet-retry control symbol and is retrying the
                                                         packet. */
	uint32_t i_error                      : 1;  /**< Input Error Encountered and possibly recovered
                                                         from.  This sticky bit is set with bit 8. */
	uint32_t i_sm_err                     : 1;  /**< Input Port State Machine has encountered an
                                                         error. */
	uint32_t reserved_5_7                 : 3;
	uint32_t pt_write                     : 1;  /**< Port has encountered a condition which required it
                                                         initiate a Maintenance Port-Write Operation.
                                                         Never set by hardware. */
	uint32_t reserved_3_3                 : 1;
	uint32_t pt_error                     : 1;  /**< Input or Output Port has encountered an
                                                         unrecoverable error condition. */
	uint32_t pt_ok                        : 1;  /**< Input or Output Port are intitialized and the port
                                                         is exchanging error free control symbols with
                                                         attached device. */
	uint32_t pt_uinit                     : 1;  /**< Port is uninitialized.  This bit and bit 1 are
                                                         mutually exclusive. */
#else
	uint32_t pt_uinit                     : 1;
	uint32_t pt_ok                        : 1;
	uint32_t pt_error                     : 1;
	uint32_t reserved_3_3                 : 1;
	uint32_t pt_write                     : 1;
	uint32_t reserved_5_7                 : 3;
	uint32_t i_sm_err                     : 1;
	uint32_t i_error                      : 1;
	uint32_t i_sm_ret                     : 1;
	uint32_t reserved_11_15               : 5;
	uint32_t o_sm_err                     : 1;
	uint32_t o_error                      : 1;
	uint32_t o_sm_ret                     : 1;
	uint32_t o_rtried                     : 1;
	uint32_t o_retry                      : 1;
	uint32_t reserved_21_23               : 3;
	uint32_t o_dgrad                      : 1;
	uint32_t o_fail                       : 1;
	uint32_t pkt_drop                     : 1;
	uint32_t reserved_27_31               : 5;
#endif
	} s;
	struct cvmx_sriomaintx_port_0_err_stat_s cn63xx;
	struct cvmx_sriomaintx_port_0_err_stat_s cn63xxp1;
	struct cvmx_sriomaintx_port_0_err_stat_s cn66xx;
};
typedef union cvmx_sriomaintx_port_0_err_stat cvmx_sriomaintx_port_0_err_stat_t;

/**
 * cvmx_sriomaint#_port_0_link_req
 *
 * SRIOMAINT_PORT_0_LINK_REQ = SRIO Port 0 Link Request
 *
 * Port 0 Manual Link Request
 *
 * Notes:
 * Writing this register generates the link request symbol or eight device reset symbols.   The
 *  progress of the request can be determined by reading SRIOMAINT(0,2..3)_PORT_0_LINK_RESP.  Only a single
 *  request should be generated at a time.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_0_LINK_REQ       hclk    hrst_n
 */
union cvmx_sriomaintx_port_0_link_req {
	uint32_t u32;
	struct cvmx_sriomaintx_port_0_link_req_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_3_31                : 29;
	uint32_t cmd                          : 3;  /**< Link Request Command.
                                                         011 - Reset Device
                                                         100 - Link Request
                                                         All other values reserved. */
#else
	uint32_t cmd                          : 3;
	uint32_t reserved_3_31                : 29;
#endif
	} s;
	struct cvmx_sriomaintx_port_0_link_req_s cn63xx;
	struct cvmx_sriomaintx_port_0_link_req_s cn66xx;
};
typedef union cvmx_sriomaintx_port_0_link_req cvmx_sriomaintx_port_0_link_req_t;

/**
 * cvmx_sriomaint#_port_0_link_resp
 *
 * SRIOMAINT_PORT_0_LINK_RESP = SRIO Port 0 Link Response
 *
 * Port 0 Manual Link Response
 *
 * Notes:
 * This register only returns responses generated by writes to SRIOMAINT(0,2..3)_PORT_0_LINK_REQ.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_0_LINK_RESP      hclk    hrst_n
 */
union cvmx_sriomaintx_port_0_link_resp {
	uint32_t u32;
	struct cvmx_sriomaintx_port_0_link_resp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t valid                        : 1;  /**< Link Response Valid.
                                                         1 = Link Response Received or Reset Device
                                                             Symbols Transmitted.  Value cleared on read.
                                                         0 = No response received. */
	uint32_t reserved_11_30               : 20;
	uint32_t ackid                        : 6;  /**< AckID received from link response.
                                                         Reset Device symbol response is always zero.
                                                         Bit 10 is used for IDLE2 and always reads zero. */
	uint32_t status                       : 5;  /**< Link Response Status.
                                                         Status supplied by link response.
                                                         Reset Device symbol response is always zero. */
#else
	uint32_t status                       : 5;
	uint32_t ackid                        : 6;
	uint32_t reserved_11_30               : 20;
	uint32_t valid                        : 1;
#endif
	} s;
	struct cvmx_sriomaintx_port_0_link_resp_s cn63xx;
	struct cvmx_sriomaintx_port_0_link_resp_s cn66xx;
};
typedef union cvmx_sriomaintx_port_0_link_resp cvmx_sriomaintx_port_0_link_resp_t;

/**
 * cvmx_sriomaint#_port_0_local_ackid
 *
 * SRIOMAINT_PORT_0_LOCAL_ACKID = SRIO Port 0 Local AckID
 *
 * Port 0 Local AckID Control
 *
 * Notes:
 * This register is typically only written when recovering from a failed link.  It may be read at any
 *  time the MAC is out of reset.  Writes to the O_ACKID field will be used for both the O_ACKID and
 *  E_ACKID.  Care must be taken to ensure that no packets are pending at the time of a write.  The
 *  number of pending packets can be read in the TX_INUSE field of SRIO(0,2..3)_MAC_BUFFERS.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_0_LOCAL_ACKID    hclk    hrst_n
 */
union cvmx_sriomaintx_port_0_local_ackid {
	uint32_t u32;
	struct cvmx_sriomaintx_port_0_local_ackid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_30_31               : 2;
	uint32_t i_ackid                      : 6;  /**< Next Expected Inbound AckID.
                                                         Bit 29 is used for IDLE2 and should be zero. */
	uint32_t reserved_14_23               : 10;
	uint32_t e_ackid                      : 6;  /**< Next Expected Unacknowledged AckID.
                                                         Bit 13 is used for IDLE2 and should be zero. */
	uint32_t reserved_6_7                 : 2;
	uint32_t o_ackid                      : 6;  /**< Next Outgoing Packet AckID.
                                                         Bit 5 is used for IDLE2 and should be zero. */
#else
	uint32_t o_ackid                      : 6;
	uint32_t reserved_6_7                 : 2;
	uint32_t e_ackid                      : 6;
	uint32_t reserved_14_23               : 10;
	uint32_t i_ackid                      : 6;
	uint32_t reserved_30_31               : 2;
#endif
	} s;
	struct cvmx_sriomaintx_port_0_local_ackid_s cn63xx;
	struct cvmx_sriomaintx_port_0_local_ackid_s cn66xx;
};
typedef union cvmx_sriomaintx_port_0_local_ackid cvmx_sriomaintx_port_0_local_ackid_t;

/**
 * cvmx_sriomaint#_port_gen_ctl
 *
 * SRIOMAINT_PORT_GEN_CTL = SRIO Port General Control
 *
 * Port General Control
 *
 * Notes:
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_GEN_CTL  hclk    hrst_n
 *
 */
union cvmx_sriomaintx_port_gen_ctl {
	uint32_t u32;
	struct cvmx_sriomaintx_port_gen_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t host                         : 1;  /**< Host Device.
                                                         The HOST reset value is based on corresponding
                                                         MIO_RST_CTL*[PRTMODE].  HOST resets to 1 when
                                                         this field selects RC (i.e. host) mode, else 0. */
	uint32_t menable                      : 1;  /**< Master Enable.  Must be set for device to issue
                                                         read, write, doorbell, message requests. */
	uint32_t discover                     : 1;  /**< Discovered. The device has been discovered by the
                                                         host responsible for initialization. */
	uint32_t reserved_0_28                : 29;
#else
	uint32_t reserved_0_28                : 29;
	uint32_t discover                     : 1;
	uint32_t menable                      : 1;
	uint32_t host                         : 1;
#endif
	} s;
	struct cvmx_sriomaintx_port_gen_ctl_s cn63xx;
	struct cvmx_sriomaintx_port_gen_ctl_s cn63xxp1;
	struct cvmx_sriomaintx_port_gen_ctl_s cn66xx;
};
typedef union cvmx_sriomaintx_port_gen_ctl cvmx_sriomaintx_port_gen_ctl_t;

/**
 * cvmx_sriomaint#_port_lt_ctl
 *
 * SRIOMAINT_PORT_LT_CTL = SRIO Link Layer Timeout Control
 *
 * Link Layer Timeout Control
 *
 * Notes:
 * This register controls the timeout for link layer transactions.  It is used as the timeout between
 *  sending a packet (of any type) or link request to receiving the corresponding link acknowledge or
 *  link-response.  Each count represents 200ns.  The minimum timeout period is the TIMEOUT x 200nS
 *  and the maximum is twice that number.  A value less than 32 may not guarantee that all timeout
 *  errors will be reported correctly.  When the timeout period expires the packet or link request is
 *  dropped and the error is logged in the LNK_TOUT field of the SRIOMAINT(0,2..3)_ERB_ERR_DET register.  A
 *  value of 0 in this register will allow the packet or link request to be issued but it will timeout
 *  immediately.  This value is not recommended for normal operation.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_LT_CTL   hclk    hrst_n
 */
union cvmx_sriomaintx_port_lt_ctl {
	uint32_t u32;
	struct cvmx_sriomaintx_port_lt_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timeout                      : 24; /**< Timeout Value */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t timeout                      : 24;
#endif
	} s;
	struct cvmx_sriomaintx_port_lt_ctl_s  cn63xx;
	struct cvmx_sriomaintx_port_lt_ctl_s  cn63xxp1;
	struct cvmx_sriomaintx_port_lt_ctl_s  cn66xx;
};
typedef union cvmx_sriomaintx_port_lt_ctl cvmx_sriomaintx_port_lt_ctl_t;

/**
 * cvmx_sriomaint#_port_mbh0
 *
 * SRIOMAINT_PORT_MBH0 = SRIO Port Maintenance Block Header 0
 *
 * Port Maintenance Block Header 0
 *
 * Notes:
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_MBH0     hclk    hrst_n
 *
 */
union cvmx_sriomaintx_port_mbh0 {
	uint32_t u32;
	struct cvmx_sriomaintx_port_mbh0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ef_ptr                       : 16; /**< Pointer to Error Management Block. */
	uint32_t ef_id                        : 16; /**< Extended Feature ID (Generic Endpoint Device) */
#else
	uint32_t ef_id                        : 16;
	uint32_t ef_ptr                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_port_mbh0_s    cn63xx;
	struct cvmx_sriomaintx_port_mbh0_s    cn63xxp1;
	struct cvmx_sriomaintx_port_mbh0_s    cn66xx;
};
typedef union cvmx_sriomaintx_port_mbh0 cvmx_sriomaintx_port_mbh0_t;

/**
 * cvmx_sriomaint#_port_rt_ctl
 *
 * SRIOMAINT_PORT_RT_CTL = SRIO Logical Layer Timeout Control
 *
 * Logical Layer Timeout Control
 *
 * Notes:
 * This register controls the timeout for logical layer transactions.  It is used under two
 *  conditions.  First, it is used as the timeout period between sending a packet requiring a packet
 *  response being sent to receiving the corresponding response.  This is used for all outgoing packet
 *  types including memory, maintenance, doorbells and message operations.  When the timeout period
 *  expires the packet is disgarded and the error is logged in the PKT_TOUT field of the
 *  SRIOMAINT(0,2..3)_ERB_LT_ERR_DET register.  The second use of this register is as a timeout period
 *  between incoming message segments of the same message.  If a message segment is received then the
 *  MSG_TOUT field of the SRIOMAINT(0,2..3)_ERB_LT_ERR_DET register is set if the next segment has not been
 *  received before the time expires.  In both cases, each count represents 200ns.  The minimum
 *  timeout period is the TIMEOUT x 200nS and the maximum is twice that number.  A value less than 32
 *  may not guarantee that all timeout errors will be reported correctly.  A value of 0 disables the
 *  logical layer timeouts and is not recommended for normal operation.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_RT_CTL   hclk    hrst_n
 */
union cvmx_sriomaintx_port_rt_ctl {
	uint32_t u32;
	struct cvmx_sriomaintx_port_rt_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timeout                      : 24; /**< Timeout Value */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t timeout                      : 24;
#endif
	} s;
	struct cvmx_sriomaintx_port_rt_ctl_s  cn63xx;
	struct cvmx_sriomaintx_port_rt_ctl_s  cn63xxp1;
	struct cvmx_sriomaintx_port_rt_ctl_s  cn66xx;
};
typedef union cvmx_sriomaintx_port_rt_ctl cvmx_sriomaintx_port_rt_ctl_t;

/**
 * cvmx_sriomaint#_port_ttl_ctl
 *
 * SRIOMAINT_PORT_TTL_CTL = SRIO Packet Time to Live Control
 *
 * Packet Time to Live
 *
 * Notes:
 * This register controls the timeout for outgoing packets.  It is used to make sure packets are
 *  being transmitted and acknowledged within a reasonable period of time.   The timeout value
 *  corresponds to TIMEOUT x 200ns and a value of 0 disables the timer.  The actualy value of the
 *  should be greater than the physical layer timout specified in SRIOMAINT(0,2..3)_PORT_LT_CTL and is
 *  typically a less SRIOMAINT(0,2..3)_PORT_LT_CTL timeout than the response timeout specified in
 *  SRIOMAINT(0,2..3)_PORT_RT_CTL.  A second application of this timer is to remove all the packets waiting
 *  to be transmitted including those already in flight.  This may necessary in the case of a link
 *  going down (see SRIO(0,2..3)_INT_REG.LINK_DWN).  This can accomplished by setting the TIMEOUT to small
 *  value all so that all TX packets can be dropped.  In either case, when the timeout expires the TTL
 *  interrupt is asserted, any packets currently being transmitted are dropped, the
 *  SRIOMAINT(0,2..3)_TX_DROP.DROP bit is set (causing any scheduled packets to be dropped), the
 *  SRIOMAINT(0,2..3)_TX_DROP.DROP_CNT is incremented for each packet and the SRIO output state is set to
 *  IDLE (all errors are cleared).  Software must clear the SRIOMAINT(0,2..3)_TX_DROP.DROP bit to resume
 *  transmitting packets.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PORT_RT_CTL   hclk    hrst_n
 */
union cvmx_sriomaintx_port_ttl_ctl {
	uint32_t u32;
	struct cvmx_sriomaintx_port_ttl_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timeout                      : 24; /**< Timeout Value */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t timeout                      : 24;
#endif
	} s;
	struct cvmx_sriomaintx_port_ttl_ctl_s cn63xx;
	struct cvmx_sriomaintx_port_ttl_ctl_s cn66xx;
};
typedef union cvmx_sriomaintx_port_ttl_ctl cvmx_sriomaintx_port_ttl_ctl_t;

/**
 * cvmx_sriomaint#_pri_dev_id
 *
 * SRIOMAINT_PRI_DEV_ID = SRIO Primary Device ID
 *
 * Primary 8 and 16 bit Device IDs
 *
 * Notes:
 * This register defines the primary 8 and 16 bit device IDs used for large and small transport.  An
 *  optional secondary set of device IDs are located in SRIOMAINT(0,2..3)_SEC_DEV_ID.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_PRI_DEV_ID    hclk    hrst_n
 */
union cvmx_sriomaintx_pri_dev_id {
	uint32_t u32;
	struct cvmx_sriomaintx_pri_dev_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t id8                          : 8;  /**< Primary 8-bit Device ID */
	uint32_t id16                         : 16; /**< Primary 16-bit Device ID */
#else
	uint32_t id16                         : 16;
	uint32_t id8                          : 8;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_sriomaintx_pri_dev_id_s   cn63xx;
	struct cvmx_sriomaintx_pri_dev_id_s   cn63xxp1;
	struct cvmx_sriomaintx_pri_dev_id_s   cn66xx;
};
typedef union cvmx_sriomaintx_pri_dev_id cvmx_sriomaintx_pri_dev_id_t;

/**
 * cvmx_sriomaint#_sec_dev_ctrl
 *
 * SRIOMAINT_SEC_DEV_CTRL = SRIO Secondary Device ID Control
 *
 * Control for Secondary Device IDs
 *
 * Notes:
 * This register enables the secondary 8 and 16 bit device IDs used for large and small transport.
 *  The corresponding secondary ID must be written before the ID is enabled.  The secondary IDs should
 *  not be enabled if the values of the primary and secondary IDs are identical.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_SEC_DEV_CTRL  hclk    hrst_n
 */
union cvmx_sriomaintx_sec_dev_ctrl {
	uint32_t u32;
	struct cvmx_sriomaintx_sec_dev_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t enable8                      : 1;  /**< Enable matches to secondary 8-bit Device ID */
	uint32_t enable16                     : 1;  /**< Enable matches to secondary 16-bit Device ID */
#else
	uint32_t enable16                     : 1;
	uint32_t enable8                      : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_sriomaintx_sec_dev_ctrl_s cn63xx;
	struct cvmx_sriomaintx_sec_dev_ctrl_s cn63xxp1;
	struct cvmx_sriomaintx_sec_dev_ctrl_s cn66xx;
};
typedef union cvmx_sriomaintx_sec_dev_ctrl cvmx_sriomaintx_sec_dev_ctrl_t;

/**
 * cvmx_sriomaint#_sec_dev_id
 *
 * SRIOMAINT_SEC_DEV_ID = SRIO Secondary Device ID
 *
 * Secondary 8 and 16 bit Device IDs
 *
 * Notes:
 * This register defines the secondary 8 and 16 bit device IDs used for large and small transport.
 *  The corresponding secondary ID must be written before the ID is enabled in the
 *  SRIOMAINT(0,2..3)_SEC_DEV_CTRL register.  The primary set of device IDs are located in
 *  SRIOMAINT(0,2..3)_PRI_DEV_ID register.  The secondary IDs should not be written to the same values as the
 *  corresponding primary IDs.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_SEC_DEV_ID    hclk    hrst_n
 */
union cvmx_sriomaintx_sec_dev_id {
	uint32_t u32;
	struct cvmx_sriomaintx_sec_dev_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t id8                          : 8;  /**< Secondary 8-bit Device ID */
	uint32_t id16                         : 16; /**< Secondary 16-bit Device ID */
#else
	uint32_t id16                         : 16;
	uint32_t id8                          : 8;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_sriomaintx_sec_dev_id_s   cn63xx;
	struct cvmx_sriomaintx_sec_dev_id_s   cn63xxp1;
	struct cvmx_sriomaintx_sec_dev_id_s   cn66xx;
};
typedef union cvmx_sriomaintx_sec_dev_id cvmx_sriomaintx_sec_dev_id_t;

/**
 * cvmx_sriomaint#_serial_lane_hdr
 *
 * SRIOMAINT_SERIAL_LANE_HDR = SRIO Serial Lane Header
 *
 * SRIO Serial Lane Header
 *
 * Notes:
 * The error management extensions block header register contains the EF_PTR to the next EF_BLK and
 *  the EF_ID that identifies this as the Serial Lane Status Block.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_SERIAL_LANE_HDR       hclk    hrst_n
 */
union cvmx_sriomaintx_serial_lane_hdr {
	uint32_t u32;
	struct cvmx_sriomaintx_serial_lane_hdr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ef_ptr                       : 16; /**< Pointer to the next block in the extended features
                                                         data structure. */
	uint32_t ef_id                        : 16;
#else
	uint32_t ef_id                        : 16;
	uint32_t ef_ptr                       : 16;
#endif
	} s;
	struct cvmx_sriomaintx_serial_lane_hdr_s cn63xx;
	struct cvmx_sriomaintx_serial_lane_hdr_s cn63xxp1;
	struct cvmx_sriomaintx_serial_lane_hdr_s cn66xx;
};
typedef union cvmx_sriomaintx_serial_lane_hdr cvmx_sriomaintx_serial_lane_hdr_t;

/**
 * cvmx_sriomaint#_src_ops
 *
 * SRIOMAINT_SRC_OPS = SRIO Source Operations
 *
 * The logical operations initiated by the Octeon.
 *
 * Notes:
 * The logical operations initiated by the Cores.   The Source OPs register shows the operations
 *  specified in the SRIO(0,2..3)_IP_FEATURE.OPS register.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_SRC_OPS       hclk    hrst_n
 */
union cvmx_sriomaintx_src_ops {
	uint32_t u32;
	struct cvmx_sriomaintx_src_ops_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t gsm_read                     : 1;  /**< PE does not support Read Home operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<31>] */
	uint32_t i_read                       : 1;  /**< PE does not support Instruction Read.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<30>] */
	uint32_t rd_own                       : 1;  /**< PE does not support Read for Ownership.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<29>] */
	uint32_t d_invald                     : 1;  /**< PE does not support Data Cache Invalidate.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<28>] */
	uint32_t castout                      : 1;  /**< PE does not support Castout Operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<27>] */
	uint32_t d_flush                      : 1;  /**< PE does not support Data Cache Flush.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<26>] */
	uint32_t io_read                      : 1;  /**< PE does not support IO Read.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<25>] */
	uint32_t i_invald                     : 1;  /**< PE does not support Instruction Cache Invalidate.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<24>] */
	uint32_t tlb_inv                      : 1;  /**< PE does not support TLB Entry Invalidate.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<23>] */
	uint32_t tlb_invs                     : 1;  /**< PE does not support TLB Entry Invalidate Sync.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<22>] */
	uint32_t reserved_16_21               : 6;
	uint32_t read                         : 1;  /**< PE can support Nread operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<15>] */
	uint32_t write                        : 1;  /**< PE can support Nwrite operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<14>] */
	uint32_t swrite                       : 1;  /**< PE can support Swrite operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<13>] */
	uint32_t write_r                      : 1;  /**< PE can support Write with Response operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<12>] */
	uint32_t msg                          : 1;  /**< PE can support Data Message operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<11>] */
	uint32_t doorbell                     : 1;  /**< PE can support Doorbell operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<10>] */
	uint32_t compswap                     : 1;  /**< PE does not support Atomic Compare and Swap.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<9>] */
	uint32_t testswap                     : 1;  /**< PE does not support Atomic Test and Swap.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<8>] */
	uint32_t atom_inc                     : 1;  /**< PE can support Atomic increment operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<7>] */
	uint32_t atom_dec                     : 1;  /**< PE can support Atomic decrement operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<6>] */
	uint32_t atom_set                     : 1;  /**< PE can support Atomic set operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<5>] */
	uint32_t atom_clr                     : 1;  /**< PE can support Atomic clear operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<4>] */
	uint32_t atom_swp                     : 1;  /**< PE does not support Atomic Swap.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<3>] */
	uint32_t port_wr                      : 1;  /**< PE can Port Write operations.
                                                         This is a RO copy of SRIO*_IP_FEATURE[OPS<2>] */
	uint32_t reserved_0_1                 : 2;
#else
	uint32_t reserved_0_1                 : 2;
	uint32_t port_wr                      : 1;
	uint32_t atom_swp                     : 1;
	uint32_t atom_clr                     : 1;
	uint32_t atom_set                     : 1;
	uint32_t atom_dec                     : 1;
	uint32_t atom_inc                     : 1;
	uint32_t testswap                     : 1;
	uint32_t compswap                     : 1;
	uint32_t doorbell                     : 1;
	uint32_t msg                          : 1;
	uint32_t write_r                      : 1;
	uint32_t swrite                       : 1;
	uint32_t write                        : 1;
	uint32_t read                         : 1;
	uint32_t reserved_16_21               : 6;
	uint32_t tlb_invs                     : 1;
	uint32_t tlb_inv                      : 1;
	uint32_t i_invald                     : 1;
	uint32_t io_read                      : 1;
	uint32_t d_flush                      : 1;
	uint32_t castout                      : 1;
	uint32_t d_invald                     : 1;
	uint32_t rd_own                       : 1;
	uint32_t i_read                       : 1;
	uint32_t gsm_read                     : 1;
#endif
	} s;
	struct cvmx_sriomaintx_src_ops_s      cn63xx;
	struct cvmx_sriomaintx_src_ops_s      cn63xxp1;
	struct cvmx_sriomaintx_src_ops_s      cn66xx;
};
typedef union cvmx_sriomaintx_src_ops cvmx_sriomaintx_src_ops_t;

/**
 * cvmx_sriomaint#_tx_drop
 *
 * SRIOMAINT_TX_DROP = SRIO MAC Outgoing Packet Drop
 *
 * Outging SRIO Packet Drop Control/Status
 *
 * Notes:
 * This register controls and provides status for dropping outgoing SRIO packets.  The DROP bit
 *  should only be cleared when no packets are currently being dropped.  This can be guaranteed by
 *  clearing the SRIOMAINT(0,2..3)_PORT_0_CTL.O_ENABLE bit before changing the DROP bit and restoring the
 *  O_ENABLE afterwards.
 *
 * Clk_Rst:        SRIOMAINT(0,2..3)_MAC_CTRL      hclk    hrst_n
 */
union cvmx_sriomaintx_tx_drop {
	uint32_t u32;
	struct cvmx_sriomaintx_tx_drop_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_17_31               : 15;
	uint32_t drop                         : 1;  /**< All outgoing packets are dropped.  Any packets
                                                         requiring a response will return 1's after the
                                                         SRIOMAINT(0,2..3)_PORT_RT_CTL Timeout expires.  This bit
                                                         is set automatically when the TTL Timeout occurs
                                                         or can be set by software and must always be
                                                         cleared by software. */
	uint32_t drop_cnt                     : 16; /**< Number of packets dropped by transmit logic.
                                                         Packets are dropped whenever a packet is ready to
                                                         be transmitted and a TTL Timeouts occur, the  DROP
                                                         bit is set or the SRIOMAINT(0,2..3)_ERB_ERR_RATE_THR
                                                         FAIL_TH has been reached and the DROP_PKT bit is
                                                         set in SRIOMAINT(0,2..3)_PORT_0_CTL.  This counter wraps
                                                         on overflow and is cleared only on reset. */
#else
	uint32_t drop_cnt                     : 16;
	uint32_t drop                         : 1;
	uint32_t reserved_17_31               : 15;
#endif
	} s;
	struct cvmx_sriomaintx_tx_drop_s      cn63xx;
	struct cvmx_sriomaintx_tx_drop_s      cn66xx;
};
typedef union cvmx_sriomaintx_tx_drop cvmx_sriomaintx_tx_drop_t;

#endif
