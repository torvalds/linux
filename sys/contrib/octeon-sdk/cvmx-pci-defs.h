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
 * cvmx-pci-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pci.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PCI_DEFS_H__
#define __CVMX_PCI_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_BAR1_INDEXX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_PCI_BAR1_INDEXX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000100ull + ((offset) & 31) * 4;
}
#else
#define CVMX_PCI_BAR1_INDEXX(offset) (0x0000000000000100ull + ((offset) & 31) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_BIST_REG CVMX_PCI_BIST_REG_FUNC()
static inline uint64_t CVMX_PCI_BIST_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN50XX)))
		cvmx_warn("CVMX_PCI_BIST_REG not supported on this chip\n");
	return 0x00000000000001C0ull;
}
#else
#define CVMX_PCI_BIST_REG (0x00000000000001C0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG00 CVMX_PCI_CFG00_FUNC()
static inline uint64_t CVMX_PCI_CFG00_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG00 not supported on this chip\n");
	return 0x0000000000000000ull;
}
#else
#define CVMX_PCI_CFG00 (0x0000000000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG01 CVMX_PCI_CFG01_FUNC()
static inline uint64_t CVMX_PCI_CFG01_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG01 not supported on this chip\n");
	return 0x0000000000000004ull;
}
#else
#define CVMX_PCI_CFG01 (0x0000000000000004ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG02 CVMX_PCI_CFG02_FUNC()
static inline uint64_t CVMX_PCI_CFG02_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG02 not supported on this chip\n");
	return 0x0000000000000008ull;
}
#else
#define CVMX_PCI_CFG02 (0x0000000000000008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG03 CVMX_PCI_CFG03_FUNC()
static inline uint64_t CVMX_PCI_CFG03_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG03 not supported on this chip\n");
	return 0x000000000000000Cull;
}
#else
#define CVMX_PCI_CFG03 (0x000000000000000Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG04 CVMX_PCI_CFG04_FUNC()
static inline uint64_t CVMX_PCI_CFG04_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG04 not supported on this chip\n");
	return 0x0000000000000010ull;
}
#else
#define CVMX_PCI_CFG04 (0x0000000000000010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG05 CVMX_PCI_CFG05_FUNC()
static inline uint64_t CVMX_PCI_CFG05_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG05 not supported on this chip\n");
	return 0x0000000000000014ull;
}
#else
#define CVMX_PCI_CFG05 (0x0000000000000014ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG06 CVMX_PCI_CFG06_FUNC()
static inline uint64_t CVMX_PCI_CFG06_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG06 not supported on this chip\n");
	return 0x0000000000000018ull;
}
#else
#define CVMX_PCI_CFG06 (0x0000000000000018ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG07 CVMX_PCI_CFG07_FUNC()
static inline uint64_t CVMX_PCI_CFG07_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG07 not supported on this chip\n");
	return 0x000000000000001Cull;
}
#else
#define CVMX_PCI_CFG07 (0x000000000000001Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG08 CVMX_PCI_CFG08_FUNC()
static inline uint64_t CVMX_PCI_CFG08_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG08 not supported on this chip\n");
	return 0x0000000000000020ull;
}
#else
#define CVMX_PCI_CFG08 (0x0000000000000020ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG09 CVMX_PCI_CFG09_FUNC()
static inline uint64_t CVMX_PCI_CFG09_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG09 not supported on this chip\n");
	return 0x0000000000000024ull;
}
#else
#define CVMX_PCI_CFG09 (0x0000000000000024ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG10 CVMX_PCI_CFG10_FUNC()
static inline uint64_t CVMX_PCI_CFG10_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG10 not supported on this chip\n");
	return 0x0000000000000028ull;
}
#else
#define CVMX_PCI_CFG10 (0x0000000000000028ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG11 CVMX_PCI_CFG11_FUNC()
static inline uint64_t CVMX_PCI_CFG11_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG11 not supported on this chip\n");
	return 0x000000000000002Cull;
}
#else
#define CVMX_PCI_CFG11 (0x000000000000002Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG12 CVMX_PCI_CFG12_FUNC()
static inline uint64_t CVMX_PCI_CFG12_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG12 not supported on this chip\n");
	return 0x0000000000000030ull;
}
#else
#define CVMX_PCI_CFG12 (0x0000000000000030ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG13 CVMX_PCI_CFG13_FUNC()
static inline uint64_t CVMX_PCI_CFG13_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG13 not supported on this chip\n");
	return 0x0000000000000034ull;
}
#else
#define CVMX_PCI_CFG13 (0x0000000000000034ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG15 CVMX_PCI_CFG15_FUNC()
static inline uint64_t CVMX_PCI_CFG15_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG15 not supported on this chip\n");
	return 0x000000000000003Cull;
}
#else
#define CVMX_PCI_CFG15 (0x000000000000003Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG16 CVMX_PCI_CFG16_FUNC()
static inline uint64_t CVMX_PCI_CFG16_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG16 not supported on this chip\n");
	return 0x0000000000000040ull;
}
#else
#define CVMX_PCI_CFG16 (0x0000000000000040ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG17 CVMX_PCI_CFG17_FUNC()
static inline uint64_t CVMX_PCI_CFG17_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG17 not supported on this chip\n");
	return 0x0000000000000044ull;
}
#else
#define CVMX_PCI_CFG17 (0x0000000000000044ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG18 CVMX_PCI_CFG18_FUNC()
static inline uint64_t CVMX_PCI_CFG18_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG18 not supported on this chip\n");
	return 0x0000000000000048ull;
}
#else
#define CVMX_PCI_CFG18 (0x0000000000000048ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG19 CVMX_PCI_CFG19_FUNC()
static inline uint64_t CVMX_PCI_CFG19_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG19 not supported on this chip\n");
	return 0x000000000000004Cull;
}
#else
#define CVMX_PCI_CFG19 (0x000000000000004Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG20 CVMX_PCI_CFG20_FUNC()
static inline uint64_t CVMX_PCI_CFG20_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG20 not supported on this chip\n");
	return 0x0000000000000050ull;
}
#else
#define CVMX_PCI_CFG20 (0x0000000000000050ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG21 CVMX_PCI_CFG21_FUNC()
static inline uint64_t CVMX_PCI_CFG21_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG21 not supported on this chip\n");
	return 0x0000000000000054ull;
}
#else
#define CVMX_PCI_CFG21 (0x0000000000000054ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG22 CVMX_PCI_CFG22_FUNC()
static inline uint64_t CVMX_PCI_CFG22_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG22 not supported on this chip\n");
	return 0x0000000000000058ull;
}
#else
#define CVMX_PCI_CFG22 (0x0000000000000058ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG56 CVMX_PCI_CFG56_FUNC()
static inline uint64_t CVMX_PCI_CFG56_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG56 not supported on this chip\n");
	return 0x00000000000000E0ull;
}
#else
#define CVMX_PCI_CFG56 (0x00000000000000E0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG57 CVMX_PCI_CFG57_FUNC()
static inline uint64_t CVMX_PCI_CFG57_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG57 not supported on this chip\n");
	return 0x00000000000000E4ull;
}
#else
#define CVMX_PCI_CFG57 (0x00000000000000E4ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG58 CVMX_PCI_CFG58_FUNC()
static inline uint64_t CVMX_PCI_CFG58_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG58 not supported on this chip\n");
	return 0x00000000000000E8ull;
}
#else
#define CVMX_PCI_CFG58 (0x00000000000000E8ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG59 CVMX_PCI_CFG59_FUNC()
static inline uint64_t CVMX_PCI_CFG59_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG59 not supported on this chip\n");
	return 0x00000000000000ECull;
}
#else
#define CVMX_PCI_CFG59 (0x00000000000000ECull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG60 CVMX_PCI_CFG60_FUNC()
static inline uint64_t CVMX_PCI_CFG60_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG60 not supported on this chip\n");
	return 0x00000000000000F0ull;
}
#else
#define CVMX_PCI_CFG60 (0x00000000000000F0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG61 CVMX_PCI_CFG61_FUNC()
static inline uint64_t CVMX_PCI_CFG61_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG61 not supported on this chip\n");
	return 0x00000000000000F4ull;
}
#else
#define CVMX_PCI_CFG61 (0x00000000000000F4ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG62 CVMX_PCI_CFG62_FUNC()
static inline uint64_t CVMX_PCI_CFG62_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG62 not supported on this chip\n");
	return 0x00000000000000F8ull;
}
#else
#define CVMX_PCI_CFG62 (0x00000000000000F8ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CFG63 CVMX_PCI_CFG63_FUNC()
static inline uint64_t CVMX_PCI_CFG63_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CFG63 not supported on this chip\n");
	return 0x00000000000000FCull;
}
#else
#define CVMX_PCI_CFG63 (0x00000000000000FCull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CNT_REG CVMX_PCI_CNT_REG_FUNC()
static inline uint64_t CVMX_PCI_CNT_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CNT_REG not supported on this chip\n");
	return 0x00000000000001B8ull;
}
#else
#define CVMX_PCI_CNT_REG (0x00000000000001B8ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_CTL_STATUS_2 CVMX_PCI_CTL_STATUS_2_FUNC()
static inline uint64_t CVMX_PCI_CTL_STATUS_2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_CTL_STATUS_2 not supported on this chip\n");
	return 0x000000000000018Cull;
}
#else
#define CVMX_PCI_CTL_STATUS_2 (0x000000000000018Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_DBELL_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCI_DBELL_X(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000080ull + ((offset) & 3) * 8;
}
#else
#define CVMX_PCI_DBELL_X(offset) (0x0000000000000080ull + ((offset) & 3) * 8)
#endif
#define CVMX_PCI_DMA_CNT0 CVMX_PCI_DMA_CNTX(0)
#define CVMX_PCI_DMA_CNT1 CVMX_PCI_DMA_CNTX(1)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_DMA_CNTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PCI_DMA_CNTX(%lu) is invalid on this chip\n", offset);
	return 0x00000000000000A0ull + ((offset) & 1) * 8;
}
#else
#define CVMX_PCI_DMA_CNTX(offset) (0x00000000000000A0ull + ((offset) & 1) * 8)
#endif
#define CVMX_PCI_DMA_INT_LEV0 CVMX_PCI_DMA_INT_LEVX(0)
#define CVMX_PCI_DMA_INT_LEV1 CVMX_PCI_DMA_INT_LEVX(1)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_DMA_INT_LEVX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PCI_DMA_INT_LEVX(%lu) is invalid on this chip\n", offset);
	return 0x00000000000000A4ull + ((offset) & 1) * 8;
}
#else
#define CVMX_PCI_DMA_INT_LEVX(offset) (0x00000000000000A4ull + ((offset) & 1) * 8)
#endif
#define CVMX_PCI_DMA_TIME0 CVMX_PCI_DMA_TIMEX(0)
#define CVMX_PCI_DMA_TIME1 CVMX_PCI_DMA_TIMEX(1)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_DMA_TIMEX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PCI_DMA_TIMEX(%lu) is invalid on this chip\n", offset);
	return 0x00000000000000B0ull + ((offset) & 1) * 4;
}
#else
#define CVMX_PCI_DMA_TIMEX(offset) (0x00000000000000B0ull + ((offset) & 1) * 4)
#endif
#define CVMX_PCI_INSTR_COUNT0 CVMX_PCI_INSTR_COUNTX(0)
#define CVMX_PCI_INSTR_COUNT1 CVMX_PCI_INSTR_COUNTX(1)
#define CVMX_PCI_INSTR_COUNT2 CVMX_PCI_INSTR_COUNTX(2)
#define CVMX_PCI_INSTR_COUNT3 CVMX_PCI_INSTR_COUNTX(3)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_INSTR_COUNTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCI_INSTR_COUNTX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000084ull + ((offset) & 3) * 8;
}
#else
#define CVMX_PCI_INSTR_COUNTX(offset) (0x0000000000000084ull + ((offset) & 3) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_INT_ENB CVMX_PCI_INT_ENB_FUNC()
static inline uint64_t CVMX_PCI_INT_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_INT_ENB not supported on this chip\n");
	return 0x0000000000000038ull;
}
#else
#define CVMX_PCI_INT_ENB (0x0000000000000038ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_INT_ENB2 CVMX_PCI_INT_ENB2_FUNC()
static inline uint64_t CVMX_PCI_INT_ENB2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_INT_ENB2 not supported on this chip\n");
	return 0x00000000000001A0ull;
}
#else
#define CVMX_PCI_INT_ENB2 (0x00000000000001A0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_INT_SUM CVMX_PCI_INT_SUM_FUNC()
static inline uint64_t CVMX_PCI_INT_SUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_INT_SUM not supported on this chip\n");
	return 0x0000000000000030ull;
}
#else
#define CVMX_PCI_INT_SUM (0x0000000000000030ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_INT_SUM2 CVMX_PCI_INT_SUM2_FUNC()
static inline uint64_t CVMX_PCI_INT_SUM2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_INT_SUM2 not supported on this chip\n");
	return 0x0000000000000198ull;
}
#else
#define CVMX_PCI_INT_SUM2 (0x0000000000000198ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_MSI_RCV CVMX_PCI_MSI_RCV_FUNC()
static inline uint64_t CVMX_PCI_MSI_RCV_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_MSI_RCV not supported on this chip\n");
	return 0x00000000000000F0ull;
}
#else
#define CVMX_PCI_MSI_RCV (0x00000000000000F0ull)
#endif
#define CVMX_PCI_PKTS_SENT0 CVMX_PCI_PKTS_SENTX(0)
#define CVMX_PCI_PKTS_SENT1 CVMX_PCI_PKTS_SENTX(1)
#define CVMX_PCI_PKTS_SENT2 CVMX_PCI_PKTS_SENTX(2)
#define CVMX_PCI_PKTS_SENT3 CVMX_PCI_PKTS_SENTX(3)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_PKTS_SENTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCI_PKTS_SENTX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000040ull + ((offset) & 3) * 16;
}
#else
#define CVMX_PCI_PKTS_SENTX(offset) (0x0000000000000040ull + ((offset) & 3) * 16)
#endif
#define CVMX_PCI_PKTS_SENT_INT_LEV0 CVMX_PCI_PKTS_SENT_INT_LEVX(0)
#define CVMX_PCI_PKTS_SENT_INT_LEV1 CVMX_PCI_PKTS_SENT_INT_LEVX(1)
#define CVMX_PCI_PKTS_SENT_INT_LEV2 CVMX_PCI_PKTS_SENT_INT_LEVX(2)
#define CVMX_PCI_PKTS_SENT_INT_LEV3 CVMX_PCI_PKTS_SENT_INT_LEVX(3)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_PKTS_SENT_INT_LEVX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCI_PKTS_SENT_INT_LEVX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000048ull + ((offset) & 3) * 16;
}
#else
#define CVMX_PCI_PKTS_SENT_INT_LEVX(offset) (0x0000000000000048ull + ((offset) & 3) * 16)
#endif
#define CVMX_PCI_PKTS_SENT_TIME0 CVMX_PCI_PKTS_SENT_TIMEX(0)
#define CVMX_PCI_PKTS_SENT_TIME1 CVMX_PCI_PKTS_SENT_TIMEX(1)
#define CVMX_PCI_PKTS_SENT_TIME2 CVMX_PCI_PKTS_SENT_TIMEX(2)
#define CVMX_PCI_PKTS_SENT_TIME3 CVMX_PCI_PKTS_SENT_TIMEX(3)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_PKTS_SENT_TIMEX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCI_PKTS_SENT_TIMEX(%lu) is invalid on this chip\n", offset);
	return 0x000000000000004Cull + ((offset) & 3) * 16;
}
#else
#define CVMX_PCI_PKTS_SENT_TIMEX(offset) (0x000000000000004Cull + ((offset) & 3) * 16)
#endif
#define CVMX_PCI_PKT_CREDITS0 CVMX_PCI_PKT_CREDITSX(0)
#define CVMX_PCI_PKT_CREDITS1 CVMX_PCI_PKT_CREDITSX(1)
#define CVMX_PCI_PKT_CREDITS2 CVMX_PCI_PKT_CREDITSX(2)
#define CVMX_PCI_PKT_CREDITS3 CVMX_PCI_PKT_CREDITSX(3)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCI_PKT_CREDITSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PCI_PKT_CREDITSX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000044ull + ((offset) & 3) * 16;
}
#else
#define CVMX_PCI_PKT_CREDITSX(offset) (0x0000000000000044ull + ((offset) & 3) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_READ_CMD_6 CVMX_PCI_READ_CMD_6_FUNC()
static inline uint64_t CVMX_PCI_READ_CMD_6_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_READ_CMD_6 not supported on this chip\n");
	return 0x0000000000000180ull;
}
#else
#define CVMX_PCI_READ_CMD_6 (0x0000000000000180ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_READ_CMD_C CVMX_PCI_READ_CMD_C_FUNC()
static inline uint64_t CVMX_PCI_READ_CMD_C_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_READ_CMD_C not supported on this chip\n");
	return 0x0000000000000184ull;
}
#else
#define CVMX_PCI_READ_CMD_C (0x0000000000000184ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_READ_CMD_E CVMX_PCI_READ_CMD_E_FUNC()
static inline uint64_t CVMX_PCI_READ_CMD_E_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_READ_CMD_E not supported on this chip\n");
	return 0x0000000000000188ull;
}
#else
#define CVMX_PCI_READ_CMD_E (0x0000000000000188ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_READ_TIMEOUT CVMX_PCI_READ_TIMEOUT_FUNC()
static inline uint64_t CVMX_PCI_READ_TIMEOUT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_READ_TIMEOUT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011F00000000B0ull);
}
#else
#define CVMX_PCI_READ_TIMEOUT (CVMX_ADD_IO_SEG(0x00011F00000000B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_SCM_REG CVMX_PCI_SCM_REG_FUNC()
static inline uint64_t CVMX_PCI_SCM_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_SCM_REG not supported on this chip\n");
	return 0x00000000000001A8ull;
}
#else
#define CVMX_PCI_SCM_REG (0x00000000000001A8ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_TSR_REG CVMX_PCI_TSR_REG_FUNC()
static inline uint64_t CVMX_PCI_TSR_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_TSR_REG not supported on this chip\n");
	return 0x00000000000001B0ull;
}
#else
#define CVMX_PCI_TSR_REG (0x00000000000001B0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_WIN_RD_ADDR CVMX_PCI_WIN_RD_ADDR_FUNC()
static inline uint64_t CVMX_PCI_WIN_RD_ADDR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_WIN_RD_ADDR not supported on this chip\n");
	return 0x0000000000000008ull;
}
#else
#define CVMX_PCI_WIN_RD_ADDR (0x0000000000000008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_WIN_RD_DATA CVMX_PCI_WIN_RD_DATA_FUNC()
static inline uint64_t CVMX_PCI_WIN_RD_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_WIN_RD_DATA not supported on this chip\n");
	return 0x0000000000000020ull;
}
#else
#define CVMX_PCI_WIN_RD_DATA (0x0000000000000020ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_WIN_WR_ADDR CVMX_PCI_WIN_WR_ADDR_FUNC()
static inline uint64_t CVMX_PCI_WIN_WR_ADDR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_WIN_WR_ADDR not supported on this chip\n");
	return 0x0000000000000000ull;
}
#else
#define CVMX_PCI_WIN_WR_ADDR (0x0000000000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_WIN_WR_DATA CVMX_PCI_WIN_WR_DATA_FUNC()
static inline uint64_t CVMX_PCI_WIN_WR_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_WIN_WR_DATA not supported on this chip\n");
	return 0x0000000000000010ull;
}
#else
#define CVMX_PCI_WIN_WR_DATA (0x0000000000000010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PCI_WIN_WR_MASK CVMX_PCI_WIN_WR_MASK_FUNC()
static inline uint64_t CVMX_PCI_WIN_WR_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PCI_WIN_WR_MASK not supported on this chip\n");
	return 0x0000000000000018ull;
}
#else
#define CVMX_PCI_WIN_WR_MASK (0x0000000000000018ull)
#endif

/**
 * cvmx_pci_bar1_index#
 *
 * PCI_BAR1_INDEXX = PCI IndexX Register
 *
 * Contains address index and control bits for access to memory ranges of Bar-1,
 * when PCI supplied address-bits [26:22] == X.
 */
union cvmx_pci_bar1_indexx {
	uint32_t u32;
	struct cvmx_pci_bar1_indexx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t addr_idx                     : 14; /**< Address bits [35:22] sent to L2C */
	uint32_t ca                           : 1;  /**< Set '1' when access is not to be cached in L2. */
	uint32_t end_swp                      : 2;  /**< Endian Swap Mode */
	uint32_t addr_v                       : 1;  /**< Set '1' when the selected address range is valid. */
#else
	uint32_t addr_v                       : 1;
	uint32_t end_swp                      : 2;
	uint32_t ca                           : 1;
	uint32_t addr_idx                     : 14;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_pci_bar1_indexx_s         cn30xx;
	struct cvmx_pci_bar1_indexx_s         cn31xx;
	struct cvmx_pci_bar1_indexx_s         cn38xx;
	struct cvmx_pci_bar1_indexx_s         cn38xxp2;
	struct cvmx_pci_bar1_indexx_s         cn50xx;
	struct cvmx_pci_bar1_indexx_s         cn58xx;
	struct cvmx_pci_bar1_indexx_s         cn58xxp1;
};
typedef union cvmx_pci_bar1_indexx cvmx_pci_bar1_indexx_t;

/**
 * cvmx_pci_bist_reg
 *
 * PCI_BIST_REG = PCI PNI BIST Status Register
 *
 * Contains the bist results for the PNI memories.
 */
union cvmx_pci_bist_reg {
	uint64_t u64;
	struct cvmx_pci_bist_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t rsp_bs                       : 1;  /**< Bist Status For b12_rsp_fifo_bist
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t dma0_bs                      : 1;  /**< Bist Status For dmao_count
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t cmd0_bs                      : 1;  /**< Bist Status For npi_cmd0_pni_am0
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t cmd_bs                       : 1;  /**< Bist Status For npi_cmd_pni_am1
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t csr2p_bs                     : 1;  /**< Bist Status For npi_csr_2_pni_am
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t csrr_bs                      : 1;  /**< Bist Status For npi_csr_rsp_2_pni_am
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t rsp2p_bs                     : 1;  /**< Bist Status For npi_rsp_2_pni_am
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t csr2n_bs                     : 1;  /**< Bist Status For pni_csr_2_npi_am
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t dat2n_bs                     : 1;  /**< Bist Status For pni_data_2_npi_am
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
	uint64_t dbg2n_bs                     : 1;  /**< Bist Status For pni_dbg_data_2_npi_am
                                                         The value of this register is available 100,000
                                                         core clocks + 21,000 pclks after:
                                                         Host Mode - deassertion of pci_rst_n
                                                         Non Host Mode - deassertion of pci_rst_n */
#else
	uint64_t dbg2n_bs                     : 1;
	uint64_t dat2n_bs                     : 1;
	uint64_t csr2n_bs                     : 1;
	uint64_t rsp2p_bs                     : 1;
	uint64_t csrr_bs                      : 1;
	uint64_t csr2p_bs                     : 1;
	uint64_t cmd_bs                       : 1;
	uint64_t cmd0_bs                      : 1;
	uint64_t dma0_bs                      : 1;
	uint64_t rsp_bs                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_pci_bist_reg_s            cn50xx;
};
typedef union cvmx_pci_bist_reg cvmx_pci_bist_reg_t;

/**
 * cvmx_pci_cfg00
 *
 * Registers at address 0x1000 -> 0x17FF are PNI
 * Start at 0x100 into range
 * these are shifted by 2 to the left to make address
 *                Registers at address 0x1800 -> 0x18FF are CFG
 * these are shifted by 2 to the left to make address
 *
 *           PCI_CFG00 = First 32-bits of PCI config space (PCI Vendor + Device)
 *
 * This register contains the first 32-bits of the PCI config space registers
 */
union cvmx_pci_cfg00 {
	uint32_t u32;
	struct cvmx_pci_cfg00_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t devid                        : 16; /**< This is the device ID for OCTEON (90nm shhrink) */
	uint32_t vendid                       : 16; /**< This is the Cavium's vendor ID */
#else
	uint32_t vendid                       : 16;
	uint32_t devid                        : 16;
#endif
	} s;
	struct cvmx_pci_cfg00_s               cn30xx;
	struct cvmx_pci_cfg00_s               cn31xx;
	struct cvmx_pci_cfg00_s               cn38xx;
	struct cvmx_pci_cfg00_s               cn38xxp2;
	struct cvmx_pci_cfg00_s               cn50xx;
	struct cvmx_pci_cfg00_s               cn58xx;
	struct cvmx_pci_cfg00_s               cn58xxp1;
};
typedef union cvmx_pci_cfg00 cvmx_pci_cfg00_t;

/**
 * cvmx_pci_cfg01
 *
 * PCI_CFG01 = Second 32-bits of PCI config space (Command/Status Register)
 *
 */
union cvmx_pci_cfg01 {
	uint32_t u32;
	struct cvmx_pci_cfg01_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dpe                          : 1;  /**< Detected Parity Error */
	uint32_t sse                          : 1;  /**< Signaled System Error */
	uint32_t rma                          : 1;  /**< Received Master Abort */
	uint32_t rta                          : 1;  /**< Received Target Abort */
	uint32_t sta                          : 1;  /**< Signaled Target Abort */
	uint32_t devt                         : 2;  /**< DEVSEL# timing (for PCI only/for PCIX = don't care) */
	uint32_t mdpe                         : 1;  /**< Master Data Parity Error */
	uint32_t fbb                          : 1;  /**< Fast Back-to-Back Transactions Capable
                                                         Mode Dependent (1 = PCI Mode / 0 = PCIX Mode) */
	uint32_t reserved_22_22               : 1;
	uint32_t m66                          : 1;  /**< 66MHz Capable */
	uint32_t cle                          : 1;  /**< Capabilities List Enable */
	uint32_t i_stat                       : 1;  /**< When INTx# is asserted by OCTEON this bit will be set.
                                                         When deasserted by OCTEON this bit will be cleared. */
	uint32_t reserved_11_18               : 8;
	uint32_t i_dis                        : 1;  /**< When asserted '1' disables the generation of INTx#
                                                         by OCTEON. When disabled '0' allows assertion of INTx#
                                                         by OCTEON. */
	uint32_t fbbe                         : 1;  /**< Fast Back to Back Transaction Enable */
	uint32_t see                          : 1;  /**< System Error Enable */
	uint32_t ads                          : 1;  /**< Address/Data Stepping
                                                         NOTE: Octeon does NOT support address/data stepping. */
	uint32_t pee                          : 1;  /**< PERR# Enable */
	uint32_t vps                          : 1;  /**< VGA Palette Snooping */
	uint32_t mwice                        : 1;  /**< Memory Write & Invalidate Command Enable */
	uint32_t scse                         : 1;  /**< Special Cycle Snooping Enable */
	uint32_t me                           : 1;  /**< Master Enable
                                                         Must be set for OCTEON to master a PCI/PCI-X
                                                         transaction. This should always be set any time
                                                         that OCTEON is connected to a PCI/PCI-X bus. */
	uint32_t msae                         : 1;  /**< Memory Space Access Enable
                                                         Must be set to recieve a PCI/PCI-X memory space
                                                         transaction. This must always be set any time that
                                                         OCTEON is connected to a PCI/PCI-X bus. */
	uint32_t isae                         : 1;  /**< I/O Space Access Enable
                                                         NOTE: For OCTEON, this bit MUST NEVER be set
                                                         (it is read-only and OCTEON does not respond to I/O
                                                         Space accesses). */
#else
	uint32_t isae                         : 1;
	uint32_t msae                         : 1;
	uint32_t me                           : 1;
	uint32_t scse                         : 1;
	uint32_t mwice                        : 1;
	uint32_t vps                          : 1;
	uint32_t pee                          : 1;
	uint32_t ads                          : 1;
	uint32_t see                          : 1;
	uint32_t fbbe                         : 1;
	uint32_t i_dis                        : 1;
	uint32_t reserved_11_18               : 8;
	uint32_t i_stat                       : 1;
	uint32_t cle                          : 1;
	uint32_t m66                          : 1;
	uint32_t reserved_22_22               : 1;
	uint32_t fbb                          : 1;
	uint32_t mdpe                         : 1;
	uint32_t devt                         : 2;
	uint32_t sta                          : 1;
	uint32_t rta                          : 1;
	uint32_t rma                          : 1;
	uint32_t sse                          : 1;
	uint32_t dpe                          : 1;
#endif
	} s;
	struct cvmx_pci_cfg01_s               cn30xx;
	struct cvmx_pci_cfg01_s               cn31xx;
	struct cvmx_pci_cfg01_s               cn38xx;
	struct cvmx_pci_cfg01_s               cn38xxp2;
	struct cvmx_pci_cfg01_s               cn50xx;
	struct cvmx_pci_cfg01_s               cn58xx;
	struct cvmx_pci_cfg01_s               cn58xxp1;
};
typedef union cvmx_pci_cfg01 cvmx_pci_cfg01_t;

/**
 * cvmx_pci_cfg02
 *
 * PCI_CFG02 = Third 32-bits of PCI config space (Class Code / Revision ID)
 *
 */
union cvmx_pci_cfg02 {
	uint32_t u32;
	struct cvmx_pci_cfg02_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cc                           : 24; /**< Class Code (Processor/MIPS)
                                                         (was 0x100000 in pass 1 and pass 2) */
	uint32_t rid                          : 8;  /**< Revision ID
                                                         (0 in pass 1, 1 in pass 1.1, 8 in pass 2.0) */
#else
	uint32_t rid                          : 8;
	uint32_t cc                           : 24;
#endif
	} s;
	struct cvmx_pci_cfg02_s               cn30xx;
	struct cvmx_pci_cfg02_s               cn31xx;
	struct cvmx_pci_cfg02_s               cn38xx;
	struct cvmx_pci_cfg02_s               cn38xxp2;
	struct cvmx_pci_cfg02_s               cn50xx;
	struct cvmx_pci_cfg02_s               cn58xx;
	struct cvmx_pci_cfg02_s               cn58xxp1;
};
typedef union cvmx_pci_cfg02 cvmx_pci_cfg02_t;

/**
 * cvmx_pci_cfg03
 *
 * PCI_CFG03 = Fourth 32-bits of PCI config space (BIST, HEADER Type, Latency timer, line size)
 *
 */
union cvmx_pci_cfg03 {
	uint32_t u32;
	struct cvmx_pci_cfg03_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bcap                         : 1;  /**< BIST Capable */
	uint32_t brb                          : 1;  /**< BIST Request/busy bit
                                                         Note: OCTEON does not support PCI BIST, therefore
                                                         this bit should remain zero. */
	uint32_t reserved_28_29               : 2;
	uint32_t bcod                         : 4;  /**< BIST Code */
	uint32_t ht                           : 8;  /**< Header Type (Type 0) */
	uint32_t lt                           : 8;  /**< Latency Timer
                                                         (0=PCI)                 (0=PCI)
                                                         (0x40=PCIX)             (0x40=PCIX) */
	uint32_t cls                          : 8;  /**< Cache Line Size */
#else
	uint32_t cls                          : 8;
	uint32_t lt                           : 8;
	uint32_t ht                           : 8;
	uint32_t bcod                         : 4;
	uint32_t reserved_28_29               : 2;
	uint32_t brb                          : 1;
	uint32_t bcap                         : 1;
#endif
	} s;
	struct cvmx_pci_cfg03_s               cn30xx;
	struct cvmx_pci_cfg03_s               cn31xx;
	struct cvmx_pci_cfg03_s               cn38xx;
	struct cvmx_pci_cfg03_s               cn38xxp2;
	struct cvmx_pci_cfg03_s               cn50xx;
	struct cvmx_pci_cfg03_s               cn58xx;
	struct cvmx_pci_cfg03_s               cn58xxp1;
};
typedef union cvmx_pci_cfg03 cvmx_pci_cfg03_t;

/**
 * cvmx_pci_cfg04
 *
 * PCI_CFG04 = Fifth 32-bits of PCI config space (Base Address Register 0 - Low)
 *
 * Description: BAR0: 4KB 64-bit Prefetchable Memory Space
 *       [0]:     0 (Memory Space)
 *       [2:1]:   2 (64bit memory decoder)
 *       [3]:     1 (Prefetchable)
 *       [11:4]:  RAZ (to imply 4KB space)
 *       [31:12]: RW (User may define base address)
 */
union cvmx_pci_cfg04 {
	uint32_t u32;
	struct cvmx_pci_cfg04_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lbase                        : 20; /**< Base Address[31:12]
                                                         Base Address[30:12] read as zero if
                                                         PCI_CTL_STATUS_2[BB0] is set (in pass 3+) */
	uint32_t lbasez                       : 8;  /**< Base Address[11:4] (Read as Zero) */
	uint32_t pf                           : 1;  /**< Prefetchable Space */
	uint32_t typ                          : 2;  /**< Type (00=32b/01=below 1MB/10=64b/11=RSV) */
	uint32_t mspc                         : 1;  /**< Memory Space Indicator */
#else
	uint32_t mspc                         : 1;
	uint32_t typ                          : 2;
	uint32_t pf                           : 1;
	uint32_t lbasez                       : 8;
	uint32_t lbase                        : 20;
#endif
	} s;
	struct cvmx_pci_cfg04_s               cn30xx;
	struct cvmx_pci_cfg04_s               cn31xx;
	struct cvmx_pci_cfg04_s               cn38xx;
	struct cvmx_pci_cfg04_s               cn38xxp2;
	struct cvmx_pci_cfg04_s               cn50xx;
	struct cvmx_pci_cfg04_s               cn58xx;
	struct cvmx_pci_cfg04_s               cn58xxp1;
};
typedef union cvmx_pci_cfg04 cvmx_pci_cfg04_t;

/**
 * cvmx_pci_cfg05
 *
 * PCI_CFG05 = Sixth 32-bits of PCI config space (Base Address Register 0 - High)
 *
 */
union cvmx_pci_cfg05 {
	uint32_t u32;
	struct cvmx_pci_cfg05_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hbase                        : 32; /**< Base Address[63:32] */
#else
	uint32_t hbase                        : 32;
#endif
	} s;
	struct cvmx_pci_cfg05_s               cn30xx;
	struct cvmx_pci_cfg05_s               cn31xx;
	struct cvmx_pci_cfg05_s               cn38xx;
	struct cvmx_pci_cfg05_s               cn38xxp2;
	struct cvmx_pci_cfg05_s               cn50xx;
	struct cvmx_pci_cfg05_s               cn58xx;
	struct cvmx_pci_cfg05_s               cn58xxp1;
};
typedef union cvmx_pci_cfg05 cvmx_pci_cfg05_t;

/**
 * cvmx_pci_cfg06
 *
 * PCI_CFG06 = Seventh 32-bits of PCI config space (Base Address Register 1 - Low)
 *
 * Description: BAR1: 128MB 64-bit Prefetchable Memory Space
 *       [0]:     0 (Memory Space)
 *       [2:1]:   2 (64bit memory decoder)
 *       [3]:     1 (Prefetchable)
 *       [26:4]:  RAZ (to imply 128MB space)
 *       [31:27]: RW (User may define base address)
 */
union cvmx_pci_cfg06 {
	uint32_t u32;
	struct cvmx_pci_cfg06_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lbase                        : 5;  /**< Base Address[31:27]
                                                         In pass 3+:
                                                           Base Address[29:27] read as zero if
                                                            PCI_CTL_STATUS_2[BB1] is set
                                                           Base Address[30] reads as zero if
                                                            PCI_CTL_STATUS_2[BB1] is set and
                                                            PCI_CTL_STATUS_2[BB1_SIZE] is set */
	uint32_t lbasez                       : 23; /**< Base Address[26:4] (Read as Zero) */
	uint32_t pf                           : 1;  /**< Prefetchable Space */
	uint32_t typ                          : 2;  /**< Type (00=32b/01=below 1MB/10=64b/11=RSV) */
	uint32_t mspc                         : 1;  /**< Memory Space Indicator */
#else
	uint32_t mspc                         : 1;
	uint32_t typ                          : 2;
	uint32_t pf                           : 1;
	uint32_t lbasez                       : 23;
	uint32_t lbase                        : 5;
#endif
	} s;
	struct cvmx_pci_cfg06_s               cn30xx;
	struct cvmx_pci_cfg06_s               cn31xx;
	struct cvmx_pci_cfg06_s               cn38xx;
	struct cvmx_pci_cfg06_s               cn38xxp2;
	struct cvmx_pci_cfg06_s               cn50xx;
	struct cvmx_pci_cfg06_s               cn58xx;
	struct cvmx_pci_cfg06_s               cn58xxp1;
};
typedef union cvmx_pci_cfg06 cvmx_pci_cfg06_t;

/**
 * cvmx_pci_cfg07
 *
 * PCI_CFG07 = Eighth 32-bits of PCI config space (Base Address Register 1 - High)
 *
 */
union cvmx_pci_cfg07 {
	uint32_t u32;
	struct cvmx_pci_cfg07_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hbase                        : 32; /**< Base Address[63:32] */
#else
	uint32_t hbase                        : 32;
#endif
	} s;
	struct cvmx_pci_cfg07_s               cn30xx;
	struct cvmx_pci_cfg07_s               cn31xx;
	struct cvmx_pci_cfg07_s               cn38xx;
	struct cvmx_pci_cfg07_s               cn38xxp2;
	struct cvmx_pci_cfg07_s               cn50xx;
	struct cvmx_pci_cfg07_s               cn58xx;
	struct cvmx_pci_cfg07_s               cn58xxp1;
};
typedef union cvmx_pci_cfg07 cvmx_pci_cfg07_t;

/**
 * cvmx_pci_cfg08
 *
 * PCI_CFG08 = Ninth 32-bits of PCI config space (Base Address Register 2 - Low)
 *
 * Description: BAR1: 2^39 (512GB) 64-bit Prefetchable Memory Space
 *       [0]:     0 (Memory Space)
 *       [2:1]:   2 (64bit memory decoder)
 *       [3]:     1 (Prefetchable)
 *       [31:4]:  RAZ
 */
union cvmx_pci_cfg08 {
	uint32_t u32;
	struct cvmx_pci_cfg08_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lbasez                       : 28; /**< Base Address[31:4] (Read as Zero) */
	uint32_t pf                           : 1;  /**< Prefetchable Space */
	uint32_t typ                          : 2;  /**< Type (00=32b/01=below 1MB/10=64b/11=RSV) */
	uint32_t mspc                         : 1;  /**< Memory Space Indicator */
#else
	uint32_t mspc                         : 1;
	uint32_t typ                          : 2;
	uint32_t pf                           : 1;
	uint32_t lbasez                       : 28;
#endif
	} s;
	struct cvmx_pci_cfg08_s               cn30xx;
	struct cvmx_pci_cfg08_s               cn31xx;
	struct cvmx_pci_cfg08_s               cn38xx;
	struct cvmx_pci_cfg08_s               cn38xxp2;
	struct cvmx_pci_cfg08_s               cn50xx;
	struct cvmx_pci_cfg08_s               cn58xx;
	struct cvmx_pci_cfg08_s               cn58xxp1;
};
typedef union cvmx_pci_cfg08 cvmx_pci_cfg08_t;

/**
 * cvmx_pci_cfg09
 *
 * PCI_CFG09 = Tenth 32-bits of PCI config space (Base Address Register 2 - High)
 *
 */
union cvmx_pci_cfg09 {
	uint32_t u32;
	struct cvmx_pci_cfg09_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hbase                        : 25; /**< Base Address[63:39] */
	uint32_t hbasez                       : 7;  /**< Base Address[38:31]  (Read as Zero) */
#else
	uint32_t hbasez                       : 7;
	uint32_t hbase                        : 25;
#endif
	} s;
	struct cvmx_pci_cfg09_s               cn30xx;
	struct cvmx_pci_cfg09_s               cn31xx;
	struct cvmx_pci_cfg09_s               cn38xx;
	struct cvmx_pci_cfg09_s               cn38xxp2;
	struct cvmx_pci_cfg09_s               cn50xx;
	struct cvmx_pci_cfg09_s               cn58xx;
	struct cvmx_pci_cfg09_s               cn58xxp1;
};
typedef union cvmx_pci_cfg09 cvmx_pci_cfg09_t;

/**
 * cvmx_pci_cfg10
 *
 * PCI_CFG10 = Eleventh 32-bits of PCI config space (Card Bus CIS Pointer)
 *
 */
union cvmx_pci_cfg10 {
	uint32_t u32;
	struct cvmx_pci_cfg10_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cisp                         : 32; /**< CardBus CIS Pointer (UNUSED) */
#else
	uint32_t cisp                         : 32;
#endif
	} s;
	struct cvmx_pci_cfg10_s               cn30xx;
	struct cvmx_pci_cfg10_s               cn31xx;
	struct cvmx_pci_cfg10_s               cn38xx;
	struct cvmx_pci_cfg10_s               cn38xxp2;
	struct cvmx_pci_cfg10_s               cn50xx;
	struct cvmx_pci_cfg10_s               cn58xx;
	struct cvmx_pci_cfg10_s               cn58xxp1;
};
typedef union cvmx_pci_cfg10 cvmx_pci_cfg10_t;

/**
 * cvmx_pci_cfg11
 *
 * PCI_CFG11 = Twelfth 32-bits of PCI config space (SubSystem ID/Subsystem Vendor ID Register)
 *
 */
union cvmx_pci_cfg11 {
	uint32_t u32;
	struct cvmx_pci_cfg11_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ssid                         : 16; /**< SubSystem ID */
	uint32_t ssvid                        : 16; /**< Subsystem Vendor ID */
#else
	uint32_t ssvid                        : 16;
	uint32_t ssid                         : 16;
#endif
	} s;
	struct cvmx_pci_cfg11_s               cn30xx;
	struct cvmx_pci_cfg11_s               cn31xx;
	struct cvmx_pci_cfg11_s               cn38xx;
	struct cvmx_pci_cfg11_s               cn38xxp2;
	struct cvmx_pci_cfg11_s               cn50xx;
	struct cvmx_pci_cfg11_s               cn58xx;
	struct cvmx_pci_cfg11_s               cn58xxp1;
};
typedef union cvmx_pci_cfg11 cvmx_pci_cfg11_t;

/**
 * cvmx_pci_cfg12
 *
 * PCI_CFG12 = Thirteenth 32-bits of PCI config space (Expansion ROM Base Address Register)
 *
 */
union cvmx_pci_cfg12 {
	uint32_t u32;
	struct cvmx_pci_cfg12_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t erbar                        : 16; /**< Expansion ROM Base Address[31:16] 64KB in size */
	uint32_t erbarz                       : 5;  /**< Expansion ROM Base Base Address (Read as Zero) */
	uint32_t reserved_1_10                : 10;
	uint32_t erbar_en                     : 1;  /**< Expansion ROM Address Decode Enable */
#else
	uint32_t erbar_en                     : 1;
	uint32_t reserved_1_10                : 10;
	uint32_t erbarz                       : 5;
	uint32_t erbar                        : 16;
#endif
	} s;
	struct cvmx_pci_cfg12_s               cn30xx;
	struct cvmx_pci_cfg12_s               cn31xx;
	struct cvmx_pci_cfg12_s               cn38xx;
	struct cvmx_pci_cfg12_s               cn38xxp2;
	struct cvmx_pci_cfg12_s               cn50xx;
	struct cvmx_pci_cfg12_s               cn58xx;
	struct cvmx_pci_cfg12_s               cn58xxp1;
};
typedef union cvmx_pci_cfg12 cvmx_pci_cfg12_t;

/**
 * cvmx_pci_cfg13
 *
 * PCI_CFG13 = Fourteenth 32-bits of PCI config space (Capabilities Pointer Register)
 *
 */
union cvmx_pci_cfg13 {
	uint32_t u32;
	struct cvmx_pci_cfg13_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t cp                           : 8;  /**< Capabilities Pointer */
#else
	uint32_t cp                           : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_pci_cfg13_s               cn30xx;
	struct cvmx_pci_cfg13_s               cn31xx;
	struct cvmx_pci_cfg13_s               cn38xx;
	struct cvmx_pci_cfg13_s               cn38xxp2;
	struct cvmx_pci_cfg13_s               cn50xx;
	struct cvmx_pci_cfg13_s               cn58xx;
	struct cvmx_pci_cfg13_s               cn58xxp1;
};
typedef union cvmx_pci_cfg13 cvmx_pci_cfg13_t;

/**
 * cvmx_pci_cfg15
 *
 * PCI_CFG15 = Sixteenth 32-bits of PCI config space (INT/ARB/LATENCY Register)
 *
 */
union cvmx_pci_cfg15 {
	uint32_t u32;
	struct cvmx_pci_cfg15_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ml                           : 8;  /**< Maximum Latency */
	uint32_t mg                           : 8;  /**< Minimum Grant */
	uint32_t inta                         : 8;  /**< Interrupt Pin (INTA#) */
	uint32_t il                           : 8;  /**< Interrupt Line */
#else
	uint32_t il                           : 8;
	uint32_t inta                         : 8;
	uint32_t mg                           : 8;
	uint32_t ml                           : 8;
#endif
	} s;
	struct cvmx_pci_cfg15_s               cn30xx;
	struct cvmx_pci_cfg15_s               cn31xx;
	struct cvmx_pci_cfg15_s               cn38xx;
	struct cvmx_pci_cfg15_s               cn38xxp2;
	struct cvmx_pci_cfg15_s               cn50xx;
	struct cvmx_pci_cfg15_s               cn58xx;
	struct cvmx_pci_cfg15_s               cn58xxp1;
};
typedef union cvmx_pci_cfg15 cvmx_pci_cfg15_t;

/**
 * cvmx_pci_cfg16
 *
 * PCI_CFG16 = Seventeenth 32-bits of PCI config space (Target Implementation Register)
 *
 */
union cvmx_pci_cfg16 {
	uint32_t u32;
	struct cvmx_pci_cfg16_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t trdnpr                       : 1;  /**< Target Read Delayed Transaction for I/O and
                                                         non-prefetchable regions discarded. */
	uint32_t trdard                       : 1;  /**< Target Read Delayed Transaction for all regions
                                                         discarded. */
	uint32_t rdsati                       : 1;  /**< Target(I/O and Memory) Read Delayed/Split at
                                                          timeout/immediately (default timeout).
                                                         Note: OCTEON requires that this bit MBZ(must be zero). */
	uint32_t trdrs                        : 1;  /**< Target(I/O and Memory) Read Delayed/Split or Retry
                                                         select (of the application interface is not ready)
                                                          0 = Delayed Split Transaction
                                                          1 = Retry Transaction (always Immediate Retry, no
                                                              AT_REQ to application). */
	uint32_t trtae                        : 1;  /**< Target(I/O and Memory) Read Target Abort Enable
                                                         (if application interface is not ready at the
                                                         latency timeout).
                                                         Note: OCTEON as target will never target-abort,
                                                         therefore this bit should never be set. */
	uint32_t twsei                        : 1;  /**< Target(I/O) Write Split Enable (at timeout /
                                                         immediately; default timeout) */
	uint32_t twsen                        : 1;  /**< T(I/O) write split Enable (if the application
                                                         interface is not ready) */
	uint32_t twtae                        : 1;  /**< Target(I/O and Memory) Write Target Abort Enable
                                                         (if the application interface is not ready at the
                                                         start of the cycle).
                                                         Note: OCTEON as target will never target-abort,
                                                         therefore this bit should never be set. */
	uint32_t tmae                         : 1;  /**< Target(Read/Write) Master Abort Enable; check
                                                         at the start of each transaction.
                                                         Note: This bit can be used to force a Master
                                                         Abort when OCTEON is acting as the intended target
                                                         device. */
	uint32_t tslte                        : 3;  /**< Target Subsequent(2nd-last) Latency Timeout Enable
                                                         Valid range: [1..7] and 0=8. */
	uint32_t tilt                         : 4;  /**< Target Initial(1st data) Latency Timeout in PCI
                                                         ModeValid range: [8..15] and 0=16. */
	uint32_t pbe                          : 12; /**< Programmable Boundary Enable to disconnect/prefetch
                                                         for target burst read cycles to prefetchable
                                                         region in PCI. A value of 1 indicates end of
                                                         boundary (64 KB down to 16 Bytes). */
	uint32_t dppmr                        : 1;  /**< Disconnect/Prefetch to prefetchable memory
                                                         regions Enable. Prefetchable memory regions
                                                         are always disconnected on a region boundary.
                                                         Non-prefetchable regions for PCI are always
                                                         disconnected on the first transfer.
                                                         Note: OCTEON as target will never target-disconnect,
                                                         therefore this bit should never be set. */
	uint32_t reserved_2_2                 : 1;
	uint32_t tswc                         : 1;  /**< Target Split Write Control
                                                         0 = Blocks all requests except PMW
                                                         1 = Blocks all requests including PMW until
                                                             split completion occurs. */
	uint32_t mltd                         : 1;  /**< Master Latency Timer Disable
                                                         Note: For OCTEON, it is recommended that this bit
                                                         be set(to disable the Master Latency timer). */
#else
	uint32_t mltd                         : 1;
	uint32_t tswc                         : 1;
	uint32_t reserved_2_2                 : 1;
	uint32_t dppmr                        : 1;
	uint32_t pbe                          : 12;
	uint32_t tilt                         : 4;
	uint32_t tslte                        : 3;
	uint32_t tmae                         : 1;
	uint32_t twtae                        : 1;
	uint32_t twsen                        : 1;
	uint32_t twsei                        : 1;
	uint32_t trtae                        : 1;
	uint32_t trdrs                        : 1;
	uint32_t rdsati                       : 1;
	uint32_t trdard                       : 1;
	uint32_t trdnpr                       : 1;
#endif
	} s;
	struct cvmx_pci_cfg16_s               cn30xx;
	struct cvmx_pci_cfg16_s               cn31xx;
	struct cvmx_pci_cfg16_s               cn38xx;
	struct cvmx_pci_cfg16_s               cn38xxp2;
	struct cvmx_pci_cfg16_s               cn50xx;
	struct cvmx_pci_cfg16_s               cn58xx;
	struct cvmx_pci_cfg16_s               cn58xxp1;
};
typedef union cvmx_pci_cfg16 cvmx_pci_cfg16_t;

/**
 * cvmx_pci_cfg17
 *
 * PCI_CFG17 = Eighteenth 32-bits of PCI config space (Target Split Completion Message
 * Enable Register)
 */
union cvmx_pci_cfg17 {
	uint32_t u32;
	struct cvmx_pci_cfg17_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t tscme                        : 32; /**< Target Split Completion Message Enable
                                                          [31:30]: 00
                                                          [29]: Split Completion Error Indication
                                                          [28]: 0
                                                          [27:20]: Split Completion Message Index
                                                          [19:0]: 0x00000
                                                         For OCTEON, this register is intended for debug use
                                                         only. (as such, it is recommended NOT to be written
                                                         with anything other than ZEROES). */
#else
	uint32_t tscme                        : 32;
#endif
	} s;
	struct cvmx_pci_cfg17_s               cn30xx;
	struct cvmx_pci_cfg17_s               cn31xx;
	struct cvmx_pci_cfg17_s               cn38xx;
	struct cvmx_pci_cfg17_s               cn38xxp2;
	struct cvmx_pci_cfg17_s               cn50xx;
	struct cvmx_pci_cfg17_s               cn58xx;
	struct cvmx_pci_cfg17_s               cn58xxp1;
};
typedef union cvmx_pci_cfg17 cvmx_pci_cfg17_t;

/**
 * cvmx_pci_cfg18
 *
 * PCI_CFG18 = Nineteenth 32-bits of PCI config space (Target Delayed/Split Request
 * Pending Sequences)
 */
union cvmx_pci_cfg18 {
	uint32_t u32;
	struct cvmx_pci_cfg18_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t tdsrps                       : 32; /**< Target Delayed/Split Request Pending Sequences
                                                         The application uses this address to remove a
                                                         pending split sequence from the target queue by
                                                         clearing the appropriate bit. Example: Clearing [14]
                                                         clears the pending sequence \#14. An application
                                                         or configuration write to this address can clear this
                                                         register.
                                                         For OCTEON, this register is intended for debug use
                                                         only and MUST NEVER be written with anything other
                                                         than ZEROES. */
#else
	uint32_t tdsrps                       : 32;
#endif
	} s;
	struct cvmx_pci_cfg18_s               cn30xx;
	struct cvmx_pci_cfg18_s               cn31xx;
	struct cvmx_pci_cfg18_s               cn38xx;
	struct cvmx_pci_cfg18_s               cn38xxp2;
	struct cvmx_pci_cfg18_s               cn50xx;
	struct cvmx_pci_cfg18_s               cn58xx;
	struct cvmx_pci_cfg18_s               cn58xxp1;
};
typedef union cvmx_pci_cfg18 cvmx_pci_cfg18_t;

/**
 * cvmx_pci_cfg19
 *
 * PCI_CFG19 = Twentieth 32-bits of PCI config space (Master/Target Implementation Register)
 *
 */
union cvmx_pci_cfg19 {
	uint32_t u32;
	struct cvmx_pci_cfg19_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t mrbcm                        : 1;  /**< Master Request (Memory Read) Byte Count/Byte
                                                         Enable select.
                                                           0 = Byte Enables valid. In PCI mode, a burst
                                                               transaction cannot be performed using
                                                               Memory Read command=4'h6.
                                                           1 = DWORD Byte Count valid (default). In PCI
                                                               Mode, the memory read byte enables are
                                                               automatically generated by the core.
                                                          NOTE:  For OCTEON, this bit must always be one
                                                          for proper operation. */
	uint32_t mrbci                        : 1;  /**< Master Request (I/O and CR cycles) byte count/byte
                                                         enable select.
                                                           0 = Byte Enables valid (default)
                                                           1 = DWORD byte count valid
                                                          NOTE: For OCTEON, this bit must always be zero
                                                          for proper operation (in support of
                                                          Type0/1 Cfg Space accesses which require byte
                                                          enable generation directly from a read mask). */
	uint32_t mdwe                         : 1;  /**< Master (Retry) Deferred Write Enable (allow
                                                         read requests to pass).
                                                          NOTE: Applicable to PCI Mode I/O and memory
                                                          transactions only.
                                                           0 = New read requests are NOT accepted until
                                                               the current write cycle completes. [Reads
                                                               cannot pass writes]
                                                           1 = New read requests are accepted, even when
                                                               there is a write cycle pending [Reads can
                                                               pass writes].
                                                          NOTE: For OCTEON, this bit must always be zero
                                                          for proper operation. */
	uint32_t mdre                         : 1;  /**< Master (Retry) Deferred Read Enable (Allows
                                                         read/write requests to pass).
                                                          NOTE: Applicable to PCI mode I/O and memory
                                                          transactions only.
                                                           0 = New read/write requests are NOT accepted
                                                               until the current read cycle completes.
                                                               [Read/write requests CANNOT pass reads]
                                                           1 = New read/write requests are accepted, even
                                                               when there is a read cycle pending.
                                                               [Read/write requests CAN pass reads]
                                                          NOTE: For OCTEON, this bit must always be zero
                                                          for proper operation. */
	uint32_t mdrimc                       : 1;  /**< Master I/O Deferred/Split Request Outstanding
                                                         Maximum Count
                                                           0 = MDRRMC[26:24]
                                                           1 = 1 */
	uint32_t mdrrmc                       : 3;  /**< Master Deferred Read Request Outstanding Max
                                                         Count (PCI only).
                                                          CR4C[26:24]  Max SAC cycles   MAX DAC cycles
                                                           000              8                4
                                                           001              1                0
                                                           010              2                1
                                                           011              3                1
                                                           100              4                2
                                                           101              5                2
                                                           110              6                3
                                                           111              7                3
                                                          For example, if these bits are programmed to
                                                          100, the core can support 2 DAC cycles, 4 SAC
                                                          cycles or a combination of 1 DAC and 2 SAC cycles.
                                                          NOTE: For the PCI-X maximum outstanding split
                                                          transactions, refer to CRE0[22:20] */
	uint32_t tmes                         : 8;  /**< Target/Master Error Sequence \# */
	uint32_t teci                         : 1;  /**< Target Error Command Indication
                                                         0 = Delayed/Split
                                                         1 = Others */
	uint32_t tmei                         : 1;  /**< Target/Master Error Indication
                                                         0 = Target
                                                         1 = Master */
	uint32_t tmse                         : 1;  /**< Target/Master System Error. This bit is set
                                                         whenever ATM_SERR_O is active. */
	uint32_t tmdpes                       : 1;  /**< Target/Master Data PERR# error status. This
                                                         bit is set whenever ATM_DATA_PERR_O is active. */
	uint32_t tmapes                       : 1;  /**< Target/Master Address PERR# error status. This
                                                         bit is set whenever ATM_ADDR_PERR_O is active. */
	uint32_t reserved_9_10                : 2;
	uint32_t tibcd                        : 1;  /**< Target Illegal I/O DWORD byte combinations detected. */
	uint32_t tibde                        : 1;  /**< Target Illegal I/O DWORD byte detection enable */
	uint32_t reserved_6_6                 : 1;
	uint32_t tidomc                       : 1;  /**< Target I/O Delayed/Split request outstanding
                                                         maximum count.
                                                          0 = TDOMC[4:0]
                                                          1 = 1 */
	uint32_t tdomc                        : 5;  /**< Target Delayed/Split request outstanding maximum
                                                         count. [1..31] and 0=32.
                                                         NOTE: If the user programs these bits beyond the
                                                         Designed Maximum outstanding count, then the
                                                         designed maximum table depth will be used instead.
                                                         No additional Deferred/Split transactions will be
                                                         accepted if this outstanding maximum count
                                                         is reached. Furthermore, no additional
                                                         deferred/split transactions will be accepted if
                                                         the I/O delay/ I/O Split Request outstanding
                                                         maximum is reached.
                                                         NOTE: For OCTEON in PCI Mode, this field MUST BE
                                                         programmed to 1. (OCTEON can only handle 1 delayed
                                                         read at a time).
                                                         For OCTEON in PCIX Mode, this field can range from
                                                         1-4. (The designed maximum table depth is 4
                                                         for PCIX mode splits). */
#else
	uint32_t tdomc                        : 5;
	uint32_t tidomc                       : 1;
	uint32_t reserved_6_6                 : 1;
	uint32_t tibde                        : 1;
	uint32_t tibcd                        : 1;
	uint32_t reserved_9_10                : 2;
	uint32_t tmapes                       : 1;
	uint32_t tmdpes                       : 1;
	uint32_t tmse                         : 1;
	uint32_t tmei                         : 1;
	uint32_t teci                         : 1;
	uint32_t tmes                         : 8;
	uint32_t mdrrmc                       : 3;
	uint32_t mdrimc                       : 1;
	uint32_t mdre                         : 1;
	uint32_t mdwe                         : 1;
	uint32_t mrbci                        : 1;
	uint32_t mrbcm                        : 1;
#endif
	} s;
	struct cvmx_pci_cfg19_s               cn30xx;
	struct cvmx_pci_cfg19_s               cn31xx;
	struct cvmx_pci_cfg19_s               cn38xx;
	struct cvmx_pci_cfg19_s               cn38xxp2;
	struct cvmx_pci_cfg19_s               cn50xx;
	struct cvmx_pci_cfg19_s               cn58xx;
	struct cvmx_pci_cfg19_s               cn58xxp1;
};
typedef union cvmx_pci_cfg19 cvmx_pci_cfg19_t;

/**
 * cvmx_pci_cfg20
 *
 * PCI_CFG20 = Twenty-first 32-bits of PCI config space (Master Deferred/Split Sequence Pending)
 *
 */
union cvmx_pci_cfg20 {
	uint32_t u32;
	struct cvmx_pci_cfg20_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t mdsp                         : 32; /**< Master Deferred/Split sequence Pending
                                                         For OCTEON, this register is intended for debug use
                                                         only and MUST NEVER be written with anything other
                                                         than ZEROES. */
#else
	uint32_t mdsp                         : 32;
#endif
	} s;
	struct cvmx_pci_cfg20_s               cn30xx;
	struct cvmx_pci_cfg20_s               cn31xx;
	struct cvmx_pci_cfg20_s               cn38xx;
	struct cvmx_pci_cfg20_s               cn38xxp2;
	struct cvmx_pci_cfg20_s               cn50xx;
	struct cvmx_pci_cfg20_s               cn58xx;
	struct cvmx_pci_cfg20_s               cn58xxp1;
};
typedef union cvmx_pci_cfg20 cvmx_pci_cfg20_t;

/**
 * cvmx_pci_cfg21
 *
 * PCI_CFG21 = Twenty-second 32-bits of PCI config space (Master Split Completion Message Register)
 *
 */
union cvmx_pci_cfg21 {
	uint32_t u32;
	struct cvmx_pci_cfg21_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t scmre                        : 32; /**< Master Split Completion message received with
                                                         error message.
                                                         For OCTEON, this register is intended for debug use
                                                         only and MUST NEVER be written with anything other
                                                         than ZEROES. */
#else
	uint32_t scmre                        : 32;
#endif
	} s;
	struct cvmx_pci_cfg21_s               cn30xx;
	struct cvmx_pci_cfg21_s               cn31xx;
	struct cvmx_pci_cfg21_s               cn38xx;
	struct cvmx_pci_cfg21_s               cn38xxp2;
	struct cvmx_pci_cfg21_s               cn50xx;
	struct cvmx_pci_cfg21_s               cn58xx;
	struct cvmx_pci_cfg21_s               cn58xxp1;
};
typedef union cvmx_pci_cfg21 cvmx_pci_cfg21_t;

/**
 * cvmx_pci_cfg22
 *
 * PCI_CFG22 = Twenty-third 32-bits of PCI config space (Master Arbiter Control Register)
 *
 */
union cvmx_pci_cfg22 {
	uint32_t u32;
	struct cvmx_pci_cfg22_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t mac                          : 7;  /**< Master Arbiter Control
                                                         [31:26]: Used only in Fixed Priority mode
                                                                  (when [25]=1)
                                                         [31:30]: MSI Request
                                                            00 = Highest Priority
                                                            01 = Medium Priority
                                                            10 = Lowest Priority
                                                            11 = RESERVED
                                                         [29:28]: Target Split Completion
                                                            00 = Highest Priority
                                                            01 = Medium Priority
                                                            10 = Lowest Priority
                                                            11 = RESERVED
                                                         [27:26]: New Request; Deferred Read,Deferred Write
                                                            00 = Highest Priority
                                                            01 = Medium Priority
                                                            10 = Lowest Priority
                                                            11 = RESERVED
                                                         [25]: Fixed/Round Robin Priority Selector
                                                            0 = Round Robin
                                                            1 = Fixed
                                                         NOTE: When [25]=1(fixed priority), the three levels
                                                         [31:26] MUST BE programmed to have mutually exclusive
                                                         priority levels for proper operation. (Failure to do
                                                         so may result in PCI hangs). */
	uint32_t reserved_19_24               : 6;
	uint32_t flush                        : 1;  /**< AM_DO_FLUSH_I control
                                                         NOTE: This bit MUST BE ONE for proper OCTEON operation */
	uint32_t mra                          : 1;  /**< Master Retry Aborted */
	uint32_t mtta                         : 1;  /**< Master TRDY timeout aborted */
	uint32_t mrv                          : 8;  /**< Master Retry Value [1..255] and 0=infinite */
	uint32_t mttv                         : 8;  /**< Master TRDY timeout value [1..255] and 0=disabled
                                                         NOTE: For OCTEON, this bit must always be zero
                                                         for proper operation. (OCTEON does not support
                                                         master TRDY timeout - target is expected to be
                                                         well behaved). */
#else
	uint32_t mttv                         : 8;
	uint32_t mrv                          : 8;
	uint32_t mtta                         : 1;
	uint32_t mra                          : 1;
	uint32_t flush                        : 1;
	uint32_t reserved_19_24               : 6;
	uint32_t mac                          : 7;
#endif
	} s;
	struct cvmx_pci_cfg22_s               cn30xx;
	struct cvmx_pci_cfg22_s               cn31xx;
	struct cvmx_pci_cfg22_s               cn38xx;
	struct cvmx_pci_cfg22_s               cn38xxp2;
	struct cvmx_pci_cfg22_s               cn50xx;
	struct cvmx_pci_cfg22_s               cn58xx;
	struct cvmx_pci_cfg22_s               cn58xxp1;
};
typedef union cvmx_pci_cfg22 cvmx_pci_cfg22_t;

/**
 * cvmx_pci_cfg56
 *
 * PCI_CFG56 = Fifty-seventh 32-bits of PCI config space (PCIX Capabilities Register)
 *
 */
union cvmx_pci_cfg56 {
	uint32_t u32;
	struct cvmx_pci_cfg56_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t most                         : 3;  /**< Maximum outstanding Split transactions
                                                           Encoded Value    \#Max outstanding splits
                                                               000                   1
                                                               001                   2
                                                               010                   3
                                                               011                   4
                                                               100                   8
                                                               101                   8(clamped)
                                                               110                   8(clamped)
                                                               111                   8(clamped)
                                                         NOTE: OCTEON only supports upto a MAXIMUM of 8
                                                         outstanding master split transactions. */
	uint32_t mmbc                         : 2;  /**< Maximum Memory Byte Count
                                                                 [0=512B,1=1024B,2=2048B,3=4096B]
                                                         NOTE: OCTEON does not support this field and has
                                                         no effect on limiting the maximum memory byte count. */
	uint32_t roe                          : 1;  /**< Relaxed Ordering Enable */
	uint32_t dpere                        : 1;  /**< Data Parity Error Recovery Enable */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer */
	uint32_t pxcid                        : 8;  /**< PCI-X Capability ID */
#else
	uint32_t pxcid                        : 8;
	uint32_t ncp                          : 8;
	uint32_t dpere                        : 1;
	uint32_t roe                          : 1;
	uint32_t mmbc                         : 2;
	uint32_t most                         : 3;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_pci_cfg56_s               cn30xx;
	struct cvmx_pci_cfg56_s               cn31xx;
	struct cvmx_pci_cfg56_s               cn38xx;
	struct cvmx_pci_cfg56_s               cn38xxp2;
	struct cvmx_pci_cfg56_s               cn50xx;
	struct cvmx_pci_cfg56_s               cn58xx;
	struct cvmx_pci_cfg56_s               cn58xxp1;
};
typedef union cvmx_pci_cfg56 cvmx_pci_cfg56_t;

/**
 * cvmx_pci_cfg57
 *
 * PCI_CFG57 = Fifty-eigth 32-bits of PCI config space (PCIX Status Register)
 *
 */
union cvmx_pci_cfg57 {
	uint32_t u32;
	struct cvmx_pci_cfg57_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_30_31               : 2;
	uint32_t scemr                        : 1;  /**< Split Completion Error Message Received */
	uint32_t mcrsd                        : 3;  /**< Maximum Cumulative Read Size designed */
	uint32_t mostd                        : 3;  /**< Maximum Outstanding Split transaction designed */
	uint32_t mmrbcd                       : 2;  /**< Maximum Memory Read byte count designed */
	uint32_t dc                           : 1;  /**< Device Complexity
                                                         0 = Simple Device
                                                         1 = Bridge Device */
	uint32_t usc                          : 1;  /**< Unexpected Split Completion */
	uint32_t scd                          : 1;  /**< Split Completion Discarded */
	uint32_t m133                         : 1;  /**< 133MHz Capable */
	uint32_t w64                          : 1;  /**< Indicates a 32b(=0) or 64b(=1) device */
	uint32_t bn                           : 8;  /**< Bus Number. Updated on all configuration write
                                                         (0x11=PCI)             cycles. Its value is dependent upon the PCI/X
                                                         (0xFF=PCIX)            mode. */
	uint32_t dn                           : 5;  /**< Device Number. Updated on all configuration
                                                         write cycles. */
	uint32_t fn                           : 3;  /**< Function Number */
#else
	uint32_t fn                           : 3;
	uint32_t dn                           : 5;
	uint32_t bn                           : 8;
	uint32_t w64                          : 1;
	uint32_t m133                         : 1;
	uint32_t scd                          : 1;
	uint32_t usc                          : 1;
	uint32_t dc                           : 1;
	uint32_t mmrbcd                       : 2;
	uint32_t mostd                        : 3;
	uint32_t mcrsd                        : 3;
	uint32_t scemr                        : 1;
	uint32_t reserved_30_31               : 2;
#endif
	} s;
	struct cvmx_pci_cfg57_s               cn30xx;
	struct cvmx_pci_cfg57_s               cn31xx;
	struct cvmx_pci_cfg57_s               cn38xx;
	struct cvmx_pci_cfg57_s               cn38xxp2;
	struct cvmx_pci_cfg57_s               cn50xx;
	struct cvmx_pci_cfg57_s               cn58xx;
	struct cvmx_pci_cfg57_s               cn58xxp1;
};
typedef union cvmx_pci_cfg57 cvmx_pci_cfg57_t;

/**
 * cvmx_pci_cfg58
 *
 * PCI_CFG58 = Fifty-ninth 32-bits of PCI config space (Power Management Capabilities Register)
 *
 */
union cvmx_pci_cfg58 {
	uint32_t u32;
	struct cvmx_pci_cfg58_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pmes                         : 5;  /**< PME Support (D0 to D3cold) */
	uint32_t d2s                          : 1;  /**< D2_Support */
	uint32_t d1s                          : 1;  /**< D1_Support */
	uint32_t auxc                         : 3;  /**< AUX_Current (0..375mA) */
	uint32_t dsi                          : 1;  /**< Device Specific Initialization */
	uint32_t reserved_20_20               : 1;
	uint32_t pmec                         : 1;  /**< PME Clock */
	uint32_t pcimiv                       : 3;  /**< Indicates the version of the PCI
                                                         Management
                                                          Interface Specification with which the core
                                                          complies.
                                                            010b = Complies with PCI Management Interface
                                                            Specification Revision 1.1 */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer */
	uint32_t pmcid                        : 8;  /**< Power Management Capability ID */
#else
	uint32_t pmcid                        : 8;
	uint32_t ncp                          : 8;
	uint32_t pcimiv                       : 3;
	uint32_t pmec                         : 1;
	uint32_t reserved_20_20               : 1;
	uint32_t dsi                          : 1;
	uint32_t auxc                         : 3;
	uint32_t d1s                          : 1;
	uint32_t d2s                          : 1;
	uint32_t pmes                         : 5;
#endif
	} s;
	struct cvmx_pci_cfg58_s               cn30xx;
	struct cvmx_pci_cfg58_s               cn31xx;
	struct cvmx_pci_cfg58_s               cn38xx;
	struct cvmx_pci_cfg58_s               cn38xxp2;
	struct cvmx_pci_cfg58_s               cn50xx;
	struct cvmx_pci_cfg58_s               cn58xx;
	struct cvmx_pci_cfg58_s               cn58xxp1;
};
typedef union cvmx_pci_cfg58 cvmx_pci_cfg58_t;

/**
 * cvmx_pci_cfg59
 *
 * PCI_CFG59 = Sixtieth 32-bits of PCI config space (Power Management Data/PMCSR Register(s))
 *
 */
union cvmx_pci_cfg59 {
	uint32_t u32;
	struct cvmx_pci_cfg59_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pmdia                        : 8;  /**< Power Management data input from application
                                                         (PME_DATA) */
	uint32_t bpccen                       : 1;  /**< BPCC_En (bus power/clock control) enable */
	uint32_t bd3h                         : 1;  /**< B2_B3\#, B2/B3 Support for D3hot */
	uint32_t reserved_16_21               : 6;
	uint32_t pmess                        : 1;  /**< PME_Status sticky bit */
	uint32_t pmedsia                      : 2;  /**< PME_Data_Scale input from application
                                                         Device                  (PME_DATA_SCALE[1:0])
                                                         Specific */
	uint32_t pmds                         : 4;  /**< Power Management Data_select */
	uint32_t pmeens                       : 1;  /**< PME_En sticky bit */
	uint32_t reserved_2_7                 : 6;
	uint32_t ps                           : 2;  /**< Power State (D0 to D3)
                                                         The N2 DOES NOT support D1/D2 Power Management
                                                         states, therefore writing to this register has
                                                         no effect (please refer to the PCI Power
                                                         Management
                                                         Specification v1.1 for further details about
                                                         it?s R/W nature. This is not a conventional
                                                         R/W style register. */
#else
	uint32_t ps                           : 2;
	uint32_t reserved_2_7                 : 6;
	uint32_t pmeens                       : 1;
	uint32_t pmds                         : 4;
	uint32_t pmedsia                      : 2;
	uint32_t pmess                        : 1;
	uint32_t reserved_16_21               : 6;
	uint32_t bd3h                         : 1;
	uint32_t bpccen                       : 1;
	uint32_t pmdia                        : 8;
#endif
	} s;
	struct cvmx_pci_cfg59_s               cn30xx;
	struct cvmx_pci_cfg59_s               cn31xx;
	struct cvmx_pci_cfg59_s               cn38xx;
	struct cvmx_pci_cfg59_s               cn38xxp2;
	struct cvmx_pci_cfg59_s               cn50xx;
	struct cvmx_pci_cfg59_s               cn58xx;
	struct cvmx_pci_cfg59_s               cn58xxp1;
};
typedef union cvmx_pci_cfg59 cvmx_pci_cfg59_t;

/**
 * cvmx_pci_cfg60
 *
 * PCI_CFG60 = Sixty-first 32-bits of PCI config space (MSI Capabilities Register)
 *
 */
union cvmx_pci_cfg60 {
	uint32_t u32;
	struct cvmx_pci_cfg60_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t m64                          : 1;  /**< 32/64 b message */
	uint32_t mme                          : 3;  /**< Multiple Message Enable(1,2,4,8,16,32) */
	uint32_t mmc                          : 3;  /**< Multiple Message Capable(0=1,1=2,2=4,3=8,4=16,5=32) */
	uint32_t msien                        : 1;  /**< MSI Enable */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer */
	uint32_t msicid                       : 8;  /**< MSI Capability ID */
#else
	uint32_t msicid                       : 8;
	uint32_t ncp                          : 8;
	uint32_t msien                        : 1;
	uint32_t mmc                          : 3;
	uint32_t mme                          : 3;
	uint32_t m64                          : 1;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_pci_cfg60_s               cn30xx;
	struct cvmx_pci_cfg60_s               cn31xx;
	struct cvmx_pci_cfg60_s               cn38xx;
	struct cvmx_pci_cfg60_s               cn38xxp2;
	struct cvmx_pci_cfg60_s               cn50xx;
	struct cvmx_pci_cfg60_s               cn58xx;
	struct cvmx_pci_cfg60_s               cn58xxp1;
};
typedef union cvmx_pci_cfg60 cvmx_pci_cfg60_t;

/**
 * cvmx_pci_cfg61
 *
 * PCI_CFG61 = Sixty-second 32-bits of PCI config space (MSI Lower Address Register)
 *
 */
union cvmx_pci_cfg61 {
	uint32_t u32;
	struct cvmx_pci_cfg61_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t msi31t2                      : 30; /**< App Specific    MSI Address [31:2] */
	uint32_t reserved_0_1                 : 2;
#else
	uint32_t reserved_0_1                 : 2;
	uint32_t msi31t2                      : 30;
#endif
	} s;
	struct cvmx_pci_cfg61_s               cn30xx;
	struct cvmx_pci_cfg61_s               cn31xx;
	struct cvmx_pci_cfg61_s               cn38xx;
	struct cvmx_pci_cfg61_s               cn38xxp2;
	struct cvmx_pci_cfg61_s               cn50xx;
	struct cvmx_pci_cfg61_s               cn58xx;
	struct cvmx_pci_cfg61_s               cn58xxp1;
};
typedef union cvmx_pci_cfg61 cvmx_pci_cfg61_t;

/**
 * cvmx_pci_cfg62
 *
 * PCI_CFG62 = Sixty-third 32-bits of PCI config space (MSI Upper Address Register)
 *
 */
union cvmx_pci_cfg62 {
	uint32_t u32;
	struct cvmx_pci_cfg62_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t msi                          : 32; /**< MSI Address [63:32] */
#else
	uint32_t msi                          : 32;
#endif
	} s;
	struct cvmx_pci_cfg62_s               cn30xx;
	struct cvmx_pci_cfg62_s               cn31xx;
	struct cvmx_pci_cfg62_s               cn38xx;
	struct cvmx_pci_cfg62_s               cn38xxp2;
	struct cvmx_pci_cfg62_s               cn50xx;
	struct cvmx_pci_cfg62_s               cn58xx;
	struct cvmx_pci_cfg62_s               cn58xxp1;
};
typedef union cvmx_pci_cfg62 cvmx_pci_cfg62_t;

/**
 * cvmx_pci_cfg63
 *
 * PCI_CFG63 = Sixty-fourth 32-bits of PCI config space (MSI Message Data Register)
 *
 */
union cvmx_pci_cfg63 {
	uint32_t u32;
	struct cvmx_pci_cfg63_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t msimd                        : 16; /**< MSI Message Data */
#else
	uint32_t msimd                        : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_pci_cfg63_s               cn30xx;
	struct cvmx_pci_cfg63_s               cn31xx;
	struct cvmx_pci_cfg63_s               cn38xx;
	struct cvmx_pci_cfg63_s               cn38xxp2;
	struct cvmx_pci_cfg63_s               cn50xx;
	struct cvmx_pci_cfg63_s               cn58xx;
	struct cvmx_pci_cfg63_s               cn58xxp1;
};
typedef union cvmx_pci_cfg63 cvmx_pci_cfg63_t;

/**
 * cvmx_pci_cnt_reg
 *
 * PCI_CNT_REG = PCI Clock Count Register
 *
 * This register is provided to software as a means to determine PCI Bus Type/Speed.
 */
union cvmx_pci_cnt_reg {
	uint64_t u64;
	struct cvmx_pci_cnt_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t hm_pcix                      : 1;  /**< PCI Host Mode Sampled Bus Type (0:PCI/1:PCIX)
                                                         This field represents what OCTEON(in Host mode)
                                                         sampled as the 'intended' PCI Bus Type based on
                                                         the PCI_PCIXCAP pin. (see HM_SPEED Bus Type/Speed
                                                         encoding table). */
	uint64_t hm_speed                     : 2;  /**< PCI Host Mode Sampled Bus Speed
                                                          This field represents what OCTEON(in Host mode)
                                                          sampled as the 'intended' PCI Bus Speed based on
                                                          the PCI100, PCI_M66EN and PCI_PCIXCAP pins.
                                                          NOTE: This DOES NOT reflect what the actual PCI
                                                          Bus Type/Speed values are. They only indicate what
                                                          OCTEON sampled as the 'intended' values.
                                                          PCI Host Mode Sampled Bus Type/Speed Table:
                                                            M66EN | PCIXCAP | PCI100  |  HM_PCIX | HM_SPEED[1:0]
                                                         ---------+---------+---------+----------+-------------
                                                              0   |    0    |    0    | 0=PCI    |  00=33 MHz
                                                              0   |    0    |    1    | 0=PCI    |  00=33 MHz
                                                              0   |    Z    |    0    | 0=PCI    |  01=66 MHz
                                                              0   |    Z    |    1    | 0=PCI    |  01=66 MHz
                                                              1   |    0    |    0    | 0=PCI    |  01=66 MHz
                                                              1   |    0    |    1    | 0=PCI    |  01=66 MHz
                                                              1   |    Z    |    0    | 0=PCI    |  01=66 MHz
                                                              1   |    Z    |    1    | 0=PCI    |  01=66 MHz
                                                              0   |    1    |    1    | 1=PCIX   |  10=100 MHz
                                                              1   |    1    |    1    | 1=PCIX   |  10=100 MHz
                                                              0   |    1    |    0    | 1=PCIX   |  11=133 MHz
                                                              1   |    1    |    0    | 1=PCIX   |  11=133 MHz
                                                          NOTE: PCIXCAP has tri-level value (0,1,Z). See PCI specification
                                                          for more details on board level hookup to achieve these
                                                          values.
                                                          NOTE: Software can use the NPI_PCI_INT_ARB_CFG[PCI_OVR]
                                                          to override the 'sampled' PCI Bus Type/Speed.
                                                          NOTE: Software can also use the PCI_CNT_REG[PCICNT] to determine
                                                          the exact PCI(X) Bus speed.
                                                          Example: PCI_REF_CLKIN=133MHz
                                                             PCI_HOST_MODE=1
                                                             PCI_M66EN=0
                                                             PCI_PCIXCAP=1
                                                             PCI_PCI100=1
                                                          For this example, OCTEON will generate
                                                          PCI_CLK_OUT=100MHz and drive the proper PCI
                                                          Initialization sequence (DEVSEL#=Deasserted,
                                                          STOP#=Asserted, TRDY#=Asserted) during PCI_RST_N
                                                          deassertion.
                                                          NOTE: The HM_SPEED field is only valid after
                                                          PLL_REF_CLK is active and PLL_DCOK is asserted.
                                                          (see HRM description for power-on/reset sequence).
                                                          NOTE: PCI_REF_CLKIN input must be 133MHz (and is used
                                                          to generate the PCI_CLK_OUT pin in Host Mode). */
	uint64_t ap_pcix                      : 1;  /**< PCI(X) Bus Type (0:PCI/1:PCIX)
                                                         At PCI_RST_N de-assertion, the PCI Initialization
                                                         pattern(PCI_DEVSEL_N, PCI_STOP_N, PCI_TRDY_N) is
                                                         captured to provide information to software regarding
                                                         the PCI Bus Type(PCI/PCIX) and PCI Bus Speed Range. */
	uint64_t ap_speed                     : 2;  /**< PCI(X) Bus Speed (0:33/1:66/2:100/3:133)
                                                                                    At PCI_RST_N de-assertion, the PCI Initialization
                                                                                    pattern(PCI_DEVSEL_N, PCI_STOP_N, PCI_TRDY_N) is
                                                                                    captured to provide information to software regarding
                                                                                    the PCI Bus Type(PCI/PCIX) and PCI Bus Speed Range.
                                                                                    PCI-X Initialization Pattern(see PCIX Spec):
                                                           PCI_DEVSEL_N PCI_STOP_N PCI_TRDY_N Mode    MaxClk(ns) MinClk(ns) MinClk(MHz) MaxClk(MHz)
                                                         -------------+----------+----------+-------+---------+----------+----------+------------------
                                                            Deasserted Deasserted Deasserted PCI 33    --         30          0         33
                                                                                             PCI 66    30         15         33         66
                                                            Deasserted Deasserted Asserted   PCI-X     20         15         50         66
                                                            Deasserted Asserted   Deasserted PCI-X     15         10         66        100
                                                            Deasserted Asserted   Asserted   PCI-X     10         7.5       100        133
                                                            Asserted   Deasserted Deasserted PCI-X   Reserved   Reserved   Reserved   Reserved
                                                            Asserted   Deasserted Asserted   PCI-X   Reserved   Reserved   Reserved   Reserved
                                                            Asserted   Asserted   Deasserted PCI-X   Reserved   Reserved   Reserved   Reserved
                                                            Asserted   Asserted   Asserted   PCI-X   Reserved   Reserved   Reserved   Reserved
                                                                                    NOTE: The PCI Bus speed 'assumed' from the initialization
                                                                                    pattern is really intended for an operational range.
                                                                                    For example: If PINIT=100, this indicates PCI-X in the
                                                                                    100-133MHz range. The PCI_CNT field can be used to further
                                                                                    determine a more exacting PCI Bus frequency value if
                                                                                    required. */
	uint64_t pcicnt                       : 32; /**< Free Running PCI Clock counter.
                                                         At PCI Reset, the PCICNT=0, and is auto-incremented
                                                         on every PCI clock and will auto-wrap back to zero
                                                         when saturated.
                                                         NOTE: Writes override the auto-increment to allow
                                                         software to preload any initial value.
                                                         The PCICNT field is provided to software as a means
                                                         to determine the PCI Bus Speed.
                                                         Assuming software has knowledge of the core frequency
                                                         (eclk), this register can be written with a value X,
                                                         wait 'n' core clocks(eclk) and then read later(Y) to
                                                         determine \#PCI clocks(Y-X) have elapsed within 'n' core
                                                         clocks to determine the PCI input Clock frequency. */
#else
	uint64_t pcicnt                       : 32;
	uint64_t ap_speed                     : 2;
	uint64_t ap_pcix                      : 1;
	uint64_t hm_speed                     : 2;
	uint64_t hm_pcix                      : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_pci_cnt_reg_s             cn50xx;
	struct cvmx_pci_cnt_reg_s             cn58xx;
	struct cvmx_pci_cnt_reg_s             cn58xxp1;
};
typedef union cvmx_pci_cnt_reg cvmx_pci_cnt_reg_t;

/**
 * cvmx_pci_ctl_status_2
 *
 * PCI_CTL_STATUS_2 = PCI Control Status 2 Register
 *
 * Control status register accessable from both PCI and NCB.
 */
union cvmx_pci_ctl_status_2 {
	uint32_t u32;
	struct cvmx_pci_ctl_status_2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t bb1_hole                     : 3;  /**< Big BAR 1 Hole
                                                         NOT IN PASS 1 NOR PASS 2
                                                         When PCI_CTL_STATUS_2[BB1]=1, this field defines
                                                         an encoded size of the upper BAR1 region which
                                                         OCTEON will mask out (ie: not respond to).
                                                         (see definition of BB1_HOLE and BB1_SIZ encodings
                                                         in the PCI_CTL_STATUS_2[BB1] definition below). */
	uint32_t bb1_siz                      : 1;  /**< Big BAR 1 Size
                                                         NOT IN PASS 1 NOR PASS 2
                                                         When PCI_CTL_STATUS_2[BB1]=1, this field defines
                                                         the programmable SIZE of BAR 1.
                                                           - 0: 1GB / 1: 2GB */
	uint32_t bb_ca                        : 1;  /**< Set to '1' for Big Bar Mode to do STT/LDT L2C
                                                         operations.
                                                         NOT IN PASS 1 NOR PASS 2 */
	uint32_t bb_es                        : 2;  /**< Big Bar Node Endian Swap Mode
                                                           - 0: No Swizzle
                                                           - 1: Byte Swizzle (per-QW)
                                                           - 2: Byte Swizzle (per-LW)
                                                           - 3: LongWord Swizzle
                                                         NOT IN PASS 1 NOR PASS 2 */
	uint32_t bb1                          : 1;  /**< Big Bar 1 Enable
                                                         NOT IN PASS 1 NOR PASS 2
                                                         When PCI_CTL_STATUS_2[BB1] is set, the following differences
                                                         occur:
                                                         - OCTEON's BAR1 becomes somewhere in the range 512-2048 MB rather
                                                           than the default 128MB.
                                                         - The following table indicates the effective size of
                                                           BAR1 when BB1 is set:
                                                             BB1_SIZ   BB1_HOLE  Effective size    Comment
                                                           +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                                0          0         1024 MB      Normal 1GB BAR
                                                                0          1         1008 MB      1 GB, 16 MB hole
                                                                0          2          992 MB      1 GB, 32 MB hole
                                                                0          3          960 MB      1 GB, 64 MB hole
                                                                0          4          896 MB      1 GB,128 MB hole
                                                                0          5          768 MB      1 GB,256 MB hole
                                                                0          6          512 MB      1 GB,512 MB hole
                                                                0          7         Illegal
                                                                1          0         2048 MB      Normal 2GB BAR
                                                                1          1         2032 MB      2 GB, 16 MB hole
                                                                1          2         2016 MB      2 GB, 32 MB hole
                                                                1          3         1984 MB      2 GB, 64 MB hole
                                                                1          4         1920 MB      2 GB,128 MB hole
                                                                1          5         1792 MB      2 GB,256 MB hole
                                                                1          6         1536 MB      2 GB,512 MB hole
                                                                1          7         Illegal
                                                         - When BB1_SIZ is 0: PCI_CFG06[LBASE<2:0>] reads as zero
                                                           and are ignored on write. BAR1 is an entirely ordinary
                                                           1 GB (power-of-two) BAR in all aspects when BB1_HOLE is 0.
                                                           When BB1_HOLE is not zero, BAR1 addresses are programmed
                                                           as if the BAR were 1GB, but, OCTEON does not respond
                                                           to addresses in the programmed holes.
                                                         - When BB1_SIZ is 1: PCI_CFG06[LBASE<3:0>] reads as zero
                                                           and are ignored on write. BAR1 is an entirely ordinary
                                                           2 GB (power-of-two) BAR in all aspects when BB1_HOLE is 0.
                                                           When BB1_HOLE is not zero, BAR1 addresses are programmed
                                                           as if the BAR were 2GB, but, OCTEON does not respond
                                                           to addresses in the programmed holes.
                                                         - Note that the BB1_HOLE value has no effect on the
                                                           PCI_CFG06[LBASE] behavior. BB1_HOLE only affects whether
                                                           OCTEON accepts an address. BB1_SIZ does affect PCI_CFG06[LBASE]
                                                           behavior, however.
                                                         - The first 128MB, i.e. addresses on the PCI bus in the range
                                                             BAR1+0          .. BAR1+0x07FFFFFF
                                                           access OCTEON's DRAM addresses with PCI_BAR1_INDEX CSR's
                                                           as before
                                                         - The remaining address space, i.e. addresses
                                                           on the PCI bus in the range
                                                              BAR1+0x08000000 .. BAR1+size-1,
                                                           where size is the size of BAR1 as selected by the above
                                                           table (based on the BB1_SIZ and BB1_HOLE values), are mapped to
                                                           OCTEON physical DRAM addresses as follows:
                                                                   PCI Address Range         OCTEON Physical Address Range
                                                           ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                            BAR1+0x08000000 .. BAR1+size-1 | 0x88000000 .. 0x7FFFFFFF+size
                                                           and PCI_CTL_STATUS_2[BB_ES] is the endian-swap and
                                                           PCI_CTL_STATUS_2[BB_CA] is the L2 cache allocation bit
                                                           for these references.
                                                           The consequences of any burst that crosses the end of the PCI
                                                           Address Range for BAR1 are unpredicable.
                                                         - The consequences of any burst access that crosses the boundary
                                                           between BAR1+0x07FFFFFF and BAR1+0x08000000 are unpredictable in PCI-X
                                                           mode. OCTEON may disconnect PCI references at this boundary. */
	uint32_t bb0                          : 1;  /**< Big Bar 0 Enable
                                                         NOT IN PASS 1 NOR PASS 2
                                                         When PCI_CTL_STATUS_2[BB0] is set, the following
                                                         differences occur:
                                                         - OCTEON's BAR0 becomes 2GB rather than the default 4KB.
                                                           PCI_CFG04[LBASE<18:0>] reads as zero and is ignored on write.
                                                         - OCTEON's BAR0 becomes burstable. (When BB0 is clear, OCTEON
                                                           single-phase disconnects PCI BAR0 reads and PCI/PCI-X BAR0
                                                           writes, and splits (burstably) PCI-X BAR0 reads.)
                                                         - The first 4KB, i.e. addresses on the PCI bus in the range
                                                               BAR0+0      .. BAR0+0xFFF
                                                           access OCTEON's PCI-type CSR's as when BB0 is clear.
                                                         - The remaining address space, i.e. addresses on the PCI bus
                                                           in the range
                                                               BAR0+0x1000 .. BAR0+0x7FFFFFFF
                                                           are mapped to OCTEON physical DRAM addresses as follows:
                                                              PCI Address Range                  OCTEON Physical Address Range
                                                           ------------------------------------+------------------------------
                                                            BAR0+0x00001000 .. BAR0+0x0FFFFFFF | 0x000001000 .. 0x00FFFFFFF
                                                            BAR0+0x10000000 .. BAR0+0x1FFFFFFF | 0x410000000 .. 0x41FFFFFFF
                                                            BAR0+0x20000000 .. BAR0+0x7FFFFFFF | 0x020000000 .. 0x07FFFFFFF
                                                           and PCI_CTL_STATUS_2[BB_ES] is the endian-swap and
                                                           PCI_CTL_STATUS_2[BB_CA] is the L2 cache allocation bit
                                                           for these references.
                                                           The consequences of any burst that crosses the end of the PCI
                                                           Address Range for BAR0 are unpredicable.
                                                         - The consequences of any burst access that crosses the boundary
                                                           between BAR0+0xFFF and BAR0+0x1000 are unpredictable in PCI-X
                                                           mode. OCTEON may disconnect PCI references at this boundary.
                                                         - The results of any burst read that crosses the boundary
                                                           between BAR0+0x0FFFFFFF and BAR0+0x10000000 are unpredictable.
                                                           The consequences of any burst write that crosses this same
                                                           boundary are unpredictable.
                                                         - The results of any burst read that crosses the boundary
                                                           between BAR0+0x1FFFFFFF and BAR0+0x20000000 are unpredictable.
                                                           The consequences of any burst write that crosses this same
                                                           boundary are unpredictable. */
	uint32_t erst_n                       : 1;  /**< Reset active Low. PASS-2 */
	uint32_t bar2pres                     : 1;  /**< From fuse block. When fuse(MIO_FUS_DAT3[BAR2_EN])
                                                         is NOT blown the value of this field is '0' after
                                                         reset and BAR2 is NOT present. When the fuse IS
                                                         blown the value of this field is '1' after reset
                                                         and BAR2 is present. Note that SW can change this
                                                         field after reset. This is a PASS-2 field. */
	uint32_t scmtyp                       : 1;  /**< Split Completion Message CMD Type (0=RD/1=WR)
                                                         When SCM=1, SCMTYP specifies the CMD intent (R/W) */
	uint32_t scm                          : 1;  /**< Split Completion Message Detected (Read or Write) */
	uint32_t en_wfilt                     : 1;  /**< When '1' the window-access filter is enabled.
                                                         Unfilter writes are:
                                                         MIO, SubId0
                                                         MIO, SubId7
                                                         NPI, SubId0
                                                         NPI, SubId7
                                                         POW, SubId7
                                                         DFA, SubId7
                                                         IPD, SubId7
                                                         Unfiltered Reads are:
                                                         MIO, SubId0
                                                         MIO, SubId7
                                                         NPI, SubId0
                                                         NPI, SubId7
                                                         POW, SubId1
                                                         POW, SubId2
                                                         POW, SubId3
                                                         POW, SubId7
                                                         DFA, SubId7
                                                         IPD, SubId7 */
	uint32_t reserved_14_14               : 1;
	uint32_t ap_pcix                      : 1;  /**< PCX Core Mode status (0=PCI Bus/1=PCIX)
                                                         If one or more of PCI_DEVSEL_N, PCI_STOP_N, and
                                                         PCI_TRDY_N are asserted at the rising edge of
                                                         PCI_RST_N, the device enters PCI-X mode.
                                                         Otherwise, the device enters conventional PCI
                                                         mode at the rising edge of RST#. */
	uint32_t ap_64ad                      : 1;  /**< PCX Core Bus status (0=32b Bus/1=64b Bus)
                                                         When PCI_RST_N pin is de-asserted, the state
                                                         of PCI_REQ64_N(driven by central agent) determines
                                                         the width of the PCI/X bus. */
	uint32_t b12_bist                     : 1;  /**< Bist Status For Memeory In B12 */
	uint32_t pmo_amod                     : 1;  /**< PMO-ARB Mode (0=FP[HP=CMD1,LP=CMD0]/1=RR) */
	uint32_t pmo_fpc                      : 3;  /**< PMO-ARB Fixed Priority Counter
                                                         When PMO_AMOD=0 (FP mode), this field represents
                                                         the \# of CMD1 requests that are issued (at higher
                                                         priority) before a single lower priority CMD0
                                                         is allowed to issue (to ensure foward progress).
                                                           - 0: 1 CMD1 Request issued before CMD0 allowed
                                                           - ...
                                                           - 7: 8 CMD1 Requests issued before CMD0 allowed */
	uint32_t tsr_hwm                      : 3;  /**< Target Split-Read ADB(allowable disconnect boundary)
                                                         High Water Mark.
                                                         Specifies the number of ADBs(128 Byte aligned chunks)
                                                         that are accumulated(pending) BEFORE the Target Split
                                                         completion is attempted on the PCI bus.
                                                            - 0: RESERVED/ILLEGAL
                                                            - 1: 2 Pending ADBs (129B-256B)
                                                            - 2: 3 Pending ADBs (257B-384B)
                                                            - 3: 4 Pending ADBs (385B-512B)
                                                            - 4: 5 Pending ADBs (513B-640B)
                                                            - 5: 6 Pending ADBs (641B-768B)
                                                            - 6: 7 Pending ADBs (769B-896B)
                                                            - 7: 8 Pending ADBs (897B-1024B)
                                                         Example: Suppose a 1KB target memory request with
                                                         starting byte offset address[6:0]=0x7F is split by
                                                         the OCTEON and the TSR_HWM=1(2 ADBs).
                                                         The OCTEON will start the target split completion
                                                         on the PCI Bus after 1B(1st ADB)+128B(2nd ADB)=129B
                                                         of data have been received from memory (even though
                                                         the remaining 895B has not yet been received). The
                                                         OCTEON will continue the split completion until it
                                                         has consumed all of the pended split data. If the
                                                         full transaction length(1KB) of data was NOT entirely
                                                         transferred, then OCTEON will terminate the split
                                                         completion and again wait for another 2 ADB-aligned data
                                                         chunks(256B) of pended split data to be received from
                                                         memory before starting another split completion request.
                                                         This allows Octeon (as split completer), to send back
                                                         multiple split completions for a given large split
                                                         transaction without having to wait for the entire
                                                         transaction length to be received from memory.
                                                         NOTE: For split transaction sizes 'smaller' than the
                                                         specified TSR_HWM value, the split completion
                                                         is started when the last datum has been received from
                                                         memory.
                                                         NOTE: It is IMPERATIVE that this field NEVER BE
                                                         written to a ZERO value. A value of zero is
                                                         reserved/illegal and can result in PCIX bus hangs). */
	uint32_t bar2_enb                     : 1;  /**< When set '1' BAR2 is enable and will respond when
                                                         clear '0' BAR2 access will be target-aborted. */
	uint32_t bar2_esx                     : 2;  /**< Value will be XORed with pci-address[37:36] to
                                                         determine the endian swap mode. */
	uint32_t bar2_cax                     : 1;  /**< Value will be XORed with pci-address[38] to
                                                         determine the L2 cache attribute.
                                                         When XOR result is 1, not cached in L2 */
#else
	uint32_t bar2_cax                     : 1;
	uint32_t bar2_esx                     : 2;
	uint32_t bar2_enb                     : 1;
	uint32_t tsr_hwm                      : 3;
	uint32_t pmo_fpc                      : 3;
	uint32_t pmo_amod                     : 1;
	uint32_t b12_bist                     : 1;
	uint32_t ap_64ad                      : 1;
	uint32_t ap_pcix                      : 1;
	uint32_t reserved_14_14               : 1;
	uint32_t en_wfilt                     : 1;
	uint32_t scm                          : 1;
	uint32_t scmtyp                       : 1;
	uint32_t bar2pres                     : 1;
	uint32_t erst_n                       : 1;
	uint32_t bb0                          : 1;
	uint32_t bb1                          : 1;
	uint32_t bb_es                        : 2;
	uint32_t bb_ca                        : 1;
	uint32_t bb1_siz                      : 1;
	uint32_t bb1_hole                     : 3;
	uint32_t reserved_29_31               : 3;
#endif
	} s;
	struct cvmx_pci_ctl_status_2_s        cn30xx;
	struct cvmx_pci_ctl_status_2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t erst_n                       : 1;  /**< Reset active Low. */
	uint32_t bar2pres                     : 1;  /**< From fuse block. When fuse(MIO_FUS_DAT3[BAR2_EN])
                                                         is NOT blown the value of this field is '0' after
                                                         reset and BAR2 is NOT present. When the fuse IS
                                                         blown the value of this field is '1' after reset
                                                         and BAR2 is present. Note that SW can change this
                                                         field after reset. */
	uint32_t scmtyp                       : 1;  /**< Split Completion Message CMD Type (0=RD/1=WR)
                                                         When SCM=1, SCMTYP specifies the CMD intent (R/W) */
	uint32_t scm                          : 1;  /**< Split Completion Message Detected (Read or Write) */
	uint32_t en_wfilt                     : 1;  /**< When '1' the window-access filter is enabled.
                                                         Unfilter writes are:
                                                         MIO,  SubId0
                                                         MIO,  SubId7
                                                         NPI,  SubId0
                                                         NPI,  SubId7
                                                         POW,  SubId7
                                                         DFA,  SubId7
                                                         IPD,  SubId7
                                                         USBN, SubId7
                                                         Unfiltered Reads are:
                                                         MIO,  SubId0
                                                         MIO,  SubId7
                                                         NPI,  SubId0
                                                         NPI,  SubId7
                                                         POW,  SubId1
                                                         POW,  SubId2
                                                         POW,  SubId3
                                                         POW,  SubId7
                                                         DFA,  SubId7
                                                         IPD,  SubId7
                                                         USBN, SubId7 */
	uint32_t reserved_14_14               : 1;
	uint32_t ap_pcix                      : 1;  /**< PCX Core Mode status (0=PCI Bus/1=PCIX) */
	uint32_t ap_64ad                      : 1;  /**< PCX Core Bus status (0=32b Bus/1=64b Bus) */
	uint32_t b12_bist                     : 1;  /**< Bist Status For Memeory In B12 */
	uint32_t pmo_amod                     : 1;  /**< PMO-ARB Mode (0=FP[HP=CMD1,LP=CMD0]/1=RR) */
	uint32_t pmo_fpc                      : 3;  /**< PMO-ARB Fixed Priority Counter
                                                         When PMO_AMOD=0 (FP mode), this field represents
                                                         the \# of CMD1 requests that are issued (at higher
                                                         priority) before a single lower priority CMD0
                                                         is allowed to issue (to ensure foward progress).
                                                           - 0: 1 CMD1 Request issued before CMD0 allowed
                                                           - ...
                                                           - 7: 8 CMD1 Requests issued before CMD0 allowed */
	uint32_t tsr_hwm                      : 3;  /**< Target Split-Read ADB(allowable disconnect boundary)
                                                         High Water Mark.
                                                         Specifies the number of ADBs(128 Byte aligned chunks)
                                                         that are accumulated(pending) BEFORE the Target Split
                                                         completion is attempted on the PCI bus.
                                                            - 0: RESERVED/ILLEGAL
                                                            - 1: 2 Pending ADBs (129B-256B)
                                                            - 2: 3 Pending ADBs (257B-384B)
                                                            - 3: 4 Pending ADBs (385B-512B)
                                                            - 4: 5 Pending ADBs (513B-640B)
                                                            - 5: 6 Pending ADBs (641B-768B)
                                                            - 6: 7 Pending ADBs (769B-896B)
                                                            - 7: 8 Pending ADBs (897B-1024B)
                                                         Example: Suppose a 1KB target memory request with
                                                         starting byte offset address[6:0]=0x7F is split by
                                                         the OCTEON and the TSR_HWM=1(2 ADBs).
                                                         The OCTEON will start the target split completion
                                                         on the PCI Bus after 1B(1st ADB)+128B(2nd ADB)=129B
                                                         of data have been received from memory (even though
                                                         the remaining 895B has not yet been received). The
                                                         OCTEON will continue the split completion until it
                                                         has consumed all of the pended split data. If the
                                                         full transaction length(1KB) of data was NOT entirely
                                                         transferred, then OCTEON will terminate the split
                                                         completion and again wait for another 2 ADB-aligned data
                                                         chunks(256B) of pended split data to be received from
                                                         memory before starting another split completion request.
                                                         This allows Octeon (as split completer), to send back
                                                         multiple split completions for a given large split
                                                         transaction without having to wait for the entire
                                                         transaction length to be received from memory.
                                                         NOTE: For split transaction sizes 'smaller' than the
                                                         specified TSR_HWM value, the split completion
                                                         is started when the last datum has been received from
                                                         memory.
                                                         NOTE: It is IMPERATIVE that this field NEVER BE
                                                         written to a ZERO value. A value of zero is
                                                         reserved/illegal and can result in PCIX bus hangs). */
	uint32_t bar2_enb                     : 1;  /**< When set '1' BAR2 is enable and will respond when
                                                         clear '0' BAR2 access will be target-aborted. */
	uint32_t bar2_esx                     : 2;  /**< Value will be XORed with pci-address[37:36] to
                                                         determine the endian swap mode. */
	uint32_t bar2_cax                     : 1;  /**< Value will be XORed with pci-address[38] to
                                                         determine the L2 cache attribute.
                                                         When XOR result is 1, not allocated in L2 cache */
#else
	uint32_t bar2_cax                     : 1;
	uint32_t bar2_esx                     : 2;
	uint32_t bar2_enb                     : 1;
	uint32_t tsr_hwm                      : 3;
	uint32_t pmo_fpc                      : 3;
	uint32_t pmo_amod                     : 1;
	uint32_t b12_bist                     : 1;
	uint32_t ap_64ad                      : 1;
	uint32_t ap_pcix                      : 1;
	uint32_t reserved_14_14               : 1;
	uint32_t en_wfilt                     : 1;
	uint32_t scm                          : 1;
	uint32_t scmtyp                       : 1;
	uint32_t bar2pres                     : 1;
	uint32_t erst_n                       : 1;
	uint32_t reserved_20_31               : 12;
#endif
	} cn31xx;
	struct cvmx_pci_ctl_status_2_s        cn38xx;
	struct cvmx_pci_ctl_status_2_cn31xx   cn38xxp2;
	struct cvmx_pci_ctl_status_2_s        cn50xx;
	struct cvmx_pci_ctl_status_2_s        cn58xx;
	struct cvmx_pci_ctl_status_2_s        cn58xxp1;
};
typedef union cvmx_pci_ctl_status_2 cvmx_pci_ctl_status_2_t;

/**
 * cvmx_pci_dbell#
 *
 * PCI_DBELL0 = PCI Doorbell-0
 *
 * The value to write to the doorbell 0 register. The value in this register is acted upon when the
 * least-significant-byte of this register is written.
 */
union cvmx_pci_dbellx {
	uint32_t u32;
	struct cvmx_pci_dbellx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t inc_val                      : 16; /**< Software writes this register with the
                                                         number of new Instructions to be processed
                                                         on the Instruction Queue. When read this
                                                         register contains the last write value. */
#else
	uint32_t inc_val                      : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_pci_dbellx_s              cn30xx;
	struct cvmx_pci_dbellx_s              cn31xx;
	struct cvmx_pci_dbellx_s              cn38xx;
	struct cvmx_pci_dbellx_s              cn38xxp2;
	struct cvmx_pci_dbellx_s              cn50xx;
	struct cvmx_pci_dbellx_s              cn58xx;
	struct cvmx_pci_dbellx_s              cn58xxp1;
};
typedef union cvmx_pci_dbellx cvmx_pci_dbellx_t;

/**
 * cvmx_pci_dma_cnt#
 *
 * PCI_DMA_CNT0 = PCI DMA Count0
 *
 * Keeps track of the number of DMAs or bytes sent by DMAs. The value in this register is acted upon when the
 * least-significant-byte of this register is written.
 */
union cvmx_pci_dma_cntx {
	uint32_t u32;
	struct cvmx_pci_dma_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dma_cnt                      : 32; /**< Update with the number of DMAs completed or the
                                                         number of bytes sent for DMA's associated with
                                                         this counter. When this register is written the
                                                         value written to [15:0] will be subtracted from
                                                         the value in this register. */
#else
	uint32_t dma_cnt                      : 32;
#endif
	} s;
	struct cvmx_pci_dma_cntx_s            cn30xx;
	struct cvmx_pci_dma_cntx_s            cn31xx;
	struct cvmx_pci_dma_cntx_s            cn38xx;
	struct cvmx_pci_dma_cntx_s            cn38xxp2;
	struct cvmx_pci_dma_cntx_s            cn50xx;
	struct cvmx_pci_dma_cntx_s            cn58xx;
	struct cvmx_pci_dma_cntx_s            cn58xxp1;
};
typedef union cvmx_pci_dma_cntx cvmx_pci_dma_cntx_t;

/**
 * cvmx_pci_dma_int_lev#
 *
 * PCI_DMA_INT_LEV0 = PCI DMA Sent Interrupt Level For DMA 0
 *
 * Interrupt when the value in PCI_DMA_CNT0 is equal to or greater than the register value.
 */
union cvmx_pci_dma_int_levx {
	uint32_t u32;
	struct cvmx_pci_dma_int_levx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_cnt                      : 32; /**< When PCI_DMA_CNT0 exceeds the value in this
                                                         DCNT0 will be set in PCI_INT_SUM and PCI_INT_SUM2. */
#else
	uint32_t pkt_cnt                      : 32;
#endif
	} s;
	struct cvmx_pci_dma_int_levx_s        cn30xx;
	struct cvmx_pci_dma_int_levx_s        cn31xx;
	struct cvmx_pci_dma_int_levx_s        cn38xx;
	struct cvmx_pci_dma_int_levx_s        cn38xxp2;
	struct cvmx_pci_dma_int_levx_s        cn50xx;
	struct cvmx_pci_dma_int_levx_s        cn58xx;
	struct cvmx_pci_dma_int_levx_s        cn58xxp1;
};
typedef union cvmx_pci_dma_int_levx cvmx_pci_dma_int_levx_t;

/**
 * cvmx_pci_dma_time#
 *
 * PCI_DMA_TIME0 = PCI DMA Sent Timer For DMA0
 *
 * Time to wait from DMA being sent before issuing an interrupt.
 */
union cvmx_pci_dma_timex {
	uint32_t u32;
	struct cvmx_pci_dma_timex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dma_time                     : 32; /**< Number of PCI clock cycle to wait before
                                                         setting DTIME0 in PCI_INT_SUM and PCI_INT_SUM2.
                                                         After PCI_DMA_CNT0 becomes non-zero.
                                                         The timer is reset when the
                                                         PCI_INT_SUM[27] register is cleared. */
#else
	uint32_t dma_time                     : 32;
#endif
	} s;
	struct cvmx_pci_dma_timex_s           cn30xx;
	struct cvmx_pci_dma_timex_s           cn31xx;
	struct cvmx_pci_dma_timex_s           cn38xx;
	struct cvmx_pci_dma_timex_s           cn38xxp2;
	struct cvmx_pci_dma_timex_s           cn50xx;
	struct cvmx_pci_dma_timex_s           cn58xx;
	struct cvmx_pci_dma_timex_s           cn58xxp1;
};
typedef union cvmx_pci_dma_timex cvmx_pci_dma_timex_t;

/**
 * cvmx_pci_instr_count#
 *
 * PCI_INSTR_COUNT0 = PCI Instructions Outstanding Request Count
 *
 * The number of instructions to be fetched by the Instruction-0 Engine.
 */
union cvmx_pci_instr_countx {
	uint32_t u32;
	struct cvmx_pci_instr_countx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t icnt                         : 32; /**< Number of Instructions to be fetched by the
                                                         Instruction Engine.
                                                         A write of any non zero value to this register
                                                         will clear the value of this register. */
#else
	uint32_t icnt                         : 32;
#endif
	} s;
	struct cvmx_pci_instr_countx_s        cn30xx;
	struct cvmx_pci_instr_countx_s        cn31xx;
	struct cvmx_pci_instr_countx_s        cn38xx;
	struct cvmx_pci_instr_countx_s        cn38xxp2;
	struct cvmx_pci_instr_countx_s        cn50xx;
	struct cvmx_pci_instr_countx_s        cn58xx;
	struct cvmx_pci_instr_countx_s        cn58xxp1;
};
typedef union cvmx_pci_instr_countx cvmx_pci_instr_countx_t;

/**
 * cvmx_pci_int_enb
 *
 * PCI_INT_ENB = PCI Interrupt Enable
 *
 * Enables interrupt bits in the PCI_INT_SUM register.
 */
union cvmx_pci_int_enb {
	uint64_t u64;
	struct cvmx_pci_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[33] */
	uint64_t ill_wr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[32] */
	uint64_t win_wr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[31] */
	uint64_t dma1_fi                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[30] */
	uint64_t dma0_fi                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[29] */
	uint64_t idtime1                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[28] */
	uint64_t idtime0                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[27] */
	uint64_t idcnt1                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[26] */
	uint64_t idcnt0                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[25] */
	uint64_t iptime3                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[24] */
	uint64_t iptime2                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[23] */
	uint64_t iptime1                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[22] */
	uint64_t iptime0                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[21] */
	uint64_t ipcnt3                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[20] */
	uint64_t ipcnt2                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[19] */
	uint64_t ipcnt1                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[18] */
	uint64_t ipcnt0                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[17] */
	uint64_t irsl_int                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[16] */
	uint64_t ill_rrd                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[15] */
	uint64_t ill_rwr                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[14] */
	uint64_t idperr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[13] */
	uint64_t iaperr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[12] */
	uint64_t iserr                        : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[11] */
	uint64_t itsr_abt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[10] */
	uint64_t imsc_msg                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[9] */
	uint64_t imsi_mabt                    : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[8] */
	uint64_t imsi_tabt                    : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[7] */
	uint64_t imsi_per                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[6] */
	uint64_t imr_tto                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[5] */
	uint64_t imr_abt                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[4] */
	uint64_t itr_abt                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[3] */
	uint64_t imr_wtto                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[2] */
	uint64_t imr_wabt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[1] */
	uint64_t itr_wabt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[0] */
#else
	uint64_t itr_wabt                     : 1;
	uint64_t imr_wabt                     : 1;
	uint64_t imr_wtto                     : 1;
	uint64_t itr_abt                      : 1;
	uint64_t imr_abt                      : 1;
	uint64_t imr_tto                      : 1;
	uint64_t imsi_per                     : 1;
	uint64_t imsi_tabt                    : 1;
	uint64_t imsi_mabt                    : 1;
	uint64_t imsc_msg                     : 1;
	uint64_t itsr_abt                     : 1;
	uint64_t iserr                        : 1;
	uint64_t iaperr                       : 1;
	uint64_t idperr                       : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t irsl_int                     : 1;
	uint64_t ipcnt0                       : 1;
	uint64_t ipcnt1                       : 1;
	uint64_t ipcnt2                       : 1;
	uint64_t ipcnt3                       : 1;
	uint64_t iptime0                      : 1;
	uint64_t iptime1                      : 1;
	uint64_t iptime2                      : 1;
	uint64_t iptime3                      : 1;
	uint64_t idcnt0                       : 1;
	uint64_t idcnt1                       : 1;
	uint64_t idtime0                      : 1;
	uint64_t idtime1                      : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_pci_int_enb_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[33] */
	uint64_t ill_wr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[32] */
	uint64_t win_wr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[31] */
	uint64_t dma1_fi                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[30] */
	uint64_t dma0_fi                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[29] */
	uint64_t idtime1                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[28] */
	uint64_t idtime0                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[27] */
	uint64_t idcnt1                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[26] */
	uint64_t idcnt0                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[25] */
	uint64_t reserved_22_24               : 3;
	uint64_t iptime0                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[21] */
	uint64_t reserved_18_20               : 3;
	uint64_t ipcnt0                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[17] */
	uint64_t irsl_int                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[16] */
	uint64_t ill_rrd                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[15] */
	uint64_t ill_rwr                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[14] */
	uint64_t idperr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[13] */
	uint64_t iaperr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[12] */
	uint64_t iserr                        : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[11] */
	uint64_t itsr_abt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[10] */
	uint64_t imsc_msg                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[9] */
	uint64_t imsi_mabt                    : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[8] */
	uint64_t imsi_tabt                    : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[7] */
	uint64_t imsi_per                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[6] */
	uint64_t imr_tto                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[5] */
	uint64_t imr_abt                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[4] */
	uint64_t itr_abt                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[3] */
	uint64_t imr_wtto                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[2] */
	uint64_t imr_wabt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[1] */
	uint64_t itr_wabt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[0] */
#else
	uint64_t itr_wabt                     : 1;
	uint64_t imr_wabt                     : 1;
	uint64_t imr_wtto                     : 1;
	uint64_t itr_abt                      : 1;
	uint64_t imr_abt                      : 1;
	uint64_t imr_tto                      : 1;
	uint64_t imsi_per                     : 1;
	uint64_t imsi_tabt                    : 1;
	uint64_t imsi_mabt                    : 1;
	uint64_t imsc_msg                     : 1;
	uint64_t itsr_abt                     : 1;
	uint64_t iserr                        : 1;
	uint64_t iaperr                       : 1;
	uint64_t idperr                       : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t irsl_int                     : 1;
	uint64_t ipcnt0                       : 1;
	uint64_t reserved_18_20               : 3;
	uint64_t iptime0                      : 1;
	uint64_t reserved_22_24               : 3;
	uint64_t idcnt0                       : 1;
	uint64_t idcnt1                       : 1;
	uint64_t idtime0                      : 1;
	uint64_t idtime1                      : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn30xx;
	struct cvmx_pci_int_enb_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[33] */
	uint64_t ill_wr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[32] */
	uint64_t win_wr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[31] */
	uint64_t dma1_fi                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[30] */
	uint64_t dma0_fi                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[29] */
	uint64_t idtime1                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[28] */
	uint64_t idtime0                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[27] */
	uint64_t idcnt1                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[26] */
	uint64_t idcnt0                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[25] */
	uint64_t reserved_23_24               : 2;
	uint64_t iptime1                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[22] */
	uint64_t iptime0                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[21] */
	uint64_t reserved_19_20               : 2;
	uint64_t ipcnt1                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[18] */
	uint64_t ipcnt0                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[17] */
	uint64_t irsl_int                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[16] */
	uint64_t ill_rrd                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[15] */
	uint64_t ill_rwr                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[14] */
	uint64_t idperr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[13] */
	uint64_t iaperr                       : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[12] */
	uint64_t iserr                        : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[11] */
	uint64_t itsr_abt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[10] */
	uint64_t imsc_msg                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[9] */
	uint64_t imsi_mabt                    : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[8] */
	uint64_t imsi_tabt                    : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[7] */
	uint64_t imsi_per                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[6] */
	uint64_t imr_tto                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[5] */
	uint64_t imr_abt                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[4] */
	uint64_t itr_abt                      : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[3] */
	uint64_t imr_wtto                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[2] */
	uint64_t imr_wabt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[1] */
	uint64_t itr_wabt                     : 1;  /**< INTA# Pin Interrupt Enable for PCI_INT_SUM[0] */
#else
	uint64_t itr_wabt                     : 1;
	uint64_t imr_wabt                     : 1;
	uint64_t imr_wtto                     : 1;
	uint64_t itr_abt                      : 1;
	uint64_t imr_abt                      : 1;
	uint64_t imr_tto                      : 1;
	uint64_t imsi_per                     : 1;
	uint64_t imsi_tabt                    : 1;
	uint64_t imsi_mabt                    : 1;
	uint64_t imsc_msg                     : 1;
	uint64_t itsr_abt                     : 1;
	uint64_t iserr                        : 1;
	uint64_t iaperr                       : 1;
	uint64_t idperr                       : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t irsl_int                     : 1;
	uint64_t ipcnt0                       : 1;
	uint64_t ipcnt1                       : 1;
	uint64_t reserved_19_20               : 2;
	uint64_t iptime0                      : 1;
	uint64_t iptime1                      : 1;
	uint64_t reserved_23_24               : 2;
	uint64_t idcnt0                       : 1;
	uint64_t idcnt1                       : 1;
	uint64_t idtime0                      : 1;
	uint64_t idtime1                      : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn31xx;
	struct cvmx_pci_int_enb_s             cn38xx;
	struct cvmx_pci_int_enb_s             cn38xxp2;
	struct cvmx_pci_int_enb_cn31xx        cn50xx;
	struct cvmx_pci_int_enb_s             cn58xx;
	struct cvmx_pci_int_enb_s             cn58xxp1;
};
typedef union cvmx_pci_int_enb cvmx_pci_int_enb_t;

/**
 * cvmx_pci_int_enb2
 *
 * PCI_INT_ENB2 = PCI Interrupt Enable2 Register
 *
 * Enables interrupt bits in the PCI_INT_SUM2 register.
 */
union cvmx_pci_int_enb2 {
	uint64_t u64;
	struct cvmx_pci_int_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[33] */
	uint64_t ill_wr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[32] */
	uint64_t win_wr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[31] */
	uint64_t dma1_fi                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[30] */
	uint64_t dma0_fi                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[29] */
	uint64_t rdtime1                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[28] */
	uint64_t rdtime0                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[27] */
	uint64_t rdcnt1                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[26] */
	uint64_t rdcnt0                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[25] */
	uint64_t rptime3                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[24] */
	uint64_t rptime2                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[23] */
	uint64_t rptime1                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[22] */
	uint64_t rptime0                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[21] */
	uint64_t rpcnt3                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[20] */
	uint64_t rpcnt2                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[19] */
	uint64_t rpcnt1                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[18] */
	uint64_t rpcnt0                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[17] */
	uint64_t rrsl_int                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[16] */
	uint64_t ill_rrd                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[15] */
	uint64_t ill_rwr                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[14] */
	uint64_t rdperr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[13] */
	uint64_t raperr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[12] */
	uint64_t rserr                        : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[11] */
	uint64_t rtsr_abt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[10] */
	uint64_t rmsc_msg                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[9] */
	uint64_t rmsi_mabt                    : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[8] */
	uint64_t rmsi_tabt                    : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[7] */
	uint64_t rmsi_per                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[6] */
	uint64_t rmr_tto                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[5] */
	uint64_t rmr_abt                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[4] */
	uint64_t rtr_abt                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[3] */
	uint64_t rmr_wtto                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[2] */
	uint64_t rmr_wabt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[1] */
	uint64_t rtr_wabt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[0] */
#else
	uint64_t rtr_wabt                     : 1;
	uint64_t rmr_wabt                     : 1;
	uint64_t rmr_wtto                     : 1;
	uint64_t rtr_abt                      : 1;
	uint64_t rmr_abt                      : 1;
	uint64_t rmr_tto                      : 1;
	uint64_t rmsi_per                     : 1;
	uint64_t rmsi_tabt                    : 1;
	uint64_t rmsi_mabt                    : 1;
	uint64_t rmsc_msg                     : 1;
	uint64_t rtsr_abt                     : 1;
	uint64_t rserr                        : 1;
	uint64_t raperr                       : 1;
	uint64_t rdperr                       : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rrsl_int                     : 1;
	uint64_t rpcnt0                       : 1;
	uint64_t rpcnt1                       : 1;
	uint64_t rpcnt2                       : 1;
	uint64_t rpcnt3                       : 1;
	uint64_t rptime0                      : 1;
	uint64_t rptime1                      : 1;
	uint64_t rptime2                      : 1;
	uint64_t rptime3                      : 1;
	uint64_t rdcnt0                       : 1;
	uint64_t rdcnt1                       : 1;
	uint64_t rdtime0                      : 1;
	uint64_t rdtime1                      : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_pci_int_enb2_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[33] */
	uint64_t ill_wr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[32] */
	uint64_t win_wr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[31] */
	uint64_t dma1_fi                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[30] */
	uint64_t dma0_fi                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[29] */
	uint64_t rdtime1                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[28] */
	uint64_t rdtime0                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[27] */
	uint64_t rdcnt1                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[26] */
	uint64_t rdcnt0                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[25] */
	uint64_t reserved_22_24               : 3;
	uint64_t rptime0                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[21] */
	uint64_t reserved_18_20               : 3;
	uint64_t rpcnt0                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[17] */
	uint64_t rrsl_int                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[16] */
	uint64_t ill_rrd                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[15] */
	uint64_t ill_rwr                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[14] */
	uint64_t rdperr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[13] */
	uint64_t raperr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[12] */
	uint64_t rserr                        : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[11] */
	uint64_t rtsr_abt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[10] */
	uint64_t rmsc_msg                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[9] */
	uint64_t rmsi_mabt                    : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[8] */
	uint64_t rmsi_tabt                    : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[7] */
	uint64_t rmsi_per                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[6] */
	uint64_t rmr_tto                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[5] */
	uint64_t rmr_abt                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[4] */
	uint64_t rtr_abt                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[3] */
	uint64_t rmr_wtto                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[2] */
	uint64_t rmr_wabt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[1] */
	uint64_t rtr_wabt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[0] */
#else
	uint64_t rtr_wabt                     : 1;
	uint64_t rmr_wabt                     : 1;
	uint64_t rmr_wtto                     : 1;
	uint64_t rtr_abt                      : 1;
	uint64_t rmr_abt                      : 1;
	uint64_t rmr_tto                      : 1;
	uint64_t rmsi_per                     : 1;
	uint64_t rmsi_tabt                    : 1;
	uint64_t rmsi_mabt                    : 1;
	uint64_t rmsc_msg                     : 1;
	uint64_t rtsr_abt                     : 1;
	uint64_t rserr                        : 1;
	uint64_t raperr                       : 1;
	uint64_t rdperr                       : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rrsl_int                     : 1;
	uint64_t rpcnt0                       : 1;
	uint64_t reserved_18_20               : 3;
	uint64_t rptime0                      : 1;
	uint64_t reserved_22_24               : 3;
	uint64_t rdcnt0                       : 1;
	uint64_t rdcnt1                       : 1;
	uint64_t rdtime0                      : 1;
	uint64_t rdtime1                      : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn30xx;
	struct cvmx_pci_int_enb2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[33] */
	uint64_t ill_wr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[32] */
	uint64_t win_wr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[31] */
	uint64_t dma1_fi                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[30] */
	uint64_t dma0_fi                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[29] */
	uint64_t rdtime1                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[28] */
	uint64_t rdtime0                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[27] */
	uint64_t rdcnt1                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[26] */
	uint64_t rdcnt0                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[25] */
	uint64_t reserved_23_24               : 2;
	uint64_t rptime1                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[22] */
	uint64_t rptime0                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[21] */
	uint64_t reserved_19_20               : 2;
	uint64_t rpcnt1                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[18] */
	uint64_t rpcnt0                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[17] */
	uint64_t rrsl_int                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[16] */
	uint64_t ill_rrd                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[15] */
	uint64_t ill_rwr                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[14] */
	uint64_t rdperr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[13] */
	uint64_t raperr                       : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[12] */
	uint64_t rserr                        : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[11] */
	uint64_t rtsr_abt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[10] */
	uint64_t rmsc_msg                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[9] */
	uint64_t rmsi_mabt                    : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[8] */
	uint64_t rmsi_tabt                    : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[7] */
	uint64_t rmsi_per                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[6] */
	uint64_t rmr_tto                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[5] */
	uint64_t rmr_abt                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[4] */
	uint64_t rtr_abt                      : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[3] */
	uint64_t rmr_wtto                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[2] */
	uint64_t rmr_wabt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[1] */
	uint64_t rtr_wabt                     : 1;  /**< RSL Chain Interrupt Enable for PCI_INT_SUM2[0] */
#else
	uint64_t rtr_wabt                     : 1;
	uint64_t rmr_wabt                     : 1;
	uint64_t rmr_wtto                     : 1;
	uint64_t rtr_abt                      : 1;
	uint64_t rmr_abt                      : 1;
	uint64_t rmr_tto                      : 1;
	uint64_t rmsi_per                     : 1;
	uint64_t rmsi_tabt                    : 1;
	uint64_t rmsi_mabt                    : 1;
	uint64_t rmsc_msg                     : 1;
	uint64_t rtsr_abt                     : 1;
	uint64_t rserr                        : 1;
	uint64_t raperr                       : 1;
	uint64_t rdperr                       : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rrsl_int                     : 1;
	uint64_t rpcnt0                       : 1;
	uint64_t rpcnt1                       : 1;
	uint64_t reserved_19_20               : 2;
	uint64_t rptime0                      : 1;
	uint64_t rptime1                      : 1;
	uint64_t reserved_23_24               : 2;
	uint64_t rdcnt0                       : 1;
	uint64_t rdcnt1                       : 1;
	uint64_t rdtime0                      : 1;
	uint64_t rdtime1                      : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn31xx;
	struct cvmx_pci_int_enb2_s            cn38xx;
	struct cvmx_pci_int_enb2_s            cn38xxp2;
	struct cvmx_pci_int_enb2_cn31xx       cn50xx;
	struct cvmx_pci_int_enb2_s            cn58xx;
	struct cvmx_pci_int_enb2_s            cn58xxp1;
};
typedef union cvmx_pci_int_enb2 cvmx_pci_int_enb2_t;

/**
 * cvmx_pci_int_sum
 *
 * PCI_INT_SUM = PCI Interrupt Summary
 *
 * The PCI Interrupt Summary Register.
 */
union cvmx_pci_int_sum {
	uint64_t u64;
	struct cvmx_pci_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< A read to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t ill_wr                       : 1;  /**< A write to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t win_wr                       : 1;  /**< A write to the disabled Window Write Data or
                                                         Read-Address Register took place. */
	uint64_t dma1_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 1. */
	uint64_t dma0_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 0. */
	uint64_t dtime1                       : 1;  /**< When the value in the PCI_DMA_CNT1
                                                         register is not 0 the DMA_CNT1 timer counts.
                                                         When the DMA1_CNT timer has a value greater
                                                         than the PCI_DMA_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dtime0                       : 1;  /**< When the value in the PCI_DMA_CNT0
                                                         register is not 0 the DMA_CNT0 timer counts.
                                                         When the DMA0_CNT timer has a value greater
                                                         than the PCI_DMA_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that PCI_DMA_CNT1
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV1 register. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that PCI_DMA_CNT0
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV0 register. */
	uint64_t ptime3                       : 1;  /**< When the value in the PCI_PKTS_SENT3
                                                         register is not 0 the Sent-3 timer counts.
                                                         When the Sent-3 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME3 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime2                       : 1;  /**< When the value in the PCI_PKTS_SENT2
                                                         register is not 0 the Sent-2 timer counts.
                                                         When the Sent-2 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME2 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime1                       : 1;  /**< When the value in the PCI_PKTS_SENT1
                                                         register is not 0 the Sent-1 timer counts.
                                                         When the Sent-1 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime0                       : 1;  /**< When the value in the PCI_PKTS_SENT0
                                                         register is not 0 the Sent-0 timer counts.
                                                         When the Sent-0 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t pcnt3                        : 1;  /**< This bit indicates that PCI_PKTS_SENT3
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV3 register. */
	uint64_t pcnt2                        : 1;  /**< This bit indicates that PCI_PKTS_SENT2
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV2 register. */
	uint64_t pcnt1                        : 1;  /**< This bit indicates that PCI_PKTS_SENT1
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV1 register. */
	uint64_t pcnt0                        : 1;  /**< This bit indicates that PCI_PKTS_SENT0
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV0 register. */
	uint64_t rsl_int                      : 1;  /**< This bit is set when the mio_pci_inta_dr wire
                                                         is asserted by the MIO. */
	uint64_t ill_rrd                      : 1;  /**< A read  to the disabled PCI registers took place. */
	uint64_t ill_rwr                      : 1;  /**< A write to the disabled PCI registers took place. */
	uint64_t dperr                        : 1;  /**< Data Parity Error detected by PCX Core */
	uint64_t aperr                        : 1;  /**< Address Parity Error detected by PCX Core */
	uint64_t serr                         : 1;  /**< SERR# detected by PCX Core */
	uint64_t tsr_abt                      : 1;  /**< Target Split-Read Abort Detected
                                                         CN58XX (as completer), has encountered an error
                                                         which prevents the split transaction from
                                                         completing. In this event, the CN58XX (as completer),
                                                         sends a SCM (Split Completion Message) to the
                                                         initiator. See: PCIX Spec v1.0a Fig 2-40.
                                                            [31:28]: Message Class = 2(completer error)
                                                            [27:20]: Message Index = 0x80
                                                            [18:12]: Remaining Lower Address
                                                            [11:0]: Remaining Byte Count */
	uint64_t msc_msg                      : 1;  /**< Master Split Completion Message (SCM) Detected
                                                         for either a Split-Read/Write error case.
                                                         Set if:
                                                            a) A Split-Write SCM is detected with SCE=1.
                                                            b) A Split-Read SCM is detected (regardless
                                                               of SCE status).
                                                         The Split completion message(SCM)
                                                         is also latched into the PCI_SCM_REG[SCM] to
                                                         assist SW with error recovery. */
	uint64_t msi_mabt                     : 1;  /**< PCI Master Abort on Master MSI */
	uint64_t msi_tabt                     : 1;  /**< PCI Target-Abort on Master MSI */
	uint64_t msi_per                      : 1;  /**< PCI Parity Error on Master MSI */
	uint64_t mr_tto                       : 1;  /**< PCI Master Retry Timeout On Master-Read */
	uint64_t mr_abt                       : 1;  /**< PCI Master Abort On Master-Read */
	uint64_t tr_abt                       : 1;  /**< PCI Target Abort On Master-Read */
	uint64_t mr_wtto                      : 1;  /**< PCI Master Retry Timeout on Master-write */
	uint64_t mr_wabt                      : 1;  /**< PCI Master Abort detected on Master-write */
	uint64_t tr_wabt                      : 1;  /**< PCI Target Abort detected on Master-write */
#else
	uint64_t tr_wabt                      : 1;
	uint64_t mr_wabt                      : 1;
	uint64_t mr_wtto                      : 1;
	uint64_t tr_abt                       : 1;
	uint64_t mr_abt                       : 1;
	uint64_t mr_tto                       : 1;
	uint64_t msi_per                      : 1;
	uint64_t msi_tabt                     : 1;
	uint64_t msi_mabt                     : 1;
	uint64_t msc_msg                      : 1;
	uint64_t tsr_abt                      : 1;
	uint64_t serr                         : 1;
	uint64_t aperr                        : 1;
	uint64_t dperr                        : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rsl_int                      : 1;
	uint64_t pcnt0                        : 1;
	uint64_t pcnt1                        : 1;
	uint64_t pcnt2                        : 1;
	uint64_t pcnt3                        : 1;
	uint64_t ptime0                       : 1;
	uint64_t ptime1                       : 1;
	uint64_t ptime2                       : 1;
	uint64_t ptime3                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_pci_int_sum_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< A read to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t ill_wr                       : 1;  /**< A write to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t win_wr                       : 1;  /**< A write to the disabled Window Write Data or
                                                         Read-Address Register took place. */
	uint64_t dma1_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 1. */
	uint64_t dma0_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 0. */
	uint64_t dtime1                       : 1;  /**< When the value in the PCI_DMA_CNT1
                                                         register is not 0 the DMA_CNT1 timer counts.
                                                         When the DMA1_CNT timer has a value greater
                                                         than the PCI_DMA_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dtime0                       : 1;  /**< When the value in the PCI_DMA_CNT0
                                                         register is not 0 the DMA_CNT0 timer counts.
                                                         When the DMA0_CNT timer has a value greater
                                                         than the PCI_DMA_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that PCI_DMA_CNT1
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV1 register. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that PCI_DMA_CNT0
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV0 register. */
	uint64_t reserved_22_24               : 3;
	uint64_t ptime0                       : 1;  /**< When the value in the PCI_PKTS_SENT0
                                                         register is not 0 the Sent-0 timer counts.
                                                         When the Sent-0 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t reserved_18_20               : 3;
	uint64_t pcnt0                        : 1;  /**< This bit indicates that PCI_PKTS_SENT0
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV0 register. */
	uint64_t rsl_int                      : 1;  /**< This bit is set when the mio_pci_inta_dr wire
                                                         is asserted by the MIO */
	uint64_t ill_rrd                      : 1;  /**< A read  to the disabled PCI registers took place. */
	uint64_t ill_rwr                      : 1;  /**< A write to the disabled PCI registers took place. */
	uint64_t dperr                        : 1;  /**< Data Parity Error detected by PCX Core */
	uint64_t aperr                        : 1;  /**< Address Parity Error detected by PCX Core */
	uint64_t serr                         : 1;  /**< SERR# detected by PCX Core */
	uint64_t tsr_abt                      : 1;  /**< Target Split-Read Abort Detected
                                                         N3K (as completer), has encountered an error
                                                         which prevents the split transaction from
                                                         completing. In this event, the N3K (as completer),
                                                         sends a SCM (Split Completion Message) to the
                                                         initiator. See: PCIX Spec v1.0a Fig 2-40.
                                                            [31:28]: Message Class = 2(completer error)
                                                            [27:20]: Message Index = 0x80
                                                            [18:12]: Remaining Lower Address
                                                            [11:0]: Remaining Byte Count */
	uint64_t msc_msg                      : 1;  /**< Master Split Completion Message (SCM) Detected
                                                         for either a Split-Read/Write error case.
                                                         Set if:
                                                            a) A Split-Write SCM is detected with SCE=1.
                                                            b) A Split-Read SCM is detected (regardless
                                                               of SCE status).
                                                         The Split completion message(SCM)
                                                         is also latched into the PCI_SCM_REG[SCM] to
                                                         assist SW with error recovery. */
	uint64_t msi_mabt                     : 1;  /**< PCI Master Abort on Master MSI */
	uint64_t msi_tabt                     : 1;  /**< PCI Target-Abort on Master MSI */
	uint64_t msi_per                      : 1;  /**< PCI Parity Error on Master MSI */
	uint64_t mr_tto                       : 1;  /**< PCI Master Retry Timeout On Master-Read */
	uint64_t mr_abt                       : 1;  /**< PCI Master Abort On Master-Read */
	uint64_t tr_abt                       : 1;  /**< PCI Target Abort On Master-Read */
	uint64_t mr_wtto                      : 1;  /**< PCI Master Retry Timeout on Master-write */
	uint64_t mr_wabt                      : 1;  /**< PCI Master Abort detected on Master-write */
	uint64_t tr_wabt                      : 1;  /**< PCI Target Abort detected on Master-write */
#else
	uint64_t tr_wabt                      : 1;
	uint64_t mr_wabt                      : 1;
	uint64_t mr_wtto                      : 1;
	uint64_t tr_abt                       : 1;
	uint64_t mr_abt                       : 1;
	uint64_t mr_tto                       : 1;
	uint64_t msi_per                      : 1;
	uint64_t msi_tabt                     : 1;
	uint64_t msi_mabt                     : 1;
	uint64_t msc_msg                      : 1;
	uint64_t tsr_abt                      : 1;
	uint64_t serr                         : 1;
	uint64_t aperr                        : 1;
	uint64_t dperr                        : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rsl_int                      : 1;
	uint64_t pcnt0                        : 1;
	uint64_t reserved_18_20               : 3;
	uint64_t ptime0                       : 1;
	uint64_t reserved_22_24               : 3;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn30xx;
	struct cvmx_pci_int_sum_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< A read to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t ill_wr                       : 1;  /**< A write to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t win_wr                       : 1;  /**< A write to the disabled Window Write Data or
                                                         Read-Address Register took place. */
	uint64_t dma1_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 1. */
	uint64_t dma0_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 0. */
	uint64_t dtime1                       : 1;  /**< When the value in the PCI_DMA_CNT1
                                                         register is not 0 the DMA_CNT1 timer counts.
                                                         When the DMA1_CNT timer has a value greater
                                                         than the PCI_DMA_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dtime0                       : 1;  /**< When the value in the PCI_DMA_CNT0
                                                         register is not 0 the DMA_CNT0 timer counts.
                                                         When the DMA0_CNT timer has a value greater
                                                         than the PCI_DMA_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that PCI_DMA_CNT1
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV1 register. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that PCI_DMA_CNT0
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV0 register. */
	uint64_t reserved_23_24               : 2;
	uint64_t ptime1                       : 1;  /**< When the value in the PCI_PKTS_SENT1
                                                         register is not 0 the Sent-1 timer counts.
                                                         When the Sent-1 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime0                       : 1;  /**< When the value in the PCI_PKTS_SENT0
                                                         register is not 0 the Sent-0 timer counts.
                                                         When the Sent-0 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t reserved_19_20               : 2;
	uint64_t pcnt1                        : 1;  /**< This bit indicates that PCI_PKTS_SENT1
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV1 register. */
	uint64_t pcnt0                        : 1;  /**< This bit indicates that PCI_PKTS_SENT0
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV0 register. */
	uint64_t rsl_int                      : 1;  /**< This bit is set when the mio_pci_inta_dr wire
                                                         is asserted by the MIO */
	uint64_t ill_rrd                      : 1;  /**< A read  to the disabled PCI registers took place. */
	uint64_t ill_rwr                      : 1;  /**< A write to the disabled PCI registers took place. */
	uint64_t dperr                        : 1;  /**< Data Parity Error detected by PCX Core */
	uint64_t aperr                        : 1;  /**< Address Parity Error detected by PCX Core */
	uint64_t serr                         : 1;  /**< SERR# detected by PCX Core */
	uint64_t tsr_abt                      : 1;  /**< Target Split-Read Abort Detected
                                                         N3K (as completer), has encountered an error
                                                         which prevents the split transaction from
                                                         completing. In this event, the N3K (as completer),
                                                         sends a SCM (Split Completion Message) to the
                                                         initiator. See: PCIX Spec v1.0a Fig 2-40.
                                                            [31:28]: Message Class = 2(completer error)
                                                            [27:20]: Message Index = 0x80
                                                            [18:12]: Remaining Lower Address
                                                            [11:0]: Remaining Byte Count */
	uint64_t msc_msg                      : 1;  /**< Master Split Completion Message (SCM) Detected
                                                         for either a Split-Read/Write error case.
                                                         Set if:
                                                            a) A Split-Write SCM is detected with SCE=1.
                                                            b) A Split-Read SCM is detected (regardless
                                                               of SCE status).
                                                         The Split completion message(SCM)
                                                         is also latched into the PCI_SCM_REG[SCM] to
                                                         assist SW with error recovery. */
	uint64_t msi_mabt                     : 1;  /**< PCI Master Abort on Master MSI */
	uint64_t msi_tabt                     : 1;  /**< PCI Target-Abort on Master MSI */
	uint64_t msi_per                      : 1;  /**< PCI Parity Error on Master MSI */
	uint64_t mr_tto                       : 1;  /**< PCI Master Retry Timeout On Master-Read */
	uint64_t mr_abt                       : 1;  /**< PCI Master Abort On Master-Read */
	uint64_t tr_abt                       : 1;  /**< PCI Target Abort On Master-Read */
	uint64_t mr_wtto                      : 1;  /**< PCI Master Retry Timeout on Master-write */
	uint64_t mr_wabt                      : 1;  /**< PCI Master Abort detected on Master-write */
	uint64_t tr_wabt                      : 1;  /**< PCI Target Abort detected on Master-write */
#else
	uint64_t tr_wabt                      : 1;
	uint64_t mr_wabt                      : 1;
	uint64_t mr_wtto                      : 1;
	uint64_t tr_abt                       : 1;
	uint64_t mr_abt                       : 1;
	uint64_t mr_tto                       : 1;
	uint64_t msi_per                      : 1;
	uint64_t msi_tabt                     : 1;
	uint64_t msi_mabt                     : 1;
	uint64_t msc_msg                      : 1;
	uint64_t tsr_abt                      : 1;
	uint64_t serr                         : 1;
	uint64_t aperr                        : 1;
	uint64_t dperr                        : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rsl_int                      : 1;
	uint64_t pcnt0                        : 1;
	uint64_t pcnt1                        : 1;
	uint64_t reserved_19_20               : 2;
	uint64_t ptime0                       : 1;
	uint64_t ptime1                       : 1;
	uint64_t reserved_23_24               : 2;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn31xx;
	struct cvmx_pci_int_sum_s             cn38xx;
	struct cvmx_pci_int_sum_s             cn38xxp2;
	struct cvmx_pci_int_sum_cn31xx        cn50xx;
	struct cvmx_pci_int_sum_s             cn58xx;
	struct cvmx_pci_int_sum_s             cn58xxp1;
};
typedef union cvmx_pci_int_sum cvmx_pci_int_sum_t;

/**
 * cvmx_pci_int_sum2
 *
 * PCI_INT_SUM2 = PCI Interrupt Summary2 Register
 *
 * The PCI Interrupt Summary2 Register copy used for RSL interrupts.
 */
union cvmx_pci_int_sum2 {
	uint64_t u64;
	struct cvmx_pci_int_sum2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< A read to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t ill_wr                       : 1;  /**< A write to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t win_wr                       : 1;  /**< A write to the disabled Window Write Data or
                                                         Read-Address Register took place. */
	uint64_t dma1_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 1. */
	uint64_t dma0_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 0. */
	uint64_t dtime1                       : 1;  /**< When the value in the PCI_DMA_CNT1
                                                         register is not 0 the DMA_CNT1 timer counts.
                                                         When the DMA1_CNT timer has a value greater
                                                         than the PCI_DMA_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dtime0                       : 1;  /**< When the value in the PCI_DMA_CNT0
                                                         register is not 0 the DMA_CNT0 timer counts.
                                                         When the DMA0_CNT timer has a value greater
                                                         than the PCI_DMA_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that PCI_DMA_CNT1
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV1 register. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that PCI_DMA_CNT0
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV0 register. */
	uint64_t ptime3                       : 1;  /**< When the value in the PCI_PKTS_SENT3
                                                         register is not 0 the Sent-3 timer counts.
                                                         When the Sent-3 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME3 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime2                       : 1;  /**< When the value in the PCI_PKTS_SENT2
                                                         register is not 0 the Sent-2 timer counts.
                                                         When the Sent-2 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME2 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime1                       : 1;  /**< When the value in the PCI_PKTS_SENT1
                                                         register is not 0 the Sent-1 timer counts.
                                                         When the Sent-1 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime0                       : 1;  /**< When the value in the PCI_PKTS_SENT0
                                                         register is not 0 the Sent-0 timer counts.
                                                         When the Sent-0 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t pcnt3                        : 1;  /**< This bit indicates that PCI_PKTS_SENT3
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV3 register. */
	uint64_t pcnt2                        : 1;  /**< This bit indicates that PCI_PKTS_SENT2
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV2 register. */
	uint64_t pcnt1                        : 1;  /**< This bit indicates that PCI_PKTS_SENT1
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV1 register. */
	uint64_t pcnt0                        : 1;  /**< This bit indicates that PCI_PKTS_SENT0
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV0 register. */
	uint64_t rsl_int                      : 1;  /**< This bit is set when the RSL Chain has
                                                         generated an interrupt. */
	uint64_t ill_rrd                      : 1;  /**< A read  to the disabled PCI registers took place. */
	uint64_t ill_rwr                      : 1;  /**< A write to the disabled PCI registers took place. */
	uint64_t dperr                        : 1;  /**< Data Parity Error detected by PCX Core */
	uint64_t aperr                        : 1;  /**< Address Parity Error detected by PCX Core */
	uint64_t serr                         : 1;  /**< SERR# detected by PCX Core */
	uint64_t tsr_abt                      : 1;  /**< Target Split-Read Abort Detected */
	uint64_t msc_msg                      : 1;  /**< Master Split Completion Message Detected */
	uint64_t msi_mabt                     : 1;  /**< PCI MSI Master Abort. */
	uint64_t msi_tabt                     : 1;  /**< PCI MSI Target Abort. */
	uint64_t msi_per                      : 1;  /**< PCI MSI Parity Error. */
	uint64_t mr_tto                       : 1;  /**< PCI Master Retry Timeout On Read. */
	uint64_t mr_abt                       : 1;  /**< PCI Master Abort On Read. */
	uint64_t tr_abt                       : 1;  /**< PCI Target Abort On Read. */
	uint64_t mr_wtto                      : 1;  /**< PCI Master Retry Timeout on write. */
	uint64_t mr_wabt                      : 1;  /**< PCI Master Abort detected on write. */
	uint64_t tr_wabt                      : 1;  /**< PCI Target Abort detected on write. */
#else
	uint64_t tr_wabt                      : 1;
	uint64_t mr_wabt                      : 1;
	uint64_t mr_wtto                      : 1;
	uint64_t tr_abt                       : 1;
	uint64_t mr_abt                       : 1;
	uint64_t mr_tto                       : 1;
	uint64_t msi_per                      : 1;
	uint64_t msi_tabt                     : 1;
	uint64_t msi_mabt                     : 1;
	uint64_t msc_msg                      : 1;
	uint64_t tsr_abt                      : 1;
	uint64_t serr                         : 1;
	uint64_t aperr                        : 1;
	uint64_t dperr                        : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rsl_int                      : 1;
	uint64_t pcnt0                        : 1;
	uint64_t pcnt1                        : 1;
	uint64_t pcnt2                        : 1;
	uint64_t pcnt3                        : 1;
	uint64_t ptime0                       : 1;
	uint64_t ptime1                       : 1;
	uint64_t ptime2                       : 1;
	uint64_t ptime3                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_pci_int_sum2_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< A read to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t ill_wr                       : 1;  /**< A write to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t win_wr                       : 1;  /**< A write to the disabled Window Write Data or
                                                         Read-Address Register took place. */
	uint64_t dma1_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 1. */
	uint64_t dma0_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 0. */
	uint64_t dtime1                       : 1;  /**< When the value in the PCI_DMA_CNT1
                                                         register is not 0 the DMA_CNT1 timer counts.
                                                         When the DMA1_CNT timer has a value greater
                                                         than the PCI_DMA_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dtime0                       : 1;  /**< When the value in the PCI_DMA_CNT0
                                                         register is not 0 the DMA_CNT0 timer counts.
                                                         When the DMA0_CNT timer has a value greater
                                                         than the PCI_DMA_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that PCI_DMA_CNT1
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV1 register. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that PCI_DMA_CNT0
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV0 register. */
	uint64_t reserved_22_24               : 3;
	uint64_t ptime0                       : 1;  /**< When the value in the PCI_PKTS_SENT0
                                                         register is not 0 the Sent-0 timer counts.
                                                         When the Sent-0 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t reserved_18_20               : 3;
	uint64_t pcnt0                        : 1;  /**< This bit indicates that PCI_PKTS_SENT0
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV0 register. */
	uint64_t rsl_int                      : 1;  /**< This bit is set when the RSL Chain has
                                                         generated an interrupt. */
	uint64_t ill_rrd                      : 1;  /**< A read  to the disabled PCI registers took place. */
	uint64_t ill_rwr                      : 1;  /**< A write to the disabled PCI registers took place. */
	uint64_t dperr                        : 1;  /**< Data Parity Error detected by PCX Core */
	uint64_t aperr                        : 1;  /**< Address Parity Error detected by PCX Core */
	uint64_t serr                         : 1;  /**< SERR# detected by PCX Core */
	uint64_t tsr_abt                      : 1;  /**< Target Split-Read Abort Detected */
	uint64_t msc_msg                      : 1;  /**< Master Split Completion Message Detected */
	uint64_t msi_mabt                     : 1;  /**< PCI MSI Master Abort. */
	uint64_t msi_tabt                     : 1;  /**< PCI MSI Target Abort. */
	uint64_t msi_per                      : 1;  /**< PCI MSI Parity Error. */
	uint64_t mr_tto                       : 1;  /**< PCI Master Retry Timeout On Read. */
	uint64_t mr_abt                       : 1;  /**< PCI Master Abort On Read. */
	uint64_t tr_abt                       : 1;  /**< PCI Target Abort On Read. */
	uint64_t mr_wtto                      : 1;  /**< PCI Master Retry Timeout on write. */
	uint64_t mr_wabt                      : 1;  /**< PCI Master Abort detected on write. */
	uint64_t tr_wabt                      : 1;  /**< PCI Target Abort detected on write. */
#else
	uint64_t tr_wabt                      : 1;
	uint64_t mr_wabt                      : 1;
	uint64_t mr_wtto                      : 1;
	uint64_t tr_abt                       : 1;
	uint64_t mr_abt                       : 1;
	uint64_t mr_tto                       : 1;
	uint64_t msi_per                      : 1;
	uint64_t msi_tabt                     : 1;
	uint64_t msi_mabt                     : 1;
	uint64_t msc_msg                      : 1;
	uint64_t tsr_abt                      : 1;
	uint64_t serr                         : 1;
	uint64_t aperr                        : 1;
	uint64_t dperr                        : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rsl_int                      : 1;
	uint64_t pcnt0                        : 1;
	uint64_t reserved_18_20               : 3;
	uint64_t ptime0                       : 1;
	uint64_t reserved_22_24               : 3;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn30xx;
	struct cvmx_pci_int_sum2_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ill_rd                       : 1;  /**< A read to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t ill_wr                       : 1;  /**< A write to a disabled area of bar1 or bar2,
                                                         when the mem area is disabled. */
	uint64_t win_wr                       : 1;  /**< A write to the disabled Window Write Data or
                                                         Read-Address Register took place. */
	uint64_t dma1_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 1. */
	uint64_t dma0_fi                      : 1;  /**< A DMA operation operation finished that was
                                                         required to set the FORCE-INT bit for counter 0. */
	uint64_t dtime1                       : 1;  /**< When the value in the PCI_DMA_CNT1
                                                         register is not 0 the DMA_CNT1 timer counts.
                                                         When the DMA1_CNT timer has a value greater
                                                         than the PCI_DMA_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dtime0                       : 1;  /**< When the value in the PCI_DMA_CNT0
                                                         register is not 0 the DMA_CNT0 timer counts.
                                                         When the DMA0_CNT timer has a value greater
                                                         than the PCI_DMA_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that PCI_DMA_CNT1
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV1 register. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that PCI_DMA_CNT0
                                                         value is greater than the value
                                                         in the PCI_DMA_INT_LEV0 register. */
	uint64_t reserved_23_24               : 2;
	uint64_t ptime1                       : 1;  /**< When the value in the PCI_PKTS_SENT1
                                                         register is not 0 the Sent-1 timer counts.
                                                         When the Sent-1 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME1 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t ptime0                       : 1;  /**< When the value in the PCI_PKTS_SENT0
                                                         register is not 0 the Sent-0 timer counts.
                                                         When the Sent-0 timer has a value greater
                                                         than the PCI_PKTS_SENT_TIME0 register this
                                                         bit is set. The timer is reset when bit is
                                                         written with a one. */
	uint64_t reserved_19_20               : 2;
	uint64_t pcnt1                        : 1;  /**< This bit indicates that PCI_PKTS_SENT1
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV1 register. */
	uint64_t pcnt0                        : 1;  /**< This bit indicates that PCI_PKTS_SENT0
                                                         value is greater than the value
                                                         in the PCI_PKTS_SENT_INT_LEV0 register. */
	uint64_t rsl_int                      : 1;  /**< This bit is set when the RSL Chain has
                                                         generated an interrupt. */
	uint64_t ill_rrd                      : 1;  /**< A read  to the disabled PCI registers took place. */
	uint64_t ill_rwr                      : 1;  /**< A write to the disabled PCI registers took place. */
	uint64_t dperr                        : 1;  /**< Data Parity Error detected by PCX Core */
	uint64_t aperr                        : 1;  /**< Address Parity Error detected by PCX Core */
	uint64_t serr                         : 1;  /**< SERR# detected by PCX Core */
	uint64_t tsr_abt                      : 1;  /**< Target Split-Read Abort Detected */
	uint64_t msc_msg                      : 1;  /**< Master Split Completion Message Detected */
	uint64_t msi_mabt                     : 1;  /**< PCI MSI Master Abort. */
	uint64_t msi_tabt                     : 1;  /**< PCI MSI Target Abort. */
	uint64_t msi_per                      : 1;  /**< PCI MSI Parity Error. */
	uint64_t mr_tto                       : 1;  /**< PCI Master Retry Timeout On Read. */
	uint64_t mr_abt                       : 1;  /**< PCI Master Abort On Read. */
	uint64_t tr_abt                       : 1;  /**< PCI Target Abort On Read. */
	uint64_t mr_wtto                      : 1;  /**< PCI Master Retry Timeout on write. */
	uint64_t mr_wabt                      : 1;  /**< PCI Master Abort detected on write. */
	uint64_t tr_wabt                      : 1;  /**< PCI Target Abort detected on write. */
#else
	uint64_t tr_wabt                      : 1;
	uint64_t mr_wabt                      : 1;
	uint64_t mr_wtto                      : 1;
	uint64_t tr_abt                       : 1;
	uint64_t mr_abt                       : 1;
	uint64_t mr_tto                       : 1;
	uint64_t msi_per                      : 1;
	uint64_t msi_tabt                     : 1;
	uint64_t msi_mabt                     : 1;
	uint64_t msc_msg                      : 1;
	uint64_t tsr_abt                      : 1;
	uint64_t serr                         : 1;
	uint64_t aperr                        : 1;
	uint64_t dperr                        : 1;
	uint64_t ill_rwr                      : 1;
	uint64_t ill_rrd                      : 1;
	uint64_t rsl_int                      : 1;
	uint64_t pcnt0                        : 1;
	uint64_t pcnt1                        : 1;
	uint64_t reserved_19_20               : 2;
	uint64_t ptime0                       : 1;
	uint64_t ptime1                       : 1;
	uint64_t reserved_23_24               : 2;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t dma0_fi                      : 1;
	uint64_t dma1_fi                      : 1;
	uint64_t win_wr                       : 1;
	uint64_t ill_wr                       : 1;
	uint64_t ill_rd                       : 1;
	uint64_t reserved_34_63               : 30;
#endif
	} cn31xx;
	struct cvmx_pci_int_sum2_s            cn38xx;
	struct cvmx_pci_int_sum2_s            cn38xxp2;
	struct cvmx_pci_int_sum2_cn31xx       cn50xx;
	struct cvmx_pci_int_sum2_s            cn58xx;
	struct cvmx_pci_int_sum2_s            cn58xxp1;
};
typedef union cvmx_pci_int_sum2 cvmx_pci_int_sum2_t;

/**
 * cvmx_pci_msi_rcv
 *
 * PCI_MSI_RCV = PCI's MSI Received Vector Register
 *
 * A bit is set in this register relative to the vector received during a MSI. The value in this
 * register is acted upon when the least-significant-byte of this register is written.
 */
union cvmx_pci_msi_rcv {
	uint32_t u32;
	struct cvmx_pci_msi_rcv_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t intr                         : 6;  /**< When an MSI is received on the PCI the bit selected
                                                         by data [5:0] will be set in this register. To
                                                         clear this bit a write must take place to the
                                                         NPI_MSI_RCV register where any bit set to 1 is
                                                         cleared. Reading this address will return an
                                                         unpredicatable value. */
#else
	uint32_t intr                         : 6;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_pci_msi_rcv_s             cn30xx;
	struct cvmx_pci_msi_rcv_s             cn31xx;
	struct cvmx_pci_msi_rcv_s             cn38xx;
	struct cvmx_pci_msi_rcv_s             cn38xxp2;
	struct cvmx_pci_msi_rcv_s             cn50xx;
	struct cvmx_pci_msi_rcv_s             cn58xx;
	struct cvmx_pci_msi_rcv_s             cn58xxp1;
};
typedef union cvmx_pci_msi_rcv cvmx_pci_msi_rcv_t;

/**
 * cvmx_pci_pkt_credits#
 *
 * PCI_PKT_CREDITS0 = PCI Packet Credits For Output 0
 *
 * Used to decrease the number of packets to be processed by the host from Output-0 and return
 * buffer/info pointer pairs to OCTEON Output-0. The value in this register is acted upon when the
 * least-significant-byte of this register is written.
 */
union cvmx_pci_pkt_creditsx {
	uint32_t u32;
	struct cvmx_pci_pkt_creditsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_cnt                      : 16; /**< The value written to this field will be
                                                         subtracted from PCI_PKTS_SENT0[PKT_CNT]. */
	uint32_t ptr_cnt                      : 16; /**< This field value is added to the
                                                         NPI's internal Buffer/Info Pointer Pair count. */
#else
	uint32_t ptr_cnt                      : 16;
	uint32_t pkt_cnt                      : 16;
#endif
	} s;
	struct cvmx_pci_pkt_creditsx_s        cn30xx;
	struct cvmx_pci_pkt_creditsx_s        cn31xx;
	struct cvmx_pci_pkt_creditsx_s        cn38xx;
	struct cvmx_pci_pkt_creditsx_s        cn38xxp2;
	struct cvmx_pci_pkt_creditsx_s        cn50xx;
	struct cvmx_pci_pkt_creditsx_s        cn58xx;
	struct cvmx_pci_pkt_creditsx_s        cn58xxp1;
};
typedef union cvmx_pci_pkt_creditsx cvmx_pci_pkt_creditsx_t;

/**
 * cvmx_pci_pkts_sent#
 *
 * PCI_PKTS_SENT0 = PCI Packets Sent 0
 *
 * Number of packets sent to the host memory from PCI Output 0
 */
union cvmx_pci_pkts_sentx {
	uint32_t u32;
	struct cvmx_pci_pkts_sentx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_cnt                      : 32; /**< Each time a packet is written to the memory via
                                                         PCI from PCI Output 0,  this counter is
                                                         incremented by 1 or the byte count of the packet
                                                         as set in NPI_OUTPUT_CONTROL[P0_BMODE]. */
#else
	uint32_t pkt_cnt                      : 32;
#endif
	} s;
	struct cvmx_pci_pkts_sentx_s          cn30xx;
	struct cvmx_pci_pkts_sentx_s          cn31xx;
	struct cvmx_pci_pkts_sentx_s          cn38xx;
	struct cvmx_pci_pkts_sentx_s          cn38xxp2;
	struct cvmx_pci_pkts_sentx_s          cn50xx;
	struct cvmx_pci_pkts_sentx_s          cn58xx;
	struct cvmx_pci_pkts_sentx_s          cn58xxp1;
};
typedef union cvmx_pci_pkts_sentx cvmx_pci_pkts_sentx_t;

/**
 * cvmx_pci_pkts_sent_int_lev#
 *
 * PCI_PKTS_SENT_INT_LEV0 = PCI Packets Sent Interrupt Level For Output 0
 *
 * Interrupt when number of packets sent is equal to or greater than the register value.
 */
union cvmx_pci_pkts_sent_int_levx {
	uint32_t u32;
	struct cvmx_pci_pkts_sent_int_levx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_cnt                      : 32; /**< When corresponding port's PCI_PKTS_SENT0 value
                                                         exceeds the value in this register, PCNT0 of the
                                                         PCI_INT_SUM and PCI_INT_SUM2 will be set. */
#else
	uint32_t pkt_cnt                      : 32;
#endif
	} s;
	struct cvmx_pci_pkts_sent_int_levx_s  cn30xx;
	struct cvmx_pci_pkts_sent_int_levx_s  cn31xx;
	struct cvmx_pci_pkts_sent_int_levx_s  cn38xx;
	struct cvmx_pci_pkts_sent_int_levx_s  cn38xxp2;
	struct cvmx_pci_pkts_sent_int_levx_s  cn50xx;
	struct cvmx_pci_pkts_sent_int_levx_s  cn58xx;
	struct cvmx_pci_pkts_sent_int_levx_s  cn58xxp1;
};
typedef union cvmx_pci_pkts_sent_int_levx cvmx_pci_pkts_sent_int_levx_t;

/**
 * cvmx_pci_pkts_sent_time#
 *
 * PCI_PKTS_SENT_TIME0 = PCI Packets Sent Timer For Output-0
 *
 * Time to wait from packet being sent to host from Output-0 before issuing an interrupt.
 */
union cvmx_pci_pkts_sent_timex {
	uint32_t u32;
	struct cvmx_pci_pkts_sent_timex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pkt_time                     : 32; /**< Number of PCI clock cycle to wait before
                                                         issuing an interrupt to the host when a
                                                         packet from this port has been sent to the
                                                         host.  The timer is reset when the
                                                         PCI_INT_SUM[21] register is cleared. */
#else
	uint32_t pkt_time                     : 32;
#endif
	} s;
	struct cvmx_pci_pkts_sent_timex_s     cn30xx;
	struct cvmx_pci_pkts_sent_timex_s     cn31xx;
	struct cvmx_pci_pkts_sent_timex_s     cn38xx;
	struct cvmx_pci_pkts_sent_timex_s     cn38xxp2;
	struct cvmx_pci_pkts_sent_timex_s     cn50xx;
	struct cvmx_pci_pkts_sent_timex_s     cn58xx;
	struct cvmx_pci_pkts_sent_timex_s     cn58xxp1;
};
typedef union cvmx_pci_pkts_sent_timex cvmx_pci_pkts_sent_timex_t;

/**
 * cvmx_pci_read_cmd_6
 *
 * PCI_READ_CMD_6 = PCI Read Command 6 Register
 *
 * Contains control inforamtion related to a received PCI Command 6.
 */
union cvmx_pci_read_cmd_6 {
	uint32_t u32;
	struct cvmx_pci_read_cmd_6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t min_data                     : 6;  /**< The number of words to have buffered in the PNI
                                                         before informing the PCIX-Core that we have
                                                         read data available for the outstanding Delayed
                                                         read. 0 is treated as a 64.
                                                         For reads to the expansion this value is not used. */
	uint32_t prefetch                     : 3;  /**< Control the amount of data to be preteched when
                                                         this type of bhmstREAD command is received.
                                                         0 = 1 32/64 bit word.
                                                         1 = From address to end of 128B block.
                                                         2 = From address to end of 128B block plus 128B.
                                                         3 = From address to end of 128B block plus 256B.
                                                         4 = From address to end of 128B block plus 384B.
                                                         For reads to the expansion this value is not used. */
#else
	uint32_t prefetch                     : 3;
	uint32_t min_data                     : 6;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_pci_read_cmd_6_s          cn30xx;
	struct cvmx_pci_read_cmd_6_s          cn31xx;
	struct cvmx_pci_read_cmd_6_s          cn38xx;
	struct cvmx_pci_read_cmd_6_s          cn38xxp2;
	struct cvmx_pci_read_cmd_6_s          cn50xx;
	struct cvmx_pci_read_cmd_6_s          cn58xx;
	struct cvmx_pci_read_cmd_6_s          cn58xxp1;
};
typedef union cvmx_pci_read_cmd_6 cvmx_pci_read_cmd_6_t;

/**
 * cvmx_pci_read_cmd_c
 *
 * PCI_READ_CMD_C = PCI Read Command C Register
 *
 * Contains control inforamtion related to a received PCI Command C.
 */
union cvmx_pci_read_cmd_c {
	uint32_t u32;
	struct cvmx_pci_read_cmd_c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t min_data                     : 6;  /**< The number of words to have buffered in the PNI
                                                         before informing the PCIX-Core that we have
                                                         read data available for the outstanding Delayed
                                                         read. 0 is treated as a 64.
                                                         For reads to the expansion this value is not used. */
	uint32_t prefetch                     : 3;  /**< Control the amount of data to be preteched when
                                                         this type of READ command is received.
                                                         0 = 1 32/64 bit word.
                                                         1 = From address to end of 128B block.
                                                         2 = From address to end of 128B block plus 128B.
                                                         3 = From address to end of 128B block plus 256B.
                                                         4 = From address to end of 128B block plus 384B.
                                                         For reads to the expansion this value is not used. */
#else
	uint32_t prefetch                     : 3;
	uint32_t min_data                     : 6;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_pci_read_cmd_c_s          cn30xx;
	struct cvmx_pci_read_cmd_c_s          cn31xx;
	struct cvmx_pci_read_cmd_c_s          cn38xx;
	struct cvmx_pci_read_cmd_c_s          cn38xxp2;
	struct cvmx_pci_read_cmd_c_s          cn50xx;
	struct cvmx_pci_read_cmd_c_s          cn58xx;
	struct cvmx_pci_read_cmd_c_s          cn58xxp1;
};
typedef union cvmx_pci_read_cmd_c cvmx_pci_read_cmd_c_t;

/**
 * cvmx_pci_read_cmd_e
 *
 * PCI_READ_CMD_E = PCI Read Command E Register
 *
 * Contains control inforamtion related to a received PCI Command 6.
 */
union cvmx_pci_read_cmd_e {
	uint32_t u32;
	struct cvmx_pci_read_cmd_e_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t min_data                     : 6;  /**< The number of words to have buffered in the PNI
                                                         before informaing the PCIX-Core that we have
                                                         read data available for the outstanding Delayed
                                                         read. 0 is treated as a 64.
                                                         For reads to the expansion this value is not used. */
	uint32_t prefetch                     : 3;  /**< Control the amount of data to be preteched when
                                                         this type of READ command is received.
                                                         0 = 1 32/64 bit word.
                                                         1 = From address to end of 128B block.
                                                         2 = From address to end of 128B block plus 128B.
                                                         3 = From address to end of 128B block plus 256B.
                                                         4 = From address to end of 128B block plus 384B.
                                                         For reads to the expansion this value is not used. */
#else
	uint32_t prefetch                     : 3;
	uint32_t min_data                     : 6;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_pci_read_cmd_e_s          cn30xx;
	struct cvmx_pci_read_cmd_e_s          cn31xx;
	struct cvmx_pci_read_cmd_e_s          cn38xx;
	struct cvmx_pci_read_cmd_e_s          cn38xxp2;
	struct cvmx_pci_read_cmd_e_s          cn50xx;
	struct cvmx_pci_read_cmd_e_s          cn58xx;
	struct cvmx_pci_read_cmd_e_s          cn58xxp1;
};
typedef union cvmx_pci_read_cmd_e cvmx_pci_read_cmd_e_t;

/**
 * cvmx_pci_read_timeout
 *
 * PCI_READ_TIMEOUT = PCI Read Timeour Register
 *
 * The address to start reading Instructions from for Input-3.
 */
union cvmx_pci_read_timeout {
	uint64_t u64;
	struct cvmx_pci_read_timeout_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t enb                          : 1;  /**< Enable the use of the Timeout function. */
	uint64_t cnt                          : 31; /**< The number of eclk cycles to wait after issuing
                                                         a read request to the PNI before setting a
                                                         timeout and not expecting the data to return.
                                                         This is considered a fatal condition by the NPI. */
#else
	uint64_t cnt                          : 31;
	uint64_t enb                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pci_read_timeout_s        cn30xx;
	struct cvmx_pci_read_timeout_s        cn31xx;
	struct cvmx_pci_read_timeout_s        cn38xx;
	struct cvmx_pci_read_timeout_s        cn38xxp2;
	struct cvmx_pci_read_timeout_s        cn50xx;
	struct cvmx_pci_read_timeout_s        cn58xx;
	struct cvmx_pci_read_timeout_s        cn58xxp1;
};
typedef union cvmx_pci_read_timeout cvmx_pci_read_timeout_t;

/**
 * cvmx_pci_scm_reg
 *
 * PCI_SCM_REG = PCI Master Split Completion Message Register
 *
 * This register contains the Master Split Completion Message(SCM) generated when a master split
 * transaction is aborted.
 */
union cvmx_pci_scm_reg {
	uint64_t u64;
	struct cvmx_pci_scm_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t scm                          : 32; /**< Contains the Split Completion Message (SCM)
                                                         driven when a master-split transaction is aborted.
                                                            [31:28]: Message Class
                                                            [27:20]: Message Index
                                                            [19]:    Reserved
                                                            [18:12]: Remaining Lower Address
                                                            [11:8]:  Upper Remaining Byte Count
                                                            [7:0]:   Lower Remaining Byte Count
                                                         Refer to the PCIX1.0a specification, Fig 2-40
                                                         for additional details for the split completion
                                                         message format. */
#else
	uint64_t scm                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pci_scm_reg_s             cn30xx;
	struct cvmx_pci_scm_reg_s             cn31xx;
	struct cvmx_pci_scm_reg_s             cn38xx;
	struct cvmx_pci_scm_reg_s             cn38xxp2;
	struct cvmx_pci_scm_reg_s             cn50xx;
	struct cvmx_pci_scm_reg_s             cn58xx;
	struct cvmx_pci_scm_reg_s             cn58xxp1;
};
typedef union cvmx_pci_scm_reg cvmx_pci_scm_reg_t;

/**
 * cvmx_pci_tsr_reg
 *
 * PCI_TSR_REG = PCI Target Split Attribute Register
 *
 * This register contains the Attribute field Master Split Completion Message(SCM) generated when a master split
 * transaction is aborted.
 */
union cvmx_pci_tsr_reg {
	uint64_t u64;
	struct cvmx_pci_tsr_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t tsr                          : 36; /**< Contains the Target Split Attribute field when a
                                                         target-split transaction is aborted.
                                                           [35:32]: Upper Byte Count
                                                           [31]:    BCM=Byte Count Modified
                                                           [30]:    SCE=Split Completion Error
                                                           [29]:    SCM=Split Completion Message
                                                           [28:24]: RESERVED
                                                           [23:16]: Completer Bus Number
                                                           [15:11]: Completer Device Number
                                                           [10:8]:  Completer Function Number
                                                           [7:0]:   Lower Byte Count
                                                         Refer to the PCIX1.0a specification, Fig 2-39
                                                         for additional details on the completer attribute
                                                         bit assignments. */
#else
	uint64_t tsr                          : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_pci_tsr_reg_s             cn30xx;
	struct cvmx_pci_tsr_reg_s             cn31xx;
	struct cvmx_pci_tsr_reg_s             cn38xx;
	struct cvmx_pci_tsr_reg_s             cn38xxp2;
	struct cvmx_pci_tsr_reg_s             cn50xx;
	struct cvmx_pci_tsr_reg_s             cn58xx;
	struct cvmx_pci_tsr_reg_s             cn58xxp1;
};
typedef union cvmx_pci_tsr_reg cvmx_pci_tsr_reg_t;

/**
 * cvmx_pci_win_rd_addr
 *
 * PCI_WIN_RD_ADDR = PCI Window Read Address Register
 *
 * Writing the least-significant-byte of this register will cause a read operation to take place,
 * UNLESS, a read operation is already taking place. A read is consider to end when the PCI_WIN_RD_DATA
 * register is read.
 */
union cvmx_pci_win_rd_addr {
	uint64_t u64;
	struct cvmx_pci_win_rd_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t iobit                        : 1;  /**< A 1 or 0 can be written here but this will always
                                                         read as '0'. */
	uint64_t reserved_0_47                : 48;
#else
	uint64_t reserved_0_47                : 48;
	uint64_t iobit                        : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} s;
	struct cvmx_pci_win_rd_addr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t iobit                        : 1;  /**< A 1 or 0 can be written here but this will always
                                                         read as '0'. */
	uint64_t rd_addr                      : 46; /**< The address to be read from. Whenever the LSB of
                                                         this register is written, the Read Operation will
                                                         take place.
                                                         [47:40] = NCB_ID
                                                         [39:3]  = Address
                                                         When [47:43] == NPI & [42:0] == 0 bits [39:0] are:
                                                              [39:32] == x, Not Used
                                                              [31:27] == RSL_ID
                                                              [12:2]  == RSL Register Offset
                                                              [1:0]   == x, Not Used */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t rd_addr                      : 46;
	uint64_t iobit                        : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn30xx;
	struct cvmx_pci_win_rd_addr_cn30xx    cn31xx;
	struct cvmx_pci_win_rd_addr_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t iobit                        : 1;  /**< A 1 or 0 can be written here but this will always
                                                         read as '0'. */
	uint64_t rd_addr                      : 45; /**< The address to be read from. Whenever the LSB of
                                                         this register is written, the Read Operation will
                                                         take place.
                                                         [47:40] = NCB_ID
                                                         [39:3]  = Address
                                                         When [47:43] == NPI & [42:0] == 0 bits [39:0] are:
                                                              [39:32] == x, Not Used
                                                              [31:27] == RSL_ID
                                                              [12:3]  == RSL Register Offset
                                                              [2:0]   == x, Not Used */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t rd_addr                      : 45;
	uint64_t iobit                        : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn38xx;
	struct cvmx_pci_win_rd_addr_cn38xx    cn38xxp2;
	struct cvmx_pci_win_rd_addr_cn30xx    cn50xx;
	struct cvmx_pci_win_rd_addr_cn38xx    cn58xx;
	struct cvmx_pci_win_rd_addr_cn38xx    cn58xxp1;
};
typedef union cvmx_pci_win_rd_addr cvmx_pci_win_rd_addr_t;

/**
 * cvmx_pci_win_rd_data
 *
 * PCI_WIN_RD_DATA = PCI Window Read Data Register
 *
 * Contains the result from the read operation that took place when the LSB of the PCI_WIN_RD_ADDR
 * register was written.
 */
union cvmx_pci_win_rd_data {
	uint64_t u64;
	struct cvmx_pci_win_rd_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rd_data                      : 64; /**< The read data. */
#else
	uint64_t rd_data                      : 64;
#endif
	} s;
	struct cvmx_pci_win_rd_data_s         cn30xx;
	struct cvmx_pci_win_rd_data_s         cn31xx;
	struct cvmx_pci_win_rd_data_s         cn38xx;
	struct cvmx_pci_win_rd_data_s         cn38xxp2;
	struct cvmx_pci_win_rd_data_s         cn50xx;
	struct cvmx_pci_win_rd_data_s         cn58xx;
	struct cvmx_pci_win_rd_data_s         cn58xxp1;
};
typedef union cvmx_pci_win_rd_data cvmx_pci_win_rd_data_t;

/**
 * cvmx_pci_win_wr_addr
 *
 * PCI_WIN_WR_ADDR = PCI Window Write Address Register
 *
 * Contains the address to be writen to when a write operation is started by writing the
 * PCI_WIN_WR_DATA register (see below).
 */
union cvmx_pci_win_wr_addr {
	uint64_t u64;
	struct cvmx_pci_win_wr_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t iobit                        : 1;  /**< A 1 or 0 can be written here but this will always
                                                         read as '0'. */
	uint64_t wr_addr                      : 45; /**< The address that will be written to when the
                                                         PCI_WIN_WR_DATA register is written.
                                                         [47:40] = NCB_ID
                                                         [39:3]  = Address
                                                         When [47:43] == NPI & [42:0] == 0 bits [39:0] are:
                                                              [39:32] == x, Not Used
                                                              [31:27] == RSL_ID
                                                              [12:3]  == RSL Register Offset
                                                              [2:0]   == x, Not Used */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t wr_addr                      : 45;
	uint64_t iobit                        : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} s;
	struct cvmx_pci_win_wr_addr_s         cn30xx;
	struct cvmx_pci_win_wr_addr_s         cn31xx;
	struct cvmx_pci_win_wr_addr_s         cn38xx;
	struct cvmx_pci_win_wr_addr_s         cn38xxp2;
	struct cvmx_pci_win_wr_addr_s         cn50xx;
	struct cvmx_pci_win_wr_addr_s         cn58xx;
	struct cvmx_pci_win_wr_addr_s         cn58xxp1;
};
typedef union cvmx_pci_win_wr_addr cvmx_pci_win_wr_addr_t;

/**
 * cvmx_pci_win_wr_data
 *
 * PCI_WIN_WR_DATA = PCI Window Write Data Register
 *
 * Contains the data to write to the address located in the PCI_WIN_WR_ADDR Register.
 * Writing the least-significant-byte of this register will cause a write operation to take place.
 */
union cvmx_pci_win_wr_data {
	uint64_t u64;
	struct cvmx_pci_win_wr_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wr_data                      : 64; /**< The data to be written. Whenever the LSB of this
                                                         register is written, the Window Write will take
                                                         place. */
#else
	uint64_t wr_data                      : 64;
#endif
	} s;
	struct cvmx_pci_win_wr_data_s         cn30xx;
	struct cvmx_pci_win_wr_data_s         cn31xx;
	struct cvmx_pci_win_wr_data_s         cn38xx;
	struct cvmx_pci_win_wr_data_s         cn38xxp2;
	struct cvmx_pci_win_wr_data_s         cn50xx;
	struct cvmx_pci_win_wr_data_s         cn58xx;
	struct cvmx_pci_win_wr_data_s         cn58xxp1;
};
typedef union cvmx_pci_win_wr_data cvmx_pci_win_wr_data_t;

/**
 * cvmx_pci_win_wr_mask
 *
 * PCI_WIN_WR_MASK = PCI Window Write Mask Register
 *
 * Contains the mask for the data in the PCI_WIN_WR_DATA Register.
 */
union cvmx_pci_win_wr_mask {
	uint64_t u64;
	struct cvmx_pci_win_wr_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t wr_mask                      : 8;  /**< The data to be written. When a bit is set '1'
                                                         the corresponding byte will not be written. */
#else
	uint64_t wr_mask                      : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pci_win_wr_mask_s         cn30xx;
	struct cvmx_pci_win_wr_mask_s         cn31xx;
	struct cvmx_pci_win_wr_mask_s         cn38xx;
	struct cvmx_pci_win_wr_mask_s         cn38xxp2;
	struct cvmx_pci_win_wr_mask_s         cn50xx;
	struct cvmx_pci_win_wr_mask_s         cn58xx;
	struct cvmx_pci_win_wr_mask_s         cn58xxp1;
};
typedef union cvmx_pci_win_wr_mask cvmx_pci_win_wr_mask_t;

#endif
