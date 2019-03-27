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
 * cvmx-sriox-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon sriox.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SRIOX_DEFS_H__
#define __CVMX_SRIOX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_ACC_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_ACC_CTRL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000148ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_ACC_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000148ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_ASMBLY_ID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_ASMBLY_ID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000200ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_ASMBLY_ID(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000200ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_ASMBLY_INFO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_ASMBLY_INFO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000208ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_ASMBLY_INFO(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000208ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_BELL_RESP_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_BELL_RESP_CTRL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000310ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_BELL_RESP_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000310ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_BIST_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_BIST_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000108ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000108ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IMSG_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_IMSG_CTRL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000508ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_IMSG_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000508ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IMSG_INST_HDRX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_IMSG_INST_HDRX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000510ull) + (((offset) & 1) + ((block_id) & 3) * 0x200000ull) * 8;
}
#else
#define CVMX_SRIOX_IMSG_INST_HDRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000510ull) + (((offset) & 1) + ((block_id) & 3) * 0x200000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IMSG_QOS_GRPX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 31)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 31)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_IMSG_QOS_GRPX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000600ull) + (((offset) & 31) + ((block_id) & 3) * 0x200000ull) * 8;
}
#else
#define CVMX_SRIOX_IMSG_QOS_GRPX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000600ull) + (((offset) & 31) + ((block_id) & 3) * 0x200000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IMSG_STATUSX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 23)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 23)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_IMSG_STATUSX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000700ull) + (((offset) & 31) + ((block_id) & 3) * 0x200000ull) * 8;
}
#else
#define CVMX_SRIOX_IMSG_STATUSX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000700ull) + (((offset) & 31) + ((block_id) & 3) * 0x200000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IMSG_VPORT_THR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_IMSG_VPORT_THR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000500ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_IMSG_VPORT_THR(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000500ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IMSG_VPORT_THR2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_IMSG_VPORT_THR2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000528ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_IMSG_VPORT_THR2(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000528ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT2_ENABLE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT2_ENABLE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80003E0ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT2_ENABLE(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003E0ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT2_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT2_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80003E8ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT2_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003E8ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT_ENABLE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT_ENABLE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000110ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT_ENABLE(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000110ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT_INFO0(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT_INFO0(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000120ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT_INFO0(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000120ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT_INFO1(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT_INFO1(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000128ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT_INFO1(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000128ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT_INFO2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT_INFO2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000130ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT_INFO2(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000130ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT_INFO3(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT_INFO3(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000138ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT_INFO3(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000138ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_INT_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_INT_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000118ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000118ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_IP_FEATURE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_IP_FEATURE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80003F8ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_IP_FEATURE(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003F8ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_MAC_BUFFERS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_MAC_BUFFERS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000390ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_MAC_BUFFERS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000390ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_MAINT_OP(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_MAINT_OP(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000158ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_MAINT_OP(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000158ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_MAINT_RD_DATA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_MAINT_RD_DATA(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000160ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_MAINT_RD_DATA(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000160ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_MCE_TX_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_MCE_TX_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000240ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_MCE_TX_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000240ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_MEM_OP_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_MEM_OP_CTRL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000168ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_MEM_OP_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000168ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_CTRLX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_CTRLX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000488ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64;
}
#else
#define CVMX_SRIOX_OMSG_CTRLX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000488ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_DONE_COUNTSX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_DONE_COUNTSX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80004B0ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64;
}
#else
#define CVMX_SRIOX_OMSG_DONE_COUNTSX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C80004B0ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_FMP_MRX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_FMP_MRX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000498ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64;
}
#else
#define CVMX_SRIOX_OMSG_FMP_MRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000498ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_NMP_MRX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_NMP_MRX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80004A0ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64;
}
#else
#define CVMX_SRIOX_OMSG_NMP_MRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C80004A0ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_PORTX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_PORTX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000480ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64;
}
#else
#define CVMX_SRIOX_OMSG_PORTX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000480ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_SILO_THR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_SILO_THR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80004F8ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_OMSG_SILO_THR(block_id) (CVMX_ADD_IO_SEG(0x00011800C80004F8ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_OMSG_SP_MRX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_OMSG_SP_MRX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000490ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64;
}
#else
#define CVMX_SRIOX_OMSG_SP_MRX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000490ull) + (((offset) & 1) + ((block_id) & 3) * 0x40000ull) * 64)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_PRIOX_IN_USE(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 3)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_PRIOX_IN_USE(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80003C0ull) + (((offset) & 3) + ((block_id) & 3) * 0x200000ull) * 8;
}
#else
#define CVMX_SRIOX_PRIOX_IN_USE(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C80003C0ull) + (((offset) & 3) + ((block_id) & 3) * 0x200000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_RX_BELL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_RX_BELL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000308ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_RX_BELL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000308ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_RX_BELL_SEQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_RX_BELL_SEQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000300ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_RX_BELL_SEQ(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000300ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_RX_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_RX_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000380ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_RX_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000380ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_S2M_TYPEX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 15)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 15)) && ((block_id == 0) || (block_id == 2) || (block_id == 3))))))
		cvmx_warn("CVMX_SRIOX_S2M_TYPEX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000180ull) + (((offset) & 15) + ((block_id) & 3) * 0x200000ull) * 8;
}
#else
#define CVMX_SRIOX_S2M_TYPEX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000180ull) + (((offset) & 15) + ((block_id) & 3) * 0x200000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_SEQ(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_SEQ(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000278ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_SEQ(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000278ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_STATUS_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_STATUS_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000100ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_STATUS_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000100ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TAG_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TAG_CTRL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000178ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TAG_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000178ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TLP_CREDITS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TLP_CREDITS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000150ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TLP_CREDITS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000150ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TX_BELL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TX_BELL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000280ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TX_BELL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000280ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TX_BELL_INFO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TX_BELL_INFO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000288ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TX_BELL_INFO(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000288ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TX_CTRL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TX_CTRL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000170ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TX_CTRL(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000170ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TX_EMPHASIS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TX_EMPHASIS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80003F0ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TX_EMPHASIS(block_id) (CVMX_ADD_IO_SEG(0x00011800C80003F0ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_TX_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_TX_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000388ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_TX_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000388ull) + ((block_id) & 3) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRIOX_WR_DONE_COUNTS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0) || ((block_id >= 2) && (block_id <= 3))))))
		cvmx_warn("CVMX_SRIOX_WR_DONE_COUNTS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000340ull) + ((block_id) & 3) * 0x1000000ull;
}
#else
#define CVMX_SRIOX_WR_DONE_COUNTS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000340ull) + ((block_id) & 3) * 0x1000000ull)
#endif

/**
 * cvmx_srio#_acc_ctrl
 *
 * SRIO_ACC_CTRL = SRIO Access Control
 *
 * General access control of the incoming BAR registers.
 *
 * Notes:
 * This register controls write access to the BAR registers via SRIO Maintenance Operations.  At
 *  powerup the BAR registers can be accessed via RSL and Maintenance Operations.  If the DENY_BAR*
 *  bits or DENY_ADR* bits are set then Maintenance Writes to the corresponding BAR fields are
 *  ignored.  Setting both the DENY_BAR and DENY_ADR for a corresponding BAR is compatable with the
 *  operation of the DENY_BAR bit found in 63xx Pass 2 and earlier.  This register does not effect
 *  read operations.  Reset values for DENY_BAR[2:0] are typically clear but they are set if
 *  the chip is operating in Authentik Mode.
 *
 * Clk_Rst:        SRIO(0,2..3)_ACC_CTRL   hclk    hrst_n
 */
union cvmx_sriox_acc_ctrl {
	uint64_t u64;
	struct cvmx_sriox_acc_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t deny_adr2                    : 1;  /**< Deny SRIO Write Access to SRIO Address Fields in
                                                         SRIOMAINT(0,2..3)_BAR2* Registers */
	uint64_t deny_adr1                    : 1;  /**< Deny SRIO Write Access to SRIO Address Fields in
                                                         SRIOMAINT(0,2..3)_BAR1* Registers */
	uint64_t deny_adr0                    : 1;  /**< Deny SRIO Write Access to SRIO Address Fields in
                                                         SRIOMAINT(0,2..3)_BAR0* Registers */
	uint64_t reserved_3_3                 : 1;
	uint64_t deny_bar2                    : 1;  /**< Deny SRIO Write Access to non-SRIO Address Fields
                                                         in the SRIOMAINT_BAR2 Registers */
	uint64_t deny_bar1                    : 1;  /**< Deny SRIO Write Access to non-SRIO Address Fields
                                                         in the SRIOMAINT_BAR1 Registers */
	uint64_t deny_bar0                    : 1;  /**< Deny SRIO Write Access to non-SRIO Address Fields
                                                         in the SRIOMAINT_BAR0 Registers */
#else
	uint64_t deny_bar0                    : 1;
	uint64_t deny_bar1                    : 1;
	uint64_t deny_bar2                    : 1;
	uint64_t reserved_3_3                 : 1;
	uint64_t deny_adr0                    : 1;
	uint64_t deny_adr1                    : 1;
	uint64_t deny_adr2                    : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_sriox_acc_ctrl_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t deny_bar2                    : 1;  /**< Deny SRIO Write Access to BAR2 Registers */
	uint64_t deny_bar1                    : 1;  /**< Deny SRIO Write Access to BAR1 Registers */
	uint64_t deny_bar0                    : 1;  /**< Deny SRIO Write Access to BAR0 Registers */
#else
	uint64_t deny_bar0                    : 1;
	uint64_t deny_bar1                    : 1;
	uint64_t deny_bar2                    : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} cn63xx;
	struct cvmx_sriox_acc_ctrl_cn63xx     cn63xxp1;
	struct cvmx_sriox_acc_ctrl_s          cn66xx;
};
typedef union cvmx_sriox_acc_ctrl cvmx_sriox_acc_ctrl_t;

/**
 * cvmx_srio#_asmbly_id
 *
 * SRIO_ASMBLY_ID = SRIO Assembly ID
 *
 * The Assembly ID register controls the Assembly ID and Vendor
 *
 * Notes:
 * This register specifies the Assembly ID and Vendor visible in SRIOMAINT(0,2..3)_ASMBLY_ID register.  The
 *  Assembly Vendor ID is typically supplied by the RapidIO Trade Association.  This register is only
 *  reset during COLD boot and may only be modified while SRIO(0,2..3)_STATUS_REG.ACCESS is zero.
 *
 * Clk_Rst:        SRIO(0,2..3)_ASMBLY_ID  sclk    srst_cold_n
 */
union cvmx_sriox_asmbly_id {
	uint64_t u64;
	struct cvmx_sriox_asmbly_id_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t assy_id                      : 16; /**< Assembly Identifer */
	uint64_t assy_ven                     : 16; /**< Assembly Vendor Identifer */
#else
	uint64_t assy_ven                     : 16;
	uint64_t assy_id                      : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_asmbly_id_s         cn63xx;
	struct cvmx_sriox_asmbly_id_s         cn63xxp1;
	struct cvmx_sriox_asmbly_id_s         cn66xx;
};
typedef union cvmx_sriox_asmbly_id cvmx_sriox_asmbly_id_t;

/**
 * cvmx_srio#_asmbly_info
 *
 * SRIO_ASMBLY_INFO = SRIO Assembly Information
 *
 * The Assembly Info register controls the Assembly Revision
 *
 * Notes:
 * The Assembly Info register controls the Assembly Revision visible in the ASSY_REV field of the
 *  SRIOMAINT(0,2..3)_ASMBLY_INFO register.  This register is only reset during COLD boot and may only be
 *  modified while SRIO(0,2..3)_STATUS_REG.ACCESS is zero.
 *
 * Clk_Rst:        SRIO(0,2..3)_ASMBLY_INFO        sclk    srst_cold_n
 */
union cvmx_sriox_asmbly_info {
	uint64_t u64;
	struct cvmx_sriox_asmbly_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t assy_rev                     : 16; /**< Assembly Revision */
	uint64_t reserved_0_15                : 16;
#else
	uint64_t reserved_0_15                : 16;
	uint64_t assy_rev                     : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_asmbly_info_s       cn63xx;
	struct cvmx_sriox_asmbly_info_s       cn63xxp1;
	struct cvmx_sriox_asmbly_info_s       cn66xx;
};
typedef union cvmx_sriox_asmbly_info cvmx_sriox_asmbly_info_t;

/**
 * cvmx_srio#_bell_resp_ctrl
 *
 * SRIO_BELL_RESP_CTRL = SRIO Doorbell Response Control
 *
 * The SRIO Doorbell Response Control Register
 *
 * Notes:
 * This register is used to override the response priority of the outgoing doorbell responses.
 *
 * Clk_Rst:        SRIO(0,2..3)_BELL_RESP_CTRL     hclk    hrst_n
 */
union cvmx_sriox_bell_resp_ctrl {
	uint64_t u64;
	struct cvmx_sriox_bell_resp_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t rp1_sid                      : 1;  /**< Sets response priority for incomimg doorbells
                                                         of priority 1 on the secondary ID (0=2, 1=3) */
	uint64_t rp0_sid                      : 2;  /**< Sets response priority for incomimg doorbells
                                                         of priority 0 on the secondary ID (0,1=1 2=2, 3=3) */
	uint64_t rp1_pid                      : 1;  /**< Sets response priority for incomimg doorbells
                                                         of priority 1 on the primary ID (0=2, 1=3) */
	uint64_t rp0_pid                      : 2;  /**< Sets response priority for incomimg doorbells
                                                         of priority 0 on the primary ID (0,1=1 2=2, 3=3) */
#else
	uint64_t rp0_pid                      : 2;
	uint64_t rp1_pid                      : 1;
	uint64_t rp0_sid                      : 2;
	uint64_t rp1_sid                      : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_sriox_bell_resp_ctrl_s    cn63xx;
	struct cvmx_sriox_bell_resp_ctrl_s    cn63xxp1;
	struct cvmx_sriox_bell_resp_ctrl_s    cn66xx;
};
typedef union cvmx_sriox_bell_resp_ctrl cvmx_sriox_bell_resp_ctrl_t;

/**
 * cvmx_srio#_bist_status
 *
 * SRIO_BIST_STATUS = SRIO Bist Status
 *
 * Results from BIST runs of SRIO's memories.
 *
 * Notes:
 * BIST Results.
 *
 * Clk_Rst:        SRIO(0,2..3)_BIST_STATUS        hclk    hrst_n
 */
union cvmx_sriox_bist_status {
	uint64_t u64;
	struct cvmx_sriox_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_45_63               : 19;
	uint64_t lram                         : 1;  /**< Incoming Doorbell Lookup RAM. */
	uint64_t mram                         : 2;  /**< Incoming Message SLI FIFO. */
	uint64_t cram                         : 2;  /**< Incoming Rd/Wr/Response Command FIFO. */
	uint64_t bell                         : 2;  /**< Incoming Doorbell FIFO. */
	uint64_t otag                         : 2;  /**< Outgoing Tag Data. */
	uint64_t itag                         : 1;  /**< Incoming TAG Data. */
	uint64_t ofree                        : 1;  /**< Outgoing Free Pointer RAM (OFIFO) */
	uint64_t rtn                          : 2;  /**< Outgoing Response Return FIFO. */
	uint64_t obulk                        : 4;  /**< Outgoing Bulk Data RAMs (OFIFO) */
	uint64_t optrs                        : 4;  /**< Outgoing Priority Pointer RAMs (OFIFO) */
	uint64_t oarb2                        : 2;  /**< Additional Outgoing Priority RAMs. */
	uint64_t rxbuf2                       : 2;  /**< Additional Incoming SRIO MAC Buffers. */
	uint64_t oarb                         : 2;  /**< Outgoing Priority RAMs (OARB) */
	uint64_t ispf                         : 1;  /**< Incoming Soft Packet FIFO */
	uint64_t ospf                         : 1;  /**< Outgoing Soft Packet FIFO */
	uint64_t txbuf                        : 2;  /**< Outgoing SRIO MAC Buffer. */
	uint64_t rxbuf                        : 2;  /**< Incoming SRIO MAC Buffer. */
	uint64_t imsg                         : 5;  /**< Incoming Message RAMs. */
	uint64_t omsg                         : 7;  /**< Outgoing Message RAMs. */
#else
	uint64_t omsg                         : 7;
	uint64_t imsg                         : 5;
	uint64_t rxbuf                        : 2;
	uint64_t txbuf                        : 2;
	uint64_t ospf                         : 1;
	uint64_t ispf                         : 1;
	uint64_t oarb                         : 2;
	uint64_t rxbuf2                       : 2;
	uint64_t oarb2                        : 2;
	uint64_t optrs                        : 4;
	uint64_t obulk                        : 4;
	uint64_t rtn                          : 2;
	uint64_t ofree                        : 1;
	uint64_t itag                         : 1;
	uint64_t otag                         : 2;
	uint64_t bell                         : 2;
	uint64_t cram                         : 2;
	uint64_t mram                         : 2;
	uint64_t lram                         : 1;
	uint64_t reserved_45_63               : 19;
#endif
	} s;
	struct cvmx_sriox_bist_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t mram                         : 2;  /**< Incoming Message SLI FIFO. */
	uint64_t cram                         : 2;  /**< Incoming Rd/Wr/Response Command FIFO. */
	uint64_t bell                         : 2;  /**< Incoming Doorbell FIFO. */
	uint64_t otag                         : 2;  /**< Outgoing Tag Data. */
	uint64_t itag                         : 1;  /**< Incoming TAG Data. */
	uint64_t ofree                        : 1;  /**< Outgoing Free Pointer RAM (OFIFO) */
	uint64_t rtn                          : 2;  /**< Outgoing Response Return FIFO. */
	uint64_t obulk                        : 4;  /**< Outgoing Bulk Data RAMs (OFIFO) */
	uint64_t optrs                        : 4;  /**< Outgoing Priority Pointer RAMs (OFIFO) */
	uint64_t oarb2                        : 2;  /**< Additional Outgoing Priority RAMs (Pass 2). */
	uint64_t rxbuf2                       : 2;  /**< Additional Incoming SRIO MAC Buffers (Pass 2). */
	uint64_t oarb                         : 2;  /**< Outgoing Priority RAMs (OARB) */
	uint64_t ispf                         : 1;  /**< Incoming Soft Packet FIFO */
	uint64_t ospf                         : 1;  /**< Outgoing Soft Packet FIFO */
	uint64_t txbuf                        : 2;  /**< Outgoing SRIO MAC Buffer. */
	uint64_t rxbuf                        : 2;  /**< Incoming SRIO MAC Buffer. */
	uint64_t imsg                         : 5;  /**< Incoming Message RAMs.
                                                         IMSG<0> (i.e. <7>) unused in Pass 2 */
	uint64_t omsg                         : 7;  /**< Outgoing Message RAMs. */
#else
	uint64_t omsg                         : 7;
	uint64_t imsg                         : 5;
	uint64_t rxbuf                        : 2;
	uint64_t txbuf                        : 2;
	uint64_t ospf                         : 1;
	uint64_t ispf                         : 1;
	uint64_t oarb                         : 2;
	uint64_t rxbuf2                       : 2;
	uint64_t oarb2                        : 2;
	uint64_t optrs                        : 4;
	uint64_t obulk                        : 4;
	uint64_t rtn                          : 2;
	uint64_t ofree                        : 1;
	uint64_t itag                         : 1;
	uint64_t otag                         : 2;
	uint64_t bell                         : 2;
	uint64_t cram                         : 2;
	uint64_t mram                         : 2;
	uint64_t reserved_44_63               : 20;
#endif
	} cn63xx;
	struct cvmx_sriox_bist_status_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t mram                         : 2;  /**< Incoming Message SLI FIFO. */
	uint64_t cram                         : 2;  /**< Incoming Rd/Wr/Response Command FIFO. */
	uint64_t bell                         : 2;  /**< Incoming Doorbell FIFO. */
	uint64_t otag                         : 2;  /**< Outgoing Tag Data. */
	uint64_t itag                         : 1;  /**< Incoming TAG Data. */
	uint64_t ofree                        : 1;  /**< Outgoing Free Pointer RAM (OFIFO) */
	uint64_t rtn                          : 2;  /**< Outgoing Response Return FIFO. */
	uint64_t obulk                        : 4;  /**< Outgoing Bulk Data RAMs (OFIFO) */
	uint64_t optrs                        : 4;  /**< Outgoing Priority Pointer RAMs (OFIFO) */
	uint64_t reserved_20_23               : 4;
	uint64_t oarb                         : 2;  /**< Outgoing Priority RAMs (OARB) */
	uint64_t ispf                         : 1;  /**< Incoming Soft Packet FIFO */
	uint64_t ospf                         : 1;  /**< Outgoing Soft Packet FIFO */
	uint64_t txbuf                        : 2;  /**< Outgoing SRIO MAC Buffer. */
	uint64_t rxbuf                        : 2;  /**< Incoming SRIO MAC Buffer. */
	uint64_t imsg                         : 5;  /**< Incoming Message RAMs. */
	uint64_t omsg                         : 7;  /**< Outgoing Message RAMs. */
#else
	uint64_t omsg                         : 7;
	uint64_t imsg                         : 5;
	uint64_t rxbuf                        : 2;
	uint64_t txbuf                        : 2;
	uint64_t ospf                         : 1;
	uint64_t ispf                         : 1;
	uint64_t oarb                         : 2;
	uint64_t reserved_20_23               : 4;
	uint64_t optrs                        : 4;
	uint64_t obulk                        : 4;
	uint64_t rtn                          : 2;
	uint64_t ofree                        : 1;
	uint64_t itag                         : 1;
	uint64_t otag                         : 2;
	uint64_t bell                         : 2;
	uint64_t cram                         : 2;
	uint64_t mram                         : 2;
	uint64_t reserved_44_63               : 20;
#endif
	} cn63xxp1;
	struct cvmx_sriox_bist_status_s       cn66xx;
};
typedef union cvmx_sriox_bist_status cvmx_sriox_bist_status_t;

/**
 * cvmx_srio#_imsg_ctrl
 *
 * SRIO_IMSG_CTRL = SRIO Incoming Message Control
 *
 * The SRIO Incoming Message Control Register
 *
 * Notes:
 * RSP_THR should not typically be modified from reset value.
 *
 * Clk_Rst:        SRIO(0,2..3)_IMSG_CTRL  hclk    hrst_n
 */
union cvmx_sriox_imsg_ctrl {
	uint64_t u64;
	struct cvmx_sriox_imsg_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t to_mode                      : 1;  /**< MP message timeout mode:
                                                         - 0: The timeout counter gets reset whenever the
                                                             next sequential segment is received, regardless
                                                             of whether it is accepted
                                                         - 1: The timeout counter gets reset only when the
                                                             next sequential segment is received and
                                                             accepted */
	uint64_t reserved_30_30               : 1;
	uint64_t rsp_thr                      : 6;  /**< Reserved */
	uint64_t reserved_22_23               : 2;
	uint64_t rp1_sid                      : 1;  /**< Sets msg response priority for incomimg messages
                                                         of priority 1 on the secondary ID (0=2, 1=3) */
	uint64_t rp0_sid                      : 2;  /**< Sets msg response priority for incomimg messages
                                                         of priority 0 on the secondary ID (0,1=1 2=2, 3=3) */
	uint64_t rp1_pid                      : 1;  /**< Sets msg response priority for incomimg messages
                                                         of priority 1 on the primary ID (0=2, 1=3) */
	uint64_t rp0_pid                      : 2;  /**< Sets msg response priority for incomimg messages
                                                         of priority 0 on the primary ID (0,1=1 2=2, 3=3) */
	uint64_t reserved_15_15               : 1;
	uint64_t prt_sel                      : 3;  /**< Port/Controller selection method:
                                                         - 0: Table lookup based on mailbox
                                                         - 1: Table lookup based on priority
                                                         - 2: Table lookup based on letter
                                                         - 3: Size-based (SP to port 0, MP to port 1)
                                                         - 4: ID-based (pri ID to port 0, sec ID to port 1) */
	uint64_t lttr                         : 4;  /**< Port/Controller selection letter table */
	uint64_t prio                         : 4;  /**< Port/Controller selection priority table */
	uint64_t mbox                         : 4;  /**< Port/Controller selection mailbox table */
#else
	uint64_t mbox                         : 4;
	uint64_t prio                         : 4;
	uint64_t lttr                         : 4;
	uint64_t prt_sel                      : 3;
	uint64_t reserved_15_15               : 1;
	uint64_t rp0_pid                      : 2;
	uint64_t rp1_pid                      : 1;
	uint64_t rp0_sid                      : 2;
	uint64_t rp1_sid                      : 1;
	uint64_t reserved_22_23               : 2;
	uint64_t rsp_thr                      : 6;
	uint64_t reserved_30_30               : 1;
	uint64_t to_mode                      : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_imsg_ctrl_s         cn63xx;
	struct cvmx_sriox_imsg_ctrl_s         cn63xxp1;
	struct cvmx_sriox_imsg_ctrl_s         cn66xx;
};
typedef union cvmx_sriox_imsg_ctrl cvmx_sriox_imsg_ctrl_t;

/**
 * cvmx_srio#_imsg_inst_hdr#
 *
 * SRIO_IMSG_INST_HDRX = SRIO Incoming Message Packet Instruction Header
 *
 * The SRIO Port/Controller X Incoming Message Packet Instruction Header Register
 *
 * Notes:
 * SRIO HW generates most of the SRIO_WORD1 fields from these values. SRIO_WORD1 is the 2nd of two
 *  header words that SRIO inserts in front of all received messages. SRIO_WORD1 may commonly be used
 *  as a PIP/IPD PKT_INST_HDR. This CSR matches the PIP/IPD PKT_INST_HDR format except for the QOS
 *  and GRP fields. SRIO*_IMSG_QOS_GRP*[QOS*,GRP*] supply the QOS and GRP fields.
 *
 * Clk_Rst:        SRIO(0,2..3)_IMSG_INST_HDR[0:1] hclk    hrst_n
 */
union cvmx_sriox_imsg_inst_hdrx {
	uint64_t u64;
	struct cvmx_sriox_imsg_inst_hdrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t r                            : 1;  /**< Port/Controller X R */
	uint64_t reserved_58_62               : 5;
	uint64_t pm                           : 2;  /**< Port/Controller X PM */
	uint64_t reserved_55_55               : 1;
	uint64_t sl                           : 7;  /**< Port/Controller X SL */
	uint64_t reserved_46_47               : 2;
	uint64_t nqos                         : 1;  /**< Port/Controller X NQOS */
	uint64_t ngrp                         : 1;  /**< Port/Controller X NGRP */
	uint64_t ntt                          : 1;  /**< Port/Controller X NTT */
	uint64_t ntag                         : 1;  /**< Port/Controller X NTAG */
	uint64_t reserved_35_41               : 7;
	uint64_t rs                           : 1;  /**< Port/Controller X RS */
	uint64_t tt                           : 2;  /**< Port/Controller X TT */
	uint64_t tag                          : 32; /**< Port/Controller X TAG */
#else
	uint64_t tag                          : 32;
	uint64_t tt                           : 2;
	uint64_t rs                           : 1;
	uint64_t reserved_35_41               : 7;
	uint64_t ntag                         : 1;
	uint64_t ntt                          : 1;
	uint64_t ngrp                         : 1;
	uint64_t nqos                         : 1;
	uint64_t reserved_46_47               : 2;
	uint64_t sl                           : 7;
	uint64_t reserved_55_55               : 1;
	uint64_t pm                           : 2;
	uint64_t reserved_58_62               : 5;
	uint64_t r                            : 1;
#endif
	} s;
	struct cvmx_sriox_imsg_inst_hdrx_s    cn63xx;
	struct cvmx_sriox_imsg_inst_hdrx_s    cn63xxp1;
	struct cvmx_sriox_imsg_inst_hdrx_s    cn66xx;
};
typedef union cvmx_sriox_imsg_inst_hdrx cvmx_sriox_imsg_inst_hdrx_t;

/**
 * cvmx_srio#_imsg_qos_grp#
 *
 * SRIO_IMSG_QOS_GRPX = SRIO Incoming Message QOS/GRP Table
 *
 * The SRIO Incoming Message QOS/GRP Table Entry X
 *
 * Notes:
 * The QOS/GRP table contains 32 entries with 8 QOS/GRP pairs per entry - 256 pairs total.  HW
 *  selects the table entry by the concatenation of SRIO_WORD0[PRIO,DIS,MBOX], thus entry 0 is used
 *  for messages with PRIO=0,DIS=0,MBOX=0, entry 1 is for PRIO=0,DIS=0,MBOX=1, etc.  HW selects the
 *  QOS/GRP pair from the table entry by the concatenation of SRIO_WORD0[ID,LETTER] as shown above. HW
 *  then inserts the QOS/GRP pair into SRIO_WORD1[QOS,GRP], which may commonly be used for the PIP/IPD
 *  PKT_INST_HDR[QOS,GRP] fields.
 *
 * Clk_Rst:        SRIO(0,2..3)_IMSG_QOS_GRP[0:1]  hclk    hrst_n
 */
union cvmx_sriox_imsg_qos_grpx {
	uint64_t u64;
	struct cvmx_sriox_imsg_qos_grpx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_63_63               : 1;
	uint64_t qos7                         : 3;  /**< Entry X:7 QOS (ID=1, LETTER=3) */
	uint64_t grp7                         : 4;  /**< Entry X:7 GRP (ID=1, LETTER=3) */
	uint64_t reserved_55_55               : 1;
	uint64_t qos6                         : 3;  /**< Entry X:6 QOS (ID=1, LETTER=2) */
	uint64_t grp6                         : 4;  /**< Entry X:6 GRP (ID=1, LETTER=2) */
	uint64_t reserved_47_47               : 1;
	uint64_t qos5                         : 3;  /**< Entry X:5 QOS (ID=1, LETTER=1) */
	uint64_t grp5                         : 4;  /**< Entry X:5 GRP (ID=1, LETTER=1) */
	uint64_t reserved_39_39               : 1;
	uint64_t qos4                         : 3;  /**< Entry X:4 QOS (ID=1, LETTER=0) */
	uint64_t grp4                         : 4;  /**< Entry X:4 GRP (ID=1, LETTER=0) */
	uint64_t reserved_31_31               : 1;
	uint64_t qos3                         : 3;  /**< Entry X:3 QOS (ID=0, LETTER=3) */
	uint64_t grp3                         : 4;  /**< Entry X:3 GRP (ID=0, LETTER=3) */
	uint64_t reserved_23_23               : 1;
	uint64_t qos2                         : 3;  /**< Entry X:2 QOS (ID=0, LETTER=2) */
	uint64_t grp2                         : 4;  /**< Entry X:2 GRP (ID=0, LETTER=2) */
	uint64_t reserved_15_15               : 1;
	uint64_t qos1                         : 3;  /**< Entry X:1 QOS (ID=0, LETTER=1) */
	uint64_t grp1                         : 4;  /**< Entry X:1 GRP (ID=0, LETTER=1) */
	uint64_t reserved_7_7                 : 1;
	uint64_t qos0                         : 3;  /**< Entry X:0 QOS (ID=0, LETTER=0) */
	uint64_t grp0                         : 4;  /**< Entry X:0 GRP (ID=0, LETTER=0) */
#else
	uint64_t grp0                         : 4;
	uint64_t qos0                         : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t grp1                         : 4;
	uint64_t qos1                         : 3;
	uint64_t reserved_15_15               : 1;
	uint64_t grp2                         : 4;
	uint64_t qos2                         : 3;
	uint64_t reserved_23_23               : 1;
	uint64_t grp3                         : 4;
	uint64_t qos3                         : 3;
	uint64_t reserved_31_31               : 1;
	uint64_t grp4                         : 4;
	uint64_t qos4                         : 3;
	uint64_t reserved_39_39               : 1;
	uint64_t grp5                         : 4;
	uint64_t qos5                         : 3;
	uint64_t reserved_47_47               : 1;
	uint64_t grp6                         : 4;
	uint64_t qos6                         : 3;
	uint64_t reserved_55_55               : 1;
	uint64_t grp7                         : 4;
	uint64_t qos7                         : 3;
	uint64_t reserved_63_63               : 1;
#endif
	} s;
	struct cvmx_sriox_imsg_qos_grpx_s     cn63xx;
	struct cvmx_sriox_imsg_qos_grpx_s     cn63xxp1;
	struct cvmx_sriox_imsg_qos_grpx_s     cn66xx;
};
typedef union cvmx_sriox_imsg_qos_grpx cvmx_sriox_imsg_qos_grpx_t;

/**
 * cvmx_srio#_imsg_status#
 *
 * SRIO_IMSG_STATUSX = SRIO Incoming Message Status Table
 *
 * The SRIO Incoming Message Status Table Entry X
 *
 * Notes:
 * Clk_Rst:        SRIO(0,2..3)_IMSG_STATUS[0:1]   hclk    hrst_n
 *
 */
union cvmx_sriox_imsg_statusx {
	uint64_t u64;
	struct cvmx_sriox_imsg_statusx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t val1                         : 1;  /**< Entry X:1 Valid */
	uint64_t err1                         : 1;  /**< Entry X:1 Error */
	uint64_t toe1                         : 1;  /**< Entry X:1 Timeout Error */
	uint64_t toc1                         : 1;  /**< Entry X:1 Timeout Count */
	uint64_t prt1                         : 1;  /**< Entry X:1 Port */
	uint64_t reserved_58_58               : 1;
	uint64_t tt1                          : 1;  /**< Entry X:1 TT ID */
	uint64_t dis1                         : 1;  /**< Entry X:1 Dest ID */
	uint64_t seg1                         : 4;  /**< Entry X:1 Next Segment */
	uint64_t mbox1                        : 2;  /**< Entry X:1 Mailbox */
	uint64_t lttr1                        : 2;  /**< Entry X:1 Letter */
	uint64_t sid1                         : 16; /**< Entry X:1 Source ID */
	uint64_t val0                         : 1;  /**< Entry X:0 Valid */
	uint64_t err0                         : 1;  /**< Entry X:0 Error */
	uint64_t toe0                         : 1;  /**< Entry X:0 Timeout Error */
	uint64_t toc0                         : 1;  /**< Entry X:0 Timeout Count */
	uint64_t prt0                         : 1;  /**< Entry X:0 Port */
	uint64_t reserved_26_26               : 1;
	uint64_t tt0                          : 1;  /**< Entry X:0 TT ID */
	uint64_t dis0                         : 1;  /**< Entry X:0 Dest ID */
	uint64_t seg0                         : 4;  /**< Entry X:0 Next Segment */
	uint64_t mbox0                        : 2;  /**< Entry X:0 Mailbox */
	uint64_t lttr0                        : 2;  /**< Entry X:0 Letter */
	uint64_t sid0                         : 16; /**< Entry X:0 Source ID */
#else
	uint64_t sid0                         : 16;
	uint64_t lttr0                        : 2;
	uint64_t mbox0                        : 2;
	uint64_t seg0                         : 4;
	uint64_t dis0                         : 1;
	uint64_t tt0                          : 1;
	uint64_t reserved_26_26               : 1;
	uint64_t prt0                         : 1;
	uint64_t toc0                         : 1;
	uint64_t toe0                         : 1;
	uint64_t err0                         : 1;
	uint64_t val0                         : 1;
	uint64_t sid1                         : 16;
	uint64_t lttr1                        : 2;
	uint64_t mbox1                        : 2;
	uint64_t seg1                         : 4;
	uint64_t dis1                         : 1;
	uint64_t tt1                          : 1;
	uint64_t reserved_58_58               : 1;
	uint64_t prt1                         : 1;
	uint64_t toc1                         : 1;
	uint64_t toe1                         : 1;
	uint64_t err1                         : 1;
	uint64_t val1                         : 1;
#endif
	} s;
	struct cvmx_sriox_imsg_statusx_s      cn63xx;
	struct cvmx_sriox_imsg_statusx_s      cn63xxp1;
	struct cvmx_sriox_imsg_statusx_s      cn66xx;
};
typedef union cvmx_sriox_imsg_statusx cvmx_sriox_imsg_statusx_t;

/**
 * cvmx_srio#_imsg_vport_thr
 *
 * SRIO_IMSG_VPORT_THR = SRIO Incoming Message Virtual Port Threshold
 *
 * The SRIO Incoming Message Virtual Port Threshold Register
 *
 * Notes:
 * SRIO0_IMSG_VPORT_THR.MAX_TOT must be >= SRIO0_IMSG_VPORT_THR.BUF_THR
 * + SRIO2_IMSG_VPORT_THR.BUF_THR + SRIO3_IMSG_VPORT_THR.BUF_THR.  This register can be accessed
 * regardless of the value in SRIO(0,2..3)_STATUS_REG.ACCESS and is not effected by MAC reset.  The maximum
 * number of VPORTs allocated to a MAC is limited to 46 if QLM0 is configured to x2 or x4 mode and 44
 * if configured in x1 mode.
 *
 * Clk_Rst:        SRIO(0,2..3)_IMSG_VPORT_THR     sclk    srst_n
 */
union cvmx_sriox_imsg_vport_thr {
	uint64_t u64;
	struct cvmx_sriox_imsg_vport_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t max_tot                      : 6;  /**< Sets max number of vports available to the chip
                                                         This field is only used in SRIO0. */
	uint64_t reserved_46_47               : 2;
	uint64_t max_s1                       : 6;  /**< Reserved
                                                         This field is only used in SRIO0. */
	uint64_t reserved_38_39               : 2;
	uint64_t max_s0                       : 6;  /**< Sets max number of vports available to SRIO0
                                                         This field is only used in SRIO0. */
	uint64_t sp_vport                     : 1;  /**< Single-segment vport pre-allocation.
                                                         When set, single-segment messages use pre-allocated
                                                         vport slots (that do not count toward thresholds).
                                                         When clear, single-segment messages must allocate
                                                         vport slots just like multi-segment messages do. */
	uint64_t reserved_20_30               : 11;
	uint64_t buf_thr                      : 4;  /**< Sets number of vports to be buffered by this
                                                         interface. BUF_THR must not be zero when receiving
                                                         messages. The max BUF_THR value is 8.
                                                         Recommend BUF_THR values 1-4. If the 46 available
                                                         vports are not statically-allocated across the two
                                                         SRIO's, smaller BUF_THR values may leave more
                                                         vports available for the other SRIO. Lack of a
                                                         buffered vport can force a retry for a received
                                                         first segment, so, particularly if SP_VPORT=0
                                                         (which is not recommended) or the segment size is
                                                         small, larger BUF_THR values may improve
                                                         performance. */
	uint64_t reserved_14_15               : 2;
	uint64_t max_p1                       : 6;  /**< Sets max number of open vports in port 1 */
	uint64_t reserved_6_7                 : 2;
	uint64_t max_p0                       : 6;  /**< Sets max number of open vports in port 0 */
#else
	uint64_t max_p0                       : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t max_p1                       : 6;
	uint64_t reserved_14_15               : 2;
	uint64_t buf_thr                      : 4;
	uint64_t reserved_20_30               : 11;
	uint64_t sp_vport                     : 1;
	uint64_t max_s0                       : 6;
	uint64_t reserved_38_39               : 2;
	uint64_t max_s1                       : 6;
	uint64_t reserved_46_47               : 2;
	uint64_t max_tot                      : 6;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_sriox_imsg_vport_thr_s    cn63xx;
	struct cvmx_sriox_imsg_vport_thr_s    cn63xxp1;
	struct cvmx_sriox_imsg_vport_thr_s    cn66xx;
};
typedef union cvmx_sriox_imsg_vport_thr cvmx_sriox_imsg_vport_thr_t;

/**
 * cvmx_srio#_imsg_vport_thr2
 *
 * SRIO_IMSG_VPORT_THR2 = SRIO Incoming Message Virtual Port Additional Threshold
 *
 * The SRIO Incoming Message Virtual Port Additional Threshold Register
 *
 * Notes:
 * Additional vport thresholds for SRIO MACs 2 and 3.  This register is only used in SRIO0 and is only
 * used when the QLM0 is configured as x1 lanes or x2 lanes.  In the x1 case the maximum number of
 * VPORTs is limited to 44.  In the x2 case the maximum number of VPORTs is limited to 46.  These
 * values are ignored in the x4 configuration.  This register can be accessed regardless of the value
 * in SRIO(0,2..3)_STATUS_REG.ACCESS and is not effected by MAC reset.
 *
 * Clk_Rst:        SRIO(0,2..3)_IMSG_VPORT_THR     sclk    srst_n
 */
union cvmx_sriox_imsg_vport_thr2 {
	uint64_t u64;
	struct cvmx_sriox_imsg_vport_thr2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_46_63               : 18;
	uint64_t max_s3                       : 6;  /**< Sets max number of vports available to SRIO3
                                                         This field is only used in SRIO0. */
	uint64_t reserved_38_39               : 2;
	uint64_t max_s2                       : 6;  /**< Sets max number of vports available to SRIO2
                                                         This field is only used in SRIO0. */
	uint64_t reserved_0_31                : 32;
#else
	uint64_t reserved_0_31                : 32;
	uint64_t max_s2                       : 6;
	uint64_t reserved_38_39               : 2;
	uint64_t max_s3                       : 6;
	uint64_t reserved_46_63               : 18;
#endif
	} s;
	struct cvmx_sriox_imsg_vport_thr2_s   cn66xx;
};
typedef union cvmx_sriox_imsg_vport_thr2 cvmx_sriox_imsg_vport_thr2_t;

/**
 * cvmx_srio#_int2_enable
 *
 * SRIO_INT2_ENABLE = SRIO Interrupt 2 Enable
 *
 * Allows SRIO to generate additional interrupts when corresponding enable bit is set.
 *
 * Notes:
 * This register enables interrupts in SRIO(0,2..3)_INT2_REG that can be asserted while the MAC is in reset.
 *  The register can be accessed/modified regardless of the value of SRIO(0,2..3)_STATUS_REG.ACCESS.
 *
 * Clk_Rst:        SRIO(0,2..3)_INT2_ENABLE        sclk    srst_n
 */
union cvmx_sriox_int2_enable {
	uint64_t u64;
	struct cvmx_sriox_int2_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t pko_rst                      : 1;  /**< PKO Reset Error Enable */
#else
	uint64_t pko_rst                      : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_sriox_int2_enable_s       cn63xx;
	struct cvmx_sriox_int2_enable_s       cn66xx;
};
typedef union cvmx_sriox_int2_enable cvmx_sriox_int2_enable_t;

/**
 * cvmx_srio#_int2_reg
 *
 * SRIO_INT2_REG = SRIO Interrupt 2 Register
 *
 * Displays and clears which enabled interrupts have occured
 *
 * Notes:
 * This register provides interrupt status. Unlike SRIO*_INT_REG, SRIO*_INT2_REG can be accessed
 *  whenever the SRIO is present, regardless of whether the corresponding SRIO is in reset or not.
 *  INT_SUM shows the status of the interrupts in SRIO(0,2..3)_INT_REG.  Any set bits written to this
 *  register clear the corresponding interrupt.  The register can be accessed/modified regardless of
 *  the value of SRIO(0,2..3)_STATUS_REG.ACCESS and probably should be the first register read when an SRIO
 *  interrupt occurs.
 *
 * Clk_Rst:        SRIO(0,2..3)_INT2_REG   sclk    srst_n
 */
union cvmx_sriox_int2_reg {
	uint64_t u64;
	struct cvmx_sriox_int2_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t int_sum                      : 1;  /**< Interrupt Set and Enabled in SRIO(0,2..3)_INT_REG */
	uint64_t reserved_1_30                : 30;
	uint64_t pko_rst                      : 1;  /**< PKO Reset Error - Message Received from PKO while
                                                         MAC in reset. */
#else
	uint64_t pko_rst                      : 1;
	uint64_t reserved_1_30                : 30;
	uint64_t int_sum                      : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_int2_reg_s          cn63xx;
	struct cvmx_sriox_int2_reg_s          cn66xx;
};
typedef union cvmx_sriox_int2_reg cvmx_sriox_int2_reg_t;

/**
 * cvmx_srio#_int_enable
 *
 * SRIO_INT_ENABLE = SRIO Interrupt Enable
 *
 * Allows SRIO to generate interrupts when corresponding enable bit is set.
 *
 * Notes:
 * This register enables interrupts.
 *
 * Clk_Rst:        SRIO(0,2..3)_INT_ENABLE hclk    hrst_n
 */
union cvmx_sriox_int_enable {
	uint64_t u64;
	struct cvmx_sriox_int_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t zero_pkt                     : 1;  /**< Received Incoming SRIO Zero byte packet */
	uint64_t ttl_tout                     : 1;  /**< Outgoing Packet Time to Live Timeout */
	uint64_t fail                         : 1;  /**< ERB Error Rate reached Fail Count */
	uint64_t degrade                      : 1;  /**< ERB Error Rate reached Degrade Count */
	uint64_t mac_buf                      : 1;  /**< SRIO MAC Buffer CRC Error */
	uint64_t f_error                      : 1;  /**< SRIO Fatal Port Error (MAC reset required) */
	uint64_t rtry_err                     : 1;  /**< Outbound Message Retry Threshold Exceeded */
	uint64_t pko_err                      : 1;  /**< Outbound Message Received PKO Error */
	uint64_t omsg_err                     : 1;  /**< Outbound Message Invalid Descriptor Error */
	uint64_t omsg1                        : 1;  /**< Controller 1 Outbound Message Complete */
	uint64_t omsg0                        : 1;  /**< Controller 0 Outbound Message Complete */
	uint64_t link_up                      : 1;  /**< Serial Link going from Inactive to Active */
	uint64_t link_dwn                     : 1;  /**< Serial Link going from Active to Inactive */
	uint64_t phy_erb                      : 1;  /**< Physical Layer Error detected in ERB */
	uint64_t log_erb                      : 1;  /**< Logical/Transport Layer Error detected in ERB */
	uint64_t soft_rx                      : 1;  /**< Incoming Packet received by Soft Packet FIFO */
	uint64_t soft_tx                      : 1;  /**< Outgoing Packet sent by Soft Packet FIFO */
	uint64_t mce_rx                       : 1;  /**< Incoming Multicast Event Symbol */
	uint64_t mce_tx                       : 1;  /**< Outgoing Multicast Event Transmit Complete */
	uint64_t wr_done                      : 1;  /**< Outgoing Last Nwrite_R DONE Response Received. */
	uint64_t sli_err                      : 1;  /**< Unsupported S2M Transaction Received. */
	uint64_t deny_wr                      : 1;  /**< Incoming Maint_Wr Access to Denied Bar Registers. */
	uint64_t bar_err                      : 1;  /**< Incoming Access Crossing/Missing BAR Address */
	uint64_t maint_op                     : 1;  /**< Internal Maintenance Operation Complete. */
	uint64_t rxbell                       : 1;  /**< One or more Incoming Doorbells Received. */
	uint64_t bell_err                     : 1;  /**< Outgoing Doorbell Timeout, Retry or Error. */
	uint64_t txbell                       : 1;  /**< Outgoing Doorbell Complete. */
#else
	uint64_t txbell                       : 1;
	uint64_t bell_err                     : 1;
	uint64_t rxbell                       : 1;
	uint64_t maint_op                     : 1;
	uint64_t bar_err                      : 1;
	uint64_t deny_wr                      : 1;
	uint64_t sli_err                      : 1;
	uint64_t wr_done                      : 1;
	uint64_t mce_tx                       : 1;
	uint64_t mce_rx                       : 1;
	uint64_t soft_tx                      : 1;
	uint64_t soft_rx                      : 1;
	uint64_t log_erb                      : 1;
	uint64_t phy_erb                      : 1;
	uint64_t link_dwn                     : 1;
	uint64_t link_up                      : 1;
	uint64_t omsg0                        : 1;
	uint64_t omsg1                        : 1;
	uint64_t omsg_err                     : 1;
	uint64_t pko_err                      : 1;
	uint64_t rtry_err                     : 1;
	uint64_t f_error                      : 1;
	uint64_t mac_buf                      : 1;
	uint64_t degrade                      : 1;
	uint64_t fail                         : 1;
	uint64_t ttl_tout                     : 1;
	uint64_t zero_pkt                     : 1;
	uint64_t reserved_27_63               : 37;
#endif
	} s;
	struct cvmx_sriox_int_enable_s        cn63xx;
	struct cvmx_sriox_int_enable_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t f_error                      : 1;  /**< SRIO Fatal Port Error (MAC reset required) */
	uint64_t rtry_err                     : 1;  /**< Outbound Message Retry Threshold Exceeded */
	uint64_t pko_err                      : 1;  /**< Outbound Message Received PKO Error */
	uint64_t omsg_err                     : 1;  /**< Outbound Message Invalid Descriptor Error */
	uint64_t omsg1                        : 1;  /**< Controller 1 Outbound Message Complete */
	uint64_t omsg0                        : 1;  /**< Controller 0 Outbound Message Complete */
	uint64_t link_up                      : 1;  /**< Serial Link going from Inactive to Active */
	uint64_t link_dwn                     : 1;  /**< Serial Link going from Active to Inactive */
	uint64_t phy_erb                      : 1;  /**< Physical Layer Error detected in ERB */
	uint64_t log_erb                      : 1;  /**< Logical/Transport Layer Error detected in ERB */
	uint64_t soft_rx                      : 1;  /**< Incoming Packet received by Soft Packet FIFO */
	uint64_t soft_tx                      : 1;  /**< Outgoing Packet sent by Soft Packet FIFO */
	uint64_t mce_rx                       : 1;  /**< Incoming Multicast Event Symbol */
	uint64_t mce_tx                       : 1;  /**< Outgoing Multicast Event Transmit Complete */
	uint64_t wr_done                      : 1;  /**< Outgoing Last Nwrite_R DONE Response Received. */
	uint64_t sli_err                      : 1;  /**< Unsupported S2M Transaction Received. */
	uint64_t deny_wr                      : 1;  /**< Incoming Maint_Wr Access to Denied Bar Registers. */
	uint64_t bar_err                      : 1;  /**< Incoming Access Crossing/Missing BAR Address */
	uint64_t maint_op                     : 1;  /**< Internal Maintenance Operation Complete. */
	uint64_t rxbell                       : 1;  /**< One or more Incoming Doorbells Received. */
	uint64_t bell_err                     : 1;  /**< Outgoing Doorbell Timeout, Retry or Error. */
	uint64_t txbell                       : 1;  /**< Outgoing Doorbell Complete. */
#else
	uint64_t txbell                       : 1;
	uint64_t bell_err                     : 1;
	uint64_t rxbell                       : 1;
	uint64_t maint_op                     : 1;
	uint64_t bar_err                      : 1;
	uint64_t deny_wr                      : 1;
	uint64_t sli_err                      : 1;
	uint64_t wr_done                      : 1;
	uint64_t mce_tx                       : 1;
	uint64_t mce_rx                       : 1;
	uint64_t soft_tx                      : 1;
	uint64_t soft_rx                      : 1;
	uint64_t log_erb                      : 1;
	uint64_t phy_erb                      : 1;
	uint64_t link_dwn                     : 1;
	uint64_t link_up                      : 1;
	uint64_t omsg0                        : 1;
	uint64_t omsg1                        : 1;
	uint64_t omsg_err                     : 1;
	uint64_t pko_err                      : 1;
	uint64_t rtry_err                     : 1;
	uint64_t f_error                      : 1;
	uint64_t reserved_22_63               : 42;
#endif
	} cn63xxp1;
	struct cvmx_sriox_int_enable_s        cn66xx;
};
typedef union cvmx_sriox_int_enable cvmx_sriox_int_enable_t;

/**
 * cvmx_srio#_int_info0
 *
 * SRIO_INT_INFO0 = SRIO Interrupt Information
 *
 * The SRIO Interrupt Information
 *
 * Notes:
 * This register contains the first header word of the illegal s2m transaction associated with the
 *  SLI_ERR interrupt.  The remaining information is located in SRIO(0,2..3)_INT_INFO1.   This register is
 *  only updated when the SLI_ERR is initially detected.  Once the interrupt is cleared then
 *  additional information can be captured.
 *  Common Errors Include:
 *   1.  Load/Stores with Length over 32
 *   2.  Load/Stores that translate to Maintenance Ops with a length over 8
 *   3.  Load Ops that translate to Atomic Ops with other than 1, 2 and 4 byte accesses
 *   4.  Load/Store Ops with a Length 0
 *   5.  Unexpected Responses
 *
 * Clk_Rst:        SRIO(0,2..3)_INT_REG    hclk    hrst_n
 */
union cvmx_sriox_int_info0 {
	uint64_t u64;
	struct cvmx_sriox_int_info0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cmd                          : 4;  /**< Command
                                                         0 = Load, Outgoing Read Request
                                                         4 = Store, Outgoing Write Request
                                                         8 = Response, Outgoing Read Response
                                                         All Others are reserved and generate errors */
	uint64_t type                         : 4;  /**< Command Type
                                                         Load/Store SRIO_S2M_TYPE used
                                                         Response (Reserved) */
	uint64_t tag                          : 8;  /**< Internal Transaction Number */
	uint64_t reserved_42_47               : 6;
	uint64_t length                       : 10; /**< Data Length in 64-bit Words (Load/Store Only) */
	uint64_t status                       : 3;  /**< Response Status
                                                         0 = Success
                                                         1 = Error
                                                         All others reserved */
	uint64_t reserved_16_28               : 13;
	uint64_t be0                          : 8;  /**< First 64-bit Word Byte Enables (Load/Store Only) */
	uint64_t be1                          : 8;  /**< Last 64-bit Word Byte Enables (Load/Store Only) */
#else
	uint64_t be1                          : 8;
	uint64_t be0                          : 8;
	uint64_t reserved_16_28               : 13;
	uint64_t status                       : 3;
	uint64_t length                       : 10;
	uint64_t reserved_42_47               : 6;
	uint64_t tag                          : 8;
	uint64_t type                         : 4;
	uint64_t cmd                          : 4;
#endif
	} s;
	struct cvmx_sriox_int_info0_s         cn63xx;
	struct cvmx_sriox_int_info0_s         cn63xxp1;
	struct cvmx_sriox_int_info0_s         cn66xx;
};
typedef union cvmx_sriox_int_info0 cvmx_sriox_int_info0_t;

/**
 * cvmx_srio#_int_info1
 *
 * SRIO_INT_INFO1 = SRIO Interrupt Information
 *
 * The SRIO Interrupt Information
 *
 * Notes:
 * This register contains the second header word of the illegal s2m transaction associated with the
 *  SLI_ERR interrupt.  The remaining information is located in SRIO(0,2..3)_INT_INFO0.   This register is
 *  only updated when the SLI_ERR is initially detected.  Once the interrupt is cleared then
 *  additional information can be captured.
 *
 * Clk_Rst:        SRIO(0,2..3)_INT_REG    hclk    hrst_n
 */
union cvmx_sriox_int_info1 {
	uint64_t u64;
	struct cvmx_sriox_int_info1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t info1                        : 64; /**< Address (Load/Store) or First 64-bit Word of
                                                         Response Data Associated with Interrupt */
#else
	uint64_t info1                        : 64;
#endif
	} s;
	struct cvmx_sriox_int_info1_s         cn63xx;
	struct cvmx_sriox_int_info1_s         cn63xxp1;
	struct cvmx_sriox_int_info1_s         cn66xx;
};
typedef union cvmx_sriox_int_info1 cvmx_sriox_int_info1_t;

/**
 * cvmx_srio#_int_info2
 *
 * SRIO_INT_INFO2 = SRIO Interrupt Information
 *
 * The SRIO Interrupt Information
 *
 * Notes:
 * This register contains the invalid outbound message descriptor associated with the OMSG_ERR
 *  interrupt.  This register is only updated when the OMSG_ERR is initially detected.  Once the
 *  interrupt is cleared then additional information can be captured.
 *
 * Clk_Rst:        SRIO(0,2..3)_INT_REG    hclk    hrst_n
 */
union cvmx_sriox_int_info2 {
	uint64_t u64;
	struct cvmx_sriox_int_info2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t prio                         : 2;  /**< PRIO field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t tt                           : 1;  /**< TT field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t sis                          : 1;  /**< SIS field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t ssize                        : 4;  /**< SSIZE field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t did                          : 16; /**< DID field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t xmbox                        : 4;  /**< XMBOX field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t mbox                         : 2;  /**< MBOX field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t letter                       : 2;  /**< LETTER field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t rsrvd                        : 30; /**< RSRVD field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t lns                          : 1;  /**< LNS field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
	uint64_t intr                         : 1;  /**< INT field of outbound message descriptor
                                                         associated with the OMSG_ERR interrupt */
#else
	uint64_t intr                         : 1;
	uint64_t lns                          : 1;
	uint64_t rsrvd                        : 30;
	uint64_t letter                       : 2;
	uint64_t mbox                         : 2;
	uint64_t xmbox                        : 4;
	uint64_t did                          : 16;
	uint64_t ssize                        : 4;
	uint64_t sis                          : 1;
	uint64_t tt                           : 1;
	uint64_t prio                         : 2;
#endif
	} s;
	struct cvmx_sriox_int_info2_s         cn63xx;
	struct cvmx_sriox_int_info2_s         cn63xxp1;
	struct cvmx_sriox_int_info2_s         cn66xx;
};
typedef union cvmx_sriox_int_info2 cvmx_sriox_int_info2_t;

/**
 * cvmx_srio#_int_info3
 *
 * SRIO_INT_INFO3 = SRIO Interrupt Information
 *
 * The SRIO Interrupt Information
 *
 * Notes:
 * This register contains the retry response associated with the RTRY_ERR interrupt.  This register
 *  is only updated when the RTRY_ERR is initially detected.  Once the interrupt is cleared then
 *  additional information can be captured.
 *
 * Clk_Rst:        SRIO(0,2..3)_INT_REG    hclk    hrst_n
 */
union cvmx_sriox_int_info3 {
	uint64_t u64;
	struct cvmx_sriox_int_info3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t prio                         : 2;  /**< Priority of received retry response message */
	uint64_t tt                           : 2;  /**< TT of received retry response message */
	uint64_t type                         : 4;  /**< Type of received retry response message
                                                         (should be 13) */
	uint64_t other                        : 48; /**< Other fields of received retry response message
                                                         If TT==0 (8-bit ID's)
                                                          OTHER<47:40> => destination ID
                                                          OTHER<39:32> => source ID
                                                          OTHER<31:28> => transaction (should be 1 - msg)
                                                          OTHER<27:24> => status (should be 3 - retry)
                                                          OTHER<23:22> => letter
                                                          OTHER<21:20> => mbox
                                                          OTHER<19:16> => msgseg
                                                          OTHER<15:0>  => unused
                                                         If TT==1 (16-bit ID's)
                                                          OTHER<47:32> => destination ID
                                                          OTHER<31:16> => source ID
                                                          OTHER<15:12> => transaction (should be 1 - msg)
                                                          OTHER<11:8>  => status (should be 3 - retry)
                                                          OTHER<7:6>   => letter
                                                          OTHER<5:4>   => mbox
                                                          OTHER<3:0>   => msgseg */
	uint64_t reserved_0_7                 : 8;
#else
	uint64_t reserved_0_7                 : 8;
	uint64_t other                        : 48;
	uint64_t type                         : 4;
	uint64_t tt                           : 2;
	uint64_t prio                         : 2;
#endif
	} s;
	struct cvmx_sriox_int_info3_s         cn63xx;
	struct cvmx_sriox_int_info3_s         cn63xxp1;
	struct cvmx_sriox_int_info3_s         cn66xx;
};
typedef union cvmx_sriox_int_info3 cvmx_sriox_int_info3_t;

/**
 * cvmx_srio#_int_reg
 *
 * SRIO_INT_REG = SRIO Interrupt Register
 *
 * Displays and clears which enabled interrupts have occured
 *
 * Notes:
 * This register provides interrupt status.  Like most SRIO CSRs, this register can only
 *  be read/written when the corresponding SRIO is both present and not in reset. (SRIO*_INT2_REG
 *  can be accessed when SRIO is in reset.) Any set bits written to this register clear the
 *  corresponding interrupt.  The RXBELL interrupt is cleared by reading all the entries in the
 *  incoming Doorbell FIFO.  The LOG_ERB interrupt must be cleared before writing zeroes
 *  to clear the bits in the SRIOMAINT*_ERB_LT_ERR_DET register.  Otherwise a new interrupt may be
 *  lost. The PHY_ERB interrupt must be cleared before writing a zero to
 *  SRIOMAINT*_ERB_ATTR_CAPT[VALID]. Otherwise, a new interrupt may be lost.  OMSG_ERR is set when an
 *  invalid outbound message descriptor is received.  The descriptor is deemed to be invalid if the
 *  SSIZE field is set to a reserved value, the SSIZE field combined with the packet length would
 *  result in more than 16 message segments, or the packet only contains a descriptor (no data).
 *
 * Clk_Rst:        SRIO(0,2..3)_INT_REG    hclk    hrst_n
 */
union cvmx_sriox_int_reg {
	uint64_t u64;
	struct cvmx_sriox_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t int2_sum                     : 1;  /**< Interrupt Set and Enabled in SRIO(0,2..3)_INT2_REG */
	uint64_t reserved_27_30               : 4;
	uint64_t zero_pkt                     : 1;  /**< Received Incoming SRIO Zero byte packet */
	uint64_t ttl_tout                     : 1;  /**< Outgoing Packet Time to Live Timeout
                                                         See SRIOMAINT(0,2..3)_DROP_PACKET */
	uint64_t fail                         : 1;  /**< ERB Error Rate reached Fail Count
                                                         See SRIOMAINT(0,2..3)_ERB_ERR_RATE */
	uint64_t degrad                       : 1;  /**< ERB Error Rate reached Degrade Count
                                                         See SRIOMAINT(0,2..3)_ERB_ERR_RATE */
	uint64_t mac_buf                      : 1;  /**< SRIO MAC Buffer CRC Error
                                                         See SRIO(0,2..3)_MAC_BUFFERS */
	uint64_t f_error                      : 1;  /**< SRIO Fatal Port Error (MAC reset required) */
	uint64_t rtry_err                     : 1;  /**< Outbound Message Retry Threshold Exceeded
                                                         See SRIO(0,2..3)_INT_INFO3
                                                         When one or more of the segments in an outgoing
                                                         message have a RTRY_ERR, SRIO will not set
                                                         OMSG* after the message "transfer". */
	uint64_t pko_err                      : 1;  /**< Outbound Message Received PKO Error */
	uint64_t omsg_err                     : 1;  /**< Outbound Message Invalid Descriptor Error
                                                         See SRIO(0,2..3)_INT_INFO2 */
	uint64_t omsg1                        : 1;  /**< Controller 1 Outbound Message Complete
                                                         See SRIO(0,2..3)_OMSG_DONE_COUNTS1 */
	uint64_t omsg0                        : 1;  /**< Controller 0 Outbound Message Complete
                                                         See SRIO(0,2..3)_OMSG_DONE_COUNTS0 */
	uint64_t link_up                      : 1;  /**< Serial Link going from Inactive to Active */
	uint64_t link_dwn                     : 1;  /**< Serial Link going from Active to Inactive */
	uint64_t phy_erb                      : 1;  /**< Physical Layer Error detected in ERB
                                                         See SRIOMAINT*_ERB_ATTR_CAPT */
	uint64_t log_erb                      : 1;  /**< Logical/Transport Layer Error detected in ERB
                                                         See SRIOMAINT(0,2..3)_ERB_LT_ERR_DET */
	uint64_t soft_rx                      : 1;  /**< Incoming Packet received by Soft Packet FIFO */
	uint64_t soft_tx                      : 1;  /**< Outgoing Packet sent by Soft Packet FIFO */
	uint64_t mce_rx                       : 1;  /**< Incoming Multicast Event Symbol */
	uint64_t mce_tx                       : 1;  /**< Outgoing Multicast Event Transmit Complete */
	uint64_t wr_done                      : 1;  /**< Outgoing Last Nwrite_R DONE Response Received.
                                                         See SRIO(0,2..3)_WR_DONE_COUNTS */
	uint64_t sli_err                      : 1;  /**< Unsupported S2M Transaction Received.
                                                         See SRIO(0,2..3)_INT_INFO[1:0] */
	uint64_t deny_wr                      : 1;  /**< Incoming Maint_Wr Access to Denied Bar Registers. */
	uint64_t bar_err                      : 1;  /**< Incoming Access Crossing/Missing BAR Address */
	uint64_t maint_op                     : 1;  /**< Internal Maintenance Operation Complete.
                                                         See SRIO(0,2..3)_MAINT_OP and SRIO(0,2..3)_MAINT_RD_DATA */
	uint64_t rxbell                       : 1;  /**< One or more Incoming Doorbells Received.
                                                         Read SRIO(0,2..3)_RX_BELL to empty FIFO */
	uint64_t bell_err                     : 1;  /**< Outgoing Doorbell Timeout, Retry or Error.
                                                         See SRIO(0,2..3)_TX_BELL_INFO */
	uint64_t txbell                       : 1;  /**< Outgoing Doorbell Complete.
                                                         TXBELL will not be asserted if a Timeout, Retry or
                                                         Error occurs. */
#else
	uint64_t txbell                       : 1;
	uint64_t bell_err                     : 1;
	uint64_t rxbell                       : 1;
	uint64_t maint_op                     : 1;
	uint64_t bar_err                      : 1;
	uint64_t deny_wr                      : 1;
	uint64_t sli_err                      : 1;
	uint64_t wr_done                      : 1;
	uint64_t mce_tx                       : 1;
	uint64_t mce_rx                       : 1;
	uint64_t soft_tx                      : 1;
	uint64_t soft_rx                      : 1;
	uint64_t log_erb                      : 1;
	uint64_t phy_erb                      : 1;
	uint64_t link_dwn                     : 1;
	uint64_t link_up                      : 1;
	uint64_t omsg0                        : 1;
	uint64_t omsg1                        : 1;
	uint64_t omsg_err                     : 1;
	uint64_t pko_err                      : 1;
	uint64_t rtry_err                     : 1;
	uint64_t f_error                      : 1;
	uint64_t mac_buf                      : 1;
	uint64_t degrad                       : 1;
	uint64_t fail                         : 1;
	uint64_t ttl_tout                     : 1;
	uint64_t zero_pkt                     : 1;
	uint64_t reserved_27_30               : 4;
	uint64_t int2_sum                     : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_int_reg_s           cn63xx;
	struct cvmx_sriox_int_reg_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t f_error                      : 1;  /**< SRIO Fatal Port Error (MAC reset required) */
	uint64_t rtry_err                     : 1;  /**< Outbound Message Retry Threshold Exceeded
                                                         See SRIO(0..1)_INT_INFO3
                                                         When one or more of the segments in an outgoing
                                                         message have a RTRY_ERR, SRIO will not set
                                                         OMSG* after the message "transfer". */
	uint64_t pko_err                      : 1;  /**< Outbound Message Received PKO Error */
	uint64_t omsg_err                     : 1;  /**< Outbound Message Invalid Descriptor Error
                                                         See SRIO(0..1)_INT_INFO2 */
	uint64_t omsg1                        : 1;  /**< Controller 1 Outbound Message Complete */
	uint64_t omsg0                        : 1;  /**< Controller 0 Outbound Message Complete */
	uint64_t link_up                      : 1;  /**< Serial Link going from Inactive to Active */
	uint64_t link_dwn                     : 1;  /**< Serial Link going from Active to Inactive */
	uint64_t phy_erb                      : 1;  /**< Physical Layer Error detected in ERB
                                                         See SRIOMAINT*_ERB_ATTR_CAPT */
	uint64_t log_erb                      : 1;  /**< Logical/Transport Layer Error detected in ERB
                                                         See SRIOMAINT(0..1)_ERB_LT_ERR_DET */
	uint64_t soft_rx                      : 1;  /**< Incoming Packet received by Soft Packet FIFO */
	uint64_t soft_tx                      : 1;  /**< Outgoing Packet sent by Soft Packet FIFO */
	uint64_t mce_rx                       : 1;  /**< Incoming Multicast Event Symbol */
	uint64_t mce_tx                       : 1;  /**< Outgoing Multicast Event Transmit Complete */
	uint64_t wr_done                      : 1;  /**< Outgoing Last Nwrite_R DONE Response Received. */
	uint64_t sli_err                      : 1;  /**< Unsupported S2M Transaction Received.
                                                         See SRIO(0..1)_INT_INFO[1:0] */
	uint64_t deny_wr                      : 1;  /**< Incoming Maint_Wr Access to Denied Bar Registers. */
	uint64_t bar_err                      : 1;  /**< Incoming Access Crossing/Missing BAR Address */
	uint64_t maint_op                     : 1;  /**< Internal Maintenance Operation Complete.
                                                         See SRIO(0..1)_MAINT_OP and SRIO(0..1)_MAINT_RD_DATA */
	uint64_t rxbell                       : 1;  /**< One or more Incoming Doorbells Received.
                                                         Read SRIO(0..1)_RX_BELL to empty FIFO */
	uint64_t bell_err                     : 1;  /**< Outgoing Doorbell Timeout, Retry or Error.
                                                         See SRIO(0..1)_TX_BELL_INFO */
	uint64_t txbell                       : 1;  /**< Outgoing Doorbell Complete.
                                                         TXBELL will not be asserted if a Timeout, Retry or
                                                         Error occurs. */
#else
	uint64_t txbell                       : 1;
	uint64_t bell_err                     : 1;
	uint64_t rxbell                       : 1;
	uint64_t maint_op                     : 1;
	uint64_t bar_err                      : 1;
	uint64_t deny_wr                      : 1;
	uint64_t sli_err                      : 1;
	uint64_t wr_done                      : 1;
	uint64_t mce_tx                       : 1;
	uint64_t mce_rx                       : 1;
	uint64_t soft_tx                      : 1;
	uint64_t soft_rx                      : 1;
	uint64_t log_erb                      : 1;
	uint64_t phy_erb                      : 1;
	uint64_t link_dwn                     : 1;
	uint64_t link_up                      : 1;
	uint64_t omsg0                        : 1;
	uint64_t omsg1                        : 1;
	uint64_t omsg_err                     : 1;
	uint64_t pko_err                      : 1;
	uint64_t rtry_err                     : 1;
	uint64_t f_error                      : 1;
	uint64_t reserved_22_63               : 42;
#endif
	} cn63xxp1;
	struct cvmx_sriox_int_reg_s           cn66xx;
};
typedef union cvmx_sriox_int_reg cvmx_sriox_int_reg_t;

/**
 * cvmx_srio#_ip_feature
 *
 * SRIO_IP_FEATURE = SRIO IP Feature Select
 *
 * Debug Register used to enable IP Core Features
 *
 * Notes:
 * This register is used to override powerup values used by the SRIOMAINT Registers and QLM
 *  configuration.  The register is only reset during COLD boot.  It should only be modified only
 *  while SRIO(0,2..3)_STATUS_REG.ACCESS is zero.
 *
 * Clk_Rst:        SRIO(0,2..3)_IP_FEATURE sclk    srst_cold_n
 */
union cvmx_sriox_ip_feature {
	uint64_t u64;
	struct cvmx_sriox_ip_feature_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ops                          : 32; /**< Reset Value for the OPs fields in both the
                                                         SRIOMAINT(0,2..3)_SRC_OPS and SRIOMAINT(0,2..3)_DST_OPS
                                                         registers. */
	uint64_t reserved_15_31               : 17;
	uint64_t no_vmin                      : 1;  /**< Lane Sync Valid Minimum Count Disable. (Pass 3)
                                                         0 = Wait for 2^12 valid codewords and at least
                                                             127 comma characters before starting
                                                             alignment.
                                                         1 = Wait only for 127 comma characters before
                                                             starting alignment. (SRIO V1.3 Compatable) */
	uint64_t a66                          : 1;  /**< 66-bit Address Support.  Value for bit 2 of the
                                                         EX_ADDR field in the SRIOMAINT(0,2..3)_PE_FEAT register. */
	uint64_t a50                          : 1;  /**< 50-bit Address Support.  Value for bit 1 of the
                                                         EX_ADDR field in the SRIOMAINT(0,2..3)_PE_FEAT register. */
	uint64_t reserved_11_11               : 1;
	uint64_t tx_flow                      : 1;  /**< Reset Value for the TX_FLOW field in the
                                                         SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG register. */
	uint64_t pt_width                     : 2;  /**< Value for the PT_WIDTH field in the
                                                         SRIOMAINT(0,2..3)_PORT_0_CTL register. */
	uint64_t tx_pol                       : 4;  /**< TX Serdes Polarity Lanes 3-0
                                                         0 = Normal Operation
                                                         1 = Invert, Swap +/- Tx SERDES Pins */
	uint64_t rx_pol                       : 4;  /**< RX Serdes Polarity Lanes 3-0
                                                         0 = Normal Operation
                                                         1 = Invert, Swap +/- Rx SERDES Pins */
#else
	uint64_t rx_pol                       : 4;
	uint64_t tx_pol                       : 4;
	uint64_t pt_width                     : 2;
	uint64_t tx_flow                      : 1;
	uint64_t reserved_11_11               : 1;
	uint64_t a50                          : 1;
	uint64_t a66                          : 1;
	uint64_t no_vmin                      : 1;
	uint64_t reserved_15_31               : 17;
	uint64_t ops                          : 32;
#endif
	} s;
	struct cvmx_sriox_ip_feature_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ops                          : 32; /**< Reset Value for the OPs fields in both the
                                                         SRIOMAINT(0..1)_SRC_OPS and SRIOMAINT(0..1)_DST_OPS
                                                         registers. */
	uint64_t reserved_14_31               : 18;
	uint64_t a66                          : 1;  /**< 66-bit Address Support.  Value for bit 2 of the
                                                         EX_ADDR field in the SRIOMAINT(0..1)_PE_FEAT register. */
	uint64_t a50                          : 1;  /**< 50-bit Address Support.  Value for bit 1 of the
                                                         EX_ADDR field in the SRIOMAINT(0..1)_PE_FEAT register. */
	uint64_t reserved_11_11               : 1;
	uint64_t tx_flow                      : 1;  /**< Reset Value for the TX_FLOW field in the
                                                         SRIOMAINT(0..1)_IR_BUFFER_CONFIG register.
                                                         Pass 2 will Reset to 1 when RTL ready.
                                                         (TX flow control not supported in pass 1) */
	uint64_t pt_width                     : 2;  /**< Value for the PT_WIDTH field in the
                                                         SRIOMAINT(0..1)_PORT_0_CTL register.
                                                         Reset to 0x2 rather than 0x3 in pass 1 (2 lane
                                                         interface supported in pass 1). */
	uint64_t tx_pol                       : 4;  /**< TX Serdes Polarity Lanes 3-0
                                                         0 = Normal Operation
                                                         1 = Invert, Swap +/- Tx SERDES Pins */
	uint64_t rx_pol                       : 4;  /**< RX Serdes Polarity Lanes 3-0
                                                         0 = Normal Operation
                                                         1 = Invert, Swap +/- Rx SERDES Pins */
#else
	uint64_t rx_pol                       : 4;
	uint64_t tx_pol                       : 4;
	uint64_t pt_width                     : 2;
	uint64_t tx_flow                      : 1;
	uint64_t reserved_11_11               : 1;
	uint64_t a50                          : 1;
	uint64_t a66                          : 1;
	uint64_t reserved_14_31               : 18;
	uint64_t ops                          : 32;
#endif
	} cn63xx;
	struct cvmx_sriox_ip_feature_cn63xx   cn63xxp1;
	struct cvmx_sriox_ip_feature_s        cn66xx;
};
typedef union cvmx_sriox_ip_feature cvmx_sriox_ip_feature_t;

/**
 * cvmx_srio#_mac_buffers
 *
 * SRIO_MAC_BUFFERS = SRIO MAC Buffer Control
 *
 * Reports errors and controls buffer usage on the main MAC buffers
 *
 * Notes:
 * Register displays errors status for each of the eight RX and TX buffers and controls use of the
 *  buffer in future operations.  It also displays the number of RX and TX buffers currently used by
 *  the MAC.
 *
 * Clk_Rst:        SRIO(0,2..3)_MAC_BUFFERS        hclk    hrst_n
 */
union cvmx_sriox_mac_buffers {
	uint64_t u64;
	struct cvmx_sriox_mac_buffers_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t tx_enb                       : 8;  /**< TX Buffer Enable.  Each bit enables a specific TX
                                                         Buffer.  At least 2 of these bits must be set for
                                                         proper operation.  These bits must be cleared to
                                                         and then set again to reuese the buffer after an
                                                         error occurs. */
	uint64_t reserved_44_47               : 4;
	uint64_t tx_inuse                     : 4;  /**< Number of TX buffers containing packets waiting
                                                         to be transmitted or to be acknowledged. */
	uint64_t tx_stat                      : 8;  /**< Errors detected in main SRIO Transmit Buffers.
                                                         CRC error detected in buffer sets bit of buffer \#
                                                         until the corresponding TX_ENB is disabled.  Each
                                                         bit set causes the SRIO(0,2..3)_INT_REG.MAC_BUF
                                                         interrupt. */
	uint64_t reserved_24_31               : 8;
	uint64_t rx_enb                       : 8;  /**< RX Buffer Enable.  Each bit enables a specific RX
                                                         Buffer.  At least 2 of these bits must be set for
                                                         proper operation.  These bits must be cleared to
                                                         and then set again to reuese the buffer after an
                                                         error occurs. */
	uint64_t reserved_12_15               : 4;
	uint64_t rx_inuse                     : 4;  /**< Number of RX buffers containing valid packets
                                                         waiting to be processed by the logical layer. */
	uint64_t rx_stat                      : 8;  /**< Errors detected in main SRIO Receive Buffers.  CRC
                                                         error detected in buffer sets bit of buffer \#
                                                         until the corresponding RX_ENB is disabled.  Each
                                                         bit set causes the SRIO(0,2..3)_INT_REG.MAC_BUF
                                                         interrupt. */
#else
	uint64_t rx_stat                      : 8;
	uint64_t rx_inuse                     : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t rx_enb                       : 8;
	uint64_t reserved_24_31               : 8;
	uint64_t tx_stat                      : 8;
	uint64_t tx_inuse                     : 4;
	uint64_t reserved_44_47               : 4;
	uint64_t tx_enb                       : 8;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_sriox_mac_buffers_s       cn63xx;
	struct cvmx_sriox_mac_buffers_s       cn66xx;
};
typedef union cvmx_sriox_mac_buffers cvmx_sriox_mac_buffers_t;

/**
 * cvmx_srio#_maint_op
 *
 * SRIO_MAINT_OP = SRIO Maintenance Operation
 *
 * Allows access to maintenance registers.
 *
 * Notes:
 * This register allows write access to the local SRIOMAINT registers.  A write to this register
 *  posts a read or write operation selected by the OP bit to the local SRIOMAINT register selected by
 *  ADDR.  This write also sets the PENDING bit.  The PENDING bit is cleared by hardware when the
 *  operation is complete.  The MAINT_OP Interrupt is also set as the PENDING bit is cleared.  While
 *  this bit is set, additional writes to this register stall the RSL.  The FAIL bit is set with the
 *  clearing of the PENDING bit when an illegal address is selected. WR_DATA is used only during write
 *  operations.  Only 32-bit Maintenance Operations are supported.
 *
 * Clk_Rst:        SRIO(0,2..3)_MAINT_OP   hclk    hrst_n
 */
union cvmx_sriox_maint_op {
	uint64_t u64;
	struct cvmx_sriox_maint_op_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wr_data                      : 32; /**< Write Data[31:0]. */
	uint64_t reserved_27_31               : 5;
	uint64_t fail                         : 1;  /**< Maintenance Operation Address Error */
	uint64_t pending                      : 1;  /**< Maintenance Operation Pending */
	uint64_t op                           : 1;  /**< Operation. 0=Read, 1=Write */
	uint64_t addr                         : 24; /**< Address. Addr[1:0] are ignored. */
#else
	uint64_t addr                         : 24;
	uint64_t op                           : 1;
	uint64_t pending                      : 1;
	uint64_t fail                         : 1;
	uint64_t reserved_27_31               : 5;
	uint64_t wr_data                      : 32;
#endif
	} s;
	struct cvmx_sriox_maint_op_s          cn63xx;
	struct cvmx_sriox_maint_op_s          cn63xxp1;
	struct cvmx_sriox_maint_op_s          cn66xx;
};
typedef union cvmx_sriox_maint_op cvmx_sriox_maint_op_t;

/**
 * cvmx_srio#_maint_rd_data
 *
 * SRIO_MAINT_RD_DATA = SRIO Maintenance Read Data
 *
 * Allows read access of maintenance registers.
 *
 * Notes:
 * This register allows read access of the local SRIOMAINT registers.  A write to the SRIO(0,2..3)_MAINT_OP
 *  register with the OP bit set to zero initiates a read request and clears the VALID bit.  The
 *  resulting read is returned here and the VALID bit is set.  Access to the register will not stall
 *  the RSL but the VALID bit should be read.
 *
 * Clk_Rst:        SRIO(0,2..3)_MAINT_RD_DATA      hclk    hrst_n
 */
union cvmx_sriox_maint_rd_data {
	uint64_t u64;
	struct cvmx_sriox_maint_rd_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t valid                        : 1;  /**< Read Data Valid. */
	uint64_t rd_data                      : 32; /**< Read Data[31:0]. */
#else
	uint64_t rd_data                      : 32;
	uint64_t valid                        : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_sriox_maint_rd_data_s     cn63xx;
	struct cvmx_sriox_maint_rd_data_s     cn63xxp1;
	struct cvmx_sriox_maint_rd_data_s     cn66xx;
};
typedef union cvmx_sriox_maint_rd_data cvmx_sriox_maint_rd_data_t;

/**
 * cvmx_srio#_mce_tx_ctl
 *
 * SRIO_MCE_TX_CTL = SRIO Multicast Event Transmit Control
 *
 * Multicast Event TX Control
 *
 * Notes:
 * Writes to this register cause the SRIO device to generate a Multicast Event.  Setting the MCE bit
 *  requests the logic to generate the Multicast Event Symbol.  Reading the MCS bit shows the status
 *  of the transmit event.  The hardware will clear the bit when the event has been transmitted and
 *  set the MCS_TX Interrupt.
 *
 * Clk_Rst:        SRIO(0,2..3)_MCE_TX_CTL hclk    hrst_n
 */
union cvmx_sriox_mce_tx_ctl {
	uint64_t u64;
	struct cvmx_sriox_mce_tx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t mce                          : 1;  /**< Multicast Event Transmit. */
#else
	uint64_t mce                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_sriox_mce_tx_ctl_s        cn63xx;
	struct cvmx_sriox_mce_tx_ctl_s        cn63xxp1;
	struct cvmx_sriox_mce_tx_ctl_s        cn66xx;
};
typedef union cvmx_sriox_mce_tx_ctl cvmx_sriox_mce_tx_ctl_t;

/**
 * cvmx_srio#_mem_op_ctrl
 *
 * SRIO_MEM_OP_CTRL = SRIO Memory Operation Control
 *
 * The SRIO Memory Operation Control
 *
 * Notes:
 * This register is used to control memory operations.  Bits are provided to override the priority of
 *  the outgoing responses to memory operations.  The memory operations with responses include NREAD,
 *  NWRITE_R, ATOMIC_INC, ATOMIC_DEC, ATOMIC_SET and ATOMIC_CLR.
 *
 * Clk_Rst:        SRIO(0,2..3)_MEM_OP_CTRL        hclk    hrst_n
 */
union cvmx_sriox_mem_op_ctrl {
	uint64_t u64;
	struct cvmx_sriox_mem_op_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t rr_ro                        : 1;  /**< Read Response Relaxed Ordering.  Controls ordering
                                                         rules for incoming memory operations
                                                          0 = Normal Ordering
                                                          1 = Relaxed Ordering */
	uint64_t w_ro                         : 1;  /**< Write Relaxed Ordering.  Controls ordering rules
                                                         for incoming memory operations
                                                          0 = Normal Ordering
                                                          1 = Relaxed Ordering */
	uint64_t reserved_6_7                 : 2;
	uint64_t rp1_sid                      : 1;  /**< Sets response priority for incomimg memory ops
                                                         of priority 1 on the secondary ID (0=2, 1=3) */
	uint64_t rp0_sid                      : 2;  /**< Sets response priority for incomimg memory ops
                                                         of priority 0 on the secondary ID (0,1=1 2=2, 3=3) */
	uint64_t rp1_pid                      : 1;  /**< Sets response priority for incomimg memory ops
                                                         of priority 1 on the primary ID (0=2, 1=3) */
	uint64_t rp0_pid                      : 2;  /**< Sets response priority for incomimg memory ops
                                                         of priority 0 on the primary ID (0,1=1 2=2, 3=3) */
#else
	uint64_t rp0_pid                      : 2;
	uint64_t rp1_pid                      : 1;
	uint64_t rp0_sid                      : 2;
	uint64_t rp1_sid                      : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t w_ro                         : 1;
	uint64_t rr_ro                        : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_sriox_mem_op_ctrl_s       cn63xx;
	struct cvmx_sriox_mem_op_ctrl_s       cn63xxp1;
	struct cvmx_sriox_mem_op_ctrl_s       cn66xx;
};
typedef union cvmx_sriox_mem_op_ctrl cvmx_sriox_mem_op_ctrl_t;

/**
 * cvmx_srio#_omsg_ctrl#
 *
 * SRIO_OMSG_CTRLX = SRIO Outbound Message Control
 *
 * The SRIO Controller X Outbound Message Control Register
 *
 * Notes:
 * 1) If IDM_TT, IDM_SIS, and IDM_DID are all clear, then the "ID match" will always be false.
 * 2) LTTR_SP and LTTR_MP must be non-zero at all times, otherwise the message output queue can
 *        get blocked
 * 3) TESTMODE has no function on controller 1
 * 4) When IDM_TT=0, it is possible for an ID match to match an 8-bit DID with a 16-bit DID - SRIO
 *        zero-extends all 8-bit DID's, and the DID comparisons are always 16-bits.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_CTRL[0:1]     hclk    hrst_n
 */
union cvmx_sriox_omsg_ctrlx {
	uint64_t u64;
	struct cvmx_sriox_omsg_ctrlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t testmode                     : 1;  /**< Controller X test mode (keep as RSVD in HRM) */
	uint64_t reserved_37_62               : 26;
	uint64_t silo_max                     : 5;  /**< Sets max number outgoing segments for controller X
                                                         Valid range is 0x01 .. 0x10  Note that lower
                                                         values will reduce bandwidth. */
	uint64_t rtry_thr                     : 16; /**< Controller X Retry threshold */
	uint64_t rtry_en                      : 1;  /**< Controller X Retry threshold enable */
	uint64_t reserved_11_14               : 4;
	uint64_t idm_tt                       : 1;  /**< Controller X ID match includes TT ID */
	uint64_t idm_sis                      : 1;  /**< Controller X ID match includes SIS */
	uint64_t idm_did                      : 1;  /**< Controller X ID match includes DID */
	uint64_t lttr_sp                      : 4;  /**< Controller X SP allowable letters in dynamic
                                                         letter select mode (LNS) */
	uint64_t lttr_mp                      : 4;  /**< Controller X MP allowable letters in dynamic
                                                         letter select mode (LNS) */
#else
	uint64_t lttr_mp                      : 4;
	uint64_t lttr_sp                      : 4;
	uint64_t idm_did                      : 1;
	uint64_t idm_sis                      : 1;
	uint64_t idm_tt                       : 1;
	uint64_t reserved_11_14               : 4;
	uint64_t rtry_en                      : 1;
	uint64_t rtry_thr                     : 16;
	uint64_t silo_max                     : 5;
	uint64_t reserved_37_62               : 26;
	uint64_t testmode                     : 1;
#endif
	} s;
	struct cvmx_sriox_omsg_ctrlx_s        cn63xx;
	struct cvmx_sriox_omsg_ctrlx_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t testmode                     : 1;  /**< Controller X test mode (keep as RSVD in HRM) */
	uint64_t reserved_32_62               : 31;
	uint64_t rtry_thr                     : 16; /**< Controller X Retry threshold */
	uint64_t rtry_en                      : 1;  /**< Controller X Retry threshold enable */
	uint64_t reserved_11_14               : 4;
	uint64_t idm_tt                       : 1;  /**< Controller X ID match includes TT ID */
	uint64_t idm_sis                      : 1;  /**< Controller X ID match includes SIS */
	uint64_t idm_did                      : 1;  /**< Controller X ID match includes DID */
	uint64_t lttr_sp                      : 4;  /**< Controller X SP allowable letters in dynamic
                                                         letter select mode (LNS) */
	uint64_t lttr_mp                      : 4;  /**< Controller X MP allowable letters in dynamic
                                                         letter select mode (LNS) */
#else
	uint64_t lttr_mp                      : 4;
	uint64_t lttr_sp                      : 4;
	uint64_t idm_did                      : 1;
	uint64_t idm_sis                      : 1;
	uint64_t idm_tt                       : 1;
	uint64_t reserved_11_14               : 4;
	uint64_t rtry_en                      : 1;
	uint64_t rtry_thr                     : 16;
	uint64_t reserved_32_62               : 31;
	uint64_t testmode                     : 1;
#endif
	} cn63xxp1;
	struct cvmx_sriox_omsg_ctrlx_s        cn66xx;
};
typedef union cvmx_sriox_omsg_ctrlx cvmx_sriox_omsg_ctrlx_t;

/**
 * cvmx_srio#_omsg_done_counts#
 *
 * SRIO_OMSG_DONE_COUNTSX = SRIO Outbound Message Complete Counts
 *
 * The SRIO Controller X Outbound Message Complete Counts Register
 *
 * Notes:
 * This register shows the number of successful and unsuccessful Outgoing Messages issued through
 *  this controller.  The only messages considered are the ones with the INT field set in the PKO
 *  message header.  This register is typically not written while Outbound SRIO Memory traffic is
 *  enabled.  The sum of the GOOD and BAD counts should equal the number of messages sent unless
 *  the MAC has been reset.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_DONE_COUNTS[0:1]      hclk    hrst_n
 */
union cvmx_sriox_omsg_done_countsx {
	uint64_t u64;
	struct cvmx_sriox_omsg_done_countsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t bad                          : 16; /**< Number of Outbound Messages requesting an INT that
                                                         did not increment GOOD. (One or more segment of the
                                                         message either timed out, reached the retry limit,
                                                         or received an ERROR response.) */
	uint64_t good                         : 16; /**< Number of Outbound Messages requesting an INT that
                                                         received a DONE response for every segment. */
#else
	uint64_t good                         : 16;
	uint64_t bad                          : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_omsg_done_countsx_s cn63xx;
	struct cvmx_sriox_omsg_done_countsx_s cn66xx;
};
typedef union cvmx_sriox_omsg_done_countsx cvmx_sriox_omsg_done_countsx_t;

/**
 * cvmx_srio#_omsg_fmp_mr#
 *
 * SRIO_OMSG_FMP_MRX = SRIO Outbound Message FIRSTMP Message Restriction
 *
 * The SRIO Controller X Outbound Message FIRSTMP Message Restriction Register
 *
 * Notes:
 * This CSR controls when FMP candidate message segments (from the two different controllers) can enter
 * the message segment silo to be sent out. A segment remains in the silo until after is has
 * been transmitted and either acknowledged or errored out.
 *
 * Candidates and silo entries are one of 4 types:
 *  SP  - a single-segment message
 *  FMP - the first segment of a multi-segment message
 *  NMP - the other segments in a multi-segment message
 *  PSD - the silo psuedo-entry that is valid only while a controller is in the middle of pushing
 *        a multi-segment message into the silo and can match against segments generated by
 *        the other controller
 *
 * When a candidate "matches" against a silo entry or pseudo entry, it cannot enter the silo.
 * By default (i.e. zeroes in this CSR), the FMP candidate matches against all entries in the
 * silo. When fields in this CSR are set, FMP candidate segments will match fewer silo entries and
 * can enter the silo more freely, probably providing better performance.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_FMP_MR[0:1]   hclk    hrst_n
 */
union cvmx_sriox_omsg_fmp_mrx {
	uint64_t u64;
	struct cvmx_sriox_omsg_fmp_mrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t ctlr_sp                      : 1;  /**< Controller X FIRSTMP enable controller SP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed SP segments that were created
                                                         by the same controller. When clear, this FMP-SP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t ctlr_fmp                     : 1;  /**< Controller X FIRSTMP enable controller FIRSTMP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed FMP segments that were created
                                                         by the same controller. When clear, this FMP-FMP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t ctlr_nmp                     : 1;  /**< Controller X FIRSTMP enable controller NFIRSTMP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed NMP segments that were created
                                                         by the same controller. When clear, this FMP-NMP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t id_sp                        : 1;  /**< Controller X FIRSTMP enable ID SP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed SP segments that "ID match" the
                                                         candidate. When clear, this FMP-SP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t id_fmp                       : 1;  /**< Controller X FIRSTMP enable ID FIRSTMP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed FMP segments that "ID match" the
                                                         candidate. When clear, this FMP-FMP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t id_nmp                       : 1;  /**< Controller X FIRSTMP enable ID NFIRSTMP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed NMP segments that "ID match" the
                                                         candidate. When clear, this FMP-NMP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t id_psd                       : 1;  /**< Controller X FIRSTMP enable ID PSEUDO
                                                         When set, the FMP candidate message segment can
                                                         only match the silo pseudo (for the other
                                                         controller) when it is an "ID match". When clear,
                                                         this FMP-PSD match can occur with any ID values.
                                                         Not used by the hardware when ALL_PSD is set. */
	uint64_t mbox_sp                      : 1;  /**< Controller X FIRSTMP enable MBOX SP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed SP segments with the same 2-bit
                                                         mbox value as the candidate. When clear, this
                                                         FMP-SP match can occur with any mbox values.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t mbox_fmp                     : 1;  /**< Controller X FIRSTMP enable MBOX FIRSTMP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed FMP segments with the same 2-bit
                                                         mbox value as the candidate. When clear, this
                                                         FMP-FMP match can occur with any mbox values.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t mbox_nmp                     : 1;  /**< Controller X FIRSTMP enable MBOX NFIRSTMP
                                                         When set, the FMP candidate message segment can
                                                         only match siloed NMP segments with the same 2-bit
                                                         mbox value as the candidate. When clear, this
                                                         FMP-NMP match can occur with any mbox values.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t mbox_psd                     : 1;  /**< Controller X FIRSTMP enable MBOX PSEUDO
                                                         When set, the FMP candidate message segment can
                                                         only match the silo pseudo (for the other
                                                         controller) if the pseudo has the same 2-bit mbox
                                                         value as the candidate. When clear, this FMP-PSD
                                                         match can occur with any mbox values.
                                                         Not used by the hardware when ALL_PSD is set. */
	uint64_t all_sp                       : 1;  /**< Controller X FIRSTMP enable all SP
                                                         When set, no FMP candidate message segments ever
                                                         match siloed SP segments and ID_SP
                                                         and MBOX_SP are not used. When clear, FMP-SP
                                                         matches can occur. */
	uint64_t all_fmp                      : 1;  /**< Controller X FIRSTMP enable all FIRSTMP
                                                         When set, no FMP candidate message segments ever
                                                         match siloed FMP segments and ID_FMP and MBOX_FMP
                                                         are not used. When clear, FMP-FMP matches can
                                                         occur. */
	uint64_t all_nmp                      : 1;  /**< Controller X FIRSTMP enable all NFIRSTMP
                                                         When set, no FMP candidate message segments ever
                                                         match siloed NMP segments and ID_NMP and MBOX_NMP
                                                         are not used. When clear, FMP-NMP matches can
                                                         occur. */
	uint64_t all_psd                      : 1;  /**< Controller X FIRSTMP enable all PSEUDO
                                                         When set, no FMP candidate message segments ever
                                                         match the silo pseudo (for the other controller)
                                                         and ID_PSD and MBOX_PSD are not used. When clear,
                                                         FMP-PSD matches can occur. */
#else
	uint64_t all_psd                      : 1;
	uint64_t all_nmp                      : 1;
	uint64_t all_fmp                      : 1;
	uint64_t all_sp                       : 1;
	uint64_t mbox_psd                     : 1;
	uint64_t mbox_nmp                     : 1;
	uint64_t mbox_fmp                     : 1;
	uint64_t mbox_sp                      : 1;
	uint64_t id_psd                       : 1;
	uint64_t id_nmp                       : 1;
	uint64_t id_fmp                       : 1;
	uint64_t id_sp                        : 1;
	uint64_t ctlr_nmp                     : 1;
	uint64_t ctlr_fmp                     : 1;
	uint64_t ctlr_sp                      : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} s;
	struct cvmx_sriox_omsg_fmp_mrx_s      cn63xx;
	struct cvmx_sriox_omsg_fmp_mrx_s      cn63xxp1;
	struct cvmx_sriox_omsg_fmp_mrx_s      cn66xx;
};
typedef union cvmx_sriox_omsg_fmp_mrx cvmx_sriox_omsg_fmp_mrx_t;

/**
 * cvmx_srio#_omsg_nmp_mr#
 *
 * SRIO_OMSG_NMP_MRX = SRIO Outbound Message NFIRSTMP Message Restriction
 *
 * The SRIO Controller X Outbound Message NFIRSTMP Message Restriction Register
 *
 * Notes:
 * This CSR controls when NMP candidate message segments (from the two different controllers) can enter
 * the message segment silo to be sent out. A segment remains in the silo until after is has
 * been transmitted and either acknowledged or errored out.
 *
 * Candidates and silo entries are one of 4 types:
 *  SP  - a single-segment message
 *  FMP - the first segment of a multi-segment message
 *  NMP - the other segments in a multi-segment message
 *  PSD - the silo psuedo-entry that is valid only while a controller is in the middle of pushing
 *        a multi-segment message into the silo and can match against segments generated by
 *        the other controller
 *
 * When a candidate "matches" against a silo entry or pseudo entry, it cannot enter the silo.
 * By default (i.e. zeroes in this CSR), the NMP candidate matches against all entries in the
 * silo. When fields in this CSR are set, NMP candidate segments will match fewer silo entries and
 * can enter the silo more freely, probably providing better performance.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_NMP_MR[0:1]   hclk    hrst_n
 */
union cvmx_sriox_omsg_nmp_mrx {
	uint64_t u64;
	struct cvmx_sriox_omsg_nmp_mrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t ctlr_sp                      : 1;  /**< Controller X NFIRSTMP enable controller SP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed SP segments that were created
                                                         by the same controller. When clear, this NMP-SP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t ctlr_fmp                     : 1;  /**< Controller X NFIRSTMP enable controller FIRSTMP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed FMP segments that were created
                                                         by the same controller. When clear, this NMP-FMP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t ctlr_nmp                     : 1;  /**< Controller X NFIRSTMP enable controller NFIRSTMP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed NMP segments that were created
                                                         by the same controller. When clear, this NMP-NMP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t id_sp                        : 1;  /**< Controller X NFIRSTMP enable ID SP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed SP segments that "ID match" the
                                                         candidate. When clear, this NMP-SP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t id_fmp                       : 1;  /**< Controller X NFIRSTMP enable ID FIRSTMP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed FMP segments that "ID match" the
                                                         candidate. When clear, this NMP-FMP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t id_nmp                       : 1;  /**< Controller X NFIRSTMP enable ID NFIRSTMP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed NMP segments that "ID match" the
                                                         candidate. When clear, this NMP-NMP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t reserved_8_8                 : 1;
	uint64_t mbox_sp                      : 1;  /**< Controller X NFIRSTMP enable MBOX SP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed SP segments with the same 2-bit
                                                         mbox  value as the candidate. When clear, this
                                                         NMP-SP match can occur with any mbox values.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t mbox_fmp                     : 1;  /**< Controller X NFIRSTMP enable MBOX FIRSTMP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed FMP segments with the same 2-bit
                                                         mbox value as the candidate. When clear, this
                                                         NMP-FMP match can occur with any mbox values.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t mbox_nmp                     : 1;  /**< Controller X NFIRSTMP enable MBOX NFIRSTMP
                                                         When set, the NMP candidate message segment can
                                                         only match siloed NMP segments with the same 2-bit
                                                         mbox value as the candidate. When clear, this
                                                         NMP-NMP match can occur with any mbox values.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t reserved_4_4                 : 1;
	uint64_t all_sp                       : 1;  /**< Controller X NFIRSTMP enable all SP
                                                         When set, no NMP candidate message segments ever
                                                         match siloed SP segments and ID_SP
                                                         and MBOX_SP are not used. When clear, NMP-SP
                                                         matches can occur. */
	uint64_t all_fmp                      : 1;  /**< Controller X NFIRSTMP enable all FIRSTMP
                                                         When set, no NMP candidate message segments ever
                                                         match siloed FMP segments and ID_FMP and MBOX_FMP
                                                         are not used. When clear, NMP-FMP matches can
                                                         occur. */
	uint64_t all_nmp                      : 1;  /**< Controller X NFIRSTMP enable all NFIRSTMP
                                                         When set, no NMP candidate message segments ever
                                                         match siloed NMP segments and ID_NMP and MBOX_NMP
                                                         are not used. When clear, NMP-NMP matches can
                                                         occur. */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t all_nmp                      : 1;
	uint64_t all_fmp                      : 1;
	uint64_t all_sp                       : 1;
	uint64_t reserved_4_4                 : 1;
	uint64_t mbox_nmp                     : 1;
	uint64_t mbox_fmp                     : 1;
	uint64_t mbox_sp                      : 1;
	uint64_t reserved_8_8                 : 1;
	uint64_t id_nmp                       : 1;
	uint64_t id_fmp                       : 1;
	uint64_t id_sp                        : 1;
	uint64_t ctlr_nmp                     : 1;
	uint64_t ctlr_fmp                     : 1;
	uint64_t ctlr_sp                      : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} s;
	struct cvmx_sriox_omsg_nmp_mrx_s      cn63xx;
	struct cvmx_sriox_omsg_nmp_mrx_s      cn63xxp1;
	struct cvmx_sriox_omsg_nmp_mrx_s      cn66xx;
};
typedef union cvmx_sriox_omsg_nmp_mrx cvmx_sriox_omsg_nmp_mrx_t;

/**
 * cvmx_srio#_omsg_port#
 *
 * SRIO_OMSG_PORTX = SRIO Outbound Message Port
 *
 * The SRIO Controller X Outbound Message Port Register
 *
 * Notes:
 * PORT maps the PKO port to SRIO interface \# / controller X as follows:
 *
 *   000 == PKO port 40
 *   001 == PKO port 41
 *   010 == PKO port 42
 *   011 == PKO port 43
 *   100 == PKO port 44
 *   101 == PKO port 45
 *   110 == PKO port 46
 *   111 == PKO port 47
 *
 *  No two PORT fields among the enabled controllers (ENABLE == 1) may be set to the same value.
 *  The register is only reset during COLD boot.  The register can be accessed/modified regardless of
 *  the value in SRIO(0,2..3)_STATUS_REG.ACCESS.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_PORT[0:1]     sclk    srst_n
 */
union cvmx_sriox_omsg_portx {
	uint64_t u64;
	struct cvmx_sriox_omsg_portx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t enable                       : 1;  /**< Controller X enable */
	uint64_t reserved_3_30                : 28;
	uint64_t port                         : 3;  /**< Controller X PKO port */
#else
	uint64_t port                         : 3;
	uint64_t reserved_3_30                : 28;
	uint64_t enable                       : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_omsg_portx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t enable                       : 1;  /**< Controller X enable */
	uint64_t reserved_2_30                : 29;
	uint64_t port                         : 2;  /**< Controller X PKO port */
#else
	uint64_t port                         : 2;
	uint64_t reserved_2_30                : 29;
	uint64_t enable                       : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} cn63xx;
	struct cvmx_sriox_omsg_portx_cn63xx   cn63xxp1;
	struct cvmx_sriox_omsg_portx_s        cn66xx;
};
typedef union cvmx_sriox_omsg_portx cvmx_sriox_omsg_portx_t;

/**
 * cvmx_srio#_omsg_silo_thr
 *
 * SRIO_OMSG_SILO_THR = SRIO Outgoing Message SILO Thresholds
 *
 * The SRIO Outgoing Message SILO Thresholds
 *
 * Notes:
 * Limits the number of Outgoing Message Segments in flight at a time.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_SILO_THR      hclk    hrst_n
 */
union cvmx_sriox_omsg_silo_thr {
	uint64_t u64;
	struct cvmx_sriox_omsg_silo_thr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t tot_silo                     : 5;  /**< Sets max number segments in flight for all
                                                         controllers.  Valid range is 0x01 .. 0x10 but
                                                         lower values reduce bandwidth. */
#else
	uint64_t tot_silo                     : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_sriox_omsg_silo_thr_s     cn63xx;
	struct cvmx_sriox_omsg_silo_thr_s     cn66xx;
};
typedef union cvmx_sriox_omsg_silo_thr cvmx_sriox_omsg_silo_thr_t;

/**
 * cvmx_srio#_omsg_sp_mr#
 *
 * SRIO_OMSG_SP_MRX = SRIO Outbound Message SP Message Restriction
 *
 * The SRIO Controller X Outbound Message SP Message Restriction Register
 *
 * Notes:
 * This CSR controls when SP candidate message segments (from the two different controllers) can enter
 * the message segment silo to be sent out. A segment remains in the silo until after is has
 * been transmitted and either acknowledged or errored out.
 *
 * Candidates and silo entries are one of 4 types:
 *  SP  - a single-segment message
 *  FMP - the first segment of a multi-segment message
 *  NMP - the other segments in a multi-segment message
 *  PSD - the silo psuedo-entry that is valid only while a controller is in the middle of pushing
 *        a multi-segment message into the silo and can match against segments generated by
 *        the other controller
 *
 * When a candidate "matches" against a silo entry or pseudo entry, it cannot enter the silo.
 * By default (i.e. zeroes in this CSR), the SP candidate matches against all entries in the
 * silo. When fields in this CSR are set, SP candidate segments will match fewer silo entries and
 * can enter the silo more freely, probably providing better performance.
 *
 * Clk_Rst:        SRIO(0,2..3)_OMSG_SP_MR[0:1]    hclk    hrst_n
 */
union cvmx_sriox_omsg_sp_mrx {
	uint64_t u64;
	struct cvmx_sriox_omsg_sp_mrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t xmbox_sp                     : 1;  /**< Controller X SP enable XMBOX SP
                                                         When set, the SP candidate message can only
                                                         match siloed SP segments with the same 4-bit xmbox
                                                         value as the candidate. When clear, this SP-SP
                                                         match can occur with any xmbox values.
                                                         When XMBOX_SP is set, MBOX_SP will commonly be set.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t ctlr_sp                      : 1;  /**< Controller X SP enable controller SP
                                                         When set, the SP candidate message can
                                                         only match siloed SP segments that were created
                                                         by the same controller. When clear, this SP-SP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t ctlr_fmp                     : 1;  /**< Controller X SP enable controller FIRSTMP
                                                         When set, the SP candidate message can
                                                         only match siloed FMP segments that were created
                                                         by the same controller. When clear, this SP-FMP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t ctlr_nmp                     : 1;  /**< Controller X SP enable controller NFIRSTMP
                                                         When set, the SP candidate message can
                                                         only match siloed NMP segments that were created
                                                         by the same controller. When clear, this SP-NMP
                                                         match can also occur when the segments were
                                                         created by the other controller.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t id_sp                        : 1;  /**< Controller X SP enable ID SP
                                                         When set, the SP candidate message can
                                                         only match siloed SP segments that "ID match" the
                                                         candidate. When clear, this SP-SP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t id_fmp                       : 1;  /**< Controller X SP enable ID FIRSTMP
                                                         When set, the SP candidate message can
                                                         only match siloed FMP segments that "ID match" the
                                                         candidate. When clear, this SP-FMP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t id_nmp                       : 1;  /**< Controller X SP enable ID NFIRSTMP
                                                         When set, the SP candidate message can
                                                         only match siloed NMP segments that "ID match" the
                                                         candidate. When clear, this SP-NMP match can occur
                                                         with any ID values.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t id_psd                       : 1;  /**< Controller X SP enable ID PSEUDO
                                                         When set, the SP candidate message can
                                                         only match the silo pseudo (for the other
                                                         controller) when it is an "ID match". When clear,
                                                         this SP-PSD match can occur with any ID values.
                                                         Not used by the hardware when ALL_PSD is set. */
	uint64_t mbox_sp                      : 1;  /**< Controller X SP enable MBOX SP
                                                         When set, the SP candidate message can only
                                                         match siloed SP segments with the same 2-bit mbox
                                                         value as the candidate. When clear, this SP-SP
                                                         match can occur with any mbox values.
                                                         Not used by the hardware when ALL_SP is set. */
	uint64_t mbox_fmp                     : 1;  /**< Controller X SP enable MBOX FIRSTMP
                                                         When set, the SP candidate message can only
                                                         match siloed FMP segments with the same 2-bit mbox
                                                         value as the candidate. When clear, this SP-FMP
                                                         match can occur with any mbox values.
                                                         Not used by the hardware when ALL_FMP is set. */
	uint64_t mbox_nmp                     : 1;  /**< Controller X SP enable MBOX NFIRSTMP
                                                         When set, the SP candidate message can only
                                                         match siloed NMP segments with the same 2-bit mbox
                                                         value as the candidate. When clear, this SP-NMP
                                                         match can occur with any mbox values.
                                                         Not used by the hardware when ALL_NMP is set. */
	uint64_t mbox_psd                     : 1;  /**< Controller X SP enable MBOX PSEUDO
                                                         When set, the SP candidate message can only
                                                         match the silo pseudo (for the other controller)
                                                         if the pseudo has the same 2-bit mbox value as the
                                                         candidate. When clear, this SP-PSD match can occur
                                                         with any mbox values.
                                                         Not used by the hardware when ALL_PSD is set. */
	uint64_t all_sp                       : 1;  /**< Controller X SP enable all SP
                                                         When set, no SP candidate messages ever
                                                         match siloed SP segments, and XMBOX_SP, ID_SP,
                                                         and MBOX_SP are not used. When clear, SP-SP
                                                         matches can occur. */
	uint64_t all_fmp                      : 1;  /**< Controller X SP enable all FIRSTMP
                                                         When set, no SP candidate messages ever
                                                         match siloed FMP segments and ID_FMP and MBOX_FMP
                                                         are not used. When clear, SP-FMP matches can
                                                         occur. */
	uint64_t all_nmp                      : 1;  /**< Controller X SP enable all NFIRSTMP
                                                         When set, no SP candidate messages ever
                                                         match siloed NMP segments and ID_NMP and MBOX_NMP
                                                         are not used. When clear, SP-NMP matches can
                                                         occur. */
	uint64_t all_psd                      : 1;  /**< Controller X SP enable all PSEUDO
                                                         When set, no SP candidate messages ever
                                                         match the silo pseudo (for the other controller)
                                                         and ID_PSD and MBOX_PSD are not used. When clear,
                                                         SP-PSD matches can occur. */
#else
	uint64_t all_psd                      : 1;
	uint64_t all_nmp                      : 1;
	uint64_t all_fmp                      : 1;
	uint64_t all_sp                       : 1;
	uint64_t mbox_psd                     : 1;
	uint64_t mbox_nmp                     : 1;
	uint64_t mbox_fmp                     : 1;
	uint64_t mbox_sp                      : 1;
	uint64_t id_psd                       : 1;
	uint64_t id_nmp                       : 1;
	uint64_t id_fmp                       : 1;
	uint64_t id_sp                        : 1;
	uint64_t ctlr_nmp                     : 1;
	uint64_t ctlr_fmp                     : 1;
	uint64_t ctlr_sp                      : 1;
	uint64_t xmbox_sp                     : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_sriox_omsg_sp_mrx_s       cn63xx;
	struct cvmx_sriox_omsg_sp_mrx_s       cn63xxp1;
	struct cvmx_sriox_omsg_sp_mrx_s       cn66xx;
};
typedef union cvmx_sriox_omsg_sp_mrx cvmx_sriox_omsg_sp_mrx_t;

/**
 * cvmx_srio#_prio#_in_use
 *
 * SRIO_PRIO[0:3]_IN_USE = S2M PRIORITY FIFO IN USE COUNTS
 *
 * SRIO S2M Priority X FIFO Inuse counts
 *
 * Notes:
 * These registers provide status information on the number of read/write requests pending in the S2M
 *  Priority FIFOs.  The information can be used to help determine when an S2M_TYPE register can be
 *  reallocated.  For example, if an S2M_TYPE is used N times in a DMA write operation and the DMA has
 *  completed.  The register corresponding to the RD/WR_PRIOR of the S2M_TYPE can be read to determine
 *  the START_CNT and then can be polled to see if the END_CNT equals the START_CNT or at least
 *  START_CNT+N.   These registers can be accessed regardless of the value of SRIO(0,2..3)_STATUS_REG.ACCESS
 *  but are reset by either the MAC or Core being reset.
 *
 * Clk_Rst:        SRIO(0,2..3)_PRIO[0:3]_IN_USE   sclk    srst_n, hrst_n
 */
union cvmx_sriox_priox_in_use {
	uint64_t u64;
	struct cvmx_sriox_priox_in_use_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t end_cnt                      : 16; /**< Count of Packets with S2M_TYPES completed for this
                                                         Priority X FIFO */
	uint64_t start_cnt                    : 16; /**< Count of Packets with S2M_TYPES started for this
                                                         Priority X FIFO */
#else
	uint64_t start_cnt                    : 16;
	uint64_t end_cnt                      : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_priox_in_use_s      cn63xx;
	struct cvmx_sriox_priox_in_use_s      cn66xx;
};
typedef union cvmx_sriox_priox_in_use cvmx_sriox_priox_in_use_t;

/**
 * cvmx_srio#_rx_bell
 *
 * SRIO_RX_BELL = SRIO Receive Doorbell
 *
 * The SRIO Incoming (RX) Doorbell
 *
 * Notes:
 * This register contains the SRIO Information, Device ID, Transaction Type and Priority of the
 *  incoming Doorbell Transaction as well as the number of transactions waiting to be read.  Reading
 *  this register causes a Doorbell to be removed from the RX Bell FIFO and the COUNT to be
 *  decremented.  If the COUNT is zero then the FIFO is empty and the other fields should be
 *  considered invalid.  When the FIFO is full an ERROR is automatically issued.  The RXBELL Interrupt
 *  can be used to detect posts to this FIFO.
 *
 * Clk_Rst:        SRIO(0,2..3)_RX_BELL    hclk    hrst_n
 */
union cvmx_sriox_rx_bell {
	uint64_t u64;
	struct cvmx_sriox_rx_bell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t data                         : 16; /**< Information field from received doorbell */
	uint64_t src_id                       : 16; /**< Doorbell Source Device ID[15:0] */
	uint64_t count                        : 8;  /**< RX Bell FIFO Count
                                                         Note:  Count must be > 0 for entry to be valid. */
	uint64_t reserved_5_7                 : 3;
	uint64_t dest_id                      : 1;  /**< Destination Device ID 0=Primary, 1=Secondary */
	uint64_t id16                         : 1;  /**< Transaction Type, 0=use ID[7:0], 1=use ID[15:0] */
	uint64_t reserved_2_2                 : 1;
	uint64_t priority                     : 2;  /**< Doorbell Priority */
#else
	uint64_t priority                     : 2;
	uint64_t reserved_2_2                 : 1;
	uint64_t id16                         : 1;
	uint64_t dest_id                      : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t count                        : 8;
	uint64_t src_id                       : 16;
	uint64_t data                         : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_sriox_rx_bell_s           cn63xx;
	struct cvmx_sriox_rx_bell_s           cn63xxp1;
	struct cvmx_sriox_rx_bell_s           cn66xx;
};
typedef union cvmx_sriox_rx_bell cvmx_sriox_rx_bell_t;

/**
 * cvmx_srio#_rx_bell_seq
 *
 * SRIO_RX_BELL_SEQ = SRIO Receive Doorbell Sequence Count
 *
 * The SRIO Incoming (RX) Doorbell Sequence Count
 *
 * Notes:
 * This register contains the value of the sequence counter when the doorbell was received and a
 *  shadow copy of the Bell FIFO Count that can be read without emptying the FIFO.  This register must
 *  be read prior to SRIO(0,2..3)_RX_BELL to guarantee that the information corresponds to the correct
 *  doorbell.
 *
 * Clk_Rst:        SRIO(0,2..3)_RX_BELL_SEQ        hclk    hrst_n
 */
union cvmx_sriox_rx_bell_seq {
	uint64_t u64;
	struct cvmx_sriox_rx_bell_seq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t count                        : 8;  /**< RX Bell FIFO Count
                                                         Note:  Count must be > 0 for entry to be valid. */
	uint64_t seq                          : 32; /**< 32-bit Sequence \# associated with Doorbell Message */
#else
	uint64_t seq                          : 32;
	uint64_t count                        : 8;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_sriox_rx_bell_seq_s       cn63xx;
	struct cvmx_sriox_rx_bell_seq_s       cn63xxp1;
	struct cvmx_sriox_rx_bell_seq_s       cn66xx;
};
typedef union cvmx_sriox_rx_bell_seq cvmx_sriox_rx_bell_seq_t;

/**
 * cvmx_srio#_rx_status
 *
 * SRIO_RX_STATUS = SRIO Inbound Credits/Response Status
 *
 * Specifies the current number of credits/responses by SRIO for Inbound Traffic
 *
 * Notes:
 * Debug Register specifying the number of credits/responses currently in use for Inbound Traffic.
 *  The maximum value for COMP, N_POST and POST is set in SRIO(0,2..3)_TLP_CREDITS.  When all inbound traffic
 *  has stopped the values should eventually return to the maximum values.  The RTN_PR[3:1] entry
 *  counts should eventually return to the reset values.
 *
 * Clk_Rst:        SRIO(0,2..3)_RX_STATUS  hclk    hrst_n
 */
union cvmx_sriox_rx_status {
	uint64_t u64;
	struct cvmx_sriox_rx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rtn_pr3                      : 8;  /**< Number of pending Priority 3 Response Entries. */
	uint64_t rtn_pr2                      : 8;  /**< Number of pending Priority 2 Response Entries. */
	uint64_t rtn_pr1                      : 8;  /**< Number of pending Priority 1 Response Entries. */
	uint64_t reserved_28_39               : 12;
	uint64_t mbox                         : 4;  /**< Credits for Mailbox Data used in M2S. */
	uint64_t comp                         : 8;  /**< Credits for Read Completions used in M2S. */
	uint64_t reserved_13_15               : 3;
	uint64_t n_post                       : 5;  /**< Credits for Read Requests used in M2S. */
	uint64_t post                         : 8;  /**< Credits for Write Request Postings used in M2S. */
#else
	uint64_t post                         : 8;
	uint64_t n_post                       : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t comp                         : 8;
	uint64_t mbox                         : 4;
	uint64_t reserved_28_39               : 12;
	uint64_t rtn_pr1                      : 8;
	uint64_t rtn_pr2                      : 8;
	uint64_t rtn_pr3                      : 8;
#endif
	} s;
	struct cvmx_sriox_rx_status_s         cn63xx;
	struct cvmx_sriox_rx_status_s         cn63xxp1;
	struct cvmx_sriox_rx_status_s         cn66xx;
};
typedef union cvmx_sriox_rx_status cvmx_sriox_rx_status_t;

/**
 * cvmx_srio#_s2m_type#
 *
 * SRIO_S2M_TYPE[0:15] = SLI to SRIO MAC Operation Type
 *
 * SRIO Operation Type selected by PP or DMA Accesses
 *
 * Notes:
 * This CSR table specifies how to convert a SLI/DPI MAC read or write into sRIO operations.
 *  Each SLI/DPI read or write access supplies a 64-bit address (MACADD[63:0]), 2-bit ADDRTYPE, and
 *  2-bit endian-swap. This SRIO*_S2M_TYPE* CSR description specifies a table with 16 CSRs. SRIO
 *  selects one of the table entries with TYPEIDX[3:0], which it creates from the SLI/DPI MAC memory
 *  space read or write as follows:
 *    TYPEIDX[1:0] = ADDRTYPE[1:0] (ADDRTYPE[1] is no-snoop to the PCIe MAC,
 *                                  ADDRTYPE[0] is relaxed-ordering to the PCIe MAC)
 *    TYPEIDX[2] = MACADD[50]
 *    TYPEIDX[3] = MACADD[59]
 *
 * Clk_Rst:        SRIO(0,2..3)_S2M_TYPE[0:15]     hclk    hrst_n
 */
union cvmx_sriox_s2m_typex {
	uint64_t u64;
	struct cvmx_sriox_s2m_typex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t wr_op                        : 3;  /**< sRIO operation for SLI/DPI writes

                                                         SLI/DPI hardware break MAC memory space writes
                                                         that they generate into pieces of maximum size
                                                         256B. For NWRITE/NWRITE_R/SWRITE WR_OP variants
                                                         below, SRIO will, if necessary to obey sRIO
                                                         requirements, automatically break the write into
                                                         even smaller writes. The same is not true for
                                                         MAINTENANCE writes and port-writes. Additional
                                                         SW/usage restrictions are required for these
                                                         MAINTENANCE WR_OP's to work correctly. SW must
                                                         restrict the alignment and length of DPI pointers,
                                                         limit the store sizes that the cores issue, and
                                                         possibly also set SLI_MEM_ACCESS_SUBID*[NMERGE]
                                                         so that all MAC memory space writes with
                                                         MAINTENANCE write and port-write WR_OP's can be
                                                         serviced in a single sRIO operation.

                                                         SRIO always sends the write data (64-bit) words
                                                         out in order.

                                                          WR_OP = 0 = Normal Write (NWRITE)
                                                                 SRIO breaks a MAC memory space write into
                                                                 the minimum number of required sRIO NWRITE
                                                                 operations. This will be 1-5 total NWRITEs,
                                                                 depending on endian-swap, alignment, and
                                                                 length.

                                                          WR_OP = 1 = Normal Write w/Response (NWRITE_R)
                                                                 SRIO breaks a MAC memory space write into
                                                                 the minimum number of required sRIO
                                                                 NWRITE_R operations. This will be 1-5 total
                                                                 NWRITE_R's, depending on endian-swap,
                                                                 alignment, and length.

                                                                 SRIO sets SRIO*_INT_REG[WR_DONE] after it
                                                                 receives the DONE response for the last
                                                                 NWRITE_R sent.

                                                          WR_OP = 2 = NWRITE, Streaming write (SWRITE),
                                                                      NWRITE
                                                                 SRIO attempts to turn the MAC memory space
                                                                 write into an SWRITE operation. There will
                                                                 be 1-5 total sRIO operations (0-2 NWRITE's
                                                                 followed by 0-1 SWRITE's followed by 0-2
                                                                 NWRITE's) generated to complete the MAC
                                                                 memory space write, depending on
                                                                 endian-swap, alignment, and length.

                                                                 If the starting address is not 64-bit
                                                                 aligned, SRIO first creates 1-4 NWRITE's to
                                                                 either align it or complete the write. Then
                                                                 SRIO creates a SWRITE including all aligned
                                                                 64-bit words. (SRIO won't create an SWRITE
                                                                 when there are none.) If store data
                                                                 remains, SRIO finally creates another 1 or
                                                                 2 NWRITE's.

                                                          WR_OP = 3 = NWRITE, SWRITE, NWRITE_R
                                                                 SRIO attempts to turn the MAC memory space
                                                                 write into an SWRITE operation followed by
                                                                 a NWRITE_R operation. The last operation
                                                                 is always NWRITE_R. There will be 1-5
                                                                 total sRIO operations (0-2 NWRITE's,
                                                                 followed by 0-1 SWRITE, followed by 1-4
                                                                 NWRITE_R's) generated to service the MAC
                                                                 memory space write, depending on
                                                                 endian-swap, alignment, and length.

                                                                 If the write is contained in one aligned
                                                                 64-bit word, SRIO will completely service
                                                                 the MAC memory space write with 1-4
                                                                 NWRITE_R's.

                                                                 Otherwise, if the write spans multiple
                                                                 words, SRIO services the write as follows.
                                                                 First, if the start of the write is not
                                                                 word-aligned, SRIO creates 1 or 2 NWRITE's
                                                                 to align it. Then SRIO creates an SWRITE
                                                                 that includes all aligned 64-bit words,
                                                                 leaving data for the final NWRITE_R(s).
                                                                 (SRIO won't create the SWRITE when there is
                                                                 no data for it.) Then SRIO finally creates
                                                                 1 or 2 NWRITE_R's.

                                                                 In any case, SRIO sets
                                                                 SRIO*_INT_REG[WR_DONE] after it receives
                                                                 the DONE response for the last NWRITE_R
                                                                 sent.

                                                          WR_OP = 4 = NWRITE, NWRITE_R
                                                                 SRIO attempts to turn the MAC memory space
                                                                 write into an NWRITE operation followed by
                                                                 a NWRITE_R operation. The last operation
                                                                 is always NWRITE_R. There will be 1-5
                                                                 total sRIO operations (0-3 NWRITE's
                                                                 followed by 1-4 NWRITE_R's) generated to
                                                                 service the MAC memory space write,
                                                                 depending on endian-swap, alignment, and
                                                                 length.

                                                                 If the write is contained in one aligned
                                                                 64-bit word, SRIO will completely service
                                                                 the MAC memory space write with 1-4
                                                                 NWRITE_R's.

                                                                 Otherwise, if the write spans multiple
                                                                 words, SRIO services the write as follows.
                                                                 First, if the start of the write is not
                                                                 word-aligned, SRIO creates 1 or 2 NWRITE's
                                                                 to align it. Then SRIO creates an NWRITE
                                                                 that includes all aligned 64-bit words,
                                                                 leaving data for the final NWRITE_R(s).
                                                                 (SRIO won't create this NWRITE when there
                                                                 is no data for it.) Then SRIO finally
                                                                 creates 1 or 2 NWRITE_R's.

                                                                 In any case, SRIO sets
                                                                 SRIO*_INT_REG[WR_DONE] after it receives
                                                                 the DONE response for the last NWRITE_R
                                                                 sent.

                                                          WR_OP = 5 = Reserved

                                                          WR_OP = 6 = Maintenance Write
                                                               - SRIO will create one sRIO MAINTENANCE write
                                                                 operation to service the MAC memory space
                                                                 write
                                                               - IAOW_SEL must be zero. (see description
                                                                 below.)
                                                               - MDS must be zero. (MDS is MACADD[63:62] -
                                                                 see IAOW_SEL description below.)
                                                               - Hop Cnt is MACADD[31:24]/SRIOAddress[31:24]
                                                               - MACADD[23:0]/SRIOAddress[23:0] selects
                                                                 maintenance register (i.e. config_offset)
                                                               - sRIODestID[15:0] is MACADD[49:34].
                                                                 (MACADD[49:42] unused when ID16=0)
                                                               - Write size/alignment must obey sRIO rules
                                                                 (4, 8, 16, 24, 32, 40, 48, 56 and 64 byte
                                                                 lengths allowed)

                                                          WR_OP = 7 = Maintenance Port Write
                                                               - SRIO will create one sRIO MAINTENANCE port
                                                                 write operation to service the MAC memory
                                                                 space write
                                                               - IAOW_SEL must be zero. (see description
                                                                 below.)
                                                               - MDS must be zero. (MDS is MACADD[63:62] -
                                                                 see IAOW_SEL description below.)
                                                               - Hop Cnt is MACADD[31:24]/sRIOAddress[31:24]
                                                               - MACADD[23:0]/sRIOAddress[23:0] MBZ
                                                                 (config_offset field reserved by sRIO)
                                                               - sRIODestID[15:0] is MACADD[49:34].
                                                                 (MACADD[49:42] unused when ID16=0)
                                                               - Write size/alignment must obey sRIO rules
                                                                 (4, 8, 16, 24, 32, 40, 48, 56 and 64 byte
                                                                 lengths allowed) */
	uint64_t reserved_15_15               : 1;
	uint64_t rd_op                        : 3;  /**< sRIO operation for SLI/DPI reads

                                                         SLI/DPI hardware and sRIO configuration
                                                         restrictions guarantee that SRIO can service any
                                                         MAC memory space read that it receives from SLI/DPI
                                                         with a single NREAD, assuming that RD_OP selects
                                                         NREAD. DPI will break a read into multiple MAC
                                                         memory space reads to ensure this holds. The same
                                                         is not true for the ATOMIC and MAINTENANCE RD_OP
                                                         values. Additional SW/usage restrictions are
                                                         required for ATOMIC and MAINTENANCE RD_OP to work
                                                         correctly. SW must restrict the alignment and
                                                         length of DPI pointers and limit the load sizes
                                                         that the cores issue such that all MAC memory space
                                                         reads with ATOMIC and MAINTENANCE RD_OP's can be
                                                         serviced in a single sRIO operation.

                                                          RD_OP = 0 = Normal Read (NREAD)
                                                               - SRIO will create one sRIO NREAD
                                                                 operation to service the MAC memory
                                                                 space read
                                                               - Read size/alignment must obey sRIO rules
                                                                 (up to 256 byte lengths). (This requirement
                                                                 is guaranteed by SLI/DPI usage restrictions
                                                                 and configuration.)

                                                          RD_OP = 1 = Reserved

                                                          RD_OP = 2 = Atomic Set
                                                               - SRIO will create one sRIO ATOMIC set
                                                                 operation to service the MAC memory
                                                                 space read
                                                               - Read size/alignment must obey sRIO rules
                                                                 (1, 2, and 4 byte lengths allowed)

                                                          RD_OP = 3 = Atomic Clear
                                                               - SRIO will create one sRIO ATOMIC clr
                                                                 operation to service the MAC memory
                                                                 space read
                                                               - Read size/alignment must obey sRIO rules
                                                                 (1, 2, and 4 byte lengths allowed)

                                                          RD_OP = 4 = Atomic Increment
                                                               - SRIO will create one sRIO ATOMIC inc
                                                                 operation to service the MAC memory
                                                                 space read
                                                               - Read size/alignment must obey sRIO rules
                                                                 (1, 2, and 4 byte lengths allowed)

                                                          RD_OP = 5 = Atomic Decrement
                                                               - SRIO will create one sRIO ATOMIC dec
                                                                 operation to service the MAC memory
                                                                 space read
                                                               - Read size/alignment must obey sRIO rules
                                                                 (1, 2, and 4 byte lengths allowed)

                                                          RD_OP = 6 = Maintenance Read
                                                               - SRIO will create one sRIO MAINTENANCE read
                                                                 operation to service the MAC memory
                                                                 space read
                                                               - IAOW_SEL must be zero. (see description
                                                                 below.)
                                                               - MDS must be zero. (MDS is MACADD[63:62] -
                                                                 see IAOW_SEL description below.)
                                                               - Hop Cnt is MACADD[31:24]/sRIOAddress[31:24]
                                                               - MACADD[23:0]/sRIOAddress[23:0] selects
                                                                 maintenance register (i.e. config_offset)
                                                               - sRIODestID[15:0] is MACADD[49:34].
                                                                 (MACADD[49:42] unused when ID16=0)
                                                               - Read size/alignment must obey sRIO rules
                                                                 (4, 8, 16, 32 and 64 byte lengths allowed)

                                                          RD_OP = 7 = Reserved */
	uint64_t wr_prior                     : 2;  /**< Transaction Priority 0-3 used for writes */
	uint64_t rd_prior                     : 2;  /**< Transaction Priority 0-3 used for reads/ATOMICs */
	uint64_t reserved_6_7                 : 2;
	uint64_t src_id                       : 1;  /**< Source ID

                                                         0 = Use Primary ID as Source ID
                                                             (SRIOMAINT*_PRI_DEV_ID[ID16 or ID8], depending
                                                             on SRIO TT ID (i.e. ID16 below))

                                                         1 = Use Secondary ID as Source ID
                                                             (SRIOMAINT*_SEC_DEV_ID[ID16 or ID8], depending
                                                             on SRIO TT ID (i.e. ID16 below)) */
	uint64_t id16                         : 1;  /**< SRIO TT ID 0=8bit, 1=16-bit
                                                         IAOW_SEL must not be 2 when ID16=1. */
	uint64_t reserved_2_3                 : 2;
	uint64_t iaow_sel                     : 2;  /**< Internal Address Offset Width Select

                                                         IAOW_SEL determines how to convert the
                                                         MACADD[63:62,58:51,49:0] recieved from SLI/DPI with
                                                         read/write into an sRIO address (sRIOAddress[...])
                                                         and sRIO destination ID (sRIODestID[...]). The sRIO
                                                         address width mode (SRIOMAINT_PE_LLC[EX_ADDR]) and
                                                         ID16, determine the  width of the sRIO address and
                                                         ID in the outgoing request(s), respectively.

                                                         MACADD[61:60] is always unused.

                                                         MACADD[59] is always TYPEIDX[3]
                                                         MACADD[50] is always TYPEIDX[2]
                                                          (TYPEIDX[3:0] selects one of these
                                                          SRIO*_S2M_TYPE* table entries.)

                                                         MACADD[17:0] always becomes sRIOAddress[17:0].

                                                          IAOW_SEL = 0 = 34-bit Address Offset

                                                              Must be used when sRIO link is in 34-bit
                                                               address width mode.
                                                              When sRIO is in 50-bit address width mode,
                                                               sRIOAddress[49:34]=0 in the outgoing request.
                                                              When sRIO is in 66-bit address width mode,
                                                               sRIOAddress[65:34]=0 in the outgoing request.

                                                              Usage of the SLI/DPI MAC address when
                                                              IAOW_SEL = 0:
                                                               MACADD[63:62] = Multi-Device Swap (MDS)
                                                                 MDS value affects MACADD[49:18] usage
                                                               MACADD[58:51] => unused
                                                               MACADD[49:18] usage depends on MDS value
                                                                MDS = 0
                                                                  MACADD[49:34] => sRIODestID[15:0]
                                                                    (MACADD[49:42] unused when ID16=0)
                                                                  MACADD[33:18] => sRIOAddress[33:18]
                                                                MDS = 1
                                                                  MACADD[49:42] => sRIODestID[15:8]
                                                                    (MACADD[49:42] unused when ID16 = 0)
                                                                  MACADD[41:34] => sRIOAddress[33:26]
                                                                  MACADD[33:26] => sRIODestID[7:0]
                                                                  MACADD[25:18] => sRIOAddress[25:18]
                                                                MDS = 2
                                                                  ID16 must be one.
                                                                  MACADD[49:34] => sRIOAddress[33:18]
                                                                  MACADD[33:18] => sRIODestID[15:0]
                                                                MDS = 3 = Reserved

                                                          IAOW_SEL = 1 = 42-bit Address Offset

                                                              Must not be used when sRIO link is in 34-bit
                                                               address width mode.
                                                              When sRIO is in 50-bit address width mode,
                                                               sRIOAddress[49:42]=0 in the outgoing request.
                                                              When sRIO is in 66-bit address width mode,
                                                               sRIOAddress[65:42]=0 in the outgoing request.

                                                              Usage of the SLI/DPI MAC address when
                                                              IAOW_SEL = 1:
                                                               MACADD[63:62] => Multi-Device Swap (MDS)
                                                                 MDS value affects MACADD[58:51,49:42,33:18]
                                                                   use
                                                               MACADD[41:34] => sRIOAddress[41:34]
                                                               MACADD[58:51,49:42,33:18] usage depends on
                                                               MDS value:
                                                                MDS = 0
                                                                  MACADD[58:51] => sRIODestID[15:8]
                                                                  MACADD[49:42] => sRIODestID[7:0]
                                                                    (MACADD[58:51] unused when ID16=0)
                                                                  MACADD[33:18] => sRIOAddress[33:18]
                                                                MDS = 1
                                                                  MACADD[58:51] => sRIODestID[15:8]
                                                                    (MACADD[58:51] unused when ID16 = 0)
                                                                  MACADD[49:42] => sRIOAddress[33:26]
                                                                  MACADD[33:26] => sRIODestID[7:0]
                                                                  MACADD[25:18] => sRIOAddress[25:18]
                                                                MDS = 2
                                                                  ID16 must be one.
                                                                  MACADD[58:51] => sRIOAddress[33:26]
                                                                  MACADD[49:42] => sRIOAddress[25:18]
                                                                  MACADD[33:18] => sRIODestID[15:0]
                                                                MDS = 3 = Reserved

                                                          IAOW_SEL = 2 = 50-bit Address Offset

                                                              Must not be used when sRIO link is in 34-bit
                                                               address width mode.
                                                              Must not be used when ID16=1.
                                                              When sRIO is in 66-bit address width mode,
                                                               sRIOAddress[65:50]=0 in the outgoing request.

                                                              Usage of the SLI/DPI MAC address when
                                                              IAOW_SEL = 2:
                                                               MACADD[63:62] => Multi-Device Swap (MDS)
                                                                 MDS value affects MACADD[58:51,33:26] use
                                                                 MDS value 3 is reserved
                                                               MACADD[49:34] => sRIOAddress[49:34]
                                                               MACADD[25:18] => sRIOAddress[25:18]
                                                               MACADD[58:51,33:26] usage depends on
                                                               MDS value:
                                                                MDS = 0
                                                                  MACADD[58:51] => sRIODestID[7:0]
                                                                  MACADD[33:26] => sRIOAddress[33:26]
                                                                MDS = 1
                                                                  MACADD[58:51] => sRIOAddress[33:26]
                                                                  MACADD[33:26] => sRIODestID[7:0]
                                                                MDS = 2 = Reserved
                                                                MDS = 3 = Reserved

                                                          IAOW_SEL = 3 = Reserved */
#else
	uint64_t iaow_sel                     : 2;
	uint64_t reserved_2_3                 : 2;
	uint64_t id16                         : 1;
	uint64_t src_id                       : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t rd_prior                     : 2;
	uint64_t wr_prior                     : 2;
	uint64_t rd_op                        : 3;
	uint64_t reserved_15_15               : 1;
	uint64_t wr_op                        : 3;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_sriox_s2m_typex_s         cn63xx;
	struct cvmx_sriox_s2m_typex_s         cn63xxp1;
	struct cvmx_sriox_s2m_typex_s         cn66xx;
};
typedef union cvmx_sriox_s2m_typex cvmx_sriox_s2m_typex_t;

/**
 * cvmx_srio#_seq
 *
 * SRIO_SEQ = SRIO Sequence Count
 *
 * The SRIO Sequence Count
 *
 * Notes:
 * This register contains the current value of the sequence counter.  This counter increments every
 *  time a doorbell or the first segment of a message is accepted.
 *
 * Clk_Rst:        SRIO(0,2..3)_SEQ        hclk    hrst_n
 */
union cvmx_sriox_seq {
	uint64_t u64;
	struct cvmx_sriox_seq_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t seq                          : 32; /**< 32-bit Sequence \# */
#else
	uint64_t seq                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_seq_s               cn63xx;
	struct cvmx_sriox_seq_s               cn63xxp1;
	struct cvmx_sriox_seq_s               cn66xx;
};
typedef union cvmx_sriox_seq cvmx_sriox_seq_t;

/**
 * cvmx_srio#_status_reg
 *
 * 13e20 reserved
 *
 *
 *                  SRIO_STATUS_REG = SRIO Status Register
 *
 * General status of the SRIO.
 *
 * Notes:
 * The SRIO field displays if the port has been configured for SRIO operation.  This register can be
 *  read regardless of whether the SRIO is selected or being reset.  Although some other registers can
 *  be accessed while the ACCESS bit is zero (see individual registers for details), the majority of
 *  SRIO registers and all the SRIOMAINT registers can be used only when the ACCESS bit is asserted.
 *
 * Clk_Rst:        SRIO(0,2..3)_STATUS_REG sclk    srst_n
 */
union cvmx_sriox_status_reg {
	uint64_t u64;
	struct cvmx_sriox_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t access                       : 1;  /**< SRIO and SRIOMAINT Register Access.
                                                         0 - Register Access Disabled.
                                                         1 - Register Access Enabled. */
	uint64_t srio                         : 1;  /**< SRIO Port Enabled.
                                                         0 - All SRIO functions disabled.
                                                         1 - All SRIO Operations permitted. */
#else
	uint64_t srio                         : 1;
	uint64_t access                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_sriox_status_reg_s        cn63xx;
	struct cvmx_sriox_status_reg_s        cn63xxp1;
	struct cvmx_sriox_status_reg_s        cn66xx;
};
typedef union cvmx_sriox_status_reg cvmx_sriox_status_reg_t;

/**
 * cvmx_srio#_tag_ctrl
 *
 * SRIO_TAG_CTRL = SRIO TAG Control
 *
 * The SRIO TAG Control
 *
 * Notes:
 * This register is used to show the state of the internal transaction tags and provides a manual
 *  reset of the outgoing tags.
 *
 * Clk_Rst:        SRIO(0,2..3)_TAG_CTRL   hclk    hrst_n
 */
union cvmx_sriox_tag_ctrl {
	uint64_t u64;
	struct cvmx_sriox_tag_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t o_clr                        : 1;  /**< Manual OTAG Clear.  This bit manually resets the
                                                         number of OTAGs back to 16 and loses track of any
                                                         outgoing packets.  This function is automatically
                                                         performed when the SRIO MAC is reset but it may be
                                                         necessary after a chip reset while the MAC is in
                                                         operation.  This bit must be set then cleared to
                                                         return to normal operation.  Typically, Outgoing
                                                         SRIO packets must be halted 6 seconds prior to
                                                         this bit is set to avoid generating duplicate tags
                                                         and unexpected response errors. */
	uint64_t reserved_13_15               : 3;
	uint64_t otag                         : 5;  /**< Number of Available Outbound Tags.  Tags are
                                                         required for all outgoing memory and maintenance
                                                         operations that require a response. (Max 16) */
	uint64_t reserved_5_7                 : 3;
	uint64_t itag                         : 5;  /**< Number of Available Inbound Tags.  Tags are
                                                         required for all incoming memory operations that
                                                         require a response. (Max 16) */
#else
	uint64_t itag                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t otag                         : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t o_clr                        : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_sriox_tag_ctrl_s          cn63xx;
	struct cvmx_sriox_tag_ctrl_s          cn63xxp1;
	struct cvmx_sriox_tag_ctrl_s          cn66xx;
};
typedef union cvmx_sriox_tag_ctrl cvmx_sriox_tag_ctrl_t;

/**
 * cvmx_srio#_tlp_credits
 *
 * SRIO_TLP_CREDITS = SRIO TLP Credits
 *
 * Specifies the number of credits the SRIO can use for incoming Commands and Messages.
 *
 * Notes:
 * Specifies the number of maximum credits the SRIO can use for incoming Commands and Messages.
 *  Reset values for COMP, N_POST and POST credits are based on the number of lanes allocated by the
 *  QLM Configuration to the SRIO MAC and whether QLM1 is used by PCIe.  If SRIO MACs are unused then
 *  credits may be allocated to other MACs under some circumstances.  The following table shows the
 *  reset values for COMP/N_POST/POST:
 *                     QLM0_CFG    QLM1_CFG    SRIO0       SRIO2      SRIO3
 *                    ======================================================
 *                        PEM        Any       0/0/0       0/0/0      0/0/0
 *                      SRIO x4      Any     128/16/128    0/0/0      0/0/0
 *                      SRIO x2      PEM      64/8/64     64/8/64     0/0/0
 *                      SRIO x2    non-PEM   128/16/128  128/16/128   0/0/0
 *                      SRIO x1      PEM      42/5/42     42/5/42    42/5/42
 *                      SRIO x1    non-PEM    64/8/64     64/8/64    64/8/64
 *
 * Clk_Rst:        SRIO(0,2..3)_TLP_CREDITS        hclk    hrst_n
 */
union cvmx_sriox_tlp_credits {
	uint64_t u64;
	struct cvmx_sriox_tlp_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t mbox                         : 4;  /**< Credits for Mailbox Data used in M2S.
                                                         Legal values are 0x2 to 0x8. */
	uint64_t comp                         : 8;  /**< Credits for Read Completions used in M2S.
                                                         Legal values are 0x22 to 0x80. */
	uint64_t reserved_13_15               : 3;
	uint64_t n_post                       : 5;  /**< Credits for Read Requests used in M2S.
                                                         Legal values are 0x4 to 0x10. */
	uint64_t post                         : 8;  /**< Credits for Write Request Postings used in M2S.
                                                         Legal values are 0x22 to 0x80. */
#else
	uint64_t post                         : 8;
	uint64_t n_post                       : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t comp                         : 8;
	uint64_t mbox                         : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_sriox_tlp_credits_s       cn63xx;
	struct cvmx_sriox_tlp_credits_s       cn63xxp1;
	struct cvmx_sriox_tlp_credits_s       cn66xx;
};
typedef union cvmx_sriox_tlp_credits cvmx_sriox_tlp_credits_t;

/**
 * cvmx_srio#_tx_bell
 *
 * SRIO_TX_BELL = SRIO Transmit Doorbell
 *
 * The SRIO Outgoing (TX) Doorbell
 *
 * Notes:
 * This register specifies SRIO Information, Device ID, Transaction Type and Priority of the outgoing
 *  Doorbell Transaction.  Writes to this register causes the Doorbell to be issued using these bits.
 *  The write also causes the PENDING bit to be set. The hardware automatically clears bit when the
 *  Doorbell operation has been acknowledged.  A write to this register while the PENDING bit is set
 *  should be avoided as it will stall the RSL until the first Doorbell has completed.
 *
 * Clk_Rst:        SRIO(0,2..3)_TX_BELL    hclk    hrst_n
 */
union cvmx_sriox_tx_bell {
	uint64_t u64;
	struct cvmx_sriox_tx_bell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t data                         : 16; /**< Information field for next doorbell operation */
	uint64_t dest_id                      : 16; /**< Doorbell Destination Device ID[15:0] */
	uint64_t reserved_9_15                : 7;
	uint64_t pending                      : 1;  /**< Doorbell Transmit in Progress */
	uint64_t reserved_5_7                 : 3;
	uint64_t src_id                       : 1;  /**< Source Device ID 0=Primary, 1=Secondary */
	uint64_t id16                         : 1;  /**< Transaction Type, 0=use ID[7:0], 1=use ID[15:0] */
	uint64_t reserved_2_2                 : 1;
	uint64_t priority                     : 2;  /**< Doorbell Priority */
#else
	uint64_t priority                     : 2;
	uint64_t reserved_2_2                 : 1;
	uint64_t id16                         : 1;
	uint64_t src_id                       : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t pending                      : 1;
	uint64_t reserved_9_15                : 7;
	uint64_t dest_id                      : 16;
	uint64_t data                         : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_sriox_tx_bell_s           cn63xx;
	struct cvmx_sriox_tx_bell_s           cn63xxp1;
	struct cvmx_sriox_tx_bell_s           cn66xx;
};
typedef union cvmx_sriox_tx_bell cvmx_sriox_tx_bell_t;

/**
 * cvmx_srio#_tx_bell_info
 *
 * SRIO_TX_BELL_INFO = SRIO Transmit Doorbell Interrupt Information
 *
 * The SRIO Outgoing (TX) Doorbell Interrupt Information
 *
 * Notes:
 * This register is only updated if the BELL_ERR bit is clear in SRIO(0,2..3)_INT_REG.  This register
 *  displays SRIO Information, Device ID, Transaction Type and Priority of the Doorbell Transaction
 *  that generated the BELL_ERR Interrupt.  The register includes either a RETRY, ERROR or TIMEOUT
 *  Status.
 *
 * Clk_Rst:        SRIO(0,2..3)_TX_BELL_INFO       hclk    hrst_n
 */
union cvmx_sriox_tx_bell_info {
	uint64_t u64;
	struct cvmx_sriox_tx_bell_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t data                         : 16; /**< Information field from last doorbell operation */
	uint64_t dest_id                      : 16; /**< Doorbell Destination Device ID[15:0] */
	uint64_t reserved_8_15                : 8;
	uint64_t timeout                      : 1;  /**< Transmit Doorbell Failed with Timeout. */
	uint64_t error                        : 1;  /**< Transmit Doorbell Destination returned Error. */
	uint64_t retry                        : 1;  /**< Transmit Doorbell Requests a retransmission. */
	uint64_t src_id                       : 1;  /**< Source Device ID 0=Primary, 1=Secondary */
	uint64_t id16                         : 1;  /**< Transaction Type, 0=use ID[7:0], 1=use ID[15:0] */
	uint64_t reserved_2_2                 : 1;
	uint64_t priority                     : 2;  /**< Doorbell Priority */
#else
	uint64_t priority                     : 2;
	uint64_t reserved_2_2                 : 1;
	uint64_t id16                         : 1;
	uint64_t src_id                       : 1;
	uint64_t retry                        : 1;
	uint64_t error                        : 1;
	uint64_t timeout                      : 1;
	uint64_t reserved_8_15                : 8;
	uint64_t dest_id                      : 16;
	uint64_t data                         : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_sriox_tx_bell_info_s      cn63xx;
	struct cvmx_sriox_tx_bell_info_s      cn63xxp1;
	struct cvmx_sriox_tx_bell_info_s      cn66xx;
};
typedef union cvmx_sriox_tx_bell_info cvmx_sriox_tx_bell_info_t;

/**
 * cvmx_srio#_tx_ctrl
 *
 * SRIO_TX_CTRL = SRIO Transmit Control
 *
 * The SRIO Transmit Control
 *
 * Notes:
 * This register is used to control SRIO Outgoing Packet Allocation.  TAG_TH[2:0] set the thresholds
 *  to allow priority traffic requiring responses to be queued based on the number of outgoing tags
 *  (TIDs) available.  16 Tags are available.  If a priority is blocked for lack of tags then all
 *  lower priority packets are also blocked irregardless of whether they require tags.
 *
 * Clk_Rst:        SRIO(0,2..3)_TX_CTRL    hclk    hrst_n
 */
union cvmx_sriox_tx_ctrl {
	uint64_t u64;
	struct cvmx_sriox_tx_ctrl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_53_63               : 11;
	uint64_t tag_th2                      : 5;  /**< Sets threshold for minimum number of OTAGs
                                                         required before a packet of priority 2 requiring a
                                                         response will be queued for transmission. (Max 16)
                                                         There generally should be no priority 3 request
                                                         packets which require a response/tag, so a TAG_THR
                                                         value as low as 0 is allowed. */
	uint64_t reserved_45_47               : 3;
	uint64_t tag_th1                      : 5;  /**< Sets threshold for minimum number of OTAGs
                                                         required before a packet of priority 1 requiring a
                                                         response will be queued for transmission. (Max 16)
                                                         Generally, TAG_TH1 must be > TAG_TH2 to leave OTAGs
                                                         for outgoing priority 2 (or 3) requests. */
	uint64_t reserved_37_39               : 3;
	uint64_t tag_th0                      : 5;  /**< Sets threshold for minimum number of OTAGs
                                                         required before a packet of priority 0 requiring a
                                                         response will be queued for transmission. (Max 16)
                                                         Generally, TAG_TH0 must be > TAG_TH1 to leave OTAGs
                                                         for outgoing priority 1 or 2 (or 3) requests. */
	uint64_t reserved_20_31               : 12;
	uint64_t tx_th2                       : 4;  /**< Reserved. (See SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2) */
	uint64_t reserved_12_15               : 4;
	uint64_t tx_th1                       : 4;  /**< Reserved. (See SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2) */
	uint64_t reserved_4_7                 : 4;
	uint64_t tx_th0                       : 4;  /**< Reserved. (See SRIOMAINT(0,2..3)_IR_BUFFER_CONFIG2) */
#else
	uint64_t tx_th0                       : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t tx_th1                       : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t tx_th2                       : 4;
	uint64_t reserved_20_31               : 12;
	uint64_t tag_th0                      : 5;
	uint64_t reserved_37_39               : 3;
	uint64_t tag_th1                      : 5;
	uint64_t reserved_45_47               : 3;
	uint64_t tag_th2                      : 5;
	uint64_t reserved_53_63               : 11;
#endif
	} s;
	struct cvmx_sriox_tx_ctrl_s           cn63xx;
	struct cvmx_sriox_tx_ctrl_s           cn63xxp1;
	struct cvmx_sriox_tx_ctrl_s           cn66xx;
};
typedef union cvmx_sriox_tx_ctrl cvmx_sriox_tx_ctrl_t;

/**
 * cvmx_srio#_tx_emphasis
 *
 * SRIO_TX_EMPHASIS = SRIO TX Lane Emphasis
 *
 * Controls TX Emphasis used by the SRIO SERDES
 *
 * Notes:
 * This controls the emphasis value used by the SRIO SERDES.  This register is only reset during COLD
 *  boot and may be modified regardless of the value in SRIO(0,2..3)_STATUS_REG.ACCESS.  This register is not
 *  connected to the QLM and thus has no effect.  It should not be included in the documentation.
 *
 * Clk_Rst:        SRIO(0,2..3)_TX_EMPHASIS        sclk    srst_cold_n
 */
union cvmx_sriox_tx_emphasis {
	uint64_t u64;
	struct cvmx_sriox_tx_emphasis_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t emph                         : 4;  /**< Emphasis Value used for all lanes.  Default value
                                                         is 0x0 for 1.25G b/s and 0xA for all other rates. */
#else
	uint64_t emph                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_sriox_tx_emphasis_s       cn63xx;
	struct cvmx_sriox_tx_emphasis_s       cn66xx;
};
typedef union cvmx_sriox_tx_emphasis cvmx_sriox_tx_emphasis_t;

/**
 * cvmx_srio#_tx_status
 *
 * SRIO_TX_STATUS = SRIO Outbound Credits/Ops Status
 *
 * Specifies the current number of credits/ops by SRIO for Outbound Traffic
 *
 * Notes:
 * Debug Register specifying the number of credits/ops currently in use for Outbound Traffic.
 *  When all outbound traffic has stopped the values should eventually return to the reset values.
 *
 * Clk_Rst:        SRIO(0,2..3)_TX_STATUS  hclk    hrst_n
 */
union cvmx_sriox_tx_status {
	uint64_t u64;
	struct cvmx_sriox_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t s2m_pr3                      : 8;  /**< Number of pending S2M Priority 3 Entries. */
	uint64_t s2m_pr2                      : 8;  /**< Number of pending S2M Priority 2 Entries. */
	uint64_t s2m_pr1                      : 8;  /**< Number of pending S2M Priority 1 Entries. */
	uint64_t s2m_pr0                      : 8;  /**< Number of pending S2M Priority 0 Entries. */
#else
	uint64_t s2m_pr0                      : 8;
	uint64_t s2m_pr1                      : 8;
	uint64_t s2m_pr2                      : 8;
	uint64_t s2m_pr3                      : 8;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_tx_status_s         cn63xx;
	struct cvmx_sriox_tx_status_s         cn63xxp1;
	struct cvmx_sriox_tx_status_s         cn66xx;
};
typedef union cvmx_sriox_tx_status cvmx_sriox_tx_status_t;

/**
 * cvmx_srio#_wr_done_counts
 *
 * SRIO_WR_DONE_COUNTS = SRIO Outgoing Write Done Counts
 *
 * The SRIO Outbound Write Done Counts
 *
 * Notes:
 * This register shows the number of successful and unsuccessful NwriteRs issued through this MAC.
 *  These count only considers the last NwriteR generated by each Store Instruction.  If any NwriteR
 *  in the series receives an ERROR Status then it is reported in SRIOMAINT(0,2..3)_ERB_LT_ERR_DET.IO_ERR.
 *  If any NwriteR does not receive a response within the timeout period then it is reported in
 *  SRIOMAINT(0,2..3)_ERB_LT_ERR_DET.PKT_TOUT.  Only errors on the last NwriteR's are counted as BAD.  This
 *  register is typically not written while Outbound SRIO Memory traffic is enabled.
 *
 * Clk_Rst:        SRIO(0,2..3)_WR_DONE_COUNTS     hclk    hrst_n
 */
union cvmx_sriox_wr_done_counts {
	uint64_t u64;
	struct cvmx_sriox_wr_done_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t bad                          : 16; /**< Count of the final outbound NwriteR in the series
                                                         associated with a Store Operation that have timed
                                                         out or received a response with an ERROR status. */
	uint64_t good                         : 16; /**< Count of the final outbound NwriteR in the series
                                                         associated with a Store operation that has
                                                         received a response with a DONE status. */
#else
	uint64_t good                         : 16;
	uint64_t bad                          : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_sriox_wr_done_counts_s    cn63xx;
	struct cvmx_sriox_wr_done_counts_s    cn66xx;
};
typedef union cvmx_sriox_wr_done_counts cvmx_sriox_wr_done_counts_t;

#endif
