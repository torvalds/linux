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
 * cvmx-ipd-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon ipd.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_IPD_DEFS_H__
#define __CVMX_IPD_DEFS_H__

#define CVMX_IPD_1ST_MBUFF_SKIP (CVMX_ADD_IO_SEG(0x00014F0000000000ull))
#define CVMX_IPD_1st_NEXT_PTR_BACK (CVMX_ADD_IO_SEG(0x00014F0000000150ull))
#define CVMX_IPD_2nd_NEXT_PTR_BACK (CVMX_ADD_IO_SEG(0x00014F0000000158ull))
#define CVMX_IPD_BIST_STATUS (CVMX_ADD_IO_SEG(0x00014F00000007F8ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_BPIDX_MBUF_TH(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_IPD_BPIDX_MBUF_TH(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000002000ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_IPD_BPIDX_MBUF_TH(offset) (CVMX_ADD_IO_SEG(0x00014F0000002000ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_BPID_BP_COUNTERX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_IPD_BPID_BP_COUNTERX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000003000ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_IPD_BPID_BP_COUNTERX(offset) (CVMX_ADD_IO_SEG(0x00014F0000003000ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_BP_PRT_RED_END CVMX_IPD_BP_PRT_RED_END_FUNC()
static inline uint64_t CVMX_IPD_BP_PRT_RED_END_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_BP_PRT_RED_END not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000328ull);
}
#else
#define CVMX_IPD_BP_PRT_RED_END (CVMX_ADD_IO_SEG(0x00014F0000000328ull))
#endif
#define CVMX_IPD_CLK_COUNT (CVMX_ADD_IO_SEG(0x00014F0000000338ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_CREDITS CVMX_IPD_CREDITS_FUNC()
static inline uint64_t CVMX_IPD_CREDITS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_CREDITS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000004410ull);
}
#else
#define CVMX_IPD_CREDITS (CVMX_ADD_IO_SEG(0x00014F0000004410ull))
#endif
#define CVMX_IPD_CTL_STATUS (CVMX_ADD_IO_SEG(0x00014F0000000018ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_ECC_CTL CVMX_IPD_ECC_CTL_FUNC()
static inline uint64_t CVMX_IPD_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000004408ull);
}
#else
#define CVMX_IPD_ECC_CTL (CVMX_ADD_IO_SEG(0x00014F0000004408ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_FREE_PTR_FIFO_CTL CVMX_IPD_FREE_PTR_FIFO_CTL_FUNC()
static inline uint64_t CVMX_IPD_FREE_PTR_FIFO_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_FREE_PTR_FIFO_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000780ull);
}
#else
#define CVMX_IPD_FREE_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000780ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_FREE_PTR_VALUE CVMX_IPD_FREE_PTR_VALUE_FUNC()
static inline uint64_t CVMX_IPD_FREE_PTR_VALUE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_FREE_PTR_VALUE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000788ull);
}
#else
#define CVMX_IPD_FREE_PTR_VALUE (CVMX_ADD_IO_SEG(0x00014F0000000788ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_HOLD_PTR_FIFO_CTL CVMX_IPD_HOLD_PTR_FIFO_CTL_FUNC()
static inline uint64_t CVMX_IPD_HOLD_PTR_FIFO_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_HOLD_PTR_FIFO_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000790ull);
}
#else
#define CVMX_IPD_HOLD_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000790ull))
#endif
#define CVMX_IPD_INT_ENB (CVMX_ADD_IO_SEG(0x00014F0000000160ull))
#define CVMX_IPD_INT_SUM (CVMX_ADD_IO_SEG(0x00014F0000000168ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_NEXT_PKT_PTR CVMX_IPD_NEXT_PKT_PTR_FUNC()
static inline uint64_t CVMX_IPD_NEXT_PKT_PTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_NEXT_PKT_PTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F00000007A0ull);
}
#else
#define CVMX_IPD_NEXT_PKT_PTR (CVMX_ADD_IO_SEG(0x00014F00000007A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_NEXT_WQE_PTR CVMX_IPD_NEXT_WQE_PTR_FUNC()
static inline uint64_t CVMX_IPD_NEXT_WQE_PTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_NEXT_WQE_PTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F00000007A8ull);
}
#else
#define CVMX_IPD_NEXT_WQE_PTR (CVMX_ADD_IO_SEG(0x00014F00000007A8ull))
#endif
#define CVMX_IPD_NOT_1ST_MBUFF_SKIP (CVMX_ADD_IO_SEG(0x00014F0000000008ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_ON_BP_DROP_PKTX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_IPD_ON_BP_DROP_PKTX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00014F0000004100ull);
}
#else
#define CVMX_IPD_ON_BP_DROP_PKTX(block_id) (CVMX_ADD_IO_SEG(0x00014F0000004100ull))
#endif
#define CVMX_IPD_PACKET_MBUFF_SIZE (CVMX_ADD_IO_SEG(0x00014F0000000010ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_PKT_ERR CVMX_IPD_PKT_ERR_FUNC()
static inline uint64_t CVMX_IPD_PKT_ERR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_PKT_ERR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F00000003F0ull);
}
#else
#define CVMX_IPD_PKT_ERR (CVMX_ADD_IO_SEG(0x00014F00000003F0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_PKT_PTR_VALID CVMX_IPD_PKT_PTR_VALID_FUNC()
static inline uint64_t CVMX_IPD_PKT_PTR_VALID_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_PKT_PTR_VALID not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000358ull);
}
#else
#define CVMX_IPD_PKT_PTR_VALID (CVMX_ADD_IO_SEG(0x00014F0000000358ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORTX_BP_PAGE_CNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || (offset == 32))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35))))))
		cvmx_warn("CVMX_IPD_PORTX_BP_PAGE_CNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000028ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_IPD_PORTX_BP_PAGE_CNT(offset) (CVMX_ADD_IO_SEG(0x00014F0000000028ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORTX_BP_PAGE_CNT2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && (((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_IPD_PORTX_BP_PAGE_CNT2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000368ull) + ((offset) & 63) * 8 - 8*36;
}
#else
#define CVMX_IPD_PORTX_BP_PAGE_CNT2(offset) (CVMX_ADD_IO_SEG(0x00014F0000000368ull) + ((offset) & 63) * 8 - 8*36)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORTX_BP_PAGE_CNT3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && (((offset >= 40) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && (((offset >= 40) && (offset <= 47))))))
		cvmx_warn("CVMX_IPD_PORTX_BP_PAGE_CNT3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F00000003D0ull) + ((offset) & 63) * 8 - 8*40;
}
#else
#define CVMX_IPD_PORTX_BP_PAGE_CNT3(offset) (CVMX_ADD_IO_SEG(0x00014F00000003D0ull) + ((offset) & 63) * 8 - 8*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_BP_COUNTERS2_PAIRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && (((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_IPD_PORT_BP_COUNTERS2_PAIRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000388ull) + ((offset) & 63) * 8 - 8*36;
}
#else
#define CVMX_IPD_PORT_BP_COUNTERS2_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000388ull) + ((offset) & 63) * 8 - 8*36)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_BP_COUNTERS3_PAIRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && (((offset >= 40) && (offset <= 43))))))
		cvmx_warn("CVMX_IPD_PORT_BP_COUNTERS3_PAIRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F00000003B0ull) + ((offset) & 63) * 8 - 8*40;
}
#else
#define CVMX_IPD_PORT_BP_COUNTERS3_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F00000003B0ull) + ((offset) & 63) * 8 - 8*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_BP_COUNTERS4_PAIRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && (((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && (((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_IPD_PORT_BP_COUNTERS4_PAIRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000410ull) + ((offset) & 63) * 8 - 8*44;
}
#else
#define CVMX_IPD_PORT_BP_COUNTERS4_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000410ull) + ((offset) & 63) * 8 - 8*44)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_BP_COUNTERS_PAIRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || (offset == 32))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35))))))
		cvmx_warn("CVMX_IPD_PORT_BP_COUNTERS_PAIRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F00000001B8ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_IPD_PORT_BP_COUNTERS_PAIRX(offset) (CVMX_ADD_IO_SEG(0x00014F00000001B8ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_PORT_PTR_FIFO_CTL CVMX_IPD_PORT_PTR_FIFO_CTL_FUNC()
static inline uint64_t CVMX_IPD_PORT_PTR_FIFO_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_PORT_PTR_FIFO_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000798ull);
}
#else
#define CVMX_IPD_PORT_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000798ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_QOS_INTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset == 0) || (offset == 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0) || (offset == 2) || (offset == 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset == 0) || (offset == 2) || (offset == 4) || (offset == 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset == 0) || (offset == 4) || (offset == 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset == 0) || (offset == 2) || (offset == 4) || (offset == 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset == 0) || (offset == 2) || (offset == 4) || (offset == 5)))))
		cvmx_warn("CVMX_IPD_PORT_QOS_INTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000808ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_IPD_PORT_QOS_INTX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000808ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_QOS_INT_ENBX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset == 0) || (offset == 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0) || (offset == 2) || (offset == 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset == 0) || (offset == 2) || (offset == 4) || (offset == 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset == 0) || (offset == 4) || (offset == 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset == 0) || (offset == 2) || (offset == 4) || (offset == 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset == 0) || (offset == 2) || (offset == 4) || (offset == 5)))))
		cvmx_warn("CVMX_IPD_PORT_QOS_INT_ENBX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000848ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_IPD_PORT_QOS_INT_ENBX(offset) (CVMX_ADD_IO_SEG(0x00014F0000000848ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_QOS_X_CNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31) || ((offset >= 256) && (offset <= 319)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31) || ((offset >= 128) && (offset <= 159)) || ((offset >= 256) && (offset <= 319)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 31) || ((offset >= 128) && (offset <= 159)) || ((offset >= 256) && (offset <= 383)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 31) || ((offset >= 256) && (offset <= 351)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 31) || ((offset >= 128) && (offset <= 159)) || ((offset >= 256) && (offset <= 335)) || ((offset >= 352) && (offset <= 383)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 511))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 31) || ((offset >= 128) && (offset <= 159)) || ((offset >= 256) && (offset <= 383))))))
		cvmx_warn("CVMX_IPD_PORT_QOS_X_CNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000888ull) + ((offset) & 511) * 8;
}
#else
#define CVMX_IPD_PORT_QOS_X_CNT(offset) (CVMX_ADD_IO_SEG(0x00014F0000000888ull) + ((offset) & 511) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_PORT_SOPX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_IPD_PORT_SOPX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00014F0000004400ull);
}
#else
#define CVMX_IPD_PORT_SOPX(block_id) (CVMX_ADD_IO_SEG(0x00014F0000004400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL_FUNC()
static inline uint64_t CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000348ull);
}
#else
#define CVMX_IPD_PRC_HOLD_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000348ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_PRC_PORT_PTR_FIFO_CTL CVMX_IPD_PRC_PORT_PTR_FIFO_CTL_FUNC()
static inline uint64_t CVMX_IPD_PRC_PORT_PTR_FIFO_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_PRC_PORT_PTR_FIFO_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000350ull);
}
#else
#define CVMX_IPD_PRC_PORT_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000350ull))
#endif
#define CVMX_IPD_PTR_COUNT (CVMX_ADD_IO_SEG(0x00014F0000000320ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_PWP_PTR_FIFO_CTL CVMX_IPD_PWP_PTR_FIFO_CTL_FUNC()
static inline uint64_t CVMX_IPD_PWP_PTR_FIFO_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_PWP_PTR_FIFO_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000340ull);
}
#else
#define CVMX_IPD_PWP_PTR_FIFO_CTL (CVMX_ADD_IO_SEG(0x00014F0000000340ull))
#endif
#define CVMX_IPD_QOS0_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(0)
#define CVMX_IPD_QOS1_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(1)
#define CVMX_IPD_QOS2_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(2)
#define CVMX_IPD_QOS3_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(3)
#define CVMX_IPD_QOS4_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(4)
#define CVMX_IPD_QOS5_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(5)
#define CVMX_IPD_QOS6_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(6)
#define CVMX_IPD_QOS7_RED_MARKS CVMX_IPD_QOSX_RED_MARKS(7)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_QOSX_RED_MARKS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_IPD_QOSX_RED_MARKS(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F0000000178ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_IPD_QOSX_RED_MARKS(offset) (CVMX_ADD_IO_SEG(0x00014F0000000178ull) + ((offset) & 7) * 8)
#endif
#define CVMX_IPD_QUE0_FREE_PAGE_CNT (CVMX_ADD_IO_SEG(0x00014F0000000330ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_RED_BPID_ENABLEX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_IPD_RED_BPID_ENABLEX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00014F0000004200ull);
}
#else
#define CVMX_IPD_RED_BPID_ENABLEX(block_id) (CVMX_ADD_IO_SEG(0x00014F0000004200ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_RED_DELAY CVMX_IPD_RED_DELAY_FUNC()
static inline uint64_t CVMX_IPD_RED_DELAY_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_RED_DELAY not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000004300ull);
}
#else
#define CVMX_IPD_RED_DELAY (CVMX_ADD_IO_SEG(0x00014F0000004300ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_RED_PORT_ENABLE CVMX_IPD_RED_PORT_ENABLE_FUNC()
static inline uint64_t CVMX_IPD_RED_PORT_ENABLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_RED_PORT_ENABLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F00000002D8ull);
}
#else
#define CVMX_IPD_RED_PORT_ENABLE (CVMX_ADD_IO_SEG(0x00014F00000002D8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_RED_PORT_ENABLE2 CVMX_IPD_RED_PORT_ENABLE2_FUNC()
static inline uint64_t CVMX_IPD_RED_PORT_ENABLE2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_RED_PORT_ENABLE2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F00000003A8ull);
}
#else
#define CVMX_IPD_RED_PORT_ENABLE2 (CVMX_ADD_IO_SEG(0x00014F00000003A8ull))
#endif
#define CVMX_IPD_RED_QUE0_PARAM CVMX_IPD_RED_QUEX_PARAM(0)
#define CVMX_IPD_RED_QUE1_PARAM CVMX_IPD_RED_QUEX_PARAM(1)
#define CVMX_IPD_RED_QUE2_PARAM CVMX_IPD_RED_QUEX_PARAM(2)
#define CVMX_IPD_RED_QUE3_PARAM CVMX_IPD_RED_QUEX_PARAM(3)
#define CVMX_IPD_RED_QUE4_PARAM CVMX_IPD_RED_QUEX_PARAM(4)
#define CVMX_IPD_RED_QUE5_PARAM CVMX_IPD_RED_QUEX_PARAM(5)
#define CVMX_IPD_RED_QUE6_PARAM CVMX_IPD_RED_QUEX_PARAM(6)
#define CVMX_IPD_RED_QUE7_PARAM CVMX_IPD_RED_QUEX_PARAM(7)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_IPD_RED_QUEX_PARAM(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_IPD_RED_QUEX_PARAM(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00014F00000002E0ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_IPD_RED_QUEX_PARAM(offset) (CVMX_ADD_IO_SEG(0x00014F00000002E0ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_REQ_WGT CVMX_IPD_REQ_WGT_FUNC()
static inline uint64_t CVMX_IPD_REQ_WGT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IPD_REQ_WGT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000004418ull);
}
#else
#define CVMX_IPD_REQ_WGT (CVMX_ADD_IO_SEG(0x00014F0000004418ull))
#endif
#define CVMX_IPD_SUB_PORT_BP_PAGE_CNT (CVMX_ADD_IO_SEG(0x00014F0000000148ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_SUB_PORT_FCS CVMX_IPD_SUB_PORT_FCS_FUNC()
static inline uint64_t CVMX_IPD_SUB_PORT_FCS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_SUB_PORT_FCS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000170ull);
}
#else
#define CVMX_IPD_SUB_PORT_FCS (CVMX_ADD_IO_SEG(0x00014F0000000170ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_SUB_PORT_QOS_CNT CVMX_IPD_SUB_PORT_QOS_CNT_FUNC()
static inline uint64_t CVMX_IPD_SUB_PORT_QOS_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_SUB_PORT_QOS_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000800ull);
}
#else
#define CVMX_IPD_SUB_PORT_QOS_CNT (CVMX_ADD_IO_SEG(0x00014F0000000800ull))
#endif
#define CVMX_IPD_WQE_FPA_QUEUE (CVMX_ADD_IO_SEG(0x00014F0000000020ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IPD_WQE_PTR_VALID CVMX_IPD_WQE_PTR_VALID_FUNC()
static inline uint64_t CVMX_IPD_WQE_PTR_VALID_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_IPD_WQE_PTR_VALID not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00014F0000000360ull);
}
#else
#define CVMX_IPD_WQE_PTR_VALID (CVMX_ADD_IO_SEG(0x00014F0000000360ull))
#endif

/**
 * cvmx_ipd_1st_mbuff_skip
 *
 * IPD_1ST_MBUFF_SKIP = IPD First MBUFF Word Skip Size
 *
 * The number of words that the IPD will skip when writing the first MBUFF.
 */
union cvmx_ipd_1st_mbuff_skip {
	uint64_t u64;
	struct cvmx_ipd_1st_mbuff_skip_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t skip_sz                      : 6;  /**< The number of 8-byte words from the top of the
                                                         1st MBUFF that the IPD will store the next-pointer.
                                                         Legal values are 0 to 32, where the MAX value
                                                         is also limited to:
                                                         IPD_PACKET_MBUFF_SIZE[MB_SIZE] - 18.
                                                         Must be at least 16 when IPD_CTL_STATUS[NO_WPTR]
                                                         is set. */
#else
	uint64_t skip_sz                      : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_ipd_1st_mbuff_skip_s      cn30xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn31xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn38xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn38xxp2;
	struct cvmx_ipd_1st_mbuff_skip_s      cn50xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn52xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn52xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s      cn56xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn56xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s      cn58xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn58xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s      cn61xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn63xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn63xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s      cn66xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn68xx;
	struct cvmx_ipd_1st_mbuff_skip_s      cn68xxp1;
	struct cvmx_ipd_1st_mbuff_skip_s      cnf71xx;
};
typedef union cvmx_ipd_1st_mbuff_skip cvmx_ipd_1st_mbuff_skip_t;

/**
 * cvmx_ipd_1st_next_ptr_back
 *
 * IPD_1st_NEXT_PTR_BACK = IPD First Next Pointer Back Values
 *
 * Contains the Back Field for use in creating the Next Pointer Header for the First MBUF
 */
union cvmx_ipd_1st_next_ptr_back {
	uint64_t u64;
	struct cvmx_ipd_1st_next_ptr_back_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t back                         : 4;  /**< Used to find head of buffer from the nxt-hdr-ptr. */
#else
	uint64_t back                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ipd_1st_next_ptr_back_s   cn30xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn31xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn38xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn38xxp2;
	struct cvmx_ipd_1st_next_ptr_back_s   cn50xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn52xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn52xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s   cn56xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn56xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s   cn58xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn58xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s   cn61xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn63xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn63xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s   cn66xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn68xx;
	struct cvmx_ipd_1st_next_ptr_back_s   cn68xxp1;
	struct cvmx_ipd_1st_next_ptr_back_s   cnf71xx;
};
typedef union cvmx_ipd_1st_next_ptr_back cvmx_ipd_1st_next_ptr_back_t;

/**
 * cvmx_ipd_2nd_next_ptr_back
 *
 * IPD_2nd_NEXT_PTR_BACK = IPD Second Next Pointer Back Value
 *
 * Contains the Back Field for use in creating the Next Pointer Header for the First MBUF
 */
union cvmx_ipd_2nd_next_ptr_back {
	uint64_t u64;
	struct cvmx_ipd_2nd_next_ptr_back_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t back                         : 4;  /**< Used to find head of buffer from the nxt-hdr-ptr. */
#else
	uint64_t back                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn30xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn31xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn38xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn38xxp2;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn50xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn52xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn52xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn56xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn56xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn58xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn58xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn61xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn63xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn63xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn66xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn68xx;
	struct cvmx_ipd_2nd_next_ptr_back_s   cn68xxp1;
	struct cvmx_ipd_2nd_next_ptr_back_s   cnf71xx;
};
typedef union cvmx_ipd_2nd_next_ptr_back cvmx_ipd_2nd_next_ptr_back_t;

/**
 * cvmx_ipd_bist_status
 *
 * IPD_BIST_STATUS = IPD BIST STATUS
 *
 * BIST Status for IPD's Memories.
 */
union cvmx_ipd_bist_status {
	uint64_t u64;
	struct cvmx_ipd_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t iiwo1                        : 1;  /**< IPD IOB WQE Dataout MEM1 Bist Status. */
	uint64_t iiwo0                        : 1;  /**< IPD IOB WQE Dataout MEM0 Bist Status. */
	uint64_t iio1                         : 1;  /**< IPD IOB Dataout MEM1 Bist Status. */
	uint64_t iio0                         : 1;  /**< IPD IOB Dataout MEM0 Bist Status. */
	uint64_t pbm4                         : 1;  /**< PBM4Memory Bist Status. */
	uint64_t csr_mem                      : 1;  /**< CSR Register Memory Bist Status. */
	uint64_t csr_ncmd                     : 1;  /**< CSR NCB Commands Memory Bist Status. */
	uint64_t pwq_wqed                     : 1;  /**< PWQ PIP WQE DONE Memory Bist Status. */
	uint64_t pwq_wp1                      : 1;  /**< PWQ WQE PAGE1 PTR Memory Bist Status. */
	uint64_t pwq_pow                      : 1;  /**< PWQ POW MEM Memory Bist Status. */
	uint64_t ipq_pbe1                     : 1;  /**< IPQ PBE1 Memory Bist Status. */
	uint64_t ipq_pbe0                     : 1;  /**< IPQ PBE0 Memory Bist Status. */
	uint64_t pbm3                         : 1;  /**< PBM3 Memory Bist Status. */
	uint64_t pbm2                         : 1;  /**< PBM2 Memory Bist Status. */
	uint64_t pbm1                         : 1;  /**< PBM1 Memory Bist Status. */
	uint64_t pbm0                         : 1;  /**< PBM0 Memory Bist Status. */
	uint64_t pbm_word                     : 1;  /**< PBM_WORD Memory Bist Status. */
	uint64_t pwq1                         : 1;  /**< PWQ1 Memory Bist Status. */
	uint64_t pwq0                         : 1;  /**< PWQ0 Memory Bist Status. */
	uint64_t prc_off                      : 1;  /**< PRC_OFF Memory Bist Status. */
	uint64_t ipd_old                      : 1;  /**< IPD_OLD Memory Bist Status. */
	uint64_t ipd_new                      : 1;  /**< IPD_NEW Memory Bist Status. */
	uint64_t pwp                          : 1;  /**< PWP Memory Bist Status. */
#else
	uint64_t pwp                          : 1;
	uint64_t ipd_new                      : 1;
	uint64_t ipd_old                      : 1;
	uint64_t prc_off                      : 1;
	uint64_t pwq0                         : 1;
	uint64_t pwq1                         : 1;
	uint64_t pbm_word                     : 1;
	uint64_t pbm0                         : 1;
	uint64_t pbm1                         : 1;
	uint64_t pbm2                         : 1;
	uint64_t pbm3                         : 1;
	uint64_t ipq_pbe0                     : 1;
	uint64_t ipq_pbe1                     : 1;
	uint64_t pwq_pow                      : 1;
	uint64_t pwq_wp1                      : 1;
	uint64_t pwq_wqed                     : 1;
	uint64_t csr_ncmd                     : 1;
	uint64_t csr_mem                      : 1;
	uint64_t pbm4                         : 1;
	uint64_t iio0                         : 1;
	uint64_t iio1                         : 1;
	uint64_t iiwo0                        : 1;
	uint64_t iiwo1                        : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_ipd_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t pwq_wqed                     : 1;  /**< PWQ PIP WQE DONE Memory Bist Status. */
	uint64_t pwq_wp1                      : 1;  /**< PWQ WQE PAGE1 PTR Memory Bist Status. */
	uint64_t pwq_pow                      : 1;  /**< PWQ POW MEM Memory Bist Status. */
	uint64_t ipq_pbe1                     : 1;  /**< IPQ PBE1 Memory Bist Status. */
	uint64_t ipq_pbe0                     : 1;  /**< IPQ PBE0 Memory Bist Status. */
	uint64_t pbm3                         : 1;  /**< PBM3 Memory Bist Status. */
	uint64_t pbm2                         : 1;  /**< PBM2 Memory Bist Status. */
	uint64_t pbm1                         : 1;  /**< PBM1 Memory Bist Status. */
	uint64_t pbm0                         : 1;  /**< PBM0 Memory Bist Status. */
	uint64_t pbm_word                     : 1;  /**< PBM_WORD Memory Bist Status. */
	uint64_t pwq1                         : 1;  /**< PWQ1 Memory Bist Status. */
	uint64_t pwq0                         : 1;  /**< PWQ0 Memory Bist Status. */
	uint64_t prc_off                      : 1;  /**< PRC_OFF Memory Bist Status. */
	uint64_t ipd_old                      : 1;  /**< IPD_OLD Memory Bist Status. */
	uint64_t ipd_new                      : 1;  /**< IPD_NEW Memory Bist Status. */
	uint64_t pwp                          : 1;  /**< PWP Memory Bist Status. */
#else
	uint64_t pwp                          : 1;
	uint64_t ipd_new                      : 1;
	uint64_t ipd_old                      : 1;
	uint64_t prc_off                      : 1;
	uint64_t pwq0                         : 1;
	uint64_t pwq1                         : 1;
	uint64_t pbm_word                     : 1;
	uint64_t pbm0                         : 1;
	uint64_t pbm1                         : 1;
	uint64_t pbm2                         : 1;
	uint64_t pbm3                         : 1;
	uint64_t ipq_pbe0                     : 1;
	uint64_t ipq_pbe1                     : 1;
	uint64_t pwq_pow                      : 1;
	uint64_t pwq_wp1                      : 1;
	uint64_t pwq_wqed                     : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} cn30xx;
	struct cvmx_ipd_bist_status_cn30xx    cn31xx;
	struct cvmx_ipd_bist_status_cn30xx    cn38xx;
	struct cvmx_ipd_bist_status_cn30xx    cn38xxp2;
	struct cvmx_ipd_bist_status_cn30xx    cn50xx;
	struct cvmx_ipd_bist_status_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t csr_mem                      : 1;  /**< CSR Register Memory Bist Status. */
	uint64_t csr_ncmd                     : 1;  /**< CSR NCB Commands Memory Bist Status. */
	uint64_t pwq_wqed                     : 1;  /**< PWQ PIP WQE DONE Memory Bist Status. */
	uint64_t pwq_wp1                      : 1;  /**< PWQ WQE PAGE1 PTR Memory Bist Status. */
	uint64_t pwq_pow                      : 1;  /**< PWQ POW MEM Memory Bist Status. */
	uint64_t ipq_pbe1                     : 1;  /**< IPQ PBE1 Memory Bist Status. */
	uint64_t ipq_pbe0                     : 1;  /**< IPQ PBE0 Memory Bist Status. */
	uint64_t pbm3                         : 1;  /**< PBM3 Memory Bist Status. */
	uint64_t pbm2                         : 1;  /**< PBM2 Memory Bist Status. */
	uint64_t pbm1                         : 1;  /**< PBM1 Memory Bist Status. */
	uint64_t pbm0                         : 1;  /**< PBM0 Memory Bist Status. */
	uint64_t pbm_word                     : 1;  /**< PBM_WORD Memory Bist Status. */
	uint64_t pwq1                         : 1;  /**< PWQ1 Memory Bist Status. */
	uint64_t pwq0                         : 1;  /**< PWQ0 Memory Bist Status. */
	uint64_t prc_off                      : 1;  /**< PRC_OFF Memory Bist Status. */
	uint64_t ipd_old                      : 1;  /**< IPD_OLD Memory Bist Status. */
	uint64_t ipd_new                      : 1;  /**< IPD_NEW Memory Bist Status. */
	uint64_t pwp                          : 1;  /**< PWP Memory Bist Status. */
#else
	uint64_t pwp                          : 1;
	uint64_t ipd_new                      : 1;
	uint64_t ipd_old                      : 1;
	uint64_t prc_off                      : 1;
	uint64_t pwq0                         : 1;
	uint64_t pwq1                         : 1;
	uint64_t pbm_word                     : 1;
	uint64_t pbm0                         : 1;
	uint64_t pbm1                         : 1;
	uint64_t pbm2                         : 1;
	uint64_t pbm3                         : 1;
	uint64_t ipq_pbe0                     : 1;
	uint64_t ipq_pbe1                     : 1;
	uint64_t pwq_pow                      : 1;
	uint64_t pwq_wp1                      : 1;
	uint64_t pwq_wqed                     : 1;
	uint64_t csr_ncmd                     : 1;
	uint64_t csr_mem                      : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} cn52xx;
	struct cvmx_ipd_bist_status_cn52xx    cn52xxp1;
	struct cvmx_ipd_bist_status_cn52xx    cn56xx;
	struct cvmx_ipd_bist_status_cn52xx    cn56xxp1;
	struct cvmx_ipd_bist_status_cn30xx    cn58xx;
	struct cvmx_ipd_bist_status_cn30xx    cn58xxp1;
	struct cvmx_ipd_bist_status_cn52xx    cn61xx;
	struct cvmx_ipd_bist_status_cn52xx    cn63xx;
	struct cvmx_ipd_bist_status_cn52xx    cn63xxp1;
	struct cvmx_ipd_bist_status_cn52xx    cn66xx;
	struct cvmx_ipd_bist_status_s         cn68xx;
	struct cvmx_ipd_bist_status_s         cn68xxp1;
	struct cvmx_ipd_bist_status_cn52xx    cnf71xx;
};
typedef union cvmx_ipd_bist_status cvmx_ipd_bist_status_t;

/**
 * cvmx_ipd_bp_prt_red_end
 *
 * IPD_BP_PRT_RED_END = IPD Backpressure Port RED Enable
 *
 * When IPD applies backpressure to a PORT and the corresponding bit in this register is set,
 * the RED Unit will drop packets for that port.
 */
union cvmx_ipd_bp_prt_red_end {
	uint64_t u64;
	struct cvmx_ipd_bp_prt_red_end_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t prt_enb                      : 48; /**< The port corresponding to the bit position in this
                                                         field will drop all NON-RAW packets to that port
                                                         when port level backpressure is applied to that
                                                         port.  The applying of port-level backpressure for
                                                         this dropping does not take into consideration the
                                                         value of IPD_PORTX_BP_PAGE_CNT[BP_ENB], nor
                                                         IPD_RED_PORT_ENABLE[PRT_ENB]. */
#else
	uint64_t prt_enb                      : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_ipd_bp_prt_red_end_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t prt_enb                      : 36; /**< The port corresponding to the bit position in this
                                                         field, will allow RED to drop back when port level
                                                         backpressure is applied to the port. The applying
                                                         of port-level backpressure for this RED dropping
                                                         does not take into consideration the value of
                                                         IPD_PORTX_BP_PAGE_CNT[BP_ENB]. */
#else
	uint64_t prt_enb                      : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} cn30xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn31xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn38xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn38xxp2;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn50xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t prt_enb                      : 40; /**< The port corresponding to the bit position in this
                                                         field, will allow RED to drop back when port level
                                                         backpressure is applied to the port. The applying
                                                         of port-level backpressure for this RED dropping
                                                         does not take into consideration the value of
                                                         IPD_PORTX_BP_PAGE_CNT[BP_ENB]. */
#else
	uint64_t prt_enb                      : 40;
	uint64_t reserved_40_63               : 24;
#endif
	} cn52xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn52xxp1;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn56xx;
	struct cvmx_ipd_bp_prt_red_end_cn52xx cn56xxp1;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn58xx;
	struct cvmx_ipd_bp_prt_red_end_cn30xx cn58xxp1;
	struct cvmx_ipd_bp_prt_red_end_s      cn61xx;
	struct cvmx_ipd_bp_prt_red_end_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t prt_enb                      : 44; /**< The port corresponding to the bit position in this
                                                         field will drop all NON-RAW packets to that port
                                                         when port level backpressure is applied to that
                                                         port.  The applying of port-level backpressure for
                                                         this dropping does not take into consideration the
                                                         value of IPD_PORTX_BP_PAGE_CNT[BP_ENB], nor
                                                         IPD_RED_PORT_ENABLE[PRT_ENB]. */
#else
	uint64_t prt_enb                      : 44;
	uint64_t reserved_44_63               : 20;
#endif
	} cn63xx;
	struct cvmx_ipd_bp_prt_red_end_cn63xx cn63xxp1;
	struct cvmx_ipd_bp_prt_red_end_s      cn66xx;
	struct cvmx_ipd_bp_prt_red_end_s      cnf71xx;
};
typedef union cvmx_ipd_bp_prt_red_end cvmx_ipd_bp_prt_red_end_t;

/**
 * cvmx_ipd_bpid#_mbuf_th
 *
 * 0x2000 2FFF
 *
 *                  IPD_BPIDX_MBUF_TH = IPD BPID  MBUFF Threshold
 *
 * The number of MBUFFs in use by the BPID, that when exceeded, backpressure will be applied to the BPID.
 */
union cvmx_ipd_bpidx_mbuf_th {
	uint64_t u64;
	struct cvmx_ipd_bpidx_mbuf_th_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bp_enb                       : 1;  /**< When set '1' BP will be applied, if '0' BP will
                                                         not be applied to bpid. */
	uint64_t page_cnt                     : 17; /**< The number of page pointers assigned to
                                                         the BPID, that when exceeded will cause
                                                         back-pressure to be applied to the BPID.
                                                         This value is in 256 page-pointer increments,
                                                         (i.e. 0 = 0-page-ptrs, 1 = 256-page-ptrs,..) */
#else
	uint64_t page_cnt                     : 17;
	uint64_t bp_enb                       : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ipd_bpidx_mbuf_th_s       cn68xx;
	struct cvmx_ipd_bpidx_mbuf_th_s       cn68xxp1;
};
typedef union cvmx_ipd_bpidx_mbuf_th cvmx_ipd_bpidx_mbuf_th_t;

/**
 * cvmx_ipd_bpid_bp_counter#
 *
 * RESERVE SPACE UPTO 0x2FFF
 *
 * 0x3000 0x3ffff
 *
 * IPD_BPID_BP_COUNTERX = MBUF BPID Counters used to generate Back Pressure Per BPID.
 */
union cvmx_ipd_bpid_bp_counterx {
	uint64_t u64;
	struct cvmx_ipd_bpid_bp_counterx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t cnt_val                      : 25; /**< Number of MBUFs being used by data on this BPID. */
#else
	uint64_t cnt_val                      : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_ipd_bpid_bp_counterx_s    cn68xx;
	struct cvmx_ipd_bpid_bp_counterx_s    cn68xxp1;
};
typedef union cvmx_ipd_bpid_bp_counterx cvmx_ipd_bpid_bp_counterx_t;

/**
 * cvmx_ipd_clk_count
 *
 * IPD_CLK_COUNT = IPD Clock Count
 *
 * Counts the number of core clocks periods since the de-asserition of reset.
 */
union cvmx_ipd_clk_count {
	uint64_t u64;
	struct cvmx_ipd_clk_count_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t clk_cnt                      : 64; /**< This counter will be zeroed when reset is applied
                                                         and will increment every rising edge of the
                                                         core-clock. */
#else
	uint64_t clk_cnt                      : 64;
#endif
	} s;
	struct cvmx_ipd_clk_count_s           cn30xx;
	struct cvmx_ipd_clk_count_s           cn31xx;
	struct cvmx_ipd_clk_count_s           cn38xx;
	struct cvmx_ipd_clk_count_s           cn38xxp2;
	struct cvmx_ipd_clk_count_s           cn50xx;
	struct cvmx_ipd_clk_count_s           cn52xx;
	struct cvmx_ipd_clk_count_s           cn52xxp1;
	struct cvmx_ipd_clk_count_s           cn56xx;
	struct cvmx_ipd_clk_count_s           cn56xxp1;
	struct cvmx_ipd_clk_count_s           cn58xx;
	struct cvmx_ipd_clk_count_s           cn58xxp1;
	struct cvmx_ipd_clk_count_s           cn61xx;
	struct cvmx_ipd_clk_count_s           cn63xx;
	struct cvmx_ipd_clk_count_s           cn63xxp1;
	struct cvmx_ipd_clk_count_s           cn66xx;
	struct cvmx_ipd_clk_count_s           cn68xx;
	struct cvmx_ipd_clk_count_s           cn68xxp1;
	struct cvmx_ipd_clk_count_s           cnf71xx;
};
typedef union cvmx_ipd_clk_count cvmx_ipd_clk_count_t;

/**
 * cvmx_ipd_credits
 *
 * IPD_CREDITS = IPD Credits
 *
 * The credits allowed for IPD.
 */
union cvmx_ipd_credits {
	uint64_t u64;
	struct cvmx_ipd_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t iob_wrc                      : 8;  /**< The present number of credits available for
                                                         stores to the IOB. */
	uint64_t iob_wr                       : 8;  /**< The number of command credits the IPD has to send
                                                         stores to the IOB. Legal values for this field
                                                         are 1-8 (a value of 0 will be treated as a 1 and
                                                         a value greater than 8 will be treated as an 8. */
#else
	uint64_t iob_wr                       : 8;
	uint64_t iob_wrc                      : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ipd_credits_s             cn68xx;
	struct cvmx_ipd_credits_s             cn68xxp1;
};
typedef union cvmx_ipd_credits cvmx_ipd_credits_t;

/**
 * cvmx_ipd_ctl_status
 *
 * IPD_CTL_STATUS = IPD's Control Status Register
 *
 * The number of words in a MBUFF used for packet data store.
 */
union cvmx_ipd_ctl_status {
	uint64_t u64;
	struct cvmx_ipd_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t use_sop                      : 1;  /**< When '1' the SOP sent by the MAC will be used in
                                                         place of the SOP generated by the IPD. */
	uint64_t rst_done                     : 1;  /**< When '0' IPD has finished reset. No access
                                                         except the reading of this bit should occur to the
                                                         IPD until this is asserted. Or a 1000 core clock
                                                         cycles has passed after the de-assertion of reset. */
	uint64_t clken                        : 1;  /**< Controls the conditional clocking within IPD
                                                         0=Allow HW to control the clocks
                                                         1=Force the clocks to be always on */
	uint64_t no_wptr                      : 1;  /**< When set '1' the WQE pointers will not be used and
                                                         the WQE will be located at the front of the packet.
                                                         When set:
                                                           - IPD_WQE_FPA_QUEUE[WQE_QUE] is not used
                                                           - IPD_1ST_MBUFF_SKIP[SKIP_SZ] must be at least 16
                                                           - If 16 <= IPD_1ST_MBUFF_SKIP[SKIP_SZ] <= 31 then
                                                             the WQE will be written into the first 128B
                                                             cache block in the first buffer that contains
                                                             the packet.
                                                           - If IPD_1ST_MBUFF_SKIP[SKIP_SZ] == 32 then
                                                             the WQE will be written into the second 128B
                                                             cache block in the first buffer that contains
                                                             the packet. */
	uint64_t pq_apkt                      : 1;  /**< When set IPD_PORT_QOS_X_CNT WILL be incremented
                                                         by one for every work queue entry that is sent to
                                                         POW. */
	uint64_t pq_nabuf                     : 1;  /**< When set IPD_PORT_QOS_X_CNT WILL NOT be
                                                         incremented when IPD allocates a buffer for a
                                                         packet. */
	uint64_t ipd_full                     : 1;  /**< When clear '0' the IPD acts normaly.
                                                         When set '1' the IPD drive the IPD_BUFF_FULL line to
                                                         the IOB-arbiter, telling it to not give grants to
                                                         NCB devices sending packet data. */
	uint64_t pkt_off                      : 1;  /**< When clear '0' the IPD working normaly,
                                                         buffering the received packet data. When set '1'
                                                         the IPD will not buffer the received packet data. */
	uint64_t len_m8                       : 1;  /**< Setting of this bit will subtract 8 from the
                                                         data-length field in the header written to the
                                                         POW and the top of a MBUFF.
                                                         OCTEAN generates a length that includes the
                                                         length of the data + 8 for the header-field. By
                                                         setting this bit the 8 for the instr-field will
                                                         not be included in the length field of the header.
                                                         NOTE: IPD is compliant with the spec when this
                                                         field is '1'. */
	uint64_t reset                        : 1;  /**< When set '1' causes a reset of the IPD, except
                                                         RSL. */
	uint64_t addpkt                       : 1;  /**< When IPD_CTL_STATUS[ADDPKT] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL],
                                                         IPD_PORT_BP_COUNTERS2_PAIR(port)[CNT_VAL] and
                                                         IPD_PORT_BP_COUNTERS3_PAIR(port)[CNT_VAL]
                                                         WILL be incremented by one for every work
                                                         queue entry that is sent to POW. */
	uint64_t naddbuf                      : 1;  /**< When IPD_CTL_STATUS[NADDBUF] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL],
                                                         IPD_PORT_BP_COUNTERS2_PAIR(port)[CNT_VAL] and
                                                         IPD_PORT_BP_COUNTERS3_PAIR(port)[CNT_VAL]
                                                         WILL NOT be incremented when IPD allocates a
                                                         buffer for a packet on the port. */
	uint64_t pkt_lend                     : 1;  /**< Changes PKT to little endian writes to L2C */
	uint64_t wqe_lend                     : 1;  /**< Changes WQE to little endian writes to L2C */
	uint64_t pbp_en                       : 1;  /**< Port back pressure enable. When set '1' enables
                                                         the sending of port level backpressure to the
                                                         Octane input-ports. The application should NOT
                                                         de-assert this bit after asserting it. The
                                                         receivers of this bit may have been put into
                                                         backpressure mode and can only be released by
                                                         IPD informing them that the backpressure has
                                                         been released.
                                                         GMXX_INF_MODE[EN] must be set to '1' for each
                                                         packet interface which requires port back pressure
                                                         prior to setting PBP_EN to '1'. */
	cvmx_ipd_mode_t opc_mode              : 2;  /**< 0 ==> All packet data (and next buffer pointers)
                                                         is written through to memory.
                                                         1 ==> All packet data (and next buffer pointers) is
                                                         written into the cache.
                                                         2 ==> The first aligned cache block holding the
                                                         packet data (and initial next buffer pointer) is
                                                         written to the L2 cache, all remaining cache blocks
                                                         are not written to the L2 cache.
                                                         3 ==> The first two aligned cache blocks holding
                                                         the packet data (and initial next buffer pointer)
                                                         are written to the L2 cache, all remaining cache
                                                         blocks are not written to the L2 cache. */
	uint64_t ipd_en                       : 1;  /**< When set '1' enable the operation of the IPD.
                                                         When clear '0', the IPD will appear to the
                                                         IOB-arbiter to be applying backpressure, this
                                                         causes the IOB-Arbiter to not send grants to NCB
                                                         devices requesting to send packet data to the IPD. */
#else
	uint64_t ipd_en                       : 1;
	cvmx_ipd_mode_t opc_mode              : 2;
	uint64_t pbp_en                       : 1;
	uint64_t wqe_lend                     : 1;
	uint64_t pkt_lend                     : 1;
	uint64_t naddbuf                      : 1;
	uint64_t addpkt                       : 1;
	uint64_t reset                        : 1;
	uint64_t len_m8                       : 1;
	uint64_t pkt_off                      : 1;
	uint64_t ipd_full                     : 1;
	uint64_t pq_nabuf                     : 1;
	uint64_t pq_apkt                      : 1;
	uint64_t no_wptr                      : 1;
	uint64_t clken                        : 1;
	uint64_t rst_done                     : 1;
	uint64_t use_sop                      : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ipd_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t len_m8                       : 1;  /**< Setting of this bit will subtract 8 from the
                                                         data-length field in the header written wo the
                                                         POW and the top of a MBUFF.
                                                         OCTEAN generates a length that includes the
                                                         length of the data + 8 for the header-field. By
                                                         setting this bit the 8 for the instr-field will
                                                         not be included in the length field of the header.
                                                         NOTE: IPD is compliant with the spec when this
                                                         field is '1'. */
	uint64_t reset                        : 1;  /**< When set '1' causes a reset of the IPD, except
                                                         RSL. */
	uint64_t addpkt                       : 1;  /**< When IPD_CTL_STATUS[ADDPKT] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL be incremented by one for every work
                                                         queue entry that is sent to POW. */
	uint64_t naddbuf                      : 1;  /**< When IPD_CTL_STATUS[NADDBUF] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL NOT be incremented when IPD allocates a
                                                         buffer for a packet on the port. */
	uint64_t pkt_lend                     : 1;  /**< Changes PKT to little endian writes to L2C */
	uint64_t wqe_lend                     : 1;  /**< Changes WQE to little endian writes to L2C */
	uint64_t pbp_en                       : 1;  /**< Port back pressure enable. When set '1' enables
                                                         the sending of port level backpressure to the
                                                         Octane input-ports. Once enabled the sending of
                                                         port-level-backpressure can not be disabled by
                                                         changing the value of this bit.
                                                         GMXX_INF_MODE[EN] must be set to '1' for each
                                                         packet interface which requires port back pressure
                                                         prior to setting PBP_EN to '1'. */
	cvmx_ipd_mode_t opc_mode              : 2;  /**< 0 ==> All packet data (and next buffer pointers)
                                                         is written through to memory.
                                                         1 ==> All packet data (and next buffer pointers) is
                                                         written into the cache.
                                                         2 ==> The first aligned cache block holding the
                                                         packet data (and initial next buffer pointer) is
                                                         written to the L2 cache, all remaining cache blocks
                                                         are not written to the L2 cache.
                                                         3 ==> The first two aligned cache blocks holding
                                                         the packet data (and initial next buffer pointer)
                                                         are written to the L2 cache, all remaining cache
                                                         blocks are not written to the L2 cache. */
	uint64_t ipd_en                       : 1;  /**< When set '1' enable the operation of the IPD. */
#else
	uint64_t ipd_en                       : 1;
	cvmx_ipd_mode_t opc_mode              : 2;
	uint64_t pbp_en                       : 1;
	uint64_t wqe_lend                     : 1;
	uint64_t pkt_lend                     : 1;
	uint64_t naddbuf                      : 1;
	uint64_t addpkt                       : 1;
	uint64_t reset                        : 1;
	uint64_t len_m8                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn30xx;
	struct cvmx_ipd_ctl_status_cn30xx     cn31xx;
	struct cvmx_ipd_ctl_status_cn30xx     cn38xx;
	struct cvmx_ipd_ctl_status_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t reset                        : 1;  /**< When set '1' causes a reset of the IPD, except
                                                         RSL. */
	uint64_t addpkt                       : 1;  /**< When IPD_CTL_STATUS[ADDPKT] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL be incremented by one for every work
                                                         queue entry that is sent to POW.
                                                         PASS-2 Field. */
	uint64_t naddbuf                      : 1;  /**< When IPD_CTL_STATUS[NADDBUF] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL NOT be incremented when IPD allocates a
                                                         buffer for a packet on the port.
                                                         PASS-2 Field. */
	uint64_t pkt_lend                     : 1;  /**< Changes PKT to little endian writes to L2C */
	uint64_t wqe_lend                     : 1;  /**< Changes WQE to little endian writes to L2C */
	uint64_t pbp_en                       : 1;  /**< Port back pressure enable. When set '1' enables
                                                         the sending of port level backpressure to the
                                                         Octane input-ports. Once enabled the sending of
                                                         port-level-backpressure can not be disabled by
                                                         changing the value of this bit. */
	cvmx_ipd_mode_t opc_mode              : 2;  /**< 0 ==> All packet data (and next buffer pointers)
                                                         is written through to memory.
                                                         1 ==> All packet data (and next buffer pointers) is
                                                         written into the cache.
                                                         2 ==> The first aligned cache block holding the
                                                         packet data (and initial next buffer pointer) is
                                                         written to the L2 cache, all remaining cache blocks
                                                         are not written to the L2 cache.
                                                         3 ==> The first two aligned cache blocks holding
                                                         the packet data (and initial next buffer pointer)
                                                         are written to the L2 cache, all remaining cache
                                                         blocks are not written to the L2 cache. */
	uint64_t ipd_en                       : 1;  /**< When set '1' enable the operation of the IPD. */
#else
	uint64_t ipd_en                       : 1;
	cvmx_ipd_mode_t opc_mode              : 2;
	uint64_t pbp_en                       : 1;
	uint64_t wqe_lend                     : 1;
	uint64_t pkt_lend                     : 1;
	uint64_t naddbuf                      : 1;
	uint64_t addpkt                       : 1;
	uint64_t reset                        : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn38xxp2;
	struct cvmx_ipd_ctl_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t no_wptr                      : 1;  /**< When set '1' the WQE pointers will not be used and
                                                         the WQE will be located at the front of the packet. */
	uint64_t pq_apkt                      : 1;  /**< Reserved. */
	uint64_t pq_nabuf                     : 1;  /**< Reserved. */
	uint64_t ipd_full                     : 1;  /**< When clear '0' the IPD acts normaly.
                                                         When set '1' the IPD drive the IPD_BUFF_FULL line to
                                                         the IOB-arbiter, telling it to not give grants to
                                                         NCB devices sending packet data. */
	uint64_t pkt_off                      : 1;  /**< When clear '0' the IPD working normaly,
                                                         buffering the received packet data. When set '1'
                                                         the IPD will not buffer the received packet data. */
	uint64_t len_m8                       : 1;  /**< Setting of this bit will subtract 8 from the
                                                         data-length field in the header written wo the
                                                         POW and the top of a MBUFF.
                                                         OCTEAN generates a length that includes the
                                                         length of the data + 8 for the header-field. By
                                                         setting this bit the 8 for the instr-field will
                                                         not be included in the length field of the header.
                                                         NOTE: IPD is compliant with the spec when this
                                                         field is '1'. */
	uint64_t reset                        : 1;  /**< When set '1' causes a reset of the IPD, except
                                                         RSL. */
	uint64_t addpkt                       : 1;  /**< When IPD_CTL_STATUS[ADDPKT] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL be incremented by one for every work
                                                         queue entry that is sent to POW. */
	uint64_t naddbuf                      : 1;  /**< When IPD_CTL_STATUS[NADDBUF] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL NOT be incremented when IPD allocates a
                                                         buffer for a packet on the port. */
	uint64_t pkt_lend                     : 1;  /**< Changes PKT to little endian writes to L2C */
	uint64_t wqe_lend                     : 1;  /**< Changes WQE to little endian writes to L2C */
	uint64_t pbp_en                       : 1;  /**< Port back pressure enable. When set '1' enables
                                                         the sending of port level backpressure to the
                                                         Octane input-ports. Once enabled the sending of
                                                         port-level-backpressure can not be disabled by
                                                         changing the value of this bit.
                                                         GMXX_INF_MODE[EN] must be set to '1' for each
                                                         packet interface which requires port back pressure
                                                         prior to setting PBP_EN to '1'. */
	cvmx_ipd_mode_t opc_mode              : 2;  /**< 0 ==> All packet data (and next buffer pointers)
                                                         is written through to memory.
                                                         1 ==> All packet data (and next buffer pointers) is
                                                         written into the cache.
                                                         2 ==> The first aligned cache block holding the
                                                         packet data (and initial next buffer pointer) is
                                                         written to the L2 cache, all remaining cache blocks
                                                         are not written to the L2 cache.
                                                         3 ==> The first two aligned cache blocks holding
                                                         the packet data (and initial next buffer pointer)
                                                         are written to the L2 cache, all remaining cache
                                                         blocks are not written to the L2 cache. */
	uint64_t ipd_en                       : 1;  /**< When set '1' enable the operation of the IPD.
                                                         When clear '0', the IPD will appear to the
                                                         IOB-arbiter to be applying backpressure, this
                                                         causes the IOB-Arbiter to not send grants to NCB
                                                         devices requesting to send packet data to the IPD. */
#else
	uint64_t ipd_en                       : 1;
	cvmx_ipd_mode_t opc_mode              : 2;
	uint64_t pbp_en                       : 1;
	uint64_t wqe_lend                     : 1;
	uint64_t pkt_lend                     : 1;
	uint64_t naddbuf                      : 1;
	uint64_t addpkt                       : 1;
	uint64_t reset                        : 1;
	uint64_t len_m8                       : 1;
	uint64_t pkt_off                      : 1;
	uint64_t ipd_full                     : 1;
	uint64_t pq_nabuf                     : 1;
	uint64_t pq_apkt                      : 1;
	uint64_t no_wptr                      : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} cn50xx;
	struct cvmx_ipd_ctl_status_cn50xx     cn52xx;
	struct cvmx_ipd_ctl_status_cn50xx     cn52xxp1;
	struct cvmx_ipd_ctl_status_cn50xx     cn56xx;
	struct cvmx_ipd_ctl_status_cn50xx     cn56xxp1;
	struct cvmx_ipd_ctl_status_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t ipd_full                     : 1;  /**< When clear '0' the IPD acts normaly.
                                                         When set '1' the IPD drive the IPD_BUFF_FULL line to
                                                         the IOB-arbiter, telling it to not give grants to
                                                         NCB devices sending packet data. */
	uint64_t pkt_off                      : 1;  /**< When clear '0' the IPD working normaly,
                                                         buffering the received packet data. When set '1'
                                                         the IPD will not buffer the received packet data. */
	uint64_t len_m8                       : 1;  /**< Setting of this bit will subtract 8 from the
                                                         data-length field in the header written wo the
                                                         POW and the top of a MBUFF.
                                                         OCTEAN PASS2 generates a length that includes the
                                                         length of the data + 8 for the header-field. By
                                                         setting this bit the 8 for the instr-field will
                                                         not be included in the length field of the header.
                                                         NOTE: IPD is compliant with the spec when this
                                                         field is '1'. */
	uint64_t reset                        : 1;  /**< When set '1' causes a reset of the IPD, except
                                                         RSL. */
	uint64_t addpkt                       : 1;  /**< When IPD_CTL_STATUS[ADDPKT] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL be incremented by one for every work
                                                         queue entry that is sent to POW.
                                                         PASS-2 Field. */
	uint64_t naddbuf                      : 1;  /**< When IPD_CTL_STATUS[NADDBUF] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL]
                                                         WILL NOT be incremented when IPD allocates a
                                                         buffer for a packet on the port.
                                                         PASS-2 Field. */
	uint64_t pkt_lend                     : 1;  /**< Changes PKT to little endian writes to L2C */
	uint64_t wqe_lend                     : 1;  /**< Changes WQE to little endian writes to L2C */
	uint64_t pbp_en                       : 1;  /**< Port back pressure enable. When set '1' enables
                                                         the sending of port level backpressure to the
                                                         Octane input-ports. Once enabled the sending of
                                                         port-level-backpressure can not be disabled by
                                                         changing the value of this bit. */
	cvmx_ipd_mode_t opc_mode              : 2;  /**< 0 ==> All packet data (and next buffer pointers)
                                                         is written through to memory.
                                                         1 ==> All packet data (and next buffer pointers) is
                                                         written into the cache.
                                                         2 ==> The first aligned cache block holding the
                                                         packet data (and initial next buffer pointer) is
                                                         written to the L2 cache, all remaining cache blocks
                                                         are not written to the L2 cache.
                                                         3 ==> The first two aligned cache blocks holding
                                                         the packet data (and initial next buffer pointer)
                                                         are written to the L2 cache, all remaining cache
                                                         blocks are not written to the L2 cache. */
	uint64_t ipd_en                       : 1;  /**< When set '1' enable the operation of the IPD.
                                                         When clear '0', the IPD will appear to the
                                                         IOB-arbiter to be applying backpressure, this
                                                         causes the IOB-Arbiter to not send grants to NCB
                                                         devices requesting to send packet data to the IPD. */
#else
	uint64_t ipd_en                       : 1;
	cvmx_ipd_mode_t opc_mode              : 2;
	uint64_t pbp_en                       : 1;
	uint64_t wqe_lend                     : 1;
	uint64_t pkt_lend                     : 1;
	uint64_t naddbuf                      : 1;
	uint64_t addpkt                       : 1;
	uint64_t reset                        : 1;
	uint64_t len_m8                       : 1;
	uint64_t pkt_off                      : 1;
	uint64_t ipd_full                     : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn58xx;
	struct cvmx_ipd_ctl_status_cn58xx     cn58xxp1;
	struct cvmx_ipd_ctl_status_s          cn61xx;
	struct cvmx_ipd_ctl_status_s          cn63xx;
	struct cvmx_ipd_ctl_status_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t clken                        : 1;  /**< Controls the conditional clocking within IPD
                                                         0=Allow HW to control the clocks
                                                         1=Force the clocks to be always on */
	uint64_t no_wptr                      : 1;  /**< When set '1' the WQE pointers will not be used and
                                                         the WQE will be located at the front of the packet.
                                                         When set:
                                                           - IPD_WQE_FPA_QUEUE[WQE_QUE] is not used
                                                           - IPD_1ST_MBUFF_SKIP[SKIP_SZ] must be at least 16
                                                           - If 16 <= IPD_1ST_MBUFF_SKIP[SKIP_SZ] <= 31 then
                                                             the WQE will be written into the first 128B
                                                             cache block in the first buffer that contains
                                                             the packet.
                                                           - If IPD_1ST_MBUFF_SKIP[SKIP_SZ] == 32 then
                                                             the WQE will be written into the second 128B
                                                             cache block in the first buffer that contains
                                                             the packet. */
	uint64_t pq_apkt                      : 1;  /**< When set IPD_PORT_QOS_X_CNT WILL be incremented
                                                         by one for every work queue entry that is sent to
                                                         POW. */
	uint64_t pq_nabuf                     : 1;  /**< When set IPD_PORT_QOS_X_CNT WILL NOT be
                                                         incremented when IPD allocates a buffer for a
                                                         packet. */
	uint64_t ipd_full                     : 1;  /**< When clear '0' the IPD acts normaly.
                                                         When set '1' the IPD drive the IPD_BUFF_FULL line to
                                                         the IOB-arbiter, telling it to not give grants to
                                                         NCB devices sending packet data. */
	uint64_t pkt_off                      : 1;  /**< When clear '0' the IPD working normaly,
                                                         buffering the received packet data. When set '1'
                                                         the IPD will not buffer the received packet data. */
	uint64_t len_m8                       : 1;  /**< Setting of this bit will subtract 8 from the
                                                         data-length field in the header written to the
                                                         POW and the top of a MBUFF.
                                                         OCTEAN generates a length that includes the
                                                         length of the data + 8 for the header-field. By
                                                         setting this bit the 8 for the instr-field will
                                                         not be included in the length field of the header.
                                                         NOTE: IPD is compliant with the spec when this
                                                         field is '1'. */
	uint64_t reset                        : 1;  /**< When set '1' causes a reset of the IPD, except
                                                         RSL. */
	uint64_t addpkt                       : 1;  /**< When IPD_CTL_STATUS[ADDPKT] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL],
                                                         IPD_PORT_BP_COUNTERS2_PAIR(port)[CNT_VAL] and
                                                         IPD_PORT_BP_COUNTERS3_PAIR(port)[CNT_VAL]
                                                         WILL be incremented by one for every work
                                                         queue entry that is sent to POW. */
	uint64_t naddbuf                      : 1;  /**< When IPD_CTL_STATUS[NADDBUF] is set,
                                                         IPD_PORT_BP_COUNTERS_PAIR(port)[CNT_VAL],
                                                         IPD_PORT_BP_COUNTERS2_PAIR(port)[CNT_VAL] and
                                                         IPD_PORT_BP_COUNTERS3_PAIR(port)[CNT_VAL]
                                                         WILL NOT be incremented when IPD allocates a
                                                         buffer for a packet on the port. */
	uint64_t pkt_lend                     : 1;  /**< Changes PKT to little endian writes to L2C */
	uint64_t wqe_lend                     : 1;  /**< Changes WQE to little endian writes to L2C */
	uint64_t pbp_en                       : 1;  /**< Port back pressure enable. When set '1' enables
                                                         the sending of port level backpressure to the
                                                         Octane input-ports. The application should NOT
                                                         de-assert this bit after asserting it. The
                                                         receivers of this bit may have been put into
                                                         backpressure mode and can only be released by
                                                         IPD informing them that the backpressure has
                                                         been released.
                                                         GMXX_INF_MODE[EN] must be set to '1' for each
                                                         packet interface which requires port back pressure
                                                         prior to setting PBP_EN to '1'. */
	cvmx_ipd_mode_t opc_mode              : 2;  /**< 0 ==> All packet data (and next buffer pointers)
                                                         is written through to memory.
                                                         1 ==> All packet data (and next buffer pointers) is
                                                         written into the cache.
                                                         2 ==> The first aligned cache block holding the
                                                         packet data (and initial next buffer pointer) is
                                                         written to the L2 cache, all remaining cache blocks
                                                         are not written to the L2 cache.
                                                         3 ==> The first two aligned cache blocks holding
                                                         the packet data (and initial next buffer pointer)
                                                         are written to the L2 cache, all remaining cache
                                                         blocks are not written to the L2 cache. */
	uint64_t ipd_en                       : 1;  /**< When set '1' enable the operation of the IPD.
                                                         When clear '0', the IPD will appear to the
                                                         IOB-arbiter to be applying backpressure, this
                                                         causes the IOB-Arbiter to not send grants to NCB
                                                         devices requesting to send packet data to the IPD. */
#else
	uint64_t ipd_en                       : 1;
	cvmx_ipd_mode_t opc_mode              : 2;
	uint64_t pbp_en                       : 1;
	uint64_t wqe_lend                     : 1;
	uint64_t pkt_lend                     : 1;
	uint64_t naddbuf                      : 1;
	uint64_t addpkt                       : 1;
	uint64_t reset                        : 1;
	uint64_t len_m8                       : 1;
	uint64_t pkt_off                      : 1;
	uint64_t ipd_full                     : 1;
	uint64_t pq_nabuf                     : 1;
	uint64_t pq_apkt                      : 1;
	uint64_t no_wptr                      : 1;
	uint64_t clken                        : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} cn63xxp1;
	struct cvmx_ipd_ctl_status_s          cn66xx;
	struct cvmx_ipd_ctl_status_s          cn68xx;
	struct cvmx_ipd_ctl_status_s          cn68xxp1;
	struct cvmx_ipd_ctl_status_s          cnf71xx;
};
typedef union cvmx_ipd_ctl_status cvmx_ipd_ctl_status_t;

/**
 * cvmx_ipd_ecc_ctl
 *
 * IPD_ECC_CTL = IPD ECC Control
 *
 * Allows inserting ECC errors for testing.
 */
union cvmx_ipd_ecc_ctl {
	uint64_t u64;
	struct cvmx_ipd_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pm3_syn                      : 2;  /**< Flip the syndrom to generate 1-bit/2-bits error
                                                         for testing of Packet Memory 3.
                                                          2'b00       : No Error Generation
                                                          2'b10, 2'b01: Flip 1 bit
                                                          2'b11       : Flip 2 bits */
	uint64_t pm2_syn                      : 2;  /**< Flip the syndrom to generate 1-bit/2-bits error
                                                         for testing of Packet Memory 2.
                                                          2'b00       : No Error Generation
                                                          2'b10, 2'b01: Flip 1 bit
                                                          2'b11       : Flip 2 bits */
	uint64_t pm1_syn                      : 2;  /**< Flip the syndrom to generate 1-bit/2-bits error
                                                         for testing of Packet Memory 1.
                                                          2'b00       : No Error Generation
                                                          2'b10, 2'b01: Flip 1 bit
                                                          2'b11       : Flip 2 bits */
	uint64_t pm0_syn                      : 2;  /**< Flip the syndrom to generate 1-bit/2-bits error
                                                         for testing of Packet Memory 0.
                                                          2'b00       : No Error Generation
                                                          2'b10, 2'b01: Flip 1 bit
                                                          2'b11       : Flip 2 bits */
#else
	uint64_t pm0_syn                      : 2;
	uint64_t pm1_syn                      : 2;
	uint64_t pm2_syn                      : 2;
	uint64_t pm3_syn                      : 2;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_ipd_ecc_ctl_s             cn68xx;
	struct cvmx_ipd_ecc_ctl_s             cn68xxp1;
};
typedef union cvmx_ipd_ecc_ctl cvmx_ipd_ecc_ctl_t;

/**
 * cvmx_ipd_free_ptr_fifo_ctl
 *
 * IPD_FREE_PTR_FIFO_CTL = IPD's FREE Pointer FIFO Control
 *
 * Allows reading of the Page-Pointers stored in the IPD's FREE Fifo.
 * See also the IPD_FREE_PTR_VALUE
 */
union cvmx_ipd_free_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_free_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t max_cnts                     : 7;  /**< Maximum number of Packet-Pointers or WQE-Pointers
                                                         that COULD be in the FIFO.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' this field
                                                         only represents the Max number of Packet-Pointers,
                                                         WQE-Pointers are not used in this mode. */
	uint64_t wraddr                       : 8;  /**< Present FIFO WQE Read address. */
	uint64_t praddr                       : 8;  /**< Present FIFO Packet Read address. */
	uint64_t cena                         : 1;  /**< Active low Chip Enable to the read the
                                                         pwp_fifo. This bit also controls the MUX-select
                                                         that steers [RADDR] to the pwp_fifo.
                                                         *WARNING - Setting this field to '0' will allow
                                                         reading of the memories thorugh the PTR field,
                                                         but will cause unpredictable operation of the IPD
                                                         under normal operation. */
	uint64_t raddr                        : 8;  /**< Sets the address to read from in the pwp_fifo.
                                                         Addresses 0 through 63 contain Packet-Pointers and
                                                         addresses 64 through 127 contain WQE-Pointers.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' addresses
                                                         64 through 127 are not valid. */
#else
	uint64_t raddr                        : 8;
	uint64_t cena                         : 1;
	uint64_t praddr                       : 8;
	uint64_t wraddr                       : 8;
	uint64_t max_cnts                     : 7;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ipd_free_ptr_fifo_ctl_s   cn68xx;
	struct cvmx_ipd_free_ptr_fifo_ctl_s   cn68xxp1;
};
typedef union cvmx_ipd_free_ptr_fifo_ctl cvmx_ipd_free_ptr_fifo_ctl_t;

/**
 * cvmx_ipd_free_ptr_value
 *
 * IPD_FREE_PTR_VALUE = IPD's FREE Pointer Value
 *
 * The value of the pointer selected through the IPD_FREE_PTR_FIFO_CTL
 */
union cvmx_ipd_free_ptr_value {
	uint64_t u64;
	struct cvmx_ipd_free_ptr_value_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t ptr                          : 33; /**< The output of the pwp_fifo. */
#else
	uint64_t ptr                          : 33;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_ipd_free_ptr_value_s      cn68xx;
	struct cvmx_ipd_free_ptr_value_s      cn68xxp1;
};
typedef union cvmx_ipd_free_ptr_value cvmx_ipd_free_ptr_value_t;

/**
 * cvmx_ipd_hold_ptr_fifo_ctl
 *
 * IPD_HOLD_PTR_FIFO_CTL = IPD's Holding Pointer FIFO Control
 *
 * Allows reading of the Page-Pointers stored in the IPD's Holding Fifo.
 */
union cvmx_ipd_hold_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_hold_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_43_63               : 21;
	uint64_t ptr                          : 33; /**< The output of the holding-fifo. */
	uint64_t max_pkt                      : 3;  /**< Maximum number of Packet-Pointers that COULD be
                                                         in the FIFO. */
	uint64_t praddr                       : 3;  /**< Present Packet-Pointer read address. */
	uint64_t cena                         : 1;  /**< Active low Chip Enable that controls the
                                                         MUX-select that steers [RADDR] to the fifo.
                                                         *WARNING - Setting this field to '0' will allow
                                                         reading of the memories thorugh the PTR field,
                                                         but will cause unpredictable operation of the IPD
                                                         under normal operation. */
	uint64_t raddr                        : 3;  /**< Sets the address to read from in the holding.
                                                         fifo in the IPD. This FIFO holds Packet-Pointers
                                                         to be used for packet data storage. */
#else
	uint64_t raddr                        : 3;
	uint64_t cena                         : 1;
	uint64_t praddr                       : 3;
	uint64_t max_pkt                      : 3;
	uint64_t ptr                          : 33;
	uint64_t reserved_43_63               : 21;
#endif
	} s;
	struct cvmx_ipd_hold_ptr_fifo_ctl_s   cn68xx;
	struct cvmx_ipd_hold_ptr_fifo_ctl_s   cn68xxp1;
};
typedef union cvmx_ipd_hold_ptr_fifo_ctl cvmx_ipd_hold_ptr_fifo_ctl_t;

/**
 * cvmx_ipd_int_enb
 *
 * IPD_INTERRUPT_ENB = IPD Interrupt Enable Register
 *
 * Used to enable the various interrupting conditions of IPD
 */
union cvmx_ipd_int_enb {
	uint64_t u64;
	struct cvmx_ipd_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t pw3_dbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw3_sbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw2_dbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw2_sbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw1_dbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw1_sbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw0_dbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pw0_sbe                      : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t dat                          : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t eop                          : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t sop                          : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pq_sub                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pq_add                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t bc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t d_coll                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t c_coll                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t cc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t dc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t bp_sub                       : 1;  /**< Enables interrupts when a backpressure subtract
                                                         has an illegal value. */
	uint64_t prc_par3                     : 1;  /**< Enable parity error interrupts for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Enable parity error interrupts for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Enable parity error interrupts for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Enable parity error interrupts for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t dc_ovr                       : 1;
	uint64_t cc_ovr                       : 1;
	uint64_t c_coll                       : 1;
	uint64_t d_coll                       : 1;
	uint64_t bc_ovr                       : 1;
	uint64_t pq_add                       : 1;
	uint64_t pq_sub                       : 1;
	uint64_t sop                          : 1;
	uint64_t eop                          : 1;
	uint64_t dat                          : 1;
	uint64_t pw0_sbe                      : 1;
	uint64_t pw0_dbe                      : 1;
	uint64_t pw1_sbe                      : 1;
	uint64_t pw1_dbe                      : 1;
	uint64_t pw2_sbe                      : 1;
	uint64_t pw2_dbe                      : 1;
	uint64_t pw3_sbe                      : 1;
	uint64_t pw3_dbe                      : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_ipd_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t bp_sub                       : 1;  /**< Enables interrupts when a backpressure subtract
                                                         has an illegal value. */
	uint64_t prc_par3                     : 1;  /**< Enable parity error interrupts for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Enable parity error interrupts for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Enable parity error interrupts for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Enable parity error interrupts for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} cn30xx;
	struct cvmx_ipd_int_enb_cn30xx        cn31xx;
	struct cvmx_ipd_int_enb_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t bc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set.
                                                         This is a PASS-3 Field. */
	uint64_t d_coll                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set.
                                                         This is a PASS-3 Field. */
	uint64_t c_coll                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set.
                                                         This is a PASS-3 Field. */
	uint64_t cc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set.
                                                         This is a PASS-3 Field. */
	uint64_t dc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set.
                                                         This is a PASS-3 Field. */
	uint64_t bp_sub                       : 1;  /**< Enables interrupts when a backpressure subtract
                                                         has an illegal value. */
	uint64_t prc_par3                     : 1;  /**< Enable parity error interrupts for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Enable parity error interrupts for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Enable parity error interrupts for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Enable parity error interrupts for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t dc_ovr                       : 1;
	uint64_t cc_ovr                       : 1;
	uint64_t c_coll                       : 1;
	uint64_t d_coll                       : 1;
	uint64_t bc_ovr                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn38xx;
	struct cvmx_ipd_int_enb_cn30xx        cn38xxp2;
	struct cvmx_ipd_int_enb_cn38xx        cn50xx;
	struct cvmx_ipd_int_enb_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t pq_sub                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t pq_add                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t bc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t d_coll                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t c_coll                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t cc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t dc_ovr                       : 1;  /**< Allows an interrupt to be sent when the
                                                         corresponding bit in the IPD_INT_SUM is set. */
	uint64_t bp_sub                       : 1;  /**< Enables interrupts when a backpressure subtract
                                                         has an illegal value. */
	uint64_t prc_par3                     : 1;  /**< Enable parity error interrupts for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Enable parity error interrupts for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Enable parity error interrupts for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Enable parity error interrupts for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t dc_ovr                       : 1;
	uint64_t cc_ovr                       : 1;
	uint64_t c_coll                       : 1;
	uint64_t d_coll                       : 1;
	uint64_t bc_ovr                       : 1;
	uint64_t pq_add                       : 1;
	uint64_t pq_sub                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn52xx;
	struct cvmx_ipd_int_enb_cn52xx        cn52xxp1;
	struct cvmx_ipd_int_enb_cn52xx        cn56xx;
	struct cvmx_ipd_int_enb_cn52xx        cn56xxp1;
	struct cvmx_ipd_int_enb_cn38xx        cn58xx;
	struct cvmx_ipd_int_enb_cn38xx        cn58xxp1;
	struct cvmx_ipd_int_enb_cn52xx        cn61xx;
	struct cvmx_ipd_int_enb_cn52xx        cn63xx;
	struct cvmx_ipd_int_enb_cn52xx        cn63xxp1;
	struct cvmx_ipd_int_enb_cn52xx        cn66xx;
	struct cvmx_ipd_int_enb_s             cn68xx;
	struct cvmx_ipd_int_enb_s             cn68xxp1;
	struct cvmx_ipd_int_enb_cn52xx        cnf71xx;
};
typedef union cvmx_ipd_int_enb cvmx_ipd_int_enb_t;

/**
 * cvmx_ipd_int_sum
 *
 * IPD_INTERRUPT_SUM = IPD Interrupt Summary Register
 *
 * Set when an interrupt condition occurs, write '1' to clear.
 */
union cvmx_ipd_int_sum {
	uint64_t u64;
	struct cvmx_ipd_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t pw3_dbe                      : 1;  /**< Packet memory 3 had ECC DBE. */
	uint64_t pw3_sbe                      : 1;  /**< Packet memory 3 had ECC SBE. */
	uint64_t pw2_dbe                      : 1;  /**< Packet memory 2 had ECC DBE. */
	uint64_t pw2_sbe                      : 1;  /**< Packet memory 2 had ECC SBE. */
	uint64_t pw1_dbe                      : 1;  /**< Packet memory 1 had ECC DBE. */
	uint64_t pw1_sbe                      : 1;  /**< Packet memory 1 had ECC SBE. */
	uint64_t pw0_dbe                      : 1;  /**< Packet memory 0 had ECC DBE. */
	uint64_t pw0_sbe                      : 1;  /**< Packet memory 0 had ECC SBE. */
	uint64_t dat                          : 1;  /**< Set when a data arrives before a SOP for the same
                                                         reasm-id for a packet.
                                                         The first detected error associated with bits [14:12]
                                                         of this register will only be set here. A new bit
                                                         can be set when the previous reported bit is cleared.
                                                         Also see IPD_PKT_ERR. */
	uint64_t eop                          : 1;  /**< Set when a EOP is followed by an EOP for the same
                                                         reasm-id for a packet.
                                                         The first detected error associated with bits [14:12]
                                                         of this register will only be set here. A new bit
                                                         can be set when the previous reported bit is cleared.
                                                         Also see IPD_PKT_ERR. */
	uint64_t sop                          : 1;  /**< Set when a SOP is followed by an SOP for the same
                                                         reasm-id for a packet.
                                                         The first detected error associated with bits [14:12]
                                                         of this register will only be set here. A new bit
                                                         can be set when the previous reported bit is cleared.
                                                         Also see IPD_PKT_ERR. */
	uint64_t pq_sub                       : 1;  /**< Set when a port-qos does an sub to the count
                                                         that causes the counter to wrap. */
	uint64_t pq_add                       : 1;  /**< Set when a port-qos does an add to the count
                                                         that causes the counter to wrap. */
	uint64_t bc_ovr                       : 1;  /**< Set when the byte-count to send to IOB overflows. */
	uint64_t d_coll                       : 1;  /**< Set when the packet/WQE data to be sent to IOB
                                                         collides. */
	uint64_t c_coll                       : 1;  /**< Set when the packet/WQE commands to be sent to IOB
                                                         collides. */
	uint64_t cc_ovr                       : 1;  /**< Set when the command credits to the IOB overflow. */
	uint64_t dc_ovr                       : 1;  /**< Set when the data credits to the IOB overflow. */
	uint64_t bp_sub                       : 1;  /**< Set when a backpressure subtract is done with a
                                                         supplied illegal value. */
	uint64_t prc_par3                     : 1;  /**< Set when a parity error is dected for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Set when a parity error is dected for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Set when a parity error is dected for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Set when a parity error is dected for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t dc_ovr                       : 1;
	uint64_t cc_ovr                       : 1;
	uint64_t c_coll                       : 1;
	uint64_t d_coll                       : 1;
	uint64_t bc_ovr                       : 1;
	uint64_t pq_add                       : 1;
	uint64_t pq_sub                       : 1;
	uint64_t sop                          : 1;
	uint64_t eop                          : 1;
	uint64_t dat                          : 1;
	uint64_t pw0_sbe                      : 1;
	uint64_t pw0_dbe                      : 1;
	uint64_t pw1_sbe                      : 1;
	uint64_t pw1_dbe                      : 1;
	uint64_t pw2_sbe                      : 1;
	uint64_t pw2_dbe                      : 1;
	uint64_t pw3_sbe                      : 1;
	uint64_t pw3_dbe                      : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_ipd_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t bp_sub                       : 1;  /**< Set when a backpressure subtract is done with a
                                                         supplied illegal value. */
	uint64_t prc_par3                     : 1;  /**< Set when a parity error is dected for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Set when a parity error is dected for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Set when a parity error is dected for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Set when a parity error is dected for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} cn30xx;
	struct cvmx_ipd_int_sum_cn30xx        cn31xx;
	struct cvmx_ipd_int_sum_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t bc_ovr                       : 1;  /**< Set when the byte-count to send to IOB overflows.
                                                         This is a PASS-3 Field. */
	uint64_t d_coll                       : 1;  /**< Set when the packet/WQE data to be sent to IOB
                                                         collides.
                                                         This is a PASS-3 Field. */
	uint64_t c_coll                       : 1;  /**< Set when the packet/WQE commands to be sent to IOB
                                                         collides.
                                                         This is a PASS-3 Field. */
	uint64_t cc_ovr                       : 1;  /**< Set when the command credits to the IOB overflow.
                                                         This is a PASS-3 Field. */
	uint64_t dc_ovr                       : 1;  /**< Set when the data credits to the IOB overflow.
                                                         This is a PASS-3 Field. */
	uint64_t bp_sub                       : 1;  /**< Set when a backpressure subtract is done with a
                                                         supplied illegal value. */
	uint64_t prc_par3                     : 1;  /**< Set when a parity error is dected for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Set when a parity error is dected for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Set when a parity error is dected for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Set when a parity error is dected for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t dc_ovr                       : 1;
	uint64_t cc_ovr                       : 1;
	uint64_t c_coll                       : 1;
	uint64_t d_coll                       : 1;
	uint64_t bc_ovr                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn38xx;
	struct cvmx_ipd_int_sum_cn30xx        cn38xxp2;
	struct cvmx_ipd_int_sum_cn38xx        cn50xx;
	struct cvmx_ipd_int_sum_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t pq_sub                       : 1;  /**< Set when a port-qos does an sub to the count
                                                         that causes the counter to wrap. */
	uint64_t pq_add                       : 1;  /**< Set when a port-qos does an add to the count
                                                         that causes the counter to wrap. */
	uint64_t bc_ovr                       : 1;  /**< Set when the byte-count to send to IOB overflows. */
	uint64_t d_coll                       : 1;  /**< Set when the packet/WQE data to be sent to IOB
                                                         collides. */
	uint64_t c_coll                       : 1;  /**< Set when the packet/WQE commands to be sent to IOB
                                                         collides. */
	uint64_t cc_ovr                       : 1;  /**< Set when the command credits to the IOB overflow. */
	uint64_t dc_ovr                       : 1;  /**< Set when the data credits to the IOB overflow. */
	uint64_t bp_sub                       : 1;  /**< Set when a backpressure subtract is done with a
                                                         supplied illegal value. */
	uint64_t prc_par3                     : 1;  /**< Set when a parity error is dected for bits
                                                         [127:96] of the PBM memory. */
	uint64_t prc_par2                     : 1;  /**< Set when a parity error is dected for bits
                                                         [95:64] of the PBM memory. */
	uint64_t prc_par1                     : 1;  /**< Set when a parity error is dected for bits
                                                         [63:32] of the PBM memory. */
	uint64_t prc_par0                     : 1;  /**< Set when a parity error is dected for bits
                                                         [31:0] of the PBM memory. */
#else
	uint64_t prc_par0                     : 1;
	uint64_t prc_par1                     : 1;
	uint64_t prc_par2                     : 1;
	uint64_t prc_par3                     : 1;
	uint64_t bp_sub                       : 1;
	uint64_t dc_ovr                       : 1;
	uint64_t cc_ovr                       : 1;
	uint64_t c_coll                       : 1;
	uint64_t d_coll                       : 1;
	uint64_t bc_ovr                       : 1;
	uint64_t pq_add                       : 1;
	uint64_t pq_sub                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn52xx;
	struct cvmx_ipd_int_sum_cn52xx        cn52xxp1;
	struct cvmx_ipd_int_sum_cn52xx        cn56xx;
	struct cvmx_ipd_int_sum_cn52xx        cn56xxp1;
	struct cvmx_ipd_int_sum_cn38xx        cn58xx;
	struct cvmx_ipd_int_sum_cn38xx        cn58xxp1;
	struct cvmx_ipd_int_sum_cn52xx        cn61xx;
	struct cvmx_ipd_int_sum_cn52xx        cn63xx;
	struct cvmx_ipd_int_sum_cn52xx        cn63xxp1;
	struct cvmx_ipd_int_sum_cn52xx        cn66xx;
	struct cvmx_ipd_int_sum_s             cn68xx;
	struct cvmx_ipd_int_sum_s             cn68xxp1;
	struct cvmx_ipd_int_sum_cn52xx        cnf71xx;
};
typedef union cvmx_ipd_int_sum cvmx_ipd_int_sum_t;

/**
 * cvmx_ipd_next_pkt_ptr
 *
 * IPD_NEXT_PKT_PTR = IPD's Next Packet Pointer
 *
 * The value of the packet-pointer fetched and in the valid register.
 */
union cvmx_ipd_next_pkt_ptr {
	uint64_t u64;
	struct cvmx_ipd_next_pkt_ptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t ptr                          : 33; /**< Pointer value. */
#else
	uint64_t ptr                          : 33;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_ipd_next_pkt_ptr_s        cn68xx;
	struct cvmx_ipd_next_pkt_ptr_s        cn68xxp1;
};
typedef union cvmx_ipd_next_pkt_ptr cvmx_ipd_next_pkt_ptr_t;

/**
 * cvmx_ipd_next_wqe_ptr
 *
 * IPD_NEXT_WQE_PTR = IPD's NEXT_WQE Pointer
 *
 * The value of the WQE-pointer fetched and in the valid register.
 */
union cvmx_ipd_next_wqe_ptr {
	uint64_t u64;
	struct cvmx_ipd_next_wqe_ptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t ptr                          : 33; /**< Pointer value.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' this field
                                                         represents a Packet-Pointer NOT a WQE pointer. */
#else
	uint64_t ptr                          : 33;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_ipd_next_wqe_ptr_s        cn68xx;
	struct cvmx_ipd_next_wqe_ptr_s        cn68xxp1;
};
typedef union cvmx_ipd_next_wqe_ptr cvmx_ipd_next_wqe_ptr_t;

/**
 * cvmx_ipd_not_1st_mbuff_skip
 *
 * IPD_NOT_1ST_MBUFF_SKIP = IPD Not First MBUFF Word Skip Size
 *
 * The number of words that the IPD will skip when writing any MBUFF that is not the first.
 */
union cvmx_ipd_not_1st_mbuff_skip {
	uint64_t u64;
	struct cvmx_ipd_not_1st_mbuff_skip_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t skip_sz                      : 6;  /**< The number of 8-byte words from the top of any
                                                         MBUFF, that is not the 1st MBUFF, that the IPD
                                                         will write the next-pointer.
                                                         Legal values are 0 to 32, where the MAX value
                                                         is also limited to:
                                                         IPD_PACKET_MBUFF_SIZE[MB_SIZE] - 16. */
#else
	uint64_t skip_sz                      : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn30xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn31xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn38xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn38xxp2;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn50xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn52xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn52xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn56xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn56xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn58xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn58xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn61xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn63xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn63xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn66xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn68xx;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cn68xxp1;
	struct cvmx_ipd_not_1st_mbuff_skip_s  cnf71xx;
};
typedef union cvmx_ipd_not_1st_mbuff_skip cvmx_ipd_not_1st_mbuff_skip_t;

/**
 * cvmx_ipd_on_bp_drop_pkt#
 *
 * RESERVE SPACE UPTO 0x3FFF
 *
 *
 * RESERVED FOR FORMER IPD_SUB_PKIND_FCS - MOVED TO PIP
 *
 * RESERVE 4008 - 40FF
 *
 *
 *                  IPD_ON_BP_DROP_PKT = IPD On Backpressure Drop Packet
 *
 * When IPD applies backpressure to a BPID and the corresponding bit in this register is set,
 * then previously received packets will be dropped when processed.
 */
union cvmx_ipd_on_bp_drop_pktx {
	uint64_t u64;
	struct cvmx_ipd_on_bp_drop_pktx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t prt_enb                      : 64; /**< The BPID corresponding to the bit position in this
                                                         field will drop all NON-RAW packets to that BPID
                                                         when BPID level backpressure is applied to that
                                                         BPID.  The applying of BPID-level backpressure for
                                                         this dropping does not take into consideration the
                                                         value of IPD_BPIDX_MBUF_TH[BP_ENB], nor
                                                         IPD_RED_BPID_ENABLE[PRT_ENB]. */
#else
	uint64_t prt_enb                      : 64;
#endif
	} s;
	struct cvmx_ipd_on_bp_drop_pktx_s     cn68xx;
	struct cvmx_ipd_on_bp_drop_pktx_s     cn68xxp1;
};
typedef union cvmx_ipd_on_bp_drop_pktx cvmx_ipd_on_bp_drop_pktx_t;

/**
 * cvmx_ipd_packet_mbuff_size
 *
 * IPD_PACKET_MBUFF_SIZE = IPD's PACKET MUBUF Size In Words
 *
 * The number of words in a MBUFF used for packet data store.
 */
union cvmx_ipd_packet_mbuff_size {
	uint64_t u64;
	struct cvmx_ipd_packet_mbuff_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t mb_size                      : 12; /**< The number of 8-byte words in a MBUF.
                                                         This must be a number in the range of 32 to
                                                         2048.
                                                         This is also the size of the FPA's
                                                         Queue-0 Free-Page. */
#else
	uint64_t mb_size                      : 12;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_ipd_packet_mbuff_size_s   cn30xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn31xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn38xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn38xxp2;
	struct cvmx_ipd_packet_mbuff_size_s   cn50xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn52xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn52xxp1;
	struct cvmx_ipd_packet_mbuff_size_s   cn56xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn56xxp1;
	struct cvmx_ipd_packet_mbuff_size_s   cn58xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn58xxp1;
	struct cvmx_ipd_packet_mbuff_size_s   cn61xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn63xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn63xxp1;
	struct cvmx_ipd_packet_mbuff_size_s   cn66xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn68xx;
	struct cvmx_ipd_packet_mbuff_size_s   cn68xxp1;
	struct cvmx_ipd_packet_mbuff_size_s   cnf71xx;
};
typedef union cvmx_ipd_packet_mbuff_size cvmx_ipd_packet_mbuff_size_t;

/**
 * cvmx_ipd_pkt_err
 *
 * IPD_PKT_ERR = IPD Packet Error Register
 *
 * Provides status about the failing packet recevie error.
 */
union cvmx_ipd_pkt_err {
	uint64_t u64;
	struct cvmx_ipd_pkt_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t reasm                        : 6;  /**< When IPD_INT_SUM[14:12] bit is set, this field
                                                         latches the failing reasm number associated with
                                                         the IPD_INT_SUM[14:12] bit set.
                                                         Values 0-62 can be seen here, reasm-id 63 is not
                                                         used. */
#else
	uint64_t reasm                        : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_ipd_pkt_err_s             cn68xx;
	struct cvmx_ipd_pkt_err_s             cn68xxp1;
};
typedef union cvmx_ipd_pkt_err cvmx_ipd_pkt_err_t;

/**
 * cvmx_ipd_pkt_ptr_valid
 *
 * IPD_PKT_PTR_VALID = IPD's Packet Pointer Valid
 *
 * The value of the packet-pointer fetched and in the valid register.
 */
union cvmx_ipd_pkt_ptr_valid {
	uint64_t u64;
	struct cvmx_ipd_pkt_ptr_valid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t ptr                          : 29; /**< Pointer value. */
#else
	uint64_t ptr                          : 29;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_ipd_pkt_ptr_valid_s       cn30xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn31xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn38xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn50xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn52xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn52xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s       cn56xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn56xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s       cn58xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn58xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s       cn61xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn63xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cn63xxp1;
	struct cvmx_ipd_pkt_ptr_valid_s       cn66xx;
	struct cvmx_ipd_pkt_ptr_valid_s       cnf71xx;
};
typedef union cvmx_ipd_pkt_ptr_valid cvmx_ipd_pkt_ptr_valid_t;

/**
 * cvmx_ipd_port#_bp_page_cnt
 *
 * IPD_PORTX_BP_PAGE_CNT = IPD Port Backpressure Page Count
 *
 * The number of pages in use by the port that when exceeded, backpressure will be applied to the port.
 * See also IPD_PORTX_BP_PAGE_CNT2
 * See also IPD_PORTX_BP_PAGE_CNT3
 */
union cvmx_ipd_portx_bp_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bp_enb                       : 1;  /**< When set '1' BP will be applied, if '0' BP will
                                                         not be applied to port. */
	uint64_t page_cnt                     : 17; /**< The number of page pointers assigned to
                                                         the port, that when exceeded will cause
                                                         back-pressure to be applied to the port.
                                                         This value is in 256 page-pointer increments,
                                                         (i.e. 0 = 0-page-ptrs, 1 = 256-page-ptrs,..) */
#else
	uint64_t page_cnt                     : 17;
	uint64_t bp_enb                       : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn30xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn31xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn38xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn38xxp2;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn50xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn52xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn52xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn56xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn56xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn58xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn58xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn61xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn63xxp1;
	struct cvmx_ipd_portx_bp_page_cnt_s   cn66xx;
	struct cvmx_ipd_portx_bp_page_cnt_s   cnf71xx;
};
typedef union cvmx_ipd_portx_bp_page_cnt cvmx_ipd_portx_bp_page_cnt_t;

/**
 * cvmx_ipd_port#_bp_page_cnt2
 *
 * IPD_PORTX_BP_PAGE_CNT2 = IPD Port Backpressure Page Count
 *
 * The number of pages in use by the port that when exceeded, backpressure will be applied to the port.
 * See also IPD_PORTX_BP_PAGE_CNT
 * See also IPD_PORTX_BP_PAGE_CNT3
 * 0x368-0x380
 */
union cvmx_ipd_portx_bp_page_cnt2 {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bp_enb                       : 1;  /**< When set '1' BP will be applied, if '0' BP will
                                                         not be applied to port. */
	uint64_t page_cnt                     : 17; /**< The number of page pointers assigned to
                                                         the port, that when exceeded will cause
                                                         back-pressure to be applied to the port.
                                                         This value is in 256 page-pointer increments,
                                                         (i.e. 0 = 0-page-ptrs, 1 = 256-page-ptrs,..) */
#else
	uint64_t page_cnt                     : 17;
	uint64_t bp_enb                       : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn52xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn52xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn56xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn56xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn61xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn63xxp1;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cn66xx;
	struct cvmx_ipd_portx_bp_page_cnt2_s  cnf71xx;
};
typedef union cvmx_ipd_portx_bp_page_cnt2 cvmx_ipd_portx_bp_page_cnt2_t;

/**
 * cvmx_ipd_port#_bp_page_cnt3
 *
 * IPD_PORTX_BP_PAGE_CNT3 = IPD Port Backpressure Page Count
 *
 * The number of pages in use by the port that when exceeded, backpressure will be applied to the port.
 * See also IPD_PORTX_BP_PAGE_CNT
 * See also IPD_PORTX_BP_PAGE_CNT2
 * 0x3d0-408
 */
union cvmx_ipd_portx_bp_page_cnt3 {
	uint64_t u64;
	struct cvmx_ipd_portx_bp_page_cnt3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bp_enb                       : 1;  /**< When set '1' BP will be applied, if '0' BP will
                                                         not be applied to port. */
	uint64_t page_cnt                     : 17; /**< The number of page pointers assigned to
                                                         the port, that when exceeded will cause
                                                         back-pressure to be applied to the port.
                                                         This value is in 256 page-pointer increments,
                                                         (i.e. 0 = 0-page-ptrs, 1 = 256-page-ptrs,..) */
#else
	uint64_t page_cnt                     : 17;
	uint64_t bp_enb                       : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ipd_portx_bp_page_cnt3_s  cn61xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s  cn63xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s  cn63xxp1;
	struct cvmx_ipd_portx_bp_page_cnt3_s  cn66xx;
	struct cvmx_ipd_portx_bp_page_cnt3_s  cnf71xx;
};
typedef union cvmx_ipd_portx_bp_page_cnt3 cvmx_ipd_portx_bp_page_cnt3_t;

/**
 * cvmx_ipd_port_bp_counters2_pair#
 *
 * IPD_PORT_BP_COUNTERS2_PAIRX = MBUF Counters port Ports used to generate Back Pressure Per Port.
 * See also IPD_PORT_BP_COUNTERS_PAIRX
 * See also IPD_PORT_BP_COUNTERS3_PAIRX
 * 0x388-0x3a0
 */
union cvmx_ipd_port_bp_counters2_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters2_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t cnt_val                      : 25; /**< Number of MBUFs being used by data on this port. */
#else
	uint64_t cnt_val                      : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn52xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn52xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn56xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn56xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn63xxp1;
	struct cvmx_ipd_port_bp_counters2_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters2_pairx_s cnf71xx;
};
typedef union cvmx_ipd_port_bp_counters2_pairx cvmx_ipd_port_bp_counters2_pairx_t;

/**
 * cvmx_ipd_port_bp_counters3_pair#
 *
 * IPD_PORT_BP_COUNTERS3_PAIRX = MBUF Counters port Ports used to generate Back Pressure Per Port.
 * See also IPD_PORT_BP_COUNTERS_PAIRX
 * See also IPD_PORT_BP_COUNTERS2_PAIRX
 *  0x3b0-0x3c8
 */
union cvmx_ipd_port_bp_counters3_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters3_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t cnt_val                      : 25; /**< Number of MBUFs being used by data on this port. */
#else
	uint64_t cnt_val                      : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn63xxp1;
	struct cvmx_ipd_port_bp_counters3_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters3_pairx_s cnf71xx;
};
typedef union cvmx_ipd_port_bp_counters3_pairx cvmx_ipd_port_bp_counters3_pairx_t;

/**
 * cvmx_ipd_port_bp_counters4_pair#
 *
 * IPD_PORT_BP_COUNTERS4_PAIRX = MBUF Counters port Ports used to generate Back Pressure Per Port.
 * See also IPD_PORT_BP_COUNTERS_PAIRX
 * See also IPD_PORT_BP_COUNTERS2_PAIRX
 *  0x410-0x3c8
 */
union cvmx_ipd_port_bp_counters4_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters4_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t cnt_val                      : 25; /**< Number of MBUFs being used by data on this port. */
#else
	uint64_t cnt_val                      : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters4_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters4_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters4_pairx_s cnf71xx;
};
typedef union cvmx_ipd_port_bp_counters4_pairx cvmx_ipd_port_bp_counters4_pairx_t;

/**
 * cvmx_ipd_port_bp_counters_pair#
 *
 * IPD_PORT_BP_COUNTERS_PAIRX = MBUF Counters port Ports used to generate Back Pressure Per Port.
 * See also IPD_PORT_BP_COUNTERS2_PAIRX
 * See also IPD_PORT_BP_COUNTERS3_PAIRX
 * 0x1b8-0x2d0
 */
union cvmx_ipd_port_bp_counters_pairx {
	uint64_t u64;
	struct cvmx_ipd_port_bp_counters_pairx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t cnt_val                      : 25; /**< Number of MBUFs being used by data on this port. */
#else
	uint64_t cnt_val                      : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_ipd_port_bp_counters_pairx_s cn30xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn31xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn38xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn38xxp2;
	struct cvmx_ipd_port_bp_counters_pairx_s cn50xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn52xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn52xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn56xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn56xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn58xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn58xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn61xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn63xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cn63xxp1;
	struct cvmx_ipd_port_bp_counters_pairx_s cn66xx;
	struct cvmx_ipd_port_bp_counters_pairx_s cnf71xx;
};
typedef union cvmx_ipd_port_bp_counters_pairx cvmx_ipd_port_bp_counters_pairx_t;

/**
 * cvmx_ipd_port_ptr_fifo_ctl
 *
 * IPD_PORT_PTR_FIFO_CTL = IPD's Reasm-Id Pointer FIFO Control
 *
 * Allows reading of the Page-Pointers stored in the IPD's Reasm-Id Fifo.
 */
union cvmx_ipd_port_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_port_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t ptr                          : 33; /**< The output of the reasm-id-ptr-fifo. */
	uint64_t max_pkt                      : 7;  /**< Maximum number of Packet-Pointers that are in
                                                         in the FIFO. */
	uint64_t cena                         : 1;  /**< Active low Chip Enable to the read the
                                                         pwp_fifo. This bit also controls the MUX-select
                                                         that steers [RADDR] to the pwp_fifo.
                                                         *WARNING - Setting this field to '0' will allow
                                                         reading of the memories thorugh the PTR field,
                                                         but will cause unpredictable operation of the IPD
                                                         under normal operation. */
	uint64_t raddr                        : 7;  /**< Sets the address to read from in the reasm-id
                                                         fifo in the IPD. This FIFO holds Packet-Pointers
                                                         to be used for packet data storage. */
#else
	uint64_t raddr                        : 7;
	uint64_t cena                         : 1;
	uint64_t max_pkt                      : 7;
	uint64_t ptr                          : 33;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_ipd_port_ptr_fifo_ctl_s   cn68xx;
	struct cvmx_ipd_port_ptr_fifo_ctl_s   cn68xxp1;
};
typedef union cvmx_ipd_port_ptr_fifo_ctl cvmx_ipd_port_ptr_fifo_ctl_t;

/**
 * cvmx_ipd_port_qos_#_cnt
 *
 * IPD_PORT_QOS_X_CNT = IPD PortX QOS-0 Count
 *
 * A counter per port/qos. Counter are originzed in sequence where the first 8 counter (0-7) belong to Port-0
 * QOS 0-7 respectively followed by port 1 at (8-15), etc
 * Ports 0-3, 32-43
 */
union cvmx_ipd_port_qos_x_cnt {
	uint64_t u64;
	struct cvmx_ipd_port_qos_x_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wmark                        : 32; /**< When the field CNT after being modified is equal to
                                                         or crosses this value (i.e. value was greater than
                                                         then becomes less then, or value was less than and
                                                         becomes greater than) the corresponding bit in
                                                         IPD_PORT_QOS_INTX is set. */
	uint64_t cnt                          : 32; /**< The packet related count that is incremented as
                                                         specified by IPD_SUB_PORT_QOS_CNT. */
#else
	uint64_t cnt                          : 32;
	uint64_t wmark                        : 32;
#endif
	} s;
	struct cvmx_ipd_port_qos_x_cnt_s      cn52xx;
	struct cvmx_ipd_port_qos_x_cnt_s      cn52xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s      cn56xx;
	struct cvmx_ipd_port_qos_x_cnt_s      cn56xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s      cn61xx;
	struct cvmx_ipd_port_qos_x_cnt_s      cn63xx;
	struct cvmx_ipd_port_qos_x_cnt_s      cn63xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s      cn66xx;
	struct cvmx_ipd_port_qos_x_cnt_s      cn68xx;
	struct cvmx_ipd_port_qos_x_cnt_s      cn68xxp1;
	struct cvmx_ipd_port_qos_x_cnt_s      cnf71xx;
};
typedef union cvmx_ipd_port_qos_x_cnt cvmx_ipd_port_qos_x_cnt_t;

/**
 * cvmx_ipd_port_qos_int#
 *
 * IPD_PORT_QOS_INTX = IPD PORT-QOS Interrupt
 *
 * See the description for IPD_PORT_QOS_X_CNT
 *
 * 0=P0-7; 1=P8-15; 2=P16-23; 3=P24-31; 4=P32-39; 5=P40-47; 6=P48-55; 7=P56-63
 *
 * Only ports used are: P0-3, P32-39, and P40-47. Therefore only IPD_PORT_QOS_INT0, IPD_PORT_QOS_INT4,
 * and IPD_PORT_QOS_INT5 exist and, furthermore:  <63:32> of IPD_PORT_QOS_INT0,
 * are reserved.
 */
union cvmx_ipd_port_qos_intx {
	uint64_t u64;
	struct cvmx_ipd_port_qos_intx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t intr                         : 64; /**< Interrupt bits. */
#else
	uint64_t intr                         : 64;
#endif
	} s;
	struct cvmx_ipd_port_qos_intx_s       cn52xx;
	struct cvmx_ipd_port_qos_intx_s       cn52xxp1;
	struct cvmx_ipd_port_qos_intx_s       cn56xx;
	struct cvmx_ipd_port_qos_intx_s       cn56xxp1;
	struct cvmx_ipd_port_qos_intx_s       cn61xx;
	struct cvmx_ipd_port_qos_intx_s       cn63xx;
	struct cvmx_ipd_port_qos_intx_s       cn63xxp1;
	struct cvmx_ipd_port_qos_intx_s       cn66xx;
	struct cvmx_ipd_port_qos_intx_s       cn68xx;
	struct cvmx_ipd_port_qos_intx_s       cn68xxp1;
	struct cvmx_ipd_port_qos_intx_s       cnf71xx;
};
typedef union cvmx_ipd_port_qos_intx cvmx_ipd_port_qos_intx_t;

/**
 * cvmx_ipd_port_qos_int_enb#
 *
 * IPD_PORT_QOS_INT_ENBX = IPD PORT-QOS Interrupt Enable
 *
 * When the IPD_PORT_QOS_INTX[\#] is '1' and IPD_PORT_QOS_INT_ENBX[\#] is '1' a interrupt will be generated.
 */
union cvmx_ipd_port_qos_int_enbx {
	uint64_t u64;
	struct cvmx_ipd_port_qos_int_enbx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t enb                          : 64; /**< Enable bits. */
#else
	uint64_t enb                          : 64;
#endif
	} s;
	struct cvmx_ipd_port_qos_int_enbx_s   cn52xx;
	struct cvmx_ipd_port_qos_int_enbx_s   cn52xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s   cn56xx;
	struct cvmx_ipd_port_qos_int_enbx_s   cn56xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s   cn61xx;
	struct cvmx_ipd_port_qos_int_enbx_s   cn63xx;
	struct cvmx_ipd_port_qos_int_enbx_s   cn63xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s   cn66xx;
	struct cvmx_ipd_port_qos_int_enbx_s   cn68xx;
	struct cvmx_ipd_port_qos_int_enbx_s   cn68xxp1;
	struct cvmx_ipd_port_qos_int_enbx_s   cnf71xx;
};
typedef union cvmx_ipd_port_qos_int_enbx cvmx_ipd_port_qos_int_enbx_t;

/**
 * cvmx_ipd_port_sop#
 *
 * IPD_PORT_SOP = IPD Reasm-Id SOP
 *
 * Set when a SOP is detected on a reasm-num. Where the reasm-num value set the bit vector of this register.
 */
union cvmx_ipd_port_sopx {
	uint64_t u64;
	struct cvmx_ipd_port_sopx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t sop                          : 64; /**< When set '1' a SOP was detected on a reasm-num,
                                                         When clear '0' no SOP was yet received or an EOP
                                                         was received on the reasm-num.
                                                         IPD only supports 63 reasm-nums, so bit [63]
                                                         should never be set. */
#else
	uint64_t sop                          : 64;
#endif
	} s;
	struct cvmx_ipd_port_sopx_s           cn68xx;
	struct cvmx_ipd_port_sopx_s           cn68xxp1;
};
typedef union cvmx_ipd_port_sopx cvmx_ipd_port_sopx_t;

/**
 * cvmx_ipd_prc_hold_ptr_fifo_ctl
 *
 * IPD_PRC_HOLD_PTR_FIFO_CTL = IPD's PRC Holding Pointer FIFO Control
 *
 * Allows reading of the Page-Pointers stored in the IPD's PRC Holding Fifo.
 */
union cvmx_ipd_prc_hold_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t max_pkt                      : 3;  /**< Maximum number of Packet-Pointers that COULD be
                                                         in the FIFO. */
	uint64_t praddr                       : 3;  /**< Present Packet-Pointer read address. */
	uint64_t ptr                          : 29; /**< The output of the prc-holding-fifo. */
	uint64_t cena                         : 1;  /**< Active low Chip Enable that controls the
                                                         MUX-select that steers [RADDR] to the fifo.
                                                         *WARNING - Setting this field to '0' will allow
                                                         reading of the memories thorugh the PTR field,
                                                         but will cause unpredictable operation of the IPD
                                                         under normal operation. */
	uint64_t raddr                        : 3;  /**< Sets the address to read from in the holding.
                                                         fifo in the PRC. This FIFO holds Packet-Pointers
                                                         to be used for packet data storage. */
#else
	uint64_t raddr                        : 3;
	uint64_t cena                         : 1;
	uint64_t ptr                          : 29;
	uint64_t praddr                       : 3;
	uint64_t max_pkt                      : 3;
	uint64_t reserved_39_63               : 25;
#endif
	} s;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn30xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn31xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn38xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn50xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn52xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn52xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn56xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn56xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn58xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn58xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn61xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn63xxp1;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cn66xx;
	struct cvmx_ipd_prc_hold_ptr_fifo_ctl_s cnf71xx;
};
typedef union cvmx_ipd_prc_hold_ptr_fifo_ctl cvmx_ipd_prc_hold_ptr_fifo_ctl_t;

/**
 * cvmx_ipd_prc_port_ptr_fifo_ctl
 *
 * IPD_PRC_PORT_PTR_FIFO_CTL = IPD's PRC PORT Pointer FIFO Control
 *
 * Allows reading of the Page-Pointers stored in the IPD's PRC PORT Fifo.
 */
union cvmx_ipd_prc_port_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t max_pkt                      : 7;  /**< Maximum number of Packet-Pointers that are in
                                                         in the FIFO. */
	uint64_t ptr                          : 29; /**< The output of the prc-port-ptr-fifo. */
	uint64_t cena                         : 1;  /**< Active low Chip Enable to the read port of the
                                                         pwp_fifo. This bit also controls the MUX-select
                                                         that steers [RADDR] to the pwp_fifo.
                                                         *WARNING - Setting this field to '0' will allow
                                                         reading of the memories thorugh the PTR field,
                                                         but will cause unpredictable operation of the IPD
                                                         under normal operation. */
	uint64_t raddr                        : 7;  /**< Sets the address to read from in the port
                                                         fifo in the PRC. This FIFO holds Packet-Pointers
                                                         to be used for packet data storage. */
#else
	uint64_t raddr                        : 7;
	uint64_t cena                         : 1;
	uint64_t ptr                          : 29;
	uint64_t max_pkt                      : 7;
	uint64_t reserved_44_63               : 20;
#endif
	} s;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn30xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn31xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn38xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn50xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn52xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn52xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn56xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn56xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn58xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn58xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn61xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn63xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn63xxp1;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cn66xx;
	struct cvmx_ipd_prc_port_ptr_fifo_ctl_s cnf71xx;
};
typedef union cvmx_ipd_prc_port_ptr_fifo_ctl cvmx_ipd_prc_port_ptr_fifo_ctl_t;

/**
 * cvmx_ipd_ptr_count
 *
 * IPD_PTR_COUNT = IPD Page Pointer Count
 *
 * Shows the number of WQE and Packet Page Pointers stored in the IPD.
 */
union cvmx_ipd_ptr_count {
	uint64_t u64;
	struct cvmx_ipd_ptr_count_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t pktv_cnt                     : 1;  /**< PKT Ptr Valid. */
	uint64_t wqev_cnt                     : 1;  /**< WQE Ptr Valid. This value is '1' when a WQE
                                                         is being for use by the IPD. The value of this
                                                         field should be added to tha value of the
                                                         WQE_PCNT field, of this register, for a total
                                                         count of the WQE Page Pointers being held by IPD.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' this field
                                                         represents a Packet-Pointer NOT a WQE pointer. */
	uint64_t pfif_cnt                     : 3;  /**< See PKT_PCNT. */
	uint64_t pkt_pcnt                     : 7;  /**< This value plus PFIF_CNT plus
                                                         IPD_PRC_PORT_PTR_FIFO_CTL[MAX_PKT] is the number
                                                         of PKT Page Pointers in IPD. */
	uint64_t wqe_pcnt                     : 7;  /**< Number of page pointers for WQE storage that are
                                                         buffered in the IPD. The total count is the value
                                                         of this buffer plus the field [WQEV_CNT]. For
                                                         PASS-1 (which does not have the WQEV_CNT field)
                                                         when the value of this register is '0' there still
                                                         may be 1 pointer being held by IPD. */
#else
	uint64_t wqe_pcnt                     : 7;
	uint64_t pkt_pcnt                     : 7;
	uint64_t pfif_cnt                     : 3;
	uint64_t wqev_cnt                     : 1;
	uint64_t pktv_cnt                     : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_ipd_ptr_count_s           cn30xx;
	struct cvmx_ipd_ptr_count_s           cn31xx;
	struct cvmx_ipd_ptr_count_s           cn38xx;
	struct cvmx_ipd_ptr_count_s           cn38xxp2;
	struct cvmx_ipd_ptr_count_s           cn50xx;
	struct cvmx_ipd_ptr_count_s           cn52xx;
	struct cvmx_ipd_ptr_count_s           cn52xxp1;
	struct cvmx_ipd_ptr_count_s           cn56xx;
	struct cvmx_ipd_ptr_count_s           cn56xxp1;
	struct cvmx_ipd_ptr_count_s           cn58xx;
	struct cvmx_ipd_ptr_count_s           cn58xxp1;
	struct cvmx_ipd_ptr_count_s           cn61xx;
	struct cvmx_ipd_ptr_count_s           cn63xx;
	struct cvmx_ipd_ptr_count_s           cn63xxp1;
	struct cvmx_ipd_ptr_count_s           cn66xx;
	struct cvmx_ipd_ptr_count_s           cn68xx;
	struct cvmx_ipd_ptr_count_s           cn68xxp1;
	struct cvmx_ipd_ptr_count_s           cnf71xx;
};
typedef union cvmx_ipd_ptr_count cvmx_ipd_ptr_count_t;

/**
 * cvmx_ipd_pwp_ptr_fifo_ctl
 *
 * IPD_PWP_PTR_FIFO_CTL = IPD's PWP Pointer FIFO Control
 *
 * Allows reading of the Page-Pointers stored in the IPD's PWP Fifo.
 */
union cvmx_ipd_pwp_ptr_fifo_ctl {
	uint64_t u64;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_61_63               : 3;
	uint64_t max_cnts                     : 7;  /**< Maximum number of Packet-Pointers or WQE-Pointers
                                                         that COULD be in the FIFO.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' this field
                                                         only represents the Max number of Packet-Pointers,
                                                         WQE-Pointers are not used in this mode. */
	uint64_t wraddr                       : 8;  /**< Present FIFO WQE Read address. */
	uint64_t praddr                       : 8;  /**< Present FIFO Packet Read address. */
	uint64_t ptr                          : 29; /**< The output of the pwp_fifo. */
	uint64_t cena                         : 1;  /**< Active low Chip Enable to the read port of the
                                                         pwp_fifo. This bit also controls the MUX-select
                                                         that steers [RADDR] to the pwp_fifo.
                                                         *WARNING - Setting this field to '0' will allow
                                                         reading of the memories thorugh the PTR field,
                                                         but will cause unpredictable operation of the IPD
                                                         under normal operation. */
	uint64_t raddr                        : 8;  /**< Sets the address to read from in the pwp_fifo.
                                                         Addresses 0 through 63 contain Packet-Pointers and
                                                         addresses 64 through 127 contain WQE-Pointers.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' addresses
                                                         64 through 127 are not valid. */
#else
	uint64_t raddr                        : 8;
	uint64_t cena                         : 1;
	uint64_t ptr                          : 29;
	uint64_t praddr                       : 8;
	uint64_t wraddr                       : 8;
	uint64_t max_cnts                     : 7;
	uint64_t reserved_61_63               : 3;
#endif
	} s;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn30xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn31xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn38xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn50xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn52xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn52xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn56xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn56xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn58xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn58xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn61xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn63xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn63xxp1;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cn66xx;
	struct cvmx_ipd_pwp_ptr_fifo_ctl_s    cnf71xx;
};
typedef union cvmx_ipd_pwp_ptr_fifo_ctl cvmx_ipd_pwp_ptr_fifo_ctl_t;

/**
 * cvmx_ipd_qos#_red_marks
 *
 * IPD_QOS0_RED_MARKS = IPD QOS 0 Marks Red High Low
 *
 * Set the pass-drop marks for qos level.
 */
union cvmx_ipd_qosx_red_marks {
	uint64_t u64;
	struct cvmx_ipd_qosx_red_marks_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t drop                         : 32; /**< Packets will be dropped when the average value of
                                                         IPD_QUE0_FREE_PAGE_CNT is equal to or less than
                                                         this value. */
	uint64_t pass                         : 32; /**< Packets will be passed when the average value of
                                                         IPD_QUE0_FREE_PAGE_CNT is larger than this value. */
#else
	uint64_t pass                         : 32;
	uint64_t drop                         : 32;
#endif
	} s;
	struct cvmx_ipd_qosx_red_marks_s      cn30xx;
	struct cvmx_ipd_qosx_red_marks_s      cn31xx;
	struct cvmx_ipd_qosx_red_marks_s      cn38xx;
	struct cvmx_ipd_qosx_red_marks_s      cn38xxp2;
	struct cvmx_ipd_qosx_red_marks_s      cn50xx;
	struct cvmx_ipd_qosx_red_marks_s      cn52xx;
	struct cvmx_ipd_qosx_red_marks_s      cn52xxp1;
	struct cvmx_ipd_qosx_red_marks_s      cn56xx;
	struct cvmx_ipd_qosx_red_marks_s      cn56xxp1;
	struct cvmx_ipd_qosx_red_marks_s      cn58xx;
	struct cvmx_ipd_qosx_red_marks_s      cn58xxp1;
	struct cvmx_ipd_qosx_red_marks_s      cn61xx;
	struct cvmx_ipd_qosx_red_marks_s      cn63xx;
	struct cvmx_ipd_qosx_red_marks_s      cn63xxp1;
	struct cvmx_ipd_qosx_red_marks_s      cn66xx;
	struct cvmx_ipd_qosx_red_marks_s      cn68xx;
	struct cvmx_ipd_qosx_red_marks_s      cn68xxp1;
	struct cvmx_ipd_qosx_red_marks_s      cnf71xx;
};
typedef union cvmx_ipd_qosx_red_marks cvmx_ipd_qosx_red_marks_t;

/**
 * cvmx_ipd_que0_free_page_cnt
 *
 * IPD_QUE0_FREE_PAGE_CNT = IPD Queue0 Free Page Count
 *
 * Number of Free-Page Pointer that are available for use in the FPA for Queue-0.
 */
union cvmx_ipd_que0_free_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_que0_free_page_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t q0_pcnt                      : 32; /**< Number of Queue-0 Page Pointers Available. */
#else
	uint64_t q0_pcnt                      : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ipd_que0_free_page_cnt_s  cn30xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn31xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn38xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn38xxp2;
	struct cvmx_ipd_que0_free_page_cnt_s  cn50xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn52xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn52xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s  cn56xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn56xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s  cn58xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn58xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s  cn61xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn63xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn63xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s  cn66xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn68xx;
	struct cvmx_ipd_que0_free_page_cnt_s  cn68xxp1;
	struct cvmx_ipd_que0_free_page_cnt_s  cnf71xx;
};
typedef union cvmx_ipd_que0_free_page_cnt cvmx_ipd_que0_free_page_cnt_t;

/**
 * cvmx_ipd_red_bpid_enable#
 *
 * IPD_RED_BPID_ENABLE = IPD RED BPID Enable
 *
 * Set the pass-drop marks for qos level.
 */
union cvmx_ipd_red_bpid_enablex {
	uint64_t u64;
	struct cvmx_ipd_red_bpid_enablex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t prt_enb                      : 64; /**< The bit position will enable the corresponding
                                                         BPIDs ability to have packets dropped by RED
                                                         probability. */
#else
	uint64_t prt_enb                      : 64;
#endif
	} s;
	struct cvmx_ipd_red_bpid_enablex_s    cn68xx;
	struct cvmx_ipd_red_bpid_enablex_s    cn68xxp1;
};
typedef union cvmx_ipd_red_bpid_enablex cvmx_ipd_red_bpid_enablex_t;

/**
 * cvmx_ipd_red_delay
 *
 * IPD_RED_DELAY = IPD RED BPID Enable
 *
 * Set the pass-drop marks for qos level.
 */
union cvmx_ipd_red_delay {
	uint64_t u64;
	struct cvmx_ipd_red_delay_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t prb_dly                      : 14; /**< Number (core clocks periods + 68) * 8 to wait
                                                         before calculating the new packet drop
                                                         probability for each QOS level. */
	uint64_t avg_dly                      : 14; /**< Number (core clocks periods + 10) * 8 to wait
                                                         before calculating the moving average for each
                                                         QOS level.
                                                         Larger AVG_DLY values cause the moving averages
                                                         of ALL QOS levels to track changes in the actual
                                                         free space more slowly. Smaller NEW_CON (and
                                                         larger AVG_CON) values can have a similar effect,
                                                         but only affect an individual QOS level, rather
                                                         than all. */
#else
	uint64_t avg_dly                      : 14;
	uint64_t prb_dly                      : 14;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_ipd_red_delay_s           cn68xx;
	struct cvmx_ipd_red_delay_s           cn68xxp1;
};
typedef union cvmx_ipd_red_delay cvmx_ipd_red_delay_t;

/**
 * cvmx_ipd_red_port_enable
 *
 * IPD_RED_PORT_ENABLE = IPD RED Port Enable
 *
 * Set the pass-drop marks for qos level.
 */
union cvmx_ipd_red_port_enable {
	uint64_t u64;
	struct cvmx_ipd_red_port_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t prb_dly                      : 14; /**< Number (core clocks periods + 68) * 8 to wait
                                                         before calculating the new packet drop
                                                         probability for each QOS level. */
	uint64_t avg_dly                      : 14; /**< Number (core clocks periods + 10) * 8 to wait
                                                         before calculating the moving average for each
                                                         QOS level.
                                                         Larger AVG_DLY values cause the moving averages
                                                         of ALL QOS levels to track changes in the actual
                                                         free space more slowly. Smaller NEW_CON (and
                                                         larger AVG_CON) values can have a similar effect,
                                                         but only affect an individual QOS level, rather
                                                         than all. */
	uint64_t prt_enb                      : 36; /**< The bit position will enable the corresponding
                                                         Ports ability to have packets dropped by RED
                                                         probability. */
#else
	uint64_t prt_enb                      : 36;
	uint64_t avg_dly                      : 14;
	uint64_t prb_dly                      : 14;
#endif
	} s;
	struct cvmx_ipd_red_port_enable_s     cn30xx;
	struct cvmx_ipd_red_port_enable_s     cn31xx;
	struct cvmx_ipd_red_port_enable_s     cn38xx;
	struct cvmx_ipd_red_port_enable_s     cn38xxp2;
	struct cvmx_ipd_red_port_enable_s     cn50xx;
	struct cvmx_ipd_red_port_enable_s     cn52xx;
	struct cvmx_ipd_red_port_enable_s     cn52xxp1;
	struct cvmx_ipd_red_port_enable_s     cn56xx;
	struct cvmx_ipd_red_port_enable_s     cn56xxp1;
	struct cvmx_ipd_red_port_enable_s     cn58xx;
	struct cvmx_ipd_red_port_enable_s     cn58xxp1;
	struct cvmx_ipd_red_port_enable_s     cn61xx;
	struct cvmx_ipd_red_port_enable_s     cn63xx;
	struct cvmx_ipd_red_port_enable_s     cn63xxp1;
	struct cvmx_ipd_red_port_enable_s     cn66xx;
	struct cvmx_ipd_red_port_enable_s     cnf71xx;
};
typedef union cvmx_ipd_red_port_enable cvmx_ipd_red_port_enable_t;

/**
 * cvmx_ipd_red_port_enable2
 *
 * IPD_RED_PORT_ENABLE2 = IPD RED Port Enable2
 *
 * Set the pass-drop marks for qos level.
 */
union cvmx_ipd_red_port_enable2 {
	uint64_t u64;
	struct cvmx_ipd_red_port_enable2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t prt_enb                      : 12; /**< Bits 11-0 corresponds to ports 47-36. These bits
                                                         have the same meaning as the PRT_ENB field of
                                                         IPD_RED_PORT_ENABLE. */
#else
	uint64_t prt_enb                      : 12;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_ipd_red_port_enable2_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t prt_enb                      : 4;  /**< Bits 3-0 cooresponds to ports 39-36. These bits
                                                         have the same meaning as the PRT_ENB field of
                                                         IPD_RED_PORT_ENABLE. */
#else
	uint64_t prt_enb                      : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} cn52xx;
	struct cvmx_ipd_red_port_enable2_cn52xx cn52xxp1;
	struct cvmx_ipd_red_port_enable2_cn52xx cn56xx;
	struct cvmx_ipd_red_port_enable2_cn52xx cn56xxp1;
	struct cvmx_ipd_red_port_enable2_s    cn61xx;
	struct cvmx_ipd_red_port_enable2_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t prt_enb                      : 8;  /**< Bits 7-0 corresponds to ports 43-36. These bits
                                                         have the same meaning as the PRT_ENB field of
                                                         IPD_RED_PORT_ENABLE. */
#else
	uint64_t prt_enb                      : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} cn63xx;
	struct cvmx_ipd_red_port_enable2_cn63xx cn63xxp1;
	struct cvmx_ipd_red_port_enable2_s    cn66xx;
	struct cvmx_ipd_red_port_enable2_s    cnf71xx;
};
typedef union cvmx_ipd_red_port_enable2 cvmx_ipd_red_port_enable2_t;

/**
 * cvmx_ipd_red_que#_param
 *
 * IPD_RED_QUE0_PARAM = IPD RED Queue-0 Parameters
 *
 * Value control the Passing and Dropping of packets by the red engine for QOS Level-0.
 */
union cvmx_ipd_red_quex_param {
	uint64_t u64;
	struct cvmx_ipd_red_quex_param_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t use_pcnt                     : 1;  /**< When set '1' red will use the actual Packet-Page
                                                         Count in place of the Average for RED calculations. */
	uint64_t new_con                      : 8;  /**< This value is used control how much of the present
                                                         Actual Queue Size is used to calculate the new
                                                         Average Queue Size. The value is a number from 0
                                                         256, which represents NEW_CON/256 of the Actual
                                                         Queue Size that will be used in the calculation.
                                                         The number in this field plus the value of
                                                         AVG_CON must be equal to 256.
                                                         Larger AVG_DLY values cause the moving averages
                                                         of ALL QOS levels to track changes in the actual
                                                         free space more slowly. Smaller NEW_CON (and
                                                         larger AVG_CON) values can have a similar effect,
                                                         but only affect an individual QOS level, rather
                                                         than all. */
	uint64_t avg_con                      : 8;  /**< This value is used control how much of the present
                                                         Average Queue Size is used to calculate the new
                                                         Average Queue Size. The value is a number from 0
                                                         256, which represents AVG_CON/256 of the Average
                                                         Queue Size that will be used in the calculation.
                                                         The number in this field plus the value of
                                                         NEW_CON must be equal to 256.
                                                         Larger AVG_DLY values cause the moving averages
                                                         of ALL QOS levels to track changes in the actual
                                                         free space more slowly. Smaller NEW_CON (and
                                                         larger AVG_CON) values can have a similar effect,
                                                         but only affect an individual QOS level, rather
                                                         than all. */
	uint64_t prb_con                      : 32; /**< Used in computing the probability of a packet being
                                                         passed or drop by the WRED engine. The field is
                                                         calculated to be (255 * 2^24)/(PASS-DROP). Where
                                                         PASS and DROP are the field from the
                                                         IPD_QOS0_RED_MARKS CSR. */
#else
	uint64_t prb_con                      : 32;
	uint64_t avg_con                      : 8;
	uint64_t new_con                      : 8;
	uint64_t use_pcnt                     : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} s;
	struct cvmx_ipd_red_quex_param_s      cn30xx;
	struct cvmx_ipd_red_quex_param_s      cn31xx;
	struct cvmx_ipd_red_quex_param_s      cn38xx;
	struct cvmx_ipd_red_quex_param_s      cn38xxp2;
	struct cvmx_ipd_red_quex_param_s      cn50xx;
	struct cvmx_ipd_red_quex_param_s      cn52xx;
	struct cvmx_ipd_red_quex_param_s      cn52xxp1;
	struct cvmx_ipd_red_quex_param_s      cn56xx;
	struct cvmx_ipd_red_quex_param_s      cn56xxp1;
	struct cvmx_ipd_red_quex_param_s      cn58xx;
	struct cvmx_ipd_red_quex_param_s      cn58xxp1;
	struct cvmx_ipd_red_quex_param_s      cn61xx;
	struct cvmx_ipd_red_quex_param_s      cn63xx;
	struct cvmx_ipd_red_quex_param_s      cn63xxp1;
	struct cvmx_ipd_red_quex_param_s      cn66xx;
	struct cvmx_ipd_red_quex_param_s      cn68xx;
	struct cvmx_ipd_red_quex_param_s      cn68xxp1;
	struct cvmx_ipd_red_quex_param_s      cnf71xx;
};
typedef union cvmx_ipd_red_quex_param cvmx_ipd_red_quex_param_t;

/**
 * cvmx_ipd_req_wgt
 *
 * IPD_REQ_WGT = IPD REQ weights
 *
 * There are 8 devices that can request to send packet traffic to the IPD. These weights are used for the Weighted Round Robin
 * grant generated by the IPD to requestors.
 */
union cvmx_ipd_req_wgt {
	uint64_t u64;
	struct cvmx_ipd_req_wgt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wgt7                         : 8;  /**< Weight for ILK  REQ */
	uint64_t wgt6                         : 8;  /**< Weight for PKO  REQ */
	uint64_t wgt5                         : 8;  /**< Weight for DPI  REQ */
	uint64_t wgt4                         : 8;  /**< Weight for AGX4 REQ */
	uint64_t wgt3                         : 8;  /**< Weight for AGX3 REQ */
	uint64_t wgt2                         : 8;  /**< Weight for AGX2 REQ */
	uint64_t wgt1                         : 8;  /**< Weight for AGX1 REQ */
	uint64_t wgt0                         : 8;  /**< Weight for AGX0 REQ */
#else
	uint64_t wgt0                         : 8;
	uint64_t wgt1                         : 8;
	uint64_t wgt2                         : 8;
	uint64_t wgt3                         : 8;
	uint64_t wgt4                         : 8;
	uint64_t wgt5                         : 8;
	uint64_t wgt6                         : 8;
	uint64_t wgt7                         : 8;
#endif
	} s;
	struct cvmx_ipd_req_wgt_s             cn68xx;
};
typedef union cvmx_ipd_req_wgt cvmx_ipd_req_wgt_t;

/**
 * cvmx_ipd_sub_port_bp_page_cnt
 *
 * IPD_SUB_PORT_BP_PAGE_CNT = IPD Subtract Port Backpressure Page Count
 *
 * Will add the value to the indicated port count register, the number of pages supplied. The value added should
 * be the 2's complement of the value that needs to be subtracted. Users add 2's complement values to the
 * port-mbuf-count register to return (lower the count) mbufs to the counter in order to avoid port-level
 * backpressure being applied to the port. Backpressure is applied when the MBUF used count of a port exceeds the
 * value in the IPD_PORTX_BP_PAGE_CNT, IPD_PORTX_BP_PAGE_CNT2, and IPD_PORTX_BP_PAGE_CNT3.
 *
 * This register can't be written from the PCI via a window write.
 */
union cvmx_ipd_sub_port_bp_page_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_bp_page_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t port                         : 6;  /**< The port to add the PAGE_CNT field to. */
	uint64_t page_cnt                     : 25; /**< The number of page pointers to add to
                                                         the port counter pointed to by the
                                                         PORT Field. */
#else
	uint64_t page_cnt                     : 25;
	uint64_t port                         : 6;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn30xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn31xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn38xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn38xxp2;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn50xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn52xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn52xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn56xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn56xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn58xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn58xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn61xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn63xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn63xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn66xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn68xx;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cn68xxp1;
	struct cvmx_ipd_sub_port_bp_page_cnt_s cnf71xx;
};
typedef union cvmx_ipd_sub_port_bp_page_cnt cvmx_ipd_sub_port_bp_page_cnt_t;

/**
 * cvmx_ipd_sub_port_fcs
 *
 * IPD_SUB_PORT_FCS = IPD Subtract Ports FCS Register
 *
 * When set '1' the port corresponding to the bit set will subtract 4 bytes from the end of
 * the packet.
 */
union cvmx_ipd_sub_port_fcs {
	uint64_t u64;
	struct cvmx_ipd_sub_port_fcs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t port_bit2                    : 4;  /**< When set '1', the port corresponding to the bit
                                                         position set, will subtract the FCS for packets
                                                         on that port. */
	uint64_t reserved_32_35               : 4;
	uint64_t port_bit                     : 32; /**< When set '1', the port corresponding to the bit
                                                         position set, will subtract the FCS for packets
                                                         on that port. */
#else
	uint64_t port_bit                     : 32;
	uint64_t reserved_32_35               : 4;
	uint64_t port_bit2                    : 4;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_ipd_sub_port_fcs_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t port_bit                     : 3;  /**< When set '1', the port corresponding to the bit
                                                         position set, will subtract the FCS for packets
                                                         on that port. */
#else
	uint64_t port_bit                     : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_ipd_sub_port_fcs_cn30xx   cn31xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t port_bit                     : 32; /**< When set '1', the port corresponding to the bit
                                                         position set, will subtract the FCS for packets
                                                         on that port. */
#else
	uint64_t port_bit                     : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} cn38xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx   cn38xxp2;
	struct cvmx_ipd_sub_port_fcs_cn30xx   cn50xx;
	struct cvmx_ipd_sub_port_fcs_s        cn52xx;
	struct cvmx_ipd_sub_port_fcs_s        cn52xxp1;
	struct cvmx_ipd_sub_port_fcs_s        cn56xx;
	struct cvmx_ipd_sub_port_fcs_s        cn56xxp1;
	struct cvmx_ipd_sub_port_fcs_cn38xx   cn58xx;
	struct cvmx_ipd_sub_port_fcs_cn38xx   cn58xxp1;
	struct cvmx_ipd_sub_port_fcs_s        cn61xx;
	struct cvmx_ipd_sub_port_fcs_s        cn63xx;
	struct cvmx_ipd_sub_port_fcs_s        cn63xxp1;
	struct cvmx_ipd_sub_port_fcs_s        cn66xx;
	struct cvmx_ipd_sub_port_fcs_s        cnf71xx;
};
typedef union cvmx_ipd_sub_port_fcs cvmx_ipd_sub_port_fcs_t;

/**
 * cvmx_ipd_sub_port_qos_cnt
 *
 * IPD_SUB_PORT_QOS_CNT = IPD Subtract Port QOS Count
 *
 * Will add the value (CNT) to the indicated Port-QOS register (PORT_QOS). The value added must be
 * be the 2's complement of the value that needs to be subtracted.
 */
union cvmx_ipd_sub_port_qos_cnt {
	uint64_t u64;
	struct cvmx_ipd_sub_port_qos_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_41_63               : 23;
	uint64_t port_qos                     : 9;  /**< The port to add the CNT field to. */
	uint64_t cnt                          : 32; /**< The value to be added to the register selected
                                                         in the PORT_QOS field. */
#else
	uint64_t cnt                          : 32;
	uint64_t port_qos                     : 9;
	uint64_t reserved_41_63               : 23;
#endif
	} s;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn52xx;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn52xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn56xx;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn56xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn61xx;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn63xx;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn63xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn66xx;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn68xx;
	struct cvmx_ipd_sub_port_qos_cnt_s    cn68xxp1;
	struct cvmx_ipd_sub_port_qos_cnt_s    cnf71xx;
};
typedef union cvmx_ipd_sub_port_qos_cnt cvmx_ipd_sub_port_qos_cnt_t;

/**
 * cvmx_ipd_wqe_fpa_queue
 *
 * IPD_WQE_FPA_QUEUE = IPD Work-Queue-Entry FPA Page Size
 *
 * Which FPA Queue (0-7) to fetch page-pointers from for WQE's
 */
union cvmx_ipd_wqe_fpa_queue {
	uint64_t u64;
	struct cvmx_ipd_wqe_fpa_queue_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t wqe_pool                     : 3;  /**< Which FPA Queue to fetch page-pointers
                                                         from for WQE's.
                                                         Not used when IPD_CTL_STATUS[NO_WPTR] is set. */
#else
	uint64_t wqe_pool                     : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_ipd_wqe_fpa_queue_s       cn30xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn31xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn38xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn38xxp2;
	struct cvmx_ipd_wqe_fpa_queue_s       cn50xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn52xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn52xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s       cn56xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn56xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s       cn58xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn58xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s       cn61xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn63xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn63xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s       cn66xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn68xx;
	struct cvmx_ipd_wqe_fpa_queue_s       cn68xxp1;
	struct cvmx_ipd_wqe_fpa_queue_s       cnf71xx;
};
typedef union cvmx_ipd_wqe_fpa_queue cvmx_ipd_wqe_fpa_queue_t;

/**
 * cvmx_ipd_wqe_ptr_valid
 *
 * IPD_WQE_PTR_VALID = IPD's WQE Pointer Valid
 *
 * The value of the WQE-pointer fetched and in the valid register.
 */
union cvmx_ipd_wqe_ptr_valid {
	uint64_t u64;
	struct cvmx_ipd_wqe_ptr_valid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t ptr                          : 29; /**< Pointer value.
                                                         When IPD_CTL_STATUS[NO_WPTR] is set '1' this field
                                                         represents a Packet-Pointer NOT a WQE pointer. */
#else
	uint64_t ptr                          : 29;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_ipd_wqe_ptr_valid_s       cn30xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn31xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn38xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn50xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn52xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn52xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s       cn56xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn56xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s       cn58xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn58xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s       cn61xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn63xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cn63xxp1;
	struct cvmx_ipd_wqe_ptr_valid_s       cn66xx;
	struct cvmx_ipd_wqe_ptr_valid_s       cnf71xx;
};
typedef union cvmx_ipd_wqe_ptr_valid cvmx_ipd_wqe_ptr_valid_t;

#endif
