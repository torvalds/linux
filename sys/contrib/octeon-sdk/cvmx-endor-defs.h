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
 * cvmx-endor-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon endor.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision: 69515 $<hr>
 *
 */
#ifndef __CVMX_ENDOR_DEFS_H__
#define __CVMX_ENDOR_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_AUTO_CLK_GATE CVMX_ENDOR_ADMA_AUTO_CLK_GATE_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_AUTO_CLK_GATE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_AUTO_CLK_GATE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844004ull);
}
#else
#define CVMX_ENDOR_ADMA_AUTO_CLK_GATE (CVMX_ADD_IO_SEG(0x00010F0000844004ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_AXIERR_INTR CVMX_ENDOR_ADMA_AXIERR_INTR_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_AXIERR_INTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_AXIERR_INTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844044ull);
}
#else
#define CVMX_ENDOR_ADMA_AXIERR_INTR (CVMX_ADD_IO_SEG(0x00010F0000844044ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_AXI_RSPCODE CVMX_ENDOR_ADMA_AXI_RSPCODE_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_AXI_RSPCODE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_AXI_RSPCODE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844050ull);
}
#else
#define CVMX_ENDOR_ADMA_AXI_RSPCODE (CVMX_ADD_IO_SEG(0x00010F0000844050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_AXI_SIGNAL CVMX_ENDOR_ADMA_AXI_SIGNAL_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_AXI_SIGNAL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_AXI_SIGNAL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844084ull);
}
#else
#define CVMX_ENDOR_ADMA_AXI_SIGNAL (CVMX_ADD_IO_SEG(0x00010F0000844084ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_DMADONE_INTR CVMX_ENDOR_ADMA_DMADONE_INTR_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_DMADONE_INTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_DMADONE_INTR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844040ull);
}
#else
#define CVMX_ENDOR_ADMA_DMADONE_INTR (CVMX_ADD_IO_SEG(0x00010F0000844040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_ADMA_DMAX_ADDR_HI(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ENDOR_ADMA_DMAX_ADDR_HI(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F000084410Cull) + ((offset) & 7) * 16;
}
#else
#define CVMX_ENDOR_ADMA_DMAX_ADDR_HI(offset) (CVMX_ADD_IO_SEG(0x00010F000084410Cull) + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_ADMA_DMAX_ADDR_LO(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ENDOR_ADMA_DMAX_ADDR_LO(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000844108ull) + ((offset) & 7) * 16;
}
#else
#define CVMX_ENDOR_ADMA_DMAX_ADDR_LO(offset) (CVMX_ADD_IO_SEG(0x00010F0000844108ull) + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_ADMA_DMAX_CFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ENDOR_ADMA_DMAX_CFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000844100ull) + ((offset) & 7) * 16;
}
#else
#define CVMX_ENDOR_ADMA_DMAX_CFG(offset) (CVMX_ADD_IO_SEG(0x00010F0000844100ull) + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_ADMA_DMAX_SIZE(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ENDOR_ADMA_DMAX_SIZE(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000844104ull) + ((offset) & 7) * 16;
}
#else
#define CVMX_ENDOR_ADMA_DMAX_SIZE(offset) (CVMX_ADD_IO_SEG(0x00010F0000844104ull) + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_DMA_PRIORITY CVMX_ENDOR_ADMA_DMA_PRIORITY_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_DMA_PRIORITY_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_DMA_PRIORITY not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844080ull);
}
#else
#define CVMX_ENDOR_ADMA_DMA_PRIORITY (CVMX_ADD_IO_SEG(0x00010F0000844080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_DMA_RESET CVMX_ENDOR_ADMA_DMA_RESET_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_DMA_RESET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_DMA_RESET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844008ull);
}
#else
#define CVMX_ENDOR_ADMA_DMA_RESET (CVMX_ADD_IO_SEG(0x00010F0000844008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_INTR_DIS CVMX_ENDOR_ADMA_INTR_DIS_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_INTR_DIS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_INTR_DIS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000084404Cull);
}
#else
#define CVMX_ENDOR_ADMA_INTR_DIS (CVMX_ADD_IO_SEG(0x00010F000084404Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_INTR_ENB CVMX_ENDOR_ADMA_INTR_ENB_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_INTR_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_INTR_ENB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844048ull);
}
#else
#define CVMX_ENDOR_ADMA_INTR_ENB (CVMX_ADD_IO_SEG(0x00010F0000844048ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_ADMA_MODULE_STATUS CVMX_ENDOR_ADMA_MODULE_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_ADMA_MODULE_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_ADMA_MODULE_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844000ull);
}
#else
#define CVMX_ENDOR_ADMA_MODULE_STATUS (CVMX_ADD_IO_SEG(0x00010F0000844000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_CNTL_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_CNTL_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008201E4ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ENDOR_INTC_CNTL_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008201E4ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_CNTL_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_CNTL_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008201E0ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ENDOR_INTC_CNTL_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F00008201E0ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_INDEX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_INDEX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008201A4ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ENDOR_INTC_INDEX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008201A4ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_INDEX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_INDEX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008201A0ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ENDOR_INTC_INDEX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F00008201A0ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_MISC_IDX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_IDX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820134ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_MISC_IDX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820134ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_MISC_IDX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_IDX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820114ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_MISC_IDX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820114ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_MISC_MASK_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_MASK_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820034ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_MISC_MASK_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820034ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_MISC_MASK_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_MASK_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820014ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_MISC_MASK_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820014ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_MISC_RINT CVMX_ENDOR_INTC_MISC_RINT_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_MISC_RINT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_RINT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820194ull);
}
#else
#define CVMX_ENDOR_INTC_MISC_RINT (CVMX_ADD_IO_SEG(0x00010F0000820194ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_MISC_STATUS_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_STATUS_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008200B4ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_MISC_STATUS_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008200B4ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_MISC_STATUS_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_MISC_STATUS_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820094ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_MISC_STATUS_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820094ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RDQ_IDX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_IDX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F000082012Cull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RDQ_IDX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F000082012Cull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RDQ_IDX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_IDX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F000082010Cull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RDQ_IDX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F000082010Cull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RDQ_MASK_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_MASK_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F000082002Cull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RDQ_MASK_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F000082002Cull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RDQ_MASK_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_MASK_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F000082000Cull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RDQ_MASK_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F000082000Cull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_RDQ_RINT CVMX_ENDOR_INTC_RDQ_RINT_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_RDQ_RINT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_RINT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000082018Cull);
}
#else
#define CVMX_ENDOR_INTC_RDQ_RINT (CVMX_ADD_IO_SEG(0x00010F000082018Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RDQ_STATUS_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_STATUS_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008200ACull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RDQ_STATUS_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008200ACull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RDQ_STATUS_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RDQ_STATUS_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F000082008Cull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RDQ_STATUS_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F000082008Cull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RD_IDX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RD_IDX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820124ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RD_IDX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820124ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RD_IDX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RD_IDX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820104ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RD_IDX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820104ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RD_MASK_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RD_MASK_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820024ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RD_MASK_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820024ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RD_MASK_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RD_MASK_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820004ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RD_MASK_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820004ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_RD_RINT CVMX_ENDOR_INTC_RD_RINT_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_RD_RINT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_RD_RINT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820184ull);
}
#else
#define CVMX_ENDOR_INTC_RD_RINT (CVMX_ADD_IO_SEG(0x00010F0000820184ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RD_STATUS_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RD_STATUS_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008200A4ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RD_STATUS_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008200A4ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_RD_STATUS_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_RD_STATUS_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820084ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_RD_STATUS_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820084ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_STAT_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_STAT_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008201C4ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ENDOR_INTC_STAT_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008201C4ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_STAT_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_STAT_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008201C0ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_ENDOR_INTC_STAT_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F00008201C0ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_SWCLR CVMX_ENDOR_INTC_SWCLR_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_SWCLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_SWCLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820204ull);
}
#else
#define CVMX_ENDOR_INTC_SWCLR (CVMX_ADD_IO_SEG(0x00010F0000820204ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_SWSET CVMX_ENDOR_INTC_SWSET_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_SWSET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_SWSET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820200ull);
}
#else
#define CVMX_ENDOR_INTC_SWSET (CVMX_ADD_IO_SEG(0x00010F0000820200ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_SW_IDX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_SW_IDX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820130ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_SW_IDX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820130ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_SW_IDX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_SW_IDX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820110ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_SW_IDX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820110ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_SW_MASK_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_SW_MASK_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820030ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_SW_MASK_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820030ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_SW_MASK_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_SW_MASK_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820010ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_SW_MASK_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820010ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_SW_RINT CVMX_ENDOR_INTC_SW_RINT_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_SW_RINT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_SW_RINT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820190ull);
}
#else
#define CVMX_ENDOR_INTC_SW_RINT (CVMX_ADD_IO_SEG(0x00010F0000820190ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_SW_STATUS_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_SW_STATUS_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008200B0ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_SW_STATUS_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008200B0ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_SW_STATUS_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_SW_STATUS_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820090ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_SW_STATUS_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820090ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WRQ_IDX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_IDX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820128ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WRQ_IDX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820128ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WRQ_IDX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_IDX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820108ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WRQ_IDX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820108ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WRQ_MASK_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_MASK_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820028ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WRQ_MASK_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820028ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WRQ_MASK_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_MASK_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820008ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WRQ_MASK_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820008ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_WRQ_RINT CVMX_ENDOR_INTC_WRQ_RINT_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_WRQ_RINT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_RINT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820188ull);
}
#else
#define CVMX_ENDOR_INTC_WRQ_RINT (CVMX_ADD_IO_SEG(0x00010F0000820188ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WRQ_STATUS_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_STATUS_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008200A8ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WRQ_STATUS_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008200A8ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WRQ_STATUS_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WRQ_STATUS_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820088ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WRQ_STATUS_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820088ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WR_IDX_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WR_IDX_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820120ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WR_IDX_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820120ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WR_IDX_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WR_IDX_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820100ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WR_IDX_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820100ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WR_MASK_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WR_MASK_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820020ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WR_MASK_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820020ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WR_MASK_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WR_MASK_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820000ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WR_MASK_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820000ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_INTC_WR_RINT CVMX_ENDOR_INTC_WR_RINT_FUNC()
static inline uint64_t CVMX_ENDOR_INTC_WR_RINT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_INTC_WR_RINT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000820180ull);
}
#else
#define CVMX_ENDOR_INTC_WR_RINT (CVMX_ADD_IO_SEG(0x00010F0000820180ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WR_STATUS_HIX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WR_STATUS_HIX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F00008200A0ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WR_STATUS_HIX(offset) (CVMX_ADD_IO_SEG(0x00010F00008200A0ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_INTC_WR_STATUS_LOX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ENDOR_INTC_WR_STATUS_LOX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000820080ull) + ((offset) & 1) * 64;
}
#else
#define CVMX_ENDOR_INTC_WR_STATUS_LOX(offset) (CVMX_ADD_IO_SEG(0x00010F0000820080ull) + ((offset) & 1) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR0 CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR0_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832054ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR0 (CVMX_ADD_IO_SEG(0x00010F0000832054ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR1 CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR1_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000083205Cull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR1 (CVMX_ADD_IO_SEG(0x00010F000083205Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR2 CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR2_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832064ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR2 (CVMX_ADD_IO_SEG(0x00010F0000832064ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR3 CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR3_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000083206Cull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_END_ADDR3 (CVMX_ADD_IO_SEG(0x00010F000083206Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR0 CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR0_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832050ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR0 (CVMX_ADD_IO_SEG(0x00010F0000832050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR1 CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR1_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832058ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR1 (CVMX_ADD_IO_SEG(0x00010F0000832058ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR2 CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR2_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832060ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR2 (CVMX_ADD_IO_SEG(0x00010F0000832060ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR3 CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR3_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832068ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_CBUF_START_ADDR3 (CVMX_ADD_IO_SEG(0x00010F0000832068ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_INTR_CLEAR CVMX_ENDOR_OFS_HMM_INTR_CLEAR_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_INTR_CLEAR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_INTR_CLEAR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832018ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_INTR_CLEAR (CVMX_ADD_IO_SEG(0x00010F0000832018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_INTR_ENB CVMX_ENDOR_OFS_HMM_INTR_ENB_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_INTR_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_INTR_ENB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000083201Cull);
}
#else
#define CVMX_ENDOR_OFS_HMM_INTR_ENB (CVMX_ADD_IO_SEG(0x00010F000083201Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_INTR_RSTATUS CVMX_ENDOR_OFS_HMM_INTR_RSTATUS_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_INTR_RSTATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_INTR_RSTATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832014ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_INTR_RSTATUS (CVMX_ADD_IO_SEG(0x00010F0000832014ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_INTR_STATUS CVMX_ENDOR_OFS_HMM_INTR_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_INTR_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_INTR_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832010ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_INTR_STATUS (CVMX_ADD_IO_SEG(0x00010F0000832010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_INTR_TEST CVMX_ENDOR_OFS_HMM_INTR_TEST_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_INTR_TEST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_INTR_TEST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832020ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_INTR_TEST (CVMX_ADD_IO_SEG(0x00010F0000832020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_MODE CVMX_ENDOR_OFS_HMM_MODE_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_MODE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_MODE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832004ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_MODE (CVMX_ADD_IO_SEG(0x00010F0000832004ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_START_ADDR0 CVMX_ENDOR_OFS_HMM_START_ADDR0_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_START_ADDR0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_START_ADDR0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832030ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_START_ADDR0 (CVMX_ADD_IO_SEG(0x00010F0000832030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_START_ADDR1 CVMX_ENDOR_OFS_HMM_START_ADDR1_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_START_ADDR1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_START_ADDR1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832034ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_START_ADDR1 (CVMX_ADD_IO_SEG(0x00010F0000832034ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_START_ADDR2 CVMX_ENDOR_OFS_HMM_START_ADDR2_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_START_ADDR2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_START_ADDR2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832038ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_START_ADDR2 (CVMX_ADD_IO_SEG(0x00010F0000832038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_START_ADDR3 CVMX_ENDOR_OFS_HMM_START_ADDR3_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_START_ADDR3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_START_ADDR3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000083203Cull);
}
#else
#define CVMX_ENDOR_OFS_HMM_START_ADDR3 (CVMX_ADD_IO_SEG(0x00010F000083203Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_STATUS CVMX_ENDOR_OFS_HMM_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832000ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_STATUS (CVMX_ADD_IO_SEG(0x00010F0000832000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_XFER_CNT CVMX_ENDOR_OFS_HMM_XFER_CNT_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_XFER_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_XFER_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000083202Cull);
}
#else
#define CVMX_ENDOR_OFS_HMM_XFER_CNT (CVMX_ADD_IO_SEG(0x00010F000083202Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_XFER_Q_STATUS CVMX_ENDOR_OFS_HMM_XFER_Q_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_XFER_Q_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_XFER_Q_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000083200Cull);
}
#else
#define CVMX_ENDOR_OFS_HMM_XFER_Q_STATUS (CVMX_ADD_IO_SEG(0x00010F000083200Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_OFS_HMM_XFER_START CVMX_ENDOR_OFS_HMM_XFER_START_FUNC()
static inline uint64_t CVMX_ENDOR_OFS_HMM_XFER_START_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_OFS_HMM_XFER_START not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000832028ull);
}
#else
#define CVMX_ENDOR_OFS_HMM_XFER_START (CVMX_ADD_IO_SEG(0x00010F0000832028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_1PPS_GEN_CFG CVMX_ENDOR_RFIF_1PPS_GEN_CFG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_1PPS_GEN_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_1PPS_GEN_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680CCull);
}
#else
#define CVMX_ENDOR_RFIF_1PPS_GEN_CFG (CVMX_ADD_IO_SEG(0x00010F00008680CCull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_1PPS_SAMPLE_CNT_OFFSET CVMX_ENDOR_RFIF_1PPS_SAMPLE_CNT_OFFSET_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_1PPS_SAMPLE_CNT_OFFSET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_1PPS_SAMPLE_CNT_OFFSET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868104ull);
}
#else
#define CVMX_ENDOR_RFIF_1PPS_SAMPLE_CNT_OFFSET (CVMX_ADD_IO_SEG(0x00010F0000868104ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_1PPS_VERIF_GEN_EN CVMX_ENDOR_RFIF_1PPS_VERIF_GEN_EN_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_1PPS_VERIF_GEN_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_1PPS_VERIF_GEN_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868110ull);
}
#else
#define CVMX_ENDOR_RFIF_1PPS_VERIF_GEN_EN (CVMX_ADD_IO_SEG(0x00010F0000868110ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_1PPS_VERIF_SCNT CVMX_ENDOR_RFIF_1PPS_VERIF_SCNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_1PPS_VERIF_SCNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_1PPS_VERIF_SCNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868114ull);
}
#else
#define CVMX_ENDOR_RFIF_1PPS_VERIF_SCNT (CVMX_ADD_IO_SEG(0x00010F0000868114ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_CONF CVMX_ENDOR_RFIF_CONF_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_CONF_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_CONF not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868010ull);
}
#else
#define CVMX_ENDOR_RFIF_CONF (CVMX_ADD_IO_SEG(0x00010F0000868010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_CONF2 CVMX_ENDOR_RFIF_CONF2_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_CONF2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_CONF2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086801Cull);
}
#else
#define CVMX_ENDOR_RFIF_CONF2 (CVMX_ADD_IO_SEG(0x00010F000086801Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_DSP1_GPIO CVMX_ENDOR_RFIF_DSP1_GPIO_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_DSP1_GPIO_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_DSP1_GPIO not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008684C0ull);
}
#else
#define CVMX_ENDOR_RFIF_DSP1_GPIO (CVMX_ADD_IO_SEG(0x00010F00008684C0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_DSP_RX_HIS CVMX_ENDOR_RFIF_DSP_RX_HIS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_DSP_RX_HIS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_DSP_RX_HIS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086840Cull);
}
#else
#define CVMX_ENDOR_RFIF_DSP_RX_HIS (CVMX_ADD_IO_SEG(0x00010F000086840Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_DSP_RX_ISM CVMX_ENDOR_RFIF_DSP_RX_ISM_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_DSP_RX_ISM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_DSP_RX_ISM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868400ull);
}
#else
#define CVMX_ENDOR_RFIF_DSP_RX_ISM (CVMX_ADD_IO_SEG(0x00010F0000868400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_FIRS_ENABLE CVMX_ENDOR_RFIF_FIRS_ENABLE_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_FIRS_ENABLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_FIRS_ENABLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008684C4ull);
}
#else
#define CVMX_ENDOR_RFIF_FIRS_ENABLE (CVMX_ADD_IO_SEG(0x00010F00008684C4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_FRAME_CNT CVMX_ENDOR_RFIF_FRAME_CNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_FRAME_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_FRAME_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868030ull);
}
#else
#define CVMX_ENDOR_RFIF_FRAME_CNT (CVMX_ADD_IO_SEG(0x00010F0000868030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_FRAME_L CVMX_ENDOR_RFIF_FRAME_L_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_FRAME_L_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_FRAME_L not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868014ull);
}
#else
#define CVMX_ENDOR_RFIF_FRAME_L (CVMX_ADD_IO_SEG(0x00010F0000868014ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_GPIO_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_ENDOR_RFIF_GPIO_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868418ull) + ((offset) & 3) * 4;
}
#else
#define CVMX_ENDOR_RFIF_GPIO_X(offset) (CVMX_ADD_IO_SEG(0x00010F0000868418ull) + ((offset) & 3) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_MAX_SAMPLE_ADJ CVMX_ENDOR_RFIF_MAX_SAMPLE_ADJ_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_MAX_SAMPLE_ADJ_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_MAX_SAMPLE_ADJ not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680DCull);
}
#else
#define CVMX_ENDOR_RFIF_MAX_SAMPLE_ADJ (CVMX_ADD_IO_SEG(0x00010F00008680DCull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_MIN_SAMPLE_ADJ CVMX_ENDOR_RFIF_MIN_SAMPLE_ADJ_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_MIN_SAMPLE_ADJ_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_MIN_SAMPLE_ADJ not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680E0ull);
}
#else
#define CVMX_ENDOR_RFIF_MIN_SAMPLE_ADJ (CVMX_ADD_IO_SEG(0x00010F00008680E0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_NUM_RX_WIN CVMX_ENDOR_RFIF_NUM_RX_WIN_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_NUM_RX_WIN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_NUM_RX_WIN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868018ull);
}
#else
#define CVMX_ENDOR_RFIF_NUM_RX_WIN (CVMX_ADD_IO_SEG(0x00010F0000868018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_PWM_ENABLE CVMX_ENDOR_RFIF_PWM_ENABLE_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_PWM_ENABLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_PWM_ENABLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868180ull);
}
#else
#define CVMX_ENDOR_RFIF_PWM_ENABLE (CVMX_ADD_IO_SEG(0x00010F0000868180ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_PWM_HIGH_TIME CVMX_ENDOR_RFIF_PWM_HIGH_TIME_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_PWM_HIGH_TIME_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_PWM_HIGH_TIME not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868184ull);
}
#else
#define CVMX_ENDOR_RFIF_PWM_HIGH_TIME (CVMX_ADD_IO_SEG(0x00010F0000868184ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_PWM_LOW_TIME CVMX_ENDOR_RFIF_PWM_LOW_TIME_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_PWM_LOW_TIME_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_PWM_LOW_TIME not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868188ull);
}
#else
#define CVMX_ENDOR_RFIF_PWM_LOW_TIME (CVMX_ADD_IO_SEG(0x00010F0000868188ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RD_TIMER64_LSB CVMX_ENDOR_RFIF_RD_TIMER64_LSB_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RD_TIMER64_LSB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RD_TIMER64_LSB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008681ACull);
}
#else
#define CVMX_ENDOR_RFIF_RD_TIMER64_LSB (CVMX_ADD_IO_SEG(0x00010F00008681ACull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RD_TIMER64_MSB CVMX_ENDOR_RFIF_RD_TIMER64_MSB_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RD_TIMER64_MSB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RD_TIMER64_MSB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008681B0ull);
}
#else
#define CVMX_ENDOR_RFIF_RD_TIMER64_MSB (CVMX_ADD_IO_SEG(0x00010F00008681B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_REAL_TIME_TIMER CVMX_ENDOR_RFIF_REAL_TIME_TIMER_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_REAL_TIME_TIMER_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_REAL_TIME_TIMER not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680C8ull);
}
#else
#define CVMX_ENDOR_RFIF_REAL_TIME_TIMER (CVMX_ADD_IO_SEG(0x00010F00008680C8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RF_CLK_TIMER CVMX_ENDOR_RFIF_RF_CLK_TIMER_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RF_CLK_TIMER_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RF_CLK_TIMER not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868194ull);
}
#else
#define CVMX_ENDOR_RFIF_RF_CLK_TIMER (CVMX_ADD_IO_SEG(0x00010F0000868194ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RF_CLK_TIMER_EN CVMX_ENDOR_RFIF_RF_CLK_TIMER_EN_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RF_CLK_TIMER_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RF_CLK_TIMER_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868198ull);
}
#else
#define CVMX_ENDOR_RFIF_RF_CLK_TIMER_EN (CVMX_ADD_IO_SEG(0x00010F0000868198ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_CORRECT_ADJ CVMX_ENDOR_RFIF_RX_CORRECT_ADJ_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_CORRECT_ADJ_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_CORRECT_ADJ not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680E8ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_CORRECT_ADJ (CVMX_ADD_IO_SEG(0x00010F00008680E8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_DIV_STATUS CVMX_ENDOR_RFIF_RX_DIV_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_DIV_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_DIV_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868004ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_DIV_STATUS (CVMX_ADD_IO_SEG(0x00010F0000868004ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_FIFO_CNT CVMX_ENDOR_RFIF_RX_FIFO_CNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_FIFO_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_FIFO_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868500ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_FIFO_CNT (CVMX_ADD_IO_SEG(0x00010F0000868500ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_IF_CFG CVMX_ENDOR_RFIF_RX_IF_CFG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_IF_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_IF_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868038ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_IF_CFG (CVMX_ADD_IO_SEG(0x00010F0000868038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_LEAD_LAG CVMX_ENDOR_RFIF_RX_LEAD_LAG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_LEAD_LAG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_LEAD_LAG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868020ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_LEAD_LAG (CVMX_ADD_IO_SEG(0x00010F0000868020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_LOAD_CFG CVMX_ENDOR_RFIF_RX_LOAD_CFG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_LOAD_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_LOAD_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868508ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_LOAD_CFG (CVMX_ADD_IO_SEG(0x00010F0000868508ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_OFFSET CVMX_ENDOR_RFIF_RX_OFFSET_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_OFFSET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_OFFSET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680D4ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_OFFSET (CVMX_ADD_IO_SEG(0x00010F00008680D4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_OFFSET_ADJ_SCNT CVMX_ENDOR_RFIF_RX_OFFSET_ADJ_SCNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_OFFSET_ADJ_SCNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_OFFSET_ADJ_SCNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868108ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_OFFSET_ADJ_SCNT (CVMX_ADD_IO_SEG(0x00010F0000868108ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_STATUS CVMX_ENDOR_RFIF_RX_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868000ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_STATUS (CVMX_ADD_IO_SEG(0x00010F0000868000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_SYNC_SCNT CVMX_ENDOR_RFIF_RX_SYNC_SCNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_SYNC_SCNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_SYNC_SCNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680C4ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_SYNC_SCNT (CVMX_ADD_IO_SEG(0x00010F00008680C4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_SYNC_VALUE CVMX_ENDOR_RFIF_RX_SYNC_VALUE_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_SYNC_VALUE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_SYNC_VALUE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680C0ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_SYNC_VALUE (CVMX_ADD_IO_SEG(0x00010F00008680C0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_TH CVMX_ENDOR_RFIF_RX_TH_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_TH_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_TH not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868410ull);
}
#else
#define CVMX_ENDOR_RFIF_RX_TH (CVMX_ADD_IO_SEG(0x00010F0000868410ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_RX_TRANSFER_SIZE CVMX_ENDOR_RFIF_RX_TRANSFER_SIZE_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_RX_TRANSFER_SIZE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_TRANSFER_SIZE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086850Cull);
}
#else
#define CVMX_ENDOR_RFIF_RX_TRANSFER_SIZE (CVMX_ADD_IO_SEG(0x00010F000086850Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_RX_W_EX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_W_EX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868084ull) + ((offset) & 3) * 4;
}
#else
#define CVMX_ENDOR_RFIF_RX_W_EX(offset) (CVMX_ADD_IO_SEG(0x00010F0000868084ull) + ((offset) & 3) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_RX_W_SX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_ENDOR_RFIF_RX_W_SX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868044ull) + ((offset) & 3) * 4;
}
#else
#define CVMX_ENDOR_RFIF_RX_W_SX(offset) (CVMX_ADD_IO_SEG(0x00010F0000868044ull) + ((offset) & 3) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SAMPLE_ADJ_CFG CVMX_ENDOR_RFIF_SAMPLE_ADJ_CFG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SAMPLE_ADJ_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SAMPLE_ADJ_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680E4ull);
}
#else
#define CVMX_ENDOR_RFIF_SAMPLE_ADJ_CFG (CVMX_ADD_IO_SEG(0x00010F00008680E4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SAMPLE_ADJ_ERROR CVMX_ENDOR_RFIF_SAMPLE_ADJ_ERROR_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SAMPLE_ADJ_ERROR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SAMPLE_ADJ_ERROR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868100ull);
}
#else
#define CVMX_ENDOR_RFIF_SAMPLE_ADJ_ERROR (CVMX_ADD_IO_SEG(0x00010F0000868100ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SAMPLE_CNT CVMX_ENDOR_RFIF_SAMPLE_CNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SAMPLE_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SAMPLE_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868028ull);
}
#else
#define CVMX_ENDOR_RFIF_SAMPLE_CNT (CVMX_ADD_IO_SEG(0x00010F0000868028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SKIP_FRM_CNT_BITS CVMX_ENDOR_RFIF_SKIP_FRM_CNT_BITS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SKIP_FRM_CNT_BITS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SKIP_FRM_CNT_BITS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868444ull);
}
#else
#define CVMX_ENDOR_RFIF_SKIP_FRM_CNT_BITS (CVMX_ADD_IO_SEG(0x00010F0000868444ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_SPI_CMDSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_CMDSX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868800ull) + ((offset) & 63) * 4;
}
#else
#define CVMX_ENDOR_RFIF_SPI_CMDSX(offset) (CVMX_ADD_IO_SEG(0x00010F0000868800ull) + ((offset) & 63) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_SPI_CMD_ATTRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_CMD_ATTRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868A00ull) + ((offset) & 63) * 4;
}
#else
#define CVMX_ENDOR_RFIF_SPI_CMD_ATTRX(offset) (CVMX_ADD_IO_SEG(0x00010F0000868A00ull) + ((offset) & 63) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SPI_CONF0 CVMX_ENDOR_RFIF_SPI_CONF0_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SPI_CONF0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_CONF0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868428ull);
}
#else
#define CVMX_ENDOR_RFIF_SPI_CONF0 (CVMX_ADD_IO_SEG(0x00010F0000868428ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SPI_CONF1 CVMX_ENDOR_RFIF_SPI_CONF1_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SPI_CONF1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_CONF1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086842Cull);
}
#else
#define CVMX_ENDOR_RFIF_SPI_CONF1 (CVMX_ADD_IO_SEG(0x00010F000086842Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SPI_CTRL CVMX_ENDOR_RFIF_SPI_CTRL_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SPI_CTRL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_CTRL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000866008ull);
}
#else
#define CVMX_ENDOR_RFIF_SPI_CTRL (CVMX_ADD_IO_SEG(0x00010F0000866008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_SPI_DINX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_DINX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868900ull) + ((offset) & 63) * 4;
}
#else
#define CVMX_ENDOR_RFIF_SPI_DINX(offset) (CVMX_ADD_IO_SEG(0x00010F0000868900ull) + ((offset) & 63) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SPI_RX_DATA CVMX_ENDOR_RFIF_SPI_RX_DATA_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SPI_RX_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_RX_DATA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000866000ull);
}
#else
#define CVMX_ENDOR_RFIF_SPI_RX_DATA (CVMX_ADD_IO_SEG(0x00010F0000866000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SPI_STATUS CVMX_ENDOR_RFIF_SPI_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SPI_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000866010ull);
}
#else
#define CVMX_ENDOR_RFIF_SPI_STATUS (CVMX_ADD_IO_SEG(0x00010F0000866010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_SPI_TX_DATA CVMX_ENDOR_RFIF_SPI_TX_DATA_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_SPI_TX_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_TX_DATA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000866004ull);
}
#else
#define CVMX_ENDOR_RFIF_SPI_TX_DATA (CVMX_ADD_IO_SEG(0x00010F0000866004ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_SPI_X_LL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_ENDOR_RFIF_SPI_X_LL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868430ull) + ((offset) & 3) * 4;
}
#else
#define CVMX_ENDOR_RFIF_SPI_X_LL(offset) (CVMX_ADD_IO_SEG(0x00010F0000868430ull) + ((offset) & 3) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TIMER64_CFG CVMX_ENDOR_RFIF_TIMER64_CFG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TIMER64_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TIMER64_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008681A0ull);
}
#else
#define CVMX_ENDOR_RFIF_TIMER64_CFG (CVMX_ADD_IO_SEG(0x00010F00008681A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TIMER64_EN CVMX_ENDOR_RFIF_TIMER64_EN_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TIMER64_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TIMER64_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086819Cull);
}
#else
#define CVMX_ENDOR_RFIF_TIMER64_EN (CVMX_ADD_IO_SEG(0x00010F000086819Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RFIF_TTI_SCNT_INTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ENDOR_RFIF_TTI_SCNT_INTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000868140ull) + ((offset) & 7) * 4;
}
#else
#define CVMX_ENDOR_RFIF_TTI_SCNT_INTX(offset) (CVMX_ADD_IO_SEG(0x00010F0000868140ull) + ((offset) & 7) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_CLR CVMX_ENDOR_RFIF_TTI_SCNT_INT_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TTI_SCNT_INT_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TTI_SCNT_INT_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868118ull);
}
#else
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_CLR (CVMX_ADD_IO_SEG(0x00010F0000868118ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_EN CVMX_ENDOR_RFIF_TTI_SCNT_INT_EN_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TTI_SCNT_INT_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TTI_SCNT_INT_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868124ull);
}
#else
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_EN (CVMX_ADD_IO_SEG(0x00010F0000868124ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_MAP CVMX_ENDOR_RFIF_TTI_SCNT_INT_MAP_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TTI_SCNT_INT_MAP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TTI_SCNT_INT_MAP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868120ull);
}
#else
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_MAP (CVMX_ADD_IO_SEG(0x00010F0000868120ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_STAT CVMX_ENDOR_RFIF_TTI_SCNT_INT_STAT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TTI_SCNT_INT_STAT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TTI_SCNT_INT_STAT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086811Cull);
}
#else
#define CVMX_ENDOR_RFIF_TTI_SCNT_INT_STAT (CVMX_ADD_IO_SEG(0x00010F000086811Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_DIV_STATUS CVMX_ENDOR_RFIF_TX_DIV_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_DIV_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_DIV_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086800Cull);
}
#else
#define CVMX_ENDOR_RFIF_TX_DIV_STATUS (CVMX_ADD_IO_SEG(0x00010F000086800Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_IF_CFG CVMX_ENDOR_RFIF_TX_IF_CFG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_IF_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_IF_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868034ull);
}
#else
#define CVMX_ENDOR_RFIF_TX_IF_CFG (CVMX_ADD_IO_SEG(0x00010F0000868034ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_LEAD_LAG CVMX_ENDOR_RFIF_TX_LEAD_LAG_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_LEAD_LAG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_LEAD_LAG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868024ull);
}
#else
#define CVMX_ENDOR_RFIF_TX_LEAD_LAG (CVMX_ADD_IO_SEG(0x00010F0000868024ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_OFFSET CVMX_ENDOR_RFIF_TX_OFFSET_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_OFFSET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_OFFSET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008680D8ull);
}
#else
#define CVMX_ENDOR_RFIF_TX_OFFSET (CVMX_ADD_IO_SEG(0x00010F00008680D8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_OFFSET_ADJ_SCNT CVMX_ENDOR_RFIF_TX_OFFSET_ADJ_SCNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_OFFSET_ADJ_SCNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_OFFSET_ADJ_SCNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086810Cull);
}
#else
#define CVMX_ENDOR_RFIF_TX_OFFSET_ADJ_SCNT (CVMX_ADD_IO_SEG(0x00010F000086810Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_STATUS CVMX_ENDOR_RFIF_TX_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868008ull);
}
#else
#define CVMX_ENDOR_RFIF_TX_STATUS (CVMX_ADD_IO_SEG(0x00010F0000868008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_TX_TH CVMX_ENDOR_RFIF_TX_TH_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_TX_TH_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_TX_TH not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868414ull);
}
#else
#define CVMX_ENDOR_RFIF_TX_TH (CVMX_ADD_IO_SEG(0x00010F0000868414ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_WIN_EN CVMX_ENDOR_RFIF_WIN_EN_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_WIN_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_WIN_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000868040ull);
}
#else
#define CVMX_ENDOR_RFIF_WIN_EN (CVMX_ADD_IO_SEG(0x00010F0000868040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_WIN_UPD_SCNT CVMX_ENDOR_RFIF_WIN_UPD_SCNT_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_WIN_UPD_SCNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_WIN_UPD_SCNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000086803Cull);
}
#else
#define CVMX_ENDOR_RFIF_WIN_UPD_SCNT (CVMX_ADD_IO_SEG(0x00010F000086803Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_WR_TIMER64_LSB CVMX_ENDOR_RFIF_WR_TIMER64_LSB_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_WR_TIMER64_LSB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_WR_TIMER64_LSB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008681A4ull);
}
#else
#define CVMX_ENDOR_RFIF_WR_TIMER64_LSB (CVMX_ADD_IO_SEG(0x00010F00008681A4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RFIF_WR_TIMER64_MSB CVMX_ENDOR_RFIF_WR_TIMER64_MSB_FUNC()
static inline uint64_t CVMX_ENDOR_RFIF_WR_TIMER64_MSB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RFIF_WR_TIMER64_MSB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008681A8ull);
}
#else
#define CVMX_ENDOR_RFIF_WR_TIMER64_MSB (CVMX_ADD_IO_SEG(0x00010F00008681A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_CLKENB0_CLR CVMX_ENDOR_RSTCLK_CLKENB0_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_CLKENB0_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_CLKENB0_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844428ull);
}
#else
#define CVMX_ENDOR_RSTCLK_CLKENB0_CLR (CVMX_ADD_IO_SEG(0x00010F0000844428ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_CLKENB0_SET CVMX_ENDOR_RSTCLK_CLKENB0_SET_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_CLKENB0_SET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_CLKENB0_SET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844424ull);
}
#else
#define CVMX_ENDOR_RSTCLK_CLKENB0_SET (CVMX_ADD_IO_SEG(0x00010F0000844424ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_CLKENB0_STATE CVMX_ENDOR_RSTCLK_CLKENB0_STATE_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_CLKENB0_STATE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_CLKENB0_STATE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844420ull);
}
#else
#define CVMX_ENDOR_RSTCLK_CLKENB0_STATE (CVMX_ADD_IO_SEG(0x00010F0000844420ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_CLKENB1_CLR CVMX_ENDOR_RSTCLK_CLKENB1_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_CLKENB1_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_CLKENB1_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844438ull);
}
#else
#define CVMX_ENDOR_RSTCLK_CLKENB1_CLR (CVMX_ADD_IO_SEG(0x00010F0000844438ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_CLKENB1_SET CVMX_ENDOR_RSTCLK_CLKENB1_SET_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_CLKENB1_SET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_CLKENB1_SET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844434ull);
}
#else
#define CVMX_ENDOR_RSTCLK_CLKENB1_SET (CVMX_ADD_IO_SEG(0x00010F0000844434ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_CLKENB1_STATE CVMX_ENDOR_RSTCLK_CLKENB1_STATE_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_CLKENB1_STATE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_CLKENB1_STATE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844430ull);
}
#else
#define CVMX_ENDOR_RSTCLK_CLKENB1_STATE (CVMX_ADD_IO_SEG(0x00010F0000844430ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_DSPSTALL_CLR CVMX_ENDOR_RSTCLK_DSPSTALL_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_DSPSTALL_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_DSPSTALL_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844448ull);
}
#else
#define CVMX_ENDOR_RSTCLK_DSPSTALL_CLR (CVMX_ADD_IO_SEG(0x00010F0000844448ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_DSPSTALL_SET CVMX_ENDOR_RSTCLK_DSPSTALL_SET_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_DSPSTALL_SET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_DSPSTALL_SET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844444ull);
}
#else
#define CVMX_ENDOR_RSTCLK_DSPSTALL_SET (CVMX_ADD_IO_SEG(0x00010F0000844444ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_DSPSTALL_STATE CVMX_ENDOR_RSTCLK_DSPSTALL_STATE_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_DSPSTALL_STATE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_DSPSTALL_STATE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844440ull);
}
#else
#define CVMX_ENDOR_RSTCLK_DSPSTALL_STATE (CVMX_ADD_IO_SEG(0x00010F0000844440ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR0_CLRMASK CVMX_ENDOR_RSTCLK_INTR0_CLRMASK_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR0_CLRMASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR0_CLRMASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844598ull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR0_CLRMASK (CVMX_ADD_IO_SEG(0x00010F0000844598ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR0_MASK CVMX_ENDOR_RSTCLK_INTR0_MASK_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR0_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR0_MASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844590ull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR0_MASK (CVMX_ADD_IO_SEG(0x00010F0000844590ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR0_SETMASK CVMX_ENDOR_RSTCLK_INTR0_SETMASK_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR0_SETMASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR0_SETMASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844594ull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR0_SETMASK (CVMX_ADD_IO_SEG(0x00010F0000844594ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR0_STATUS CVMX_ENDOR_RSTCLK_INTR0_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR0_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR0_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F000084459Cull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR0_STATUS (CVMX_ADD_IO_SEG(0x00010F000084459Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR1_CLRMASK CVMX_ENDOR_RSTCLK_INTR1_CLRMASK_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR1_CLRMASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR1_CLRMASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008445A8ull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR1_CLRMASK (CVMX_ADD_IO_SEG(0x00010F00008445A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR1_MASK CVMX_ENDOR_RSTCLK_INTR1_MASK_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR1_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR1_MASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008445A0ull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR1_MASK (CVMX_ADD_IO_SEG(0x00010F00008445A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR1_SETMASK CVMX_ENDOR_RSTCLK_INTR1_SETMASK_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR1_SETMASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR1_SETMASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008445A4ull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR1_SETMASK (CVMX_ADD_IO_SEG(0x00010F00008445A4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_INTR1_STATUS CVMX_ENDOR_RSTCLK_INTR1_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_INTR1_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_INTR1_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008445ACull);
}
#else
#define CVMX_ENDOR_RSTCLK_INTR1_STATUS (CVMX_ADD_IO_SEG(0x00010F00008445ACull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_PHY_CONFIG CVMX_ENDOR_RSTCLK_PHY_CONFIG_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_PHY_CONFIG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_PHY_CONFIG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844450ull);
}
#else
#define CVMX_ENDOR_RSTCLK_PHY_CONFIG (CVMX_ADD_IO_SEG(0x00010F0000844450ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_PROC_MON CVMX_ENDOR_RSTCLK_PROC_MON_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_PROC_MON_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_PROC_MON not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008445B0ull);
}
#else
#define CVMX_ENDOR_RSTCLK_PROC_MON (CVMX_ADD_IO_SEG(0x00010F00008445B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_PROC_MON_COUNT CVMX_ENDOR_RSTCLK_PROC_MON_COUNT_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_PROC_MON_COUNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_PROC_MON_COUNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F00008445B4ull);
}
#else
#define CVMX_ENDOR_RSTCLK_PROC_MON_COUNT (CVMX_ADD_IO_SEG(0x00010F00008445B4ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_RESET0_CLR CVMX_ENDOR_RSTCLK_RESET0_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_RESET0_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_RESET0_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844408ull);
}
#else
#define CVMX_ENDOR_RSTCLK_RESET0_CLR (CVMX_ADD_IO_SEG(0x00010F0000844408ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_RESET0_SET CVMX_ENDOR_RSTCLK_RESET0_SET_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_RESET0_SET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_RESET0_SET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844404ull);
}
#else
#define CVMX_ENDOR_RSTCLK_RESET0_SET (CVMX_ADD_IO_SEG(0x00010F0000844404ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_RESET0_STATE CVMX_ENDOR_RSTCLK_RESET0_STATE_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_RESET0_STATE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_RESET0_STATE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844400ull);
}
#else
#define CVMX_ENDOR_RSTCLK_RESET0_STATE (CVMX_ADD_IO_SEG(0x00010F0000844400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_RESET1_CLR CVMX_ENDOR_RSTCLK_RESET1_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_RESET1_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_RESET1_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844418ull);
}
#else
#define CVMX_ENDOR_RSTCLK_RESET1_CLR (CVMX_ADD_IO_SEG(0x00010F0000844418ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_RESET1_SET CVMX_ENDOR_RSTCLK_RESET1_SET_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_RESET1_SET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_RESET1_SET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844414ull);
}
#else
#define CVMX_ENDOR_RSTCLK_RESET1_SET (CVMX_ADD_IO_SEG(0x00010F0000844414ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_RESET1_STATE CVMX_ENDOR_RSTCLK_RESET1_STATE_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_RESET1_STATE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_RESET1_STATE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844410ull);
}
#else
#define CVMX_ENDOR_RSTCLK_RESET1_STATE (CVMX_ADD_IO_SEG(0x00010F0000844410ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_SW_INTR_CLR CVMX_ENDOR_RSTCLK_SW_INTR_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_SW_INTR_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_SW_INTR_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844588ull);
}
#else
#define CVMX_ENDOR_RSTCLK_SW_INTR_CLR (CVMX_ADD_IO_SEG(0x00010F0000844588ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_SW_INTR_SET CVMX_ENDOR_RSTCLK_SW_INTR_SET_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_SW_INTR_SET_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_SW_INTR_SET not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844584ull);
}
#else
#define CVMX_ENDOR_RSTCLK_SW_INTR_SET (CVMX_ADD_IO_SEG(0x00010F0000844584ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_SW_INTR_STATUS CVMX_ENDOR_RSTCLK_SW_INTR_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_SW_INTR_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_SW_INTR_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844580ull);
}
#else
#define CVMX_ENDOR_RSTCLK_SW_INTR_STATUS (CVMX_ADD_IO_SEG(0x00010F0000844580ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_TIMER_CTL CVMX_ENDOR_RSTCLK_TIMER_CTL_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_TIMER_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_TIMER_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844500ull);
}
#else
#define CVMX_ENDOR_RSTCLK_TIMER_CTL (CVMX_ADD_IO_SEG(0x00010F0000844500ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_TIMER_INTR_CLR CVMX_ENDOR_RSTCLK_TIMER_INTR_CLR_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_TIMER_INTR_CLR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_TIMER_INTR_CLR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844534ull);
}
#else
#define CVMX_ENDOR_RSTCLK_TIMER_INTR_CLR (CVMX_ADD_IO_SEG(0x00010F0000844534ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_TIMER_INTR_STATUS CVMX_ENDOR_RSTCLK_TIMER_INTR_STATUS_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_TIMER_INTR_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_TIMER_INTR_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844530ull);
}
#else
#define CVMX_ENDOR_RSTCLK_TIMER_INTR_STATUS (CVMX_ADD_IO_SEG(0x00010F0000844530ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_TIMER_MAX CVMX_ENDOR_RSTCLK_TIMER_MAX_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_TIMER_MAX_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_TIMER_MAX not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844508ull);
}
#else
#define CVMX_ENDOR_RSTCLK_TIMER_MAX (CVMX_ADD_IO_SEG(0x00010F0000844508ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_TIMER_VALUE CVMX_ENDOR_RSTCLK_TIMER_VALUE_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_TIMER_VALUE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_TIMER_VALUE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844504ull);
}
#else
#define CVMX_ENDOR_RSTCLK_TIMER_VALUE (CVMX_ADD_IO_SEG(0x00010F0000844504ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ENDOR_RSTCLK_TIMEX_THRD(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ENDOR_RSTCLK_TIMEX_THRD(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010F0000844510ull) + ((offset) & 7) * 4;
}
#else
#define CVMX_ENDOR_RSTCLK_TIMEX_THRD(offset) (CVMX_ADD_IO_SEG(0x00010F0000844510ull) + ((offset) & 7) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENDOR_RSTCLK_VERSION CVMX_ENDOR_RSTCLK_VERSION_FUNC()
static inline uint64_t CVMX_ENDOR_RSTCLK_VERSION_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_ENDOR_RSTCLK_VERSION not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00010F0000844570ull);
}
#else
#define CVMX_ENDOR_RSTCLK_VERSION (CVMX_ADD_IO_SEG(0x00010F0000844570ull))
#endif

/**
 * cvmx_endor_adma_auto_clk_gate
 */
union cvmx_endor_adma_auto_clk_gate {
	uint32_t u32;
	struct cvmx_endor_adma_auto_clk_gate_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t auto_gate                    : 1;  /**< 1==enable auto-clock-gating */
#else
	uint32_t auto_gate                    : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_adma_auto_clk_gate_s cnf71xx;
};
typedef union cvmx_endor_adma_auto_clk_gate cvmx_endor_adma_auto_clk_gate_t;

/**
 * cvmx_endor_adma_axi_rspcode
 */
union cvmx_endor_adma_axi_rspcode {
	uint32_t u32;
	struct cvmx_endor_adma_axi_rspcode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t ch7_axi_rspcode              : 2;  /**< dma \#7 AXI response code */
	uint32_t ch6_axi_rspcode              : 2;  /**< dma \#6 AXI response code */
	uint32_t ch5_axi_rspcode              : 2;  /**< dma \#5 AXI response code */
	uint32_t ch4_axi_rspcode              : 2;  /**< dma \#4 AXI response code */
	uint32_t ch3_axi_rspcode              : 2;  /**< dma \#3 AXI response code */
	uint32_t ch2_axi_rspcode              : 2;  /**< dma \#2 AXI response code */
	uint32_t ch1_axi_rspcode              : 2;  /**< dma \#1 AXI response code */
	uint32_t ch0_axi_rspcode              : 2;  /**< dma \#0 AXI response code */
#else
	uint32_t ch0_axi_rspcode              : 2;
	uint32_t ch1_axi_rspcode              : 2;
	uint32_t ch2_axi_rspcode              : 2;
	uint32_t ch3_axi_rspcode              : 2;
	uint32_t ch4_axi_rspcode              : 2;
	uint32_t ch5_axi_rspcode              : 2;
	uint32_t ch6_axi_rspcode              : 2;
	uint32_t ch7_axi_rspcode              : 2;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_endor_adma_axi_rspcode_s  cnf71xx;
};
typedef union cvmx_endor_adma_axi_rspcode cvmx_endor_adma_axi_rspcode_t;

/**
 * cvmx_endor_adma_axi_signal
 */
union cvmx_endor_adma_axi_signal {
	uint32_t u32;
	struct cvmx_endor_adma_axi_signal_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t awcobuf                      : 1;  /**< ADMA_COBUF */
	uint32_t reserved_10_23               : 14;
	uint32_t awlock                       : 2;  /**< ADMA_AWLOCK */
	uint32_t reserved_2_7                 : 6;
	uint32_t arlock                       : 2;  /**< ADMA_ARLOCK */
#else
	uint32_t arlock                       : 2;
	uint32_t reserved_2_7                 : 6;
	uint32_t awlock                       : 2;
	uint32_t reserved_10_23               : 14;
	uint32_t awcobuf                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_endor_adma_axi_signal_s   cnf71xx;
};
typedef union cvmx_endor_adma_axi_signal cvmx_endor_adma_axi_signal_t;

/**
 * cvmx_endor_adma_axierr_intr
 */
union cvmx_endor_adma_axierr_intr {
	uint32_t u32;
	struct cvmx_endor_adma_axierr_intr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t axi_err_int                  : 1;  /**< AXI Error interrupt */
#else
	uint32_t axi_err_int                  : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_adma_axierr_intr_s  cnf71xx;
};
typedef union cvmx_endor_adma_axierr_intr cvmx_endor_adma_axierr_intr_t;

/**
 * cvmx_endor_adma_dma#_addr_hi
 */
union cvmx_endor_adma_dmax_addr_hi {
	uint32_t u32;
	struct cvmx_endor_adma_dmax_addr_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t hi_addr                      : 8;  /**< dma low address[63:32] */
#else
	uint32_t hi_addr                      : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_adma_dmax_addr_hi_s cnf71xx;
};
typedef union cvmx_endor_adma_dmax_addr_hi cvmx_endor_adma_dmax_addr_hi_t;

/**
 * cvmx_endor_adma_dma#_addr_lo
 */
union cvmx_endor_adma_dmax_addr_lo {
	uint32_t u32;
	struct cvmx_endor_adma_dmax_addr_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lo_addr                      : 32; /**< dma low address[31:0] */
#else
	uint32_t lo_addr                      : 32;
#endif
	} s;
	struct cvmx_endor_adma_dmax_addr_lo_s cnf71xx;
};
typedef union cvmx_endor_adma_dmax_addr_lo cvmx_endor_adma_dmax_addr_lo_t;

/**
 * cvmx_endor_adma_dma#_cfg
 */
union cvmx_endor_adma_dmax_cfg {
	uint32_t u32;
	struct cvmx_endor_adma_dmax_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t endian                       : 1;  /**< 0==byte-swap, 1==word */
	uint32_t reserved_18_23               : 6;
	uint32_t hmm_ofs                      : 2;  /**< HMM memory byte offset */
	uint32_t reserved_13_15               : 3;
	uint32_t awcache_lbm                  : 1;  /**< AWCACHE last burst mode, 1==force 0 on the last write data */
	uint32_t awcache                      : 4;  /**< ADMA_AWCACHE */
	uint32_t reserved_6_7                 : 2;
	uint32_t bst_bound                    : 1;  /**< burst boundary (0==4kB, 1==128 byte) */
	uint32_t max_bstlen                   : 1;  /**< maximum burst length(0==8 dword) */
	uint32_t reserved_1_3                 : 3;
	uint32_t enable                       : 1;  /**< 1 == dma enable */
#else
	uint32_t enable                       : 1;
	uint32_t reserved_1_3                 : 3;
	uint32_t max_bstlen                   : 1;
	uint32_t bst_bound                    : 1;
	uint32_t reserved_6_7                 : 2;
	uint32_t awcache                      : 4;
	uint32_t awcache_lbm                  : 1;
	uint32_t reserved_13_15               : 3;
	uint32_t hmm_ofs                      : 2;
	uint32_t reserved_18_23               : 6;
	uint32_t endian                       : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_endor_adma_dmax_cfg_s     cnf71xx;
};
typedef union cvmx_endor_adma_dmax_cfg cvmx_endor_adma_dmax_cfg_t;

/**
 * cvmx_endor_adma_dma#_size
 */
union cvmx_endor_adma_dmax_size {
	uint32_t u32;
	struct cvmx_endor_adma_dmax_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t dma_size                     : 18; /**< dma transfer byte size */
#else
	uint32_t dma_size                     : 18;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_endor_adma_dmax_size_s    cnf71xx;
};
typedef union cvmx_endor_adma_dmax_size cvmx_endor_adma_dmax_size_t;

/**
 * cvmx_endor_adma_dma_priority
 */
union cvmx_endor_adma_dma_priority {
	uint32_t u32;
	struct cvmx_endor_adma_dma_priority_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t rdma_rr_prty                 : 1;  /**< 1 == round-robin for DMA read channel */
	uint32_t wdma_rr_prty                 : 1;  /**< 1 == round-robin for DMA write channel */
	uint32_t wdma_fix_prty                : 4;  /**< dma fixed priority */
#else
	uint32_t wdma_fix_prty                : 4;
	uint32_t wdma_rr_prty                 : 1;
	uint32_t rdma_rr_prty                 : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_adma_dma_priority_s cnf71xx;
};
typedef union cvmx_endor_adma_dma_priority cvmx_endor_adma_dma_priority_t;

/**
 * cvmx_endor_adma_dma_reset
 */
union cvmx_endor_adma_dma_reset {
	uint32_t u32;
	struct cvmx_endor_adma_dma_reset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t dma_ch_reset                 : 8;  /**< dma channel reset */
#else
	uint32_t dma_ch_reset                 : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_adma_dma_reset_s    cnf71xx;
};
typedef union cvmx_endor_adma_dma_reset cvmx_endor_adma_dma_reset_t;

/**
 * cvmx_endor_adma_dmadone_intr
 */
union cvmx_endor_adma_dmadone_intr {
	uint32_t u32;
	struct cvmx_endor_adma_dmadone_intr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t dma_ch_done                  : 8;  /**< done-interrupt status of the DMA channel */
#else
	uint32_t dma_ch_done                  : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_adma_dmadone_intr_s cnf71xx;
};
typedef union cvmx_endor_adma_dmadone_intr cvmx_endor_adma_dmadone_intr_t;

/**
 * cvmx_endor_adma_intr_dis
 */
union cvmx_endor_adma_intr_dis {
	uint32_t u32;
	struct cvmx_endor_adma_intr_dis_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_17_31               : 15;
	uint32_t axierr_intr_dis              : 1;  /**< AXI Error interrupt disable (1==enable) */
	uint32_t dmadone_intr_dis             : 16; /**< dma done interrupt disable (1==enable) */
#else
	uint32_t dmadone_intr_dis             : 16;
	uint32_t axierr_intr_dis              : 1;
	uint32_t reserved_17_31               : 15;
#endif
	} s;
	struct cvmx_endor_adma_intr_dis_s     cnf71xx;
};
typedef union cvmx_endor_adma_intr_dis cvmx_endor_adma_intr_dis_t;

/**
 * cvmx_endor_adma_intr_enb
 */
union cvmx_endor_adma_intr_enb {
	uint32_t u32;
	struct cvmx_endor_adma_intr_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_17_31               : 15;
	uint32_t axierr_intr_enb              : 1;  /**< AXI Error interrupt enable (1==enable) */
	uint32_t dmadone_intr_enb             : 16; /**< dma done interrupt enable (1==enable) */
#else
	uint32_t dmadone_intr_enb             : 16;
	uint32_t axierr_intr_enb              : 1;
	uint32_t reserved_17_31               : 15;
#endif
	} s;
	struct cvmx_endor_adma_intr_enb_s     cnf71xx;
};
typedef union cvmx_endor_adma_intr_enb cvmx_endor_adma_intr_enb_t;

/**
 * cvmx_endor_adma_module_status
 */
union cvmx_endor_adma_module_status {
	uint32_t u32;
	struct cvmx_endor_adma_module_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t non_dmardch_stt              : 1;  /**< non-DMA read channel status */
	uint32_t non_dmawrch_stt              : 1;  /**< non-DMA write channel status (1==transfer in progress) */
	uint32_t dma_ch_stt                   : 14; /**< dma channel status (1==transfer in progress)
                                                         blah, blah */
#else
	uint32_t dma_ch_stt                   : 14;
	uint32_t non_dmawrch_stt              : 1;
	uint32_t non_dmardch_stt              : 1;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_endor_adma_module_status_s cnf71xx;
};
typedef union cvmx_endor_adma_module_status cvmx_endor_adma_module_status_t;

/**
 * cvmx_endor_intc_cntl_hi#
 *
 * ENDOR_INTC_CNTL_HI - Interrupt Enable HI
 *
 */
union cvmx_endor_intc_cntl_hix {
	uint32_t u32;
	struct cvmx_endor_intc_cntl_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t enab                         : 1;  /**< Interrupt Enable */
#else
	uint32_t enab                         : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_intc_cntl_hix_s     cnf71xx;
};
typedef union cvmx_endor_intc_cntl_hix cvmx_endor_intc_cntl_hix_t;

/**
 * cvmx_endor_intc_cntl_lo#
 *
 * ENDOR_INTC_CNTL_LO - Interrupt Enable LO
 *
 */
union cvmx_endor_intc_cntl_lox {
	uint32_t u32;
	struct cvmx_endor_intc_cntl_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t enab                         : 1;  /**< Interrupt Enable */
#else
	uint32_t enab                         : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_intc_cntl_lox_s     cnf71xx;
};
typedef union cvmx_endor_intc_cntl_lox cvmx_endor_intc_cntl_lox_t;

/**
 * cvmx_endor_intc_index_hi#
 *
 * ENDOR_INTC_INDEX_HI - Overall Index HI
 *
 */
union cvmx_endor_intc_index_hix {
	uint32_t u32;
	struct cvmx_endor_intc_index_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t index                        : 9;  /**< Overall Interrup Index */
#else
	uint32_t index                        : 9;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_endor_intc_index_hix_s    cnf71xx;
};
typedef union cvmx_endor_intc_index_hix cvmx_endor_intc_index_hix_t;

/**
 * cvmx_endor_intc_index_lo#
 *
 * ENDOR_INTC_INDEX_LO - Overall Index LO
 *
 */
union cvmx_endor_intc_index_lox {
	uint32_t u32;
	struct cvmx_endor_intc_index_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t index                        : 9;  /**< Overall Interrup Index */
#else
	uint32_t index                        : 9;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_endor_intc_index_lox_s    cnf71xx;
};
typedef union cvmx_endor_intc_index_lox cvmx_endor_intc_index_lox_t;

/**
 * cvmx_endor_intc_misc_idx_hi#
 *
 * ENDOR_INTC_MISC_IDX_HI - Misc Group Index HI
 *
 */
union cvmx_endor_intc_misc_idx_hix {
	uint32_t u32;
	struct cvmx_endor_intc_misc_idx_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Misc Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_misc_idx_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_misc_idx_hix cvmx_endor_intc_misc_idx_hix_t;

/**
 * cvmx_endor_intc_misc_idx_lo#
 *
 * ENDOR_INTC_MISC_IDX_LO - Misc Group Index LO
 *
 */
union cvmx_endor_intc_misc_idx_lox {
	uint32_t u32;
	struct cvmx_endor_intc_misc_idx_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Misc Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_misc_idx_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_misc_idx_lox cvmx_endor_intc_misc_idx_lox_t;

/**
 * cvmx_endor_intc_misc_mask_hi#
 *
 * ENDOR_INTC_MISC_MASK_HI = Interrupt MISC Group Mask
 *
 */
union cvmx_endor_intc_misc_mask_hix {
	uint32_t u32;
	struct cvmx_endor_intc_misc_mask_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rf_rx_ppssync                : 1;  /**< RX PPS Sync Done */
	uint32_t rf_rx_spiskip                : 1;  /**< RX SPI Event Skipped */
	uint32_t rf_spi3                      : 1;  /**< SPI Transfer Done Event 3 */
	uint32_t rf_spi2                      : 1;  /**< SPI Transfer Done Event 2 */
	uint32_t rf_spi1                      : 1;  /**< SPI Transfer Done Event 1 */
	uint32_t rf_spi0                      : 1;  /**< SPI Transfer Done Event 0 */
	uint32_t rf_rx_strx                   : 1;  /**< RX Start RX */
	uint32_t rf_rx_stframe                : 1;  /**< RX Start Frame */
	uint32_t rf_rxd_ffflag                : 1;  /**< RX DIV FIFO flags asserted */
	uint32_t rf_rxd_ffthresh              : 1;  /**< RX DIV FIFO Threshhold reached */
	uint32_t rf_rx_ffflag                 : 1;  /**< RX FIFO flags asserted */
	uint32_t rf_rx_ffthresh               : 1;  /**< RX FIFO Threshhold reached */
	uint32_t tti_timer                    : 8;  /**< TTI Timer Interrupt */
	uint32_t axi_berr                     : 1;  /**< AXI Bus Error */
	uint32_t rfspi                        : 1;  /**< RFSPI Interrupt */
	uint32_t ifftpapr                     : 1;  /**< IFFTPAPR HAB Interrupt */
	uint32_t h3genc                       : 1;  /**< 3G Encoder HAB Interrupt */
	uint32_t lteenc                       : 1;  /**< LTE Encoder HAB Interrupt */
	uint32_t vdec                         : 1;  /**< Viterbi Decoder HAB Interrupt */
	uint32_t turbo_rddone                 : 1;  /**< TURBO Decoder HAB Read Done */
	uint32_t turbo_done                   : 1;  /**< TURBO Decoder HAB Done */
	uint32_t turbo                        : 1;  /**< TURBO Decoder HAB Interrupt */
	uint32_t dftdmp                       : 1;  /**< DFTDMP HAB Interrupt */
	uint32_t rach                         : 1;  /**< RACH HAB Interrupt */
	uint32_t ulfe                         : 1;  /**< ULFE HAB Interrupt */
#else
	uint32_t ulfe                         : 1;
	uint32_t rach                         : 1;
	uint32_t dftdmp                       : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_done                   : 1;
	uint32_t turbo_rddone                 : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t h3genc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t axi_berr                     : 1;
	uint32_t tti_timer                    : 8;
	uint32_t rf_rx_ffthresh               : 1;
	uint32_t rf_rx_ffflag                 : 1;
	uint32_t rf_rxd_ffthresh              : 1;
	uint32_t rf_rxd_ffflag                : 1;
	uint32_t rf_rx_stframe                : 1;
	uint32_t rf_rx_strx                   : 1;
	uint32_t rf_spi0                      : 1;
	uint32_t rf_spi1                      : 1;
	uint32_t rf_spi2                      : 1;
	uint32_t rf_spi3                      : 1;
	uint32_t rf_rx_spiskip                : 1;
	uint32_t rf_rx_ppssync                : 1;
#endif
	} s;
	struct cvmx_endor_intc_misc_mask_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_misc_mask_hix cvmx_endor_intc_misc_mask_hix_t;

/**
 * cvmx_endor_intc_misc_mask_lo#
 *
 * ENDOR_INTC_MISC_MASK_LO = Interrupt MISC Group Mask
 *
 */
union cvmx_endor_intc_misc_mask_lox {
	uint32_t u32;
	struct cvmx_endor_intc_misc_mask_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rf_rx_ppssync                : 1;  /**< RX PPS Sync Done */
	uint32_t rf_rx_spiskip                : 1;  /**< RX SPI Event Skipped */
	uint32_t rf_spi3                      : 1;  /**< SPI Transfer Done Event 3 */
	uint32_t rf_spi2                      : 1;  /**< SPI Transfer Done Event 2 */
	uint32_t rf_spi1                      : 1;  /**< SPI Transfer Done Event 1 */
	uint32_t rf_spi0                      : 1;  /**< SPI Transfer Done Event 0 */
	uint32_t rf_rx_strx                   : 1;  /**< RX Start RX */
	uint32_t rf_rx_stframe                : 1;  /**< RX Start Frame */
	uint32_t rf_rxd_ffflag                : 1;  /**< RX DIV FIFO flags asserted */
	uint32_t rf_rxd_ffthresh              : 1;  /**< RX DIV FIFO Threshhold reached */
	uint32_t rf_rx_ffflag                 : 1;  /**< RX FIFO flags asserted */
	uint32_t rf_rx_ffthresh               : 1;  /**< RX FIFO Threshhold reached */
	uint32_t tti_timer                    : 8;  /**< TTI Timer Interrupt */
	uint32_t axi_berr                     : 1;  /**< AXI Bus Error */
	uint32_t rfspi                        : 1;  /**< RFSPI Interrupt */
	uint32_t ifftpapr                     : 1;  /**< IFFTPAPR HAB Interrupt */
	uint32_t h3genc                       : 1;  /**< 3G Encoder HAB Interrupt */
	uint32_t lteenc                       : 1;  /**< LTE Encoder HAB Interrupt */
	uint32_t vdec                         : 1;  /**< Viterbi Decoder HAB Interrupt */
	uint32_t turbo_rddone                 : 1;  /**< TURBO Decoder HAB Read Done */
	uint32_t turbo_done                   : 1;  /**< TURBO Decoder HAB Done */
	uint32_t turbo                        : 1;  /**< TURBO Decoder HAB Interrupt */
	uint32_t dftdmp                       : 1;  /**< DFTDMP HAB Interrupt */
	uint32_t rach                         : 1;  /**< RACH HAB Interrupt */
	uint32_t ulfe                         : 1;  /**< ULFE HAB Interrupt */
#else
	uint32_t ulfe                         : 1;
	uint32_t rach                         : 1;
	uint32_t dftdmp                       : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_done                   : 1;
	uint32_t turbo_rddone                 : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t h3genc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t axi_berr                     : 1;
	uint32_t tti_timer                    : 8;
	uint32_t rf_rx_ffthresh               : 1;
	uint32_t rf_rx_ffflag                 : 1;
	uint32_t rf_rxd_ffthresh              : 1;
	uint32_t rf_rxd_ffflag                : 1;
	uint32_t rf_rx_stframe                : 1;
	uint32_t rf_rx_strx                   : 1;
	uint32_t rf_spi0                      : 1;
	uint32_t rf_spi1                      : 1;
	uint32_t rf_spi2                      : 1;
	uint32_t rf_spi3                      : 1;
	uint32_t rf_rx_spiskip                : 1;
	uint32_t rf_rx_ppssync                : 1;
#endif
	} s;
	struct cvmx_endor_intc_misc_mask_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_misc_mask_lox cvmx_endor_intc_misc_mask_lox_t;

/**
 * cvmx_endor_intc_misc_rint
 *
 * ENDOR_INTC_MISC_RINT - MISC Raw Interrupt Status
 *
 */
union cvmx_endor_intc_misc_rint {
	uint32_t u32;
	struct cvmx_endor_intc_misc_rint_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rf_rx_ppssync                : 1;  /**< RX PPS Sync Done */
	uint32_t rf_rx_spiskip                : 1;  /**< RX SPI Event Skipped */
	uint32_t rf_spi3                      : 1;  /**< SPI Transfer Done Event 3 */
	uint32_t rf_spi2                      : 1;  /**< SPI Transfer Done Event 2 */
	uint32_t rf_spi1                      : 1;  /**< SPI Transfer Done Event 1 */
	uint32_t rf_spi0                      : 1;  /**< SPI Transfer Done Event 0 */
	uint32_t rf_rx_strx                   : 1;  /**< RX Start RX */
	uint32_t rf_rx_stframe                : 1;  /**< RX Start Frame */
	uint32_t rf_rxd_ffflag                : 1;  /**< RX DIV FIFO flags asserted */
	uint32_t rf_rxd_ffthresh              : 1;  /**< RX DIV FIFO Threshhold reached */
	uint32_t rf_rx_ffflag                 : 1;  /**< RX FIFO flags asserted */
	uint32_t rf_rx_ffthresh               : 1;  /**< RX FIFO Threshhold reached */
	uint32_t tti_timer                    : 8;  /**< TTI Timer Interrupt */
	uint32_t axi_berr                     : 1;  /**< AXI Bus Error */
	uint32_t rfspi                        : 1;  /**< RFSPI Interrupt */
	uint32_t ifftpapr                     : 1;  /**< IFFTPAPR HAB Interrupt */
	uint32_t h3genc                       : 1;  /**< 3G Encoder HAB Interrupt */
	uint32_t lteenc                       : 1;  /**< LTE Encoder HAB Interrupt */
	uint32_t vdec                         : 1;  /**< Viterbi Decoder HAB Interrupt */
	uint32_t turbo_rddone                 : 1;  /**< TURBO Decoder HAB Read Done */
	uint32_t turbo_done                   : 1;  /**< TURBO Decoder HAB Done */
	uint32_t turbo                        : 1;  /**< TURBO Decoder HAB Interrupt */
	uint32_t dftdmp                       : 1;  /**< DFTDMP HAB Interrupt */
	uint32_t rach                         : 1;  /**< RACH HAB Interrupt */
	uint32_t ulfe                         : 1;  /**< ULFE HAB Interrupt */
#else
	uint32_t ulfe                         : 1;
	uint32_t rach                         : 1;
	uint32_t dftdmp                       : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_done                   : 1;
	uint32_t turbo_rddone                 : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t h3genc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t axi_berr                     : 1;
	uint32_t tti_timer                    : 8;
	uint32_t rf_rx_ffthresh               : 1;
	uint32_t rf_rx_ffflag                 : 1;
	uint32_t rf_rxd_ffthresh              : 1;
	uint32_t rf_rxd_ffflag                : 1;
	uint32_t rf_rx_stframe                : 1;
	uint32_t rf_rx_strx                   : 1;
	uint32_t rf_spi0                      : 1;
	uint32_t rf_spi1                      : 1;
	uint32_t rf_spi2                      : 1;
	uint32_t rf_spi3                      : 1;
	uint32_t rf_rx_spiskip                : 1;
	uint32_t rf_rx_ppssync                : 1;
#endif
	} s;
	struct cvmx_endor_intc_misc_rint_s    cnf71xx;
};
typedef union cvmx_endor_intc_misc_rint cvmx_endor_intc_misc_rint_t;

/**
 * cvmx_endor_intc_misc_status_hi#
 *
 * ENDOR_INTC_MISC_STATUS_HI = Interrupt MISC Group Mask
 *
 */
union cvmx_endor_intc_misc_status_hix {
	uint32_t u32;
	struct cvmx_endor_intc_misc_status_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rf_rx_ppssync                : 1;  /**< RX PPS Sync Done */
	uint32_t rf_rx_spiskip                : 1;  /**< RX SPI Event Skipped */
	uint32_t rf_spi3                      : 1;  /**< SPI Transfer Done Event 3 */
	uint32_t rf_spi2                      : 1;  /**< SPI Transfer Done Event 2 */
	uint32_t rf_spi1                      : 1;  /**< SPI Transfer Done Event 1 */
	uint32_t rf_spi0                      : 1;  /**< SPI Transfer Done Event 0 */
	uint32_t rf_rx_strx                   : 1;  /**< RX Start RX */
	uint32_t rf_rx_stframe                : 1;  /**< RX Start Frame */
	uint32_t rf_rxd_ffflag                : 1;  /**< RX DIV FIFO flags asserted */
	uint32_t rf_rxd_ffthresh              : 1;  /**< RX DIV FIFO Threshhold reached */
	uint32_t rf_rx_ffflag                 : 1;  /**< RX FIFO flags asserted */
	uint32_t rf_rx_ffthresh               : 1;  /**< RX FIFO Threshhold reached */
	uint32_t tti_timer                    : 8;  /**< TTI Timer Interrupt */
	uint32_t axi_berr                     : 1;  /**< AXI Bus Error */
	uint32_t rfspi                        : 1;  /**< RFSPI Interrupt */
	uint32_t ifftpapr                     : 1;  /**< IFFTPAPR HAB Interrupt */
	uint32_t h3genc                       : 1;  /**< 3G Encoder HAB Interrupt */
	uint32_t lteenc                       : 1;  /**< LTE Encoder HAB Interrupt */
	uint32_t vdec                         : 1;  /**< Viterbi Decoder HAB Interrupt */
	uint32_t turbo_rddone                 : 1;  /**< TURBO Decoder HAB Read Done */
	uint32_t turbo_done                   : 1;  /**< TURBO Decoder HAB Done */
	uint32_t turbo                        : 1;  /**< TURBO Decoder HAB Interrupt */
	uint32_t dftdmp                       : 1;  /**< DFTDMP HAB Interrupt */
	uint32_t rach                         : 1;  /**< RACH HAB Interrupt */
	uint32_t ulfe                         : 1;  /**< ULFE HAB Interrupt */
#else
	uint32_t ulfe                         : 1;
	uint32_t rach                         : 1;
	uint32_t dftdmp                       : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_done                   : 1;
	uint32_t turbo_rddone                 : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t h3genc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t axi_berr                     : 1;
	uint32_t tti_timer                    : 8;
	uint32_t rf_rx_ffthresh               : 1;
	uint32_t rf_rx_ffflag                 : 1;
	uint32_t rf_rxd_ffthresh              : 1;
	uint32_t rf_rxd_ffflag                : 1;
	uint32_t rf_rx_stframe                : 1;
	uint32_t rf_rx_strx                   : 1;
	uint32_t rf_spi0                      : 1;
	uint32_t rf_spi1                      : 1;
	uint32_t rf_spi2                      : 1;
	uint32_t rf_spi3                      : 1;
	uint32_t rf_rx_spiskip                : 1;
	uint32_t rf_rx_ppssync                : 1;
#endif
	} s;
	struct cvmx_endor_intc_misc_status_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_misc_status_hix cvmx_endor_intc_misc_status_hix_t;

/**
 * cvmx_endor_intc_misc_status_lo#
 *
 * ENDOR_INTC_MISC_STATUS_LO = Interrupt MISC Group Mask
 *
 */
union cvmx_endor_intc_misc_status_lox {
	uint32_t u32;
	struct cvmx_endor_intc_misc_status_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rf_rx_ppssync                : 1;  /**< RX PPS Sync Done */
	uint32_t rf_rx_spiskip                : 1;  /**< RX SPI Event Skipped */
	uint32_t rf_spi3                      : 1;  /**< SPI Transfer Done Event 3 */
	uint32_t rf_spi2                      : 1;  /**< SPI Transfer Done Event 2 */
	uint32_t rf_spi1                      : 1;  /**< SPI Transfer Done Event 1 */
	uint32_t rf_spi0                      : 1;  /**< SPI Transfer Done Event 0 */
	uint32_t rf_rx_strx                   : 1;  /**< RX Start RX */
	uint32_t rf_rx_stframe                : 1;  /**< RX Start Frame */
	uint32_t rf_rxd_ffflag                : 1;  /**< RX DIV FIFO flags asserted */
	uint32_t rf_rxd_ffthresh              : 1;  /**< RX DIV FIFO Threshhold reached */
	uint32_t rf_rx_ffflag                 : 1;  /**< RX FIFO flags asserted */
	uint32_t rf_rx_ffthresh               : 1;  /**< RX FIFO Threshhold reached */
	uint32_t tti_timer                    : 8;  /**< TTI Timer Interrupt */
	uint32_t axi_berr                     : 1;  /**< AXI Bus Error */
	uint32_t rfspi                        : 1;  /**< RFSPI Interrupt */
	uint32_t ifftpapr                     : 1;  /**< IFFTPAPR HAB Interrupt */
	uint32_t h3genc                       : 1;  /**< 3G Encoder HAB Interrupt */
	uint32_t lteenc                       : 1;  /**< LTE Encoder HAB Interrupt */
	uint32_t vdec                         : 1;  /**< Viterbi Decoder HAB Interrupt */
	uint32_t turbo_rddone                 : 1;  /**< TURBO Decoder HAB Read Done */
	uint32_t turbo_done                   : 1;  /**< TURBO Decoder HAB Done */
	uint32_t turbo                        : 1;  /**< TURBO Decoder HAB Interrupt */
	uint32_t dftdmp                       : 1;  /**< DFTDMP HAB Interrupt */
	uint32_t rach                         : 1;  /**< RACH HAB Interrupt */
	uint32_t ulfe                         : 1;  /**< ULFE HAB Interrupt */
#else
	uint32_t ulfe                         : 1;
	uint32_t rach                         : 1;
	uint32_t dftdmp                       : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_done                   : 1;
	uint32_t turbo_rddone                 : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t h3genc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t axi_berr                     : 1;
	uint32_t tti_timer                    : 8;
	uint32_t rf_rx_ffthresh               : 1;
	uint32_t rf_rx_ffflag                 : 1;
	uint32_t rf_rxd_ffthresh              : 1;
	uint32_t rf_rxd_ffflag                : 1;
	uint32_t rf_rx_stframe                : 1;
	uint32_t rf_rx_strx                   : 1;
	uint32_t rf_spi0                      : 1;
	uint32_t rf_spi1                      : 1;
	uint32_t rf_spi2                      : 1;
	uint32_t rf_spi3                      : 1;
	uint32_t rf_rx_spiskip                : 1;
	uint32_t rf_rx_ppssync                : 1;
#endif
	} s;
	struct cvmx_endor_intc_misc_status_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_misc_status_lox cvmx_endor_intc_misc_status_lox_t;

/**
 * cvmx_endor_intc_rd_idx_hi#
 *
 * ENDOR_INTC_RD_IDX_HI - Read Done Group Index HI
 *
 */
union cvmx_endor_intc_rd_idx_hix {
	uint32_t u32;
	struct cvmx_endor_intc_rd_idx_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Read Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_rd_idx_hix_s   cnf71xx;
};
typedef union cvmx_endor_intc_rd_idx_hix cvmx_endor_intc_rd_idx_hix_t;

/**
 * cvmx_endor_intc_rd_idx_lo#
 *
 * ENDOR_INTC_RD_IDX_LO - Read Done Group Index LO
 *
 */
union cvmx_endor_intc_rd_idx_lox {
	uint32_t u32;
	struct cvmx_endor_intc_rd_idx_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Read Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_rd_idx_lox_s   cnf71xx;
};
typedef union cvmx_endor_intc_rd_idx_lox cvmx_endor_intc_rd_idx_lox_t;

/**
 * cvmx_endor_intc_rd_mask_hi#
 *
 * ENDOR_INTC_RD_MASK_HI = Interrupt Read Done Group Mask
 *
 */
union cvmx_endor_intc_rd_mask_hix {
	uint32_t u32;
	struct cvmx_endor_intc_rd_mask_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rd_mask_hix_s  cnf71xx;
};
typedef union cvmx_endor_intc_rd_mask_hix cvmx_endor_intc_rd_mask_hix_t;

/**
 * cvmx_endor_intc_rd_mask_lo#
 *
 * ENDOR_INTC_RD_MASK_LO = Interrupt Read Done Group Mask
 *
 */
union cvmx_endor_intc_rd_mask_lox {
	uint32_t u32;
	struct cvmx_endor_intc_rd_mask_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rd_mask_lox_s  cnf71xx;
};
typedef union cvmx_endor_intc_rd_mask_lox cvmx_endor_intc_rd_mask_lox_t;

/**
 * cvmx_endor_intc_rd_rint
 *
 * ENDOR_INTC_RD_RINT - Read Done Group Raw Interrupt Status
 *
 */
union cvmx_endor_intc_rd_rint {
	uint32_t u32;
	struct cvmx_endor_intc_rd_rint_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rd_rint_s      cnf71xx;
};
typedef union cvmx_endor_intc_rd_rint cvmx_endor_intc_rd_rint_t;

/**
 * cvmx_endor_intc_rd_status_hi#
 *
 * ENDOR_INTC_RD_STATUS_HI = Interrupt Read Done Group Mask
 *
 */
union cvmx_endor_intc_rd_status_hix {
	uint32_t u32;
	struct cvmx_endor_intc_rd_status_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rd_status_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_rd_status_hix cvmx_endor_intc_rd_status_hix_t;

/**
 * cvmx_endor_intc_rd_status_lo#
 *
 * ENDOR_INTC_RD_STATUS_LO = Interrupt Read Done Group Mask
 *
 */
union cvmx_endor_intc_rd_status_lox {
	uint32_t u32;
	struct cvmx_endor_intc_rd_status_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rd_status_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_rd_status_lox cvmx_endor_intc_rd_status_lox_t;

/**
 * cvmx_endor_intc_rdq_idx_hi#
 *
 * ENDOR_INTC_RDQ_IDX_HI - Read Queue Done Group Index HI
 *
 */
union cvmx_endor_intc_rdq_idx_hix {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_idx_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Read Queue Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_rdq_idx_hix_s  cnf71xx;
};
typedef union cvmx_endor_intc_rdq_idx_hix cvmx_endor_intc_rdq_idx_hix_t;

/**
 * cvmx_endor_intc_rdq_idx_lo#
 *
 * ENDOR_INTC_RDQ_IDX_LO - Read Queue Done Group Index LO
 *
 */
union cvmx_endor_intc_rdq_idx_lox {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_idx_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Read Queue Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_rdq_idx_lox_s  cnf71xx;
};
typedef union cvmx_endor_intc_rdq_idx_lox cvmx_endor_intc_rdq_idx_lox_t;

/**
 * cvmx_endor_intc_rdq_mask_hi#
 *
 * ENDOR_INTC_RDQ_MASK_HI = Interrupt Read Queue Done Group Mask
 *
 */
union cvmx_endor_intc_rdq_mask_hix {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_mask_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rdq_mask_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_rdq_mask_hix cvmx_endor_intc_rdq_mask_hix_t;

/**
 * cvmx_endor_intc_rdq_mask_lo#
 *
 * ENDOR_INTC_RDQ_MASK_LO = Interrupt Read Queue Done Group Mask
 *
 */
union cvmx_endor_intc_rdq_mask_lox {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_mask_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rdq_mask_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_rdq_mask_lox cvmx_endor_intc_rdq_mask_lox_t;

/**
 * cvmx_endor_intc_rdq_rint
 *
 * ENDOR_INTC_RDQ_RINT - Read Queue Done Group Raw Interrupt Status
 *
 */
union cvmx_endor_intc_rdq_rint {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_rint_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rdq_rint_s     cnf71xx;
};
typedef union cvmx_endor_intc_rdq_rint cvmx_endor_intc_rdq_rint_t;

/**
 * cvmx_endor_intc_rdq_status_hi#
 *
 * ENDOR_INTC_RDQ_STATUS_HI = Interrupt Read Queue Done Group Mask
 *
 */
union cvmx_endor_intc_rdq_status_hix {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_status_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rdq_status_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_rdq_status_hix cvmx_endor_intc_rdq_status_hix_t;

/**
 * cvmx_endor_intc_rdq_status_lo#
 *
 * ENDOR_INTC_RDQ_STATUS_LO = Interrupt Read Queue Done Group Mask
 *
 */
union cvmx_endor_intc_rdq_status_lox {
	uint32_t u32;
	struct cvmx_endor_intc_rdq_status_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t t3_rfif_1                    : 1;  /**< RFIF_1 Read Done */
	uint32_t t3_rfif_0                    : 1;  /**< RFIF_0 Read Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Read Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Read Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Read Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Read Done */
	uint32_t t3_int                       : 1;  /**< TX to PHY Read Done */
	uint32_t t3_ext                       : 1;  /**< TX to Host Read Done */
	uint32_t t2_int                       : 1;  /**< RX1 to PHY Read Done */
	uint32_t t2_harq                      : 1;  /**< HARQ to Host Read Done */
	uint32_t t2_ext                       : 1;  /**< RX1 to Host Read Done */
	uint32_t t1_int                       : 1;  /**< RX0 to PHY Read Done */
	uint32_t t1_ext                       : 1;  /**< RX0 to Host Read Done */
	uint32_t ifftpapr_rm                  : 1;  /**< IFFTPAPR_RM Read Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Read Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Read Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Read Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Read Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Read Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Read Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Read Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Read Done */
	uint32_t rachsnif                     : 1;  /**< RACH Read Done */
	uint32_t ulfe                         : 1;  /**< ULFE Read Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif                     : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t ifftpapr_rm                  : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_int                       : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t3_rfif_0                    : 1;
	uint32_t t3_rfif_1                    : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_intc_rdq_status_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_rdq_status_lox cvmx_endor_intc_rdq_status_lox_t;

/**
 * cvmx_endor_intc_stat_hi#
 *
 * ENDOR_INTC_STAT_HI - Grouped Interrupt Status HI
 *
 */
union cvmx_endor_intc_stat_hix {
	uint32_t u32;
	struct cvmx_endor_intc_stat_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t misc                         : 1;  /**< Misc Group Interrupt */
	uint32_t sw                           : 1;  /**< SW Group Interrupt */
	uint32_t wrqdone                      : 1;  /**< Write  Queue Done Group Interrupt */
	uint32_t rdqdone                      : 1;  /**< Read  Queue Done Group Interrupt */
	uint32_t rddone                       : 1;  /**< Read  Done Group Interrupt */
	uint32_t wrdone                       : 1;  /**< Write Done Group Interrupt */
#else
	uint32_t wrdone                       : 1;
	uint32_t rddone                       : 1;
	uint32_t rdqdone                      : 1;
	uint32_t wrqdone                      : 1;
	uint32_t sw                           : 1;
	uint32_t misc                         : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_stat_hix_s     cnf71xx;
};
typedef union cvmx_endor_intc_stat_hix cvmx_endor_intc_stat_hix_t;

/**
 * cvmx_endor_intc_stat_lo#
 *
 * ENDOR_INTC_STAT_LO - Grouped Interrupt Status LO
 *
 */
union cvmx_endor_intc_stat_lox {
	uint32_t u32;
	struct cvmx_endor_intc_stat_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t misc                         : 1;  /**< Misc Group Interrupt */
	uint32_t sw                           : 1;  /**< SW Group Interrupt */
	uint32_t wrqdone                      : 1;  /**< Write  Queue Done Group Interrupt */
	uint32_t rdqdone                      : 1;  /**< Read  Queue Done Group Interrupt */
	uint32_t rddone                       : 1;  /**< Read  Done Group Interrupt */
	uint32_t wrdone                       : 1;  /**< Write Done Group Interrupt */
#else
	uint32_t wrdone                       : 1;
	uint32_t rddone                       : 1;
	uint32_t rdqdone                      : 1;
	uint32_t wrqdone                      : 1;
	uint32_t sw                           : 1;
	uint32_t misc                         : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_stat_lox_s     cnf71xx;
};
typedef union cvmx_endor_intc_stat_lox cvmx_endor_intc_stat_lox_t;

/**
 * cvmx_endor_intc_sw_idx_hi#
 *
 * ENDOR_INTC_SW_IDX_HI - SW Group Index HI
 *
 */
union cvmx_endor_intc_sw_idx_hix {
	uint32_t u32;
	struct cvmx_endor_intc_sw_idx_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< SW Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_sw_idx_hix_s   cnf71xx;
};
typedef union cvmx_endor_intc_sw_idx_hix cvmx_endor_intc_sw_idx_hix_t;

/**
 * cvmx_endor_intc_sw_idx_lo#
 *
 * ENDOR_INTC_SW_IDX_LO - SW Group Index LO
 *
 */
union cvmx_endor_intc_sw_idx_lox {
	uint32_t u32;
	struct cvmx_endor_intc_sw_idx_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< SW Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_sw_idx_lox_s   cnf71xx;
};
typedef union cvmx_endor_intc_sw_idx_lox cvmx_endor_intc_sw_idx_lox_t;

/**
 * cvmx_endor_intc_sw_mask_hi#
 *
 * ENDOR_INTC_SW_MASK_HI = Interrupt SW Mask
 *
 */
union cvmx_endor_intc_sw_mask_hix {
	uint32_t u32;
	struct cvmx_endor_intc_sw_mask_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t swint                        : 32; /**< ULFE Read Done */
#else
	uint32_t swint                        : 32;
#endif
	} s;
	struct cvmx_endor_intc_sw_mask_hix_s  cnf71xx;
};
typedef union cvmx_endor_intc_sw_mask_hix cvmx_endor_intc_sw_mask_hix_t;

/**
 * cvmx_endor_intc_sw_mask_lo#
 *
 * ENDOR_INTC_SW_MASK_LO = Interrupt SW Mask
 *
 */
union cvmx_endor_intc_sw_mask_lox {
	uint32_t u32;
	struct cvmx_endor_intc_sw_mask_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t swint                        : 32; /**< ULFE Read Done */
#else
	uint32_t swint                        : 32;
#endif
	} s;
	struct cvmx_endor_intc_sw_mask_lox_s  cnf71xx;
};
typedef union cvmx_endor_intc_sw_mask_lox cvmx_endor_intc_sw_mask_lox_t;

/**
 * cvmx_endor_intc_sw_rint
 *
 * ENDOR_INTC_SW_RINT - SW Raw Interrupt Status
 *
 */
union cvmx_endor_intc_sw_rint {
	uint32_t u32;
	struct cvmx_endor_intc_sw_rint_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t swint                        : 32; /**< ULFE Read Done */
#else
	uint32_t swint                        : 32;
#endif
	} s;
	struct cvmx_endor_intc_sw_rint_s      cnf71xx;
};
typedef union cvmx_endor_intc_sw_rint cvmx_endor_intc_sw_rint_t;

/**
 * cvmx_endor_intc_sw_status_hi#
 *
 * ENDOR_INTC_SW_STATUS_HI = Interrupt SW Mask
 *
 */
union cvmx_endor_intc_sw_status_hix {
	uint32_t u32;
	struct cvmx_endor_intc_sw_status_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t swint                        : 32; /**< ULFE Read Done */
#else
	uint32_t swint                        : 32;
#endif
	} s;
	struct cvmx_endor_intc_sw_status_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_sw_status_hix cvmx_endor_intc_sw_status_hix_t;

/**
 * cvmx_endor_intc_sw_status_lo#
 *
 * ENDOR_INTC_SW_STATUS_LO = Interrupt SW Mask
 *
 */
union cvmx_endor_intc_sw_status_lox {
	uint32_t u32;
	struct cvmx_endor_intc_sw_status_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t swint                        : 32; /**< ULFE Read Done */
#else
	uint32_t swint                        : 32;
#endif
	} s;
	struct cvmx_endor_intc_sw_status_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_sw_status_lox cvmx_endor_intc_sw_status_lox_t;

/**
 * cvmx_endor_intc_swclr
 *
 * ENDOR_INTC_SWCLR- SW Interrupt Clear
 *
 */
union cvmx_endor_intc_swclr {
	uint32_t u32;
	struct cvmx_endor_intc_swclr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t clr                          : 32; /**< Clear SW Interrupt bit */
#else
	uint32_t clr                          : 32;
#endif
	} s;
	struct cvmx_endor_intc_swclr_s        cnf71xx;
};
typedef union cvmx_endor_intc_swclr cvmx_endor_intc_swclr_t;

/**
 * cvmx_endor_intc_swset
 *
 * ENDOR_INTC_SWSET - SW Interrupt Set
 *
 */
union cvmx_endor_intc_swset {
	uint32_t u32;
	struct cvmx_endor_intc_swset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t set                          : 32; /**< Set SW Interrupt bit */
#else
	uint32_t set                          : 32;
#endif
	} s;
	struct cvmx_endor_intc_swset_s        cnf71xx;
};
typedef union cvmx_endor_intc_swset cvmx_endor_intc_swset_t;

/**
 * cvmx_endor_intc_wr_idx_hi#
 *
 * ENDOR_INTC_WR_IDX_HI - Write Done Group Index HI
 *
 */
union cvmx_endor_intc_wr_idx_hix {
	uint32_t u32;
	struct cvmx_endor_intc_wr_idx_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Write Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_wr_idx_hix_s   cnf71xx;
};
typedef union cvmx_endor_intc_wr_idx_hix cvmx_endor_intc_wr_idx_hix_t;

/**
 * cvmx_endor_intc_wr_idx_lo#
 *
 * ENDOR_INTC_WR_IDX_LO - Write Done Group Index LO
 *
 */
union cvmx_endor_intc_wr_idx_lox {
	uint32_t u32;
	struct cvmx_endor_intc_wr_idx_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Write Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_wr_idx_lox_s   cnf71xx;
};
typedef union cvmx_endor_intc_wr_idx_lox cvmx_endor_intc_wr_idx_lox_t;

/**
 * cvmx_endor_intc_wr_mask_hi#
 *
 * ENDOR_INTC_WR_MASK_HI = Interrupt Write Done Group Mask
 *
 */
union cvmx_endor_intc_wr_mask_hix {
	uint32_t u32;
	struct cvmx_endor_intc_wr_mask_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t t1_rfif_1                    : 1;  /**< RFIF_1 Write Done */
	uint32_t t1_rfif_0                    : 1;  /**< RFIF_0 Write Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Write Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Write Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Write Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Write Done */
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t1_rfif_0                    : 1;
	uint32_t t1_rfif_1                    : 1;
	uint32_t reserved_29_31               : 3;
#endif
	} s;
	struct cvmx_endor_intc_wr_mask_hix_s  cnf71xx;
};
typedef union cvmx_endor_intc_wr_mask_hix cvmx_endor_intc_wr_mask_hix_t;

/**
 * cvmx_endor_intc_wr_mask_lo#
 *
 * ENDOR_INTC_WR_MASK_LO = Interrupt Write Done Group Mask
 *
 */
union cvmx_endor_intc_wr_mask_lox {
	uint32_t u32;
	struct cvmx_endor_intc_wr_mask_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t t1_rfif_1                    : 1;  /**< RFIF_1 Write Done */
	uint32_t t1_rfif_0                    : 1;  /**< RFIF_0 Write Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Write Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Write Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Write Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Write Done */
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t1_rfif_0                    : 1;
	uint32_t t1_rfif_1                    : 1;
	uint32_t reserved_29_31               : 3;
#endif
	} s;
	struct cvmx_endor_intc_wr_mask_lox_s  cnf71xx;
};
typedef union cvmx_endor_intc_wr_mask_lox cvmx_endor_intc_wr_mask_lox_t;

/**
 * cvmx_endor_intc_wr_rint
 *
 * ENDOR_INTC_WR_RINT - Write Done Group Raw Interrupt Status
 *
 */
union cvmx_endor_intc_wr_rint {
	uint32_t u32;
	struct cvmx_endor_intc_wr_rint_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t t1_rfif_1                    : 1;  /**< RFIF_1 Write Done */
	uint32_t t1_rfif_0                    : 1;  /**< RFIF_0 Write Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Write Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Write Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Write Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Write Done */
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t1_rfif_0                    : 1;
	uint32_t t1_rfif_1                    : 1;
	uint32_t reserved_29_31               : 3;
#endif
	} s;
	struct cvmx_endor_intc_wr_rint_s      cnf71xx;
};
typedef union cvmx_endor_intc_wr_rint cvmx_endor_intc_wr_rint_t;

/**
 * cvmx_endor_intc_wr_status_hi#
 *
 * ENDOR_INTC_WR_STATUS_HI = Interrupt Write Done Group Mask
 *
 */
union cvmx_endor_intc_wr_status_hix {
	uint32_t u32;
	struct cvmx_endor_intc_wr_status_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t t1_rfif_1                    : 1;  /**< RFIF_1 Write Done */
	uint32_t t1_rfif_0                    : 1;  /**< RFIF_0 Write Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Write Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Write Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Write Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Write Done */
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t1_rfif_0                    : 1;
	uint32_t t1_rfif_1                    : 1;
	uint32_t reserved_29_31               : 3;
#endif
	} s;
	struct cvmx_endor_intc_wr_status_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_wr_status_hix cvmx_endor_intc_wr_status_hix_t;

/**
 * cvmx_endor_intc_wr_status_lo#
 *
 * ENDOR_INTC_WR_STATUS_LO = Interrupt Write Done Group Mask
 *
 */
union cvmx_endor_intc_wr_status_lox {
	uint32_t u32;
	struct cvmx_endor_intc_wr_status_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t t1_rfif_1                    : 1;  /**< RFIF_1 Write Done */
	uint32_t t1_rfif_0                    : 1;  /**< RFIF_0 Write Done */
	uint32_t axi_rx1_harq                 : 1;  /**< HARQ to Host Write Done */
	uint32_t axi_rx1                      : 1;  /**< RX1 to Host Write Done */
	uint32_t axi_rx0                      : 1;  /**< RX0 to Host Write Done */
	uint32_t axi_tx                       : 1;  /**< TX to Host Write Done */
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t axi_tx                       : 1;
	uint32_t axi_rx0                      : 1;
	uint32_t axi_rx1                      : 1;
	uint32_t axi_rx1_harq                 : 1;
	uint32_t t1_rfif_0                    : 1;
	uint32_t t1_rfif_1                    : 1;
	uint32_t reserved_29_31               : 3;
#endif
	} s;
	struct cvmx_endor_intc_wr_status_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_wr_status_lox cvmx_endor_intc_wr_status_lox_t;

/**
 * cvmx_endor_intc_wrq_idx_hi#
 *
 * ENDOR_INTC_WRQ_IDX_HI - Write Queue Done Group Index HI
 *
 */
union cvmx_endor_intc_wrq_idx_hix {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_idx_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Write Queue Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_wrq_idx_hix_s  cnf71xx;
};
typedef union cvmx_endor_intc_wrq_idx_hix cvmx_endor_intc_wrq_idx_hix_t;

/**
 * cvmx_endor_intc_wrq_idx_lo#
 *
 * ENDOR_INTC_WRQ_IDX_LO - Write Queue Done Group Index LO
 *
 */
union cvmx_endor_intc_wrq_idx_lox {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_idx_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t grpidx                       : 6;  /**< Write Queue Done Group Interrupt Index */
#else
	uint32_t grpidx                       : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_intc_wrq_idx_lox_s  cnf71xx;
};
typedef union cvmx_endor_intc_wrq_idx_lox cvmx_endor_intc_wrq_idx_lox_t;

/**
 * cvmx_endor_intc_wrq_mask_hi#
 *
 * ENDOR_INTC_WRQ_MASK_HI = Interrupt Write Queue Done Group Mask
 *
 */
union cvmx_endor_intc_wrq_mask_hix {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_mask_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_intc_wrq_mask_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_wrq_mask_hix cvmx_endor_intc_wrq_mask_hix_t;

/**
 * cvmx_endor_intc_wrq_mask_lo#
 *
 * ENDOR_INTC_WRQ_MASK_LO = Interrupt Write Queue Done Group Mask
 *
 */
union cvmx_endor_intc_wrq_mask_lox {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_mask_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_intc_wrq_mask_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_wrq_mask_lox cvmx_endor_intc_wrq_mask_lox_t;

/**
 * cvmx_endor_intc_wrq_rint
 *
 * ENDOR_INTC_WRQ_RINT - Write Queue Done Group Raw Interrupt Status
 *
 */
union cvmx_endor_intc_wrq_rint {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_rint_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_intc_wrq_rint_s     cnf71xx;
};
typedef union cvmx_endor_intc_wrq_rint cvmx_endor_intc_wrq_rint_t;

/**
 * cvmx_endor_intc_wrq_status_hi#
 *
 * ENDOR_INTC_WRQ_STATUS_HI = Interrupt Write Queue Done Group Mask
 *
 */
union cvmx_endor_intc_wrq_status_hix {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_status_hix_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_intc_wrq_status_hix_s cnf71xx;
};
typedef union cvmx_endor_intc_wrq_status_hix cvmx_endor_intc_wrq_status_hix_t;

/**
 * cvmx_endor_intc_wrq_status_lo#
 *
 * ENDOR_INTC_WRQ_STATUS_LO = Interrupt Write Queue Done Group Mask
 *
 */
union cvmx_endor_intc_wrq_status_lox {
	uint32_t u32;
	struct cvmx_endor_intc_wrq_status_lox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t t3_instr                     : 1;  /**< TX Instr Write Done */
	uint32_t t3_int                       : 1;  /**< PHY to TX Write Done */
	uint32_t t3_ext                       : 1;  /**< Host to TX Write Done */
	uint32_t t2_instr                     : 1;  /**< RX1 Instr Write Done */
	uint32_t t2_harq                      : 1;  /**< Host to HARQ Write Done */
	uint32_t t2_int                       : 1;  /**< PHY to RX1 Write Done */
	uint32_t t2_ext                       : 1;  /**< Host to RX1 Write Done */
	uint32_t t1_instr                     : 1;  /**< RX0 Instr Write Done */
	uint32_t t1_int                       : 1;  /**< PHY to RX0 Write Done */
	uint32_t t1_ext                       : 1;  /**< Host to RX0 Write Done */
	uint32_t ifftpapr_1                   : 1;  /**< IFFTPAPR_1 Write Done */
	uint32_t ifftpapr_0                   : 1;  /**< IFFTPAPR_0 Write Done */
	uint32_t lteenc_cch                   : 1;  /**< LTE Encoder CCH Write Done */
	uint32_t lteenc_tb1                   : 1;  /**< LTE Encoder TB1 Write Done */
	uint32_t lteenc_tb0                   : 1;  /**< LTE Encoder TB0 Write Done */
	uint32_t vitbdec                      : 1;  /**< Viterbi Decoder Write Done */
	uint32_t turbo_hq                     : 1;  /**< Turbo Decoder HARQ Write Done */
	uint32_t turbo_sb                     : 1;  /**< Turbo Decoder Soft Bits Write Done */
	uint32_t turbo                        : 1;  /**< Turbo Decoder Write Done */
	uint32_t dftdm                        : 1;  /**< DFT/Demapper Write Done */
	uint32_t rachsnif_1                   : 1;  /**< RACH_1 Write Done */
	uint32_t rachsnif_0                   : 1;  /**< RACH_0 Write Done */
	uint32_t ulfe                         : 1;  /**< ULFE Write Done */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachsnif_0                   : 1;
	uint32_t rachsnif_1                   : 1;
	uint32_t dftdm                        : 1;
	uint32_t turbo                        : 1;
	uint32_t turbo_sb                     : 1;
	uint32_t turbo_hq                     : 1;
	uint32_t vitbdec                      : 1;
	uint32_t lteenc_tb0                   : 1;
	uint32_t lteenc_tb1                   : 1;
	uint32_t lteenc_cch                   : 1;
	uint32_t ifftpapr_0                   : 1;
	uint32_t ifftpapr_1                   : 1;
	uint32_t t1_ext                       : 1;
	uint32_t t1_int                       : 1;
	uint32_t t1_instr                     : 1;
	uint32_t t2_ext                       : 1;
	uint32_t t2_int                       : 1;
	uint32_t t2_harq                      : 1;
	uint32_t t2_instr                     : 1;
	uint32_t t3_ext                       : 1;
	uint32_t t3_int                       : 1;
	uint32_t t3_instr                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_intc_wrq_status_lox_s cnf71xx;
};
typedef union cvmx_endor_intc_wrq_status_lox cvmx_endor_intc_wrq_status_lox_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_end_addr0
 */
union cvmx_endor_ofs_hmm_cbuf_end_addr0 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr0_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_end_addr0 cvmx_endor_ofs_hmm_cbuf_end_addr0_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_end_addr1
 */
union cvmx_endor_ofs_hmm_cbuf_end_addr1 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr1_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_end_addr1 cvmx_endor_ofs_hmm_cbuf_end_addr1_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_end_addr2
 */
union cvmx_endor_ofs_hmm_cbuf_end_addr2 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr2_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_end_addr2 cvmx_endor_ofs_hmm_cbuf_end_addr2_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_end_addr3
 */
union cvmx_endor_ofs_hmm_cbuf_end_addr3 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_end_addr3_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_end_addr3 cvmx_endor_ofs_hmm_cbuf_end_addr3_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_start_addr0
 */
union cvmx_endor_ofs_hmm_cbuf_start_addr0 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr0_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_start_addr0 cvmx_endor_ofs_hmm_cbuf_start_addr0_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_start_addr1
 */
union cvmx_endor_ofs_hmm_cbuf_start_addr1 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr1_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_start_addr1 cvmx_endor_ofs_hmm_cbuf_start_addr1_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_start_addr2
 */
union cvmx_endor_ofs_hmm_cbuf_start_addr2 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr2_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_start_addr2 cvmx_endor_ofs_hmm_cbuf_start_addr2_t;

/**
 * cvmx_endor_ofs_hmm_cbuf_start_addr3
 */
union cvmx_endor_ofs_hmm_cbuf_start_addr3 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_cbuf_start_addr3_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_cbuf_start_addr3 cvmx_endor_ofs_hmm_cbuf_start_addr3_t;

/**
 * cvmx_endor_ofs_hmm_intr_clear
 */
union cvmx_endor_ofs_hmm_intr_clear {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_intr_clear_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t xfer_q_empty                 : 1;  /**< reserved. */
	uint32_t xfer_complete                : 1;  /**< reserved. */
#else
	uint32_t xfer_complete                : 1;
	uint32_t xfer_q_empty                 : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_intr_clear_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_intr_clear cvmx_endor_ofs_hmm_intr_clear_t;

/**
 * cvmx_endor_ofs_hmm_intr_enb
 */
union cvmx_endor_ofs_hmm_intr_enb {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_intr_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t xfer_q_empty                 : 1;  /**< reserved. */
	uint32_t xfer_complete                : 1;  /**< reserved. */
#else
	uint32_t xfer_complete                : 1;
	uint32_t xfer_q_empty                 : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_intr_enb_s  cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_intr_enb cvmx_endor_ofs_hmm_intr_enb_t;

/**
 * cvmx_endor_ofs_hmm_intr_rstatus
 */
union cvmx_endor_ofs_hmm_intr_rstatus {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_intr_rstatus_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t xfer_q_empty                 : 1;  /**< reserved. */
	uint32_t xfer_complete                : 1;  /**< reserved. */
#else
	uint32_t xfer_complete                : 1;
	uint32_t xfer_q_empty                 : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_intr_rstatus_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_intr_rstatus cvmx_endor_ofs_hmm_intr_rstatus_t;

/**
 * cvmx_endor_ofs_hmm_intr_status
 */
union cvmx_endor_ofs_hmm_intr_status {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_intr_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t xfer_q_empty                 : 1;  /**< reserved. */
	uint32_t xfer_complete                : 1;  /**< reserved. */
#else
	uint32_t xfer_complete                : 1;
	uint32_t xfer_q_empty                 : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_intr_status_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_intr_status cvmx_endor_ofs_hmm_intr_status_t;

/**
 * cvmx_endor_ofs_hmm_intr_test
 */
union cvmx_endor_ofs_hmm_intr_test {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_intr_test_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t xfer_q_empty                 : 1;  /**< reserved. */
	uint32_t xfer_complete                : 1;  /**< reserved. */
#else
	uint32_t xfer_complete                : 1;
	uint32_t xfer_q_empty                 : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_intr_test_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_intr_test cvmx_endor_ofs_hmm_intr_test_t;

/**
 * cvmx_endor_ofs_hmm_mode
 */
union cvmx_endor_ofs_hmm_mode {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t itlv_bufmode                 : 2;  /**< interleave buffer : 0==1:1, 1==2:1, 2==4:1 */
	uint32_t reserved_2_3                 : 2;
	uint32_t mem_clr_enb                  : 1;  /**< reserved. */
	uint32_t auto_clk_enb                 : 1;  /**< reserved. */
#else
	uint32_t auto_clk_enb                 : 1;
	uint32_t mem_clr_enb                  : 1;
	uint32_t reserved_2_3                 : 2;
	uint32_t itlv_bufmode                 : 2;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_mode_s      cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_mode cvmx_endor_ofs_hmm_mode_t;

/**
 * cvmx_endor_ofs_hmm_start_addr0
 */
union cvmx_endor_ofs_hmm_start_addr0 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_start_addr0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_start_addr0_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_start_addr0 cvmx_endor_ofs_hmm_start_addr0_t;

/**
 * cvmx_endor_ofs_hmm_start_addr1
 */
union cvmx_endor_ofs_hmm_start_addr1 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_start_addr1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_start_addr1_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_start_addr1 cvmx_endor_ofs_hmm_start_addr1_t;

/**
 * cvmx_endor_ofs_hmm_start_addr2
 */
union cvmx_endor_ofs_hmm_start_addr2 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_start_addr2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_start_addr2_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_start_addr2 cvmx_endor_ofs_hmm_start_addr2_t;

/**
 * cvmx_endor_ofs_hmm_start_addr3
 */
union cvmx_endor_ofs_hmm_start_addr3 {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_start_addr3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t addr                         : 24; /**< reserved. */
#else
	uint32_t addr                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_start_addr3_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_start_addr3 cvmx_endor_ofs_hmm_start_addr3_t;

/**
 * cvmx_endor_ofs_hmm_status
 */
union cvmx_endor_ofs_hmm_status {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_status_s    cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_status cvmx_endor_ofs_hmm_status_t;

/**
 * cvmx_endor_ofs_hmm_xfer_cnt
 */
union cvmx_endor_ofs_hmm_xfer_cnt {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_xfer_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t xfer_comp_intr               : 1;  /**< transfer complete interrupt. */
	uint32_t slice_mode                   : 1;  /**< reserved. */
	uint32_t cbuf_mode                    : 1;  /**< reserved. */
	uint32_t reserved_16_28               : 13;
	uint32_t wordcnt                      : 16; /**< word count. */
#else
	uint32_t wordcnt                      : 16;
	uint32_t reserved_16_28               : 13;
	uint32_t cbuf_mode                    : 1;
	uint32_t slice_mode                   : 1;
	uint32_t xfer_comp_intr               : 1;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_xfer_cnt_s  cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_xfer_cnt cvmx_endor_ofs_hmm_xfer_cnt_t;

/**
 * cvmx_endor_ofs_hmm_xfer_q_status
 */
union cvmx_endor_ofs_hmm_xfer_q_status {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_xfer_q_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t status                       : 32; /**< number of slots to queue buffer transaction. */
#else
	uint32_t status                       : 32;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_xfer_q_status_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_xfer_q_status cvmx_endor_ofs_hmm_xfer_q_status_t;

/**
 * cvmx_endor_ofs_hmm_xfer_start
 */
union cvmx_endor_ofs_hmm_xfer_start {
	uint32_t u32;
	struct cvmx_endor_ofs_hmm_xfer_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t start                        : 1;  /**< reserved. */
#else
	uint32_t start                        : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_ofs_hmm_xfer_start_s cnf71xx;
};
typedef union cvmx_endor_ofs_hmm_xfer_start cvmx_endor_ofs_hmm_xfer_start_t;

/**
 * cvmx_endor_rfif_1pps_gen_cfg
 */
union cvmx_endor_rfif_1pps_gen_cfg {
	uint32_t u32;
	struct cvmx_endor_rfif_1pps_gen_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t ena                          : 1;  /**< Enable 1PPS Generation and Tracking
                                                         - 0: 1PPS signal not tracked or generated
                                                         - 1: 1PPS signal generated and tracked */
#else
	uint32_t ena                          : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_rfif_1pps_gen_cfg_s cnf71xx;
};
typedef union cvmx_endor_rfif_1pps_gen_cfg cvmx_endor_rfif_1pps_gen_cfg_t;

/**
 * cvmx_endor_rfif_1pps_sample_cnt_offset
 */
union cvmx_endor_rfif_1pps_sample_cnt_offset {
	uint32_t u32;
	struct cvmx_endor_rfif_1pps_sample_cnt_offset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t offset                       : 20; /**< This register holds the sample count at which the 1PPS
                                                         was received.
                                                         Upon reset, the sample counter starts at 0 when the
                                                         first 1PPS is received and then increments to wrap
                                                         around at FRAME_L-1. At each subsequent 1PPS, a
                                                         snapshot of the sample counter is taken and the count
                                                         is made available via this register. This enables
                                                         software to monitor the RF clock drift relative to
                                                         the 1PPS. */
#else
	uint32_t offset                       : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_1pps_sample_cnt_offset_s cnf71xx;
};
typedef union cvmx_endor_rfif_1pps_sample_cnt_offset cvmx_endor_rfif_1pps_sample_cnt_offset_t;

/**
 * cvmx_endor_rfif_1pps_verif_gen_en
 */
union cvmx_endor_rfif_1pps_verif_gen_en {
	uint32_t u32;
	struct cvmx_endor_rfif_1pps_verif_gen_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t ena                          : 1;  /**< 1PPS generation for verification purposes
                                                         - 0: Disabled (default)
                                                         - 1: Enabled
                                                          Note the external 1PPS is not considered, when this bit
                                                          is set to 1. */
#else
	uint32_t ena                          : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_rfif_1pps_verif_gen_en_s cnf71xx;
};
typedef union cvmx_endor_rfif_1pps_verif_gen_en cvmx_endor_rfif_1pps_verif_gen_en_t;

/**
 * cvmx_endor_rfif_1pps_verif_scnt
 */
union cvmx_endor_rfif_1pps_verif_scnt {
	uint32_t u32;
	struct cvmx_endor_rfif_1pps_verif_scnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t cnt                          : 20; /**< Sample count at which the 1PPS is generated for
                                                         verification purposes. */
#else
	uint32_t cnt                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_1pps_verif_scnt_s cnf71xx;
};
typedef union cvmx_endor_rfif_1pps_verif_scnt cvmx_endor_rfif_1pps_verif_scnt_t;

/**
 * cvmx_endor_rfif_conf
 */
union cvmx_endor_rfif_conf {
	uint32_t u32;
	struct cvmx_endor_rfif_conf_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t loopback                     : 1;  /**< FDD loop back mode
                                                         - 0: Not in loopback mode(default)
                                                         - 1: loops back the tx ouput to the rx input inside the
                                                          rf_if */
	uint32_t mol                          : 1;  /**< Manual Override Lock */
	uint32_t upd_style                    : 1;  /**< TX and RX Windows parameters update style (default:0)
                                                         - 0: updated as written to the register (on the fly)
                                                          (not fully verified but kept in case limitations are
                                                          found with the other update scheme.)
                                                         - 1: updated at the specified time by registers 00F and
                                                          90F.
                                                          Note the frame length is updated after the last TX
                                                          window.
                                                         - 1: eNB, enables using 1PPS synchronization scheme. */
	uint32_t diversity                    : 1;  /**< RX diversity disable (Used to support FDD SISO with CLK
                                                          4X)
                                                         - 0: Data gets written to the diversity FIFO in MIMO mode
                                                          (default).
                                                         - 1: No data written to the diversity FIFO in MIMO mode. */
	uint32_t duplex                       : 1;  /**< Division Duplex Mode
                                                         - 0: TDD (default)
                                                         - 1: FDD */
	uint32_t prod_type                    : 1;  /**< Product Type
                                                         - 0: UE (default), enables using sync and timing advance
                                                          synchronization schemes. */
	uint32_t txnrx_ctrl                   : 1;  /**< RFIC IF TXnRX signal pulse control. Changing the value
                                                         of this bit generates a pulse on the TXNRX signal of
                                                         the RFIC interface. This feature is enabled when bit
                                                         9 has already been asserted. */
	uint32_t ena_ctrl                     : 1;  /**< RFIC IF ENABLE signal pulse control. Changing the value
                                                         of this bit generates a pulse on the ENABLE signal of
                                                         the RFIC interface. This feature is enabled when bit 9
                                                         has already been asserted. */
	uint32_t man_ctrl                     : 1;  /**< RF IC Manual Control Enable. Setting this bit to 1
                                                         enables manual control of the TXNRX and ENABLE signals.
                                                         When set to 0 (default), the TXNRX and ENABLE signals
                                                         are automatically controlled when opening and closing
                                                         RX/TX windows. The manual mode is used to initialize
                                                         the RFIC in alert mode. */
	uint32_t dsp_rx_int_en                : 1;  /**< DSP RX interrupt mask enable
                                                         - 0: DSP RX receives interrupts
                                                         - 1: DSP RX doesn't receive interrupts, needs to poll
                                                          ISRs */
	uint32_t adi_en                       : 1;  /**< ADI enable signal pulsed or leveled behavior
                                                         - 0: pulsed
                                                         - 1: leveled */
	uint32_t clr_fifo_of                  : 1;  /**< Clear RX FIFO overflow flag. */
	uint32_t clr_fifo_ur                  : 1;  /**< Clear RX FIFO under run flag. */
	uint32_t wavesat_mode                 : 1;  /**< AD9361 wavesat mode, where enable becomes rx_control
                                                          and txnrx becomes tx_control. The wavesat mode permits
                                                          an independent control of the rx and tx data flows.
                                                         - 0: wavesat mode
                                                         - 1: regular mode */
	uint32_t flush                        : 1;  /**< Flush RX FIFO auto clear register. */
	uint32_t inv                          : 1;  /**< Data inversion (bit 0 becomes bit 11, bit 1 becomes 10) */
	uint32_t mode                         : 1;  /**< 0: SISO 1: MIMO */
	uint32_t enable                       : 1;  /**< 1=enable, 0=disabled */
#else
	uint32_t enable                       : 1;
	uint32_t mode                         : 1;
	uint32_t inv                          : 1;
	uint32_t flush                        : 1;
	uint32_t wavesat_mode                 : 1;
	uint32_t clr_fifo_ur                  : 1;
	uint32_t clr_fifo_of                  : 1;
	uint32_t adi_en                       : 1;
	uint32_t dsp_rx_int_en                : 1;
	uint32_t man_ctrl                     : 1;
	uint32_t ena_ctrl                     : 1;
	uint32_t txnrx_ctrl                   : 1;
	uint32_t prod_type                    : 1;
	uint32_t duplex                       : 1;
	uint32_t diversity                    : 1;
	uint32_t upd_style                    : 1;
	uint32_t mol                          : 1;
	uint32_t loopback                     : 1;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_endor_rfif_conf_s         cnf71xx;
};
typedef union cvmx_endor_rfif_conf cvmx_endor_rfif_conf_t;

/**
 * cvmx_endor_rfif_conf2
 */
union cvmx_endor_rfif_conf2 {
	uint32_t u32;
	struct cvmx_endor_rfif_conf2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_3_31                : 29;
	uint32_t latency                      : 1;  /**< RF DATA variable latency
                                                         - 0: fixed latency (prior to AD9163)
                                                         - 1: variable latency (starting with the AD9361) */
	uint32_t iq_cfg                       : 1;  /**< IQ port configuration
                                                         - 0: Single port (10Mhz BW and less)
                                                         - 1: Dual ports (more then 10Mhz BW) */
	uint32_t behavior                     : 1;  /**< RX and TX FRAME signals behavior:
                                                         - 0: Pulsed every frame
                                                         - 1: Leveled during the whole RX and TX periods */
#else
	uint32_t behavior                     : 1;
	uint32_t iq_cfg                       : 1;
	uint32_t latency                      : 1;
	uint32_t reserved_3_31                : 29;
#endif
	} s;
	struct cvmx_endor_rfif_conf2_s        cnf71xx;
};
typedef union cvmx_endor_rfif_conf2 cvmx_endor_rfif_conf2_t;

/**
 * cvmx_endor_rfif_dsp1_gpio
 */
union cvmx_endor_rfif_dsp1_gpio {
	uint32_t u32;
	struct cvmx_endor_rfif_dsp1_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t val                          : 4;  /**< Values to output to the DSP1_GPIO ports */
#else
	uint32_t val                          : 4;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_endor_rfif_dsp1_gpio_s    cnf71xx;
};
typedef union cvmx_endor_rfif_dsp1_gpio cvmx_endor_rfif_dsp1_gpio_t;

/**
 * cvmx_endor_rfif_dsp_rx_his
 */
union cvmx_endor_rfif_dsp_rx_his {
	uint32_t u32;
	struct cvmx_endor_rfif_dsp_rx_his_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_endor_rfif_dsp_rx_his_s   cnf71xx;
};
typedef union cvmx_endor_rfif_dsp_rx_his cvmx_endor_rfif_dsp_rx_his_t;

/**
 * cvmx_endor_rfif_dsp_rx_ism
 */
union cvmx_endor_rfif_dsp_rx_ism {
	uint32_t u32;
	struct cvmx_endor_rfif_dsp_rx_ism_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t ena                          : 8;  /**< Enable interrupt bits. Set to each bit to 1 to enable
                                                         the interrupts listed in the table below. The default
                                                         value is 0x0. */
	uint32_t reserved_0_15                : 16;
#else
	uint32_t reserved_0_15                : 16;
	uint32_t ena                          : 8;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_dsp_rx_ism_s   cnf71xx;
};
typedef union cvmx_endor_rfif_dsp_rx_ism cvmx_endor_rfif_dsp_rx_ism_t;

/**
 * cvmx_endor_rfif_firs_enable
 */
union cvmx_endor_rfif_firs_enable {
	uint32_t u32;
	struct cvmx_endor_rfif_firs_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t tx_div_fil                   : 1;  /**< TX DIV filtering control bit
                                                         - 0: TX DIV filtering disabled
                                                         - 1: TX DIV filtering enabled */
	uint32_t tx_fil                       : 1;  /**< TX filtering control bit
                                                         - 0: TX filtering disabled
                                                         - 1: TX filtering enabled */
	uint32_t rx_dif_fil                   : 1;  /**< RX DIV filtering control bit
                                                         - 0: RX DIV filtering disabled
                                                         - 1: RX DIV filtering enabled */
	uint32_t rx_fil                       : 1;  /**< RX filtering control bit
                                                         - 0: RX filtering disabled
                                                         - 1: RX filtering enabled */
#else
	uint32_t rx_fil                       : 1;
	uint32_t rx_dif_fil                   : 1;
	uint32_t tx_fil                       : 1;
	uint32_t tx_div_fil                   : 1;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_endor_rfif_firs_enable_s  cnf71xx;
};
typedef union cvmx_endor_rfif_firs_enable cvmx_endor_rfif_firs_enable_t;

/**
 * cvmx_endor_rfif_frame_cnt
 */
union cvmx_endor_rfif_frame_cnt {
	uint32_t u32;
	struct cvmx_endor_rfif_frame_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t cnt                          : 20; /**< Frame count (value wraps around 2**16) */
#else
	uint32_t cnt                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_frame_cnt_s    cnf71xx;
};
typedef union cvmx_endor_rfif_frame_cnt cvmx_endor_rfif_frame_cnt_t;

/**
 * cvmx_endor_rfif_frame_l
 */
union cvmx_endor_rfif_frame_l {
	uint32_t u32;
	struct cvmx_endor_rfif_frame_l_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t length                       : 20; /**< Frame length in terms of RF clock cycles:
                                                         RFIC in single port modes
                                                         TDD SISO ? FRAME_L = num_samples
                                                         TDD MIMO ? FRAME_L = num_samples * 2
                                                         FDD SISO ? FRAME_L = num_samples * 2
                                                         FDD MIMO ? FRAME_L = num_samples * 4
                                                         RFIC in dual ports modes
                                                         TDD SISO ? FRAME_L = num_samples * 0.5
                                                         TDD MIMO ? FRAME_L = num_samples
                                                         FDD SISO ? FRAME_L = num_samples
                                                         FDD MIMO ? FRAME_L = num_samples * 2 */
#else
	uint32_t length                       : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_frame_l_s      cnf71xx;
};
typedef union cvmx_endor_rfif_frame_l cvmx_endor_rfif_frame_l_t;

/**
 * cvmx_endor_rfif_gpio_#
 */
union cvmx_endor_rfif_gpio_x {
	uint32_t u32;
	struct cvmx_endor_rfif_gpio_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t fall_val                     : 11; /**< Signed value (lead/lag) on falling edge of level signal */
	uint32_t rise_val                     : 11; /**< Signed value (lead/lag) on rising edge of level signal */
	uint32_t src                          : 2;  /**< Signal active high source:
                                                         - 00: idle
                                                         - 01: RX
                                                         - 10: TX
                                                         - 11: idle */
#else
	uint32_t src                          : 2;
	uint32_t rise_val                     : 11;
	uint32_t fall_val                     : 11;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_gpio_x_s       cnf71xx;
};
typedef union cvmx_endor_rfif_gpio_x cvmx_endor_rfif_gpio_x_t;

/**
 * cvmx_endor_rfif_max_sample_adj
 */
union cvmx_endor_rfif_max_sample_adj {
	uint32_t u32;
	struct cvmx_endor_rfif_max_sample_adj_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_10_31               : 22;
	uint32_t num                          : 10; /**< Indicates the maximum number of samples that can be
                                                         adjusted per frame. Note the value to be programmed
                                                         varies with the mode of operation as follow:
                                                         MAX_SAMPLE_ADJ  = num_samples*MIMO*FDD*DP
                                                         Where:
                                                         MIMO = 2 in MIMO mode and 1 otherwise.
                                                         FDD = 2 in FDD mode and 1 otherwise.
                                                         DP = 0.5 in RF IF Dual Port mode, 1 otherwise. */
#else
	uint32_t num                          : 10;
	uint32_t reserved_10_31               : 22;
#endif
	} s;
	struct cvmx_endor_rfif_max_sample_adj_s cnf71xx;
};
typedef union cvmx_endor_rfif_max_sample_adj cvmx_endor_rfif_max_sample_adj_t;

/**
 * cvmx_endor_rfif_min_sample_adj
 */
union cvmx_endor_rfif_min_sample_adj {
	uint32_t u32;
	struct cvmx_endor_rfif_min_sample_adj_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_10_31               : 22;
	uint32_t num                          : 10; /**< Indicates the minimum number of samples that can be
                                                         adjusted per frame. Note the value to be programmed
                                                         varies with the mode of operation as follow:
                                                         MIN_SAMPLE_ADJ  = num_samples*MIMO*FDD*DP
                                                         Where:
                                                         MIMO = 2 in MIMO mode and 1 otherwise.
                                                         FDD = 2 in FDD mode and 1 otherwise.
                                                         DP = 0.5 in RF IF Dual Port mode, 1 otherwise. */
#else
	uint32_t num                          : 10;
	uint32_t reserved_10_31               : 22;
#endif
	} s;
	struct cvmx_endor_rfif_min_sample_adj_s cnf71xx;
};
typedef union cvmx_endor_rfif_min_sample_adj cvmx_endor_rfif_min_sample_adj_t;

/**
 * cvmx_endor_rfif_num_rx_win
 */
union cvmx_endor_rfif_num_rx_win {
	uint32_t u32;
	struct cvmx_endor_rfif_num_rx_win_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_3_31                : 29;
	uint32_t num                          : 3;  /**< Number of RX windows
                                                         - 0: No RX window
                                                         - 1: One RX window
                                                          - ...
                                                         - 4: Four RX windows
                                                          Other: Not defined */
#else
	uint32_t num                          : 3;
	uint32_t reserved_3_31                : 29;
#endif
	} s;
	struct cvmx_endor_rfif_num_rx_win_s   cnf71xx;
};
typedef union cvmx_endor_rfif_num_rx_win cvmx_endor_rfif_num_rx_win_t;

/**
 * cvmx_endor_rfif_pwm_enable
 */
union cvmx_endor_rfif_pwm_enable {
	uint32_t u32;
	struct cvmx_endor_rfif_pwm_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t ena                          : 1;  /**< PWM signal generation enable:
                                                         - 1: PWM enabled
                                                         - 0: PWM disabled (default) */
#else
	uint32_t ena                          : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_rfif_pwm_enable_s   cnf71xx;
};
typedef union cvmx_endor_rfif_pwm_enable cvmx_endor_rfif_pwm_enable_t;

/**
 * cvmx_endor_rfif_pwm_high_time
 */
union cvmx_endor_rfif_pwm_high_time {
	uint32_t u32;
	struct cvmx_endor_rfif_pwm_high_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t hi_time                      : 24; /**< PWM high time. The default is 0h00FFFF cycles. Program
                                                         to n for n+1 high cycles. */
#else
	uint32_t hi_time                      : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_pwm_high_time_s cnf71xx;
};
typedef union cvmx_endor_rfif_pwm_high_time cvmx_endor_rfif_pwm_high_time_t;

/**
 * cvmx_endor_rfif_pwm_low_time
 */
union cvmx_endor_rfif_pwm_low_time {
	uint32_t u32;
	struct cvmx_endor_rfif_pwm_low_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t lo_time                      : 24; /**< PWM low time. The default is 0h00FFFF cycles. Program
                                                         to n for n+1 low cycles. */
#else
	uint32_t lo_time                      : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_pwm_low_time_s cnf71xx;
};
typedef union cvmx_endor_rfif_pwm_low_time cvmx_endor_rfif_pwm_low_time_t;

/**
 * cvmx_endor_rfif_rd_timer64_lsb
 */
union cvmx_endor_rfif_rd_timer64_lsb {
	uint32_t u32;
	struct cvmx_endor_rfif_rd_timer64_lsb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t val                          : 32; /**< 64-bit timer initial value of the 32 LSB.
                                                         Note the value written in WR_TIMER64_LSB is not
                                                         propagating until the timer64 is enabled. */
#else
	uint32_t val                          : 32;
#endif
	} s;
	struct cvmx_endor_rfif_rd_timer64_lsb_s cnf71xx;
};
typedef union cvmx_endor_rfif_rd_timer64_lsb cvmx_endor_rfif_rd_timer64_lsb_t;

/**
 * cvmx_endor_rfif_rd_timer64_msb
 */
union cvmx_endor_rfif_rd_timer64_msb {
	uint32_t u32;
	struct cvmx_endor_rfif_rd_timer64_msb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t val                          : 32; /**< 64-bit timer initial value of the 32 MSB.
                                                         Note the value written in WR_TIMER64_MSB is not
                                                         propagating until the timer64 is enabled. */
#else
	uint32_t val                          : 32;
#endif
	} s;
	struct cvmx_endor_rfif_rd_timer64_msb_s cnf71xx;
};
typedef union cvmx_endor_rfif_rd_timer64_msb cvmx_endor_rfif_rd_timer64_msb_t;

/**
 * cvmx_endor_rfif_real_time_timer
 */
union cvmx_endor_rfif_real_time_timer {
	uint32_t u32;
	struct cvmx_endor_rfif_real_time_timer_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer                        : 32; /**< The full 32 bits of the real time timer fed from a core
                                                         clock based counter. */
#else
	uint32_t timer                        : 32;
#endif
	} s;
	struct cvmx_endor_rfif_real_time_timer_s cnf71xx;
};
typedef union cvmx_endor_rfif_real_time_timer cvmx_endor_rfif_real_time_timer_t;

/**
 * cvmx_endor_rfif_rf_clk_timer
 */
union cvmx_endor_rfif_rf_clk_timer {
	uint32_t u32;
	struct cvmx_endor_rfif_rf_clk_timer_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer                        : 32; /**< Timer running off the RF CLK.
                                                         1- The counter is disabled by default;
                                                         2- The counter is enabled by writing 1 to register 066;
                                                         3- The counter waits for the 1PPS to start incrementing
                                                         4- The 1PPS is received and the counter starts
                                                         incrementing;
                                                         5- The counter is reset after receiving the 30th 1PPS
                                                         (after 30 seconds);
                                                         6- The counter keeps incrementing and is reset as in 5,
                                                         unless it is disabled. */
#else
	uint32_t timer                        : 32;
#endif
	} s;
	struct cvmx_endor_rfif_rf_clk_timer_s cnf71xx;
};
typedef union cvmx_endor_rfif_rf_clk_timer cvmx_endor_rfif_rf_clk_timer_t;

/**
 * cvmx_endor_rfif_rf_clk_timer_en
 */
union cvmx_endor_rfif_rf_clk_timer_en {
	uint32_t u32;
	struct cvmx_endor_rfif_rf_clk_timer_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t ena                          : 1;  /**< RF CLK based timer enable
                                                         - 0: Disabled
                                                         - 1: Enabled */
#else
	uint32_t ena                          : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_rfif_rf_clk_timer_en_s cnf71xx;
};
typedef union cvmx_endor_rfif_rf_clk_timer_en cvmx_endor_rfif_rf_clk_timer_en_t;

/**
 * cvmx_endor_rfif_rx_correct_adj
 */
union cvmx_endor_rfif_rx_correct_adj {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_correct_adj_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t offset                       : 4;  /**< Indicates the sample counter offset for the last sample
                                                         flag insertion, which determines when the rx samples
                                                         are dropped or added. This register can take values
                                                         from 0 to 15 and should be configured as follow:
                                                         4, when MIN_SAMPLE_ADJ = 1
                                                         5 , when MIN_SAMPLE_ADJ = 2
                                                         6 , when MIN_SAMPLE_ADJ = 4 */
#else
	uint32_t offset                       : 4;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_endor_rfif_rx_correct_adj_s cnf71xx;
};
typedef union cvmx_endor_rfif_rx_correct_adj cvmx_endor_rfif_rx_correct_adj_t;

/**
 * cvmx_endor_rfif_rx_div_status
 *
 * Notes:
 * In TDD Mode, bits 15:12 are DDR state machine status.
 *
 */
union cvmx_endor_rfif_rx_div_status {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_div_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t rfic_ena                     : 1;  /**< RFIC enabled (in alert state) */
	uint32_t sync_late                    : 1;  /**< Sync late (Used for UE products). */
	uint32_t reserved_19_20               : 2;
	uint32_t thresh_rch                   : 1;  /**< Threshold Reached (RX/RX_div/TX) */
	uint32_t fifo_of                      : 1;  /**< FIFO overflow */
	uint32_t fifo_ur                      : 1;  /**< FIFO underrun */
	uint32_t tx_sm                        : 2;  /**< TX state machine status */
	uint32_t rx_sm                        : 2;  /**< RX state machine status */
	uint32_t hab_req_sm                   : 4;  /**< HAB request manager SM
                                                         - 0: idle
                                                         - 1: wait_cs
                                                         - 2: Term
                                                         - 3: rd_fifo(RX)/ write fifo(TX)
                                                         - 4: wait_th
                                                          Others: not used */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t hab_req_sm                   : 4;
	uint32_t rx_sm                        : 2;
	uint32_t tx_sm                        : 2;
	uint32_t fifo_ur                      : 1;
	uint32_t fifo_of                      : 1;
	uint32_t thresh_rch                   : 1;
	uint32_t reserved_19_20               : 2;
	uint32_t sync_late                    : 1;
	uint32_t rfic_ena                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_rfif_rx_div_status_s cnf71xx;
};
typedef union cvmx_endor_rfif_rx_div_status cvmx_endor_rfif_rx_div_status_t;

/**
 * cvmx_endor_rfif_rx_fifo_cnt
 */
union cvmx_endor_rfif_rx_fifo_cnt {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_fifo_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t cnt                          : 13; /**< RX FIFO fill level. This register can take values
                                                         between 0 and 5136. */
#else
	uint32_t cnt                          : 13;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rfif_rx_fifo_cnt_s  cnf71xx;
};
typedef union cvmx_endor_rfif_rx_fifo_cnt cvmx_endor_rfif_rx_fifo_cnt_t;

/**
 * cvmx_endor_rfif_rx_if_cfg
 */
union cvmx_endor_rfif_rx_if_cfg {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_if_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t eorl                         : 1;  /**< Early or Late TX_FRAME
                                                         - 0: The TX_FRAME asserts after the tx_lead and deasserts
                                                          before the tx_lag
                                                         - 1: The TX_FRAME asserts (3:0) cycles after the
                                                          TX_ON/ENABLE and deasserts (3:0) cycles after the
                                                          TX_ON/ENABLE signal. */
	uint32_t half_lat                     : 1;  /**< Half cycle latency
                                                         - 0: Captures I and Q on the falling and rising edge of
                                                          the clock respectively.
                                                         - 1: Captures I and Q on the rising and falling edge of
                                                          the clock respectively. */
	uint32_t cap_lat                      : 4;  /**< Enable to capture latency
                                                          The data from the RF IC starts and stops being captured
                                                          a number of cycles after the enable pulse.
                                                         - 0: Invalid
                                                         - 1: One cycle latency
                                                         - 2: Two cycles of latency
                                                         - 3: Three cycles of latency
                                                          - ...
                                                          - 15: Seven cycles of latency */
#else
	uint32_t cap_lat                      : 4;
	uint32_t half_lat                     : 1;
	uint32_t eorl                         : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_rfif_rx_if_cfg_s    cnf71xx;
};
typedef union cvmx_endor_rfif_rx_if_cfg cvmx_endor_rfif_rx_if_cfg_t;

/**
 * cvmx_endor_rfif_rx_lead_lag
 */
union cvmx_endor_rfif_rx_lead_lag {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_lead_lag_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t lag                          : 12; /**< unsigned value (lag) on end of window */
	uint32_t lead                         : 12; /**< unsigned value (lead) on beginning of window */
#else
	uint32_t lead                         : 12;
	uint32_t lag                          : 12;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_rx_lead_lag_s  cnf71xx;
};
typedef union cvmx_endor_rfif_rx_lead_lag cvmx_endor_rfif_rx_lead_lag_t;

/**
 * cvmx_endor_rfif_rx_load_cfg
 */
union cvmx_endor_rfif_rx_load_cfg {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_load_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t hidden                       : 1;  /**< Hidden bit set to 1 during synthesis
                                                         (set_case_analysis) if only one destination can be
                                                         programmed at a time. In this case there is no need to
                                                         gate the VLD with the RDYs, to ease timing closure. */
	uint32_t reserved_9_11                : 3;
	uint32_t alt_ant                      : 1;  /**< Send data alternating antenna 0 (first) and antenna 1
                                                         (second) data on the RX HMI interface when set to 1.
                                                         By default, only the data from antenna 0 is sent on
                                                         this interface. */
	uint32_t reserved_3_7                 : 5;
	uint32_t exe3                         : 1;  /**< Setting this bit to 1 indicates the RF_IF to load
                                                         and execute the programmed DMA transfer size (register
                                                         RX_TRANSFER_SIZE) from the FIFO to destination 3. */
	uint32_t exe2                         : 1;  /**< Setting this bit to 1 indicates the RF_IF to load
                                                         and execute the programmed DMA transfer size (register
                                                         RX_TRANSFER_SIZE) from the FIFO to destination 2. */
	uint32_t exe1                         : 1;  /**< Setting this bit to 1 indicates the RF_IF to load
                                                         and execute the programmed DMA transfer size (register
                                                         RX_TRANSFER_SIZE) from the FIFO to destination 1. */
#else
	uint32_t exe1                         : 1;
	uint32_t exe2                         : 1;
	uint32_t exe3                         : 1;
	uint32_t reserved_3_7                 : 5;
	uint32_t alt_ant                      : 1;
	uint32_t reserved_9_11                : 3;
	uint32_t hidden                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rfif_rx_load_cfg_s  cnf71xx;
};
typedef union cvmx_endor_rfif_rx_load_cfg cvmx_endor_rfif_rx_load_cfg_t;

/**
 * cvmx_endor_rfif_rx_offset
 */
union cvmx_endor_rfif_rx_offset {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_offset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t offset                       : 20; /**< Indicates the number of RF clock cycles after the
                                                         GPS/ETH 1PPS is received before the start of the RX
                                                         frame. See description Figure 44. */
#else
	uint32_t offset                       : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_rx_offset_s    cnf71xx;
};
typedef union cvmx_endor_rfif_rx_offset cvmx_endor_rfif_rx_offset_t;

/**
 * cvmx_endor_rfif_rx_offset_adj_scnt
 */
union cvmx_endor_rfif_rx_offset_adj_scnt {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_offset_adj_scnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t cnt                          : 20; /**< Indicates the RX sample count at which the 1PPS
                                                         incremental adjustments will be applied. */
#else
	uint32_t cnt                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_rx_offset_adj_scnt_s cnf71xx;
};
typedef union cvmx_endor_rfif_rx_offset_adj_scnt cvmx_endor_rfif_rx_offset_adj_scnt_t;

/**
 * cvmx_endor_rfif_rx_status
 *
 * Notes:
 * In TDD Mode, bits 15:12 are DDR state machine status.
 *
 */
union cvmx_endor_rfif_rx_status {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t rfic_ena                     : 1;  /**< RFIC enabled (in alert state) */
	uint32_t sync_late                    : 1;  /**< Sync late (Used for UE products). */
	uint32_t reserved_19_20               : 2;
	uint32_t thresh_rch                   : 1;  /**< Threshold Reached (RX/RX_div/TX) */
	uint32_t fifo_of                      : 1;  /**< FIFO overflow */
	uint32_t fifo_ur                      : 1;  /**< FIFO underrun */
	uint32_t tx_sm                        : 2;  /**< TX state machine status */
	uint32_t rx_sm                        : 2;  /**< RX state machine status */
	uint32_t hab_req_sm                   : 4;  /**< HAB request manager SM
                                                         - 0: idle
                                                         - 1: wait_cs
                                                         - 2: Term
                                                         - 3: rd_fifo(RX)/ write fifo(TX)
                                                         - 4: wait_th
                                                          Others: not used */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t hab_req_sm                   : 4;
	uint32_t rx_sm                        : 2;
	uint32_t tx_sm                        : 2;
	uint32_t fifo_ur                      : 1;
	uint32_t fifo_of                      : 1;
	uint32_t thresh_rch                   : 1;
	uint32_t reserved_19_20               : 2;
	uint32_t sync_late                    : 1;
	uint32_t rfic_ena                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_rfif_rx_status_s    cnf71xx;
};
typedef union cvmx_endor_rfif_rx_status cvmx_endor_rfif_rx_status_t;

/**
 * cvmx_endor_rfif_rx_sync_scnt
 */
union cvmx_endor_rfif_rx_sync_scnt {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_sync_scnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t cnt                          : 20; /**< Sample count at which the start of frame reference will
                                                         be modified as described with register 0x30. */
#else
	uint32_t cnt                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_rx_sync_scnt_s cnf71xx;
};
typedef union cvmx_endor_rfif_rx_sync_scnt cvmx_endor_rfif_rx_sync_scnt_t;

/**
 * cvmx_endor_rfif_rx_sync_value
 */
union cvmx_endor_rfif_rx_sync_value {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_sync_value_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t val                          : 20; /**< RX Synchronization offset value. This register
                                                         indicates the sample number at which the start of frame
                                                         must be moved to. This value must be smaller than
                                                         FRAME_L, but it cannot be negative. See below how the
                                                         sample count gets updated based on registers 0x30 and
                                                         0x31 at sample count RX_SYNC_VALUE.
                                                         If RX_SYNC_SCNT >= RX_SYNC_VALUE
                                                         sample_count = RX_SYNC_SCNT ? RX_SYNC_VALUE + 1
                                                         Else
                                                         sample_count = RX_SYNC_SCNT + FRAME_L ?
                                                         RX_SYNC_VALUE + 1
                                                         Note this is not used for eNB products, only for UE
                                                         products.
                                                         Note this register is cleared after the correction is
                                                         applied. */
#else
	uint32_t val                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_rx_sync_value_s cnf71xx;
};
typedef union cvmx_endor_rfif_rx_sync_value cvmx_endor_rfif_rx_sync_value_t;

/**
 * cvmx_endor_rfif_rx_th
 */
union cvmx_endor_rfif_rx_th {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_th_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_12_31               : 20;
	uint32_t thr                          : 12; /**< FIFO level reached before granting a RX DMA request.
                                                         This RX FIFO fill level threshold can be used
                                                         in two ways:
                                                              1- When the FIFO fill level reaches the threshold,
                                                         there is enough data in the FIFO to start the data
                                                         transfer, so it grants a DMA transfer from the RX FIFO
                                                         to the HAB's memory.
                                                              2- It can also be used to generate an interrupt to
                                                         the DSP when the FIFO threshold is reached. */
#else
	uint32_t thr                          : 12;
	uint32_t reserved_12_31               : 20;
#endif
	} s;
	struct cvmx_endor_rfif_rx_th_s        cnf71xx;
};
typedef union cvmx_endor_rfif_rx_th cvmx_endor_rfif_rx_th_t;

/**
 * cvmx_endor_rfif_rx_transfer_size
 */
union cvmx_endor_rfif_rx_transfer_size {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_transfer_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t size                         : 13; /**< Indicates the size of the DMA data transfer from the
                                                         rf_if RX FIFO out via the HMI IF.
                                                         The DMA transfers to the HAB1 and HAB2 */
#else
	uint32_t size                         : 13;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rfif_rx_transfer_size_s cnf71xx;
};
typedef union cvmx_endor_rfif_rx_transfer_size cvmx_endor_rfif_rx_transfer_size_t;

/**
 * cvmx_endor_rfif_rx_w_e#
 */
union cvmx_endor_rfif_rx_w_ex {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_w_ex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t end_cnt                      : 20; /**< End count for each of the 4 RX windows. The maximum
                                                         value should be FRAME_L, unless the window must stay
                                                         opened for ever. */
#else
	uint32_t end_cnt                      : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_rx_w_ex_s      cnf71xx;
};
typedef union cvmx_endor_rfif_rx_w_ex cvmx_endor_rfif_rx_w_ex_t;

/**
 * cvmx_endor_rfif_rx_w_s#
 */
union cvmx_endor_rfif_rx_w_sx {
	uint32_t u32;
	struct cvmx_endor_rfif_rx_w_sx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t start_pnt                    : 20; /**< Start points for each of the 4 RX windows
                                                         Some restrictions applies to the start and end values:
                                                         1- The first RX window must always start at the sample
                                                         count 0.
                                                         2- The other start point must be greater than rx_lead,
                                                         refer to 0x008.
                                                         3- All start point values must be smaller than the
                                                         endpoints in TDD mode.
                                                         4- RX windows have priorities over TX windows in TDD
                                                         mode.
                                                         5- There must be a minimum of 7 samples between
                                                         closing a window and opening a new one. However, it is
                                                         recommended to leave a 10 samples gap. Note that this
                                                         number could increase with different RF ICs used. */
#else
	uint32_t start_pnt                    : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_rx_w_sx_s      cnf71xx;
};
typedef union cvmx_endor_rfif_rx_w_sx cvmx_endor_rfif_rx_w_sx_t;

/**
 * cvmx_endor_rfif_sample_adj_cfg
 */
union cvmx_endor_rfif_sample_adj_cfg {
	uint32_t u32;
	struct cvmx_endor_rfif_sample_adj_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t adj                          : 1;  /**< Indicates whether samples must be removed from the
                                                          beginning or the end of the frame.
                                                         - 1: add/remove samples from the beginning of the frame
                                                         - 0: add/remove samples from the end of the frame
                                                          (default) */
#else
	uint32_t adj                          : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_rfif_sample_adj_cfg_s cnf71xx;
};
typedef union cvmx_endor_rfif_sample_adj_cfg cvmx_endor_rfif_sample_adj_cfg_t;

/**
 * cvmx_endor_rfif_sample_adj_error
 */
union cvmx_endor_rfif_sample_adj_error {
	uint32_t u32;
	struct cvmx_endor_rfif_sample_adj_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t offset                       : 32; /**< Count of the number of times the TX FIFO did not have
                                                         enough IQ samples to be dropped for a TX timing
                                                         adjustment.
                                                         0-7 = TX FIFO sample adjustment error
                                                         - 16:23 = TX DIV sample adjustment error */
#else
	uint32_t offset                       : 32;
#endif
	} s;
	struct cvmx_endor_rfif_sample_adj_error_s cnf71xx;
};
typedef union cvmx_endor_rfif_sample_adj_error cvmx_endor_rfif_sample_adj_error_t;

/**
 * cvmx_endor_rfif_sample_cnt
 */
union cvmx_endor_rfif_sample_cnt {
	uint32_t u32;
	struct cvmx_endor_rfif_sample_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t cnt                          : 20; /**< Sample count modulo FRAME_L. The start of frame is
                                                         aligned with count 0. */
#else
	uint32_t cnt                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_sample_cnt_s   cnf71xx;
};
typedef union cvmx_endor_rfif_sample_cnt cvmx_endor_rfif_sample_cnt_t;

/**
 * cvmx_endor_rfif_skip_frm_cnt_bits
 */
union cvmx_endor_rfif_skip_frm_cnt_bits {
	uint32_t u32;
	struct cvmx_endor_rfif_skip_frm_cnt_bits_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t bits                         : 2;  /**< Indicates the number of sample count bits to skip, in
                                                          order to reduce the sample count update frequency and
                                                          permit a reliable clock crossing from the RF to the
                                                          HAB clock domain.
                                                         - 0: No bits are skipped
                                                          - ...
                                                         - 3: 3 bits are skipped */
#else
	uint32_t bits                         : 2;
	uint32_t reserved_2_31                : 30;
#endif
	} s;
	struct cvmx_endor_rfif_skip_frm_cnt_bits_s cnf71xx;
};
typedef union cvmx_endor_rfif_skip_frm_cnt_bits cvmx_endor_rfif_skip_frm_cnt_bits_t;

/**
 * cvmx_endor_rfif_spi_#_ll
 */
union cvmx_endor_rfif_spi_x_ll {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_x_ll_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t num                          : 20; /**< SPI event X start sample count */
#else
	uint32_t num                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_spi_x_ll_s     cnf71xx;
};
typedef union cvmx_endor_rfif_spi_x_ll cvmx_endor_rfif_spi_x_ll_t;

/**
 * cvmx_endor_rfif_spi_cmd_attr#
 */
union cvmx_endor_rfif_spi_cmd_attrx {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_cmd_attrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t slave                        : 1;  /**< Slave select (in case there are 2 ADI chips)
                                                         - 0: slave 1
                                                         - 1: slave 2 */
	uint32_t bytes                        : 1;  /**< Number of data bytes transfer
                                                         - 0: 1 byte transfer mode
                                                         - 1: 2 bytes transfer mode */
	uint32_t gen_int                      : 1;  /**< Generate an interrupt upon the SPI event completion:
                                                         - 0: no interrupt generated  1: interrupt generated */
	uint32_t rw                           : 1;  /**< r/w: r:0 ; w:1. */
#else
	uint32_t rw                           : 1;
	uint32_t gen_int                      : 1;
	uint32_t bytes                        : 1;
	uint32_t slave                        : 1;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_endor_rfif_spi_cmd_attrx_s cnf71xx;
};
typedef union cvmx_endor_rfif_spi_cmd_attrx cvmx_endor_rfif_spi_cmd_attrx_t;

/**
 * cvmx_endor_rfif_spi_cmds#
 */
union cvmx_endor_rfif_spi_cmdsx {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_cmdsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t word                         : 24; /**< Spi command word. */
#else
	uint32_t word                         : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_spi_cmdsx_s    cnf71xx;
};
typedef union cvmx_endor_rfif_spi_cmdsx cvmx_endor_rfif_spi_cmdsx_t;

/**
 * cvmx_endor_rfif_spi_conf0
 */
union cvmx_endor_rfif_spi_conf0 {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_conf0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t num_cmds3                    : 6;  /**< Number of SPI cmds to transfer for event 3 */
	uint32_t num_cmds2                    : 6;  /**< Number of SPI cmds to transfer for event 2 */
	uint32_t num_cmds1                    : 6;  /**< Number of SPI cmds to transfer for event 1 */
	uint32_t num_cmds0                    : 6;  /**< Number of SPI cmds to transfer for event 0 */
#else
	uint32_t num_cmds0                    : 6;
	uint32_t num_cmds1                    : 6;
	uint32_t num_cmds2                    : 6;
	uint32_t num_cmds3                    : 6;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_spi_conf0_s    cnf71xx;
};
typedef union cvmx_endor_rfif_spi_conf0 cvmx_endor_rfif_spi_conf0_t;

/**
 * cvmx_endor_rfif_spi_conf1
 */
union cvmx_endor_rfif_spi_conf1 {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_conf1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t start3                       : 6;  /**< SPI commands start address for event 3 */
	uint32_t start2                       : 6;  /**< SPI commands start address for event 2 */
	uint32_t start1                       : 6;  /**< SPI commands start address for event 1 */
	uint32_t start0                       : 6;  /**< SPI commands start address for event 0 */
#else
	uint32_t start0                       : 6;
	uint32_t start1                       : 6;
	uint32_t start2                       : 6;
	uint32_t start3                       : 6;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_spi_conf1_s    cnf71xx;
};
typedef union cvmx_endor_rfif_spi_conf1 cvmx_endor_rfif_spi_conf1_t;

/**
 * cvmx_endor_rfif_spi_ctrl
 */
union cvmx_endor_rfif_spi_ctrl {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ctrl                         : 32; /**< Control */
#else
	uint32_t ctrl                         : 32;
#endif
	} s;
	struct cvmx_endor_rfif_spi_ctrl_s     cnf71xx;
};
typedef union cvmx_endor_rfif_spi_ctrl cvmx_endor_rfif_spi_ctrl_t;

/**
 * cvmx_endor_rfif_spi_din#
 */
union cvmx_endor_rfif_spi_dinx {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_dinx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t data                         : 16; /**< Data read back from spi commands. */
#else
	uint32_t data                         : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_endor_rfif_spi_dinx_s     cnf71xx;
};
typedef union cvmx_endor_rfif_spi_dinx cvmx_endor_rfif_spi_dinx_t;

/**
 * cvmx_endor_rfif_spi_rx_data
 */
union cvmx_endor_rfif_spi_rx_data {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_rx_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rd_data                      : 32; /**< SPI Read Data */
#else
	uint32_t rd_data                      : 32;
#endif
	} s;
	struct cvmx_endor_rfif_spi_rx_data_s  cnf71xx;
};
typedef union cvmx_endor_rfif_spi_rx_data cvmx_endor_rfif_spi_rx_data_t;

/**
 * cvmx_endor_rfif_spi_status
 */
union cvmx_endor_rfif_spi_status {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_12_31               : 20;
	uint32_t sr_state                     : 4;  /**< SPI State Machine
                                                         1 : INIT
                                                         2 : IDLE
                                                         3 : WAIT_FIFO
                                                         4 : READ_FIFO
                                                         5 : LOAD_SR
                                                         6 : SHIFT_SR
                                                         7 : WAIT_CLK
                                                         8 : WAIT_FOR_SS */
	uint32_t rx_fifo_lvl                  : 4;  /**< Level of RX FIFO */
	uint32_t tx_fifo_lvl                  : 4;  /**< Level of TX FIFO */
#else
	uint32_t tx_fifo_lvl                  : 4;
	uint32_t rx_fifo_lvl                  : 4;
	uint32_t sr_state                     : 4;
	uint32_t reserved_12_31               : 20;
#endif
	} s;
	struct cvmx_endor_rfif_spi_status_s   cnf71xx;
};
typedef union cvmx_endor_rfif_spi_status cvmx_endor_rfif_spi_status_t;

/**
 * cvmx_endor_rfif_spi_tx_data
 */
union cvmx_endor_rfif_spi_tx_data {
	uint32_t u32;
	struct cvmx_endor_rfif_spi_tx_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t write                        : 1;  /**< When set, execute write. Otherwise, read. */
	uint32_t reserved_25_30               : 6;
	uint32_t addr                         : 9;  /**< SPI Address */
	uint32_t data                         : 8;  /**< SPI Data */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t data                         : 8;
	uint32_t addr                         : 9;
	uint32_t reserved_25_30               : 6;
	uint32_t write                        : 1;
#endif
	} s;
	struct cvmx_endor_rfif_spi_tx_data_s  cnf71xx;
};
typedef union cvmx_endor_rfif_spi_tx_data cvmx_endor_rfif_spi_tx_data_t;

/**
 * cvmx_endor_rfif_timer64_cfg
 */
union cvmx_endor_rfif_timer64_cfg {
	uint32_t u32;
	struct cvmx_endor_rfif_timer64_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t clks                         : 8;  /**< 7-0: Number of rf clock cycles per 64-bit timer
                                                         increment. Set to n for n+1 cycles (default=0x7F for
                                                         128 cycles).  The valid range for the register is 3 to
                                                         255. */
#else
	uint32_t clks                         : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rfif_timer64_cfg_s  cnf71xx;
};
typedef union cvmx_endor_rfif_timer64_cfg cvmx_endor_rfif_timer64_cfg_t;

/**
 * cvmx_endor_rfif_timer64_en
 *
 * Notes:
 * This is how the 64-bit timer works:
 * 1- Configuration
 *     - Write counter LSB (reg:0x69)
 *     - Write counter MSB (reg:0x6A)
 *     - Write config (reg:0x68)
 * 2- Enable the counter
 * 3- Wait for the 1PPS
 * 4- Start incrementing the counter every n+1 rf clock cycles
 * 5- Read the MSB and LSB registers (reg:0x6B and 0x6C)
 *
 * 6- There is no 64-bit snapshot mechanism. Software has to consider the
 *    32 LSB might rollover and increment the 32 MSB between the LSB and the
 *    MSB reads. You may want to use the following concatenation recipe:
 *
 * a) Read the 32 MSB (MSB1)
 * b) Read the 32 LSB
 * c) Read the 32 MSB again (MSB2)
 * d) Concatenate the 32 MSB an 32 LSB
 *      -If both 32 MSB are equal or LSB(31)=1, concatenate MSB1 and LSB
 *      -Else concatenate the MSB2 and LSB
 */
union cvmx_endor_rfif_timer64_en {
	uint32_t u32;
	struct cvmx_endor_rfif_timer64_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t ena                          : 1;  /**< Enable for the 64-bit rf clock based timer.
                                                         - 0: Disabled
                                                         - 1: Enabled */
#else
	uint32_t ena                          : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_endor_rfif_timer64_en_s   cnf71xx;
};
typedef union cvmx_endor_rfif_timer64_en cvmx_endor_rfif_timer64_en_t;

/**
 * cvmx_endor_rfif_tti_scnt_int#
 */
union cvmx_endor_rfif_tti_scnt_intx {
	uint32_t u32;
	struct cvmx_endor_rfif_tti_scnt_intx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t intr                         : 20; /**< TTI Sample Count Interrupt:
                                                         Indicates the sample count of the selected reference
                                                         counter at which to generate an interrupt. */
#else
	uint32_t intr                         : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_tti_scnt_intx_s cnf71xx;
};
typedef union cvmx_endor_rfif_tti_scnt_intx cvmx_endor_rfif_tti_scnt_intx_t;

/**
 * cvmx_endor_rfif_tti_scnt_int_clr
 */
union cvmx_endor_rfif_tti_scnt_int_clr {
	uint32_t u32;
	struct cvmx_endor_rfif_tti_scnt_int_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t cnt                          : 8;  /**< TTI Sample Count Interrupt Status register:
                                                         Writing 0x1 to clear the TTI_SCNT_INT_STAT(0), writing
                                                         0x2 to clear the TTI_SCNT_INT_STAT(1) and so on. */
#else
	uint32_t cnt                          : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rfif_tti_scnt_int_clr_s cnf71xx;
};
typedef union cvmx_endor_rfif_tti_scnt_int_clr cvmx_endor_rfif_tti_scnt_int_clr_t;

/**
 * cvmx_endor_rfif_tti_scnt_int_en
 */
union cvmx_endor_rfif_tti_scnt_int_en {
	uint32_t u32;
	struct cvmx_endor_rfif_tti_scnt_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t ena                          : 8;  /**< TTI Sample Counter Interrupt Enable:
                                                         Bit 0: 1  Enables TTI_SCNT_INT_0
                                                         Bit 1: 1 Enables TTI_SCNT_INT_1
                                                         - ...
                                                         Bit 7: 1  Enables TTI_SCNT_INT_7
                                                         Note these interrupts are disabled by default (=0x00). */
#else
	uint32_t ena                          : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rfif_tti_scnt_int_en_s cnf71xx;
};
typedef union cvmx_endor_rfif_tti_scnt_int_en cvmx_endor_rfif_tti_scnt_int_en_t;

/**
 * cvmx_endor_rfif_tti_scnt_int_map
 */
union cvmx_endor_rfif_tti_scnt_int_map {
	uint32_t u32;
	struct cvmx_endor_rfif_tti_scnt_int_map_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t map                          : 8;  /**< TTI Sample Count Interrupt Mapping to a Reference
                                                         Counter:
                                                         Indicates the reference counter the TTI Sample Count
                                                         Interrupts must be generated from. A value of 0
                                                         indicates the RX reference counter (default) and a
                                                         value of 1 indicates the TX reference counter. The
                                                         bit 0 is associated with TTI_SCNT_INT_0, the bit 1
                                                         is associated with TTI_SCNT_INT_1 and so on.
                                                         Note that This register has not effect in TDD mode,
                                                         only in FDD mode. */
#else
	uint32_t map                          : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rfif_tti_scnt_int_map_s cnf71xx;
};
typedef union cvmx_endor_rfif_tti_scnt_int_map cvmx_endor_rfif_tti_scnt_int_map_t;

/**
 * cvmx_endor_rfif_tti_scnt_int_stat
 */
union cvmx_endor_rfif_tti_scnt_int_stat {
	uint32_t u32;
	struct cvmx_endor_rfif_tti_scnt_int_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t cnt                          : 8;  /**< TTI Sample Count Interrupt Status register:
                                                         Indicates if a TTI_SCNT_INT_X occurred (1) or not (0).
                                                         The bit 0 is associated with TTI_SCNT_INT_0 and so on
                                                         incrementally. Writing a 1 will clear the interrupt
                                                         bit. */
#else
	uint32_t cnt                          : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rfif_tti_scnt_int_stat_s cnf71xx;
};
typedef union cvmx_endor_rfif_tti_scnt_int_stat cvmx_endor_rfif_tti_scnt_int_stat_t;

/**
 * cvmx_endor_rfif_tx_div_status
 *
 * Notes:
 * In TDD Mode, bits 15:12 are DDR state machine status.
 *
 */
union cvmx_endor_rfif_tx_div_status {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_div_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t rfic_ena                     : 1;  /**< RFIC enabled (in alert state) */
	uint32_t sync_late                    : 1;  /**< Sync late (Used for UE products). */
	uint32_t reserved_19_20               : 2;
	uint32_t thresh_rch                   : 1;  /**< Threshold Reached (RX/RX_div/TX) */
	uint32_t fifo_of                      : 1;  /**< FIFO overflow */
	uint32_t fifo_ur                      : 1;  /**< FIFO underrun */
	uint32_t tx_sm                        : 2;  /**< TX state machine status */
	uint32_t rx_sm                        : 2;  /**< RX state machine status */
	uint32_t hab_req_sm                   : 4;  /**< HAB request manager SM
                                                         - 0: idle
                                                         - 1: wait_cs
                                                         - 2: Term
                                                         - 3: rd_fifo(RX)/ write fifo(TX)
                                                         - 4: wait_th
                                                          Others: not used */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t hab_req_sm                   : 4;
	uint32_t rx_sm                        : 2;
	uint32_t tx_sm                        : 2;
	uint32_t fifo_ur                      : 1;
	uint32_t fifo_of                      : 1;
	uint32_t thresh_rch                   : 1;
	uint32_t reserved_19_20               : 2;
	uint32_t sync_late                    : 1;
	uint32_t rfic_ena                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_rfif_tx_div_status_s cnf71xx;
};
typedef union cvmx_endor_rfif_tx_div_status cvmx_endor_rfif_tx_div_status_t;

/**
 * cvmx_endor_rfif_tx_if_cfg
 */
union cvmx_endor_rfif_tx_if_cfg {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_if_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t mode                         : 1;  /**< TX communication mode
                                                         - 0: TX SISO (default)
                                                         - 1: TX MIMO */
	uint32_t dis_sch                      : 1;  /**< Disabled antenna driving scheme (TX SISO/RX MIMO
                                                          feature only)
                                                         - 0: Constant 0 for debugging (default)
                                                         - 1: Same as previous cycle to minimize IO switching */
	uint32_t antenna                      : 2;  /**< Transmit on antenna A and/or B (TX SISO/RX MIMO
                                                          feature only)
                                                         - 0: Transmit on antenna A (default)
                                                         - 1: Transmit on antenna B
                                                         - 2: Transmit on A and B
                                                         - 3: Reserved */
#else
	uint32_t antenna                      : 2;
	uint32_t dis_sch                      : 1;
	uint32_t mode                         : 1;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_endor_rfif_tx_if_cfg_s    cnf71xx;
};
typedef union cvmx_endor_rfif_tx_if_cfg cvmx_endor_rfif_tx_if_cfg_t;

/**
 * cvmx_endor_rfif_tx_lead_lag
 */
union cvmx_endor_rfif_tx_lead_lag {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_lead_lag_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t lag                          : 12; /**< unsigned value (lag) on end of window */
	uint32_t lead                         : 12; /**< unsigned value (lead) on beginning of window */
#else
	uint32_t lead                         : 12;
	uint32_t lag                          : 12;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rfif_tx_lead_lag_s  cnf71xx;
};
typedef union cvmx_endor_rfif_tx_lead_lag cvmx_endor_rfif_tx_lead_lag_t;

/**
 * cvmx_endor_rfif_tx_offset
 */
union cvmx_endor_rfif_tx_offset {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_offset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t offset                       : 20; /**< Indicates the number of RF clock cycles after the
                                                         GPS/ETH 1PPS is received before the start of the RX
                                                         frame. See description Figure 44. */
#else
	uint32_t offset                       : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_tx_offset_s    cnf71xx;
};
typedef union cvmx_endor_rfif_tx_offset cvmx_endor_rfif_tx_offset_t;

/**
 * cvmx_endor_rfif_tx_offset_adj_scnt
 */
union cvmx_endor_rfif_tx_offset_adj_scnt {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_offset_adj_scnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t cnt                          : 20; /**< Indicates the TX sample count at which the 1PPS
                                                         incremental adjustments will be applied. */
#else
	uint32_t cnt                          : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_tx_offset_adj_scnt_s cnf71xx;
};
typedef union cvmx_endor_rfif_tx_offset_adj_scnt cvmx_endor_rfif_tx_offset_adj_scnt_t;

/**
 * cvmx_endor_rfif_tx_status
 *
 * Notes:
 * In TDD Mode, bits 15:12 are DDR state machine status.
 *
 */
union cvmx_endor_rfif_tx_status {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t rfic_ena                     : 1;  /**< RFIC enabled (in alert state) */
	uint32_t sync_late                    : 1;  /**< Sync late (Used for UE products). */
	uint32_t reserved_19_20               : 2;
	uint32_t thresh_rch                   : 1;  /**< Threshold Reached (RX/RX_div/TX) */
	uint32_t fifo_of                      : 1;  /**< FIFO overflow */
	uint32_t fifo_ur                      : 1;  /**< FIFO underrun */
	uint32_t tx_sm                        : 2;  /**< TX state machine status */
	uint32_t rx_sm                        : 2;  /**< RX state machine status */
	uint32_t hab_req_sm                   : 4;  /**< HAB request manager SM
                                                         - 0: idle
                                                         - 1: wait_cs
                                                         - 2: Term
                                                         - 3: rd_fifo(RX)/ write fifo(TX)
                                                         - 4: wait_th
                                                          Others: not used */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t hab_req_sm                   : 4;
	uint32_t rx_sm                        : 2;
	uint32_t tx_sm                        : 2;
	uint32_t fifo_ur                      : 1;
	uint32_t fifo_of                      : 1;
	uint32_t thresh_rch                   : 1;
	uint32_t reserved_19_20               : 2;
	uint32_t sync_late                    : 1;
	uint32_t rfic_ena                     : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_endor_rfif_tx_status_s    cnf71xx;
};
typedef union cvmx_endor_rfif_tx_status cvmx_endor_rfif_tx_status_t;

/**
 * cvmx_endor_rfif_tx_th
 */
union cvmx_endor_rfif_tx_th {
	uint32_t u32;
	struct cvmx_endor_rfif_tx_th_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_12_31               : 20;
	uint32_t thr                          : 12; /**< FIFO level reached before granting a TX DMA request.
                                                         This TX FIFO fill level threshold can be used
                                                         in two ways:
                                                              1- When the FIFO fill level reaches the threshold,
                                                         there is enough data in the FIFO to start the data
                                                         transfer, so it grants a DMA transfer from the TX FIFO
                                                         to the HAB's memory.
                                                              2- It can also be used to generate an interrupt to
                                                         the DSP when the FIFO threshold is reached. */
#else
	uint32_t thr                          : 12;
	uint32_t reserved_12_31               : 20;
#endif
	} s;
	struct cvmx_endor_rfif_tx_th_s        cnf71xx;
};
typedef union cvmx_endor_rfif_tx_th cvmx_endor_rfif_tx_th_t;

/**
 * cvmx_endor_rfif_win_en
 */
union cvmx_endor_rfif_win_en {
	uint32_t u32;
	struct cvmx_endor_rfif_win_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t enable                       : 4;  /**< Receive windows enable (all enabled by default)
                                                         Bit 0: 1 window 1 enabled, 0 window 1 disabled
                                                         - ...
                                                         Bit 3: 1 window 3 enabled, 0 window 3 disabled.
                                                         Bits 23-4: not used */
#else
	uint32_t enable                       : 4;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_endor_rfif_win_en_s       cnf71xx;
};
typedef union cvmx_endor_rfif_win_en cvmx_endor_rfif_win_en_t;

/**
 * cvmx_endor_rfif_win_upd_scnt
 */
union cvmx_endor_rfif_win_upd_scnt {
	uint32_t u32;
	struct cvmx_endor_rfif_win_upd_scnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t scnt                         : 20; /**< Receive window update sample count. This is the count
                                                         at which the following registers newly programmed value
                                                         will take effect. RX_WIN_EN(3-0), RX_W_S (19-0),
                                                         RX_W_E(19-0), NUM_RX_WIN(3-0),  FRAME_L(19-0),
                                                         RX_LEAD_LAG(23-0) */
#else
	uint32_t scnt                         : 20;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_endor_rfif_win_upd_scnt_s cnf71xx;
};
typedef union cvmx_endor_rfif_win_upd_scnt cvmx_endor_rfif_win_upd_scnt_t;

/**
 * cvmx_endor_rfif_wr_timer64_lsb
 */
union cvmx_endor_rfif_wr_timer64_lsb {
	uint32_t u32;
	struct cvmx_endor_rfif_wr_timer64_lsb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t val                          : 32; /**< 64-bit timer initial value of the 32 LSB. */
#else
	uint32_t val                          : 32;
#endif
	} s;
	struct cvmx_endor_rfif_wr_timer64_lsb_s cnf71xx;
};
typedef union cvmx_endor_rfif_wr_timer64_lsb cvmx_endor_rfif_wr_timer64_lsb_t;

/**
 * cvmx_endor_rfif_wr_timer64_msb
 */
union cvmx_endor_rfif_wr_timer64_msb {
	uint32_t u32;
	struct cvmx_endor_rfif_wr_timer64_msb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t val                          : 32; /**< 64-bit timer initial value of the 32 MSB. */
#else
	uint32_t val                          : 32;
#endif
	} s;
	struct cvmx_endor_rfif_wr_timer64_msb_s cnf71xx;
};
typedef union cvmx_endor_rfif_wr_timer64_msb cvmx_endor_rfif_wr_timer64_msb_t;

/**
 * cvmx_endor_rstclk_clkenb0_clr
 */
union cvmx_endor_rstclk_clkenb0_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_clkenb0_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t axidma                       : 1;  /**< abc */
	uint32_t txseq                        : 1;  /**< abc */
	uint32_t v3genc                       : 1;  /**< abc */
	uint32_t ifftpapr                     : 1;  /**< abc */
	uint32_t lteenc                       : 1;  /**< abc */
	uint32_t vdec                         : 1;  /**< abc */
	uint32_t turbodsp                     : 1;  /**< abc */
	uint32_t turbophy                     : 1;  /**< abc */
	uint32_t rx1seq                       : 1;  /**< abc */
	uint32_t dftdmap                      : 1;  /**< abc */
	uint32_t rx0seq                       : 1;  /**< abc */
	uint32_t rachfe                       : 1;  /**< abc */
	uint32_t ulfe                         : 1;  /**< abc */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachfe                       : 1;
	uint32_t rx0seq                       : 1;
	uint32_t dftdmap                      : 1;
	uint32_t rx1seq                       : 1;
	uint32_t turbophy                     : 1;
	uint32_t turbodsp                     : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t v3genc                       : 1;
	uint32_t txseq                        : 1;
	uint32_t axidma                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rstclk_clkenb0_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_clkenb0_clr cvmx_endor_rstclk_clkenb0_clr_t;

/**
 * cvmx_endor_rstclk_clkenb0_set
 */
union cvmx_endor_rstclk_clkenb0_set {
	uint32_t u32;
	struct cvmx_endor_rstclk_clkenb0_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t axidma                       : 1;  /**< abc */
	uint32_t txseq                        : 1;  /**< abc */
	uint32_t v3genc                       : 1;  /**< abc */
	uint32_t ifftpapr                     : 1;  /**< abc */
	uint32_t lteenc                       : 1;  /**< abc */
	uint32_t vdec                         : 1;  /**< abc */
	uint32_t turbodsp                     : 1;  /**< abc */
	uint32_t turbophy                     : 1;  /**< abc */
	uint32_t rx1seq                       : 1;  /**< abc */
	uint32_t dftdmap                      : 1;  /**< abc */
	uint32_t rx0seq                       : 1;  /**< abc */
	uint32_t rachfe                       : 1;  /**< abc */
	uint32_t ulfe                         : 1;  /**< abc */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachfe                       : 1;
	uint32_t rx0seq                       : 1;
	uint32_t dftdmap                      : 1;
	uint32_t rx1seq                       : 1;
	uint32_t turbophy                     : 1;
	uint32_t turbodsp                     : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t v3genc                       : 1;
	uint32_t txseq                        : 1;
	uint32_t axidma                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rstclk_clkenb0_set_s cnf71xx;
};
typedef union cvmx_endor_rstclk_clkenb0_set cvmx_endor_rstclk_clkenb0_set_t;

/**
 * cvmx_endor_rstclk_clkenb0_state
 */
union cvmx_endor_rstclk_clkenb0_state {
	uint32_t u32;
	struct cvmx_endor_rstclk_clkenb0_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t axidma                       : 1;  /**< abc */
	uint32_t txseq                        : 1;  /**< abc */
	uint32_t v3genc                       : 1;  /**< abc */
	uint32_t ifftpapr                     : 1;  /**< abc */
	uint32_t lteenc                       : 1;  /**< abc */
	uint32_t vdec                         : 1;  /**< abc */
	uint32_t turbodsp                     : 1;  /**< abc */
	uint32_t turbophy                     : 1;  /**< abc */
	uint32_t rx1seq                       : 1;  /**< abc */
	uint32_t dftdmap                      : 1;  /**< abc */
	uint32_t rx0seq                       : 1;  /**< abc */
	uint32_t rachfe                       : 1;  /**< abc */
	uint32_t ulfe                         : 1;  /**< abc */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachfe                       : 1;
	uint32_t rx0seq                       : 1;
	uint32_t dftdmap                      : 1;
	uint32_t rx1seq                       : 1;
	uint32_t turbophy                     : 1;
	uint32_t turbodsp                     : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t v3genc                       : 1;
	uint32_t txseq                        : 1;
	uint32_t axidma                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rstclk_clkenb0_state_s cnf71xx;
};
typedef union cvmx_endor_rstclk_clkenb0_state cvmx_endor_rstclk_clkenb0_state_t;

/**
 * cvmx_endor_rstclk_clkenb1_clr
 */
union cvmx_endor_rstclk_clkenb1_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_clkenb1_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_7_31                : 25;
	uint32_t token                        : 1;  /**< abc */
	uint32_t tile3dsp                     : 1;  /**< abc */
	uint32_t tile2dsp                     : 1;  /**< abc */
	uint32_t tile1dsp                     : 1;  /**< abc */
	uint32_t rfspi                        : 1;  /**< abc */
	uint32_t rfif_hab                     : 1;  /**< abc */
	uint32_t rfif_rf                      : 1;  /**< abc */
#else
	uint32_t rfif_rf                      : 1;
	uint32_t rfif_hab                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t tile1dsp                     : 1;
	uint32_t tile2dsp                     : 1;
	uint32_t tile3dsp                     : 1;
	uint32_t token                        : 1;
	uint32_t reserved_7_31                : 25;
#endif
	} s;
	struct cvmx_endor_rstclk_clkenb1_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_clkenb1_clr cvmx_endor_rstclk_clkenb1_clr_t;

/**
 * cvmx_endor_rstclk_clkenb1_set
 */
union cvmx_endor_rstclk_clkenb1_set {
	uint32_t u32;
	struct cvmx_endor_rstclk_clkenb1_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_7_31                : 25;
	uint32_t token                        : 1;  /**< abc */
	uint32_t tile3dsp                     : 1;  /**< abc */
	uint32_t tile2dsp                     : 1;  /**< abc */
	uint32_t tile1dsp                     : 1;  /**< abc */
	uint32_t rfspi                        : 1;  /**< abc */
	uint32_t rfif_hab                     : 1;  /**< abc */
	uint32_t rfif_rf                      : 1;  /**< abc */
#else
	uint32_t rfif_rf                      : 1;
	uint32_t rfif_hab                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t tile1dsp                     : 1;
	uint32_t tile2dsp                     : 1;
	uint32_t tile3dsp                     : 1;
	uint32_t token                        : 1;
	uint32_t reserved_7_31                : 25;
#endif
	} s;
	struct cvmx_endor_rstclk_clkenb1_set_s cnf71xx;
};
typedef union cvmx_endor_rstclk_clkenb1_set cvmx_endor_rstclk_clkenb1_set_t;

/**
 * cvmx_endor_rstclk_clkenb1_state
 */
union cvmx_endor_rstclk_clkenb1_state {
	uint32_t u32;
	struct cvmx_endor_rstclk_clkenb1_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_7_31                : 25;
	uint32_t token                        : 1;  /**< abc */
	uint32_t tile3dsp                     : 1;  /**< abc */
	uint32_t tile2dsp                     : 1;  /**< abc */
	uint32_t tile1dsp                     : 1;  /**< abc */
	uint32_t rfspi                        : 1;  /**< abc */
	uint32_t rfif_hab                     : 1;  /**< abc */
	uint32_t rfif_rf                      : 1;  /**< abc */
#else
	uint32_t rfif_rf                      : 1;
	uint32_t rfif_hab                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t tile1dsp                     : 1;
	uint32_t tile2dsp                     : 1;
	uint32_t tile3dsp                     : 1;
	uint32_t token                        : 1;
	uint32_t reserved_7_31                : 25;
#endif
	} s;
	struct cvmx_endor_rstclk_clkenb1_state_s cnf71xx;
};
typedef union cvmx_endor_rstclk_clkenb1_state cvmx_endor_rstclk_clkenb1_state_t;

/**
 * cvmx_endor_rstclk_dspstall_clr
 */
union cvmx_endor_rstclk_dspstall_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_dspstall_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t txdsp1                       : 1;  /**< abc */
	uint32_t txdsp0                       : 1;  /**< abc */
	uint32_t rx1dsp1                      : 1;  /**< abc */
	uint32_t rx1dsp0                      : 1;  /**< abc */
	uint32_t rx0dsp1                      : 1;  /**< abc */
	uint32_t rx0dsp0                      : 1;  /**< abc */
#else
	uint32_t rx0dsp0                      : 1;
	uint32_t rx0dsp1                      : 1;
	uint32_t rx1dsp0                      : 1;
	uint32_t rx1dsp1                      : 1;
	uint32_t txdsp0                       : 1;
	uint32_t txdsp1                       : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_rstclk_dspstall_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_dspstall_clr cvmx_endor_rstclk_dspstall_clr_t;

/**
 * cvmx_endor_rstclk_dspstall_set
 */
union cvmx_endor_rstclk_dspstall_set {
	uint32_t u32;
	struct cvmx_endor_rstclk_dspstall_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t txdsp1                       : 1;  /**< abc */
	uint32_t txdsp0                       : 1;  /**< abc */
	uint32_t rx1dsp1                      : 1;  /**< abc */
	uint32_t rx1dsp0                      : 1;  /**< abc */
	uint32_t rx0dsp1                      : 1;  /**< abc */
	uint32_t rx0dsp0                      : 1;  /**< abc */
#else
	uint32_t rx0dsp0                      : 1;
	uint32_t rx0dsp1                      : 1;
	uint32_t rx1dsp0                      : 1;
	uint32_t rx1dsp1                      : 1;
	uint32_t txdsp0                       : 1;
	uint32_t txdsp1                       : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_rstclk_dspstall_set_s cnf71xx;
};
typedef union cvmx_endor_rstclk_dspstall_set cvmx_endor_rstclk_dspstall_set_t;

/**
 * cvmx_endor_rstclk_dspstall_state
 */
union cvmx_endor_rstclk_dspstall_state {
	uint32_t u32;
	struct cvmx_endor_rstclk_dspstall_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t txdsp1                       : 1;  /**< abc */
	uint32_t txdsp0                       : 1;  /**< abc */
	uint32_t rx1dsp1                      : 1;  /**< abc */
	uint32_t rx1dsp0                      : 1;  /**< abc */
	uint32_t rx0dsp1                      : 1;  /**< abc */
	uint32_t rx0dsp0                      : 1;  /**< abc */
#else
	uint32_t rx0dsp0                      : 1;
	uint32_t rx0dsp1                      : 1;
	uint32_t rx1dsp0                      : 1;
	uint32_t rx1dsp1                      : 1;
	uint32_t txdsp0                       : 1;
	uint32_t txdsp1                       : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_rstclk_dspstall_state_s cnf71xx;
};
typedef union cvmx_endor_rstclk_dspstall_state cvmx_endor_rstclk_dspstall_state_t;

/**
 * cvmx_endor_rstclk_intr0_clrmask
 */
union cvmx_endor_rstclk_intr0_clrmask {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr0_clrmask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer_intr                   : 8;  /**< reserved. */
	uint32_t sw_intr                      : 24; /**< reserved. */
#else
	uint32_t sw_intr                      : 24;
	uint32_t timer_intr                   : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_intr0_clrmask_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr0_clrmask cvmx_endor_rstclk_intr0_clrmask_t;

/**
 * cvmx_endor_rstclk_intr0_mask
 */
union cvmx_endor_rstclk_intr0_mask {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr0_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer_intr                   : 8;  /**< reserved. */
	uint32_t sw_intr                      : 24; /**< reserved. */
#else
	uint32_t sw_intr                      : 24;
	uint32_t timer_intr                   : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_intr0_mask_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr0_mask cvmx_endor_rstclk_intr0_mask_t;

/**
 * cvmx_endor_rstclk_intr0_setmask
 */
union cvmx_endor_rstclk_intr0_setmask {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr0_setmask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer_intr                   : 8;  /**< reserved. */
	uint32_t sw_intr                      : 24; /**< reserved. */
#else
	uint32_t sw_intr                      : 24;
	uint32_t timer_intr                   : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_intr0_setmask_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr0_setmask cvmx_endor_rstclk_intr0_setmask_t;

/**
 * cvmx_endor_rstclk_intr0_status
 */
union cvmx_endor_rstclk_intr0_status {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr0_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_intr0_status_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr0_status cvmx_endor_rstclk_intr0_status_t;

/**
 * cvmx_endor_rstclk_intr1_clrmask
 */
union cvmx_endor_rstclk_intr1_clrmask {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr1_clrmask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_intr1_clrmask_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr1_clrmask cvmx_endor_rstclk_intr1_clrmask_t;

/**
 * cvmx_endor_rstclk_intr1_mask
 */
union cvmx_endor_rstclk_intr1_mask {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr1_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_intr1_mask_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr1_mask cvmx_endor_rstclk_intr1_mask_t;

/**
 * cvmx_endor_rstclk_intr1_setmask
 */
union cvmx_endor_rstclk_intr1_setmask {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr1_setmask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_intr1_setmask_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr1_setmask cvmx_endor_rstclk_intr1_setmask_t;

/**
 * cvmx_endor_rstclk_intr1_status
 */
union cvmx_endor_rstclk_intr1_status {
	uint32_t u32;
	struct cvmx_endor_rstclk_intr1_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_intr1_status_s cnf71xx;
};
typedef union cvmx_endor_rstclk_intr1_status cvmx_endor_rstclk_intr1_status_t;

/**
 * cvmx_endor_rstclk_phy_config
 */
union cvmx_endor_rstclk_phy_config {
	uint32_t u32;
	struct cvmx_endor_rstclk_phy_config_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t t3smem_initenb               : 1;  /**< abc */
	uint32_t t3imem_initenb               : 1;  /**< abc */
	uint32_t t2smem_initenb               : 1;  /**< abc */
	uint32_t t2imem_initenb               : 1;  /**< abc */
	uint32_t t1smem_initenb               : 1;  /**< abc */
	uint32_t t1imem_initenb               : 1;  /**< abc */
#else
	uint32_t t1imem_initenb               : 1;
	uint32_t t1smem_initenb               : 1;
	uint32_t t2imem_initenb               : 1;
	uint32_t t2smem_initenb               : 1;
	uint32_t t3imem_initenb               : 1;
	uint32_t t3smem_initenb               : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_endor_rstclk_phy_config_s cnf71xx;
};
typedef union cvmx_endor_rstclk_phy_config cvmx_endor_rstclk_phy_config_t;

/**
 * cvmx_endor_rstclk_proc_mon
 */
union cvmx_endor_rstclk_proc_mon {
	uint32_t u32;
	struct cvmx_endor_rstclk_proc_mon_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t transistor_sel               : 2;  /**< 01==RVT, 10==HVT. */
	uint32_t ringosc_count                : 16; /**< reserved. */
#else
	uint32_t ringosc_count                : 16;
	uint32_t transistor_sel               : 2;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_endor_rstclk_proc_mon_s   cnf71xx;
};
typedef union cvmx_endor_rstclk_proc_mon cvmx_endor_rstclk_proc_mon_t;

/**
 * cvmx_endor_rstclk_proc_mon_count
 */
union cvmx_endor_rstclk_proc_mon_count {
	uint32_t u32;
	struct cvmx_endor_rstclk_proc_mon_count_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t count                        : 24; /**< reserved. */
#else
	uint32_t count                        : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_proc_mon_count_s cnf71xx;
};
typedef union cvmx_endor_rstclk_proc_mon_count cvmx_endor_rstclk_proc_mon_count_t;

/**
 * cvmx_endor_rstclk_reset0_clr
 */
union cvmx_endor_rstclk_reset0_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_reset0_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t axidma                       : 1;  /**< abc */
	uint32_t txseq                        : 1;  /**< abc */
	uint32_t v3genc                       : 1;  /**< abc */
	uint32_t ifftpapr                     : 1;  /**< abc */
	uint32_t lteenc                       : 1;  /**< abc */
	uint32_t vdec                         : 1;  /**< abc */
	uint32_t turbodsp                     : 1;  /**< abc */
	uint32_t turbophy                     : 1;  /**< abc */
	uint32_t rx1seq                       : 1;  /**< abc */
	uint32_t dftdmap                      : 1;  /**< abc */
	uint32_t rx0seq                       : 1;  /**< abc */
	uint32_t rachfe                       : 1;  /**< abc */
	uint32_t ulfe                         : 1;  /**< abc */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachfe                       : 1;
	uint32_t rx0seq                       : 1;
	uint32_t dftdmap                      : 1;
	uint32_t rx1seq                       : 1;
	uint32_t turbophy                     : 1;
	uint32_t turbodsp                     : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t v3genc                       : 1;
	uint32_t txseq                        : 1;
	uint32_t axidma                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rstclk_reset0_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_reset0_clr cvmx_endor_rstclk_reset0_clr_t;

/**
 * cvmx_endor_rstclk_reset0_set
 */
union cvmx_endor_rstclk_reset0_set {
	uint32_t u32;
	struct cvmx_endor_rstclk_reset0_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t axidma                       : 1;  /**< abc */
	uint32_t txseq                        : 1;  /**< abc */
	uint32_t v3genc                       : 1;  /**< abc */
	uint32_t ifftpapr                     : 1;  /**< abc */
	uint32_t lteenc                       : 1;  /**< abc */
	uint32_t vdec                         : 1;  /**< abc */
	uint32_t turbodsp                     : 1;  /**< abc */
	uint32_t turbophy                     : 1;  /**< abc */
	uint32_t rx1seq                       : 1;  /**< abc */
	uint32_t dftdmap                      : 1;  /**< abc */
	uint32_t rx0seq                       : 1;  /**< abc */
	uint32_t rachfe                       : 1;  /**< abc */
	uint32_t ulfe                         : 1;  /**< abc */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachfe                       : 1;
	uint32_t rx0seq                       : 1;
	uint32_t dftdmap                      : 1;
	uint32_t rx1seq                       : 1;
	uint32_t turbophy                     : 1;
	uint32_t turbodsp                     : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t v3genc                       : 1;
	uint32_t txseq                        : 1;
	uint32_t axidma                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rstclk_reset0_set_s cnf71xx;
};
typedef union cvmx_endor_rstclk_reset0_set cvmx_endor_rstclk_reset0_set_t;

/**
 * cvmx_endor_rstclk_reset0_state
 */
union cvmx_endor_rstclk_reset0_state {
	uint32_t u32;
	struct cvmx_endor_rstclk_reset0_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t axidma                       : 1;  /**< abc */
	uint32_t txseq                        : 1;  /**< abc */
	uint32_t v3genc                       : 1;  /**< abc */
	uint32_t ifftpapr                     : 1;  /**< abc */
	uint32_t lteenc                       : 1;  /**< abc */
	uint32_t vdec                         : 1;  /**< abc */
	uint32_t turbodsp                     : 1;  /**< abc */
	uint32_t turbophy                     : 1;  /**< abc */
	uint32_t rx1seq                       : 1;  /**< abc */
	uint32_t dftdmap                      : 1;  /**< abc */
	uint32_t rx0seq                       : 1;  /**< abc */
	uint32_t rachfe                       : 1;  /**< abc */
	uint32_t ulfe                         : 1;  /**< abc */
#else
	uint32_t ulfe                         : 1;
	uint32_t rachfe                       : 1;
	uint32_t rx0seq                       : 1;
	uint32_t dftdmap                      : 1;
	uint32_t rx1seq                       : 1;
	uint32_t turbophy                     : 1;
	uint32_t turbodsp                     : 1;
	uint32_t vdec                         : 1;
	uint32_t lteenc                       : 1;
	uint32_t ifftpapr                     : 1;
	uint32_t v3genc                       : 1;
	uint32_t txseq                        : 1;
	uint32_t axidma                       : 1;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_endor_rstclk_reset0_state_s cnf71xx;
};
typedef union cvmx_endor_rstclk_reset0_state cvmx_endor_rstclk_reset0_state_t;

/**
 * cvmx_endor_rstclk_reset1_clr
 */
union cvmx_endor_rstclk_reset1_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_reset1_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_7_31                : 25;
	uint32_t token                        : 1;  /**< abc */
	uint32_t tile3dsp                     : 1;  /**< abc */
	uint32_t tile2dsp                     : 1;  /**< abc */
	uint32_t tile1dsp                     : 1;  /**< abc */
	uint32_t rfspi                        : 1;  /**< abc */
	uint32_t rfif_hab                     : 1;  /**< abc */
	uint32_t rfif_rf                      : 1;  /**< abc */
#else
	uint32_t rfif_rf                      : 1;
	uint32_t rfif_hab                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t tile1dsp                     : 1;
	uint32_t tile2dsp                     : 1;
	uint32_t tile3dsp                     : 1;
	uint32_t token                        : 1;
	uint32_t reserved_7_31                : 25;
#endif
	} s;
	struct cvmx_endor_rstclk_reset1_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_reset1_clr cvmx_endor_rstclk_reset1_clr_t;

/**
 * cvmx_endor_rstclk_reset1_set
 */
union cvmx_endor_rstclk_reset1_set {
	uint32_t u32;
	struct cvmx_endor_rstclk_reset1_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_7_31                : 25;
	uint32_t token                        : 1;  /**< abc */
	uint32_t tile3dsp                     : 1;  /**< abc */
	uint32_t tile2dsp                     : 1;  /**< abc */
	uint32_t tile1dsp                     : 1;  /**< abc */
	uint32_t rfspi                        : 1;  /**< abc */
	uint32_t rfif_hab                     : 1;  /**< abc */
	uint32_t rfif_rf                      : 1;  /**< abc */
#else
	uint32_t rfif_rf                      : 1;
	uint32_t rfif_hab                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t tile1dsp                     : 1;
	uint32_t tile2dsp                     : 1;
	uint32_t tile3dsp                     : 1;
	uint32_t token                        : 1;
	uint32_t reserved_7_31                : 25;
#endif
	} s;
	struct cvmx_endor_rstclk_reset1_set_s cnf71xx;
};
typedef union cvmx_endor_rstclk_reset1_set cvmx_endor_rstclk_reset1_set_t;

/**
 * cvmx_endor_rstclk_reset1_state
 */
union cvmx_endor_rstclk_reset1_state {
	uint32_t u32;
	struct cvmx_endor_rstclk_reset1_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_7_31                : 25;
	uint32_t token                        : 1;  /**< abc */
	uint32_t tile3dsp                     : 1;  /**< abc */
	uint32_t tile2dsp                     : 1;  /**< abc */
	uint32_t tile1dsp                     : 1;  /**< abc */
	uint32_t rfspi                        : 1;  /**< abc */
	uint32_t rfif_hab                     : 1;  /**< abc */
	uint32_t rfif_rf                      : 1;  /**< abc */
#else
	uint32_t rfif_rf                      : 1;
	uint32_t rfif_hab                     : 1;
	uint32_t rfspi                        : 1;
	uint32_t tile1dsp                     : 1;
	uint32_t tile2dsp                     : 1;
	uint32_t tile3dsp                     : 1;
	uint32_t token                        : 1;
	uint32_t reserved_7_31                : 25;
#endif
	} s;
	struct cvmx_endor_rstclk_reset1_state_s cnf71xx;
};
typedef union cvmx_endor_rstclk_reset1_state cvmx_endor_rstclk_reset1_state_t;

/**
 * cvmx_endor_rstclk_sw_intr_clr
 */
union cvmx_endor_rstclk_sw_intr_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_sw_intr_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer_intr                   : 8;  /**< reserved. */
	uint32_t sw_intr                      : 24; /**< reserved. */
#else
	uint32_t sw_intr                      : 24;
	uint32_t timer_intr                   : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_sw_intr_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_sw_intr_clr cvmx_endor_rstclk_sw_intr_clr_t;

/**
 * cvmx_endor_rstclk_sw_intr_set
 */
union cvmx_endor_rstclk_sw_intr_set {
	uint32_t u32;
	struct cvmx_endor_rstclk_sw_intr_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer_intr                   : 8;  /**< reserved. */
	uint32_t sw_intr                      : 24; /**< reserved. */
#else
	uint32_t sw_intr                      : 24;
	uint32_t timer_intr                   : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_sw_intr_set_s cnf71xx;
};
typedef union cvmx_endor_rstclk_sw_intr_set cvmx_endor_rstclk_sw_intr_set_t;

/**
 * cvmx_endor_rstclk_sw_intr_status
 */
union cvmx_endor_rstclk_sw_intr_status {
	uint32_t u32;
	struct cvmx_endor_rstclk_sw_intr_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t timer_intr                   : 8;  /**< reserved. */
	uint32_t sw_intr                      : 24; /**< reserved. */
#else
	uint32_t sw_intr                      : 24;
	uint32_t timer_intr                   : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_sw_intr_status_s cnf71xx;
};
typedef union cvmx_endor_rstclk_sw_intr_status cvmx_endor_rstclk_sw_intr_status_t;

/**
 * cvmx_endor_rstclk_time#_thrd
 */
union cvmx_endor_rstclk_timex_thrd {
	uint32_t u32;
	struct cvmx_endor_rstclk_timex_thrd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t value                        : 24; /**< abc */
#else
	uint32_t value                        : 24;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_endor_rstclk_timex_thrd_s cnf71xx;
};
typedef union cvmx_endor_rstclk_timex_thrd cvmx_endor_rstclk_timex_thrd_t;

/**
 * cvmx_endor_rstclk_timer_ctl
 */
union cvmx_endor_rstclk_timer_ctl {
	uint32_t u32;
	struct cvmx_endor_rstclk_timer_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t intr_enb                     : 8;  /**< abc */
	uint32_t reserved_3_7                 : 5;
	uint32_t enb                          : 1;  /**< abc */
	uint32_t cont                         : 1;  /**< abc */
	uint32_t clr                          : 1;  /**< abc */
#else
	uint32_t clr                          : 1;
	uint32_t cont                         : 1;
	uint32_t enb                          : 1;
	uint32_t reserved_3_7                 : 5;
	uint32_t intr_enb                     : 8;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_endor_rstclk_timer_ctl_s  cnf71xx;
};
typedef union cvmx_endor_rstclk_timer_ctl cvmx_endor_rstclk_timer_ctl_t;

/**
 * cvmx_endor_rstclk_timer_intr_clr
 */
union cvmx_endor_rstclk_timer_intr_clr {
	uint32_t u32;
	struct cvmx_endor_rstclk_timer_intr_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t clr                          : 8;  /**< reserved. */
#else
	uint32_t clr                          : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rstclk_timer_intr_clr_s cnf71xx;
};
typedef union cvmx_endor_rstclk_timer_intr_clr cvmx_endor_rstclk_timer_intr_clr_t;

/**
 * cvmx_endor_rstclk_timer_intr_status
 */
union cvmx_endor_rstclk_timer_intr_status {
	uint32_t u32;
	struct cvmx_endor_rstclk_timer_intr_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t status                       : 8;  /**< reserved. */
#else
	uint32_t status                       : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_endor_rstclk_timer_intr_status_s cnf71xx;
};
typedef union cvmx_endor_rstclk_timer_intr_status cvmx_endor_rstclk_timer_intr_status_t;

/**
 * cvmx_endor_rstclk_timer_max
 */
union cvmx_endor_rstclk_timer_max {
	uint32_t u32;
	struct cvmx_endor_rstclk_timer_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_timer_max_s  cnf71xx;
};
typedef union cvmx_endor_rstclk_timer_max cvmx_endor_rstclk_timer_max_t;

/**
 * cvmx_endor_rstclk_timer_value
 */
union cvmx_endor_rstclk_timer_value {
	uint32_t u32;
	struct cvmx_endor_rstclk_timer_value_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t value                        : 32; /**< reserved. */
#else
	uint32_t value                        : 32;
#endif
	} s;
	struct cvmx_endor_rstclk_timer_value_s cnf71xx;
};
typedef union cvmx_endor_rstclk_timer_value cvmx_endor_rstclk_timer_value_t;

/**
 * cvmx_endor_rstclk_version
 */
union cvmx_endor_rstclk_version {
	uint32_t u32;
	struct cvmx_endor_rstclk_version_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t major                        : 8;  /**< reserved. */
	uint32_t minor                        : 8;  /**< reserved. */
#else
	uint32_t minor                        : 8;
	uint32_t major                        : 8;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_endor_rstclk_version_s    cnf71xx;
};
typedef union cvmx_endor_rstclk_version cvmx_endor_rstclk_version_t;

#endif
