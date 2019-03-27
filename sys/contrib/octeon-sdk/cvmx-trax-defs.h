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
 * cvmx-trax-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon trax.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_TRAX_DEFS_H__
#define __CVMX_TRAX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_BIST_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_BIST_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000010ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000010ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000000ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000000ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_CYCLES_SINCE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_CYCLES_SINCE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000018ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_CYCLES_SINCE(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000018ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_CYCLES_SINCE1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_CYCLES_SINCE1(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000028ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_CYCLES_SINCE1(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000028ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_FILT_ADR_ADR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_FILT_ADR_ADR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000058ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_FILT_ADR_ADR(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000058ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_FILT_ADR_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_FILT_ADR_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000060ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_FILT_ADR_MSK(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000060ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_FILT_CMD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_FILT_CMD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000040ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_FILT_CMD(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000040ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_FILT_DID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_FILT_DID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000050ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_FILT_DID(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000050ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_FILT_SID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_FILT_SID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000048ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_FILT_SID(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000048ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_INT_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_INT_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000008ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_INT_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000008ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_READ_DAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_READ_DAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000020ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_READ_DAT(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000020ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_READ_DAT_HI(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_READ_DAT_HI(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000030ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_READ_DAT_HI(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000030ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG0_ADR_ADR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG0_ADR_ADR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000098ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG0_ADR_ADR(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000098ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG0_ADR_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG0_ADR_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A80000A0ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG0_ADR_MSK(block_id) (CVMX_ADD_IO_SEG(0x00011800A80000A0ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG0_CMD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG0_CMD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000080ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG0_CMD(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000080ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG0_DID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG0_DID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000090ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG0_DID(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000090ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG0_SID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG0_SID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A8000088ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG0_SID(block_id) (CVMX_ADD_IO_SEG(0x00011800A8000088ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG1_ADR_ADR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG1_ADR_ADR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A80000D8ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG1_ADR_ADR(block_id) (CVMX_ADD_IO_SEG(0x00011800A80000D8ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG1_ADR_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG1_ADR_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A80000E0ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG1_ADR_MSK(block_id) (CVMX_ADD_IO_SEG(0x00011800A80000E0ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG1_CMD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG1_CMD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A80000C0ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG1_CMD(block_id) (CVMX_ADD_IO_SEG(0x00011800A80000C0ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG1_DID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG1_DID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A80000D0ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG1_DID(block_id) (CVMX_ADD_IO_SEG(0x00011800A80000D0ull) + ((block_id) & 3) * 0x100000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_TRAX_TRIG1_SID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_TRAX_TRIG1_SID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A80000C8ull) + ((block_id) & 3) * 0x100000ull;
}
#else
#define CVMX_TRAX_TRIG1_SID(block_id) (CVMX_ADD_IO_SEG(0x00011800A80000C8ull) + ((block_id) & 3) * 0x100000ull)
#endif

/**
 * cvmx_tra#_bist_status
 *
 * TRA_BIST_STATUS = Trace Buffer BiST Status
 *
 * Description:
 */
union cvmx_trax_bist_status {
	uint64_t u64;
	struct cvmx_trax_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t tcf                          : 1;  /**< Bist Results for TCF memory
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t tdf1                         : 1;  /**< Bist Results for TDF memory 1
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t tdf1                         : 1;
	uint64_t tcf                          : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_trax_bist_status_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t tcf                          : 1;  /**< Bist Results for TCF memory
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t tdf1                         : 1;  /**< Bist Results for TDF memory 1
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t tdf0                         : 1;  /**< Bist Results for TCF memory 0
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t tdf0                         : 1;
	uint64_t tdf1                         : 1;
	uint64_t tcf                          : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} cn31xx;
	struct cvmx_trax_bist_status_cn31xx   cn38xx;
	struct cvmx_trax_bist_status_cn31xx   cn38xxp2;
	struct cvmx_trax_bist_status_cn31xx   cn52xx;
	struct cvmx_trax_bist_status_cn31xx   cn52xxp1;
	struct cvmx_trax_bist_status_cn31xx   cn56xx;
	struct cvmx_trax_bist_status_cn31xx   cn56xxp1;
	struct cvmx_trax_bist_status_cn31xx   cn58xx;
	struct cvmx_trax_bist_status_cn31xx   cn58xxp1;
	struct cvmx_trax_bist_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t tdf                          : 1;  /**< Bist Results for TCF memory
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t tdf                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} cn61xx;
	struct cvmx_trax_bist_status_cn61xx   cn63xx;
	struct cvmx_trax_bist_status_cn61xx   cn63xxp1;
	struct cvmx_trax_bist_status_cn61xx   cn66xx;
	struct cvmx_trax_bist_status_cn61xx   cn68xx;
	struct cvmx_trax_bist_status_cn61xx   cn68xxp1;
	struct cvmx_trax_bist_status_cn61xx   cnf71xx;
};
typedef union cvmx_trax_bist_status cvmx_trax_bist_status_t;

/**
 * cvmx_tra#_ctl
 *
 * TRA_CTL = Trace Buffer Control
 *
 * Description:
 *
 * Notes:
 * It is illegal to change the values of WRAP, TRIG_CTL, IGNORE_O while tracing (i.e. when ENA=1).
 * Note that the following fields are present only in chip revisions beginning with pass2: IGNORE_O
 */
union cvmx_trax_ctl {
	uint64_t u64;
	struct cvmx_trax_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t rdat_md                      : 1;  /**< TRA_READ_DAT mode bit
                                                         If set, the TRA_READ_DAT reads will return the lower
                                                         64 bits of the TRA entry and the upper bits must be
                                                         read through TRA_READ_DAT_HI.  If not set the return
                                                         value from TRA_READ_DAT accesses will switch between
                                                         the lower bits and the upper bits of the TRA entry. */
	uint64_t clkalways                    : 1;  /**< Conditional clock enable
                                                         If set, the TRA clock is never disabled. */
	uint64_t ignore_o                     : 1;  /**< Ignore overflow during wrap mode
                                                         If set and wrapping mode is enabled, then tracing
                                                         will not stop at the overflow condition.  Each
                                                         write during an overflow will overwrite the
                                                         oldest, unread entry and the read pointer is
                                                         incremented by one entry.  This bit has no effect
                                                         if WRAP=0. */
	uint64_t mcd0_ena                     : 1;  /**< MCD0 enable
                                                         If set and any PP sends the MCD0 signal, the
                                                         tracing is disabled. */
	uint64_t mcd0_thr                     : 1;  /**< MCD0_threshold
                                                         At a fill threshold event, sends an MCD0
                                                         wire pulse that can cause cores to enter debug
                                                         mode, if enabled.  This MCD0 wire pulse will not
                                                         occur while (TRA_INT_STATUS.MCD0_THR == 1). */
	uint64_t mcd0_trg                     : 1;  /**< MCD0_trigger
                                                         At an end trigger event, sends an MCD0
                                                         wire pulse that can cause cores to enter debug
                                                         mode, if enabled.  This MCD0 wire pulse will not
                                                         occur while (TRA_INT_STATUS.MCD0_TRG == 1). */
	uint64_t ciu_thr                      : 1;  /**< CIU_threshold
                                                         When set during a fill threshold event,
                                                         TRA_INT_STATUS[CIU_THR] is set, which can cause
                                                         core interrupts, if enabled. */
	uint64_t ciu_trg                      : 1;  /**< CIU_trigger
                                                         When set during an end trigger event,
                                                         TRA_INT_STATUS[CIU_TRG] is set, which can cause
                                                         core interrupts, if enabled. */
	uint64_t full_thr                     : 2;  /**< Full Threshhold
                                                         0=none
                                                         1=1/2 full
                                                         2=3/4 full
                                                         3=4/4 full */
	uint64_t time_grn                     : 3;  /**< Timestamp granularity
                                                         granularity=8^n cycles, n=0,1,2,3,4,5,6,7 */
	uint64_t trig_ctl                     : 2;  /**< Trigger Control
                                                         Note: trigger events are written to the trace
                                                         0=no triggers
                                                         1=trigger0=start trigger, trigger1=stop trigger
                                                         2=(trigger0 || trigger1)=start trigger
                                                         3=(trigger0 || trigger1)=stop trigger */
	uint64_t wrap                         : 1;  /**< Wrap mode
                                                         When WRAP=0, the trace buffer will disable itself
                                                         after having logged 1024 entries.  When WRAP=1,
                                                         the trace buffer will never disable itself.
                                                         In this case, tracing may or may not be
                                                         temporarily suspended during the overflow
                                                         condition (see IGNORE_O above).
                                                         0=do not wrap
                                                         1=wrap */
	uint64_t ena                          : 1;  /**< Enable Trace
                                                         Master enable.  Tracing only happens when ENA=1.
                                                         When ENA changes from 0 to 1, the read and write
                                                         pointers are reset to 0x00 to begin a new trace.
                                                         The MCD0 event may set ENA=0 (see MCD0_ENA
                                                         above).  When using triggers, tracing occurs only
                                                         between start and stop triggers (including the
                                                         triggers themselves).
                                                         0=disable
                                                         1=enable */
#else
	uint64_t ena                          : 1;
	uint64_t wrap                         : 1;
	uint64_t trig_ctl                     : 2;
	uint64_t time_grn                     : 3;
	uint64_t full_thr                     : 2;
	uint64_t ciu_trg                      : 1;
	uint64_t ciu_thr                      : 1;
	uint64_t mcd0_trg                     : 1;
	uint64_t mcd0_thr                     : 1;
	uint64_t mcd0_ena                     : 1;
	uint64_t ignore_o                     : 1;
	uint64_t clkalways                    : 1;
	uint64_t rdat_md                      : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_trax_ctl_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t ignore_o                     : 1;  /**< Ignore overflow during wrap mode
                                                         If set and wrapping mode is enabled, then tracing
                                                         will not stop at the overflow condition.  Each
                                                         write during an overflow will overwrite the
                                                         oldest, unread entry and the read pointer is
                                                         incremented by one entry.  This bit has no effect
                                                         if WRAP=0. */
	uint64_t mcd0_ena                     : 1;  /**< MCD0 enable
                                                         If set and any PP sends the MCD0 signal, the
                                                         tracing is disabled. */
	uint64_t mcd0_thr                     : 1;  /**< MCD0_threshold
                                                         At a fill threshold event, sends an MCD0
                                                         wire pulse that can cause cores to enter debug
                                                         mode, if enabled.  This MCD0 wire pulse will not
                                                         occur while (TRA(0..0)_INT_STATUS.MCD0_THR == 1). */
	uint64_t mcd0_trg                     : 1;  /**< MCD0_trigger
                                                         At an end trigger event, sends an MCD0
                                                         wire pulse that can cause cores to enter debug
                                                         mode, if enabled.  This MCD0 wire pulse will not
                                                         occur while (TRA(0..0)_INT_STATUS.MCD0_TRG == 1). */
	uint64_t ciu_thr                      : 1;  /**< CIU_threshold
                                                         When set during a fill threshold event,
                                                         TRA(0..0)_INT_STATUS[CIU_THR] is set, which can cause
                                                         core interrupts, if enabled. */
	uint64_t ciu_trg                      : 1;  /**< CIU_trigger
                                                         When set during an end trigger event,
                                                         TRA(0..0)_INT_STATUS[CIU_TRG] is set, which can cause
                                                         core interrupts, if enabled. */
	uint64_t full_thr                     : 2;  /**< Full Threshhold
                                                         0=none
                                                         1=1/2 full
                                                         2=3/4 full
                                                         3=4/4 full */
	uint64_t time_grn                     : 3;  /**< Timestamp granularity
                                                         granularity=8^n cycles, n=0,1,2,3,4,5,6,7 */
	uint64_t trig_ctl                     : 2;  /**< Trigger Control
                                                         Note: trigger events are written to the trace
                                                         0=no triggers
                                                         1=trigger0=start trigger, trigger1=stop trigger
                                                         2=(trigger0 || trigger1)=start trigger
                                                         3=(trigger0 || trigger1)=stop trigger */
	uint64_t wrap                         : 1;  /**< Wrap mode
                                                         When WRAP=0, the trace buffer will disable itself
                                                         after having logged 256 entries.  When WRAP=1,
                                                         the trace buffer will never disable itself.
                                                         In this case, tracing may or may not be
                                                         temporarily suspended during the overflow
                                                         condition (see IGNORE_O above).
                                                         0=do not wrap
                                                         1=wrap */
	uint64_t ena                          : 1;  /**< Enable Trace
                                                         Master enable.  Tracing only happens when ENA=1.
                                                         When ENA changes from 0 to 1, the read and write
                                                         pointers are reset to 0x00 to begin a new trace.
                                                         The MCD0 event may set ENA=0 (see MCD0_ENA
                                                         above).  When using triggers, tracing occurs only
                                                         between start and stop triggers (including the
                                                         triggers themselves).
                                                         0=disable
                                                         1=enable */
#else
	uint64_t ena                          : 1;
	uint64_t wrap                         : 1;
	uint64_t trig_ctl                     : 2;
	uint64_t time_grn                     : 3;
	uint64_t full_thr                     : 2;
	uint64_t ciu_trg                      : 1;
	uint64_t ciu_thr                      : 1;
	uint64_t mcd0_trg                     : 1;
	uint64_t mcd0_thr                     : 1;
	uint64_t mcd0_ena                     : 1;
	uint64_t ignore_o                     : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} cn31xx;
	struct cvmx_trax_ctl_cn31xx           cn38xx;
	struct cvmx_trax_ctl_cn31xx           cn38xxp2;
	struct cvmx_trax_ctl_cn31xx           cn52xx;
	struct cvmx_trax_ctl_cn31xx           cn52xxp1;
	struct cvmx_trax_ctl_cn31xx           cn56xx;
	struct cvmx_trax_ctl_cn31xx           cn56xxp1;
	struct cvmx_trax_ctl_cn31xx           cn58xx;
	struct cvmx_trax_ctl_cn31xx           cn58xxp1;
	struct cvmx_trax_ctl_s                cn61xx;
	struct cvmx_trax_ctl_s                cn63xx;
	struct cvmx_trax_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t clkalways                    : 1;  /**< Conditional clock enable
                                                         If set, the TRA clock is never disabled. */
	uint64_t ignore_o                     : 1;  /**< Ignore overflow during wrap mode
                                                         If set and wrapping mode is enabled, then tracing
                                                         will not stop at the overflow condition.  Each
                                                         write during an overflow will overwrite the
                                                         oldest, unread entry and the read pointer is
                                                         incremented by one entry.  This bit has no effect
                                                         if WRAP=0. */
	uint64_t mcd0_ena                     : 1;  /**< MCD0 enable
                                                         If set and any PP sends the MCD0 signal, the
                                                         tracing is disabled. */
	uint64_t mcd0_thr                     : 1;  /**< MCD0_threshold
                                                         At a fill threshold event, sends an MCD0
                                                         wire pulse that can cause cores to enter debug
                                                         mode, if enabled.  This MCD0 wire pulse will not
                                                         occur while (TRA_INT_STATUS.MCD0_THR == 1). */
	uint64_t mcd0_trg                     : 1;  /**< MCD0_trigger
                                                         At an end trigger event, sends an MCD0
                                                         wire pulse that can cause cores to enter debug
                                                         mode, if enabled.  This MCD0 wire pulse will not
                                                         occur while (TRA_INT_STATUS.MCD0_TRG == 1). */
	uint64_t ciu_thr                      : 1;  /**< CIU_threshold
                                                         When set during a fill threshold event,
                                                         TRA_INT_STATUS[CIU_THR] is set, which can cause
                                                         core interrupts, if enabled. */
	uint64_t ciu_trg                      : 1;  /**< CIU_trigger
                                                         When set during an end trigger event,
                                                         TRA_INT_STATUS[CIU_TRG] is set, which can cause
                                                         core interrupts, if enabled. */
	uint64_t full_thr                     : 2;  /**< Full Threshhold
                                                         0=none
                                                         1=1/2 full
                                                         2=3/4 full
                                                         3=4/4 full */
	uint64_t time_grn                     : 3;  /**< Timestamp granularity
                                                         granularity=8^n cycles, n=0,1,2,3,4,5,6,7 */
	uint64_t trig_ctl                     : 2;  /**< Trigger Control
                                                         Note: trigger events are written to the trace
                                                         0=no triggers
                                                         1=trigger0=start trigger, trigger1=stop trigger
                                                         2=(trigger0 || trigger1)=start trigger
                                                         3=(trigger0 || trigger1)=stop trigger */
	uint64_t wrap                         : 1;  /**< Wrap mode
                                                         When WRAP=0, the trace buffer will disable itself
                                                         after having logged 1024 entries.  When WRAP=1,
                                                         the trace buffer will never disable itself.
                                                         In this case, tracing may or may not be
                                                         temporarily suspended during the overflow
                                                         condition (see IGNORE_O above).
                                                         0=do not wrap
                                                         1=wrap */
	uint64_t ena                          : 1;  /**< Enable Trace
                                                         Master enable.  Tracing only happens when ENA=1.
                                                         When ENA changes from 0 to 1, the read and write
                                                         pointers are reset to 0x00 to begin a new trace.
                                                         The MCD0 event may set ENA=0 (see MCD0_ENA
                                                         above).  When using triggers, tracing occurs only
                                                         between start and stop triggers (including the
                                                         triggers themselves).
                                                         0=disable
                                                         1=enable */
#else
	uint64_t ena                          : 1;
	uint64_t wrap                         : 1;
	uint64_t trig_ctl                     : 2;
	uint64_t time_grn                     : 3;
	uint64_t full_thr                     : 2;
	uint64_t ciu_trg                      : 1;
	uint64_t ciu_thr                      : 1;
	uint64_t mcd0_trg                     : 1;
	uint64_t mcd0_thr                     : 1;
	uint64_t mcd0_ena                     : 1;
	uint64_t ignore_o                     : 1;
	uint64_t clkalways                    : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} cn63xxp1;
	struct cvmx_trax_ctl_s                cn66xx;
	struct cvmx_trax_ctl_s                cn68xx;
	struct cvmx_trax_ctl_s                cn68xxp1;
	struct cvmx_trax_ctl_s                cnf71xx;
};
typedef union cvmx_trax_ctl cvmx_trax_ctl_t;

/**
 * cvmx_tra#_cycles_since
 *
 * TRA_CYCLES_SINCE = Trace Buffer Cycles Since Last Write, Read/Write pointers
 *
 * Description:
 *
 * Notes:
 * This CSR is obsolete.  Use TRA_CYCLES_SINCE1 instead.
 *
 */
union cvmx_trax_cycles_since {
	uint64_t u64;
	struct cvmx_trax_cycles_since_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cycles                       : 48; /**< Cycles since the last entry was written */
	uint64_t rptr                         : 8;  /**< Read pointer */
	uint64_t wptr                         : 8;  /**< Write pointer */
#else
	uint64_t wptr                         : 8;
	uint64_t rptr                         : 8;
	uint64_t cycles                       : 48;
#endif
	} s;
	struct cvmx_trax_cycles_since_s       cn31xx;
	struct cvmx_trax_cycles_since_s       cn38xx;
	struct cvmx_trax_cycles_since_s       cn38xxp2;
	struct cvmx_trax_cycles_since_s       cn52xx;
	struct cvmx_trax_cycles_since_s       cn52xxp1;
	struct cvmx_trax_cycles_since_s       cn56xx;
	struct cvmx_trax_cycles_since_s       cn56xxp1;
	struct cvmx_trax_cycles_since_s       cn58xx;
	struct cvmx_trax_cycles_since_s       cn58xxp1;
	struct cvmx_trax_cycles_since_s       cn61xx;
	struct cvmx_trax_cycles_since_s       cn63xx;
	struct cvmx_trax_cycles_since_s       cn63xxp1;
	struct cvmx_trax_cycles_since_s       cn66xx;
	struct cvmx_trax_cycles_since_s       cn68xx;
	struct cvmx_trax_cycles_since_s       cn68xxp1;
	struct cvmx_trax_cycles_since_s       cnf71xx;
};
typedef union cvmx_trax_cycles_since cvmx_trax_cycles_since_t;

/**
 * cvmx_tra#_cycles_since1
 *
 * TRA_CYCLES_SINCE1 = Trace Buffer Cycles Since Last Write, Read/Write pointers
 *
 * Description:
 */
union cvmx_trax_cycles_since1 {
	uint64_t u64;
	struct cvmx_trax_cycles_since1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cycles                       : 40; /**< Cycles since the last entry was written */
	uint64_t reserved_22_23               : 2;
	uint64_t rptr                         : 10; /**< Read pointer */
	uint64_t reserved_10_11               : 2;
	uint64_t wptr                         : 10; /**< Write pointer */
#else
	uint64_t wptr                         : 10;
	uint64_t reserved_10_11               : 2;
	uint64_t rptr                         : 10;
	uint64_t reserved_22_23               : 2;
	uint64_t cycles                       : 40;
#endif
	} s;
	struct cvmx_trax_cycles_since1_s      cn52xx;
	struct cvmx_trax_cycles_since1_s      cn52xxp1;
	struct cvmx_trax_cycles_since1_s      cn56xx;
	struct cvmx_trax_cycles_since1_s      cn56xxp1;
	struct cvmx_trax_cycles_since1_s      cn58xx;
	struct cvmx_trax_cycles_since1_s      cn58xxp1;
	struct cvmx_trax_cycles_since1_s      cn61xx;
	struct cvmx_trax_cycles_since1_s      cn63xx;
	struct cvmx_trax_cycles_since1_s      cn63xxp1;
	struct cvmx_trax_cycles_since1_s      cn66xx;
	struct cvmx_trax_cycles_since1_s      cn68xx;
	struct cvmx_trax_cycles_since1_s      cn68xxp1;
	struct cvmx_trax_cycles_since1_s      cnf71xx;
};
typedef union cvmx_trax_cycles_since1 cvmx_trax_cycles_since1_t;

/**
 * cvmx_tra#_filt_adr_adr
 *
 * TRA_FILT_ADR_ADR = Trace Buffer Filter Address Address
 *
 * Description:
 */
union cvmx_trax_filt_adr_adr {
	uint64_t u64;
	struct cvmx_trax_filt_adr_adr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t adr                          : 38; /**< Unmasked Address
                                                         The combination of TRA_FILT_ADR_ADR and
                                                         TRA_FILT_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches */
#else
	uint64_t adr                          : 38;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_trax_filt_adr_adr_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t adr                          : 36; /**< Unmasked Address
                                                         The combination of TRA(0..0)_FILT_ADR_ADR and
                                                         TRA(0..0)_FILT_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches */
#else
	uint64_t adr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn38xx;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn38xxp2;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn52xx;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn52xxp1;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn56xx;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn56xxp1;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn58xx;
	struct cvmx_trax_filt_adr_adr_cn31xx  cn58xxp1;
	struct cvmx_trax_filt_adr_adr_s       cn61xx;
	struct cvmx_trax_filt_adr_adr_s       cn63xx;
	struct cvmx_trax_filt_adr_adr_s       cn63xxp1;
	struct cvmx_trax_filt_adr_adr_s       cn66xx;
	struct cvmx_trax_filt_adr_adr_s       cn68xx;
	struct cvmx_trax_filt_adr_adr_s       cn68xxp1;
	struct cvmx_trax_filt_adr_adr_s       cnf71xx;
};
typedef union cvmx_trax_filt_adr_adr cvmx_trax_filt_adr_adr_t;

/**
 * cvmx_tra#_filt_adr_msk
 *
 * TRA_FILT_ADR_MSK = Trace Buffer Filter Address Mask
 *
 * Description:
 */
union cvmx_trax_filt_adr_msk {
	uint64_t u64;
	struct cvmx_trax_filt_adr_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t adr                          : 38; /**< Address Mask
                                                         The combination of TRA_FILT_ADR_ADR and
                                                         TRA_FILT_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches.  When a mask bit is not
                                                         set, the corresponding address bits are assumed
                                                         to match.  Also, note that IOBDMAs do not have
                                                         proper addresses, so when TRA_FILT_CMD[IOBDMA]
                                                         is set, TRA_FILT_ADR_MSK must be zero to
                                                         guarantee that any IOBDMAs enter the trace. */
#else
	uint64_t adr                          : 38;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_trax_filt_adr_msk_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t adr                          : 36; /**< Address Mask
                                                         The combination of TRA(0..0)_FILT_ADR_ADR and
                                                         TRA(0..0)_FILT_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches.  When a mask bit is not
                                                         set, the corresponding address bits are assumed
                                                         to match.  Also, note that IOBDMAs do not have
                                                         proper addresses, so when TRA(0..0)_FILT_CMD[IOBDMA]
                                                         is set, TRA(0..0)_FILT_ADR_MSK must be zero to
                                                         guarantee that any IOBDMAs enter the trace. */
#else
	uint64_t adr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn38xx;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn38xxp2;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn52xx;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn52xxp1;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn56xx;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn56xxp1;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn58xx;
	struct cvmx_trax_filt_adr_msk_cn31xx  cn58xxp1;
	struct cvmx_trax_filt_adr_msk_s       cn61xx;
	struct cvmx_trax_filt_adr_msk_s       cn63xx;
	struct cvmx_trax_filt_adr_msk_s       cn63xxp1;
	struct cvmx_trax_filt_adr_msk_s       cn66xx;
	struct cvmx_trax_filt_adr_msk_s       cn68xx;
	struct cvmx_trax_filt_adr_msk_s       cn68xxp1;
	struct cvmx_trax_filt_adr_msk_s       cnf71xx;
};
typedef union cvmx_trax_filt_adr_msk cvmx_trax_filt_adr_msk_t;

/**
 * cvmx_tra#_filt_cmd
 *
 * TRA_FILT_CMD = Trace Buffer Filter Command Mask
 *
 * Description:
 *
 * Notes:
 * Note that the trace buffer does not do proper IOBDMA address compares.  Thus, if IOBDMA is set, then
 * the address compare must be disabled (i.e. TRA_FILT_ADR_MSK set to zero) to guarantee that IOBDMAs
 * enter the trace.
 */
union cvmx_trax_filt_cmd {
	uint64_t u64;
	struct cvmx_trax_filt_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t saa64                        : 1;  /**< Enable SAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t saa32                        : 1;  /**< Enable SAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_60_61               : 2;
	uint64_t faa64                        : 1;  /**< Enable FAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t faa32                        : 1;  /**< Enable FAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_56_57               : 2;
	uint64_t decr64                       : 1;  /**< Enable DECR64  tracing
                                                         0=disable, 1=enable */
	uint64_t decr32                       : 1;  /**< Enable DECR32  tracing
                                                         0=disable, 1=enable */
	uint64_t decr16                       : 1;  /**< Enable DECR16  tracing
                                                         0=disable, 1=enable */
	uint64_t decr8                        : 1;  /**< Enable DECR8   tracing
                                                         0=disable, 1=enable */
	uint64_t incr64                       : 1;  /**< Enable INCR64  tracing
                                                         0=disable, 1=enable */
	uint64_t incr32                       : 1;  /**< Enable INCR32  tracing
                                                         0=disable, 1=enable */
	uint64_t incr16                       : 1;  /**< Enable INCR16  tracing
                                                         0=disable, 1=enable */
	uint64_t incr8                        : 1;  /**< Enable INCR8   tracing
                                                         0=disable, 1=enable */
	uint64_t clr64                        : 1;  /**< Enable CLR64   tracing
                                                         0=disable, 1=enable */
	uint64_t clr32                        : 1;  /**< Enable CLR32   tracing
                                                         0=disable, 1=enable */
	uint64_t clr16                        : 1;  /**< Enable CLR16   tracing
                                                         0=disable, 1=enable */
	uint64_t clr8                         : 1;  /**< Enable CLR8    tracing
                                                         0=disable, 1=enable */
	uint64_t set64                        : 1;  /**< Enable SET64   tracing
                                                         0=disable, 1=enable */
	uint64_t set32                        : 1;  /**< Enable SET32   tracing
                                                         0=disable, 1=enable */
	uint64_t set16                        : 1;  /**< Enable SET16   tracing
                                                         0=disable, 1=enable */
	uint64_t set8                         : 1;  /**< Enable SET8    tracing
                                                         0=disable, 1=enable */
	uint64_t iobst64                      : 1;  /**< Enable IOBST64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst32                      : 1;  /**< Enable IOBST32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst16                      : 1;  /**< Enable IOBST16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst8                       : 1;  /**< Enable IOBST8  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_32_35               : 4;
	uint64_t lckl2                        : 1;  /**< Enable LCKL2   tracing
                                                         0=disable, 1=enable */
	uint64_t wbl2                         : 1;  /**< Enable WBL2    tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2                        : 1;  /**< Enable WBIL2   tracing
                                                         0=disable, 1=enable */
	uint64_t invl2                        : 1;  /**< Enable INVL2   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_27_27               : 1;
	uint64_t stgl2i                       : 1;  /**< Enable STGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t ltgl2i                       : 1;  /**< Enable LTGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2i                       : 1;  /**< Enable WBIL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t fas64                        : 1;  /**< Enable FAS64   tracing
                                                         0=disable, 1=enable */
	uint64_t fas32                        : 1;  /**< Enable FAS32   tracing
                                                         0=disable, 1=enable */
	uint64_t sttil1                       : 1;  /**< Enable STTIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stfil1                       : 1;  /**< Enable STFIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_16_19               : 4;
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_0_13                : 14;
#else
	uint64_t reserved_0_13                : 14;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t reserved_16_19               : 4;
	uint64_t stfil1                       : 1;
	uint64_t sttil1                       : 1;
	uint64_t fas32                        : 1;
	uint64_t fas64                        : 1;
	uint64_t wbil2i                       : 1;
	uint64_t ltgl2i                       : 1;
	uint64_t stgl2i                       : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t invl2                        : 1;
	uint64_t wbil2                        : 1;
	uint64_t wbl2                         : 1;
	uint64_t lckl2                        : 1;
	uint64_t reserved_32_35               : 4;
	uint64_t iobst8                       : 1;
	uint64_t iobst16                      : 1;
	uint64_t iobst32                      : 1;
	uint64_t iobst64                      : 1;
	uint64_t set8                         : 1;
	uint64_t set16                        : 1;
	uint64_t set32                        : 1;
	uint64_t set64                        : 1;
	uint64_t clr8                         : 1;
	uint64_t clr16                        : 1;
	uint64_t clr32                        : 1;
	uint64_t clr64                        : 1;
	uint64_t incr8                        : 1;
	uint64_t incr16                       : 1;
	uint64_t incr32                       : 1;
	uint64_t incr64                       : 1;
	uint64_t decr8                        : 1;
	uint64_t decr16                       : 1;
	uint64_t decr32                       : 1;
	uint64_t decr64                       : 1;
	uint64_t reserved_56_57               : 2;
	uint64_t faa32                        : 1;
	uint64_t faa64                        : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t saa32                        : 1;
	uint64_t saa64                        : 1;
#endif
	} s;
	struct cvmx_trax_filt_cmd_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t dwb                          : 1;
	uint64_t pl2                          : 1;
	uint64_t psl1                         : 1;
	uint64_t ldd                          : 1;
	uint64_t ldi                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stf                          : 1;
	uint64_t stc                          : 1;
	uint64_t stp                          : 1;
	uint64_t stt                          : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} cn31xx;
	struct cvmx_trax_filt_cmd_cn31xx      cn38xx;
	struct cvmx_trax_filt_cmd_cn31xx      cn38xxp2;
	struct cvmx_trax_filt_cmd_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t saa                          : 1;  /**< Enable SAA     tracing
                                                         0=disable, 1=enable */
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t dwb                          : 1;
	uint64_t pl2                          : 1;
	uint64_t psl1                         : 1;
	uint64_t ldd                          : 1;
	uint64_t ldi                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stf                          : 1;
	uint64_t stc                          : 1;
	uint64_t stp                          : 1;
	uint64_t stt                          : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t saa                          : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn52xx;
	struct cvmx_trax_filt_cmd_cn52xx      cn52xxp1;
	struct cvmx_trax_filt_cmd_cn52xx      cn56xx;
	struct cvmx_trax_filt_cmd_cn52xx      cn56xxp1;
	struct cvmx_trax_filt_cmd_cn52xx      cn58xx;
	struct cvmx_trax_filt_cmd_cn52xx      cn58xxp1;
	struct cvmx_trax_filt_cmd_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t saa64                        : 1;  /**< Enable SAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t saa32                        : 1;  /**< Enable SAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_60_61               : 2;
	uint64_t faa64                        : 1;  /**< Enable FAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t faa32                        : 1;  /**< Enable FAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_56_57               : 2;
	uint64_t decr64                       : 1;  /**< Enable DECR64  tracing
                                                         0=disable, 1=enable */
	uint64_t decr32                       : 1;  /**< Enable DECR32  tracing
                                                         0=disable, 1=enable */
	uint64_t decr16                       : 1;  /**< Enable DECR16  tracing
                                                         0=disable, 1=enable */
	uint64_t decr8                        : 1;  /**< Enable DECR8   tracing
                                                         0=disable, 1=enable */
	uint64_t incr64                       : 1;  /**< Enable INCR64  tracing
                                                         0=disable, 1=enable */
	uint64_t incr32                       : 1;  /**< Enable INCR32  tracing
                                                         0=disable, 1=enable */
	uint64_t incr16                       : 1;  /**< Enable INCR16  tracing
                                                         0=disable, 1=enable */
	uint64_t incr8                        : 1;  /**< Enable INCR8   tracing
                                                         0=disable, 1=enable */
	uint64_t clr64                        : 1;  /**< Enable CLR64   tracing
                                                         0=disable, 1=enable */
	uint64_t clr32                        : 1;  /**< Enable CLR32   tracing
                                                         0=disable, 1=enable */
	uint64_t clr16                        : 1;  /**< Enable CLR16   tracing
                                                         0=disable, 1=enable */
	uint64_t clr8                         : 1;  /**< Enable CLR8    tracing
                                                         0=disable, 1=enable */
	uint64_t set64                        : 1;  /**< Enable SET64   tracing
                                                         0=disable, 1=enable */
	uint64_t set32                        : 1;  /**< Enable SET32   tracing
                                                         0=disable, 1=enable */
	uint64_t set16                        : 1;  /**< Enable SET16   tracing
                                                         0=disable, 1=enable */
	uint64_t set8                         : 1;  /**< Enable SET8    tracing
                                                         0=disable, 1=enable */
	uint64_t iobst64                      : 1;  /**< Enable IOBST64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst32                      : 1;  /**< Enable IOBST32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst16                      : 1;  /**< Enable IOBST16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst8                       : 1;  /**< Enable IOBST8  tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t lckl2                        : 1;  /**< Enable LCKL2   tracing
                                                         0=disable, 1=enable */
	uint64_t wbl2                         : 1;  /**< Enable WBL2    tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2                        : 1;  /**< Enable WBIL2   tracing
                                                         0=disable, 1=enable */
	uint64_t invl2                        : 1;  /**< Enable INVL2   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_27_27               : 1;
	uint64_t stgl2i                       : 1;  /**< Enable STGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t ltgl2i                       : 1;  /**< Enable LTGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2i                       : 1;  /**< Enable WBIL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t fas64                        : 1;  /**< Enable FAS64   tracing
                                                         0=disable, 1=enable */
	uint64_t fas32                        : 1;  /**< Enable FAS32   tracing
                                                         0=disable, 1=enable */
	uint64_t sttil1                       : 1;  /**< Enable STTIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stfil1                       : 1;  /**< Enable STFIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_10_14               : 5;
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_6_7                 : 2;
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
	uint64_t rpl2                         : 1;  /**< Enable RPL2    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t nop                          : 1;  /**< Enable NOP     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t nop                          : 1;
	uint64_t ldt                          : 1;
	uint64_t ldi                          : 1;
	uint64_t pl2                          : 1;
	uint64_t rpl2                         : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t ldd                          : 1;
	uint64_t psl1                         : 1;
	uint64_t reserved_10_14               : 5;
	uint64_t iobdma                       : 1;
	uint64_t stf                          : 1;
	uint64_t stt                          : 1;
	uint64_t stp                          : 1;
	uint64_t stc                          : 1;
	uint64_t stfil1                       : 1;
	uint64_t sttil1                       : 1;
	uint64_t fas32                        : 1;
	uint64_t fas64                        : 1;
	uint64_t wbil2i                       : 1;
	uint64_t ltgl2i                       : 1;
	uint64_t stgl2i                       : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t invl2                        : 1;
	uint64_t wbil2                        : 1;
	uint64_t wbl2                         : 1;
	uint64_t lckl2                        : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst8                       : 1;
	uint64_t iobst16                      : 1;
	uint64_t iobst32                      : 1;
	uint64_t iobst64                      : 1;
	uint64_t set8                         : 1;
	uint64_t set16                        : 1;
	uint64_t set32                        : 1;
	uint64_t set64                        : 1;
	uint64_t clr8                         : 1;
	uint64_t clr16                        : 1;
	uint64_t clr32                        : 1;
	uint64_t clr64                        : 1;
	uint64_t incr8                        : 1;
	uint64_t incr16                       : 1;
	uint64_t incr32                       : 1;
	uint64_t incr64                       : 1;
	uint64_t decr8                        : 1;
	uint64_t decr16                       : 1;
	uint64_t decr32                       : 1;
	uint64_t decr64                       : 1;
	uint64_t reserved_56_57               : 2;
	uint64_t faa32                        : 1;
	uint64_t faa64                        : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t saa32                        : 1;
	uint64_t saa64                        : 1;
#endif
	} cn61xx;
	struct cvmx_trax_filt_cmd_cn61xx      cn63xx;
	struct cvmx_trax_filt_cmd_cn61xx      cn63xxp1;
	struct cvmx_trax_filt_cmd_cn61xx      cn66xx;
	struct cvmx_trax_filt_cmd_cn61xx      cn68xx;
	struct cvmx_trax_filt_cmd_cn61xx      cn68xxp1;
	struct cvmx_trax_filt_cmd_cn61xx      cnf71xx;
};
typedef union cvmx_trax_filt_cmd cvmx_trax_filt_cmd_t;

/**
 * cvmx_tra#_filt_did
 *
 * TRA_FILT_DID = Trace Buffer Filter DestinationId Mask
 *
 * Description:
 */
union cvmx_trax_filt_did {
	uint64_t u64;
	struct cvmx_trax_filt_did_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t pow                          : 1;  /**< Enable tracing of requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t reserved_9_11                : 3;
	uint64_t rng                          : 1;  /**< Enable tracing of requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable tracing of requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable tracing of requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable tracing of requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable tracing of requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t reserved_3_3                 : 1;
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable tracing of MIO accesses
                                                         (CIU and GPIO CSR's, boot bus accesses) */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t reserved_3_3                 : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t pow                          : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_trax_filt_did_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t illegal                      : 19; /**< Illegal destinations */
	uint64_t pow                          : 1;  /**< Enable tracing of requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t illegal2                     : 3;  /**< Illegal destinations */
	uint64_t rng                          : 1;  /**< Enable tracing of requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable tracing of requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable tracing of requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable tracing of requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable tracing of requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t pci                          : 1;  /**< Enable tracing of requests to PCI and RSL-type
                                                         CSR's (RSL CSR's, PCI bus operations, PCI
                                                         CSR's) */
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable tracing of CIU and GPIO CSR's */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t pci                          : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t illegal2                     : 3;
	uint64_t pow                          : 1;
	uint64_t illegal                      : 19;
	uint64_t reserved_32_63               : 32;
#endif
	} cn31xx;
	struct cvmx_trax_filt_did_cn31xx      cn38xx;
	struct cvmx_trax_filt_did_cn31xx      cn38xxp2;
	struct cvmx_trax_filt_did_cn31xx      cn52xx;
	struct cvmx_trax_filt_did_cn31xx      cn52xxp1;
	struct cvmx_trax_filt_did_cn31xx      cn56xx;
	struct cvmx_trax_filt_did_cn31xx      cn56xxp1;
	struct cvmx_trax_filt_did_cn31xx      cn58xx;
	struct cvmx_trax_filt_did_cn31xx      cn58xxp1;
	struct cvmx_trax_filt_did_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t illegal5                     : 1;  /**< Illegal destinations */
	uint64_t fau                          : 1;  /**< Enable tracing of FAU accesses */
	uint64_t illegal4                     : 2;  /**< Illegal destinations */
	uint64_t dpi                          : 1;  /**< Enable tracing of DPI accesses
                                                         (DPI NCB CSRs) */
	uint64_t illegal                      : 12; /**< Illegal destinations */
	uint64_t rad                          : 1;  /**< Enable tracing of RAD accesses
                                                         (doorbells) */
	uint64_t usb0                         : 1;  /**< Enable tracing of USB0 accesses
                                                         (UAHC0 EHCI and OHCI NCB CSRs) */
	uint64_t pow                          : 1;  /**< Enable tracing of requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t illegal2                     : 1;  /**< Illegal destination */
	uint64_t pko                          : 1;  /**< Enable tracing of PKO accesses
                                                         (doorbells) */
	uint64_t ipd                          : 1;  /**< Enable tracing of IPD CSR accesses
                                                         (IPD CSRs) */
	uint64_t rng                          : 1;  /**< Enable tracing of requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable tracing of requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable tracing of requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable tracing of requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable tracing of requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t sli                          : 1;  /**< Enable tracing of requests to SLI and RSL-type
                                                         CSR's (RSL CSR's, PCI/sRIO bus operations, SLI
                                                         CSR's) */
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable tracing of MIO accesses
                                                         (CIU and GPIO CSR's, boot bus accesses) */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t sli                          : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pko                          : 1;
	uint64_t illegal2                     : 1;
	uint64_t pow                          : 1;
	uint64_t usb0                         : 1;
	uint64_t rad                          : 1;
	uint64_t illegal                      : 12;
	uint64_t dpi                          : 1;
	uint64_t illegal4                     : 2;
	uint64_t fau                          : 1;
	uint64_t illegal5                     : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} cn61xx;
	struct cvmx_trax_filt_did_cn61xx      cn63xx;
	struct cvmx_trax_filt_did_cn61xx      cn63xxp1;
	struct cvmx_trax_filt_did_cn61xx      cn66xx;
	struct cvmx_trax_filt_did_cn61xx      cn68xx;
	struct cvmx_trax_filt_did_cn61xx      cn68xxp1;
	struct cvmx_trax_filt_did_cn61xx      cnf71xx;
};
typedef union cvmx_trax_filt_did cvmx_trax_filt_did_t;

/**
 * cvmx_tra#_filt_sid
 *
 * TRA_FILT_SID = Trace Buffer Filter SourceId Mask
 *
 * Description:
 */
union cvmx_trax_filt_sid {
	uint64_t u64;
	struct cvmx_trax_filt_sid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable tracing of requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable tracing of requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable tracing of read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable tracing of write requests from PIP/IPD */
	uint64_t pp                           : 16; /**< Enable tracing from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=3 */
#else
	uint64_t pp                           : 16;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_trax_filt_sid_s           cn31xx;
	struct cvmx_trax_filt_sid_s           cn38xx;
	struct cvmx_trax_filt_sid_s           cn38xxp2;
	struct cvmx_trax_filt_sid_s           cn52xx;
	struct cvmx_trax_filt_sid_s           cn52xxp1;
	struct cvmx_trax_filt_sid_s           cn56xx;
	struct cvmx_trax_filt_sid_s           cn56xxp1;
	struct cvmx_trax_filt_sid_s           cn58xx;
	struct cvmx_trax_filt_sid_s           cn58xxp1;
	struct cvmx_trax_filt_sid_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable tracing of requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable tracing of requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable tracing of read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable tracing of write requests from PIP/IPD */
	uint64_t reserved_4_15                : 12;
	uint64_t pp                           : 4;  /**< Enable tracing from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=3 */
#else
	uint64_t pp                           : 4;
	uint64_t reserved_4_15                : 12;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn61xx;
	struct cvmx_trax_filt_sid_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable tracing of requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable tracing of requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable tracing of read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable tracing of write requests from PIP/IPD */
	uint64_t reserved_8_15                : 8;
	uint64_t pp                           : 8;  /**< Enable tracing from PP[N] with matching SourceID
                                                         0=disable, 1=enableper bit N where  0<=N<=15 */
#else
	uint64_t pp                           : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn63xx;
	struct cvmx_trax_filt_sid_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable tracing of requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable tracing of requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable tracing of read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable tracing of write requests from PIP/IPD */
	uint64_t reserved_6_15                : 10;
	uint64_t pp                           : 6;  /**< Enable tracing from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=5 */
#else
	uint64_t pp                           : 6;
	uint64_t reserved_6_15                : 10;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn63xxp1;
	struct cvmx_trax_filt_sid_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable tracing of requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable tracing of requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable tracing of read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable tracing of write requests from PIP/IPD */
	uint64_t reserved_10_15               : 6;
	uint64_t pp                           : 10; /**< Enable tracing from PP[N] with matching SourceID
                                                         0=disable, 1=enableper bit N where  0<=N<=15 */
#else
	uint64_t pp                           : 10;
	uint64_t reserved_10_15               : 6;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn66xx;
	struct cvmx_trax_filt_sid_cn63xx      cn68xx;
	struct cvmx_trax_filt_sid_cn63xx      cn68xxp1;
	struct cvmx_trax_filt_sid_cn61xx      cnf71xx;
};
typedef union cvmx_trax_filt_sid cvmx_trax_filt_sid_t;

/**
 * cvmx_tra#_int_status
 *
 * TRA_INT_STATUS = Trace Buffer Interrupt Status
 *
 * Description:
 *
 * Notes:
 * During a CSR write to this register, the write data is used as a mask to clear the selected status
 * bits (status'[3:0] = status[3:0] & ~write_data[3:0]).
 */
union cvmx_trax_int_status {
	uint64_t u64;
	struct cvmx_trax_int_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mcd0_thr                     : 1;  /**< MCD0 full threshold interrupt status
                                                         0=trace buffer did not generate MCD0 wire pulse
                                                         1=trace buffer did     generate MCD0 wire pulse
                                                           and prevents additional MCD0_THR MCD0 wire pulses */
	uint64_t mcd0_trg                     : 1;  /**< MCD0 end trigger interrupt status
                                                         0=trace buffer did not generate interrupt
                                                         1=trace buffer did     generate interrupt
                                                           and prevents additional MCD0_TRG MCD0 wire pulses */
	uint64_t ciu_thr                      : 1;  /**< CIU full threshold interrupt status
                                                         0=trace buffer did not generate interrupt
                                                         1=trace buffer did     generate interrupt */
	uint64_t ciu_trg                      : 1;  /**< CIU end trigger interrupt status
                                                         0=trace buffer did not generate interrupt
                                                         1=trace buffer did     generate interrupt */
#else
	uint64_t ciu_trg                      : 1;
	uint64_t ciu_thr                      : 1;
	uint64_t mcd0_trg                     : 1;
	uint64_t mcd0_thr                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_trax_int_status_s         cn31xx;
	struct cvmx_trax_int_status_s         cn38xx;
	struct cvmx_trax_int_status_s         cn38xxp2;
	struct cvmx_trax_int_status_s         cn52xx;
	struct cvmx_trax_int_status_s         cn52xxp1;
	struct cvmx_trax_int_status_s         cn56xx;
	struct cvmx_trax_int_status_s         cn56xxp1;
	struct cvmx_trax_int_status_s         cn58xx;
	struct cvmx_trax_int_status_s         cn58xxp1;
	struct cvmx_trax_int_status_s         cn61xx;
	struct cvmx_trax_int_status_s         cn63xx;
	struct cvmx_trax_int_status_s         cn63xxp1;
	struct cvmx_trax_int_status_s         cn66xx;
	struct cvmx_trax_int_status_s         cn68xx;
	struct cvmx_trax_int_status_s         cn68xxp1;
	struct cvmx_trax_int_status_s         cnf71xx;
};
typedef union cvmx_trax_int_status cvmx_trax_int_status_t;

/**
 * cvmx_tra#_read_dat
 *
 * TRA_READ_DAT = Trace Buffer Read Data
 *
 * Description:
 *
 * Notes:
 * This CSR is a memory of 1024 entries.  When the trace was enabled, the read pointer was set to entry
 * 0 by hardware.  Each read to this address increments the read pointer.
 */
union cvmx_trax_read_dat {
	uint64_t u64;
	struct cvmx_trax_read_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< Trace buffer data for current entry
                                                         if TRA_CTL[16]== 1; returns lower 64 bits of entry
                                                         else two access are necessary to get all of 69bits
                                                         first access of a pair is the lower 64 bits and
                                                         second access is the upper 5 bits. */
#else
	uint64_t data                         : 64;
#endif
	} s;
	struct cvmx_trax_read_dat_s           cn31xx;
	struct cvmx_trax_read_dat_s           cn38xx;
	struct cvmx_trax_read_dat_s           cn38xxp2;
	struct cvmx_trax_read_dat_s           cn52xx;
	struct cvmx_trax_read_dat_s           cn52xxp1;
	struct cvmx_trax_read_dat_s           cn56xx;
	struct cvmx_trax_read_dat_s           cn56xxp1;
	struct cvmx_trax_read_dat_s           cn58xx;
	struct cvmx_trax_read_dat_s           cn58xxp1;
	struct cvmx_trax_read_dat_s           cn61xx;
	struct cvmx_trax_read_dat_s           cn63xx;
	struct cvmx_trax_read_dat_s           cn63xxp1;
	struct cvmx_trax_read_dat_s           cn66xx;
	struct cvmx_trax_read_dat_s           cn68xx;
	struct cvmx_trax_read_dat_s           cn68xxp1;
	struct cvmx_trax_read_dat_s           cnf71xx;
};
typedef union cvmx_trax_read_dat cvmx_trax_read_dat_t;

/**
 * cvmx_tra#_read_dat_hi
 *
 * TRA_READ_DAT_HI = Trace Buffer Read Data- upper 5 bits do not use if TRA_CTL[16]==0
 *
 * Description:
 *
 * Notes:
 * This CSR is a memory of 1024 entries. Reads to this address do not increment the read pointer.  The
 * 5 bits read are the upper 5 bits of the TRA entry last read by the TRA_READ_DAT reg.
 */
union cvmx_trax_read_dat_hi {
	uint64_t u64;
	struct cvmx_trax_read_dat_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t data                         : 5;  /**< Trace buffer data[68:64] for current entry */
#else
	uint64_t data                         : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_trax_read_dat_hi_s        cn61xx;
	struct cvmx_trax_read_dat_hi_s        cn63xx;
	struct cvmx_trax_read_dat_hi_s        cn66xx;
	struct cvmx_trax_read_dat_hi_s        cn68xx;
	struct cvmx_trax_read_dat_hi_s        cn68xxp1;
	struct cvmx_trax_read_dat_hi_s        cnf71xx;
};
typedef union cvmx_trax_read_dat_hi cvmx_trax_read_dat_hi_t;

/**
 * cvmx_tra#_trig0_adr_adr
 *
 * TRA_TRIG0_ADR_ADR = Trace Buffer Filter Address Address
 *
 * Description:
 */
union cvmx_trax_trig0_adr_adr {
	uint64_t u64;
	struct cvmx_trax_trig0_adr_adr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t adr                          : 38; /**< Unmasked Address
                                                         The combination of TRA_TRIG0_ADR_ADR and
                                                         TRA_TRIG0_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches */
#else
	uint64_t adr                          : 38;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_trax_trig0_adr_adr_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t adr                          : 36; /**< Unmasked Address
                                                         The combination of TRA(0..0)_TRIG0_ADR_ADR and
                                                         TRA(0..0)_TRIG0_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches */
#else
	uint64_t adr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn38xx;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn38xxp2;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn52xx;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn52xxp1;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn56xx;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn56xxp1;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn58xx;
	struct cvmx_trax_trig0_adr_adr_cn31xx cn58xxp1;
	struct cvmx_trax_trig0_adr_adr_s      cn61xx;
	struct cvmx_trax_trig0_adr_adr_s      cn63xx;
	struct cvmx_trax_trig0_adr_adr_s      cn63xxp1;
	struct cvmx_trax_trig0_adr_adr_s      cn66xx;
	struct cvmx_trax_trig0_adr_adr_s      cn68xx;
	struct cvmx_trax_trig0_adr_adr_s      cn68xxp1;
	struct cvmx_trax_trig0_adr_adr_s      cnf71xx;
};
typedef union cvmx_trax_trig0_adr_adr cvmx_trax_trig0_adr_adr_t;

/**
 * cvmx_tra#_trig0_adr_msk
 *
 * TRA_TRIG0_ADR_MSK = Trace Buffer Filter Address Mask
 *
 * Description:
 */
union cvmx_trax_trig0_adr_msk {
	uint64_t u64;
	struct cvmx_trax_trig0_adr_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t adr                          : 38; /**< Address Mask
                                                         The combination of TRA_TRIG0_ADR_ADR and
                                                         TRA_TRIG0_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches.  When a mask bit is not
                                                         set, the corresponding address bits are assumed
                                                         to match.  Also, note that IOBDMAs do not have
                                                         proper addresses, so when TRA_TRIG0_CMD[IOBDMA]
                                                         is set, TRA_FILT_TRIG0_MSK must be zero to
                                                         guarantee that any IOBDMAs are recognized as
                                                         triggers. */
#else
	uint64_t adr                          : 38;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_trax_trig0_adr_msk_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t adr                          : 36; /**< Address Mask
                                                         The combination of TRA(0..0)_TRIG0_ADR_ADR and
                                                         TRA(0..0)_TRIG0_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches.  When a mask bit is not
                                                         set, the corresponding address bits are assumed
                                                         to match.  Also, note that IOBDMAs do not have
                                                         proper addresses, so when TRA(0..0)_TRIG0_CMD[IOBDMA]
                                                         is set, TRA(0..0)_FILT_TRIG0_MSK must be zero to
                                                         guarantee that any IOBDMAs are recognized as
                                                         triggers. */
#else
	uint64_t adr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn38xx;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn38xxp2;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn52xx;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn52xxp1;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn56xx;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn56xxp1;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn58xx;
	struct cvmx_trax_trig0_adr_msk_cn31xx cn58xxp1;
	struct cvmx_trax_trig0_adr_msk_s      cn61xx;
	struct cvmx_trax_trig0_adr_msk_s      cn63xx;
	struct cvmx_trax_trig0_adr_msk_s      cn63xxp1;
	struct cvmx_trax_trig0_adr_msk_s      cn66xx;
	struct cvmx_trax_trig0_adr_msk_s      cn68xx;
	struct cvmx_trax_trig0_adr_msk_s      cn68xxp1;
	struct cvmx_trax_trig0_adr_msk_s      cnf71xx;
};
typedef union cvmx_trax_trig0_adr_msk cvmx_trax_trig0_adr_msk_t;

/**
 * cvmx_tra#_trig0_cmd
 *
 * TRA_TRIG0_CMD = Trace Buffer Filter Command Mask
 *
 * Description:
 *
 * Notes:
 * Note that the trace buffer does not do proper IOBDMA address compares.  Thus, if IOBDMA is set, then
 * the address compare must be disabled (i.e. TRA_TRIG0_ADR_MSK set to zero) to guarantee that IOBDMAs
 * are recognized as triggers.
 */
union cvmx_trax_trig0_cmd {
	uint64_t u64;
	struct cvmx_trax_trig0_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t saa64                        : 1;  /**< Enable SAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t saa32                        : 1;  /**< Enable SAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_60_61               : 2;
	uint64_t faa64                        : 1;  /**< Enable FAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t faa32                        : 1;  /**< Enable FAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_56_57               : 2;
	uint64_t decr64                       : 1;  /**< Enable DECR64  tracing
                                                         0=disable, 1=enable */
	uint64_t decr32                       : 1;  /**< Enable DECR32  tracing
                                                         0=disable, 1=enable */
	uint64_t decr16                       : 1;  /**< Enable DECR16  tracing
                                                         0=disable, 1=enable */
	uint64_t decr8                        : 1;  /**< Enable DECR8   tracing
                                                         0=disable, 1=enable */
	uint64_t incr64                       : 1;  /**< Enable INCR64  tracing
                                                         0=disable, 1=enable */
	uint64_t incr32                       : 1;  /**< Enable INCR32  tracing
                                                         0=disable, 1=enable */
	uint64_t incr16                       : 1;  /**< Enable INCR16  tracing
                                                         0=disable, 1=enable */
	uint64_t incr8                        : 1;  /**< Enable INCR8   tracing
                                                         0=disable, 1=enable */
	uint64_t clr64                        : 1;  /**< Enable CLR64   tracing
                                                         0=disable, 1=enable */
	uint64_t clr32                        : 1;  /**< Enable CLR32   tracing
                                                         0=disable, 1=enable */
	uint64_t clr16                        : 1;  /**< Enable CLR16   tracing
                                                         0=disable, 1=enable */
	uint64_t clr8                         : 1;  /**< Enable CLR8    tracing
                                                         0=disable, 1=enable */
	uint64_t set64                        : 1;  /**< Enable SET64   tracing
                                                         0=disable, 1=enable */
	uint64_t set32                        : 1;  /**< Enable SET32   tracing
                                                         0=disable, 1=enable */
	uint64_t set16                        : 1;  /**< Enable SET16   tracing
                                                         0=disable, 1=enable */
	uint64_t set8                         : 1;  /**< Enable SET8    tracing
                                                         0=disable, 1=enable */
	uint64_t iobst64                      : 1;  /**< Enable IOBST64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst32                      : 1;  /**< Enable IOBST32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst16                      : 1;  /**< Enable IOBST16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst8                       : 1;  /**< Enable IOBST8  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_32_35               : 4;
	uint64_t lckl2                        : 1;  /**< Enable LCKL2   tracing
                                                         0=disable, 1=enable */
	uint64_t wbl2                         : 1;  /**< Enable WBL2    tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2                        : 1;  /**< Enable WBIL2   tracing
                                                         0=disable, 1=enable */
	uint64_t invl2                        : 1;  /**< Enable INVL2   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_27_27               : 1;
	uint64_t stgl2i                       : 1;  /**< Enable STGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t ltgl2i                       : 1;  /**< Enable LTGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2i                       : 1;  /**< Enable WBIL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t fas64                        : 1;  /**< Enable FAS64   tracing
                                                         0=disable, 1=enable */
	uint64_t fas32                        : 1;  /**< Enable FAS32   tracing
                                                         0=disable, 1=enable */
	uint64_t sttil1                       : 1;  /**< Enable STTIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stfil1                       : 1;  /**< Enable STFIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_16_19               : 4;
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_0_13                : 14;
#else
	uint64_t reserved_0_13                : 14;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t reserved_16_19               : 4;
	uint64_t stfil1                       : 1;
	uint64_t sttil1                       : 1;
	uint64_t fas32                        : 1;
	uint64_t fas64                        : 1;
	uint64_t wbil2i                       : 1;
	uint64_t ltgl2i                       : 1;
	uint64_t stgl2i                       : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t invl2                        : 1;
	uint64_t wbil2                        : 1;
	uint64_t wbl2                         : 1;
	uint64_t lckl2                        : 1;
	uint64_t reserved_32_35               : 4;
	uint64_t iobst8                       : 1;
	uint64_t iobst16                      : 1;
	uint64_t iobst32                      : 1;
	uint64_t iobst64                      : 1;
	uint64_t set8                         : 1;
	uint64_t set16                        : 1;
	uint64_t set32                        : 1;
	uint64_t set64                        : 1;
	uint64_t clr8                         : 1;
	uint64_t clr16                        : 1;
	uint64_t clr32                        : 1;
	uint64_t clr64                        : 1;
	uint64_t incr8                        : 1;
	uint64_t incr16                       : 1;
	uint64_t incr32                       : 1;
	uint64_t incr64                       : 1;
	uint64_t decr8                        : 1;
	uint64_t decr16                       : 1;
	uint64_t decr32                       : 1;
	uint64_t decr64                       : 1;
	uint64_t reserved_56_57               : 2;
	uint64_t faa32                        : 1;
	uint64_t faa64                        : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t saa32                        : 1;
	uint64_t saa64                        : 1;
#endif
	} s;
	struct cvmx_trax_trig0_cmd_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t dwb                          : 1;
	uint64_t pl2                          : 1;
	uint64_t psl1                         : 1;
	uint64_t ldd                          : 1;
	uint64_t ldi                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stf                          : 1;
	uint64_t stc                          : 1;
	uint64_t stp                          : 1;
	uint64_t stt                          : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} cn31xx;
	struct cvmx_trax_trig0_cmd_cn31xx     cn38xx;
	struct cvmx_trax_trig0_cmd_cn31xx     cn38xxp2;
	struct cvmx_trax_trig0_cmd_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t saa                          : 1;  /**< Enable SAA     tracing
                                                         0=disable, 1=enable */
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t dwb                          : 1;
	uint64_t pl2                          : 1;
	uint64_t psl1                         : 1;
	uint64_t ldd                          : 1;
	uint64_t ldi                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stf                          : 1;
	uint64_t stc                          : 1;
	uint64_t stp                          : 1;
	uint64_t stt                          : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t saa                          : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn52xx;
	struct cvmx_trax_trig0_cmd_cn52xx     cn52xxp1;
	struct cvmx_trax_trig0_cmd_cn52xx     cn56xx;
	struct cvmx_trax_trig0_cmd_cn52xx     cn56xxp1;
	struct cvmx_trax_trig0_cmd_cn52xx     cn58xx;
	struct cvmx_trax_trig0_cmd_cn52xx     cn58xxp1;
	struct cvmx_trax_trig0_cmd_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t saa64                        : 1;  /**< Enable SAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t saa32                        : 1;  /**< Enable SAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_60_61               : 2;
	uint64_t faa64                        : 1;  /**< Enable FAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t faa32                        : 1;  /**< Enable FAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_56_57               : 2;
	uint64_t decr64                       : 1;  /**< Enable DECR64  tracing
                                                         0=disable, 1=enable */
	uint64_t decr32                       : 1;  /**< Enable DECR32  tracing
                                                         0=disable, 1=enable */
	uint64_t decr16                       : 1;  /**< Enable DECR16  tracing
                                                         0=disable, 1=enable */
	uint64_t decr8                        : 1;  /**< Enable DECR8   tracing
                                                         0=disable, 1=enable */
	uint64_t incr64                       : 1;  /**< Enable INCR64  tracing
                                                         0=disable, 1=enable */
	uint64_t incr32                       : 1;  /**< Enable INCR32  tracing
                                                         0=disable, 1=enable */
	uint64_t incr16                       : 1;  /**< Enable INCR16  tracing
                                                         0=disable, 1=enable */
	uint64_t incr8                        : 1;  /**< Enable INCR8   tracing
                                                         0=disable, 1=enable */
	uint64_t clr64                        : 1;  /**< Enable CLR64   tracing
                                                         0=disable, 1=enable */
	uint64_t clr32                        : 1;  /**< Enable CLR32   tracing
                                                         0=disable, 1=enable */
	uint64_t clr16                        : 1;  /**< Enable CLR16   tracing
                                                         0=disable, 1=enable */
	uint64_t clr8                         : 1;  /**< Enable CLR8    tracing
                                                         0=disable, 1=enable */
	uint64_t set64                        : 1;  /**< Enable SET64   tracing
                                                         0=disable, 1=enable */
	uint64_t set32                        : 1;  /**< Enable SET32   tracing
                                                         0=disable, 1=enable */
	uint64_t set16                        : 1;  /**< Enable SET16   tracing
                                                         0=disable, 1=enable */
	uint64_t set8                         : 1;  /**< Enable SET8    tracing
                                                         0=disable, 1=enable */
	uint64_t iobst64                      : 1;  /**< Enable IOBST64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst32                      : 1;  /**< Enable IOBST32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst16                      : 1;  /**< Enable IOBST16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst8                       : 1;  /**< Enable IOBST8  tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t lckl2                        : 1;  /**< Enable LCKL2   tracing
                                                         0=disable, 1=enable */
	uint64_t wbl2                         : 1;  /**< Enable WBL2    tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2                        : 1;  /**< Enable WBIL2   tracing
                                                         0=disable, 1=enable */
	uint64_t invl2                        : 1;  /**< Enable INVL2   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_27_27               : 1;
	uint64_t stgl2i                       : 1;  /**< Enable STGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t ltgl2i                       : 1;  /**< Enable LTGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2i                       : 1;  /**< Enable WBIL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t fas64                        : 1;  /**< Enable FAS64   tracing
                                                         0=disable, 1=enable */
	uint64_t fas32                        : 1;  /**< Enable FAS32   tracing
                                                         0=disable, 1=enable */
	uint64_t sttil1                       : 1;  /**< Enable STTIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stfil1                       : 1;  /**< Enable STFIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_10_14               : 5;
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_6_7                 : 2;
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
	uint64_t rpl2                         : 1;  /**< Enable RPL2    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t nop                          : 1;  /**< Enable NOP     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t nop                          : 1;
	uint64_t ldt                          : 1;
	uint64_t ldi                          : 1;
	uint64_t pl2                          : 1;
	uint64_t rpl2                         : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t ldd                          : 1;
	uint64_t psl1                         : 1;
	uint64_t reserved_10_14               : 5;
	uint64_t iobdma                       : 1;
	uint64_t stf                          : 1;
	uint64_t stt                          : 1;
	uint64_t stp                          : 1;
	uint64_t stc                          : 1;
	uint64_t stfil1                       : 1;
	uint64_t sttil1                       : 1;
	uint64_t fas32                        : 1;
	uint64_t fas64                        : 1;
	uint64_t wbil2i                       : 1;
	uint64_t ltgl2i                       : 1;
	uint64_t stgl2i                       : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t invl2                        : 1;
	uint64_t wbil2                        : 1;
	uint64_t wbl2                         : 1;
	uint64_t lckl2                        : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst8                       : 1;
	uint64_t iobst16                      : 1;
	uint64_t iobst32                      : 1;
	uint64_t iobst64                      : 1;
	uint64_t set8                         : 1;
	uint64_t set16                        : 1;
	uint64_t set32                        : 1;
	uint64_t set64                        : 1;
	uint64_t clr8                         : 1;
	uint64_t clr16                        : 1;
	uint64_t clr32                        : 1;
	uint64_t clr64                        : 1;
	uint64_t incr8                        : 1;
	uint64_t incr16                       : 1;
	uint64_t incr32                       : 1;
	uint64_t incr64                       : 1;
	uint64_t decr8                        : 1;
	uint64_t decr16                       : 1;
	uint64_t decr32                       : 1;
	uint64_t decr64                       : 1;
	uint64_t reserved_56_57               : 2;
	uint64_t faa32                        : 1;
	uint64_t faa64                        : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t saa32                        : 1;
	uint64_t saa64                        : 1;
#endif
	} cn61xx;
	struct cvmx_trax_trig0_cmd_cn61xx     cn63xx;
	struct cvmx_trax_trig0_cmd_cn61xx     cn63xxp1;
	struct cvmx_trax_trig0_cmd_cn61xx     cn66xx;
	struct cvmx_trax_trig0_cmd_cn61xx     cn68xx;
	struct cvmx_trax_trig0_cmd_cn61xx     cn68xxp1;
	struct cvmx_trax_trig0_cmd_cn61xx     cnf71xx;
};
typedef union cvmx_trax_trig0_cmd cvmx_trax_trig0_cmd_t;

/**
 * cvmx_tra#_trig0_did
 *
 * TRA_TRIG0_DID = Trace Buffer Filter DestinationId Mask
 *
 * Description:
 */
union cvmx_trax_trig0_did {
	uint64_t u64;
	struct cvmx_trax_trig0_did_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t pow                          : 1;  /**< Enable triggering on requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t reserved_9_11                : 3;
	uint64_t rng                          : 1;  /**< Enable triggering on requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable triggering on requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable triggering on requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable triggering on requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable triggering on requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t reserved_3_3                 : 1;
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable triggering on MIO accesses
                                                         (CIU and GPIO CSR's, boot bus accesses) */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t reserved_3_3                 : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t pow                          : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_trax_trig0_did_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t illegal                      : 19; /**< Illegal destinations */
	uint64_t pow                          : 1;  /**< Enable triggering on requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t illegal2                     : 3;  /**< Illegal destinations */
	uint64_t rng                          : 1;  /**< Enable triggering on requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable triggering on requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable triggering on requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable triggering on requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable triggering on requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t pci                          : 1;  /**< Enable triggering on requests to PCI and RSL-type
                                                         CSR's (RSL CSR's, PCI bus operations, PCI
                                                         CSR's) */
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable triggering on CIU and GPIO CSR's */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t pci                          : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t illegal2                     : 3;
	uint64_t pow                          : 1;
	uint64_t illegal                      : 19;
	uint64_t reserved_32_63               : 32;
#endif
	} cn31xx;
	struct cvmx_trax_trig0_did_cn31xx     cn38xx;
	struct cvmx_trax_trig0_did_cn31xx     cn38xxp2;
	struct cvmx_trax_trig0_did_cn31xx     cn52xx;
	struct cvmx_trax_trig0_did_cn31xx     cn52xxp1;
	struct cvmx_trax_trig0_did_cn31xx     cn56xx;
	struct cvmx_trax_trig0_did_cn31xx     cn56xxp1;
	struct cvmx_trax_trig0_did_cn31xx     cn58xx;
	struct cvmx_trax_trig0_did_cn31xx     cn58xxp1;
	struct cvmx_trax_trig0_did_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t illegal5                     : 1;  /**< Illegal destinations */
	uint64_t fau                          : 1;  /**< Enable triggering on FAU accesses */
	uint64_t illegal4                     : 2;  /**< Illegal destinations */
	uint64_t dpi                          : 1;  /**< Enable triggering on DPI accesses
                                                         (DPI NCB CSRs) */
	uint64_t illegal                      : 12; /**< Illegal destinations */
	uint64_t rad                          : 1;  /**< Enable triggering on RAD accesses
                                                         (doorbells) */
	uint64_t usb0                         : 1;  /**< Enable triggering on USB0 accesses
                                                         (UAHC0 EHCI and OHCI NCB CSRs) */
	uint64_t pow                          : 1;  /**< Enable triggering on requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t illegal2                     : 1;  /**< Illegal destination */
	uint64_t pko                          : 1;  /**< Enable triggering on PKO accesses
                                                         (doorbells) */
	uint64_t ipd                          : 1;  /**< Enable triggering on IPD CSR accesses
                                                         (IPD CSRs) */
	uint64_t rng                          : 1;  /**< Enable triggering on requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable triggering on requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable triggering on requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable triggering on requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable triggering on requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t sli                          : 1;  /**< Enable triggering on requests to SLI and RSL-type
                                                         CSR's (RSL CSR's, PCI/sRIO bus operations, SLI
                                                         CSR's) */
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable triggering on MIO accesses
                                                         (CIU and GPIO CSR's, boot bus accesses) */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t sli                          : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pko                          : 1;
	uint64_t illegal2                     : 1;
	uint64_t pow                          : 1;
	uint64_t usb0                         : 1;
	uint64_t rad                          : 1;
	uint64_t illegal                      : 12;
	uint64_t dpi                          : 1;
	uint64_t illegal4                     : 2;
	uint64_t fau                          : 1;
	uint64_t illegal5                     : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} cn61xx;
	struct cvmx_trax_trig0_did_cn61xx     cn63xx;
	struct cvmx_trax_trig0_did_cn61xx     cn63xxp1;
	struct cvmx_trax_trig0_did_cn61xx     cn66xx;
	struct cvmx_trax_trig0_did_cn61xx     cn68xx;
	struct cvmx_trax_trig0_did_cn61xx     cn68xxp1;
	struct cvmx_trax_trig0_did_cn61xx     cnf71xx;
};
typedef union cvmx_trax_trig0_did cvmx_trax_trig0_did_t;

/**
 * cvmx_tra#_trig0_sid
 *
 * TRA_TRIG0_SID = Trace Buffer Filter SourceId Mask
 *
 * Description:
 */
union cvmx_trax_trig0_sid {
	uint64_t u64;
	struct cvmx_trax_trig0_sid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t pp                           : 16; /**< Enable triggering from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=3 */
#else
	uint64_t pp                           : 16;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_trax_trig0_sid_s          cn31xx;
	struct cvmx_trax_trig0_sid_s          cn38xx;
	struct cvmx_trax_trig0_sid_s          cn38xxp2;
	struct cvmx_trax_trig0_sid_s          cn52xx;
	struct cvmx_trax_trig0_sid_s          cn52xxp1;
	struct cvmx_trax_trig0_sid_s          cn56xx;
	struct cvmx_trax_trig0_sid_s          cn56xxp1;
	struct cvmx_trax_trig0_sid_s          cn58xx;
	struct cvmx_trax_trig0_sid_s          cn58xxp1;
	struct cvmx_trax_trig0_sid_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_4_15                : 12;
	uint64_t pp                           : 4;  /**< Enable triggering from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=3 */
#else
	uint64_t pp                           : 4;
	uint64_t reserved_4_15                : 12;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn61xx;
	struct cvmx_trax_trig0_sid_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_8_15                : 8;
	uint64_t pp                           : 8;  /**< Enable triggering from PP[N] with matching SourceID
                                                         0=disable, 1=enableper bit N where  0<=N<=15 */
#else
	uint64_t pp                           : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn63xx;
	struct cvmx_trax_trig0_sid_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_6_15                : 10;
	uint64_t pp                           : 6;  /**< Enable triggering from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=5 */
#else
	uint64_t pp                           : 6;
	uint64_t reserved_6_15                : 10;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn63xxp1;
	struct cvmx_trax_trig0_sid_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_10_15               : 6;
	uint64_t pp                           : 10; /**< Enable triggering from PP[N] with matching SourceID
                                                         0=disable, 1=enableper bit N where  0<=N<=15 */
#else
	uint64_t pp                           : 10;
	uint64_t reserved_10_15               : 6;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn66xx;
	struct cvmx_trax_trig0_sid_cn63xx     cn68xx;
	struct cvmx_trax_trig0_sid_cn63xx     cn68xxp1;
	struct cvmx_trax_trig0_sid_cn61xx     cnf71xx;
};
typedef union cvmx_trax_trig0_sid cvmx_trax_trig0_sid_t;

/**
 * cvmx_tra#_trig1_adr_adr
 *
 * TRA_TRIG1_ADR_ADR = Trace Buffer Filter Address Address
 *
 * Description:
 */
union cvmx_trax_trig1_adr_adr {
	uint64_t u64;
	struct cvmx_trax_trig1_adr_adr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t adr                          : 38; /**< Unmasked Address
                                                         The combination of TRA_TRIG1_ADR_ADR and
                                                         TRA_TRIG1_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches */
#else
	uint64_t adr                          : 38;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_trax_trig1_adr_adr_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t adr                          : 36; /**< Unmasked Address
                                                         The combination of TRA(0..0)_TRIG1_ADR_ADR and
                                                         TRA(0..0)_TRIG1_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches */
#else
	uint64_t adr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn38xx;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn38xxp2;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn52xx;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn52xxp1;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn56xx;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn56xxp1;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn58xx;
	struct cvmx_trax_trig1_adr_adr_cn31xx cn58xxp1;
	struct cvmx_trax_trig1_adr_adr_s      cn61xx;
	struct cvmx_trax_trig1_adr_adr_s      cn63xx;
	struct cvmx_trax_trig1_adr_adr_s      cn63xxp1;
	struct cvmx_trax_trig1_adr_adr_s      cn66xx;
	struct cvmx_trax_trig1_adr_adr_s      cn68xx;
	struct cvmx_trax_trig1_adr_adr_s      cn68xxp1;
	struct cvmx_trax_trig1_adr_adr_s      cnf71xx;
};
typedef union cvmx_trax_trig1_adr_adr cvmx_trax_trig1_adr_adr_t;

/**
 * cvmx_tra#_trig1_adr_msk
 *
 * TRA_TRIG1_ADR_MSK = Trace Buffer Filter Address Mask
 *
 * Description:
 */
union cvmx_trax_trig1_adr_msk {
	uint64_t u64;
	struct cvmx_trax_trig1_adr_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t adr                          : 38; /**< Address Mask
                                                         The combination of TRA_TRIG1_ADR_ADR and
                                                         TRA_TRIG1_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches.  When a mask bit is not
                                                         set, the corresponding address bits are assumed
                                                         to match.  Also, note that IOBDMAs do not have
                                                         proper addresses, so when TRA_TRIG1_CMD[IOBDMA]
                                                         is set, TRA_FILT_TRIG1_MSK must be zero to
                                                         guarantee that any IOBDMAs are recognized as
                                                         triggers. */
#else
	uint64_t adr                          : 38;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_trax_trig1_adr_msk_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t adr                          : 36; /**< Address Mask
                                                         The combination of TRA(0..0)_TRIG1_ADR_ADR and
                                                         TRA(0..0)_TRIG1_ADR_MSK is a masked address to
                                                         enable tracing of only those commands whose
                                                         masked address matches.  When a mask bit is not
                                                         set, the corresponding address bits are assumed
                                                         to match.  Also, note that IOBDMAs do not have
                                                         proper addresses, so when TRA(0..0)_TRIG1_CMD[IOBDMA]
                                                         is set, TRA(0..0)_FILT_TRIG1_MSK must be zero to
                                                         guarantee that any IOBDMAs are recognized as
                                                         triggers. */
#else
	uint64_t adr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn31xx;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn38xx;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn38xxp2;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn52xx;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn52xxp1;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn56xx;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn56xxp1;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn58xx;
	struct cvmx_trax_trig1_adr_msk_cn31xx cn58xxp1;
	struct cvmx_trax_trig1_adr_msk_s      cn61xx;
	struct cvmx_trax_trig1_adr_msk_s      cn63xx;
	struct cvmx_trax_trig1_adr_msk_s      cn63xxp1;
	struct cvmx_trax_trig1_adr_msk_s      cn66xx;
	struct cvmx_trax_trig1_adr_msk_s      cn68xx;
	struct cvmx_trax_trig1_adr_msk_s      cn68xxp1;
	struct cvmx_trax_trig1_adr_msk_s      cnf71xx;
};
typedef union cvmx_trax_trig1_adr_msk cvmx_trax_trig1_adr_msk_t;

/**
 * cvmx_tra#_trig1_cmd
 *
 * TRA_TRIG1_CMD = Trace Buffer Filter Command Mask
 *
 * Description:
 *
 * Notes:
 * Note that the trace buffer does not do proper IOBDMA address compares.  Thus, if IOBDMA is set, then
 * the address compare must be disabled (i.e. TRA_TRIG1_ADR_MSK set to zero) to guarantee that IOBDMAs
 * are recognized as triggers.
 */
union cvmx_trax_trig1_cmd {
	uint64_t u64;
	struct cvmx_trax_trig1_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t saa64                        : 1;  /**< Enable SAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t saa32                        : 1;  /**< Enable SAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_60_61               : 2;
	uint64_t faa64                        : 1;  /**< Enable FAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t faa32                        : 1;  /**< Enable FAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_56_57               : 2;
	uint64_t decr64                       : 1;  /**< Enable DECR64  tracing
                                                         0=disable, 1=enable */
	uint64_t decr32                       : 1;  /**< Enable DECR32  tracing
                                                         0=disable, 1=enable */
	uint64_t decr16                       : 1;  /**< Enable DECR16  tracing
                                                         0=disable, 1=enable */
	uint64_t decr8                        : 1;  /**< Enable DECR8   tracing
                                                         0=disable, 1=enable */
	uint64_t incr64                       : 1;  /**< Enable INCR64  tracing
                                                         0=disable, 1=enable */
	uint64_t incr32                       : 1;  /**< Enable INCR32  tracing
                                                         0=disable, 1=enable */
	uint64_t incr16                       : 1;  /**< Enable INCR16  tracing
                                                         0=disable, 1=enable */
	uint64_t incr8                        : 1;  /**< Enable INCR8   tracing
                                                         0=disable, 1=enable */
	uint64_t clr64                        : 1;  /**< Enable CLR64   tracing
                                                         0=disable, 1=enable */
	uint64_t clr32                        : 1;  /**< Enable CLR32   tracing
                                                         0=disable, 1=enable */
	uint64_t clr16                        : 1;  /**< Enable CLR16   tracing
                                                         0=disable, 1=enable */
	uint64_t clr8                         : 1;  /**< Enable CLR8    tracing
                                                         0=disable, 1=enable */
	uint64_t set64                        : 1;  /**< Enable SET64   tracing
                                                         0=disable, 1=enable */
	uint64_t set32                        : 1;  /**< Enable SET32   tracing
                                                         0=disable, 1=enable */
	uint64_t set16                        : 1;  /**< Enable SET16   tracing
                                                         0=disable, 1=enable */
	uint64_t set8                         : 1;  /**< Enable SET8    tracing
                                                         0=disable, 1=enable */
	uint64_t iobst64                      : 1;  /**< Enable IOBST64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst32                      : 1;  /**< Enable IOBST32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst16                      : 1;  /**< Enable IOBST16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst8                       : 1;  /**< Enable IOBST8  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_32_35               : 4;
	uint64_t lckl2                        : 1;  /**< Enable LCKL2   tracing
                                                         0=disable, 1=enable */
	uint64_t wbl2                         : 1;  /**< Enable WBL2    tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2                        : 1;  /**< Enable WBIL2   tracing
                                                         0=disable, 1=enable */
	uint64_t invl2                        : 1;  /**< Enable INVL2   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_27_27               : 1;
	uint64_t stgl2i                       : 1;  /**< Enable STGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t ltgl2i                       : 1;  /**< Enable LTGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2i                       : 1;  /**< Enable WBIL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t fas64                        : 1;  /**< Enable FAS64   tracing
                                                         0=disable, 1=enable */
	uint64_t fas32                        : 1;  /**< Enable FAS32   tracing
                                                         0=disable, 1=enable */
	uint64_t sttil1                       : 1;  /**< Enable STTIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stfil1                       : 1;  /**< Enable STFIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_16_19               : 4;
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_0_13                : 14;
#else
	uint64_t reserved_0_13                : 14;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t reserved_16_19               : 4;
	uint64_t stfil1                       : 1;
	uint64_t sttil1                       : 1;
	uint64_t fas32                        : 1;
	uint64_t fas64                        : 1;
	uint64_t wbil2i                       : 1;
	uint64_t ltgl2i                       : 1;
	uint64_t stgl2i                       : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t invl2                        : 1;
	uint64_t wbil2                        : 1;
	uint64_t wbl2                         : 1;
	uint64_t lckl2                        : 1;
	uint64_t reserved_32_35               : 4;
	uint64_t iobst8                       : 1;
	uint64_t iobst16                      : 1;
	uint64_t iobst32                      : 1;
	uint64_t iobst64                      : 1;
	uint64_t set8                         : 1;
	uint64_t set16                        : 1;
	uint64_t set32                        : 1;
	uint64_t set64                        : 1;
	uint64_t clr8                         : 1;
	uint64_t clr16                        : 1;
	uint64_t clr32                        : 1;
	uint64_t clr64                        : 1;
	uint64_t incr8                        : 1;
	uint64_t incr16                       : 1;
	uint64_t incr32                       : 1;
	uint64_t incr64                       : 1;
	uint64_t decr8                        : 1;
	uint64_t decr16                       : 1;
	uint64_t decr32                       : 1;
	uint64_t decr64                       : 1;
	uint64_t reserved_56_57               : 2;
	uint64_t faa32                        : 1;
	uint64_t faa64                        : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t saa32                        : 1;
	uint64_t saa64                        : 1;
#endif
	} s;
	struct cvmx_trax_trig1_cmd_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t dwb                          : 1;
	uint64_t pl2                          : 1;
	uint64_t psl1                         : 1;
	uint64_t ldd                          : 1;
	uint64_t ldi                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stf                          : 1;
	uint64_t stc                          : 1;
	uint64_t stp                          : 1;
	uint64_t stt                          : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} cn31xx;
	struct cvmx_trax_trig1_cmd_cn31xx     cn38xx;
	struct cvmx_trax_trig1_cmd_cn31xx     cn38xxp2;
	struct cvmx_trax_trig1_cmd_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t saa                          : 1;  /**< Enable SAA     tracing
                                                         0=disable, 1=enable */
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t iobst                        : 1;  /**< Enable IOBST   tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t dwb                          : 1;
	uint64_t pl2                          : 1;
	uint64_t psl1                         : 1;
	uint64_t ldd                          : 1;
	uint64_t ldi                          : 1;
	uint64_t ldt                          : 1;
	uint64_t stf                          : 1;
	uint64_t stc                          : 1;
	uint64_t stp                          : 1;
	uint64_t stt                          : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst                        : 1;
	uint64_t iobdma                       : 1;
	uint64_t saa                          : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn52xx;
	struct cvmx_trax_trig1_cmd_cn52xx     cn52xxp1;
	struct cvmx_trax_trig1_cmd_cn52xx     cn56xx;
	struct cvmx_trax_trig1_cmd_cn52xx     cn56xxp1;
	struct cvmx_trax_trig1_cmd_cn52xx     cn58xx;
	struct cvmx_trax_trig1_cmd_cn52xx     cn58xxp1;
	struct cvmx_trax_trig1_cmd_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t saa64                        : 1;  /**< Enable SAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t saa32                        : 1;  /**< Enable SAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_60_61               : 2;
	uint64_t faa64                        : 1;  /**< Enable FAA64 tracing
                                                         0=disable, 1=enable */
	uint64_t faa32                        : 1;  /**< Enable FAA32 tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_56_57               : 2;
	uint64_t decr64                       : 1;  /**< Enable DECR64  tracing
                                                         0=disable, 1=enable */
	uint64_t decr32                       : 1;  /**< Enable DECR32  tracing
                                                         0=disable, 1=enable */
	uint64_t decr16                       : 1;  /**< Enable DECR16  tracing
                                                         0=disable, 1=enable */
	uint64_t decr8                        : 1;  /**< Enable DECR8   tracing
                                                         0=disable, 1=enable */
	uint64_t incr64                       : 1;  /**< Enable INCR64  tracing
                                                         0=disable, 1=enable */
	uint64_t incr32                       : 1;  /**< Enable INCR32  tracing
                                                         0=disable, 1=enable */
	uint64_t incr16                       : 1;  /**< Enable INCR16  tracing
                                                         0=disable, 1=enable */
	uint64_t incr8                        : 1;  /**< Enable INCR8   tracing
                                                         0=disable, 1=enable */
	uint64_t clr64                        : 1;  /**< Enable CLR64   tracing
                                                         0=disable, 1=enable */
	uint64_t clr32                        : 1;  /**< Enable CLR32   tracing
                                                         0=disable, 1=enable */
	uint64_t clr16                        : 1;  /**< Enable CLR16   tracing
                                                         0=disable, 1=enable */
	uint64_t clr8                         : 1;  /**< Enable CLR8    tracing
                                                         0=disable, 1=enable */
	uint64_t set64                        : 1;  /**< Enable SET64   tracing
                                                         0=disable, 1=enable */
	uint64_t set32                        : 1;  /**< Enable SET32   tracing
                                                         0=disable, 1=enable */
	uint64_t set16                        : 1;  /**< Enable SET16   tracing
                                                         0=disable, 1=enable */
	uint64_t set8                         : 1;  /**< Enable SET8    tracing
                                                         0=disable, 1=enable */
	uint64_t iobst64                      : 1;  /**< Enable IOBST64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst32                      : 1;  /**< Enable IOBST32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst16                      : 1;  /**< Enable IOBST16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobst8                       : 1;  /**< Enable IOBST8  tracing
                                                         0=disable, 1=enable */
	uint64_t iobld64                      : 1;  /**< Enable IOBLD64 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld32                      : 1;  /**< Enable IOBLD32 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld16                      : 1;  /**< Enable IOBLD16 tracing
                                                         0=disable, 1=enable */
	uint64_t iobld8                       : 1;  /**< Enable IOBLD8  tracing
                                                         0=disable, 1=enable */
	uint64_t lckl2                        : 1;  /**< Enable LCKL2   tracing
                                                         0=disable, 1=enable */
	uint64_t wbl2                         : 1;  /**< Enable WBL2    tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2                        : 1;  /**< Enable WBIL2   tracing
                                                         0=disable, 1=enable */
	uint64_t invl2                        : 1;  /**< Enable INVL2   tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_27_27               : 1;
	uint64_t stgl2i                       : 1;  /**< Enable STGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t ltgl2i                       : 1;  /**< Enable LTGL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t wbil2i                       : 1;  /**< Enable WBIL2I  tracing
                                                         0=disable, 1=enable */
	uint64_t fas64                        : 1;  /**< Enable FAS64   tracing
                                                         0=disable, 1=enable */
	uint64_t fas32                        : 1;  /**< Enable FAS32   tracing
                                                         0=disable, 1=enable */
	uint64_t sttil1                       : 1;  /**< Enable STTIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stfil1                       : 1;  /**< Enable STFIL1  tracing
                                                         0=disable, 1=enable */
	uint64_t stc                          : 1;  /**< Enable STC     tracing
                                                         0=disable, 1=enable */
	uint64_t stp                          : 1;  /**< Enable STP     tracing
                                                         0=disable, 1=enable */
	uint64_t stt                          : 1;  /**< Enable STT     tracing
                                                         0=disable, 1=enable */
	uint64_t stf                          : 1;  /**< Enable STF     tracing
                                                         0=disable, 1=enable */
	uint64_t iobdma                       : 1;  /**< Enable IOBDMA  tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_10_14               : 5;
	uint64_t psl1                         : 1;  /**< Enable PSL1    tracing
                                                         0=disable, 1=enable */
	uint64_t ldd                          : 1;  /**< Enable LDD     tracing
                                                         0=disable, 1=enable */
	uint64_t reserved_6_7                 : 2;
	uint64_t dwb                          : 1;  /**< Enable DWB     tracing
                                                         0=disable, 1=enable */
	uint64_t rpl2                         : 1;  /**< Enable RPL2    tracing
                                                         0=disable, 1=enable */
	uint64_t pl2                          : 1;  /**< Enable PL2     tracing
                                                         0=disable, 1=enable */
	uint64_t ldi                          : 1;  /**< Enable LDI     tracing
                                                         0=disable, 1=enable */
	uint64_t ldt                          : 1;  /**< Enable LDT     tracing
                                                         0=disable, 1=enable */
	uint64_t nop                          : 1;  /**< Enable NOP     tracing
                                                         0=disable, 1=enable */
#else
	uint64_t nop                          : 1;
	uint64_t ldt                          : 1;
	uint64_t ldi                          : 1;
	uint64_t pl2                          : 1;
	uint64_t rpl2                         : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t ldd                          : 1;
	uint64_t psl1                         : 1;
	uint64_t reserved_10_14               : 5;
	uint64_t iobdma                       : 1;
	uint64_t stf                          : 1;
	uint64_t stt                          : 1;
	uint64_t stp                          : 1;
	uint64_t stc                          : 1;
	uint64_t stfil1                       : 1;
	uint64_t sttil1                       : 1;
	uint64_t fas32                        : 1;
	uint64_t fas64                        : 1;
	uint64_t wbil2i                       : 1;
	uint64_t ltgl2i                       : 1;
	uint64_t stgl2i                       : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t invl2                        : 1;
	uint64_t wbil2                        : 1;
	uint64_t wbl2                         : 1;
	uint64_t lckl2                        : 1;
	uint64_t iobld8                       : 1;
	uint64_t iobld16                      : 1;
	uint64_t iobld32                      : 1;
	uint64_t iobld64                      : 1;
	uint64_t iobst8                       : 1;
	uint64_t iobst16                      : 1;
	uint64_t iobst32                      : 1;
	uint64_t iobst64                      : 1;
	uint64_t set8                         : 1;
	uint64_t set16                        : 1;
	uint64_t set32                        : 1;
	uint64_t set64                        : 1;
	uint64_t clr8                         : 1;
	uint64_t clr16                        : 1;
	uint64_t clr32                        : 1;
	uint64_t clr64                        : 1;
	uint64_t incr8                        : 1;
	uint64_t incr16                       : 1;
	uint64_t incr32                       : 1;
	uint64_t incr64                       : 1;
	uint64_t decr8                        : 1;
	uint64_t decr16                       : 1;
	uint64_t decr32                       : 1;
	uint64_t decr64                       : 1;
	uint64_t reserved_56_57               : 2;
	uint64_t faa32                        : 1;
	uint64_t faa64                        : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t saa32                        : 1;
	uint64_t saa64                        : 1;
#endif
	} cn61xx;
	struct cvmx_trax_trig1_cmd_cn61xx     cn63xx;
	struct cvmx_trax_trig1_cmd_cn61xx     cn63xxp1;
	struct cvmx_trax_trig1_cmd_cn61xx     cn66xx;
	struct cvmx_trax_trig1_cmd_cn61xx     cn68xx;
	struct cvmx_trax_trig1_cmd_cn61xx     cn68xxp1;
	struct cvmx_trax_trig1_cmd_cn61xx     cnf71xx;
};
typedef union cvmx_trax_trig1_cmd cvmx_trax_trig1_cmd_t;

/**
 * cvmx_tra#_trig1_did
 *
 * TRA_TRIG1_DID = Trace Buffer Filter DestinationId Mask
 *
 * Description:
 */
union cvmx_trax_trig1_did {
	uint64_t u64;
	struct cvmx_trax_trig1_did_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t pow                          : 1;  /**< Enable triggering on requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t reserved_9_11                : 3;
	uint64_t rng                          : 1;  /**< Enable triggering on requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable triggering on requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable triggering on requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable triggering on requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable triggering on requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t reserved_3_3                 : 1;
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable triggering on MIO accesses
                                                         (CIU and GPIO CSR's, boot bus accesses) */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t reserved_3_3                 : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t pow                          : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_trax_trig1_did_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t illegal                      : 19; /**< Illegal destinations */
	uint64_t pow                          : 1;  /**< Enable triggering on requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t illegal2                     : 3;  /**< Illegal destinations */
	uint64_t rng                          : 1;  /**< Enable triggering on requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable triggering on requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable triggering on requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable triggering on requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable triggering on requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t pci                          : 1;  /**< Enable triggering on requests to PCI and RSL-type
                                                         CSR's (RSL CSR's, PCI bus operations, PCI
                                                         CSR's) */
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable triggering on CIU and GPIO CSR's */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t pci                          : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t illegal2                     : 3;
	uint64_t pow                          : 1;
	uint64_t illegal                      : 19;
	uint64_t reserved_32_63               : 32;
#endif
	} cn31xx;
	struct cvmx_trax_trig1_did_cn31xx     cn38xx;
	struct cvmx_trax_trig1_did_cn31xx     cn38xxp2;
	struct cvmx_trax_trig1_did_cn31xx     cn52xx;
	struct cvmx_trax_trig1_did_cn31xx     cn52xxp1;
	struct cvmx_trax_trig1_did_cn31xx     cn56xx;
	struct cvmx_trax_trig1_did_cn31xx     cn56xxp1;
	struct cvmx_trax_trig1_did_cn31xx     cn58xx;
	struct cvmx_trax_trig1_did_cn31xx     cn58xxp1;
	struct cvmx_trax_trig1_did_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t illegal5                     : 1;  /**< Illegal destinations */
	uint64_t fau                          : 1;  /**< Enable triggering on FAU accesses */
	uint64_t illegal4                     : 2;  /**< Illegal destinations */
	uint64_t dpi                          : 1;  /**< Enable triggering on DPI accesses
                                                         (DPI NCB CSRs) */
	uint64_t illegal                      : 12; /**< Illegal destinations */
	uint64_t rad                          : 1;  /**< Enable triggering on RAD accesses
                                                         (doorbells) */
	uint64_t usb0                         : 1;  /**< Enable triggering on USB0 accesses
                                                         (UAHC0 EHCI and OHCI NCB CSRs) */
	uint64_t pow                          : 1;  /**< Enable triggering on requests to POW
                                                         (get work, add work, status/memory/index
                                                         loads, NULLRd loads, CSR's) */
	uint64_t illegal2                     : 1;  /**< Illegal destination */
	uint64_t pko                          : 1;  /**< Enable triggering on PKO accesses
                                                         (doorbells) */
	uint64_t ipd                          : 1;  /**< Enable triggering on IPD CSR accesses
                                                         (IPD CSRs) */
	uint64_t rng                          : 1;  /**< Enable triggering on requests to RNG
                                                         (loads/IOBDMA's are legal) */
	uint64_t zip                          : 1;  /**< Enable triggering on requests to ZIP
                                                         (doorbell stores are legal) */
	uint64_t dfa                          : 1;  /**< Enable triggering on requests to DFA
                                                         (CSR's and operations are legal) */
	uint64_t fpa                          : 1;  /**< Enable triggering on requests to FPA
                                                         (alloc's (loads/IOBDMA's), frees (stores) are legal) */
	uint64_t key                          : 1;  /**< Enable triggering on requests to KEY memory
                                                         (loads/IOBDMA's/stores are legal) */
	uint64_t sli                          : 1;  /**< Enable triggering on requests to SLI and RSL-type
                                                         CSR's (RSL CSR's, PCI/sRIO bus operations, SLI
                                                         CSR's) */
	uint64_t illegal3                     : 2;  /**< Illegal destinations */
	uint64_t mio                          : 1;  /**< Enable triggering on MIO accesses
                                                         (CIU and GPIO CSR's, boot bus accesses) */
#else
	uint64_t mio                          : 1;
	uint64_t illegal3                     : 2;
	uint64_t sli                          : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t rng                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pko                          : 1;
	uint64_t illegal2                     : 1;
	uint64_t pow                          : 1;
	uint64_t usb0                         : 1;
	uint64_t rad                          : 1;
	uint64_t illegal                      : 12;
	uint64_t dpi                          : 1;
	uint64_t illegal4                     : 2;
	uint64_t fau                          : 1;
	uint64_t illegal5                     : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} cn61xx;
	struct cvmx_trax_trig1_did_cn61xx     cn63xx;
	struct cvmx_trax_trig1_did_cn61xx     cn63xxp1;
	struct cvmx_trax_trig1_did_cn61xx     cn66xx;
	struct cvmx_trax_trig1_did_cn61xx     cn68xx;
	struct cvmx_trax_trig1_did_cn61xx     cn68xxp1;
	struct cvmx_trax_trig1_did_cn61xx     cnf71xx;
};
typedef union cvmx_trax_trig1_did cvmx_trax_trig1_did_t;

/**
 * cvmx_tra#_trig1_sid
 *
 * TRA_TRIG1_SID = Trace Buffer Filter SourceId Mask
 *
 * Description:
 */
union cvmx_trax_trig1_sid {
	uint64_t u64;
	struct cvmx_trax_trig1_sid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t pp                           : 16; /**< Enable trigering from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=3 */
#else
	uint64_t pp                           : 16;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_trax_trig1_sid_s          cn31xx;
	struct cvmx_trax_trig1_sid_s          cn38xx;
	struct cvmx_trax_trig1_sid_s          cn38xxp2;
	struct cvmx_trax_trig1_sid_s          cn52xx;
	struct cvmx_trax_trig1_sid_s          cn52xxp1;
	struct cvmx_trax_trig1_sid_s          cn56xx;
	struct cvmx_trax_trig1_sid_s          cn56xxp1;
	struct cvmx_trax_trig1_sid_s          cn58xx;
	struct cvmx_trax_trig1_sid_s          cn58xxp1;
	struct cvmx_trax_trig1_sid_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_4_15                : 12;
	uint64_t pp                           : 4;  /**< Enable trigering from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=3 */
#else
	uint64_t pp                           : 4;
	uint64_t reserved_4_15                : 12;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn61xx;
	struct cvmx_trax_trig1_sid_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_8_15                : 8;
	uint64_t pp                           : 8;  /**< Enable trigering from PP[N] with matching SourceID
                                                         0=disable, 1=enableper bit N where  0<=N<=15 */
#else
	uint64_t pp                           : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn63xx;
	struct cvmx_trax_trig1_sid_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_6_15                : 10;
	uint64_t pp                           : 6;  /**< Enable trigering from PP[N] with matching SourceID
                                                         0=disable, 1=enable per bit N where 0<=N<=5 */
#else
	uint64_t pp                           : 6;
	uint64_t reserved_6_15                : 10;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn63xxp1;
	struct cvmx_trax_trig1_sid_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t dwb                          : 1;  /**< Enable triggering on requests from the IOB DWB engine */
	uint64_t iobreq                       : 1;  /**< Enable triggering on requests from FPA,TIM,DFA,
                                                         PCI,ZIP,POW, and PKO (writes) */
	uint64_t pko                          : 1;  /**< Enable triggering on read requests from PKO */
	uint64_t pki                          : 1;  /**< Enable triggering on write requests from PIP/IPD */
	uint64_t reserved_10_15               : 6;
	uint64_t pp                           : 10; /**< Enable trigering from PP[N] with matching SourceID
                                                         0=disable, 1=enableper bit N where  0<=N<=15 */
#else
	uint64_t pp                           : 10;
	uint64_t reserved_10_15               : 6;
	uint64_t pki                          : 1;
	uint64_t pko                          : 1;
	uint64_t iobreq                       : 1;
	uint64_t dwb                          : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn66xx;
	struct cvmx_trax_trig1_sid_cn63xx     cn68xx;
	struct cvmx_trax_trig1_sid_cn63xx     cn68xxp1;
	struct cvmx_trax_trig1_sid_cn61xx     cnf71xx;
};
typedef union cvmx_trax_trig1_sid cvmx_trax_trig1_sid_t;

#include "cvmx-tra-defs.h"
#endif
