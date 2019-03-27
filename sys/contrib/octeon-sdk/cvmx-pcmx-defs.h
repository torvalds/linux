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
 * cvmx-pcmx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pcmx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PCMX_DEFS_H__
#define __CVMX_PCMX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_DMA_CFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_DMA_CFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010018ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_DMA_CFG(offset) (CVMX_ADD_IO_SEG(0x0001070000010018ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_INT_ENA(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_INT_ENA(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010020ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_INT_ENA(offset) (CVMX_ADD_IO_SEG(0x0001070000010020ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_INT_SUM(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_INT_SUM(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010028ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_INT_SUM(offset) (CVMX_ADD_IO_SEG(0x0001070000010028ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXADDR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXADDR(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010068ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXADDR(offset) (CVMX_ADD_IO_SEG(0x0001070000010068ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXCNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXCNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010060ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXCNT(offset) (CVMX_ADD_IO_SEG(0x0001070000010060ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100C0ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK0(offset) (CVMX_ADD_IO_SEG(0x00010700000100C0ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100C8ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK1(offset) (CVMX_ADD_IO_SEG(0x00010700000100C8ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100D0ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK2(offset) (CVMX_ADD_IO_SEG(0x00010700000100D0ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100D8ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK3(offset) (CVMX_ADD_IO_SEG(0x00010700000100D8ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100E0ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK4(offset) (CVMX_ADD_IO_SEG(0x00010700000100E0ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK5(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK5(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100E8ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK5(offset) (CVMX_ADD_IO_SEG(0x00010700000100E8ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK6(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK6(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100F0ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK6(offset) (CVMX_ADD_IO_SEG(0x00010700000100F0ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXMSK7(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXMSK7(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100F8ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXMSK7(offset) (CVMX_ADD_IO_SEG(0x00010700000100F8ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_RXSTART(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_RXSTART(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010058ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_RXSTART(offset) (CVMX_ADD_IO_SEG(0x0001070000010058ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TDM_CFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TDM_CFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010010ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TDM_CFG(offset) (CVMX_ADD_IO_SEG(0x0001070000010010ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TDM_DBG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TDM_DBG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010030ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TDM_DBG(offset) (CVMX_ADD_IO_SEG(0x0001070000010030ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXADDR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXADDR(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010050ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXADDR(offset) (CVMX_ADD_IO_SEG(0x0001070000010050ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXCNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXCNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010048ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXCNT(offset) (CVMX_ADD_IO_SEG(0x0001070000010048ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010080ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK0(offset) (CVMX_ADD_IO_SEG(0x0001070000010080ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010088ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK1(offset) (CVMX_ADD_IO_SEG(0x0001070000010088ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010090ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK2(offset) (CVMX_ADD_IO_SEG(0x0001070000010090ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010098ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK3(offset) (CVMX_ADD_IO_SEG(0x0001070000010098ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100A0ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK4(offset) (CVMX_ADD_IO_SEG(0x00010700000100A0ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK5(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK5(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100A8ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK5(offset) (CVMX_ADD_IO_SEG(0x00010700000100A8ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK6(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK6(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100B0ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK6(offset) (CVMX_ADD_IO_SEG(0x00010700000100B0ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXMSK7(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXMSK7(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010700000100B8ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXMSK7(offset) (CVMX_ADD_IO_SEG(0x00010700000100B8ull) + ((offset) & 3) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCMX_TXSTART(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCMX_TXSTART(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010040ull) + ((offset) & 3) * 16384;
}
#else
#define CVMX_PCMX_TXSTART(offset) (CVMX_ADD_IO_SEG(0x0001070000010040ull) + ((offset) & 3) * 16384)
#endif

/**
 * cvmx_pcm#_dma_cfg
 */
union cvmx_pcmx_dma_cfg {
	uint64_t u64;
	struct cvmx_pcmx_dma_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rdpend                       : 1;  /**< If 0, no L2C read responses pending               |          NS
                                                            1, L2C read responses are outstanding
                                                         NOTE: When restarting after stopping a running TDM
                                                         engine, software must wait for RDPEND to read 0
                                                         before writing PCMn_TDM_CFG[ENABLE] to a 1 */
	uint64_t reserved_54_62               : 9;
	uint64_t rxslots                      : 10; /**< Number of 8-bit slots to receive per frame        |          NS
                                                         (number of slots in a receive superframe) */
	uint64_t reserved_42_43               : 2;
	uint64_t txslots                      : 10; /**< Number of 8-bit slots to transmit per frame       |          NS
                                                         (number of slots in a transmit superframe) */
	uint64_t reserved_30_31               : 2;
	uint64_t rxst                         : 10; /**< Number of frame writes for interrupt              |          NS */
	uint64_t reserved_19_19               : 1;
	uint64_t useldt                       : 1;  /**< If 0, use LDI command to read from L2C            |          NS
                                                         1, use LDT command to read from L2C */
	uint64_t txrd                         : 10; /**< Number of frame reads for interrupt               |          NS */
	uint64_t fetchsiz                     : 4;  /**< FETCHSIZ+1 timeslots are read when threshold is   |          NS
                                                         reached. */
	uint64_t thresh                       : 4;  /**< If number of bytes remaining in the DMA fifo is <=|          NS
                                                         THRESH, initiate a fetch of timeslot data from the
                                                         transmit memory region.
                                                         NOTE: there are only 16B of buffer for each engine
                                                         so the seetings for FETCHSIZ and THRESH must be
                                                         such that the buffer will not be overrun:

                                                         THRESH + min(FETCHSIZ + 1,TXSLOTS) MUST BE <= 16 */
#else
	uint64_t thresh                       : 4;
	uint64_t fetchsiz                     : 4;
	uint64_t txrd                         : 10;
	uint64_t useldt                       : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t rxst                         : 10;
	uint64_t reserved_30_31               : 2;
	uint64_t txslots                      : 10;
	uint64_t reserved_42_43               : 2;
	uint64_t rxslots                      : 10;
	uint64_t reserved_54_62               : 9;
	uint64_t rdpend                       : 1;
#endif
	} s;
	struct cvmx_pcmx_dma_cfg_s            cn30xx;
	struct cvmx_pcmx_dma_cfg_s            cn31xx;
	struct cvmx_pcmx_dma_cfg_s            cn50xx;
	struct cvmx_pcmx_dma_cfg_s            cn61xx;
	struct cvmx_pcmx_dma_cfg_s            cnf71xx;
};
typedef union cvmx_pcmx_dma_cfg cvmx_pcmx_dma_cfg_t;

/**
 * cvmx_pcm#_int_ena
 */
union cvmx_pcmx_int_ena {
	uint64_t u64;
	struct cvmx_pcmx_int_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t rxovf                        : 1;  /**< Enable interrupt if RX byte overflows           |          NS */
	uint64_t txempty                      : 1;  /**< Enable interrupt on TX byte empty               |          NS */
	uint64_t txrd                         : 1;  /**< Enable DMA engine frame read interrupts         |          NS */
	uint64_t txwrap                       : 1;  /**< Enable TX region wrap interrupts                |          NS */
	uint64_t rxst                         : 1;  /**< Enable DMA engine frame store interrupts        |          NS */
	uint64_t rxwrap                       : 1;  /**< Enable RX region wrap interrupts                |          NS */
	uint64_t fsyncextra                   : 1;  /**< Enable FSYNC extra interrupts                   |          NS
                                                         NOTE: FSYNCEXTRA errors are defined as an FSYNC
                                                         found in the "wrong" spot of a frame given the
                                                         programming of PCMn_CLK_CFG[NUMSLOTS] and
                                                         PCMn_CLK_CFG[EXTRABIT]. */
	uint64_t fsyncmissed                  : 1;  /**< Enable FSYNC missed interrupts                  |          NS
                                                         NOTE: FSYNCMISSED errors are defined as an FSYNC
                                                         missing from the correct spot in a frame given
                                                         the programming of PCMn_CLK_CFG[NUMSLOTS] and
                                                         PCMn_CLK_CFG[EXTRABIT]. */
#else
	uint64_t fsyncmissed                  : 1;
	uint64_t fsyncextra                   : 1;
	uint64_t rxwrap                       : 1;
	uint64_t rxst                         : 1;
	uint64_t txwrap                       : 1;
	uint64_t txrd                         : 1;
	uint64_t txempty                      : 1;
	uint64_t rxovf                        : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pcmx_int_ena_s            cn30xx;
	struct cvmx_pcmx_int_ena_s            cn31xx;
	struct cvmx_pcmx_int_ena_s            cn50xx;
	struct cvmx_pcmx_int_ena_s            cn61xx;
	struct cvmx_pcmx_int_ena_s            cnf71xx;
};
typedef union cvmx_pcmx_int_ena cvmx_pcmx_int_ena_t;

/**
 * cvmx_pcm#_int_sum
 */
union cvmx_pcmx_int_sum {
	uint64_t u64;
	struct cvmx_pcmx_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t rxovf                        : 1;  /**< RX byte overflowed                              |           NS */
	uint64_t txempty                      : 1;  /**< TX byte was empty when sampled                  |           NS */
	uint64_t txrd                         : 1;  /**< DMA engine frame read interrupt occurred        |           NS */
	uint64_t txwrap                       : 1;  /**< TX region wrap interrupt occurred               |           NS */
	uint64_t rxst                         : 1;  /**< DMA engine frame store interrupt occurred       |           NS */
	uint64_t rxwrap                       : 1;  /**< RX region wrap interrupt occurred               |           NS */
	uint64_t fsyncextra                   : 1;  /**< FSYNC extra interrupt occurred                  |           NS */
	uint64_t fsyncmissed                  : 1;  /**< FSYNC missed interrupt occurred                 |           NS */
#else
	uint64_t fsyncmissed                  : 1;
	uint64_t fsyncextra                   : 1;
	uint64_t rxwrap                       : 1;
	uint64_t rxst                         : 1;
	uint64_t txwrap                       : 1;
	uint64_t txrd                         : 1;
	uint64_t txempty                      : 1;
	uint64_t rxovf                        : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pcmx_int_sum_s            cn30xx;
	struct cvmx_pcmx_int_sum_s            cn31xx;
	struct cvmx_pcmx_int_sum_s            cn50xx;
	struct cvmx_pcmx_int_sum_s            cn61xx;
	struct cvmx_pcmx_int_sum_s            cnf71xx;
};
typedef union cvmx_pcmx_int_sum cvmx_pcmx_int_sum_t;

/**
 * cvmx_pcm#_rxaddr
 */
union cvmx_pcmx_rxaddr {
	uint64_t u64;
	struct cvmx_pcmx_rxaddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< Address of the next write to the receive memory    |           NS
                                                         region */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_pcmx_rxaddr_s             cn30xx;
	struct cvmx_pcmx_rxaddr_s             cn31xx;
	struct cvmx_pcmx_rxaddr_s             cn50xx;
	struct cvmx_pcmx_rxaddr_s             cn61xx;
	struct cvmx_pcmx_rxaddr_s             cnf71xx;
};
typedef union cvmx_pcmx_rxaddr cvmx_pcmx_rxaddr_t;

/**
 * cvmx_pcm#_rxcnt
 */
union cvmx_pcmx_rxcnt {
	uint64_t u64;
	struct cvmx_pcmx_rxcnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t cnt                          : 16; /**< Number of superframes in receive memory region     |          NS */
#else
	uint64_t cnt                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcmx_rxcnt_s              cn30xx;
	struct cvmx_pcmx_rxcnt_s              cn31xx;
	struct cvmx_pcmx_rxcnt_s              cn50xx;
	struct cvmx_pcmx_rxcnt_s              cn61xx;
	struct cvmx_pcmx_rxcnt_s              cnf71xx;
};
typedef union cvmx_pcmx_rxcnt cvmx_pcmx_rxcnt_t;

/**
 * cvmx_pcm#_rxmsk0
 */
union cvmx_pcmx_rxmsk0 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 63 to 0                |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk0_s             cn30xx;
	struct cvmx_pcmx_rxmsk0_s             cn31xx;
	struct cvmx_pcmx_rxmsk0_s             cn50xx;
	struct cvmx_pcmx_rxmsk0_s             cn61xx;
	struct cvmx_pcmx_rxmsk0_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk0 cvmx_pcmx_rxmsk0_t;

/**
 * cvmx_pcm#_rxmsk1
 */
union cvmx_pcmx_rxmsk1 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 127 to 64              |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk1_s             cn30xx;
	struct cvmx_pcmx_rxmsk1_s             cn31xx;
	struct cvmx_pcmx_rxmsk1_s             cn50xx;
	struct cvmx_pcmx_rxmsk1_s             cn61xx;
	struct cvmx_pcmx_rxmsk1_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk1 cvmx_pcmx_rxmsk1_t;

/**
 * cvmx_pcm#_rxmsk2
 */
union cvmx_pcmx_rxmsk2 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 191 to 128             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk2_s             cn30xx;
	struct cvmx_pcmx_rxmsk2_s             cn31xx;
	struct cvmx_pcmx_rxmsk2_s             cn50xx;
	struct cvmx_pcmx_rxmsk2_s             cn61xx;
	struct cvmx_pcmx_rxmsk2_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk2 cvmx_pcmx_rxmsk2_t;

/**
 * cvmx_pcm#_rxmsk3
 */
union cvmx_pcmx_rxmsk3 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 255 to 192             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk3_s             cn30xx;
	struct cvmx_pcmx_rxmsk3_s             cn31xx;
	struct cvmx_pcmx_rxmsk3_s             cn50xx;
	struct cvmx_pcmx_rxmsk3_s             cn61xx;
	struct cvmx_pcmx_rxmsk3_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk3 cvmx_pcmx_rxmsk3_t;

/**
 * cvmx_pcm#_rxmsk4
 */
union cvmx_pcmx_rxmsk4 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 319 to 256             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk4_s             cn30xx;
	struct cvmx_pcmx_rxmsk4_s             cn31xx;
	struct cvmx_pcmx_rxmsk4_s             cn50xx;
	struct cvmx_pcmx_rxmsk4_s             cn61xx;
	struct cvmx_pcmx_rxmsk4_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk4 cvmx_pcmx_rxmsk4_t;

/**
 * cvmx_pcm#_rxmsk5
 */
union cvmx_pcmx_rxmsk5 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 383 to 320             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk5_s             cn30xx;
	struct cvmx_pcmx_rxmsk5_s             cn31xx;
	struct cvmx_pcmx_rxmsk5_s             cn50xx;
	struct cvmx_pcmx_rxmsk5_s             cn61xx;
	struct cvmx_pcmx_rxmsk5_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk5 cvmx_pcmx_rxmsk5_t;

/**
 * cvmx_pcm#_rxmsk6
 */
union cvmx_pcmx_rxmsk6 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 447 to 384             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk6_s             cn30xx;
	struct cvmx_pcmx_rxmsk6_s             cn31xx;
	struct cvmx_pcmx_rxmsk6_s             cn50xx;
	struct cvmx_pcmx_rxmsk6_s             cn61xx;
	struct cvmx_pcmx_rxmsk6_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk6 cvmx_pcmx_rxmsk6_t;

/**
 * cvmx_pcm#_rxmsk7
 */
union cvmx_pcmx_rxmsk7 {
	uint64_t u64;
	struct cvmx_pcmx_rxmsk7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Receive mask bits for slots 511 to 448             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_rxmsk7_s             cn30xx;
	struct cvmx_pcmx_rxmsk7_s             cn31xx;
	struct cvmx_pcmx_rxmsk7_s             cn50xx;
	struct cvmx_pcmx_rxmsk7_s             cn61xx;
	struct cvmx_pcmx_rxmsk7_s             cnf71xx;
};
typedef union cvmx_pcmx_rxmsk7 cvmx_pcmx_rxmsk7_t;

/**
 * cvmx_pcm#_rxstart
 */
union cvmx_pcmx_rxstart {
	uint64_t u64;
	struct cvmx_pcmx_rxstart_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 33; /**< Starting address for the receive memory region     |          NS */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t addr                         : 33;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_pcmx_rxstart_s            cn30xx;
	struct cvmx_pcmx_rxstart_s            cn31xx;
	struct cvmx_pcmx_rxstart_s            cn50xx;
	struct cvmx_pcmx_rxstart_s            cn61xx;
	struct cvmx_pcmx_rxstart_s            cnf71xx;
};
typedef union cvmx_pcmx_rxstart cvmx_pcmx_rxstart_t;

/**
 * cvmx_pcm#_tdm_cfg
 */
union cvmx_pcmx_tdm_cfg {
	uint64_t u64;
	struct cvmx_pcmx_tdm_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t drvtim                       : 16; /**< Number of ECLKs from start of bit time to stop    |          NS
                                                         driving last bit of timeslot (if not driving next
                                                         timeslot) */
	uint64_t samppt                       : 16; /**< Number of ECLKs from start of bit time to sample  |          NS
                                                         data bit. */
	uint64_t reserved_3_31                : 29;
	uint64_t lsbfirst                     : 1;  /**< If 0, shift/receive MSB first                     |          NS
                                                         1, shift/receive LSB first */
	uint64_t useclk1                      : 1;  /**< If 0, this PCM is based on BCLK/FSYNC0            |          NS
                                                         1, this PCM is based on BCLK/FSYNC1 */
	uint64_t enable                       : 1;  /**< If 1, PCM is enabled, otherwise pins are GPIOs    |          NS
                                                         NOTE: when TDM is disabled by detection of an
                                                         FSYNC error all transmission and reception is
                                                         halted.  In addition, PCMn_TX/RXADDR are updated
                                                         to point to the position at which the error was
                                                         detected. */
#else
	uint64_t enable                       : 1;
	uint64_t useclk1                      : 1;
	uint64_t lsbfirst                     : 1;
	uint64_t reserved_3_31                : 29;
	uint64_t samppt                       : 16;
	uint64_t drvtim                       : 16;
#endif
	} s;
	struct cvmx_pcmx_tdm_cfg_s            cn30xx;
	struct cvmx_pcmx_tdm_cfg_s            cn31xx;
	struct cvmx_pcmx_tdm_cfg_s            cn50xx;
	struct cvmx_pcmx_tdm_cfg_s            cn61xx;
	struct cvmx_pcmx_tdm_cfg_s            cnf71xx;
};
typedef union cvmx_pcmx_tdm_cfg cvmx_pcmx_tdm_cfg_t;

/**
 * cvmx_pcm#_tdm_dbg
 */
union cvmx_pcmx_tdm_dbg {
	uint64_t u64;
	struct cvmx_pcmx_tdm_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t debuginfo                    : 64; /**< Miscellaneous debug information                   |           NS */
#else
	uint64_t debuginfo                    : 64;
#endif
	} s;
	struct cvmx_pcmx_tdm_dbg_s            cn30xx;
	struct cvmx_pcmx_tdm_dbg_s            cn31xx;
	struct cvmx_pcmx_tdm_dbg_s            cn50xx;
	struct cvmx_pcmx_tdm_dbg_s            cn61xx;
	struct cvmx_pcmx_tdm_dbg_s            cnf71xx;
};
typedef union cvmx_pcmx_tdm_dbg cvmx_pcmx_tdm_dbg_t;

/**
 * cvmx_pcm#_txaddr
 */
union cvmx_pcmx_txaddr {
	uint64_t u64;
	struct cvmx_pcmx_txaddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 33; /**< Address of the next read from the transmit memory  |           NS
                                                         region */
	uint64_t fram                         : 3;  /**< Frame offset                                       |           NS
                                                         NOTE: this is used to extract the correct byte from
                                                         each 64b word read from the transmit memory region */
#else
	uint64_t fram                         : 3;
	uint64_t addr                         : 33;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_pcmx_txaddr_s             cn30xx;
	struct cvmx_pcmx_txaddr_s             cn31xx;
	struct cvmx_pcmx_txaddr_s             cn50xx;
	struct cvmx_pcmx_txaddr_s             cn61xx;
	struct cvmx_pcmx_txaddr_s             cnf71xx;
};
typedef union cvmx_pcmx_txaddr cvmx_pcmx_txaddr_t;

/**
 * cvmx_pcm#_txcnt
 */
union cvmx_pcmx_txcnt {
	uint64_t u64;
	struct cvmx_pcmx_txcnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t cnt                          : 16; /**< Number of superframes in transmit memory region    |          NS */
#else
	uint64_t cnt                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcmx_txcnt_s              cn30xx;
	struct cvmx_pcmx_txcnt_s              cn31xx;
	struct cvmx_pcmx_txcnt_s              cn50xx;
	struct cvmx_pcmx_txcnt_s              cn61xx;
	struct cvmx_pcmx_txcnt_s              cnf71xx;
};
typedef union cvmx_pcmx_txcnt cvmx_pcmx_txcnt_t;

/**
 * cvmx_pcm#_txmsk0
 */
union cvmx_pcmx_txmsk0 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 63 to 0               |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk0_s             cn30xx;
	struct cvmx_pcmx_txmsk0_s             cn31xx;
	struct cvmx_pcmx_txmsk0_s             cn50xx;
	struct cvmx_pcmx_txmsk0_s             cn61xx;
	struct cvmx_pcmx_txmsk0_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk0 cvmx_pcmx_txmsk0_t;

/**
 * cvmx_pcm#_txmsk1
 */
union cvmx_pcmx_txmsk1 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 127 to 64             |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk1_s             cn30xx;
	struct cvmx_pcmx_txmsk1_s             cn31xx;
	struct cvmx_pcmx_txmsk1_s             cn50xx;
	struct cvmx_pcmx_txmsk1_s             cn61xx;
	struct cvmx_pcmx_txmsk1_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk1 cvmx_pcmx_txmsk1_t;

/**
 * cvmx_pcm#_txmsk2
 */
union cvmx_pcmx_txmsk2 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 191 to 128            |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk2_s             cn30xx;
	struct cvmx_pcmx_txmsk2_s             cn31xx;
	struct cvmx_pcmx_txmsk2_s             cn50xx;
	struct cvmx_pcmx_txmsk2_s             cn61xx;
	struct cvmx_pcmx_txmsk2_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk2 cvmx_pcmx_txmsk2_t;

/**
 * cvmx_pcm#_txmsk3
 */
union cvmx_pcmx_txmsk3 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 255 to 192            |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk3_s             cn30xx;
	struct cvmx_pcmx_txmsk3_s             cn31xx;
	struct cvmx_pcmx_txmsk3_s             cn50xx;
	struct cvmx_pcmx_txmsk3_s             cn61xx;
	struct cvmx_pcmx_txmsk3_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk3 cvmx_pcmx_txmsk3_t;

/**
 * cvmx_pcm#_txmsk4
 */
union cvmx_pcmx_txmsk4 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 319 to 256            |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk4_s             cn30xx;
	struct cvmx_pcmx_txmsk4_s             cn31xx;
	struct cvmx_pcmx_txmsk4_s             cn50xx;
	struct cvmx_pcmx_txmsk4_s             cn61xx;
	struct cvmx_pcmx_txmsk4_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk4 cvmx_pcmx_txmsk4_t;

/**
 * cvmx_pcm#_txmsk5
 */
union cvmx_pcmx_txmsk5 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 383 to 320            |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk5_s             cn30xx;
	struct cvmx_pcmx_txmsk5_s             cn31xx;
	struct cvmx_pcmx_txmsk5_s             cn50xx;
	struct cvmx_pcmx_txmsk5_s             cn61xx;
	struct cvmx_pcmx_txmsk5_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk5 cvmx_pcmx_txmsk5_t;

/**
 * cvmx_pcm#_txmsk6
 */
union cvmx_pcmx_txmsk6 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 447 to 384            |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk6_s             cn30xx;
	struct cvmx_pcmx_txmsk6_s             cn31xx;
	struct cvmx_pcmx_txmsk6_s             cn50xx;
	struct cvmx_pcmx_txmsk6_s             cn61xx;
	struct cvmx_pcmx_txmsk6_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk6 cvmx_pcmx_txmsk6_t;

/**
 * cvmx_pcm#_txmsk7
 */
union cvmx_pcmx_txmsk7 {
	uint64_t u64;
	struct cvmx_pcmx_txmsk7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mask                         : 64; /**< Transmit mask bits for slots 511 to 448            |          NS
                                                         (1 means transmit, 0 means don't transmit) */
#else
	uint64_t mask                         : 64;
#endif
	} s;
	struct cvmx_pcmx_txmsk7_s             cn30xx;
	struct cvmx_pcmx_txmsk7_s             cn31xx;
	struct cvmx_pcmx_txmsk7_s             cn50xx;
	struct cvmx_pcmx_txmsk7_s             cn61xx;
	struct cvmx_pcmx_txmsk7_s             cnf71xx;
};
typedef union cvmx_pcmx_txmsk7 cvmx_pcmx_txmsk7_t;

/**
 * cvmx_pcm#_txstart
 */
union cvmx_pcmx_txstart {
	uint64_t u64;
	struct cvmx_pcmx_txstart_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 33; /**< Starting address for the transmit memory region    |          NS */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t addr                         : 33;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_pcmx_txstart_s            cn30xx;
	struct cvmx_pcmx_txstart_s            cn31xx;
	struct cvmx_pcmx_txstart_s            cn50xx;
	struct cvmx_pcmx_txstart_s            cn61xx;
	struct cvmx_pcmx_txstart_s            cnf71xx;
};
typedef union cvmx_pcmx_txstart cvmx_pcmx_txstart_t;

#endif
