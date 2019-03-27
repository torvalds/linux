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
 * cvmx-rad-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon rad.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_RAD_DEFS_H__
#define __CVMX_RAD_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_MEM_DEBUG0 CVMX_RAD_MEM_DEBUG0_FUNC()
static inline uint64_t CVMX_RAD_MEM_DEBUG0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_MEM_DEBUG0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070001000ull);
}
#else
#define CVMX_RAD_MEM_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180070001000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_MEM_DEBUG1 CVMX_RAD_MEM_DEBUG1_FUNC()
static inline uint64_t CVMX_RAD_MEM_DEBUG1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_MEM_DEBUG1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070001008ull);
}
#else
#define CVMX_RAD_MEM_DEBUG1 (CVMX_ADD_IO_SEG(0x0001180070001008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_MEM_DEBUG2 CVMX_RAD_MEM_DEBUG2_FUNC()
static inline uint64_t CVMX_RAD_MEM_DEBUG2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_MEM_DEBUG2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070001010ull);
}
#else
#define CVMX_RAD_MEM_DEBUG2 (CVMX_ADD_IO_SEG(0x0001180070001010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_BIST_RESULT CVMX_RAD_REG_BIST_RESULT_FUNC()
static inline uint64_t CVMX_RAD_REG_BIST_RESULT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_BIST_RESULT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000080ull);
}
#else
#define CVMX_RAD_REG_BIST_RESULT (CVMX_ADD_IO_SEG(0x0001180070000080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_CMD_BUF CVMX_RAD_REG_CMD_BUF_FUNC()
static inline uint64_t CVMX_RAD_REG_CMD_BUF_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_CMD_BUF not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000008ull);
}
#else
#define CVMX_RAD_REG_CMD_BUF (CVMX_ADD_IO_SEG(0x0001180070000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_CTL CVMX_RAD_REG_CTL_FUNC()
static inline uint64_t CVMX_RAD_REG_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000000ull);
}
#else
#define CVMX_RAD_REG_CTL (CVMX_ADD_IO_SEG(0x0001180070000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG0 CVMX_RAD_REG_DEBUG0_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000100ull);
}
#else
#define CVMX_RAD_REG_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180070000100ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG1 CVMX_RAD_REG_DEBUG1_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000108ull);
}
#else
#define CVMX_RAD_REG_DEBUG1 (CVMX_ADD_IO_SEG(0x0001180070000108ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG10 CVMX_RAD_REG_DEBUG10_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG10_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG10 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000150ull);
}
#else
#define CVMX_RAD_REG_DEBUG10 (CVMX_ADD_IO_SEG(0x0001180070000150ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG11 CVMX_RAD_REG_DEBUG11_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG11_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG11 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000158ull);
}
#else
#define CVMX_RAD_REG_DEBUG11 (CVMX_ADD_IO_SEG(0x0001180070000158ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG12 CVMX_RAD_REG_DEBUG12_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG12_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG12 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000160ull);
}
#else
#define CVMX_RAD_REG_DEBUG12 (CVMX_ADD_IO_SEG(0x0001180070000160ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG2 CVMX_RAD_REG_DEBUG2_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000110ull);
}
#else
#define CVMX_RAD_REG_DEBUG2 (CVMX_ADD_IO_SEG(0x0001180070000110ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG3 CVMX_RAD_REG_DEBUG3_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000118ull);
}
#else
#define CVMX_RAD_REG_DEBUG3 (CVMX_ADD_IO_SEG(0x0001180070000118ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG4 CVMX_RAD_REG_DEBUG4_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG4_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG4 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000120ull);
}
#else
#define CVMX_RAD_REG_DEBUG4 (CVMX_ADD_IO_SEG(0x0001180070000120ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG5 CVMX_RAD_REG_DEBUG5_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG5_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG5 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000128ull);
}
#else
#define CVMX_RAD_REG_DEBUG5 (CVMX_ADD_IO_SEG(0x0001180070000128ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG6 CVMX_RAD_REG_DEBUG6_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG6_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG6 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000130ull);
}
#else
#define CVMX_RAD_REG_DEBUG6 (CVMX_ADD_IO_SEG(0x0001180070000130ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG7 CVMX_RAD_REG_DEBUG7_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG7_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG7 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000138ull);
}
#else
#define CVMX_RAD_REG_DEBUG7 (CVMX_ADD_IO_SEG(0x0001180070000138ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG8 CVMX_RAD_REG_DEBUG8_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG8_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG8 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000140ull);
}
#else
#define CVMX_RAD_REG_DEBUG8 (CVMX_ADD_IO_SEG(0x0001180070000140ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_DEBUG9 CVMX_RAD_REG_DEBUG9_FUNC()
static inline uint64_t CVMX_RAD_REG_DEBUG9_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_DEBUG9 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000148ull);
}
#else
#define CVMX_RAD_REG_DEBUG9 (CVMX_ADD_IO_SEG(0x0001180070000148ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_ERROR CVMX_RAD_REG_ERROR_FUNC()
static inline uint64_t CVMX_RAD_REG_ERROR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_ERROR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000088ull);
}
#else
#define CVMX_RAD_REG_ERROR (CVMX_ADD_IO_SEG(0x0001180070000088ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_INT_MASK CVMX_RAD_REG_INT_MASK_FUNC()
static inline uint64_t CVMX_RAD_REG_INT_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_INT_MASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000090ull);
}
#else
#define CVMX_RAD_REG_INT_MASK (CVMX_ADD_IO_SEG(0x0001180070000090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_POLYNOMIAL CVMX_RAD_REG_POLYNOMIAL_FUNC()
static inline uint64_t CVMX_RAD_REG_POLYNOMIAL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_POLYNOMIAL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000010ull);
}
#else
#define CVMX_RAD_REG_POLYNOMIAL (CVMX_ADD_IO_SEG(0x0001180070000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RAD_REG_READ_IDX CVMX_RAD_REG_READ_IDX_FUNC()
static inline uint64_t CVMX_RAD_REG_READ_IDX_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RAD_REG_READ_IDX not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180070000018ull);
}
#else
#define CVMX_RAD_REG_READ_IDX (CVMX_ADD_IO_SEG(0x0001180070000018ull))
#endif

/**
 * cvmx_rad_mem_debug0
 *
 * Notes:
 * This CSR is a memory of 32 entries, and thus, the RAD_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_rad_mem_debug0 {
	uint64_t u64;
	struct cvmx_rad_mem_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t iword                        : 64; /**< IWord */
#else
	uint64_t iword                        : 64;
#endif
	} s;
	struct cvmx_rad_mem_debug0_s          cn52xx;
	struct cvmx_rad_mem_debug0_s          cn52xxp1;
	struct cvmx_rad_mem_debug0_s          cn56xx;
	struct cvmx_rad_mem_debug0_s          cn56xxp1;
	struct cvmx_rad_mem_debug0_s          cn61xx;
	struct cvmx_rad_mem_debug0_s          cn63xx;
	struct cvmx_rad_mem_debug0_s          cn63xxp1;
	struct cvmx_rad_mem_debug0_s          cn66xx;
	struct cvmx_rad_mem_debug0_s          cn68xx;
	struct cvmx_rad_mem_debug0_s          cn68xxp1;
	struct cvmx_rad_mem_debug0_s          cnf71xx;
};
typedef union cvmx_rad_mem_debug0 cvmx_rad_mem_debug0_t;

/**
 * cvmx_rad_mem_debug1
 *
 * Notes:
 * This CSR is a memory of 256 entries, and thus, the RAD_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_rad_mem_debug1 {
	uint64_t u64;
	struct cvmx_rad_mem_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t p_dat                        : 64; /**< P data */
#else
	uint64_t p_dat                        : 64;
#endif
	} s;
	struct cvmx_rad_mem_debug1_s          cn52xx;
	struct cvmx_rad_mem_debug1_s          cn52xxp1;
	struct cvmx_rad_mem_debug1_s          cn56xx;
	struct cvmx_rad_mem_debug1_s          cn56xxp1;
	struct cvmx_rad_mem_debug1_s          cn61xx;
	struct cvmx_rad_mem_debug1_s          cn63xx;
	struct cvmx_rad_mem_debug1_s          cn63xxp1;
	struct cvmx_rad_mem_debug1_s          cn66xx;
	struct cvmx_rad_mem_debug1_s          cn68xx;
	struct cvmx_rad_mem_debug1_s          cn68xxp1;
	struct cvmx_rad_mem_debug1_s          cnf71xx;
};
typedef union cvmx_rad_mem_debug1 cvmx_rad_mem_debug1_t;

/**
 * cvmx_rad_mem_debug2
 *
 * Notes:
 * This CSR is a memory of 256 entries, and thus, the RAD_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_rad_mem_debug2 {
	uint64_t u64;
	struct cvmx_rad_mem_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t q_dat                        : 64; /**< Q data */
#else
	uint64_t q_dat                        : 64;
#endif
	} s;
	struct cvmx_rad_mem_debug2_s          cn52xx;
	struct cvmx_rad_mem_debug2_s          cn52xxp1;
	struct cvmx_rad_mem_debug2_s          cn56xx;
	struct cvmx_rad_mem_debug2_s          cn56xxp1;
	struct cvmx_rad_mem_debug2_s          cn61xx;
	struct cvmx_rad_mem_debug2_s          cn63xx;
	struct cvmx_rad_mem_debug2_s          cn63xxp1;
	struct cvmx_rad_mem_debug2_s          cn66xx;
	struct cvmx_rad_mem_debug2_s          cn68xx;
	struct cvmx_rad_mem_debug2_s          cn68xxp1;
	struct cvmx_rad_mem_debug2_s          cnf71xx;
};
typedef union cvmx_rad_mem_debug2 cvmx_rad_mem_debug2_t;

/**
 * cvmx_rad_reg_bist_result
 *
 * Notes:
 * Access to the internal BiST results
 * Each bit is the BiST result of an individual memory (per bit, 0=pass and 1=fail).
 */
union cvmx_rad_reg_bist_result {
	uint64_t u64;
	struct cvmx_rad_reg_bist_result_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t sta                          : 1;  /**< BiST result of the STA     memories */
	uint64_t ncb_oub                      : 1;  /**< BiST result of the NCB_OUB memories */
	uint64_t ncb_inb                      : 2;  /**< BiST result of the NCB_INB memories */
	uint64_t dat                          : 2;  /**< BiST result of the DAT     memories */
#else
	uint64_t dat                          : 2;
	uint64_t ncb_inb                      : 2;
	uint64_t ncb_oub                      : 1;
	uint64_t sta                          : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_rad_reg_bist_result_s     cn52xx;
	struct cvmx_rad_reg_bist_result_s     cn52xxp1;
	struct cvmx_rad_reg_bist_result_s     cn56xx;
	struct cvmx_rad_reg_bist_result_s     cn56xxp1;
	struct cvmx_rad_reg_bist_result_s     cn61xx;
	struct cvmx_rad_reg_bist_result_s     cn63xx;
	struct cvmx_rad_reg_bist_result_s     cn63xxp1;
	struct cvmx_rad_reg_bist_result_s     cn66xx;
	struct cvmx_rad_reg_bist_result_s     cn68xx;
	struct cvmx_rad_reg_bist_result_s     cn68xxp1;
	struct cvmx_rad_reg_bist_result_s     cnf71xx;
};
typedef union cvmx_rad_reg_bist_result cvmx_rad_reg_bist_result_t;

/**
 * cvmx_rad_reg_cmd_buf
 *
 * Notes:
 * Sets the command buffer parameters
 * The size of the command buffer segments is measured in uint64s.  The pool specifies 1 of 8 free
 * lists to be used when freeing command buffer segments.  The PTR field is overwritten with the next
 * pointer each time that the command buffer segment is exhausted.
 */
union cvmx_rad_reg_cmd_buf {
	uint64_t u64;
	struct cvmx_rad_reg_cmd_buf_s {
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
	struct cvmx_rad_reg_cmd_buf_s         cn52xx;
	struct cvmx_rad_reg_cmd_buf_s         cn52xxp1;
	struct cvmx_rad_reg_cmd_buf_s         cn56xx;
	struct cvmx_rad_reg_cmd_buf_s         cn56xxp1;
	struct cvmx_rad_reg_cmd_buf_s         cn61xx;
	struct cvmx_rad_reg_cmd_buf_s         cn63xx;
	struct cvmx_rad_reg_cmd_buf_s         cn63xxp1;
	struct cvmx_rad_reg_cmd_buf_s         cn66xx;
	struct cvmx_rad_reg_cmd_buf_s         cn68xx;
	struct cvmx_rad_reg_cmd_buf_s         cn68xxp1;
	struct cvmx_rad_reg_cmd_buf_s         cnf71xx;
};
typedef union cvmx_rad_reg_cmd_buf cvmx_rad_reg_cmd_buf_t;

/**
 * cvmx_rad_reg_ctl
 *
 * Notes:
 * MAX_READ is a throttle to control NCB usage.  Values >8 are illegal.
 *
 */
union cvmx_rad_reg_ctl {
	uint64_t u64;
	struct cvmx_rad_reg_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t max_read                     : 4;  /**< Maximum number of outstanding data read commands */
	uint64_t store_le                     : 1;  /**< Force STORE0 byte write address to little endian */
	uint64_t reset                        : 1;  /**< Reset oneshot pulse (lasts for 4 cycles) */
#else
	uint64_t reset                        : 1;
	uint64_t store_le                     : 1;
	uint64_t max_read                     : 4;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_rad_reg_ctl_s             cn52xx;
	struct cvmx_rad_reg_ctl_s             cn52xxp1;
	struct cvmx_rad_reg_ctl_s             cn56xx;
	struct cvmx_rad_reg_ctl_s             cn56xxp1;
	struct cvmx_rad_reg_ctl_s             cn61xx;
	struct cvmx_rad_reg_ctl_s             cn63xx;
	struct cvmx_rad_reg_ctl_s             cn63xxp1;
	struct cvmx_rad_reg_ctl_s             cn66xx;
	struct cvmx_rad_reg_ctl_s             cn68xx;
	struct cvmx_rad_reg_ctl_s             cn68xxp1;
	struct cvmx_rad_reg_ctl_s             cnf71xx;
};
typedef union cvmx_rad_reg_ctl cvmx_rad_reg_ctl_t;

/**
 * cvmx_rad_reg_debug0
 */
union cvmx_rad_reg_debug0 {
	uint64_t u64;
	struct cvmx_rad_reg_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_57_63               : 7;
	uint64_t loop                         : 25; /**< Loop offset */
	uint64_t reserved_22_31               : 10;
	uint64_t iridx                        : 6;  /**< IWords read index */
	uint64_t reserved_14_15               : 2;
	uint64_t iwidx                        : 6;  /**< IWords write index */
	uint64_t owordqv                      : 1;  /**< Valid for OWORDQ */
	uint64_t owordpv                      : 1;  /**< Valid for OWORDP */
	uint64_t commit                       : 1;  /**< Waiting for write commit */
	uint64_t state                        : 5;  /**< Main state */
#else
	uint64_t state                        : 5;
	uint64_t commit                       : 1;
	uint64_t owordpv                      : 1;
	uint64_t owordqv                      : 1;
	uint64_t iwidx                        : 6;
	uint64_t reserved_14_15               : 2;
	uint64_t iridx                        : 6;
	uint64_t reserved_22_31               : 10;
	uint64_t loop                         : 25;
	uint64_t reserved_57_63               : 7;
#endif
	} s;
	struct cvmx_rad_reg_debug0_s          cn52xx;
	struct cvmx_rad_reg_debug0_s          cn52xxp1;
	struct cvmx_rad_reg_debug0_s          cn56xx;
	struct cvmx_rad_reg_debug0_s          cn56xxp1;
	struct cvmx_rad_reg_debug0_s          cn61xx;
	struct cvmx_rad_reg_debug0_s          cn63xx;
	struct cvmx_rad_reg_debug0_s          cn63xxp1;
	struct cvmx_rad_reg_debug0_s          cn66xx;
	struct cvmx_rad_reg_debug0_s          cn68xx;
	struct cvmx_rad_reg_debug0_s          cn68xxp1;
	struct cvmx_rad_reg_debug0_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug0 cvmx_rad_reg_debug0_t;

/**
 * cvmx_rad_reg_debug1
 */
union cvmx_rad_reg_debug1 {
	uint64_t u64;
	struct cvmx_rad_reg_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cword                        : 64; /**< CWord */
#else
	uint64_t cword                        : 64;
#endif
	} s;
	struct cvmx_rad_reg_debug1_s          cn52xx;
	struct cvmx_rad_reg_debug1_s          cn52xxp1;
	struct cvmx_rad_reg_debug1_s          cn56xx;
	struct cvmx_rad_reg_debug1_s          cn56xxp1;
	struct cvmx_rad_reg_debug1_s          cn61xx;
	struct cvmx_rad_reg_debug1_s          cn63xx;
	struct cvmx_rad_reg_debug1_s          cn63xxp1;
	struct cvmx_rad_reg_debug1_s          cn66xx;
	struct cvmx_rad_reg_debug1_s          cn68xx;
	struct cvmx_rad_reg_debug1_s          cn68xxp1;
	struct cvmx_rad_reg_debug1_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug1 cvmx_rad_reg_debug1_t;

/**
 * cvmx_rad_reg_debug10
 */
union cvmx_rad_reg_debug10 {
	uint64_t u64;
	struct cvmx_rad_reg_debug10_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t flags                        : 8;  /**< OCTL flags */
	uint64_t size                         : 16; /**< OCTL size (bytes) */
	uint64_t ptr                          : 40; /**< OCTL pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t flags                        : 8;
#endif
	} s;
	struct cvmx_rad_reg_debug10_s         cn52xx;
	struct cvmx_rad_reg_debug10_s         cn52xxp1;
	struct cvmx_rad_reg_debug10_s         cn56xx;
	struct cvmx_rad_reg_debug10_s         cn56xxp1;
	struct cvmx_rad_reg_debug10_s         cn61xx;
	struct cvmx_rad_reg_debug10_s         cn63xx;
	struct cvmx_rad_reg_debug10_s         cn63xxp1;
	struct cvmx_rad_reg_debug10_s         cn66xx;
	struct cvmx_rad_reg_debug10_s         cn68xx;
	struct cvmx_rad_reg_debug10_s         cn68xxp1;
	struct cvmx_rad_reg_debug10_s         cnf71xx;
};
typedef union cvmx_rad_reg_debug10 cvmx_rad_reg_debug10_t;

/**
 * cvmx_rad_reg_debug11
 */
union cvmx_rad_reg_debug11 {
	uint64_t u64;
	struct cvmx_rad_reg_debug11_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t q                            : 1;  /**< OCTL q flag */
	uint64_t p                            : 1;  /**< OCTL p flag */
	uint64_t wc                           : 1;  /**< OCTL write commit flag */
	uint64_t eod                          : 1;  /**< OCTL eod flag */
	uint64_t sod                          : 1;  /**< OCTL sod flag */
	uint64_t index                        : 8;  /**< OCTL index */
#else
	uint64_t index                        : 8;
	uint64_t sod                          : 1;
	uint64_t eod                          : 1;
	uint64_t wc                           : 1;
	uint64_t p                            : 1;
	uint64_t q                            : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_rad_reg_debug11_s         cn52xx;
	struct cvmx_rad_reg_debug11_s         cn52xxp1;
	struct cvmx_rad_reg_debug11_s         cn56xx;
	struct cvmx_rad_reg_debug11_s         cn56xxp1;
	struct cvmx_rad_reg_debug11_s         cn61xx;
	struct cvmx_rad_reg_debug11_s         cn63xx;
	struct cvmx_rad_reg_debug11_s         cn63xxp1;
	struct cvmx_rad_reg_debug11_s         cn66xx;
	struct cvmx_rad_reg_debug11_s         cn68xx;
	struct cvmx_rad_reg_debug11_s         cn68xxp1;
	struct cvmx_rad_reg_debug11_s         cnf71xx;
};
typedef union cvmx_rad_reg_debug11 cvmx_rad_reg_debug11_t;

/**
 * cvmx_rad_reg_debug12
 */
union cvmx_rad_reg_debug12 {
	uint64_t u64;
	struct cvmx_rad_reg_debug12_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t asserts                      : 15; /**< Various assertion checks */
#else
	uint64_t asserts                      : 15;
	uint64_t reserved_15_63               : 49;
#endif
	} s;
	struct cvmx_rad_reg_debug12_s         cn52xx;
	struct cvmx_rad_reg_debug12_s         cn52xxp1;
	struct cvmx_rad_reg_debug12_s         cn56xx;
	struct cvmx_rad_reg_debug12_s         cn56xxp1;
	struct cvmx_rad_reg_debug12_s         cn61xx;
	struct cvmx_rad_reg_debug12_s         cn63xx;
	struct cvmx_rad_reg_debug12_s         cn63xxp1;
	struct cvmx_rad_reg_debug12_s         cn66xx;
	struct cvmx_rad_reg_debug12_s         cn68xx;
	struct cvmx_rad_reg_debug12_s         cn68xxp1;
	struct cvmx_rad_reg_debug12_s         cnf71xx;
};
typedef union cvmx_rad_reg_debug12 cvmx_rad_reg_debug12_t;

/**
 * cvmx_rad_reg_debug2
 */
union cvmx_rad_reg_debug2 {
	uint64_t u64;
	struct cvmx_rad_reg_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t owordp                       : 64; /**< OWordP */
#else
	uint64_t owordp                       : 64;
#endif
	} s;
	struct cvmx_rad_reg_debug2_s          cn52xx;
	struct cvmx_rad_reg_debug2_s          cn52xxp1;
	struct cvmx_rad_reg_debug2_s          cn56xx;
	struct cvmx_rad_reg_debug2_s          cn56xxp1;
	struct cvmx_rad_reg_debug2_s          cn61xx;
	struct cvmx_rad_reg_debug2_s          cn63xx;
	struct cvmx_rad_reg_debug2_s          cn63xxp1;
	struct cvmx_rad_reg_debug2_s          cn66xx;
	struct cvmx_rad_reg_debug2_s          cn68xx;
	struct cvmx_rad_reg_debug2_s          cn68xxp1;
	struct cvmx_rad_reg_debug2_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug2 cvmx_rad_reg_debug2_t;

/**
 * cvmx_rad_reg_debug3
 */
union cvmx_rad_reg_debug3 {
	uint64_t u64;
	struct cvmx_rad_reg_debug3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t owordq                       : 64; /**< OWordQ */
#else
	uint64_t owordq                       : 64;
#endif
	} s;
	struct cvmx_rad_reg_debug3_s          cn52xx;
	struct cvmx_rad_reg_debug3_s          cn52xxp1;
	struct cvmx_rad_reg_debug3_s          cn56xx;
	struct cvmx_rad_reg_debug3_s          cn56xxp1;
	struct cvmx_rad_reg_debug3_s          cn61xx;
	struct cvmx_rad_reg_debug3_s          cn63xx;
	struct cvmx_rad_reg_debug3_s          cn63xxp1;
	struct cvmx_rad_reg_debug3_s          cn66xx;
	struct cvmx_rad_reg_debug3_s          cn68xx;
	struct cvmx_rad_reg_debug3_s          cn68xxp1;
	struct cvmx_rad_reg_debug3_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug3 cvmx_rad_reg_debug3_t;

/**
 * cvmx_rad_reg_debug4
 */
union cvmx_rad_reg_debug4 {
	uint64_t u64;
	struct cvmx_rad_reg_debug4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rword                        : 64; /**< RWord */
#else
	uint64_t rword                        : 64;
#endif
	} s;
	struct cvmx_rad_reg_debug4_s          cn52xx;
	struct cvmx_rad_reg_debug4_s          cn52xxp1;
	struct cvmx_rad_reg_debug4_s          cn56xx;
	struct cvmx_rad_reg_debug4_s          cn56xxp1;
	struct cvmx_rad_reg_debug4_s          cn61xx;
	struct cvmx_rad_reg_debug4_s          cn63xx;
	struct cvmx_rad_reg_debug4_s          cn63xxp1;
	struct cvmx_rad_reg_debug4_s          cn66xx;
	struct cvmx_rad_reg_debug4_s          cn68xx;
	struct cvmx_rad_reg_debug4_s          cn68xxp1;
	struct cvmx_rad_reg_debug4_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug4 cvmx_rad_reg_debug4_t;

/**
 * cvmx_rad_reg_debug5
 */
union cvmx_rad_reg_debug5 {
	uint64_t u64;
	struct cvmx_rad_reg_debug5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_53_63               : 11;
	uint64_t niropc7                      : 3;  /**< NCBI ropc (stage7 grant) */
	uint64_t nirque7                      : 2;  /**< NCBI rque (stage7 grant) */
	uint64_t nirval7                      : 5;  /**< NCBI rval (stage7 grant) */
	uint64_t niropc6                      : 3;  /**< NCBI ropc (stage6 arb) */
	uint64_t nirque6                      : 2;  /**< NCBI rque (stage6 arb) */
	uint64_t nirarb6                      : 1;  /**< NCBI rarb (stage6 arb) */
	uint64_t nirval6                      : 5;  /**< NCBI rval (stage6 arb) */
	uint64_t niridx1                      : 4;  /**< NCBI ridx1 */
	uint64_t niwidx1                      : 4;  /**< NCBI widx1 */
	uint64_t niridx0                      : 4;  /**< NCBI ridx0 */
	uint64_t niwidx0                      : 4;  /**< NCBI widx0 */
	uint64_t wccreds                      : 2;  /**< WC credits */
	uint64_t fpacreds                     : 2;  /**< POW credits */
	uint64_t reserved_10_11               : 2;
	uint64_t powcreds                     : 2;  /**< POW credits */
	uint64_t n1creds                      : 4;  /**< NCBI1 credits */
	uint64_t n0creds                      : 4;  /**< NCBI0 credits */
#else
	uint64_t n0creds                      : 4;
	uint64_t n1creds                      : 4;
	uint64_t powcreds                     : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t fpacreds                     : 2;
	uint64_t wccreds                      : 2;
	uint64_t niwidx0                      : 4;
	uint64_t niridx0                      : 4;
	uint64_t niwidx1                      : 4;
	uint64_t niridx1                      : 4;
	uint64_t nirval6                      : 5;
	uint64_t nirarb6                      : 1;
	uint64_t nirque6                      : 2;
	uint64_t niropc6                      : 3;
	uint64_t nirval7                      : 5;
	uint64_t nirque7                      : 2;
	uint64_t niropc7                      : 3;
	uint64_t reserved_53_63               : 11;
#endif
	} s;
	struct cvmx_rad_reg_debug5_s          cn52xx;
	struct cvmx_rad_reg_debug5_s          cn52xxp1;
	struct cvmx_rad_reg_debug5_s          cn56xx;
	struct cvmx_rad_reg_debug5_s          cn56xxp1;
	struct cvmx_rad_reg_debug5_s          cn61xx;
	struct cvmx_rad_reg_debug5_s          cn63xx;
	struct cvmx_rad_reg_debug5_s          cn63xxp1;
	struct cvmx_rad_reg_debug5_s          cn66xx;
	struct cvmx_rad_reg_debug5_s          cn68xx;
	struct cvmx_rad_reg_debug5_s          cn68xxp1;
	struct cvmx_rad_reg_debug5_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug5 cvmx_rad_reg_debug5_t;

/**
 * cvmx_rad_reg_debug6
 */
union cvmx_rad_reg_debug6 {
	uint64_t u64;
	struct cvmx_rad_reg_debug6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cnt                          : 8;  /**< CCTL count[7:0] (bytes) */
	uint64_t size                         : 16; /**< CCTL size (bytes) */
	uint64_t ptr                          : 40; /**< CCTL pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t cnt                          : 8;
#endif
	} s;
	struct cvmx_rad_reg_debug6_s          cn52xx;
	struct cvmx_rad_reg_debug6_s          cn52xxp1;
	struct cvmx_rad_reg_debug6_s          cn56xx;
	struct cvmx_rad_reg_debug6_s          cn56xxp1;
	struct cvmx_rad_reg_debug6_s          cn61xx;
	struct cvmx_rad_reg_debug6_s          cn63xx;
	struct cvmx_rad_reg_debug6_s          cn63xxp1;
	struct cvmx_rad_reg_debug6_s          cn66xx;
	struct cvmx_rad_reg_debug6_s          cn68xx;
	struct cvmx_rad_reg_debug6_s          cn68xxp1;
	struct cvmx_rad_reg_debug6_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug6 cvmx_rad_reg_debug6_t;

/**
 * cvmx_rad_reg_debug7
 */
union cvmx_rad_reg_debug7 {
	uint64_t u64;
	struct cvmx_rad_reg_debug7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t cnt                          : 15; /**< CCTL count[22:8] (bytes) */
#else
	uint64_t cnt                          : 15;
	uint64_t reserved_15_63               : 49;
#endif
	} s;
	struct cvmx_rad_reg_debug7_s          cn52xx;
	struct cvmx_rad_reg_debug7_s          cn52xxp1;
	struct cvmx_rad_reg_debug7_s          cn56xx;
	struct cvmx_rad_reg_debug7_s          cn56xxp1;
	struct cvmx_rad_reg_debug7_s          cn61xx;
	struct cvmx_rad_reg_debug7_s          cn63xx;
	struct cvmx_rad_reg_debug7_s          cn63xxp1;
	struct cvmx_rad_reg_debug7_s          cn66xx;
	struct cvmx_rad_reg_debug7_s          cn68xx;
	struct cvmx_rad_reg_debug7_s          cn68xxp1;
	struct cvmx_rad_reg_debug7_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug7 cvmx_rad_reg_debug7_t;

/**
 * cvmx_rad_reg_debug8
 */
union cvmx_rad_reg_debug8 {
	uint64_t u64;
	struct cvmx_rad_reg_debug8_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t flags                        : 8;  /**< ICTL flags */
	uint64_t size                         : 16; /**< ICTL size (bytes) */
	uint64_t ptr                          : 40; /**< ICTL pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t flags                        : 8;
#endif
	} s;
	struct cvmx_rad_reg_debug8_s          cn52xx;
	struct cvmx_rad_reg_debug8_s          cn52xxp1;
	struct cvmx_rad_reg_debug8_s          cn56xx;
	struct cvmx_rad_reg_debug8_s          cn56xxp1;
	struct cvmx_rad_reg_debug8_s          cn61xx;
	struct cvmx_rad_reg_debug8_s          cn63xx;
	struct cvmx_rad_reg_debug8_s          cn63xxp1;
	struct cvmx_rad_reg_debug8_s          cn66xx;
	struct cvmx_rad_reg_debug8_s          cn68xx;
	struct cvmx_rad_reg_debug8_s          cn68xxp1;
	struct cvmx_rad_reg_debug8_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug8 cvmx_rad_reg_debug8_t;

/**
 * cvmx_rad_reg_debug9
 */
union cvmx_rad_reg_debug9 {
	uint64_t u64;
	struct cvmx_rad_reg_debug9_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t eod                          : 1;  /**< ICTL eod flag */
	uint64_t ini                          : 1;  /**< ICTL init flag */
	uint64_t q                            : 1;  /**< ICTL q enable */
	uint64_t p                            : 1;  /**< ICTL p enable */
	uint64_t mul                          : 8;  /**< ICTL multiplier */
	uint64_t index                        : 8;  /**< ICTL index */
#else
	uint64_t index                        : 8;
	uint64_t mul                          : 8;
	uint64_t p                            : 1;
	uint64_t q                            : 1;
	uint64_t ini                          : 1;
	uint64_t eod                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_rad_reg_debug9_s          cn52xx;
	struct cvmx_rad_reg_debug9_s          cn52xxp1;
	struct cvmx_rad_reg_debug9_s          cn56xx;
	struct cvmx_rad_reg_debug9_s          cn56xxp1;
	struct cvmx_rad_reg_debug9_s          cn61xx;
	struct cvmx_rad_reg_debug9_s          cn63xx;
	struct cvmx_rad_reg_debug9_s          cn63xxp1;
	struct cvmx_rad_reg_debug9_s          cn66xx;
	struct cvmx_rad_reg_debug9_s          cn68xx;
	struct cvmx_rad_reg_debug9_s          cn68xxp1;
	struct cvmx_rad_reg_debug9_s          cnf71xx;
};
typedef union cvmx_rad_reg_debug9 cvmx_rad_reg_debug9_t;

/**
 * cvmx_rad_reg_error
 */
union cvmx_rad_reg_error {
	uint64_t u64;
	struct cvmx_rad_reg_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t doorbell                     : 1;  /**< A doorbell count has overflowed */
#else
	uint64_t doorbell                     : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_rad_reg_error_s           cn52xx;
	struct cvmx_rad_reg_error_s           cn52xxp1;
	struct cvmx_rad_reg_error_s           cn56xx;
	struct cvmx_rad_reg_error_s           cn56xxp1;
	struct cvmx_rad_reg_error_s           cn61xx;
	struct cvmx_rad_reg_error_s           cn63xx;
	struct cvmx_rad_reg_error_s           cn63xxp1;
	struct cvmx_rad_reg_error_s           cn66xx;
	struct cvmx_rad_reg_error_s           cn68xx;
	struct cvmx_rad_reg_error_s           cn68xxp1;
	struct cvmx_rad_reg_error_s           cnf71xx;
};
typedef union cvmx_rad_reg_error cvmx_rad_reg_error_t;

/**
 * cvmx_rad_reg_int_mask
 *
 * Notes:
 * When a mask bit is set, the corresponding interrupt is enabled.
 *
 */
union cvmx_rad_reg_int_mask {
	uint64_t u64;
	struct cvmx_rad_reg_int_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t doorbell                     : 1;  /**< Bit mask corresponding to RAD_REG_ERROR[0] above */
#else
	uint64_t doorbell                     : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_rad_reg_int_mask_s        cn52xx;
	struct cvmx_rad_reg_int_mask_s        cn52xxp1;
	struct cvmx_rad_reg_int_mask_s        cn56xx;
	struct cvmx_rad_reg_int_mask_s        cn56xxp1;
	struct cvmx_rad_reg_int_mask_s        cn61xx;
	struct cvmx_rad_reg_int_mask_s        cn63xx;
	struct cvmx_rad_reg_int_mask_s        cn63xxp1;
	struct cvmx_rad_reg_int_mask_s        cn66xx;
	struct cvmx_rad_reg_int_mask_s        cn68xx;
	struct cvmx_rad_reg_int_mask_s        cn68xxp1;
	struct cvmx_rad_reg_int_mask_s        cnf71xx;
};
typedef union cvmx_rad_reg_int_mask cvmx_rad_reg_int_mask_t;

/**
 * cvmx_rad_reg_polynomial
 *
 * Notes:
 * The polynomial is x^8 + C7*x^7 + C6*x^6 + C5*x^5 + C4*x^4 + C3*x^3 + C2*x^2 + C1*x^1 + C0.
 *
 */
union cvmx_rad_reg_polynomial {
	uint64_t u64;
	struct cvmx_rad_reg_polynomial_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t coeffs                       : 8;  /**< coefficients of GF(2^8) irreducible polynomial */
#else
	uint64_t coeffs                       : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_rad_reg_polynomial_s      cn52xx;
	struct cvmx_rad_reg_polynomial_s      cn52xxp1;
	struct cvmx_rad_reg_polynomial_s      cn56xx;
	struct cvmx_rad_reg_polynomial_s      cn56xxp1;
	struct cvmx_rad_reg_polynomial_s      cn61xx;
	struct cvmx_rad_reg_polynomial_s      cn63xx;
	struct cvmx_rad_reg_polynomial_s      cn63xxp1;
	struct cvmx_rad_reg_polynomial_s      cn66xx;
	struct cvmx_rad_reg_polynomial_s      cn68xx;
	struct cvmx_rad_reg_polynomial_s      cn68xxp1;
	struct cvmx_rad_reg_polynomial_s      cnf71xx;
};
typedef union cvmx_rad_reg_polynomial cvmx_rad_reg_polynomial_t;

/**
 * cvmx_rad_reg_read_idx
 *
 * Notes:
 * Provides the read index during a CSR read operation to any of the CSRs that are physically stored
 * as memories.  The names of these CSRs begin with the prefix "RAD_MEM_".
 * IDX[15:0] is the read index.  INC[15:0] is an increment that is added to IDX[15:0] after any CSR read.
 * The intended use is to initially write this CSR such that IDX=0 and INC=1.  Then, the entire
 * contents of a CSR memory can be read with consecutive CSR read commands.
 */
union cvmx_rad_reg_read_idx {
	uint64_t u64;
	struct cvmx_rad_reg_read_idx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t inc                          : 16; /**< Increment to add to current index for next index */
	uint64_t index                        : 16; /**< Index to use for next memory CSR read */
#else
	uint64_t index                        : 16;
	uint64_t inc                          : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_rad_reg_read_idx_s        cn52xx;
	struct cvmx_rad_reg_read_idx_s        cn52xxp1;
	struct cvmx_rad_reg_read_idx_s        cn56xx;
	struct cvmx_rad_reg_read_idx_s        cn56xxp1;
	struct cvmx_rad_reg_read_idx_s        cn61xx;
	struct cvmx_rad_reg_read_idx_s        cn63xx;
	struct cvmx_rad_reg_read_idx_s        cn63xxp1;
	struct cvmx_rad_reg_read_idx_s        cn66xx;
	struct cvmx_rad_reg_read_idx_s        cn68xx;
	struct cvmx_rad_reg_read_idx_s        cn68xxp1;
	struct cvmx_rad_reg_read_idx_s        cnf71xx;
};
typedef union cvmx_rad_reg_read_idx cvmx_rad_reg_read_idx_t;

#endif
