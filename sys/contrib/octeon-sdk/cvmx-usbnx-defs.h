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
 * cvmx-usbnx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon usbnx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_USBNX_DEFS_H__
#define __CVMX_USBNX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_BIST_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_BIST_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800680007F8ull) + ((block_id) & 1) * 0x10000000ull;
}
#else
#define CVMX_USBNX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800680007F8ull) + ((block_id) & 1) * 0x10000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_CLK_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_CLK_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180068000010ull) + ((block_id) & 1) * 0x10000000ull;
}
#else
#define CVMX_USBNX_CLK_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180068000010ull) + ((block_id) & 1) * 0x10000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_CTL_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_CTL_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000800ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000800ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN0(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000818ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN0(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000818ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN1(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000820ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN1(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000820ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000828ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN2(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000828ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN3(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN3(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000830ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN3(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000830ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN4(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN4(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000838ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN4(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000838ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN5(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN5(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000840ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN5(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000840ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN6(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN6(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000848ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN6(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000848ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_INB_CHN7(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_INB_CHN7(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000850ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_INB_CHN7(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000850ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN0(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000858ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN0(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000858ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN1(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000860ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN1(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000860ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000868ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN2(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000868ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN3(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN3(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000870ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN3(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000870ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN4(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN4(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000878ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN4(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000878ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN5(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN5(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000880ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN5(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000880ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN6(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN6(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000888ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN6(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000888ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA0_OUTB_CHN7(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA0_OUTB_CHN7(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000890ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA0_OUTB_CHN7(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000890ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_DMA_TEST(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_DMA_TEST(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000808ull) + ((block_id) & 1) * 0x100000000000ull;
}
#else
#define CVMX_USBNX_DMA_TEST(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000808ull) + ((block_id) & 1) * 0x100000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_INT_ENB(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_INT_ENB(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180068000008ull) + ((block_id) & 1) * 0x10000000ull;
}
#else
#define CVMX_USBNX_INT_ENB(block_id) (CVMX_ADD_IO_SEG(0x0001180068000008ull) + ((block_id) & 1) * 0x10000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_INT_SUM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_INT_SUM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180068000000ull) + ((block_id) & 1) * 0x10000000ull;
}
#else
#define CVMX_USBNX_INT_SUM(block_id) (CVMX_ADD_IO_SEG(0x0001180068000000ull) + ((block_id) & 1) * 0x10000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_USBNX_USBP_CTL_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_USBNX_USBP_CTL_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180068000018ull) + ((block_id) & 1) * 0x10000000ull;
}
#else
#define CVMX_USBNX_USBP_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x0001180068000018ull) + ((block_id) & 1) * 0x10000000ull)
#endif

/**
 * cvmx_usbn#_bist_status
 *
 * USBN_BIST_STATUS = USBN's Control and Status
 *
 * Contain general control bits and status information for the USBN.
 */
union cvmx_usbnx_bist_status {
	uint64_t u64;
	struct cvmx_usbnx_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t u2nc_bis                     : 1;  /**< Bist status U2N CTL FIFO Memory. */
	uint64_t u2nf_bis                     : 1;  /**< Bist status U2N FIFO Memory. */
	uint64_t e2hc_bis                     : 1;  /**< Bist status E2H CTL FIFO Memory. */
	uint64_t n2uf_bis                     : 1;  /**< Bist status N2U  FIFO Memory. */
	uint64_t usbc_bis                     : 1;  /**< Bist status USBC FIFO Memory. */
	uint64_t nif_bis                      : 1;  /**< Bist status for Inbound Memory. */
	uint64_t nof_bis                      : 1;  /**< Bist status for Outbound Memory. */
#else
	uint64_t nof_bis                      : 1;
	uint64_t nif_bis                      : 1;
	uint64_t usbc_bis                     : 1;
	uint64_t n2uf_bis                     : 1;
	uint64_t e2hc_bis                     : 1;
	uint64_t u2nf_bis                     : 1;
	uint64_t u2nc_bis                     : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_usbnx_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t usbc_bis                     : 1;  /**< Bist status USBC FIFO Memory. */
	uint64_t nif_bis                      : 1;  /**< Bist status for Inbound Memory. */
	uint64_t nof_bis                      : 1;  /**< Bist status for Outbound Memory. */
#else
	uint64_t nof_bis                      : 1;
	uint64_t nif_bis                      : 1;
	uint64_t usbc_bis                     : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_usbnx_bist_status_cn30xx  cn31xx;
	struct cvmx_usbnx_bist_status_s       cn50xx;
	struct cvmx_usbnx_bist_status_s       cn52xx;
	struct cvmx_usbnx_bist_status_s       cn52xxp1;
	struct cvmx_usbnx_bist_status_s       cn56xx;
	struct cvmx_usbnx_bist_status_s       cn56xxp1;
};
typedef union cvmx_usbnx_bist_status cvmx_usbnx_bist_status_t;

/**
 * cvmx_usbn#_clk_ctl
 *
 * USBN_CLK_CTL = USBN's Clock Control
 *
 * This register is used to control the frequency of the hclk and the hreset and phy_rst signals.
 */
union cvmx_usbnx_clk_ctl {
	uint64_t u64;
	struct cvmx_usbnx_clk_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t divide2                      : 2;  /**< The 'hclk' used by the USB subsystem is derived
                                                         from the eclk.
                                                         Also see the field DIVIDE. DIVIDE2<1> must currently
                                                         be zero because it is not implemented, so the maximum
                                                         ratio of eclk/hclk is currently 16.
                                                         The actual divide number for hclk is:
                                                         (DIVIDE2 + 1) * (DIVIDE + 1) */
	uint64_t hclk_rst                     : 1;  /**< When this field is '0' the HCLK-DIVIDER used to
                                                         generate the hclk in the USB Subsystem is held
                                                         in reset. This bit must be set to '0' before
                                                         changing the value os DIVIDE in this register.
                                                         The reset to the HCLK_DIVIDERis also asserted
                                                         when core reset is asserted. */
	uint64_t p_x_on                       : 1;  /**< Force USB-PHY on during suspend.
                                                         '1' USB-PHY XO block is powered-down during
                                                             suspend.
                                                         '0' USB-PHY XO block is powered-up during
                                                             suspend.
                                                         The value of this field must be set while POR is
                                                         active. */
	uint64_t reserved_14_15               : 2;
	uint64_t p_com_on                     : 1;  /**< '0' Force USB-PHY XO Bias, Bandgap and PLL to
                                                             remain powered in Suspend Mode.
                                                         '1' The USB-PHY XO Bias, Bandgap and PLL are
                                                             powered down in suspend mode.
                                                         The value of this field must be set while POR is
                                                         active. */
	uint64_t p_c_sel                      : 2;  /**< Phy clock speed select.
                                                         Selects the reference clock / crystal frequency.
                                                         '11': Reserved
                                                         '10': 48 MHz (reserved when a crystal is used)
                                                         '01': 24 MHz (reserved when a crystal is used)
                                                         '00': 12 MHz
                                                         The value of this field must be set while POR is
                                                         active.
                                                         NOTE: if a crystal is used as a reference clock,
                                                         this field must be set to 12 MHz. */
	uint64_t cdiv_byp                     : 1;  /**< Used to enable the bypass input to the USB_CLK_DIV. */
	uint64_t sd_mode                      : 2;  /**< Scaledown mode for the USBC. Control timing events
                                                         in the USBC, for normal operation this must be '0'. */
	uint64_t s_bist                       : 1;  /**< Starts bist on the hclk memories, during the '0'
                                                         to '1' transition. */
	uint64_t por                          : 1;  /**< Power On Reset for the PHY.
                                                         Resets all the PHYS registers and state machines. */
	uint64_t enable                       : 1;  /**< When '1' allows the generation of the hclk. When
                                                         '0' the hclk will not be generated. SEE DIVIDE
                                                         field of this register. */
	uint64_t prst                         : 1;  /**< When this field is '0' the reset associated with
                                                         the phy_clk functionality in the USB Subsystem is
                                                         help in reset. This bit should not be set to '1'
                                                         until the time it takes 6 clocks (hclk or phy_clk,
                                                         whichever is slower) has passed. Under normal
                                                         operation once this bit is set to '1' it should not
                                                         be set to '0'. */
	uint64_t hrst                         : 1;  /**< When this field is '0' the reset associated with
                                                         the hclk functioanlity in the USB Subsystem is
                                                         held in reset.This bit should not be set to '1'
                                                         until 12ms after phy_clk is stable. Under normal
                                                         operation, once this bit is set to '1' it should
                                                         not be set to '0'. */
	uint64_t divide                       : 3;  /**< The frequency of 'hclk' used by the USB subsystem
                                                         is the eclk frequency divided by the value of
                                                         (DIVIDE2 + 1) * (DIVIDE + 1), also see the field
                                                         DIVIDE2 of this register.
                                                         The hclk frequency should be less than 125Mhz.
                                                         After writing a value to this field the SW should
                                                         read the field for the value written.
                                                         The ENABLE field of this register should not be set
                                                         until AFTER this field is set and then read. */
#else
	uint64_t divide                       : 3;
	uint64_t hrst                         : 1;
	uint64_t prst                         : 1;
	uint64_t enable                       : 1;
	uint64_t por                          : 1;
	uint64_t s_bist                       : 1;
	uint64_t sd_mode                      : 2;
	uint64_t cdiv_byp                     : 1;
	uint64_t p_c_sel                      : 2;
	uint64_t p_com_on                     : 1;
	uint64_t reserved_14_15               : 2;
	uint64_t p_x_on                       : 1;
	uint64_t hclk_rst                     : 1;
	uint64_t divide2                      : 2;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_usbnx_clk_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t hclk_rst                     : 1;  /**< When this field is '0' the HCLK-DIVIDER used to
                                                         generate the hclk in the USB Subsystem is held
                                                         in reset. This bit must be set to '0' before
                                                         changing the value os DIVIDE in this register.
                                                         The reset to the HCLK_DIVIDERis also asserted
                                                         when core reset is asserted. */
	uint64_t p_x_on                       : 1;  /**< Force USB-PHY on during suspend.
                                                         '1' USB-PHY XO block is powered-down during
                                                             suspend.
                                                         '0' USB-PHY XO block is powered-up during
                                                             suspend.
                                                         The value of this field must be set while POR is
                                                         active. */
	uint64_t p_rclk                       : 1;  /**< Phy refrence clock enable.
                                                         '1' The PHY PLL uses the XO block output as a
                                                         reference.
                                                         '0' Reserved. */
	uint64_t p_xenbn                      : 1;  /**< Phy external clock enable.
                                                         '1' The XO block uses the clock from a crystal.
                                                         '0' The XO block uses an external clock supplied
                                                             on the XO pin. USB_XI should be tied to
                                                             ground for this usage. */
	uint64_t p_com_on                     : 1;  /**< '0' Force USB-PHY XO Bias, Bandgap and PLL to
                                                             remain powered in Suspend Mode.
                                                         '1' The USB-PHY XO Bias, Bandgap and PLL are
                                                             powered down in suspend mode.
                                                         The value of this field must be set while POR is
                                                         active. */
	uint64_t p_c_sel                      : 2;  /**< Phy clock speed select.
                                                         Selects the reference clock / crystal frequency.
                                                         '11': Reserved
                                                         '10': 48 MHz
                                                         '01': 24 MHz
                                                         '00': 12 MHz
                                                         The value of this field must be set while POR is
                                                         active. */
	uint64_t cdiv_byp                     : 1;  /**< Used to enable the bypass input to the USB_CLK_DIV. */
	uint64_t sd_mode                      : 2;  /**< Scaledown mode for the USBC. Control timing events
                                                         in the USBC, for normal operation this must be '0'. */
	uint64_t s_bist                       : 1;  /**< Starts bist on the hclk memories, during the '0'
                                                         to '1' transition. */
	uint64_t por                          : 1;  /**< Power On Reset for the PHY.
                                                         Resets all the PHYS registers and state machines. */
	uint64_t enable                       : 1;  /**< When '1' allows the generation of the hclk. When
                                                         '0' the hclk will not be generated. */
	uint64_t prst                         : 1;  /**< When this field is '0' the reset associated with
                                                         the phy_clk functionality in the USB Subsystem is
                                                         help in reset. This bit should not be set to '1'
                                                         until the time it takes 6 clocks (hclk or phy_clk,
                                                         whichever is slower) has passed. Under normal
                                                         operation once this bit is set to '1' it should not
                                                         be set to '0'. */
	uint64_t hrst                         : 1;  /**< When this field is '0' the reset associated with
                                                         the hclk functioanlity in the USB Subsystem is
                                                         held in reset.This bit should not be set to '1'
                                                         until 12ms after phy_clk is stable. Under normal
                                                         operation, once this bit is set to '1' it should
                                                         not be set to '0'. */
	uint64_t divide                       : 3;  /**< The 'hclk' used by the USB subsystem is derived
                                                         from the eclk. The eclk will be divided by the
                                                         value of this field +1 to determine the hclk
                                                         frequency. (Also see HRST of this register).
                                                         The hclk frequency must be less than 125 MHz. */
#else
	uint64_t divide                       : 3;
	uint64_t hrst                         : 1;
	uint64_t prst                         : 1;
	uint64_t enable                       : 1;
	uint64_t por                          : 1;
	uint64_t s_bist                       : 1;
	uint64_t sd_mode                      : 2;
	uint64_t cdiv_byp                     : 1;
	uint64_t p_c_sel                      : 2;
	uint64_t p_com_on                     : 1;
	uint64_t p_xenbn                      : 1;
	uint64_t p_rclk                       : 1;
	uint64_t p_x_on                       : 1;
	uint64_t hclk_rst                     : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} cn30xx;
	struct cvmx_usbnx_clk_ctl_cn30xx      cn31xx;
	struct cvmx_usbnx_clk_ctl_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t divide2                      : 2;  /**< The 'hclk' used by the USB subsystem is derived
                                                         from the eclk.
                                                         Also see the field DIVIDE. DIVIDE2<1> must currently
                                                         be zero because it is not implemented, so the maximum
                                                         ratio of eclk/hclk is currently 16.
                                                         The actual divide number for hclk is:
                                                         (DIVIDE2 + 1) * (DIVIDE + 1) */
	uint64_t hclk_rst                     : 1;  /**< When this field is '0' the HCLK-DIVIDER used to
                                                         generate the hclk in the USB Subsystem is held
                                                         in reset. This bit must be set to '0' before
                                                         changing the value os DIVIDE in this register.
                                                         The reset to the HCLK_DIVIDERis also asserted
                                                         when core reset is asserted. */
	uint64_t reserved_16_16               : 1;
	uint64_t p_rtype                      : 2;  /**< PHY reference clock type
                                                         '0' The USB-PHY uses a 12MHz crystal as a clock
                                                             source at the USB_XO and USB_XI pins
                                                         '1' Reserved
                                                         '2' The USB_PHY uses 12/24/48MHz 2.5V board clock
                                                             at the USB_XO pin. USB_XI should be tied to
                                                             ground in this case.
                                                         '3' Reserved
                                                         (bit 14 was P_XENBN on 3xxx)
                                                         (bit 15 was P_RCLK on 3xxx) */
	uint64_t p_com_on                     : 1;  /**< '0' Force USB-PHY XO Bias, Bandgap and PLL to
                                                             remain powered in Suspend Mode.
                                                         '1' The USB-PHY XO Bias, Bandgap and PLL are
                                                             powered down in suspend mode.
                                                         The value of this field must be set while POR is
                                                         active. */
	uint64_t p_c_sel                      : 2;  /**< Phy clock speed select.
                                                         Selects the reference clock / crystal frequency.
                                                         '11': Reserved
                                                         '10': 48 MHz (reserved when a crystal is used)
                                                         '01': 24 MHz (reserved when a crystal is used)
                                                         '00': 12 MHz
                                                         The value of this field must be set while POR is
                                                         active.
                                                         NOTE: if a crystal is used as a reference clock,
                                                         this field must be set to 12 MHz. */
	uint64_t cdiv_byp                     : 1;  /**< Used to enable the bypass input to the USB_CLK_DIV. */
	uint64_t sd_mode                      : 2;  /**< Scaledown mode for the USBC. Control timing events
                                                         in the USBC, for normal operation this must be '0'. */
	uint64_t s_bist                       : 1;  /**< Starts bist on the hclk memories, during the '0'
                                                         to '1' transition. */
	uint64_t por                          : 1;  /**< Power On Reset for the PHY.
                                                         Resets all the PHYS registers and state machines. */
	uint64_t enable                       : 1;  /**< When '1' allows the generation of the hclk. When
                                                         '0' the hclk will not be generated. SEE DIVIDE
                                                         field of this register. */
	uint64_t prst                         : 1;  /**< When this field is '0' the reset associated with
                                                         the phy_clk functionality in the USB Subsystem is
                                                         help in reset. This bit should not be set to '1'
                                                         until the time it takes 6 clocks (hclk or phy_clk,
                                                         whichever is slower) has passed. Under normal
                                                         operation once this bit is set to '1' it should not
                                                         be set to '0'. */
	uint64_t hrst                         : 1;  /**< When this field is '0' the reset associated with
                                                         the hclk functioanlity in the USB Subsystem is
                                                         held in reset.This bit should not be set to '1'
                                                         until 12ms after phy_clk is stable. Under normal
                                                         operation, once this bit is set to '1' it should
                                                         not be set to '0'. */
	uint64_t divide                       : 3;  /**< The frequency of 'hclk' used by the USB subsystem
                                                         is the eclk frequency divided by the value of
                                                         (DIVIDE2 + 1) * (DIVIDE + 1), also see the field
                                                         DIVIDE2 of this register.
                                                         The hclk frequency should be less than 125Mhz.
                                                         After writing a value to this field the SW should
                                                         read the field for the value written.
                                                         The ENABLE field of this register should not be set
                                                         until AFTER this field is set and then read. */
#else
	uint64_t divide                       : 3;
	uint64_t hrst                         : 1;
	uint64_t prst                         : 1;
	uint64_t enable                       : 1;
	uint64_t por                          : 1;
	uint64_t s_bist                       : 1;
	uint64_t sd_mode                      : 2;
	uint64_t cdiv_byp                     : 1;
	uint64_t p_c_sel                      : 2;
	uint64_t p_com_on                     : 1;
	uint64_t p_rtype                      : 2;
	uint64_t reserved_16_16               : 1;
	uint64_t hclk_rst                     : 1;
	uint64_t divide2                      : 2;
	uint64_t reserved_20_63               : 44;
#endif
	} cn50xx;
	struct cvmx_usbnx_clk_ctl_cn50xx      cn52xx;
	struct cvmx_usbnx_clk_ctl_cn50xx      cn52xxp1;
	struct cvmx_usbnx_clk_ctl_cn50xx      cn56xx;
	struct cvmx_usbnx_clk_ctl_cn50xx      cn56xxp1;
};
typedef union cvmx_usbnx_clk_ctl cvmx_usbnx_clk_ctl_t;

/**
 * cvmx_usbn#_ctl_status
 *
 * USBN_CTL_STATUS = USBN's Control And Status Register
 *
 * Contains general control and status information for the USBN block.
 */
union cvmx_usbnx_ctl_status {
	uint64_t u64;
	struct cvmx_usbnx_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t dma_0pag                     : 1;  /**< When '1' sets the DMA engine will set the zero-Page
                                                         bit in the L2C store operation to the IOB. */
	uint64_t dma_stt                      : 1;  /**< When '1' sets the DMA engine to use STT operations. */
	uint64_t dma_test                     : 1;  /**< When '1' sets the DMA engine into Test-Mode.
                                                         For normal operation this bit should be '0'. */
	uint64_t inv_a2                       : 1;  /**< When '1' causes the address[2] driven on the AHB
                                                         for USB-CORE FIFO access to be inverted. Also data
                                                         writen to and read from the AHB will have it byte
                                                         order swapped. If the orginal order was A-B-C-D the
                                                         new byte order will be D-C-B-A. */
	uint64_t l2c_emod                     : 2;  /**< Endian format for data from/to the L2C.
                                                         IN:   A-B-C-D-E-F-G-H
                                                         OUT0: A-B-C-D-E-F-G-H
                                                         OUT1: H-G-F-E-D-C-B-A
                                                         OUT2: D-C-B-A-H-G-F-E
                                                         OUT3: E-F-G-H-A-B-C-D */
#else
	uint64_t l2c_emod                     : 2;
	uint64_t inv_a2                       : 1;
	uint64_t dma_test                     : 1;
	uint64_t dma_stt                      : 1;
	uint64_t dma_0pag                     : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_usbnx_ctl_status_s        cn30xx;
	struct cvmx_usbnx_ctl_status_s        cn31xx;
	struct cvmx_usbnx_ctl_status_s        cn50xx;
	struct cvmx_usbnx_ctl_status_s        cn52xx;
	struct cvmx_usbnx_ctl_status_s        cn52xxp1;
	struct cvmx_usbnx_ctl_status_s        cn56xx;
	struct cvmx_usbnx_ctl_status_s        cn56xxp1;
};
typedef union cvmx_usbnx_ctl_status cvmx_usbnx_ctl_status_t;

/**
 * cvmx_usbn#_dma0_inb_chn0
 *
 * USBN_DMA0_INB_CHN0 = USBN's Inbound DMA for USB0 Channel0
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel0.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn0 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn0_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn0 cvmx_usbnx_dma0_inb_chn0_t;

/**
 * cvmx_usbn#_dma0_inb_chn1
 *
 * USBN_DMA0_INB_CHN1 = USBN's Inbound DMA for USB0 Channel1
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel1.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn1 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn1_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn1 cvmx_usbnx_dma0_inb_chn1_t;

/**
 * cvmx_usbn#_dma0_inb_chn2
 *
 * USBN_DMA0_INB_CHN2 = USBN's Inbound DMA for USB0 Channel2
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel2.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn2 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn2_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn2 cvmx_usbnx_dma0_inb_chn2_t;

/**
 * cvmx_usbn#_dma0_inb_chn3
 *
 * USBN_DMA0_INB_CHN3 = USBN's Inbound DMA for USB0 Channel3
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel3.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn3 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn3_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn3 cvmx_usbnx_dma0_inb_chn3_t;

/**
 * cvmx_usbn#_dma0_inb_chn4
 *
 * USBN_DMA0_INB_CHN4 = USBN's Inbound DMA for USB0 Channel4
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel4.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn4 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn4_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn4 cvmx_usbnx_dma0_inb_chn4_t;

/**
 * cvmx_usbn#_dma0_inb_chn5
 *
 * USBN_DMA0_INB_CHN5 = USBN's Inbound DMA for USB0 Channel5
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel5.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn5 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn5_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn5 cvmx_usbnx_dma0_inb_chn5_t;

/**
 * cvmx_usbn#_dma0_inb_chn6
 *
 * USBN_DMA0_INB_CHN6 = USBN's Inbound DMA for USB0 Channel6
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel6.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn6 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn6_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn6 cvmx_usbnx_dma0_inb_chn6_t;

/**
 * cvmx_usbn#_dma0_inb_chn7
 *
 * USBN_DMA0_INB_CHN7 = USBN's Inbound DMA for USB0 Channel7
 *
 * Contains the starting address for use when USB0 writes to L2C via Channel7.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_inb_chn7 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_inb_chn7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Write to L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn30xx;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn31xx;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn50xx;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn52xx;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn52xxp1;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn56xx;
	struct cvmx_usbnx_dma0_inb_chn7_s     cn56xxp1;
};
typedef union cvmx_usbnx_dma0_inb_chn7 cvmx_usbnx_dma0_inb_chn7_t;

/**
 * cvmx_usbn#_dma0_outb_chn0
 *
 * USBN_DMA0_OUTB_CHN0 = USBN's Outbound DMA for USB0 Channel0
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel0.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn0 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn0_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn0 cvmx_usbnx_dma0_outb_chn0_t;

/**
 * cvmx_usbn#_dma0_outb_chn1
 *
 * USBN_DMA0_OUTB_CHN1 = USBN's Outbound DMA for USB0 Channel1
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel1.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn1 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn1_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn1 cvmx_usbnx_dma0_outb_chn1_t;

/**
 * cvmx_usbn#_dma0_outb_chn2
 *
 * USBN_DMA0_OUTB_CHN2 = USBN's Outbound DMA for USB0 Channel2
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel2.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn2 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn2_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn2 cvmx_usbnx_dma0_outb_chn2_t;

/**
 * cvmx_usbn#_dma0_outb_chn3
 *
 * USBN_DMA0_OUTB_CHN3 = USBN's Outbound DMA for USB0 Channel3
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel3.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn3 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn3_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn3 cvmx_usbnx_dma0_outb_chn3_t;

/**
 * cvmx_usbn#_dma0_outb_chn4
 *
 * USBN_DMA0_OUTB_CHN4 = USBN's Outbound DMA for USB0 Channel4
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel4.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn4 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn4_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn4 cvmx_usbnx_dma0_outb_chn4_t;

/**
 * cvmx_usbn#_dma0_outb_chn5
 *
 * USBN_DMA0_OUTB_CHN5 = USBN's Outbound DMA for USB0 Channel5
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel5.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn5 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn5_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn5 cvmx_usbnx_dma0_outb_chn5_t;

/**
 * cvmx_usbn#_dma0_outb_chn6
 *
 * USBN_DMA0_OUTB_CHN6 = USBN's Outbound DMA for USB0 Channel6
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel6.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn6 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn6_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn6 cvmx_usbnx_dma0_outb_chn6_t;

/**
 * cvmx_usbn#_dma0_outb_chn7
 *
 * USBN_DMA0_OUTB_CHN7 = USBN's Outbound DMA for USB0 Channel7
 *
 * Contains the starting address for use when USB0 reads from L2C via Channel7.
 * Writing of this register sets the base address.
 */
union cvmx_usbnx_dma0_outb_chn7 {
	uint64_t u64;
	struct cvmx_usbnx_dma0_outb_chn7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Base address for DMA Read from L2C. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn30xx;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn31xx;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn50xx;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn52xx;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn52xxp1;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn56xx;
	struct cvmx_usbnx_dma0_outb_chn7_s    cn56xxp1;
};
typedef union cvmx_usbnx_dma0_outb_chn7 cvmx_usbnx_dma0_outb_chn7_t;

/**
 * cvmx_usbn#_dma_test
 *
 * USBN_DMA_TEST = USBN's DMA TestRegister
 *
 * This register can cause the external DMA engine to the USB-Core to make transfers from/to L2C/USB-FIFOs
 */
union cvmx_usbnx_dma_test {
	uint64_t u64;
	struct cvmx_usbnx_dma_test_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t done                         : 1;  /**< This field is set when a DMA completes. Writing a
                                                         '1' to this field clears this bit. */
	uint64_t req                          : 1;  /**< DMA Request. Writing a 1 to this register
                                                         will cause a DMA request as specified in the other
                                                         fields of this register to take place. This field
                                                         will always read as '0'. */
	uint64_t f_addr                       : 18; /**< The address to read from in the Data-Fifo. */
	uint64_t count                        : 11; /**< DMA Request Count. */
	uint64_t channel                      : 5;  /**< DMA Channel/Enpoint. */
	uint64_t burst                        : 4;  /**< DMA Burst Size. */
#else
	uint64_t burst                        : 4;
	uint64_t channel                      : 5;
	uint64_t count                        : 11;
	uint64_t f_addr                       : 18;
	uint64_t req                          : 1;
	uint64_t done                         : 1;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_usbnx_dma_test_s          cn30xx;
	struct cvmx_usbnx_dma_test_s          cn31xx;
	struct cvmx_usbnx_dma_test_s          cn50xx;
	struct cvmx_usbnx_dma_test_s          cn52xx;
	struct cvmx_usbnx_dma_test_s          cn52xxp1;
	struct cvmx_usbnx_dma_test_s          cn56xx;
	struct cvmx_usbnx_dma_test_s          cn56xxp1;
};
typedef union cvmx_usbnx_dma_test cvmx_usbnx_dma_test_t;

/**
 * cvmx_usbn#_int_enb
 *
 * USBN_INT_ENB = USBN's Interrupt Enable
 *
 * The USBN's interrupt enable register.
 */
union cvmx_usbnx_int_enb {
	uint64_t u64;
	struct cvmx_usbnx_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t nd4o_dpf                     : 1;  /**< When set (1) and bit 37 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nd4o_dpe                     : 1;  /**< When set (1) and bit 36 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nd4o_rpf                     : 1;  /**< When set (1) and bit 35 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nd4o_rpe                     : 1;  /**< When set (1) and bit 34 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t ltl_f_pf                     : 1;  /**< When set (1) and bit 33 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t ltl_f_pe                     : 1;  /**< When set (1) and bit 32 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t u2n_c_pe                     : 1;  /**< When set (1) and bit 31 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t u2n_c_pf                     : 1;  /**< When set (1) and bit 30 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t u2n_d_pf                     : 1;  /**< When set (1) and bit 29 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t u2n_d_pe                     : 1;  /**< When set (1) and bit 28 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t n2u_pe                       : 1;  /**< When set (1) and bit 27 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t n2u_pf                       : 1;  /**< When set (1) and bit 26 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t uod_pf                       : 1;  /**< When set (1) and bit 25 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t uod_pe                       : 1;  /**< When set (1) and bit 24 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q3_e                      : 1;  /**< When set (1) and bit 23 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q3_f                      : 1;  /**< When set (1) and bit 22 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q2_e                      : 1;  /**< When set (1) and bit 21 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q2_f                      : 1;  /**< When set (1) and bit 20 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rg_fi_f                      : 1;  /**< When set (1) and bit 19 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rg_fi_e                      : 1;  /**< When set (1) and bit 18 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2_fi_f                      : 1;  /**< When set (1) and bit 17 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2_fi_e                      : 1;  /**< When set (1) and bit 16 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2c_a_f                      : 1;  /**< When set (1) and bit 15 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2c_s_e                      : 1;  /**< When set (1) and bit 14 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t dcred_f                      : 1;  /**< When set (1) and bit 13 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t dcred_e                      : 1;  /**< When set (1) and bit 12 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lt_pu_f                      : 1;  /**< When set (1) and bit 11 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lt_po_e                      : 1;  /**< When set (1) and bit 10 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nt_pu_f                      : 1;  /**< When set (1) and bit 9 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nt_po_e                      : 1;  /**< When set (1) and bit 8 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pt_pu_f                      : 1;  /**< When set (1) and bit 7 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pt_po_e                      : 1;  /**< When set (1) and bit 6 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lr_pu_f                      : 1;  /**< When set (1) and bit 5 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lr_po_e                      : 1;  /**< When set (1) and bit 4 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nr_pu_f                      : 1;  /**< When set (1) and bit 3 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nr_po_e                      : 1;  /**< When set (1) and bit 2 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pr_pu_f                      : 1;  /**< When set (1) and bit 1 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pr_po_e                      : 1;  /**< When set (1) and bit 0 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
#else
	uint64_t pr_po_e                      : 1;
	uint64_t pr_pu_f                      : 1;
	uint64_t nr_po_e                      : 1;
	uint64_t nr_pu_f                      : 1;
	uint64_t lr_po_e                      : 1;
	uint64_t lr_pu_f                      : 1;
	uint64_t pt_po_e                      : 1;
	uint64_t pt_pu_f                      : 1;
	uint64_t nt_po_e                      : 1;
	uint64_t nt_pu_f                      : 1;
	uint64_t lt_po_e                      : 1;
	uint64_t lt_pu_f                      : 1;
	uint64_t dcred_e                      : 1;
	uint64_t dcred_f                      : 1;
	uint64_t l2c_s_e                      : 1;
	uint64_t l2c_a_f                      : 1;
	uint64_t l2_fi_e                      : 1;
	uint64_t l2_fi_f                      : 1;
	uint64_t rg_fi_e                      : 1;
	uint64_t rg_fi_f                      : 1;
	uint64_t rq_q2_f                      : 1;
	uint64_t rq_q2_e                      : 1;
	uint64_t rq_q3_f                      : 1;
	uint64_t rq_q3_e                      : 1;
	uint64_t uod_pe                       : 1;
	uint64_t uod_pf                       : 1;
	uint64_t n2u_pf                       : 1;
	uint64_t n2u_pe                       : 1;
	uint64_t u2n_d_pe                     : 1;
	uint64_t u2n_d_pf                     : 1;
	uint64_t u2n_c_pf                     : 1;
	uint64_t u2n_c_pe                     : 1;
	uint64_t ltl_f_pe                     : 1;
	uint64_t ltl_f_pf                     : 1;
	uint64_t nd4o_rpe                     : 1;
	uint64_t nd4o_rpf                     : 1;
	uint64_t nd4o_dpe                     : 1;
	uint64_t nd4o_dpf                     : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_usbnx_int_enb_s           cn30xx;
	struct cvmx_usbnx_int_enb_s           cn31xx;
	struct cvmx_usbnx_int_enb_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t nd4o_dpf                     : 1;  /**< When set (1) and bit 37 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nd4o_dpe                     : 1;  /**< When set (1) and bit 36 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nd4o_rpf                     : 1;  /**< When set (1) and bit 35 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nd4o_rpe                     : 1;  /**< When set (1) and bit 34 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t ltl_f_pf                     : 1;  /**< When set (1) and bit 33 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t ltl_f_pe                     : 1;  /**< When set (1) and bit 32 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t reserved_26_31               : 6;
	uint64_t uod_pf                       : 1;  /**< When set (1) and bit 25 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t uod_pe                       : 1;  /**< When set (1) and bit 24 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q3_e                      : 1;  /**< When set (1) and bit 23 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q3_f                      : 1;  /**< When set (1) and bit 22 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q2_e                      : 1;  /**< When set (1) and bit 21 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rq_q2_f                      : 1;  /**< When set (1) and bit 20 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rg_fi_f                      : 1;  /**< When set (1) and bit 19 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t rg_fi_e                      : 1;  /**< When set (1) and bit 18 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2_fi_f                      : 1;  /**< When set (1) and bit 17 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2_fi_e                      : 1;  /**< When set (1) and bit 16 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2c_a_f                      : 1;  /**< When set (1) and bit 15 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t l2c_s_e                      : 1;  /**< When set (1) and bit 14 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t dcred_f                      : 1;  /**< When set (1) and bit 13 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t dcred_e                      : 1;  /**< When set (1) and bit 12 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lt_pu_f                      : 1;  /**< When set (1) and bit 11 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lt_po_e                      : 1;  /**< When set (1) and bit 10 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nt_pu_f                      : 1;  /**< When set (1) and bit 9 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nt_po_e                      : 1;  /**< When set (1) and bit 8 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pt_pu_f                      : 1;  /**< When set (1) and bit 7 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pt_po_e                      : 1;  /**< When set (1) and bit 6 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lr_pu_f                      : 1;  /**< When set (1) and bit 5 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t lr_po_e                      : 1;  /**< When set (1) and bit 4 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nr_pu_f                      : 1;  /**< When set (1) and bit 3 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t nr_po_e                      : 1;  /**< When set (1) and bit 2 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pr_pu_f                      : 1;  /**< When set (1) and bit 1 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
	uint64_t pr_po_e                      : 1;  /**< When set (1) and bit 0 of the USBN_INT_SUM
                                                         register is asserted the USBN will assert an
                                                         interrupt. */
#else
	uint64_t pr_po_e                      : 1;
	uint64_t pr_pu_f                      : 1;
	uint64_t nr_po_e                      : 1;
	uint64_t nr_pu_f                      : 1;
	uint64_t lr_po_e                      : 1;
	uint64_t lr_pu_f                      : 1;
	uint64_t pt_po_e                      : 1;
	uint64_t pt_pu_f                      : 1;
	uint64_t nt_po_e                      : 1;
	uint64_t nt_pu_f                      : 1;
	uint64_t lt_po_e                      : 1;
	uint64_t lt_pu_f                      : 1;
	uint64_t dcred_e                      : 1;
	uint64_t dcred_f                      : 1;
	uint64_t l2c_s_e                      : 1;
	uint64_t l2c_a_f                      : 1;
	uint64_t l2_fi_e                      : 1;
	uint64_t l2_fi_f                      : 1;
	uint64_t rg_fi_e                      : 1;
	uint64_t rg_fi_f                      : 1;
	uint64_t rq_q2_f                      : 1;
	uint64_t rq_q2_e                      : 1;
	uint64_t rq_q3_f                      : 1;
	uint64_t rq_q3_e                      : 1;
	uint64_t uod_pe                       : 1;
	uint64_t uod_pf                       : 1;
	uint64_t reserved_26_31               : 6;
	uint64_t ltl_f_pe                     : 1;
	uint64_t ltl_f_pf                     : 1;
	uint64_t nd4o_rpe                     : 1;
	uint64_t nd4o_rpf                     : 1;
	uint64_t nd4o_dpe                     : 1;
	uint64_t nd4o_dpf                     : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} cn50xx;
	struct cvmx_usbnx_int_enb_cn50xx      cn52xx;
	struct cvmx_usbnx_int_enb_cn50xx      cn52xxp1;
	struct cvmx_usbnx_int_enb_cn50xx      cn56xx;
	struct cvmx_usbnx_int_enb_cn50xx      cn56xxp1;
};
typedef union cvmx_usbnx_int_enb cvmx_usbnx_int_enb_t;

/**
 * cvmx_usbn#_int_sum
 *
 * USBN_INT_SUM = USBN's Interrupt Summary Register
 *
 * Contains the diffrent interrupt summary bits of the USBN.
 */
union cvmx_usbnx_int_sum {
	uint64_t u64;
	struct cvmx_usbnx_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t nd4o_dpf                     : 1;  /**< NCB DMA Out Data Fifo Push Full. */
	uint64_t nd4o_dpe                     : 1;  /**< NCB DMA Out Data Fifo Pop Empty. */
	uint64_t nd4o_rpf                     : 1;  /**< NCB DMA Out Request Fifo Push Full. */
	uint64_t nd4o_rpe                     : 1;  /**< NCB DMA Out Request Fifo Pop Empty. */
	uint64_t ltl_f_pf                     : 1;  /**< L2C Transfer Length Fifo Push Full. */
	uint64_t ltl_f_pe                     : 1;  /**< L2C Transfer Length Fifo Pop Empty. */
	uint64_t u2n_c_pe                     : 1;  /**< U2N Control Fifo Pop Empty. */
	uint64_t u2n_c_pf                     : 1;  /**< U2N Control Fifo Push Full. */
	uint64_t u2n_d_pf                     : 1;  /**< U2N Data Fifo Push Full. */
	uint64_t u2n_d_pe                     : 1;  /**< U2N Data Fifo Pop Empty. */
	uint64_t n2u_pe                       : 1;  /**< N2U Fifo Pop Empty. */
	uint64_t n2u_pf                       : 1;  /**< N2U Fifo Push Full. */
	uint64_t uod_pf                       : 1;  /**< UOD Fifo Push Full. */
	uint64_t uod_pe                       : 1;  /**< UOD Fifo Pop Empty. */
	uint64_t rq_q3_e                      : 1;  /**< Request Queue-3 Fifo Pushed When Full. */
	uint64_t rq_q3_f                      : 1;  /**< Request Queue-3 Fifo Pushed When Full. */
	uint64_t rq_q2_e                      : 1;  /**< Request Queue-2 Fifo Pushed When Full. */
	uint64_t rq_q2_f                      : 1;  /**< Request Queue-2 Fifo Pushed When Full. */
	uint64_t rg_fi_f                      : 1;  /**< Register Request Fifo Pushed When Full. */
	uint64_t rg_fi_e                      : 1;  /**< Register Request Fifo Pushed When Full. */
	uint64_t lt_fi_f                      : 1;  /**< L2C Request Fifo Pushed When Full. */
	uint64_t lt_fi_e                      : 1;  /**< L2C Request Fifo Pushed When Full. */
	uint64_t l2c_a_f                      : 1;  /**< L2C Credit Count Added When Full. */
	uint64_t l2c_s_e                      : 1;  /**< L2C Credit Count Subtracted When Empty. */
	uint64_t dcred_f                      : 1;  /**< Data CreditFifo Pushed When Full. */
	uint64_t dcred_e                      : 1;  /**< Data Credit Fifo Pushed When Full. */
	uint64_t lt_pu_f                      : 1;  /**< L2C Trasaction Fifo Pushed When Full. */
	uint64_t lt_po_e                      : 1;  /**< L2C Trasaction Fifo Popped When Full. */
	uint64_t nt_pu_f                      : 1;  /**< NPI Trasaction Fifo Pushed When Full. */
	uint64_t nt_po_e                      : 1;  /**< NPI Trasaction Fifo Popped When Full. */
	uint64_t pt_pu_f                      : 1;  /**< PP  Trasaction Fifo Pushed When Full. */
	uint64_t pt_po_e                      : 1;  /**< PP  Trasaction Fifo Popped When Full. */
	uint64_t lr_pu_f                      : 1;  /**< L2C Request Fifo Pushed When Full. */
	uint64_t lr_po_e                      : 1;  /**< L2C Request Fifo Popped When Empty. */
	uint64_t nr_pu_f                      : 1;  /**< NPI Request Fifo Pushed When Full. */
	uint64_t nr_po_e                      : 1;  /**< NPI Request Fifo Popped When Empty. */
	uint64_t pr_pu_f                      : 1;  /**< PP  Request Fifo Pushed When Full. */
	uint64_t pr_po_e                      : 1;  /**< PP  Request Fifo Popped When Empty. */
#else
	uint64_t pr_po_e                      : 1;
	uint64_t pr_pu_f                      : 1;
	uint64_t nr_po_e                      : 1;
	uint64_t nr_pu_f                      : 1;
	uint64_t lr_po_e                      : 1;
	uint64_t lr_pu_f                      : 1;
	uint64_t pt_po_e                      : 1;
	uint64_t pt_pu_f                      : 1;
	uint64_t nt_po_e                      : 1;
	uint64_t nt_pu_f                      : 1;
	uint64_t lt_po_e                      : 1;
	uint64_t lt_pu_f                      : 1;
	uint64_t dcred_e                      : 1;
	uint64_t dcred_f                      : 1;
	uint64_t l2c_s_e                      : 1;
	uint64_t l2c_a_f                      : 1;
	uint64_t lt_fi_e                      : 1;
	uint64_t lt_fi_f                      : 1;
	uint64_t rg_fi_e                      : 1;
	uint64_t rg_fi_f                      : 1;
	uint64_t rq_q2_f                      : 1;
	uint64_t rq_q2_e                      : 1;
	uint64_t rq_q3_f                      : 1;
	uint64_t rq_q3_e                      : 1;
	uint64_t uod_pe                       : 1;
	uint64_t uod_pf                       : 1;
	uint64_t n2u_pf                       : 1;
	uint64_t n2u_pe                       : 1;
	uint64_t u2n_d_pe                     : 1;
	uint64_t u2n_d_pf                     : 1;
	uint64_t u2n_c_pf                     : 1;
	uint64_t u2n_c_pe                     : 1;
	uint64_t ltl_f_pe                     : 1;
	uint64_t ltl_f_pf                     : 1;
	uint64_t nd4o_rpe                     : 1;
	uint64_t nd4o_rpf                     : 1;
	uint64_t nd4o_dpe                     : 1;
	uint64_t nd4o_dpf                     : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_usbnx_int_sum_s           cn30xx;
	struct cvmx_usbnx_int_sum_s           cn31xx;
	struct cvmx_usbnx_int_sum_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t nd4o_dpf                     : 1;  /**< NCB DMA Out Data Fifo Push Full. */
	uint64_t nd4o_dpe                     : 1;  /**< NCB DMA Out Data Fifo Pop Empty. */
	uint64_t nd4o_rpf                     : 1;  /**< NCB DMA Out Request Fifo Push Full. */
	uint64_t nd4o_rpe                     : 1;  /**< NCB DMA Out Request Fifo Pop Empty. */
	uint64_t ltl_f_pf                     : 1;  /**< L2C Transfer Length Fifo Push Full. */
	uint64_t ltl_f_pe                     : 1;  /**< L2C Transfer Length Fifo Pop Empty. */
	uint64_t reserved_26_31               : 6;
	uint64_t uod_pf                       : 1;  /**< UOD Fifo Push Full. */
	uint64_t uod_pe                       : 1;  /**< UOD Fifo Pop Empty. */
	uint64_t rq_q3_e                      : 1;  /**< Request Queue-3 Fifo Pushed When Full. */
	uint64_t rq_q3_f                      : 1;  /**< Request Queue-3 Fifo Pushed When Full. */
	uint64_t rq_q2_e                      : 1;  /**< Request Queue-2 Fifo Pushed When Full. */
	uint64_t rq_q2_f                      : 1;  /**< Request Queue-2 Fifo Pushed When Full. */
	uint64_t rg_fi_f                      : 1;  /**< Register Request Fifo Pushed When Full. */
	uint64_t rg_fi_e                      : 1;  /**< Register Request Fifo Pushed When Full. */
	uint64_t lt_fi_f                      : 1;  /**< L2C Request Fifo Pushed When Full. */
	uint64_t lt_fi_e                      : 1;  /**< L2C Request Fifo Pushed When Full. */
	uint64_t l2c_a_f                      : 1;  /**< L2C Credit Count Added When Full. */
	uint64_t l2c_s_e                      : 1;  /**< L2C Credit Count Subtracted When Empty. */
	uint64_t dcred_f                      : 1;  /**< Data CreditFifo Pushed When Full. */
	uint64_t dcred_e                      : 1;  /**< Data Credit Fifo Pushed When Full. */
	uint64_t lt_pu_f                      : 1;  /**< L2C Trasaction Fifo Pushed When Full. */
	uint64_t lt_po_e                      : 1;  /**< L2C Trasaction Fifo Popped When Full. */
	uint64_t nt_pu_f                      : 1;  /**< NPI Trasaction Fifo Pushed When Full. */
	uint64_t nt_po_e                      : 1;  /**< NPI Trasaction Fifo Popped When Full. */
	uint64_t pt_pu_f                      : 1;  /**< PP  Trasaction Fifo Pushed When Full. */
	uint64_t pt_po_e                      : 1;  /**< PP  Trasaction Fifo Popped When Full. */
	uint64_t lr_pu_f                      : 1;  /**< L2C Request Fifo Pushed When Full. */
	uint64_t lr_po_e                      : 1;  /**< L2C Request Fifo Popped When Empty. */
	uint64_t nr_pu_f                      : 1;  /**< NPI Request Fifo Pushed When Full. */
	uint64_t nr_po_e                      : 1;  /**< NPI Request Fifo Popped When Empty. */
	uint64_t pr_pu_f                      : 1;  /**< PP  Request Fifo Pushed When Full. */
	uint64_t pr_po_e                      : 1;  /**< PP  Request Fifo Popped When Empty. */
#else
	uint64_t pr_po_e                      : 1;
	uint64_t pr_pu_f                      : 1;
	uint64_t nr_po_e                      : 1;
	uint64_t nr_pu_f                      : 1;
	uint64_t lr_po_e                      : 1;
	uint64_t lr_pu_f                      : 1;
	uint64_t pt_po_e                      : 1;
	uint64_t pt_pu_f                      : 1;
	uint64_t nt_po_e                      : 1;
	uint64_t nt_pu_f                      : 1;
	uint64_t lt_po_e                      : 1;
	uint64_t lt_pu_f                      : 1;
	uint64_t dcred_e                      : 1;
	uint64_t dcred_f                      : 1;
	uint64_t l2c_s_e                      : 1;
	uint64_t l2c_a_f                      : 1;
	uint64_t lt_fi_e                      : 1;
	uint64_t lt_fi_f                      : 1;
	uint64_t rg_fi_e                      : 1;
	uint64_t rg_fi_f                      : 1;
	uint64_t rq_q2_f                      : 1;
	uint64_t rq_q2_e                      : 1;
	uint64_t rq_q3_f                      : 1;
	uint64_t rq_q3_e                      : 1;
	uint64_t uod_pe                       : 1;
	uint64_t uod_pf                       : 1;
	uint64_t reserved_26_31               : 6;
	uint64_t ltl_f_pe                     : 1;
	uint64_t ltl_f_pf                     : 1;
	uint64_t nd4o_rpe                     : 1;
	uint64_t nd4o_rpf                     : 1;
	uint64_t nd4o_dpe                     : 1;
	uint64_t nd4o_dpf                     : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} cn50xx;
	struct cvmx_usbnx_int_sum_cn50xx      cn52xx;
	struct cvmx_usbnx_int_sum_cn50xx      cn52xxp1;
	struct cvmx_usbnx_int_sum_cn50xx      cn56xx;
	struct cvmx_usbnx_int_sum_cn50xx      cn56xxp1;
};
typedef union cvmx_usbnx_int_sum cvmx_usbnx_int_sum_t;

/**
 * cvmx_usbn#_usbp_ctl_status
 *
 * USBN_USBP_CTL_STATUS = USBP Control And Status Register
 *
 * Contains general control and status information for the USBN block.
 */
union cvmx_usbnx_usbp_ctl_status {
	uint64_t u64;
	struct cvmx_usbnx_usbp_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t txrisetune                   : 1;  /**< HS Transmitter Rise/Fall Time Adjustment */
	uint64_t txvreftune                   : 4;  /**< HS DC Voltage Level Adjustment */
	uint64_t txfslstune                   : 4;  /**< FS/LS Source Impedence Adjustment */
	uint64_t txhsxvtune                   : 2;  /**< Transmitter High-Speed Crossover Adjustment */
	uint64_t sqrxtune                     : 3;  /**< Squelch Threshold Adjustment */
	uint64_t compdistune                  : 3;  /**< Disconnect Threshold Adjustment */
	uint64_t otgtune                      : 3;  /**< VBUS Valid Threshold Adjustment */
	uint64_t otgdisable                   : 1;  /**< OTG Block Disable */
	uint64_t portreset                    : 1;  /**< Per_Port Reset */
	uint64_t drvvbus                      : 1;  /**< Drive VBUS */
	uint64_t lsbist                       : 1;  /**< Low-Speed BIST Enable. */
	uint64_t fsbist                       : 1;  /**< Full-Speed BIST Enable. */
	uint64_t hsbist                       : 1;  /**< High-Speed BIST Enable. */
	uint64_t bist_done                    : 1;  /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
	uint64_t bist_err                     : 1;  /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
	uint64_t tdata_out                    : 4;  /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
	uint64_t siddq                        : 1;  /**< Drives the USBP (USB-PHY) SIDDQ input.
                                                         Normally should be set to zero.
                                                         When customers have no intent to use USB PHY
                                                         interface, they should:
                                                           - still provide 3.3V to USB_VDD33, and
                                                           - tie USB_REXT to 3.3V supply, and
                                                           - set USBN*_USBP_CTL_STATUS[SIDDQ]=1 */
	uint64_t txpreemphasistune            : 1;  /**< HS Transmitter Pre-Emphasis Enable */
	uint64_t dma_bmode                    : 1;  /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
	uint64_t usbc_end                     : 1;  /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
	uint64_t usbp_bist                    : 1;  /**< PHY, This is cleared '0' to run BIST on the USBP. */
	uint64_t tclk                         : 1;  /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
	uint64_t dp_pulld                     : 1;  /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t dm_pulld                     : 1;  /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t hst_mode                     : 1;  /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
	uint64_t tuning                       : 4;  /**< Transmitter Tuning for High-Speed Operation.
                                                         Tunes the current supply and rise/fall output
                                                         times for high-speed operation.
                                                         [20:19] == 11: Current supply increased
                                                         approximately 9%
                                                         [20:19] == 10: Current supply increased
                                                         approximately 4.5%
                                                         [20:19] == 01: Design default.
                                                         [20:19] == 00: Current supply decreased
                                                         approximately 4.5%
                                                         [22:21] == 11: Rise and fall times are increased.
                                                         [22:21] == 10: Design default.
                                                         [22:21] == 01: Rise and fall times are decreased.
                                                         [22:21] == 00: Rise and fall times are decreased
                                                         further as compared to the 01 setting. */
	uint64_t tx_bs_enh                    : 1;  /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
	uint64_t tx_bs_en                     : 1;  /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
	uint64_t loop_enb                     : 1;  /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
	uint64_t vtest_enb                    : 1;  /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
	uint64_t bist_enb                     : 1;  /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
	uint64_t tdata_sel                    : 1;  /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
	uint64_t taddr_in                     : 4;  /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
	uint64_t tdata_in                     : 8;  /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
	uint64_t ate_reset                    : 1;  /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
#else
	uint64_t ate_reset                    : 1;
	uint64_t tdata_in                     : 8;
	uint64_t taddr_in                     : 4;
	uint64_t tdata_sel                    : 1;
	uint64_t bist_enb                     : 1;
	uint64_t vtest_enb                    : 1;
	uint64_t loop_enb                     : 1;
	uint64_t tx_bs_en                     : 1;
	uint64_t tx_bs_enh                    : 1;
	uint64_t tuning                       : 4;
	uint64_t hst_mode                     : 1;
	uint64_t dm_pulld                     : 1;
	uint64_t dp_pulld                     : 1;
	uint64_t tclk                         : 1;
	uint64_t usbp_bist                    : 1;
	uint64_t usbc_end                     : 1;
	uint64_t dma_bmode                    : 1;
	uint64_t txpreemphasistune            : 1;
	uint64_t siddq                        : 1;
	uint64_t tdata_out                    : 4;
	uint64_t bist_err                     : 1;
	uint64_t bist_done                    : 1;
	uint64_t hsbist                       : 1;
	uint64_t fsbist                       : 1;
	uint64_t lsbist                       : 1;
	uint64_t drvvbus                      : 1;
	uint64_t portreset                    : 1;
	uint64_t otgdisable                   : 1;
	uint64_t otgtune                      : 3;
	uint64_t compdistune                  : 3;
	uint64_t sqrxtune                     : 3;
	uint64_t txhsxvtune                   : 2;
	uint64_t txfslstune                   : 4;
	uint64_t txvreftune                   : 4;
	uint64_t txrisetune                   : 1;
#endif
	} s;
	struct cvmx_usbnx_usbp_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t bist_done                    : 1;  /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
	uint64_t bist_err                     : 1;  /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
	uint64_t tdata_out                    : 4;  /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
	uint64_t reserved_30_31               : 2;
	uint64_t dma_bmode                    : 1;  /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
	uint64_t usbc_end                     : 1;  /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
	uint64_t usbp_bist                    : 1;  /**< PHY, This is cleared '0' to run BIST on the USBP. */
	uint64_t tclk                         : 1;  /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
	uint64_t dp_pulld                     : 1;  /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t dm_pulld                     : 1;  /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t hst_mode                     : 1;  /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
	uint64_t tuning                       : 4;  /**< Transmitter Tuning for High-Speed Operation.
                                                         Tunes the current supply and rise/fall output
                                                         times for high-speed operation.
                                                         [20:19] == 11: Current supply increased
                                                         approximately 9%
                                                         [20:19] == 10: Current supply increased
                                                         approximately 4.5%
                                                         [20:19] == 01: Design default.
                                                         [20:19] == 00: Current supply decreased
                                                         approximately 4.5%
                                                         [22:21] == 11: Rise and fall times are increased.
                                                         [22:21] == 10: Design default.
                                                         [22:21] == 01: Rise and fall times are decreased.
                                                         [22:21] == 00: Rise and fall times are decreased
                                                         further as compared to the 01 setting. */
	uint64_t tx_bs_enh                    : 1;  /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
	uint64_t tx_bs_en                     : 1;  /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
	uint64_t loop_enb                     : 1;  /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
	uint64_t vtest_enb                    : 1;  /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
	uint64_t bist_enb                     : 1;  /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
	uint64_t tdata_sel                    : 1;  /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
	uint64_t taddr_in                     : 4;  /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
	uint64_t tdata_in                     : 8;  /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
	uint64_t ate_reset                    : 1;  /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
#else
	uint64_t ate_reset                    : 1;
	uint64_t tdata_in                     : 8;
	uint64_t taddr_in                     : 4;
	uint64_t tdata_sel                    : 1;
	uint64_t bist_enb                     : 1;
	uint64_t vtest_enb                    : 1;
	uint64_t loop_enb                     : 1;
	uint64_t tx_bs_en                     : 1;
	uint64_t tx_bs_enh                    : 1;
	uint64_t tuning                       : 4;
	uint64_t hst_mode                     : 1;
	uint64_t dm_pulld                     : 1;
	uint64_t dp_pulld                     : 1;
	uint64_t tclk                         : 1;
	uint64_t usbp_bist                    : 1;
	uint64_t usbc_end                     : 1;
	uint64_t dma_bmode                    : 1;
	uint64_t reserved_30_31               : 2;
	uint64_t tdata_out                    : 4;
	uint64_t bist_err                     : 1;
	uint64_t bist_done                    : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} cn30xx;
	struct cvmx_usbnx_usbp_ctl_status_cn30xx cn31xx;
	struct cvmx_usbnx_usbp_ctl_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t txrisetune                   : 1;  /**< HS Transmitter Rise/Fall Time Adjustment */
	uint64_t txvreftune                   : 4;  /**< HS DC Voltage Level Adjustment */
	uint64_t txfslstune                   : 4;  /**< FS/LS Source Impedence Adjustment */
	uint64_t txhsxvtune                   : 2;  /**< Transmitter High-Speed Crossover Adjustment */
	uint64_t sqrxtune                     : 3;  /**< Squelch Threshold Adjustment */
	uint64_t compdistune                  : 3;  /**< Disconnect Threshold Adjustment */
	uint64_t otgtune                      : 3;  /**< VBUS Valid Threshold Adjustment */
	uint64_t otgdisable                   : 1;  /**< OTG Block Disable */
	uint64_t portreset                    : 1;  /**< Per_Port Reset */
	uint64_t drvvbus                      : 1;  /**< Drive VBUS */
	uint64_t lsbist                       : 1;  /**< Low-Speed BIST Enable. */
	uint64_t fsbist                       : 1;  /**< Full-Speed BIST Enable. */
	uint64_t hsbist                       : 1;  /**< High-Speed BIST Enable. */
	uint64_t bist_done                    : 1;  /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
	uint64_t bist_err                     : 1;  /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
	uint64_t tdata_out                    : 4;  /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
	uint64_t reserved_31_31               : 1;
	uint64_t txpreemphasistune            : 1;  /**< HS Transmitter Pre-Emphasis Enable */
	uint64_t dma_bmode                    : 1;  /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
	uint64_t usbc_end                     : 1;  /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
	uint64_t usbp_bist                    : 1;  /**< PHY, This is cleared '0' to run BIST on the USBP. */
	uint64_t tclk                         : 1;  /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
	uint64_t dp_pulld                     : 1;  /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t dm_pulld                     : 1;  /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t hst_mode                     : 1;  /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
	uint64_t reserved_19_22               : 4;
	uint64_t tx_bs_enh                    : 1;  /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
	uint64_t tx_bs_en                     : 1;  /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
	uint64_t loop_enb                     : 1;  /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
	uint64_t vtest_enb                    : 1;  /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
	uint64_t bist_enb                     : 1;  /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
	uint64_t tdata_sel                    : 1;  /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
	uint64_t taddr_in                     : 4;  /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
	uint64_t tdata_in                     : 8;  /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
	uint64_t ate_reset                    : 1;  /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
#else
	uint64_t ate_reset                    : 1;
	uint64_t tdata_in                     : 8;
	uint64_t taddr_in                     : 4;
	uint64_t tdata_sel                    : 1;
	uint64_t bist_enb                     : 1;
	uint64_t vtest_enb                    : 1;
	uint64_t loop_enb                     : 1;
	uint64_t tx_bs_en                     : 1;
	uint64_t tx_bs_enh                    : 1;
	uint64_t reserved_19_22               : 4;
	uint64_t hst_mode                     : 1;
	uint64_t dm_pulld                     : 1;
	uint64_t dp_pulld                     : 1;
	uint64_t tclk                         : 1;
	uint64_t usbp_bist                    : 1;
	uint64_t usbc_end                     : 1;
	uint64_t dma_bmode                    : 1;
	uint64_t txpreemphasistune            : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t tdata_out                    : 4;
	uint64_t bist_err                     : 1;
	uint64_t bist_done                    : 1;
	uint64_t hsbist                       : 1;
	uint64_t fsbist                       : 1;
	uint64_t lsbist                       : 1;
	uint64_t drvvbus                      : 1;
	uint64_t portreset                    : 1;
	uint64_t otgdisable                   : 1;
	uint64_t otgtune                      : 3;
	uint64_t compdistune                  : 3;
	uint64_t sqrxtune                     : 3;
	uint64_t txhsxvtune                   : 2;
	uint64_t txfslstune                   : 4;
	uint64_t txvreftune                   : 4;
	uint64_t txrisetune                   : 1;
#endif
	} cn50xx;
	struct cvmx_usbnx_usbp_ctl_status_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t txrisetune                   : 1;  /**< HS Transmitter Rise/Fall Time Adjustment */
	uint64_t txvreftune                   : 4;  /**< HS DC Voltage Level Adjustment */
	uint64_t txfslstune                   : 4;  /**< FS/LS Source Impedence Adjustment */
	uint64_t txhsxvtune                   : 2;  /**< Transmitter High-Speed Crossover Adjustment */
	uint64_t sqrxtune                     : 3;  /**< Squelch Threshold Adjustment */
	uint64_t compdistune                  : 3;  /**< Disconnect Threshold Adjustment */
	uint64_t otgtune                      : 3;  /**< VBUS Valid Threshold Adjustment */
	uint64_t otgdisable                   : 1;  /**< OTG Block Disable */
	uint64_t portreset                    : 1;  /**< Per_Port Reset */
	uint64_t drvvbus                      : 1;  /**< Drive VBUS */
	uint64_t lsbist                       : 1;  /**< Low-Speed BIST Enable. */
	uint64_t fsbist                       : 1;  /**< Full-Speed BIST Enable. */
	uint64_t hsbist                       : 1;  /**< High-Speed BIST Enable. */
	uint64_t bist_done                    : 1;  /**< PHY Bist Done.
                                                         Asserted at the end of the PHY BIST sequence. */
	uint64_t bist_err                     : 1;  /**< PHY Bist Error.
                                                         Indicates an internal error was detected during
                                                         the BIST sequence. */
	uint64_t tdata_out                    : 4;  /**< PHY Test Data Out.
                                                         Presents either internaly generated signals or
                                                         test register contents, based upon the value of
                                                         test_data_out_sel. */
	uint64_t siddq                        : 1;  /**< Drives the USBP (USB-PHY) SIDDQ input.
                                                         Normally should be set to zero.
                                                         When customers have no intent to use USB PHY
                                                         interface, they should:
                                                           - still provide 3.3V to USB_VDD33, and
                                                           - tie USB_REXT to 3.3V supply, and
                                                           - set USBN*_USBP_CTL_STATUS[SIDDQ]=1 */
	uint64_t txpreemphasistune            : 1;  /**< HS Transmitter Pre-Emphasis Enable */
	uint64_t dma_bmode                    : 1;  /**< When set to 1 the L2C DMA address will be updated
                                                         with byte-counts between packets. When set to 0
                                                         the L2C DMA address is incremented to the next
                                                         4-byte aligned address after adding byte-count. */
	uint64_t usbc_end                     : 1;  /**< Bigendian input to the USB Core. This should be
                                                         set to '0' for operation. */
	uint64_t usbp_bist                    : 1;  /**< PHY, This is cleared '0' to run BIST on the USBP. */
	uint64_t tclk                         : 1;  /**< PHY Test Clock, used to load TDATA_IN to the USBP. */
	uint64_t dp_pulld                     : 1;  /**< PHY DP_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D+ line. '1' pull down-resistance is connected
                                                         to D+/ '0' pull down resistance is not connected
                                                         to D+. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t dm_pulld                     : 1;  /**< PHY DM_PULLDOWN input to the USB-PHY.
                                                         This signal enables the pull-down resistance on
                                                         the D- line. '1' pull down-resistance is connected
                                                         to D-. '0' pull down resistance is not connected
                                                         to D-. When an A/B device is acting as a host
                                                         (downstream-facing port), dp_pulldown and
                                                         dm_pulldown are enabled. This must not toggle
                                                         during normal opeartion. */
	uint64_t hst_mode                     : 1;  /**< When '0' the USB is acting as HOST, when '1'
                                                         USB is acting as device. This field needs to be
                                                         set while the USB is in reset. */
	uint64_t reserved_19_22               : 4;
	uint64_t tx_bs_enh                    : 1;  /**< Transmit Bit Stuffing on [15:8].
                                                         Enables or disables bit stuffing on data[15:8]
                                                         when bit-stuffing is enabled. */
	uint64_t tx_bs_en                     : 1;  /**< Transmit Bit Stuffing on [7:0].
                                                         Enables or disables bit stuffing on data[7:0]
                                                         when bit-stuffing is enabled. */
	uint64_t loop_enb                     : 1;  /**< PHY Loopback Test Enable.
                                                         '1': During data transmission the receive is
                                                         enabled.
                                                         '0': During data transmission the receive is
                                                         disabled.
                                                         Must be '0' for normal operation. */
	uint64_t vtest_enb                    : 1;  /**< Analog Test Pin Enable.
                                                         '1' The PHY's analog_test pin is enabled for the
                                                         input and output of applicable analog test signals.
                                                         '0' THe analog_test pin is disabled. */
	uint64_t bist_enb                     : 1;  /**< Built-In Self Test Enable.
                                                         Used to activate BIST in the PHY. */
	uint64_t tdata_sel                    : 1;  /**< Test Data Out Select.
                                                         '1' test_data_out[3:0] (PHY) register contents
                                                         are output. '0' internaly generated signals are
                                                         output. */
	uint64_t taddr_in                     : 4;  /**< Mode Address for Test Interface.
                                                         Specifies the register address for writing to or
                                                         reading from the PHY test interface register. */
	uint64_t tdata_in                     : 8;  /**< Internal Testing Register Input Data and Select
                                                         This is a test bus. Data is present on [3:0],
                                                         and its corresponding select (enable) is present
                                                         on bits [7:4]. */
	uint64_t ate_reset                    : 1;  /**< Reset input from automatic test equipment.
                                                         This is a test signal. When the USB Core is
                                                         powered up (not in Susned Mode), an automatic
                                                         tester can use this to disable phy_clock and
                                                         free_clk, then re-eanable them with an aligned
                                                         phase.
                                                         '1': The phy_clk and free_clk outputs are
                                                         disabled. "0": The phy_clock and free_clk outputs
                                                         are available within a specific period after the
                                                         de-assertion. */
#else
	uint64_t ate_reset                    : 1;
	uint64_t tdata_in                     : 8;
	uint64_t taddr_in                     : 4;
	uint64_t tdata_sel                    : 1;
	uint64_t bist_enb                     : 1;
	uint64_t vtest_enb                    : 1;
	uint64_t loop_enb                     : 1;
	uint64_t tx_bs_en                     : 1;
	uint64_t tx_bs_enh                    : 1;
	uint64_t reserved_19_22               : 4;
	uint64_t hst_mode                     : 1;
	uint64_t dm_pulld                     : 1;
	uint64_t dp_pulld                     : 1;
	uint64_t tclk                         : 1;
	uint64_t usbp_bist                    : 1;
	uint64_t usbc_end                     : 1;
	uint64_t dma_bmode                    : 1;
	uint64_t txpreemphasistune            : 1;
	uint64_t siddq                        : 1;
	uint64_t tdata_out                    : 4;
	uint64_t bist_err                     : 1;
	uint64_t bist_done                    : 1;
	uint64_t hsbist                       : 1;
	uint64_t fsbist                       : 1;
	uint64_t lsbist                       : 1;
	uint64_t drvvbus                      : 1;
	uint64_t portreset                    : 1;
	uint64_t otgdisable                   : 1;
	uint64_t otgtune                      : 3;
	uint64_t compdistune                  : 3;
	uint64_t sqrxtune                     : 3;
	uint64_t txhsxvtune                   : 2;
	uint64_t txfslstune                   : 4;
	uint64_t txvreftune                   : 4;
	uint64_t txrisetune                   : 1;
#endif
	} cn52xx;
	struct cvmx_usbnx_usbp_ctl_status_cn50xx cn52xxp1;
	struct cvmx_usbnx_usbp_ctl_status_cn52xx cn56xx;
	struct cvmx_usbnx_usbp_ctl_status_cn50xx cn56xxp1;
};
typedef union cvmx_usbnx_usbp_ctl_status cvmx_usbnx_usbp_ctl_status_t;

#endif
