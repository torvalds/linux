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
 * cvmx-pciercx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pciercx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PCIERCX_DEFS_H__
#define __CVMX_PCIERCX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG000(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG000(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000000ull;
}
#else
#define CVMX_PCIERCX_CFG000(block_id) (0x0000000000000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG001(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG001(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000004ull;
}
#else
#define CVMX_PCIERCX_CFG001(block_id) (0x0000000000000004ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG002(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG002(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000008ull;
}
#else
#define CVMX_PCIERCX_CFG002(block_id) (0x0000000000000008ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG003(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG003(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000000Cull;
}
#else
#define CVMX_PCIERCX_CFG003(block_id) (0x000000000000000Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG004(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG004(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000010ull;
}
#else
#define CVMX_PCIERCX_CFG004(block_id) (0x0000000000000010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG005(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG005(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000014ull;
}
#else
#define CVMX_PCIERCX_CFG005(block_id) (0x0000000000000014ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG006(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG006(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000018ull;
}
#else
#define CVMX_PCIERCX_CFG006(block_id) (0x0000000000000018ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG007(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG007(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000001Cull;
}
#else
#define CVMX_PCIERCX_CFG007(block_id) (0x000000000000001Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG008(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG008(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000020ull;
}
#else
#define CVMX_PCIERCX_CFG008(block_id) (0x0000000000000020ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG009(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG009(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000024ull;
}
#else
#define CVMX_PCIERCX_CFG009(block_id) (0x0000000000000024ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG010(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG010(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000028ull;
}
#else
#define CVMX_PCIERCX_CFG010(block_id) (0x0000000000000028ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG011(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG011(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000002Cull;
}
#else
#define CVMX_PCIERCX_CFG011(block_id) (0x000000000000002Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG012(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG012(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000030ull;
}
#else
#define CVMX_PCIERCX_CFG012(block_id) (0x0000000000000030ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG013(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG013(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000034ull;
}
#else
#define CVMX_PCIERCX_CFG013(block_id) (0x0000000000000034ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG014(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG014(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000038ull;
}
#else
#define CVMX_PCIERCX_CFG014(block_id) (0x0000000000000038ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG015(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG015(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000003Cull;
}
#else
#define CVMX_PCIERCX_CFG015(block_id) (0x000000000000003Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG016(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG016(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000040ull;
}
#else
#define CVMX_PCIERCX_CFG016(block_id) (0x0000000000000040ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG017(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG017(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000044ull;
}
#else
#define CVMX_PCIERCX_CFG017(block_id) (0x0000000000000044ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG020(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG020(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000050ull;
}
#else
#define CVMX_PCIERCX_CFG020(block_id) (0x0000000000000050ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG021(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG021(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000054ull;
}
#else
#define CVMX_PCIERCX_CFG021(block_id) (0x0000000000000054ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG022(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG022(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000058ull;
}
#else
#define CVMX_PCIERCX_CFG022(block_id) (0x0000000000000058ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG023(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG023(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000005Cull;
}
#else
#define CVMX_PCIERCX_CFG023(block_id) (0x000000000000005Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG028(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG028(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000070ull;
}
#else
#define CVMX_PCIERCX_CFG028(block_id) (0x0000000000000070ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG029(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG029(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000074ull;
}
#else
#define CVMX_PCIERCX_CFG029(block_id) (0x0000000000000074ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG030(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG030(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000078ull;
}
#else
#define CVMX_PCIERCX_CFG030(block_id) (0x0000000000000078ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG031(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG031(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000007Cull;
}
#else
#define CVMX_PCIERCX_CFG031(block_id) (0x000000000000007Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG032(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG032(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000080ull;
}
#else
#define CVMX_PCIERCX_CFG032(block_id) (0x0000000000000080ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG033(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG033(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000084ull;
}
#else
#define CVMX_PCIERCX_CFG033(block_id) (0x0000000000000084ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG034(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG034(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000088ull;
}
#else
#define CVMX_PCIERCX_CFG034(block_id) (0x0000000000000088ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG035(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG035(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000008Cull;
}
#else
#define CVMX_PCIERCX_CFG035(block_id) (0x000000000000008Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG036(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG036(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000090ull;
}
#else
#define CVMX_PCIERCX_CFG036(block_id) (0x0000000000000090ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG037(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG037(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000094ull;
}
#else
#define CVMX_PCIERCX_CFG037(block_id) (0x0000000000000094ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG038(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG038(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000098ull;
}
#else
#define CVMX_PCIERCX_CFG038(block_id) (0x0000000000000098ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG039(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG039(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000009Cull;
}
#else
#define CVMX_PCIERCX_CFG039(block_id) (0x000000000000009Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG040(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG040(%lu) is invalid on this chip\n", block_id);
	return 0x00000000000000A0ull;
}
#else
#define CVMX_PCIERCX_CFG040(block_id) (0x00000000000000A0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG041(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG041(%lu) is invalid on this chip\n", block_id);
	return 0x00000000000000A4ull;
}
#else
#define CVMX_PCIERCX_CFG041(block_id) (0x00000000000000A4ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG042(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG042(%lu) is invalid on this chip\n", block_id);
	return 0x00000000000000A8ull;
}
#else
#define CVMX_PCIERCX_CFG042(block_id) (0x00000000000000A8ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG064(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG064(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000100ull;
}
#else
#define CVMX_PCIERCX_CFG064(block_id) (0x0000000000000100ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG065(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG065(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000104ull;
}
#else
#define CVMX_PCIERCX_CFG065(block_id) (0x0000000000000104ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG066(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG066(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000108ull;
}
#else
#define CVMX_PCIERCX_CFG066(block_id) (0x0000000000000108ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG067(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG067(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000010Cull;
}
#else
#define CVMX_PCIERCX_CFG067(block_id) (0x000000000000010Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG068(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG068(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000110ull;
}
#else
#define CVMX_PCIERCX_CFG068(block_id) (0x0000000000000110ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG069(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG069(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000114ull;
}
#else
#define CVMX_PCIERCX_CFG069(block_id) (0x0000000000000114ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG070(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG070(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000118ull;
}
#else
#define CVMX_PCIERCX_CFG070(block_id) (0x0000000000000118ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG071(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG071(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000011Cull;
}
#else
#define CVMX_PCIERCX_CFG071(block_id) (0x000000000000011Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG072(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG072(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000120ull;
}
#else
#define CVMX_PCIERCX_CFG072(block_id) (0x0000000000000120ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG073(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG073(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000124ull;
}
#else
#define CVMX_PCIERCX_CFG073(block_id) (0x0000000000000124ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG074(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG074(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000128ull;
}
#else
#define CVMX_PCIERCX_CFG074(block_id) (0x0000000000000128ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG075(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG075(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000012Cull;
}
#else
#define CVMX_PCIERCX_CFG075(block_id) (0x000000000000012Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG076(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG076(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000130ull;
}
#else
#define CVMX_PCIERCX_CFG076(block_id) (0x0000000000000130ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG077(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG077(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000134ull;
}
#else
#define CVMX_PCIERCX_CFG077(block_id) (0x0000000000000134ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG448(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG448(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000700ull;
}
#else
#define CVMX_PCIERCX_CFG448(block_id) (0x0000000000000700ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG449(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG449(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000704ull;
}
#else
#define CVMX_PCIERCX_CFG449(block_id) (0x0000000000000704ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG450(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG450(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000708ull;
}
#else
#define CVMX_PCIERCX_CFG450(block_id) (0x0000000000000708ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG451(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG451(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000070Cull;
}
#else
#define CVMX_PCIERCX_CFG451(block_id) (0x000000000000070Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG452(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG452(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000710ull;
}
#else
#define CVMX_PCIERCX_CFG452(block_id) (0x0000000000000710ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG453(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG453(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000714ull;
}
#else
#define CVMX_PCIERCX_CFG453(block_id) (0x0000000000000714ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG454(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG454(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000718ull;
}
#else
#define CVMX_PCIERCX_CFG454(block_id) (0x0000000000000718ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG455(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG455(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000071Cull;
}
#else
#define CVMX_PCIERCX_CFG455(block_id) (0x000000000000071Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG456(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG456(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000720ull;
}
#else
#define CVMX_PCIERCX_CFG456(block_id) (0x0000000000000720ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG458(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG458(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000728ull;
}
#else
#define CVMX_PCIERCX_CFG458(block_id) (0x0000000000000728ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG459(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG459(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000072Cull;
}
#else
#define CVMX_PCIERCX_CFG459(block_id) (0x000000000000072Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG460(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG460(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000730ull;
}
#else
#define CVMX_PCIERCX_CFG460(block_id) (0x0000000000000730ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG461(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG461(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000734ull;
}
#else
#define CVMX_PCIERCX_CFG461(block_id) (0x0000000000000734ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG462(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG462(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000738ull;
}
#else
#define CVMX_PCIERCX_CFG462(block_id) (0x0000000000000738ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG463(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG463(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000073Cull;
}
#else
#define CVMX_PCIERCX_CFG463(block_id) (0x000000000000073Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG464(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG464(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000740ull;
}
#else
#define CVMX_PCIERCX_CFG464(block_id) (0x0000000000000740ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG465(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG465(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000744ull;
}
#else
#define CVMX_PCIERCX_CFG465(block_id) (0x0000000000000744ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG466(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG466(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000748ull;
}
#else
#define CVMX_PCIERCX_CFG466(block_id) (0x0000000000000748ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG467(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG467(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000074Cull;
}
#else
#define CVMX_PCIERCX_CFG467(block_id) (0x000000000000074Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG468(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG468(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000750ull;
}
#else
#define CVMX_PCIERCX_CFG468(block_id) (0x0000000000000750ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG490(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG490(%lu) is invalid on this chip\n", block_id);
	return 0x00000000000007A8ull;
}
#else
#define CVMX_PCIERCX_CFG490(block_id) (0x00000000000007A8ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG491(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG491(%lu) is invalid on this chip\n", block_id);
	return 0x00000000000007ACull;
}
#else
#define CVMX_PCIERCX_CFG491(block_id) (0x00000000000007ACull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG492(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG492(%lu) is invalid on this chip\n", block_id);
	return 0x00000000000007B0ull;
}
#else
#define CVMX_PCIERCX_CFG492(block_id) (0x00000000000007B0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG515(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG515(%lu) is invalid on this chip\n", block_id);
	return 0x000000000000080Cull;
}
#else
#define CVMX_PCIERCX_CFG515(block_id) (0x000000000000080Cull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG516(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG516(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000810ull;
}
#else
#define CVMX_PCIERCX_CFG516(block_id) (0x0000000000000810ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCIERCX_CFG517(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PCIERCX_CFG517(%lu) is invalid on this chip\n", block_id);
	return 0x0000000000000814ull;
}
#else
#define CVMX_PCIERCX_CFG517(block_id) (0x0000000000000814ull)
#endif

/**
 * cvmx_pcierc#_cfg000
 *
 * PCIE_CFG000 = First 32-bits of PCIE type 1 config space (Device ID and Vendor ID Register)
 *
 */
union cvmx_pciercx_cfg000 {
	uint32_t u32;
	struct cvmx_pciercx_cfg000_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t devid                        : 16; /**< Device ID, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t vendid                       : 16; /**< Vendor ID, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
#else
	uint32_t vendid                       : 16;
	uint32_t devid                        : 16;
#endif
	} s;
	struct cvmx_pciercx_cfg000_s          cn52xx;
	struct cvmx_pciercx_cfg000_s          cn52xxp1;
	struct cvmx_pciercx_cfg000_s          cn56xx;
	struct cvmx_pciercx_cfg000_s          cn56xxp1;
	struct cvmx_pciercx_cfg000_s          cn61xx;
	struct cvmx_pciercx_cfg000_s          cn63xx;
	struct cvmx_pciercx_cfg000_s          cn63xxp1;
	struct cvmx_pciercx_cfg000_s          cn66xx;
	struct cvmx_pciercx_cfg000_s          cn68xx;
	struct cvmx_pciercx_cfg000_s          cn68xxp1;
	struct cvmx_pciercx_cfg000_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg000 cvmx_pciercx_cfg000_t;

/**
 * cvmx_pcierc#_cfg001
 *
 * PCIE_CFG001 = Second 32-bits of PCIE type 1 config space (Command/Status Register)
 *
 */
union cvmx_pciercx_cfg001 {
	uint32_t u32;
	struct cvmx_pciercx_cfg001_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dpe                          : 1;  /**< Detected Parity Error */
	uint32_t sse                          : 1;  /**< Signaled System Error */
	uint32_t rma                          : 1;  /**< Received Master Abort */
	uint32_t rta                          : 1;  /**< Received Target Abort */
	uint32_t sta                          : 1;  /**< Signaled Target Abort */
	uint32_t devt                         : 2;  /**< DEVSEL Timing
                                                         Not applicable for PCI Express. Hardwired to 0. */
	uint32_t mdpe                         : 1;  /**< Master Data Parity Error */
	uint32_t fbb                          : 1;  /**< Fast Back-to-Back Capable
                                                         Not applicable for PCI Express. Hardwired to 0. */
	uint32_t reserved_22_22               : 1;
	uint32_t m66                          : 1;  /**< 66 MHz Capable
                                                         Not applicable for PCI Express. Hardwired to 0. */
	uint32_t cl                           : 1;  /**< Capabilities List
                                                         Indicates presence of an extended capability item.
                                                         Hardwired to 1. */
	uint32_t i_stat                       : 1;  /**< INTx Status */
	uint32_t reserved_11_18               : 8;
	uint32_t i_dis                        : 1;  /**< INTx Assertion Disable */
	uint32_t fbbe                         : 1;  /**< Fast Back-to-Back Enable
                                                         Not applicable for PCI Express. Must be hardwired to 0. */
	uint32_t see                          : 1;  /**< SERR# Enable */
	uint32_t ids_wcc                      : 1;  /**< IDSEL Stepping/Wait Cycle Control
                                                         Not applicable for PCI Express. Must be hardwired to 0 */
	uint32_t per                          : 1;  /**< Parity Error Response */
	uint32_t vps                          : 1;  /**< VGA Palette Snoop
                                                         Not applicable for PCI Express. Must be hardwired to 0. */
	uint32_t mwice                        : 1;  /**< Memory Write and Invalidate
                                                         Not applicable for PCI Express. Must be hardwired to 0. */
	uint32_t scse                         : 1;  /**< Special Cycle Enable
                                                         Not applicable for PCI Express. Must be hardwired to 0. */
	uint32_t me                           : 1;  /**< Bus Master Enable */
	uint32_t msae                         : 1;  /**< Memory Space Enable */
	uint32_t isae                         : 1;  /**< I/O Space Enable */
#else
	uint32_t isae                         : 1;
	uint32_t msae                         : 1;
	uint32_t me                           : 1;
	uint32_t scse                         : 1;
	uint32_t mwice                        : 1;
	uint32_t vps                          : 1;
	uint32_t per                          : 1;
	uint32_t ids_wcc                      : 1;
	uint32_t see                          : 1;
	uint32_t fbbe                         : 1;
	uint32_t i_dis                        : 1;
	uint32_t reserved_11_18               : 8;
	uint32_t i_stat                       : 1;
	uint32_t cl                           : 1;
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
	struct cvmx_pciercx_cfg001_s          cn52xx;
	struct cvmx_pciercx_cfg001_s          cn52xxp1;
	struct cvmx_pciercx_cfg001_s          cn56xx;
	struct cvmx_pciercx_cfg001_s          cn56xxp1;
	struct cvmx_pciercx_cfg001_s          cn61xx;
	struct cvmx_pciercx_cfg001_s          cn63xx;
	struct cvmx_pciercx_cfg001_s          cn63xxp1;
	struct cvmx_pciercx_cfg001_s          cn66xx;
	struct cvmx_pciercx_cfg001_s          cn68xx;
	struct cvmx_pciercx_cfg001_s          cn68xxp1;
	struct cvmx_pciercx_cfg001_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg001 cvmx_pciercx_cfg001_t;

/**
 * cvmx_pcierc#_cfg002
 *
 * PCIE_CFG002 = Third 32-bits of PCIE type 1 config space (Revision ID/Class Code Register)
 *
 */
union cvmx_pciercx_cfg002 {
	uint32_t u32;
	struct cvmx_pciercx_cfg002_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bcc                          : 8;  /**< Base Class Code, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t sc                           : 8;  /**< Subclass Code, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t pi                           : 8;  /**< Programming Interface, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t rid                          : 8;  /**< Revision ID, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
#else
	uint32_t rid                          : 8;
	uint32_t pi                           : 8;
	uint32_t sc                           : 8;
	uint32_t bcc                          : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg002_s          cn52xx;
	struct cvmx_pciercx_cfg002_s          cn52xxp1;
	struct cvmx_pciercx_cfg002_s          cn56xx;
	struct cvmx_pciercx_cfg002_s          cn56xxp1;
	struct cvmx_pciercx_cfg002_s          cn61xx;
	struct cvmx_pciercx_cfg002_s          cn63xx;
	struct cvmx_pciercx_cfg002_s          cn63xxp1;
	struct cvmx_pciercx_cfg002_s          cn66xx;
	struct cvmx_pciercx_cfg002_s          cn68xx;
	struct cvmx_pciercx_cfg002_s          cn68xxp1;
	struct cvmx_pciercx_cfg002_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg002 cvmx_pciercx_cfg002_t;

/**
 * cvmx_pcierc#_cfg003
 *
 * PCIE_CFG003 = Fourth 32-bits of PCIE type 1 config space (Cache Line Size/Master Latency Timer/Header Type Register/BIST Register)
 *
 */
union cvmx_pciercx_cfg003 {
	uint32_t u32;
	struct cvmx_pciercx_cfg003_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bist                         : 8;  /**< The BIST register functions are not supported.
                                                         All 8 bits of the BIST register are hardwired to 0. */
	uint32_t mfd                          : 1;  /**< Multi Function Device
                                                         The Multi Function Device bit is writable through PEM(0..1)_CFG_WR.
                                                         However, this is a single function device. Therefore, the
                                                         application must not write a 1 to this bit. */
	uint32_t chf                          : 7;  /**< Configuration Header Format
                                                         Hardwired to 1. */
	uint32_t lt                           : 8;  /**< Master Latency Timer
                                                         Not applicable for PCI Express, hardwired to 0. */
	uint32_t cls                          : 8;  /**< Cache Line Size
                                                         The Cache Line Size register is RW for legacy compatibility
                                                         purposes and is not applicable to PCI Express device
                                                         functionality. */
#else
	uint32_t cls                          : 8;
	uint32_t lt                           : 8;
	uint32_t chf                          : 7;
	uint32_t mfd                          : 1;
	uint32_t bist                         : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg003_s          cn52xx;
	struct cvmx_pciercx_cfg003_s          cn52xxp1;
	struct cvmx_pciercx_cfg003_s          cn56xx;
	struct cvmx_pciercx_cfg003_s          cn56xxp1;
	struct cvmx_pciercx_cfg003_s          cn61xx;
	struct cvmx_pciercx_cfg003_s          cn63xx;
	struct cvmx_pciercx_cfg003_s          cn63xxp1;
	struct cvmx_pciercx_cfg003_s          cn66xx;
	struct cvmx_pciercx_cfg003_s          cn68xx;
	struct cvmx_pciercx_cfg003_s          cn68xxp1;
	struct cvmx_pciercx_cfg003_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg003 cvmx_pciercx_cfg003_t;

/**
 * cvmx_pcierc#_cfg004
 *
 * PCIE_CFG004 = Fifth 32-bits of PCIE type 1 config space (Base Address Register 0 - Low)
 *
 */
union cvmx_pciercx_cfg004 {
	uint32_t u32;
	struct cvmx_pciercx_cfg004_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg004_s          cn52xx;
	struct cvmx_pciercx_cfg004_s          cn52xxp1;
	struct cvmx_pciercx_cfg004_s          cn56xx;
	struct cvmx_pciercx_cfg004_s          cn56xxp1;
	struct cvmx_pciercx_cfg004_s          cn61xx;
	struct cvmx_pciercx_cfg004_s          cn63xx;
	struct cvmx_pciercx_cfg004_s          cn63xxp1;
	struct cvmx_pciercx_cfg004_s          cn66xx;
	struct cvmx_pciercx_cfg004_s          cn68xx;
	struct cvmx_pciercx_cfg004_s          cn68xxp1;
	struct cvmx_pciercx_cfg004_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg004 cvmx_pciercx_cfg004_t;

/**
 * cvmx_pcierc#_cfg005
 *
 * PCIE_CFG005 = Sixth 32-bits of PCIE type 1 config space (Base Address Register 0 - High)
 *
 */
union cvmx_pciercx_cfg005 {
	uint32_t u32;
	struct cvmx_pciercx_cfg005_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg005_s          cn52xx;
	struct cvmx_pciercx_cfg005_s          cn52xxp1;
	struct cvmx_pciercx_cfg005_s          cn56xx;
	struct cvmx_pciercx_cfg005_s          cn56xxp1;
	struct cvmx_pciercx_cfg005_s          cn61xx;
	struct cvmx_pciercx_cfg005_s          cn63xx;
	struct cvmx_pciercx_cfg005_s          cn63xxp1;
	struct cvmx_pciercx_cfg005_s          cn66xx;
	struct cvmx_pciercx_cfg005_s          cn68xx;
	struct cvmx_pciercx_cfg005_s          cn68xxp1;
	struct cvmx_pciercx_cfg005_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg005 cvmx_pciercx_cfg005_t;

/**
 * cvmx_pcierc#_cfg006
 *
 * PCIE_CFG006 = Seventh 32-bits of PCIE type 1 config space (Bus Number Registers)
 *
 */
union cvmx_pciercx_cfg006 {
	uint32_t u32;
	struct cvmx_pciercx_cfg006_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t slt                          : 8;  /**< Secondary Latency Timer
                                                         Not applicable to PCI Express, hardwired to 0x00. */
	uint32_t subbnum                      : 8;  /**< Subordinate Bus Number */
	uint32_t sbnum                        : 8;  /**< Secondary Bus Number */
	uint32_t pbnum                        : 8;  /**< Primary Bus Number */
#else
	uint32_t pbnum                        : 8;
	uint32_t sbnum                        : 8;
	uint32_t subbnum                      : 8;
	uint32_t slt                          : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg006_s          cn52xx;
	struct cvmx_pciercx_cfg006_s          cn52xxp1;
	struct cvmx_pciercx_cfg006_s          cn56xx;
	struct cvmx_pciercx_cfg006_s          cn56xxp1;
	struct cvmx_pciercx_cfg006_s          cn61xx;
	struct cvmx_pciercx_cfg006_s          cn63xx;
	struct cvmx_pciercx_cfg006_s          cn63xxp1;
	struct cvmx_pciercx_cfg006_s          cn66xx;
	struct cvmx_pciercx_cfg006_s          cn68xx;
	struct cvmx_pciercx_cfg006_s          cn68xxp1;
	struct cvmx_pciercx_cfg006_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg006 cvmx_pciercx_cfg006_t;

/**
 * cvmx_pcierc#_cfg007
 *
 * PCIE_CFG007 = Eighth 32-bits of PCIE type 1 config space (IO Base and IO Limit/Secondary Status Register)
 *
 */
union cvmx_pciercx_cfg007 {
	uint32_t u32;
	struct cvmx_pciercx_cfg007_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dpe                          : 1;  /**< Detected Parity Error */
	uint32_t sse                          : 1;  /**< Signaled System Error */
	uint32_t rma                          : 1;  /**< Received Master Abort */
	uint32_t rta                          : 1;  /**< Received Target Abort */
	uint32_t sta                          : 1;  /**< Signaled Target Abort */
	uint32_t devt                         : 2;  /**< DEVSEL Timing
                                                         Not applicable for PCI Express. Hardwired to 0. */
	uint32_t mdpe                         : 1;  /**< Master Data Parity Error */
	uint32_t fbb                          : 1;  /**< Fast Back-to-Back Capable
                                                         Not applicable for PCI Express. Hardwired to 0. */
	uint32_t reserved_22_22               : 1;
	uint32_t m66                          : 1;  /**< 66 MHz Capable
                                                         Not applicable for PCI Express. Hardwired to 0. */
	uint32_t reserved_16_20               : 5;
	uint32_t lio_limi                     : 4;  /**< I/O Space Limit */
	uint32_t reserved_9_11                : 3;
	uint32_t io32b                        : 1;  /**< 32-Bit I/O Space */
	uint32_t lio_base                     : 4;  /**< I/O Space Base */
	uint32_t reserved_1_3                 : 3;
	uint32_t io32a                        : 1;  /**< 32-Bit I/O Space
                                                         o 0 = 16-bit I/O addressing
                                                         o 1 = 32-bit I/O addressing
                                                         This bit is writable through PEM(0..1)_CFG_WR.
                                                         When the application
                                                         writes to this bit through PEM(0..1)_CFG_WR,
                                                         the same value is written
                                                         to bit 8 of this register. */
#else
	uint32_t io32a                        : 1;
	uint32_t reserved_1_3                 : 3;
	uint32_t lio_base                     : 4;
	uint32_t io32b                        : 1;
	uint32_t reserved_9_11                : 3;
	uint32_t lio_limi                     : 4;
	uint32_t reserved_16_20               : 5;
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
	struct cvmx_pciercx_cfg007_s          cn52xx;
	struct cvmx_pciercx_cfg007_s          cn52xxp1;
	struct cvmx_pciercx_cfg007_s          cn56xx;
	struct cvmx_pciercx_cfg007_s          cn56xxp1;
	struct cvmx_pciercx_cfg007_s          cn61xx;
	struct cvmx_pciercx_cfg007_s          cn63xx;
	struct cvmx_pciercx_cfg007_s          cn63xxp1;
	struct cvmx_pciercx_cfg007_s          cn66xx;
	struct cvmx_pciercx_cfg007_s          cn68xx;
	struct cvmx_pciercx_cfg007_s          cn68xxp1;
	struct cvmx_pciercx_cfg007_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg007 cvmx_pciercx_cfg007_t;

/**
 * cvmx_pcierc#_cfg008
 *
 * PCIE_CFG008 = Ninth 32-bits of PCIE type 1 config space (Memory Base and Memory Limit Register)
 *
 */
union cvmx_pciercx_cfg008 {
	uint32_t u32;
	struct cvmx_pciercx_cfg008_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ml_addr                      : 12; /**< Memory Limit Address */
	uint32_t reserved_16_19               : 4;
	uint32_t mb_addr                      : 12; /**< Memory Base Address */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t mb_addr                      : 12;
	uint32_t reserved_16_19               : 4;
	uint32_t ml_addr                      : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg008_s          cn52xx;
	struct cvmx_pciercx_cfg008_s          cn52xxp1;
	struct cvmx_pciercx_cfg008_s          cn56xx;
	struct cvmx_pciercx_cfg008_s          cn56xxp1;
	struct cvmx_pciercx_cfg008_s          cn61xx;
	struct cvmx_pciercx_cfg008_s          cn63xx;
	struct cvmx_pciercx_cfg008_s          cn63xxp1;
	struct cvmx_pciercx_cfg008_s          cn66xx;
	struct cvmx_pciercx_cfg008_s          cn68xx;
	struct cvmx_pciercx_cfg008_s          cn68xxp1;
	struct cvmx_pciercx_cfg008_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg008 cvmx_pciercx_cfg008_t;

/**
 * cvmx_pcierc#_cfg009
 *
 * PCIE_CFG009 = Tenth 32-bits of PCIE type 1 config space (Prefetchable Memory Base and Limit Register)
 *
 */
union cvmx_pciercx_cfg009 {
	uint32_t u32;
	struct cvmx_pciercx_cfg009_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lmem_limit                   : 12; /**< Upper 12 bits of 32-bit Prefetchable Memory End Address */
	uint32_t reserved_17_19               : 3;
	uint32_t mem64b                       : 1;  /**< 64-Bit Memory Addressing
                                                         o 0 = 32-bit memory addressing
                                                         o 1 = 64-bit memory addressing */
	uint32_t lmem_base                    : 12; /**< Upper 12 bits of 32-bit Prefetchable Memory Start Address */
	uint32_t reserved_1_3                 : 3;
	uint32_t mem64a                       : 1;  /**< 64-Bit Memory Addressing
                                                         o 0 = 32-bit memory addressing
                                                         o 1 = 64-bit memory addressing
                                                         This bit is writable through PEM(0..1)_CFG_WR.
                                                         When the application
                                                         writes to this bit through PEM(0..1)_CFG_WR,
                                                         the same value is written
                                                         to bit 16 of this register. */
#else
	uint32_t mem64a                       : 1;
	uint32_t reserved_1_3                 : 3;
	uint32_t lmem_base                    : 12;
	uint32_t mem64b                       : 1;
	uint32_t reserved_17_19               : 3;
	uint32_t lmem_limit                   : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg009_s          cn52xx;
	struct cvmx_pciercx_cfg009_s          cn52xxp1;
	struct cvmx_pciercx_cfg009_s          cn56xx;
	struct cvmx_pciercx_cfg009_s          cn56xxp1;
	struct cvmx_pciercx_cfg009_s          cn61xx;
	struct cvmx_pciercx_cfg009_s          cn63xx;
	struct cvmx_pciercx_cfg009_s          cn63xxp1;
	struct cvmx_pciercx_cfg009_s          cn66xx;
	struct cvmx_pciercx_cfg009_s          cn68xx;
	struct cvmx_pciercx_cfg009_s          cn68xxp1;
	struct cvmx_pciercx_cfg009_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg009 cvmx_pciercx_cfg009_t;

/**
 * cvmx_pcierc#_cfg010
 *
 * PCIE_CFG010 = Eleventh 32-bits of PCIE type 1 config space (Prefetchable Base Upper 32 Bits Register)
 *
 */
union cvmx_pciercx_cfg010 {
	uint32_t u32;
	struct cvmx_pciercx_cfg010_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t umem_base                    : 32; /**< Upper 32 Bits of Base Address of Prefetchable Memory Space
                                                         Used only when 64-bit prefetchable memory addressing is
                                                         enabled. */
#else
	uint32_t umem_base                    : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg010_s          cn52xx;
	struct cvmx_pciercx_cfg010_s          cn52xxp1;
	struct cvmx_pciercx_cfg010_s          cn56xx;
	struct cvmx_pciercx_cfg010_s          cn56xxp1;
	struct cvmx_pciercx_cfg010_s          cn61xx;
	struct cvmx_pciercx_cfg010_s          cn63xx;
	struct cvmx_pciercx_cfg010_s          cn63xxp1;
	struct cvmx_pciercx_cfg010_s          cn66xx;
	struct cvmx_pciercx_cfg010_s          cn68xx;
	struct cvmx_pciercx_cfg010_s          cn68xxp1;
	struct cvmx_pciercx_cfg010_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg010 cvmx_pciercx_cfg010_t;

/**
 * cvmx_pcierc#_cfg011
 *
 * PCIE_CFG011 = Twelfth 32-bits of PCIE type 1 config space (Prefetchable Limit Upper 32 Bits Register)
 *
 */
union cvmx_pciercx_cfg011 {
	uint32_t u32;
	struct cvmx_pciercx_cfg011_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t umem_limit                   : 32; /**< Upper 32 Bits of Limit Address of Prefetchable Memory Space
                                                         Used only when 64-bit prefetchable memory addressing is
                                                         enabled. */
#else
	uint32_t umem_limit                   : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg011_s          cn52xx;
	struct cvmx_pciercx_cfg011_s          cn52xxp1;
	struct cvmx_pciercx_cfg011_s          cn56xx;
	struct cvmx_pciercx_cfg011_s          cn56xxp1;
	struct cvmx_pciercx_cfg011_s          cn61xx;
	struct cvmx_pciercx_cfg011_s          cn63xx;
	struct cvmx_pciercx_cfg011_s          cn63xxp1;
	struct cvmx_pciercx_cfg011_s          cn66xx;
	struct cvmx_pciercx_cfg011_s          cn68xx;
	struct cvmx_pciercx_cfg011_s          cn68xxp1;
	struct cvmx_pciercx_cfg011_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg011 cvmx_pciercx_cfg011_t;

/**
 * cvmx_pcierc#_cfg012
 *
 * PCIE_CFG012 = Thirteenth 32-bits of PCIE type 1 config space (IO Base and Limit Upper 16 Bits Register)
 *
 */
union cvmx_pciercx_cfg012 {
	uint32_t u32;
	struct cvmx_pciercx_cfg012_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t uio_limit                    : 16; /**< Upper 16 Bits of I/O Limit (if 32-bit I/O decoding is supported
                                                         for devices on the secondary side) */
	uint32_t uio_base                     : 16; /**< Upper 16 Bits of I/O Base (if 32-bit I/O decoding is supported
                                                         for devices on the secondary side) */
#else
	uint32_t uio_base                     : 16;
	uint32_t uio_limit                    : 16;
#endif
	} s;
	struct cvmx_pciercx_cfg012_s          cn52xx;
	struct cvmx_pciercx_cfg012_s          cn52xxp1;
	struct cvmx_pciercx_cfg012_s          cn56xx;
	struct cvmx_pciercx_cfg012_s          cn56xxp1;
	struct cvmx_pciercx_cfg012_s          cn61xx;
	struct cvmx_pciercx_cfg012_s          cn63xx;
	struct cvmx_pciercx_cfg012_s          cn63xxp1;
	struct cvmx_pciercx_cfg012_s          cn66xx;
	struct cvmx_pciercx_cfg012_s          cn68xx;
	struct cvmx_pciercx_cfg012_s          cn68xxp1;
	struct cvmx_pciercx_cfg012_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg012 cvmx_pciercx_cfg012_t;

/**
 * cvmx_pcierc#_cfg013
 *
 * PCIE_CFG013 = Fourteenth 32-bits of PCIE type 1 config space (Capability Pointer Register)
 *
 */
union cvmx_pciercx_cfg013 {
	uint32_t u32;
	struct cvmx_pciercx_cfg013_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t cp                           : 8;  /**< First Capability Pointer.
                                                         Points to Power Management Capability structure by
                                                         default, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
#else
	uint32_t cp                           : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_pciercx_cfg013_s          cn52xx;
	struct cvmx_pciercx_cfg013_s          cn52xxp1;
	struct cvmx_pciercx_cfg013_s          cn56xx;
	struct cvmx_pciercx_cfg013_s          cn56xxp1;
	struct cvmx_pciercx_cfg013_s          cn61xx;
	struct cvmx_pciercx_cfg013_s          cn63xx;
	struct cvmx_pciercx_cfg013_s          cn63xxp1;
	struct cvmx_pciercx_cfg013_s          cn66xx;
	struct cvmx_pciercx_cfg013_s          cn68xx;
	struct cvmx_pciercx_cfg013_s          cn68xxp1;
	struct cvmx_pciercx_cfg013_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg013 cvmx_pciercx_cfg013_t;

/**
 * cvmx_pcierc#_cfg014
 *
 * PCIE_CFG014 = Fifteenth 32-bits of PCIE type 1 config space (Expansion ROM Base Address Register)
 *
 */
union cvmx_pciercx_cfg014 {
	uint32_t u32;
	struct cvmx_pciercx_cfg014_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg014_s          cn52xx;
	struct cvmx_pciercx_cfg014_s          cn52xxp1;
	struct cvmx_pciercx_cfg014_s          cn56xx;
	struct cvmx_pciercx_cfg014_s          cn56xxp1;
	struct cvmx_pciercx_cfg014_s          cn61xx;
	struct cvmx_pciercx_cfg014_s          cn63xx;
	struct cvmx_pciercx_cfg014_s          cn63xxp1;
	struct cvmx_pciercx_cfg014_s          cn66xx;
	struct cvmx_pciercx_cfg014_s          cn68xx;
	struct cvmx_pciercx_cfg014_s          cn68xxp1;
	struct cvmx_pciercx_cfg014_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg014 cvmx_pciercx_cfg014_t;

/**
 * cvmx_pcierc#_cfg015
 *
 * PCIE_CFG015 = Sixteenth 32-bits of PCIE type 1 config space (Interrupt Line Register/Interrupt Pin/Bridge Control Register)
 *
 */
union cvmx_pciercx_cfg015 {
	uint32_t u32;
	struct cvmx_pciercx_cfg015_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_28_31               : 4;
	uint32_t dtsees                       : 1;  /**< Discard Timer SERR Enable Status
                                                         Not applicable to PCI Express, hardwired to 0. */
	uint32_t dts                          : 1;  /**< Discard Timer Status
                                                         Not applicable to PCI Express, hardwired to 0. */
	uint32_t sdt                          : 1;  /**< Secondary Discard Timer
                                                         Not applicable to PCI Express, hardwired to 0. */
	uint32_t pdt                          : 1;  /**< Primary Discard Timer
                                                         Not applicable to PCI Express, hardwired to 0. */
	uint32_t fbbe                         : 1;  /**< Fast Back-to-Back Transactions Enable
                                                         Not applicable to PCI Express, hardwired to 0. */
	uint32_t sbrst                        : 1;  /**< Secondary Bus Reset
                                                         Hot reset. Causes TS1s with the hot reset bit to be sent to
                                                         the link partner. When set, SW should wait 2ms before
                                                         clearing. The link partner normally responds by sending TS1s
                                                         with the hot reset bit set, which will cause a link
                                                         down event - refer to "PCIe Link-Down Reset in RC Mode"
                                                         section. */
	uint32_t mam                          : 1;  /**< Master Abort Mode
                                                         Not applicable to PCI Express, hardwired to 0. */
	uint32_t vga16d                       : 1;  /**< VGA 16-Bit Decode */
	uint32_t vgae                         : 1;  /**< VGA Enable */
	uint32_t isae                         : 1;  /**< ISA Enable */
	uint32_t see                          : 1;  /**< SERR Enable */
	uint32_t pere                         : 1;  /**< Parity Error Response Enable */
	uint32_t inta                         : 8;  /**< Interrupt Pin
                                                         Identifies the legacy interrupt Message that the device
                                                         (or device function) uses.
                                                         The Interrupt Pin register is writable through PEM(0..1)_CFG_WR.
                                                         In a single-function configuration, only INTA is used.
                                                         Therefore, the application must not change this field. */
	uint32_t il                           : 8;  /**< Interrupt Line */
#else
	uint32_t il                           : 8;
	uint32_t inta                         : 8;
	uint32_t pere                         : 1;
	uint32_t see                          : 1;
	uint32_t isae                         : 1;
	uint32_t vgae                         : 1;
	uint32_t vga16d                       : 1;
	uint32_t mam                          : 1;
	uint32_t sbrst                        : 1;
	uint32_t fbbe                         : 1;
	uint32_t pdt                          : 1;
	uint32_t sdt                          : 1;
	uint32_t dts                          : 1;
	uint32_t dtsees                       : 1;
	uint32_t reserved_28_31               : 4;
#endif
	} s;
	struct cvmx_pciercx_cfg015_s          cn52xx;
	struct cvmx_pciercx_cfg015_s          cn52xxp1;
	struct cvmx_pciercx_cfg015_s          cn56xx;
	struct cvmx_pciercx_cfg015_s          cn56xxp1;
	struct cvmx_pciercx_cfg015_s          cn61xx;
	struct cvmx_pciercx_cfg015_s          cn63xx;
	struct cvmx_pciercx_cfg015_s          cn63xxp1;
	struct cvmx_pciercx_cfg015_s          cn66xx;
	struct cvmx_pciercx_cfg015_s          cn68xx;
	struct cvmx_pciercx_cfg015_s          cn68xxp1;
	struct cvmx_pciercx_cfg015_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg015 cvmx_pciercx_cfg015_t;

/**
 * cvmx_pcierc#_cfg016
 *
 * PCIE_CFG016 = Seventeenth 32-bits of PCIE type 1 config space
 * (Power Management Capability ID/
 * Power Management Next Item Pointer/
 * Power Management Capabilities Register)
 */
union cvmx_pciercx_cfg016 {
	uint32_t u32;
	struct cvmx_pciercx_cfg016_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pmes                         : 5;  /**< PME_Support
                                                         A value of 0 for any bit indicates that the
                                                         device (or function) is not capable of generating PME Messages
                                                         while in that power state:
                                                         o Bit 11: If set, PME Messages can be generated from D0
                                                         o Bit 12: If set, PME Messages can be generated from D1
                                                         o Bit 13: If set, PME Messages can be generated from D2
                                                         o Bit 14: If set, PME Messages can be generated from D3hot
                                                         o Bit 15: If set, PME Messages can be generated from D3cold
                                                         The PME_Support field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t d2s                          : 1;  /**< D2 Support, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t d1s                          : 1;  /**< D1 Support, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t auxc                         : 3;  /**< AUX Current, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t dsi                          : 1;  /**< Device Specific Initialization (DSI), writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t reserved_20_20               : 1;
	uint32_t pme_clock                    : 1;  /**< PME Clock, hardwired to 0 */
	uint32_t pmsv                         : 3;  /**< Power Management Specification Version, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer
                                                         Points to the MSI capabilities by default, writable
                                                         through PEM(0..1)_CFG_WR. */
	uint32_t pmcid                        : 8;  /**< Power Management Capability ID */
#else
	uint32_t pmcid                        : 8;
	uint32_t ncp                          : 8;
	uint32_t pmsv                         : 3;
	uint32_t pme_clock                    : 1;
	uint32_t reserved_20_20               : 1;
	uint32_t dsi                          : 1;
	uint32_t auxc                         : 3;
	uint32_t d1s                          : 1;
	uint32_t d2s                          : 1;
	uint32_t pmes                         : 5;
#endif
	} s;
	struct cvmx_pciercx_cfg016_s          cn52xx;
	struct cvmx_pciercx_cfg016_s          cn52xxp1;
	struct cvmx_pciercx_cfg016_s          cn56xx;
	struct cvmx_pciercx_cfg016_s          cn56xxp1;
	struct cvmx_pciercx_cfg016_s          cn61xx;
	struct cvmx_pciercx_cfg016_s          cn63xx;
	struct cvmx_pciercx_cfg016_s          cn63xxp1;
	struct cvmx_pciercx_cfg016_s          cn66xx;
	struct cvmx_pciercx_cfg016_s          cn68xx;
	struct cvmx_pciercx_cfg016_s          cn68xxp1;
	struct cvmx_pciercx_cfg016_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg016 cvmx_pciercx_cfg016_t;

/**
 * cvmx_pcierc#_cfg017
 *
 * PCIE_CFG017 = Eighteenth 32-bits of PCIE type 1 config space (Power Management Control and Status Register)
 *
 */
union cvmx_pciercx_cfg017 {
	uint32_t u32;
	struct cvmx_pciercx_cfg017_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pmdia                        : 8;  /**< Data register for additional information (not supported) */
	uint32_t bpccee                       : 1;  /**< Bus Power/Clock Control Enable, hardwired to 0 */
	uint32_t bd3h                         : 1;  /**< B2/B3 Support, hardwired to 0 */
	uint32_t reserved_16_21               : 6;
	uint32_t pmess                        : 1;  /**< PME Status
                                                         Indicates if a previously enabled PME event occurred or not. */
	uint32_t pmedsia                      : 2;  /**< Data Scale (not supported) */
	uint32_t pmds                         : 4;  /**< Data Select (not supported) */
	uint32_t pmeens                       : 1;  /**< PME Enable
                                                         A value of 1 indicates that the device is enabled to
                                                         generate PME. */
	uint32_t reserved_4_7                 : 4;
	uint32_t nsr                          : 1;  /**< No Soft Reset, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t reserved_2_2                 : 1;
	uint32_t ps                           : 2;  /**< Power State
                                                         Controls the device power state:
                                                           o 00b: D0
                                                           o 01b: D1
                                                           o 10b: D2
                                                           o 11b: D3
                                                         The written value is ignored if the specific state is
                                                         not supported. */
#else
	uint32_t ps                           : 2;
	uint32_t reserved_2_2                 : 1;
	uint32_t nsr                          : 1;
	uint32_t reserved_4_7                 : 4;
	uint32_t pmeens                       : 1;
	uint32_t pmds                         : 4;
	uint32_t pmedsia                      : 2;
	uint32_t pmess                        : 1;
	uint32_t reserved_16_21               : 6;
	uint32_t bd3h                         : 1;
	uint32_t bpccee                       : 1;
	uint32_t pmdia                        : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg017_s          cn52xx;
	struct cvmx_pciercx_cfg017_s          cn52xxp1;
	struct cvmx_pciercx_cfg017_s          cn56xx;
	struct cvmx_pciercx_cfg017_s          cn56xxp1;
	struct cvmx_pciercx_cfg017_s          cn61xx;
	struct cvmx_pciercx_cfg017_s          cn63xx;
	struct cvmx_pciercx_cfg017_s          cn63xxp1;
	struct cvmx_pciercx_cfg017_s          cn66xx;
	struct cvmx_pciercx_cfg017_s          cn68xx;
	struct cvmx_pciercx_cfg017_s          cn68xxp1;
	struct cvmx_pciercx_cfg017_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg017 cvmx_pciercx_cfg017_t;

/**
 * cvmx_pcierc#_cfg020
 *
 * PCIE_CFG020 = Twenty-first 32-bits of PCIE type 1 config space
 * (MSI Capability ID/
 *  MSI Next Item Pointer/
 *  MSI Control Register)
 */
union cvmx_pciercx_cfg020 {
	uint32_t u32;
	struct cvmx_pciercx_cfg020_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t pvm                          : 1;  /**< Per-vector masking capable */
	uint32_t m64                          : 1;  /**< 64-bit Address Capable, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t mme                          : 3;  /**< Multiple Message Enabled
                                                         Indicates that multiple Message mode is enabled by system
                                                         software. The number of Messages enabled must be less than
                                                         or equal to the Multiple Message Capable value. */
	uint32_t mmc                          : 3;  /**< Multiple Message Capable, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t msien                        : 1;  /**< MSI Enabled
                                                         When set, INTx must be disabled.
                                                         This bit must never be set, as internal-MSI is not supported in
                                                         RC mode. (Note that this has no effect on external MSI, which
                                                         will be commonly used in RC mode.) */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer
                                                         Points to PCI Express Capabilities by default,
                                                         writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t msicid                       : 8;  /**< MSI Capability ID */
#else
	uint32_t msicid                       : 8;
	uint32_t ncp                          : 8;
	uint32_t msien                        : 1;
	uint32_t mmc                          : 3;
	uint32_t mme                          : 3;
	uint32_t m64                          : 1;
	uint32_t pvm                          : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_pciercx_cfg020_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t m64                          : 1;  /**< 64-bit Address Capable, writable through PESC(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t mme                          : 3;  /**< Multiple Message Enabled
                                                         Indicates that multiple Message mode is enabled by system
                                                         software. The number of Messages enabled must be less than
                                                         or equal to the Multiple Message Capable value. */
	uint32_t mmc                          : 3;  /**< Multiple Message Capable, writable through PESC(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t msien                        : 1;  /**< MSI Enabled
                                                         When set, INTx must be disabled.
                                                         This bit must never be set, as internal-MSI is not supported in
                                                         RC mode. (Note that this has no effect on external MSI, which
                                                         will be commonly used in RC mode.) */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer
                                                         Points to PCI Express Capabilities by default,
                                                         writable through PESC(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
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
	} cn52xx;
	struct cvmx_pciercx_cfg020_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg020_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg020_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg020_s          cn61xx;
	struct cvmx_pciercx_cfg020_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg020_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg020_cn52xx     cn66xx;
	struct cvmx_pciercx_cfg020_cn52xx     cn68xx;
	struct cvmx_pciercx_cfg020_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg020_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg020 cvmx_pciercx_cfg020_t;

/**
 * cvmx_pcierc#_cfg021
 *
 * PCIE_CFG021 = Twenty-second 32-bits of PCIE type 1 config space (MSI Lower 32 Bits Address Register)
 *
 */
union cvmx_pciercx_cfg021 {
	uint32_t u32;
	struct cvmx_pciercx_cfg021_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lmsi                         : 30; /**< Lower 32-bit Address */
	uint32_t reserved_0_1                 : 2;
#else
	uint32_t reserved_0_1                 : 2;
	uint32_t lmsi                         : 30;
#endif
	} s;
	struct cvmx_pciercx_cfg021_s          cn52xx;
	struct cvmx_pciercx_cfg021_s          cn52xxp1;
	struct cvmx_pciercx_cfg021_s          cn56xx;
	struct cvmx_pciercx_cfg021_s          cn56xxp1;
	struct cvmx_pciercx_cfg021_s          cn61xx;
	struct cvmx_pciercx_cfg021_s          cn63xx;
	struct cvmx_pciercx_cfg021_s          cn63xxp1;
	struct cvmx_pciercx_cfg021_s          cn66xx;
	struct cvmx_pciercx_cfg021_s          cn68xx;
	struct cvmx_pciercx_cfg021_s          cn68xxp1;
	struct cvmx_pciercx_cfg021_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg021 cvmx_pciercx_cfg021_t;

/**
 * cvmx_pcierc#_cfg022
 *
 * PCIE_CFG022 = Twenty-third 32-bits of PCIE type 1 config space (MSI Upper 32 bits Address Register)
 *
 */
union cvmx_pciercx_cfg022 {
	uint32_t u32;
	struct cvmx_pciercx_cfg022_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t umsi                         : 32; /**< Upper 32-bit Address */
#else
	uint32_t umsi                         : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg022_s          cn52xx;
	struct cvmx_pciercx_cfg022_s          cn52xxp1;
	struct cvmx_pciercx_cfg022_s          cn56xx;
	struct cvmx_pciercx_cfg022_s          cn56xxp1;
	struct cvmx_pciercx_cfg022_s          cn61xx;
	struct cvmx_pciercx_cfg022_s          cn63xx;
	struct cvmx_pciercx_cfg022_s          cn63xxp1;
	struct cvmx_pciercx_cfg022_s          cn66xx;
	struct cvmx_pciercx_cfg022_s          cn68xx;
	struct cvmx_pciercx_cfg022_s          cn68xxp1;
	struct cvmx_pciercx_cfg022_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg022 cvmx_pciercx_cfg022_t;

/**
 * cvmx_pcierc#_cfg023
 *
 * PCIE_CFG023 = Twenty-fourth 32-bits of PCIE type 1 config space (MSI Data Register)
 *
 */
union cvmx_pciercx_cfg023 {
	uint32_t u32;
	struct cvmx_pciercx_cfg023_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t msimd                        : 16; /**< MSI Data
                                                         Pattern assigned by system software, bits [4:0] are Or-ed with
                                                         MSI_VECTOR to generate 32 MSI Messages per function. */
#else
	uint32_t msimd                        : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_pciercx_cfg023_s          cn52xx;
	struct cvmx_pciercx_cfg023_s          cn52xxp1;
	struct cvmx_pciercx_cfg023_s          cn56xx;
	struct cvmx_pciercx_cfg023_s          cn56xxp1;
	struct cvmx_pciercx_cfg023_s          cn61xx;
	struct cvmx_pciercx_cfg023_s          cn63xx;
	struct cvmx_pciercx_cfg023_s          cn63xxp1;
	struct cvmx_pciercx_cfg023_s          cn66xx;
	struct cvmx_pciercx_cfg023_s          cn68xx;
	struct cvmx_pciercx_cfg023_s          cn68xxp1;
	struct cvmx_pciercx_cfg023_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg023 cvmx_pciercx_cfg023_t;

/**
 * cvmx_pcierc#_cfg028
 *
 * PCIE_CFG028 = Twenty-ninth 32-bits of PCIE type 1 config space
 * (PCI Express Capabilities List Register/
 *  PCI Express Capabilities Register)
 */
union cvmx_pciercx_cfg028 {
	uint32_t u32;
	struct cvmx_pciercx_cfg028_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_30_31               : 2;
	uint32_t imn                          : 5;  /**< Interrupt Message Number
                                                         Updated by hardware, writable through PEM(0..1)_CFG_WR.
                                                          However, the application must not change this field. */
	uint32_t si                           : 1;  /**< Slot Implemented
                                                         This bit is writable through PEM(0..1)_CFG_WR.
                                                         However, it must 0 for an
                                                         Endpoint device. Therefore, the application must not write a
                                                         1 to this bit. */
	uint32_t dpt                          : 4;  /**< Device Port Type */
	uint32_t pciecv                       : 4;  /**< PCI Express Capability Version */
	uint32_t ncp                          : 8;  /**< Next Capability Pointer
                                                         writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t pcieid                       : 8;  /**< PCIE Capability ID */
#else
	uint32_t pcieid                       : 8;
	uint32_t ncp                          : 8;
	uint32_t pciecv                       : 4;
	uint32_t dpt                          : 4;
	uint32_t si                           : 1;
	uint32_t imn                          : 5;
	uint32_t reserved_30_31               : 2;
#endif
	} s;
	struct cvmx_pciercx_cfg028_s          cn52xx;
	struct cvmx_pciercx_cfg028_s          cn52xxp1;
	struct cvmx_pciercx_cfg028_s          cn56xx;
	struct cvmx_pciercx_cfg028_s          cn56xxp1;
	struct cvmx_pciercx_cfg028_s          cn61xx;
	struct cvmx_pciercx_cfg028_s          cn63xx;
	struct cvmx_pciercx_cfg028_s          cn63xxp1;
	struct cvmx_pciercx_cfg028_s          cn66xx;
	struct cvmx_pciercx_cfg028_s          cn68xx;
	struct cvmx_pciercx_cfg028_s          cn68xxp1;
	struct cvmx_pciercx_cfg028_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg028 cvmx_pciercx_cfg028_t;

/**
 * cvmx_pcierc#_cfg029
 *
 * PCIE_CFG029 = Thirtieth 32-bits of PCIE type 1 config space (Device Capabilities Register)
 *
 */
union cvmx_pciercx_cfg029 {
	uint32_t u32;
	struct cvmx_pciercx_cfg029_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_28_31               : 4;
	uint32_t cspls                        : 2;  /**< Captured Slot Power Limit Scale
                                                         Not applicable for RC port, upstream port only. */
	uint32_t csplv                        : 8;  /**< Captured Slot Power Limit Value
                                                         Not applicable for RC port, upstream port only. */
	uint32_t reserved_16_17               : 2;
	uint32_t rber                         : 1;  /**< Role-Based Error Reporting, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t reserved_12_14               : 3;
	uint32_t el1al                        : 3;  /**< Endpoint L1 Acceptable Latency, writable through PEM(0..1)_CFG_WR
                                                         Must be 0x0 for non-endpoint devices. */
	uint32_t el0al                        : 3;  /**< Endpoint L0s Acceptable Latency, writable through PEM(0..1)_CFG_WR
                                                         Must be 0x0 for non-endpoint devices. */
	uint32_t etfs                         : 1;  /**< Extended Tag Field Supported
                                                         This bit is writable through PEM(0..1)_CFG_WR.
                                                         However, the application
                                                         must not write a 1 to this bit. */
	uint32_t pfs                          : 2;  /**< Phantom Function Supported
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, Phantom
                                                         Function is not supported. Therefore, the application must not
                                                         write any value other than 0x0 to this field. */
	uint32_t mpss                         : 3;  /**< Max_Payload_Size Supported, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
#else
	uint32_t mpss                         : 3;
	uint32_t pfs                          : 2;
	uint32_t etfs                         : 1;
	uint32_t el0al                        : 3;
	uint32_t el1al                        : 3;
	uint32_t reserved_12_14               : 3;
	uint32_t rber                         : 1;
	uint32_t reserved_16_17               : 2;
	uint32_t csplv                        : 8;
	uint32_t cspls                        : 2;
	uint32_t reserved_28_31               : 4;
#endif
	} s;
	struct cvmx_pciercx_cfg029_s          cn52xx;
	struct cvmx_pciercx_cfg029_s          cn52xxp1;
	struct cvmx_pciercx_cfg029_s          cn56xx;
	struct cvmx_pciercx_cfg029_s          cn56xxp1;
	struct cvmx_pciercx_cfg029_s          cn61xx;
	struct cvmx_pciercx_cfg029_s          cn63xx;
	struct cvmx_pciercx_cfg029_s          cn63xxp1;
	struct cvmx_pciercx_cfg029_s          cn66xx;
	struct cvmx_pciercx_cfg029_s          cn68xx;
	struct cvmx_pciercx_cfg029_s          cn68xxp1;
	struct cvmx_pciercx_cfg029_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg029 cvmx_pciercx_cfg029_t;

/**
 * cvmx_pcierc#_cfg030
 *
 * PCIE_CFG030 = Thirty-first 32-bits of PCIE type 1 config space
 * (Device Control Register/Device Status Register)
 */
union cvmx_pciercx_cfg030 {
	uint32_t u32;
	struct cvmx_pciercx_cfg030_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_22_31               : 10;
	uint32_t tp                           : 1;  /**< Transaction Pending
                                                         Hard-wired to 0. */
	uint32_t ap_d                         : 1;  /**< Aux Power Detected
                                                         Set to 1 if Aux power detected. */
	uint32_t ur_d                         : 1;  /**< Unsupported Request Detected
                                                         Errors are logged in this register regardless of whether
                                                          error reporting is enabled in the Device Control register.
                                                         UR_D occurs when we receive something we don't support.
                                                         Unsupported requests are Nonfatal errors, so UR_D should
                                                         cause NFE_D.  Receiving a  vendor defined message should
                                                         cause an unsupported request. */
	uint32_t fe_d                         : 1;  /**< Fatal Error Detected
                                                         Errors are logged in this register regardless of whether
                                                          error reporting is enabled in the Device Control register.
                                                         FE_D is set if receive any of the errors in PCIE_CFG066 that
                                                         has a severity set to Fatal.  Malformed TLP's generally fit
                                                         into this category. */
	uint32_t nfe_d                        : 1;  /**< Non-Fatal Error detected
                                                         Errors are logged in this register regardless of whether
                                                          error reporting is enabled in the Device Control register.
                                                         NFE_D is set if we receive any of the errors in PCIE_CFG066
                                                         that has a severity set to Nonfatal and does NOT meet Advisory
                                                         Nonfatal criteria , which
                                                         most poisoned TLP's should be. */
	uint32_t ce_d                         : 1;  /**< Correctable Error Detected
                                                          Errors are logged in this register regardless of whether
                                                          error reporting is enabled in the Device Control register.
                                                         CE_D is set if we receive any of the errors in PCIE_CFG068
                                                         for example a Replay Timer Timeout.  Also, it can be set if
                                                         we get any of the errors in PCIE_CFG066 that has a severity
                                                         set to Nonfatal and meets the Advisory Nonfatal criteria,
                                                         which most ECRC errors should be. */
	uint32_t reserved_15_15               : 1;
	uint32_t mrrs                         : 3;  /**< Max Read Request Size
                                                          0 = 128B
                                                          1 = 256B
                                                          2 = 512B
                                                          3 = 1024B
                                                          4 = 2048B
                                                          5 = 4096B
                                                         Note: SLI_S2M_PORT#_CTL[MRRS] and DPI_SLI_PRT#_CFG[MRRS] and
                                                               also must be set properly.
                                                               SLI_S2M_PORT#_CTL[MRRS] and DPI_SLI_PRT#_CFG[MRRS] must
                                                               not exceed the desired max read request size. */
	uint32_t ns_en                        : 1;  /**< Enable No Snoop */
	uint32_t ap_en                        : 1;  /**< AUX Power PM Enable */
	uint32_t pf_en                        : 1;  /**< Phantom Function Enable
                                                         This bit should never be set - OCTEON requests never use
                                                         phantom functions. */
	uint32_t etf_en                       : 1;  /**< Extended Tag Field Enable
                                                         This bit should never be set - OCTEON requests never use
                                                         extended tags. */
	uint32_t mps                          : 3;  /**< Max Payload Size
                                                          Legal values:
                                                           0  = 128B
                                                           1  = 256B
                                                          Larger sizes not supported.
                                                         Note: Both PCI Express Ports must be set to the same value
                                                               for Peer-to-Peer to function properly.
                                                         Note: DPI_SLI_PRT#_CFG[MPS] must also be set to the same
                                                               value for proper functionality. */
	uint32_t ro_en                        : 1;  /**< Enable Relaxed Ordering
                                                         This bit is not used. */
	uint32_t ur_en                        : 1;  /**< Unsupported Request Reporting Enable */
	uint32_t fe_en                        : 1;  /**< Fatal Error Reporting Enable */
	uint32_t nfe_en                       : 1;  /**< Non-Fatal Error Reporting Enable */
	uint32_t ce_en                        : 1;  /**< Correctable Error Reporting Enable */
#else
	uint32_t ce_en                        : 1;
	uint32_t nfe_en                       : 1;
	uint32_t fe_en                        : 1;
	uint32_t ur_en                        : 1;
	uint32_t ro_en                        : 1;
	uint32_t mps                          : 3;
	uint32_t etf_en                       : 1;
	uint32_t pf_en                        : 1;
	uint32_t ap_en                        : 1;
	uint32_t ns_en                        : 1;
	uint32_t mrrs                         : 3;
	uint32_t reserved_15_15               : 1;
	uint32_t ce_d                         : 1;
	uint32_t nfe_d                        : 1;
	uint32_t fe_d                         : 1;
	uint32_t ur_d                         : 1;
	uint32_t ap_d                         : 1;
	uint32_t tp                           : 1;
	uint32_t reserved_22_31               : 10;
#endif
	} s;
	struct cvmx_pciercx_cfg030_s          cn52xx;
	struct cvmx_pciercx_cfg030_s          cn52xxp1;
	struct cvmx_pciercx_cfg030_s          cn56xx;
	struct cvmx_pciercx_cfg030_s          cn56xxp1;
	struct cvmx_pciercx_cfg030_s          cn61xx;
	struct cvmx_pciercx_cfg030_s          cn63xx;
	struct cvmx_pciercx_cfg030_s          cn63xxp1;
	struct cvmx_pciercx_cfg030_s          cn66xx;
	struct cvmx_pciercx_cfg030_s          cn68xx;
	struct cvmx_pciercx_cfg030_s          cn68xxp1;
	struct cvmx_pciercx_cfg030_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg030 cvmx_pciercx_cfg030_t;

/**
 * cvmx_pcierc#_cfg031
 *
 * PCIE_CFG031 = Thirty-second 32-bits of PCIE type 1 config space
 * (Link Capabilities Register)
 */
union cvmx_pciercx_cfg031 {
	uint32_t u32;
	struct cvmx_pciercx_cfg031_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pnum                         : 8;  /**< Port Number
                                                         writable through PEM(0..1)_CFG_WR, however the application
                                                         must not change this field. */
	uint32_t reserved_23_23               : 1;
	uint32_t aspm                         : 1;  /**< ASPM Optionality Compliance */
	uint32_t lbnc                         : 1;  /**< Link Bandwidth Notification Capability
                                                         Set to 1 for Root Complex devices. writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t dllarc                       : 1;  /**< Data Link Layer Active Reporting Capable
                                                         Set to 1 for Root Complex devices and 0 for Endpoint devices. */
	uint32_t sderc                        : 1;  /**< Surprise Down Error Reporting Capable
                                                         Not supported, hardwired to 0x0. */
	uint32_t cpm                          : 1;  /**< Clock Power Management
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t l1el                         : 3;  /**< L1 Exit Latency
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t l0el                         : 3;  /**< L0s Exit Latency
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t aslpms                       : 2;  /**< Active State Link PM Support
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t mlw                          : 6;  /**< Maximum Link Width
                                                         The default value is the value you specify during hardware
                                                         configuration (x1 or x2) writable through PEM(0..1)_CFG_WR. */
	uint32_t mls                          : 4;  /**< Maximum Link Speed
                                                         The reset value of this field is controlled by a value sent from
                                                         the lsb of the MIO_QLM#_SPD register.
                                                         qlm#_spd[0]   RST_VALUE   NOTE
                                                         1             0001b       2.5 GHz supported
                                                         0             0010b       5.0 GHz and 2.5 GHz supported
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t mls                          : 4;
	uint32_t mlw                          : 6;
	uint32_t aslpms                       : 2;
	uint32_t l0el                         : 3;
	uint32_t l1el                         : 3;
	uint32_t cpm                          : 1;
	uint32_t sderc                        : 1;
	uint32_t dllarc                       : 1;
	uint32_t lbnc                         : 1;
	uint32_t aspm                         : 1;
	uint32_t reserved_23_23               : 1;
	uint32_t pnum                         : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg031_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pnum                         : 8;  /**< Port Number, writable through PESC(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t reserved_22_23               : 2;
	uint32_t lbnc                         : 1;  /**< Link Bandwith Notification Capability */
	uint32_t dllarc                       : 1;  /**< Data Link Layer Active Reporting Capable
                                                         Set to 1 for Root Complex devices and 0 for Endpoint devices. */
	uint32_t sderc                        : 1;  /**< Surprise Down Error Reporting Capable
                                                         Not supported, hardwired to 0x0. */
	uint32_t cpm                          : 1;  /**< Clock Power Management
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PESC(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t l1el                         : 3;  /**< L1 Exit Latency
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PESC(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t l0el                         : 3;  /**< L0s Exit Latency
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PESC(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t aslpms                       : 2;  /**< Active State Link PM Support
                                                         The default value is the value you specify during hardware
                                                         configuration, writable through PESC(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t mlw                          : 6;  /**< Maximum Link Width
                                                         The default value is the value you specify during hardware
                                                         configuration (x1, x4, x8, or x16), writable through PESC(0..1)_CFG_WR.
                                                         The SW needs to set this to 0x4 or 0x2 depending on the max
                                                         number of lanes (QLM_CFG == 1 set to 0x4 else 0x2). */
	uint32_t mls                          : 4;  /**< Maximum Link Speed
                                                         Default value is 0x1 for 2.5 Gbps Link.
                                                         This field is writable through PESC(0..1)_CFG_WR.
                                                         However, 0x1 is the
                                                         only supported value. Therefore, the application must not write
                                                         any value other than 0x1 to this field. */
#else
	uint32_t mls                          : 4;
	uint32_t mlw                          : 6;
	uint32_t aslpms                       : 2;
	uint32_t l0el                         : 3;
	uint32_t l1el                         : 3;
	uint32_t cpm                          : 1;
	uint32_t sderc                        : 1;
	uint32_t dllarc                       : 1;
	uint32_t lbnc                         : 1;
	uint32_t reserved_22_23               : 2;
	uint32_t pnum                         : 8;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg031_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg031_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg031_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg031_s          cn61xx;
	struct cvmx_pciercx_cfg031_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg031_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg031_s          cn66xx;
	struct cvmx_pciercx_cfg031_s          cn68xx;
	struct cvmx_pciercx_cfg031_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg031_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg031 cvmx_pciercx_cfg031_t;

/**
 * cvmx_pcierc#_cfg032
 *
 * PCIE_CFG032 = Thirty-third 32-bits of PCIE type 1 config space
 * (Link Control Register/Link Status Register)
 */
union cvmx_pciercx_cfg032 {
	uint32_t u32;
	struct cvmx_pciercx_cfg032_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lab                          : 1;  /**< Link Autonomous Bandwidth Status
                                                         this bit is set to indicate that hardware has autonomously
                                                         changed Link speed or width, without the Port transitioning
                                                         through DL_Down status, for reasons other than to attempt
                                                         to correct unreliable Link operation. */
	uint32_t lbm                          : 1;  /**< Link Bandwidth Management Status
                                                         This bit is set to indicate either of the following has
                                                         occurred without the Port transitioning through DL_DOWN status
                                                         o A link retraining has completed following a write of 1b to
                                                           the Retrain Link bit
                                                         o Hardware has changed the Link speed or width to attempt to
                                                           correct unreliable Link operation, either through a LTSSM
                                                           timeout of higher level process.  This bit must be set if
                                                           the Physical Layer reports a speed or width change was
                                                           inititiated by the Downstream component tha was not
                                                           indicated as an autonomous change */
	uint32_t dlla                         : 1;  /**< Data Link Layer Active */
	uint32_t scc                          : 1;  /**< Slot Clock Configuration
                                                         Indicates that the component uses the same physical reference
                                                         clock that the platform provides on the connector. The default
                                                         value is the value you select during hardware configuration,
                                                         writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t lt                           : 1;  /**< Link Training */
	uint32_t reserved_26_26               : 1;
	uint32_t nlw                          : 6;  /**< Negotiated Link Width
                                                         Set automatically by hardware after Link initialization.
                                                         Value is undefined when link is not up. */
	uint32_t ls                           : 4;  /**< Link Speed
                                                         0001 == The negotiated Link speed: 2.5 Gbps
                                                         0010 == The negotiated Link speed: 5.0 Gbps
                                                         0100 == The negotiated Link speed: 8.0 Gbps (Not Supported) */
	uint32_t reserved_12_15               : 4;
	uint32_t lab_int_enb                  : 1;  /**< Link Autonomous Bandwidth Interrupt Enable
                                                         When set, enables the generation of an interrupt to indicate
                                                         that the Link Autonomous Bandwidth Status bit has been set. */
	uint32_t lbm_int_enb                  : 1;  /**< Link Bandwidth Management Interrupt Enable
                                                         When set, enables the generation of an interrupt to indicate
                                                         that the Link Bandwidth Management Status bit has been set. */
	uint32_t hawd                         : 1;  /**< Hardware Autonomous Width Disable
                                                         (Not Supported) */
	uint32_t ecpm                         : 1;  /**< Enable Clock Power Management
                                                         Hardwired to 0 if Clock Power Management is disabled in
                                                         the Link Capabilities register. */
	uint32_t es                           : 1;  /**< Extended Synch */
	uint32_t ccc                          : 1;  /**< Common Clock Configuration */
	uint32_t rl                           : 1;  /**< Retrain Link */
	uint32_t ld                           : 1;  /**< Link Disable */
	uint32_t rcb                          : 1;  /**< Read Completion Boundary (RCB), writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field
                                                         because an RCB of 64 bytes is not supported. */
	uint32_t reserved_2_2                 : 1;
	uint32_t aslpc                        : 2;  /**< Active State Link PM Control */
#else
	uint32_t aslpc                        : 2;
	uint32_t reserved_2_2                 : 1;
	uint32_t rcb                          : 1;
	uint32_t ld                           : 1;
	uint32_t rl                           : 1;
	uint32_t ccc                          : 1;
	uint32_t es                           : 1;
	uint32_t ecpm                         : 1;
	uint32_t hawd                         : 1;
	uint32_t lbm_int_enb                  : 1;
	uint32_t lab_int_enb                  : 1;
	uint32_t reserved_12_15               : 4;
	uint32_t ls                           : 4;
	uint32_t nlw                          : 6;
	uint32_t reserved_26_26               : 1;
	uint32_t lt                           : 1;
	uint32_t scc                          : 1;
	uint32_t dlla                         : 1;
	uint32_t lbm                          : 1;
	uint32_t lab                          : 1;
#endif
	} s;
	struct cvmx_pciercx_cfg032_s          cn52xx;
	struct cvmx_pciercx_cfg032_s          cn52xxp1;
	struct cvmx_pciercx_cfg032_s          cn56xx;
	struct cvmx_pciercx_cfg032_s          cn56xxp1;
	struct cvmx_pciercx_cfg032_s          cn61xx;
	struct cvmx_pciercx_cfg032_s          cn63xx;
	struct cvmx_pciercx_cfg032_s          cn63xxp1;
	struct cvmx_pciercx_cfg032_s          cn66xx;
	struct cvmx_pciercx_cfg032_s          cn68xx;
	struct cvmx_pciercx_cfg032_s          cn68xxp1;
	struct cvmx_pciercx_cfg032_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg032 cvmx_pciercx_cfg032_t;

/**
 * cvmx_pcierc#_cfg033
 *
 * PCIE_CFG033 = Thirty-fourth 32-bits of PCIE type 1 config space
 * (Slot Capabilities Register)
 */
union cvmx_pciercx_cfg033 {
	uint32_t u32;
	struct cvmx_pciercx_cfg033_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ps_num                       : 13; /**< Physical Slot Number, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t nccs                         : 1;  /**< No Command Complete Support, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t emip                         : 1;  /**< Electromechanical Interlock Present, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t sp_ls                        : 2;  /**< Slot Power Limit Scale, writable through PEM(0..1)_CFG_WR. */
	uint32_t sp_lv                        : 8;  /**< Slot Power Limit Value, writable through PEM(0..1)_CFG_WR. */
	uint32_t hp_c                         : 1;  /**< Hot-Plug Capable, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t hp_s                         : 1;  /**< Hot-Plug Surprise, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t pip                          : 1;  /**< Power Indicator Present, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t aip                          : 1;  /**< Attention Indicator Present, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t mrlsp                        : 1;  /**< MRL Sensor Present, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t pcp                          : 1;  /**< Power Controller Present, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
	uint32_t abp                          : 1;  /**< Attention Button Present, writable through PEM(0..1)_CFG_WR
                                                         However, the application must not change this field. */
#else
	uint32_t abp                          : 1;
	uint32_t pcp                          : 1;
	uint32_t mrlsp                        : 1;
	uint32_t aip                          : 1;
	uint32_t pip                          : 1;
	uint32_t hp_s                         : 1;
	uint32_t hp_c                         : 1;
	uint32_t sp_lv                        : 8;
	uint32_t sp_ls                        : 2;
	uint32_t emip                         : 1;
	uint32_t nccs                         : 1;
	uint32_t ps_num                       : 13;
#endif
	} s;
	struct cvmx_pciercx_cfg033_s          cn52xx;
	struct cvmx_pciercx_cfg033_s          cn52xxp1;
	struct cvmx_pciercx_cfg033_s          cn56xx;
	struct cvmx_pciercx_cfg033_s          cn56xxp1;
	struct cvmx_pciercx_cfg033_s          cn61xx;
	struct cvmx_pciercx_cfg033_s          cn63xx;
	struct cvmx_pciercx_cfg033_s          cn63xxp1;
	struct cvmx_pciercx_cfg033_s          cn66xx;
	struct cvmx_pciercx_cfg033_s          cn68xx;
	struct cvmx_pciercx_cfg033_s          cn68xxp1;
	struct cvmx_pciercx_cfg033_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg033 cvmx_pciercx_cfg033_t;

/**
 * cvmx_pcierc#_cfg034
 *
 * PCIE_CFG034 = Thirty-fifth 32-bits of PCIE type 1 config space
 * (Slot Control Register/Slot Status Register)
 */
union cvmx_pciercx_cfg034 {
	uint32_t u32;
	struct cvmx_pciercx_cfg034_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t dlls_c                       : 1;  /**< Data Link Layer State Changed */
	uint32_t emis                         : 1;  /**< Electromechanical Interlock Status */
	uint32_t pds                          : 1;  /**< Presence Detect State */
	uint32_t mrlss                        : 1;  /**< MRL Sensor State */
	uint32_t ccint_d                      : 1;  /**< Command Completed */
	uint32_t pd_c                         : 1;  /**< Presence Detect Changed */
	uint32_t mrls_c                       : 1;  /**< MRL Sensor Changed */
	uint32_t pf_d                         : 1;  /**< Power Fault Detected */
	uint32_t abp_d                        : 1;  /**< Attention Button Pressed */
	uint32_t reserved_13_15               : 3;
	uint32_t dlls_en                      : 1;  /**< Data Link Layer State Changed Enable */
	uint32_t emic                         : 1;  /**< Electromechanical Interlock Control */
	uint32_t pcc                          : 1;  /**< Power Controller Control */
	uint32_t pic                          : 2;  /**< Power Indicator Control */
	uint32_t aic                          : 2;  /**< Attention Indicator Control */
	uint32_t hpint_en                     : 1;  /**< Hot-Plug Interrupt Enable */
	uint32_t ccint_en                     : 1;  /**< Command Completed Interrupt Enable */
	uint32_t pd_en                        : 1;  /**< Presence Detect Changed Enable */
	uint32_t mrls_en                      : 1;  /**< MRL Sensor Changed Enable */
	uint32_t pf_en                        : 1;  /**< Power Fault Detected Enable */
	uint32_t abp_en                       : 1;  /**< Attention Button Pressed Enable */
#else
	uint32_t abp_en                       : 1;
	uint32_t pf_en                        : 1;
	uint32_t mrls_en                      : 1;
	uint32_t pd_en                        : 1;
	uint32_t ccint_en                     : 1;
	uint32_t hpint_en                     : 1;
	uint32_t aic                          : 2;
	uint32_t pic                          : 2;
	uint32_t pcc                          : 1;
	uint32_t emic                         : 1;
	uint32_t dlls_en                      : 1;
	uint32_t reserved_13_15               : 3;
	uint32_t abp_d                        : 1;
	uint32_t pf_d                         : 1;
	uint32_t mrls_c                       : 1;
	uint32_t pd_c                         : 1;
	uint32_t ccint_d                      : 1;
	uint32_t mrlss                        : 1;
	uint32_t pds                          : 1;
	uint32_t emis                         : 1;
	uint32_t dlls_c                       : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_pciercx_cfg034_s          cn52xx;
	struct cvmx_pciercx_cfg034_s          cn52xxp1;
	struct cvmx_pciercx_cfg034_s          cn56xx;
	struct cvmx_pciercx_cfg034_s          cn56xxp1;
	struct cvmx_pciercx_cfg034_s          cn61xx;
	struct cvmx_pciercx_cfg034_s          cn63xx;
	struct cvmx_pciercx_cfg034_s          cn63xxp1;
	struct cvmx_pciercx_cfg034_s          cn66xx;
	struct cvmx_pciercx_cfg034_s          cn68xx;
	struct cvmx_pciercx_cfg034_s          cn68xxp1;
	struct cvmx_pciercx_cfg034_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg034 cvmx_pciercx_cfg034_t;

/**
 * cvmx_pcierc#_cfg035
 *
 * PCIE_CFG035 = Thirty-sixth 32-bits of PCIE type 1 config space
 * (Root Control Register/Root Capabilities Register)
 */
union cvmx_pciercx_cfg035 {
	uint32_t u32;
	struct cvmx_pciercx_cfg035_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_17_31               : 15;
	uint32_t crssv                        : 1;  /**< CRS Software Visibility
                                                         Not supported, hardwired to 0x0. */
	uint32_t reserved_5_15                : 11;
	uint32_t crssve                       : 1;  /**< CRS Software Visibility Enable
                                                         Not supported, hardwired to 0x0. */
	uint32_t pmeie                        : 1;  /**< PME Interrupt Enable */
	uint32_t sefee                        : 1;  /**< System Error on Fatal Error Enable */
	uint32_t senfee                       : 1;  /**< System Error on Non-fatal Error Enable */
	uint32_t secee                        : 1;  /**< System Error on Correctable Error Enable */
#else
	uint32_t secee                        : 1;
	uint32_t senfee                       : 1;
	uint32_t sefee                        : 1;
	uint32_t pmeie                        : 1;
	uint32_t crssve                       : 1;
	uint32_t reserved_5_15                : 11;
	uint32_t crssv                        : 1;
	uint32_t reserved_17_31               : 15;
#endif
	} s;
	struct cvmx_pciercx_cfg035_s          cn52xx;
	struct cvmx_pciercx_cfg035_s          cn52xxp1;
	struct cvmx_pciercx_cfg035_s          cn56xx;
	struct cvmx_pciercx_cfg035_s          cn56xxp1;
	struct cvmx_pciercx_cfg035_s          cn61xx;
	struct cvmx_pciercx_cfg035_s          cn63xx;
	struct cvmx_pciercx_cfg035_s          cn63xxp1;
	struct cvmx_pciercx_cfg035_s          cn66xx;
	struct cvmx_pciercx_cfg035_s          cn68xx;
	struct cvmx_pciercx_cfg035_s          cn68xxp1;
	struct cvmx_pciercx_cfg035_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg035 cvmx_pciercx_cfg035_t;

/**
 * cvmx_pcierc#_cfg036
 *
 * PCIE_CFG036 = Thirty-seventh 32-bits of PCIE type 1 config space
 * (Root Status Register)
 */
union cvmx_pciercx_cfg036 {
	uint32_t u32;
	struct cvmx_pciercx_cfg036_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t pme_pend                     : 1;  /**< PME Pending */
	uint32_t pme_stat                     : 1;  /**< PME Status */
	uint32_t pme_rid                      : 16; /**< PME Requester ID */
#else
	uint32_t pme_rid                      : 16;
	uint32_t pme_stat                     : 1;
	uint32_t pme_pend                     : 1;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_pciercx_cfg036_s          cn52xx;
	struct cvmx_pciercx_cfg036_s          cn52xxp1;
	struct cvmx_pciercx_cfg036_s          cn56xx;
	struct cvmx_pciercx_cfg036_s          cn56xxp1;
	struct cvmx_pciercx_cfg036_s          cn61xx;
	struct cvmx_pciercx_cfg036_s          cn63xx;
	struct cvmx_pciercx_cfg036_s          cn63xxp1;
	struct cvmx_pciercx_cfg036_s          cn66xx;
	struct cvmx_pciercx_cfg036_s          cn68xx;
	struct cvmx_pciercx_cfg036_s          cn68xxp1;
	struct cvmx_pciercx_cfg036_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg036 cvmx_pciercx_cfg036_t;

/**
 * cvmx_pcierc#_cfg037
 *
 * PCIE_CFG037 = Thirty-eighth 32-bits of PCIE type 1 config space
 * (Device Capabilities 2 Register)
 */
union cvmx_pciercx_cfg037 {
	uint32_t u32;
	struct cvmx_pciercx_cfg037_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t obffs                        : 2;  /**< Optimized Buffer Flush Fill (OBFF) Supported
                                                         (Not Supported) */
	uint32_t reserved_12_17               : 6;
	uint32_t ltrs                         : 1;  /**< Latency Tolerance Reporting (LTR) Mechanism Supported
                                                         (Not Supported) */
	uint32_t noroprpr                     : 1;  /**< No RO-enabled PR-PR Passing
                                                         When set, the routing element never carries out the passing
                                                         permitted in the Relaxed Ordering Model. */
	uint32_t atom128s                     : 1;  /**< 128-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom64s                      : 1;  /**< 64-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom32s                      : 1;  /**< 32-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom_ops                     : 1;  /**< AtomicOp Routing Supported
                                                         (Not Supported) */
	uint32_t reserved_5_5                 : 1;
	uint32_t ctds                         : 1;  /**< Completion Timeout Disable Supported */
	uint32_t ctrs                         : 4;  /**< Completion Timeout Ranges Supported */
#else
	uint32_t ctrs                         : 4;
	uint32_t ctds                         : 1;
	uint32_t reserved_5_5                 : 1;
	uint32_t atom_ops                     : 1;
	uint32_t atom32s                      : 1;
	uint32_t atom64s                      : 1;
	uint32_t atom128s                     : 1;
	uint32_t noroprpr                     : 1;
	uint32_t ltrs                         : 1;
	uint32_t reserved_12_17               : 6;
	uint32_t obffs                        : 2;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg037_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_5_31                : 27;
	uint32_t ctds                         : 1;  /**< Completion Timeout Disable Supported */
	uint32_t ctrs                         : 4;  /**< Completion Timeout Ranges Supported
                                                         Value of 0 indicates that Completion Timeout Programming
                                                         is not supported
                                                         Completion timeout is 16.7ms. */
#else
	uint32_t ctrs                         : 4;
	uint32_t ctds                         : 1;
	uint32_t reserved_5_31                : 27;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg037_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg037_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg037_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg037_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t tph                          : 2;  /**< TPH Completer Supported
                                                         (Not Supported) */
	uint32_t reserved_11_11               : 1;
	uint32_t noroprpr                     : 1;  /**< No RO-enabled PR-PR Passing
                                                         When set, the routing element never carries out the passing
                                                         permitted in the Relaxed Ordering Model. */
	uint32_t atom128s                     : 1;  /**< 128-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom64s                      : 1;  /**< 64-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom32s                      : 1;  /**< 32-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom_ops                     : 1;  /**< AtomicOp Routing Supported
                                                         (Not Supported) */
	uint32_t ari_fw                       : 1;  /**< ARI Forwarding Supported
                                                         (Not Supported) */
	uint32_t ctds                         : 1;  /**< Completion Timeout Disable Supported */
	uint32_t ctrs                         : 4;  /**< Completion Timeout Ranges Supported */
#else
	uint32_t ctrs                         : 4;
	uint32_t ctds                         : 1;
	uint32_t ari_fw                       : 1;
	uint32_t atom_ops                     : 1;
	uint32_t atom32s                      : 1;
	uint32_t atom64s                      : 1;
	uint32_t atom128s                     : 1;
	uint32_t noroprpr                     : 1;
	uint32_t reserved_11_11               : 1;
	uint32_t tph                          : 2;
	uint32_t reserved_14_31               : 18;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg037_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg037_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg037_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t tph                          : 2;  /**< TPH Completer Supported
                                                         (Not Supported) */
	uint32_t reserved_11_11               : 1;
	uint32_t noroprpr                     : 1;  /**< No RO-enabled PR-PR Passing
                                                         When set, the routing element never carries out the passing
                                                         permitted in the Relaxed Ordering Model. */
	uint32_t atom128s                     : 1;  /**< 128-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom64s                      : 1;  /**< 64-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom32s                      : 1;  /**< 32-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom_ops                     : 1;  /**< AtomicOp Routing Supported
                                                         (Not Supported) */
	uint32_t ari                          : 1;  /**< Alternate Routing ID Forwarding Supported
                                                         (Not Supported) */
	uint32_t ctds                         : 1;  /**< Completion Timeout Disable Supported */
	uint32_t ctrs                         : 4;  /**< Completion Timeout Ranges Supported */
#else
	uint32_t ctrs                         : 4;
	uint32_t ctds                         : 1;
	uint32_t ari                          : 1;
	uint32_t atom_ops                     : 1;
	uint32_t atom32s                      : 1;
	uint32_t atom64s                      : 1;
	uint32_t atom128s                     : 1;
	uint32_t noroprpr                     : 1;
	uint32_t reserved_11_11               : 1;
	uint32_t tph                          : 2;
	uint32_t reserved_14_31               : 18;
#endif
	} cn66xx;
	struct cvmx_pciercx_cfg037_cn66xx     cn68xx;
	struct cvmx_pciercx_cfg037_cn66xx     cn68xxp1;
	struct cvmx_pciercx_cfg037_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t obffs                        : 2;  /**< Optimized Buffer Flush Fill (OBFF) Supported
                                                         (Not Supported) */
	uint32_t reserved_14_17               : 4;
	uint32_t tphs                         : 2;  /**< TPH Completer Supported
                                                         (Not Supported) */
	uint32_t ltrs                         : 1;  /**< Latency Tolerance Reporting (LTR) Mechanism Supported
                                                         (Not Supported) */
	uint32_t noroprpr                     : 1;  /**< No RO-enabled PR-PR Passing
                                                         When set, the routing element never carries out the passing
                                                         permitted in the Relaxed Ordering Model. */
	uint32_t atom128s                     : 1;  /**< 128-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom64s                      : 1;  /**< 64-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom32s                      : 1;  /**< 32-bit AtomicOp Supported
                                                         (Not Supported) */
	uint32_t atom_ops                     : 1;  /**< AtomicOp Routing Supported
                                                         (Not Supported) */
	uint32_t ari_fw                       : 1;  /**< ARI Forwarding Supported
                                                         (Not Supported) */
	uint32_t ctds                         : 1;  /**< Completion Timeout Disable Supported */
	uint32_t ctrs                         : 4;  /**< Completion Timeout Ranges Supported */
#else
	uint32_t ctrs                         : 4;
	uint32_t ctds                         : 1;
	uint32_t ari_fw                       : 1;
	uint32_t atom_ops                     : 1;
	uint32_t atom32s                      : 1;
	uint32_t atom64s                      : 1;
	uint32_t atom128s                     : 1;
	uint32_t noroprpr                     : 1;
	uint32_t ltrs                         : 1;
	uint32_t tphs                         : 2;
	uint32_t reserved_14_17               : 4;
	uint32_t obffs                        : 2;
	uint32_t reserved_20_31               : 12;
#endif
	} cnf71xx;
};
typedef union cvmx_pciercx_cfg037 cvmx_pciercx_cfg037_t;

/**
 * cvmx_pcierc#_cfg038
 *
 * PCIE_CFG038 = Thirty-ninth 32-bits of PCIE type 1 config space
 * (Device Control 2 Register)
 */
union cvmx_pciercx_cfg038 {
	uint32_t u32;
	struct cvmx_pciercx_cfg038_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_15_31               : 17;
	uint32_t obffe                        : 2;  /**< Optimized Buffer Flush Fill (OBFF) Enable
                                                         (Not Supported) */
	uint32_t reserved_11_12               : 2;
	uint32_t ltre                         : 1;  /**< Latency Tolerance Reporting (LTR) Mechanism Enable
                                                         (Not Supported) */
	uint32_t id0_cp                       : 1;  /**< ID Based Ordering Completion Enable
                                                         (Not Supported) */
	uint32_t id0_rq                       : 1;  /**< ID Based Ordering Request Enable
                                                         (Not Supported) */
	uint32_t atom_op_eb                   : 1;  /**< AtomicOp Egress Blocking
                                                         (Not Supported)m */
	uint32_t atom_op                      : 1;  /**< AtomicOp Requester Enable
                                                         (Not Supported) */
	uint32_t ari                          : 1;  /**< Alternate Routing ID Forwarding Supported
                                                         (Not Supported) */
	uint32_t ctd                          : 1;  /**< Completion Timeout Disable */
	uint32_t ctv                          : 4;  /**< Completion Timeout Value
                                                         o 0000b Default range: 16 ms to 55 ms
                                                         o 0001b 50 us to 100 us
                                                         o 0010b 1 ms to 10 ms
                                                         o 0101b 16 ms to 55 ms
                                                         o 0110b 65 ms to 210 ms
                                                         o 1001b 260 ms to 900 ms
                                                         o 1010b 1 s to 3.5 s
                                                         o 1101b 4 s to 13 s
                                                         o 1110b 17 s to 64 s
                                                         Values not defined are reserved */
#else
	uint32_t ctv                          : 4;
	uint32_t ctd                          : 1;
	uint32_t ari                          : 1;
	uint32_t atom_op                      : 1;
	uint32_t atom_op_eb                   : 1;
	uint32_t id0_rq                       : 1;
	uint32_t id0_cp                       : 1;
	uint32_t ltre                         : 1;
	uint32_t reserved_11_12               : 2;
	uint32_t obffe                        : 2;
	uint32_t reserved_15_31               : 17;
#endif
	} s;
	struct cvmx_pciercx_cfg038_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_5_31                : 27;
	uint32_t ctd                          : 1;  /**< Completion Timeout Disable */
	uint32_t ctv                          : 4;  /**< Completion Timeout Value
                                                         Completion Timeout Programming is not supported
                                                         Completion timeout is 16.7ms. */
#else
	uint32_t ctv                          : 4;
	uint32_t ctd                          : 1;
	uint32_t reserved_5_31                : 27;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg038_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg038_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg038_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg038_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_10_31               : 22;
	uint32_t id0_cp                       : 1;  /**< ID Based Ordering Completion Enable
                                                         (Not Supported) */
	uint32_t id0_rq                       : 1;  /**< ID Based Ordering Request Enable
                                                         (Not Supported) */
	uint32_t atom_op_eb                   : 1;  /**< AtomicOp Egress Blocking
                                                         (Not Supported)m */
	uint32_t atom_op                      : 1;  /**< AtomicOp Requester Enable
                                                         (Not Supported) */
	uint32_t ari                          : 1;  /**< Alternate Routing ID Forwarding Supported
                                                         (Not Supported) */
	uint32_t ctd                          : 1;  /**< Completion Timeout Disable */
	uint32_t ctv                          : 4;  /**< Completion Timeout Value
                                                         o 0000b Default range: 16 ms to 55 ms
                                                         o 0001b 50 us to 100 us
                                                         o 0010b 1 ms to 10 ms
                                                         o 0101b 16 ms to 55 ms
                                                         o 0110b 65 ms to 210 ms
                                                         o 1001b 260 ms to 900 ms
                                                         o 1010b 1 s to 3.5 s
                                                         o 1101b 4 s to 13 s
                                                         o 1110b 17 s to 64 s
                                                         Values not defined are reserved */
#else
	uint32_t ctv                          : 4;
	uint32_t ctd                          : 1;
	uint32_t ari                          : 1;
	uint32_t atom_op                      : 1;
	uint32_t atom_op_eb                   : 1;
	uint32_t id0_rq                       : 1;
	uint32_t id0_cp                       : 1;
	uint32_t reserved_10_31               : 22;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg038_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg038_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg038_cn61xx     cn66xx;
	struct cvmx_pciercx_cfg038_cn61xx     cn68xx;
	struct cvmx_pciercx_cfg038_cn61xx     cn68xxp1;
	struct cvmx_pciercx_cfg038_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg038 cvmx_pciercx_cfg038_t;

/**
 * cvmx_pcierc#_cfg039
 *
 * PCIE_CFG039 = Fourtieth 32-bits of PCIE type 1 config space
 * (Link Capabilities 2 Register)
 */
union cvmx_pciercx_cfg039 {
	uint32_t u32;
	struct cvmx_pciercx_cfg039_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t cls                          : 1;  /**< Crosslink Supported */
	uint32_t slsv                         : 7;  /**< Supported Link Speeds Vector
                                                         Indicates the supported Link speeds of the associated Port.
                                                         For each bit, a value of 1b indicates that the cooresponding
                                                         Link speed is supported; otherwise, the Link speed is not
                                                         supported.
                                                         Bit definitions are:
                                                         Bit 1 2.5 GT/s
                                                         Bit 2 5.0 GT/s
                                                         Bit 3 8.0 GT/s (Not Supported)
                                                         Bits 7:4 reserved
                                                         The reset value of this field is controlled by a value sent from
                                                         the lsb of the MIO_QLM#_SPD register
                                                         qlm#_spd[0]   RST_VALUE   NOTE
                                                         1             0001b       2.5 GHz supported
                                                         0             0011b       5.0 GHz and 2.5 GHz supported */
	uint32_t reserved_0_0                 : 1;
#else
	uint32_t reserved_0_0                 : 1;
	uint32_t slsv                         : 7;
	uint32_t cls                          : 1;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_pciercx_cfg039_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg039_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg039_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg039_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg039_s          cn61xx;
	struct cvmx_pciercx_cfg039_s          cn63xx;
	struct cvmx_pciercx_cfg039_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg039_s          cn66xx;
	struct cvmx_pciercx_cfg039_s          cn68xx;
	struct cvmx_pciercx_cfg039_s          cn68xxp1;
	struct cvmx_pciercx_cfg039_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg039 cvmx_pciercx_cfg039_t;

/**
 * cvmx_pcierc#_cfg040
 *
 * PCIE_CFG040 = Fourty-first 32-bits of PCIE type 1 config space
 * (Link Control 2 Register/Link Status 2 Register)
 */
union cvmx_pciercx_cfg040 {
	uint32_t u32;
	struct cvmx_pciercx_cfg040_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_17_31               : 15;
	uint32_t cdl                          : 1;  /**< Current De-emphasis Level
                                                         When the Link is operating at 5 GT/s speed, this bit
                                                         reflects the level of de-emphasis. Encodings:
                                                          1b: -3.5 dB
                                                          0b: -6 dB
                                                         Note: The value in this bit is undefined when the Link is
                                                         operating at 2.5 GT/s speed */
	uint32_t reserved_13_15               : 3;
	uint32_t cde                          : 1;  /**< Compliance De-emphasis
                                                         This bit sets the de-emphasis level in Polling. Compliance
                                                         state if the entry occurred due to the Tx Compliance
                                                         Receive bit being 1b. Encodings:
                                                          1b: -3.5 dB
                                                          0b: -6 dB
                                                         Note: When the Link is operating at 2.5 GT/s, the setting
                                                         of this bit has no effect. */
	uint32_t csos                         : 1;  /**< Compliance SOS
                                                         When set to 1b, the LTSSM is required to send SKP
                                                         Ordered Sets periodically in between the (modified)
                                                         compliance patterns.
                                                         Note: When the Link is operating at 2.5 GT/s, the setting
                                                         of this bit has no effect. */
	uint32_t emc                          : 1;  /**< Enter Modified Compliance
                                                         When this bit is set to 1b, the device transmits a modified
                                                         compliance pattern if the LTSSM enters Polling.
                                                         Compliance state. */
	uint32_t tm                           : 3;  /**< Transmit Margin
                                                         This field controls the value of the non-de-emphasized
                                                         voltage level at the Transmitter signals:
                                                          - 000: 800-1200 mV for full swing 400-600 mV for half-swing
                                                          - 001-010: values must be monotonic with a non-zero slope
                                                          - 011: 200-400 mV for full-swing and 100-200 mV for halfswing
                                                          - 100-111: reserved
                                                         This field is reset to 000b on entry to the LTSSM Polling.
                                                         Compliance substate.
                                                         When operating in 5.0 GT/s mode with full swing, the
                                                         de-emphasis ratio must be maintained within +/- 1 dB
                                                         from the specification-defined operational value
                                                         either -3.5 or -6 dB). */
	uint32_t sde                          : 1;  /**< Selectable De-emphasis
                                                         When the Link is operating at 5.0 GT/s speed, selects the
                                                         level of de-emphasis:
                                                         - 1: -3.5 dB
                                                         - 0: -6 dB
                                                         When the Link is operating at 2.5 GT/s speed, the setting
                                                         of this bit has no effect. */
	uint32_t hasd                         : 1;  /**< Hardware Autonomous Speed Disable
                                                         When asserted, the
                                                         application must disable hardware from changing the Link
                                                         speed for device-specific reasons other than attempting to
                                                         correct unreliable Link operation by reducing Link speed.
                                                         Initial transition to the highest supported common link
                                                         speed is not blocked by this signal. */
	uint32_t ec                           : 1;  /**< Enter Compliance
                                                         Software is permitted to force a link to enter Compliance
                                                         mode at the speed indicated in the Target Link Speed
                                                         field by setting this bit to 1b in both components on a link
                                                         and then initiating a hot reset on the link. */
	uint32_t tls                          : 4;  /**< Target Link Speed
                                                         For Downstream ports, this field sets an upper limit on link
                                                         operational speed by restricting the values advertised by
                                                         the upstream component in its training sequences:
                                                           - 0001: 2.5Gb/s Target Link Speed
                                                           - 0010: 5Gb/s Target Link Speed
                                                           - 0100: 8Gb/s Target Link Speed (Not Supported)
                                                         All other encodings are reserved.
                                                         If a value is written to this field that does not correspond to
                                                         a speed included in the Supported Link Speeds field, the
                                                         result is undefined.
                                                         For both Upstream and Downstream ports, this field is
                                                         used to set the target compliance mode speed when
                                                         software is using the Enter Compliance bit to force a link
                                                         into compliance mode.
                                                         The reset value of this field is controlled by a value sent from
                                                         the lsb of the MIO_QLM#_SPD register.
                                                         qlm#_spd[0]   RST_VALUE   NOTE
                                                         1             0001b       2.5 GHz supported
                                                         0             0010b       5.0 GHz and 2.5 GHz supported */
#else
	uint32_t tls                          : 4;
	uint32_t ec                           : 1;
	uint32_t hasd                         : 1;
	uint32_t sde                          : 1;
	uint32_t tm                           : 3;
	uint32_t emc                          : 1;
	uint32_t csos                         : 1;
	uint32_t cde                          : 1;
	uint32_t reserved_13_15               : 3;
	uint32_t cdl                          : 1;
	uint32_t reserved_17_31               : 15;
#endif
	} s;
	struct cvmx_pciercx_cfg040_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg040_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg040_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg040_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg040_s          cn61xx;
	struct cvmx_pciercx_cfg040_s          cn63xx;
	struct cvmx_pciercx_cfg040_s          cn63xxp1;
	struct cvmx_pciercx_cfg040_s          cn66xx;
	struct cvmx_pciercx_cfg040_s          cn68xx;
	struct cvmx_pciercx_cfg040_s          cn68xxp1;
	struct cvmx_pciercx_cfg040_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg040 cvmx_pciercx_cfg040_t;

/**
 * cvmx_pcierc#_cfg041
 *
 * PCIE_CFG041 = Fourty-second 32-bits of PCIE type 1 config space
 * (Slot Capabilities 2 Register)
 */
union cvmx_pciercx_cfg041 {
	uint32_t u32;
	struct cvmx_pciercx_cfg041_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg041_s          cn52xx;
	struct cvmx_pciercx_cfg041_s          cn52xxp1;
	struct cvmx_pciercx_cfg041_s          cn56xx;
	struct cvmx_pciercx_cfg041_s          cn56xxp1;
	struct cvmx_pciercx_cfg041_s          cn61xx;
	struct cvmx_pciercx_cfg041_s          cn63xx;
	struct cvmx_pciercx_cfg041_s          cn63xxp1;
	struct cvmx_pciercx_cfg041_s          cn66xx;
	struct cvmx_pciercx_cfg041_s          cn68xx;
	struct cvmx_pciercx_cfg041_s          cn68xxp1;
	struct cvmx_pciercx_cfg041_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg041 cvmx_pciercx_cfg041_t;

/**
 * cvmx_pcierc#_cfg042
 *
 * PCIE_CFG042 = Fourty-third 32-bits of PCIE type 1 config space
 * (Slot Control 2 Register/Slot Status 2 Register)
 */
union cvmx_pciercx_cfg042 {
	uint32_t u32;
	struct cvmx_pciercx_cfg042_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_0_31                : 32;
#else
	uint32_t reserved_0_31                : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg042_s          cn52xx;
	struct cvmx_pciercx_cfg042_s          cn52xxp1;
	struct cvmx_pciercx_cfg042_s          cn56xx;
	struct cvmx_pciercx_cfg042_s          cn56xxp1;
	struct cvmx_pciercx_cfg042_s          cn61xx;
	struct cvmx_pciercx_cfg042_s          cn63xx;
	struct cvmx_pciercx_cfg042_s          cn63xxp1;
	struct cvmx_pciercx_cfg042_s          cn66xx;
	struct cvmx_pciercx_cfg042_s          cn68xx;
	struct cvmx_pciercx_cfg042_s          cn68xxp1;
	struct cvmx_pciercx_cfg042_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg042 cvmx_pciercx_cfg042_t;

/**
 * cvmx_pcierc#_cfg064
 *
 * PCIE_CFG064 = Sixty-fifth 32-bits of PCIE type 1 config space
 * (PCI Express Extended Capability Header)
 */
union cvmx_pciercx_cfg064 {
	uint32_t u32;
	struct cvmx_pciercx_cfg064_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t nco                          : 12; /**< Next Capability Offset */
	uint32_t cv                           : 4;  /**< Capability Version */
	uint32_t pcieec                       : 16; /**< PCIE Express Extended Capability */
#else
	uint32_t pcieec                       : 16;
	uint32_t cv                           : 4;
	uint32_t nco                          : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg064_s          cn52xx;
	struct cvmx_pciercx_cfg064_s          cn52xxp1;
	struct cvmx_pciercx_cfg064_s          cn56xx;
	struct cvmx_pciercx_cfg064_s          cn56xxp1;
	struct cvmx_pciercx_cfg064_s          cn61xx;
	struct cvmx_pciercx_cfg064_s          cn63xx;
	struct cvmx_pciercx_cfg064_s          cn63xxp1;
	struct cvmx_pciercx_cfg064_s          cn66xx;
	struct cvmx_pciercx_cfg064_s          cn68xx;
	struct cvmx_pciercx_cfg064_s          cn68xxp1;
	struct cvmx_pciercx_cfg064_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg064 cvmx_pciercx_cfg064_t;

/**
 * cvmx_pcierc#_cfg065
 *
 * PCIE_CFG065 = Sixty-sixth 32-bits of PCIE type 1 config space
 * (Uncorrectable Error Status Register)
 */
union cvmx_pciercx_cfg065 {
	uint32_t u32;
	struct cvmx_pciercx_cfg065_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t uatombs                      : 1;  /**< Unsupported AtomicOp Egress Blocked Status */
	uint32_t reserved_23_23               : 1;
	uint32_t ucies                        : 1;  /**< Uncorrectable Internal Error Status */
	uint32_t reserved_21_21               : 1;
	uint32_t ures                         : 1;  /**< Unsupported Request Error Status */
	uint32_t ecrces                       : 1;  /**< ECRC Error Status */
	uint32_t mtlps                        : 1;  /**< Malformed TLP Status */
	uint32_t ros                          : 1;  /**< Receiver Overflow Status */
	uint32_t ucs                          : 1;  /**< Unexpected Completion Status */
	uint32_t cas                          : 1;  /**< Completer Abort Status */
	uint32_t cts                          : 1;  /**< Completion Timeout Status */
	uint32_t fcpes                        : 1;  /**< Flow Control Protocol Error Status */
	uint32_t ptlps                        : 1;  /**< Poisoned TLP Status */
	uint32_t reserved_6_11                : 6;
	uint32_t sdes                         : 1;  /**< Surprise Down Error Status (not supported) */
	uint32_t dlpes                        : 1;  /**< Data Link Protocol Error Status */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpes                        : 1;
	uint32_t sdes                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlps                        : 1;
	uint32_t fcpes                        : 1;
	uint32_t cts                          : 1;
	uint32_t cas                          : 1;
	uint32_t ucs                          : 1;
	uint32_t ros                          : 1;
	uint32_t mtlps                        : 1;
	uint32_t ecrces                       : 1;
	uint32_t ures                         : 1;
	uint32_t reserved_21_21               : 1;
	uint32_t ucies                        : 1;
	uint32_t reserved_23_23               : 1;
	uint32_t uatombs                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_pciercx_cfg065_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_21_31               : 11;
	uint32_t ures                         : 1;  /**< Unsupported Request Error Status */
	uint32_t ecrces                       : 1;  /**< ECRC Error Status */
	uint32_t mtlps                        : 1;  /**< Malformed TLP Status */
	uint32_t ros                          : 1;  /**< Receiver Overflow Status */
	uint32_t ucs                          : 1;  /**< Unexpected Completion Status */
	uint32_t cas                          : 1;  /**< Completer Abort Status */
	uint32_t cts                          : 1;  /**< Completion Timeout Status */
	uint32_t fcpes                        : 1;  /**< Flow Control Protocol Error Status */
	uint32_t ptlps                        : 1;  /**< Poisoned TLP Status */
	uint32_t reserved_6_11                : 6;
	uint32_t sdes                         : 1;  /**< Surprise Down Error Status (not supported) */
	uint32_t dlpes                        : 1;  /**< Data Link Protocol Error Status */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpes                        : 1;
	uint32_t sdes                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlps                        : 1;
	uint32_t fcpes                        : 1;
	uint32_t cts                          : 1;
	uint32_t cas                          : 1;
	uint32_t ucs                          : 1;
	uint32_t ros                          : 1;
	uint32_t mtlps                        : 1;
	uint32_t ecrces                       : 1;
	uint32_t ures                         : 1;
	uint32_t reserved_21_31               : 11;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg065_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg065_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg065_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg065_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t uatombs                      : 1;  /**< Unsupported AtomicOp Egress Blocked Status */
	uint32_t reserved_21_23               : 3;
	uint32_t ures                         : 1;  /**< Unsupported Request Error Status */
	uint32_t ecrces                       : 1;  /**< ECRC Error Status */
	uint32_t mtlps                        : 1;  /**< Malformed TLP Status */
	uint32_t ros                          : 1;  /**< Receiver Overflow Status */
	uint32_t ucs                          : 1;  /**< Unexpected Completion Status */
	uint32_t cas                          : 1;  /**< Completer Abort Status */
	uint32_t cts                          : 1;  /**< Completion Timeout Status */
	uint32_t fcpes                        : 1;  /**< Flow Control Protocol Error Status */
	uint32_t ptlps                        : 1;  /**< Poisoned TLP Status */
	uint32_t reserved_6_11                : 6;
	uint32_t sdes                         : 1;  /**< Surprise Down Error Status (not supported) */
	uint32_t dlpes                        : 1;  /**< Data Link Protocol Error Status */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpes                        : 1;
	uint32_t sdes                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlps                        : 1;
	uint32_t fcpes                        : 1;
	uint32_t cts                          : 1;
	uint32_t cas                          : 1;
	uint32_t ucs                          : 1;
	uint32_t ros                          : 1;
	uint32_t mtlps                        : 1;
	uint32_t ecrces                       : 1;
	uint32_t ures                         : 1;
	uint32_t reserved_21_23               : 3;
	uint32_t uatombs                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg065_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg065_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg065_cn61xx     cn66xx;
	struct cvmx_pciercx_cfg065_cn61xx     cn68xx;
	struct cvmx_pciercx_cfg065_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg065_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg065 cvmx_pciercx_cfg065_t;

/**
 * cvmx_pcierc#_cfg066
 *
 * PCIE_CFG066 = Sixty-seventh 32-bits of PCIE type 1 config space
 * (Uncorrectable Error Mask Register)
 */
union cvmx_pciercx_cfg066 {
	uint32_t u32;
	struct cvmx_pciercx_cfg066_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t uatombm                      : 1;  /**< Unsupported AtomicOp Egress Blocked Mask */
	uint32_t reserved_23_23               : 1;
	uint32_t uciem                        : 1;  /**< Uncorrectable Internal Error Mask */
	uint32_t reserved_21_21               : 1;
	uint32_t urem                         : 1;  /**< Unsupported Request Error Mask */
	uint32_t ecrcem                       : 1;  /**< ECRC Error Mask */
	uint32_t mtlpm                        : 1;  /**< Malformed TLP Mask */
	uint32_t rom                          : 1;  /**< Receiver Overflow Mask */
	uint32_t ucm                          : 1;  /**< Unexpected Completion Mask */
	uint32_t cam                          : 1;  /**< Completer Abort Mask */
	uint32_t ctm                          : 1;  /**< Completion Timeout Mask */
	uint32_t fcpem                        : 1;  /**< Flow Control Protocol Error Mask */
	uint32_t ptlpm                        : 1;  /**< Poisoned TLP Mask */
	uint32_t reserved_6_11                : 6;
	uint32_t sdem                         : 1;  /**< Surprise Down Error Mask (not supported) */
	uint32_t dlpem                        : 1;  /**< Data Link Protocol Error Mask */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpem                        : 1;
	uint32_t sdem                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlpm                        : 1;
	uint32_t fcpem                        : 1;
	uint32_t ctm                          : 1;
	uint32_t cam                          : 1;
	uint32_t ucm                          : 1;
	uint32_t rom                          : 1;
	uint32_t mtlpm                        : 1;
	uint32_t ecrcem                       : 1;
	uint32_t urem                         : 1;
	uint32_t reserved_21_21               : 1;
	uint32_t uciem                        : 1;
	uint32_t reserved_23_23               : 1;
	uint32_t uatombm                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_pciercx_cfg066_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_21_31               : 11;
	uint32_t urem                         : 1;  /**< Unsupported Request Error Mask */
	uint32_t ecrcem                       : 1;  /**< ECRC Error Mask */
	uint32_t mtlpm                        : 1;  /**< Malformed TLP Mask */
	uint32_t rom                          : 1;  /**< Receiver Overflow Mask */
	uint32_t ucm                          : 1;  /**< Unexpected Completion Mask */
	uint32_t cam                          : 1;  /**< Completer Abort Mask */
	uint32_t ctm                          : 1;  /**< Completion Timeout Mask */
	uint32_t fcpem                        : 1;  /**< Flow Control Protocol Error Mask */
	uint32_t ptlpm                        : 1;  /**< Poisoned TLP Mask */
	uint32_t reserved_6_11                : 6;
	uint32_t sdem                         : 1;  /**< Surprise Down Error Mask (not supported) */
	uint32_t dlpem                        : 1;  /**< Data Link Protocol Error Mask */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpem                        : 1;
	uint32_t sdem                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlpm                        : 1;
	uint32_t fcpem                        : 1;
	uint32_t ctm                          : 1;
	uint32_t cam                          : 1;
	uint32_t ucm                          : 1;
	uint32_t rom                          : 1;
	uint32_t mtlpm                        : 1;
	uint32_t ecrcem                       : 1;
	uint32_t urem                         : 1;
	uint32_t reserved_21_31               : 11;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg066_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg066_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg066_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg066_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t uatombm                      : 1;  /**< Unsupported AtomicOp Egress Blocked Mask */
	uint32_t reserved_21_23               : 3;
	uint32_t urem                         : 1;  /**< Unsupported Request Error Mask */
	uint32_t ecrcem                       : 1;  /**< ECRC Error Mask */
	uint32_t mtlpm                        : 1;  /**< Malformed TLP Mask */
	uint32_t rom                          : 1;  /**< Receiver Overflow Mask */
	uint32_t ucm                          : 1;  /**< Unexpected Completion Mask */
	uint32_t cam                          : 1;  /**< Completer Abort Mask */
	uint32_t ctm                          : 1;  /**< Completion Timeout Mask */
	uint32_t fcpem                        : 1;  /**< Flow Control Protocol Error Mask */
	uint32_t ptlpm                        : 1;  /**< Poisoned TLP Mask */
	uint32_t reserved_6_11                : 6;
	uint32_t sdem                         : 1;  /**< Surprise Down Error Mask (not supported) */
	uint32_t dlpem                        : 1;  /**< Data Link Protocol Error Mask */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpem                        : 1;
	uint32_t sdem                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlpm                        : 1;
	uint32_t fcpem                        : 1;
	uint32_t ctm                          : 1;
	uint32_t cam                          : 1;
	uint32_t ucm                          : 1;
	uint32_t rom                          : 1;
	uint32_t mtlpm                        : 1;
	uint32_t ecrcem                       : 1;
	uint32_t urem                         : 1;
	uint32_t reserved_21_23               : 3;
	uint32_t uatombm                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg066_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg066_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg066_cn61xx     cn66xx;
	struct cvmx_pciercx_cfg066_cn61xx     cn68xx;
	struct cvmx_pciercx_cfg066_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg066_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg066 cvmx_pciercx_cfg066_t;

/**
 * cvmx_pcierc#_cfg067
 *
 * PCIE_CFG067 = Sixty-eighth 32-bits of PCIE type 1 config space
 * (Uncorrectable Error Severity Register)
 */
union cvmx_pciercx_cfg067 {
	uint32_t u32;
	struct cvmx_pciercx_cfg067_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t uatombs                      : 1;  /**< Unsupported AtomicOp Egress Blocked Severity */
	uint32_t reserved_23_23               : 1;
	uint32_t ucies                        : 1;  /**< Uncorrectable Internal Error Severity */
	uint32_t reserved_21_21               : 1;
	uint32_t ures                         : 1;  /**< Unsupported Request Error Severity */
	uint32_t ecrces                       : 1;  /**< ECRC Error Severity */
	uint32_t mtlps                        : 1;  /**< Malformed TLP Severity */
	uint32_t ros                          : 1;  /**< Receiver Overflow Severity */
	uint32_t ucs                          : 1;  /**< Unexpected Completion Severity */
	uint32_t cas                          : 1;  /**< Completer Abort Severity */
	uint32_t cts                          : 1;  /**< Completion Timeout Severity */
	uint32_t fcpes                        : 1;  /**< Flow Control Protocol Error Severity */
	uint32_t ptlps                        : 1;  /**< Poisoned TLP Severity */
	uint32_t reserved_6_11                : 6;
	uint32_t sdes                         : 1;  /**< Surprise Down Error Severity (not supported) */
	uint32_t dlpes                        : 1;  /**< Data Link Protocol Error Severity */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpes                        : 1;
	uint32_t sdes                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlps                        : 1;
	uint32_t fcpes                        : 1;
	uint32_t cts                          : 1;
	uint32_t cas                          : 1;
	uint32_t ucs                          : 1;
	uint32_t ros                          : 1;
	uint32_t mtlps                        : 1;
	uint32_t ecrces                       : 1;
	uint32_t ures                         : 1;
	uint32_t reserved_21_21               : 1;
	uint32_t ucies                        : 1;
	uint32_t reserved_23_23               : 1;
	uint32_t uatombs                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} s;
	struct cvmx_pciercx_cfg067_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_21_31               : 11;
	uint32_t ures                         : 1;  /**< Unsupported Request Error Severity */
	uint32_t ecrces                       : 1;  /**< ECRC Error Severity */
	uint32_t mtlps                        : 1;  /**< Malformed TLP Severity */
	uint32_t ros                          : 1;  /**< Receiver Overflow Severity */
	uint32_t ucs                          : 1;  /**< Unexpected Completion Severity */
	uint32_t cas                          : 1;  /**< Completer Abort Severity */
	uint32_t cts                          : 1;  /**< Completion Timeout Severity */
	uint32_t fcpes                        : 1;  /**< Flow Control Protocol Error Severity */
	uint32_t ptlps                        : 1;  /**< Poisoned TLP Severity */
	uint32_t reserved_6_11                : 6;
	uint32_t sdes                         : 1;  /**< Surprise Down Error Severity (not supported) */
	uint32_t dlpes                        : 1;  /**< Data Link Protocol Error Severity */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpes                        : 1;
	uint32_t sdes                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlps                        : 1;
	uint32_t fcpes                        : 1;
	uint32_t cts                          : 1;
	uint32_t cas                          : 1;
	uint32_t ucs                          : 1;
	uint32_t ros                          : 1;
	uint32_t mtlps                        : 1;
	uint32_t ecrces                       : 1;
	uint32_t ures                         : 1;
	uint32_t reserved_21_31               : 11;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg067_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg067_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg067_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg067_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_25_31               : 7;
	uint32_t uatombs                      : 1;  /**< Unsupported AtomicOp Egress Blocked Severity */
	uint32_t reserved_21_23               : 3;
	uint32_t ures                         : 1;  /**< Unsupported Request Error Severity */
	uint32_t ecrces                       : 1;  /**< ECRC Error Severity */
	uint32_t mtlps                        : 1;  /**< Malformed TLP Severity */
	uint32_t ros                          : 1;  /**< Receiver Overflow Severity */
	uint32_t ucs                          : 1;  /**< Unexpected Completion Severity */
	uint32_t cas                          : 1;  /**< Completer Abort Severity */
	uint32_t cts                          : 1;  /**< Completion Timeout Severity */
	uint32_t fcpes                        : 1;  /**< Flow Control Protocol Error Severity */
	uint32_t ptlps                        : 1;  /**< Poisoned TLP Severity */
	uint32_t reserved_6_11                : 6;
	uint32_t sdes                         : 1;  /**< Surprise Down Error Severity (not supported) */
	uint32_t dlpes                        : 1;  /**< Data Link Protocol Error Severity */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dlpes                        : 1;
	uint32_t sdes                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t ptlps                        : 1;
	uint32_t fcpes                        : 1;
	uint32_t cts                          : 1;
	uint32_t cas                          : 1;
	uint32_t ucs                          : 1;
	uint32_t ros                          : 1;
	uint32_t mtlps                        : 1;
	uint32_t ecrces                       : 1;
	uint32_t ures                         : 1;
	uint32_t reserved_21_23               : 3;
	uint32_t uatombs                      : 1;
	uint32_t reserved_25_31               : 7;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg067_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg067_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg067_cn61xx     cn66xx;
	struct cvmx_pciercx_cfg067_cn61xx     cn68xx;
	struct cvmx_pciercx_cfg067_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg067_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg067 cvmx_pciercx_cfg067_t;

/**
 * cvmx_pcierc#_cfg068
 *
 * PCIE_CFG068 = Sixty-ninth 32-bits of PCIE type 1 config space
 * (Correctable Error Status Register)
 */
union cvmx_pciercx_cfg068 {
	uint32_t u32;
	struct cvmx_pciercx_cfg068_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_15_31               : 17;
	uint32_t cies                         : 1;  /**< Corrected Internal Error Status */
	uint32_t anfes                        : 1;  /**< Advisory Non-Fatal Error Status */
	uint32_t rtts                         : 1;  /**< Replay Timer Timeout Status */
	uint32_t reserved_9_11                : 3;
	uint32_t rnrs                         : 1;  /**< REPLAY_NUM Rollover Status */
	uint32_t bdllps                       : 1;  /**< Bad DLLP Status */
	uint32_t btlps                        : 1;  /**< Bad TLP Status */
	uint32_t reserved_1_5                 : 5;
	uint32_t res                          : 1;  /**< Receiver Error Status */
#else
	uint32_t res                          : 1;
	uint32_t reserved_1_5                 : 5;
	uint32_t btlps                        : 1;
	uint32_t bdllps                       : 1;
	uint32_t rnrs                         : 1;
	uint32_t reserved_9_11                : 3;
	uint32_t rtts                         : 1;
	uint32_t anfes                        : 1;
	uint32_t cies                         : 1;
	uint32_t reserved_15_31               : 17;
#endif
	} s;
	struct cvmx_pciercx_cfg068_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t anfes                        : 1;  /**< Advisory Non-Fatal Error Status */
	uint32_t rtts                         : 1;  /**< Replay Timer Timeout Status */
	uint32_t reserved_9_11                : 3;
	uint32_t rnrs                         : 1;  /**< REPLAY_NUM Rollover Status */
	uint32_t bdllps                       : 1;  /**< Bad DLLP Status */
	uint32_t btlps                        : 1;  /**< Bad TLP Status */
	uint32_t reserved_1_5                 : 5;
	uint32_t res                          : 1;  /**< Receiver Error Status */
#else
	uint32_t res                          : 1;
	uint32_t reserved_1_5                 : 5;
	uint32_t btlps                        : 1;
	uint32_t bdllps                       : 1;
	uint32_t rnrs                         : 1;
	uint32_t reserved_9_11                : 3;
	uint32_t rtts                         : 1;
	uint32_t anfes                        : 1;
	uint32_t reserved_14_31               : 18;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg068_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg068_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg068_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg068_cn52xx     cn61xx;
	struct cvmx_pciercx_cfg068_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg068_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg068_cn52xx     cn66xx;
	struct cvmx_pciercx_cfg068_cn52xx     cn68xx;
	struct cvmx_pciercx_cfg068_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg068_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg068 cvmx_pciercx_cfg068_t;

/**
 * cvmx_pcierc#_cfg069
 *
 * PCIE_CFG069 = Seventieth 32-bits of PCIE type 1 config space
 * (Correctable Error Mask Register)
 */
union cvmx_pciercx_cfg069 {
	uint32_t u32;
	struct cvmx_pciercx_cfg069_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_15_31               : 17;
	uint32_t ciem                         : 1;  /**< Corrected Internal Error Mask */
	uint32_t anfem                        : 1;  /**< Advisory Non-Fatal Error Mask */
	uint32_t rttm                         : 1;  /**< Replay Timer Timeout Mask */
	uint32_t reserved_9_11                : 3;
	uint32_t rnrm                         : 1;  /**< REPLAY_NUM Rollover Mask */
	uint32_t bdllpm                       : 1;  /**< Bad DLLP Mask */
	uint32_t btlpm                        : 1;  /**< Bad TLP Mask */
	uint32_t reserved_1_5                 : 5;
	uint32_t rem                          : 1;  /**< Receiver Error Mask */
#else
	uint32_t rem                          : 1;
	uint32_t reserved_1_5                 : 5;
	uint32_t btlpm                        : 1;
	uint32_t bdllpm                       : 1;
	uint32_t rnrm                         : 1;
	uint32_t reserved_9_11                : 3;
	uint32_t rttm                         : 1;
	uint32_t anfem                        : 1;
	uint32_t ciem                         : 1;
	uint32_t reserved_15_31               : 17;
#endif
	} s;
	struct cvmx_pciercx_cfg069_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t anfem                        : 1;  /**< Advisory Non-Fatal Error Mask */
	uint32_t rttm                         : 1;  /**< Replay Timer Timeout Mask */
	uint32_t reserved_9_11                : 3;
	uint32_t rnrm                         : 1;  /**< REPLAY_NUM Rollover Mask */
	uint32_t bdllpm                       : 1;  /**< Bad DLLP Mask */
	uint32_t btlpm                        : 1;  /**< Bad TLP Mask */
	uint32_t reserved_1_5                 : 5;
	uint32_t rem                          : 1;  /**< Receiver Error Mask */
#else
	uint32_t rem                          : 1;
	uint32_t reserved_1_5                 : 5;
	uint32_t btlpm                        : 1;
	uint32_t bdllpm                       : 1;
	uint32_t rnrm                         : 1;
	uint32_t reserved_9_11                : 3;
	uint32_t rttm                         : 1;
	uint32_t anfem                        : 1;
	uint32_t reserved_14_31               : 18;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg069_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg069_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg069_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg069_cn52xx     cn61xx;
	struct cvmx_pciercx_cfg069_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg069_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg069_cn52xx     cn66xx;
	struct cvmx_pciercx_cfg069_cn52xx     cn68xx;
	struct cvmx_pciercx_cfg069_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg069_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg069 cvmx_pciercx_cfg069_t;

/**
 * cvmx_pcierc#_cfg070
 *
 * PCIE_CFG070 = Seventy-first 32-bits of PCIE type 1 config space
 * (Advanced Capabilities and Control Register)
 */
union cvmx_pciercx_cfg070 {
	uint32_t u32;
	struct cvmx_pciercx_cfg070_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_9_31                : 23;
	uint32_t ce                           : 1;  /**< ECRC Check Enable */
	uint32_t cc                           : 1;  /**< ECRC Check Capable */
	uint32_t ge                           : 1;  /**< ECRC Generation Enable */
	uint32_t gc                           : 1;  /**< ECRC Generation Capability */
	uint32_t fep                          : 5;  /**< First Error Pointer */
#else
	uint32_t fep                          : 5;
	uint32_t gc                           : 1;
	uint32_t ge                           : 1;
	uint32_t cc                           : 1;
	uint32_t ce                           : 1;
	uint32_t reserved_9_31                : 23;
#endif
	} s;
	struct cvmx_pciercx_cfg070_s          cn52xx;
	struct cvmx_pciercx_cfg070_s          cn52xxp1;
	struct cvmx_pciercx_cfg070_s          cn56xx;
	struct cvmx_pciercx_cfg070_s          cn56xxp1;
	struct cvmx_pciercx_cfg070_s          cn61xx;
	struct cvmx_pciercx_cfg070_s          cn63xx;
	struct cvmx_pciercx_cfg070_s          cn63xxp1;
	struct cvmx_pciercx_cfg070_s          cn66xx;
	struct cvmx_pciercx_cfg070_s          cn68xx;
	struct cvmx_pciercx_cfg070_s          cn68xxp1;
	struct cvmx_pciercx_cfg070_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg070 cvmx_pciercx_cfg070_t;

/**
 * cvmx_pcierc#_cfg071
 *
 * PCIE_CFG071 = Seventy-second 32-bits of PCIE type 1 config space
 *                  (Header Log Register 1)
 *
 * The Header Log registers collect the header for the TLP corresponding to a detected error.
 */
union cvmx_pciercx_cfg071 {
	uint32_t u32;
	struct cvmx_pciercx_cfg071_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dword1                       : 32; /**< Header Log Register (first DWORD) */
#else
	uint32_t dword1                       : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg071_s          cn52xx;
	struct cvmx_pciercx_cfg071_s          cn52xxp1;
	struct cvmx_pciercx_cfg071_s          cn56xx;
	struct cvmx_pciercx_cfg071_s          cn56xxp1;
	struct cvmx_pciercx_cfg071_s          cn61xx;
	struct cvmx_pciercx_cfg071_s          cn63xx;
	struct cvmx_pciercx_cfg071_s          cn63xxp1;
	struct cvmx_pciercx_cfg071_s          cn66xx;
	struct cvmx_pciercx_cfg071_s          cn68xx;
	struct cvmx_pciercx_cfg071_s          cn68xxp1;
	struct cvmx_pciercx_cfg071_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg071 cvmx_pciercx_cfg071_t;

/**
 * cvmx_pcierc#_cfg072
 *
 * PCIE_CFG072 = Seventy-third 32-bits of PCIE type 1 config space
 *                  (Header Log Register 2)
 *
 * The Header Log registers collect the header for the TLP corresponding to a detected error.
 */
union cvmx_pciercx_cfg072 {
	uint32_t u32;
	struct cvmx_pciercx_cfg072_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dword2                       : 32; /**< Header Log Register (second DWORD) */
#else
	uint32_t dword2                       : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg072_s          cn52xx;
	struct cvmx_pciercx_cfg072_s          cn52xxp1;
	struct cvmx_pciercx_cfg072_s          cn56xx;
	struct cvmx_pciercx_cfg072_s          cn56xxp1;
	struct cvmx_pciercx_cfg072_s          cn61xx;
	struct cvmx_pciercx_cfg072_s          cn63xx;
	struct cvmx_pciercx_cfg072_s          cn63xxp1;
	struct cvmx_pciercx_cfg072_s          cn66xx;
	struct cvmx_pciercx_cfg072_s          cn68xx;
	struct cvmx_pciercx_cfg072_s          cn68xxp1;
	struct cvmx_pciercx_cfg072_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg072 cvmx_pciercx_cfg072_t;

/**
 * cvmx_pcierc#_cfg073
 *
 * PCIE_CFG073 = Seventy-fourth 32-bits of PCIE type 1 config space
 *                  (Header Log Register 3)
 *
 * The Header Log registers collect the header for the TLP corresponding to a detected error.
 */
union cvmx_pciercx_cfg073 {
	uint32_t u32;
	struct cvmx_pciercx_cfg073_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dword3                       : 32; /**< Header Log Register (third DWORD) */
#else
	uint32_t dword3                       : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg073_s          cn52xx;
	struct cvmx_pciercx_cfg073_s          cn52xxp1;
	struct cvmx_pciercx_cfg073_s          cn56xx;
	struct cvmx_pciercx_cfg073_s          cn56xxp1;
	struct cvmx_pciercx_cfg073_s          cn61xx;
	struct cvmx_pciercx_cfg073_s          cn63xx;
	struct cvmx_pciercx_cfg073_s          cn63xxp1;
	struct cvmx_pciercx_cfg073_s          cn66xx;
	struct cvmx_pciercx_cfg073_s          cn68xx;
	struct cvmx_pciercx_cfg073_s          cn68xxp1;
	struct cvmx_pciercx_cfg073_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg073 cvmx_pciercx_cfg073_t;

/**
 * cvmx_pcierc#_cfg074
 *
 * PCIE_CFG074 = Seventy-fifth 32-bits of PCIE type 1 config space
 *                  (Header Log Register 4)
 *
 * The Header Log registers collect the header for the TLP corresponding to a detected error.
 */
union cvmx_pciercx_cfg074 {
	uint32_t u32;
	struct cvmx_pciercx_cfg074_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dword4                       : 32; /**< Header Log Register (fourth DWORD) */
#else
	uint32_t dword4                       : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg074_s          cn52xx;
	struct cvmx_pciercx_cfg074_s          cn52xxp1;
	struct cvmx_pciercx_cfg074_s          cn56xx;
	struct cvmx_pciercx_cfg074_s          cn56xxp1;
	struct cvmx_pciercx_cfg074_s          cn61xx;
	struct cvmx_pciercx_cfg074_s          cn63xx;
	struct cvmx_pciercx_cfg074_s          cn63xxp1;
	struct cvmx_pciercx_cfg074_s          cn66xx;
	struct cvmx_pciercx_cfg074_s          cn68xx;
	struct cvmx_pciercx_cfg074_s          cn68xxp1;
	struct cvmx_pciercx_cfg074_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg074 cvmx_pciercx_cfg074_t;

/**
 * cvmx_pcierc#_cfg075
 *
 * PCIE_CFG075 = Seventy-sixth 32-bits of PCIE type 1 config space
 * (Root Error Command Register)
 */
union cvmx_pciercx_cfg075 {
	uint32_t u32;
	struct cvmx_pciercx_cfg075_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_3_31                : 29;
	uint32_t fere                         : 1;  /**< Fatal Error Reporting Enable */
	uint32_t nfere                        : 1;  /**< Non-Fatal Error Reporting Enable */
	uint32_t cere                         : 1;  /**< Correctable Error Reporting Enable */
#else
	uint32_t cere                         : 1;
	uint32_t nfere                        : 1;
	uint32_t fere                         : 1;
	uint32_t reserved_3_31                : 29;
#endif
	} s;
	struct cvmx_pciercx_cfg075_s          cn52xx;
	struct cvmx_pciercx_cfg075_s          cn52xxp1;
	struct cvmx_pciercx_cfg075_s          cn56xx;
	struct cvmx_pciercx_cfg075_s          cn56xxp1;
	struct cvmx_pciercx_cfg075_s          cn61xx;
	struct cvmx_pciercx_cfg075_s          cn63xx;
	struct cvmx_pciercx_cfg075_s          cn63xxp1;
	struct cvmx_pciercx_cfg075_s          cn66xx;
	struct cvmx_pciercx_cfg075_s          cn68xx;
	struct cvmx_pciercx_cfg075_s          cn68xxp1;
	struct cvmx_pciercx_cfg075_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg075 cvmx_pciercx_cfg075_t;

/**
 * cvmx_pcierc#_cfg076
 *
 * PCIE_CFG076 = Seventy-seventh 32-bits of PCIE type 1 config space
 * (Root Error Status Register)
 */
union cvmx_pciercx_cfg076 {
	uint32_t u32;
	struct cvmx_pciercx_cfg076_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t aeimn                        : 5;  /**< Advanced Error Interrupt Message Number,
                                                         writable through PEM(0..1)_CFG_WR */
	uint32_t reserved_7_26                : 20;
	uint32_t femr                         : 1;  /**< Fatal Error Messages Received */
	uint32_t nfemr                        : 1;  /**< Non-Fatal Error Messages Received */
	uint32_t fuf                          : 1;  /**< First Uncorrectable Fatal */
	uint32_t multi_efnfr                  : 1;  /**< Multiple ERR_FATAL/NONFATAL Received */
	uint32_t efnfr                        : 1;  /**< ERR_FATAL/NONFATAL Received */
	uint32_t multi_ecr                    : 1;  /**< Multiple ERR_COR Received */
	uint32_t ecr                          : 1;  /**< ERR_COR Received */
#else
	uint32_t ecr                          : 1;
	uint32_t multi_ecr                    : 1;
	uint32_t efnfr                        : 1;
	uint32_t multi_efnfr                  : 1;
	uint32_t fuf                          : 1;
	uint32_t nfemr                        : 1;
	uint32_t femr                         : 1;
	uint32_t reserved_7_26                : 20;
	uint32_t aeimn                        : 5;
#endif
	} s;
	struct cvmx_pciercx_cfg076_s          cn52xx;
	struct cvmx_pciercx_cfg076_s          cn52xxp1;
	struct cvmx_pciercx_cfg076_s          cn56xx;
	struct cvmx_pciercx_cfg076_s          cn56xxp1;
	struct cvmx_pciercx_cfg076_s          cn61xx;
	struct cvmx_pciercx_cfg076_s          cn63xx;
	struct cvmx_pciercx_cfg076_s          cn63xxp1;
	struct cvmx_pciercx_cfg076_s          cn66xx;
	struct cvmx_pciercx_cfg076_s          cn68xx;
	struct cvmx_pciercx_cfg076_s          cn68xxp1;
	struct cvmx_pciercx_cfg076_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg076 cvmx_pciercx_cfg076_t;

/**
 * cvmx_pcierc#_cfg077
 *
 * PCIE_CFG077 = Seventy-eighth 32-bits of PCIE type 1 config space
 * (Error Source Identification Register)
 */
union cvmx_pciercx_cfg077 {
	uint32_t u32;
	struct cvmx_pciercx_cfg077_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t efnfsi                       : 16; /**< ERR_FATAL/NONFATAL Source Identification */
	uint32_t ecsi                         : 16; /**< ERR_COR Source Identification */
#else
	uint32_t ecsi                         : 16;
	uint32_t efnfsi                       : 16;
#endif
	} s;
	struct cvmx_pciercx_cfg077_s          cn52xx;
	struct cvmx_pciercx_cfg077_s          cn52xxp1;
	struct cvmx_pciercx_cfg077_s          cn56xx;
	struct cvmx_pciercx_cfg077_s          cn56xxp1;
	struct cvmx_pciercx_cfg077_s          cn61xx;
	struct cvmx_pciercx_cfg077_s          cn63xx;
	struct cvmx_pciercx_cfg077_s          cn63xxp1;
	struct cvmx_pciercx_cfg077_s          cn66xx;
	struct cvmx_pciercx_cfg077_s          cn68xx;
	struct cvmx_pciercx_cfg077_s          cn68xxp1;
	struct cvmx_pciercx_cfg077_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg077 cvmx_pciercx_cfg077_t;

/**
 * cvmx_pcierc#_cfg448
 *
 * PCIE_CFG448 = Four hundred forty-ninth 32-bits of PCIE type 1 config space
 * (Ack Latency Timer and Replay Timer Register)
 */
union cvmx_pciercx_cfg448 {
	uint32_t u32;
	struct cvmx_pciercx_cfg448_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rtl                          : 16; /**< Replay Time Limit
                                                         The replay timer expires when it reaches this limit. The PCI
                                                         Express bus initiates a replay upon reception of a Nak or when
                                                         the replay timer expires.
                                                         This value will be set correctly by the hardware out of reset
                                                         or when the negotiated Link-Width or Payload-Size changes. If
                                                         the user changes this value through a CSR write or by an
                                                         EEPROM load then they should refer to the PCIe Specification
                                                         for the correct value. */
	uint32_t rtltl                        : 16; /**< Round Trip Latency Time Limit
                                                         The Ack/Nak latency timer expires when it reaches this limit.
                                                         This value will be set correctly by the hardware out of reset
                                                         or when the negotiated Link-Width or Payload-Size changes. If
                                                         the user changes this value through a CSR write or by an
                                                         EEPROM load then they should refer to the PCIe Specification
                                                         for the correct value. */
#else
	uint32_t rtltl                        : 16;
	uint32_t rtl                          : 16;
#endif
	} s;
	struct cvmx_pciercx_cfg448_s          cn52xx;
	struct cvmx_pciercx_cfg448_s          cn52xxp1;
	struct cvmx_pciercx_cfg448_s          cn56xx;
	struct cvmx_pciercx_cfg448_s          cn56xxp1;
	struct cvmx_pciercx_cfg448_s          cn61xx;
	struct cvmx_pciercx_cfg448_s          cn63xx;
	struct cvmx_pciercx_cfg448_s          cn63xxp1;
	struct cvmx_pciercx_cfg448_s          cn66xx;
	struct cvmx_pciercx_cfg448_s          cn68xx;
	struct cvmx_pciercx_cfg448_s          cn68xxp1;
	struct cvmx_pciercx_cfg448_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg448 cvmx_pciercx_cfg448_t;

/**
 * cvmx_pcierc#_cfg449
 *
 * PCIE_CFG449 = Four hundred fiftieth 32-bits of PCIE type 1 config space
 * (Other Message Register)
 */
union cvmx_pciercx_cfg449 {
	uint32_t u32;
	struct cvmx_pciercx_cfg449_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t omr                          : 32; /**< Other Message Register
                                                         This register can be used for either of the following purposes:
                                                         o To send a specific PCI Express Message, the application
                                                           writes the payload of the Message into this register, then
                                                           sets bit 0 of the Port Link Control Register to send the
                                                           Message.
                                                         o To store a corruption pattern for corrupting the LCRC on all
                                                           TLPs, the application places a 32-bit corruption pattern into
                                                           this register and enables this function by setting bit 25 of
                                                           the Port Link Control Register. When enabled, the transmit
                                                           LCRC result is XOR'd with this pattern before inserting
                                                           it into the packet. */
#else
	uint32_t omr                          : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg449_s          cn52xx;
	struct cvmx_pciercx_cfg449_s          cn52xxp1;
	struct cvmx_pciercx_cfg449_s          cn56xx;
	struct cvmx_pciercx_cfg449_s          cn56xxp1;
	struct cvmx_pciercx_cfg449_s          cn61xx;
	struct cvmx_pciercx_cfg449_s          cn63xx;
	struct cvmx_pciercx_cfg449_s          cn63xxp1;
	struct cvmx_pciercx_cfg449_s          cn66xx;
	struct cvmx_pciercx_cfg449_s          cn68xx;
	struct cvmx_pciercx_cfg449_s          cn68xxp1;
	struct cvmx_pciercx_cfg449_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg449 cvmx_pciercx_cfg449_t;

/**
 * cvmx_pcierc#_cfg450
 *
 * PCIE_CFG450 = Four hundred fifty-first 32-bits of PCIE type 1 config space
 * (Port Force Link Register)
 */
union cvmx_pciercx_cfg450 {
	uint32_t u32;
	struct cvmx_pciercx_cfg450_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lpec                         : 8;  /**< Low Power Entrance Count
                                                         The Power Management state will wait for this many clock cycles
                                                         for the associated completion of a CfgWr to PCIE_CFG017 register
                                                         Power State (PS) field register to go low-power. This register
                                                         is intended for applications that do not let the PCI Express
                                                         bus handle a completion for configuration request to the
                                                         Power Management Control and Status (PCIE_CFG017) register. */
	uint32_t reserved_22_23               : 2;
	uint32_t link_state                   : 6;  /**< Link State
                                                         The Link state that the PCI Express Bus will be forced to
                                                         when bit 15 (Force Link) is set.
                                                         State encoding:
                                                         o DETECT_QUIET              00h
                                                         o DETECT_ACT                01h
                                                         o POLL_ACTIVE               02h
                                                         o POLL_COMPLIANCE           03h
                                                         o POLL_CONFIG               04h
                                                         o PRE_DETECT_QUIET          05h
                                                         o DETECT_WAIT               06h
                                                         o CFG_LINKWD_START          07h
                                                         o CFG_LINKWD_ACEPT          08h
                                                         o CFG_LANENUM_WAIT          09h
                                                         o CFG_LANENUM_ACEPT         0Ah
                                                         o CFG_COMPLETE              0Bh
                                                         o CFG_IDLE                  0Ch
                                                         o RCVRY_LOCK                0Dh
                                                         o RCVRY_SPEED               0Eh
                                                         o RCVRY_RCVRCFG             0Fh
                                                         o RCVRY_IDLE                10h
                                                         o L0                        11h
                                                         o L0S                       12h
                                                         o L123_SEND_EIDLE           13h
                                                         o L1_IDLE                   14h
                                                         o L2_IDLE                   15h
                                                         o L2_WAKE                   16h
                                                         o DISABLED_ENTRY            17h
                                                         o DISABLED_IDLE             18h
                                                         o DISABLED                  19h
                                                         o LPBK_ENTRY                1Ah
                                                         o LPBK_ACTIVE               1Bh
                                                         o LPBK_EXIT                 1Ch
                                                         o LPBK_EXIT_TIMEOUT         1Dh
                                                         o HOT_RESET_ENTRY           1Eh
                                                         o HOT_RESET                 1Fh */
	uint32_t force_link                   : 1;  /**< Force Link
                                                         Forces the Link to the state specified by the Link State field.
                                                         The Force Link pulse will trigger Link re-negotiation.
                                                         * As the The Force Link is a pulse, writing a 1 to it does
                                                           trigger the forced link state event, even thought reading it
                                                           always returns a 0. */
	uint32_t reserved_8_14                : 7;
	uint32_t link_num                     : 8;  /**< Link Number */
#else
	uint32_t link_num                     : 8;
	uint32_t reserved_8_14                : 7;
	uint32_t force_link                   : 1;
	uint32_t link_state                   : 6;
	uint32_t reserved_22_23               : 2;
	uint32_t lpec                         : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg450_s          cn52xx;
	struct cvmx_pciercx_cfg450_s          cn52xxp1;
	struct cvmx_pciercx_cfg450_s          cn56xx;
	struct cvmx_pciercx_cfg450_s          cn56xxp1;
	struct cvmx_pciercx_cfg450_s          cn61xx;
	struct cvmx_pciercx_cfg450_s          cn63xx;
	struct cvmx_pciercx_cfg450_s          cn63xxp1;
	struct cvmx_pciercx_cfg450_s          cn66xx;
	struct cvmx_pciercx_cfg450_s          cn68xx;
	struct cvmx_pciercx_cfg450_s          cn68xxp1;
	struct cvmx_pciercx_cfg450_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg450 cvmx_pciercx_cfg450_t;

/**
 * cvmx_pcierc#_cfg451
 *
 * PCIE_CFG451 = Four hundred fifty-second 32-bits of PCIE type 1 config space
 * (Ack Frequency Register)
 */
union cvmx_pciercx_cfg451 {
	uint32_t u32;
	struct cvmx_pciercx_cfg451_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_31_31               : 1;
	uint32_t easpml1                      : 1;  /**< Enter ASPM L1 without receive in L0s
                                                         Allow core to enter ASPM L1 even when link partner did
                                                         not go to L0s (receive is not in L0s).
                                                         When not set, core goes to ASPM L1 only after idle period
                                                         during which both receive and transmit are in L0s. */
	uint32_t l1el                         : 3;  /**< L1 Entrance Latency
                                                         Values correspond to:
                                                         o 000: 1 ms
                                                         o 001: 2 ms
                                                         o 010: 4 ms
                                                         o 011: 8 ms
                                                         o 100: 16 ms
                                                         o 101: 32 ms
                                                         o 110 or 111: 64 ms */
	uint32_t l0el                         : 3;  /**< L0s Entrance Latency
                                                         Values correspond to:
                                                         o 000: 1 ms
                                                         o 001: 2 ms
                                                         o 010: 3 ms
                                                         o 011: 4 ms
                                                         o 100: 5 ms
                                                         o 101: 6 ms
                                                         o 110 or 111: 7 ms */
	uint32_t n_fts_cc                     : 8;  /**< N_FTS when common clock is used.
                                                         The number of Fast Training Sequence ordered sets to be
                                                         transmitted when transitioning from L0s to L0. The maximum
                                                         number of FTS ordered-sets that a component can request is 255.
                                                          Note: The core does not support a value of zero; a value of
                                                                zero can cause the LTSSM to go into the recovery state
                                                                when exiting from L0s. */
	uint32_t n_fts                        : 8;  /**< N_FTS
                                                         The number of Fast Training Sequence ordered sets to be
                                                         transmitted when transitioning from L0s to L0. The maximum
                                                         number of FTS ordered-sets that a component can request is 255.
                                                         Note: The core does not support a value of zero; a value of
                                                               zero can cause the LTSSM to go into the recovery state
                                                               when exiting from L0s. */
	uint32_t ack_freq                     : 8;  /**< Ack Frequency
                                                         The number of pending Ack's specified here (up to 255) before
                                                         sending an Ack. */
#else
	uint32_t ack_freq                     : 8;
	uint32_t n_fts                        : 8;
	uint32_t n_fts_cc                     : 8;
	uint32_t l0el                         : 3;
	uint32_t l1el                         : 3;
	uint32_t easpml1                      : 1;
	uint32_t reserved_31_31               : 1;
#endif
	} s;
	struct cvmx_pciercx_cfg451_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_30_31               : 2;
	uint32_t l1el                         : 3;  /**< L1 Entrance Latency
                                                         Values correspond to:
                                                         o 000: 1 ms
                                                         o 001: 2 ms
                                                         o 010: 4 ms
                                                         o 011: 8 ms
                                                         o 100: 16 ms
                                                         o 101: 32 ms
                                                         o 110 or 111: 64 ms */
	uint32_t l0el                         : 3;  /**< L0s Entrance Latency
                                                         Values correspond to:
                                                         o 000: 1 ms
                                                         o 001: 2 ms
                                                         o 010: 3 ms
                                                         o 011: 4 ms
                                                         o 100: 5 ms
                                                         o 101: 6 ms
                                                         o 110 or 111: 7 ms */
	uint32_t n_fts_cc                     : 8;  /**< N_FTS when common clock is used.
                                                         The number of Fast Training Sequence ordered sets to be
                                                         transmitted when transitioning from L0s to L0. The maximum
                                                         number of FTS ordered-sets that a component can request is 255.
                                                          Note: The core does not support a value of zero; a value of
                                                                zero can cause the LTSSM to go into the recovery state
                                                                when exiting from L0s. */
	uint32_t n_fts                        : 8;  /**< N_FTS
                                                         The number of Fast Training Sequence ordered sets to be
                                                         transmitted when transitioning from L0s to L0. The maximum
                                                         number of FTS ordered-sets that a component can request is 255.
                                                         Note: The core does not support a value of zero; a value of
                                                               zero can cause the LTSSM to go into the recovery state
                                                               when exiting from L0s. */
	uint32_t ack_freq                     : 8;  /**< Ack Frequency
                                                         The number of pending Ack's specified here (up to 255) before
                                                         sending an Ack. */
#else
	uint32_t ack_freq                     : 8;
	uint32_t n_fts                        : 8;
	uint32_t n_fts_cc                     : 8;
	uint32_t l0el                         : 3;
	uint32_t l1el                         : 3;
	uint32_t reserved_30_31               : 2;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg451_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg451_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg451_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg451_s          cn61xx;
	struct cvmx_pciercx_cfg451_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg451_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg451_s          cn66xx;
	struct cvmx_pciercx_cfg451_s          cn68xx;
	struct cvmx_pciercx_cfg451_s          cn68xxp1;
	struct cvmx_pciercx_cfg451_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg451 cvmx_pciercx_cfg451_t;

/**
 * cvmx_pcierc#_cfg452
 *
 * PCIE_CFG452 = Four hundred fifty-third 32-bits of PCIE type 1 config space
 * (Port Link Control Register)
 */
union cvmx_pciercx_cfg452 {
	uint32_t u32;
	struct cvmx_pciercx_cfg452_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_26_31               : 6;
	uint32_t eccrc                        : 1;  /**< Enable Corrupted CRC
                                                         Causes corrupt LCRC for TLPs when set,
                                                         using the pattern contained in the Other Message register.
                                                         This is a test feature, not to be used in normal operation. */
	uint32_t reserved_22_24               : 3;
	uint32_t lme                          : 6;  /**< Link Mode Enable
                                                         o 000001: x1
                                                         o 000011: x2
                                                         o 000111: x4  (not supported)
                                                         o 001111: x8  (not supported)
                                                         o 011111: x16 (not supported)
                                                         o 111111: x32 (not supported)
                                                         This field indicates the MAXIMUM number of lanes supported
                                                         by the PCIe port. The value can be set less than 0x3
                                                         to limit the number of lanes the PCIe will attempt to use.
                                                         The programming of this field needs to be done by SW BEFORE
                                                         enabling the link. See also MLW.
                                                         (Note: The value of this field does NOT indicate the number
                                                          of lanes in use by the PCIe. LME sets the max number of lanes
                                                          in the PCIe core that COULD be used. As per the PCIe specs,
                                                          the PCIe core can negotiate a smaller link width, so
                                                          x1 is also supported when LME=0x3, for example.) */
	uint32_t reserved_8_15                : 8;
	uint32_t flm                          : 1;  /**< Fast Link Mode
                                                         Sets all internal timers to fast mode for simulation purposes. */
	uint32_t reserved_6_6                 : 1;
	uint32_t dllle                        : 1;  /**< DLL Link Enable
                                                         Enables Link initialization. If DLL Link Enable = 0, the PCI
                                                         Express bus does not transmit InitFC DLLPs and does not
                                                         establish a Link. */
	uint32_t reserved_4_4                 : 1;
	uint32_t ra                           : 1;  /**< Reset Assert
                                                         Triggers a recovery and forces the LTSSM to the Hot Reset
                                                         state (downstream port only). */
	uint32_t le                           : 1;  /**< Loopback Enable
                                                         Initiate loopback mode as a master. On a 0->1 transition,
                                                         the PCIe core sends TS ordered sets with the loopback bit set
                                                         to cause the link partner to enter into loopback mode as a
                                                         slave. Normal transmission is not possible when LE=1. To exit
                                                         loopback mode, take the link through a reset sequence. */
	uint32_t sd                           : 1;  /**< Scramble Disable
                                                         Turns off data scrambling. */
	uint32_t omr                          : 1;  /**< Other Message Request
                                                         When software writes a `1' to this bit, the PCI Express bus
                                                         transmits the Message contained in the Other Message register. */
#else
	uint32_t omr                          : 1;
	uint32_t sd                           : 1;
	uint32_t le                           : 1;
	uint32_t ra                           : 1;
	uint32_t reserved_4_4                 : 1;
	uint32_t dllle                        : 1;
	uint32_t reserved_6_6                 : 1;
	uint32_t flm                          : 1;
	uint32_t reserved_8_15                : 8;
	uint32_t lme                          : 6;
	uint32_t reserved_22_24               : 3;
	uint32_t eccrc                        : 1;
	uint32_t reserved_26_31               : 6;
#endif
	} s;
	struct cvmx_pciercx_cfg452_s          cn52xx;
	struct cvmx_pciercx_cfg452_s          cn52xxp1;
	struct cvmx_pciercx_cfg452_s          cn56xx;
	struct cvmx_pciercx_cfg452_s          cn56xxp1;
	struct cvmx_pciercx_cfg452_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_22_31               : 10;
	uint32_t lme                          : 6;  /**< Link Mode Enable
                                                         o 000001: x1
                                                         o 000011: x2
                                                         o 000111: x4
                                                         o 001111: x8  (not supported)
                                                         o 011111: x16 (not supported)
                                                         o 111111: x32 (not supported)
                                                         This field indicates the MAXIMUM number of lanes supported
                                                         by the PCIe port. The value can be set less than 0x7
                                                         to limit the number of lanes the PCIe will attempt to use.
                                                         The programming of this field needs to be done by SW BEFORE
                                                         enabling the link. See also MLW.
                                                         (Note: The value of this field does NOT indicate the number
                                                          of lanes in use by the PCIe. LME sets the max number of lanes
                                                          in the PCIe core that COULD be used. As per the PCIe specs,
                                                          the PCIe core can negotiate a smaller link width, so all
                                                          of x4, x2, and x1 are supported when LME=0x7,
                                                          for example.) */
	uint32_t reserved_8_15                : 8;
	uint32_t flm                          : 1;  /**< Fast Link Mode
                                                         Sets all internal timers to fast mode for simulation purposes. */
	uint32_t reserved_6_6                 : 1;
	uint32_t dllle                        : 1;  /**< DLL Link Enable
                                                         Enables Link initialization. If DLL Link Enable = 0, the PCI
                                                         Express bus does not transmit InitFC DLLPs and does not
                                                         establish a Link. */
	uint32_t reserved_4_4                 : 1;
	uint32_t ra                           : 1;  /**< Reset Assert
                                                         Triggers a recovery and forces the LTSSM to the Hot Reset
                                                         state (downstream port only). */
	uint32_t le                           : 1;  /**< Loopback Enable
                                                         Initiate loopback mode as a master. On a 0->1 transition,
                                                         the PCIe core sends TS ordered sets with the loopback bit set
                                                         to cause the link partner to enter into loopback mode as a
                                                         slave. Normal transmission is not possible when LE=1. To exit
                                                         loopback mode, take the link through a reset sequence. */
	uint32_t sd                           : 1;  /**< Scramble Disable
                                                         Turns off data scrambling. */
	uint32_t omr                          : 1;  /**< Other Message Request
                                                         When software writes a `1' to this bit, the PCI Express bus
                                                         transmits the Message contained in the Other Message register. */
#else
	uint32_t omr                          : 1;
	uint32_t sd                           : 1;
	uint32_t le                           : 1;
	uint32_t ra                           : 1;
	uint32_t reserved_4_4                 : 1;
	uint32_t dllle                        : 1;
	uint32_t reserved_6_6                 : 1;
	uint32_t flm                          : 1;
	uint32_t reserved_8_15                : 8;
	uint32_t lme                          : 6;
	uint32_t reserved_22_31               : 10;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg452_s          cn63xx;
	struct cvmx_pciercx_cfg452_s          cn63xxp1;
	struct cvmx_pciercx_cfg452_cn61xx     cn66xx;
	struct cvmx_pciercx_cfg452_cn61xx     cn68xx;
	struct cvmx_pciercx_cfg452_cn61xx     cn68xxp1;
	struct cvmx_pciercx_cfg452_cn61xx     cnf71xx;
};
typedef union cvmx_pciercx_cfg452 cvmx_pciercx_cfg452_t;

/**
 * cvmx_pcierc#_cfg453
 *
 * PCIE_CFG453 = Four hundred fifty-fourth 32-bits of PCIE type 1 config space
 * (Lane Skew Register)
 */
union cvmx_pciercx_cfg453 {
	uint32_t u32;
	struct cvmx_pciercx_cfg453_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dlld                         : 1;  /**< Disable Lane-to-Lane Deskew
                                                         Disables the internal Lane-to-Lane deskew logic. */
	uint32_t reserved_26_30               : 5;
	uint32_t ack_nak                      : 1;  /**< Ack/Nak Disable
                                                         Prevents the PCI Express bus from sending Ack and Nak DLLPs. */
	uint32_t fcd                          : 1;  /**< Flow Control Disable
                                                         Prevents the PCI Express bus from sending FC DLLPs. */
	uint32_t ilst                         : 24; /**< Insert Lane Skew for Transmit (not supported for x16)
                                                         Causes skew between lanes for test purposes. There are three
                                                         bits per Lane. The value is in units of one symbol time. For
                                                         example, the value 010b for a Lane forces a skew of two symbol
                                                         times for that Lane. The maximum skew value for any Lane is 5
                                                         symbol times. */
#else
	uint32_t ilst                         : 24;
	uint32_t fcd                          : 1;
	uint32_t ack_nak                      : 1;
	uint32_t reserved_26_30               : 5;
	uint32_t dlld                         : 1;
#endif
	} s;
	struct cvmx_pciercx_cfg453_s          cn52xx;
	struct cvmx_pciercx_cfg453_s          cn52xxp1;
	struct cvmx_pciercx_cfg453_s          cn56xx;
	struct cvmx_pciercx_cfg453_s          cn56xxp1;
	struct cvmx_pciercx_cfg453_s          cn61xx;
	struct cvmx_pciercx_cfg453_s          cn63xx;
	struct cvmx_pciercx_cfg453_s          cn63xxp1;
	struct cvmx_pciercx_cfg453_s          cn66xx;
	struct cvmx_pciercx_cfg453_s          cn68xx;
	struct cvmx_pciercx_cfg453_s          cn68xxp1;
	struct cvmx_pciercx_cfg453_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg453 cvmx_pciercx_cfg453_t;

/**
 * cvmx_pcierc#_cfg454
 *
 * PCIE_CFG454 = Four hundred fifty-fifth 32-bits of PCIE type 1 config space
 * (Symbol Number Register)
 */
union cvmx_pciercx_cfg454 {
	uint32_t u32;
	struct cvmx_pciercx_cfg454_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cx_nfunc                     : 3;  /**< Number of Functions (minus 1)
                                                         Configuration Requests targeted at function numbers above this
                                                         value will be returned with unsupported request */
	uint32_t tmfcwt                       : 5;  /**< Timer Modifier for Flow Control Watchdog Timer
                                                         Increases the timer value for the Flow Control watchdog timer,
                                                         in increments of 16 clock cycles. */
	uint32_t tmanlt                       : 5;  /**< Timer Modifier for Ack/Nak Latency Timer
                                                         Increases the timer value for the Ack/Nak latency timer, in
                                                         increments of 64 clock cycles. */
	uint32_t tmrt                         : 5;  /**< Timer Modifier for Replay Timer
                                                         Increases the timer value for the replay timer, in increments
                                                         of 64 clock cycles. */
	uint32_t reserved_11_13               : 3;
	uint32_t nskps                        : 3;  /**< Number of SKP Symbols */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t nskps                        : 3;
	uint32_t reserved_11_13               : 3;
	uint32_t tmrt                         : 5;
	uint32_t tmanlt                       : 5;
	uint32_t tmfcwt                       : 5;
	uint32_t cx_nfunc                     : 3;
#endif
	} s;
	struct cvmx_pciercx_cfg454_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_29_31               : 3;
	uint32_t tmfcwt                       : 5;  /**< Timer Modifier for Flow Control Watchdog Timer
                                                         Increases the timer value for the Flow Control watchdog timer,
                                                         in increments of 16 clock cycles. */
	uint32_t tmanlt                       : 5;  /**< Timer Modifier for Ack/Nak Latency Timer
                                                         Increases the timer value for the Ack/Nak latency timer, in
                                                         increments of 64 clock cycles. */
	uint32_t tmrt                         : 5;  /**< Timer Modifier for Replay Timer
                                                         Increases the timer value for the replay timer, in increments
                                                         of 64 clock cycles. */
	uint32_t reserved_11_13               : 3;
	uint32_t nskps                        : 3;  /**< Number of SKP Symbols */
	uint32_t reserved_4_7                 : 4;
	uint32_t ntss                         : 4;  /**< Number of TS Symbols
                                                         Sets the number of TS identifier symbols that are sent in TS1
                                                         and TS2 ordered sets. */
#else
	uint32_t ntss                         : 4;
	uint32_t reserved_4_7                 : 4;
	uint32_t nskps                        : 3;
	uint32_t reserved_11_13               : 3;
	uint32_t tmrt                         : 5;
	uint32_t tmanlt                       : 5;
	uint32_t tmfcwt                       : 5;
	uint32_t reserved_29_31               : 3;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg454_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg454_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg454_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg454_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cx_nfunc                     : 3;  /**< Number of Functions (minus 1)
                                                         Configuration Requests targeted at function numbers above this
                                                         value will be returned with unsupported request */
	uint32_t tmfcwt                       : 5;  /**< Timer Modifier for Flow Control Watchdog Timer
                                                         Increases the timer value for the Flow Control watchdog timer,
                                                         in increments of 16 clock cycles. */
	uint32_t tmanlt                       : 5;  /**< Timer Modifier for Ack/Nak Latency Timer
                                                         Increases the timer value for the Ack/Nak latency timer, in
                                                         increments of 64 clock cycles. */
	uint32_t tmrt                         : 5;  /**< Timer Modifier for Replay Timer
                                                         Increases the timer value for the replay timer, in increments
                                                         of 64 clock cycles. */
	uint32_t reserved_8_13                : 6;
	uint32_t mfuncn                       : 8;  /**< Max Number of Functions Supported */
#else
	uint32_t mfuncn                       : 8;
	uint32_t reserved_8_13                : 6;
	uint32_t tmrt                         : 5;
	uint32_t tmanlt                       : 5;
	uint32_t tmfcwt                       : 5;
	uint32_t cx_nfunc                     : 3;
#endif
	} cn61xx;
	struct cvmx_pciercx_cfg454_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg454_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg454_cn61xx     cn66xx;
	struct cvmx_pciercx_cfg454_cn61xx     cn68xx;
	struct cvmx_pciercx_cfg454_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg454_cn61xx     cnf71xx;
};
typedef union cvmx_pciercx_cfg454 cvmx_pciercx_cfg454_t;

/**
 * cvmx_pcierc#_cfg455
 *
 * PCIE_CFG455 = Four hundred fifty-sixth 32-bits of PCIE type 1 config space
 * (Symbol Timer Register/Filter Mask Register 1)
 */
union cvmx_pciercx_cfg455 {
	uint32_t u32;
	struct cvmx_pciercx_cfg455_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t m_cfg0_filt                  : 1;  /**< Mask filtering of received Configuration Requests (RC mode only) */
	uint32_t m_io_filt                    : 1;  /**< Mask filtering of received I/O Requests (RC mode only) */
	uint32_t msg_ctrl                     : 1;  /**< Message Control
                                                         The application must not change this field. */
	uint32_t m_cpl_ecrc_filt              : 1;  /**< Mask ECRC error filtering for Completions */
	uint32_t m_ecrc_filt                  : 1;  /**< Mask ECRC error filtering */
	uint32_t m_cpl_len_err                : 1;  /**< Mask Length mismatch error for received Completions */
	uint32_t m_cpl_attr_err               : 1;  /**< Mask Attributes mismatch error for received Completions */
	uint32_t m_cpl_tc_err                 : 1;  /**< Mask Traffic Class mismatch error for received Completions */
	uint32_t m_cpl_fun_err                : 1;  /**< Mask function mismatch error for received Completions */
	uint32_t m_cpl_rid_err                : 1;  /**< Mask Requester ID mismatch error for received Completions */
	uint32_t m_cpl_tag_err                : 1;  /**< Mask Tag error rules for received Completions */
	uint32_t m_lk_filt                    : 1;  /**< Mask Locked Request filtering */
	uint32_t m_cfg1_filt                  : 1;  /**< Mask Type 1 Configuration Request filtering */
	uint32_t m_bar_match                  : 1;  /**< Mask BAR match filtering */
	uint32_t m_pois_filt                  : 1;  /**< Mask poisoned TLP filtering */
	uint32_t m_fun                        : 1;  /**< Mask function */
	uint32_t dfcwt                        : 1;  /**< Disable FC Watchdog Timer */
	uint32_t reserved_11_14               : 4;
	uint32_t skpiv                        : 11; /**< SKP Interval Value */
#else
	uint32_t skpiv                        : 11;
	uint32_t reserved_11_14               : 4;
	uint32_t dfcwt                        : 1;
	uint32_t m_fun                        : 1;
	uint32_t m_pois_filt                  : 1;
	uint32_t m_bar_match                  : 1;
	uint32_t m_cfg1_filt                  : 1;
	uint32_t m_lk_filt                    : 1;
	uint32_t m_cpl_tag_err                : 1;
	uint32_t m_cpl_rid_err                : 1;
	uint32_t m_cpl_fun_err                : 1;
	uint32_t m_cpl_tc_err                 : 1;
	uint32_t m_cpl_attr_err               : 1;
	uint32_t m_cpl_len_err                : 1;
	uint32_t m_ecrc_filt                  : 1;
	uint32_t m_cpl_ecrc_filt              : 1;
	uint32_t msg_ctrl                     : 1;
	uint32_t m_io_filt                    : 1;
	uint32_t m_cfg0_filt                  : 1;
#endif
	} s;
	struct cvmx_pciercx_cfg455_s          cn52xx;
	struct cvmx_pciercx_cfg455_s          cn52xxp1;
	struct cvmx_pciercx_cfg455_s          cn56xx;
	struct cvmx_pciercx_cfg455_s          cn56xxp1;
	struct cvmx_pciercx_cfg455_s          cn61xx;
	struct cvmx_pciercx_cfg455_s          cn63xx;
	struct cvmx_pciercx_cfg455_s          cn63xxp1;
	struct cvmx_pciercx_cfg455_s          cn66xx;
	struct cvmx_pciercx_cfg455_s          cn68xx;
	struct cvmx_pciercx_cfg455_s          cn68xxp1;
	struct cvmx_pciercx_cfg455_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg455 cvmx_pciercx_cfg455_t;

/**
 * cvmx_pcierc#_cfg456
 *
 * PCIE_CFG456 = Four hundred fifty-seventh 32-bits of PCIE type 1 config space
 * (Filter Mask Register 2)
 */
union cvmx_pciercx_cfg456 {
	uint32_t u32;
	struct cvmx_pciercx_cfg456_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_4_31                : 28;
	uint32_t m_handle_flush               : 1;  /**< Mask Core Filter to handle flush request */
	uint32_t m_dabort_4ucpl               : 1;  /**< Mask DLLP abort for unexpected CPL */
	uint32_t m_vend1_drp                  : 1;  /**< Mask Vendor MSG Type 1 dropped silently */
	uint32_t m_vend0_drp                  : 1;  /**< Mask Vendor MSG Type 0 dropped with UR error reporting. */
#else
	uint32_t m_vend0_drp                  : 1;
	uint32_t m_vend1_drp                  : 1;
	uint32_t m_dabort_4ucpl               : 1;
	uint32_t m_handle_flush               : 1;
	uint32_t reserved_4_31                : 28;
#endif
	} s;
	struct cvmx_pciercx_cfg456_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_2_31                : 30;
	uint32_t m_vend1_drp                  : 1;  /**< Mask Vendor MSG Type 1 dropped silently */
	uint32_t m_vend0_drp                  : 1;  /**< Mask Vendor MSG Type 0 dropped with UR error reporting. */
#else
	uint32_t m_vend0_drp                  : 1;
	uint32_t m_vend1_drp                  : 1;
	uint32_t reserved_2_31                : 30;
#endif
	} cn52xx;
	struct cvmx_pciercx_cfg456_cn52xx     cn52xxp1;
	struct cvmx_pciercx_cfg456_cn52xx     cn56xx;
	struct cvmx_pciercx_cfg456_cn52xx     cn56xxp1;
	struct cvmx_pciercx_cfg456_s          cn61xx;
	struct cvmx_pciercx_cfg456_cn52xx     cn63xx;
	struct cvmx_pciercx_cfg456_cn52xx     cn63xxp1;
	struct cvmx_pciercx_cfg456_s          cn66xx;
	struct cvmx_pciercx_cfg456_s          cn68xx;
	struct cvmx_pciercx_cfg456_cn52xx     cn68xxp1;
	struct cvmx_pciercx_cfg456_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg456 cvmx_pciercx_cfg456_t;

/**
 * cvmx_pcierc#_cfg458
 *
 * PCIE_CFG458 = Four hundred fifty-ninth 32-bits of PCIE type 1 config space
 * (Debug Register 0)
 */
union cvmx_pciercx_cfg458 {
	uint32_t u32;
	struct cvmx_pciercx_cfg458_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dbg_info_l32                 : 32; /**< The value on cxpl_debug_info[31:0]. */
#else
	uint32_t dbg_info_l32                 : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg458_s          cn52xx;
	struct cvmx_pciercx_cfg458_s          cn52xxp1;
	struct cvmx_pciercx_cfg458_s          cn56xx;
	struct cvmx_pciercx_cfg458_s          cn56xxp1;
	struct cvmx_pciercx_cfg458_s          cn61xx;
	struct cvmx_pciercx_cfg458_s          cn63xx;
	struct cvmx_pciercx_cfg458_s          cn63xxp1;
	struct cvmx_pciercx_cfg458_s          cn66xx;
	struct cvmx_pciercx_cfg458_s          cn68xx;
	struct cvmx_pciercx_cfg458_s          cn68xxp1;
	struct cvmx_pciercx_cfg458_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg458 cvmx_pciercx_cfg458_t;

/**
 * cvmx_pcierc#_cfg459
 *
 * PCIE_CFG459 = Four hundred sixtieth 32-bits of PCIE type 1 config space
 * (Debug Register 1)
 */
union cvmx_pciercx_cfg459 {
	uint32_t u32;
	struct cvmx_pciercx_cfg459_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dbg_info_u32                 : 32; /**< The value on cxpl_debug_info[63:32]. */
#else
	uint32_t dbg_info_u32                 : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg459_s          cn52xx;
	struct cvmx_pciercx_cfg459_s          cn52xxp1;
	struct cvmx_pciercx_cfg459_s          cn56xx;
	struct cvmx_pciercx_cfg459_s          cn56xxp1;
	struct cvmx_pciercx_cfg459_s          cn61xx;
	struct cvmx_pciercx_cfg459_s          cn63xx;
	struct cvmx_pciercx_cfg459_s          cn63xxp1;
	struct cvmx_pciercx_cfg459_s          cn66xx;
	struct cvmx_pciercx_cfg459_s          cn68xx;
	struct cvmx_pciercx_cfg459_s          cn68xxp1;
	struct cvmx_pciercx_cfg459_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg459 cvmx_pciercx_cfg459_t;

/**
 * cvmx_pcierc#_cfg460
 *
 * PCIE_CFG460 = Four hundred sixty-first 32-bits of PCIE type 1 config space
 * (Transmit Posted FC Credit Status)
 */
union cvmx_pciercx_cfg460 {
	uint32_t u32;
	struct cvmx_pciercx_cfg460_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t tphfcc                       : 8;  /**< Transmit Posted Header FC Credits
                                                         The Posted Header credits advertised by the receiver at the
                                                         other end of the Link, updated with each UpdateFC DLLP. */
	uint32_t tpdfcc                       : 12; /**< Transmit Posted Data FC Credits
                                                         The Posted Data credits advertised by the receiver at the other
                                                         end of the Link, updated with each UpdateFC DLLP. */
#else
	uint32_t tpdfcc                       : 12;
	uint32_t tphfcc                       : 8;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg460_s          cn52xx;
	struct cvmx_pciercx_cfg460_s          cn52xxp1;
	struct cvmx_pciercx_cfg460_s          cn56xx;
	struct cvmx_pciercx_cfg460_s          cn56xxp1;
	struct cvmx_pciercx_cfg460_s          cn61xx;
	struct cvmx_pciercx_cfg460_s          cn63xx;
	struct cvmx_pciercx_cfg460_s          cn63xxp1;
	struct cvmx_pciercx_cfg460_s          cn66xx;
	struct cvmx_pciercx_cfg460_s          cn68xx;
	struct cvmx_pciercx_cfg460_s          cn68xxp1;
	struct cvmx_pciercx_cfg460_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg460 cvmx_pciercx_cfg460_t;

/**
 * cvmx_pcierc#_cfg461
 *
 * PCIE_CFG461 = Four hundred sixty-second 32-bits of PCIE type 1 config space
 * (Transmit Non-Posted FC Credit Status)
 */
union cvmx_pciercx_cfg461 {
	uint32_t u32;
	struct cvmx_pciercx_cfg461_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t tchfcc                       : 8;  /**< Transmit Non-Posted Header FC Credits
                                                         The Non-Posted Header credits advertised by the receiver at the
                                                         other end of the Link, updated with each UpdateFC DLLP. */
	uint32_t tcdfcc                       : 12; /**< Transmit Non-Posted Data FC Credits
                                                         The Non-Posted Data credits advertised by the receiver at the
                                                         other end of the Link, updated with each UpdateFC DLLP. */
#else
	uint32_t tcdfcc                       : 12;
	uint32_t tchfcc                       : 8;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg461_s          cn52xx;
	struct cvmx_pciercx_cfg461_s          cn52xxp1;
	struct cvmx_pciercx_cfg461_s          cn56xx;
	struct cvmx_pciercx_cfg461_s          cn56xxp1;
	struct cvmx_pciercx_cfg461_s          cn61xx;
	struct cvmx_pciercx_cfg461_s          cn63xx;
	struct cvmx_pciercx_cfg461_s          cn63xxp1;
	struct cvmx_pciercx_cfg461_s          cn66xx;
	struct cvmx_pciercx_cfg461_s          cn68xx;
	struct cvmx_pciercx_cfg461_s          cn68xxp1;
	struct cvmx_pciercx_cfg461_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg461 cvmx_pciercx_cfg461_t;

/**
 * cvmx_pcierc#_cfg462
 *
 * PCIE_CFG462 = Four hundred sixty-third 32-bits of PCIE type 1 config space
 * (Transmit Completion FC Credit Status )
 */
union cvmx_pciercx_cfg462 {
	uint32_t u32;
	struct cvmx_pciercx_cfg462_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_20_31               : 12;
	uint32_t tchfcc                       : 8;  /**< Transmit Completion Header FC Credits
                                                         The Completion Header credits advertised by the receiver at the
                                                         other end of the Link, updated with each UpdateFC DLLP. */
	uint32_t tcdfcc                       : 12; /**< Transmit Completion Data FC Credits
                                                         The Completion Data credits advertised by the receiver at the
                                                         other end of the Link, updated with each UpdateFC DLLP. */
#else
	uint32_t tcdfcc                       : 12;
	uint32_t tchfcc                       : 8;
	uint32_t reserved_20_31               : 12;
#endif
	} s;
	struct cvmx_pciercx_cfg462_s          cn52xx;
	struct cvmx_pciercx_cfg462_s          cn52xxp1;
	struct cvmx_pciercx_cfg462_s          cn56xx;
	struct cvmx_pciercx_cfg462_s          cn56xxp1;
	struct cvmx_pciercx_cfg462_s          cn61xx;
	struct cvmx_pciercx_cfg462_s          cn63xx;
	struct cvmx_pciercx_cfg462_s          cn63xxp1;
	struct cvmx_pciercx_cfg462_s          cn66xx;
	struct cvmx_pciercx_cfg462_s          cn68xx;
	struct cvmx_pciercx_cfg462_s          cn68xxp1;
	struct cvmx_pciercx_cfg462_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg462 cvmx_pciercx_cfg462_t;

/**
 * cvmx_pcierc#_cfg463
 *
 * PCIE_CFG463 = Four hundred sixty-fourth 32-bits of PCIE type 1 config space
 * (Queue Status)
 */
union cvmx_pciercx_cfg463 {
	uint32_t u32;
	struct cvmx_pciercx_cfg463_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_3_31                : 29;
	uint32_t rqne                         : 1;  /**< Received Queue Not Empty
                                                         Indicates there is data in one or more of the receive buffers. */
	uint32_t trbne                        : 1;  /**< Transmit Retry Buffer Not Empty
                                                         Indicates that there is data in the transmit retry buffer. */
	uint32_t rtlpfccnr                    : 1;  /**< Received TLP FC Credits Not Returned
                                                         Indicates that the PCI Express bus has sent a TLP but has not
                                                         yet received an UpdateFC DLLP indicating that the credits for
                                                         that TLP have been restored by the receiver at the other end of
                                                         the Link. */
#else
	uint32_t rtlpfccnr                    : 1;
	uint32_t trbne                        : 1;
	uint32_t rqne                         : 1;
	uint32_t reserved_3_31                : 29;
#endif
	} s;
	struct cvmx_pciercx_cfg463_s          cn52xx;
	struct cvmx_pciercx_cfg463_s          cn52xxp1;
	struct cvmx_pciercx_cfg463_s          cn56xx;
	struct cvmx_pciercx_cfg463_s          cn56xxp1;
	struct cvmx_pciercx_cfg463_s          cn61xx;
	struct cvmx_pciercx_cfg463_s          cn63xx;
	struct cvmx_pciercx_cfg463_s          cn63xxp1;
	struct cvmx_pciercx_cfg463_s          cn66xx;
	struct cvmx_pciercx_cfg463_s          cn68xx;
	struct cvmx_pciercx_cfg463_s          cn68xxp1;
	struct cvmx_pciercx_cfg463_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg463 cvmx_pciercx_cfg463_t;

/**
 * cvmx_pcierc#_cfg464
 *
 * PCIE_CFG464 = Four hundred sixty-fifth 32-bits of PCIE type 1 config space
 * (VC Transmit Arbitration Register 1)
 */
union cvmx_pciercx_cfg464 {
	uint32_t u32;
	struct cvmx_pciercx_cfg464_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t wrr_vc3                      : 8;  /**< WRR Weight for VC3 */
	uint32_t wrr_vc2                      : 8;  /**< WRR Weight for VC2 */
	uint32_t wrr_vc1                      : 8;  /**< WRR Weight for VC1 */
	uint32_t wrr_vc0                      : 8;  /**< WRR Weight for VC0 */
#else
	uint32_t wrr_vc0                      : 8;
	uint32_t wrr_vc1                      : 8;
	uint32_t wrr_vc2                      : 8;
	uint32_t wrr_vc3                      : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg464_s          cn52xx;
	struct cvmx_pciercx_cfg464_s          cn52xxp1;
	struct cvmx_pciercx_cfg464_s          cn56xx;
	struct cvmx_pciercx_cfg464_s          cn56xxp1;
	struct cvmx_pciercx_cfg464_s          cn61xx;
	struct cvmx_pciercx_cfg464_s          cn63xx;
	struct cvmx_pciercx_cfg464_s          cn63xxp1;
	struct cvmx_pciercx_cfg464_s          cn66xx;
	struct cvmx_pciercx_cfg464_s          cn68xx;
	struct cvmx_pciercx_cfg464_s          cn68xxp1;
	struct cvmx_pciercx_cfg464_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg464 cvmx_pciercx_cfg464_t;

/**
 * cvmx_pcierc#_cfg465
 *
 * PCIE_CFG465 = Four hundred sixty-sixth 32-bits of config space
 * (VC Transmit Arbitration Register 2)
 */
union cvmx_pciercx_cfg465 {
	uint32_t u32;
	struct cvmx_pciercx_cfg465_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t wrr_vc7                      : 8;  /**< WRR Weight for VC7 */
	uint32_t wrr_vc6                      : 8;  /**< WRR Weight for VC6 */
	uint32_t wrr_vc5                      : 8;  /**< WRR Weight for VC5 */
	uint32_t wrr_vc4                      : 8;  /**< WRR Weight for VC4 */
#else
	uint32_t wrr_vc4                      : 8;
	uint32_t wrr_vc5                      : 8;
	uint32_t wrr_vc6                      : 8;
	uint32_t wrr_vc7                      : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg465_s          cn52xx;
	struct cvmx_pciercx_cfg465_s          cn52xxp1;
	struct cvmx_pciercx_cfg465_s          cn56xx;
	struct cvmx_pciercx_cfg465_s          cn56xxp1;
	struct cvmx_pciercx_cfg465_s          cn61xx;
	struct cvmx_pciercx_cfg465_s          cn63xx;
	struct cvmx_pciercx_cfg465_s          cn63xxp1;
	struct cvmx_pciercx_cfg465_s          cn66xx;
	struct cvmx_pciercx_cfg465_s          cn68xx;
	struct cvmx_pciercx_cfg465_s          cn68xxp1;
	struct cvmx_pciercx_cfg465_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg465 cvmx_pciercx_cfg465_t;

/**
 * cvmx_pcierc#_cfg466
 *
 * PCIE_CFG466 = Four hundred sixty-seventh 32-bits of PCIE type 1 config space
 * (VC0 Posted Receive Queue Control)
 */
union cvmx_pciercx_cfg466 {
	uint32_t u32;
	struct cvmx_pciercx_cfg466_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rx_queue_order               : 1;  /**< VC Ordering for Receive Queues
                                                         Determines the VC ordering rule for the receive queues, used
                                                         only in the segmented-buffer configuration,
                                                         writable through PEM(0..1)_CFG_WR:
                                                         o 1: Strict ordering, higher numbered VCs have higher priority
                                                         o 0: Round robin
                                                         However, the application must not change this field. */
	uint32_t type_ordering                : 1;  /**< TLP Type Ordering for VC0
                                                         Determines the TLP type ordering rule for VC0 receive queues,
                                                         used only in the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR:
                                                         o 1: Ordering of received TLPs follows the rules in
                                                              PCI Express Base Specification
                                                         o 0: Strict ordering for received TLPs: Posted, then
                                                              Completion, then Non-Posted
                                                         However, the application must not change this field. */
	uint32_t reserved_24_29               : 6;
	uint32_t queue_mode                   : 3;  /**< VC0 Posted TLP Queue Mode
                                                         The operating mode of the Posted receive queue for VC0, used
                                                         only in the segmented-buffer configuration, writable through
                                                         PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field.
                                                         Only one bit can be set at a time:
                                                         o Bit 23: Bypass
                                                         o Bit 22: Cut-through
                                                         o Bit 21: Store-and-forward */
	uint32_t reserved_20_20               : 1;
	uint32_t header_credits               : 8;  /**< VC0 Posted Header Credits
                                                         The number of initial Posted header credits for VC0, used for
                                                         all receive queue buffer configurations.
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t data_credits                 : 12; /**< VC0 Posted Data Credits
                                                         The number of initial Posted data credits for VC0, used for all
                                                         receive queue buffer configurations.
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t data_credits                 : 12;
	uint32_t header_credits               : 8;
	uint32_t reserved_20_20               : 1;
	uint32_t queue_mode                   : 3;
	uint32_t reserved_24_29               : 6;
	uint32_t type_ordering                : 1;
	uint32_t rx_queue_order               : 1;
#endif
	} s;
	struct cvmx_pciercx_cfg466_s          cn52xx;
	struct cvmx_pciercx_cfg466_s          cn52xxp1;
	struct cvmx_pciercx_cfg466_s          cn56xx;
	struct cvmx_pciercx_cfg466_s          cn56xxp1;
	struct cvmx_pciercx_cfg466_s          cn61xx;
	struct cvmx_pciercx_cfg466_s          cn63xx;
	struct cvmx_pciercx_cfg466_s          cn63xxp1;
	struct cvmx_pciercx_cfg466_s          cn66xx;
	struct cvmx_pciercx_cfg466_s          cn68xx;
	struct cvmx_pciercx_cfg466_s          cn68xxp1;
	struct cvmx_pciercx_cfg466_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg466 cvmx_pciercx_cfg466_t;

/**
 * cvmx_pcierc#_cfg467
 *
 * PCIE_CFG467 = Four hundred sixty-eighth 32-bits of PCIE type 1 config space
 * (VC0 Non-Posted Receive Queue Control)
 */
union cvmx_pciercx_cfg467 {
	uint32_t u32;
	struct cvmx_pciercx_cfg467_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t queue_mode                   : 3;  /**< VC0 Non-Posted TLP Queue Mode
                                                         The operating mode of the Non-Posted receive queue for VC0,
                                                         used only in the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         Only one bit can be set at a time:
                                                         o Bit 23: Bypass
                                                         o Bit 22: Cut-through
                                                         o Bit 21: Store-and-forward
                                                         However, the application must not change this field. */
	uint32_t reserved_20_20               : 1;
	uint32_t header_credits               : 8;  /**< VC0 Non-Posted Header Credits
                                                         The number of initial Non-Posted header credits for VC0, used
                                                         for all receive queue buffer configurations.
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t data_credits                 : 12; /**< VC0 Non-Posted Data Credits
                                                         The number of initial Non-Posted data credits for VC0, used for
                                                         all receive queue buffer configurations.
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t data_credits                 : 12;
	uint32_t header_credits               : 8;
	uint32_t reserved_20_20               : 1;
	uint32_t queue_mode                   : 3;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg467_s          cn52xx;
	struct cvmx_pciercx_cfg467_s          cn52xxp1;
	struct cvmx_pciercx_cfg467_s          cn56xx;
	struct cvmx_pciercx_cfg467_s          cn56xxp1;
	struct cvmx_pciercx_cfg467_s          cn61xx;
	struct cvmx_pciercx_cfg467_s          cn63xx;
	struct cvmx_pciercx_cfg467_s          cn63xxp1;
	struct cvmx_pciercx_cfg467_s          cn66xx;
	struct cvmx_pciercx_cfg467_s          cn68xx;
	struct cvmx_pciercx_cfg467_s          cn68xxp1;
	struct cvmx_pciercx_cfg467_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg467 cvmx_pciercx_cfg467_t;

/**
 * cvmx_pcierc#_cfg468
 *
 * PCIE_CFG468 = Four hundred sixty-ninth 32-bits of PCIE type 1 config space
 * (VC0 Completion Receive Queue Control)
 */
union cvmx_pciercx_cfg468 {
	uint32_t u32;
	struct cvmx_pciercx_cfg468_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t queue_mode                   : 3;  /**< VC0 Completion TLP Queue Mode
                                                         The operating mode of the Completion receive queue for VC0,
                                                         used only in the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         Only one bit can be set at a time:
                                                         o Bit 23: Bypass
                                                         o Bit 22: Cut-through
                                                         o Bit 21: Store-and-forward
                                                         However, the application must not change this field. */
	uint32_t reserved_20_20               : 1;
	uint32_t header_credits               : 8;  /**< VC0 Completion Header Credits
                                                         The number of initial Completion header credits for VC0, used
                                                         for all receive queue buffer configurations.
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t data_credits                 : 12; /**< VC0 Completion Data Credits
                                                         The number of initial Completion data credits for VC0, used for
                                                         all receive queue buffer configurations.
                                                         This field is writable through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t data_credits                 : 12;
	uint32_t header_credits               : 8;
	uint32_t reserved_20_20               : 1;
	uint32_t queue_mode                   : 3;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_pciercx_cfg468_s          cn52xx;
	struct cvmx_pciercx_cfg468_s          cn52xxp1;
	struct cvmx_pciercx_cfg468_s          cn56xx;
	struct cvmx_pciercx_cfg468_s          cn56xxp1;
	struct cvmx_pciercx_cfg468_s          cn61xx;
	struct cvmx_pciercx_cfg468_s          cn63xx;
	struct cvmx_pciercx_cfg468_s          cn63xxp1;
	struct cvmx_pciercx_cfg468_s          cn66xx;
	struct cvmx_pciercx_cfg468_s          cn68xx;
	struct cvmx_pciercx_cfg468_s          cn68xxp1;
	struct cvmx_pciercx_cfg468_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg468 cvmx_pciercx_cfg468_t;

/**
 * cvmx_pcierc#_cfg490
 *
 * PCIE_CFG490 = Four hundred ninety-first 32-bits of PCIE type 1 config space
 * (VC0 Posted Buffer Depth)
 */
union cvmx_pciercx_cfg490 {
	uint32_t u32;
	struct cvmx_pciercx_cfg490_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_26_31               : 6;
	uint32_t header_depth                 : 10; /**< VC0 Posted Header Queue Depth
                                                         Sets the number of entries in the Posted header queue for VC0
                                                         when using the segmented-buffer configuration, writable through
                                                         PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t reserved_14_15               : 2;
	uint32_t data_depth                   : 14; /**< VC0 Posted Data Queue Depth
                                                         Sets the number of entries in the Posted data queue for VC0
                                                         when using the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t data_depth                   : 14;
	uint32_t reserved_14_15               : 2;
	uint32_t header_depth                 : 10;
	uint32_t reserved_26_31               : 6;
#endif
	} s;
	struct cvmx_pciercx_cfg490_s          cn52xx;
	struct cvmx_pciercx_cfg490_s          cn52xxp1;
	struct cvmx_pciercx_cfg490_s          cn56xx;
	struct cvmx_pciercx_cfg490_s          cn56xxp1;
	struct cvmx_pciercx_cfg490_s          cn61xx;
	struct cvmx_pciercx_cfg490_s          cn63xx;
	struct cvmx_pciercx_cfg490_s          cn63xxp1;
	struct cvmx_pciercx_cfg490_s          cn66xx;
	struct cvmx_pciercx_cfg490_s          cn68xx;
	struct cvmx_pciercx_cfg490_s          cn68xxp1;
	struct cvmx_pciercx_cfg490_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg490 cvmx_pciercx_cfg490_t;

/**
 * cvmx_pcierc#_cfg491
 *
 * PCIE_CFG491 = Four hundred ninety-second 32-bits of PCIE type 1 config space
 * (VC0 Non-Posted Buffer Depth)
 */
union cvmx_pciercx_cfg491 {
	uint32_t u32;
	struct cvmx_pciercx_cfg491_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_26_31               : 6;
	uint32_t header_depth                 : 10; /**< VC0 Non-Posted Header Queue Depth
                                                         Sets the number of entries in the Non-Posted header queue for
                                                         VC0 when using the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t reserved_14_15               : 2;
	uint32_t data_depth                   : 14; /**< VC0 Non-Posted Data Queue Depth
                                                         Sets the number of entries in the Non-Posted data queue for VC0
                                                         when using the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t data_depth                   : 14;
	uint32_t reserved_14_15               : 2;
	uint32_t header_depth                 : 10;
	uint32_t reserved_26_31               : 6;
#endif
	} s;
	struct cvmx_pciercx_cfg491_s          cn52xx;
	struct cvmx_pciercx_cfg491_s          cn52xxp1;
	struct cvmx_pciercx_cfg491_s          cn56xx;
	struct cvmx_pciercx_cfg491_s          cn56xxp1;
	struct cvmx_pciercx_cfg491_s          cn61xx;
	struct cvmx_pciercx_cfg491_s          cn63xx;
	struct cvmx_pciercx_cfg491_s          cn63xxp1;
	struct cvmx_pciercx_cfg491_s          cn66xx;
	struct cvmx_pciercx_cfg491_s          cn68xx;
	struct cvmx_pciercx_cfg491_s          cn68xxp1;
	struct cvmx_pciercx_cfg491_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg491 cvmx_pciercx_cfg491_t;

/**
 * cvmx_pcierc#_cfg492
 *
 * PCIE_CFG492 = Four hundred ninety-third 32-bits of PCIE type 1 config space
 * (VC0 Completion Buffer Depth)
 */
union cvmx_pciercx_cfg492 {
	uint32_t u32;
	struct cvmx_pciercx_cfg492_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_26_31               : 6;
	uint32_t header_depth                 : 10; /**< VC0 Completion Header Queue Depth
                                                         Sets the number of entries in the Completion header queue for
                                                         VC0 when using the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
	uint32_t reserved_14_15               : 2;
	uint32_t data_depth                   : 14; /**< VC0 Completion Data Queue Depth
                                                         Sets the number of entries in the Completion data queue for VC0
                                                         when using the segmented-buffer configuration, writable
                                                         through PEM(0..1)_CFG_WR.
                                                         However, the application must not change this field. */
#else
	uint32_t data_depth                   : 14;
	uint32_t reserved_14_15               : 2;
	uint32_t header_depth                 : 10;
	uint32_t reserved_26_31               : 6;
#endif
	} s;
	struct cvmx_pciercx_cfg492_s          cn52xx;
	struct cvmx_pciercx_cfg492_s          cn52xxp1;
	struct cvmx_pciercx_cfg492_s          cn56xx;
	struct cvmx_pciercx_cfg492_s          cn56xxp1;
	struct cvmx_pciercx_cfg492_s          cn61xx;
	struct cvmx_pciercx_cfg492_s          cn63xx;
	struct cvmx_pciercx_cfg492_s          cn63xxp1;
	struct cvmx_pciercx_cfg492_s          cn66xx;
	struct cvmx_pciercx_cfg492_s          cn68xx;
	struct cvmx_pciercx_cfg492_s          cn68xxp1;
	struct cvmx_pciercx_cfg492_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg492 cvmx_pciercx_cfg492_t;

/**
 * cvmx_pcierc#_cfg515
 *
 * PCIE_CFG515 = Five hundred sixteenth 32-bits of PCIE type 1 config space
 * (Port Logic Register (Gen2))
 */
union cvmx_pciercx_cfg515 {
	uint32_t u32;
	struct cvmx_pciercx_cfg515_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_21_31               : 11;
	uint32_t s_d_e                        : 1;  /**< SEL_DE_EMPHASIS
                                                         Used to set the de-emphasis level for upstream ports. */
	uint32_t ctcrb                        : 1;  /**< Config Tx Compliance Receive Bit
                                                         When set to 1, signals LTSSM to transmit TS ordered sets
                                                         with the compliance receive bit assert (equal to 1). */
	uint32_t cpyts                        : 1;  /**< Config PHY Tx Swing
                                                         Indicates the voltage level the PHY should drive. When set to
                                                         1, indicates Full Swing. When set to 0, indicates Low Swing */
	uint32_t dsc                          : 1;  /**< Directed Speed Change
                                                         o a write of '1' will initiate a speed change
                                                         o always reads a zero */
	uint32_t le                           : 9;  /**< Lane Enable
                                                         Indicates the number of lanes to check for exit from electrical
                                                         idle in Polling.Active and Polling.Compliance. 1 = x1, 2 = x2,
                                                         etc. Used to limit the maximum link width to ignore broken
                                                         lanes that detect a receiver, but will not exit electrical
                                                         idle and
                                                         would otherwise prevent a valid link from being configured. */
	uint32_t n_fts                        : 8;  /**< N_FTS
                                                         Sets the Number of Fast Training Sequences (N_FTS) that
                                                         the core advertises as its N_FTS during GEN2 Link training.
                                                         This value is used to inform the Link partner about the PHYs
                                                         ability to recover synchronization after a low power state.
                                                         Note: Do not set N_FTS to zero; doing so can cause the
                                                               LTSSM to go into the recovery state when exiting from
                                                               L0s. */
#else
	uint32_t n_fts                        : 8;
	uint32_t le                           : 9;
	uint32_t dsc                          : 1;
	uint32_t cpyts                        : 1;
	uint32_t ctcrb                        : 1;
	uint32_t s_d_e                        : 1;
	uint32_t reserved_21_31               : 11;
#endif
	} s;
	struct cvmx_pciercx_cfg515_s          cn61xx;
	struct cvmx_pciercx_cfg515_s          cn63xx;
	struct cvmx_pciercx_cfg515_s          cn63xxp1;
	struct cvmx_pciercx_cfg515_s          cn66xx;
	struct cvmx_pciercx_cfg515_s          cn68xx;
	struct cvmx_pciercx_cfg515_s          cn68xxp1;
	struct cvmx_pciercx_cfg515_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg515 cvmx_pciercx_cfg515_t;

/**
 * cvmx_pcierc#_cfg516
 *
 * PCIE_CFG516 = Five hundred seventeenth 32-bits of PCIE type 1 config space
 * (PHY Status Register)
 */
union cvmx_pciercx_cfg516 {
	uint32_t u32;
	struct cvmx_pciercx_cfg516_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t phy_stat                     : 32; /**< PHY Status */
#else
	uint32_t phy_stat                     : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg516_s          cn52xx;
	struct cvmx_pciercx_cfg516_s          cn52xxp1;
	struct cvmx_pciercx_cfg516_s          cn56xx;
	struct cvmx_pciercx_cfg516_s          cn56xxp1;
	struct cvmx_pciercx_cfg516_s          cn61xx;
	struct cvmx_pciercx_cfg516_s          cn63xx;
	struct cvmx_pciercx_cfg516_s          cn63xxp1;
	struct cvmx_pciercx_cfg516_s          cn66xx;
	struct cvmx_pciercx_cfg516_s          cn68xx;
	struct cvmx_pciercx_cfg516_s          cn68xxp1;
	struct cvmx_pciercx_cfg516_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg516 cvmx_pciercx_cfg516_t;

/**
 * cvmx_pcierc#_cfg517
 *
 * PCIE_CFG517 = Five hundred eighteenth 32-bits of PCIE type 1 config space
 * (PHY Control Register)
 */
union cvmx_pciercx_cfg517 {
	uint32_t u32;
	struct cvmx_pciercx_cfg517_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t phy_ctrl                     : 32; /**< PHY Control */
#else
	uint32_t phy_ctrl                     : 32;
#endif
	} s;
	struct cvmx_pciercx_cfg517_s          cn52xx;
	struct cvmx_pciercx_cfg517_s          cn52xxp1;
	struct cvmx_pciercx_cfg517_s          cn56xx;
	struct cvmx_pciercx_cfg517_s          cn56xxp1;
	struct cvmx_pciercx_cfg517_s          cn61xx;
	struct cvmx_pciercx_cfg517_s          cn63xx;
	struct cvmx_pciercx_cfg517_s          cn63xxp1;
	struct cvmx_pciercx_cfg517_s          cn66xx;
	struct cvmx_pciercx_cfg517_s          cn68xx;
	struct cvmx_pciercx_cfg517_s          cn68xxp1;
	struct cvmx_pciercx_cfg517_s          cnf71xx;
};
typedef union cvmx_pciercx_cfg517 cvmx_pciercx_cfg517_t;

#endif
