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
 * cvmx-ciu2-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon ciu2.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_CIU2_DEFS_H__
#define __CVMX_CIU2_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_ACK_IOX_INT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_ACK_IOX_INT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080C0800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_ACK_IOX_INT(block_id) (CVMX_ADD_IO_SEG(0x00010701080C0800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_ACK_PPX_IP2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_ACK_PPX_IP2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000C0000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_ACK_PPX_IP2(block_id) (CVMX_ADD_IO_SEG(0x00010701000C0000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_ACK_PPX_IP3(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_ACK_PPX_IP3(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000C0200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_ACK_PPX_IP3(block_id) (CVMX_ADD_IO_SEG(0x00010701000C0200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_ACK_PPX_IP4(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_ACK_PPX_IP4(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000C0400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_ACK_PPX_IP4(block_id) (CVMX_ADD_IO_SEG(0x00010701000C0400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108097800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108097800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_GPIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_GPIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B7800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B7800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_GPIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_GPIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A7800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A7800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108094800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070108094800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_IO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_IO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B4800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B4800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_IO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_IO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A4800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A4800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108098800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070108098800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MBOX_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MBOX_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B8800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B8800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MBOX_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MBOX_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A8800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A8800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108095800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070108095800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MEM_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MEM_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B5800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B5800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MEM_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MEM_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A5800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A5800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108093800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108093800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B3800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B3800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_MIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_MIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A3800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A3800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108096800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070108096800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_PKT_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_PKT_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B6800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B6800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_PKT_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_PKT_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A6800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A6800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108092800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070108092800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_RML_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_RML_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B2800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B2800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_RML_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_RML_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A2800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A2800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108091800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070108091800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_WDOG_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_WDOG_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B1800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B1800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_WDOG_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_WDOG_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A1800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A1800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108090800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070108090800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_WRKQ_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_WRKQ_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080B0800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B0800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_IOX_INT_WRKQ_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_EN_IOX_INT_WRKQ_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701080A0800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_IOX_INT_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A0800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100097000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100097000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_GPIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_GPIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B7000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B7000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_GPIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_GPIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A7000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A7000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100094000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100094000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_IO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_IO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B4000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B4000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_IO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_IO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A4000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A4000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100098000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100098000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MBOX_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MBOX_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B8000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B8000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MBOX_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MBOX_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A8000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A8000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100095000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100095000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MEM_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MEM_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B5000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B5000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MEM_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MEM_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A5000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A5000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100093000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100093000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B3000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B3000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_MIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_MIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A3000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A3000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100096000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100096000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_PKT_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_PKT_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B6000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B6000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_PKT_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_PKT_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A6000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A6000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100092000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100092000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_RML_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_RML_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B2000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B2000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_RML_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_RML_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A2000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A2000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100091000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100091000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_WDOG_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_WDOG_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B1000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B1000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_WDOG_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_WDOG_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A1000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A1000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100090000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100090000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B0000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B0000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A0000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A0000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100097200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100097200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_GPIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_GPIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B7200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B7200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_GPIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_GPIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A7200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A7200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100094200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100094200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_IO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_IO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B4200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B4200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_IO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_IO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A4200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A4200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100098200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100098200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MBOX_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MBOX_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B8200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B8200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MBOX_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MBOX_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A8200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A8200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100095200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100095200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MEM_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MEM_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B5200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B5200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MEM_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MEM_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A5200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A5200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100093200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100093200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B3200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B3200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_MIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_MIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A3200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A3200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100096200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100096200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_PKT_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_PKT_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B6200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B6200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_PKT_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_PKT_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A6200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A6200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100092200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100092200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_RML_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_RML_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B2200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B2200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_RML_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_RML_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A2200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A2200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100091200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100091200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_WDOG_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_WDOG_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B1200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B1200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_WDOG_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_WDOG_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A1200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A1200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100090200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100090200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_WRKQ_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_WRKQ_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B0200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B0200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP3_WRKQ_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP3_WRKQ_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A0200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP3_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A0200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100097400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100097400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_GPIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_GPIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B7400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B7400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_GPIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_GPIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A7400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A7400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100094400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100094400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_IO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_IO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B4400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B4400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_IO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_IO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A4400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A4400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100098400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100098400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MBOX_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MBOX_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B8400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B8400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MBOX_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MBOX_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A8400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A8400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100095400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100095400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MEM_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MEM_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B5400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B5400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MEM_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MEM_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A5400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A5400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100093400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100093400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MIO_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MIO_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B3400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B3400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_MIO_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_MIO_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A3400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A3400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100096400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100096400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_PKT_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_PKT_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B6400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B6400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_PKT_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_PKT_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A6400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A6400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100092400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100092400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_RML_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_RML_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B2400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B2400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_RML_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_RML_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A2400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A2400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100091400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100091400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_WDOG_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_WDOG_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B1400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B1400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_WDOG_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_WDOG_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A1400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A1400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100090400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100090400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_WRKQ_W1C(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_WRKQ_W1C(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000B0400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B0400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_EN_PPX_IP4_WRKQ_W1S(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_EN_PPX_IP4_WRKQ_W1S(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000A0400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_EN_PPX_IP4_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A0400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_CIU2_INTR_CIU_READY CVMX_CIU2_INTR_CIU_READY_FUNC()
static inline uint64_t CVMX_CIU2_INTR_CIU_READY_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_CIU2_INTR_CIU_READY not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070100102008ull);
}
#else
#define CVMX_CIU2_INTR_CIU_READY (CVMX_ADD_IO_SEG(0x0001070100102008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_CIU2_INTR_RAM_ECC_CTL CVMX_CIU2_INTR_RAM_ECC_CTL_FUNC()
static inline uint64_t CVMX_CIU2_INTR_RAM_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_CIU2_INTR_RAM_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070100102010ull);
}
#else
#define CVMX_CIU2_INTR_RAM_ECC_CTL (CVMX_ADD_IO_SEG(0x0001070100102010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_CIU2_INTR_RAM_ECC_ST CVMX_CIU2_INTR_RAM_ECC_ST_FUNC()
static inline uint64_t CVMX_CIU2_INTR_RAM_ECC_ST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_CIU2_INTR_RAM_ECC_ST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070100102018ull);
}
#else
#define CVMX_CIU2_INTR_RAM_ECC_ST (CVMX_ADD_IO_SEG(0x0001070100102018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_CIU2_INTR_SLOWDOWN CVMX_CIU2_INTR_SLOWDOWN_FUNC()
static inline uint64_t CVMX_CIU2_INTR_SLOWDOWN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_CIU2_INTR_SLOWDOWN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070100102000ull);
}
#else
#define CVMX_CIU2_INTR_SLOWDOWN (CVMX_ADD_IO_SEG(0x0001070100102000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_MSIRED_PPX_IP2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_MSIRED_PPX_IP2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000C1000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_MSIRED_PPX_IP2(block_id) (CVMX_ADD_IO_SEG(0x00010701000C1000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_MSIRED_PPX_IP3(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_MSIRED_PPX_IP3(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000C1200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_MSIRED_PPX_IP3(block_id) (CVMX_ADD_IO_SEG(0x00010701000C1200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_MSIRED_PPX_IP4(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_MSIRED_PPX_IP4(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00010701000C1400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_MSIRED_PPX_IP4(block_id) (CVMX_ADD_IO_SEG(0x00010701000C1400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_MSI_RCVX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 255)))))
		cvmx_warn("CVMX_CIU2_MSI_RCVX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010701000C2000ull) + ((offset) & 255) * 8;
}
#else
#define CVMX_CIU2_MSI_RCVX(offset) (CVMX_ADD_IO_SEG(0x00010701000C2000ull) + ((offset) & 255) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_MSI_SELX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 255)))))
		cvmx_warn("CVMX_CIU2_MSI_SELX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00010701000C3000ull) + ((offset) & 255) * 8;
}
#else
#define CVMX_CIU2_MSI_SELX(offset) (CVMX_ADD_IO_SEG(0x00010701000C3000ull) + ((offset) & 255) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108047800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108047800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108044800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070108044800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108045800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070108045800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108043800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108043800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108046800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070108046800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108042800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070108042800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108041800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070108041800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_IOX_INT_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_RAW_IOX_INT_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108040800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_IOX_INT_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070108040800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100047000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100047000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100044000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100044000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100045000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100045000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100043000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100043000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100046000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100046000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100042000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100042000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100041000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100041000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP2_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP2_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100040000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP2_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100040000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100047200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100047200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100044200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100044200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100045200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100045200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100043200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100043200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100046200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100046200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100042200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100042200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100041200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100041200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP3_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP3_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100040200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP3_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100040200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100047400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100047400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100044400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100044400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100045400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100045400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100043400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100043400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100046400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100046400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100042400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100042400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100041400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100041400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_RAW_PPX_IP4_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_RAW_PPX_IP4_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100040400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_RAW_PPX_IP4_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100040400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108087800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108087800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108084800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070108084800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108088800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070108088800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108085800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070108085800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108083800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108083800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108086800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070108086800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108082800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070108082800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108081800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070108081800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_IOX_INT_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_CIU2_SRC_IOX_INT_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070108080800ull) + ((block_id) & 1) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_IOX_INT_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070108080800ull) + ((block_id) & 1) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100087000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100087000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100084000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100084000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100088000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100088000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100085000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100085000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100083000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100083000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100086000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100086000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100082000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100082000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100081000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100081000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP2_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP2_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100080000ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP2_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100080000ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100087200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100087200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100084200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100084200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100088200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100088200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100085200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100085200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100083200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100083200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100086200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100086200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100082200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100082200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100081200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100081200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP3_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP3_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100080200ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP3_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100080200ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_GPIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_GPIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100087400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100087400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_IO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_IO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100084400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100084400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_MBOX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_MBOX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100088400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100088400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_MEM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_MEM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100085400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100085400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_MIO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_MIO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100083400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100083400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_PKT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_PKT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100086400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100086400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_RML(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_RML(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100082400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100082400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_WDOG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_WDOG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100081400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100081400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SRC_PPX_IP4_WRKQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 31)))))
		cvmx_warn("CVMX_CIU2_SRC_PPX_IP4_WRKQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001070100080400ull) + ((block_id) & 31) * 0x200000ull;
}
#else
#define CVMX_CIU2_SRC_PPX_IP4_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100080400ull) + ((block_id) & 31) * 0x200000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SUM_IOX_INT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_CIU2_SUM_IOX_INT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070100000800ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_CIU2_SUM_IOX_INT(offset) (CVMX_ADD_IO_SEG(0x0001070100000800ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SUM_PPX_IP2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_CIU2_SUM_PPX_IP2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070100000000ull) + ((offset) & 31) * 8;
}
#else
#define CVMX_CIU2_SUM_PPX_IP2(offset) (CVMX_ADD_IO_SEG(0x0001070100000000ull) + ((offset) & 31) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SUM_PPX_IP3(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_CIU2_SUM_PPX_IP3(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070100000200ull) + ((offset) & 31) * 8;
}
#else
#define CVMX_CIU2_SUM_PPX_IP3(offset) (CVMX_ADD_IO_SEG(0x0001070100000200ull) + ((offset) & 31) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_CIU2_SUM_PPX_IP4(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_CIU2_SUM_PPX_IP4(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070100000400ull) + ((offset) & 31) * 8;
}
#else
#define CVMX_CIU2_SUM_PPX_IP4(offset) (CVMX_ADD_IO_SEG(0x0001070100000400ull) + ((offset) & 31) * 8)
#endif

/**
 * cvmx_ciu2_ack_io#_int
 */
union cvmx_ciu2_ack_iox_int {
	uint64_t u64;
	struct cvmx_ciu2_ack_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t ack                          : 1;  /**< Read to clear the corresponding interrupt to
                                                         PP/IO.  Without this read the interrupt will not
                                                         deassert until the next CIU interrupt scan, up to
                                                         200 cycles away. */
#else
	uint64_t ack                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_ciu2_ack_iox_int_s        cn68xx;
	struct cvmx_ciu2_ack_iox_int_s        cn68xxp1;
};
typedef union cvmx_ciu2_ack_iox_int cvmx_ciu2_ack_iox_int_t;

/**
 * cvmx_ciu2_ack_pp#_ip2
 *
 * CIU2_ACK_PPX_IPx      (Pass 2)
 *
 */
union cvmx_ciu2_ack_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu2_ack_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t ack                          : 1;  /**< Read to clear the corresponding interrupt to
                                                         PP/IO.  Without this read the interrupt will not
                                                         deassert until the next CIU interrupt scan, up to
                                                         200 cycles away. */
#else
	uint64_t ack                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_ciu2_ack_ppx_ip2_s        cn68xx;
	struct cvmx_ciu2_ack_ppx_ip2_s        cn68xxp1;
};
typedef union cvmx_ciu2_ack_ppx_ip2 cvmx_ciu2_ack_ppx_ip2_t;

/**
 * cvmx_ciu2_ack_pp#_ip3
 */
union cvmx_ciu2_ack_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu2_ack_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t ack                          : 1;  /**< Read to clear the corresponding interrupt to
                                                         PP/IO.  Without this read the interrupt will not
                                                         deassert until the next CIU interrupt scan, up to
                                                         200 cycles away. */
#else
	uint64_t ack                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_ciu2_ack_ppx_ip3_s        cn68xx;
	struct cvmx_ciu2_ack_ppx_ip3_s        cn68xxp1;
};
typedef union cvmx_ciu2_ack_ppx_ip3 cvmx_ciu2_ack_ppx_ip3_t;

/**
 * cvmx_ciu2_ack_pp#_ip4
 */
union cvmx_ciu2_ack_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu2_ack_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t ack                          : 1;  /**< Read to clear the corresponding interrupt to
                                                         PP/IO.  Without this read the interrupt will not
                                                         deassert until the next CIU interrupt scan, up to
                                                         200 cycles away. */
#else
	uint64_t ack                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_ciu2_ack_ppx_ip4_s        cn68xx;
	struct cvmx_ciu2_ack_ppx_ip4_s        cn68xxp1;
};
typedef union cvmx_ciu2_ack_ppx_ip4 cvmx_ciu2_ack_ppx_ip4_t;

/**
 * cvmx_ciu2_en_io#_int_gpio
 */
union cvmx_ciu2_en_iox_int_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt-enable */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_gpio_s    cn68xx;
	struct cvmx_ciu2_en_iox_int_gpio_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_gpio cvmx_ciu2_en_iox_int_gpio_t;

/**
 * cvmx_ciu2_en_io#_int_gpio_w1c
 */
union cvmx_ciu2_en_iox_int_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< Write 1 to clear CIU2_EN_xx_yy_GPIO[GPIO] */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_gpio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_gpio_w1c cvmx_ciu2_en_iox_int_gpio_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_gpio_w1s
 */
union cvmx_ciu2_en_iox_int_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt enable,write 1 to enable CIU2_EN */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_gpio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_gpio_w1s cvmx_ciu2_en_iox_int_gpio_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_io
 */
union cvmx_ciu2_en_iox_int_io {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt-enable */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA interrupt-enable */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit interrupt-enable
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI  interrupt-enable */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt-enable */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_io_s      cn68xx;
	struct cvmx_ciu2_en_iox_int_io_s      cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_io cvmx_ciu2_en_iox_int_io_t;

/**
 * cvmx_ciu2_en_io#_int_io_w1c
 */
union cvmx_ciu2_en_iox_int_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_io_w1c_s  cn68xx;
	struct cvmx_ciu2_en_iox_int_io_w1c_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_io_w1c cvmx_ciu2_en_iox_int_io_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_io_w1s
 */
union cvmx_ciu2_en_iox_int_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_io_w1s_s  cn68xx;
	struct cvmx_ciu2_en_iox_int_io_w1s_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_io_w1s cvmx_ciu2_en_iox_int_io_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_mbox
 */
union cvmx_ciu2_en_iox_int_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt-enable, use with CIU2_MBOX
                                                         to generate CIU2_SRC_xx_yy_MBOX */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mbox_s    cn68xx;
	struct cvmx_ciu2_en_iox_int_mbox_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mbox cvmx_ciu2_en_iox_int_mbox_t;

/**
 * cvmx_ciu2_en_io#_int_mbox_w1c
 */
union cvmx_ciu2_en_iox_int_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mbox_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mbox_w1c cvmx_ciu2_en_iox_int_mbox_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_mbox_w1s
 */
union cvmx_ciu2_en_iox_int_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mbox_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mbox_w1s cvmx_ciu2_en_iox_int_mbox_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_mem
 */
union cvmx_ciu2_en_iox_int_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt-enable */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mem_s     cn68xx;
	struct cvmx_ciu2_en_iox_int_mem_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mem cvmx_ciu2_en_iox_int_mem_t;

/**
 * cvmx_ciu2_en_io#_int_mem_w1c
 */
union cvmx_ciu2_en_iox_int_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mem_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mem_w1c cvmx_ciu2_en_iox_int_mem_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_mem_w1s
 */
union cvmx_ciu2_en_iox_int_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mem_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mem_w1s cvmx_ciu2_en_iox_int_mem_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_mio
 */
union cvmx_ciu2_en_iox_int_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt-enable */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt-enable */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt-enable */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt-enable */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x interrupt-enable */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines interrupt-enable */
	uint64_t mio                          : 1;  /**< MIO boot interrupt-enable */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt-enable */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupt-enable */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt-enable */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt-enable */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt-enable */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mio_s     cn68xx;
	struct cvmx_ciu2_en_iox_int_mio_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mio cvmx_ciu2_en_iox_int_mio_t;

/**
 * cvmx_ciu2_en_io#_int_mio_w1c
 */
union cvmx_ciu2_en_iox_int_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mio_w1c cvmx_ciu2_en_iox_int_mio_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_mio_w1s
 */
union cvmx_ciu2_en_iox_int_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_mio_w1s cvmx_ciu2_en_iox_int_mio_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_pkt
 */
union cvmx_ciu2_en_iox_int_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_pkt_s     cn68xx;
	struct cvmx_ciu2_en_iox_int_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_pkt cvmx_ciu2_en_iox_int_pkt_t;

/**
 * cvmx_ciu2_en_io#_int_pkt_w1c
 */
union cvmx_ciu2_en_iox_int_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_pkt_w1c cvmx_ciu2_en_iox_int_pkt_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_pkt_w1s
 */
union cvmx_ciu2_en_iox_int_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_pkt_w1s cvmx_ciu2_en_iox_int_pkt_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_rml
 */
union cvmx_ciu2_en_iox_int_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_rml_s     cn68xx;
	struct cvmx_ciu2_en_iox_int_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_rml cvmx_ciu2_en_iox_int_rml_t;

/**
 * cvmx_ciu2_en_io#_int_rml_w1c
 */
union cvmx_ciu2_en_iox_int_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_rml_w1c cvmx_ciu2_en_iox_int_rml_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_rml_w1s
 */
union cvmx_ciu2_en_iox_int_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_rml_w1s cvmx_ciu2_en_iox_int_rml_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_wdog
 */
union cvmx_ciu2_en_iox_int_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupt-enable */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wdog_s    cn68xx;
	struct cvmx_ciu2_en_iox_int_wdog_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_wdog cvmx_ciu2_en_iox_int_wdog_t;

/**
 * cvmx_ciu2_en_io#_int_wdog_w1c
 */
union cvmx_ciu2_en_iox_int_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< write 1 to clear CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wdog_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_wdog_w1c cvmx_ciu2_en_iox_int_wdog_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_wdog_w1s
 */
union cvmx_ciu2_en_iox_int_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< Write 1 to enable CIU2_EN_xx_yy_WDOG[WDOG] */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wdog_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_wdog_w1s cvmx_ciu2_en_iox_int_wdog_w1s_t;

/**
 * cvmx_ciu2_en_io#_int_wrkq
 */
union cvmx_ciu2_en_iox_int_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupt-enable */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wrkq_s    cn68xx;
	struct cvmx_ciu2_en_iox_int_wrkq_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_wrkq cvmx_ciu2_en_iox_int_wrkq_t;

/**
 * cvmx_ciu2_en_io#_int_wrkq_w1c
 */
union cvmx_ciu2_en_iox_int_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to clear CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         For W1C bits, write 1 to clear the corresponding
                                                         CIU2_EN_xx_yy_WRKQ,write 0 to retain previous value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wrkq_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_wrkq_w1c cvmx_ciu2_en_iox_int_wrkq_w1c_t;

/**
 * cvmx_ciu2_en_io#_int_wrkq_w1s
 */
union cvmx_ciu2_en_iox_int_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to enable CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         1 bit/group. For all W1S bits, write 1 to enable
                                                         corresponding CIU2_EN_xx_yy_WRKQ[WORKQ] bit,
                                                         writing 0 to retain previous value. */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wrkq_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_iox_int_wrkq_w1s cvmx_ciu2_en_iox_int_wrkq_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_gpio
 */
union cvmx_ciu2_en_ppx_ip2_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt-enable */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_gpio_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_gpio_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_gpio cvmx_ciu2_en_ppx_ip2_gpio_t;

/**
 * cvmx_ciu2_en_pp#_ip2_gpio_w1c
 */
union cvmx_ciu2_en_ppx_ip2_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< Write 1 to clear CIU2_EN_xx_yy_GPIO[GPIO] */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_gpio_w1c cvmx_ciu2_en_ppx_ip2_gpio_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_gpio_w1s
 */
union cvmx_ciu2_en_ppx_ip2_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt enable,write 1 to enable CIU2_EN */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_gpio_w1s cvmx_ciu2_en_ppx_ip2_gpio_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_io
 */
union cvmx_ciu2_en_ppx_ip2_io {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt-enable */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA interrupt-enable */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit interrupt-enable
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI  interrupt-enable */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt-enable */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_io_s      cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_io_s      cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_io cvmx_ciu2_en_ppx_ip2_io_t;

/**
 * cvmx_ciu2_en_pp#_ip2_io_w1c
 */
union cvmx_ciu2_en_ppx_ip2_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_io_w1c_s  cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_io_w1c_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_io_w1c cvmx_ciu2_en_ppx_ip2_io_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_io_w1s
 */
union cvmx_ciu2_en_ppx_ip2_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_io_w1s_s  cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_io_w1s_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_io_w1s cvmx_ciu2_en_ppx_ip2_io_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mbox
 */
union cvmx_ciu2_en_ppx_ip2_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt-enable, use with CIU2_MBOX
                                                         to generate CIU2_SRC_xx_yy_MBOX */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mbox_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mbox_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mbox cvmx_ciu2_en_ppx_ip2_mbox_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mbox_w1c
 */
union cvmx_ciu2_en_ppx_ip2_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mbox_w1c cvmx_ciu2_en_ppx_ip2_mbox_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mbox_w1s
 */
union cvmx_ciu2_en_ppx_ip2_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mbox_w1s cvmx_ciu2_en_ppx_ip2_mbox_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mem
 */
union cvmx_ciu2_en_ppx_ip2_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt-enable */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mem_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mem_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mem cvmx_ciu2_en_ppx_ip2_mem_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mem_w1c
 */
union cvmx_ciu2_en_ppx_ip2_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mem_w1c cvmx_ciu2_en_ppx_ip2_mem_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mem_w1s
 */
union cvmx_ciu2_en_ppx_ip2_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mem_w1s cvmx_ciu2_en_ppx_ip2_mem_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mio
 */
union cvmx_ciu2_en_ppx_ip2_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt-enable */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt-enable */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt-enable */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt-enable */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x interrupt-enable */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines interrupt-enable */
	uint64_t mio                          : 1;  /**< MIO boot interrupt-enable */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt-enable */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupt-enable */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt-enable */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt-enable */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt-enable */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mio_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mio_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mio cvmx_ciu2_en_ppx_ip2_mio_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mio_w1c
 */
union cvmx_ciu2_en_ppx_ip2_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mio_w1c cvmx_ciu2_en_ppx_ip2_mio_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_mio_w1s
 */
union cvmx_ciu2_en_ppx_ip2_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_mio_w1s cvmx_ciu2_en_ppx_ip2_mio_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_pkt
 */
union cvmx_ciu2_en_ppx_ip2_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_pkt_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_pkt cvmx_ciu2_en_ppx_ip2_pkt_t;

/**
 * cvmx_ciu2_en_pp#_ip2_pkt_w1c
 */
union cvmx_ciu2_en_ppx_ip2_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_pkt_w1c cvmx_ciu2_en_ppx_ip2_pkt_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_pkt_w1s
 */
union cvmx_ciu2_en_ppx_ip2_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_pkt_w1s cvmx_ciu2_en_ppx_ip2_pkt_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_rml
 */
union cvmx_ciu2_en_ppx_ip2_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_rml_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_rml cvmx_ciu2_en_ppx_ip2_rml_t;

/**
 * cvmx_ciu2_en_pp#_ip2_rml_w1c
 */
union cvmx_ciu2_en_ppx_ip2_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_rml_w1c cvmx_ciu2_en_ppx_ip2_rml_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_rml_w1s
 */
union cvmx_ciu2_en_ppx_ip2_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_rml_w1s cvmx_ciu2_en_ppx_ip2_rml_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_wdog
 */
union cvmx_ciu2_en_ppx_ip2_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupt-enable */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wdog_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wdog_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_wdog cvmx_ciu2_en_ppx_ip2_wdog_t;

/**
 * cvmx_ciu2_en_pp#_ip2_wdog_w1c
 */
union cvmx_ciu2_en_ppx_ip2_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< write 1 to clear CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_wdog_w1c cvmx_ciu2_en_ppx_ip2_wdog_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_wdog_w1s
 */
union cvmx_ciu2_en_ppx_ip2_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< Write 1 to enable CIU2_EN_xx_yy_WDOG[WDOG] */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_wdog_w1s cvmx_ciu2_en_ppx_ip2_wdog_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip2_wrkq
 */
union cvmx_ciu2_en_ppx_ip2_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupt-enable */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_wrkq cvmx_ciu2_en_ppx_ip2_wrkq_t;

/**
 * cvmx_ciu2_en_pp#_ip2_wrkq_w1c
 */
union cvmx_ciu2_en_ppx_ip2_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to clear CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         For W1C bits, write 1 to clear the corresponding
                                                         CIU2_EN_xx_yy_WRKQ,write 0 to retain previous value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_wrkq_w1c cvmx_ciu2_en_ppx_ip2_wrkq_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip2_wrkq_w1s
 */
union cvmx_ciu2_en_ppx_ip2_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to enable CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         1 bit/group. For all W1S bits, write 1 to enable
                                                         corresponding CIU2_EN_xx_yy_WRKQ[WORKQ] bit,
                                                         writing 0 to retain previous value. */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip2_wrkq_w1s cvmx_ciu2_en_ppx_ip2_wrkq_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_gpio
 */
union cvmx_ciu2_en_ppx_ip3_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt-enable */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_gpio_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_gpio_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_gpio cvmx_ciu2_en_ppx_ip3_gpio_t;

/**
 * cvmx_ciu2_en_pp#_ip3_gpio_w1c
 */
union cvmx_ciu2_en_ppx_ip3_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< Write 1 to clear CIU2_EN_xx_yy_GPIO[GPIO] */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_gpio_w1c cvmx_ciu2_en_ppx_ip3_gpio_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_gpio_w1s
 */
union cvmx_ciu2_en_ppx_ip3_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt enable,write 1 to enable CIU2_EN */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_gpio_w1s cvmx_ciu2_en_ppx_ip3_gpio_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_io
 */
union cvmx_ciu2_en_ppx_ip3_io {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt-enable */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA interrupt-enable */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit interrupt-enable
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI  interrupt-enable */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt-enable */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_io_s      cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_io_s      cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_io cvmx_ciu2_en_ppx_ip3_io_t;

/**
 * cvmx_ciu2_en_pp#_ip3_io_w1c
 */
union cvmx_ciu2_en_ppx_ip3_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_io_w1c_s  cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_io_w1c_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_io_w1c cvmx_ciu2_en_ppx_ip3_io_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_io_w1s
 */
union cvmx_ciu2_en_ppx_ip3_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_io_w1s_s  cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_io_w1s_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_io_w1s cvmx_ciu2_en_ppx_ip3_io_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mbox
 */
union cvmx_ciu2_en_ppx_ip3_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt-enable, use with CIU2_MBOX
                                                         to generate CIU2_SRC_xx_yy_MBOX */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mbox_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mbox_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mbox cvmx_ciu2_en_ppx_ip3_mbox_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mbox_w1c
 */
union cvmx_ciu2_en_ppx_ip3_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mbox_w1c cvmx_ciu2_en_ppx_ip3_mbox_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mbox_w1s
 */
union cvmx_ciu2_en_ppx_ip3_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mbox_w1s cvmx_ciu2_en_ppx_ip3_mbox_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mem
 */
union cvmx_ciu2_en_ppx_ip3_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt-enable */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mem_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mem_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mem cvmx_ciu2_en_ppx_ip3_mem_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mem_w1c
 */
union cvmx_ciu2_en_ppx_ip3_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mem_w1c cvmx_ciu2_en_ppx_ip3_mem_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mem_w1s
 */
union cvmx_ciu2_en_ppx_ip3_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mem_w1s cvmx_ciu2_en_ppx_ip3_mem_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mio
 */
union cvmx_ciu2_en_ppx_ip3_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt-enable */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt-enable */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt-enable */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt-enable */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x interrupt-enable */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines interrupt-enable */
	uint64_t mio                          : 1;  /**< MIO boot interrupt-enable */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt-enable */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupt-enable */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt-enable */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt-enable */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt-enable */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mio_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mio_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mio cvmx_ciu2_en_ppx_ip3_mio_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mio_w1c
 */
union cvmx_ciu2_en_ppx_ip3_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mio_w1c cvmx_ciu2_en_ppx_ip3_mio_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_mio_w1s
 */
union cvmx_ciu2_en_ppx_ip3_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_mio_w1s cvmx_ciu2_en_ppx_ip3_mio_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_pkt
 */
union cvmx_ciu2_en_ppx_ip3_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_pkt_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_pkt cvmx_ciu2_en_ppx_ip3_pkt_t;

/**
 * cvmx_ciu2_en_pp#_ip3_pkt_w1c
 */
union cvmx_ciu2_en_ppx_ip3_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_pkt_w1c cvmx_ciu2_en_ppx_ip3_pkt_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_pkt_w1s
 */
union cvmx_ciu2_en_ppx_ip3_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_pkt_w1s cvmx_ciu2_en_ppx_ip3_pkt_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_rml
 */
union cvmx_ciu2_en_ppx_ip3_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_rml_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_rml cvmx_ciu2_en_ppx_ip3_rml_t;

/**
 * cvmx_ciu2_en_pp#_ip3_rml_w1c
 */
union cvmx_ciu2_en_ppx_ip3_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_rml_w1c cvmx_ciu2_en_ppx_ip3_rml_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_rml_w1s
 */
union cvmx_ciu2_en_ppx_ip3_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_rml_w1s cvmx_ciu2_en_ppx_ip3_rml_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_wdog
 */
union cvmx_ciu2_en_ppx_ip3_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupt-enable */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wdog_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wdog_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_wdog cvmx_ciu2_en_ppx_ip3_wdog_t;

/**
 * cvmx_ciu2_en_pp#_ip3_wdog_w1c
 */
union cvmx_ciu2_en_ppx_ip3_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< write 1 to clear CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_wdog_w1c cvmx_ciu2_en_ppx_ip3_wdog_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_wdog_w1s
 */
union cvmx_ciu2_en_ppx_ip3_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< Write 1 to enable CIU2_EN_xx_yy_WDOG[WDOG] */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_wdog_w1s cvmx_ciu2_en_ppx_ip3_wdog_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip3_wrkq
 */
union cvmx_ciu2_en_ppx_ip3_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupt-enable */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_wrkq cvmx_ciu2_en_ppx_ip3_wrkq_t;

/**
 * cvmx_ciu2_en_pp#_ip3_wrkq_w1c
 */
union cvmx_ciu2_en_ppx_ip3_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to clear CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         For W1C bits, write 1 to clear the corresponding
                                                         CIU2_EN_xx_yy_WRKQ,write 0 to retain previous value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_wrkq_w1c cvmx_ciu2_en_ppx_ip3_wrkq_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip3_wrkq_w1s
 */
union cvmx_ciu2_en_ppx_ip3_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to enable CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         1 bit/group. For all W1S bits, write 1 to enable
                                                         corresponding CIU2_EN_xx_yy_WRKQ[WORKQ] bit,
                                                         writing 0 to retain previous value. */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip3_wrkq_w1s cvmx_ciu2_en_ppx_ip3_wrkq_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_gpio
 */
union cvmx_ciu2_en_ppx_ip4_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt-enable */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_gpio_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_gpio_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_gpio cvmx_ciu2_en_ppx_ip4_gpio_t;

/**
 * cvmx_ciu2_en_pp#_ip4_gpio_w1c
 */
union cvmx_ciu2_en_ppx_ip4_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< Write 1 to clear CIU2_EN_xx_yy_GPIO[GPIO] */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_gpio_w1c cvmx_ciu2_en_ppx_ip4_gpio_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_gpio_w1s
 */
union cvmx_ciu2_en_ppx_ip4_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupt enable,write 1 to enable CIU2_EN */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_gpio_w1s cvmx_ciu2_en_ppx_ip4_gpio_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_io
 */
union cvmx_ciu2_en_ppx_ip4_io {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt-enable */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA interrupt-enable */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit interrupt-enable
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI  interrupt-enable */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt-enable */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_io_s      cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_io_s      cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_io cvmx_ciu2_en_ppx_ip4_io_t;

/**
 * cvmx_ciu2_en_pp#_ip4_io_w1c
 */
union cvmx_ciu2_en_ppx_ip4_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_io_w1c_s  cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_io_w1c_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_io_w1c cvmx_ciu2_en_ppx_ip4_io_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_io_w1s
 */
union cvmx_ciu2_en_ppx_ip4_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[MSIRED]
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_io_w1s_s  cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_io_w1s_s  cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_io_w1s cvmx_ciu2_en_ppx_ip4_io_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mbox
 */
union cvmx_ciu2_en_ppx_ip4_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt-enable, use with CIU2_MBOX
                                                         to generate CIU2_SRC_xx_yy_MBOX */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mbox_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mbox_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mbox cvmx_ciu2_en_ppx_ip4_mbox_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mbox_w1c
 */
union cvmx_ciu2_en_ppx_ip4_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mbox_w1c cvmx_ciu2_en_ppx_ip4_mbox_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mbox_w1s
 */
union cvmx_ciu2_en_ppx_ip4_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MBOX[MBOX] */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mbox_w1s cvmx_ciu2_en_ppx_ip4_mbox_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mem
 */
union cvmx_ciu2_en_ppx_ip4_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt-enable */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mem_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mem_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mem cvmx_ciu2_en_ppx_ip4_mem_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mem_w1c
 */
union cvmx_ciu2_en_ppx_ip4_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mem_w1c cvmx_ciu2_en_ppx_ip4_mem_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mem_w1s
 */
union cvmx_ciu2_en_ppx_ip4_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mem_w1s cvmx_ciu2_en_ppx_ip4_mem_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mio
 */
union cvmx_ciu2_en_ppx_ip4_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt-enable */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt-enable */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt-enable */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt-enable */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x interrupt-enable */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines interrupt-enable */
	uint64_t mio                          : 1;  /**< MIO boot interrupt-enable */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt-enable */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupt-enable */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt-enable */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt-enable */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt-enable */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mio_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mio_s     cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mio cvmx_ciu2_en_ppx_ip4_mio_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mio_w1c
 */
union cvmx_ciu2_en_ppx_ip4_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mio_w1c cvmx_ciu2_en_ppx_ip4_mio_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_mio_w1s
 */
union cvmx_ciu2_en_ppx_ip4_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[NAND] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[SSQIQ] */
	uint64_t ipdppthr                     : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_mio_w1s cvmx_ciu2_en_ppx_ip4_mio_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_pkt
 */
union cvmx_ciu2_en_ppx_ip4_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_pkt_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x interrupt-enable */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt-enable */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt-enable */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt-enable */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_pkt cvmx_ciu2_en_ppx_ip4_pkt_t;

/**
 * cvmx_ciu2_en_pp#_ip4_pkt_w1c
 */
union cvmx_ciu2_en_ppx_ip4_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to clear CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_pkt_w1c cvmx_ciu2_en_ppx_ip4_pkt_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_pkt_w1s
 */
union cvmx_ciu2_en_ppx_ip4_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< Write 1 to enable CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_pkt_w1s cvmx_ciu2_en_ppx_ip4_pkt_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_rml
 */
union cvmx_ciu2_en_ppx_ip4_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA interrupt-enable */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_rml_s     cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt-enable */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt-enable */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt-enable */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt-enable */
	uint64_t sli                          : 1;  /**< SLI interrupt-enable */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt-enable */
	uint64_t rad                          : 1;  /**< RAD interrupt-enable */
	uint64_t tim                          : 1;  /**< TIM interrupt-enable */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt-enable */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt-enable */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt-enable */
	uint64_t pip                          : 1;  /**< PIP interrupt-enable */
	uint64_t ipd                          : 1;  /**< IPD interrupt-enable */
	uint64_t fpa                          : 1;  /**< FPA interrupt-enable */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt-enable */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_rml cvmx_ciu2_en_ppx_ip4_rml_t;

/**
 * cvmx_ciu2_en_pp#_ip4_rml_w1c
 */
union cvmx_ciu2_en_ppx_ip4_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to clear CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_rml_w1c cvmx_ciu2_en_ppx_ip4_rml_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_rml_w1s
 */
union cvmx_ciu2_en_ppx_ip4_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI_DMA] */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< Write 1 to enable CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_rml_w1s cvmx_ciu2_en_ppx_ip4_rml_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_wdog
 */
union cvmx_ciu2_en_ppx_ip4_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupt-enable */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wdog_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wdog_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_wdog cvmx_ciu2_en_ppx_ip4_wdog_t;

/**
 * cvmx_ciu2_en_pp#_ip4_wdog_w1c
 */
union cvmx_ciu2_en_ppx_ip4_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< write 1 to clear CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_wdog_w1c cvmx_ciu2_en_ppx_ip4_wdog_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_wdog_w1s
 */
union cvmx_ciu2_en_ppx_ip4_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< Write 1 to enable CIU2_EN_xx_yy_WDOG[WDOG] */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_wdog_w1s cvmx_ciu2_en_ppx_ip4_wdog_w1s_t;

/**
 * cvmx_ciu2_en_pp#_ip4_wrkq
 */
union cvmx_ciu2_en_ppx_ip4_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupt-enable */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_s    cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_s    cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_wrkq cvmx_ciu2_en_ppx_ip4_wrkq_t;

/**
 * cvmx_ciu2_en_pp#_ip4_wrkq_w1c
 */
union cvmx_ciu2_en_ppx_ip4_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to clear CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         For W1C bits, write 1 to clear the corresponding
                                                         CIU2_EN_xx_yy_WRKQ,write 0 to retain previous value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1c_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_wrkq_w1c cvmx_ciu2_en_ppx_ip4_wrkq_w1c_t;

/**
 * cvmx_ciu2_en_pp#_ip4_wrkq_w1s
 */
union cvmx_ciu2_en_ppx_ip4_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< Write 1 to enable CIU2_EN_xx_yy_WRKQ[WORKQ]
                                                         1 bit/group. For all W1S bits, write 1 to enable
                                                         corresponding CIU2_EN_xx_yy_WRKQ[WORKQ] bit,
                                                         writing 0 to retain previous value. */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1s_s cn68xxp1;
};
typedef union cvmx_ciu2_en_ppx_ip4_wrkq_w1s cvmx_ciu2_en_ppx_ip4_wrkq_w1s_t;

/**
 * cvmx_ciu2_intr_ciu_ready
 */
union cvmx_ciu2_intr_ciu_ready {
	uint64_t u64;
	struct cvmx_ciu2_intr_ciu_ready_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t ready                        : 1;  /**< Because of the delay of the IRQ updates which may
                                                         take about 200 sclk cycles, software should read
                                                         this register after servicing interrupts and wait
                                                         for response before enabling interrupt watching.
                                                         Or, the outdated interrupt will show up again.
                                                         The read back data return when all interrupts have
                                                         been serviced, and read back data is always zero.
                                                         In o68 pass2, CIU_READY gets replaced by CIU2_ACK
                                                         This becomes an internal debug feature. */
#else
	uint64_t ready                        : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_ciu2_intr_ciu_ready_s     cn68xx;
	struct cvmx_ciu2_intr_ciu_ready_s     cn68xxp1;
};
typedef union cvmx_ciu2_intr_ciu_ready cvmx_ciu2_intr_ciu_ready_t;

/**
 * cvmx_ciu2_intr_ram_ecc_ctl
 */
union cvmx_ciu2_intr_ram_ecc_ctl {
	uint64_t u64;
	struct cvmx_ciu2_intr_ram_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t flip_synd                    : 2;  /**< Testing feature. Flip Syndrom to generate single or
                                                         double bit error. FLIP_SYND[0] generate even number
                                                         -ed bits error,FLIP_SYND[1] generate odd bits error */
	uint64_t ecc_ena                      : 1;  /**< ECC Enable: When set will enable the 9bit ECC
                                                         check/correct logic for CIU interrupt enable RAM.
                                                         With ECC enabled, the ECC code will be generated
                                                         and written in the memory and then later on reads,
                                                         used to check and correct Single bit error and
                                                         detect Double Bit error. */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t flip_synd                    : 2;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_ciu2_intr_ram_ecc_ctl_s   cn68xx;
	struct cvmx_ciu2_intr_ram_ecc_ctl_s   cn68xxp1;
};
typedef union cvmx_ciu2_intr_ram_ecc_ctl cvmx_ciu2_intr_ram_ecc_ctl_t;

/**
 * cvmx_ciu2_intr_ram_ecc_st
 */
union cvmx_ciu2_intr_ram_ecc_st {
	uint64_t u64;
	struct cvmx_ciu2_intr_ram_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t addr                         : 7;  /**< Latch the address for latest sde/dde occured
                                                         The value only 0-98 indicates the different 98 IRQs
                                                         Software can read all corresponding corrected value
                                                         from CIU2_EN_PPX_IPx_*** or CIU2_EN_IOX_INT_*** and
                                                         rewite to the same address to corrected the bit err */
	uint64_t reserved_13_15               : 3;
	uint64_t syndrom                      : 9;  /**< Report the latest error syndrom */
	uint64_t reserved_2_3                 : 2;
	uint64_t dbe                          : 1;  /**< Double bit error observed. Write '1' to clear */
	uint64_t sbe                          : 1;  /**< Single bit error observed. Write '1' to clear */
#else
	uint64_t sbe                          : 1;
	uint64_t dbe                          : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t syndrom                      : 9;
	uint64_t reserved_13_15               : 3;
	uint64_t addr                         : 7;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_ciu2_intr_ram_ecc_st_s    cn68xx;
	struct cvmx_ciu2_intr_ram_ecc_st_s    cn68xxp1;
};
typedef union cvmx_ciu2_intr_ram_ecc_st cvmx_ciu2_intr_ram_ecc_st_t;

/**
 * cvmx_ciu2_intr_slowdown
 */
union cvmx_ciu2_intr_slowdown {
	uint64_t u64;
	struct cvmx_ciu2_intr_slowdown_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t ctl                          : 3;  /**< Slowdown CIU interrupt walker processing time.
                                                         IRQ2/3/4 for all 32 PPs are sent to PP (MRC) in
                                                         a serial bus to reduce global routing. There is
                                                         no backpressure mechanism designed for this scheme.
                                                         It will be only a problem when sclk is faster, this
                                                         Control will process 1 interrupt in 2^(CTL) sclks
                                                         With different setting, clock rate ratio can handle
                                                         SLOWDOWN       sclk_freq/aclk_freq ratio
                                                          0                      3
                                                          1                      6
                                                          n                      3*2^(n) */
#else
	uint64_t ctl                          : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_ciu2_intr_slowdown_s      cn68xx;
	struct cvmx_ciu2_intr_slowdown_s      cn68xxp1;
};
typedef union cvmx_ciu2_intr_slowdown cvmx_ciu2_intr_slowdown_t;

/**
 * cvmx_ciu2_msi_rcv#
 *
 * CIU2_MSI_RCV  Received MSI state bits    (Pass 2)
 *
 */
union cvmx_ciu2_msi_rcvx {
	uint64_t u64;
	struct cvmx_ciu2_msi_rcvx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t msi_rcv                      : 1;  /**< MSI state bit, set on MSI delivery or by software
                                                         "write 1" to set or "write 0" to clear.
                                                         This register is used to create the
                                                         CIU2_RAW_xx_yy_IO[MSIRED] interrupt.  See also
                                                         SLI_MSI_RCV. */
#else
	uint64_t msi_rcv                      : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_ciu2_msi_rcvx_s           cn68xx;
	struct cvmx_ciu2_msi_rcvx_s           cn68xxp1;
};
typedef union cvmx_ciu2_msi_rcvx cvmx_ciu2_msi_rcvx_t;

/**
 * cvmx_ciu2_msi_sel#
 *
 * CIU2_MSI_SEL  Received MSI SEL enable    (Pass 2)
 *
 */
union cvmx_ciu2_msi_selx {
	uint64_t u64;
	struct cvmx_ciu2_msi_selx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t pp_num                       : 5;  /**< Processor number to receive this MSI interrupt */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip_num                       : 2;  /**< Interrupt priority level to receive this MSI
                                                         interrupt (00=IP2, 01=IP3, 10=IP4, 11=rsvd) */
	uint64_t reserved_1_3                 : 3;
	uint64_t en                           : 1;  /**< Enable interrupt delivery.
                                                         Must be set for PP_NUM and IP_NUM to have effect. */
#else
	uint64_t en                           : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t ip_num                       : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t pp_num                       : 5;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_ciu2_msi_selx_s           cn68xx;
	struct cvmx_ciu2_msi_selx_s           cn68xxp1;
};
typedef union cvmx_ciu2_msi_selx cvmx_ciu2_msi_selx_t;

/**
 * cvmx_ciu2_msired_pp#_ip2
 *
 * CIU2_MSIRED_PPX_IPx      (Pass 2)
 * Contains reduced MSI interrupt numbers for delivery to software.
 * Note MSIRED delivery can only be made to PPs, not to IO; thus there are no CIU2_MSIRED_IO registers.
 */
union cvmx_ciu2_msired_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu2_msired_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t intr                         : 1;  /**< Interrupt pending */
	uint64_t reserved_17_19               : 3;
	uint64_t newint                       : 1;  /**< New interrupt to be delivered.
                                                         Internal state, for diagnostic use only.          |   $PR */
	uint64_t reserved_8_15                : 8;
	uint64_t msi_num                      : 8;  /**< MSI number causing this interrupt.
                                                         If multiple MSIs are pending to the same PP and IP,
                                                         then this contains the numerically lowest MSI number */
#else
	uint64_t msi_num                      : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t newint                       : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t intr                         : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_ciu2_msired_ppx_ip2_s     cn68xx;
	struct cvmx_ciu2_msired_ppx_ip2_s     cn68xxp1;
};
typedef union cvmx_ciu2_msired_ppx_ip2 cvmx_ciu2_msired_ppx_ip2_t;

/**
 * cvmx_ciu2_msired_pp#_ip3
 */
union cvmx_ciu2_msired_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu2_msired_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t intr                         : 1;  /**< Interrupt pending */
	uint64_t reserved_17_19               : 3;
	uint64_t newint                       : 1;  /**< New interrupt to be delivered.
                                                         Internal state, for diagnostic use only.          |   $PR */
	uint64_t reserved_8_15                : 8;
	uint64_t msi_num                      : 8;  /**< MSI number causing this interrupt.
                                                         If multiple MSIs are pending to the same PP and IP,
                                                         then this contains the numerically lowest MSI number */
#else
	uint64_t msi_num                      : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t newint                       : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t intr                         : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_ciu2_msired_ppx_ip3_s     cn68xx;
	struct cvmx_ciu2_msired_ppx_ip3_s     cn68xxp1;
};
typedef union cvmx_ciu2_msired_ppx_ip3 cvmx_ciu2_msired_ppx_ip3_t;

/**
 * cvmx_ciu2_msired_pp#_ip4
 */
union cvmx_ciu2_msired_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu2_msired_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t intr                         : 1;  /**< Interrupt pending */
	uint64_t reserved_17_19               : 3;
	uint64_t newint                       : 1;  /**< New interrupt to be delivered.
                                                         Internal state, for diagnostic use only.          |   $PR */
	uint64_t reserved_8_15                : 8;
	uint64_t msi_num                      : 8;  /**< MSI number causing this interrupt.
                                                         If multiple MSIs are pending to the same PP and IP,
                                                         then this contains the numerically lowest MSI number */
#else
	uint64_t msi_num                      : 8;
	uint64_t reserved_8_15                : 8;
	uint64_t newint                       : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t intr                         : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_ciu2_msired_ppx_ip4_s     cn68xx;
	struct cvmx_ciu2_msired_ppx_ip4_s     cn68xxp1;
};
typedef union cvmx_ciu2_msired_ppx_ip4 cvmx_ciu2_msired_ppx_ip4_t;

/**
 * cvmx_ciu2_raw_io#_int_gpio
 */
union cvmx_ciu2_raw_iox_int_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts
                                                         For GPIO, all 98 RAW readout will be same value */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_gpio_s   cn68xx;
	struct cvmx_ciu2_raw_iox_int_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_gpio cvmx_ciu2_raw_iox_int_gpio_t;

/**
 * cvmx_ciu2_raw_io#_int_io
 */
union cvmx_ciu2_raw_iox_int_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt
                                                         See PEMx_INT_SUM (enabled by PEMx_INT_ENB) */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA software enable
                                                         See CIU_PCI_INTA */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit, copy of
                                                         CIU2_MSIRED_PPx_IPy.INT, all IO interrupts
                                                         CIU2_RAW_IOX_INT_IO[MSIRED] always zero.
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI
                                                         See SLI_MSI_RCVn for bit <40+n> */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D
                                                         PCI_INTR[3] = INTD
                                                         PCI_INTR[2] = INTC
                                                         PCI_INTR[1] = INTB
                                                         PCI_INTR[0] = INTA
                                                         Refer to "Receiving Emulated INTA/INTB/
                                                         INTC/INTD" in the SLI chapter of the spec
                                                         For IO, all 98 RAW readout will be different */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_io_s     cn68xx;
	struct cvmx_ciu2_raw_iox_int_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_io cvmx_ciu2_raw_iox_int_io_t;

/**
 * cvmx_ciu2_raw_io#_int_mem
 */
union cvmx_ciu2_raw_iox_int_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt
                                                         See LMC*_INT
                                                         For MEM, all 98 RAW readout will be same value */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_mem_s    cn68xx;
	struct cvmx_ciu2_raw_iox_int_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_mem cvmx_ciu2_raw_iox_int_mem_t;

/**
 * cvmx_ciu2_raw_io#_int_mio
 */
union cvmx_ciu2_raw_iox_int_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt
                                                         See MIO_RST_INT */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt
                                                         Set when HW decrements MIO_PTP_EVT_CNT to zero */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB EHCI or OHCI Interrupt
                                                         See UAHC0_EHCI_USBSTS UAHC0_OHCI0_HCINTERRUPTSTATUS */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt
                                                         See UCTL*_INT_REG */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts
                                                         See MIO_UARTn_IIR[IID] for bit <34+n> */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt
                                                         See MIO_TWSx_INT */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt
                                                         See MIO_BOOT_DMA_INT*, MIO_NDF_DMA_INT */
	uint64_t mio                          : 1;  /**< MIO boot interrupt
                                                         See MIO_BOOT_ERR */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt
                                                         See NDF_INT */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts
                                                         Set any time the corresponding CIU timer expires */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt
                                                         Set any time PIP/IPD drops a packet */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt
                                                         See SSO_IQ_INT */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt
                                                         See IPD_PORT_QOS_INT*
                                                         For MIO, all 98 RAW readout will be same value */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_mio_s    cn68xx;
	struct cvmx_ciu2_raw_iox_int_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_mio cvmx_ciu2_raw_iox_int_mio_t;

/**
 * cvmx_ciu2_raw_io#_int_pkt
 */
union cvmx_ciu2_raw_iox_int_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt pulse */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_pkt_s    cn68xx;
	struct cvmx_ciu2_raw_iox_int_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_pkt cvmx_ciu2_raw_iox_int_pkt_t;

/**
 * cvmx_ciu2_raw_io#_int_rml
 */
union cvmx_ciu2_raw_iox_int_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_rml_s    cn68xx;
	struct cvmx_ciu2_raw_iox_int_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_rml cvmx_ciu2_raw_iox_int_rml_t;

/**
 * cvmx_ciu2_raw_io#_int_wdog
 */
union cvmx_ciu2_raw_iox_int_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts
                                                         For WDOG, all 98 RAW readout will be same value */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_wdog_s   cn68xx;
	struct cvmx_ciu2_raw_iox_int_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_wdog cvmx_ciu2_raw_iox_int_wdog_t;

/**
 * cvmx_ciu2_raw_io#_int_wrkq
 */
union cvmx_ciu2_raw_iox_int_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupts
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO.
                                                          For WRKQ, all 98 RAW readout will be same value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_wrkq_s   cn68xx;
	struct cvmx_ciu2_raw_iox_int_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_iox_int_wrkq cvmx_ciu2_raw_iox_int_wrkq_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_gpio
 */
union cvmx_ciu2_raw_ppx_ip2_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts
                                                         For GPIO, all 98 RAW readout will be same value */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_gpio_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_gpio cvmx_ciu2_raw_ppx_ip2_gpio_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_io
 */
union cvmx_ciu2_raw_ppx_ip2_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt
                                                         See PEMx_INT_SUM (enabled by PEMx_INT_ENB) */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA software enable
                                                         See CIU_PCI_INTA */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit, copy of
                                                         CIU2_MSIRED_PPx_IPy.INT, all IO interrupts
                                                         CIU2_RAW_IOX_INT_IO[MSIRED] always zero.
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI
                                                         See SLI_MSI_RCVn for bit <40+n> */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D
                                                         PCI_INTR[3] = INTD
                                                         PCI_INTR[2] = INTC
                                                         PCI_INTR[1] = INTB
                                                         PCI_INTR[0] = INTA
                                                         Refer to "Receiving Emulated INTA/INTB/
                                                         INTC/INTD" in the SLI chapter of the spec
                                                         For IO, all 98 RAW readout will be different */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_io_s     cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_io cvmx_ciu2_raw_ppx_ip2_io_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_mem
 */
union cvmx_ciu2_raw_ppx_ip2_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt
                                                         See LMC*_INT
                                                         For MEM, all 98 RAW readout will be same value */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_mem_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_mem cvmx_ciu2_raw_ppx_ip2_mem_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_mio
 */
union cvmx_ciu2_raw_ppx_ip2_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt
                                                         See MIO_RST_INT */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt
                                                         Set when HW decrements MIO_PTP_EVT_CNT to zero */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB EHCI or OHCI Interrupt
                                                         See UAHC0_EHCI_USBSTS UAHC0_OHCI0_HCINTERRUPTSTATUS */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt
                                                         See UCTL*_INT_REG */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts
                                                         See MIO_UARTn_IIR[IID] for bit <34+n> */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt
                                                         See MIO_TWSx_INT */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt
                                                         See MIO_BOOT_DMA_INT*, MIO_NDF_DMA_INT */
	uint64_t mio                          : 1;  /**< MIO boot interrupt
                                                         See MIO_BOOT_ERR */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt
                                                         See NDF_INT */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts
                                                         Set any time the corresponding CIU timer expires */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt
                                                         Set any time PIP/IPD drops a packet */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt
                                                         See SSO_IQ_INT */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt
                                                         See IPD_PORT_QOS_INT*
                                                         For MIO, all 98 RAW readout will be same value */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_mio_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_mio cvmx_ciu2_raw_ppx_ip2_mio_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_pkt
 */
union cvmx_ciu2_raw_ppx_ip2_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt pulse */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_pkt_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_pkt cvmx_ciu2_raw_ppx_ip2_pkt_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_rml
 */
union cvmx_ciu2_raw_ppx_ip2_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_rml_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_rml cvmx_ciu2_raw_ppx_ip2_rml_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_wdog
 */
union cvmx_ciu2_raw_ppx_ip2_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts
                                                         For WDOG, all 98 RAW readout will be same value */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_wdog_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_wdog cvmx_ciu2_raw_ppx_ip2_wdog_t;

/**
 * cvmx_ciu2_raw_pp#_ip2_wrkq
 */
union cvmx_ciu2_raw_ppx_ip2_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupts
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO.
                                                          For WRKQ, all 98 RAW readout will be same value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_wrkq_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip2_wrkq cvmx_ciu2_raw_ppx_ip2_wrkq_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_gpio
 */
union cvmx_ciu2_raw_ppx_ip3_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts
                                                         For GPIO, all 98 RAW readout will be same value */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_gpio_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_gpio cvmx_ciu2_raw_ppx_ip3_gpio_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_io
 */
union cvmx_ciu2_raw_ppx_ip3_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt
                                                         See PEMx_INT_SUM (enabled by PEMx_INT_ENB) */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA software enable
                                                         See CIU_PCI_INTA */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit, copy of
                                                         CIU2_MSIRED_PPx_IPy.INT, all IO interrupts
                                                         CIU2_RAW_IOX_INT_IO[MSIRED] always zero.
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI
                                                         See SLI_MSI_RCVn for bit <40+n> */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D
                                                         PCI_INTR[3] = INTD
                                                         PCI_INTR[2] = INTC
                                                         PCI_INTR[1] = INTB
                                                         PCI_INTR[0] = INTA
                                                         Refer to "Receiving Emulated INTA/INTB/
                                                         INTC/INTD" in the SLI chapter of the spec
                                                         For IO, all 98 RAW readout will be different */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_io_s     cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_io cvmx_ciu2_raw_ppx_ip3_io_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_mem
 */
union cvmx_ciu2_raw_ppx_ip3_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt
                                                         See LMC*_INT
                                                         For MEM, all 98 RAW readout will be same value */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_mem_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_mem cvmx_ciu2_raw_ppx_ip3_mem_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_mio
 */
union cvmx_ciu2_raw_ppx_ip3_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt
                                                         See MIO_RST_INT */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt
                                                         Set when HW decrements MIO_PTP_EVT_CNT to zero */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB EHCI or OHCI Interrupt
                                                         See UAHC0_EHCI_USBSTS UAHC0_OHCI0_HCINTERRUPTSTATUS */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt
                                                         See UCTL*_INT_REG */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts
                                                         See MIO_UARTn_IIR[IID] for bit <34+n> */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt
                                                         See MIO_TWSx_INT */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt
                                                         See MIO_BOOT_DMA_INT*, MIO_NDF_DMA_INT */
	uint64_t mio                          : 1;  /**< MIO boot interrupt
                                                         See MIO_BOOT_ERR */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt
                                                         See NDF_INT */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts
                                                         Set any time the corresponding CIU timer expires */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt
                                                         Set any time PIP/IPD drops a packet */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt
                                                         See SSO_IQ_INT */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt
                                                         See IPD_PORT_QOS_INT*
                                                         For MIO, all 98 RAW readout will be same value */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_mio_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_mio cvmx_ciu2_raw_ppx_ip3_mio_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_pkt
 */
union cvmx_ciu2_raw_ppx_ip3_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt pulse */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_pkt_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_pkt cvmx_ciu2_raw_ppx_ip3_pkt_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_rml
 */
union cvmx_ciu2_raw_ppx_ip3_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_rml_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_rml cvmx_ciu2_raw_ppx_ip3_rml_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_wdog
 */
union cvmx_ciu2_raw_ppx_ip3_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts
                                                         For WDOG, all 98 RAW readout will be same value */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_wdog_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_wdog cvmx_ciu2_raw_ppx_ip3_wdog_t;

/**
 * cvmx_ciu2_raw_pp#_ip3_wrkq
 */
union cvmx_ciu2_raw_ppx_ip3_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupts
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO.
                                                          For WRKQ, all 98 RAW readout will be same value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_wrkq_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip3_wrkq cvmx_ciu2_raw_ppx_ip3_wrkq_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_gpio
 */
union cvmx_ciu2_raw_ppx_ip4_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts
                                                         For GPIO, all 98 RAW readout will be same value */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_gpio_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_gpio cvmx_ciu2_raw_ppx_ip4_gpio_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_io
 */
union cvmx_ciu2_raw_ppx_ip4_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt
                                                         See PEMx_INT_SUM (enabled by PEMx_INT_ENB) */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA software enable
                                                         See CIU_PCI_INTA */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit, copy of
                                                         CIU2_MSIRED_PPx_IPy.INT, all IO interrupts
                                                         CIU2_RAW_IOX_INT_IO[MSIRED] always zero.
                                                         This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI
                                                         See SLI_MSI_RCVn for bit <40+n> */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D
                                                         PCI_INTR[3] = INTD
                                                         PCI_INTR[2] = INTC
                                                         PCI_INTR[1] = INTB
                                                         PCI_INTR[0] = INTA
                                                         Refer to "Receiving Emulated INTA/INTB/
                                                         INTC/INTD" in the SLI chapter of the spec
                                                         For IO, all 98 RAW readout will be different */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_io_s     cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_io cvmx_ciu2_raw_ppx_ip4_io_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_mem
 */
union cvmx_ciu2_raw_ppx_ip4_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt
                                                         See LMC*_INT
                                                         For MEM, all 98 RAW readout will be same value */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_mem_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_mem cvmx_ciu2_raw_ppx_ip4_mem_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_mio
 */
union cvmx_ciu2_raw_ppx_ip4_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt
                                                         See MIO_RST_INT */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt
                                                         Set when HW decrements MIO_PTP_EVT_CNT to zero */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB EHCI or OHCI Interrupt
                                                         See UAHC0_EHCI_USBSTS UAHC0_OHCI0_HCINTERRUPTSTATUS */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt
                                                         See UCTL*_INT_REG */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts
                                                         See MIO_UARTn_IIR[IID] for bit <34+n> */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt
                                                         See MIO_TWSx_INT */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt
                                                         See MIO_BOOT_DMA_INT*, MIO_NDF_DMA_INT */
	uint64_t mio                          : 1;  /**< MIO boot interrupt
                                                         See MIO_BOOT_ERR */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt
                                                         See NDF_INT */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts
                                                         Set any time the corresponding CIU timer expires */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt
                                                         Set any time PIP/IPD drops a packet */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt
                                                         See SSO_IQ_INT */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port counter threshold interrupt
                                                         See IPD_PORT_QOS_INT*
                                                         For MIO, all 98 RAW readout will be same value */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_mio_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_mio cvmx_ciu2_raw_ppx_ip4_mio_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_pkt
 */
union cvmx_ciu2_raw_ppx_ip4_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupt pulse */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_pkt_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts
                                                         See MIX*_ISR */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt
                                                         See AGL_GMX_RX*_INT_REG, AGL_GMX_TX_INT_REG */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX 0-4 packet drop interrupt pulse
                                                         Set any time corresponding GMX drops a packet */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX 0-4 interrupt
                                                         See GMX*_RX*_INT_REG, GMX*_TX_INT_REG,
                                                         PCS0_INT*_REG, PCSX*_INT_REG
                                                         For PKT, all 98 RAW readout will be same value */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_pkt cvmx_ciu2_raw_ppx_ip4_pkt_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_rml
 */
union cvmx_ciu2_raw_ppx_ip4_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_rml_s    cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt
                                                         See TRA_INT_STATUS */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt
                                                         See L2C_INT_REG */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt
                                                         See DFA_ERROR */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt
                                                         See DPI_INT_REG */
	uint64_t sli                          : 1;  /**< SLI interrupt
                                                         See SLI_INT_SUM (enabled by SLI_INT_ENB_CIU) */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt
                                                         See KEY_INT_SUM */
	uint64_t rad                          : 1;  /**< RAD interrupt
                                                         See RAD_REG_ERROR */
	uint64_t tim                          : 1;  /**< TIM interrupt
                                                         See TIM_INT_ECCERR, TIM_INT0 */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt
                                                         See ZIP_INT_REG */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt
                                                         See SSO_ERR */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt
                                                         See PKO_REG_ERROR */
	uint64_t pip                          : 1;  /**< PIP interrupt
                                                         See PIP_INT_REG */
	uint64_t ipd                          : 1;  /**< IPD interrupt
                                                         See IPD_INT_SUM */
	uint64_t fpa                          : 1;  /**< FPA interrupt
                                                         See FPA_INT_SUM */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt
                                                         See IOB_INT_SUM
                                                         For RML, all 98 RAW readout will be same value */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_rml cvmx_ciu2_raw_ppx_ip4_rml_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_wdog
 */
union cvmx_ciu2_raw_ppx_ip4_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts
                                                         For WDOG, all 98 RAW readout will be same value */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_wdog_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_wdog cvmx_ciu2_raw_ppx_ip4_wdog_t;

/**
 * cvmx_ciu2_raw_pp#_ip4_wrkq
 */
union cvmx_ciu2_raw_ppx_ip4_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue interrupts
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO.
                                                          For WRKQ, all 98 RAW readout will be same value */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_wrkq_s   cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_raw_ppx_ip4_wrkq cvmx_ciu2_raw_ppx_ip4_wrkq_t;

/**
 * cvmx_ciu2_src_io#_int_gpio
 */
union cvmx_ciu2_src_iox_int_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts source */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_gpio_s   cn68xx;
	struct cvmx_ciu2_src_iox_int_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_gpio cvmx_ciu2_src_iox_int_gpio_t;

/**
 * cvmx_ciu2_src_io#_int_io
 */
union cvmx_ciu2_src_iox_int_io {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt source
                                                         CIU2_RAW_IO[PEM] & CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA source
                                                         CIU2_RAW_IO[PCI_INTA] & CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit source
                                                         CIU2_RAW_IO[MSIRED] & CIU2_EN_xx_yy_IO[MSIRED]
                                                          This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI source
                                                         CIU2_RAW_IO[PCI_MSI] & CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt source
                                                         CIU2_RAW_IO[PCI_INTR] &CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_io_s     cn68xx;
	struct cvmx_ciu2_src_iox_int_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_io cvmx_ciu2_src_iox_int_io_t;

/**
 * cvmx_ciu2_src_io#_int_mbox
 */
union cvmx_ciu2_src_iox_int_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt Source (RAW & ENABLE)
                                                         For CIU2_SRC_PPX_IPx_MBOX:
                                                         Four mailbox interrupts for entries 0-31
                                                         RAW & ENABLE
                                                          [3]  is the or of <31:24> of CIU2_MBOX
                                                          [2]  is the or of <23:16> of CIU2_MBOX
                                                          [1]  is the or of <15:8> of CIU2_MBOX
                                                          [0]  is the or of <7:0> of CIU2_MBOX
                                                          CIU2_MBOX value can be read out via CSR address
                                                          CIU_MBOX_SET/CLR
                                                         For CIU2_SRC_IOX_INT_MBOX:
                                                           always zero */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_mbox_s   cn68xx;
	struct cvmx_ciu2_src_iox_int_mbox_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_mbox cvmx_ciu2_src_iox_int_mbox_t;

/**
 * cvmx_ciu2_src_io#_int_mem
 */
union cvmx_ciu2_src_iox_int_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt source
                                                         CIU2_RAW_MEM[LMC] & CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_mem_s    cn68xx;
	struct cvmx_ciu2_src_iox_int_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_mem cvmx_ciu2_src_iox_int_mem_t;

/**
 * cvmx_ciu2_src_io#_int_mio
 */
union cvmx_ciu2_src_iox_int_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt source
                                                         CIU2_RAW_MIO[RST] & CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt source
                                                         CIU2_RAW_MIO[PTP] & CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt source
                                                         CIU2_RAW_MIO[USB_HCI] & CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt source
                                                         CIU2_RAW_MIO[USB_UCTL] &CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts source
                                                         CIU2_RAW_MIO[UART] & CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt source
                                                         CIU2_RAW_MIO[TWSI] & CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt source
                                                         CIU2_RAW_MIO[BOOTDMA] & CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< MIO boot interrupt source
                                                         CIU2_RAW_MIO[MIO] & CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt source
                                                         CIU2_RAW_MIO[NAND] & CIU2_EN_xx_yy_MIO[NANAD] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts source
                                                         CIU2_RAW_MIO[TIMER] & CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt source
                                                         CIU2_RAW_MIO[IPD_DRP] & CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt source
                                                         CIU2_RAW_MIO[SSOIQ] & CIU2_EN_xx_yy_MIO[SSOIQ] */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port cnt threshold interrupt source
                                                         CIU2_RAW_MIO[IPDPPTHR] &CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_mio_s    cn68xx;
	struct cvmx_ciu2_src_iox_int_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_mio cvmx_ciu2_src_iox_int_mio_t;

/**
 * cvmx_ciu2_src_io#_int_pkt
 */
union cvmx_ciu2_src_iox_int_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupts source
                                                         CIU2_RAW_PKT[ILK_DRP] & CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_pkt_s    cn68xx;
	struct cvmx_ciu2_src_iox_int_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_pkt cvmx_ciu2_src_iox_int_pkt_t;

/**
 * cvmx_ciu2_src_io#_int_rml
 */
union cvmx_ciu2_src_iox_int_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_rml_s    cn68xx;
	struct cvmx_ciu2_src_iox_int_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_rml cvmx_ciu2_src_iox_int_rml_t;

/**
 * cvmx_ciu2_src_io#_int_wdog
 */
union cvmx_ciu2_src_iox_int_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts source
                                                         CIU2_RAW_WDOG & CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_wdog_s   cn68xx;
	struct cvmx_ciu2_src_iox_int_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_wdog cvmx_ciu2_src_iox_int_wdog_t;

/**
 * cvmx_ciu2_src_io#_int_wrkq
 */
union cvmx_ciu2_src_iox_int_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue intr source,
                                                         CIU2_RAW_WRKQ & CIU2_EN_xx_yy_WRKQ */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_wrkq_s   cn68xx;
	struct cvmx_ciu2_src_iox_int_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_iox_int_wrkq cvmx_ciu2_src_iox_int_wrkq_t;

/**
 * cvmx_ciu2_src_pp#_ip2_gpio
 */
union cvmx_ciu2_src_ppx_ip2_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts source */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_gpio_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_gpio cvmx_ciu2_src_ppx_ip2_gpio_t;

/**
 * cvmx_ciu2_src_pp#_ip2_io
 */
union cvmx_ciu2_src_ppx_ip2_io {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt source
                                                         CIU2_RAW_IO[PEM] & CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA source
                                                         CIU2_RAW_IO[PCI_INTA] & CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit source
                                                         CIU2_RAW_IO[MSIRED] & CIU2_EN_xx_yy_IO[MSIRED]
                                                          This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI source
                                                         CIU2_RAW_IO[PCI_MSI] & CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt source
                                                         CIU2_RAW_IO[PCI_INTR] &CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_io_s     cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_io cvmx_ciu2_src_ppx_ip2_io_t;

/**
 * cvmx_ciu2_src_pp#_ip2_mbox
 */
union cvmx_ciu2_src_ppx_ip2_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt Source (RAW & ENABLE)
                                                         For CIU2_SRC_PPX_IPx_MBOX:
                                                         Four mailbox interrupts for entries 0-31
                                                         RAW & ENABLE
                                                          [3]  is the or of <31:24> of CIU2_MBOX
                                                          [2]  is the or of <23:16> of CIU2_MBOX
                                                          [1]  is the or of <15:8> of CIU2_MBOX
                                                          [0]  is the or of <7:0> of CIU2_MBOX
                                                          CIU2_MBOX value can be read out via CSR address
                                                          CIU_MBOX_SET/CLR
                                                         For CIU2_SRC_IOX_INT_MBOX:
                                                           always zero */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_mbox_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_mbox_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_mbox cvmx_ciu2_src_ppx_ip2_mbox_t;

/**
 * cvmx_ciu2_src_pp#_ip2_mem
 */
union cvmx_ciu2_src_ppx_ip2_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt source
                                                         CIU2_RAW_MEM[LMC] & CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_mem_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_mem cvmx_ciu2_src_ppx_ip2_mem_t;

/**
 * cvmx_ciu2_src_pp#_ip2_mio
 */
union cvmx_ciu2_src_ppx_ip2_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt source
                                                         CIU2_RAW_MIO[RST] & CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt source
                                                         CIU2_RAW_MIO[PTP] & CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt source
                                                         CIU2_RAW_MIO[USB_HCI] & CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt source
                                                         CIU2_RAW_MIO[USB_UCTL] &CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts source
                                                         CIU2_RAW_MIO[UART] & CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt source
                                                         CIU2_RAW_MIO[TWSI] & CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt source
                                                         CIU2_RAW_MIO[BOOTDMA] & CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< MIO boot interrupt source
                                                         CIU2_RAW_MIO[MIO] & CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt source
                                                         CIU2_RAW_MIO[NAND] & CIU2_EN_xx_yy_MIO[NANAD] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts source
                                                         CIU2_RAW_MIO[TIMER] & CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt source
                                                         CIU2_RAW_MIO[IPD_DRP] & CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt source
                                                         CIU2_RAW_MIO[SSOIQ] & CIU2_EN_xx_yy_MIO[SSOIQ] */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port cnt threshold interrupt source
                                                         CIU2_RAW_MIO[IPDPPTHR] &CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_mio_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_mio cvmx_ciu2_src_ppx_ip2_mio_t;

/**
 * cvmx_ciu2_src_pp#_ip2_pkt
 */
union cvmx_ciu2_src_ppx_ip2_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupts source
                                                         CIU2_RAW_PKT[ILK_DRP] & CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_pkt_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_pkt cvmx_ciu2_src_ppx_ip2_pkt_t;

/**
 * cvmx_ciu2_src_pp#_ip2_rml
 */
union cvmx_ciu2_src_ppx_ip2_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_rml_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_rml cvmx_ciu2_src_ppx_ip2_rml_t;

/**
 * cvmx_ciu2_src_pp#_ip2_wdog
 */
union cvmx_ciu2_src_ppx_ip2_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts source
                                                         CIU2_RAW_WDOG & CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_wdog_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_wdog cvmx_ciu2_src_ppx_ip2_wdog_t;

/**
 * cvmx_ciu2_src_pp#_ip2_wrkq
 *
 * All SRC values is generated by AND Raw value (CIU2_RAW_XXX) with CIU2_EN_PPX_IPx_XXX
 *
 */
union cvmx_ciu2_src_ppx_ip2_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue intr source,
                                                         CIU2_RAW_WRKQ & CIU2_EN_xx_yy_WRKQ */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_wrkq_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip2_wrkq cvmx_ciu2_src_ppx_ip2_wrkq_t;

/**
 * cvmx_ciu2_src_pp#_ip3_gpio
 */
union cvmx_ciu2_src_ppx_ip3_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts source */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_gpio_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_gpio cvmx_ciu2_src_ppx_ip3_gpio_t;

/**
 * cvmx_ciu2_src_pp#_ip3_io
 */
union cvmx_ciu2_src_ppx_ip3_io {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt source
                                                         CIU2_RAW_IO[PEM] & CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA source
                                                         CIU2_RAW_IO[PCI_INTA] & CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit source
                                                         CIU2_RAW_IO[MSIRED] & CIU2_EN_xx_yy_IO[MSIRED]
                                                          This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI source
                                                         CIU2_RAW_IO[PCI_MSI] & CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt source
                                                         CIU2_RAW_IO[PCI_INTR] &CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_io_s     cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_io cvmx_ciu2_src_ppx_ip3_io_t;

/**
 * cvmx_ciu2_src_pp#_ip3_mbox
 */
union cvmx_ciu2_src_ppx_ip3_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt Source (RAW & ENABLE)
                                                         For CIU2_SRC_PPX_IPx_MBOX:
                                                         Four mailbox interrupts for entries 0-31
                                                         RAW & ENABLE
                                                          [3]  is the or of <31:24> of CIU2_MBOX
                                                          [2]  is the or of <23:16> of CIU2_MBOX
                                                          [1]  is the or of <15:8> of CIU2_MBOX
                                                          [0]  is the or of <7:0> of CIU2_MBOX
                                                          CIU2_MBOX value can be read out via CSR address
                                                          CIU_MBOX_SET/CLR
                                                         For CIU2_SRC_IOX_INT_MBOX:
                                                           always zero */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_mbox_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_mbox_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_mbox cvmx_ciu2_src_ppx_ip3_mbox_t;

/**
 * cvmx_ciu2_src_pp#_ip3_mem
 */
union cvmx_ciu2_src_ppx_ip3_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt source
                                                         CIU2_RAW_MEM[LMC] & CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_mem_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_mem cvmx_ciu2_src_ppx_ip3_mem_t;

/**
 * cvmx_ciu2_src_pp#_ip3_mio
 */
union cvmx_ciu2_src_ppx_ip3_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt source
                                                         CIU2_RAW_MIO[RST] & CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt source
                                                         CIU2_RAW_MIO[PTP] & CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt source
                                                         CIU2_RAW_MIO[USB_HCI] & CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt source
                                                         CIU2_RAW_MIO[USB_UCTL] &CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts source
                                                         CIU2_RAW_MIO[UART] & CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt source
                                                         CIU2_RAW_MIO[TWSI] & CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt source
                                                         CIU2_RAW_MIO[BOOTDMA] & CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< MIO boot interrupt source
                                                         CIU2_RAW_MIO[MIO] & CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt source
                                                         CIU2_RAW_MIO[NAND] & CIU2_EN_xx_yy_MIO[NANAD] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts source
                                                         CIU2_RAW_MIO[TIMER] & CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt source
                                                         CIU2_RAW_MIO[IPD_DRP] & CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt source
                                                         CIU2_RAW_MIO[SSOIQ] & CIU2_EN_xx_yy_MIO[SSOIQ] */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port cnt threshold interrupt source
                                                         CIU2_RAW_MIO[IPDPPTHR] &CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_mio_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_mio cvmx_ciu2_src_ppx_ip3_mio_t;

/**
 * cvmx_ciu2_src_pp#_ip3_pkt
 */
union cvmx_ciu2_src_ppx_ip3_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupts source
                                                         CIU2_RAW_PKT[ILK_DRP] & CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_pkt_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_pkt cvmx_ciu2_src_ppx_ip3_pkt_t;

/**
 * cvmx_ciu2_src_pp#_ip3_rml
 */
union cvmx_ciu2_src_ppx_ip3_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_rml_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_rml cvmx_ciu2_src_ppx_ip3_rml_t;

/**
 * cvmx_ciu2_src_pp#_ip3_wdog
 */
union cvmx_ciu2_src_ppx_ip3_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts source
                                                         CIU2_RAW_WDOG & CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_wdog_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_wdog cvmx_ciu2_src_ppx_ip3_wdog_t;

/**
 * cvmx_ciu2_src_pp#_ip3_wrkq
 */
union cvmx_ciu2_src_ppx_ip3_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue intr source,
                                                         CIU2_RAW_WRKQ & CIU2_EN_xx_yy_WRKQ */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_wrkq_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip3_wrkq cvmx_ciu2_src_ppx_ip3_wrkq_t;

/**
 * cvmx_ciu2_src_pp#_ip4_gpio
 */
union cvmx_ciu2_src_ppx_ip4_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t gpio                         : 16; /**< 16 GPIO interrupts source */
#else
	uint64_t gpio                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_gpio_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_gpio_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_gpio cvmx_ciu2_src_ppx_ip4_gpio_t;

/**
 * cvmx_ciu2_src_pp#_ip4_io
 */
union cvmx_ciu2_src_ppx_ip4_io {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t pem                          : 2;  /**< PEMx interrupt source
                                                         CIU2_RAW_IO[PEM] & CIU2_EN_xx_yy_IO[PEM] */
	uint64_t reserved_18_31               : 14;
	uint64_t pci_inta                     : 2;  /**< PCI_INTA source
                                                         CIU2_RAW_IO[PCI_INTA] & CIU2_EN_xx_yy_IO[PCI_INTA] */
	uint64_t reserved_13_15               : 3;
	uint64_t msired                       : 1;  /**< MSI summary bit source
                                                         CIU2_RAW_IO[MSIRED] & CIU2_EN_xx_yy_IO[MSIRED]
                                                          This bit may not be functional in pass 1. */
	uint64_t pci_msi                      : 4;  /**< PCIe/sRIO MSI source
                                                         CIU2_RAW_IO[PCI_MSI] & CIU2_EN_xx_yy_IO[PCI_MSI] */
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_intr                     : 4;  /**< PCIe INTA/B/C/D interrupt source
                                                         CIU2_RAW_IO[PCI_INTR] &CIU2_EN_xx_yy_IO[PCI_INTR] */
#else
	uint64_t pci_intr                     : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t pci_msi                      : 4;
	uint64_t msired                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t pci_inta                     : 2;
	uint64_t reserved_18_31               : 14;
	uint64_t pem                          : 2;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_io_s     cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_io_s     cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_io cvmx_ciu2_src_ppx_ip4_io_t;

/**
 * cvmx_ciu2_src_pp#_ip4_mbox
 */
union cvmx_ciu2_src_ppx_ip4_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mbox                         : 4;  /**< Mailbox interrupt Source (RAW & ENABLE)
                                                         For CIU2_SRC_PPX_IPx_MBOX:
                                                         Four mailbox interrupts for entries 0-31
                                                         RAW & ENABLE
                                                          [3]  is the or of <31:24> of CIU2_MBOX
                                                          [2]  is the or of <23:16> of CIU2_MBOX
                                                          [1]  is the or of <15:8> of CIU2_MBOX
                                                          [0]  is the or of <7:0> of CIU2_MBOX
                                                          CIU2_MBOX value can be read out via CSR address
                                                          CIU_MBOX_SET/CLR
                                                         For CIU2_SRC_IOX_INT_MBOX:
                                                           always zero */
#else
	uint64_t mbox                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_mbox_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_mbox_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_mbox cvmx_ciu2_src_ppx_ip4_mbox_t;

/**
 * cvmx_ciu2_src_pp#_ip4_mem
 */
union cvmx_ciu2_src_ppx_ip4_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lmc                          : 4;  /**< LMC* interrupt source
                                                         CIU2_RAW_MEM[LMC] & CIU2_EN_xx_yy_MEM[LMC] */
#else
	uint64_t lmc                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_mem_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_mem_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_mem cvmx_ciu2_src_ppx_ip4_mem_t;

/**
 * cvmx_ciu2_src_pp#_ip4_mio
 */
union cvmx_ciu2_src_ppx_ip4_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rst                          : 1;  /**< MIO RST interrupt source
                                                         CIU2_RAW_MIO[RST] & CIU2_EN_xx_yy_MIO[RST] */
	uint64_t reserved_49_62               : 14;
	uint64_t ptp                          : 1;  /**< PTP interrupt source
                                                         CIU2_RAW_MIO[PTP] & CIU2_EN_xx_yy_MIO[PTP] */
	uint64_t reserved_45_47               : 3;
	uint64_t usb_hci                      : 1;  /**< USB HCI Interrupt source
                                                         CIU2_RAW_MIO[USB_HCI] & CIU2_EN_xx_yy_MIO[USB_HCI] */
	uint64_t reserved_41_43               : 3;
	uint64_t usb_uctl                     : 1;  /**< USB UCTL* interrupt source
                                                         CIU2_RAW_MIO[USB_UCTL] &CIU2_EN_xx_yy_MIO[USB_UCTL] */
	uint64_t reserved_38_39               : 2;
	uint64_t uart                         : 2;  /**< Two UART interrupts source
                                                         CIU2_RAW_MIO[UART] & CIU2_EN_xx_yy_MIO[UART] */
	uint64_t reserved_34_35               : 2;
	uint64_t twsi                         : 2;  /**< TWSI x Interrupt source
                                                         CIU2_RAW_MIO[TWSI] & CIU2_EN_xx_yy_MIO[TWSI] */
	uint64_t reserved_19_31               : 13;
	uint64_t bootdma                      : 1;  /**< Boot bus DMA engines Interrupt source
                                                         CIU2_RAW_MIO[BOOTDMA] & CIU2_EN_xx_yy_MIO[BOOTDMA] */
	uint64_t mio                          : 1;  /**< MIO boot interrupt source
                                                         CIU2_RAW_MIO[MIO] & CIU2_EN_xx_yy_MIO[MIO] */
	uint64_t nand                         : 1;  /**< NAND Flash Controller interrupt source
                                                         CIU2_RAW_MIO[NAND] & CIU2_EN_xx_yy_MIO[NANAD] */
	uint64_t reserved_12_15               : 4;
	uint64_t timer                        : 4;  /**< General timer interrupts source
                                                         CIU2_RAW_MIO[TIMER] & CIU2_EN_xx_yy_MIO[TIMER] */
	uint64_t reserved_3_7                 : 5;
	uint64_t ipd_drp                      : 1;  /**< IPD QOS packet drop interrupt source
                                                         CIU2_RAW_MIO[IPD_DRP] & CIU2_EN_xx_yy_MIO[IPD_DRP] */
	uint64_t ssoiq                        : 1;  /**< SSO IQ interrupt source
                                                         CIU2_RAW_MIO[SSOIQ] & CIU2_EN_xx_yy_MIO[SSOIQ] */
	uint64_t ipdppthr                     : 1;  /**< IPD per-port cnt threshold interrupt source
                                                         CIU2_RAW_MIO[IPDPPTHR] &CIU2_EN_xx_yy_MIO[IPDPPTHR] */
#else
	uint64_t ipdppthr                     : 1;
	uint64_t ssoiq                        : 1;
	uint64_t ipd_drp                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t timer                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t nand                         : 1;
	uint64_t mio                          : 1;
	uint64_t bootdma                      : 1;
	uint64_t reserved_19_31               : 13;
	uint64_t twsi                         : 2;
	uint64_t reserved_34_35               : 2;
	uint64_t uart                         : 2;
	uint64_t reserved_38_39               : 2;
	uint64_t usb_uctl                     : 1;
	uint64_t reserved_41_43               : 3;
	uint64_t usb_hci                      : 1;
	uint64_t reserved_45_47               : 3;
	uint64_t ptp                          : 1;
	uint64_t reserved_49_62               : 14;
	uint64_t rst                          : 1;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_mio_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_mio_s    cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_mio cvmx_ciu2_src_ppx_ip4_mio_t;

/**
 * cvmx_ciu2_src_pp#_ip4_pkt
 */
union cvmx_ciu2_src_ppx_ip4_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t ilk_drp                      : 2;  /**< ILK Packet Drop interrupts source
                                                         CIU2_RAW_PKT[ILK_DRP] & CIU2_EN_xx_yy_PKT[ILK_DRP] */
	uint64_t reserved_49_51               : 3;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t ilk_drp                      : 2;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_pkt_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ilk                          : 1;  /**< ILK interface interrupts source
                                                         CIU2_RAW_PKT[ILK] & CIU2_EN_xx_yy_PKT[ILK] */
	uint64_t reserved_41_47               : 7;
	uint64_t mii                          : 1;  /**< RGMII/MII/MIX Interface x Interrupts source
                                                         CIU2_RAW_PKT[MII] & CIU2_EN_xx_yy_PKT[MII] */
	uint64_t reserved_33_39               : 7;
	uint64_t agl                          : 1;  /**< AGL interrupt source
                                                         CIU2_RAW_PKT[AGL] & CIU2_EN_xx_yy_PKT[AGL] */
	uint64_t reserved_13_31               : 19;
	uint64_t gmx_drp                      : 5;  /**< GMX packet drop interrupt, RAW & ENABLE
                                                         CIU2_RAW_PKT[GMX_DRP] & CIU2_EN_xx_yy_PKT[GMX_DRP] */
	uint64_t reserved_5_7                 : 3;
	uint64_t agx                          : 5;  /**< GMX interrupt source
                                                         CIU2_RAW_PKT[AGX] & CIU2_EN_xx_yy_PKT[AGX] */
#else
	uint64_t agx                          : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t gmx_drp                      : 5;
	uint64_t reserved_13_31               : 19;
	uint64_t agl                          : 1;
	uint64_t reserved_33_39               : 7;
	uint64_t mii                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t ilk                          : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_pkt cvmx_ciu2_src_ppx_ip4_pkt_t;

/**
 * cvmx_ciu2_src_pp#_ip4_rml
 */
union cvmx_ciu2_src_ppx_ip4_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_37_39               : 3;
	uint64_t dpi_dma                      : 1;  /**< DPI DMA instruction completion  interrupt
                                                         See DPI DMA instruction completion */
	uint64_t reserved_34_35               : 2;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_35               : 2;
	uint64_t dpi_dma                      : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_rml_s    cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t trace                        : 4;  /**< Trace buffer interrupt source
                                                         CIU2_RAW_RML[TRACE] & CIU2_EN_xx_yy_RML[TRACE] */
	uint64_t reserved_49_51               : 3;
	uint64_t l2c                          : 1;  /**< L2C interrupt source
                                                         CIU2_RAW_RML[L2C] & CIU2_EN_xx_yy_RML[L2C] */
	uint64_t reserved_41_47               : 7;
	uint64_t dfa                          : 1;  /**< DFA interrupt source
                                                         CIU2_RAW_RML[DFA] & CIU2_EN_xx_yy_RML[DFA] */
	uint64_t reserved_34_39               : 6;
	uint64_t dpi                          : 1;  /**< DPI interrupt source
                                                         CIU2_RAW_RML[DPI] & CIU2_EN_xx_yy_RML[DPI] */
	uint64_t sli                          : 1;  /**< SLI interrupt source
                                                         CIU2_RAW_RML[SLI] & CIU2_EN_xx_yy_RML[SLI] */
	uint64_t reserved_31_31               : 1;
	uint64_t key                          : 1;  /**< KEY interrupt source
                                                         CIU2_RAW_RML[KEY] & CIU2_EN_xx_yy_RML[KEY] */
	uint64_t rad                          : 1;  /**< RAD interrupt source
                                                         CIU2_RAW_RML[RAD] & CIU2_EN_xx_yy_RML[RAD] */
	uint64_t tim                          : 1;  /**< TIM interrupt source
                                                         CIU2_RAW_RML[TIM] & CIU2_EN_xx_yy_RML[TIM] */
	uint64_t reserved_25_27               : 3;
	uint64_t zip                          : 1;  /**< ZIP interrupt source
                                                         CIU2_RAW_RML[ZIP] & CIU2_EN_xx_yy_RML[ZIP] */
	uint64_t reserved_17_23               : 7;
	uint64_t sso                          : 1;  /**< SSO err interrupt source
                                                         CIU2_RAW_RML[SSO] & CIU2_EN_xx_yy_RML[SSO] */
	uint64_t reserved_8_15                : 8;
	uint64_t pko                          : 1;  /**< PKO interrupt source
                                                         CIU2_RAW_RML[PKO] & CIU2_EN_xx_yy_RML[PKO] */
	uint64_t pip                          : 1;  /**< PIP interrupt source
                                                         CIU2_RAW_RML[PIP] & CIU2_EN_xx_yy_RML[PIP] */
	uint64_t ipd                          : 1;  /**< IPD interrupt source
                                                         CIU2_RAW_RML[IPD] & CIU2_EN_xx_yy_RML[IPD] */
	uint64_t fpa                          : 1;  /**< FPA interrupt source
                                                         CIU2_RAW_RML[FPA] & CIU2_EN_xx_yy_RML[FPA] */
	uint64_t reserved_1_3                 : 3;
	uint64_t iob                          : 1;  /**< IOB interrupt source
                                                         CIU2_RAW_RML[IOB] & CIU2_EN_xx_yy_RML[IOB] */
#else
	uint64_t iob                          : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t fpa                          : 1;
	uint64_t ipd                          : 1;
	uint64_t pip                          : 1;
	uint64_t pko                          : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t sso                          : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t zip                          : 1;
	uint64_t reserved_25_27               : 3;
	uint64_t tim                          : 1;
	uint64_t rad                          : 1;
	uint64_t key                          : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t sli                          : 1;
	uint64_t dpi                          : 1;
	uint64_t reserved_34_39               : 6;
	uint64_t dfa                          : 1;
	uint64_t reserved_41_47               : 7;
	uint64_t l2c                          : 1;
	uint64_t reserved_49_51               : 3;
	uint64_t trace                        : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_rml cvmx_ciu2_src_ppx_ip4_rml_t;

/**
 * cvmx_ciu2_src_pp#_ip4_wdog
 */
union cvmx_ciu2_src_ppx_ip4_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wdog                         : 32; /**< 32 watchdog interrupts source
                                                         CIU2_RAW_WDOG & CIU2_EN_xx_yy_WDOG */
#else
	uint64_t wdog                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_wdog_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_wdog_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_wdog cvmx_ciu2_src_ppx_ip4_wdog_t;

/**
 * cvmx_ciu2_src_pp#_ip4_wrkq
 */
union cvmx_ciu2_src_ppx_ip4_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t workq                        : 64; /**< 64 work queue intr source,
                                                         CIU2_RAW_WRKQ & CIU2_EN_xx_yy_WRKQ */
#else
	uint64_t workq                        : 64;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_wrkq_s   cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_wrkq_s   cn68xxp1;
};
typedef union cvmx_ciu2_src_ppx_ip4_wrkq cvmx_ciu2_src_ppx_ip4_wrkq_t;

/**
 * cvmx_ciu2_sum_io#_int
 */
union cvmx_ciu2_sum_iox_int {
	uint64_t u64;
	struct cvmx_ciu2_sum_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mbox                         : 4;  /**< MBOX interrupt summary
                                                         Direct connect to CIU2_SRC_*_MBOX[MBOX]
                                                         See CIU_MBOX_SET/CLR / CIU2_SRC_*_MBOX */
	uint64_t reserved_8_59                : 52;
	uint64_t gpio                         : 1;  /**< GPIO interrupt summary,
                                                         Report ORed result of CIU2_SRC_*_GPIO[63:0]
                                                         See CIU2_RAW_GPIO / CIU2_SRC_*_GPIO */
	uint64_t pkt                          : 1;  /**< Packet I/O interrupt summary
                                                         Report ORed result of CIU2_SRC_*_PKT[63:0]
                                                         See CIU2_RAW_PKT / CIU2_SRC_*_PKT */
	uint64_t mem                          : 1;  /**< MEM  interrupt Summary
                                                         Report ORed result of CIU2_SRC_*_MEM[63:0]
                                                         See CIU2_RAW_MEM / CIU2_SRC_*_MEM */
	uint64_t io                           : 1;  /**< I/O  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_IO[63:0]
                                                         See CIU2_RAW_IO / CIU2_SRC_*_IO */
	uint64_t mio                          : 1;  /**< MIO  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_MIO[63:0]
                                                         See CIU2_RAW_MIO / CIU2_SRC_*_MIO */
	uint64_t rml                          : 1;  /**< RML Interrupt
                                                         Report ORed result of CIU2_SRC_*_RML[63:0]
                                                         See CIU2_RAW_RML / CIU2_SRC_*_RML */
	uint64_t wdog                         : 1;  /**< WDOG summary bit
                                                         Report ORed result of CIU2_SRC_*_WDOG[63:0]
                                                         See CIU2_RAW_WDOG / CIU2_SRC_*_WDOG
                                                          This read-only bit reads as a one whenever
                                                          CIU2_RAW_WDOG bit is set and corresponding
                                                          enable bit in CIU2_EN_PPx_IPy_WDOG or
                                                          CIU2_EN_IOx_INT_WDOG is set, where x and y are
                                                          the same x and y in the CIU2_SUM_PPx_IPy or
                                                          CIU2_SUM_IOx_INT registers.
                                                          Alternatively, the CIU2_SRC_PPx_IPy_WDOG and
                                                          CIU2_SRC_IOx_INT_WDOG registers can be used. */
	uint64_t workq                        : 1;  /**< 64 work queue interrupts
                                                         Report ORed result of CIU2_SRC_*_WRKQ[63:0]
                                                         See CIU2_RAW_WRKQ / CIU2_SRC_*_WRKQ
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO. */
#else
	uint64_t workq                        : 1;
	uint64_t wdog                         : 1;
	uint64_t rml                          : 1;
	uint64_t mio                          : 1;
	uint64_t io                           : 1;
	uint64_t mem                          : 1;
	uint64_t pkt                          : 1;
	uint64_t gpio                         : 1;
	uint64_t reserved_8_59                : 52;
	uint64_t mbox                         : 4;
#endif
	} s;
	struct cvmx_ciu2_sum_iox_int_s        cn68xx;
	struct cvmx_ciu2_sum_iox_int_s        cn68xxp1;
};
typedef union cvmx_ciu2_sum_iox_int cvmx_ciu2_sum_iox_int_t;

/**
 * cvmx_ciu2_sum_pp#_ip2
 */
union cvmx_ciu2_sum_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu2_sum_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mbox                         : 4;  /**< MBOX interrupt summary
                                                         Direct connect to CIU2_SRC_*_MBOX[MBOX]
                                                         See CIU_MBOX_SET/CLR / CIU2_SRC_*_MBOX */
	uint64_t reserved_8_59                : 52;
	uint64_t gpio                         : 1;  /**< GPIO interrupt summary,
                                                         Report ORed result of CIU2_SRC_*_GPIO[63:0]
                                                         See CIU2_RAW_GPIO / CIU2_SRC_*_GPIO */
	uint64_t pkt                          : 1;  /**< Packet I/O interrupt summary
                                                         Report ORed result of CIU2_SRC_*_PKT[63:0]
                                                         See CIU2_RAW_PKT / CIU2_SRC_*_PKT */
	uint64_t mem                          : 1;  /**< MEM  interrupt Summary
                                                         Report ORed result of CIU2_SRC_*_MEM[63:0]
                                                         See CIU2_RAW_MEM / CIU2_SRC_*_MEM */
	uint64_t io                           : 1;  /**< I/O  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_IO[63:0]
                                                         See CIU2_RAW_IO / CIU2_SRC_*_IO */
	uint64_t mio                          : 1;  /**< MIO  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_MIO[63:0]
                                                         See CIU2_RAW_MIO / CIU2_SRC_*_MIO */
	uint64_t rml                          : 1;  /**< RML Interrupt
                                                         Report ORed result of CIU2_SRC_*_RML[63:0]
                                                         See CIU2_RAW_RML / CIU2_SRC_*_RML */
	uint64_t wdog                         : 1;  /**< WDOG summary bit
                                                         Report ORed result of CIU2_SRC_*_WDOG[63:0]
                                                         See CIU2_RAW_WDOG / CIU2_SRC_*_WDOG
                                                          This read-only bit reads as a one whenever
                                                          CIU2_RAW_WDOG bit is set and corresponding
                                                          enable bit in CIU2_EN_PPx_IPy_WDOG or
                                                          CIU2_EN_IOx_INT_WDOG is set, where x and y are
                                                          the same x and y in the CIU2_SUM_PPx_IPy or
                                                          CIU2_SUM_IOx_INT registers.
                                                          Alternatively, the CIU2_SRC_PPx_IPy_WDOG and
                                                          CIU2_SRC_IOx_INT_WDOG registers can be used. */
	uint64_t workq                        : 1;  /**< 64 work queue interrupts
                                                         Report ORed result of CIU2_SRC_*_WRKQ[63:0]
                                                         See CIU2_RAW_WRKQ / CIU2_SRC_*_WRKQ
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO. */
#else
	uint64_t workq                        : 1;
	uint64_t wdog                         : 1;
	uint64_t rml                          : 1;
	uint64_t mio                          : 1;
	uint64_t io                           : 1;
	uint64_t mem                          : 1;
	uint64_t pkt                          : 1;
	uint64_t gpio                         : 1;
	uint64_t reserved_8_59                : 52;
	uint64_t mbox                         : 4;
#endif
	} s;
	struct cvmx_ciu2_sum_ppx_ip2_s        cn68xx;
	struct cvmx_ciu2_sum_ppx_ip2_s        cn68xxp1;
};
typedef union cvmx_ciu2_sum_ppx_ip2 cvmx_ciu2_sum_ppx_ip2_t;

/**
 * cvmx_ciu2_sum_pp#_ip3
 */
union cvmx_ciu2_sum_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu2_sum_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mbox                         : 4;  /**< MBOX interrupt summary
                                                         Direct connect to CIU2_SRC_*_MBOX[MBOX]
                                                         See CIU_MBOX_SET/CLR / CIU2_SRC_*_MBOX */
	uint64_t reserved_8_59                : 52;
	uint64_t gpio                         : 1;  /**< GPIO interrupt summary,
                                                         Report ORed result of CIU2_SRC_*_GPIO[63:0]
                                                         See CIU2_RAW_GPIO / CIU2_SRC_*_GPIO */
	uint64_t pkt                          : 1;  /**< Packet I/O interrupt summary
                                                         Report ORed result of CIU2_SRC_*_PKT[63:0]
                                                         See CIU2_RAW_PKT / CIU2_SRC_*_PKT */
	uint64_t mem                          : 1;  /**< MEM  interrupt Summary
                                                         Report ORed result of CIU2_SRC_*_MEM[63:0]
                                                         See CIU2_RAW_MEM / CIU2_SRC_*_MEM */
	uint64_t io                           : 1;  /**< I/O  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_IO[63:0]
                                                         See CIU2_RAW_IO / CIU2_SRC_*_IO */
	uint64_t mio                          : 1;  /**< MIO  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_MIO[63:0]
                                                         See CIU2_RAW_MIO / CIU2_SRC_*_MIO */
	uint64_t rml                          : 1;  /**< RML Interrupt
                                                         Report ORed result of CIU2_SRC_*_RML[63:0]
                                                         See CIU2_RAW_RML / CIU2_SRC_*_RML */
	uint64_t wdog                         : 1;  /**< WDOG summary bit
                                                         Report ORed result of CIU2_SRC_*_WDOG[63:0]
                                                         See CIU2_RAW_WDOG / CIU2_SRC_*_WDOG
                                                          This read-only bit reads as a one whenever
                                                          CIU2_RAW_WDOG bit is set and corresponding
                                                          enable bit in CIU2_EN_PPx_IPy_WDOG or
                                                          CIU2_EN_IOx_INT_WDOG is set, where x and y are
                                                          the same x and y in the CIU2_SUM_PPx_IPy or
                                                          CIU2_SUM_IOx_INT registers.
                                                          Alternatively, the CIU2_SRC_PPx_IPy_WDOG and
                                                          CIU2_SRC_IOx_INT_WDOG registers can be used. */
	uint64_t workq                        : 1;  /**< 64 work queue interrupts
                                                         Report ORed result of CIU2_SRC_*_WRKQ[63:0]
                                                         See CIU2_RAW_WRKQ / CIU2_SRC_*_WRKQ
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO. */
#else
	uint64_t workq                        : 1;
	uint64_t wdog                         : 1;
	uint64_t rml                          : 1;
	uint64_t mio                          : 1;
	uint64_t io                           : 1;
	uint64_t mem                          : 1;
	uint64_t pkt                          : 1;
	uint64_t gpio                         : 1;
	uint64_t reserved_8_59                : 52;
	uint64_t mbox                         : 4;
#endif
	} s;
	struct cvmx_ciu2_sum_ppx_ip3_s        cn68xx;
	struct cvmx_ciu2_sum_ppx_ip3_s        cn68xxp1;
};
typedef union cvmx_ciu2_sum_ppx_ip3 cvmx_ciu2_sum_ppx_ip3_t;

/**
 * cvmx_ciu2_sum_pp#_ip4
 */
union cvmx_ciu2_sum_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu2_sum_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mbox                         : 4;  /**< MBOX interrupt summary
                                                         Direct connect to CIU2_SRC_*_MBOX[MBOX]
                                                         See CIU_MBOX_SET/CLR / CIU2_SRC_*_MBOX */
	uint64_t reserved_8_59                : 52;
	uint64_t gpio                         : 1;  /**< GPIO interrupt summary,
                                                         Report ORed result of CIU2_SRC_*_GPIO[63:0]
                                                         See CIU2_RAW_GPIO / CIU2_SRC_*_GPIO */
	uint64_t pkt                          : 1;  /**< Packet I/O interrupt summary
                                                         Report ORed result of CIU2_SRC_*_PKT[63:0]
                                                         See CIU2_RAW_PKT / CIU2_SRC_*_PKT */
	uint64_t mem                          : 1;  /**< MEM  interrupt Summary
                                                         Report ORed result of CIU2_SRC_*_MEM[63:0]
                                                         See CIU2_RAW_MEM / CIU2_SRC_*_MEM */
	uint64_t io                           : 1;  /**< I/O  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_IO[63:0]
                                                         See CIU2_RAW_IO / CIU2_SRC_*_IO */
	uint64_t mio                          : 1;  /**< MIO  interrupt summary
                                                         Report ORed result of CIU2_SRC_*_MIO[63:0]
                                                         See CIU2_RAW_MIO / CIU2_SRC_*_MIO */
	uint64_t rml                          : 1;  /**< RML Interrupt
                                                         Report ORed result of CIU2_SRC_*_RML[63:0]
                                                         See CIU2_RAW_RML / CIU2_SRC_*_RML */
	uint64_t wdog                         : 1;  /**< WDOG summary bit
                                                         Report ORed result of CIU2_SRC_*_WDOG[63:0]
                                                         See CIU2_RAW_WDOG / CIU2_SRC_*_WDOG
                                                          This read-only bit reads as a one whenever
                                                          CIU2_RAW_WDOG bit is set and corresponding
                                                          enable bit in CIU2_EN_PPx_IPy_WDOG or
                                                          CIU2_EN_IOx_INT_WDOG is set, where x and y are
                                                          the same x and y in the CIU2_SUM_PPx_IPy or
                                                          CIU2_SUM_IOx_INT registers.
                                                          Alternatively, the CIU2_SRC_PPx_IPy_WDOG and
                                                          CIU2_SRC_IOx_INT_WDOG registers can be used. */
	uint64_t workq                        : 1;  /**< 64 work queue interrupts
                                                         Report ORed result of CIU2_SRC_*_WRKQ[63:0]
                                                         See CIU2_RAW_WRKQ / CIU2_SRC_*_WRKQ
                                                         See SSO_WQ_INT[WQ_INT]
                                                          1 bit/group. A copy of the R/W1C bit in the SSO. */
#else
	uint64_t workq                        : 1;
	uint64_t wdog                         : 1;
	uint64_t rml                          : 1;
	uint64_t mio                          : 1;
	uint64_t io                           : 1;
	uint64_t mem                          : 1;
	uint64_t pkt                          : 1;
	uint64_t gpio                         : 1;
	uint64_t reserved_8_59                : 52;
	uint64_t mbox                         : 4;
#endif
	} s;
	struct cvmx_ciu2_sum_ppx_ip4_s        cn68xx;
	struct cvmx_ciu2_sum_ppx_ip4_s        cn68xxp1;
};
typedef union cvmx_ciu2_sum_ppx_ip4 cvmx_ciu2_sum_ppx_ip4_t;

#endif
