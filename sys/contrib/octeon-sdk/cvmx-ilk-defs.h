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
 * cvmx-ilk-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon ilk.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_ILK_DEFS_H__
#define __CVMX_ILK_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_BIST_SUM CVMX_ILK_BIST_SUM_FUNC()
static inline uint64_t CVMX_ILK_BIST_SUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_BIST_SUM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000038ull);
}
#else
#define CVMX_ILK_BIST_SUM (CVMX_ADD_IO_SEG(0x0001180014000038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_GBL_CFG CVMX_ILK_GBL_CFG_FUNC()
static inline uint64_t CVMX_ILK_GBL_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_GBL_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000000ull);
}
#else
#define CVMX_ILK_GBL_CFG (CVMX_ADD_IO_SEG(0x0001180014000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_GBL_INT CVMX_ILK_GBL_INT_FUNC()
static inline uint64_t CVMX_ILK_GBL_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_GBL_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000008ull);
}
#else
#define CVMX_ILK_GBL_INT (CVMX_ADD_IO_SEG(0x0001180014000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_GBL_INT_EN CVMX_ILK_GBL_INT_EN_FUNC()
static inline uint64_t CVMX_ILK_GBL_INT_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_GBL_INT_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000010ull);
}
#else
#define CVMX_ILK_GBL_INT_EN (CVMX_ADD_IO_SEG(0x0001180014000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_INT_SUM CVMX_ILK_INT_SUM_FUNC()
static inline uint64_t CVMX_ILK_INT_SUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_INT_SUM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000030ull);
}
#else
#define CVMX_ILK_INT_SUM (CVMX_ADD_IO_SEG(0x0001180014000030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_LNE_DBG CVMX_ILK_LNE_DBG_FUNC()
static inline uint64_t CVMX_ILK_LNE_DBG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_LNE_DBG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014030008ull);
}
#else
#define CVMX_ILK_LNE_DBG (CVMX_ADD_IO_SEG(0x0001180014030008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_LNE_STS_MSG CVMX_ILK_LNE_STS_MSG_FUNC()
static inline uint64_t CVMX_ILK_LNE_STS_MSG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_LNE_STS_MSG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014030000ull);
}
#else
#define CVMX_ILK_LNE_STS_MSG (CVMX_ADD_IO_SEG(0x0001180014030000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_RXF_IDX_PMAP CVMX_ILK_RXF_IDX_PMAP_FUNC()
static inline uint64_t CVMX_ILK_RXF_IDX_PMAP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_RXF_IDX_PMAP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000020ull);
}
#else
#define CVMX_ILK_RXF_IDX_PMAP (CVMX_ADD_IO_SEG(0x0001180014000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_RXF_MEM_PMAP CVMX_ILK_RXF_MEM_PMAP_FUNC()
static inline uint64_t CVMX_ILK_RXF_MEM_PMAP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_RXF_MEM_PMAP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000028ull);
}
#else
#define CVMX_ILK_RXF_MEM_PMAP (CVMX_ADD_IO_SEG(0x0001180014000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_CFG0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_CFG0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020000ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_CFG0(offset) (CVMX_ADD_IO_SEG(0x0001180014020000ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_CFG1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_CFG1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020008ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_CFG1(offset) (CVMX_ADD_IO_SEG(0x0001180014020008ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_FLOW_CTL0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_FLOW_CTL0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020090ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_FLOW_CTL0(offset) (CVMX_ADD_IO_SEG(0x0001180014020090ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_FLOW_CTL1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_FLOW_CTL1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020098ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_FLOW_CTL1(offset) (CVMX_ADD_IO_SEG(0x0001180014020098ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_IDX_CAL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_IDX_CAL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800140200A0ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_IDX_CAL(offset) (CVMX_ADD_IO_SEG(0x00011800140200A0ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_IDX_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_IDX_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020070ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_IDX_STAT0(offset) (CVMX_ADD_IO_SEG(0x0001180014020070ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_IDX_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_IDX_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020078ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_IDX_STAT1(offset) (CVMX_ADD_IO_SEG(0x0001180014020078ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_INT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_INT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020010ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_INT(offset) (CVMX_ADD_IO_SEG(0x0001180014020010ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_INT_EN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_INT_EN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020018ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_INT_EN(offset) (CVMX_ADD_IO_SEG(0x0001180014020018ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_JABBER(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_JABBER(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800140200B8ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_JABBER(offset) (CVMX_ADD_IO_SEG(0x00011800140200B8ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_MEM_CAL0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_MEM_CAL0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800140200A8ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_MEM_CAL0(offset) (CVMX_ADD_IO_SEG(0x00011800140200A8ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_MEM_CAL1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_MEM_CAL1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800140200B0ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_MEM_CAL1(offset) (CVMX_ADD_IO_SEG(0x00011800140200B0ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_MEM_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_MEM_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020080ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_MEM_STAT0(offset) (CVMX_ADD_IO_SEG(0x0001180014020080ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_MEM_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_MEM_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020088ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_MEM_STAT1(offset) (CVMX_ADD_IO_SEG(0x0001180014020088ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_RID(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_RID(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800140200C0ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_RID(offset) (CVMX_ADD_IO_SEG(0x00011800140200C0ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020020ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT0(offset) (CVMX_ADD_IO_SEG(0x0001180014020020ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020028ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT1(offset) (CVMX_ADD_IO_SEG(0x0001180014020028ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020030ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT2(offset) (CVMX_ADD_IO_SEG(0x0001180014020030ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020038ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT3(offset) (CVMX_ADD_IO_SEG(0x0001180014020038ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020040ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT4(offset) (CVMX_ADD_IO_SEG(0x0001180014020040ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT5(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT5(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020048ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT5(offset) (CVMX_ADD_IO_SEG(0x0001180014020048ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT6(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT6(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020050ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT6(offset) (CVMX_ADD_IO_SEG(0x0001180014020050ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT7(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT7(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020058ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT7(offset) (CVMX_ADD_IO_SEG(0x0001180014020058ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT8(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT8(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020060ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT8(offset) (CVMX_ADD_IO_SEG(0x0001180014020060ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RXX_STAT9(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_RXX_STAT9(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014020068ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_RXX_STAT9(offset) (CVMX_ADD_IO_SEG(0x0001180014020068ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_CFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_CFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038000ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_CFG(offset) (CVMX_ADD_IO_SEG(0x0001180014038000ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_INT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_INT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038008ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_INT(offset) (CVMX_ADD_IO_SEG(0x0001180014038008ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_INT_EN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_INT_EN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038010ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_INT_EN(offset) (CVMX_ADD_IO_SEG(0x0001180014038010ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038018ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT0(offset) (CVMX_ADD_IO_SEG(0x0001180014038018ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038020ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT1(offset) (CVMX_ADD_IO_SEG(0x0001180014038020ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038028ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT2(offset) (CVMX_ADD_IO_SEG(0x0001180014038028ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038030ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT3(offset) (CVMX_ADD_IO_SEG(0x0001180014038030ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038038ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT4(offset) (CVMX_ADD_IO_SEG(0x0001180014038038ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT5(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT5(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038040ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT5(offset) (CVMX_ADD_IO_SEG(0x0001180014038040ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT6(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT6(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038048ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT6(offset) (CVMX_ADD_IO_SEG(0x0001180014038048ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT7(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT7(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038050ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT7(offset) (CVMX_ADD_IO_SEG(0x0001180014038050ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT8(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT8(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038058ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT8(offset) (CVMX_ADD_IO_SEG(0x0001180014038058ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_RX_LNEX_STAT9(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_ILK_RX_LNEX_STAT9(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014038060ull) + ((offset) & 7) * 1024;
}
#else
#define CVMX_ILK_RX_LNEX_STAT9(offset) (CVMX_ADD_IO_SEG(0x0001180014038060ull) + ((offset) & 7) * 1024)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ILK_SER_CFG CVMX_ILK_SER_CFG_FUNC()
static inline uint64_t CVMX_ILK_SER_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_ILK_SER_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180014000018ull);
}
#else
#define CVMX_ILK_SER_CFG (CVMX_ADD_IO_SEG(0x0001180014000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_CFG0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_CFG0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010000ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_CFG0(offset) (CVMX_ADD_IO_SEG(0x0001180014010000ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_CFG1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_CFG1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010008ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_CFG1(offset) (CVMX_ADD_IO_SEG(0x0001180014010008ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_DBG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_DBG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010070ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_DBG(offset) (CVMX_ADD_IO_SEG(0x0001180014010070ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_FLOW_CTL0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_FLOW_CTL0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010048ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_FLOW_CTL0(offset) (CVMX_ADD_IO_SEG(0x0001180014010048ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_FLOW_CTL1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_FLOW_CTL1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010050ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_FLOW_CTL1(offset) (CVMX_ADD_IO_SEG(0x0001180014010050ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_IDX_CAL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_IDX_CAL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010058ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_IDX_CAL(offset) (CVMX_ADD_IO_SEG(0x0001180014010058ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_IDX_PMAP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_IDX_PMAP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010010ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_IDX_PMAP(offset) (CVMX_ADD_IO_SEG(0x0001180014010010ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_IDX_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_IDX_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010020ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_IDX_STAT0(offset) (CVMX_ADD_IO_SEG(0x0001180014010020ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_IDX_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_IDX_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010028ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_IDX_STAT1(offset) (CVMX_ADD_IO_SEG(0x0001180014010028ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_INT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_INT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010078ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_INT(offset) (CVMX_ADD_IO_SEG(0x0001180014010078ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_INT_EN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_INT_EN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010080ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_INT_EN(offset) (CVMX_ADD_IO_SEG(0x0001180014010080ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_MEM_CAL0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_MEM_CAL0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010060ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_MEM_CAL0(offset) (CVMX_ADD_IO_SEG(0x0001180014010060ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_MEM_CAL1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_MEM_CAL1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010068ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_MEM_CAL1(offset) (CVMX_ADD_IO_SEG(0x0001180014010068ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_MEM_PMAP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_MEM_PMAP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010018ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_MEM_PMAP(offset) (CVMX_ADD_IO_SEG(0x0001180014010018ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_MEM_STAT0(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_MEM_STAT0(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010030ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_MEM_STAT0(offset) (CVMX_ADD_IO_SEG(0x0001180014010030ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_MEM_STAT1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_MEM_STAT1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010038ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_MEM_STAT1(offset) (CVMX_ADD_IO_SEG(0x0001180014010038ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_PIPE(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_PIPE(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010088ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_PIPE(offset) (CVMX_ADD_IO_SEG(0x0001180014010088ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ILK_TXX_RMATCH(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_ILK_TXX_RMATCH(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180014010040ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_ILK_TXX_RMATCH(offset) (CVMX_ADD_IO_SEG(0x0001180014010040ull) + ((offset) & 1) * 16384)
#endif

/**
 * cvmx_ilk_bist_sum
 */
union cvmx_ilk_bist_sum {
	uint64_t u64;
	struct cvmx_ilk_bist_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t rxf_x2p1                     : 1;  /**< Bist status of rxf.x2p_fif_mem1 */
	uint64_t rxf_x2p0                     : 1;  /**< Bist status of rxf.x2p_fif_mem0 */
	uint64_t rxf_pmap                     : 1;  /**< Bist status of rxf.rx_map_mem */
	uint64_t rxf_mem2                     : 1;  /**< Bist status of rxf.rx_fif_mem2 */
	uint64_t rxf_mem1                     : 1;  /**< Bist status of rxf.rx_fif_mem1 */
	uint64_t rxf_mem0                     : 1;  /**< Bist status of rxf.rx_fif_mem0 */
	uint64_t reserved_36_51               : 16;
	uint64_t rle7_dsk1                    : 1;  /**< Bist status of lne.rle7.dsk.dsk_fif_mem1 */
	uint64_t rle7_dsk0                    : 1;  /**< Bist status of lne.rle7.dsk.dsk_fif_mem0 */
	uint64_t rle6_dsk1                    : 1;  /**< Bist status of lne.rle6.dsk.dsk_fif_mem1 */
	uint64_t rle6_dsk0                    : 1;  /**< Bist status of lne.rle6.dsk.dsk_fif_mem0 */
	uint64_t rle5_dsk1                    : 1;  /**< Bist status of lne.rle5.dsk.dsk_fif_mem1 */
	uint64_t rle5_dsk0                    : 1;  /**< Bist status of lne.rle5.dsk.dsk_fif_mem0 */
	uint64_t rle4_dsk1                    : 1;  /**< Bist status of lne.rle4.dsk.dsk_fif_mem1 */
	uint64_t rle4_dsk0                    : 1;  /**< Bist status of lne.rle4.dsk.dsk_fif_mem0 */
	uint64_t rle3_dsk1                    : 1;  /**< Bist status of lne.rle3.dsk.dsk_fif_mem1 */
	uint64_t rle3_dsk0                    : 1;  /**< Bist status of lne.rle3.dsk.dsk_fif_mem0 */
	uint64_t rle2_dsk1                    : 1;  /**< Bist status of lne.rle2.dsk.dsk_fif_mem1 */
	uint64_t rle2_dsk0                    : 1;  /**< Bist status of lne.rle2.dsk.dsk_fif_mem0 */
	uint64_t rle1_dsk1                    : 1;  /**< Bist status of lne.rle1.dsk.dsk_fif_mem1 */
	uint64_t rle1_dsk0                    : 1;  /**< Bist status of lne.rle1.dsk.dsk_fif_mem0 */
	uint64_t rle0_dsk1                    : 1;  /**< Bist status of lne.rle0.dsk.dsk_fif_mem1 */
	uint64_t rle0_dsk0                    : 1;  /**< Bist status of lne.rle0.dsk.dsk_fif_mem0 */
	uint64_t reserved_19_19               : 1;
	uint64_t rlk1_stat1                   : 1;  /**< Bist status of rlk1.csr.stat_mem1    ***NOTE: Added in pass 2.0 */
	uint64_t rlk1_fwc                     : 1;  /**< Bist status of rlk1.fwc.cal_chan_ram */
	uint64_t rlk1_stat                    : 1;  /**< Bist status of rlk1.csr.stat_mem */
	uint64_t reserved_15_15               : 1;
	uint64_t rlk0_stat1                   : 1;  /**< Bist status of rlk0.csr.stat_mem1    ***NOTE: Added in pass 2.0 */
	uint64_t rlk0_fwc                     : 1;  /**< Bist status of rlk0.fwc.cal_chan_ram */
	uint64_t rlk0_stat                    : 1;  /**< Bist status of rlk0.csr.stat_mem */
	uint64_t tlk1_stat1                   : 1;  /**< Bist status of tlk1.csr.stat_mem1 */
	uint64_t tlk1_fwc                     : 1;  /**< Bist status of tlk1.fwc.cal_chan_ram */
	uint64_t reserved_9_9                 : 1;
	uint64_t tlk1_txf2                    : 1;  /**< Bist status of tlk1.txf.tx_map_mem */
	uint64_t tlk1_txf1                    : 1;  /**< Bist status of tlk1.txf.tx_fif_mem1 */
	uint64_t tlk1_txf0                    : 1;  /**< Bist status of tlk1.txf.tx_fif_mem0 */
	uint64_t tlk0_stat1                   : 1;  /**< Bist status of tlk0.csr.stat_mem1 */
	uint64_t tlk0_fwc                     : 1;  /**< Bist status of tlk0.fwc.cal_chan_ram */
	uint64_t reserved_3_3                 : 1;
	uint64_t tlk0_txf2                    : 1;  /**< Bist status of tlk0.txf.tx_map_mem */
	uint64_t tlk0_txf1                    : 1;  /**< Bist status of tlk0.txf.tx_fif_mem1 */
	uint64_t tlk0_txf0                    : 1;  /**< Bist status of tlk0.txf.tx_fif_mem0 */
#else
	uint64_t tlk0_txf0                    : 1;
	uint64_t tlk0_txf1                    : 1;
	uint64_t tlk0_txf2                    : 1;
	uint64_t reserved_3_3                 : 1;
	uint64_t tlk0_fwc                     : 1;
	uint64_t tlk0_stat1                   : 1;
	uint64_t tlk1_txf0                    : 1;
	uint64_t tlk1_txf1                    : 1;
	uint64_t tlk1_txf2                    : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t tlk1_fwc                     : 1;
	uint64_t tlk1_stat1                   : 1;
	uint64_t rlk0_stat                    : 1;
	uint64_t rlk0_fwc                     : 1;
	uint64_t rlk0_stat1                   : 1;
	uint64_t reserved_15_15               : 1;
	uint64_t rlk1_stat                    : 1;
	uint64_t rlk1_fwc                     : 1;
	uint64_t rlk1_stat1                   : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t rle0_dsk0                    : 1;
	uint64_t rle0_dsk1                    : 1;
	uint64_t rle1_dsk0                    : 1;
	uint64_t rle1_dsk1                    : 1;
	uint64_t rle2_dsk0                    : 1;
	uint64_t rle2_dsk1                    : 1;
	uint64_t rle3_dsk0                    : 1;
	uint64_t rle3_dsk1                    : 1;
	uint64_t rle4_dsk0                    : 1;
	uint64_t rle4_dsk1                    : 1;
	uint64_t rle5_dsk0                    : 1;
	uint64_t rle5_dsk1                    : 1;
	uint64_t rle6_dsk0                    : 1;
	uint64_t rle6_dsk1                    : 1;
	uint64_t rle7_dsk0                    : 1;
	uint64_t rle7_dsk1                    : 1;
	uint64_t reserved_36_51               : 16;
	uint64_t rxf_mem0                     : 1;
	uint64_t rxf_mem1                     : 1;
	uint64_t rxf_mem2                     : 1;
	uint64_t rxf_pmap                     : 1;
	uint64_t rxf_x2p0                     : 1;
	uint64_t rxf_x2p1                     : 1;
	uint64_t reserved_58_63               : 6;
#endif
	} s;
	struct cvmx_ilk_bist_sum_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t rxf_x2p1                     : 1;  /**< Bist status of rxf.x2p_fif_mem1 */
	uint64_t rxf_x2p0                     : 1;  /**< Bist status of rxf.x2p_fif_mem0 */
	uint64_t rxf_pmap                     : 1;  /**< Bist status of rxf.rx_map_mem */
	uint64_t rxf_mem2                     : 1;  /**< Bist status of rxf.rx_fif_mem2 */
	uint64_t rxf_mem1                     : 1;  /**< Bist status of rxf.rx_fif_mem1 */
	uint64_t rxf_mem0                     : 1;  /**< Bist status of rxf.rx_fif_mem0 */
	uint64_t reserved_36_51               : 16;
	uint64_t rle7_dsk1                    : 1;  /**< Bist status of lne.rle7.dsk.dsk_fif_mem1 */
	uint64_t rle7_dsk0                    : 1;  /**< Bist status of lne.rle7.dsk.dsk_fif_mem0 */
	uint64_t rle6_dsk1                    : 1;  /**< Bist status of lne.rle6.dsk.dsk_fif_mem1 */
	uint64_t rle6_dsk0                    : 1;  /**< Bist status of lne.rle6.dsk.dsk_fif_mem0 */
	uint64_t rle5_dsk1                    : 1;  /**< Bist status of lne.rle5.dsk.dsk_fif_mem1 */
	uint64_t rle5_dsk0                    : 1;  /**< Bist status of lne.rle5.dsk.dsk_fif_mem0 */
	uint64_t rle4_dsk1                    : 1;  /**< Bist status of lne.rle4.dsk.dsk_fif_mem1 */
	uint64_t rle4_dsk0                    : 1;  /**< Bist status of lne.rle4.dsk.dsk_fif_mem0 */
	uint64_t rle3_dsk1                    : 1;  /**< Bist status of lne.rle3.dsk.dsk_fif_mem1 */
	uint64_t rle3_dsk0                    : 1;  /**< Bist status of lne.rle3.dsk.dsk_fif_mem0 */
	uint64_t rle2_dsk1                    : 1;  /**< Bist status of lne.rle2.dsk.dsk_fif_mem1 */
	uint64_t rle2_dsk0                    : 1;  /**< Bist status of lne.rle2.dsk.dsk_fif_mem0 */
	uint64_t rle1_dsk1                    : 1;  /**< Bist status of lne.rle1.dsk.dsk_fif_mem1 */
	uint64_t rle1_dsk0                    : 1;  /**< Bist status of lne.rle1.dsk.dsk_fif_mem0 */
	uint64_t rle0_dsk1                    : 1;  /**< Bist status of lne.rle0.dsk.dsk_fif_mem1 */
	uint64_t rle0_dsk0                    : 1;  /**< Bist status of lne.rle0.dsk.dsk_fif_mem0 */
	uint64_t reserved_19_19               : 1;
	uint64_t rlk1_stat1                   : 1;  /**< Bist status of rlk1.csr.stat_mem1    ***NOTE: Added in pass 2.0 */
	uint64_t rlk1_fwc                     : 1;  /**< Bist status of rlk1.fwc.cal_chan_ram */
	uint64_t rlk1_stat                    : 1;  /**< Bist status of rlk1.csr.stat_mem0 */
	uint64_t reserved_15_15               : 1;
	uint64_t rlk0_stat1                   : 1;  /**< Bist status of rlk0.csr.stat_mem1    ***NOTE: Added in pass 2.0 */
	uint64_t rlk0_fwc                     : 1;  /**< Bist status of rlk0.fwc.cal_chan_ram */
	uint64_t rlk0_stat                    : 1;  /**< Bist status of rlk0.csr.stat_mem0 */
	uint64_t tlk1_stat1                   : 1;  /**< Bist status of tlk1.csr.stat_mem1 */
	uint64_t tlk1_fwc                     : 1;  /**< Bist status of tlk1.fwc.cal_chan_ram */
	uint64_t tlk1_stat0                   : 1;  /**< Bist status of tlk1.csr.stat_mem0 */
	uint64_t tlk1_txf2                    : 1;  /**< Bist status of tlk1.txf.tx_map_mem */
	uint64_t tlk1_txf1                    : 1;  /**< Bist status of tlk1.txf.tx_fif_mem1 */
	uint64_t tlk1_txf0                    : 1;  /**< Bist status of tlk1.txf.tx_fif_mem0 */
	uint64_t tlk0_stat1                   : 1;  /**< Bist status of tlk0.csr.stat_mem1 */
	uint64_t tlk0_fwc                     : 1;  /**< Bist status of tlk0.fwc.cal_chan_ram */
	uint64_t tlk0_stat0                   : 1;  /**< Bist status of tlk0.csr.stat_mem0 */
	uint64_t tlk0_txf2                    : 1;  /**< Bist status of tlk0.txf.tx_map_mem */
	uint64_t tlk0_txf1                    : 1;  /**< Bist status of tlk0.txf.tx_fif_mem1 */
	uint64_t tlk0_txf0                    : 1;  /**< Bist status of tlk0.txf.tx_fif_mem0 */
#else
	uint64_t tlk0_txf0                    : 1;
	uint64_t tlk0_txf1                    : 1;
	uint64_t tlk0_txf2                    : 1;
	uint64_t tlk0_stat0                   : 1;
	uint64_t tlk0_fwc                     : 1;
	uint64_t tlk0_stat1                   : 1;
	uint64_t tlk1_txf0                    : 1;
	uint64_t tlk1_txf1                    : 1;
	uint64_t tlk1_txf2                    : 1;
	uint64_t tlk1_stat0                   : 1;
	uint64_t tlk1_fwc                     : 1;
	uint64_t tlk1_stat1                   : 1;
	uint64_t rlk0_stat                    : 1;
	uint64_t rlk0_fwc                     : 1;
	uint64_t rlk0_stat1                   : 1;
	uint64_t reserved_15_15               : 1;
	uint64_t rlk1_stat                    : 1;
	uint64_t rlk1_fwc                     : 1;
	uint64_t rlk1_stat1                   : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t rle0_dsk0                    : 1;
	uint64_t rle0_dsk1                    : 1;
	uint64_t rle1_dsk0                    : 1;
	uint64_t rle1_dsk1                    : 1;
	uint64_t rle2_dsk0                    : 1;
	uint64_t rle2_dsk1                    : 1;
	uint64_t rle3_dsk0                    : 1;
	uint64_t rle3_dsk1                    : 1;
	uint64_t rle4_dsk0                    : 1;
	uint64_t rle4_dsk1                    : 1;
	uint64_t rle5_dsk0                    : 1;
	uint64_t rle5_dsk1                    : 1;
	uint64_t rle6_dsk0                    : 1;
	uint64_t rle6_dsk1                    : 1;
	uint64_t rle7_dsk0                    : 1;
	uint64_t rle7_dsk1                    : 1;
	uint64_t reserved_36_51               : 16;
	uint64_t rxf_mem0                     : 1;
	uint64_t rxf_mem1                     : 1;
	uint64_t rxf_mem2                     : 1;
	uint64_t rxf_pmap                     : 1;
	uint64_t rxf_x2p0                     : 1;
	uint64_t rxf_x2p1                     : 1;
	uint64_t reserved_58_63               : 6;
#endif
	} cn68xx;
	struct cvmx_ilk_bist_sum_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t rxf_x2p1                     : 1;  /**< Bist status of rxf.x2p_fif_mem1 */
	uint64_t rxf_x2p0                     : 1;  /**< Bist status of rxf.x2p_fif_mem0 */
	uint64_t rxf_pmap                     : 1;  /**< Bist status of rxf.rx_map_mem */
	uint64_t rxf_mem2                     : 1;  /**< Bist status of rxf.rx_fif_mem2 */
	uint64_t rxf_mem1                     : 1;  /**< Bist status of rxf.rx_fif_mem1 */
	uint64_t rxf_mem0                     : 1;  /**< Bist status of rxf.rx_fif_mem0 */
	uint64_t reserved_36_51               : 16;
	uint64_t rle7_dsk1                    : 1;  /**< Bist status of lne.rle7.dsk.dsk_fif_mem1 */
	uint64_t rle7_dsk0                    : 1;  /**< Bist status of lne.rle7.dsk.dsk_fif_mem0 */
	uint64_t rle6_dsk1                    : 1;  /**< Bist status of lne.rle6.dsk.dsk_fif_mem1 */
	uint64_t rle6_dsk0                    : 1;  /**< Bist status of lne.rle6.dsk.dsk_fif_mem0 */
	uint64_t rle5_dsk1                    : 1;  /**< Bist status of lne.rle5.dsk.dsk_fif_mem1 */
	uint64_t rle5_dsk0                    : 1;  /**< Bist status of lne.rle5.dsk.dsk_fif_mem0 */
	uint64_t rle4_dsk1                    : 1;  /**< Bist status of lne.rle4.dsk.dsk_fif_mem1 */
	uint64_t rle4_dsk0                    : 1;  /**< Bist status of lne.rle4.dsk.dsk_fif_mem0 */
	uint64_t rle3_dsk1                    : 1;  /**< Bist status of lne.rle3.dsk.dsk_fif_mem1 */
	uint64_t rle3_dsk0                    : 1;  /**< Bist status of lne.rle3.dsk.dsk_fif_mem0 */
	uint64_t rle2_dsk1                    : 1;  /**< Bist status of lne.rle2.dsk.dsk_fif_mem1 */
	uint64_t rle2_dsk0                    : 1;  /**< Bist status of lne.rle2.dsk.dsk_fif_mem0 */
	uint64_t rle1_dsk1                    : 1;  /**< Bist status of lne.rle1.dsk.dsk_fif_mem1 */
	uint64_t rle1_dsk0                    : 1;  /**< Bist status of lne.rle1.dsk.dsk_fif_mem0 */
	uint64_t rle0_dsk1                    : 1;  /**< Bist status of lne.rle0.dsk.dsk_fif_mem1 */
	uint64_t rle0_dsk0                    : 1;  /**< Bist status of lne.rle0.dsk.dsk_fif_mem0 */
	uint64_t reserved_18_19               : 2;
	uint64_t rlk1_fwc                     : 1;  /**< Bist status of rlk1.fwc.cal_chan_ram */
	uint64_t rlk1_stat                    : 1;  /**< Bist status of rlk1.csr.stat_mem */
	uint64_t reserved_14_15               : 2;
	uint64_t rlk0_fwc                     : 1;  /**< Bist status of rlk0.fwc.cal_chan_ram */
	uint64_t rlk0_stat                    : 1;  /**< Bist status of rlk0.csr.stat_mem */
	uint64_t reserved_11_11               : 1;
	uint64_t tlk1_fwc                     : 1;  /**< Bist status of tlk1.fwc.cal_chan_ram */
	uint64_t tlk1_stat                    : 1;  /**< Bist status of tlk1.csr.stat_mem */
	uint64_t tlk1_txf2                    : 1;  /**< Bist status of tlk1.txf.tx_map_mem */
	uint64_t tlk1_txf1                    : 1;  /**< Bist status of tlk1.txf.tx_fif_mem1 */
	uint64_t tlk1_txf0                    : 1;  /**< Bist status of tlk1.txf.tx_fif_mem0 */
	uint64_t reserved_5_5                 : 1;
	uint64_t tlk0_fwc                     : 1;  /**< Bist status of tlk0.fwc.cal_chan_ram */
	uint64_t tlk0_stat                    : 1;  /**< Bist status of tlk0.csr.stat_mem */
	uint64_t tlk0_txf2                    : 1;  /**< Bist status of tlk0.txf.tx_map_mem */
	uint64_t tlk0_txf1                    : 1;  /**< Bist status of tlk0.txf.tx_fif_mem1 */
	uint64_t tlk0_txf0                    : 1;  /**< Bist status of tlk0.txf.tx_fif_mem0 */
#else
	uint64_t tlk0_txf0                    : 1;
	uint64_t tlk0_txf1                    : 1;
	uint64_t tlk0_txf2                    : 1;
	uint64_t tlk0_stat                    : 1;
	uint64_t tlk0_fwc                     : 1;
	uint64_t reserved_5_5                 : 1;
	uint64_t tlk1_txf0                    : 1;
	uint64_t tlk1_txf1                    : 1;
	uint64_t tlk1_txf2                    : 1;
	uint64_t tlk1_stat                    : 1;
	uint64_t tlk1_fwc                     : 1;
	uint64_t reserved_11_11               : 1;
	uint64_t rlk0_stat                    : 1;
	uint64_t rlk0_fwc                     : 1;
	uint64_t reserved_14_15               : 2;
	uint64_t rlk1_stat                    : 1;
	uint64_t rlk1_fwc                     : 1;
	uint64_t reserved_18_19               : 2;
	uint64_t rle0_dsk0                    : 1;
	uint64_t rle0_dsk1                    : 1;
	uint64_t rle1_dsk0                    : 1;
	uint64_t rle1_dsk1                    : 1;
	uint64_t rle2_dsk0                    : 1;
	uint64_t rle2_dsk1                    : 1;
	uint64_t rle3_dsk0                    : 1;
	uint64_t rle3_dsk1                    : 1;
	uint64_t rle4_dsk0                    : 1;
	uint64_t rle4_dsk1                    : 1;
	uint64_t rle5_dsk0                    : 1;
	uint64_t rle5_dsk1                    : 1;
	uint64_t rle6_dsk0                    : 1;
	uint64_t rle6_dsk1                    : 1;
	uint64_t rle7_dsk0                    : 1;
	uint64_t rle7_dsk1                    : 1;
	uint64_t reserved_36_51               : 16;
	uint64_t rxf_mem0                     : 1;
	uint64_t rxf_mem1                     : 1;
	uint64_t rxf_mem2                     : 1;
	uint64_t rxf_pmap                     : 1;
	uint64_t rxf_x2p0                     : 1;
	uint64_t rxf_x2p1                     : 1;
	uint64_t reserved_58_63               : 6;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_bist_sum cvmx_ilk_bist_sum_t;

/**
 * cvmx_ilk_gbl_cfg
 */
union cvmx_ilk_gbl_cfg {
	uint64_t u64;
	struct cvmx_ilk_gbl_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t rid_rstdis                   : 1;  /**< Disable automatic reassembly-id error recovery. For diagnostic
                                                         use only.

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t reset                        : 1;  /**< Reset ILK.  For diagnostic use only.

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t cclk_dis                     : 1;  /**< Disable ILK conditional clocking.   For diagnostic use only. */
	uint64_t rxf_xlink                    : 1;  /**< Causes external loopback traffic to switch links.  Enabling
                                                         this allow simultaneous use of external and internal loopback. */
#else
	uint64_t rxf_xlink                    : 1;
	uint64_t cclk_dis                     : 1;
	uint64_t reset                        : 1;
	uint64_t rid_rstdis                   : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ilk_gbl_cfg_s             cn68xx;
	struct cvmx_ilk_gbl_cfg_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t cclk_dis                     : 1;  /**< Disable ILK conditional clocking.   For diagnostic use only. */
	uint64_t rxf_xlink                    : 1;  /**< Causes external loopback traffic to switch links.  Enabling
                                                         this allow simultaneous use of external and internal loopback. */
#else
	uint64_t rxf_xlink                    : 1;
	uint64_t cclk_dis                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_gbl_cfg cvmx_ilk_gbl_cfg_t;

/**
 * cvmx_ilk_gbl_int
 */
union cvmx_ilk_gbl_int {
	uint64_t u64;
	struct cvmx_ilk_gbl_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t rxf_push_full                : 1;  /**< RXF overflow */
	uint64_t rxf_pop_empty                : 1;  /**< RXF underflow */
	uint64_t rxf_ctl_perr                 : 1;  /**< RXF parity error occurred on sideband control signals.  Data
                                                         cycle will be dropped. */
	uint64_t rxf_lnk1_perr                : 1;  /**< RXF parity error occurred on RxLink1 packet data
                                                         Packet will be marked with error at eop */
	uint64_t rxf_lnk0_perr                : 1;  /**< RXF parity error occurred on RxLink0 packet data.  Packet will
                                                         be marked with error at eop */
#else
	uint64_t rxf_lnk0_perr                : 1;
	uint64_t rxf_lnk1_perr                : 1;
	uint64_t rxf_ctl_perr                 : 1;
	uint64_t rxf_pop_empty                : 1;
	uint64_t rxf_push_full                : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_ilk_gbl_int_s             cn68xx;
	struct cvmx_ilk_gbl_int_s             cn68xxp1;
};
typedef union cvmx_ilk_gbl_int cvmx_ilk_gbl_int_t;

/**
 * cvmx_ilk_gbl_int_en
 */
union cvmx_ilk_gbl_int_en {
	uint64_t u64;
	struct cvmx_ilk_gbl_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t rxf_push_full                : 1;  /**< RXF overflow */
	uint64_t rxf_pop_empty                : 1;  /**< RXF underflow */
	uint64_t rxf_ctl_perr                 : 1;  /**< RXF parity error occurred on sideband control signals.  Data
                                                         cycle will be dropped. */
	uint64_t rxf_lnk1_perr                : 1;  /**< RXF parity error occurred on RxLink1 packet data
                                                         Packet will be marked with error at eop */
	uint64_t rxf_lnk0_perr                : 1;  /**< RXF parity error occurred on RxLink0 packet data
                                                         Packet will be marked with error at eop */
#else
	uint64_t rxf_lnk0_perr                : 1;
	uint64_t rxf_lnk1_perr                : 1;
	uint64_t rxf_ctl_perr                 : 1;
	uint64_t rxf_pop_empty                : 1;
	uint64_t rxf_push_full                : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_ilk_gbl_int_en_s          cn68xx;
	struct cvmx_ilk_gbl_int_en_s          cn68xxp1;
};
typedef union cvmx_ilk_gbl_int_en cvmx_ilk_gbl_int_en_t;

/**
 * cvmx_ilk_int_sum
 */
union cvmx_ilk_int_sum {
	uint64_t u64;
	struct cvmx_ilk_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t rle7_int                     : 1;  /**< RxLane7 interrupt status. See ILK_RX_LNE7_INT */
	uint64_t rle6_int                     : 1;  /**< RxLane6 interrupt status. See ILK_RX_LNE6_INT */
	uint64_t rle5_int                     : 1;  /**< RxLane5 interrupt status. See ILK_RX_LNE5_INT */
	uint64_t rle4_int                     : 1;  /**< RxLane4 interrupt status. See ILK_RX_LNE4_INT */
	uint64_t rle3_int                     : 1;  /**< RxLane3 interrupt status. See ILK_RX_LNE3_INT */
	uint64_t rle2_int                     : 1;  /**< RxLane2 interrupt status. See ILK_RX_LNE2_INT */
	uint64_t rle1_int                     : 1;  /**< RxLane1 interrupt status. See ILK_RX_LNE1_INT */
	uint64_t rle0_int                     : 1;  /**< RxLane0 interrupt status. See ILK_RX_LNE0_INT */
	uint64_t rlk1_int                     : 1;  /**< RxLink1 interrupt status. See ILK_RX1_INT */
	uint64_t rlk0_int                     : 1;  /**< RxLink0 interrupt status. See ILK_RX0_INT */
	uint64_t tlk1_int                     : 1;  /**< TxLink1 interrupt status. See ILK_TX1_INT */
	uint64_t tlk0_int                     : 1;  /**< TxLink0 interrupt status. See ILK_TX0_INT */
	uint64_t gbl_int                      : 1;  /**< Global interrupt status. See ILK_GBL_INT */
#else
	uint64_t gbl_int                      : 1;
	uint64_t tlk0_int                     : 1;
	uint64_t tlk1_int                     : 1;
	uint64_t rlk0_int                     : 1;
	uint64_t rlk1_int                     : 1;
	uint64_t rle0_int                     : 1;
	uint64_t rle1_int                     : 1;
	uint64_t rle2_int                     : 1;
	uint64_t rle3_int                     : 1;
	uint64_t rle4_int                     : 1;
	uint64_t rle5_int                     : 1;
	uint64_t rle6_int                     : 1;
	uint64_t rle7_int                     : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_ilk_int_sum_s             cn68xx;
	struct cvmx_ilk_int_sum_s             cn68xxp1;
};
typedef union cvmx_ilk_int_sum cvmx_ilk_int_sum_t;

/**
 * cvmx_ilk_lne_dbg
 */
union cvmx_ilk_lne_dbg {
	uint64_t u64;
	struct cvmx_ilk_lne_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t tx_bad_crc32                 : 1;  /**< Send 1 diagnostic word with bad CRC32 to the selected lane.
                                                         Note: injects just once */
	uint64_t tx_bad_6467_cnt              : 5;  /**< Send N bad 64B/67B codewords on selected lane */
	uint64_t tx_bad_sync_cnt              : 3;  /**< Send N bad sync words on selected lane */
	uint64_t tx_bad_scram_cnt             : 3;  /**< Send N bad scram state on selected lane */
	uint64_t reserved_40_47               : 8;
	uint64_t tx_bad_lane_sel              : 8;  /**< Select lane to apply error injection counts */
	uint64_t reserved_24_31               : 8;
	uint64_t tx_dis_dispr                 : 8;  /**< Per-lane disparity disable */
	uint64_t reserved_8_15                : 8;
	uint64_t tx_dis_scram                 : 8;  /**< Per-lane scrambler disable */
#else
	uint64_t tx_dis_scram                 : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t tx_dis_dispr                 : 8;
	uint64_t reserved_24_31               : 8;
	uint64_t tx_bad_lane_sel              : 8;
	uint64_t reserved_40_47               : 8;
	uint64_t tx_bad_scram_cnt             : 3;
	uint64_t tx_bad_sync_cnt              : 3;
	uint64_t tx_bad_6467_cnt              : 5;
	uint64_t tx_bad_crc32                 : 1;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_ilk_lne_dbg_s             cn68xx;
	struct cvmx_ilk_lne_dbg_s             cn68xxp1;
};
typedef union cvmx_ilk_lne_dbg cvmx_ilk_lne_dbg_t;

/**
 * cvmx_ilk_lne_sts_msg
 */
union cvmx_ilk_lne_sts_msg {
	uint64_t u64;
	struct cvmx_ilk_lne_sts_msg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t rx_lnk_stat                  : 8;  /**< Link status received in the diagnostic word (per-lane) */
	uint64_t reserved_40_47               : 8;
	uint64_t rx_lne_stat                  : 8;  /**< Lane status received in the diagnostic word (per-lane) */
	uint64_t reserved_24_31               : 8;
	uint64_t tx_lnk_stat                  : 8;  /**< Link status transmitted in the diagnostic word (per-lane) */
	uint64_t reserved_8_15                : 8;
	uint64_t tx_lne_stat                  : 8;  /**< Lane status transmitted in the diagnostic word (per-lane) */
#else
	uint64_t tx_lne_stat                  : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t tx_lnk_stat                  : 8;
	uint64_t reserved_24_31               : 8;
	uint64_t rx_lne_stat                  : 8;
	uint64_t reserved_40_47               : 8;
	uint64_t rx_lnk_stat                  : 8;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ilk_lne_sts_msg_s         cn68xx;
	struct cvmx_ilk_lne_sts_msg_s         cn68xxp1;
};
typedef union cvmx_ilk_lne_sts_msg cvmx_ilk_lne_sts_msg_t;

/**
 * cvmx_ilk_rx#_cfg0
 */
union cvmx_ilk_rxx_cfg0 {
	uint64_t u64;
	struct cvmx_ilk_rxx_cfg0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ext_lpbk_fc                  : 1;  /**< Enable Rx-Tx flowcontrol loopback (external) */
	uint64_t ext_lpbk                     : 1;  /**< Enable Rx-Tx data loopback (external). Note that with differing
                                                         transmit & receive clocks, skip word are  inserted/deleted */
	uint64_t reserved_60_61               : 2;
	uint64_t lnk_stats_wrap               : 1;  /**< Upon overflow, a statistics counter should wrap instead of
                                                         saturating.

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t bcw_push                     : 1;  /**< The 8 byte burst control word containing the SOP will be
                                                         prepended to the corresponding packet.

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t mproto_ign                   : 1;  /**< When LA_MODE=1 and MPROTO_IGN=0, the multi-protocol bit of the
                                                         LA control word is used to determine if the burst is an LA or
                                                         non-LA burst.   When LA_MODE=1 and MPROTO_IGN=1, all bursts
                                                         are treated LA.   When LA_MODE=0, this field is ignored

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t ptrn_mode                    : 1;  /**< Enable programmable test pattern mode */
	uint64_t lnk_stats_rdclr              : 1;  /**< CSR read to ILK_RXx_STAT* clears the counter after returning
                                                         its current value. */
	uint64_t lnk_stats_ena                : 1;  /**< Enable link statistics counters */
	uint64_t mltuse_fc_ena                : 1;  /**< Use multi-use field for calendar */
	uint64_t cal_ena                      : 1;  /**< Enable Rx calendar.  When the calendar table is disabled, all
                                                         port-pipes receive XON. */
	uint64_t mfrm_len                     : 13; /**< The quantity of data sent on each lane including one sync word,
                                                         scrambler state, diag word, zero or more skip words, and the
                                                         data  payload.  Must be large than ILK_RXX_CFG1[SKIP_CNT]+9.
                                                         Supported range:ILK_RXX_CFG1[SKIP_CNT]+9 < MFRM_LEN <= 4096) */
	uint64_t brst_shrt                    : 7;  /**< Minimum interval between burst control words, as a multiple of
                                                         8 bytes.  Supported range from 8 bytes to 512 (ie. 0 <
                                                         BRST_SHRT <= 64)
                                                         This field affects the ILK_RX*_STAT4[BRST_SHRT_ERR_CNT]
                                                         counter. It does not affect correct operation of the link. */
	uint64_t lane_rev                     : 1;  /**< Lane reversal.   When enabled, lane de-striping is performed
                                                         from most significant lane enabled to least significant lane
                                                         enabled.  LANE_ENA must be zero before changing LANE_REV. */
	uint64_t brst_max                     : 5;  /**< Maximum size of a data burst, as a multiple of 64 byte blocks.
                                                         Supported range is from 64 bytes to  1024 bytes. (ie. 0 <
                                                         BRST_MAX <= 16)
                                                         This field affects the ILK_RX*_STAT2[BRST_NOT_FULL_CNT] and
                                                         ILK_RX*_STAT3[BRST_MAX_ERR_CNT] counters. It does not affect
                                                         correct operation of the link. */
	uint64_t reserved_25_25               : 1;
	uint64_t cal_depth                    : 9;  /**< Number of valid entries in the calendar.   Supported range from
                                                         1 to 288. */
	uint64_t reserved_8_15                : 8;
	uint64_t lane_ena                     : 8;  /**< Lane enable mask.  Link is enabled if any lane is enabled.  The
                                                         same lane should not be enabled in multiple ILK_RXx_CFG0.  Each
                                                         bit of LANE_ENA maps to a RX lane (RLE) and a QLM lane.  NOTE:
                                                         LANE_REV has no effect on this mapping.

                                                               LANE_ENA[0] = RLE0 = QLM1 lane 0
                                                               LANE_ENA[1] = RLE1 = QLM1 lane 1
                                                               LANE_ENA[2] = RLE2 = QLM1 lane 2
                                                               LANE_ENA[3] = RLE3 = QLM1 lane 3
                                                               LANE_ENA[4] = RLE4 = QLM2 lane 0
                                                               LANE_ENA[5] = RLE5 = QLM2 lane 1
                                                               LANE_ENA[6] = RLE6 = QLM2 lane 2
                                                               LANE_ENA[7] = RLE7 = QLM2 lane 3 */
#else
	uint64_t lane_ena                     : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t cal_depth                    : 9;
	uint64_t reserved_25_25               : 1;
	uint64_t brst_max                     : 5;
	uint64_t lane_rev                     : 1;
	uint64_t brst_shrt                    : 7;
	uint64_t mfrm_len                     : 13;
	uint64_t cal_ena                      : 1;
	uint64_t mltuse_fc_ena                : 1;
	uint64_t lnk_stats_ena                : 1;
	uint64_t lnk_stats_rdclr              : 1;
	uint64_t ptrn_mode                    : 1;
	uint64_t mproto_ign                   : 1;
	uint64_t bcw_push                     : 1;
	uint64_t lnk_stats_wrap               : 1;
	uint64_t reserved_60_61               : 2;
	uint64_t ext_lpbk                     : 1;
	uint64_t ext_lpbk_fc                  : 1;
#endif
	} s;
	struct cvmx_ilk_rxx_cfg0_s            cn68xx;
	struct cvmx_ilk_rxx_cfg0_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ext_lpbk_fc                  : 1;  /**< Enable Rx-Tx flowcontrol loopback (external) */
	uint64_t ext_lpbk                     : 1;  /**< Enable Rx-Tx data loopback (external). Note that with differing
                                                         transmit & receive clocks, skip word are  inserted/deleted */
	uint64_t reserved_57_61               : 5;
	uint64_t ptrn_mode                    : 1;  /**< Enable programmable test pattern mode */
	uint64_t lnk_stats_rdclr              : 1;  /**< CSR read to ILK_RXx_STAT* clears the counter after returning
                                                         its current value. */
	uint64_t lnk_stats_ena                : 1;  /**< Enable link statistics counters */
	uint64_t mltuse_fc_ena                : 1;  /**< Use multi-use field for calendar */
	uint64_t cal_ena                      : 1;  /**< Enable Rx calendar.  When the calendar table is disabled, all
                                                         port-pipes receive XON. */
	uint64_t mfrm_len                     : 13; /**< The quantity of data sent on each lane including one sync word,
                                                         scrambler state, diag word, zero or more skip words, and the
                                                         data  payload.  Must be large than ILK_RXX_CFG1[SKIP_CNT]+9.
                                                         Supported range:ILK_RXX_CFG1[SKIP_CNT]+9 < MFRM_LEN <= 4096) */
	uint64_t brst_shrt                    : 7;  /**< Minimum interval between burst control words, as a multiple of
                                                         8 bytes.  Supported range from 8 bytes to 512 (ie. 0 <
                                                         BRST_SHRT <= 64)
                                                         This field affects the ILK_RX*_STAT4[BRST_SHRT_ERR_CNT]
                                                         counter. It does not affect correct operation of the link. */
	uint64_t lane_rev                     : 1;  /**< Lane reversal.   When enabled, lane de-striping is performed
                                                         from most significant lane enabled to least significant lane
                                                         enabled.  LANE_ENA must be zero before changing LANE_REV. */
	uint64_t brst_max                     : 5;  /**< Maximum size of a data burst, as a multiple of 64 byte blocks.
                                                         Supported range is from 64 bytes to  1024 bytes. (ie. 0 <
                                                         BRST_MAX <= 16)
                                                         This field affects the ILK_RX*_STAT2[BRST_NOT_FULL_CNT] and
                                                         ILK_RX*_STAT3[BRST_MAX_ERR_CNT] counters. It does not affect
                                                         correct operation of the link. */
	uint64_t reserved_25_25               : 1;
	uint64_t cal_depth                    : 9;  /**< Number of valid entries in the calendar.   Supported range from
                                                         1 to 288. */
	uint64_t reserved_8_15                : 8;
	uint64_t lane_ena                     : 8;  /**< Lane enable mask.  Link is enabled if any lane is enabled.  The
                                                         same lane should not be enabled in multiple ILK_RXx_CFG0.  Each
                                                         bit of LANE_ENA maps to a RX lane (RLE) and a QLM lane.  NOTE:
                                                         LANE_REV has no effect on this mapping.

                                                               LANE_ENA[0] = RLE0 = QLM1 lane 0
                                                               LANE_ENA[1] = RLE1 = QLM1 lane 1
                                                               LANE_ENA[2] = RLE2 = QLM1 lane 2
                                                               LANE_ENA[3] = RLE3 = QLM1 lane 3
                                                               LANE_ENA[4] = RLE4 = QLM2 lane 0
                                                               LANE_ENA[5] = RLE5 = QLM2 lane 1
                                                               LANE_ENA[6] = RLE6 = QLM2 lane 2
                                                               LANE_ENA[7] = RLE7 = QLM2 lane 3 */
#else
	uint64_t lane_ena                     : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t cal_depth                    : 9;
	uint64_t reserved_25_25               : 1;
	uint64_t brst_max                     : 5;
	uint64_t lane_rev                     : 1;
	uint64_t brst_shrt                    : 7;
	uint64_t mfrm_len                     : 13;
	uint64_t cal_ena                      : 1;
	uint64_t mltuse_fc_ena                : 1;
	uint64_t lnk_stats_ena                : 1;
	uint64_t lnk_stats_rdclr              : 1;
	uint64_t ptrn_mode                    : 1;
	uint64_t reserved_57_61               : 5;
	uint64_t ext_lpbk                     : 1;
	uint64_t ext_lpbk_fc                  : 1;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rxx_cfg0 cvmx_ilk_rxx_cfg0_t;

/**
 * cvmx_ilk_rx#_cfg1
 */
union cvmx_ilk_rxx_cfg1 {
	uint64_t u64;
	struct cvmx_ilk_rxx_cfg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_62_63               : 2;
	uint64_t rx_fifo_cnt                  : 12; /**< Number of 64-bit words currently consumed by this link in the
                                                         RX fifo. */
	uint64_t reserved_48_49               : 2;
	uint64_t rx_fifo_hwm                  : 12; /**< Number of 64-bit words consumed by this link before switch
                                                         transmitted link flow control status from XON to XOFF.

                                                         XON  = RX_FIFO_CNT < RX_FIFO_HWM
                                                         XOFF = RX_FIFO_CNT >= RX_FIFO_HWM. */
	uint64_t reserved_34_35               : 2;
	uint64_t rx_fifo_max                  : 12; /**< Maximum number of 64-bit words consumed by this link in the RX
                                                         fifo.  The sum of all links should be equal to 2048 (16KB) */
	uint64_t pkt_flush                    : 1;  /**< Packet receive flush.  Writing PKT_FLUSH=1 will cause all open
                                                         packets to be error-out, just as though the link went down. */
	uint64_t pkt_ena                      : 1;  /**< Packet receive enable.  When PKT_ENA=0, any received SOP causes
                                                         the entire packet to be dropped. */
	uint64_t la_mode                      : 1;  /**< 0 = Interlaken
                                                         1 = Interlaken Look-Aside */
	uint64_t tx_link_fc                   : 1;  /**< Link flow control status transmitted by the Tx-Link
                                                         XON when RX_FIFO_CNT <= RX_FIFO_HWM and lane alignment is done */
	uint64_t rx_link_fc                   : 1;  /**< Link flow control status received in burst/idle control words.
                                                         XOFF will cause Tx-Link to stop transmitting on all channels. */
	uint64_t rx_align_ena                 : 1;  /**< Enable the lane alignment.  This should only be done after all
                                                         enabled lanes have achieved word boundary lock and scrambler
                                                         synchronization.  Note: Hardware will clear this when any
                                                         participating lane loses either word boundary lock or scrambler
                                                         synchronization */
	uint64_t reserved_8_15                : 8;
	uint64_t rx_bdry_lock_ena             : 8;  /**< Enable word boundary lock.  While disabled, received data is
                                                         tossed.  Once enabled,  received data is searched for legal
                                                         2bit patterns.  Automatically cleared for disabled lanes. */
#else
	uint64_t rx_bdry_lock_ena             : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t rx_align_ena                 : 1;
	uint64_t rx_link_fc                   : 1;
	uint64_t tx_link_fc                   : 1;
	uint64_t la_mode                      : 1;
	uint64_t pkt_ena                      : 1;
	uint64_t pkt_flush                    : 1;
	uint64_t rx_fifo_max                  : 12;
	uint64_t reserved_34_35               : 2;
	uint64_t rx_fifo_hwm                  : 12;
	uint64_t reserved_48_49               : 2;
	uint64_t rx_fifo_cnt                  : 12;
	uint64_t reserved_62_63               : 2;
#endif
	} s;
	struct cvmx_ilk_rxx_cfg1_s            cn68xx;
	struct cvmx_ilk_rxx_cfg1_s            cn68xxp1;
};
typedef union cvmx_ilk_rxx_cfg1 cvmx_ilk_rxx_cfg1_t;

/**
 * cvmx_ilk_rx#_flow_ctl0
 */
union cvmx_ilk_rxx_flow_ctl0 {
	uint64_t u64;
	struct cvmx_ilk_rxx_flow_ctl0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t status                       : 64; /**< Flow control status for port-pipes 63-0, where a 1 indicates
                                                         the presence of backpressure (ie. XOFF) and 0 indicates the
                                                         absence of backpressure (ie. XON) */
#else
	uint64_t status                       : 64;
#endif
	} s;
	struct cvmx_ilk_rxx_flow_ctl0_s       cn68xx;
	struct cvmx_ilk_rxx_flow_ctl0_s       cn68xxp1;
};
typedef union cvmx_ilk_rxx_flow_ctl0 cvmx_ilk_rxx_flow_ctl0_t;

/**
 * cvmx_ilk_rx#_flow_ctl1
 */
union cvmx_ilk_rxx_flow_ctl1 {
	uint64_t u64;
	struct cvmx_ilk_rxx_flow_ctl1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t status                       : 64; /**< Flow control status for port-pipes 127-64, where a 1 indicates
                                                         the presence of backpressure (ie. XOFF) and 0 indicates the
                                                         absence of backpressure (ie. XON) */
#else
	uint64_t status                       : 64;
#endif
	} s;
	struct cvmx_ilk_rxx_flow_ctl1_s       cn68xx;
	struct cvmx_ilk_rxx_flow_ctl1_s       cn68xxp1;
};
typedef union cvmx_ilk_rxx_flow_ctl1 cvmx_ilk_rxx_flow_ctl1_t;

/**
 * cvmx_ilk_rx#_idx_cal
 */
union cvmx_ilk_rxx_idx_cal {
	uint64_t u64;
	struct cvmx_ilk_rxx_idx_cal_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t inc                          : 6;  /**< Increment to add to current index for next index. NOTE:
                                                         Increment performed after access to   ILK_RXx_MEM_CAL1 */
	uint64_t reserved_6_7                 : 2;
	uint64_t index                        : 6;  /**< Specify the group of 8 entries accessed by the next CSR
                                                         read/write to calendar table memory.  Software must never write
                                                         IDX >= 36 */
#else
	uint64_t index                        : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t inc                          : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_ilk_rxx_idx_cal_s         cn68xx;
	struct cvmx_ilk_rxx_idx_cal_s         cn68xxp1;
};
typedef union cvmx_ilk_rxx_idx_cal cvmx_ilk_rxx_idx_cal_t;

/**
 * cvmx_ilk_rx#_idx_stat0
 */
union cvmx_ilk_rxx_idx_stat0 {
	uint64_t u64;
	struct cvmx_ilk_rxx_idx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t clr                          : 1;  /**< CSR read to ILK_RXx_MEM_STAT0 clears the selected counter after
                                                         returning its current value. */
	uint64_t reserved_24_30               : 7;
	uint64_t inc                          : 8;  /**< Increment to add to current index for next index */
	uint64_t reserved_8_15                : 8;
	uint64_t index                        : 8;  /**< Specify the channel accessed during the next CSR read to the
                                                         ILK_RXx_MEM_STAT0 */
#else
	uint64_t index                        : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t inc                          : 8;
	uint64_t reserved_24_30               : 7;
	uint64_t clr                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ilk_rxx_idx_stat0_s       cn68xx;
	struct cvmx_ilk_rxx_idx_stat0_s       cn68xxp1;
};
typedef union cvmx_ilk_rxx_idx_stat0 cvmx_ilk_rxx_idx_stat0_t;

/**
 * cvmx_ilk_rx#_idx_stat1
 */
union cvmx_ilk_rxx_idx_stat1 {
	uint64_t u64;
	struct cvmx_ilk_rxx_idx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t clr                          : 1;  /**< CSR read to ILK_RXx_MEM_STAT1 clears the selected counter after
                                                         returning its current value. */
	uint64_t reserved_24_30               : 7;
	uint64_t inc                          : 8;  /**< Increment to add to current index for next index */
	uint64_t reserved_8_15                : 8;
	uint64_t index                        : 8;  /**< Specify the channel accessed during the next CSR read to the
                                                         ILK_RXx_MEM_STAT1 */
#else
	uint64_t index                        : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t inc                          : 8;
	uint64_t reserved_24_30               : 7;
	uint64_t clr                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ilk_rxx_idx_stat1_s       cn68xx;
	struct cvmx_ilk_rxx_idx_stat1_s       cn68xxp1;
};
typedef union cvmx_ilk_rxx_idx_stat1 cvmx_ilk_rxx_idx_stat1_t;

/**
 * cvmx_ilk_rx#_int
 */
union cvmx_ilk_rxx_int {
	uint64_t u64;
	struct cvmx_ilk_rxx_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t pkt_drop_sop                 : 1;  /**< Entire packet dropped due to RX_FIFO_CNT == RX_FIFO_MAX,
                                                         lack of reassembly-ids or because ILK_RXX_CFG1[PKT_ENA]=0      | $RW
                                                         because ILK_RXX_CFG1[PKT_ENA]=0

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t pkt_drop_rid                 : 1;  /**< Entire packet dropped due to the lack of reassembly-ids or
                                                         because ILK_RXX_CFG1[PKT_ENA]=0 */
	uint64_t pkt_drop_rxf                 : 1;  /**< Some/all of a packet dropped due to RX_FIFO_CNT == RX_FIFO_MAX */
	uint64_t lane_bad_word                : 1;  /**< A lane encountered either a bad 64B/67B codeword or an unknown
                                                         control word type. */
	uint64_t stat_cnt_ovfl                : 1;  /**< Statistics counter overflow */
	uint64_t lane_align_done              : 1;  /**< Lane alignment successful */
	uint64_t word_sync_done               : 1;  /**< All enabled lanes have achieved word boundary lock and
                                                         scrambler synchronization.  Lane alignment may now be enabled. */
	uint64_t crc24_err                    : 1;  /**< Burst CRC24 error.  All open packets will be receive an error. */
	uint64_t lane_align_fail              : 1;  /**< Lane Alignment fails (4 tries).  Hardware will repeat lane
                                                         alignment until is succeeds or until ILK_RXx_CFG1[RX_ALIGN_ENA]
                                                         is cleared. */
#else
	uint64_t lane_align_fail              : 1;
	uint64_t crc24_err                    : 1;
	uint64_t word_sync_done               : 1;
	uint64_t lane_align_done              : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t lane_bad_word                : 1;
	uint64_t pkt_drop_rxf                 : 1;
	uint64_t pkt_drop_rid                 : 1;
	uint64_t pkt_drop_sop                 : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_ilk_rxx_int_s             cn68xx;
	struct cvmx_ilk_rxx_int_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pkt_drop_rid                 : 1;  /**< Entire packet dropped due to the lack of reassembly-ids or
                                                         because ILK_RXX_CFG1[PKT_ENA]=0 */
	uint64_t pkt_drop_rxf                 : 1;  /**< Some/all of a packet dropped due to RX_FIFO_CNT == RX_FIFO_MAX */
	uint64_t lane_bad_word                : 1;  /**< A lane encountered either a bad 64B/67B codeword or an unknown
                                                         control word type. */
	uint64_t stat_cnt_ovfl                : 1;  /**< Statistics counter overflow */
	uint64_t lane_align_done              : 1;  /**< Lane alignment successful */
	uint64_t word_sync_done               : 1;  /**< All enabled lanes have achieved word boundary lock and
                                                         scrambler synchronization.  Lane alignment may now be enabled. */
	uint64_t crc24_err                    : 1;  /**< Burst CRC24 error.  All open packets will be receive an error. */
	uint64_t lane_align_fail              : 1;  /**< Lane Alignment fails (4 tries).  Hardware will repeat lane
                                                         alignment until is succeeds or until ILK_RXx_CFG1[RX_ALIGN_ENA]
                                                         is cleared. */
#else
	uint64_t lane_align_fail              : 1;
	uint64_t crc24_err                    : 1;
	uint64_t word_sync_done               : 1;
	uint64_t lane_align_done              : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t lane_bad_word                : 1;
	uint64_t pkt_drop_rxf                 : 1;
	uint64_t pkt_drop_rid                 : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rxx_int cvmx_ilk_rxx_int_t;

/**
 * cvmx_ilk_rx#_int_en
 */
union cvmx_ilk_rxx_int_en {
	uint64_t u64;
	struct cvmx_ilk_rxx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t pkt_drop_sop                 : 1;  /**< Entire packet dropped due to RX_FIFO_CNT == RX_FIFO_MAX,
                                                         lack of reassembly-ids or because ILK_RXX_CFG1[PKT_ENA]=0      | $PRW
                                                         because ILK_RXX_CFG1[PKT_ENA]=0

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t pkt_drop_rid                 : 1;  /**< Entire packet dropped due to the lack of reassembly-ids or
                                                         because ILK_RXX_CFG1[PKT_ENA]=0 */
	uint64_t pkt_drop_rxf                 : 1;  /**< Some/all of a packet dropped due to RX_FIFO_CNT == RX_FIFO_MAX */
	uint64_t lane_bad_word                : 1;  /**< A lane encountered either a bad 64B/67B codeword or an unknown
                                                         control word type. */
	uint64_t stat_cnt_ovfl                : 1;  /**< Statistics counter overflow */
	uint64_t lane_align_done              : 1;  /**< Lane alignment successful */
	uint64_t word_sync_done               : 1;  /**< All enabled lanes have achieved word boundary lock and
                                                         scrambler synchronization.  Lane alignment may now be enabled. */
	uint64_t crc24_err                    : 1;  /**< Burst CRC24 error.  All open packets will be receive an error. */
	uint64_t lane_align_fail              : 1;  /**< Lane Alignment fails (4 tries) */
#else
	uint64_t lane_align_fail              : 1;
	uint64_t crc24_err                    : 1;
	uint64_t word_sync_done               : 1;
	uint64_t lane_align_done              : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t lane_bad_word                : 1;
	uint64_t pkt_drop_rxf                 : 1;
	uint64_t pkt_drop_rid                 : 1;
	uint64_t pkt_drop_sop                 : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_ilk_rxx_int_en_s          cn68xx;
	struct cvmx_ilk_rxx_int_en_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pkt_drop_rid                 : 1;  /**< Entire packet dropped due to the lack of reassembly-ids or
                                                         because ILK_RXX_CFG1[PKT_ENA]=0 */
	uint64_t pkt_drop_rxf                 : 1;  /**< Some/all of a packet dropped due to RX_FIFO_CNT == RX_FIFO_MAX */
	uint64_t lane_bad_word                : 1;  /**< A lane encountered either a bad 64B/67B codeword or an unknown
                                                         control word type. */
	uint64_t stat_cnt_ovfl                : 1;  /**< Statistics counter overflow */
	uint64_t lane_align_done              : 1;  /**< Lane alignment successful */
	uint64_t word_sync_done               : 1;  /**< All enabled lanes have achieved word boundary lock and
                                                         scrambler synchronization.  Lane alignment may now be enabled. */
	uint64_t crc24_err                    : 1;  /**< Burst CRC24 error.  All open packets will be receive an error. */
	uint64_t lane_align_fail              : 1;  /**< Lane Alignment fails (4 tries) */
#else
	uint64_t lane_align_fail              : 1;
	uint64_t crc24_err                    : 1;
	uint64_t word_sync_done               : 1;
	uint64_t lane_align_done              : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t lane_bad_word                : 1;
	uint64_t pkt_drop_rxf                 : 1;
	uint64_t pkt_drop_rid                 : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rxx_int_en cvmx_ilk_rxx_int_en_t;

/**
 * cvmx_ilk_rx#_jabber
 */
union cvmx_ilk_rxx_jabber {
	uint64_t u64;
	struct cvmx_ilk_rxx_jabber_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t cnt                          : 16; /**< Byte count for jabber check.   Failing packets will be
                                                         truncated to CNT bytes.

                                                         NOTE: Hardware tracks the size of up to two concurrent packet
                                                         per link.  If using segment mode with more than 2 channels,
                                                         some large packets may not be flagged or truncated.

                                                         NOTE: CNT must be 8-byte aligned such that CNT[2:0] == 0 */
#else
	uint64_t cnt                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ilk_rxx_jabber_s          cn68xx;
	struct cvmx_ilk_rxx_jabber_s          cn68xxp1;
};
typedef union cvmx_ilk_rxx_jabber cvmx_ilk_rxx_jabber_t;

/**
 * cvmx_ilk_rx#_mem_cal0
 *
 * Notes:
 * Software must program the calendar table prior to enabling the
 * link.
 *
 * Software must always write ILK_RXx_MEM_CAL0 then ILK_RXx_MEM_CAL1.
 * Software must never write them in reverse order or write one without
 * writing the other.
 *
 * A given calendar table entry has no effect on PKO pipe
 * backpressure when either:
 *  - ENTRY_CTLx=Link (1), or
 *  - ENTRY_CTLx=XON (3) and PORT_PIPEx is outside the range of ILK_TXx_PIPE[BASE/NUMP].
 *
 * Within the 8 calendar table entries of one IDX value, if more
 * than one affects the same PKO pipe, XOFF always wins over XON,
 * regardless of the calendar table order.
 *
 * Software must always read ILK_RXx_MEM_CAL0 then ILK_RXx_MEM_CAL1.  Software
 * must never read them in reverse order or read one without reading the
 * other.
 */
union cvmx_ilk_rxx_mem_cal0 {
	uint64_t u64;
	struct cvmx_ilk_rxx_mem_cal0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t entry_ctl3                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+3

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE3.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE3 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE3.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE3. The calendar table entry is
                                                                            effectively unused if PORT_PIPE3 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe3                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+3

                                                         PORT_PIPE3 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL3 is "XOFF" (2) or "PKO port-pipe" (0). */
	uint64_t entry_ctl2                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+2

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE2.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE2 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE2.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE2. The calendar table entry is
                                                                            effectively unused if PORT_PIPE2 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe2                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+2

                                                         PORT_PIPE2 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL2 is "XOFF" (2) or "PKO port-pipe" (0). */
	uint64_t entry_ctl1                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+1

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE1.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE1 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE1.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE1. The calendar table entry is
                                                                            effectively unused if PORT_PIPE1 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe1                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+1

                                                         PORT_PIPE1 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL1 is "XOFF" (2) or "PKO port-pipe" (0). */
	uint64_t entry_ctl0                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+0

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE0.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE0 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE0.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE0. The calendar table entry is
                                                                            effectively unused if PORT_PIPEx is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe0                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+0

                                                         PORT_PIPE0 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL0 is "XOFF" (2) or "PKO port-pipe" (0). */
#else
	uint64_t port_pipe0                   : 7;
	uint64_t entry_ctl0                   : 2;
	uint64_t port_pipe1                   : 7;
	uint64_t entry_ctl1                   : 2;
	uint64_t port_pipe2                   : 7;
	uint64_t entry_ctl2                   : 2;
	uint64_t port_pipe3                   : 7;
	uint64_t entry_ctl3                   : 2;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_ilk_rxx_mem_cal0_s        cn68xx;
	struct cvmx_ilk_rxx_mem_cal0_s        cn68xxp1;
};
typedef union cvmx_ilk_rxx_mem_cal0 cvmx_ilk_rxx_mem_cal0_t;

/**
 * cvmx_ilk_rx#_mem_cal1
 *
 * Notes:
 * Software must program the calendar table prior to enabling the
 * link.
 *
 * Software must always write ILK_RXx_MEM_CAL0 then ILK_RXx_MEM_CAL1.
 * Software must never write them in reverse order or write one without
 * writing the other.
 *
 * A given calendar table entry has no effect on PKO pipe
 * backpressure when either:
 *  - ENTRY_CTLx=Link (1), or
 *  - ENTRY_CTLx=XON (3) and PORT_PIPEx is outside the range of ILK_TXx_PIPE[BASE/NUMP].
 *
 * Within the 8 calendar table entries of one IDX value, if more
 * than one affects the same PKO pipe, XOFF always wins over XON,
 * regardless of the calendar table order.
 *
 * Software must always read ILK_RXx_MEM_CAL0 then ILK_Rx_MEM_CAL1.  Software
 * must never read them in reverse order or read one without reading the
 * other.
 */
union cvmx_ilk_rxx_mem_cal1 {
	uint64_t u64;
	struct cvmx_ilk_rxx_mem_cal1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t entry_ctl7                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+7

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE7.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE7 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE7.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE7. The calendar table entry is
                                                                            effectively unused if PORT_PIPE3 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe7                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+7

                                                         PORT_PIPE7 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL7 is "XOFF" (2) or "PKO port-pipe" (0). */
	uint64_t entry_ctl6                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+6

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE6.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE6 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE6.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE6. The calendar table entry is
                                                                            effectively unused if PORT_PIPE6 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe6                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+6

                                                         PORT_PIPE6 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL6 is "XOFF" (2) or "PKO port-pipe" (0). */
	uint64_t entry_ctl5                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+5

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE5.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE5 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE5.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE5. The calendar table entry is
                                                                            effectively unused if PORT_PIPE5 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe5                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+5

                                                         PORT_PIPE5 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL5 is "XOFF" (2) or "PKO port-pipe" (0). */
	uint64_t entry_ctl4                   : 2;  /**< XON/XOFF destination for entry (IDX*8)+4

                                                         - 0: PKO port-pipe  Apply backpressure received from the
                                                                            remote tranmitter to the PKO pipe selected
                                                                            by PORT_PIPE4.

                                                         - 1: Link           Apply the backpressure received from the
                                                                            remote transmitter to link backpressure.
                                                                            PORT_PIPE4 is unused.

                                                         - 2: XOFF           Apply XOFF to the PKO pipe selected by
                                                                            PORT_PIPE4.

                                                         - 3: XON            Apply XON to the PKO pipe selected by
                                                                            PORT_PIPE4. The calendar table entry is
                                                                            effectively unused if PORT_PIPE4 is out of
                                                                            range of ILK_TXx_PIPE[BASE/NUMP]. */
	uint64_t port_pipe4                   : 7;  /**< Select PKO port-pipe for calendar table entry (IDX*8)+4

                                                         PORT_PIPE4 must reside in the range of ILK_TXx_PIPE[BASE/NUMP]
                                                         when ENTRY_CTL4 is "XOFF" (2) or "PKO port-pipe" (0). */
#else
	uint64_t port_pipe4                   : 7;
	uint64_t entry_ctl4                   : 2;
	uint64_t port_pipe5                   : 7;
	uint64_t entry_ctl5                   : 2;
	uint64_t port_pipe6                   : 7;
	uint64_t entry_ctl6                   : 2;
	uint64_t port_pipe7                   : 7;
	uint64_t entry_ctl7                   : 2;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_ilk_rxx_mem_cal1_s        cn68xx;
	struct cvmx_ilk_rxx_mem_cal1_s        cn68xxp1;
};
typedef union cvmx_ilk_rxx_mem_cal1 cvmx_ilk_rxx_mem_cal1_t;

/**
 * cvmx_ilk_rx#_mem_stat0
 */
union cvmx_ilk_rxx_mem_stat0 {
	uint64_t u64;
	struct cvmx_ilk_rxx_mem_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t rx_pkt                       : 28; /**< Number of packets received (256M)
                                                         Channel selected by ILK_RXx_IDX_STAT0[IDX].  Saturates.
                                                         Interrupt on saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t rx_pkt                       : 28;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_ilk_rxx_mem_stat0_s       cn68xx;
	struct cvmx_ilk_rxx_mem_stat0_s       cn68xxp1;
};
typedef union cvmx_ilk_rxx_mem_stat0 cvmx_ilk_rxx_mem_stat0_t;

/**
 * cvmx_ilk_rx#_mem_stat1
 */
union cvmx_ilk_rxx_mem_stat1 {
	uint64_t u64;
	struct cvmx_ilk_rxx_mem_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t rx_bytes                     : 36; /**< Number of bytes received (64GB)
                                                         Channel selected by ILK_RXx_IDX_STAT1[IDX].    Saturates.
                                                         Interrupt on saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t rx_bytes                     : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_ilk_rxx_mem_stat1_s       cn68xx;
	struct cvmx_ilk_rxx_mem_stat1_s       cn68xxp1;
};
typedef union cvmx_ilk_rxx_mem_stat1 cvmx_ilk_rxx_mem_stat1_t;

/**
 * cvmx_ilk_rx#_rid
 */
union cvmx_ilk_rxx_rid {
	uint64_t u64;
	struct cvmx_ilk_rxx_rid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t max_cnt                      : 6;  /**< Maximum number of reassembly-ids allowed for a given link.  If
                                                         an SOP arrives and the link has already allocated at least
                                                         MAX_CNT reassembly-ids, the packet will be dropped.

                                                         Note: An an SOP allocates a reassembly-ids.
                                                         Note: An an EOP frees a reassembly-ids.

                                                         ***NOTE: Added in pass 2.0 */
#else
	uint64_t max_cnt                      : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_ilk_rxx_rid_s             cn68xx;
};
typedef union cvmx_ilk_rxx_rid cvmx_ilk_rxx_rid_t;

/**
 * cvmx_ilk_rx#_stat0
 */
union cvmx_ilk_rxx_stat0 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t crc24_match_cnt              : 33; /**< Number of CRC24 matches received.  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t crc24_match_cnt              : 33;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_ilk_rxx_stat0_s           cn68xx;
	struct cvmx_ilk_rxx_stat0_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t crc24_match_cnt              : 27; /**< Number of CRC24 matches received.  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t crc24_match_cnt              : 27;
	uint64_t reserved_27_63               : 37;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat0 cvmx_ilk_rxx_stat0_t;

/**
 * cvmx_ilk_rx#_stat1
 */
union cvmx_ilk_rxx_stat1 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t crc24_err_cnt                : 18; /**< Number of bursts with a detected CRC error.  Saturates.
                                                         Interrupt on saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t crc24_err_cnt                : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rxx_stat1_s           cn68xx;
	struct cvmx_ilk_rxx_stat1_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat1 cvmx_ilk_rxx_stat1_t;

/**
 * cvmx_ilk_rx#_stat2
 */
union cvmx_ilk_rxx_stat2 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t brst_not_full_cnt            : 16; /**< Number of bursts received which terminated without an eop and
                                                         contained fewer than BurstMax words.  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
	uint64_t reserved_28_31               : 4;
	uint64_t brst_cnt                     : 28; /**< Number of bursts correctly received. (ie. good CRC24, not in
                                                         violation of BurstMax or BurstShort) */
#else
	uint64_t brst_cnt                     : 28;
	uint64_t reserved_28_31               : 4;
	uint64_t brst_not_full_cnt            : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_ilk_rxx_stat2_s           cn68xx;
	struct cvmx_ilk_rxx_stat2_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t brst_not_full_cnt            : 16; /**< Number of bursts received which terminated without an eop and
                                                         contained fewer than BurstMax words.  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
	uint64_t reserved_16_31               : 16;
	uint64_t brst_cnt                     : 16; /**< Number of bursts correctly received. (ie. good CRC24, not in
                                                         violation of BurstMax or BurstShort) */
#else
	uint64_t brst_cnt                     : 16;
	uint64_t reserved_16_31               : 16;
	uint64_t brst_not_full_cnt            : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat2 cvmx_ilk_rxx_stat2_t;

/**
 * cvmx_ilk_rx#_stat3
 */
union cvmx_ilk_rxx_stat3 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t brst_max_err_cnt             : 16; /**< Number of bursts received longer than the BurstMax parameter */
#else
	uint64_t brst_max_err_cnt             : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ilk_rxx_stat3_s           cn68xx;
	struct cvmx_ilk_rxx_stat3_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat3 cvmx_ilk_rxx_stat3_t;

/**
 * cvmx_ilk_rx#_stat4
 */
union cvmx_ilk_rxx_stat4 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t brst_shrt_err_cnt            : 16; /**< Number of bursts received that violate the BurstShort
                                                         parameter.  Saturates.  Interrupt on saturation if
                                                         ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t brst_shrt_err_cnt            : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ilk_rxx_stat4_s           cn68xx;
	struct cvmx_ilk_rxx_stat4_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat4 cvmx_ilk_rxx_stat4_t;

/**
 * cvmx_ilk_rx#_stat5
 */
union cvmx_ilk_rxx_stat5 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t align_cnt                    : 23; /**< Number of alignment sequences received  (ie. those that do not
                                                         violate the current alignment).  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t align_cnt                    : 23;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_ilk_rxx_stat5_s           cn68xx;
	struct cvmx_ilk_rxx_stat5_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t align_cnt                    : 16; /**< Number of alignment sequences received  (ie. those that do not
                                                         violate the current alignment).  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t align_cnt                    : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat5 cvmx_ilk_rxx_stat5_t;

/**
 * cvmx_ilk_rx#_stat6
 */
union cvmx_ilk_rxx_stat6 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t align_err_cnt                : 16; /**< Number of alignment sequences received in error (ie. those that
                                                         violate the current alignment).  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t align_err_cnt                : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ilk_rxx_stat6_s           cn68xx;
	struct cvmx_ilk_rxx_stat6_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat6 cvmx_ilk_rxx_stat6_t;

/**
 * cvmx_ilk_rx#_stat7
 */
union cvmx_ilk_rxx_stat7 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t bad_64b67b_cnt               : 16; /**< Number of bad 64B/67B codewords.  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t bad_64b67b_cnt               : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ilk_rxx_stat7_s           cn68xx;
	struct cvmx_ilk_rxx_stat7_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat7 cvmx_ilk_rxx_stat7_t;

/**
 * cvmx_ilk_rx#_stat8
 */
union cvmx_ilk_rxx_stat8 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat8_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pkt_drop_rid_cnt             : 16; /**< Number of packets dropped due to the lack of reassembly-ids or
                                                         because ILK_RXX_CFG1[PKT_ENA]=0.  Saturates.  Interrupt on
                                                         saturation if ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
	uint64_t pkt_drop_rxf_cnt             : 16; /**< Number of packets dropped due to RX_FIFO_CNT >= RX_FIFO_MAX.
                                                         Saturates.  Interrupt on saturation if
                                                         ILK_RXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t pkt_drop_rxf_cnt             : 16;
	uint64_t pkt_drop_rid_cnt             : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ilk_rxx_stat8_s           cn68xx;
	struct cvmx_ilk_rxx_stat8_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat8 cvmx_ilk_rxx_stat8_t;

/**
 * cvmx_ilk_rx#_stat9
 */
union cvmx_ilk_rxx_stat9 {
	uint64_t u64;
	struct cvmx_ilk_rxx_stat9_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_ilk_rxx_stat9_s           cn68xx;
	struct cvmx_ilk_rxx_stat9_s           cn68xxp1;
};
typedef union cvmx_ilk_rxx_stat9 cvmx_ilk_rxx_stat9_t;

/**
 * cvmx_ilk_rx_lne#_cfg
 */
union cvmx_ilk_rx_lnex_cfg {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t rx_dis_psh_skip              : 1;  /**< When RX_DIS_PSH_SKIP=0, skip words are de-stripped.
                                                         When RX_DIS_PSH_SKIP=1, skip words are discarded in the lane
                                                         logic.

                                                         If the lane is in internal loopback mode, RX_DIS_PSH_SKIP
                                                         is ignored and skip words are always discarded in the lane
                                                         logic.

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t reserved_6_7                 : 2;
	uint64_t rx_scrm_sync                 : 1;  /**< Rx scrambler synchronization status

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t rx_bdry_sync                 : 1;  /**< Rx word boundary sync status */
	uint64_t rx_dis_ukwn                  : 1;  /**< Disable normal response to unknown words.  They are still
                                                         logged but do not cause an error to all open channels */
	uint64_t rx_dis_scram                 : 1;  /**< Disable lane scrambler (debug) */
	uint64_t stat_rdclr                   : 1;  /**< CSR read to ILK_RX_LNEx_STAT* clears the selected counter after
                                                         returning its current value. */
	uint64_t stat_ena                     : 1;  /**< Enable RX lane statistics counters */
#else
	uint64_t stat_ena                     : 1;
	uint64_t stat_rdclr                   : 1;
	uint64_t rx_dis_scram                 : 1;
	uint64_t rx_dis_ukwn                  : 1;
	uint64_t rx_bdry_sync                 : 1;
	uint64_t rx_scrm_sync                 : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t rx_dis_psh_skip              : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_cfg_s         cn68xx;
	struct cvmx_ilk_rx_lnex_cfg_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t rx_bdry_sync                 : 1;  /**< Rx word boundary sync status */
	uint64_t rx_dis_ukwn                  : 1;  /**< Disable normal response to unknown words.  They are still
                                                         logged but do not cause an error to all open channels */
	uint64_t rx_dis_scram                 : 1;  /**< Disable lane scrambler (debug) */
	uint64_t stat_rdclr                   : 1;  /**< CSR read to ILK_RX_LNEx_STAT* clears the selected counter after
                                                         returning its current value. */
	uint64_t stat_ena                     : 1;  /**< Enable RX lane statistics counters */
#else
	uint64_t stat_ena                     : 1;
	uint64_t stat_rdclr                   : 1;
	uint64_t rx_dis_scram                 : 1;
	uint64_t rx_dis_ukwn                  : 1;
	uint64_t rx_bdry_sync                 : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_cfg cvmx_ilk_rx_lnex_cfg_t;

/**
 * cvmx_ilk_rx_lne#_int
 */
union cvmx_ilk_rx_lnex_int {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t bad_64b67b                   : 1;  /**< Bad 64B/67B codeword encountered.  Once the bad word reaches
                                                         the burst control unit (as deonted by
                                                         ILK_RXx_INT[LANE_BAD_WORD]) it will be tossed and all open
                                                         packets will receive an error. */
	uint64_t stat_cnt_ovfl                : 1;  /**< Rx lane statistic counter overflow */
	uint64_t stat_msg                     : 1;  /**< Status bits for the link or a lane transitioned from a '1'
                                                         (healthy) to a '0' (problem) */
	uint64_t dskew_fifo_ovfl              : 1;  /**< Rx deskew fifo overflow occurred. */
	uint64_t scrm_sync_loss               : 1;  /**< 4 consecutive bad sync words or 3 consecutive scramble state
                                                         mismatches */
	uint64_t ukwn_cntl_word               : 1;  /**< Unknown framing control word. Block type does not match any of
                                                         (SYNC,SCRAM,SKIP,DIAG) */
	uint64_t crc32_err                    : 1;  /**< Diagnostic CRC32 errors */
	uint64_t bdry_sync_loss               : 1;  /**< Rx logic loses word boundary sync (16 tries).  Hardware will
                                                         automatically attempt to regain word boundary sync */
	uint64_t serdes_lock_loss             : 1;  /**< Rx SERDES loses lock */
#else
	uint64_t serdes_lock_loss             : 1;
	uint64_t bdry_sync_loss               : 1;
	uint64_t crc32_err                    : 1;
	uint64_t ukwn_cntl_word               : 1;
	uint64_t scrm_sync_loss               : 1;
	uint64_t dskew_fifo_ovfl              : 1;
	uint64_t stat_msg                     : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t bad_64b67b                   : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_int_s         cn68xx;
	struct cvmx_ilk_rx_lnex_int_s         cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_int cvmx_ilk_rx_lnex_int_t;

/**
 * cvmx_ilk_rx_lne#_int_en
 */
union cvmx_ilk_rx_lnex_int_en {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t bad_64b67b                   : 1;  /**< Bad 64B/67B codeword encountered.  Once the bad word reaches
                                                         the burst control unit (as deonted by
                                                         ILK_RXx_INT[LANE_BAD_WORD]) it will be tossed and all open
                                                         packets will receive an error. */
	uint64_t stat_cnt_ovfl                : 1;  /**< Rx lane statistic counter overflow */
	uint64_t stat_msg                     : 1;  /**< Status bits for the link or a lane transitioned from a '1'
                                                         (healthy) to a '0' (problem) */
	uint64_t dskew_fifo_ovfl              : 1;  /**< Rx deskew fifo overflow occurred. */
	uint64_t scrm_sync_loss               : 1;  /**< 4 consecutive bad sync words or 3 consecutive scramble state
                                                         mismatches */
	uint64_t ukwn_cntl_word               : 1;  /**< Unknown framing control word. Block type does not match any of
                                                         (SYNC,SCRAM,SKIP,DIAG) */
	uint64_t crc32_err                    : 1;  /**< Diagnostic CRC32 error */
	uint64_t bdry_sync_loss               : 1;  /**< Rx logic loses word boundary sync (16 tries).  Hardware will
                                                         automatically attempt to regain word boundary sync */
	uint64_t serdes_lock_loss             : 1;  /**< Rx SERDES loses lock */
#else
	uint64_t serdes_lock_loss             : 1;
	uint64_t bdry_sync_loss               : 1;
	uint64_t crc32_err                    : 1;
	uint64_t ukwn_cntl_word               : 1;
	uint64_t scrm_sync_loss               : 1;
	uint64_t dskew_fifo_ovfl              : 1;
	uint64_t stat_msg                     : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t bad_64b67b                   : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_int_en_s      cn68xx;
	struct cvmx_ilk_rx_lnex_int_en_s      cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_int_en cvmx_ilk_rx_lnex_int_en_t;

/**
 * cvmx_ilk_rx_lne#_stat0
 */
union cvmx_ilk_rx_lnex_stat0 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t ser_lock_loss_cnt            : 18; /**< Number of times the lane lost clock-data-recovery.
                                                         Saturates.  Interrupt on saturation if
                                                         ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t ser_lock_loss_cnt            : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat0_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat0_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat0 cvmx_ilk_rx_lnex_stat0_t;

/**
 * cvmx_ilk_rx_lne#_stat1
 */
union cvmx_ilk_rx_lnex_stat1 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bdry_sync_loss_cnt           : 18; /**< Number of times a lane lost word boundary synchronization.
                                                         Saturates.  Interrupt on saturation if
                                                         ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t bdry_sync_loss_cnt           : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat1_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat1_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat1 cvmx_ilk_rx_lnex_stat1_t;

/**
 * cvmx_ilk_rx_lne#_stat2
 */
union cvmx_ilk_rx_lnex_stat2 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_50_63               : 14;
	uint64_t syncw_good_cnt               : 18; /**< Number of good synchronization words.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
	uint64_t reserved_18_31               : 14;
	uint64_t syncw_bad_cnt                : 18; /**< Number of bad synchronization words.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t syncw_bad_cnt                : 18;
	uint64_t reserved_18_31               : 14;
	uint64_t syncw_good_cnt               : 18;
	uint64_t reserved_50_63               : 14;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat2_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat2_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat2 cvmx_ilk_rx_lnex_stat2_t;

/**
 * cvmx_ilk_rx_lne#_stat3
 */
union cvmx_ilk_rx_lnex_stat3 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bad_64b67b_cnt               : 18; /**< Number of bad 64B/67B words, meaning bit 65 or 64 has been
                                                         corrupted.  Saturates.  Interrupt on saturation if
                                                         ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t bad_64b67b_cnt               : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat3_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat3_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat3 cvmx_ilk_rx_lnex_stat3_t;

/**
 * cvmx_ilk_rx_lne#_stat4
 */
union cvmx_ilk_rx_lnex_stat4 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t cntl_word_cnt                : 27; /**< Number of control words received.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
	uint64_t reserved_27_31               : 5;
	uint64_t data_word_cnt                : 27; /**< Number of data words received.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t data_word_cnt                : 27;
	uint64_t reserved_27_31               : 5;
	uint64_t cntl_word_cnt                : 27;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat4_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat4_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat4 cvmx_ilk_rx_lnex_stat4_t;

/**
 * cvmx_ilk_rx_lne#_stat5
 */
union cvmx_ilk_rx_lnex_stat5 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t unkwn_word_cnt               : 18; /**< Number of unknown control words.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t unkwn_word_cnt               : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat5_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat5_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat5 cvmx_ilk_rx_lnex_stat5_t;

/**
 * cvmx_ilk_rx_lne#_stat6
 */
union cvmx_ilk_rx_lnex_stat6 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t scrm_sync_loss_cnt           : 18; /**< Number of times scrambler synchronization was lost (due to
                                                         either 4 consecutive bad sync words or 3 consecutive scrambler
                                                         state mismatches).  Saturates.  Interrupt on saturation if
                                                         ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t scrm_sync_loss_cnt           : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat6_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat6_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat6 cvmx_ilk_rx_lnex_stat6_t;

/**
 * cvmx_ilk_rx_lne#_stat7
 */
union cvmx_ilk_rx_lnex_stat7 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t scrm_match_cnt               : 18; /**< Number of scrambler state matches received.  Saturates.
                                                         Interrupt on saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t scrm_match_cnt               : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat7_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat7_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat7 cvmx_ilk_rx_lnex_stat7_t;

/**
 * cvmx_ilk_rx_lne#_stat8
 */
union cvmx_ilk_rx_lnex_stat8 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat8_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t skipw_good_cnt               : 18; /**< Number of good skip words.  Saturates.  Interrupt on saturation
                                                         if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t skipw_good_cnt               : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat8_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat8_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat8 cvmx_ilk_rx_lnex_stat8_t;

/**
 * cvmx_ilk_rx_lne#_stat9
 */
union cvmx_ilk_rx_lnex_stat9 {
	uint64_t u64;
	struct cvmx_ilk_rx_lnex_stat9_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_50_63               : 14;
	uint64_t crc32_err_cnt                : 18; /**< Number of errors in the lane CRC.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
	uint64_t reserved_27_31               : 5;
	uint64_t crc32_match_cnt              : 27; /**< Number of CRC32 matches received.  Saturates.  Interrupt on
                                                         saturation if ILK_RX_LNEX_INT_EN[STAT_CNT_OVFL]=1 */
#else
	uint64_t crc32_match_cnt              : 27;
	uint64_t reserved_27_31               : 5;
	uint64_t crc32_err_cnt                : 18;
	uint64_t reserved_50_63               : 14;
#endif
	} s;
	struct cvmx_ilk_rx_lnex_stat9_s       cn68xx;
	struct cvmx_ilk_rx_lnex_stat9_s       cn68xxp1;
};
typedef union cvmx_ilk_rx_lnex_stat9 cvmx_ilk_rx_lnex_stat9_t;

/**
 * cvmx_ilk_rxf_idx_pmap
 */
union cvmx_ilk_rxf_idx_pmap {
	uint64_t u64;
	struct cvmx_ilk_rxf_idx_pmap_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t inc                          : 9;  /**< Increment to add to current index for next index. */
	uint64_t reserved_9_15                : 7;
	uint64_t index                        : 9;  /**< Specify the link/channel accessed by the next CSR read/write to
                                                         port map memory.   IDX[8]=link, IDX[7:0]=channel */
#else
	uint64_t index                        : 9;
	uint64_t reserved_9_15                : 7;
	uint64_t inc                          : 9;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_ilk_rxf_idx_pmap_s        cn68xx;
	struct cvmx_ilk_rxf_idx_pmap_s        cn68xxp1;
};
typedef union cvmx_ilk_rxf_idx_pmap cvmx_ilk_rxf_idx_pmap_t;

/**
 * cvmx_ilk_rxf_mem_pmap
 */
union cvmx_ilk_rxf_mem_pmap {
	uint64_t u64;
	struct cvmx_ilk_rxf_mem_pmap_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t port_kind                    : 6;  /**< Specify the port-kind for the link/channel selected by
                                                         ILK_IDX_PMAP[IDX] */
#else
	uint64_t port_kind                    : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_ilk_rxf_mem_pmap_s        cn68xx;
	struct cvmx_ilk_rxf_mem_pmap_s        cn68xxp1;
};
typedef union cvmx_ilk_rxf_mem_pmap cvmx_ilk_rxf_mem_pmap_t;

/**
 * cvmx_ilk_ser_cfg
 */
union cvmx_ilk_ser_cfg {
	uint64_t u64;
	struct cvmx_ilk_ser_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_57_63               : 7;
	uint64_t ser_rxpol_auto               : 1;  /**< Serdes lane receive polarity auto detection mode */
	uint64_t reserved_48_55               : 8;
	uint64_t ser_rxpol                    : 8;  /**< Serdes lane receive polarity
                                                         - 0: rx without inversion
                                                         - 1: rx with inversion */
	uint64_t reserved_32_39               : 8;
	uint64_t ser_txpol                    : 8;  /**< Serdes lane transmit polarity
                                                         - 0: tx without inversion
                                                         - 1: tx with inversion */
	uint64_t reserved_16_23               : 8;
	uint64_t ser_reset_n                  : 8;  /**< Serdes lane reset */
	uint64_t reserved_6_7                 : 2;
	uint64_t ser_pwrup                    : 2;  /**< Serdes modules (QLM) power up. */
	uint64_t reserved_2_3                 : 2;
	uint64_t ser_haul                     : 2;  /**< Serdes module (QLM) haul mode */
#else
	uint64_t ser_haul                     : 2;
	uint64_t reserved_2_3                 : 2;
	uint64_t ser_pwrup                    : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t ser_reset_n                  : 8;
	uint64_t reserved_16_23               : 8;
	uint64_t ser_txpol                    : 8;
	uint64_t reserved_32_39               : 8;
	uint64_t ser_rxpol                    : 8;
	uint64_t reserved_48_55               : 8;
	uint64_t ser_rxpol_auto               : 1;
	uint64_t reserved_57_63               : 7;
#endif
	} s;
	struct cvmx_ilk_ser_cfg_s             cn68xx;
	struct cvmx_ilk_ser_cfg_s             cn68xxp1;
};
typedef union cvmx_ilk_ser_cfg cvmx_ilk_ser_cfg_t;

/**
 * cvmx_ilk_tx#_cfg0
 */
union cvmx_ilk_txx_cfg0 {
	uint64_t u64;
	struct cvmx_ilk_txx_cfg0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ext_lpbk_fc                  : 1;  /**< Enable Rx-Tx flowcontrol loopback (external) */
	uint64_t ext_lpbk                     : 1;  /**< Enable Rx-Tx data loopback (external). Note that with differing
                                                         transmit & receive clocks, skip word are  inserted/deleted */
	uint64_t int_lpbk                     : 1;  /**< Enable Tx-Rx loopback (internal) */
	uint64_t reserved_57_60               : 4;
	uint64_t ptrn_mode                    : 1;  /**< Enable programmable test pattern mode.  This mode allows
                                                         software to send a packet containing a programmable pattern.
                                                         While in this mode, the scramblers and disparity inversion will
                                                         be disabled.  In addition, no framing layer control words will
                                                         be transmitted (ie. no SYNC, scrambler state, skip, or
                                                         diagnostic words will be transmitted).

                                                         NOTE: Software must first write ILK_TXX_CFG0[LANE_ENA]=0 before
                                                         enabling/disabling this mode. */
	uint64_t reserved_55_55               : 1;
	uint64_t lnk_stats_ena                : 1;  /**< Enable link statistics counters */
	uint64_t mltuse_fc_ena                : 1;  /**< When set, the multi-use field of control words will contain
                                                         flow control status.  Otherwise, the multi-use field will
                                                         contain ILK_TXX_CFG1[TX_MLTUSE] */
	uint64_t cal_ena                      : 1;  /**< Enable Tx calendar, else default calendar used:
                                                              First control word:
                                                               Entry 0  = link
                                                               Entry 1  = backpressue id 0
                                                               Entry 2  = backpressue id 1
                                                               ...etc.
                                                            Second control word:
                                                               Entry 15 = link
                                                               Entry 16 = backpressue id 15
                                                               Entry 17 = backpressue id 16
                                                               ...etc.
                                                         This continues until the status for all 64 backpressue ids gets
                                                         transmitted (ie. 0-68 calendar table entries).  The remaining 3
                                                         calendar table entries (ie. 69-71) will always transmit XOFF.

                                                         To disable backpressure completely, enable the calendar table
                                                         and program each calendar table entry to transmit XON */
	uint64_t mfrm_len                     : 13; /**< The quantity of data sent on each lane including one sync word,
                                                         scrambler state, diag word, zero or more skip words, and the
                                                         data  payload.  Must be large than ILK_TXX_CFG1[SKIP_CNT]+9.
                                                         Supported range:ILK_TXX_CFG1[SKIP_CNT]+9 < MFRM_LEN <= 4096) */
	uint64_t brst_shrt                    : 7;  /**< Minimum interval between burst control words, as a multiple of
                                                         8 bytes.  Supported range from 8 bytes to 512 (ie. 0 <
                                                         BRST_SHRT <= 64) */
	uint64_t lane_rev                     : 1;  /**< Lane reversal.   When enabled, lane striping is performed from
                                                         most significant lane enabled to least significant lane
                                                         enabled.  LANE_ENA must be zero before changing LANE_REV. */
	uint64_t brst_max                     : 5;  /**< Maximum size of a data burst, as a multiple of 64 byte blocks.
                                                         Supported range is from 64 bytes to 1024 bytes. (ie. 0 <
                                                         BRST_MAX <= 16) */
	uint64_t reserved_25_25               : 1;
	uint64_t cal_depth                    : 9;  /**< Number of valid entries in the calendar.  CAL_DEPTH[2:0] must
                                                         be zero.  Supported range from 8 to 288.  If CAL_ENA is 0,
                                                         this field has no effect and the calendar depth is 72 entries. */
	uint64_t reserved_8_15                : 8;
	uint64_t lane_ena                     : 8;  /**< Lane enable mask.  Link is enabled if any lane is enabled.  The
                                                         same lane should not be enabled in multiple ILK_TXx_CFG0.  Each
                                                         bit of LANE_ENA maps to a TX lane (TLE) and a QLM lane.  NOTE:
                                                         LANE_REV has no effect on this mapping.

                                                               LANE_ENA[0] = TLE0 = QLM1 lane 0
                                                               LANE_ENA[1] = TLE1 = QLM1 lane 1
                                                               LANE_ENA[2] = TLE2 = QLM1 lane 2
                                                               LANE_ENA[3] = TLE3 = QLM1 lane 3
                                                               LANE_ENA[4] = TLE4 = QLM2 lane 0
                                                               LANE_ENA[5] = TLE5 = QLM2 lane 1
                                                               LANE_ENA[6] = TLE6 = QLM2 lane 2
                                                               LANE_ENA[7] = TLE7 = QLM2 lane 3 */
#else
	uint64_t lane_ena                     : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t cal_depth                    : 9;
	uint64_t reserved_25_25               : 1;
	uint64_t brst_max                     : 5;
	uint64_t lane_rev                     : 1;
	uint64_t brst_shrt                    : 7;
	uint64_t mfrm_len                     : 13;
	uint64_t cal_ena                      : 1;
	uint64_t mltuse_fc_ena                : 1;
	uint64_t lnk_stats_ena                : 1;
	uint64_t reserved_55_55               : 1;
	uint64_t ptrn_mode                    : 1;
	uint64_t reserved_57_60               : 4;
	uint64_t int_lpbk                     : 1;
	uint64_t ext_lpbk                     : 1;
	uint64_t ext_lpbk_fc                  : 1;
#endif
	} s;
	struct cvmx_ilk_txx_cfg0_s            cn68xx;
	struct cvmx_ilk_txx_cfg0_s            cn68xxp1;
};
typedef union cvmx_ilk_txx_cfg0 cvmx_ilk_txx_cfg0_t;

/**
 * cvmx_ilk_tx#_cfg1
 */
union cvmx_ilk_txx_cfg1 {
	uint64_t u64;
	struct cvmx_ilk_txx_cfg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t pkt_busy                     : 1;  /**< Tx-Link is transmitting data. */
	uint64_t pipe_crd_dis                 : 1;  /**< Disable pipe credits.   Should be set when PKO is configure to
                                                         ignore pipe credits. */
	uint64_t ptp_delay                    : 5;  /**< Timestamp commit delay.  Must not be zero. */
	uint64_t skip_cnt                     : 4;  /**< Number of skip words to insert after the scrambler state */
	uint64_t pkt_flush                    : 1;  /**< Packet transmit flush.  While PKT_FLUSH=1, the TxFifo will
                                                         continuously drain; all data will be dropped.  Software should
                                                         first write PKT_ENA=0 and wait packet transmission to stop. */
	uint64_t pkt_ena                      : 1;  /**< Packet transmit enable.  When PKT_ENA=0, the Tx-Link will stop
                                                         transmitting packets, as per RX_LINK_FC_PKT */
	uint64_t la_mode                      : 1;  /**< 0 = Interlaken
                                                         1 = Interlaken Look-Aside */
	uint64_t tx_link_fc                   : 1;  /**< Link flow control status transmitted by the Tx-Link
                                                         XON when RX_FIFO_CNT <= RX_FIFO_HWM and lane alignment is done */
	uint64_t rx_link_fc                   : 1;  /**< Link flow control status received in burst/idle control words.
                                                         When RX_LINK_FC_IGN=0, XOFF will cause Tx-Link to stop
                                                         transmitting on all channels. */
	uint64_t reserved_12_16               : 5;
	uint64_t tx_link_fc_jam               : 1;  /**< All flow control transmitted in burst/idle control words will
                                                         be XOFF whenever TX_LINK_FC is XOFF.   Enable this to allow
                                                         link XOFF to automatically XOFF all channels. */
	uint64_t rx_link_fc_pkt               : 1;  /**< Link flow control received in burst/idle control words causes
                                                         Tx-Link to stop transmitting at the end of a packet instead of
                                                         the end of a burst */
	uint64_t rx_link_fc_ign               : 1;  /**< Ignore the link flow control status received in burst/idle
                                                         control words */
	uint64_t rmatch                       : 1;  /**< Enable rate matching circuitry */
	uint64_t tx_mltuse                    : 8;  /**< Multiple Use bits used when ILKx_TX_CFG[LA_MODE=0] and
                                                         ILKx_TX_CFG[MLTUSE_FC_ENA] is zero */
#else
	uint64_t tx_mltuse                    : 8;
	uint64_t rmatch                       : 1;
	uint64_t rx_link_fc_ign               : 1;
	uint64_t rx_link_fc_pkt               : 1;
	uint64_t tx_link_fc_jam               : 1;
	uint64_t reserved_12_16               : 5;
	uint64_t rx_link_fc                   : 1;
	uint64_t tx_link_fc                   : 1;
	uint64_t la_mode                      : 1;
	uint64_t pkt_ena                      : 1;
	uint64_t pkt_flush                    : 1;
	uint64_t skip_cnt                     : 4;
	uint64_t ptp_delay                    : 5;
	uint64_t pipe_crd_dis                 : 1;
	uint64_t pkt_busy                     : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_ilk_txx_cfg1_s            cn68xx;
	struct cvmx_ilk_txx_cfg1_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pipe_crd_dis                 : 1;  /**< Disable pipe credits.   Should be set when PKO is configure to
                                                         ignore pipe credits. */
	uint64_t ptp_delay                    : 5;  /**< Timestamp commit delay.  Must not be zero. */
	uint64_t skip_cnt                     : 4;  /**< Number of skip words to insert after the scrambler state */
	uint64_t pkt_flush                    : 1;  /**< Packet transmit flush.  While PKT_FLUSH=1, the TxFifo will
                                                         continuously drain; all data will be dropped.  Software should
                                                         first write PKT_ENA=0 and wait packet transmission to stop. */
	uint64_t pkt_ena                      : 1;  /**< Packet transmit enable.  When PKT_ENA=0, the Tx-Link will stop
                                                         transmitting packets, as per RX_LINK_FC_PKT */
	uint64_t la_mode                      : 1;  /**< 0 = Interlaken
                                                         1 = Interlaken Look-Aside */
	uint64_t tx_link_fc                   : 1;  /**< Link flow control status transmitted by the Tx-Link
                                                         XON when RX_FIFO_CNT <= RX_FIFO_HWM and lane alignment is done */
	uint64_t rx_link_fc                   : 1;  /**< Link flow control status received in burst/idle control words.
                                                         When RX_LINK_FC_IGN=0, XOFF will cause Tx-Link to stop
                                                         transmitting on all channels. */
	uint64_t reserved_12_16               : 5;
	uint64_t tx_link_fc_jam               : 1;  /**< All flow control transmitted in burst/idle control words will
                                                         be XOFF whenever TX_LINK_FC is XOFF.   Enable this to allow
                                                         link XOFF to automatically XOFF all channels. */
	uint64_t rx_link_fc_pkt               : 1;  /**< Link flow control received in burst/idle control words causes
                                                         Tx-Link to stop transmitting at the end of a packet instead of
                                                         the end of a burst */
	uint64_t rx_link_fc_ign               : 1;  /**< Ignore the link flow control status received in burst/idle
                                                         control words */
	uint64_t rmatch                       : 1;  /**< Enable rate matching circuitry */
	uint64_t tx_mltuse                    : 8;  /**< Multiple Use bits used when ILKx_TX_CFG[LA_MODE=0] and
                                                         ILKx_TX_CFG[MLTUSE_FC_ENA] is zero */
#else
	uint64_t tx_mltuse                    : 8;
	uint64_t rmatch                       : 1;
	uint64_t rx_link_fc_ign               : 1;
	uint64_t rx_link_fc_pkt               : 1;
	uint64_t tx_link_fc_jam               : 1;
	uint64_t reserved_12_16               : 5;
	uint64_t rx_link_fc                   : 1;
	uint64_t tx_link_fc                   : 1;
	uint64_t la_mode                      : 1;
	uint64_t pkt_ena                      : 1;
	uint64_t pkt_flush                    : 1;
	uint64_t skip_cnt                     : 4;
	uint64_t ptp_delay                    : 5;
	uint64_t pipe_crd_dis                 : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_txx_cfg1 cvmx_ilk_txx_cfg1_t;

/**
 * cvmx_ilk_tx#_dbg
 */
union cvmx_ilk_txx_dbg {
	uint64_t u64;
	struct cvmx_ilk_txx_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t tx_bad_crc24                 : 1;  /**< Send a control word with bad CRC24.  Hardware will clear this
                                                         field once the injection is performed. */
	uint64_t tx_bad_ctlw2                 : 1;  /**< Send a control word without the control bit set */
	uint64_t tx_bad_ctlw1                 : 1;  /**< Send a data word with the control bit set */
#else
	uint64_t tx_bad_ctlw1                 : 1;
	uint64_t tx_bad_ctlw2                 : 1;
	uint64_t tx_bad_crc24                 : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_ilk_txx_dbg_s             cn68xx;
	struct cvmx_ilk_txx_dbg_s             cn68xxp1;
};
typedef union cvmx_ilk_txx_dbg cvmx_ilk_txx_dbg_t;

/**
 * cvmx_ilk_tx#_flow_ctl0
 */
union cvmx_ilk_txx_flow_ctl0 {
	uint64_t u64;
	struct cvmx_ilk_txx_flow_ctl0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t status                       : 64; /**< IPD flow control status for backpressue id 63-0, where a 0
                                                         indicates the presence of backpressure (ie. XOFF) and 1
                                                         indicates the absence of backpressure (ie. XON) */
#else
	uint64_t status                       : 64;
#endif
	} s;
	struct cvmx_ilk_txx_flow_ctl0_s       cn68xx;
	struct cvmx_ilk_txx_flow_ctl0_s       cn68xxp1;
};
typedef union cvmx_ilk_txx_flow_ctl0 cvmx_ilk_txx_flow_ctl0_t;

/**
 * cvmx_ilk_tx#_flow_ctl1
 *
 * Notes:
 * Do not publish.
 *
 */
union cvmx_ilk_txx_flow_ctl1 {
	uint64_t u64;
	struct cvmx_ilk_txx_flow_ctl1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_ilk_txx_flow_ctl1_s       cn68xx;
	struct cvmx_ilk_txx_flow_ctl1_s       cn68xxp1;
};
typedef union cvmx_ilk_txx_flow_ctl1 cvmx_ilk_txx_flow_ctl1_t;

/**
 * cvmx_ilk_tx#_idx_cal
 */
union cvmx_ilk_txx_idx_cal {
	uint64_t u64;
	struct cvmx_ilk_txx_idx_cal_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t inc                          : 6;  /**< Increment to add to current index for next index. NOTE:
                                                         Increment only performed after *MEM_CAL1 access (ie. not
                                                         *MEM_CAL0) */
	uint64_t reserved_6_7                 : 2;
	uint64_t index                        : 6;  /**< Specify the group of 8 entries accessed by the next CSR
                                                         read/write to calendar table memory.  Software must ensure IDX
                                                         is <36 whenever writing to *MEM_CAL1 */
#else
	uint64_t index                        : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t inc                          : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_ilk_txx_idx_cal_s         cn68xx;
	struct cvmx_ilk_txx_idx_cal_s         cn68xxp1;
};
typedef union cvmx_ilk_txx_idx_cal cvmx_ilk_txx_idx_cal_t;

/**
 * cvmx_ilk_tx#_idx_pmap
 */
union cvmx_ilk_txx_idx_pmap {
	uint64_t u64;
	struct cvmx_ilk_txx_idx_pmap_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t inc                          : 7;  /**< Increment to add to current index for next index. */
	uint64_t reserved_7_15                : 9;
	uint64_t index                        : 7;  /**< Specify the port-pipe accessed by the next CSR read/write to
                                                         ILK_TXx_MEM_PMAP.   Note that IDX=n is always port-pipe n,
                                                         regardless of ILK_TXx_PIPE[BASE] */
#else
	uint64_t index                        : 7;
	uint64_t reserved_7_15                : 9;
	uint64_t inc                          : 7;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_ilk_txx_idx_pmap_s        cn68xx;
	struct cvmx_ilk_txx_idx_pmap_s        cn68xxp1;
};
typedef union cvmx_ilk_txx_idx_pmap cvmx_ilk_txx_idx_pmap_t;

/**
 * cvmx_ilk_tx#_idx_stat0
 */
union cvmx_ilk_txx_idx_stat0 {
	uint64_t u64;
	struct cvmx_ilk_txx_idx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t clr                          : 1;  /**< CSR read to ILK_TXx_MEM_STAT0 clears the selected counter after
                                                         returning its current value. */
	uint64_t reserved_24_30               : 7;
	uint64_t inc                          : 8;  /**< Increment to add to current index for next index */
	uint64_t reserved_8_15                : 8;
	uint64_t index                        : 8;  /**< Specify the channel accessed during the next CSR read to the
                                                         ILK_TXx_MEM_STAT0 */
#else
	uint64_t index                        : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t inc                          : 8;
	uint64_t reserved_24_30               : 7;
	uint64_t clr                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ilk_txx_idx_stat0_s       cn68xx;
	struct cvmx_ilk_txx_idx_stat0_s       cn68xxp1;
};
typedef union cvmx_ilk_txx_idx_stat0 cvmx_ilk_txx_idx_stat0_t;

/**
 * cvmx_ilk_tx#_idx_stat1
 */
union cvmx_ilk_txx_idx_stat1 {
	uint64_t u64;
	struct cvmx_ilk_txx_idx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t clr                          : 1;  /**< CSR read to ILK_TXx_MEM_STAT1 clears the selected counter after
                                                         returning its current value. */
	uint64_t reserved_24_30               : 7;
	uint64_t inc                          : 8;  /**< Increment to add to current index for next index */
	uint64_t reserved_8_15                : 8;
	uint64_t index                        : 8;  /**< Specify the channel accessed during the next CSR read to the
                                                         ILK_TXx_MEM_STAT1 */
#else
	uint64_t index                        : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t inc                          : 8;
	uint64_t reserved_24_30               : 7;
	uint64_t clr                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ilk_txx_idx_stat1_s       cn68xx;
	struct cvmx_ilk_txx_idx_stat1_s       cn68xxp1;
};
typedef union cvmx_ilk_txx_idx_stat1 cvmx_ilk_txx_idx_stat1_t;

/**
 * cvmx_ilk_tx#_int
 */
union cvmx_ilk_txx_int {
	uint64_t u64;
	struct cvmx_ilk_txx_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t stat_cnt_ovfl                : 1;  /**< Statistics counter overflow */
	uint64_t bad_pipe                     : 1;  /**< Received a PKO port-pipe out of the range specified by
                                                         ILK_TXX_PIPE */
	uint64_t bad_seq                      : 1;  /**< Received sequence is not SOP followed by 0 or more data cycles
                                                         followed by EOP.  PKO config assigned multiple engines to the
                                                         same ILK Tx Link. */
	uint64_t txf_err                      : 1;  /**< TX fifo parity error occurred.  At EOP time, EOP_Format will
                                                         reflect the error. */
#else
	uint64_t txf_err                      : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_pipe                     : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ilk_txx_int_s             cn68xx;
	struct cvmx_ilk_txx_int_s             cn68xxp1;
};
typedef union cvmx_ilk_txx_int cvmx_ilk_txx_int_t;

/**
 * cvmx_ilk_tx#_int_en
 */
union cvmx_ilk_txx_int_en {
	uint64_t u64;
	struct cvmx_ilk_txx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t stat_cnt_ovfl                : 1;  /**< Statistics counter overflow */
	uint64_t bad_pipe                     : 1;  /**< Received a PKO port-pipe out of the range specified by
                                                         ILK_TXX_PIPE. */
	uint64_t bad_seq                      : 1;  /**< Received sequence is not SOP followed by 0 or more data cycles
                                                         followed by EOP.  PKO config assigned multiple engines to the
                                                         same ILK Tx Link. */
	uint64_t txf_err                      : 1;  /**< TX fifo parity error occurred.  At EOP time, EOP_Format will
                                                         reflect the error. */
#else
	uint64_t txf_err                      : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_pipe                     : 1;
	uint64_t stat_cnt_ovfl                : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ilk_txx_int_en_s          cn68xx;
	struct cvmx_ilk_txx_int_en_s          cn68xxp1;
};
typedef union cvmx_ilk_txx_int_en cvmx_ilk_txx_int_en_t;

/**
 * cvmx_ilk_tx#_mem_cal0
 *
 * Notes:
 * Software must always read ILK_TXx_MEM_CAL0 then ILK_TXx_MEM_CAL1.  Software
 * must never read them in reverse order or read one without reading the
 * other.
 *
 * Software must always write ILK_TXx_MEM_CAL0 then ILK_TXx_MEM_CAL1.
 * Software must never write them in reverse order or write one without
 * writing the other.
 */
union cvmx_ilk_txx_mem_cal0 {
	uint64_t u64;
	struct cvmx_ilk_txx_mem_cal0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t entry_ctl3                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+3
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_33_33               : 1;
	uint64_t bpid3                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+3
                                                         (unused if ENTRY_CTL3 != 0) */
	uint64_t entry_ctl2                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+2
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_24_24               : 1;
	uint64_t bpid2                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+2
                                                         (unused if ENTRY_CTL2 != 0) */
	uint64_t entry_ctl1                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+1
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_15_15               : 1;
	uint64_t bpid1                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+1
                                                         (unused if ENTRY_CTL1 != 0) */
	uint64_t entry_ctl0                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+0
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_6_6                 : 1;
	uint64_t bpid0                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+0
                                                         (unused if ENTRY_CTL0 != 0) */
#else
	uint64_t bpid0                        : 6;
	uint64_t reserved_6_6                 : 1;
	uint64_t entry_ctl0                   : 2;
	uint64_t bpid1                        : 6;
	uint64_t reserved_15_15               : 1;
	uint64_t entry_ctl1                   : 2;
	uint64_t bpid2                        : 6;
	uint64_t reserved_24_24               : 1;
	uint64_t entry_ctl2                   : 2;
	uint64_t bpid3                        : 6;
	uint64_t reserved_33_33               : 1;
	uint64_t entry_ctl3                   : 2;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_ilk_txx_mem_cal0_s        cn68xx;
	struct cvmx_ilk_txx_mem_cal0_s        cn68xxp1;
};
typedef union cvmx_ilk_txx_mem_cal0 cvmx_ilk_txx_mem_cal0_t;

/**
 * cvmx_ilk_tx#_mem_cal1
 *
 * Notes:
 * Software must always read ILK_TXx_MEM_CAL0 then ILK_TXx_MEM_CAL1.  Software
 * must never read them in reverse order or read one without reading the
 * other.
 *
 * Software must always write ILK_TXx_MEM_CAL0 then ILK_TXx_MEM_CAL1.
 * Software must never write them in reverse order or write one without
 * writing the other.
 */
union cvmx_ilk_txx_mem_cal1 {
	uint64_t u64;
	struct cvmx_ilk_txx_mem_cal1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t entry_ctl7                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+7
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_33_33               : 1;
	uint64_t bpid7                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+7
                                                         (unused if ENTRY_CTL7 != 0) */
	uint64_t entry_ctl6                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+6
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_24_24               : 1;
	uint64_t bpid6                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+6
                                                         (unused if ENTRY_CTL6 != 0) */
	uint64_t entry_ctl5                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+5
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_15_15               : 1;
	uint64_t bpid5                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+5
                                                         (unused if ENTRY_CTL5 != 0) */
	uint64_t entry_ctl4                   : 2;  /**< Select source of XON/XOFF for entry (IDX*8)+4
                                                         - 0: IPD backpressue id
                                                         - 1: Link
                                                         - 2: XOFF
                                                         - 3: XON */
	uint64_t reserved_6_6                 : 1;
	uint64_t bpid4                        : 6;  /**< Select IPD backpressue id for calendar table entry (IDX*8)+4
                                                         (unused if ENTRY_CTL4 != 0) */
#else
	uint64_t bpid4                        : 6;
	uint64_t reserved_6_6                 : 1;
	uint64_t entry_ctl4                   : 2;
	uint64_t bpid5                        : 6;
	uint64_t reserved_15_15               : 1;
	uint64_t entry_ctl5                   : 2;
	uint64_t bpid6                        : 6;
	uint64_t reserved_24_24               : 1;
	uint64_t entry_ctl6                   : 2;
	uint64_t bpid7                        : 6;
	uint64_t reserved_33_33               : 1;
	uint64_t entry_ctl7                   : 2;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_ilk_txx_mem_cal1_s        cn68xx;
	struct cvmx_ilk_txx_mem_cal1_s        cn68xxp1;
};
typedef union cvmx_ilk_txx_mem_cal1 cvmx_ilk_txx_mem_cal1_t;

/**
 * cvmx_ilk_tx#_mem_pmap
 */
union cvmx_ilk_txx_mem_pmap {
	uint64_t u64;
	struct cvmx_ilk_txx_mem_pmap_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t remap                        : 1;  /**< Dynamically select channel using bits[39:32] of an 8-byte
                                                         header prepended to any packet transmitted on the port-pipe
                                                         selected by ILK_TXx_IDX_PMAP[IDX].

                                                         ***NOTE: Added in pass 2.0 */
	uint64_t reserved_8_15                : 8;
	uint64_t channel                      : 8;  /**< Specify the channel for the port-pipe selected by
                                                         ILK_TXx_IDX_PMAP[IDX] */
#else
	uint64_t channel                      : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t remap                        : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_ilk_txx_mem_pmap_s        cn68xx;
	struct cvmx_ilk_txx_mem_pmap_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t channel                      : 8;  /**< Specify the channel for the port-pipe selected by
                                                         ILK_TXx_IDX_PMAP[IDX] */
#else
	uint64_t channel                      : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} cn68xxp1;
};
typedef union cvmx_ilk_txx_mem_pmap cvmx_ilk_txx_mem_pmap_t;

/**
 * cvmx_ilk_tx#_mem_stat0
 */
union cvmx_ilk_txx_mem_stat0 {
	uint64_t u64;
	struct cvmx_ilk_txx_mem_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t tx_pkt                       : 28; /**< Number of packets transmitted per channel (256M)
                                                         Channel selected by ILK_TXx_IDX_STAT0[IDX].  Interrupt on
                                                         saturation if ILK_TXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t tx_pkt                       : 28;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_ilk_txx_mem_stat0_s       cn68xx;
	struct cvmx_ilk_txx_mem_stat0_s       cn68xxp1;
};
typedef union cvmx_ilk_txx_mem_stat0 cvmx_ilk_txx_mem_stat0_t;

/**
 * cvmx_ilk_tx#_mem_stat1
 */
union cvmx_ilk_txx_mem_stat1 {
	uint64_t u64;
	struct cvmx_ilk_txx_mem_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t tx_bytes                     : 36; /**< Number of bytes transmitted per channel (64GB) Channel selected
                                                         by ILK_TXx_IDX_STAT1[IDX].    Saturates.  Interrupt on
                                                         saturation if ILK_TXX_INT_EN[STAT_CNT_OVFL]=1. */
#else
	uint64_t tx_bytes                     : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_ilk_txx_mem_stat1_s       cn68xx;
	struct cvmx_ilk_txx_mem_stat1_s       cn68xxp1;
};
typedef union cvmx_ilk_txx_mem_stat1 cvmx_ilk_txx_mem_stat1_t;

/**
 * cvmx_ilk_tx#_pipe
 */
union cvmx_ilk_txx_pipe {
	uint64_t u64;
	struct cvmx_ilk_txx_pipe_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t nump                         : 8;  /**< Number of pipes assigned to this Tx Link */
	uint64_t reserved_7_15                : 9;
	uint64_t base                         : 7;  /**< When NUMP is non-zero, indicates the base pipe number this
                                                         Tx link will accept.  This Tx will accept PKO packets from
                                                         pipes in the range of:  BASE .. (BASE+(NUMP-1))

                                                           BASE and NUMP must be constrained such that
                                                           1) BASE+(NUMP-1) < 127
                                                           2) Each used PKO pipe must map to exactly
                                                              one port|channel
                                                           3) The pipe ranges must be consistent with
                                                              the PKO configuration. */
#else
	uint64_t base                         : 7;
	uint64_t reserved_7_15                : 9;
	uint64_t nump                         : 8;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_ilk_txx_pipe_s            cn68xx;
	struct cvmx_ilk_txx_pipe_s            cn68xxp1;
};
typedef union cvmx_ilk_txx_pipe cvmx_ilk_txx_pipe_t;

/**
 * cvmx_ilk_tx#_rmatch
 */
union cvmx_ilk_txx_rmatch {
	uint64_t u64;
	struct cvmx_ilk_txx_rmatch_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_50_63               : 14;
	uint64_t grnlrty                      : 2;  /**< Granularity of a token, where 1 token equal (1<<GRNLRTY) bytes. */
	uint64_t brst_limit                   : 16; /**< Size of token bucket, also the maximum quantity of data that
                                                         may be burst across the interface before invoking rate limiting
                                                         logic. */
	uint64_t time_limit                   : 16; /**< Number of cycles per time interval. (Must be >= 4) */
	uint64_t rate_limit                   : 16; /**< Number of tokens added to the bucket when the interval timer
                                                         expires. */
#else
	uint64_t rate_limit                   : 16;
	uint64_t time_limit                   : 16;
	uint64_t brst_limit                   : 16;
	uint64_t grnlrty                      : 2;
	uint64_t reserved_50_63               : 14;
#endif
	} s;
	struct cvmx_ilk_txx_rmatch_s          cn68xx;
	struct cvmx_ilk_txx_rmatch_s          cn68xxp1;
};
typedef union cvmx_ilk_txx_rmatch cvmx_ilk_txx_rmatch_t;

#endif
