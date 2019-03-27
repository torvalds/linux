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
 * cvmx-pow-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pow.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_POW_DEFS_H__
#define __CVMX_POW_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_BIST_STAT CVMX_POW_BIST_STAT_FUNC()
static inline uint64_t CVMX_POW_BIST_STAT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_BIST_STAT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00016700000003F8ull);
}
#else
#define CVMX_POW_BIST_STAT (CVMX_ADD_IO_SEG(0x00016700000003F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_DS_PC CVMX_POW_DS_PC_FUNC()
static inline uint64_t CVMX_POW_DS_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_DS_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000398ull);
}
#else
#define CVMX_POW_DS_PC (CVMX_ADD_IO_SEG(0x0001670000000398ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_ECC_ERR CVMX_POW_ECC_ERR_FUNC()
static inline uint64_t CVMX_POW_ECC_ERR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_ECC_ERR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000218ull);
}
#else
#define CVMX_POW_ECC_ERR (CVMX_ADD_IO_SEG(0x0001670000000218ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_INT_CTL CVMX_POW_INT_CTL_FUNC()
static inline uint64_t CVMX_POW_INT_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_INT_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000220ull);
}
#else
#define CVMX_POW_INT_CTL (CVMX_ADD_IO_SEG(0x0001670000000220ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_IQ_CNTX(unsigned long offset)
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
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_POW_IQ_CNTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000340ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_POW_IQ_CNTX(offset) (CVMX_ADD_IO_SEG(0x0001670000000340ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_IQ_COM_CNT CVMX_POW_IQ_COM_CNT_FUNC()
static inline uint64_t CVMX_POW_IQ_COM_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_IQ_COM_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000388ull);
}
#else
#define CVMX_POW_IQ_COM_CNT (CVMX_ADD_IO_SEG(0x0001670000000388ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_IQ_INT CVMX_POW_IQ_INT_FUNC()
static inline uint64_t CVMX_POW_IQ_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_IQ_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000238ull);
}
#else
#define CVMX_POW_IQ_INT (CVMX_ADD_IO_SEG(0x0001670000000238ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_IQ_INT_EN CVMX_POW_IQ_INT_EN_FUNC()
static inline uint64_t CVMX_POW_IQ_INT_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_IQ_INT_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000240ull);
}
#else
#define CVMX_POW_IQ_INT_EN (CVMX_ADD_IO_SEG(0x0001670000000240ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_IQ_THRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_POW_IQ_THRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00016700000003A0ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_POW_IQ_THRX(offset) (CVMX_ADD_IO_SEG(0x00016700000003A0ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_NOS_CNT CVMX_POW_NOS_CNT_FUNC()
static inline uint64_t CVMX_POW_NOS_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_NOS_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000228ull);
}
#else
#define CVMX_POW_NOS_CNT (CVMX_ADD_IO_SEG(0x0001670000000228ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_NW_TIM CVMX_POW_NW_TIM_FUNC()
static inline uint64_t CVMX_POW_NW_TIM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_NW_TIM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000210ull);
}
#else
#define CVMX_POW_NW_TIM (CVMX_ADD_IO_SEG(0x0001670000000210ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_PF_RST_MSK CVMX_POW_PF_RST_MSK_FUNC()
static inline uint64_t CVMX_POW_PF_RST_MSK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_PF_RST_MSK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000230ull);
}
#else
#define CVMX_POW_PF_RST_MSK (CVMX_ADD_IO_SEG(0x0001670000000230ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_PP_GRP_MSKX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 11))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 5))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 9))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_POW_PP_GRP_MSKX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000000ull) + ((offset) & 15) * 8;
}
#else
#define CVMX_POW_PP_GRP_MSKX(offset) (CVMX_ADD_IO_SEG(0x0001670000000000ull) + ((offset) & 15) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_QOS_RNDX(unsigned long offset)
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
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_POW_QOS_RNDX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00016700000001C0ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_POW_QOS_RNDX(offset) (CVMX_ADD_IO_SEG(0x00016700000001C0ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_QOS_THRX(unsigned long offset)
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
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_POW_QOS_THRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000180ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_POW_QOS_THRX(offset) (CVMX_ADD_IO_SEG(0x0001670000000180ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_TS_PC CVMX_POW_TS_PC_FUNC()
static inline uint64_t CVMX_POW_TS_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_TS_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000390ull);
}
#else
#define CVMX_POW_TS_PC (CVMX_ADD_IO_SEG(0x0001670000000390ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_WA_COM_PC CVMX_POW_WA_COM_PC_FUNC()
static inline uint64_t CVMX_POW_WA_COM_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_WA_COM_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000380ull);
}
#else
#define CVMX_POW_WA_COM_PC (CVMX_ADD_IO_SEG(0x0001670000000380ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_WA_PCX(unsigned long offset)
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
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_POW_WA_PCX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000300ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_POW_WA_PCX(offset) (CVMX_ADD_IO_SEG(0x0001670000000300ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_WQ_INT CVMX_POW_WQ_INT_FUNC()
static inline uint64_t CVMX_POW_WQ_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_WQ_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000200ull);
}
#else
#define CVMX_POW_WQ_INT (CVMX_ADD_IO_SEG(0x0001670000000200ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_WQ_INT_CNTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 15)))))
		cvmx_warn("CVMX_POW_WQ_INT_CNTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000100ull) + ((offset) & 15) * 8;
}
#else
#define CVMX_POW_WQ_INT_CNTX(offset) (CVMX_ADD_IO_SEG(0x0001670000000100ull) + ((offset) & 15) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_POW_WQ_INT_PC CVMX_POW_WQ_INT_PC_FUNC()
static inline uint64_t CVMX_POW_WQ_INT_PC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_POW_WQ_INT_PC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001670000000208ull);
}
#else
#define CVMX_POW_WQ_INT_PC (CVMX_ADD_IO_SEG(0x0001670000000208ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_WQ_INT_THRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 15)))))
		cvmx_warn("CVMX_POW_WQ_INT_THRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000080ull) + ((offset) & 15) * 8;
}
#else
#define CVMX_POW_WQ_INT_THRX(offset) (CVMX_ADD_IO_SEG(0x0001670000000080ull) + ((offset) & 15) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_POW_WS_PCX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 15))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 15)))))
		cvmx_warn("CVMX_POW_WS_PCX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001670000000280ull) + ((offset) & 15) * 8;
}
#else
#define CVMX_POW_WS_PCX(offset) (CVMX_ADD_IO_SEG(0x0001670000000280ull) + ((offset) & 15) * 8)
#endif

/**
 * cvmx_pow_bist_stat
 *
 * POW_BIST_STAT = POW BIST Status Register
 *
 * Contains the BIST status for the POW memories ('0' = pass, '1' = fail).
 *
 * Also contains the BIST status for the PP's.  Each bit in the PP field is the OR of all BIST
 * results for the corresponding physical PP ('0' = pass, '1' = fail).
 */
union cvmx_pow_bist_stat {
	uint64_t u64;
	struct cvmx_pow_bist_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pp                           : 16; /**< Physical PP BIST status */
	uint64_t reserved_0_15                : 16;
#else
	uint64_t reserved_0_15                : 16;
	uint64_t pp                           : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_bist_stat_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t pp                           : 1;  /**< Physical PP BIST status */
	uint64_t reserved_9_15                : 7;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbt1                         : 1;  /**< NCB transmitter memory 1 BIST status */
	uint64_t nbt0                         : 1;  /**< NCB transmitter memory 0 BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t nbr1                         : 1;  /**< NCB receiver memory 1 BIST status */
	uint64_t nbr0                         : 1;  /**< NCB receiver memory 0 BIST status */
	uint64_t pend                         : 1;  /**< Pending switch memory BIST status */
	uint64_t adr                          : 1;  /**< Address memory BIST status */
#else
	uint64_t adr                          : 1;
	uint64_t pend                         : 1;
	uint64_t nbr0                         : 1;
	uint64_t nbr1                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt0                         : 1;
	uint64_t nbt1                         : 1;
	uint64_t cam                          : 1;
	uint64_t reserved_9_15                : 7;
	uint64_t pp                           : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn30xx;
	struct cvmx_pow_bist_stat_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t pp                           : 2;  /**< Physical PP BIST status */
	uint64_t reserved_9_15                : 7;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbt1                         : 1;  /**< NCB transmitter memory 1 BIST status */
	uint64_t nbt0                         : 1;  /**< NCB transmitter memory 0 BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t nbr1                         : 1;  /**< NCB receiver memory 1 BIST status */
	uint64_t nbr0                         : 1;  /**< NCB receiver memory 0 BIST status */
	uint64_t pend                         : 1;  /**< Pending switch memory BIST status */
	uint64_t adr                          : 1;  /**< Address memory BIST status */
#else
	uint64_t adr                          : 1;
	uint64_t pend                         : 1;
	uint64_t nbr0                         : 1;
	uint64_t nbr1                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt0                         : 1;
	uint64_t nbt1                         : 1;
	uint64_t cam                          : 1;
	uint64_t reserved_9_15                : 7;
	uint64_t pp                           : 2;
	uint64_t reserved_18_63               : 46;
#endif
	} cn31xx;
	struct cvmx_pow_bist_stat_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pp                           : 16; /**< Physical PP BIST status */
	uint64_t reserved_10_15               : 6;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbt                          : 1;  /**< NCB transmitter memory BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t nbr1                         : 1;  /**< NCB receiver memory 1 BIST status */
	uint64_t nbr0                         : 1;  /**< NCB receiver memory 0 BIST status */
	uint64_t pend1                        : 1;  /**< Pending switch memory 1 BIST status */
	uint64_t pend0                        : 1;  /**< Pending switch memory 0 BIST status */
	uint64_t adr1                         : 1;  /**< Address memory 1 BIST status */
	uint64_t adr0                         : 1;  /**< Address memory 0 BIST status */
#else
	uint64_t adr0                         : 1;
	uint64_t adr1                         : 1;
	uint64_t pend0                        : 1;
	uint64_t pend1                        : 1;
	uint64_t nbr0                         : 1;
	uint64_t nbr1                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt                          : 1;
	uint64_t cam                          : 1;
	uint64_t reserved_10_15               : 6;
	uint64_t pp                           : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} cn38xx;
	struct cvmx_pow_bist_stat_cn38xx      cn38xxp2;
	struct cvmx_pow_bist_stat_cn31xx      cn50xx;
	struct cvmx_pow_bist_stat_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pp                           : 4;  /**< Physical PP BIST status */
	uint64_t reserved_9_15                : 7;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbt1                         : 1;  /**< NCB transmitter memory 1 BIST status */
	uint64_t nbt0                         : 1;  /**< NCB transmitter memory 0 BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t nbr1                         : 1;  /**< NCB receiver memory 1 BIST status */
	uint64_t nbr0                         : 1;  /**< NCB receiver memory 0 BIST status */
	uint64_t pend                         : 1;  /**< Pending switch memory BIST status */
	uint64_t adr                          : 1;  /**< Address memory BIST status */
#else
	uint64_t adr                          : 1;
	uint64_t pend                         : 1;
	uint64_t nbr0                         : 1;
	uint64_t nbr1                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt0                         : 1;
	uint64_t nbt1                         : 1;
	uint64_t cam                          : 1;
	uint64_t reserved_9_15                : 7;
	uint64_t pp                           : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn52xx;
	struct cvmx_pow_bist_stat_cn52xx      cn52xxp1;
	struct cvmx_pow_bist_stat_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t pp                           : 12; /**< Physical PP BIST status */
	uint64_t reserved_10_15               : 6;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbt                          : 1;  /**< NCB transmitter memory BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t nbr1                         : 1;  /**< NCB receiver memory 1 BIST status */
	uint64_t nbr0                         : 1;  /**< NCB receiver memory 0 BIST status */
	uint64_t pend1                        : 1;  /**< Pending switch memory 1 BIST status */
	uint64_t pend0                        : 1;  /**< Pending switch memory 0 BIST status */
	uint64_t adr1                         : 1;  /**< Address memory 1 BIST status */
	uint64_t adr0                         : 1;  /**< Address memory 0 BIST status */
#else
	uint64_t adr0                         : 1;
	uint64_t adr1                         : 1;
	uint64_t pend0                        : 1;
	uint64_t pend1                        : 1;
	uint64_t nbr0                         : 1;
	uint64_t nbr1                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt                          : 1;
	uint64_t cam                          : 1;
	uint64_t reserved_10_15               : 6;
	uint64_t pp                           : 12;
	uint64_t reserved_28_63               : 36;
#endif
	} cn56xx;
	struct cvmx_pow_bist_stat_cn56xx      cn56xxp1;
	struct cvmx_pow_bist_stat_cn38xx      cn58xx;
	struct cvmx_pow_bist_stat_cn38xx      cn58xxp1;
	struct cvmx_pow_bist_stat_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pp                           : 4;  /**< Physical PP BIST status */
	uint64_t reserved_12_15               : 4;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbr                          : 3;  /**< NCB receiver memory BIST status */
	uint64_t nbt                          : 4;  /**< NCB transmitter memory BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t pend                         : 1;  /**< Pending switch memory BIST status */
	uint64_t adr                          : 1;  /**< Address memory BIST status */
#else
	uint64_t adr                          : 1;
	uint64_t pend                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt                          : 4;
	uint64_t nbr                          : 3;
	uint64_t cam                          : 1;
	uint64_t reserved_12_15               : 4;
	uint64_t pp                           : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn61xx;
	struct cvmx_pow_bist_stat_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t pp                           : 6;  /**< Physical PP BIST status */
	uint64_t reserved_12_15               : 4;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbr                          : 3;  /**< NCB receiver memory BIST status */
	uint64_t nbt                          : 4;  /**< NCB transmitter memory BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t pend                         : 1;  /**< Pending switch memory BIST status */
	uint64_t adr                          : 1;  /**< Address memory BIST status */
#else
	uint64_t adr                          : 1;
	uint64_t pend                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt                          : 4;
	uint64_t nbr                          : 3;
	uint64_t cam                          : 1;
	uint64_t reserved_12_15               : 4;
	uint64_t pp                           : 6;
	uint64_t reserved_22_63               : 42;
#endif
	} cn63xx;
	struct cvmx_pow_bist_stat_cn63xx      cn63xxp1;
	struct cvmx_pow_bist_stat_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_26_63               : 38;
	uint64_t pp                           : 10; /**< Physical PP BIST status */
	uint64_t reserved_12_15               : 4;
	uint64_t cam                          : 1;  /**< POW CAM BIST status */
	uint64_t nbr                          : 3;  /**< NCB receiver memory BIST status */
	uint64_t nbt                          : 4;  /**< NCB transmitter memory BIST status */
	uint64_t index                        : 1;  /**< Index memory BIST status */
	uint64_t fidx                         : 1;  /**< Forward index memory BIST status */
	uint64_t pend                         : 1;  /**< Pending switch memory BIST status */
	uint64_t adr                          : 1;  /**< Address memory BIST status */
#else
	uint64_t adr                          : 1;
	uint64_t pend                         : 1;
	uint64_t fidx                         : 1;
	uint64_t index                        : 1;
	uint64_t nbt                          : 4;
	uint64_t nbr                          : 3;
	uint64_t cam                          : 1;
	uint64_t reserved_12_15               : 4;
	uint64_t pp                           : 10;
	uint64_t reserved_26_63               : 38;
#endif
	} cn66xx;
	struct cvmx_pow_bist_stat_cn61xx      cnf71xx;
};
typedef union cvmx_pow_bist_stat cvmx_pow_bist_stat_t;

/**
 * cvmx_pow_ds_pc
 *
 * POW_DS_PC = POW De-Schedule Performance Counter
 *
 * Counts the number of de-schedule requests.  Write to clear.
 */
union cvmx_pow_ds_pc {
	uint64_t u64;
	struct cvmx_pow_ds_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t ds_pc                        : 32; /**< De-schedule performance counter */
#else
	uint64_t ds_pc                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_ds_pc_s               cn30xx;
	struct cvmx_pow_ds_pc_s               cn31xx;
	struct cvmx_pow_ds_pc_s               cn38xx;
	struct cvmx_pow_ds_pc_s               cn38xxp2;
	struct cvmx_pow_ds_pc_s               cn50xx;
	struct cvmx_pow_ds_pc_s               cn52xx;
	struct cvmx_pow_ds_pc_s               cn52xxp1;
	struct cvmx_pow_ds_pc_s               cn56xx;
	struct cvmx_pow_ds_pc_s               cn56xxp1;
	struct cvmx_pow_ds_pc_s               cn58xx;
	struct cvmx_pow_ds_pc_s               cn58xxp1;
	struct cvmx_pow_ds_pc_s               cn61xx;
	struct cvmx_pow_ds_pc_s               cn63xx;
	struct cvmx_pow_ds_pc_s               cn63xxp1;
	struct cvmx_pow_ds_pc_s               cn66xx;
	struct cvmx_pow_ds_pc_s               cnf71xx;
};
typedef union cvmx_pow_ds_pc cvmx_pow_ds_pc_t;

/**
 * cvmx_pow_ecc_err
 *
 * POW_ECC_ERR = POW ECC Error Register
 *
 * Contains the single and double error bits and the corresponding interrupt enables for the ECC-
 * protected POW index memory.  Also contains the syndrome value in the event of an ECC error.
 *
 * Also contains the remote pointer error bit and interrupt enable.  RPE is set when the POW detected
 * corruption on one or more of the input queue lists in L2/DRAM (POW's local copy of the tail pointer
 * for the L2/DRAM input queue did not match the last entry on the the list).   This is caused by
 * L2/DRAM corruption, and is generally a fatal error because it likely caused POW to load bad work
 * queue entries.
 *
 * This register also contains the illegal operation error bits and the corresponding interrupt
 * enables as follows:
 *
 *  <0> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP from PP in NULL_NULL state
 *  <1> Received SWTAG/SWTAG_DESCH/DESCH/UPD_WQP from PP in NULL state
 *  <2> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/GET_WORK from PP with pending tag switch to ORDERED or ATOMIC
 *  <3> Received SWTAG/SWTAG_FULL/SWTAG_DESCH from PP with tag specified as NULL_NULL
 *  <4> Received SWTAG_FULL/SWTAG_DESCH from PP with tag specified as NULL
 *  <5> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP/GET_WORK/NULL_RD from PP with GET_WORK pending
 *  <6> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP/GET_WORK/NULL_RD from PP with NULL_RD pending
 *  <7> Received CLR_NSCHED from PP with SWTAG_DESCH/DESCH/CLR_NSCHED pending
 *  <8> Received SWTAG/SWTAG_FULL/SWTAG_DESCH/DESCH/UPD_WQP/GET_WORK/NULL_RD from PP with CLR_NSCHED pending
 *  <9> Received illegal opcode
 * <10> Received ADD_WORK with tag specified as NULL_NULL
 * <11> Received DBG load from PP with DBG load pending
 * <12> Received CSR load from PP with CSR load pending
 */
union cvmx_pow_ecc_err {
	uint64_t u64;
	struct cvmx_pow_ecc_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_45_63               : 19;
	uint64_t iop_ie                       : 13; /**< Illegal operation interrupt enables */
	uint64_t reserved_29_31               : 3;
	uint64_t iop                          : 13; /**< Illegal operation errors */
	uint64_t reserved_14_15               : 2;
	uint64_t rpe_ie                       : 1;  /**< Remote pointer error interrupt enable */
	uint64_t rpe                          : 1;  /**< Remote pointer error */
	uint64_t reserved_9_11                : 3;
	uint64_t syn                          : 5;  /**< Syndrome value (only valid when DBE or SBE is set) */
	uint64_t dbe_ie                       : 1;  /**< Double bit error interrupt enable */
	uint64_t sbe_ie                       : 1;  /**< Single bit error interrupt enable */
	uint64_t dbe                          : 1;  /**< Double bit error */
	uint64_t sbe                          : 1;  /**< Single bit error */
#else
	uint64_t sbe                          : 1;
	uint64_t dbe                          : 1;
	uint64_t sbe_ie                       : 1;
	uint64_t dbe_ie                       : 1;
	uint64_t syn                          : 5;
	uint64_t reserved_9_11                : 3;
	uint64_t rpe                          : 1;
	uint64_t rpe_ie                       : 1;
	uint64_t reserved_14_15               : 2;
	uint64_t iop                          : 13;
	uint64_t reserved_29_31               : 3;
	uint64_t iop_ie                       : 13;
	uint64_t reserved_45_63               : 19;
#endif
	} s;
	struct cvmx_pow_ecc_err_s             cn30xx;
	struct cvmx_pow_ecc_err_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t rpe_ie                       : 1;  /**< Remote pointer error interrupt enable */
	uint64_t rpe                          : 1;  /**< Remote pointer error */
	uint64_t reserved_9_11                : 3;
	uint64_t syn                          : 5;  /**< Syndrome value (only valid when DBE or SBE is set) */
	uint64_t dbe_ie                       : 1;  /**< Double bit error interrupt enable */
	uint64_t sbe_ie                       : 1;  /**< Single bit error interrupt enable */
	uint64_t dbe                          : 1;  /**< Double bit error */
	uint64_t sbe                          : 1;  /**< Single bit error */
#else
	uint64_t sbe                          : 1;
	uint64_t dbe                          : 1;
	uint64_t sbe_ie                       : 1;
	uint64_t dbe_ie                       : 1;
	uint64_t syn                          : 5;
	uint64_t reserved_9_11                : 3;
	uint64_t rpe                          : 1;
	uint64_t rpe_ie                       : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} cn31xx;
	struct cvmx_pow_ecc_err_s             cn38xx;
	struct cvmx_pow_ecc_err_cn31xx        cn38xxp2;
	struct cvmx_pow_ecc_err_s             cn50xx;
	struct cvmx_pow_ecc_err_s             cn52xx;
	struct cvmx_pow_ecc_err_s             cn52xxp1;
	struct cvmx_pow_ecc_err_s             cn56xx;
	struct cvmx_pow_ecc_err_s             cn56xxp1;
	struct cvmx_pow_ecc_err_s             cn58xx;
	struct cvmx_pow_ecc_err_s             cn58xxp1;
	struct cvmx_pow_ecc_err_s             cn61xx;
	struct cvmx_pow_ecc_err_s             cn63xx;
	struct cvmx_pow_ecc_err_s             cn63xxp1;
	struct cvmx_pow_ecc_err_s             cn66xx;
	struct cvmx_pow_ecc_err_s             cnf71xx;
};
typedef union cvmx_pow_ecc_err cvmx_pow_ecc_err_t;

/**
 * cvmx_pow_int_ctl
 *
 * POW_INT_CTL = POW Internal Control Register
 *
 * Contains POW internal control values (for internal use, not typically for customer use):
 *
 * PFR_DIS = Disable high-performance pre-fetch reset mode.
 *
 * NBR_THR = Assert ncb__busy when the number of remaining coherent bus NBR credits equals is less
 * than or equal to this value.
 */
union cvmx_pow_int_ctl {
	uint64_t u64;
	struct cvmx_pow_int_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t pfr_dis                      : 1;  /**< High-perf pre-fetch reset mode disable */
	uint64_t nbr_thr                      : 5;  /**< NBR busy threshold */
#else
	uint64_t nbr_thr                      : 5;
	uint64_t pfr_dis                      : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_pow_int_ctl_s             cn30xx;
	struct cvmx_pow_int_ctl_s             cn31xx;
	struct cvmx_pow_int_ctl_s             cn38xx;
	struct cvmx_pow_int_ctl_s             cn38xxp2;
	struct cvmx_pow_int_ctl_s             cn50xx;
	struct cvmx_pow_int_ctl_s             cn52xx;
	struct cvmx_pow_int_ctl_s             cn52xxp1;
	struct cvmx_pow_int_ctl_s             cn56xx;
	struct cvmx_pow_int_ctl_s             cn56xxp1;
	struct cvmx_pow_int_ctl_s             cn58xx;
	struct cvmx_pow_int_ctl_s             cn58xxp1;
	struct cvmx_pow_int_ctl_s             cn61xx;
	struct cvmx_pow_int_ctl_s             cn63xx;
	struct cvmx_pow_int_ctl_s             cn63xxp1;
	struct cvmx_pow_int_ctl_s             cn66xx;
	struct cvmx_pow_int_ctl_s             cnf71xx;
};
typedef union cvmx_pow_int_ctl cvmx_pow_int_ctl_t;

/**
 * cvmx_pow_iq_cnt#
 *
 * POW_IQ_CNTX = POW Input Queue Count Register (1 per QOS level)
 *
 * Contains a read-only count of the number of work queue entries for each QOS level.
 */
union cvmx_pow_iq_cntx {
	uint64_t u64;
	struct cvmx_pow_iq_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_cnt                       : 32; /**< Input queue count for QOS level X */
#else
	uint64_t iq_cnt                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_iq_cntx_s             cn30xx;
	struct cvmx_pow_iq_cntx_s             cn31xx;
	struct cvmx_pow_iq_cntx_s             cn38xx;
	struct cvmx_pow_iq_cntx_s             cn38xxp2;
	struct cvmx_pow_iq_cntx_s             cn50xx;
	struct cvmx_pow_iq_cntx_s             cn52xx;
	struct cvmx_pow_iq_cntx_s             cn52xxp1;
	struct cvmx_pow_iq_cntx_s             cn56xx;
	struct cvmx_pow_iq_cntx_s             cn56xxp1;
	struct cvmx_pow_iq_cntx_s             cn58xx;
	struct cvmx_pow_iq_cntx_s             cn58xxp1;
	struct cvmx_pow_iq_cntx_s             cn61xx;
	struct cvmx_pow_iq_cntx_s             cn63xx;
	struct cvmx_pow_iq_cntx_s             cn63xxp1;
	struct cvmx_pow_iq_cntx_s             cn66xx;
	struct cvmx_pow_iq_cntx_s             cnf71xx;
};
typedef union cvmx_pow_iq_cntx cvmx_pow_iq_cntx_t;

/**
 * cvmx_pow_iq_com_cnt
 *
 * POW_IQ_COM_CNT = POW Input Queue Combined Count Register
 *
 * Contains a read-only count of the total number of work queue entries in all QOS levels.
 */
union cvmx_pow_iq_com_cnt {
	uint64_t u64;
	struct cvmx_pow_iq_com_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_cnt                       : 32; /**< Input queue combined count */
#else
	uint64_t iq_cnt                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_iq_com_cnt_s          cn30xx;
	struct cvmx_pow_iq_com_cnt_s          cn31xx;
	struct cvmx_pow_iq_com_cnt_s          cn38xx;
	struct cvmx_pow_iq_com_cnt_s          cn38xxp2;
	struct cvmx_pow_iq_com_cnt_s          cn50xx;
	struct cvmx_pow_iq_com_cnt_s          cn52xx;
	struct cvmx_pow_iq_com_cnt_s          cn52xxp1;
	struct cvmx_pow_iq_com_cnt_s          cn56xx;
	struct cvmx_pow_iq_com_cnt_s          cn56xxp1;
	struct cvmx_pow_iq_com_cnt_s          cn58xx;
	struct cvmx_pow_iq_com_cnt_s          cn58xxp1;
	struct cvmx_pow_iq_com_cnt_s          cn61xx;
	struct cvmx_pow_iq_com_cnt_s          cn63xx;
	struct cvmx_pow_iq_com_cnt_s          cn63xxp1;
	struct cvmx_pow_iq_com_cnt_s          cn66xx;
	struct cvmx_pow_iq_com_cnt_s          cnf71xx;
};
typedef union cvmx_pow_iq_com_cnt cvmx_pow_iq_com_cnt_t;

/**
 * cvmx_pow_iq_int
 *
 * POW_IQ_INT = POW Input Queue Interrupt Register
 *
 * Contains the bits (1 per QOS level) that can trigger the input queue interrupt.  An IQ_INT bit
 * will be set if POW_IQ_CNT#QOS# changes and the resulting value is equal to POW_IQ_THR#QOS#.
 */
union cvmx_pow_iq_int {
	uint64_t u64;
	struct cvmx_pow_iq_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t iq_int                       : 8;  /**< Input queue interrupt bits */
#else
	uint64_t iq_int                       : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pow_iq_int_s              cn52xx;
	struct cvmx_pow_iq_int_s              cn52xxp1;
	struct cvmx_pow_iq_int_s              cn56xx;
	struct cvmx_pow_iq_int_s              cn56xxp1;
	struct cvmx_pow_iq_int_s              cn61xx;
	struct cvmx_pow_iq_int_s              cn63xx;
	struct cvmx_pow_iq_int_s              cn63xxp1;
	struct cvmx_pow_iq_int_s              cn66xx;
	struct cvmx_pow_iq_int_s              cnf71xx;
};
typedef union cvmx_pow_iq_int cvmx_pow_iq_int_t;

/**
 * cvmx_pow_iq_int_en
 *
 * POW_IQ_INT_EN = POW Input Queue Interrupt Enable Register
 *
 * Contains the bits (1 per QOS level) that enable the input queue interrupt.
 */
union cvmx_pow_iq_int_en {
	uint64_t u64;
	struct cvmx_pow_iq_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t int_en                       : 8;  /**< Input queue interrupt enable bits */
#else
	uint64_t int_en                       : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pow_iq_int_en_s           cn52xx;
	struct cvmx_pow_iq_int_en_s           cn52xxp1;
	struct cvmx_pow_iq_int_en_s           cn56xx;
	struct cvmx_pow_iq_int_en_s           cn56xxp1;
	struct cvmx_pow_iq_int_en_s           cn61xx;
	struct cvmx_pow_iq_int_en_s           cn63xx;
	struct cvmx_pow_iq_int_en_s           cn63xxp1;
	struct cvmx_pow_iq_int_en_s           cn66xx;
	struct cvmx_pow_iq_int_en_s           cnf71xx;
};
typedef union cvmx_pow_iq_int_en cvmx_pow_iq_int_en_t;

/**
 * cvmx_pow_iq_thr#
 *
 * POW_IQ_THRX = POW Input Queue Threshold Register (1 per QOS level)
 *
 * Threshold value for triggering input queue interrupts.
 */
union cvmx_pow_iq_thrx {
	uint64_t u64;
	struct cvmx_pow_iq_thrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_thr                       : 32; /**< Input queue threshold for QOS level X */
#else
	uint64_t iq_thr                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_iq_thrx_s             cn52xx;
	struct cvmx_pow_iq_thrx_s             cn52xxp1;
	struct cvmx_pow_iq_thrx_s             cn56xx;
	struct cvmx_pow_iq_thrx_s             cn56xxp1;
	struct cvmx_pow_iq_thrx_s             cn61xx;
	struct cvmx_pow_iq_thrx_s             cn63xx;
	struct cvmx_pow_iq_thrx_s             cn63xxp1;
	struct cvmx_pow_iq_thrx_s             cn66xx;
	struct cvmx_pow_iq_thrx_s             cnf71xx;
};
typedef union cvmx_pow_iq_thrx cvmx_pow_iq_thrx_t;

/**
 * cvmx_pow_nos_cnt
 *
 * POW_NOS_CNT = POW No-schedule Count Register
 *
 * Contains the number of work queue entries on the no-schedule list.
 */
union cvmx_pow_nos_cnt {
	uint64_t u64;
	struct cvmx_pow_nos_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t nos_cnt                      : 12; /**< # of work queue entries on the no-schedule list */
#else
	uint64_t nos_cnt                      : 12;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_pow_nos_cnt_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t nos_cnt                      : 7;  /**< # of work queue entries on the no-schedule list */
#else
	uint64_t nos_cnt                      : 7;
	uint64_t reserved_7_63                : 57;
#endif
	} cn30xx;
	struct cvmx_pow_nos_cnt_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t nos_cnt                      : 9;  /**< # of work queue entries on the no-schedule list */
#else
	uint64_t nos_cnt                      : 9;
	uint64_t reserved_9_63                : 55;
#endif
	} cn31xx;
	struct cvmx_pow_nos_cnt_s             cn38xx;
	struct cvmx_pow_nos_cnt_s             cn38xxp2;
	struct cvmx_pow_nos_cnt_cn31xx        cn50xx;
	struct cvmx_pow_nos_cnt_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t nos_cnt                      : 10; /**< # of work queue entries on the no-schedule list */
#else
	uint64_t nos_cnt                      : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} cn52xx;
	struct cvmx_pow_nos_cnt_cn52xx        cn52xxp1;
	struct cvmx_pow_nos_cnt_s             cn56xx;
	struct cvmx_pow_nos_cnt_s             cn56xxp1;
	struct cvmx_pow_nos_cnt_s             cn58xx;
	struct cvmx_pow_nos_cnt_s             cn58xxp1;
	struct cvmx_pow_nos_cnt_cn52xx        cn61xx;
	struct cvmx_pow_nos_cnt_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t nos_cnt                      : 11; /**< # of work queue entries on the no-schedule list */
#else
	uint64_t nos_cnt                      : 11;
	uint64_t reserved_11_63               : 53;
#endif
	} cn63xx;
	struct cvmx_pow_nos_cnt_cn63xx        cn63xxp1;
	struct cvmx_pow_nos_cnt_cn63xx        cn66xx;
	struct cvmx_pow_nos_cnt_cn52xx        cnf71xx;
};
typedef union cvmx_pow_nos_cnt cvmx_pow_nos_cnt_t;

/**
 * cvmx_pow_nw_tim
 *
 * POW_NW_TIM = POW New Work Timer Period Register
 *
 * Sets the minimum period for a new work request timeout.  Period is specified in n-1 notation
 * where the increment value is 1024 clock cycles.  Thus, a value of 0x0 in this register translates
 * to 1024 cycles, 0x1 translates to 2048 cycles, 0x2 translates to 3072 cycles, etc...  Note: the
 * maximum period for a new work request timeout is 2 times the minimum period.  Note: the new work
 * request timeout counter is reset when this register is written.
 *
 * There are two new work request timeout cases:
 *
 * - WAIT bit clear.  The new work request can timeout if the timer expires before the pre-fetch
 *   engine has reached the end of all work queues.  This can occur if the executable work queue
 *   entry is deep in the queue and the pre-fetch engine is subject to many resets (i.e. high switch,
 *   de-schedule, or new work load from other PP's).  Thus, it is possible for a PP to receive a work
 *   response with the NO_WORK bit set even though there was at least one executable entry in the
 *   work queues.  The other (and typical) scenario for receiving a NO_WORK response with the WAIT
 *   bit clear is that the pre-fetch engine has reached the end of all work queues without finding
 *   executable work.
 *
 * - WAIT bit set.  The new work request can timeout if the timer expires before the pre-fetch
 *   engine has found executable work.  In this case, the only scenario where the PP will receive a
 *   work response with the NO_WORK bit set is if the timer expires.  Note: it is still possible for
 *   a PP to receive a NO_WORK response even though there was at least one executable entry in the
 *   work queues.
 *
 * In either case, it's important to note that switches and de-schedules are higher priority
 * operations that can cause the pre-fetch engine to reset.  Thus in a system with many switches or
 * de-schedules occuring, it's possible for the new work timer to expire (resulting in NO_WORK
 * responses) before the pre-fetch engine is able to get very deep into the work queues.
 */
union cvmx_pow_nw_tim {
	uint64_t u64;
	struct cvmx_pow_nw_tim_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t nw_tim                       : 10; /**< New work timer period */
#else
	uint64_t nw_tim                       : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_pow_nw_tim_s              cn30xx;
	struct cvmx_pow_nw_tim_s              cn31xx;
	struct cvmx_pow_nw_tim_s              cn38xx;
	struct cvmx_pow_nw_tim_s              cn38xxp2;
	struct cvmx_pow_nw_tim_s              cn50xx;
	struct cvmx_pow_nw_tim_s              cn52xx;
	struct cvmx_pow_nw_tim_s              cn52xxp1;
	struct cvmx_pow_nw_tim_s              cn56xx;
	struct cvmx_pow_nw_tim_s              cn56xxp1;
	struct cvmx_pow_nw_tim_s              cn58xx;
	struct cvmx_pow_nw_tim_s              cn58xxp1;
	struct cvmx_pow_nw_tim_s              cn61xx;
	struct cvmx_pow_nw_tim_s              cn63xx;
	struct cvmx_pow_nw_tim_s              cn63xxp1;
	struct cvmx_pow_nw_tim_s              cn66xx;
	struct cvmx_pow_nw_tim_s              cnf71xx;
};
typedef union cvmx_pow_nw_tim cvmx_pow_nw_tim_t;

/**
 * cvmx_pow_pf_rst_msk
 *
 * POW_PF_RST_MSK = POW Prefetch Reset Mask
 *
 * Resets the work prefetch engine when work is stored in an internal buffer (either when the add
 * work arrives or when the work is reloaded from an external buffer) for an enabled QOS level
 * (1 bit per QOS level).
 */
union cvmx_pow_pf_rst_msk {
	uint64_t u64;
	struct cvmx_pow_pf_rst_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t rst_msk                      : 8;  /**< Prefetch engine reset mask */
#else
	uint64_t rst_msk                      : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pow_pf_rst_msk_s          cn50xx;
	struct cvmx_pow_pf_rst_msk_s          cn52xx;
	struct cvmx_pow_pf_rst_msk_s          cn52xxp1;
	struct cvmx_pow_pf_rst_msk_s          cn56xx;
	struct cvmx_pow_pf_rst_msk_s          cn56xxp1;
	struct cvmx_pow_pf_rst_msk_s          cn58xx;
	struct cvmx_pow_pf_rst_msk_s          cn58xxp1;
	struct cvmx_pow_pf_rst_msk_s          cn61xx;
	struct cvmx_pow_pf_rst_msk_s          cn63xx;
	struct cvmx_pow_pf_rst_msk_s          cn63xxp1;
	struct cvmx_pow_pf_rst_msk_s          cn66xx;
	struct cvmx_pow_pf_rst_msk_s          cnf71xx;
};
typedef union cvmx_pow_pf_rst_msk cvmx_pow_pf_rst_msk_t;

/**
 * cvmx_pow_pp_grp_msk#
 *
 * POW_PP_GRP_MSKX = POW PP Group Mask Register (1 per PP)
 *
 * Selects which group(s) a PP belongs to.  A '1' in any bit position sets the PP's membership in
 * the corresponding group.  A value of 0x0 will prevent the PP from receiving new work.  Note:
 * disabled or non-existent PP's should have this field set to 0xffff (the reset value) in order to
 * maximize POW performance.
 *
 * Also contains the QOS level priorities for each PP.  0x0 is highest priority, and 0x7 the lowest.
 * Setting the priority to 0xf will prevent that PP from receiving work from that QOS level.
 * Priority values 0x8 through 0xe are reserved and should not be used.  For a given PP, priorities
 * should begin at 0x0 and remain contiguous throughout the range.
 */
union cvmx_pow_pp_grp_mskx {
	uint64_t u64;
	struct cvmx_pow_pp_grp_mskx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t qos7_pri                     : 4;  /**< PPX priority for QOS level 7 */
	uint64_t qos6_pri                     : 4;  /**< PPX priority for QOS level 6 */
	uint64_t qos5_pri                     : 4;  /**< PPX priority for QOS level 5 */
	uint64_t qos4_pri                     : 4;  /**< PPX priority for QOS level 4 */
	uint64_t qos3_pri                     : 4;  /**< PPX priority for QOS level 3 */
	uint64_t qos2_pri                     : 4;  /**< PPX priority for QOS level 2 */
	uint64_t qos1_pri                     : 4;  /**< PPX priority for QOS level 1 */
	uint64_t qos0_pri                     : 4;  /**< PPX priority for QOS level 0 */
	uint64_t grp_msk                      : 16; /**< PPX group mask */
#else
	uint64_t grp_msk                      : 16;
	uint64_t qos0_pri                     : 4;
	uint64_t qos1_pri                     : 4;
	uint64_t qos2_pri                     : 4;
	uint64_t qos3_pri                     : 4;
	uint64_t qos4_pri                     : 4;
	uint64_t qos5_pri                     : 4;
	uint64_t qos6_pri                     : 4;
	uint64_t qos7_pri                     : 4;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pow_pp_grp_mskx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t grp_msk                      : 16; /**< PPX group mask */
#else
	uint64_t grp_msk                      : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} cn30xx;
	struct cvmx_pow_pp_grp_mskx_cn30xx    cn31xx;
	struct cvmx_pow_pp_grp_mskx_cn30xx    cn38xx;
	struct cvmx_pow_pp_grp_mskx_cn30xx    cn38xxp2;
	struct cvmx_pow_pp_grp_mskx_s         cn50xx;
	struct cvmx_pow_pp_grp_mskx_s         cn52xx;
	struct cvmx_pow_pp_grp_mskx_s         cn52xxp1;
	struct cvmx_pow_pp_grp_mskx_s         cn56xx;
	struct cvmx_pow_pp_grp_mskx_s         cn56xxp1;
	struct cvmx_pow_pp_grp_mskx_s         cn58xx;
	struct cvmx_pow_pp_grp_mskx_s         cn58xxp1;
	struct cvmx_pow_pp_grp_mskx_s         cn61xx;
	struct cvmx_pow_pp_grp_mskx_s         cn63xx;
	struct cvmx_pow_pp_grp_mskx_s         cn63xxp1;
	struct cvmx_pow_pp_grp_mskx_s         cn66xx;
	struct cvmx_pow_pp_grp_mskx_s         cnf71xx;
};
typedef union cvmx_pow_pp_grp_mskx cvmx_pow_pp_grp_mskx_t;

/**
 * cvmx_pow_qos_rnd#
 *
 * POW_QOS_RNDX = POW QOS Issue Round Register (4 rounds per register x 8 registers = 32 rounds)
 *
 * Contains the round definitions for issuing new work.  Each round consists of 8 bits with each bit
 * corresponding to a QOS level.  There are 4 rounds contained in each register for a total of 32
 * rounds.  The issue logic traverses through the rounds sequentially (lowest round to highest round)
 * in an attempt to find new work for each PP.  Within each round, the issue logic traverses through
 * the QOS levels sequentially (highest QOS to lowest QOS) skipping over each QOS level with a clear
 * bit in the round mask.  Note: setting a QOS level to all zeroes in all issue round registers will
 * prevent work from being issued from that QOS level.
 */
union cvmx_pow_qos_rndx {
	uint64_t u64;
	struct cvmx_pow_qos_rndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t rnd_p3                       : 8;  /**< Round mask for round Xx4+3 */
	uint64_t rnd_p2                       : 8;  /**< Round mask for round Xx4+2 */
	uint64_t rnd_p1                       : 8;  /**< Round mask for round Xx4+1 */
	uint64_t rnd                          : 8;  /**< Round mask for round Xx4 */
#else
	uint64_t rnd                          : 8;
	uint64_t rnd_p1                       : 8;
	uint64_t rnd_p2                       : 8;
	uint64_t rnd_p3                       : 8;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_qos_rndx_s            cn30xx;
	struct cvmx_pow_qos_rndx_s            cn31xx;
	struct cvmx_pow_qos_rndx_s            cn38xx;
	struct cvmx_pow_qos_rndx_s            cn38xxp2;
	struct cvmx_pow_qos_rndx_s            cn50xx;
	struct cvmx_pow_qos_rndx_s            cn52xx;
	struct cvmx_pow_qos_rndx_s            cn52xxp1;
	struct cvmx_pow_qos_rndx_s            cn56xx;
	struct cvmx_pow_qos_rndx_s            cn56xxp1;
	struct cvmx_pow_qos_rndx_s            cn58xx;
	struct cvmx_pow_qos_rndx_s            cn58xxp1;
	struct cvmx_pow_qos_rndx_s            cn61xx;
	struct cvmx_pow_qos_rndx_s            cn63xx;
	struct cvmx_pow_qos_rndx_s            cn63xxp1;
	struct cvmx_pow_qos_rndx_s            cn66xx;
	struct cvmx_pow_qos_rndx_s            cnf71xx;
};
typedef union cvmx_pow_qos_rndx cvmx_pow_qos_rndx_t;

/**
 * cvmx_pow_qos_thr#
 *
 * POW_QOS_THRX = POW QOS Threshold Register (1 per QOS level)
 *
 * Contains the thresholds for allocating POW internal storage buffers.  If the number of remaining
 * free buffers drops below the minimum threshold (MIN_THR) or the number of allocated buffers for
 * this QOS level rises above the maximum threshold (MAX_THR), future incoming work queue entries
 * will be buffered externally rather than internally.  This register also contains a read-only count
 * of the current number of free buffers (FREE_CNT), the number of internal buffers currently
 * allocated to this QOS level (BUF_CNT), and the total number of buffers on the de-schedule list
 * (DES_CNT) (which is not the same as the total number of de-scheduled buffers).
 */
union cvmx_pow_qos_thrx {
	uint64_t u64;
	struct cvmx_pow_qos_thrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t des_cnt                      : 12; /**< # of buffers on de-schedule list */
	uint64_t buf_cnt                      : 12; /**< # of internal buffers allocated to QOS level X */
	uint64_t free_cnt                     : 12; /**< # of total free buffers */
	uint64_t reserved_23_23               : 1;
	uint64_t max_thr                      : 11; /**< Max threshold for QOS level X */
	uint64_t reserved_11_11               : 1;
	uint64_t min_thr                      : 11; /**< Min threshold for QOS level X */
#else
	uint64_t min_thr                      : 11;
	uint64_t reserved_11_11               : 1;
	uint64_t max_thr                      : 11;
	uint64_t reserved_23_23               : 1;
	uint64_t free_cnt                     : 12;
	uint64_t buf_cnt                      : 12;
	uint64_t des_cnt                      : 12;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_pow_qos_thrx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_55_63               : 9;
	uint64_t des_cnt                      : 7;  /**< # of buffers on de-schedule list */
	uint64_t reserved_43_47               : 5;
	uint64_t buf_cnt                      : 7;  /**< # of internal buffers allocated to QOS level X */
	uint64_t reserved_31_35               : 5;
	uint64_t free_cnt                     : 7;  /**< # of total free buffers */
	uint64_t reserved_18_23               : 6;
	uint64_t max_thr                      : 6;  /**< Max threshold for QOS level X */
	uint64_t reserved_6_11                : 6;
	uint64_t min_thr                      : 6;  /**< Min threshold for QOS level X */
#else
	uint64_t min_thr                      : 6;
	uint64_t reserved_6_11                : 6;
	uint64_t max_thr                      : 6;
	uint64_t reserved_18_23               : 6;
	uint64_t free_cnt                     : 7;
	uint64_t reserved_31_35               : 5;
	uint64_t buf_cnt                      : 7;
	uint64_t reserved_43_47               : 5;
	uint64_t des_cnt                      : 7;
	uint64_t reserved_55_63               : 9;
#endif
	} cn30xx;
	struct cvmx_pow_qos_thrx_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_57_63               : 7;
	uint64_t des_cnt                      : 9;  /**< # of buffers on de-schedule list */
	uint64_t reserved_45_47               : 3;
	uint64_t buf_cnt                      : 9;  /**< # of internal buffers allocated to QOS level X */
	uint64_t reserved_33_35               : 3;
	uint64_t free_cnt                     : 9;  /**< # of total free buffers */
	uint64_t reserved_20_23               : 4;
	uint64_t max_thr                      : 8;  /**< Max threshold for QOS level X */
	uint64_t reserved_8_11                : 4;
	uint64_t min_thr                      : 8;  /**< Min threshold for QOS level X */
#else
	uint64_t min_thr                      : 8;
	uint64_t reserved_8_11                : 4;
	uint64_t max_thr                      : 8;
	uint64_t reserved_20_23               : 4;
	uint64_t free_cnt                     : 9;
	uint64_t reserved_33_35               : 3;
	uint64_t buf_cnt                      : 9;
	uint64_t reserved_45_47               : 3;
	uint64_t des_cnt                      : 9;
	uint64_t reserved_57_63               : 7;
#endif
	} cn31xx;
	struct cvmx_pow_qos_thrx_s            cn38xx;
	struct cvmx_pow_qos_thrx_s            cn38xxp2;
	struct cvmx_pow_qos_thrx_cn31xx       cn50xx;
	struct cvmx_pow_qos_thrx_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t des_cnt                      : 10; /**< # of buffers on de-schedule list */
	uint64_t reserved_46_47               : 2;
	uint64_t buf_cnt                      : 10; /**< # of internal buffers allocated to QOS level X */
	uint64_t reserved_34_35               : 2;
	uint64_t free_cnt                     : 10; /**< # of total free buffers */
	uint64_t reserved_21_23               : 3;
	uint64_t max_thr                      : 9;  /**< Max threshold for QOS level X */
	uint64_t reserved_9_11                : 3;
	uint64_t min_thr                      : 9;  /**< Min threshold for QOS level X */
#else
	uint64_t min_thr                      : 9;
	uint64_t reserved_9_11                : 3;
	uint64_t max_thr                      : 9;
	uint64_t reserved_21_23               : 3;
	uint64_t free_cnt                     : 10;
	uint64_t reserved_34_35               : 2;
	uint64_t buf_cnt                      : 10;
	uint64_t reserved_46_47               : 2;
	uint64_t des_cnt                      : 10;
	uint64_t reserved_58_63               : 6;
#endif
	} cn52xx;
	struct cvmx_pow_qos_thrx_cn52xx       cn52xxp1;
	struct cvmx_pow_qos_thrx_s            cn56xx;
	struct cvmx_pow_qos_thrx_s            cn56xxp1;
	struct cvmx_pow_qos_thrx_s            cn58xx;
	struct cvmx_pow_qos_thrx_s            cn58xxp1;
	struct cvmx_pow_qos_thrx_cn52xx       cn61xx;
	struct cvmx_pow_qos_thrx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t des_cnt                      : 11; /**< # of buffers on de-schedule list */
	uint64_t reserved_47_47               : 1;
	uint64_t buf_cnt                      : 11; /**< # of internal buffers allocated to QOS level X */
	uint64_t reserved_35_35               : 1;
	uint64_t free_cnt                     : 11; /**< # of total free buffers */
	uint64_t reserved_22_23               : 2;
	uint64_t max_thr                      : 10; /**< Max threshold for QOS level X */
	uint64_t reserved_10_11               : 2;
	uint64_t min_thr                      : 10; /**< Min threshold for QOS level X */
#else
	uint64_t min_thr                      : 10;
	uint64_t reserved_10_11               : 2;
	uint64_t max_thr                      : 10;
	uint64_t reserved_22_23               : 2;
	uint64_t free_cnt                     : 11;
	uint64_t reserved_35_35               : 1;
	uint64_t buf_cnt                      : 11;
	uint64_t reserved_47_47               : 1;
	uint64_t des_cnt                      : 11;
	uint64_t reserved_59_63               : 5;
#endif
	} cn63xx;
	struct cvmx_pow_qos_thrx_cn63xx       cn63xxp1;
	struct cvmx_pow_qos_thrx_cn63xx       cn66xx;
	struct cvmx_pow_qos_thrx_cn52xx       cnf71xx;
};
typedef union cvmx_pow_qos_thrx cvmx_pow_qos_thrx_t;

/**
 * cvmx_pow_ts_pc
 *
 * POW_TS_PC = POW Tag Switch Performance Counter
 *
 * Counts the number of tag switch requests.  Write to clear.
 */
union cvmx_pow_ts_pc {
	uint64_t u64;
	struct cvmx_pow_ts_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t ts_pc                        : 32; /**< Tag switch performance counter */
#else
	uint64_t ts_pc                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_ts_pc_s               cn30xx;
	struct cvmx_pow_ts_pc_s               cn31xx;
	struct cvmx_pow_ts_pc_s               cn38xx;
	struct cvmx_pow_ts_pc_s               cn38xxp2;
	struct cvmx_pow_ts_pc_s               cn50xx;
	struct cvmx_pow_ts_pc_s               cn52xx;
	struct cvmx_pow_ts_pc_s               cn52xxp1;
	struct cvmx_pow_ts_pc_s               cn56xx;
	struct cvmx_pow_ts_pc_s               cn56xxp1;
	struct cvmx_pow_ts_pc_s               cn58xx;
	struct cvmx_pow_ts_pc_s               cn58xxp1;
	struct cvmx_pow_ts_pc_s               cn61xx;
	struct cvmx_pow_ts_pc_s               cn63xx;
	struct cvmx_pow_ts_pc_s               cn63xxp1;
	struct cvmx_pow_ts_pc_s               cn66xx;
	struct cvmx_pow_ts_pc_s               cnf71xx;
};
typedef union cvmx_pow_ts_pc cvmx_pow_ts_pc_t;

/**
 * cvmx_pow_wa_com_pc
 *
 * POW_WA_COM_PC = POW Work Add Combined Performance Counter
 *
 * Counts the number of add new work requests for all QOS levels.  Write to clear.
 */
union cvmx_pow_wa_com_pc {
	uint64_t u64;
	struct cvmx_pow_wa_com_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wa_pc                        : 32; /**< Work add combined performance counter */
#else
	uint64_t wa_pc                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_wa_com_pc_s           cn30xx;
	struct cvmx_pow_wa_com_pc_s           cn31xx;
	struct cvmx_pow_wa_com_pc_s           cn38xx;
	struct cvmx_pow_wa_com_pc_s           cn38xxp2;
	struct cvmx_pow_wa_com_pc_s           cn50xx;
	struct cvmx_pow_wa_com_pc_s           cn52xx;
	struct cvmx_pow_wa_com_pc_s           cn52xxp1;
	struct cvmx_pow_wa_com_pc_s           cn56xx;
	struct cvmx_pow_wa_com_pc_s           cn56xxp1;
	struct cvmx_pow_wa_com_pc_s           cn58xx;
	struct cvmx_pow_wa_com_pc_s           cn58xxp1;
	struct cvmx_pow_wa_com_pc_s           cn61xx;
	struct cvmx_pow_wa_com_pc_s           cn63xx;
	struct cvmx_pow_wa_com_pc_s           cn63xxp1;
	struct cvmx_pow_wa_com_pc_s           cn66xx;
	struct cvmx_pow_wa_com_pc_s           cnf71xx;
};
typedef union cvmx_pow_wa_com_pc cvmx_pow_wa_com_pc_t;

/**
 * cvmx_pow_wa_pc#
 *
 * POW_WA_PCX = POW Work Add Performance Counter (1 per QOS level)
 *
 * Counts the number of add new work requests for each QOS level.  Write to clear.
 */
union cvmx_pow_wa_pcx {
	uint64_t u64;
	struct cvmx_pow_wa_pcx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wa_pc                        : 32; /**< Work add performance counter for QOS level X */
#else
	uint64_t wa_pc                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_wa_pcx_s              cn30xx;
	struct cvmx_pow_wa_pcx_s              cn31xx;
	struct cvmx_pow_wa_pcx_s              cn38xx;
	struct cvmx_pow_wa_pcx_s              cn38xxp2;
	struct cvmx_pow_wa_pcx_s              cn50xx;
	struct cvmx_pow_wa_pcx_s              cn52xx;
	struct cvmx_pow_wa_pcx_s              cn52xxp1;
	struct cvmx_pow_wa_pcx_s              cn56xx;
	struct cvmx_pow_wa_pcx_s              cn56xxp1;
	struct cvmx_pow_wa_pcx_s              cn58xx;
	struct cvmx_pow_wa_pcx_s              cn58xxp1;
	struct cvmx_pow_wa_pcx_s              cn61xx;
	struct cvmx_pow_wa_pcx_s              cn63xx;
	struct cvmx_pow_wa_pcx_s              cn63xxp1;
	struct cvmx_pow_wa_pcx_s              cn66xx;
	struct cvmx_pow_wa_pcx_s              cnf71xx;
};
typedef union cvmx_pow_wa_pcx cvmx_pow_wa_pcx_t;

/**
 * cvmx_pow_wq_int
 *
 * POW_WQ_INT = POW Work Queue Interrupt Register
 *
 * Contains the bits (1 per group) that set work queue interrupts and are used to clear these
 * interrupts.  Also contains the input queue interrupt temporary disable bits (1 per group).  For
 * more information regarding this register, see the interrupt section.
 */
union cvmx_pow_wq_int {
	uint64_t u64;
	struct cvmx_pow_wq_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iq_dis                       : 16; /**< Input queue interrupt temporary disable mask
                                                         Corresponding WQ_INT<*> bit cannot be set due to
                                                         IQ_CNT/IQ_THR check when this bit is set.
                                                         Corresponding IQ_DIS bit is cleared by HW whenever:
                                                          - POW_WQ_INT_CNT*[IQ_CNT] is zero, or
                                                          - POW_WQ_INT_CNT*[TC_CNT]==1 when periodic
                                                            counter POW_WQ_INT_PC[PC]==0 */
	uint64_t wq_int                       : 16; /**< Work queue interrupt bits
                                                         Corresponding WQ_INT bit is set by HW whenever:
                                                          - POW_WQ_INT_CNT*[IQ_CNT] >=
                                                            POW_WQ_INT_THR*[IQ_THR] and the threshold
                                                            interrupt is not disabled.
                                                            IQ_DIS<*>==1 disables the interrupt.
                                                            POW_WQ_INT_THR*[IQ_THR]==0 disables the int.
                                                          - POW_WQ_INT_CNT*[DS_CNT] >=
                                                            POW_WQ_INT_THR*[DS_THR] and the threshold
                                                            interrupt is not disabled
                                                            POW_WQ_INT_THR*[DS_THR]==0 disables the int.
                                                          - POW_WQ_INT_CNT*[TC_CNT]==1 when periodic
                                                            counter POW_WQ_INT_PC[PC]==0 and
                                                            POW_WQ_INT_THR*[TC_EN]==1 and at least one of:
                                                            - POW_WQ_INT_CNT*[IQ_CNT] > 0
                                                            - POW_WQ_INT_CNT*[DS_CNT] > 0 */
#else
	uint64_t wq_int                       : 16;
	uint64_t iq_dis                       : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_wq_int_s              cn30xx;
	struct cvmx_pow_wq_int_s              cn31xx;
	struct cvmx_pow_wq_int_s              cn38xx;
	struct cvmx_pow_wq_int_s              cn38xxp2;
	struct cvmx_pow_wq_int_s              cn50xx;
	struct cvmx_pow_wq_int_s              cn52xx;
	struct cvmx_pow_wq_int_s              cn52xxp1;
	struct cvmx_pow_wq_int_s              cn56xx;
	struct cvmx_pow_wq_int_s              cn56xxp1;
	struct cvmx_pow_wq_int_s              cn58xx;
	struct cvmx_pow_wq_int_s              cn58xxp1;
	struct cvmx_pow_wq_int_s              cn61xx;
	struct cvmx_pow_wq_int_s              cn63xx;
	struct cvmx_pow_wq_int_s              cn63xxp1;
	struct cvmx_pow_wq_int_s              cn66xx;
	struct cvmx_pow_wq_int_s              cnf71xx;
};
typedef union cvmx_pow_wq_int cvmx_pow_wq_int_t;

/**
 * cvmx_pow_wq_int_cnt#
 *
 * POW_WQ_INT_CNTX = POW Work Queue Interrupt Count Register (1 per group)
 *
 * Contains a read-only copy of the counts used to trigger work queue interrupts.  For more
 * information regarding this register, see the interrupt section.
 */
union cvmx_pow_wq_int_cntx {
	uint64_t u64;
	struct cvmx_pow_wq_int_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t tc_cnt                       : 4;  /**< Time counter current value for group X
                                                         HW sets TC_CNT to POW_WQ_INT_THR*[TC_THR] whenever:
                                                          - corresponding POW_WQ_INT_CNT*[IQ_CNT]==0 and
                                                            corresponding POW_WQ_INT_CNT*[DS_CNT]==0
                                                          - corresponding POW_WQ_INT[WQ_INT<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT[IQ_DIS<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT_THR* is written by SW
                                                          - TC_CNT==1 and periodic counter
                                                            POW_WQ_INT_PC[PC]==0
                                                         Otherwise, HW decrements TC_CNT whenever the
                                                         periodic counter POW_WQ_INT_PC[PC]==0.
                                                         TC_CNT is 0 whenever POW_WQ_INT_THR*[TC_THR]==0. */
	uint64_t ds_cnt                       : 12; /**< De-schedule executable count for group X */
	uint64_t iq_cnt                       : 12; /**< Input queue executable count for group X */
#else
	uint64_t iq_cnt                       : 12;
	uint64_t ds_cnt                       : 12;
	uint64_t tc_cnt                       : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_pow_wq_int_cntx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t tc_cnt                       : 4;  /**< Time counter current value for group X
                                                         HW sets TC_CNT to POW_WQ_INT_THR*[TC_THR] whenever:
                                                          - corresponding POW_WQ_INT_CNT*[IQ_CNT]==0 and
                                                            corresponding POW_WQ_INT_CNT*[DS_CNT]==0
                                                          - corresponding POW_WQ_INT[WQ_INT<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT[IQ_DIS<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT_THR* is written by SW
                                                          - TC_CNT==1 and periodic counter
                                                            POW_WQ_INT_PC[PC]==0
                                                         Otherwise, HW decrements TC_CNT whenever the
                                                         periodic counter POW_WQ_INT_PC[PC]==0.
                                                         TC_CNT is 0 whenever POW_WQ_INT_THR*[TC_THR]==0. */
	uint64_t reserved_19_23               : 5;
	uint64_t ds_cnt                       : 7;  /**< De-schedule executable count for group X */
	uint64_t reserved_7_11                : 5;
	uint64_t iq_cnt                       : 7;  /**< Input queue executable count for group X */
#else
	uint64_t iq_cnt                       : 7;
	uint64_t reserved_7_11                : 5;
	uint64_t ds_cnt                       : 7;
	uint64_t reserved_19_23               : 5;
	uint64_t tc_cnt                       : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} cn30xx;
	struct cvmx_pow_wq_int_cntx_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t tc_cnt                       : 4;  /**< Time counter current value for group X
                                                         HW sets TC_CNT to POW_WQ_INT_THR*[TC_THR] whenever:
                                                          - corresponding POW_WQ_INT_CNT*[IQ_CNT]==0 and
                                                            corresponding POW_WQ_INT_CNT*[DS_CNT]==0
                                                          - corresponding POW_WQ_INT[WQ_INT<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT[IQ_DIS<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT_THR* is written by SW
                                                          - TC_CNT==1 and periodic counter
                                                            POW_WQ_INT_PC[PC]==0
                                                         Otherwise, HW decrements TC_CNT whenever the
                                                         periodic counter POW_WQ_INT_PC[PC]==0.
                                                         TC_CNT is 0 whenever POW_WQ_INT_THR*[TC_THR]==0. */
	uint64_t reserved_21_23               : 3;
	uint64_t ds_cnt                       : 9;  /**< De-schedule executable count for group X */
	uint64_t reserved_9_11                : 3;
	uint64_t iq_cnt                       : 9;  /**< Input queue executable count for group X */
#else
	uint64_t iq_cnt                       : 9;
	uint64_t reserved_9_11                : 3;
	uint64_t ds_cnt                       : 9;
	uint64_t reserved_21_23               : 3;
	uint64_t tc_cnt                       : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} cn31xx;
	struct cvmx_pow_wq_int_cntx_s         cn38xx;
	struct cvmx_pow_wq_int_cntx_s         cn38xxp2;
	struct cvmx_pow_wq_int_cntx_cn31xx    cn50xx;
	struct cvmx_pow_wq_int_cntx_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t tc_cnt                       : 4;  /**< Time counter current value for group X
                                                         HW sets TC_CNT to POW_WQ_INT_THR*[TC_THR] whenever:
                                                          - corresponding POW_WQ_INT_CNT*[IQ_CNT]==0 and
                                                            corresponding POW_WQ_INT_CNT*[DS_CNT]==0
                                                          - corresponding POW_WQ_INT[WQ_INT<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT[IQ_DIS<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT_THR* is written by SW
                                                          - TC_CNT==1 and periodic counter
                                                            POW_WQ_INT_PC[PC]==0
                                                         Otherwise, HW decrements TC_CNT whenever the
                                                         periodic counter POW_WQ_INT_PC[PC]==0.
                                                         TC_CNT is 0 whenever POW_WQ_INT_THR*[TC_THR]==0. */
	uint64_t reserved_22_23               : 2;
	uint64_t ds_cnt                       : 10; /**< De-schedule executable count for group X */
	uint64_t reserved_10_11               : 2;
	uint64_t iq_cnt                       : 10; /**< Input queue executable count for group X */
#else
	uint64_t iq_cnt                       : 10;
	uint64_t reserved_10_11               : 2;
	uint64_t ds_cnt                       : 10;
	uint64_t reserved_22_23               : 2;
	uint64_t tc_cnt                       : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} cn52xx;
	struct cvmx_pow_wq_int_cntx_cn52xx    cn52xxp1;
	struct cvmx_pow_wq_int_cntx_s         cn56xx;
	struct cvmx_pow_wq_int_cntx_s         cn56xxp1;
	struct cvmx_pow_wq_int_cntx_s         cn58xx;
	struct cvmx_pow_wq_int_cntx_s         cn58xxp1;
	struct cvmx_pow_wq_int_cntx_cn52xx    cn61xx;
	struct cvmx_pow_wq_int_cntx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t tc_cnt                       : 4;  /**< Time counter current value for group X
                                                         HW sets TC_CNT to POW_WQ_INT_THR*[TC_THR] whenever:
                                                          - corresponding POW_WQ_INT_CNT*[IQ_CNT]==0 and
                                                            corresponding POW_WQ_INT_CNT*[DS_CNT]==0
                                                          - corresponding POW_WQ_INT[WQ_INT<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT[IQ_DIS<*>] is written
                                                            with a 1 by SW
                                                          - corresponding POW_WQ_INT_THR* is written by SW
                                                          - TC_CNT==1 and periodic counter
                                                            POW_WQ_INT_PC[PC]==0
                                                         Otherwise, HW decrements TC_CNT whenever the
                                                         periodic counter POW_WQ_INT_PC[PC]==0.
                                                         TC_CNT is 0 whenever POW_WQ_INT_THR*[TC_THR]==0. */
	uint64_t reserved_23_23               : 1;
	uint64_t ds_cnt                       : 11; /**< De-schedule executable count for group X */
	uint64_t reserved_11_11               : 1;
	uint64_t iq_cnt                       : 11; /**< Input queue executable count for group X */
#else
	uint64_t iq_cnt                       : 11;
	uint64_t reserved_11_11               : 1;
	uint64_t ds_cnt                       : 11;
	uint64_t reserved_23_23               : 1;
	uint64_t tc_cnt                       : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} cn63xx;
	struct cvmx_pow_wq_int_cntx_cn63xx    cn63xxp1;
	struct cvmx_pow_wq_int_cntx_cn63xx    cn66xx;
	struct cvmx_pow_wq_int_cntx_cn52xx    cnf71xx;
};
typedef union cvmx_pow_wq_int_cntx cvmx_pow_wq_int_cntx_t;

/**
 * cvmx_pow_wq_int_pc
 *
 * POW_WQ_INT_PC = POW Work Queue Interrupt Periodic Counter Register
 *
 * Contains the threshold value for the work queue interrupt periodic counter and also a read-only
 * copy of the periodic counter.  For more information regarding this register, see the interrupt
 * section.
 */
union cvmx_pow_wq_int_pc {
	uint64_t u64;
	struct cvmx_pow_wq_int_pc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t pc                           : 28; /**< Work queue interrupt periodic counter */
	uint64_t reserved_28_31               : 4;
	uint64_t pc_thr                       : 20; /**< Work queue interrupt periodic counter threshold */
	uint64_t reserved_0_7                 : 8;
#else
	uint64_t reserved_0_7                 : 8;
	uint64_t pc_thr                       : 20;
	uint64_t reserved_28_31               : 4;
	uint64_t pc                           : 28;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_pow_wq_int_pc_s           cn30xx;
	struct cvmx_pow_wq_int_pc_s           cn31xx;
	struct cvmx_pow_wq_int_pc_s           cn38xx;
	struct cvmx_pow_wq_int_pc_s           cn38xxp2;
	struct cvmx_pow_wq_int_pc_s           cn50xx;
	struct cvmx_pow_wq_int_pc_s           cn52xx;
	struct cvmx_pow_wq_int_pc_s           cn52xxp1;
	struct cvmx_pow_wq_int_pc_s           cn56xx;
	struct cvmx_pow_wq_int_pc_s           cn56xxp1;
	struct cvmx_pow_wq_int_pc_s           cn58xx;
	struct cvmx_pow_wq_int_pc_s           cn58xxp1;
	struct cvmx_pow_wq_int_pc_s           cn61xx;
	struct cvmx_pow_wq_int_pc_s           cn63xx;
	struct cvmx_pow_wq_int_pc_s           cn63xxp1;
	struct cvmx_pow_wq_int_pc_s           cn66xx;
	struct cvmx_pow_wq_int_pc_s           cnf71xx;
};
typedef union cvmx_pow_wq_int_pc cvmx_pow_wq_int_pc_t;

/**
 * cvmx_pow_wq_int_thr#
 *
 * POW_WQ_INT_THRX = POW Work Queue Interrupt Threshold Register (1 per group)
 *
 * Contains the thresholds for enabling and setting work queue interrupts.  For more information
 * regarding this register, see the interrupt section.
 *
 * Note: Up to 4 of the POW's internal storage buffers can be allocated for hardware use and are
 * therefore not available for incoming work queue entries.  Additionally, any PP that is not in the
 * NULL_NULL state consumes a buffer.  Thus in a 4 PP system, it is not advisable to set either
 * IQ_THR or DS_THR to greater than 512 - 4 - 4 = 504.  Doing so may prevent the interrupt from
 * ever triggering.
 */
union cvmx_pow_wq_int_thrx {
	uint64_t u64;
	struct cvmx_pow_wq_int_thrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t tc_en                        : 1;  /**< Time counter interrupt enable for group X
                                                         TC_EN must be zero when TC_THR==0 */
	uint64_t tc_thr                       : 4;  /**< Time counter interrupt threshold for group X
                                                         When TC_THR==0, POW_WQ_INT_CNT*[TC_CNT] is zero */
	uint64_t reserved_23_23               : 1;
	uint64_t ds_thr                       : 11; /**< De-schedule count threshold for group X
                                                         DS_THR==0 disables the threshold interrupt */
	uint64_t reserved_11_11               : 1;
	uint64_t iq_thr                       : 11; /**< Input queue count threshold for group X
                                                         IQ_THR==0 disables the threshold interrupt */
#else
	uint64_t iq_thr                       : 11;
	uint64_t reserved_11_11               : 1;
	uint64_t ds_thr                       : 11;
	uint64_t reserved_23_23               : 1;
	uint64_t tc_thr                       : 4;
	uint64_t tc_en                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_pow_wq_int_thrx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t tc_en                        : 1;  /**< Time counter interrupt enable for group X
                                                         TC_EN must be zero when TC_THR==0 */
	uint64_t tc_thr                       : 4;  /**< Time counter interrupt threshold for group X
                                                         When TC_THR==0, POW_WQ_INT_CNT*[TC_CNT] is zero */
	uint64_t reserved_18_23               : 6;
	uint64_t ds_thr                       : 6;  /**< De-schedule count threshold for group X
                                                         DS_THR==0 disables the threshold interrupt */
	uint64_t reserved_6_11                : 6;
	uint64_t iq_thr                       : 6;  /**< Input queue count threshold for group X
                                                         IQ_THR==0 disables the threshold interrupt */
#else
	uint64_t iq_thr                       : 6;
	uint64_t reserved_6_11                : 6;
	uint64_t ds_thr                       : 6;
	uint64_t reserved_18_23               : 6;
	uint64_t tc_thr                       : 4;
	uint64_t tc_en                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn30xx;
	struct cvmx_pow_wq_int_thrx_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t tc_en                        : 1;  /**< Time counter interrupt enable for group X
                                                         TC_EN must be zero when TC_THR==0 */
	uint64_t tc_thr                       : 4;  /**< Time counter interrupt threshold for group X
                                                         When TC_THR==0, POW_WQ_INT_CNT*[TC_CNT] is zero */
	uint64_t reserved_20_23               : 4;
	uint64_t ds_thr                       : 8;  /**< De-schedule count threshold for group X
                                                         DS_THR==0 disables the threshold interrupt */
	uint64_t reserved_8_11                : 4;
	uint64_t iq_thr                       : 8;  /**< Input queue count threshold for group X
                                                         IQ_THR==0 disables the threshold interrupt */
#else
	uint64_t iq_thr                       : 8;
	uint64_t reserved_8_11                : 4;
	uint64_t ds_thr                       : 8;
	uint64_t reserved_20_23               : 4;
	uint64_t tc_thr                       : 4;
	uint64_t tc_en                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn31xx;
	struct cvmx_pow_wq_int_thrx_s         cn38xx;
	struct cvmx_pow_wq_int_thrx_s         cn38xxp2;
	struct cvmx_pow_wq_int_thrx_cn31xx    cn50xx;
	struct cvmx_pow_wq_int_thrx_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t tc_en                        : 1;  /**< Time counter interrupt enable for group X
                                                         TC_EN must be zero when TC_THR==0 */
	uint64_t tc_thr                       : 4;  /**< Time counter interrupt threshold for group X
                                                         When TC_THR==0, POW_WQ_INT_CNT*[TC_CNT] is zero */
	uint64_t reserved_21_23               : 3;
	uint64_t ds_thr                       : 9;  /**< De-schedule count threshold for group X
                                                         DS_THR==0 disables the threshold interrupt */
	uint64_t reserved_9_11                : 3;
	uint64_t iq_thr                       : 9;  /**< Input queue count threshold for group X
                                                         IQ_THR==0 disables the threshold interrupt */
#else
	uint64_t iq_thr                       : 9;
	uint64_t reserved_9_11                : 3;
	uint64_t ds_thr                       : 9;
	uint64_t reserved_21_23               : 3;
	uint64_t tc_thr                       : 4;
	uint64_t tc_en                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn52xx;
	struct cvmx_pow_wq_int_thrx_cn52xx    cn52xxp1;
	struct cvmx_pow_wq_int_thrx_s         cn56xx;
	struct cvmx_pow_wq_int_thrx_s         cn56xxp1;
	struct cvmx_pow_wq_int_thrx_s         cn58xx;
	struct cvmx_pow_wq_int_thrx_s         cn58xxp1;
	struct cvmx_pow_wq_int_thrx_cn52xx    cn61xx;
	struct cvmx_pow_wq_int_thrx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t tc_en                        : 1;  /**< Time counter interrupt enable for group X
                                                         TC_EN must be zero when TC_THR==0 */
	uint64_t tc_thr                       : 4;  /**< Time counter interrupt threshold for group X
                                                         When TC_THR==0, POW_WQ_INT_CNT*[TC_CNT] is zero */
	uint64_t reserved_22_23               : 2;
	uint64_t ds_thr                       : 10; /**< De-schedule count threshold for group X
                                                         DS_THR==0 disables the threshold interrupt */
	uint64_t reserved_10_11               : 2;
	uint64_t iq_thr                       : 10; /**< Input queue count threshold for group X
                                                         IQ_THR==0 disables the threshold interrupt */
#else
	uint64_t iq_thr                       : 10;
	uint64_t reserved_10_11               : 2;
	uint64_t ds_thr                       : 10;
	uint64_t reserved_22_23               : 2;
	uint64_t tc_thr                       : 4;
	uint64_t tc_en                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn63xx;
	struct cvmx_pow_wq_int_thrx_cn63xx    cn63xxp1;
	struct cvmx_pow_wq_int_thrx_cn63xx    cn66xx;
	struct cvmx_pow_wq_int_thrx_cn52xx    cnf71xx;
};
typedef union cvmx_pow_wq_int_thrx cvmx_pow_wq_int_thrx_t;

/**
 * cvmx_pow_ws_pc#
 *
 * POW_WS_PCX = POW Work Schedule Performance Counter (1 per group)
 *
 * Counts the number of work schedules for each group.  Write to clear.
 */
union cvmx_pow_ws_pcx {
	uint64_t u64;
	struct cvmx_pow_ws_pcx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t ws_pc                        : 32; /**< Work schedule performance counter for group X */
#else
	uint64_t ws_pc                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pow_ws_pcx_s              cn30xx;
	struct cvmx_pow_ws_pcx_s              cn31xx;
	struct cvmx_pow_ws_pcx_s              cn38xx;
	struct cvmx_pow_ws_pcx_s              cn38xxp2;
	struct cvmx_pow_ws_pcx_s              cn50xx;
	struct cvmx_pow_ws_pcx_s              cn52xx;
	struct cvmx_pow_ws_pcx_s              cn52xxp1;
	struct cvmx_pow_ws_pcx_s              cn56xx;
	struct cvmx_pow_ws_pcx_s              cn56xxp1;
	struct cvmx_pow_ws_pcx_s              cn58xx;
	struct cvmx_pow_ws_pcx_s              cn58xxp1;
	struct cvmx_pow_ws_pcx_s              cn61xx;
	struct cvmx_pow_ws_pcx_s              cn63xx;
	struct cvmx_pow_ws_pcx_s              cn63xxp1;
	struct cvmx_pow_ws_pcx_s              cn66xx;
	struct cvmx_pow_ws_pcx_s              cnf71xx;
};
typedef union cvmx_pow_ws_pcx cvmx_pow_ws_pcx_t;

#endif
