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
 * cvmx-zip-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon zip.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_ZIP_DEFS_H__
#define __CVMX_ZIP_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_CMD_BIST_RESULT CVMX_ZIP_CMD_BIST_RESULT_FUNC()
static inline uint64_t CVMX_ZIP_CMD_BIST_RESULT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_CMD_BIST_RESULT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000080ull);
}
#else
#define CVMX_ZIP_CMD_BIST_RESULT (CVMX_ADD_IO_SEG(0x0001180038000080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_CMD_BUF CVMX_ZIP_CMD_BUF_FUNC()
static inline uint64_t CVMX_ZIP_CMD_BUF_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_CMD_BUF not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000008ull);
}
#else
#define CVMX_ZIP_CMD_BUF (CVMX_ADD_IO_SEG(0x0001180038000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_CMD_CTL CVMX_ZIP_CMD_CTL_FUNC()
static inline uint64_t CVMX_ZIP_CMD_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_CMD_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000000ull);
}
#else
#define CVMX_ZIP_CMD_CTL (CVMX_ADD_IO_SEG(0x0001180038000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_CONSTANTS CVMX_ZIP_CONSTANTS_FUNC()
static inline uint64_t CVMX_ZIP_CONSTANTS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_CONSTANTS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800380000A0ull);
}
#else
#define CVMX_ZIP_CONSTANTS (CVMX_ADD_IO_SEG(0x00011800380000A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_COREX_BIST_STATUS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_COREX_BIST_STATUS(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000520ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_COREX_BIST_STATUS(offset) (CVMX_ADD_IO_SEG(0x0001180038000520ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_CTL_BIST_STATUS CVMX_ZIP_CTL_BIST_STATUS_FUNC()
static inline uint64_t CVMX_ZIP_CTL_BIST_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_CTL_BIST_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000510ull);
}
#else
#define CVMX_ZIP_CTL_BIST_STATUS (CVMX_ADD_IO_SEG(0x0001180038000510ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_CTL_CFG CVMX_ZIP_CTL_CFG_FUNC()
static inline uint64_t CVMX_ZIP_CTL_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_CTL_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000560ull);
}
#else
#define CVMX_ZIP_CTL_CFG (CVMX_ADD_IO_SEG(0x0001180038000560ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_DBG_COREX_INST(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_DBG_COREX_INST(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000640ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_DBG_COREX_INST(offset) (CVMX_ADD_IO_SEG(0x0001180038000640ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_DBG_COREX_STA(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_DBG_COREX_STA(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000680ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_DBG_COREX_STA(offset) (CVMX_ADD_IO_SEG(0x0001180038000680ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_DBG_QUEX_STA(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_DBG_QUEX_STA(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000600ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_DBG_QUEX_STA(offset) (CVMX_ADD_IO_SEG(0x0001180038000600ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_DEBUG0 CVMX_ZIP_DEBUG0_FUNC()
static inline uint64_t CVMX_ZIP_DEBUG0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_DEBUG0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000098ull);
}
#else
#define CVMX_ZIP_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180038000098ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_ECC_CTL CVMX_ZIP_ECC_CTL_FUNC()
static inline uint64_t CVMX_ZIP_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000568ull);
}
#else
#define CVMX_ZIP_ECC_CTL (CVMX_ADD_IO_SEG(0x0001180038000568ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_ERROR CVMX_ZIP_ERROR_FUNC()
static inline uint64_t CVMX_ZIP_ERROR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_ERROR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000088ull);
}
#else
#define CVMX_ZIP_ERROR (CVMX_ADD_IO_SEG(0x0001180038000088ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_INT_ENA CVMX_ZIP_INT_ENA_FUNC()
static inline uint64_t CVMX_ZIP_INT_ENA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_INT_ENA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000580ull);
}
#else
#define CVMX_ZIP_INT_ENA (CVMX_ADD_IO_SEG(0x0001180038000580ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_INT_MASK CVMX_ZIP_INT_MASK_FUNC()
static inline uint64_t CVMX_ZIP_INT_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_INT_MASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000090ull);
}
#else
#define CVMX_ZIP_INT_MASK (CVMX_ADD_IO_SEG(0x0001180038000090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_INT_REG CVMX_ZIP_INT_REG_FUNC()
static inline uint64_t CVMX_ZIP_INT_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_INT_REG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000570ull);
}
#else
#define CVMX_ZIP_INT_REG (CVMX_ADD_IO_SEG(0x0001180038000570ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_QUEX_BUF(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_QUEX_BUF(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000100ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_QUEX_BUF(offset) (CVMX_ADD_IO_SEG(0x0001180038000100ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_QUEX_ECC_ERR_STA(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_QUEX_ECC_ERR_STA(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000590ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_QUEX_ECC_ERR_STA(offset) (CVMX_ADD_IO_SEG(0x0001180038000590ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ZIP_QUEX_MAP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ZIP_QUEX_MAP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180038000300ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ZIP_QUEX_MAP(offset) (CVMX_ADD_IO_SEG(0x0001180038000300ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_QUE_ENA CVMX_ZIP_QUE_ENA_FUNC()
static inline uint64_t CVMX_ZIP_QUE_ENA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_QUE_ENA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000500ull);
}
#else
#define CVMX_ZIP_QUE_ENA (CVMX_ADD_IO_SEG(0x0001180038000500ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_QUE_PRI CVMX_ZIP_QUE_PRI_FUNC()
static inline uint64_t CVMX_ZIP_QUE_PRI_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_QUE_PRI not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000508ull);
}
#else
#define CVMX_ZIP_QUE_PRI (CVMX_ADD_IO_SEG(0x0001180038000508ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ZIP_THROTTLE CVMX_ZIP_THROTTLE_FUNC()
static inline uint64_t CVMX_ZIP_THROTTLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ZIP_THROTTLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180038000010ull);
}
#else
#define CVMX_ZIP_THROTTLE (CVMX_ADD_IO_SEG(0x0001180038000010ull))
#endif

/**
 * cvmx_zip_cmd_bist_result
 *
 * ZIP_CMD_BIST_RESULT =  ZIP Command BIST Result Register
 *
 * Description:
 * This register is a reformatted register with same fields as O63 2.x.
 * The purpose of this register is for software backward compatibility.
 * Some bits are the bist result of combined status of memories (per bit, 0=pass and 1=fail).
 */
union cvmx_zip_cmd_bist_result {
	uint64_t u64;
	struct cvmx_zip_cmd_bist_result_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_57_63               : 7;
	uint64_t zip_core                     : 53; /**< BiST result of the ZIP_CORE memories */
	uint64_t zip_ctl                      : 4;  /**< BiST result of the ZIP_CTL  memories */
#else
	uint64_t zip_ctl                      : 4;
	uint64_t zip_core                     : 53;
	uint64_t reserved_57_63               : 7;
#endif
	} s;
	struct cvmx_zip_cmd_bist_result_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t zip_core                     : 27; /**< BiST result of the ZIP_CORE memories */
	uint64_t zip_ctl                      : 4;  /**< BiST result of the ZIP_CTL  memories */
#else
	uint64_t zip_ctl                      : 4;
	uint64_t zip_core                     : 27;
	uint64_t reserved_31_63               : 33;
#endif
	} cn31xx;
	struct cvmx_zip_cmd_bist_result_cn31xx cn38xx;
	struct cvmx_zip_cmd_bist_result_cn31xx cn38xxp2;
	struct cvmx_zip_cmd_bist_result_cn31xx cn56xx;
	struct cvmx_zip_cmd_bist_result_cn31xx cn56xxp1;
	struct cvmx_zip_cmd_bist_result_cn31xx cn58xx;
	struct cvmx_zip_cmd_bist_result_cn31xx cn58xxp1;
	struct cvmx_zip_cmd_bist_result_s     cn61xx;
	struct cvmx_zip_cmd_bist_result_s     cn63xx;
	struct cvmx_zip_cmd_bist_result_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_43_63               : 21;
	uint64_t zip_core                     : 39; /**< BiST result of the ZIP_CORE memories */
	uint64_t zip_ctl                      : 4;  /**< BiST result of the ZIP_CTL  memories */
#else
	uint64_t zip_ctl                      : 4;
	uint64_t zip_core                     : 39;
	uint64_t reserved_43_63               : 21;
#endif
	} cn63xxp1;
	struct cvmx_zip_cmd_bist_result_s     cn66xx;
	struct cvmx_zip_cmd_bist_result_s     cn68xx;
	struct cvmx_zip_cmd_bist_result_s     cn68xxp1;
};
typedef union cvmx_zip_cmd_bist_result cvmx_zip_cmd_bist_result_t;

/**
 * cvmx_zip_cmd_buf
 *
 * ZIP_CMD_BUF =  ZIP Command Buffer Parameter Register
 *
 * Description:
 * This is an alias to ZIP_QUE0_BUF. The purpose of this register is for software backward compatibility.
 * This register set the buffer parameters for the instruction queue 0.
 */
union cvmx_zip_cmd_buf {
	uint64_t u64;
	struct cvmx_zip_cmd_buf_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t dwb                          : 9;  /**< Number of DontWriteBacks */
	uint64_t pool                         : 3;  /**< Free list used to free command buffer segments */
	uint64_t size                         : 13; /**< Number of uint64s per command buffer segment */
	uint64_t ptr                          : 33; /**< Initial command buffer pointer[39:7] (128B-aligned) */
#else
	uint64_t ptr                          : 33;
	uint64_t size                         : 13;
	uint64_t pool                         : 3;
	uint64_t dwb                          : 9;
	uint64_t reserved_58_63               : 6;
#endif
	} s;
	struct cvmx_zip_cmd_buf_s             cn31xx;
	struct cvmx_zip_cmd_buf_s             cn38xx;
	struct cvmx_zip_cmd_buf_s             cn38xxp2;
	struct cvmx_zip_cmd_buf_s             cn56xx;
	struct cvmx_zip_cmd_buf_s             cn56xxp1;
	struct cvmx_zip_cmd_buf_s             cn58xx;
	struct cvmx_zip_cmd_buf_s             cn58xxp1;
	struct cvmx_zip_cmd_buf_s             cn61xx;
	struct cvmx_zip_cmd_buf_s             cn63xx;
	struct cvmx_zip_cmd_buf_s             cn63xxp1;
	struct cvmx_zip_cmd_buf_s             cn66xx;
	struct cvmx_zip_cmd_buf_s             cn68xx;
	struct cvmx_zip_cmd_buf_s             cn68xxp1;
};
typedef union cvmx_zip_cmd_buf cvmx_zip_cmd_buf_t;

/**
 * cvmx_zip_cmd_ctl
 *
 * ZIP_CMD_CTL = ZIP Clock/Reset Control Register
 *
 * Description:
 *       This register controls clock and reset.
 */
union cvmx_zip_cmd_ctl {
	uint64_t u64;
	struct cvmx_zip_cmd_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t forceclk                     : 1;  /**< Force zip_ctl__zip<0|1>_clock_on_b == 1 when set */
	uint64_t reset                        : 1;  /**< Reset one-shot pulse for zip cores */
#else
	uint64_t reset                        : 1;
	uint64_t forceclk                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_zip_cmd_ctl_s             cn31xx;
	struct cvmx_zip_cmd_ctl_s             cn38xx;
	struct cvmx_zip_cmd_ctl_s             cn38xxp2;
	struct cvmx_zip_cmd_ctl_s             cn56xx;
	struct cvmx_zip_cmd_ctl_s             cn56xxp1;
	struct cvmx_zip_cmd_ctl_s             cn58xx;
	struct cvmx_zip_cmd_ctl_s             cn58xxp1;
	struct cvmx_zip_cmd_ctl_s             cn61xx;
	struct cvmx_zip_cmd_ctl_s             cn63xx;
	struct cvmx_zip_cmd_ctl_s             cn63xxp1;
	struct cvmx_zip_cmd_ctl_s             cn66xx;
	struct cvmx_zip_cmd_ctl_s             cn68xx;
	struct cvmx_zip_cmd_ctl_s             cn68xxp1;
};
typedef union cvmx_zip_cmd_ctl cvmx_zip_cmd_ctl_t;

/**
 * cvmx_zip_constants
 *
 * ZIP_CONSTANTS =  ZIP Constants Register
 *
 * Description:
 *   This contains all the current implementation related parameters of the zip core in this chip.
 */
union cvmx_zip_constants {
	uint64_t u64;
	struct cvmx_zip_constants_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t nexec                        : 8;  /**< Number of available ZIP Exec Units */
	uint64_t reserved_49_55               : 7;
	uint64_t syncflush_capable            : 1;  /**< 1: SYNCFLUSH is supported
                                                         - 0: SYNCFLUSH is not supported.
                                                          Note: for O68 1.0, SYNCFLUSH is supported
                                                                although this field is 0. */
	uint64_t depth                        : 16; /**< Maximum search depth for compression */
	uint64_t onfsize                      : 12; /**< Output near full threshold in bytes */
	uint64_t ctxsize                      : 12; /**< Decompression Context size in bytes */
	uint64_t reserved_1_7                 : 7;
	uint64_t disabled                     : 1;  /**< 1=zip is disabled, 0=zip is enabled */
#else
	uint64_t disabled                     : 1;
	uint64_t reserved_1_7                 : 7;
	uint64_t ctxsize                      : 12;
	uint64_t onfsize                      : 12;
	uint64_t depth                        : 16;
	uint64_t syncflush_capable            : 1;
	uint64_t reserved_49_55               : 7;
	uint64_t nexec                        : 8;
#endif
	} s;
	struct cvmx_zip_constants_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t depth                        : 16; /**< Maximum search depth for compression */
	uint64_t onfsize                      : 12; /**< Output near full threshhold in bytes */
	uint64_t ctxsize                      : 12; /**< Context size in bytes */
	uint64_t reserved_1_7                 : 7;
	uint64_t disabled                     : 1;  /**< 1=zip unit isdisabled, 0=zip unit not disabled */
#else
	uint64_t disabled                     : 1;
	uint64_t reserved_1_7                 : 7;
	uint64_t ctxsize                      : 12;
	uint64_t onfsize                      : 12;
	uint64_t depth                        : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} cn31xx;
	struct cvmx_zip_constants_cn31xx      cn38xx;
	struct cvmx_zip_constants_cn31xx      cn38xxp2;
	struct cvmx_zip_constants_cn31xx      cn56xx;
	struct cvmx_zip_constants_cn31xx      cn56xxp1;
	struct cvmx_zip_constants_cn31xx      cn58xx;
	struct cvmx_zip_constants_cn31xx      cn58xxp1;
	struct cvmx_zip_constants_s           cn61xx;
	struct cvmx_zip_constants_cn31xx      cn63xx;
	struct cvmx_zip_constants_cn31xx      cn63xxp1;
	struct cvmx_zip_constants_s           cn66xx;
	struct cvmx_zip_constants_s           cn68xx;
	struct cvmx_zip_constants_cn31xx      cn68xxp1;
};
typedef union cvmx_zip_constants cvmx_zip_constants_t;

/**
 * cvmx_zip_core#_bist_status
 *
 * ZIP_CORE_BIST_STATUS =  ZIP CORE Bist Status Registers
 *
 * Description:
 *   Those register have the bist status of memories in zip cores.
 *   Each bit is the bist result of an individual memory (per bit, 0=pass and 1=fail).
 */
union cvmx_zip_corex_bist_status {
	uint64_t u64;
	struct cvmx_zip_corex_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_53_63               : 11;
	uint64_t bstatus                      : 53; /**< BIST result of the ZIP_CORE memories */
#else
	uint64_t bstatus                      : 53;
	uint64_t reserved_53_63               : 11;
#endif
	} s;
	struct cvmx_zip_corex_bist_status_s   cn68xx;
	struct cvmx_zip_corex_bist_status_s   cn68xxp1;
};
typedef union cvmx_zip_corex_bist_status cvmx_zip_corex_bist_status_t;

/**
 * cvmx_zip_ctl_bist_status
 *
 * ZIP_CTL_BIST_STATUS =  ZIP CONTROL Bist Status Register
 *
 * Description:
 *   This register has the bist status of memories in zip_ctl (Instruction Buffer, G/S Pointer Fifo, Input Data Buffer,
 *   Output Data Buffers).
 *   Each bit is the bist result of an individual memory (per bit, 0=pass and 1=fail).
 */
union cvmx_zip_ctl_bist_status {
	uint64_t u64;
	struct cvmx_zip_ctl_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t bstatus                      : 7;  /**< BIST result of the memories */
#else
	uint64_t bstatus                      : 7;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_zip_ctl_bist_status_s     cn68xx;
	struct cvmx_zip_ctl_bist_status_s     cn68xxp1;
};
typedef union cvmx_zip_ctl_bist_status cvmx_zip_ctl_bist_status_t;

/**
 * cvmx_zip_ctl_cfg
 *
 * ZIP_CTL_CFG =  ZIP Controller Configuration Register
 *
 * Description:
 * This register controls the behavior zip dma engine. It is recommended to kept those field in the default values for normal
 * operation. Changing the values of the fields may be useful for diagnostics.
 */
union cvmx_zip_ctl_cfg {
	uint64_t u64;
	struct cvmx_zip_ctl_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t ildf                         : 3;  /**< Instruction Load Command FIFO Credits <= 4 */
	uint64_t reserved_22_23               : 2;
	uint64_t iprf                         : 2;  /**< Instruction Page Return Cmd FIFO Credits <= 2 */
	uint64_t reserved_19_19               : 1;
	uint64_t gstf                         : 3;  /**< G/S Tag FIFO Credits <= 4 */
	uint64_t reserved_15_15               : 1;
	uint64_t stcf                         : 3;  /**< Store Command FIFO Credits <= 4 */
	uint64_t reserved_11_11               : 1;
	uint64_t ldf                          : 3;  /**< Load Cmd FIFO Credits <= 4 */
	uint64_t reserved_6_7                 : 2;
	uint64_t wkqf                         : 2;  /**< WorkQueue FIFO Credits <= 2 */
	uint64_t reserved_2_3                 : 2;
	uint64_t busy                         : 1;  /**< 1: ZIP system is busy; 0: ZIP system is idle. */
	uint64_t lmod                         : 1;  /**< Legacy Mode. */
#else
	uint64_t lmod                         : 1;
	uint64_t busy                         : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t wkqf                         : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t ldf                          : 3;
	uint64_t reserved_11_11               : 1;
	uint64_t stcf                         : 3;
	uint64_t reserved_15_15               : 1;
	uint64_t gstf                         : 3;
	uint64_t reserved_19_19               : 1;
	uint64_t iprf                         : 2;
	uint64_t reserved_22_23               : 2;
	uint64_t ildf                         : 3;
	uint64_t reserved_27_63               : 37;
#endif
	} s;
	struct cvmx_zip_ctl_cfg_s             cn68xx;
	struct cvmx_zip_ctl_cfg_s             cn68xxp1;
};
typedef union cvmx_zip_ctl_cfg cvmx_zip_ctl_cfg_t;

/**
 * cvmx_zip_dbg_core#_inst
 *
 * ZIP_DBG_COREX_INST =  ZIP Core Current Instruction Registers
 *
 * Description:
 * This register reflects the status of the current instruction that zip core is executing/ has executed.
 * This register is only for debug use.
 */
union cvmx_zip_dbg_corex_inst {
	uint64_t u64;
	struct cvmx_zip_dbg_corex_inst_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t busy                         : 1;  /**< Core State: 1 - Core is busy; 0 - Core is idle */
	uint64_t reserved_33_62               : 30;
	uint64_t qid                          : 1;  /**< Queue Index of instruction executed (BUSY=0) or
                                                         being executed (BUSY=1) on this core */
	uint64_t iid                          : 32; /**< Instruction Index executed (BUSY=0) or being
                                                         executed (BUSY=1) on this core */
#else
	uint64_t iid                          : 32;
	uint64_t qid                          : 1;
	uint64_t reserved_33_62               : 30;
	uint64_t busy                         : 1;
#endif
	} s;
	struct cvmx_zip_dbg_corex_inst_s      cn68xx;
	struct cvmx_zip_dbg_corex_inst_s      cn68xxp1;
};
typedef union cvmx_zip_dbg_corex_inst cvmx_zip_dbg_corex_inst_t;

/**
 * cvmx_zip_dbg_core#_sta
 *
 * ZIP_DBG_COREX_STA =  ZIP Core Status Registers
 *
 * Description:
 * These register reflect the status of the zip cores.
 * This register is only for debug use.
 */
union cvmx_zip_dbg_corex_sta {
	uint64_t u64;
	struct cvmx_zip_dbg_corex_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t busy                         : 1;  /**< Core State: 1 - Core is busy; 0 - Core is idle */
	uint64_t reserved_37_62               : 26;
	uint64_t ist                          : 5;  /**< State of current instruction is executing */
	uint64_t nie                          : 32; /**< Number of instructions executed on this core */
#else
	uint64_t nie                          : 32;
	uint64_t ist                          : 5;
	uint64_t reserved_37_62               : 26;
	uint64_t busy                         : 1;
#endif
	} s;
	struct cvmx_zip_dbg_corex_sta_s       cn68xx;
	struct cvmx_zip_dbg_corex_sta_s       cn68xxp1;
};
typedef union cvmx_zip_dbg_corex_sta cvmx_zip_dbg_corex_sta_t;

/**
 * cvmx_zip_dbg_que#_sta
 *
 * ZIP_DBG_QUEX_STA =  ZIP Queue Status Registers
 *
 * Description:
 * This register reflects status of the zip instruction queue.
 * This register is only for debug use.
 */
union cvmx_zip_dbg_quex_sta {
	uint64_t u64;
	struct cvmx_zip_dbg_quex_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t busy                         : 1;  /**< Queue State: 1 - Queue is busy; 0 - Queue is idle */
	uint64_t reserved_52_62               : 11;
	uint64_t cdbc                         : 20; /**< Current DoorBell Counter */
	uint64_t nii                          : 32; /**< Number of instructions issued from this queue.
                                                         Reset to 0 when ZIP_QUEn_BUF is written. */
#else
	uint64_t nii                          : 32;
	uint64_t cdbc                         : 20;
	uint64_t reserved_52_62               : 11;
	uint64_t busy                         : 1;
#endif
	} s;
	struct cvmx_zip_dbg_quex_sta_s        cn68xx;
	struct cvmx_zip_dbg_quex_sta_s        cn68xxp1;
};
typedef union cvmx_zip_dbg_quex_sta cvmx_zip_dbg_quex_sta_t;

/**
 * cvmx_zip_debug0
 *
 * ZIP_DEBUG0 =  ZIP DEBUG Register
 *
 * Description:
 */
union cvmx_zip_debug0 {
	uint64_t u64;
	struct cvmx_zip_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_30_63               : 34;
	uint64_t asserts                      : 30; /**< FIFO assertion checks */
#else
	uint64_t asserts                      : 30;
	uint64_t reserved_30_63               : 34;
#endif
	} s;
	struct cvmx_zip_debug0_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t asserts                      : 14; /**< FIFO assertion checks */
#else
	uint64_t asserts                      : 14;
	uint64_t reserved_14_63               : 50;
#endif
	} cn31xx;
	struct cvmx_zip_debug0_cn31xx         cn38xx;
	struct cvmx_zip_debug0_cn31xx         cn38xxp2;
	struct cvmx_zip_debug0_cn31xx         cn56xx;
	struct cvmx_zip_debug0_cn31xx         cn56xxp1;
	struct cvmx_zip_debug0_cn31xx         cn58xx;
	struct cvmx_zip_debug0_cn31xx         cn58xxp1;
	struct cvmx_zip_debug0_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t asserts                      : 17; /**< FIFO assertion checks */
#else
	uint64_t asserts                      : 17;
	uint64_t reserved_17_63               : 47;
#endif
	} cn61xx;
	struct cvmx_zip_debug0_cn61xx         cn63xx;
	struct cvmx_zip_debug0_cn61xx         cn63xxp1;
	struct cvmx_zip_debug0_cn61xx         cn66xx;
	struct cvmx_zip_debug0_s              cn68xx;
	struct cvmx_zip_debug0_s              cn68xxp1;
};
typedef union cvmx_zip_debug0 cvmx_zip_debug0_t;

/**
 * cvmx_zip_ecc_ctl
 *
 * ZIP_ECC_CTL =  ZIP ECC Control Register
 *
 * Description:
 *   This register enables ECC for each individual internal memory that requires ECC. For debug purpose, it can also
 *   control 1 or 2 bits be flipped in the ECC data.
 */
union cvmx_zip_ecc_ctl {
	uint64_t u64;
	struct cvmx_zip_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ibge                         : 2;  /**< controls instruction buffer flip syndrome
                                                         2'b00       : No Error Generation
                                                         2'b10, 2'b01: Flip 1 bit
                                                         2'b11       : Flip 2 bits */
	uint64_t reserved_1_31                : 31;
	uint64_t iben                         : 1;  /**< 1: ECC Enabled for instruction buffer
                                                         - 0: ECC Disabled for instruction buffer */
#else
	uint64_t iben                         : 1;
	uint64_t reserved_1_31                : 31;
	uint64_t ibge                         : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_zip_ecc_ctl_s             cn68xx;
	struct cvmx_zip_ecc_ctl_s             cn68xxp1;
};
typedef union cvmx_zip_ecc_ctl cvmx_zip_ecc_ctl_t;

/**
 * cvmx_zip_error
 *
 * ZIP_ERROR =  ZIP ERROR Register
 *
 * Description:
 * This register is an alias to ZIP_INT_REG[DOORBELL0].
 * The purpose of this register is for software backward compatibility.
 */
union cvmx_zip_error {
	uint64_t u64;
	struct cvmx_zip_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t doorbell                     : 1;  /**< A doorbell count has overflowed */
#else
	uint64_t doorbell                     : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_zip_error_s               cn31xx;
	struct cvmx_zip_error_s               cn38xx;
	struct cvmx_zip_error_s               cn38xxp2;
	struct cvmx_zip_error_s               cn56xx;
	struct cvmx_zip_error_s               cn56xxp1;
	struct cvmx_zip_error_s               cn58xx;
	struct cvmx_zip_error_s               cn58xxp1;
	struct cvmx_zip_error_s               cn61xx;
	struct cvmx_zip_error_s               cn63xx;
	struct cvmx_zip_error_s               cn63xxp1;
	struct cvmx_zip_error_s               cn66xx;
	struct cvmx_zip_error_s               cn68xx;
	struct cvmx_zip_error_s               cn68xxp1;
};
typedef union cvmx_zip_error cvmx_zip_error_t;

/**
 * cvmx_zip_int_ena
 *
 * ZIP_INT_ENA =  ZIP Interrupt Enable Register
 *
 * Description:
 *   Only when an interrupt source is enabled, an interrupt can be fired.
 *   When a bit is set to 1, the corresponding interrupt is enabled.
 */
union cvmx_zip_int_ena {
	uint64_t u64;
	struct cvmx_zip_int_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t doorbell1                    : 1;  /**< Enable for Doorbell 1 count overflow */
	uint64_t doorbell0                    : 1;  /**< Enable for Doorbell 0 count overflow */
	uint64_t reserved_3_7                 : 5;
	uint64_t ibdbe                        : 1;  /**< Enable for IBUF Double Bit Error */
	uint64_t ibsbe                        : 1;  /**< Enable for IBUF Single Bit Error */
	uint64_t fife                         : 1;  /**< Enable for FIFO errors */
#else
	uint64_t fife                         : 1;
	uint64_t ibsbe                        : 1;
	uint64_t ibdbe                        : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t doorbell0                    : 1;
	uint64_t doorbell1                    : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_zip_int_ena_s             cn68xx;
	struct cvmx_zip_int_ena_s             cn68xxp1;
};
typedef union cvmx_zip_int_ena cvmx_zip_int_ena_t;

/**
 * cvmx_zip_int_mask
 *
 * ZIP_INT_MASK =  ZIP Interrupt Mask Register
 *
 * Description:
 * This register is an alias to ZIP_INT_ENA[DOORBELL0].
 * The purpose of this register is for software backward compatibility.
 */
union cvmx_zip_int_mask {
	uint64_t u64;
	struct cvmx_zip_int_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t doorbell                     : 1;  /**< Bit mask corresponding to ZIP_ERROR[0] above */
#else
	uint64_t doorbell                     : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_zip_int_mask_s            cn31xx;
	struct cvmx_zip_int_mask_s            cn38xx;
	struct cvmx_zip_int_mask_s            cn38xxp2;
	struct cvmx_zip_int_mask_s            cn56xx;
	struct cvmx_zip_int_mask_s            cn56xxp1;
	struct cvmx_zip_int_mask_s            cn58xx;
	struct cvmx_zip_int_mask_s            cn58xxp1;
	struct cvmx_zip_int_mask_s            cn61xx;
	struct cvmx_zip_int_mask_s            cn63xx;
	struct cvmx_zip_int_mask_s            cn63xxp1;
	struct cvmx_zip_int_mask_s            cn66xx;
	struct cvmx_zip_int_mask_s            cn68xx;
	struct cvmx_zip_int_mask_s            cn68xxp1;
};
typedef union cvmx_zip_int_mask cvmx_zip_int_mask_t;

/**
 * cvmx_zip_int_reg
 *
 * ZIP_INT_REG =  ZIP Interrupt Status Register
 *
 * Description:
 *   This registers contains the status of all the interrupt source. An interrupt will be generated only when
 *   the corresponding interrupt source is enabled in ZIP_INT_ENA.
 */
union cvmx_zip_int_reg {
	uint64_t u64;
	struct cvmx_zip_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t doorbell1                    : 1;  /**< Doorbell 1 count has overflowed */
	uint64_t doorbell0                    : 1;  /**< Doorbell 0 count has overflowed */
	uint64_t reserved_3_7                 : 5;
	uint64_t ibdbe                        : 1;  /**< IBUF Double Bit Error */
	uint64_t ibsbe                        : 1;  /**< IBUF Single Bit Error */
	uint64_t fife                         : 1;  /**< FIFO errors and the detailed status is in
                                                         ZIP_DEBUG0 */
#else
	uint64_t fife                         : 1;
	uint64_t ibsbe                        : 1;
	uint64_t ibdbe                        : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t doorbell0                    : 1;
	uint64_t doorbell1                    : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_zip_int_reg_s             cn68xx;
	struct cvmx_zip_int_reg_s             cn68xxp1;
};
typedef union cvmx_zip_int_reg cvmx_zip_int_reg_t;

/**
 * cvmx_zip_que#_buf
 *
 * NOTE: Fields NEXEC and SYNCFLUSH_CAPABLE are only valid for chips after O68 2.0 (including O68 2.0).
 *
 *
 *                  ZIP_QUEX_BUF =  ZIP Queue Buffer Parameter Registers
 *
 * Description:
 * These registers set the buffer parameters for the instruction queues . The size of the instruction buffer
 * segments is measured in uint64s.  The pool specifies (1 of 8 free lists to be used when freeing command
 * buffer segments).  The PTR field is overwritten with the next pointer each time that the command
 * buffer segment is exhausted. When quiescent (i.e. outstanding doorbell count is 0), it is safe
 * to rewrite this register to effectively reset the command buffer state machine.  New commands
 * will then be read from the newly specified command buffer pointer.
 */
union cvmx_zip_quex_buf {
	uint64_t u64;
	struct cvmx_zip_quex_buf_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t dwb                          : 9;  /**< Number of DontWriteBacks */
	uint64_t pool                         : 3;  /**< Free list used to free command buffer segments */
	uint64_t size                         : 13; /**< Number of uint64s per command buffer segment */
	uint64_t ptr                          : 33; /**< Initial command buffer pointer[39:7] (128B-aligned) */
#else
	uint64_t ptr                          : 33;
	uint64_t size                         : 13;
	uint64_t pool                         : 3;
	uint64_t dwb                          : 9;
	uint64_t reserved_58_63               : 6;
#endif
	} s;
	struct cvmx_zip_quex_buf_s            cn68xx;
	struct cvmx_zip_quex_buf_s            cn68xxp1;
};
typedef union cvmx_zip_quex_buf cvmx_zip_quex_buf_t;

/**
 * cvmx_zip_que#_ecc_err_sta
 *
 * ZIP_QUEX_ECC_ERR_STA =  ZIP Queue ECC ERROR STATUS Register
 *
 * Description:
 *   This register contains the first ECC SBE/DBE status for the instruction buffer of a given zip instruction queue.
 */
union cvmx_zip_quex_ecc_err_sta {
	uint64_t u64;
	struct cvmx_zip_quex_ecc_err_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t wnum                         : 3;  /**< Index of the first IWORD that DBE happened
                                                         (Valid when ZIP_INT_REG[IBDBE] or [IBSBE] is set). */
	uint64_t inum                         : 32; /**< Index of the first instruction that DBE happened
                                                         (Valid when ZIP_INT_REG[IBDBE] or [IBSBE] is set). */
#else
	uint64_t inum                         : 32;
	uint64_t wnum                         : 3;
	uint64_t reserved_35_63               : 29;
#endif
	} s;
	struct cvmx_zip_quex_ecc_err_sta_s    cn68xx;
	struct cvmx_zip_quex_ecc_err_sta_s    cn68xxp1;
};
typedef union cvmx_zip_quex_ecc_err_sta cvmx_zip_quex_ecc_err_sta_t;

/**
 * cvmx_zip_que#_map
 *
 * ZIP_QUEX_MAP =  ZIP Queue Mapping Registers
 *
 * Description:
 *  These registers control how each instruction queue maps to 2 zip cores.
 *  Bit[0] corresponds to zip core 0 and bit[1] corresponds to zip core 1.
 *  A "1" means instructions from the queue can be served by the corresponding zip core.
 */
union cvmx_zip_quex_map {
	uint64_t u64;
	struct cvmx_zip_quex_map_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t zce                          : 2;  /**< Zip Core Enable
                                                         Controls the logical instruction queue can be
                                                         serviced by which zip core. Setting ZCE==0
                                                         effectively disables the queue from being served
                                                         (however the instruction can still be fetched).
                                                         ZCE[1]=1, zip core 1 can serve the queue.
                                                         ZCE[0]=1, zip core 0 can serve the queue. */
#else
	uint64_t zce                          : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_zip_quex_map_s            cn68xx;
	struct cvmx_zip_quex_map_s            cn68xxp1;
};
typedef union cvmx_zip_quex_map cvmx_zip_quex_map_t;

/**
 * cvmx_zip_que_ena
 *
 * ZIP_QUE_ENA =  ZIP Queue Enable Register
 *
 * Description:
 *    If a queue is disabled, ZIP_CTL will stop fetching instructions from the queue.
 */
union cvmx_zip_que_ena {
	uint64_t u64;
	struct cvmx_zip_que_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t ena                          : 2;  /**< Enables the logical instruction queues.
                                                         - 1: Queue is enabled. 0: Queue is disabled
                                                          ENA[1]=1 enables queue 1
                                                          ENA[0]=1 enables queue 0 */
#else
	uint64_t ena                          : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_zip_que_ena_s             cn68xx;
	struct cvmx_zip_que_ena_s             cn68xxp1;
};
typedef union cvmx_zip_que_ena cvmx_zip_que_ena_t;

/**
 * cvmx_zip_que_pri
 *
 * ZIP_QUE_PRI =  ZIP Queue Priority Register
 *
 * Description:
 *   This registers defines the priority between instruction queue 1 and instruction queue 0.
 *   Bit[0] corresponds to queue 0 and bit[1] corresponds to queue 1. A "1" means high priority.
 */
union cvmx_zip_que_pri {
	uint64_t u64;
	struct cvmx_zip_que_pri_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t pri                          : 2;  /**< Priority
                                                         2'b10: Queue 1 has higher priority.
                                                         2'b01: Queue 0 has higher priority.
                                                         2'b11,2'b00: round robin */
#else
	uint64_t pri                          : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_zip_que_pri_s             cn68xx;
	struct cvmx_zip_que_pri_s             cn68xxp1;
};
typedef union cvmx_zip_que_pri cvmx_zip_que_pri_t;

/**
 * cvmx_zip_throttle
 *
 * ZIP_THROTTLE =  ZIP Throttle Register
 *
 * Description:
 * This register controls the maximum number of in-flight X2I data fetch transactions.  Values > 16 are illegal.
 * Writing 0 to this register causes the ZIP module to temporarily suspend NCB accesses; it is not recommended
 * for normal operation, but may be useful for diagnostics.
 */
union cvmx_zip_throttle {
	uint64_t u64;
	struct cvmx_zip_throttle_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t max_infl                     : 5;  /**< Maximum number of in-flight data fetch transactions on
                                                         NCB. */
#else
	uint64_t max_infl                     : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_zip_throttle_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t max_infl                     : 4;  /**< Maximum number of inflight data fetch transactions
                                                         on NCB. */
#else
	uint64_t max_infl                     : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} cn61xx;
	struct cvmx_zip_throttle_cn61xx       cn63xx;
	struct cvmx_zip_throttle_cn61xx       cn63xxp1;
	struct cvmx_zip_throttle_cn61xx       cn66xx;
	struct cvmx_zip_throttle_s            cn68xx;
	struct cvmx_zip_throttle_s            cn68xxp1;
};
typedef union cvmx_zip_throttle cvmx_zip_throttle_t;

#endif
