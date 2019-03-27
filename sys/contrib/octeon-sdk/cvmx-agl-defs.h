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
 * cvmx-agl-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon agl.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_AGL_DEFS_H__
#define __CVMX_AGL_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_BAD_REG CVMX_AGL_GMX_BAD_REG_FUNC()
static inline uint64_t CVMX_AGL_GMX_BAD_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_BAD_REG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000518ull);
}
#else
#define CVMX_AGL_GMX_BAD_REG (CVMX_ADD_IO_SEG(0x00011800E0000518ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_BIST CVMX_AGL_GMX_BIST_FUNC()
static inline uint64_t CVMX_AGL_GMX_BIST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_BIST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000400ull);
}
#else
#define CVMX_AGL_GMX_BIST (CVMX_ADD_IO_SEG(0x00011800E0000400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_DRV_CTL CVMX_AGL_GMX_DRV_CTL_FUNC()
static inline uint64_t CVMX_AGL_GMX_DRV_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_AGL_GMX_DRV_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00007F0ull);
}
#else
#define CVMX_AGL_GMX_DRV_CTL (CVMX_ADD_IO_SEG(0x00011800E00007F0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_INF_MODE CVMX_AGL_GMX_INF_MODE_FUNC()
static inline uint64_t CVMX_AGL_GMX_INF_MODE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_AGL_GMX_INF_MODE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00007F8ull);
}
#else
#define CVMX_AGL_GMX_INF_MODE (CVMX_ADD_IO_SEG(0x00011800E00007F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_PRTX_CFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_PRTX_CFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000010ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_PRTX_CFG(offset) (CVMX_ADD_IO_SEG(0x00011800E0000010ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000180ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM0(offset) (CVMX_ADD_IO_SEG(0x00011800E0000180ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000188ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM1(offset) (CVMX_ADD_IO_SEG(0x00011800E0000188ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000190ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM2(offset) (CVMX_ADD_IO_SEG(0x00011800E0000190ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000198ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM3(offset) (CVMX_ADD_IO_SEG(0x00011800E0000198ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00001A0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM4(offset) (CVMX_ADD_IO_SEG(0x00011800E00001A0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM5(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM5(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00001A8ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM5(offset) (CVMX_ADD_IO_SEG(0x00011800E00001A8ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CAM_EN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CAM_EN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000108ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CAM_EN(offset) (CVMX_ADD_IO_SEG(0x00011800E0000108ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_ADR_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_ADR_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000100ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_ADR_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000100ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_DECISION(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_DECISION(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000040ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_DECISION(offset) (CVMX_ADD_IO_SEG(0x00011800E0000040ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_FRM_CHK(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_FRM_CHK(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000020ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_FRM_CHK(offset) (CVMX_ADD_IO_SEG(0x00011800E0000020ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_FRM_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_FRM_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000018ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_FRM_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000018ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_FRM_MAX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_FRM_MAX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000030ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_FRM_MAX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000030ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_FRM_MIN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_FRM_MIN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000028ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_FRM_MIN(offset) (CVMX_ADD_IO_SEG(0x00011800E0000028ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_IFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_IFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000058ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_IFG(offset) (CVMX_ADD_IO_SEG(0x00011800E0000058ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_INT_EN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_INT_EN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000008ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_INT_EN(offset) (CVMX_ADD_IO_SEG(0x00011800E0000008ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_INT_REG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_INT_REG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000000ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_INT_REG(offset) (CVMX_ADD_IO_SEG(0x00011800E0000000ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_JABBER(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_JABBER(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000038ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_JABBER(offset) (CVMX_ADD_IO_SEG(0x00011800E0000038ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_PAUSE_DROP_TIME(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_PAUSE_DROP_TIME(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000068ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_PAUSE_DROP_TIME(offset) (CVMX_ADD_IO_SEG(0x00011800E0000068ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_RX_INBND(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_RX_INBND(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000060ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_RX_INBND(offset) (CVMX_ADD_IO_SEG(0x00011800E0000060ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000050ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000050ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_OCTS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_OCTS(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000088ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_OCTS(offset) (CVMX_ADD_IO_SEG(0x00011800E0000088ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_OCTS_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_OCTS_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000098ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_OCTS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000098ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_OCTS_DMAC(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_OCTS_DMAC(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00000A8ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_OCTS_DMAC(offset) (CVMX_ADD_IO_SEG(0x00011800E00000A8ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_OCTS_DRP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_OCTS_DRP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00000B8ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_OCTS_DRP(offset) (CVMX_ADD_IO_SEG(0x00011800E00000B8ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_PKTS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_PKTS(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000080ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_PKTS(offset) (CVMX_ADD_IO_SEG(0x00011800E0000080ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_PKTS_BAD(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_PKTS_BAD(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00000C0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_PKTS_BAD(offset) (CVMX_ADD_IO_SEG(0x00011800E00000C0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_PKTS_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_PKTS_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000090ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_PKTS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000090ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_PKTS_DMAC(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_PKTS_DMAC(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00000A0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_PKTS_DMAC(offset) (CVMX_ADD_IO_SEG(0x00011800E00000A0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_STATS_PKTS_DRP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_STATS_PKTS_DRP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00000B0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_STATS_PKTS_DRP(offset) (CVMX_ADD_IO_SEG(0x00011800E00000B0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RXX_UDD_SKP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RXX_UDD_SKP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000048ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_RXX_UDD_SKP(offset) (CVMX_ADD_IO_SEG(0x00011800E0000048ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RX_BP_DROPX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RX_BP_DROPX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000420ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_AGL_GMX_RX_BP_DROPX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000420ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RX_BP_OFFX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RX_BP_OFFX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000460ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_AGL_GMX_RX_BP_OFFX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000460ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_RX_BP_ONX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_RX_BP_ONX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000440ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_AGL_GMX_RX_BP_ONX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000440ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_RX_PRT_INFO CVMX_AGL_GMX_RX_PRT_INFO_FUNC()
static inline uint64_t CVMX_AGL_GMX_RX_PRT_INFO_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_RX_PRT_INFO not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00004E8ull);
}
#else
#define CVMX_AGL_GMX_RX_PRT_INFO (CVMX_ADD_IO_SEG(0x00011800E00004E8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_RX_TX_STATUS CVMX_AGL_GMX_RX_TX_STATUS_FUNC()
static inline uint64_t CVMX_AGL_GMX_RX_TX_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_RX_TX_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00007E8ull);
}
#else
#define CVMX_AGL_GMX_RX_TX_STATUS (CVMX_ADD_IO_SEG(0x00011800E00007E8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_SMACX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_SMACX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000230ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_SMACX(offset) (CVMX_ADD_IO_SEG(0x00011800E0000230ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_STAT_BP CVMX_AGL_GMX_STAT_BP_FUNC()
static inline uint64_t CVMX_AGL_GMX_STAT_BP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_STAT_BP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000520ull);
}
#else
#define CVMX_AGL_GMX_STAT_BP (CVMX_ADD_IO_SEG(0x00011800E0000520ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_APPEND(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_APPEND(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000218ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_APPEND(offset) (CVMX_ADD_IO_SEG(0x00011800E0000218ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_CLK(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_CLK(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000208ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_CLK(offset) (CVMX_ADD_IO_SEG(0x00011800E0000208ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000270ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000270ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_MIN_PKT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_MIN_PKT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000240ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_MIN_PKT(offset) (CVMX_ADD_IO_SEG(0x00011800E0000240ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_PAUSE_PKT_INTERVAL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_PAUSE_PKT_INTERVAL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000248ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_PAUSE_PKT_INTERVAL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000248ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_PAUSE_PKT_TIME(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_PAUSE_PKT_TIME(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000238ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_PAUSE_PKT_TIME(offset) (CVMX_ADD_IO_SEG(0x00011800E0000238ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_PAUSE_TOGO(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_PAUSE_TOGO(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000258ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_PAUSE_TOGO(offset) (CVMX_ADD_IO_SEG(0x00011800E0000258ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_PAUSE_ZERO(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_PAUSE_ZERO(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000260ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_PAUSE_ZERO(offset) (CVMX_ADD_IO_SEG(0x00011800E0000260ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_SOFT_PAUSE(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_SOFT_PAUSE(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000250ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_SOFT_PAUSE(offset) (CVMX_ADD_IO_SEG(0x00011800E0000250ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000280ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT0(offset) (CVMX_ADD_IO_SEG(0x00011800E0000280ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000288ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT1(offset) (CVMX_ADD_IO_SEG(0x00011800E0000288ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000290ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT2(offset) (CVMX_ADD_IO_SEG(0x00011800E0000290ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000298ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT3(offset) (CVMX_ADD_IO_SEG(0x00011800E0000298ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00002A0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT4(offset) (CVMX_ADD_IO_SEG(0x00011800E00002A0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT5(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT5(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00002A8ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT5(offset) (CVMX_ADD_IO_SEG(0x00011800E00002A8ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT6(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT6(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00002B0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT6(offset) (CVMX_ADD_IO_SEG(0x00011800E00002B0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT7(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT7(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00002B8ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT7(offset) (CVMX_ADD_IO_SEG(0x00011800E00002B8ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT8(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT8(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00002C0ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT8(offset) (CVMX_ADD_IO_SEG(0x00011800E00002C0ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STAT9(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STAT9(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E00002C8ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STAT9(offset) (CVMX_ADD_IO_SEG(0x00011800E00002C8ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_STATS_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_STATS_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000268ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_STATS_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0000268ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_GMX_TXX_THRESH(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_GMX_TXX_THRESH(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0000210ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_AGL_GMX_TXX_THRESH(offset) (CVMX_ADD_IO_SEG(0x00011800E0000210ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_BP CVMX_AGL_GMX_TX_BP_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_BP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_BP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00004D0ull);
}
#else
#define CVMX_AGL_GMX_TX_BP (CVMX_ADD_IO_SEG(0x00011800E00004D0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_COL_ATTEMPT CVMX_AGL_GMX_TX_COL_ATTEMPT_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_COL_ATTEMPT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_COL_ATTEMPT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000498ull);
}
#else
#define CVMX_AGL_GMX_TX_COL_ATTEMPT (CVMX_ADD_IO_SEG(0x00011800E0000498ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_IFG CVMX_AGL_GMX_TX_IFG_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_IFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_IFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000488ull);
}
#else
#define CVMX_AGL_GMX_TX_IFG (CVMX_ADD_IO_SEG(0x00011800E0000488ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_INT_EN CVMX_AGL_GMX_TX_INT_EN_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_INT_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_INT_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000508ull);
}
#else
#define CVMX_AGL_GMX_TX_INT_EN (CVMX_ADD_IO_SEG(0x00011800E0000508ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_INT_REG CVMX_AGL_GMX_TX_INT_REG_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_INT_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_INT_REG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000500ull);
}
#else
#define CVMX_AGL_GMX_TX_INT_REG (CVMX_ADD_IO_SEG(0x00011800E0000500ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_JAM CVMX_AGL_GMX_TX_JAM_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_JAM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_JAM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E0000490ull);
}
#else
#define CVMX_AGL_GMX_TX_JAM (CVMX_ADD_IO_SEG(0x00011800E0000490ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_LFSR CVMX_AGL_GMX_TX_LFSR_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_LFSR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_LFSR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00004F8ull);
}
#else
#define CVMX_AGL_GMX_TX_LFSR (CVMX_ADD_IO_SEG(0x00011800E00004F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_OVR_BP CVMX_AGL_GMX_TX_OVR_BP_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_OVR_BP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_OVR_BP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00004C8ull);
}
#else
#define CVMX_AGL_GMX_TX_OVR_BP (CVMX_ADD_IO_SEG(0x00011800E00004C8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_PAUSE_PKT_DMAC CVMX_AGL_GMX_TX_PAUSE_PKT_DMAC_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_PAUSE_PKT_DMAC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_PAUSE_PKT_DMAC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00004A0ull);
}
#else
#define CVMX_AGL_GMX_TX_PAUSE_PKT_DMAC (CVMX_ADD_IO_SEG(0x00011800E00004A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_AGL_GMX_TX_PAUSE_PKT_TYPE CVMX_AGL_GMX_TX_PAUSE_PKT_TYPE_FUNC()
static inline uint64_t CVMX_AGL_GMX_TX_PAUSE_PKT_TYPE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_AGL_GMX_TX_PAUSE_PKT_TYPE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800E00004A8ull);
}
#else
#define CVMX_AGL_GMX_TX_PAUSE_PKT_TYPE (CVMX_ADD_IO_SEG(0x00011800E00004A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_AGL_PRTX_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_AGL_PRTX_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800E0002000ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_AGL_PRTX_CTL(offset) (CVMX_ADD_IO_SEG(0x00011800E0002000ull) + ((offset) & 1) * 8)
#endif

/**
 * cvmx_agl_gmx_bad_reg
 *
 * AGL_GMX_BAD_REG = A collection of things that have gone very, very wrong
 *
 *
 * Notes:
 * OUT_OVR[0], LOSTSTAT[0], OVRFLW, TXPOP, TXPSH    will be reset when MIX0_CTL[RESET] is set to 1.
 * OUT_OVR[1], LOSTSTAT[1], OVRFLW1, TXPOP1, TXPSH1 will be reset when MIX1_CTL[RESET] is set to 1.
 * STATOVR will be reset when both MIX0/1_CTL[RESET] are set to 1.
 */
union cvmx_agl_gmx_bad_reg {
	uint64_t u64;
	struct cvmx_agl_gmx_bad_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t txpsh1                       : 1;  /**< TX FIFO overflow (MII1) */
	uint64_t txpop1                       : 1;  /**< TX FIFO underflow (MII1) */
	uint64_t ovrflw1                      : 1;  /**< RX FIFO overflow (MII1) */
	uint64_t txpsh                        : 1;  /**< TX FIFO overflow (MII0) */
	uint64_t txpop                        : 1;  /**< TX FIFO underflow (MII0) */
	uint64_t ovrflw                       : 1;  /**< RX FIFO overflow (MII0) */
	uint64_t reserved_27_31               : 5;
	uint64_t statovr                      : 1;  /**< TX Statistics overflow */
	uint64_t reserved_24_25               : 2;
	uint64_t loststat                     : 2;  /**< TX Statistics data was over-written
                                                         In MII/RGMII, one bit per port
                                                         TX Stats are corrupted */
	uint64_t reserved_4_21                : 18;
	uint64_t out_ovr                      : 2;  /**< Outbound data FIFO overflow */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t out_ovr                      : 2;
	uint64_t reserved_4_21                : 18;
	uint64_t loststat                     : 2;
	uint64_t reserved_24_25               : 2;
	uint64_t statovr                      : 1;
	uint64_t reserved_27_31               : 5;
	uint64_t ovrflw                       : 1;
	uint64_t txpop                        : 1;
	uint64_t txpsh                        : 1;
	uint64_t ovrflw1                      : 1;
	uint64_t txpop1                       : 1;
	uint64_t txpsh1                       : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} s;
	struct cvmx_agl_gmx_bad_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t txpsh1                       : 1;  /**< TX FIFO overflow (MII1) */
	uint64_t txpop1                       : 1;  /**< TX FIFO underflow (MII1) */
	uint64_t ovrflw1                      : 1;  /**< RX FIFO overflow (MII1) */
	uint64_t txpsh                        : 1;  /**< TX FIFO overflow (MII0) */
	uint64_t txpop                        : 1;  /**< TX FIFO underflow (MII0) */
	uint64_t ovrflw                       : 1;  /**< RX FIFO overflow (MII0) */
	uint64_t reserved_27_31               : 5;
	uint64_t statovr                      : 1;  /**< TX Statistics overflow */
	uint64_t reserved_23_25               : 3;
	uint64_t loststat                     : 1;  /**< TX Statistics data was over-written
                                                         TX Stats are corrupted */
	uint64_t reserved_4_21                : 18;
	uint64_t out_ovr                      : 2;  /**< Outbound data FIFO overflow */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t out_ovr                      : 2;
	uint64_t reserved_4_21                : 18;
	uint64_t loststat                     : 1;
	uint64_t reserved_23_25               : 3;
	uint64_t statovr                      : 1;
	uint64_t reserved_27_31               : 5;
	uint64_t ovrflw                       : 1;
	uint64_t txpop                        : 1;
	uint64_t txpsh                        : 1;
	uint64_t ovrflw1                      : 1;
	uint64_t txpop1                       : 1;
	uint64_t txpsh1                       : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_bad_reg_cn52xx    cn52xxp1;
	struct cvmx_agl_gmx_bad_reg_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t txpsh                        : 1;  /**< TX FIFO overflow */
	uint64_t txpop                        : 1;  /**< TX FIFO underflow */
	uint64_t ovrflw                       : 1;  /**< RX FIFO overflow */
	uint64_t reserved_27_31               : 5;
	uint64_t statovr                      : 1;  /**< TX Statistics overflow */
	uint64_t reserved_23_25               : 3;
	uint64_t loststat                     : 1;  /**< TX Statistics data was over-written
                                                         TX Stats are corrupted */
	uint64_t reserved_3_21                : 19;
	uint64_t out_ovr                      : 1;  /**< Outbound data FIFO overflow */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t out_ovr                      : 1;
	uint64_t reserved_3_21                : 19;
	uint64_t loststat                     : 1;
	uint64_t reserved_23_25               : 3;
	uint64_t statovr                      : 1;
	uint64_t reserved_27_31               : 5;
	uint64_t ovrflw                       : 1;
	uint64_t txpop                        : 1;
	uint64_t txpsh                        : 1;
	uint64_t reserved_35_63               : 29;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_bad_reg_cn56xx    cn56xxp1;
	struct cvmx_agl_gmx_bad_reg_s         cn61xx;
	struct cvmx_agl_gmx_bad_reg_s         cn63xx;
	struct cvmx_agl_gmx_bad_reg_s         cn63xxp1;
	struct cvmx_agl_gmx_bad_reg_s         cn66xx;
	struct cvmx_agl_gmx_bad_reg_s         cn68xx;
	struct cvmx_agl_gmx_bad_reg_s         cn68xxp1;
};
typedef union cvmx_agl_gmx_bad_reg cvmx_agl_gmx_bad_reg_t;

/**
 * cvmx_agl_gmx_bist
 *
 * AGL_GMX_BIST = GMX BIST Results
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_bist {
	uint64_t u64;
	struct cvmx_agl_gmx_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t status                       : 25; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         - 0: gmx#.inb.fif_bnk0
                                                         - 1: gmx#.inb.fif_bnk1
                                                         - 2: gmx#.inb.fif_bnk2
                                                         - 3: gmx#.inb.fif_bnk3
                                                         - 4: gmx#.inb.fif_bnk_ext0
                                                         - 5: gmx#.inb.fif_bnk_ext1
                                                         - 6: gmx#.inb.fif_bnk_ext2
                                                         - 7: gmx#.inb.fif_bnk_ext3
                                                         - 8: gmx#.outb.fif.fif_bnk0
                                                         - 9: gmx#.outb.fif.fif_bnk1
                                                         - 10: RAZ
                                                         - 11: RAZ
                                                         - 12: gmx#.outb.fif.fif_bnk_ext0
                                                         - 13: gmx#.outb.fif.fif_bnk_ext1
                                                         - 14: RAZ
                                                         - 15: RAZ
                                                         - 16: RAZ
                                                         - 17: RAZ
                                                         - 18: RAZ
                                                         - 19: RAZ
                                                         - 20: gmx#.csr.drf20x32m2_bist
                                                         - 21: gmx#.csr.drf20x48m2_bist
                                                         - 22: gmx#.outb.stat.drf16x27m1_bist
                                                         - 23: gmx#.outb.stat.drf40x64m1_bist
                                                         - 24: RAZ */
#else
	uint64_t status                       : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_agl_gmx_bist_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t status                       : 10; /**< BIST Results.
                                                          HW sets a bit in BIST for for memory that fails
                                                         - 0: gmx#.inb.drf128x78m1_bist
                                                         - 1: gmx#.outb.fif.drf128x71m1_bist
                                                         - 2: gmx#.csr.gmi0.srf8x64m1_bist
                                                         - 3: gmx#.csr.gmi1.srf8x64m1_bist
                                                         - 4: 0
                                                         - 5: 0
                                                         - 6: gmx#.csr.drf20x80m1_bist
                                                         - 7: gmx#.outb.stat.drf16x27m1_bist
                                                         - 8: gmx#.outb.stat.drf40x64m1_bist
                                                         - 9: 0 */
#else
	uint64_t status                       : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_bist_cn52xx       cn52xxp1;
	struct cvmx_agl_gmx_bist_cn52xx       cn56xx;
	struct cvmx_agl_gmx_bist_cn52xx       cn56xxp1;
	struct cvmx_agl_gmx_bist_s            cn61xx;
	struct cvmx_agl_gmx_bist_s            cn63xx;
	struct cvmx_agl_gmx_bist_s            cn63xxp1;
	struct cvmx_agl_gmx_bist_s            cn66xx;
	struct cvmx_agl_gmx_bist_s            cn68xx;
	struct cvmx_agl_gmx_bist_s            cn68xxp1;
};
typedef union cvmx_agl_gmx_bist cvmx_agl_gmx_bist_t;

/**
 * cvmx_agl_gmx_drv_ctl
 *
 * AGL_GMX_DRV_CTL = GMX Drive Control
 *
 *
 * Notes:
 * NCTL, PCTL, BYP_EN    will be reset when MIX0_CTL[RESET] is set to 1.
 * NCTL1, PCTL1, BYP_EN1 will be reset when MIX1_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_drv_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_drv_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t byp_en1                      : 1;  /**< Compensation Controller Bypass Enable (MII1) */
	uint64_t reserved_45_47               : 3;
	uint64_t pctl1                        : 5;  /**< AGL PCTL (MII1) */
	uint64_t reserved_37_39               : 3;
	uint64_t nctl1                        : 5;  /**< AGL NCTL (MII1) */
	uint64_t reserved_17_31               : 15;
	uint64_t byp_en                       : 1;  /**< Compensation Controller Bypass Enable */
	uint64_t reserved_13_15               : 3;
	uint64_t pctl                         : 5;  /**< AGL PCTL */
	uint64_t reserved_5_7                 : 3;
	uint64_t nctl                         : 5;  /**< AGL NCTL */
#else
	uint64_t nctl                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t pctl                         : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t byp_en                       : 1;
	uint64_t reserved_17_31               : 15;
	uint64_t nctl1                        : 5;
	uint64_t reserved_37_39               : 3;
	uint64_t pctl1                        : 5;
	uint64_t reserved_45_47               : 3;
	uint64_t byp_en1                      : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} s;
	struct cvmx_agl_gmx_drv_ctl_s         cn52xx;
	struct cvmx_agl_gmx_drv_ctl_s         cn52xxp1;
	struct cvmx_agl_gmx_drv_ctl_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t byp_en                       : 1;  /**< Compensation Controller Bypass Enable */
	uint64_t reserved_13_15               : 3;
	uint64_t pctl                         : 5;  /**< AGL PCTL */
	uint64_t reserved_5_7                 : 3;
	uint64_t nctl                         : 5;  /**< AGL NCTL */
#else
	uint64_t nctl                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t pctl                         : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t byp_en                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_drv_ctl_cn56xx    cn56xxp1;
};
typedef union cvmx_agl_gmx_drv_ctl cvmx_agl_gmx_drv_ctl_t;

/**
 * cvmx_agl_gmx_inf_mode
 *
 * AGL_GMX_INF_MODE = Interface Mode
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_inf_mode {
	uint64_t u64;
	struct cvmx_agl_gmx_inf_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t en                           : 1;  /**< Interface Enable */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_agl_gmx_inf_mode_s        cn52xx;
	struct cvmx_agl_gmx_inf_mode_s        cn52xxp1;
	struct cvmx_agl_gmx_inf_mode_s        cn56xx;
	struct cvmx_agl_gmx_inf_mode_s        cn56xxp1;
};
typedef union cvmx_agl_gmx_inf_mode cvmx_agl_gmx_inf_mode_t;

/**
 * cvmx_agl_gmx_prt#_cfg
 *
 * AGL_GMX_PRT_CFG = Port description
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_prtx_cfg {
	uint64_t u64;
	struct cvmx_agl_gmx_prtx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t tx_idle                      : 1;  /**< TX Machine is idle */
	uint64_t rx_idle                      : 1;  /**< RX Machine is idle */
	uint64_t reserved_9_11                : 3;
	uint64_t speed_msb                    : 1;  /**< Link Speed MSB [SPEED_MSB:SPEED]
                                                         10 = 10Mbs operation
                                                         00 = 100Mbs operation
                                                         01 = 1000Mbs operation
                                                         11 = Reserved */
	uint64_t reserved_7_7                 : 1;
	uint64_t burst                        : 1;  /**< Half-Duplex Burst Enable
                                                         Only valid for 1000Mbs half-duplex operation
                                                          0 = burst length of 0x2000 (halfdup / 1000Mbs)
                                                          1 = burst length of 0x0    (all other modes) */
	uint64_t tx_en                        : 1;  /**< Port enable.  Must be set for Octane to send
                                                         RMGII traffic.   When this bit clear on a given
                                                         port, then all packet cycles will appear as
                                                         inter-frame cycles. */
	uint64_t rx_en                        : 1;  /**< Port enable.  Must be set for Octane to receive
                                                         RMGII traffic.  When this bit clear on a given
                                                         port, then the all packet cycles will appear as
                                                         inter-frame cycles. */
	uint64_t slottime                     : 1;  /**< Slot Time for Half-Duplex operation
                                                         0 = 512 bitimes (10/100Mbs operation)
                                                         1 = 4096 bitimes (1000Mbs operation) */
	uint64_t duplex                       : 1;  /**< Duplex
                                                         0 = Half Duplex (collisions/extentions/bursts)
                                                         1 = Full Duplex */
	uint64_t speed                        : 1;  /**< Link Speed LSB [SPEED_MSB:SPEED]
                                                         10 = 10Mbs operation
                                                         00 = 100Mbs operation
                                                         01 = 1000Mbs operation
                                                         11 = Reserved */
	uint64_t en                           : 1;  /**< Link Enable
                                                         When EN is clear, packets will not be received
                                                         or transmitted (including PAUSE and JAM packets).
                                                         If EN is cleared while a packet is currently
                                                         being received or transmitted, the packet will
                                                         be allowed to complete before the bus is idled.
                                                         On the RX side, subsequent packets in a burst
                                                         will be ignored. */
#else
	uint64_t en                           : 1;
	uint64_t speed                        : 1;
	uint64_t duplex                       : 1;
	uint64_t slottime                     : 1;
	uint64_t rx_en                        : 1;
	uint64_t tx_en                        : 1;
	uint64_t burst                        : 1;
	uint64_t reserved_7_7                 : 1;
	uint64_t speed_msb                    : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t rx_idle                      : 1;
	uint64_t tx_idle                      : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_agl_gmx_prtx_cfg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t tx_en                        : 1;  /**< Port enable.  Must be set for Octane to send
                                                         RMGII traffic.   When this bit clear on a given
                                                         port, then all MII cycles will appear as
                                                         inter-frame cycles. */
	uint64_t rx_en                        : 1;  /**< Port enable.  Must be set for Octane to receive
                                                         RMGII traffic.  When this bit clear on a given
                                                         port, then the all MII cycles will appear as
                                                         inter-frame cycles. */
	uint64_t slottime                     : 1;  /**< Slot Time for Half-Duplex operation
                                                         0 = 512 bitimes (10/100Mbs operation)
                                                         1 = Reserved */
	uint64_t duplex                       : 1;  /**< Duplex
                                                         0 = Half Duplex (collisions/extentions/bursts)
                                                         1 = Full Duplex */
	uint64_t speed                        : 1;  /**< Link Speed
                                                         0 = 10/100Mbs operation
                                                         1 = Reserved */
	uint64_t en                           : 1;  /**< Link Enable
                                                         When EN is clear, packets will not be received
                                                         or transmitted (including PAUSE and JAM packets).
                                                         If EN is cleared while a packet is currently
                                                         being received or transmitted, the packet will
                                                         be allowed to complete before the bus is idled.
                                                         On the RX side, subsequent packets in a burst
                                                         will be ignored. */
#else
	uint64_t en                           : 1;
	uint64_t speed                        : 1;
	uint64_t duplex                       : 1;
	uint64_t slottime                     : 1;
	uint64_t rx_en                        : 1;
	uint64_t tx_en                        : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_prtx_cfg_cn52xx   cn52xxp1;
	struct cvmx_agl_gmx_prtx_cfg_cn52xx   cn56xx;
	struct cvmx_agl_gmx_prtx_cfg_cn52xx   cn56xxp1;
	struct cvmx_agl_gmx_prtx_cfg_s        cn61xx;
	struct cvmx_agl_gmx_prtx_cfg_s        cn63xx;
	struct cvmx_agl_gmx_prtx_cfg_s        cn63xxp1;
	struct cvmx_agl_gmx_prtx_cfg_s        cn66xx;
	struct cvmx_agl_gmx_prtx_cfg_s        cn68xx;
	struct cvmx_agl_gmx_prtx_cfg_s        cn68xxp1;
};
typedef union cvmx_agl_gmx_prtx_cfg cvmx_agl_gmx_prtx_cfg_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam0
 *
 * AGL_GMX_RX_ADR_CAM = Address Filtering Control
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam0 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on
                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam0_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam0 cvmx_agl_gmx_rxx_adr_cam0_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam1
 *
 * AGL_GMX_RX_ADR_CAM = Address Filtering Control
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam1 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on
                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam1_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam1 cvmx_agl_gmx_rxx_adr_cam1_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam2
 *
 * AGL_GMX_RX_ADR_CAM = Address Filtering Control
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam2 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on
                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam2_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam2 cvmx_agl_gmx_rxx_adr_cam2_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam3
 *
 * AGL_GMX_RX_ADR_CAM = Address Filtering Control
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam3 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on
                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam3_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam3 cvmx_agl_gmx_rxx_adr_cam3_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam4
 *
 * AGL_GMX_RX_ADR_CAM = Address Filtering Control
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam4 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on
                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam4_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam4 cvmx_agl_gmx_rxx_adr_cam4_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam5
 *
 * AGL_GMX_RX_ADR_CAM = Address Filtering Control
 *
 *
 * Notes:
 * Not reset when MIX*_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam5 {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on
                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam5_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam5 cvmx_agl_gmx_rxx_adr_cam5_t;

/**
 * cvmx_agl_gmx_rx#_adr_cam_en
 *
 * AGL_GMX_RX_ADR_CAM_EN = Address Filtering Control Enable
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_adr_cam_en {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t en                           : 8;  /**< CAM Entry Enables */
#else
	uint64_t en                           : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn52xx;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn56xx;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn61xx;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn63xx;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn66xx;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn68xx;
	struct cvmx_agl_gmx_rxx_adr_cam_en_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_cam_en cvmx_agl_gmx_rxx_adr_cam_en_t;

/**
 * cvmx_agl_gmx_rx#_adr_ctl
 *
 * AGL_GMX_RX_ADR_CTL = Address Filtering Control
 *
 *
 * Notes:
 * * ALGORITHM
 *   Here is some pseudo code that represents the address filter behavior.
 *
 *      @verbatim
 *      bool dmac_addr_filter(uint8 prt, uint48 dmac) [
 *        ASSERT(prt >= 0 && prt <= 3);
 *        if (is_bcst(dmac))                               // broadcast accept
 *          return (AGL_GMX_RX[prt]_ADR_CTL[BCST] ? ACCEPT : REJECT);
 *        if (is_mcst(dmac) & AGL_GMX_RX[prt]_ADR_CTL[MCST] == 1)   // multicast reject
 *          return REJECT;
 *        if (is_mcst(dmac) & AGL_GMX_RX[prt]_ADR_CTL[MCST] == 2)   // multicast accept
 *          return ACCEPT;
 *
 *        cam_hit = 0;
 *
 *        for (i=0; i<8; i++) [
 *          if (AGL_GMX_RX[prt]_ADR_CAM_EN[EN<i>] == 0)
 *            continue;
 *          uint48 unswizzled_mac_adr = 0x0;
 *          for (j=5; j>=0; j--) [
 *             unswizzled_mac_adr = (unswizzled_mac_adr << 8) | AGL_GMX_RX[prt]_ADR_CAM[j][ADR<i*8+7:i*8>];
 *          ]
 *          if (unswizzled_mac_adr == dmac) [
 *            cam_hit = 1;
 *            break;
 *          ]
 *        ]
 *
 *        if (cam_hit)
 *          return (AGL_GMX_RX[prt]_ADR_CTL[CAM_MODE] ? ACCEPT : REJECT);
 *        else
 *          return (AGL_GMX_RX[prt]_ADR_CTL[CAM_MODE] ? REJECT : ACCEPT);
 *      ]
 *      @endverbatim
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_adr_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_adr_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t cam_mode                     : 1;  /**< Allow or deny DMAC address filter
                                                         0 = reject the packet on DMAC address match
                                                         1 = accept the packet on DMAC address match */
	uint64_t mcst                         : 2;  /**< Multicast Mode
                                                         0 = Use the Address Filter CAM
                                                         1 = Force reject all multicast packets
                                                         2 = Force accept all multicast packets
                                                         3 = Reserved */
	uint64_t bcst                         : 1;  /**< Accept All Broadcast Packets */
#else
	uint64_t bcst                         : 1;
	uint64_t mcst                         : 2;
	uint64_t cam_mode                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn52xx;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn52xxp1;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn56xx;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn56xxp1;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn61xx;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn63xx;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn66xx;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn68xx;
	struct cvmx_agl_gmx_rxx_adr_ctl_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_adr_ctl cvmx_agl_gmx_rxx_adr_ctl_t;

/**
 * cvmx_agl_gmx_rx#_decision
 *
 * AGL_GMX_RX_DECISION = The byte count to decide when to accept or filter a packet
 *
 *
 * Notes:
 * As each byte in a packet is received by GMX, the L2 byte count is compared
 * against the AGL_GMX_RX_DECISION[CNT].  The L2 byte count is the number of bytes
 * from the beginning of the L2 header (DMAC).  In normal operation, the L2
 * header begins after the PREAMBLE+SFD (AGL_GMX_RX_FRM_CTL[PRE_CHK]=1) and any
 * optional UDD skip data (AGL_GMX_RX_UDD_SKP[LEN]).
 *
 * When AGL_GMX_RX_FRM_CTL[PRE_CHK] is clear, PREAMBLE+SFD are prepended to the
 * packet and would require UDD skip length to account for them.
 *
 *                                                 L2 Size
 * Port Mode             <=AGL_GMX_RX_DECISION bytes (default=24)  >AGL_GMX_RX_DECISION bytes (default=24)
 *
 * MII/Full Duplex       accept packet                             apply filters
 *                       no filtering is applied                   accept packet based on DMAC and PAUSE packet filters
 *
 * MII/Half Duplex       drop packet                               apply filters
 *                       packet is unconditionally dropped         accept packet based on DMAC
 *
 * where l2_size = MAX(0, total_packet_size - AGL_GMX_RX_UDD_SKP[LEN] - ((AGL_GMX_RX_FRM_CTL[PRE_CHK]==1)*8)
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_decision {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_decision_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t cnt                          : 5;  /**< The byte count to decide when to accept or filter
                                                         a packet. */
#else
	uint64_t cnt                          : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_decision_s    cn52xx;
	struct cvmx_agl_gmx_rxx_decision_s    cn52xxp1;
	struct cvmx_agl_gmx_rxx_decision_s    cn56xx;
	struct cvmx_agl_gmx_rxx_decision_s    cn56xxp1;
	struct cvmx_agl_gmx_rxx_decision_s    cn61xx;
	struct cvmx_agl_gmx_rxx_decision_s    cn63xx;
	struct cvmx_agl_gmx_rxx_decision_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_decision_s    cn66xx;
	struct cvmx_agl_gmx_rxx_decision_s    cn68xx;
	struct cvmx_agl_gmx_rxx_decision_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_decision cvmx_agl_gmx_rxx_decision_t;

/**
 * cvmx_agl_gmx_rx#_frm_chk
 *
 * AGL_GMX_RX_FRM_CHK = Which frame errors will set the ERR bit of the frame
 *
 *
 * Notes:
 * If AGL_GMX_RX_UDD_SKP[LEN] != 0, then LENERR will be forced to zero in HW.
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_frm_chk {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_chk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t niberr                       : 1;  /**< Nibble error */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with packet data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< Carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_frm_chk_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with MII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t reserved_1_1                 : 1;
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_rxx_frm_chk_cn52xx cn52xxp1;
	struct cvmx_agl_gmx_rxx_frm_chk_cn52xx cn56xx;
	struct cvmx_agl_gmx_rxx_frm_chk_cn52xx cn56xxp1;
	struct cvmx_agl_gmx_rxx_frm_chk_s     cn61xx;
	struct cvmx_agl_gmx_rxx_frm_chk_s     cn63xx;
	struct cvmx_agl_gmx_rxx_frm_chk_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_frm_chk_s     cn66xx;
	struct cvmx_agl_gmx_rxx_frm_chk_s     cn68xx;
	struct cvmx_agl_gmx_rxx_frm_chk_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_frm_chk cvmx_agl_gmx_rxx_frm_chk_t;

/**
 * cvmx_agl_gmx_rx#_frm_ctl
 *
 * AGL_GMX_RX_FRM_CTL = Frame Control
 *
 *
 * Notes:
 * * PRE_STRP
 *   When PRE_CHK is set (indicating that the PREAMBLE will be sent), PRE_STRP
 *   determines if the PREAMBLE+SFD bytes are thrown away or sent to the Octane
 *   core as part of the packet.
 *
 *   In either mode, the PREAMBLE+SFD bytes are not counted toward the packet
 *   size when checking against the MIN and MAX bounds.  Furthermore, the bytes
 *   are skipped when locating the start of the L2 header for DMAC and Control
 *   frame recognition.
 *
 * * CTL_BCK/CTL_DRP
 *   These bits control how the HW handles incoming PAUSE packets.  Here are
 *   the most common modes of operation:
 *     CTL_BCK=1,CTL_DRP=1   - HW does it all
 *     CTL_BCK=0,CTL_DRP=0   - SW sees all pause frames
 *     CTL_BCK=0,CTL_DRP=1   - all pause frames are completely ignored
 *
 *   These control bits should be set to CTL_BCK=0,CTL_DRP=0 in halfdup mode.
 *   Since PAUSE packets only apply to fulldup operation, any PAUSE packet
 *   would constitute an exception which should be handled by the processing
 *   cores.  PAUSE packets should not be forwarded.
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_frm_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t ptp_mode                     : 1;  /**< Timestamp mode
                                                         When PTP_MODE is set, a 64-bit timestamp will be
                                                         prepended to every incoming packet. The timestamp
                                                         bytes are added to the packet in such a way as to
                                                         not modify the packet's receive byte count.  This
                                                         implies that the AGL_GMX_RX_JABBER,
                                                         AGL_GMX_RX_FRM_MIN, AGL_GMX_RX_FRM_MAX,
                                                         AGL_GMX_RX_DECISION, AGL_GMX_RX_UDD_SKP, and the
                                                         AGL_GMX_RX_STATS_* do not require any adjustment
                                                         as they operate on the received packet size.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1. */
	uint64_t reserved_11_11               : 1;
	uint64_t null_dis                     : 1;  /**< When set, do not modify the MOD bits on NULL ticks
                                                         due to PARITAL packets */
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PRE_STRP should be set to
                                                         account for the variable nature of the PREAMBLE.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for non-min
                                                         sized pkts with padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t pre_free                     : 1;  /**< When set, PREAMBLE checking is less strict.
                                                         AGL will begin the frame at the first SFD.
                                                         PRE_FREE must be set if PRE_ALIGN is set.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped
                                                         PRE_STRP must be set if PRE_ALIGN is set.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send a valid 802.3
                                                         PREAMBLE to begin every frame. AGL checks that a
                                                         valid PREAMBLE is received (based on PRE_FREE).
                                                         When a problem does occur within the PREAMBLE
                                                         seqeunce, the frame is marked as bad and not sent
                                                         into the core.  The AGL_GMX_RX_INT_REG[PCTERR]
                                                         interrupt is also raised. */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t pre_align                    : 1;
	uint64_t null_dis                     : 1;
	uint64_t reserved_11_11               : 1;
	uint64_t ptp_mode                     : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_frm_ctl_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PREAMBLE can be consumed
                                                         by the HW so when PRE_ALIGN is set, PRE_FREE,
                                                         PRE_STRP must be set for correct operation.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for non-min
                                                         sized pkts with padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t pre_free                     : 1;  /**< When set, PREAMBLE checking is  less strict.
                                                         0 - 254 cycles of PREAMBLE followed by SFD
                                                         PRE_FREE must be set if PRE_ALIGN is set.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped
                                                         PRE_STRP must be set if PRE_ALIGN is set.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send PREAMBLE+SFD
                                                         to begin every frame.  GMX checks that the
                                                         PREAMBLE is sent correctly */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t pre_align                    : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_rxx_frm_ctl_cn52xx cn52xxp1;
	struct cvmx_agl_gmx_rxx_frm_ctl_cn52xx cn56xx;
	struct cvmx_agl_gmx_rxx_frm_ctl_cn52xx cn56xxp1;
	struct cvmx_agl_gmx_rxx_frm_ctl_s     cn61xx;
	struct cvmx_agl_gmx_rxx_frm_ctl_s     cn63xx;
	struct cvmx_agl_gmx_rxx_frm_ctl_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_frm_ctl_s     cn66xx;
	struct cvmx_agl_gmx_rxx_frm_ctl_s     cn68xx;
	struct cvmx_agl_gmx_rxx_frm_ctl_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_frm_ctl cvmx_agl_gmx_rxx_frm_ctl_t;

/**
 * cvmx_agl_gmx_rx#_frm_max
 *
 * AGL_GMX_RX_FRM_MAX = Frame Max length
 *
 *
 * Notes:
 * When changing the LEN field, be sure that LEN does not exceed
 * AGL_GMX_RX_JABBER[CNT]. Failure to meet this constraint will cause packets that
 * are within the maximum length parameter to be rejected because they exceed
 * the AGL_GMX_RX_JABBER[CNT] limit.
 *
 * Notes:
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_frm_max {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t len                          : 16; /**< Byte count for Max-sized frame check
                                                         AGL_GMX_RXn_FRM_CHK[MAXERR] enables the check
                                                         for port n.
                                                         If enabled, failing packets set the MAXERR
                                                         interrupt and the MIX opcode is set to OVER_FCS
                                                         (0x3, if packet has bad FCS) or OVER_ERR (0x4, if
                                                         packet has good FCS).
                                                         LEN <= AGL_GMX_RX_JABBER[CNT] */
#else
	uint64_t len                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn52xx;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn52xxp1;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn56xx;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn56xxp1;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn61xx;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn63xx;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn66xx;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn68xx;
	struct cvmx_agl_gmx_rxx_frm_max_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_frm_max cvmx_agl_gmx_rxx_frm_max_t;

/**
 * cvmx_agl_gmx_rx#_frm_min
 *
 * AGL_GMX_RX_FRM_MIN = Frame Min length
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_frm_min {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_frm_min_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t len                          : 16; /**< Byte count for Min-sized frame check
                                                         AGL_GMX_RXn_FRM_CHK[MINERR] enables the check
                                                         for port n.
                                                         If enabled, failing packets set the MINERR
                                                         interrupt and the MIX opcode is set to UNDER_FCS
                                                         (0x6, if packet has bad FCS) or UNDER_ERR (0x8,
                                                         if packet has good FCS). */
#else
	uint64_t len                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn52xx;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn52xxp1;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn56xx;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn56xxp1;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn61xx;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn63xx;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn66xx;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn68xx;
	struct cvmx_agl_gmx_rxx_frm_min_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_frm_min cvmx_agl_gmx_rxx_frm_min_t;

/**
 * cvmx_agl_gmx_rx#_ifg
 *
 * AGL_GMX_RX_IFG = RX Min IFG
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_ifg {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t ifg                          : 4;  /**< Min IFG (in IFG*8 bits) between packets used to
                                                         determine IFGERR. Normally IFG is 96 bits.
                                                         Note in some operating modes, IFG cycles can be
                                                         inserted or removed in order to achieve clock rate
                                                         adaptation. For these reasons, the default value
                                                         is slightly conservative and does not check upto
                                                         the full 96 bits of IFG. */
#else
	uint64_t ifg                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_ifg_s         cn52xx;
	struct cvmx_agl_gmx_rxx_ifg_s         cn52xxp1;
	struct cvmx_agl_gmx_rxx_ifg_s         cn56xx;
	struct cvmx_agl_gmx_rxx_ifg_s         cn56xxp1;
	struct cvmx_agl_gmx_rxx_ifg_s         cn61xx;
	struct cvmx_agl_gmx_rxx_ifg_s         cn63xx;
	struct cvmx_agl_gmx_rxx_ifg_s         cn63xxp1;
	struct cvmx_agl_gmx_rxx_ifg_s         cn66xx;
	struct cvmx_agl_gmx_rxx_ifg_s         cn68xx;
	struct cvmx_agl_gmx_rxx_ifg_s         cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_ifg cvmx_agl_gmx_rxx_ifg_t;

/**
 * cvmx_agl_gmx_rx#_int_en
 *
 * AGL_GMX_RX_INT_EN = Interrupt Enable
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_int_en {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex             |             NS */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed              |             NS */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus             |             NS */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< Packet reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble)              |             NS */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< Carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< MII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t reserved_1_1                 : 1;
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_rxx_int_en_cn52xx cn52xxp1;
	struct cvmx_agl_gmx_rxx_int_en_cn52xx cn56xx;
	struct cvmx_agl_gmx_rxx_int_en_cn52xx cn56xxp1;
	struct cvmx_agl_gmx_rxx_int_en_s      cn61xx;
	struct cvmx_agl_gmx_rxx_int_en_s      cn63xx;
	struct cvmx_agl_gmx_rxx_int_en_s      cn63xxp1;
	struct cvmx_agl_gmx_rxx_int_en_s      cn66xx;
	struct cvmx_agl_gmx_rxx_int_en_s      cn68xx;
	struct cvmx_agl_gmx_rxx_int_en_s      cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_int_en cvmx_agl_gmx_rxx_int_en_t;

/**
 * cvmx_agl_gmx_rx#_int_reg
 *
 * AGL_GMX_RX_INT_REG = Interrupt Register
 *
 *
 * Notes:
 * (1) exceptions will only be raised to the control processor if the
 *     corresponding bit in the AGL_GMX_RX_INT_EN register is set.
 *
 * (2) exception conditions 10:0 can also set the rcv/opcode in the received
 *     packet's workQ entry.  The AGL_GMX_RX_FRM_CHK register provides a bit mask
 *     for configuring which conditions set the error.
 *
 * (3) in half duplex operation, the expectation is that collisions will appear
 *     as MINERRs.
 *
 * (4) JABBER - An RX Jabber error indicates that a packet was received which
 *              is longer than the maximum allowed packet as defined by the
 *              system.  GMX will truncate the packet at the JABBER count.
 *              Failure to do so could lead to system instabilty.
 *
 * (6) MAXERR - for untagged frames, the total frame DA+SA+TL+DATA+PAD+FCS >
 *              AGL_GMX_RX_FRM_MAX.  For tagged frames, DA+SA+VLAN+TL+DATA+PAD+FCS
 *              > AGL_GMX_RX_FRM_MAX + 4*VLAN_VAL + 4*VLAN_STACKED.
 *
 * (7) MINERR - total frame DA+SA+TL+DATA+PAD+FCS < AGL_GMX_RX_FRM_MIN.
 *
 * (8) ALNERR - Indicates that the packet received was not an integer number of
 *              bytes.  If FCS checking is enabled, ALNERR will only assert if
 *              the FCS is bad.  If FCS checking is disabled, ALNERR will
 *              assert in all non-integer frame cases.
 *
 * (9) Collisions - Collisions can only occur in half-duplex mode.  A collision
 *                  is assumed by the receiver when the received
 *                  frame < AGL_GMX_RX_FRM_MIN - this is normally a MINERR
 *
 * (A) LENERR - Length errors occur when the received packet does not match the
 *              length field.  LENERR is only checked for packets between 64
 *              and 1500 bytes.  For untagged frames, the length must exact
 *              match.  For tagged frames the length or length+4 must match.
 *
 * (B) PCTERR - checks that the frame begins with a valid PREAMBLE sequence.
 *              Does not check the number of PREAMBLE cycles.
 *
 * (C) OVRERR -
 *
 *              OVRERR is an architectural assertion check internal to GMX to
 *              make sure no assumption was violated.  In a correctly operating
 *              system, this interrupt can never fire.
 *
 *              GMX has an internal arbiter which selects which of 4 ports to
 *              buffer in the main RX FIFO.  If we normally buffer 8 bytes,
 *              then each port will typically push a tick every 8 cycles - if
 *              the packet interface is going as fast as possible.  If there
 *              are four ports, they push every two cycles.  So that's the
 *              assumption.  That the inbound module will always be able to
 *              consume the tick before another is produced.  If that doesn't
 *              happen - that's when OVRERR will assert.
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_int_reg {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RGMII inbound LinkDuplex             |             NS */
	uint64_t phy_spd                      : 1;  /**< Change in the RGMII inbound LinkSpeed              |             NS */
	uint64_t phy_link                     : 1;  /**< Change in the RGMII inbound LinkStatus             |             NS */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< Packet reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble)              |             NS */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Packet Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< Carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< MII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with MII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t reserved_1_1                 : 1;
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_rxx_int_reg_cn52xx cn52xxp1;
	struct cvmx_agl_gmx_rxx_int_reg_cn52xx cn56xx;
	struct cvmx_agl_gmx_rxx_int_reg_cn52xx cn56xxp1;
	struct cvmx_agl_gmx_rxx_int_reg_s     cn61xx;
	struct cvmx_agl_gmx_rxx_int_reg_s     cn63xx;
	struct cvmx_agl_gmx_rxx_int_reg_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_int_reg_s     cn66xx;
	struct cvmx_agl_gmx_rxx_int_reg_s     cn68xx;
	struct cvmx_agl_gmx_rxx_int_reg_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_int_reg cvmx_agl_gmx_rxx_int_reg_t;

/**
 * cvmx_agl_gmx_rx#_jabber
 *
 * AGL_GMX_RX_JABBER = The max size packet after which GMX will truncate
 *
 *
 * Notes:
 * CNT must be 8-byte aligned such that CNT[2:0] == 0
 *
 *   The packet that will be sent to the packet input logic will have an
 *   additionl 8 bytes if AGL_GMX_RX_FRM_CTL[PRE_CHK] is set and
 *   AGL_GMX_RX_FRM_CTL[PRE_STRP] is clear.  The max packet that will be sent is
 *   defined as...
 *
 *        max_sized_packet = AGL_GMX_RX_JABBER[CNT]+((AGL_GMX_RX_FRM_CTL[PRE_CHK] & !AGL_GMX_RX_FRM_CTL[PRE_STRP])*8)
 *
 *   Be sure the CNT field value is at least as large as the
 *   AGL_GMX_RX_FRM_MAX[LEN] value. Failure to meet this constraint will cause
 *   packets that are within the AGL_GMX_RX_FRM_MAX[LEN] length to be rejected
 *   because they exceed the CNT limit.
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_jabber {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_jabber_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t cnt                          : 16; /**< Byte count for jabber check
                                                         Failing packets set the JABBER interrupt and are
                                                         optionally sent with opcode==JABBER
                                                         GMX will truncate the packet to CNT bytes
                                                         CNT >= AGL_GMX_RX_FRM_MAX[LEN] */
#else
	uint64_t cnt                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_jabber_s      cn52xx;
	struct cvmx_agl_gmx_rxx_jabber_s      cn52xxp1;
	struct cvmx_agl_gmx_rxx_jabber_s      cn56xx;
	struct cvmx_agl_gmx_rxx_jabber_s      cn56xxp1;
	struct cvmx_agl_gmx_rxx_jabber_s      cn61xx;
	struct cvmx_agl_gmx_rxx_jabber_s      cn63xx;
	struct cvmx_agl_gmx_rxx_jabber_s      cn63xxp1;
	struct cvmx_agl_gmx_rxx_jabber_s      cn66xx;
	struct cvmx_agl_gmx_rxx_jabber_s      cn68xx;
	struct cvmx_agl_gmx_rxx_jabber_s      cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_jabber cvmx_agl_gmx_rxx_jabber_t;

/**
 * cvmx_agl_gmx_rx#_pause_drop_time
 *
 * AGL_GMX_RX_PAUSE_DROP_TIME = The TIME field in a PAUSE Packet which was dropped due to GMX RX FIFO full condition
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_pause_drop_time {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t status                       : 16; /**< Time extracted from the dropped PAUSE packet */
#else
	uint64_t status                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn52xx;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn56xx;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn61xx;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn63xx;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn66xx;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn68xx;
	struct cvmx_agl_gmx_rxx_pause_drop_time_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_pause_drop_time cvmx_agl_gmx_rxx_pause_drop_time_t;

/**
 * cvmx_agl_gmx_rx#_rx_inbnd
 *
 * AGL_GMX_RX_INBND = RGMII InBand Link Status
 *
 *
 * Notes:
 * These fields are only valid if the attached PHY is operating in RGMII mode
 * and supports the optional in-band status (see section 3.4.1 of the RGMII
 * specification, version 1.3 for more information).
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_rx_inbnd {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t duplex                       : 1;  /**< RGMII Inbound LinkDuplex                           |             NS
                                                         0=half-duplex
                                                         1=full-duplex */
	uint64_t speed                        : 2;  /**< RGMII Inbound LinkSpeed                            |             NS
                                                         00=2.5MHz
                                                         01=25MHz
                                                         10=125MHz
                                                         11=Reserved */
	uint64_t status                       : 1;  /**< RGMII Inbound LinkStatus                           |             NS
                                                         0=down
                                                         1=up */
#else
	uint64_t status                       : 1;
	uint64_t speed                        : 2;
	uint64_t duplex                       : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s    cn61xx;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s    cn63xx;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s    cn63xxp1;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s    cn66xx;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s    cn68xx;
	struct cvmx_agl_gmx_rxx_rx_inbnd_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_rx_inbnd cvmx_agl_gmx_rxx_rx_inbnd_t;

/**
 * cvmx_agl_gmx_rx#_stats_ctl
 *
 * AGL_GMX_RX_STATS_CTL = RX Stats Control register
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rxx_stats_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t rd_clr                       : 1;  /**< RX Stats registers will clear on reads */
#else
	uint64_t rd_clr                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn52xx;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn56xx;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn61xx;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn63xx;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn66xx;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn68xx;
	struct cvmx_agl_gmx_rxx_stats_ctl_s   cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_ctl cvmx_agl_gmx_rxx_stats_ctl_t;

/**
 * cvmx_agl_gmx_rx#_stats_octs
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_octs {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of received good packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn52xx;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn56xx;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn61xx;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn63xx;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn66xx;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn68xx;
	struct cvmx_agl_gmx_rxx_stats_octs_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_octs cvmx_agl_gmx_rxx_stats_octs_t;

/**
 * cvmx_agl_gmx_rx#_stats_octs_ctl
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_octs_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of received pause packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_octs_ctl_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_octs_ctl cvmx_agl_gmx_rxx_stats_octs_ctl_t;

/**
 * cvmx_agl_gmx_rx#_stats_octs_dmac
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_octs_dmac {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of filtered dmac packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_octs_dmac_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_octs_dmac cvmx_agl_gmx_rxx_stats_octs_dmac_t;

/**
 * cvmx_agl_gmx_rx#_stats_octs_drp
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_octs_drp {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of dropped packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_octs_drp_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_octs_drp cvmx_agl_gmx_rxx_stats_octs_drp_t;

/**
 * cvmx_agl_gmx_rx#_stats_pkts
 *
 * AGL_GMX_RX_STATS_PKTS
 *
 * Count of good received packets - packets that are not recognized as PAUSE
 * packets, dropped due the DMAC filter, dropped due FIFO full status, or
 * have any other OPCODE (FCS, Length, etc).
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_pkts {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of received good packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn52xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn56xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn61xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn63xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn66xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn68xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_pkts cvmx_agl_gmx_rxx_stats_pkts_t;

/**
 * cvmx_agl_gmx_rx#_stats_pkts_bad
 *
 * AGL_GMX_RX_STATS_PKTS_BAD
 *
 * Count of all packets received with some error that were not dropped
 * either due to the dmac filter or lack of room in the receive FIFO.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_pkts_bad {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of bad packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_bad_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_pkts_bad cvmx_agl_gmx_rxx_stats_pkts_bad_t;

/**
 * cvmx_agl_gmx_rx#_stats_pkts_ctl
 *
 * AGL_GMX_RX_STATS_PKTS_CTL
 *
 * Count of all packets received that were recognized as Flow Control or
 * PAUSE packets.  PAUSE packets with any kind of error are counted in
 * AGL_GMX_RX_STATS_PKTS_BAD.  Pause packets can be optionally dropped or
 * forwarded based on the AGL_GMX_RX_FRM_CTL[CTL_DRP] bit.  This count
 * increments regardless of whether the packet is dropped.  Pause packets
 * will never be counted in AGL_GMX_RX_STATS_PKTS.  Packets dropped due the dmac
 * filter will be counted in AGL_GMX_RX_STATS_PKTS_DMAC and not here.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_pkts_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of received pause packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_ctl_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_pkts_ctl cvmx_agl_gmx_rxx_stats_pkts_ctl_t;

/**
 * cvmx_agl_gmx_rx#_stats_pkts_dmac
 *
 * AGL_GMX_RX_STATS_PKTS_DMAC
 *
 * Count of all packets received that were dropped by the dmac filter.
 * Packets that match the DMAC will be dropped and counted here regardless
 * of if they were bad packets.  These packets will never be counted in
 * AGL_GMX_RX_STATS_PKTS.
 *
 * Some packets that were not able to satisify the DECISION_CNT may not
 * actually be dropped by Octeon, but they will be counted here as if they
 * were dropped.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_pkts_dmac {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of filtered dmac packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_dmac_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_pkts_dmac cvmx_agl_gmx_rxx_stats_pkts_dmac_t;

/**
 * cvmx_agl_gmx_rx#_stats_pkts_drp
 *
 * AGL_GMX_RX_STATS_PKTS_DRP
 *
 * Count of all packets received that were dropped due to a full receive
 * FIFO.  This counts good and bad packets received - all packets dropped by
 * the FIFO.  It does not count packets dropped by the dmac or pause packet
 * filters.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_stats_pkts_drp {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of dropped packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn52xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn52xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn56xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn56xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn61xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn63xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn63xxp1;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn66xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn68xx;
	struct cvmx_agl_gmx_rxx_stats_pkts_drp_s cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_stats_pkts_drp cvmx_agl_gmx_rxx_stats_pkts_drp_t;

/**
 * cvmx_agl_gmx_rx#_udd_skp
 *
 * AGL_GMX_RX_UDD_SKP = Amount of User-defined data before the start of the L2 data
 *
 *
 * Notes:
 * (1) The skip bytes are part of the packet and will be sent down the NCB
 *     packet interface and will be handled by PKI.
 *
 * (2) The system can determine if the UDD bytes are included in the FCS check
 *     by using the FCSSEL field - if the FCS check is enabled.
 *
 * (3) Assume that the preamble/sfd is always at the start of the frame - even
 *     before UDD bytes.  In most cases, there will be no preamble in these
 *     cases since it will be MII to MII communication without a PHY
 *     involved.
 *
 * (4) We can still do address filtering and control packet filtering is the
 *     user desires.
 *
 * (5) UDD_SKP must be 0 in half-duplex operation unless
 *     AGL_GMX_RX_FRM_CTL[PRE_CHK] is clear.  If AGL_GMX_RX_FRM_CTL[PRE_CHK] is set,
 *     then UDD_SKP will normally be 8.
 *
 * (6) In all cases, the UDD bytes will be sent down the packet interface as
 *     part of the packet.  The UDD bytes are never stripped from the actual
 *     packet.
 *
 * (7) If LEN != 0, then AGL_GMX_RX_FRM_CHK[LENERR] will be disabled and AGL_GMX_RX_INT_REG[LENERR] will be zero
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rxx_udd_skp {
	uint64_t u64;
	struct cvmx_agl_gmx_rxx_udd_skp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t fcssel                       : 1;  /**< Include the skip bytes in the FCS calculation
                                                         0 = all skip bytes are included in FCS
                                                         1 = the skip bytes are not included in FCS */
	uint64_t reserved_7_7                 : 1;
	uint64_t len                          : 7;  /**< Amount of User-defined data before the start of
                                                         the L2 data.  Zero means L2 comes first.
                                                         Max value is 64. */
#else
	uint64_t len                          : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t fcssel                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn52xx;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn52xxp1;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn56xx;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn56xxp1;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn61xx;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn63xx;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn63xxp1;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn66xx;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn68xx;
	struct cvmx_agl_gmx_rxx_udd_skp_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rxx_udd_skp cvmx_agl_gmx_rxx_udd_skp_t;

/**
 * cvmx_agl_gmx_rx_bp_drop#
 *
 * AGL_GMX_RX_BP_DROP = FIFO mark for packet drop
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rx_bp_dropx {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_bp_dropx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t mark                         : 6;  /**< Number of 8B ticks to reserve in the RX FIFO.
                                                         When the FIFO exceeds this count, packets will
                                                         be dropped and not buffered.
                                                         MARK should typically be programmed to 2.
                                                         Failure to program correctly can lead to system
                                                         instability. */
#else
	uint64_t mark                         : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn52xx;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn52xxp1;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn56xx;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn56xxp1;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn61xx;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn63xx;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn63xxp1;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn66xx;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn68xx;
	struct cvmx_agl_gmx_rx_bp_dropx_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rx_bp_dropx cvmx_agl_gmx_rx_bp_dropx_t;

/**
 * cvmx_agl_gmx_rx_bp_off#
 *
 * AGL_GMX_RX_BP_OFF = Lowater mark for packet drop
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rx_bp_offx {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_bp_offx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t mark                         : 6;  /**< Water mark (8B ticks) to deassert backpressure */
#else
	uint64_t mark                         : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn52xx;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn52xxp1;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn56xx;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn56xxp1;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn61xx;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn63xx;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn63xxp1;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn66xx;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn68xx;
	struct cvmx_agl_gmx_rx_bp_offx_s      cn68xxp1;
};
typedef union cvmx_agl_gmx_rx_bp_offx cvmx_agl_gmx_rx_bp_offx_t;

/**
 * cvmx_agl_gmx_rx_bp_on#
 *
 * AGL_GMX_RX_BP_ON = Hiwater mark for port/interface backpressure
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_rx_bp_onx {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_bp_onx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t mark                         : 9;  /**< Hiwater mark (8B ticks) for backpressure. */
#else
	uint64_t mark                         : 9;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn52xx;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn52xxp1;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn56xx;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn56xxp1;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn61xx;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn63xx;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn63xxp1;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn66xx;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn68xx;
	struct cvmx_agl_gmx_rx_bp_onx_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_rx_bp_onx cvmx_agl_gmx_rx_bp_onx_t;

/**
 * cvmx_agl_gmx_rx_prt_info
 *
 * AGL_GMX_RX_PRT_INFO = state information for the ports
 *
 *
 * Notes:
 * COMMIT[0], DROP[0] will be reset when MIX0_CTL[RESET] is set to 1.
 * COMMIT[1], DROP[1] will be reset when MIX1_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rx_prt_info {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_prt_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t drop                         : 2;  /**< Port indication that data was dropped */
	uint64_t reserved_2_15                : 14;
	uint64_t commit                       : 2;  /**< Port indication that SOP was accepted */
#else
	uint64_t commit                       : 2;
	uint64_t reserved_2_15                : 14;
	uint64_t drop                         : 2;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_agl_gmx_rx_prt_info_s     cn52xx;
	struct cvmx_agl_gmx_rx_prt_info_s     cn52xxp1;
	struct cvmx_agl_gmx_rx_prt_info_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t drop                         : 1;  /**< Port indication that data was dropped */
	uint64_t reserved_1_15                : 15;
	uint64_t commit                       : 1;  /**< Port indication that SOP was accepted */
#else
	uint64_t commit                       : 1;
	uint64_t reserved_1_15                : 15;
	uint64_t drop                         : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_rx_prt_info_cn56xx cn56xxp1;
	struct cvmx_agl_gmx_rx_prt_info_s     cn61xx;
	struct cvmx_agl_gmx_rx_prt_info_s     cn63xx;
	struct cvmx_agl_gmx_rx_prt_info_s     cn63xxp1;
	struct cvmx_agl_gmx_rx_prt_info_s     cn66xx;
	struct cvmx_agl_gmx_rx_prt_info_s     cn68xx;
	struct cvmx_agl_gmx_rx_prt_info_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_rx_prt_info cvmx_agl_gmx_rx_prt_info_t;

/**
 * cvmx_agl_gmx_rx_tx_status
 *
 * AGL_GMX_RX_TX_STATUS = GMX RX/TX Status
 *
 *
 * Notes:
 * RX[0], TX[0] will be reset when MIX0_CTL[RESET] is set to 1.
 * RX[1], TX[1] will be reset when MIX1_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_rx_tx_status {
	uint64_t u64;
	struct cvmx_agl_gmx_rx_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t tx                           : 2;  /**< Transmit data since last read */
	uint64_t reserved_2_3                 : 2;
	uint64_t rx                           : 2;  /**< Receive data since last read */
#else
	uint64_t rx                           : 2;
	uint64_t reserved_2_3                 : 2;
	uint64_t tx                           : 2;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_agl_gmx_rx_tx_status_s    cn52xx;
	struct cvmx_agl_gmx_rx_tx_status_s    cn52xxp1;
	struct cvmx_agl_gmx_rx_tx_status_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t tx                           : 1;  /**< Transmit data since last read */
	uint64_t reserved_1_3                 : 3;
	uint64_t rx                           : 1;  /**< Receive data since last read */
#else
	uint64_t rx                           : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t tx                           : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_rx_tx_status_cn56xx cn56xxp1;
	struct cvmx_agl_gmx_rx_tx_status_s    cn61xx;
	struct cvmx_agl_gmx_rx_tx_status_s    cn63xx;
	struct cvmx_agl_gmx_rx_tx_status_s    cn63xxp1;
	struct cvmx_agl_gmx_rx_tx_status_s    cn66xx;
	struct cvmx_agl_gmx_rx_tx_status_s    cn68xx;
	struct cvmx_agl_gmx_rx_tx_status_s    cn68xxp1;
};
typedef union cvmx_agl_gmx_rx_tx_status cvmx_agl_gmx_rx_tx_status_t;

/**
 * cvmx_agl_gmx_smac#
 *
 * AGL_GMX_SMAC = Packet SMAC
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_smacx {
	uint64_t u64;
	struct cvmx_agl_gmx_smacx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t smac                         : 48; /**< The SMAC field is used for generating and
                                                         accepting Control Pause packets */
#else
	uint64_t smac                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_smacx_s           cn52xx;
	struct cvmx_agl_gmx_smacx_s           cn52xxp1;
	struct cvmx_agl_gmx_smacx_s           cn56xx;
	struct cvmx_agl_gmx_smacx_s           cn56xxp1;
	struct cvmx_agl_gmx_smacx_s           cn61xx;
	struct cvmx_agl_gmx_smacx_s           cn63xx;
	struct cvmx_agl_gmx_smacx_s           cn63xxp1;
	struct cvmx_agl_gmx_smacx_s           cn66xx;
	struct cvmx_agl_gmx_smacx_s           cn68xx;
	struct cvmx_agl_gmx_smacx_s           cn68xxp1;
};
typedef union cvmx_agl_gmx_smacx cvmx_agl_gmx_smacx_t;

/**
 * cvmx_agl_gmx_stat_bp
 *
 * AGL_GMX_STAT_BP = Number of cycles that the TX/Stats block has help up operation
 *
 *
 * Notes:
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 *
 *
 *
 * It has no relationship with the TX FIFO per se.  The TX engine sends packets
 * from PKO and upon completion, sends a command to the TX stats block for an
 * update based on the packet size.  The stats operation can take a few cycles -
 * normally not enough to be visible considering the 64B min packet size that is
 * ethernet convention.
 *
 * In the rare case in which SW attempted to schedule really, really, small packets
 * or the sclk (6xxx) is running ass-slow, then the stats updates may not happen in
 * real time and can back up the TX engine.
 *
 * This counter is the number of cycles in which the TX engine was stalled.  In
 * normal operation, it should always be zeros.
 */
union cvmx_agl_gmx_stat_bp {
	uint64_t u64;
	struct cvmx_agl_gmx_stat_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t bp                           : 1;  /**< Current TX stats BP state
                                                         When the TX stats machine cannot update the stats
                                                         registers quickly enough, the machine has the
                                                         ability to BP TX datapath.  This is a rare event
                                                         and will not occur in normal operation.
                                                         0 = no backpressure is applied
                                                         1 = backpressure is applied to TX datapath to
                                                             allow stat update operations to complete */
	uint64_t cnt                          : 16; /**< Number of cycles that BP has been asserted
                                                         Saturating counter */
#else
	uint64_t cnt                          : 16;
	uint64_t bp                           : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_agl_gmx_stat_bp_s         cn52xx;
	struct cvmx_agl_gmx_stat_bp_s         cn52xxp1;
	struct cvmx_agl_gmx_stat_bp_s         cn56xx;
	struct cvmx_agl_gmx_stat_bp_s         cn56xxp1;
	struct cvmx_agl_gmx_stat_bp_s         cn61xx;
	struct cvmx_agl_gmx_stat_bp_s         cn63xx;
	struct cvmx_agl_gmx_stat_bp_s         cn63xxp1;
	struct cvmx_agl_gmx_stat_bp_s         cn66xx;
	struct cvmx_agl_gmx_stat_bp_s         cn68xx;
	struct cvmx_agl_gmx_stat_bp_s         cn68xxp1;
};
typedef union cvmx_agl_gmx_stat_bp cvmx_agl_gmx_stat_bp_t;

/**
 * cvmx_agl_gmx_tx#_append
 *
 * AGL_GMX_TX_APPEND = Packet TX Append Control
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_append {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_append_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t force_fcs                    : 1;  /**< Append the Ethernet FCS on each pause packet
                                                         when FCS is clear.  Pause packets are normally
                                                         padded to 60 bytes.  If
                                                         AGL_GMX_TX_MIN_PKT[MIN_SIZE] exceeds 59, then
                                                         FORCE_FCS will not be used. */
	uint64_t fcs                          : 1;  /**< Append the Ethernet FCS on each packet */
	uint64_t pad                          : 1;  /**< Append PAD bytes such that min sized */
	uint64_t preamble                     : 1;  /**< Prepend the Ethernet preamble on each transfer */
#else
	uint64_t preamble                     : 1;
	uint64_t pad                          : 1;
	uint64_t fcs                          : 1;
	uint64_t force_fcs                    : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_agl_gmx_txx_append_s      cn52xx;
	struct cvmx_agl_gmx_txx_append_s      cn52xxp1;
	struct cvmx_agl_gmx_txx_append_s      cn56xx;
	struct cvmx_agl_gmx_txx_append_s      cn56xxp1;
	struct cvmx_agl_gmx_txx_append_s      cn61xx;
	struct cvmx_agl_gmx_txx_append_s      cn63xx;
	struct cvmx_agl_gmx_txx_append_s      cn63xxp1;
	struct cvmx_agl_gmx_txx_append_s      cn66xx;
	struct cvmx_agl_gmx_txx_append_s      cn68xx;
	struct cvmx_agl_gmx_txx_append_s      cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_append cvmx_agl_gmx_txx_append_t;

/**
 * cvmx_agl_gmx_tx#_clk
 *
 * AGL_GMX_TX_CLK = RGMII TX Clock Generation Register
 *
 *
 * Notes:
 * Normal Programming Values:
 *  (1) RGMII, 1000Mbs   (AGL_GMX_PRT_CFG[SPEED]==1), CLK_CNT == 1
 *  (2) RGMII, 10/100Mbs (AGL_GMX_PRT_CFG[SPEED]==0), CLK_CNT == 50/5
 *  (3) MII,   10/100Mbs (AGL_GMX_PRT_CFG[SPEED]==0), CLK_CNT == 1
 *
 * RGMII Example:
 *  Given a 125MHz PLL reference clock...
 *   CLK_CNT ==  1 ==> 125.0MHz TXC clock period (8ns* 1)
 *   CLK_CNT ==  5 ==>  25.0MHz TXC clock period (8ns* 5)
 *   CLK_CNT == 50 ==>   2.5MHz TXC clock period (8ns*50)
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_clk {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_clk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t clk_cnt                      : 6;  /**< Controls the RGMII TXC frequency                   |             NS
                                                         TXC(period) =
                                                          rgm_ref_clk(period)*CLK_CNT */
#else
	uint64_t clk_cnt                      : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_agl_gmx_txx_clk_s         cn61xx;
	struct cvmx_agl_gmx_txx_clk_s         cn63xx;
	struct cvmx_agl_gmx_txx_clk_s         cn63xxp1;
	struct cvmx_agl_gmx_txx_clk_s         cn66xx;
	struct cvmx_agl_gmx_txx_clk_s         cn68xx;
	struct cvmx_agl_gmx_txx_clk_s         cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_clk cvmx_agl_gmx_txx_clk_t;

/**
 * cvmx_agl_gmx_tx#_ctl
 *
 * AGL_GMX_TX_CTL = TX Control register
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t xsdef_en                     : 1;  /**< Enables the excessive deferral check for stats
                                                         and interrupts */
	uint64_t xscol_en                     : 1;  /**< Enables the excessive collision check for stats
                                                         and interrupts */
#else
	uint64_t xscol_en                     : 1;
	uint64_t xsdef_en                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_agl_gmx_txx_ctl_s         cn52xx;
	struct cvmx_agl_gmx_txx_ctl_s         cn52xxp1;
	struct cvmx_agl_gmx_txx_ctl_s         cn56xx;
	struct cvmx_agl_gmx_txx_ctl_s         cn56xxp1;
	struct cvmx_agl_gmx_txx_ctl_s         cn61xx;
	struct cvmx_agl_gmx_txx_ctl_s         cn63xx;
	struct cvmx_agl_gmx_txx_ctl_s         cn63xxp1;
	struct cvmx_agl_gmx_txx_ctl_s         cn66xx;
	struct cvmx_agl_gmx_txx_ctl_s         cn68xx;
	struct cvmx_agl_gmx_txx_ctl_s         cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_ctl cvmx_agl_gmx_txx_ctl_t;

/**
 * cvmx_agl_gmx_tx#_min_pkt
 *
 * AGL_GMX_TX_MIN_PKT = Packet TX Min Size Packet (PAD upto min size)
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_min_pkt {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_min_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t min_size                     : 8;  /**< Min frame in bytes before the FCS is applied
                                                         Padding is only appened when
                                                         AGL_GMX_TX_APPEND[PAD] for the coresponding packet
                                                         port is set. Packets will be padded to
                                                         MIN_SIZE+1 The reset value will pad to 60 bytes. */
#else
	uint64_t min_size                     : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn52xx;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn52xxp1;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn56xx;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn56xxp1;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn61xx;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn63xx;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn63xxp1;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn66xx;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn68xx;
	struct cvmx_agl_gmx_txx_min_pkt_s     cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_min_pkt cvmx_agl_gmx_txx_min_pkt_t;

/**
 * cvmx_agl_gmx_tx#_pause_pkt_interval
 *
 * AGL_GMX_TX_PAUSE_PKT_INTERVAL = Packet TX Pause Packet transmission interval - how often PAUSE packets will be sent
 *
 *
 * Notes:
 * Choosing proper values of AGL_GMX_TX_PAUSE_PKT_TIME[TIME] and
 * AGL_GMX_TX_PAUSE_PKT_INTERVAL[INTERVAL] can be challenging to the system
 * designer.  It is suggested that TIME be much greater than INTERVAL and
 * AGL_GMX_TX_PAUSE_ZERO[SEND] be set.  This allows a periodic refresh of the PAUSE
 * count and then when the backpressure condition is lifted, a PAUSE packet
 * with TIME==0 will be sent indicating that Octane is ready for additional
 * data.
 *
 * If the system chooses to not set AGL_GMX_TX_PAUSE_ZERO[SEND], then it is
 * suggested that TIME and INTERVAL are programmed such that they satisify the
 * following rule...
 *
 *    INTERVAL <= TIME - (largest_pkt_size + IFG + pause_pkt_size)
 *
 * where largest_pkt_size is that largest packet that the system can send
 * (normally 1518B), IFG is the interframe gap and pause_pkt_size is the size
 * of the PAUSE packet (normally 64B).
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_pause_pkt_interval {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t interval                     : 16; /**< Arbitrate for a pause packet every (INTERVAL*512)
                                                         bit-times.
                                                         Normally, 0 < INTERVAL < AGL_GMX_TX_PAUSE_PKT_TIME
                                                         INTERVAL=0, will only send a single PAUSE packet
                                                         for each backpressure event */
#else
	uint64_t interval                     : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn52xx;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn52xxp1;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn56xx;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn56xxp1;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn61xx;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn63xx;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn63xxp1;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn66xx;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn68xx;
	struct cvmx_agl_gmx_txx_pause_pkt_interval_s cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_pause_pkt_interval cvmx_agl_gmx_txx_pause_pkt_interval_t;

/**
 * cvmx_agl_gmx_tx#_pause_pkt_time
 *
 * AGL_GMX_TX_PAUSE_PKT_TIME = Packet TX Pause Packet pause_time field
 *
 *
 * Notes:
 * Choosing proper values of AGL_GMX_TX_PAUSE_PKT_TIME[TIME] and
 * AGL_GMX_TX_PAUSE_PKT_INTERVAL[INTERVAL] can be challenging to the system
 * designer.  It is suggested that TIME be much greater than INTERVAL and
 * AGL_GMX_TX_PAUSE_ZERO[SEND] be set.  This allows a periodic refresh of the PAUSE
 * count and then when the backpressure condition is lifted, a PAUSE packet
 * with TIME==0 will be sent indicating that Octane is ready for additional
 * data.
 *
 * If the system chooses to not set AGL_GMX_TX_PAUSE_ZERO[SEND], then it is
 * suggested that TIME and INTERVAL are programmed such that they satisify the
 * following rule...
 *
 *    INTERVAL <= TIME - (largest_pkt_size + IFG + pause_pkt_size)
 *
 * where largest_pkt_size is that largest packet that the system can send
 * (normally 1518B), IFG is the interframe gap and pause_pkt_size is the size
 * of the PAUSE packet (normally 64B).
 *
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_pause_pkt_time {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t time                         : 16; /**< The pause_time field placed is outbnd pause pkts
                                                         pause_time is in 512 bit-times
                                                         Normally, TIME > AGL_GMX_TX_PAUSE_PKT_INTERVAL */
#else
	uint64_t time                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn52xx;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn52xxp1;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn56xx;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn56xxp1;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn61xx;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn63xx;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn63xxp1;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn66xx;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn68xx;
	struct cvmx_agl_gmx_txx_pause_pkt_time_s cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_pause_pkt_time cvmx_agl_gmx_txx_pause_pkt_time_t;

/**
 * cvmx_agl_gmx_tx#_pause_togo
 *
 * AGL_GMX_TX_PAUSE_TOGO = Packet TX Amount of time remaining to backpressure
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_pause_togo {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_togo_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t time                         : 16; /**< Amount of time remaining to backpressure */
#else
	uint64_t time                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn52xx;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn52xxp1;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn56xx;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn56xxp1;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn61xx;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn63xx;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn63xxp1;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn66xx;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn68xx;
	struct cvmx_agl_gmx_txx_pause_togo_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_pause_togo cvmx_agl_gmx_txx_pause_togo_t;

/**
 * cvmx_agl_gmx_tx#_pause_zero
 *
 * AGL_GMX_TX_PAUSE_ZERO = Packet TX Amount of time remaining to backpressure
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_pause_zero {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_pause_zero_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t send                         : 1;  /**< When backpressure condition clear, send PAUSE
                                                         packet with pause_time of zero to enable the
                                                         channel */
#else
	uint64_t send                         : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn52xx;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn52xxp1;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn56xx;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn56xxp1;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn61xx;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn63xx;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn63xxp1;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn66xx;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn68xx;
	struct cvmx_agl_gmx_txx_pause_zero_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_pause_zero cvmx_agl_gmx_txx_pause_zero_t;

/**
 * cvmx_agl_gmx_tx#_soft_pause
 *
 * AGL_GMX_TX_SOFT_PAUSE = Packet TX Software Pause
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_soft_pause {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_soft_pause_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t time                         : 16; /**< Back off the TX bus for (TIME*512) bit-times
                                                         for full-duplex operation only */
#else
	uint64_t time                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn52xx;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn52xxp1;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn56xx;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn56xxp1;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn61xx;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn63xx;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn63xxp1;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn66xx;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn68xx;
	struct cvmx_agl_gmx_txx_soft_pause_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_soft_pause cvmx_agl_gmx_txx_soft_pause_t;

/**
 * cvmx_agl_gmx_tx#_stat0
 *
 * AGL_GMX_TX_STAT0 = AGL_GMX_TX_STATS_XSDEF / AGL_GMX_TX_STATS_XSCOL
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat0 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t xsdef                        : 32; /**< Number of packets dropped (never successfully
                                                         sent) due to excessive deferal */
	uint64_t xscol                        : 32; /**< Number of packets dropped (never successfully
                                                         sent) due to excessive collision.  Defined by
                                                         AGL_GMX_TX_COL_ATTEMPT[LIMIT]. */
#else
	uint64_t xscol                        : 32;
	uint64_t xsdef                        : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat0_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat0_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat0_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat0_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat0_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat0_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat0_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat0_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat0_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat0_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat0 cvmx_agl_gmx_txx_stat0_t;

/**
 * cvmx_agl_gmx_tx#_stat1
 *
 * AGL_GMX_TX_STAT1 = AGL_GMX_TX_STATS_SCOL  / AGL_GMX_TX_STATS_MCOL
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat1 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t scol                         : 32; /**< Number of packets sent with a single collision */
	uint64_t mcol                         : 32; /**< Number of packets sent with multiple collisions
                                                         but < AGL_GMX_TX_COL_ATTEMPT[LIMIT]. */
#else
	uint64_t mcol                         : 32;
	uint64_t scol                         : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat1_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat1_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat1_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat1_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat1_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat1_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat1_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat1_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat1_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat1_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat1 cvmx_agl_gmx_txx_stat1_t;

/**
 * cvmx_agl_gmx_tx#_stat2
 *
 * AGL_GMX_TX_STAT2 = AGL_GMX_TX_STATS_OCTS
 *
 *
 * Notes:
 * - Octect counts are the sum of all data transmitted on the wire including
 *   packet data, pad bytes, fcs bytes, pause bytes, and jam bytes.  The octect
 *   counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat2 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Number of total octets sent on the interface.
                                                         Does not count octets from frames that were
                                                         truncated due to collisions in halfdup mode. */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat2_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat2_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat2_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat2_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat2_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat2_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat2_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat2_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat2_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat2_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat2 cvmx_agl_gmx_txx_stat2_t;

/**
 * cvmx_agl_gmx_tx#_stat3
 *
 * AGL_GMX_TX_STAT3 = AGL_GMX_TX_STATS_PKTS
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat3 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pkts                         : 32; /**< Number of total frames sent on the interface.
                                                         Does not count frames that were truncated due to
                                                          collisions in halfdup mode. */
#else
	uint64_t pkts                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat3_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat3_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat3_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat3_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat3_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat3_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat3_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat3_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat3_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat3_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat3 cvmx_agl_gmx_txx_stat3_t;

/**
 * cvmx_agl_gmx_tx#_stat4
 *
 * AGL_GMX_TX_STAT4 = AGL_GMX_TX_STATS_HIST1 (64) / AGL_GMX_TX_STATS_HIST0 (<64)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat4 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist1                        : 32; /**< Number of packets sent with an octet count of 64. */
	uint64_t hist0                        : 32; /**< Number of packets sent with an octet count
                                                         of < 64. */
#else
	uint64_t hist0                        : 32;
	uint64_t hist1                        : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat4_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat4_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat4_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat4_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat4_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat4_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat4_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat4_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat4_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat4_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat4 cvmx_agl_gmx_txx_stat4_t;

/**
 * cvmx_agl_gmx_tx#_stat5
 *
 * AGL_GMX_TX_STAT5 = AGL_GMX_TX_STATS_HIST3 (128- 255) / AGL_GMX_TX_STATS_HIST2 (65- 127)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat5 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist3                        : 32; /**< Number of packets sent with an octet count of
                                                         128 - 255. */
	uint64_t hist2                        : 32; /**< Number of packets sent with an octet count of
                                                         65 - 127. */
#else
	uint64_t hist2                        : 32;
	uint64_t hist3                        : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat5_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat5_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat5_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat5_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat5_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat5_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat5_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat5_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat5_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat5_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat5 cvmx_agl_gmx_txx_stat5_t;

/**
 * cvmx_agl_gmx_tx#_stat6
 *
 * AGL_GMX_TX_STAT6 = AGL_GMX_TX_STATS_HIST5 (512-1023) / AGL_GMX_TX_STATS_HIST4 (256-511)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat6 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist5                        : 32; /**< Number of packets sent with an octet count of
                                                         512 - 1023. */
	uint64_t hist4                        : 32; /**< Number of packets sent with an octet count of
                                                         256 - 511. */
#else
	uint64_t hist4                        : 32;
	uint64_t hist5                        : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat6_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat6_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat6_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat6_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat6_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat6_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat6_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat6_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat6_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat6_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat6 cvmx_agl_gmx_txx_stat6_t;

/**
 * cvmx_agl_gmx_tx#_stat7
 *
 * AGL_GMX_TX_STAT7 = AGL_GMX_TX_STATS_HIST7 (1024-1518) / AGL_GMX_TX_STATS_HIST6 (>1518)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat7 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist7                        : 32; /**< Number of packets sent with an octet count
                                                         of > 1518. */
	uint64_t hist6                        : 32; /**< Number of packets sent with an octet count of
                                                         1024 - 1518. */
#else
	uint64_t hist6                        : 32;
	uint64_t hist7                        : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat7_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat7_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat7_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat7_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat7_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat7_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat7_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat7_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat7_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat7_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat7 cvmx_agl_gmx_txx_stat7_t;

/**
 * cvmx_agl_gmx_tx#_stat8
 *
 * AGL_GMX_TX_STAT8 = AGL_GMX_TX_STATS_MCST  / AGL_GMX_TX_STATS_BCST
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Note, GMX determines if the packet is MCST or BCST from the DMAC of the
 *   packet.  GMX assumes that the DMAC lies in the first 6 bytes of the packet
 *   as per the 802.3 frame definition.  If the system requires additional data
 *   before the L2 header, then the MCST and BCST counters may not reflect
 *   reality and should be ignored by software.
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat8 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat8_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mcst                         : 32; /**< Number of packets sent to multicast DMAC.
                                                         Does not include BCST packets. */
	uint64_t bcst                         : 32; /**< Number of packets sent to broadcast DMAC.
                                                         Does not include MCST packets. */
#else
	uint64_t bcst                         : 32;
	uint64_t mcst                         : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat8_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat8_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat8_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat8_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat8_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat8_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat8_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat8_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat8_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat8_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat8 cvmx_agl_gmx_txx_stat8_t;

/**
 * cvmx_agl_gmx_tx#_stat9
 *
 * AGL_GMX_TX_STAT9 = AGL_GMX_TX_STATS_UNDFLW / AGL_GMX_TX_STATS_CTL
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when AGL_GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Not reset when MIX*_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_txx_stat9 {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stat9_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t undflw                       : 32; /**< Number of underflow packets */
	uint64_t ctl                          : 32; /**< Number of Control packets (PAUSE flow control)
                                                         generated by GMX.  It does not include control
                                                         packets forwarded or generated by the PP's. */
#else
	uint64_t ctl                          : 32;
	uint64_t undflw                       : 32;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stat9_s       cn52xx;
	struct cvmx_agl_gmx_txx_stat9_s       cn52xxp1;
	struct cvmx_agl_gmx_txx_stat9_s       cn56xx;
	struct cvmx_agl_gmx_txx_stat9_s       cn56xxp1;
	struct cvmx_agl_gmx_txx_stat9_s       cn61xx;
	struct cvmx_agl_gmx_txx_stat9_s       cn63xx;
	struct cvmx_agl_gmx_txx_stat9_s       cn63xxp1;
	struct cvmx_agl_gmx_txx_stat9_s       cn66xx;
	struct cvmx_agl_gmx_txx_stat9_s       cn68xx;
	struct cvmx_agl_gmx_txx_stat9_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stat9 cvmx_agl_gmx_txx_stat9_t;

/**
 * cvmx_agl_gmx_tx#_stats_ctl
 *
 * AGL_GMX_TX_STATS_CTL = TX Stats Control register
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_stats_ctl {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t rd_clr                       : 1;  /**< Stats registers will clear on reads */
#else
	uint64_t rd_clr                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn52xx;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn52xxp1;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn56xx;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn56xxp1;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn61xx;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn63xx;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn63xxp1;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn66xx;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn68xx;
	struct cvmx_agl_gmx_txx_stats_ctl_s   cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_stats_ctl cvmx_agl_gmx_txx_stats_ctl_t;

/**
 * cvmx_agl_gmx_tx#_thresh
 *
 * AGL_GMX_TX_THRESH = Packet TX Threshold
 *
 *
 * Notes:
 * Additionally reset when MIX<prt>_CTL[RESET] is set to 1.
 *
 */
union cvmx_agl_gmx_txx_thresh {
	uint64_t u64;
	struct cvmx_agl_gmx_txx_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t cnt                          : 6;  /**< Number of 16B ticks to accumulate in the TX FIFO
                                                         before sending on the packet interface
                                                         This register should be large enough to prevent
                                                         underflow on the packet interface and must never
                                                         be set below 4.  This register cannot exceed the
                                                         the TX FIFO depth which is 128, 8B entries. */
#else
	uint64_t cnt                          : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_agl_gmx_txx_thresh_s      cn52xx;
	struct cvmx_agl_gmx_txx_thresh_s      cn52xxp1;
	struct cvmx_agl_gmx_txx_thresh_s      cn56xx;
	struct cvmx_agl_gmx_txx_thresh_s      cn56xxp1;
	struct cvmx_agl_gmx_txx_thresh_s      cn61xx;
	struct cvmx_agl_gmx_txx_thresh_s      cn63xx;
	struct cvmx_agl_gmx_txx_thresh_s      cn63xxp1;
	struct cvmx_agl_gmx_txx_thresh_s      cn66xx;
	struct cvmx_agl_gmx_txx_thresh_s      cn68xx;
	struct cvmx_agl_gmx_txx_thresh_s      cn68xxp1;
};
typedef union cvmx_agl_gmx_txx_thresh cvmx_agl_gmx_txx_thresh_t;

/**
 * cvmx_agl_gmx_tx_bp
 *
 * AGL_GMX_TX_BP = Packet TX BackPressure Register
 *
 *
 * Notes:
 * BP[0] will be reset when MIX0_CTL[RESET] is set to 1.
 * BP[1] will be reset when MIX1_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_tx_bp {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t bp                           : 2;  /**< Port BackPressure status
                                                         0=Port is available
                                                         1=Port should be back pressured */
#else
	uint64_t bp                           : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_agl_gmx_tx_bp_s           cn52xx;
	struct cvmx_agl_gmx_tx_bp_s           cn52xxp1;
	struct cvmx_agl_gmx_tx_bp_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t bp                           : 1;  /**< Port BackPressure status
                                                         0=Port is available
                                                         1=Port should be back pressured */
#else
	uint64_t bp                           : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_tx_bp_cn56xx      cn56xxp1;
	struct cvmx_agl_gmx_tx_bp_s           cn61xx;
	struct cvmx_agl_gmx_tx_bp_s           cn63xx;
	struct cvmx_agl_gmx_tx_bp_s           cn63xxp1;
	struct cvmx_agl_gmx_tx_bp_s           cn66xx;
	struct cvmx_agl_gmx_tx_bp_s           cn68xx;
	struct cvmx_agl_gmx_tx_bp_s           cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_bp cvmx_agl_gmx_tx_bp_t;

/**
 * cvmx_agl_gmx_tx_col_attempt
 *
 * AGL_GMX_TX_COL_ATTEMPT = Packet TX collision attempts before dropping frame
 *
 *
 * Notes:
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 *
 */
union cvmx_agl_gmx_tx_col_attempt {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_col_attempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t limit                        : 5;  /**< Collision Attempts */
#else
	uint64_t limit                        : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn52xx;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn52xxp1;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn56xx;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn56xxp1;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn61xx;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn63xx;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn63xxp1;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn66xx;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn68xx;
	struct cvmx_agl_gmx_tx_col_attempt_s  cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_col_attempt cvmx_agl_gmx_tx_col_attempt_t;

/**
 * cvmx_agl_gmx_tx_ifg
 *
 * Common
 *
 *
 * AGL_GMX_TX_IFG = Packet TX Interframe Gap
 *
 * Notes:
 * Notes:
 * * Programming IFG1 and IFG2.
 *
 *   For half-duplex systems that require IEEE 802.3 compatibility, IFG1 must
 *   be in the range of 1-8, IFG2 must be in the range of 4-12, and the
 *   IFG1+IFG2 sum must be 12.
 *
 *   For full-duplex systems that require IEEE 802.3 compatibility, IFG1 must
 *   be in the range of 1-11, IFG2 must be in the range of 1-11, and the
 *   IFG1+IFG2 sum must be 12.
 *
 *   For all other systems, IFG1 and IFG2 can be any value in the range of
 *   1-15.  Allowing for a total possible IFG sum of 2-30.
 *
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 */
union cvmx_agl_gmx_tx_ifg {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t ifg2                         : 4;  /**< 1/3 of the interframe gap timing
                                                         If CRS is detected during IFG2, then the
                                                         interFrameSpacing timer is not reset and a frame
                                                         is transmited once the timer expires. */
	uint64_t ifg1                         : 4;  /**< 2/3 of the interframe gap timing
                                                         If CRS is detected during IFG1, then the
                                                         interFrameSpacing timer is reset and a frame is
                                                         not transmited. */
#else
	uint64_t ifg1                         : 4;
	uint64_t ifg2                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_agl_gmx_tx_ifg_s          cn52xx;
	struct cvmx_agl_gmx_tx_ifg_s          cn52xxp1;
	struct cvmx_agl_gmx_tx_ifg_s          cn56xx;
	struct cvmx_agl_gmx_tx_ifg_s          cn56xxp1;
	struct cvmx_agl_gmx_tx_ifg_s          cn61xx;
	struct cvmx_agl_gmx_tx_ifg_s          cn63xx;
	struct cvmx_agl_gmx_tx_ifg_s          cn63xxp1;
	struct cvmx_agl_gmx_tx_ifg_s          cn66xx;
	struct cvmx_agl_gmx_tx_ifg_s          cn68xx;
	struct cvmx_agl_gmx_tx_ifg_s          cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_ifg cvmx_agl_gmx_tx_ifg_t;

/**
 * cvmx_agl_gmx_tx_int_en
 *
 * AGL_GMX_TX_INT_EN = Interrupt Enable
 *
 *
 * Notes:
 * UNDFLW[0], XSCOL[0], XSDEF[0], LATE_COL[0], PTP_LOST[0] will be reset when MIX0_CTL[RESET] is set to 1.
 * UNDFLW[1], XSCOL[1], XSDEF[1], LATE_COL[1], PTP_LOST[1] will be reset when MIX1_CTL[RESET] is set to 1.
 * PKO_NXA will bee reset when both MIX0/1_CTL[RESET] are set to 1.
 */
union cvmx_agl_gmx_tx_int_en {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t ptp_lost                     : 2;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t reserved_18_19               : 2;
	uint64_t late_col                     : 2;  /**< TX Late Collision */
	uint64_t reserved_14_15               : 2;
	uint64_t xsdef                        : 2;  /**< TX Excessive deferral (halfdup mode only) */
	uint64_t reserved_10_11               : 2;
	uint64_t xscol                        : 2;  /**< TX Excessive collisions (halfdup mode only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t undflw                       : 2;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 2;
	uint64_t reserved_4_7                 : 4;
	uint64_t xscol                        : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t xsdef                        : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t late_col                     : 2;
	uint64_t reserved_18_19               : 2;
	uint64_t ptp_lost                     : 2;
	uint64_t reserved_22_63               : 42;
#endif
	} s;
	struct cvmx_agl_gmx_tx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t late_col                     : 2;  /**< TX Late Collision */
	uint64_t reserved_14_15               : 2;
	uint64_t xsdef                        : 2;  /**< TX Excessive deferral (MII/halfdup mode only) */
	uint64_t reserved_10_11               : 2;
	uint64_t xscol                        : 2;  /**< TX Excessive collisions (MII/halfdup mode only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t undflw                       : 2;  /**< TX Underflow (MII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 2;
	uint64_t reserved_4_7                 : 4;
	uint64_t xscol                        : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t xsdef                        : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t late_col                     : 2;
	uint64_t reserved_18_63               : 46;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_tx_int_en_cn52xx  cn52xxp1;
	struct cvmx_agl_gmx_tx_int_en_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t late_col                     : 1;  /**< TX Late Collision */
	uint64_t reserved_13_15               : 3;
	uint64_t xsdef                        : 1;  /**< TX Excessive deferral (MII/halfdup mode only) */
	uint64_t reserved_9_11                : 3;
	uint64_t xscol                        : 1;  /**< TX Excessive collisions (MII/halfdup mode only) */
	uint64_t reserved_3_7                 : 5;
	uint64_t undflw                       : 1;  /**< TX Underflow (MII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t xscol                        : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t xsdef                        : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t late_col                     : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_tx_int_en_cn56xx  cn56xxp1;
	struct cvmx_agl_gmx_tx_int_en_s       cn61xx;
	struct cvmx_agl_gmx_tx_int_en_s       cn63xx;
	struct cvmx_agl_gmx_tx_int_en_s       cn63xxp1;
	struct cvmx_agl_gmx_tx_int_en_s       cn66xx;
	struct cvmx_agl_gmx_tx_int_en_s       cn68xx;
	struct cvmx_agl_gmx_tx_int_en_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_int_en cvmx_agl_gmx_tx_int_en_t;

/**
 * cvmx_agl_gmx_tx_int_reg
 *
 * AGL_GMX_TX_INT_REG = Interrupt Register
 *
 *
 * Notes:
 * UNDFLW[0], XSCOL[0], XSDEF[0], LATE_COL[0], PTP_LOST[0] will be reset when MIX0_CTL[RESET] is set to 1.
 * UNDFLW[1], XSCOL[1], XSDEF[1], LATE_COL[1], PTP_LOST[1] will be reset when MIX1_CTL[RESET] is set to 1.
 * PKO_NXA will bee reset when both MIX0/1_CTL[RESET] are set to 1.
 */
union cvmx_agl_gmx_tx_int_reg {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t ptp_lost                     : 2;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t reserved_18_19               : 2;
	uint64_t late_col                     : 2;  /**< TX Late Collision */
	uint64_t reserved_14_15               : 2;
	uint64_t xsdef                        : 2;  /**< TX Excessive deferral (halfdup mode only) */
	uint64_t reserved_10_11               : 2;
	uint64_t xscol                        : 2;  /**< TX Excessive collisions (halfdup mode only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t undflw                       : 2;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 2;
	uint64_t reserved_4_7                 : 4;
	uint64_t xscol                        : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t xsdef                        : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t late_col                     : 2;
	uint64_t reserved_18_19               : 2;
	uint64_t ptp_lost                     : 2;
	uint64_t reserved_22_63               : 42;
#endif
	} s;
	struct cvmx_agl_gmx_tx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t late_col                     : 2;  /**< TX Late Collision */
	uint64_t reserved_14_15               : 2;
	uint64_t xsdef                        : 2;  /**< TX Excessive deferral (MII/halfdup mode only) */
	uint64_t reserved_10_11               : 2;
	uint64_t xscol                        : 2;  /**< TX Excessive collisions (MII/halfdup mode only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t undflw                       : 2;  /**< TX Underflow (MII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 2;
	uint64_t reserved_4_7                 : 4;
	uint64_t xscol                        : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t xsdef                        : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t late_col                     : 2;
	uint64_t reserved_18_63               : 46;
#endif
	} cn52xx;
	struct cvmx_agl_gmx_tx_int_reg_cn52xx cn52xxp1;
	struct cvmx_agl_gmx_tx_int_reg_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t late_col                     : 1;  /**< TX Late Collision */
	uint64_t reserved_13_15               : 3;
	uint64_t xsdef                        : 1;  /**< TX Excessive deferral (MII/halfdup mode only) */
	uint64_t reserved_9_11                : 3;
	uint64_t xscol                        : 1;  /**< TX Excessive collisions (MII/halfdup mode only) */
	uint64_t reserved_3_7                 : 5;
	uint64_t undflw                       : 1;  /**< TX Underflow (MII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t xscol                        : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t xsdef                        : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t late_col                     : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_tx_int_reg_cn56xx cn56xxp1;
	struct cvmx_agl_gmx_tx_int_reg_s      cn61xx;
	struct cvmx_agl_gmx_tx_int_reg_s      cn63xx;
	struct cvmx_agl_gmx_tx_int_reg_s      cn63xxp1;
	struct cvmx_agl_gmx_tx_int_reg_s      cn66xx;
	struct cvmx_agl_gmx_tx_int_reg_s      cn68xx;
	struct cvmx_agl_gmx_tx_int_reg_s      cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_int_reg cvmx_agl_gmx_tx_int_reg_t;

/**
 * cvmx_agl_gmx_tx_jam
 *
 * AGL_GMX_TX_JAM = Packet TX Jam Pattern
 *
 *
 * Notes:
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 *
 */
union cvmx_agl_gmx_tx_jam {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_jam_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t jam                          : 8;  /**< Jam pattern */
#else
	uint64_t jam                          : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_agl_gmx_tx_jam_s          cn52xx;
	struct cvmx_agl_gmx_tx_jam_s          cn52xxp1;
	struct cvmx_agl_gmx_tx_jam_s          cn56xx;
	struct cvmx_agl_gmx_tx_jam_s          cn56xxp1;
	struct cvmx_agl_gmx_tx_jam_s          cn61xx;
	struct cvmx_agl_gmx_tx_jam_s          cn63xx;
	struct cvmx_agl_gmx_tx_jam_s          cn63xxp1;
	struct cvmx_agl_gmx_tx_jam_s          cn66xx;
	struct cvmx_agl_gmx_tx_jam_s          cn68xx;
	struct cvmx_agl_gmx_tx_jam_s          cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_jam cvmx_agl_gmx_tx_jam_t;

/**
 * cvmx_agl_gmx_tx_lfsr
 *
 * AGL_GMX_TX_LFSR = LFSR used to implement truncated binary exponential backoff
 *
 *
 * Notes:
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 *
 */
union cvmx_agl_gmx_tx_lfsr {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_lfsr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t lfsr                         : 16; /**< The current state of the LFSR used to feed random
                                                         numbers to compute truncated binary exponential
                                                         backoff. */
#else
	uint64_t lfsr                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_tx_lfsr_s         cn52xx;
	struct cvmx_agl_gmx_tx_lfsr_s         cn52xxp1;
	struct cvmx_agl_gmx_tx_lfsr_s         cn56xx;
	struct cvmx_agl_gmx_tx_lfsr_s         cn56xxp1;
	struct cvmx_agl_gmx_tx_lfsr_s         cn61xx;
	struct cvmx_agl_gmx_tx_lfsr_s         cn63xx;
	struct cvmx_agl_gmx_tx_lfsr_s         cn63xxp1;
	struct cvmx_agl_gmx_tx_lfsr_s         cn66xx;
	struct cvmx_agl_gmx_tx_lfsr_s         cn68xx;
	struct cvmx_agl_gmx_tx_lfsr_s         cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_lfsr cvmx_agl_gmx_tx_lfsr_t;

/**
 * cvmx_agl_gmx_tx_ovr_bp
 *
 * AGL_GMX_TX_OVR_BP = Packet TX Override BackPressure
 *
 *
 * Notes:
 * IGN_FULL[0], BP[0], EN[0] will be reset when MIX0_CTL[RESET] is set to 1.
 * IGN_FULL[1], BP[1], EN[1] will be reset when MIX1_CTL[RESET] is set to 1.
 */
union cvmx_agl_gmx_tx_ovr_bp {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_ovr_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t en                           : 2;  /**< Per port Enable back pressure override */
	uint64_t reserved_6_7                 : 2;
	uint64_t bp                           : 2;  /**< Port BackPressure status to use
                                                         0=Port is available
                                                         1=Port should be back pressured */
	uint64_t reserved_2_3                 : 2;
	uint64_t ign_full                     : 2;  /**< Ignore the RX FIFO full when computing BP */
#else
	uint64_t ign_full                     : 2;
	uint64_t reserved_2_3                 : 2;
	uint64_t bp                           : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t en                           : 2;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn52xx;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn52xxp1;
	struct cvmx_agl_gmx_tx_ovr_bp_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t en                           : 1;  /**< Per port Enable back pressure override */
	uint64_t reserved_5_7                 : 3;
	uint64_t bp                           : 1;  /**< Port BackPressure status to use
                                                         0=Port is available
                                                         1=Port should be back pressured */
	uint64_t reserved_1_3                 : 3;
	uint64_t ign_full                     : 1;  /**< Ignore the RX FIFO full when computing BP */
#else
	uint64_t ign_full                     : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t bp                           : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t en                           : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn56xx;
	struct cvmx_agl_gmx_tx_ovr_bp_cn56xx  cn56xxp1;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn61xx;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn63xx;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn63xxp1;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn66xx;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn68xx;
	struct cvmx_agl_gmx_tx_ovr_bp_s       cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_ovr_bp cvmx_agl_gmx_tx_ovr_bp_t;

/**
 * cvmx_agl_gmx_tx_pause_pkt_dmac
 *
 * AGL_GMX_TX_PAUSE_PKT_DMAC = Packet TX Pause Packet DMAC field
 *
 *
 * Notes:
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 *
 */
union cvmx_agl_gmx_tx_pause_pkt_dmac {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t dmac                         : 48; /**< The DMAC field placed is outbnd pause pkts */
#else
	uint64_t dmac                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn52xx;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn52xxp1;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn56xx;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn56xxp1;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn61xx;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn63xx;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn63xxp1;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn66xx;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn68xx;
	struct cvmx_agl_gmx_tx_pause_pkt_dmac_s cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_pause_pkt_dmac cvmx_agl_gmx_tx_pause_pkt_dmac_t;

/**
 * cvmx_agl_gmx_tx_pause_pkt_type
 *
 * AGL_GMX_TX_PAUSE_PKT_TYPE = Packet TX Pause Packet TYPE field
 *
 *
 * Notes:
 * Additionally reset when both MIX0/1_CTL[RESET] are set to 1.
 *
 */
union cvmx_agl_gmx_tx_pause_pkt_type {
	uint64_t u64;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t type                         : 16; /**< The TYPE field placed is outbnd pause pkts */
#else
	uint64_t type                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn52xx;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn52xxp1;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn56xx;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn56xxp1;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn61xx;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn63xx;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn63xxp1;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn66xx;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn68xx;
	struct cvmx_agl_gmx_tx_pause_pkt_type_s cn68xxp1;
};
typedef union cvmx_agl_gmx_tx_pause_pkt_type cvmx_agl_gmx_tx_pause_pkt_type_t;

/**
 * cvmx_agl_prt#_ctl
 *
 * AGL_PRT_CTL = AGL Port Control
 *
 *
 * Notes:
 * The RGMII timing specification requires that devices transmit clock and
 * data synchronously. The specification requires external sources (namely
 * the PC board trace routes) to introduce the appropriate 1.5 to 2.0 ns of
 * delay.
 *
 * To eliminate the need for the PC board delays, the MIX RGMII interface
 * has optional onboard DLL's for both transmit and receive. For correct
 * operation, at most one of the transmitter, board, or receiver involved
 * in an RGMII link should introduce delay. By default/reset,
 * the MIX RGMII receivers delay the received clock, and the MIX
 * RGMII transmitters do not delay the transmitted clock. Whether this
 * default works as-is with a given link partner depends on the behavior
 * of the link partner and the PC board.
 *
 * These are the possible modes of MIX RGMII receive operation:
 *  o AGL_PRTx_CTL[CLKRX_BYP] = 0 (reset value) - The OCTEON MIX RGMII
 *    receive interface introduces clock delay using its internal DLL.
 *    This mode is appropriate if neither the remote
 *    transmitter nor the PC board delays the clock.
 *  o AGL_PRTx_CTL[CLKRX_BYP] = 1, [CLKRX_SET] = 0x0 - The OCTEON MIX
 *    RGMII receive interface introduces no clock delay. This mode
 *    is appropriate if either the remote transmitter or the PC board
 *    delays the clock.
 *
 * These are the possible modes of MIX RGMII transmit operation:
 *  o AGL_PRTx_CTL[CLKTX_BYP] = 1, [CLKTX_SET] = 0x0 (reset value) -
 *    The OCTEON MIX RGMII transmit interface introduces no clock
 *    delay. This mode is appropriate is either the remote receiver
 *    or the PC board delays the clock.
 *  o AGL_PRTx_CTL[CLKTX_BYP] = 0 - The OCTEON MIX RGMII transmit
 *    interface introduces clock delay using its internal DLL.
 *    This mode is appropriate if neither the remote receiver
 *    nor the PC board delays the clock.
 *
 * AGL_PRT0_CTL will be reset when MIX0_CTL[RESET] is set to 1.
 * AGL_PRT1_CTL will be reset when MIX1_CTL[RESET] is set to 1.
 */
union cvmx_agl_prtx_ctl {
	uint64_t u64;
	struct cvmx_agl_prtx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t drv_byp                      : 1;  /**< Bypass the compensation controller and use
                                                         DRV_NCTL and DRV_PCTL */
	uint64_t reserved_62_62               : 1;
	uint64_t cmp_pctl                     : 6;  /**< PCTL drive strength from the compensation ctl */
	uint64_t reserved_54_55               : 2;
	uint64_t cmp_nctl                     : 6;  /**< NCTL drive strength from the compensation ctl */
	uint64_t reserved_46_47               : 2;
	uint64_t drv_pctl                     : 6;  /**< PCTL drive strength to use in bypass mode
                                                         Reset value of 19 is for 50 ohm termination */
	uint64_t reserved_38_39               : 2;
	uint64_t drv_nctl                     : 6;  /**< NCTL drive strength to use in bypass mode
                                                         Reset value of 15 is for 50 ohm termination */
	uint64_t reserved_29_31               : 3;
	uint64_t clk_set                      : 5;  /**< The clock delay as determined by the DLL */
	uint64_t clkrx_byp                    : 1;  /**< Bypass the RX clock delay setting
                                                         Skews RXC from RXD,RXCTL in RGMII mode
                                                         By default, HW internally shifts the RXC clock
                                                         to sample RXD,RXCTL assuming clock and data and
                                                         sourced synchronously from the link partner.
                                                         In MII mode, the CLKRX_BYP is forced to 1. */
	uint64_t reserved_21_22               : 2;
	uint64_t clkrx_set                    : 5;  /**< RX clock delay setting to use in bypass mode
                                                         Skews RXC from RXD in RGMII mode */
	uint64_t clktx_byp                    : 1;  /**< Bypass the TX clock delay setting
                                                         Skews TXC from TXD,TXCTL in RGMII mode
                                                         By default, clock and data and sourced
                                                         synchronously.
                                                         In MII mode, the CLKRX_BYP is forced to 1. */
	uint64_t reserved_13_14               : 2;
	uint64_t clktx_set                    : 5;  /**< TX clock delay setting to use in bypass mode
                                                         Skews TXC from TXD in RGMII mode */
	uint64_t reserved_5_7                 : 3;
	uint64_t dllrst                       : 1;  /**< DLL Reset */
	uint64_t comp                         : 1;  /**< Compensation Enable */
	uint64_t enable                       : 1;  /**< Port Enable */
	uint64_t clkrst                       : 1;  /**< Clock Tree Reset */
	uint64_t mode                         : 1;  /**< Port Mode
                                                         MODE must be set the same for all ports in which
                                                         AGL_PRTx_CTL[ENABLE] is set.
                                                         0=RGMII
                                                         1=MII */
#else
	uint64_t mode                         : 1;
	uint64_t clkrst                       : 1;
	uint64_t enable                       : 1;
	uint64_t comp                         : 1;
	uint64_t dllrst                       : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t clktx_set                    : 5;
	uint64_t reserved_13_14               : 2;
	uint64_t clktx_byp                    : 1;
	uint64_t clkrx_set                    : 5;
	uint64_t reserved_21_22               : 2;
	uint64_t clkrx_byp                    : 1;
	uint64_t clk_set                      : 5;
	uint64_t reserved_29_31               : 3;
	uint64_t drv_nctl                     : 6;
	uint64_t reserved_38_39               : 2;
	uint64_t drv_pctl                     : 6;
	uint64_t reserved_46_47               : 2;
	uint64_t cmp_nctl                     : 6;
	uint64_t reserved_54_55               : 2;
	uint64_t cmp_pctl                     : 6;
	uint64_t reserved_62_62               : 1;
	uint64_t drv_byp                      : 1;
#endif
	} s;
	struct cvmx_agl_prtx_ctl_s            cn61xx;
	struct cvmx_agl_prtx_ctl_s            cn63xx;
	struct cvmx_agl_prtx_ctl_s            cn63xxp1;
	struct cvmx_agl_prtx_ctl_s            cn66xx;
	struct cvmx_agl_prtx_ctl_s            cn68xx;
	struct cvmx_agl_prtx_ctl_s            cn68xxp1;
};
typedef union cvmx_agl_prtx_ctl cvmx_agl_prtx_ctl_t;

#endif
