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
 * cvmx-pko-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pko.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PKO_DEFS_H__
#define __CVMX_PKO_DEFS_H__

#define CVMX_PKO_MEM_COUNT0 (CVMX_ADD_IO_SEG(0x0001180050001080ull))
#define CVMX_PKO_MEM_COUNT1 (CVMX_ADD_IO_SEG(0x0001180050001088ull))
#define CVMX_PKO_MEM_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180050001100ull))
#define CVMX_PKO_MEM_DEBUG1 (CVMX_ADD_IO_SEG(0x0001180050001108ull))
#define CVMX_PKO_MEM_DEBUG10 (CVMX_ADD_IO_SEG(0x0001180050001150ull))
#define CVMX_PKO_MEM_DEBUG11 (CVMX_ADD_IO_SEG(0x0001180050001158ull))
#define CVMX_PKO_MEM_DEBUG12 (CVMX_ADD_IO_SEG(0x0001180050001160ull))
#define CVMX_PKO_MEM_DEBUG13 (CVMX_ADD_IO_SEG(0x0001180050001168ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_DEBUG14 CVMX_PKO_MEM_DEBUG14_FUNC()
static inline uint64_t CVMX_PKO_MEM_DEBUG14_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_DEBUG14 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001170ull);
}
#else
#define CVMX_PKO_MEM_DEBUG14 (CVMX_ADD_IO_SEG(0x0001180050001170ull))
#endif
#define CVMX_PKO_MEM_DEBUG2 (CVMX_ADD_IO_SEG(0x0001180050001110ull))
#define CVMX_PKO_MEM_DEBUG3 (CVMX_ADD_IO_SEG(0x0001180050001118ull))
#define CVMX_PKO_MEM_DEBUG4 (CVMX_ADD_IO_SEG(0x0001180050001120ull))
#define CVMX_PKO_MEM_DEBUG5 (CVMX_ADD_IO_SEG(0x0001180050001128ull))
#define CVMX_PKO_MEM_DEBUG6 (CVMX_ADD_IO_SEG(0x0001180050001130ull))
#define CVMX_PKO_MEM_DEBUG7 (CVMX_ADD_IO_SEG(0x0001180050001138ull))
#define CVMX_PKO_MEM_DEBUG8 (CVMX_ADD_IO_SEG(0x0001180050001140ull))
#define CVMX_PKO_MEM_DEBUG9 (CVMX_ADD_IO_SEG(0x0001180050001148ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_IPORT_PTRS CVMX_PKO_MEM_IPORT_PTRS_FUNC()
static inline uint64_t CVMX_PKO_MEM_IPORT_PTRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_MEM_IPORT_PTRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001030ull);
}
#else
#define CVMX_PKO_MEM_IPORT_PTRS (CVMX_ADD_IO_SEG(0x0001180050001030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_IPORT_QOS CVMX_PKO_MEM_IPORT_QOS_FUNC()
static inline uint64_t CVMX_PKO_MEM_IPORT_QOS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_MEM_IPORT_QOS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001038ull);
}
#else
#define CVMX_PKO_MEM_IPORT_QOS (CVMX_ADD_IO_SEG(0x0001180050001038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_IQUEUE_PTRS CVMX_PKO_MEM_IQUEUE_PTRS_FUNC()
static inline uint64_t CVMX_PKO_MEM_IQUEUE_PTRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_MEM_IQUEUE_PTRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001040ull);
}
#else
#define CVMX_PKO_MEM_IQUEUE_PTRS (CVMX_ADD_IO_SEG(0x0001180050001040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_IQUEUE_QOS CVMX_PKO_MEM_IQUEUE_QOS_FUNC()
static inline uint64_t CVMX_PKO_MEM_IQUEUE_QOS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_MEM_IQUEUE_QOS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001048ull);
}
#else
#define CVMX_PKO_MEM_IQUEUE_QOS (CVMX_ADD_IO_SEG(0x0001180050001048ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_PORT_PTRS CVMX_PKO_MEM_PORT_PTRS_FUNC()
static inline uint64_t CVMX_PKO_MEM_PORT_PTRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_PORT_PTRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001010ull);
}
#else
#define CVMX_PKO_MEM_PORT_PTRS (CVMX_ADD_IO_SEG(0x0001180050001010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_PORT_QOS CVMX_PKO_MEM_PORT_QOS_FUNC()
static inline uint64_t CVMX_PKO_MEM_PORT_QOS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_PORT_QOS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001018ull);
}
#else
#define CVMX_PKO_MEM_PORT_QOS (CVMX_ADD_IO_SEG(0x0001180050001018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_PORT_RATE0 CVMX_PKO_MEM_PORT_RATE0_FUNC()
static inline uint64_t CVMX_PKO_MEM_PORT_RATE0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_PORT_RATE0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001020ull);
}
#else
#define CVMX_PKO_MEM_PORT_RATE0 (CVMX_ADD_IO_SEG(0x0001180050001020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_PORT_RATE1 CVMX_PKO_MEM_PORT_RATE1_FUNC()
static inline uint64_t CVMX_PKO_MEM_PORT_RATE1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_PORT_RATE1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001028ull);
}
#else
#define CVMX_PKO_MEM_PORT_RATE1 (CVMX_ADD_IO_SEG(0x0001180050001028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_QUEUE_PTRS CVMX_PKO_MEM_QUEUE_PTRS_FUNC()
static inline uint64_t CVMX_PKO_MEM_QUEUE_PTRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_QUEUE_PTRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001000ull);
}
#else
#define CVMX_PKO_MEM_QUEUE_PTRS (CVMX_ADD_IO_SEG(0x0001180050001000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_QUEUE_QOS CVMX_PKO_MEM_QUEUE_QOS_FUNC()
static inline uint64_t CVMX_PKO_MEM_QUEUE_QOS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_MEM_QUEUE_QOS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001008ull);
}
#else
#define CVMX_PKO_MEM_QUEUE_QOS (CVMX_ADD_IO_SEG(0x0001180050001008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_THROTTLE_INT CVMX_PKO_MEM_THROTTLE_INT_FUNC()
static inline uint64_t CVMX_PKO_MEM_THROTTLE_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_MEM_THROTTLE_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001058ull);
}
#else
#define CVMX_PKO_MEM_THROTTLE_INT (CVMX_ADD_IO_SEG(0x0001180050001058ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_MEM_THROTTLE_PIPE CVMX_PKO_MEM_THROTTLE_PIPE_FUNC()
static inline uint64_t CVMX_PKO_MEM_THROTTLE_PIPE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_MEM_THROTTLE_PIPE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050001050ull);
}
#else
#define CVMX_PKO_MEM_THROTTLE_PIPE (CVMX_ADD_IO_SEG(0x0001180050001050ull))
#endif
#define CVMX_PKO_REG_BIST_RESULT (CVMX_ADD_IO_SEG(0x0001180050000080ull))
#define CVMX_PKO_REG_CMD_BUF (CVMX_ADD_IO_SEG(0x0001180050000010ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PKO_REG_CRC_CTLX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PKO_REG_CRC_CTLX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180050000028ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PKO_REG_CRC_CTLX(offset) (CVMX_ADD_IO_SEG(0x0001180050000028ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_CRC_ENABLE CVMX_PKO_REG_CRC_ENABLE_FUNC()
static inline uint64_t CVMX_PKO_REG_CRC_ENABLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_PKO_REG_CRC_ENABLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000020ull);
}
#else
#define CVMX_PKO_REG_CRC_ENABLE (CVMX_ADD_IO_SEG(0x0001180050000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PKO_REG_CRC_IVX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PKO_REG_CRC_IVX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180050000038ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PKO_REG_CRC_IVX(offset) (CVMX_ADD_IO_SEG(0x0001180050000038ull) + ((offset) & 1) * 8)
#endif
#define CVMX_PKO_REG_DEBUG0 (CVMX_ADD_IO_SEG(0x0001180050000098ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_DEBUG1 CVMX_PKO_REG_DEBUG1_FUNC()
static inline uint64_t CVMX_PKO_REG_DEBUG1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_DEBUG1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800500000A0ull);
}
#else
#define CVMX_PKO_REG_DEBUG1 (CVMX_ADD_IO_SEG(0x00011800500000A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_DEBUG2 CVMX_PKO_REG_DEBUG2_FUNC()
static inline uint64_t CVMX_PKO_REG_DEBUG2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_DEBUG2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800500000A8ull);
}
#else
#define CVMX_PKO_REG_DEBUG2 (CVMX_ADD_IO_SEG(0x00011800500000A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_DEBUG3 CVMX_PKO_REG_DEBUG3_FUNC()
static inline uint64_t CVMX_PKO_REG_DEBUG3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_DEBUG3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800500000B0ull);
}
#else
#define CVMX_PKO_REG_DEBUG3 (CVMX_ADD_IO_SEG(0x00011800500000B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_DEBUG4 CVMX_PKO_REG_DEBUG4_FUNC()
static inline uint64_t CVMX_PKO_REG_DEBUG4_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_REG_DEBUG4 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800500000B8ull);
}
#else
#define CVMX_PKO_REG_DEBUG4 (CVMX_ADD_IO_SEG(0x00011800500000B8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_ENGINE_INFLIGHT CVMX_PKO_REG_ENGINE_INFLIGHT_FUNC()
static inline uint64_t CVMX_PKO_REG_ENGINE_INFLIGHT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_ENGINE_INFLIGHT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000050ull);
}
#else
#define CVMX_PKO_REG_ENGINE_INFLIGHT (CVMX_ADD_IO_SEG(0x0001180050000050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_ENGINE_INFLIGHT1 CVMX_PKO_REG_ENGINE_INFLIGHT1_FUNC()
static inline uint64_t CVMX_PKO_REG_ENGINE_INFLIGHT1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_REG_ENGINE_INFLIGHT1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000318ull);
}
#else
#define CVMX_PKO_REG_ENGINE_INFLIGHT1 (CVMX_ADD_IO_SEG(0x0001180050000318ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PKO_REG_ENGINE_STORAGEX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PKO_REG_ENGINE_STORAGEX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180050000300ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PKO_REG_ENGINE_STORAGEX(offset) (CVMX_ADD_IO_SEG(0x0001180050000300ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_ENGINE_THRESH CVMX_PKO_REG_ENGINE_THRESH_FUNC()
static inline uint64_t CVMX_PKO_REG_ENGINE_THRESH_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_ENGINE_THRESH not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000058ull);
}
#else
#define CVMX_PKO_REG_ENGINE_THRESH (CVMX_ADD_IO_SEG(0x0001180050000058ull))
#endif
#define CVMX_PKO_REG_ERROR (CVMX_ADD_IO_SEG(0x0001180050000088ull))
#define CVMX_PKO_REG_FLAGS (CVMX_ADD_IO_SEG(0x0001180050000000ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_GMX_PORT_MODE CVMX_PKO_REG_GMX_PORT_MODE_FUNC()
static inline uint64_t CVMX_PKO_REG_GMX_PORT_MODE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_GMX_PORT_MODE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000018ull);
}
#else
#define CVMX_PKO_REG_GMX_PORT_MODE (CVMX_ADD_IO_SEG(0x0001180050000018ull))
#endif
#define CVMX_PKO_REG_INT_MASK (CVMX_ADD_IO_SEG(0x0001180050000090ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_LOOPBACK_BPID CVMX_PKO_REG_LOOPBACK_BPID_FUNC()
static inline uint64_t CVMX_PKO_REG_LOOPBACK_BPID_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_REG_LOOPBACK_BPID not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000118ull);
}
#else
#define CVMX_PKO_REG_LOOPBACK_BPID (CVMX_ADD_IO_SEG(0x0001180050000118ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_LOOPBACK_PKIND CVMX_PKO_REG_LOOPBACK_PKIND_FUNC()
static inline uint64_t CVMX_PKO_REG_LOOPBACK_PKIND_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_REG_LOOPBACK_PKIND not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000068ull);
}
#else
#define CVMX_PKO_REG_LOOPBACK_PKIND (CVMX_ADD_IO_SEG(0x0001180050000068ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_MIN_PKT CVMX_PKO_REG_MIN_PKT_FUNC()
static inline uint64_t CVMX_PKO_REG_MIN_PKT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_REG_MIN_PKT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000070ull);
}
#else
#define CVMX_PKO_REG_MIN_PKT (CVMX_ADD_IO_SEG(0x0001180050000070ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_PREEMPT CVMX_PKO_REG_PREEMPT_FUNC()
static inline uint64_t CVMX_PKO_REG_PREEMPT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_PREEMPT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000110ull);
}
#else
#define CVMX_PKO_REG_PREEMPT (CVMX_ADD_IO_SEG(0x0001180050000110ull))
#endif
#define CVMX_PKO_REG_QUEUE_MODE (CVMX_ADD_IO_SEG(0x0001180050000048ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_QUEUE_PREEMPT CVMX_PKO_REG_QUEUE_PREEMPT_FUNC()
static inline uint64_t CVMX_PKO_REG_QUEUE_PREEMPT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_QUEUE_PREEMPT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000108ull);
}
#else
#define CVMX_PKO_REG_QUEUE_PREEMPT (CVMX_ADD_IO_SEG(0x0001180050000108ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_QUEUE_PTRS1 CVMX_PKO_REG_QUEUE_PTRS1_FUNC()
static inline uint64_t CVMX_PKO_REG_QUEUE_PTRS1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN5XXX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_QUEUE_PTRS1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000100ull);
}
#else
#define CVMX_PKO_REG_QUEUE_PTRS1 (CVMX_ADD_IO_SEG(0x0001180050000100ull))
#endif
#define CVMX_PKO_REG_READ_IDX (CVMX_ADD_IO_SEG(0x0001180050000008ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_THROTTLE CVMX_PKO_REG_THROTTLE_FUNC()
static inline uint64_t CVMX_PKO_REG_THROTTLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_PKO_REG_THROTTLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000078ull);
}
#else
#define CVMX_PKO_REG_THROTTLE (CVMX_ADD_IO_SEG(0x0001180050000078ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PKO_REG_TIMESTAMP CVMX_PKO_REG_TIMESTAMP_FUNC()
static inline uint64_t CVMX_PKO_REG_TIMESTAMP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PKO_REG_TIMESTAMP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180050000060ull);
}
#else
#define CVMX_PKO_REG_TIMESTAMP (CVMX_ADD_IO_SEG(0x0001180050000060ull))
#endif

/**
 * cvmx_pko_mem_count0
 *
 * Notes:
 * Total number of packets seen by PKO, per port
 * A write to this address will clear the entry whose index is specified as COUNT[5:0].
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_count0 {
	uint64_t u64;
	struct cvmx_pko_mem_count0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t count                        : 32; /**< Total number of packets seen by PKO */
#else
	uint64_t count                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pko_mem_count0_s          cn30xx;
	struct cvmx_pko_mem_count0_s          cn31xx;
	struct cvmx_pko_mem_count0_s          cn38xx;
	struct cvmx_pko_mem_count0_s          cn38xxp2;
	struct cvmx_pko_mem_count0_s          cn50xx;
	struct cvmx_pko_mem_count0_s          cn52xx;
	struct cvmx_pko_mem_count0_s          cn52xxp1;
	struct cvmx_pko_mem_count0_s          cn56xx;
	struct cvmx_pko_mem_count0_s          cn56xxp1;
	struct cvmx_pko_mem_count0_s          cn58xx;
	struct cvmx_pko_mem_count0_s          cn58xxp1;
	struct cvmx_pko_mem_count0_s          cn61xx;
	struct cvmx_pko_mem_count0_s          cn63xx;
	struct cvmx_pko_mem_count0_s          cn63xxp1;
	struct cvmx_pko_mem_count0_s          cn66xx;
	struct cvmx_pko_mem_count0_s          cn68xx;
	struct cvmx_pko_mem_count0_s          cn68xxp1;
	struct cvmx_pko_mem_count0_s          cnf71xx;
};
typedef union cvmx_pko_mem_count0 cvmx_pko_mem_count0_t;

/**
 * cvmx_pko_mem_count1
 *
 * Notes:
 * Total number of bytes seen by PKO, per port
 * A write to this address will clear the entry whose index is specified as COUNT[5:0].
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_count1 {
	uint64_t u64;
	struct cvmx_pko_mem_count1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t count                        : 48; /**< Total number of bytes seen by PKO */
#else
	uint64_t count                        : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pko_mem_count1_s          cn30xx;
	struct cvmx_pko_mem_count1_s          cn31xx;
	struct cvmx_pko_mem_count1_s          cn38xx;
	struct cvmx_pko_mem_count1_s          cn38xxp2;
	struct cvmx_pko_mem_count1_s          cn50xx;
	struct cvmx_pko_mem_count1_s          cn52xx;
	struct cvmx_pko_mem_count1_s          cn52xxp1;
	struct cvmx_pko_mem_count1_s          cn56xx;
	struct cvmx_pko_mem_count1_s          cn56xxp1;
	struct cvmx_pko_mem_count1_s          cn58xx;
	struct cvmx_pko_mem_count1_s          cn58xxp1;
	struct cvmx_pko_mem_count1_s          cn61xx;
	struct cvmx_pko_mem_count1_s          cn63xx;
	struct cvmx_pko_mem_count1_s          cn63xxp1;
	struct cvmx_pko_mem_count1_s          cn66xx;
	struct cvmx_pko_mem_count1_s          cn68xx;
	struct cvmx_pko_mem_count1_s          cn68xxp1;
	struct cvmx_pko_mem_count1_s          cnf71xx;
};
typedef union cvmx_pko_mem_count1 cvmx_pko_mem_count1_t;

/**
 * cvmx_pko_mem_debug0
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.cmnd[63:0]
 * This CSR is a memory of 12 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug0 {
	uint64_t u64;
	struct cvmx_pko_mem_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fau                          : 28; /**< Fetch and add command words */
	uint64_t cmd                          : 14; /**< Command word */
	uint64_t segs                         : 6;  /**< Number of segments/gather size */
	uint64_t size                         : 16; /**< Packet length in bytes */
#else
	uint64_t size                         : 16;
	uint64_t segs                         : 6;
	uint64_t cmd                          : 14;
	uint64_t fau                          : 28;
#endif
	} s;
	struct cvmx_pko_mem_debug0_s          cn30xx;
	struct cvmx_pko_mem_debug0_s          cn31xx;
	struct cvmx_pko_mem_debug0_s          cn38xx;
	struct cvmx_pko_mem_debug0_s          cn38xxp2;
	struct cvmx_pko_mem_debug0_s          cn50xx;
	struct cvmx_pko_mem_debug0_s          cn52xx;
	struct cvmx_pko_mem_debug0_s          cn52xxp1;
	struct cvmx_pko_mem_debug0_s          cn56xx;
	struct cvmx_pko_mem_debug0_s          cn56xxp1;
	struct cvmx_pko_mem_debug0_s          cn58xx;
	struct cvmx_pko_mem_debug0_s          cn58xxp1;
	struct cvmx_pko_mem_debug0_s          cn61xx;
	struct cvmx_pko_mem_debug0_s          cn63xx;
	struct cvmx_pko_mem_debug0_s          cn63xxp1;
	struct cvmx_pko_mem_debug0_s          cn66xx;
	struct cvmx_pko_mem_debug0_s          cn68xx;
	struct cvmx_pko_mem_debug0_s          cn68xxp1;
	struct cvmx_pko_mem_debug0_s          cnf71xx;
};
typedef union cvmx_pko_mem_debug0 cvmx_pko_mem_debug0_t;

/**
 * cvmx_pko_mem_debug1
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.curr[63:0]
 * This CSR is a memory of 12 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug1 {
	uint64_t u64;
	struct cvmx_pko_mem_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t i                            : 1;  /**< "I"  value used for free operation */
	uint64_t back                         : 4;  /**< Back value used for free operation */
	uint64_t pool                         : 3;  /**< Pool value used for free operation */
	uint64_t size                         : 16; /**< Size in bytes */
	uint64_t ptr                          : 40; /**< Data pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t pool                         : 3;
	uint64_t back                         : 4;
	uint64_t i                            : 1;
#endif
	} s;
	struct cvmx_pko_mem_debug1_s          cn30xx;
	struct cvmx_pko_mem_debug1_s          cn31xx;
	struct cvmx_pko_mem_debug1_s          cn38xx;
	struct cvmx_pko_mem_debug1_s          cn38xxp2;
	struct cvmx_pko_mem_debug1_s          cn50xx;
	struct cvmx_pko_mem_debug1_s          cn52xx;
	struct cvmx_pko_mem_debug1_s          cn52xxp1;
	struct cvmx_pko_mem_debug1_s          cn56xx;
	struct cvmx_pko_mem_debug1_s          cn56xxp1;
	struct cvmx_pko_mem_debug1_s          cn58xx;
	struct cvmx_pko_mem_debug1_s          cn58xxp1;
	struct cvmx_pko_mem_debug1_s          cn61xx;
	struct cvmx_pko_mem_debug1_s          cn63xx;
	struct cvmx_pko_mem_debug1_s          cn63xxp1;
	struct cvmx_pko_mem_debug1_s          cn66xx;
	struct cvmx_pko_mem_debug1_s          cn68xx;
	struct cvmx_pko_mem_debug1_s          cn68xxp1;
	struct cvmx_pko_mem_debug1_s          cnf71xx;
};
typedef union cvmx_pko_mem_debug1 cvmx_pko_mem_debug1_t;

/**
 * cvmx_pko_mem_debug10
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko.dat.ptr.ptrs1, pko.dat.ptr.ptrs2
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug10 {
	uint64_t u64;
	struct cvmx_pko_mem_debug10_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug10_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fau                          : 28; /**< Fetch and add command words */
	uint64_t cmd                          : 14; /**< Command word */
	uint64_t segs                         : 6;  /**< Number of segments/gather size */
	uint64_t size                         : 16; /**< Packet length in bytes */
#else
	uint64_t size                         : 16;
	uint64_t segs                         : 6;
	uint64_t cmd                          : 14;
	uint64_t fau                          : 28;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug10_cn30xx    cn31xx;
	struct cvmx_pko_mem_debug10_cn30xx    cn38xx;
	struct cvmx_pko_mem_debug10_cn30xx    cn38xxp2;
	struct cvmx_pko_mem_debug10_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ptrs1                        : 17; /**< Internal state */
	uint64_t reserved_17_31               : 15;
	uint64_t ptrs2                        : 17; /**< Internal state */
#else
	uint64_t ptrs2                        : 17;
	uint64_t reserved_17_31               : 15;
	uint64_t ptrs1                        : 17;
	uint64_t reserved_49_63               : 15;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn52xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn52xxp1;
	struct cvmx_pko_mem_debug10_cn50xx    cn56xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn56xxp1;
	struct cvmx_pko_mem_debug10_cn50xx    cn58xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn58xxp1;
	struct cvmx_pko_mem_debug10_cn50xx    cn61xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn63xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn63xxp1;
	struct cvmx_pko_mem_debug10_cn50xx    cn66xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn68xx;
	struct cvmx_pko_mem_debug10_cn50xx    cn68xxp1;
	struct cvmx_pko_mem_debug10_cn50xx    cnf71xx;
};
typedef union cvmx_pko_mem_debug10 cvmx_pko_mem_debug10_t;

/**
 * cvmx_pko_mem_debug11
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko.out.sta.state[22:0]
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug11 {
	uint64_t u64;
	struct cvmx_pko_mem_debug11_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t i                            : 1;  /**< "I"  value used for free operation */
	uint64_t back                         : 4;  /**< Back value used for free operation */
	uint64_t pool                         : 3;  /**< Pool value used for free operation */
	uint64_t size                         : 16; /**< Size in bytes */
	uint64_t reserved_0_39                : 40;
#else
	uint64_t reserved_0_39                : 40;
	uint64_t size                         : 16;
	uint64_t pool                         : 3;
	uint64_t back                         : 4;
	uint64_t i                            : 1;
#endif
	} s;
	struct cvmx_pko_mem_debug11_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t i                            : 1;  /**< "I"  value used for free operation */
	uint64_t back                         : 4;  /**< Back value used for free operation */
	uint64_t pool                         : 3;  /**< Pool value used for free operation */
	uint64_t size                         : 16; /**< Size in bytes */
	uint64_t ptr                          : 40; /**< Data pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t pool                         : 3;
	uint64_t back                         : 4;
	uint64_t i                            : 1;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug11_cn30xx    cn31xx;
	struct cvmx_pko_mem_debug11_cn30xx    cn38xx;
	struct cvmx_pko_mem_debug11_cn30xx    cn38xxp2;
	struct cvmx_pko_mem_debug11_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t maj                          : 1;  /**< Internal state */
	uint64_t uid                          : 3;  /**< Internal state */
	uint64_t sop                          : 1;  /**< Internal state */
	uint64_t len                          : 1;  /**< Internal state */
	uint64_t chk                          : 1;  /**< Internal state */
	uint64_t cnt                          : 13; /**< Internal state */
	uint64_t mod                          : 3;  /**< Internal state */
#else
	uint64_t mod                          : 3;
	uint64_t cnt                          : 13;
	uint64_t chk                          : 1;
	uint64_t len                          : 1;
	uint64_t sop                          : 1;
	uint64_t uid                          : 3;
	uint64_t maj                          : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn52xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn52xxp1;
	struct cvmx_pko_mem_debug11_cn50xx    cn56xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn56xxp1;
	struct cvmx_pko_mem_debug11_cn50xx    cn58xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn58xxp1;
	struct cvmx_pko_mem_debug11_cn50xx    cn61xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn63xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn63xxp1;
	struct cvmx_pko_mem_debug11_cn50xx    cn66xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn68xx;
	struct cvmx_pko_mem_debug11_cn50xx    cn68xxp1;
	struct cvmx_pko_mem_debug11_cn50xx    cnf71xx;
};
typedef union cvmx_pko_mem_debug11 cvmx_pko_mem_debug11_t;

/**
 * cvmx_pko_mem_debug12
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko.out.ctl.cmnd[63:0]
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug12 {
	uint64_t u64;
	struct cvmx_pko_mem_debug12_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug12_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< WorkQ data or Store0 pointer */
#else
	uint64_t data                         : 64;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug12_cn30xx    cn31xx;
	struct cvmx_pko_mem_debug12_cn30xx    cn38xx;
	struct cvmx_pko_mem_debug12_cn30xx    cn38xxp2;
	struct cvmx_pko_mem_debug12_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fau                          : 28; /**< Fetch and add command words */
	uint64_t cmd                          : 14; /**< Command word */
	uint64_t segs                         : 6;  /**< Number of segments/gather size */
	uint64_t size                         : 16; /**< Packet length in bytes */
#else
	uint64_t size                         : 16;
	uint64_t segs                         : 6;
	uint64_t cmd                          : 14;
	uint64_t fau                          : 28;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug12_cn50xx    cn52xx;
	struct cvmx_pko_mem_debug12_cn50xx    cn52xxp1;
	struct cvmx_pko_mem_debug12_cn50xx    cn56xx;
	struct cvmx_pko_mem_debug12_cn50xx    cn56xxp1;
	struct cvmx_pko_mem_debug12_cn50xx    cn58xx;
	struct cvmx_pko_mem_debug12_cn50xx    cn58xxp1;
	struct cvmx_pko_mem_debug12_cn50xx    cn61xx;
	struct cvmx_pko_mem_debug12_cn50xx    cn63xx;
	struct cvmx_pko_mem_debug12_cn50xx    cn63xxp1;
	struct cvmx_pko_mem_debug12_cn50xx    cn66xx;
	struct cvmx_pko_mem_debug12_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t state                        : 64; /**< Internal state */
#else
	uint64_t state                        : 64;
#endif
	} cn68xx;
	struct cvmx_pko_mem_debug12_cn68xx    cn68xxp1;
	struct cvmx_pko_mem_debug12_cn50xx    cnf71xx;
};
typedef union cvmx_pko_mem_debug12 cvmx_pko_mem_debug12_t;

/**
 * cvmx_pko_mem_debug13
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko.out.ctl.head[63:0]
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug13 {
	uint64_t u64;
	struct cvmx_pko_mem_debug13_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug13_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_51_63               : 13;
	uint64_t widx                         : 17; /**< PDB widx */
	uint64_t ridx2                        : 17; /**< PDB ridx2 */
	uint64_t widx2                        : 17; /**< PDB widx2 */
#else
	uint64_t widx2                        : 17;
	uint64_t ridx2                        : 17;
	uint64_t widx                         : 17;
	uint64_t reserved_51_63               : 13;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug13_cn30xx    cn31xx;
	struct cvmx_pko_mem_debug13_cn30xx    cn38xx;
	struct cvmx_pko_mem_debug13_cn30xx    cn38xxp2;
	struct cvmx_pko_mem_debug13_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t i                            : 1;  /**< "I"  value used for free operation */
	uint64_t back                         : 4;  /**< Back value used for free operation */
	uint64_t pool                         : 3;  /**< Pool value used for free operation */
	uint64_t size                         : 16; /**< Size in bytes */
	uint64_t ptr                          : 40; /**< Data pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t pool                         : 3;
	uint64_t back                         : 4;
	uint64_t i                            : 1;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug13_cn50xx    cn52xx;
	struct cvmx_pko_mem_debug13_cn50xx    cn52xxp1;
	struct cvmx_pko_mem_debug13_cn50xx    cn56xx;
	struct cvmx_pko_mem_debug13_cn50xx    cn56xxp1;
	struct cvmx_pko_mem_debug13_cn50xx    cn58xx;
	struct cvmx_pko_mem_debug13_cn50xx    cn58xxp1;
	struct cvmx_pko_mem_debug13_cn50xx    cn61xx;
	struct cvmx_pko_mem_debug13_cn50xx    cn63xx;
	struct cvmx_pko_mem_debug13_cn50xx    cn63xxp1;
	struct cvmx_pko_mem_debug13_cn50xx    cn66xx;
	struct cvmx_pko_mem_debug13_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t state                        : 64; /**< Internal state */
#else
	uint64_t state                        : 64;
#endif
	} cn68xx;
	struct cvmx_pko_mem_debug13_cn68xx    cn68xxp1;
	struct cvmx_pko_mem_debug13_cn50xx    cnf71xx;
};
typedef union cvmx_pko_mem_debug13 cvmx_pko_mem_debug13_t;

/**
 * cvmx_pko_mem_debug14
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko.prt.psb.save[63:0]
 * This CSR is a memory of 132 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug14 {
	uint64_t u64;
	struct cvmx_pko_mem_debug14_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug14_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t ridx                         : 17; /**< PDB ridx */
#else
	uint64_t ridx                         : 17;
	uint64_t reserved_17_63               : 47;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug14_cn30xx    cn31xx;
	struct cvmx_pko_mem_debug14_cn30xx    cn38xx;
	struct cvmx_pko_mem_debug14_cn30xx    cn38xxp2;
	struct cvmx_pko_mem_debug14_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< Command words */
#else
	uint64_t data                         : 64;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug14_cn52xx    cn52xxp1;
	struct cvmx_pko_mem_debug14_cn52xx    cn56xx;
	struct cvmx_pko_mem_debug14_cn52xx    cn56xxp1;
	struct cvmx_pko_mem_debug14_cn52xx    cn61xx;
	struct cvmx_pko_mem_debug14_cn52xx    cn63xx;
	struct cvmx_pko_mem_debug14_cn52xx    cn63xxp1;
	struct cvmx_pko_mem_debug14_cn52xx    cn66xx;
	struct cvmx_pko_mem_debug14_cn52xx    cnf71xx;
};
typedef union cvmx_pko_mem_debug14 cvmx_pko_mem_debug14_t;

/**
 * cvmx_pko_mem_debug2
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.head[63:0]
 * This CSR is a memory of 12 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug2 {
	uint64_t u64;
	struct cvmx_pko_mem_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t i                            : 1;  /**< "I"  value used for free operation */
	uint64_t back                         : 4;  /**< Back value used for free operation */
	uint64_t pool                         : 3;  /**< Pool value used for free operation */
	uint64_t size                         : 16; /**< Size in bytes */
	uint64_t ptr                          : 40; /**< Data pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t pool                         : 3;
	uint64_t back                         : 4;
	uint64_t i                            : 1;
#endif
	} s;
	struct cvmx_pko_mem_debug2_s          cn30xx;
	struct cvmx_pko_mem_debug2_s          cn31xx;
	struct cvmx_pko_mem_debug2_s          cn38xx;
	struct cvmx_pko_mem_debug2_s          cn38xxp2;
	struct cvmx_pko_mem_debug2_s          cn50xx;
	struct cvmx_pko_mem_debug2_s          cn52xx;
	struct cvmx_pko_mem_debug2_s          cn52xxp1;
	struct cvmx_pko_mem_debug2_s          cn56xx;
	struct cvmx_pko_mem_debug2_s          cn56xxp1;
	struct cvmx_pko_mem_debug2_s          cn58xx;
	struct cvmx_pko_mem_debug2_s          cn58xxp1;
	struct cvmx_pko_mem_debug2_s          cn61xx;
	struct cvmx_pko_mem_debug2_s          cn63xx;
	struct cvmx_pko_mem_debug2_s          cn63xxp1;
	struct cvmx_pko_mem_debug2_s          cn66xx;
	struct cvmx_pko_mem_debug2_s          cn68xx;
	struct cvmx_pko_mem_debug2_s          cn68xxp1;
	struct cvmx_pko_mem_debug2_s          cnf71xx;
};
typedef union cvmx_pko_mem_debug2 cvmx_pko_mem_debug2_t;

/**
 * cvmx_pko_mem_debug3
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.resp[63:0]
 * This CSR is a memory of 12 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug3 {
	uint64_t u64;
	struct cvmx_pko_mem_debug3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug3_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t i                            : 1;  /**< "I"  value used for free operation */
	uint64_t back                         : 4;  /**< Back value used for free operation */
	uint64_t pool                         : 3;  /**< Pool value used for free operation */
	uint64_t size                         : 16; /**< Size in bytes */
	uint64_t ptr                          : 40; /**< Data pointer */
#else
	uint64_t ptr                          : 40;
	uint64_t size                         : 16;
	uint64_t pool                         : 3;
	uint64_t back                         : 4;
	uint64_t i                            : 1;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug3_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug3_cn30xx     cn38xx;
	struct cvmx_pko_mem_debug3_cn30xx     cn38xxp2;
	struct cvmx_pko_mem_debug3_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< WorkQ data or Store0 pointer */
#else
	uint64_t data                         : 64;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn52xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn52xxp1;
	struct cvmx_pko_mem_debug3_cn50xx     cn56xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn56xxp1;
	struct cvmx_pko_mem_debug3_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug3_cn50xx     cn61xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn63xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn63xxp1;
	struct cvmx_pko_mem_debug3_cn50xx     cn66xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn68xx;
	struct cvmx_pko_mem_debug3_cn50xx     cn68xxp1;
	struct cvmx_pko_mem_debug3_cn50xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug3 cvmx_pko_mem_debug3_t;

/**
 * cvmx_pko_mem_debug4
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.state[63:0]
 * This CSR is a memory of 12 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug4 {
	uint64_t u64;
	struct cvmx_pko_mem_debug4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug4_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< WorkQ data or Store0 pointer */
#else
	uint64_t data                         : 64;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug4_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug4_cn30xx     cn38xx;
	struct cvmx_pko_mem_debug4_cn30xx     cn38xxp2;
	struct cvmx_pko_mem_debug4_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cmnd_segs                    : 3;  /**< Internal state */
	uint64_t cmnd_siz                     : 16; /**< Internal state */
	uint64_t cmnd_off                     : 6;  /**< Internal state */
	uint64_t uid                          : 3;  /**< Internal state */
	uint64_t dread_sop                    : 1;  /**< Internal state */
	uint64_t init_dwrite                  : 1;  /**< Internal state */
	uint64_t chk_once                     : 1;  /**< Internal state */
	uint64_t chk_mode                     : 1;  /**< Internal state */
	uint64_t active                       : 1;  /**< Internal state */
	uint64_t static_p                     : 1;  /**< Internal state */
	uint64_t qos                          : 3;  /**< Internal state */
	uint64_t qcb_ridx                     : 5;  /**< Internal state */
	uint64_t qid_off_max                  : 4;  /**< Internal state */
	uint64_t qid_off                      : 4;  /**< Internal state */
	uint64_t qid_base                     : 8;  /**< Internal state */
	uint64_t wait                         : 1;  /**< Internal state */
	uint64_t minor                        : 2;  /**< Internal state */
	uint64_t major                        : 3;  /**< Internal state */
#else
	uint64_t major                        : 3;
	uint64_t minor                        : 2;
	uint64_t wait                         : 1;
	uint64_t qid_base                     : 8;
	uint64_t qid_off                      : 4;
	uint64_t qid_off_max                  : 4;
	uint64_t qcb_ridx                     : 5;
	uint64_t qos                          : 3;
	uint64_t static_p                     : 1;
	uint64_t active                       : 1;
	uint64_t chk_mode                     : 1;
	uint64_t chk_once                     : 1;
	uint64_t init_dwrite                  : 1;
	uint64_t dread_sop                    : 1;
	uint64_t uid                          : 3;
	uint64_t cmnd_off                     : 6;
	uint64_t cmnd_siz                     : 16;
	uint64_t cmnd_segs                    : 3;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug4_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t curr_siz                     : 8;  /**< Internal state */
	uint64_t curr_off                     : 16; /**< Internal state */
	uint64_t cmnd_segs                    : 6;  /**< Internal state */
	uint64_t cmnd_siz                     : 16; /**< Internal state */
	uint64_t cmnd_off                     : 6;  /**< Internal state */
	uint64_t uid                          : 2;  /**< Internal state */
	uint64_t dread_sop                    : 1;  /**< Internal state */
	uint64_t init_dwrite                  : 1;  /**< Internal state */
	uint64_t chk_once                     : 1;  /**< Internal state */
	uint64_t chk_mode                     : 1;  /**< Internal state */
	uint64_t wait                         : 1;  /**< Internal state */
	uint64_t minor                        : 2;  /**< Internal state */
	uint64_t major                        : 3;  /**< Internal state */
#else
	uint64_t major                        : 3;
	uint64_t minor                        : 2;
	uint64_t wait                         : 1;
	uint64_t chk_mode                     : 1;
	uint64_t chk_once                     : 1;
	uint64_t init_dwrite                  : 1;
	uint64_t dread_sop                    : 1;
	uint64_t uid                          : 2;
	uint64_t cmnd_off                     : 6;
	uint64_t cmnd_siz                     : 16;
	uint64_t cmnd_segs                    : 6;
	uint64_t curr_off                     : 16;
	uint64_t curr_siz                     : 8;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug4_cn52xx     cn52xxp1;
	struct cvmx_pko_mem_debug4_cn52xx     cn56xx;
	struct cvmx_pko_mem_debug4_cn52xx     cn56xxp1;
	struct cvmx_pko_mem_debug4_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug4_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug4_cn52xx     cn61xx;
	struct cvmx_pko_mem_debug4_cn52xx     cn63xx;
	struct cvmx_pko_mem_debug4_cn52xx     cn63xxp1;
	struct cvmx_pko_mem_debug4_cn52xx     cn66xx;
	struct cvmx_pko_mem_debug4_cn52xx     cn68xx;
	struct cvmx_pko_mem_debug4_cn52xx     cn68xxp1;
	struct cvmx_pko_mem_debug4_cn52xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug4 cvmx_pko_mem_debug4_t;

/**
 * cvmx_pko_mem_debug5
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.state[127:64]
 * This CSR is a memory of 12 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug5 {
	uint64_t u64;
	struct cvmx_pko_mem_debug5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug5_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dwri_mod                     : 1;  /**< Dwrite mod */
	uint64_t dwri_sop                     : 1;  /**< Dwrite sop needed */
	uint64_t dwri_len                     : 1;  /**< Dwrite len */
	uint64_t dwri_cnt                     : 13; /**< Dwrite count */
	uint64_t cmnd_siz                     : 16; /**< Copy of cmnd.size */
	uint64_t uid                          : 1;  /**< UID */
	uint64_t xfer_wor                     : 1;  /**< Transfer work needed */
	uint64_t xfer_dwr                     : 1;  /**< Transfer dwrite needed */
	uint64_t cbuf_fre                     : 1;  /**< Cbuf needs free */
	uint64_t reserved_27_27               : 1;
	uint64_t chk_mode                     : 1;  /**< Checksum mode */
	uint64_t active                       : 1;  /**< Port is active */
	uint64_t qos                          : 3;  /**< Current QOS round */
	uint64_t qcb_ridx                     : 5;  /**< Buffer read  index for QCB */
	uint64_t qid_off                      : 3;  /**< Offset to be added to QID_BASE for current queue */
	uint64_t qid_base                     : 7;  /**< Absolute QID of the queue array base = &QUEUES[0] */
	uint64_t wait                         : 1;  /**< State wait when set */
	uint64_t minor                        : 2;  /**< State minor code */
	uint64_t major                        : 4;  /**< State major code */
#else
	uint64_t major                        : 4;
	uint64_t minor                        : 2;
	uint64_t wait                         : 1;
	uint64_t qid_base                     : 7;
	uint64_t qid_off                      : 3;
	uint64_t qcb_ridx                     : 5;
	uint64_t qos                          : 3;
	uint64_t active                       : 1;
	uint64_t chk_mode                     : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t cbuf_fre                     : 1;
	uint64_t xfer_dwr                     : 1;
	uint64_t xfer_wor                     : 1;
	uint64_t uid                          : 1;
	uint64_t cmnd_siz                     : 16;
	uint64_t dwri_cnt                     : 13;
	uint64_t dwri_len                     : 1;
	uint64_t dwri_sop                     : 1;
	uint64_t dwri_mod                     : 1;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug5_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug5_cn30xx     cn38xx;
	struct cvmx_pko_mem_debug5_cn30xx     cn38xxp2;
	struct cvmx_pko_mem_debug5_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t curr_ptr                     : 29; /**< Internal state */
	uint64_t curr_siz                     : 16; /**< Internal state */
	uint64_t curr_off                     : 16; /**< Internal state */
	uint64_t cmnd_segs                    : 3;  /**< Internal state */
#else
	uint64_t cmnd_segs                    : 3;
	uint64_t curr_off                     : 16;
	uint64_t curr_siz                     : 16;
	uint64_t curr_ptr                     : 29;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug5_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t nxt_inflt                    : 6;  /**< Internal state */
	uint64_t curr_ptr                     : 40; /**< Internal state */
	uint64_t curr_siz                     : 8;  /**< Internal state */
#else
	uint64_t curr_siz                     : 8;
	uint64_t curr_ptr                     : 40;
	uint64_t nxt_inflt                    : 6;
	uint64_t reserved_54_63               : 10;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug5_cn52xx     cn52xxp1;
	struct cvmx_pko_mem_debug5_cn52xx     cn56xx;
	struct cvmx_pko_mem_debug5_cn52xx     cn56xxp1;
	struct cvmx_pko_mem_debug5_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug5_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug5_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t ptp                          : 1;  /**< Internal state */
	uint64_t major_3                      : 1;  /**< Internal state */
	uint64_t nxt_inflt                    : 6;  /**< Internal state */
	uint64_t curr_ptr                     : 40; /**< Internal state */
	uint64_t curr_siz                     : 8;  /**< Internal state */
#else
	uint64_t curr_siz                     : 8;
	uint64_t curr_ptr                     : 40;
	uint64_t nxt_inflt                    : 6;
	uint64_t major_3                      : 1;
	uint64_t ptp                          : 1;
	uint64_t reserved_56_63               : 8;
#endif
	} cn61xx;
	struct cvmx_pko_mem_debug5_cn61xx     cn63xx;
	struct cvmx_pko_mem_debug5_cn61xx     cn63xxp1;
	struct cvmx_pko_mem_debug5_cn61xx     cn66xx;
	struct cvmx_pko_mem_debug5_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_57_63               : 7;
	uint64_t uid_2                        : 1;  /**< Internal state */
	uint64_t ptp                          : 1;  /**< Internal state */
	uint64_t major_3                      : 1;  /**< Internal state */
	uint64_t nxt_inflt                    : 6;  /**< Internal state */
	uint64_t curr_ptr                     : 40; /**< Internal state */
	uint64_t curr_siz                     : 8;  /**< Internal state */
#else
	uint64_t curr_siz                     : 8;
	uint64_t curr_ptr                     : 40;
	uint64_t nxt_inflt                    : 6;
	uint64_t major_3                      : 1;
	uint64_t ptp                          : 1;
	uint64_t uid_2                        : 1;
	uint64_t reserved_57_63               : 7;
#endif
	} cn68xx;
	struct cvmx_pko_mem_debug5_cn68xx     cn68xxp1;
	struct cvmx_pko_mem_debug5_cn61xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug5 cvmx_pko_mem_debug5_t;

/**
 * cvmx_pko_mem_debug6
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko_prt_psb.port[63:0]
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug6 {
	uint64_t u64;
	struct cvmx_pko_mem_debug6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t qid_offres                   : 4;  /**< Internal state */
	uint64_t qid_offths                   : 4;  /**< Internal state */
	uint64_t preempter                    : 1;  /**< Internal state */
	uint64_t preemptee                    : 1;  /**< Internal state */
	uint64_t preempted                    : 1;  /**< Internal state */
	uint64_t active                       : 1;  /**< Internal state */
	uint64_t statc                        : 1;  /**< Internal state */
	uint64_t qos                          : 3;  /**< Internal state */
	uint64_t qcb_ridx                     : 5;  /**< Internal state */
	uint64_t qid_offmax                   : 4;  /**< Internal state */
	uint64_t reserved_0_11                : 12;
#else
	uint64_t reserved_0_11                : 12;
	uint64_t qid_offmax                   : 4;
	uint64_t qcb_ridx                     : 5;
	uint64_t qos                          : 3;
	uint64_t statc                        : 1;
	uint64_t active                       : 1;
	uint64_t preempted                    : 1;
	uint64_t preemptee                    : 1;
	uint64_t preempter                    : 1;
	uint64_t qid_offths                   : 4;
	uint64_t qid_offres                   : 4;
	uint64_t reserved_37_63               : 27;
#endif
	} s;
	struct cvmx_pko_mem_debug6_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t qid_offm                     : 3;  /**< Qid offset max */
	uint64_t static_p                     : 1;  /**< Static port when set */
	uint64_t work_min                     : 3;  /**< Work minor */
	uint64_t dwri_chk                     : 1;  /**< Dwrite checksum mode */
	uint64_t dwri_uid                     : 1;  /**< Dwrite UID */
	uint64_t dwri_mod                     : 2;  /**< Dwrite mod */
#else
	uint64_t dwri_mod                     : 2;
	uint64_t dwri_uid                     : 1;
	uint64_t dwri_chk                     : 1;
	uint64_t work_min                     : 3;
	uint64_t static_p                     : 1;
	uint64_t qid_offm                     : 3;
	uint64_t reserved_11_63               : 53;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug6_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug6_cn30xx     cn38xx;
	struct cvmx_pko_mem_debug6_cn30xx     cn38xxp2;
	struct cvmx_pko_mem_debug6_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t curr_ptr                     : 11; /**< Internal state */
#else
	uint64_t curr_ptr                     : 11;
	uint64_t reserved_11_63               : 53;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug6_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t qid_offres                   : 4;  /**< Internal state */
	uint64_t qid_offths                   : 4;  /**< Internal state */
	uint64_t preempter                    : 1;  /**< Internal state */
	uint64_t preemptee                    : 1;  /**< Internal state */
	uint64_t preempted                    : 1;  /**< Internal state */
	uint64_t active                       : 1;  /**< Internal state */
	uint64_t statc                        : 1;  /**< Internal state */
	uint64_t qos                          : 3;  /**< Internal state */
	uint64_t qcb_ridx                     : 5;  /**< Internal state */
	uint64_t qid_offmax                   : 4;  /**< Internal state */
	uint64_t qid_off                      : 4;  /**< Internal state */
	uint64_t qid_base                     : 8;  /**< Internal state */
#else
	uint64_t qid_base                     : 8;
	uint64_t qid_off                      : 4;
	uint64_t qid_offmax                   : 4;
	uint64_t qcb_ridx                     : 5;
	uint64_t qos                          : 3;
	uint64_t statc                        : 1;
	uint64_t active                       : 1;
	uint64_t preempted                    : 1;
	uint64_t preemptee                    : 1;
	uint64_t preempter                    : 1;
	uint64_t qid_offths                   : 4;
	uint64_t qid_offres                   : 4;
	uint64_t reserved_37_63               : 27;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug6_cn52xx     cn52xxp1;
	struct cvmx_pko_mem_debug6_cn52xx     cn56xx;
	struct cvmx_pko_mem_debug6_cn52xx     cn56xxp1;
	struct cvmx_pko_mem_debug6_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug6_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug6_cn52xx     cn61xx;
	struct cvmx_pko_mem_debug6_cn52xx     cn63xx;
	struct cvmx_pko_mem_debug6_cn52xx     cn63xxp1;
	struct cvmx_pko_mem_debug6_cn52xx     cn66xx;
	struct cvmx_pko_mem_debug6_cn52xx     cn68xx;
	struct cvmx_pko_mem_debug6_cn52xx     cn68xxp1;
	struct cvmx_pko_mem_debug6_cn52xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug6 cvmx_pko_mem_debug6_t;

/**
 * cvmx_pko_mem_debug7
 *
 * Notes:
 * Internal per-queue state intended for debug use only - pko_prt_qsb.state[63:0]
 * This CSR is a memory of 256 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug7 {
	uint64_t u64;
	struct cvmx_pko_mem_debug7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_mem_debug7_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t dwb                          : 9;  /**< Calculated DWB count used for free operation */
	uint64_t start                        : 33; /**< Calculated start address used for free operation */
	uint64_t size                         : 16; /**< Packet length in bytes */
#else
	uint64_t size                         : 16;
	uint64_t start                        : 33;
	uint64_t dwb                          : 9;
	uint64_t reserved_58_63               : 6;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug7_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug7_cn30xx     cn38xx;
	struct cvmx_pko_mem_debug7_cn30xx     cn38xxp2;
	struct cvmx_pko_mem_debug7_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t qos                          : 5;  /**< QOS mask to enable the queue when set */
	uint64_t tail                         : 1;  /**< This queue is the last (tail) in the queue array */
	uint64_t buf_siz                      : 13; /**< Command buffer remaining size in words */
	uint64_t buf_ptr                      : 33; /**< Command word pointer */
	uint64_t qcb_widx                     : 6;  /**< Buffer write index for QCB */
	uint64_t qcb_ridx                     : 6;  /**< Buffer read  index for QCB */
#else
	uint64_t qcb_ridx                     : 6;
	uint64_t qcb_widx                     : 6;
	uint64_t buf_ptr                      : 33;
	uint64_t buf_siz                      : 13;
	uint64_t tail                         : 1;
	uint64_t qos                          : 5;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug7_cn50xx     cn52xx;
	struct cvmx_pko_mem_debug7_cn50xx     cn52xxp1;
	struct cvmx_pko_mem_debug7_cn50xx     cn56xx;
	struct cvmx_pko_mem_debug7_cn50xx     cn56xxp1;
	struct cvmx_pko_mem_debug7_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug7_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug7_cn50xx     cn61xx;
	struct cvmx_pko_mem_debug7_cn50xx     cn63xx;
	struct cvmx_pko_mem_debug7_cn50xx     cn63xxp1;
	struct cvmx_pko_mem_debug7_cn50xx     cn66xx;
	struct cvmx_pko_mem_debug7_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t qos                          : 3;  /**< QOS mask to enable the queue when set */
	uint64_t tail                         : 1;  /**< This queue is the last (tail) in the queue array */
	uint64_t buf_siz                      : 13; /**< Command buffer remaining size in words */
	uint64_t buf_ptr                      : 33; /**< Command word pointer */
	uint64_t qcb_widx                     : 7;  /**< Buffer write index for QCB */
	uint64_t qcb_ridx                     : 7;  /**< Buffer read  index for QCB */
#else
	uint64_t qcb_ridx                     : 7;
	uint64_t qcb_widx                     : 7;
	uint64_t buf_ptr                      : 33;
	uint64_t buf_siz                      : 13;
	uint64_t tail                         : 1;
	uint64_t qos                          : 3;
#endif
	} cn68xx;
	struct cvmx_pko_mem_debug7_cn68xx     cn68xxp1;
	struct cvmx_pko_mem_debug7_cn50xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug7 cvmx_pko_mem_debug7_t;

/**
 * cvmx_pko_mem_debug8
 *
 * Notes:
 * Internal per-queue state intended for debug use only - pko_prt_qsb.state[91:64]
 * This CSR is a memory of 256 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug8 {
	uint64_t u64;
	struct cvmx_pko_mem_debug8_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t tail                         : 1;  /**< This queue is the last (tail) in the queue array */
	uint64_t buf_siz                      : 13; /**< Command buffer remaining size in words */
	uint64_t reserved_0_44                : 45;
#else
	uint64_t reserved_0_44                : 45;
	uint64_t buf_siz                      : 13;
	uint64_t tail                         : 1;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_pko_mem_debug8_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t qos                          : 5;  /**< QOS mask to enable the queue when set */
	uint64_t tail                         : 1;  /**< This queue is the last (tail) in the queue array */
	uint64_t buf_siz                      : 13; /**< Command buffer remaining size in words */
	uint64_t buf_ptr                      : 33; /**< Command word pointer */
	uint64_t qcb_widx                     : 6;  /**< Buffer write index for QCB */
	uint64_t qcb_ridx                     : 6;  /**< Buffer read  index for QCB */
#else
	uint64_t qcb_ridx                     : 6;
	uint64_t qcb_widx                     : 6;
	uint64_t buf_ptr                      : 33;
	uint64_t buf_siz                      : 13;
	uint64_t tail                         : 1;
	uint64_t qos                          : 5;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug8_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug8_cn30xx     cn38xx;
	struct cvmx_pko_mem_debug8_cn30xx     cn38xxp2;
	struct cvmx_pko_mem_debug8_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t doorbell                     : 20; /**< Doorbell count */
	uint64_t reserved_6_7                 : 2;
	uint64_t static_p                     : 1;  /**< Static priority */
	uint64_t s_tail                       : 1;  /**< Static tail */
	uint64_t static_q                     : 1;  /**< Static priority */
	uint64_t qos                          : 3;  /**< QOS mask to enable the queue when set */
#else
	uint64_t qos                          : 3;
	uint64_t static_q                     : 1;
	uint64_t s_tail                       : 1;
	uint64_t static_p                     : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t doorbell                     : 20;
	uint64_t reserved_28_63               : 36;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug8_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t preempter                    : 1;  /**< Preempter */
	uint64_t doorbell                     : 20; /**< Doorbell count */
	uint64_t reserved_7_7                 : 1;
	uint64_t preemptee                    : 1;  /**< Preemptee */
	uint64_t static_p                     : 1;  /**< Static priority */
	uint64_t s_tail                       : 1;  /**< Static tail */
	uint64_t static_q                     : 1;  /**< Static priority */
	uint64_t qos                          : 3;  /**< QOS mask to enable the queue when set */
#else
	uint64_t qos                          : 3;
	uint64_t static_q                     : 1;
	uint64_t s_tail                       : 1;
	uint64_t static_p                     : 1;
	uint64_t preemptee                    : 1;
	uint64_t reserved_7_7                 : 1;
	uint64_t doorbell                     : 20;
	uint64_t preempter                    : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn52xx;
	struct cvmx_pko_mem_debug8_cn52xx     cn52xxp1;
	struct cvmx_pko_mem_debug8_cn52xx     cn56xx;
	struct cvmx_pko_mem_debug8_cn52xx     cn56xxp1;
	struct cvmx_pko_mem_debug8_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug8_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug8_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_42_63               : 22;
	uint64_t qid_qqos                     : 8;  /**< QOS_MASK */
	uint64_t reserved_33_33               : 1;
	uint64_t qid_idx                      : 4;  /**< IDX */
	uint64_t preempter                    : 1;  /**< Preempter */
	uint64_t doorbell                     : 20; /**< Doorbell count */
	uint64_t reserved_7_7                 : 1;
	uint64_t preemptee                    : 1;  /**< Preemptee */
	uint64_t static_p                     : 1;  /**< Static priority */
	uint64_t s_tail                       : 1;  /**< Static tail */
	uint64_t static_q                     : 1;  /**< Static priority */
	uint64_t qos                          : 3;  /**< QOS mask to enable the queue when set */
#else
	uint64_t qos                          : 3;
	uint64_t static_q                     : 1;
	uint64_t s_tail                       : 1;
	uint64_t static_p                     : 1;
	uint64_t preemptee                    : 1;
	uint64_t reserved_7_7                 : 1;
	uint64_t doorbell                     : 20;
	uint64_t preempter                    : 1;
	uint64_t qid_idx                      : 4;
	uint64_t reserved_33_33               : 1;
	uint64_t qid_qqos                     : 8;
	uint64_t reserved_42_63               : 22;
#endif
	} cn61xx;
	struct cvmx_pko_mem_debug8_cn52xx     cn63xx;
	struct cvmx_pko_mem_debug8_cn52xx     cn63xxp1;
	struct cvmx_pko_mem_debug8_cn61xx     cn66xx;
	struct cvmx_pko_mem_debug8_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t preempter                    : 1;  /**< Preempter */
	uint64_t doorbell                     : 20; /**< Doorbell count */
	uint64_t reserved_9_15                : 7;
	uint64_t preemptee                    : 1;  /**< Preemptee */
	uint64_t static_p                     : 1;  /**< Static priority */
	uint64_t s_tail                       : 1;  /**< Static tail */
	uint64_t static_q                     : 1;  /**< Static priority */
	uint64_t qos                          : 5;  /**< QOS mask to enable the queue when set */
#else
	uint64_t qos                          : 5;
	uint64_t static_q                     : 1;
	uint64_t s_tail                       : 1;
	uint64_t static_p                     : 1;
	uint64_t preemptee                    : 1;
	uint64_t reserved_9_15                : 7;
	uint64_t doorbell                     : 20;
	uint64_t preempter                    : 1;
	uint64_t reserved_37_63               : 27;
#endif
	} cn68xx;
	struct cvmx_pko_mem_debug8_cn68xx     cn68xxp1;
	struct cvmx_pko_mem_debug8_cn61xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug8 cvmx_pko_mem_debug8_t;

/**
 * cvmx_pko_mem_debug9
 *
 * Notes:
 * Internal per-port state intended for debug use only - pko.dat.ptr.ptrs0, pko.dat.ptr.ptrs3
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.
 */
union cvmx_pko_mem_debug9 {
	uint64_t u64;
	struct cvmx_pko_mem_debug9_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ptrs0                        : 17; /**< Internal state */
	uint64_t reserved_0_31                : 32;
#else
	uint64_t reserved_0_31                : 32;
	uint64_t ptrs0                        : 17;
	uint64_t reserved_49_63               : 15;
#endif
	} s;
	struct cvmx_pko_mem_debug9_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t doorbell                     : 20; /**< Doorbell count */
	uint64_t reserved_5_7                 : 3;
	uint64_t s_tail                       : 1;  /**< reads as zero (S_TAIL cannot be read) */
	uint64_t static_q                     : 1;  /**< reads as zero (STATIC_Q cannot be read) */
	uint64_t qos                          : 3;  /**< QOS mask to enable the queue when set */
#else
	uint64_t qos                          : 3;
	uint64_t static_q                     : 1;
	uint64_t s_tail                       : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t doorbell                     : 20;
	uint64_t reserved_28_63               : 36;
#endif
	} cn30xx;
	struct cvmx_pko_mem_debug9_cn30xx     cn31xx;
	struct cvmx_pko_mem_debug9_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t doorbell                     : 20; /**< Doorbell count */
	uint64_t reserved_6_7                 : 2;
	uint64_t static_p                     : 1;  /**< Static priority (port) */
	uint64_t s_tail                       : 1;  /**< Static tail */
	uint64_t static_q                     : 1;  /**< Static priority */
	uint64_t qos                          : 3;  /**< QOS mask to enable the queue when set */
#else
	uint64_t qos                          : 3;
	uint64_t static_q                     : 1;
	uint64_t s_tail                       : 1;
	uint64_t static_p                     : 1;
	uint64_t reserved_6_7                 : 2;
	uint64_t doorbell                     : 20;
	uint64_t reserved_28_63               : 36;
#endif
	} cn38xx;
	struct cvmx_pko_mem_debug9_cn38xx     cn38xxp2;
	struct cvmx_pko_mem_debug9_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t ptrs0                        : 17; /**< Internal state */
	uint64_t reserved_17_31               : 15;
	uint64_t ptrs3                        : 17; /**< Internal state */
#else
	uint64_t ptrs3                        : 17;
	uint64_t reserved_17_31               : 15;
	uint64_t ptrs0                        : 17;
	uint64_t reserved_49_63               : 15;
#endif
	} cn50xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn52xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn52xxp1;
	struct cvmx_pko_mem_debug9_cn50xx     cn56xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn56xxp1;
	struct cvmx_pko_mem_debug9_cn50xx     cn58xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn58xxp1;
	struct cvmx_pko_mem_debug9_cn50xx     cn61xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn63xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn63xxp1;
	struct cvmx_pko_mem_debug9_cn50xx     cn66xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn68xx;
	struct cvmx_pko_mem_debug9_cn50xx     cn68xxp1;
	struct cvmx_pko_mem_debug9_cn50xx     cnf71xx;
};
typedef union cvmx_pko_mem_debug9 cvmx_pko_mem_debug9_t;

/**
 * cvmx_pko_mem_iport_ptrs
 *
 * Notes:
 * This CSR is a memory of 128 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  The index to this CSR is an IPORT.  A read of any
 * entry that has not been previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_iport_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_iport_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_63_63               : 1;
	uint64_t crc                          : 1;  /**< Set if this IPID uses CRC */
	uint64_t static_p                     : 1;  /**< Set if this IPID has static priority */
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t min_pkt                      : 3;  /**< Min packet size specified by PKO_REG_MIN_PKT[MIN_PKT] */
	uint64_t reserved_31_49               : 19;
	uint64_t pipe                         : 7;  /**< The PKO pipe or loopback port
                                                         When INT != PIP/IPD:
                                                          PIPE is the PKO pipe to which this port is mapped
                                                          All used PKO-internal ports that map to the same
                                                          PIPE must also map to the same INT and EID in
                                                          this case.
                                                         When INT == PIP/IPD:
                                                          PIPE must be in the range
                                                                  0..PKO_REG_LOOPBACK[NUM_PORTS]-1
                                                          in this case and selects one of the loopback
                                                          ports. */
	uint64_t reserved_21_23               : 3;
	uint64_t intr                         : 5;  /**< The interface to which this port is mapped
                                                         All used PKO-internal ports that map to the same EID
                                                         must also map to the same INT. All used PKO-internal
                                                         ports that map to the same INT must also map to the
                                                         same EID.
                                                         Encoding:
                                                           0 = GMX0 XAUI/DXAUI/RXAUI0 or SGMII0
                                                           1 = GMX0 SGMII1
                                                           2 = GMX0 SGMII2
                                                           3 = GMX0 SGMII3
                                                           4 = GMX1 RXAUI
                                                           8 = GMX2 XAUI/DXAUI or SGMII0
                                                           9 = GMX2 SGMII1
                                                          10 = GMX2 SGMII2
                                                          11 = GMX2 SGMII3
                                                          12 = GMX3 XAUI/DXAUI or SGMII0
                                                          13 = GMX3 SGMII1
                                                          14 = GMX3 SGMII2
                                                          15 = GMX3 SGMII3
                                                          16 = GMX4 XAUI/DXAUI or SGMII0
                                                          17 = GMX4 SGMII1
                                                          18 = GMX4 SGMII2
                                                          19 = GMX4 SGMII3
                                                          28 = ILK interface 0
                                                          29 = ILK interface 1
                                                          30 = DPI
                                                          31 = PIP/IPD
                                                          others = reserved */
	uint64_t reserved_13_15               : 3;
	uint64_t eid                          : 5;  /**< Engine ID to which this port is mapped
                                                         EID==31 can be used with unused PKO-internal ports.
                                                         Otherwise, 0-19 are legal EID values. */
	uint64_t reserved_7_7                 : 1;
	uint64_t ipid                         : 7;  /**< PKO-internal Port ID to be accessed */
#else
	uint64_t ipid                         : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t eid                          : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t intr                         : 5;
	uint64_t reserved_21_23               : 3;
	uint64_t pipe                         : 7;
	uint64_t reserved_31_49               : 19;
	uint64_t min_pkt                      : 3;
	uint64_t qos_mask                     : 8;
	uint64_t static_p                     : 1;
	uint64_t crc                          : 1;
	uint64_t reserved_63_63               : 1;
#endif
	} s;
	struct cvmx_pko_mem_iport_ptrs_s      cn68xx;
	struct cvmx_pko_mem_iport_ptrs_s      cn68xxp1;
};
typedef union cvmx_pko_mem_iport_ptrs cvmx_pko_mem_iport_ptrs_t;

/**
 * cvmx_pko_mem_iport_qos
 *
 * Notes:
 * Sets the QOS mask, per port.  These QOS_MASK bits are logically and physically the same QOS_MASK
 * bits in PKO_MEM_IPORT_PTRS.  This CSR address allows the QOS_MASK bits to be written during PKO
 * operation without affecting any other port state.  The engine to which port PID is mapped is engine
 * EID.  Note that the port to engine mapping must be the same as was previously programmed via the
 * PKO_MEM_IPORT_PTRS CSR.
 * This CSR is a memory of 128 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  The index to this CSR is an IPORT.  A read of
 * any entry that has not been previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_iport_qos {
	uint64_t u64;
	struct cvmx_pko_mem_iport_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_61_63               : 3;
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t reserved_13_52               : 40;
	uint64_t eid                          : 5;  /**< Engine ID to which this port is mapped */
	uint64_t reserved_7_7                 : 1;
	uint64_t ipid                         : 7;  /**< PKO-internal Port ID */
#else
	uint64_t ipid                         : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t eid                          : 5;
	uint64_t reserved_13_52               : 40;
	uint64_t qos_mask                     : 8;
	uint64_t reserved_61_63               : 3;
#endif
	} s;
	struct cvmx_pko_mem_iport_qos_s       cn68xx;
	struct cvmx_pko_mem_iport_qos_s       cn68xxp1;
};
typedef union cvmx_pko_mem_iport_qos cvmx_pko_mem_iport_qos_t;

/**
 * cvmx_pko_mem_iqueue_ptrs
 *
 * Notes:
 * Sets the queue to port mapping and the initial command buffer pointer, per queue.  Unused queues must
 * set BUF_PTR=0.  Each queue may map to at most one port.  No more than 32 queues may map to a port.
 * The set of queues that is mapped to a port must be a contiguous array of queues.  The port to which
 * queue QID is mapped is port IPID.  The index of queue QID in port IPID's queue list is IDX.  The last
 * queue in port IPID's queue array must have its TAIL bit set.
 * STATIC_Q marks queue QID as having static priority.  STATIC_P marks the port IPID to which QID is
 * mapped as having at least one queue with static priority.  If any QID that maps to IPID has static
 * priority, then all QID that map to IPID must have STATIC_P set.  Queues marked as static priority
 * must be contiguous and begin at IDX 0.  The last queue that is marked as having static priority
 * must have its S_TAIL bit set.
 * This CSR is a memory of 256 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  The index to this CSR is an IQUEUE.  A read of any
 * entry that has not been previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_iqueue_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_iqueue_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t s_tail                       : 1;  /**< Set if this QID is the tail of the static queues */
	uint64_t static_p                     : 1;  /**< Set if any QID in this IPID has static priority */
	uint64_t static_q                     : 1;  /**< Set if this QID has static priority */
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t buf_ptr                      : 31; /**< Command buffer pointer[37:7] */
	uint64_t tail                         : 1;  /**< Set if this QID is the tail of the queue array */
	uint64_t index                        : 5;  /**< Index (distance from head) in the queue array */
	uint64_t reserved_15_15               : 1;
	uint64_t ipid                         : 7;  /**< PKO-Internal Port ID to which this queue is mapped */
	uint64_t qid                          : 8;  /**< Queue ID */
#else
	uint64_t qid                          : 8;
	uint64_t ipid                         : 7;
	uint64_t reserved_15_15               : 1;
	uint64_t index                        : 5;
	uint64_t tail                         : 1;
	uint64_t buf_ptr                      : 31;
	uint64_t qos_mask                     : 8;
	uint64_t static_q                     : 1;
	uint64_t static_p                     : 1;
	uint64_t s_tail                       : 1;
#endif
	} s;
	struct cvmx_pko_mem_iqueue_ptrs_s     cn68xx;
	struct cvmx_pko_mem_iqueue_ptrs_s     cn68xxp1;
};
typedef union cvmx_pko_mem_iqueue_ptrs cvmx_pko_mem_iqueue_ptrs_t;

/**
 * cvmx_pko_mem_iqueue_qos
 *
 * Notes:
 * Sets the QOS mask, per queue.  These QOS_MASK bits are logically and physically the same QOS_MASK
 * bits in PKO_MEM_IQUEUE_PTRS.  This CSR address allows the QOS_MASK bits to be written during PKO
 * operation without affecting any other queue state.  The port to which queue QID is mapped is port
 * IPID.  Note that the queue to port mapping must be the same as was previously programmed via the
 * PKO_MEM_IQUEUE_PTRS CSR.
 * This CSR is a memory of 256 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  The index to this CSR is an IQUEUE.  A read of any
 * entry that has not been previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_iqueue_qos {
	uint64_t u64;
	struct cvmx_pko_mem_iqueue_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_61_63               : 3;
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t reserved_15_52               : 38;
	uint64_t ipid                         : 7;  /**< PKO-Internal Port ID to which this queue is mapped */
	uint64_t qid                          : 8;  /**< Queue ID */
#else
	uint64_t qid                          : 8;
	uint64_t ipid                         : 7;
	uint64_t reserved_15_52               : 38;
	uint64_t qos_mask                     : 8;
	uint64_t reserved_61_63               : 3;
#endif
	} s;
	struct cvmx_pko_mem_iqueue_qos_s      cn68xx;
	struct cvmx_pko_mem_iqueue_qos_s      cn68xxp1;
};
typedef union cvmx_pko_mem_iqueue_qos cvmx_pko_mem_iqueue_qos_t;

/**
 * cvmx_pko_mem_port_ptrs
 *
 * Notes:
 * Sets the port to engine mapping, per port.  Ports marked as static priority need not be contiguous,
 * but they must be the lowest numbered PIDs mapped to this EID and must have QOS_MASK=0xff.  If EID==8
 * or EID==9, then PID[1:0] is used to direct the packet to the correct port on that interface.
 * EID==15 can be used for unused PKO-internal ports.
 * BP_PORT==63 means that the PKO-internal port is not backpressured.
 * BP_PORTs are assumed to belong to an interface as follows:
 *   46 <= BP_PORT < 48 -> srio       interface 3
 *   44 <= BP_PORT < 46 -> srio       interface 2
 *   42 <= BP_PORT < 44 -> srio       interface 1
 *   40 <= BP_PORT < 42 -> srio       interface 0
 *   36 <= BP_PORT < 40 -> loopback   interface
 *   32 <= BP_PORT < 36 -> PCIe       interface
 *   0  <= BP_PORT < 16 -> SGMII/Xaui interface 0
 *
 * Note that the SRIO interfaces do not actually provide backpressure.  Thus, ports that use
 * 40 <= BP_PORT < 48 for backpressure will never be backpressured.
 *
 * The reset configuration is the following:
 *   PID EID(ext port) BP_PORT QOS_MASK STATIC_P
 *   -------------------------------------------
 *     0   0( 0)             0     0xff        0
 *     1   1( 1)             1     0xff        0
 *     2   2( 2)             2     0xff        0
 *     3   3( 3)             3     0xff        0
 *     4   0( 0)             4     0xff        0
 *     5   1( 1)             5     0xff        0
 *     6   2( 2)             6     0xff        0
 *     7   3( 3)             7     0xff        0
 *     8   0( 0)             8     0xff        0
 *     9   1( 1)             9     0xff        0
 *    10   2( 2)            10     0xff        0
 *    11   3( 3)            11     0xff        0
 *    12   0( 0)            12     0xff        0
 *    13   1( 1)            13     0xff        0
 *    14   2( 2)            14     0xff        0
 *    15   3( 3)            15     0xff        0
 *   -------------------------------------------
 *    16   4(16)            16     0xff        0
 *    17   5(17)            17     0xff        0
 *    18   6(18)            18     0xff        0
 *    19   7(19)            19     0xff        0
 *    20   4(16)            20     0xff        0
 *    21   5(17)            21     0xff        0
 *    22   6(18)            22     0xff        0
 *    23   7(19)            23     0xff        0
 *    24   4(16)            24     0xff        0
 *    25   5(17)            25     0xff        0
 *    26   6(18)            26     0xff        0
 *    27   7(19)            27     0xff        0
 *    28   4(16)            28     0xff        0
 *    29   5(17)            29     0xff        0
 *    30   6(18)            30     0xff        0
 *    31   7(19)            31     0xff        0
 *   -------------------------------------------
 *    32   8(32)            32     0xff        0
 *    33   8(33)            33     0xff        0
 *    34   8(34)            34     0xff        0
 *    35   8(35)            35     0xff        0
 *   -------------------------------------------
 *    36   9(36)            36     0xff        0
 *    37   9(37)            37     0xff        0
 *    38   9(38)            38     0xff        0
 *    39   9(39)            39     0xff        0
 *
 * This CSR is a memory of 48 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_port_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_port_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_62_63               : 2;
	uint64_t static_p                     : 1;  /**< Set if this PID has static priority */
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t reserved_16_52               : 37;
	uint64_t bp_port                      : 6;  /**< PID listens to BP_PORT for per-packet backpressure
                                                         Legal BP_PORTs: 0-15, 32-47, 63 (63 means no BP) */
	uint64_t eid                          : 4;  /**< Engine ID to which this port is mapped
                                                         Legal EIDs: 0-3, 8-13, 15 (15 only if port not used) */
	uint64_t pid                          : 6;  /**< Port ID[5:0] */
#else
	uint64_t pid                          : 6;
	uint64_t eid                          : 4;
	uint64_t bp_port                      : 6;
	uint64_t reserved_16_52               : 37;
	uint64_t qos_mask                     : 8;
	uint64_t static_p                     : 1;
	uint64_t reserved_62_63               : 2;
#endif
	} s;
	struct cvmx_pko_mem_port_ptrs_s       cn52xx;
	struct cvmx_pko_mem_port_ptrs_s       cn52xxp1;
	struct cvmx_pko_mem_port_ptrs_s       cn56xx;
	struct cvmx_pko_mem_port_ptrs_s       cn56xxp1;
	struct cvmx_pko_mem_port_ptrs_s       cn61xx;
	struct cvmx_pko_mem_port_ptrs_s       cn63xx;
	struct cvmx_pko_mem_port_ptrs_s       cn63xxp1;
	struct cvmx_pko_mem_port_ptrs_s       cn66xx;
	struct cvmx_pko_mem_port_ptrs_s       cnf71xx;
};
typedef union cvmx_pko_mem_port_ptrs cvmx_pko_mem_port_ptrs_t;

/**
 * cvmx_pko_mem_port_qos
 *
 * Notes:
 * Sets the QOS mask, per port.  These QOS_MASK bits are logically and physically the same QOS_MASK
 * bits in PKO_MEM_PORT_PTRS.  This CSR address allows the QOS_MASK bits to be written during PKO
 * operation without affecting any other port state.  The engine to which port PID is mapped is engine
 * EID.  Note that the port to engine mapping must be the same as was previously programmed via the
 * PKO_MEM_PORT_PTRS CSR.
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_port_qos {
	uint64_t u64;
	struct cvmx_pko_mem_port_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_61_63               : 3;
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t reserved_10_52               : 43;
	uint64_t eid                          : 4;  /**< Engine ID to which this port is mapped
                                                         Legal EIDs: 0-3, 8-11 */
	uint64_t pid                          : 6;  /**< Port ID[5:0] */
#else
	uint64_t pid                          : 6;
	uint64_t eid                          : 4;
	uint64_t reserved_10_52               : 43;
	uint64_t qos_mask                     : 8;
	uint64_t reserved_61_63               : 3;
#endif
	} s;
	struct cvmx_pko_mem_port_qos_s        cn52xx;
	struct cvmx_pko_mem_port_qos_s        cn52xxp1;
	struct cvmx_pko_mem_port_qos_s        cn56xx;
	struct cvmx_pko_mem_port_qos_s        cn56xxp1;
	struct cvmx_pko_mem_port_qos_s        cn61xx;
	struct cvmx_pko_mem_port_qos_s        cn63xx;
	struct cvmx_pko_mem_port_qos_s        cn63xxp1;
	struct cvmx_pko_mem_port_qos_s        cn66xx;
	struct cvmx_pko_mem_port_qos_s        cnf71xx;
};
typedef union cvmx_pko_mem_port_qos cvmx_pko_mem_port_qos_t;

/**
 * cvmx_pko_mem_port_rate0
 *
 * Notes:
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_port_rate0 {
	uint64_t u64;
	struct cvmx_pko_mem_port_rate0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_51_63               : 13;
	uint64_t rate_word                    : 19; /**< Rate limiting adder per 8 byte */
	uint64_t rate_pkt                     : 24; /**< Rate limiting adder per packet */
	uint64_t reserved_7_7                 : 1;
	uint64_t pid                          : 7;  /**< Port ID[5:0] */
#else
	uint64_t pid                          : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t rate_pkt                     : 24;
	uint64_t rate_word                    : 19;
	uint64_t reserved_51_63               : 13;
#endif
	} s;
	struct cvmx_pko_mem_port_rate0_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_51_63               : 13;
	uint64_t rate_word                    : 19; /**< Rate limiting adder per 8 byte */
	uint64_t rate_pkt                     : 24; /**< Rate limiting adder per packet */
	uint64_t reserved_6_7                 : 2;
	uint64_t pid                          : 6;  /**< Port ID[5:0] */
#else
	uint64_t pid                          : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t rate_pkt                     : 24;
	uint64_t rate_word                    : 19;
	uint64_t reserved_51_63               : 13;
#endif
	} cn52xx;
	struct cvmx_pko_mem_port_rate0_cn52xx cn52xxp1;
	struct cvmx_pko_mem_port_rate0_cn52xx cn56xx;
	struct cvmx_pko_mem_port_rate0_cn52xx cn56xxp1;
	struct cvmx_pko_mem_port_rate0_cn52xx cn61xx;
	struct cvmx_pko_mem_port_rate0_cn52xx cn63xx;
	struct cvmx_pko_mem_port_rate0_cn52xx cn63xxp1;
	struct cvmx_pko_mem_port_rate0_cn52xx cn66xx;
	struct cvmx_pko_mem_port_rate0_s      cn68xx;
	struct cvmx_pko_mem_port_rate0_s      cn68xxp1;
	struct cvmx_pko_mem_port_rate0_cn52xx cnf71xx;
};
typedef union cvmx_pko_mem_port_rate0 cvmx_pko_mem_port_rate0_t;

/**
 * cvmx_pko_mem_port_rate1
 *
 * Notes:
 * Writing PKO_MEM_PORT_RATE1[PID,RATE_LIM] has the side effect of setting the corresponding
 * accumulator to zero.
 * This CSR is a memory of 44 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_port_rate1 {
	uint64_t u64;
	struct cvmx_pko_mem_port_rate1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t rate_lim                     : 24; /**< Rate limiting accumulator limit */
	uint64_t reserved_7_7                 : 1;
	uint64_t pid                          : 7;  /**< Port ID[5:0] */
#else
	uint64_t pid                          : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t rate_lim                     : 24;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pko_mem_port_rate1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t rate_lim                     : 24; /**< Rate limiting accumulator limit */
	uint64_t reserved_6_7                 : 2;
	uint64_t pid                          : 6;  /**< Port ID[5:0] */
#else
	uint64_t pid                          : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t rate_lim                     : 24;
	uint64_t reserved_32_63               : 32;
#endif
	} cn52xx;
	struct cvmx_pko_mem_port_rate1_cn52xx cn52xxp1;
	struct cvmx_pko_mem_port_rate1_cn52xx cn56xx;
	struct cvmx_pko_mem_port_rate1_cn52xx cn56xxp1;
	struct cvmx_pko_mem_port_rate1_cn52xx cn61xx;
	struct cvmx_pko_mem_port_rate1_cn52xx cn63xx;
	struct cvmx_pko_mem_port_rate1_cn52xx cn63xxp1;
	struct cvmx_pko_mem_port_rate1_cn52xx cn66xx;
	struct cvmx_pko_mem_port_rate1_s      cn68xx;
	struct cvmx_pko_mem_port_rate1_s      cn68xxp1;
	struct cvmx_pko_mem_port_rate1_cn52xx cnf71xx;
};
typedef union cvmx_pko_mem_port_rate1 cvmx_pko_mem_port_rate1_t;

/**
 * cvmx_pko_mem_queue_ptrs
 *
 * Notes:
 * Sets the queue to port mapping and the initial command buffer pointer, per queue
 * Each queue may map to at most one port.  No more than 16 queues may map to a port.  The set of
 * queues that is mapped to a port must be a contiguous array of queues.  The port to which queue QID
 * is mapped is port PID.  The index of queue QID in port PID's queue list is IDX.  The last queue in
 * port PID's queue array must have its TAIL bit set.  Unused queues must be mapped to port 63.
 * STATIC_Q marks queue QID as having static priority.  STATIC_P marks the port PID to which QID is
 * mapped as having at least one queue with static priority.  If any QID that maps to PID has static
 * priority, then all QID that map to PID must have STATIC_P set.  Queues marked as static priority
 * must be contiguous and begin at IDX 0.  The last queue that is marked as having static priority
 * must have its S_TAIL bit set.
 * This CSR is a memory of 256 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_queue_ptrs {
	uint64_t u64;
	struct cvmx_pko_mem_queue_ptrs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t s_tail                       : 1;  /**< Set if this QID is the tail of the static queues */
	uint64_t static_p                     : 1;  /**< Set if any QID in this PID has static priority */
	uint64_t static_q                     : 1;  /**< Set if this QID has static priority */
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t buf_ptr                      : 36; /**< Command buffer pointer, <23:17> MBZ */
	uint64_t tail                         : 1;  /**< Set if this QID is the tail of the queue array */
	uint64_t index                        : 3;  /**< Index[2:0] (distance from head) in the queue array */
	uint64_t port                         : 6;  /**< Port ID to which this queue is mapped */
	uint64_t queue                        : 7;  /**< Queue ID[6:0] */
#else
	uint64_t queue                        : 7;
	uint64_t port                         : 6;
	uint64_t index                        : 3;
	uint64_t tail                         : 1;
	uint64_t buf_ptr                      : 36;
	uint64_t qos_mask                     : 8;
	uint64_t static_q                     : 1;
	uint64_t static_p                     : 1;
	uint64_t s_tail                       : 1;
#endif
	} s;
	struct cvmx_pko_mem_queue_ptrs_s      cn30xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn31xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn38xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn38xxp2;
	struct cvmx_pko_mem_queue_ptrs_s      cn50xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn52xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn52xxp1;
	struct cvmx_pko_mem_queue_ptrs_s      cn56xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn56xxp1;
	struct cvmx_pko_mem_queue_ptrs_s      cn58xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn58xxp1;
	struct cvmx_pko_mem_queue_ptrs_s      cn61xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn63xx;
	struct cvmx_pko_mem_queue_ptrs_s      cn63xxp1;
	struct cvmx_pko_mem_queue_ptrs_s      cn66xx;
	struct cvmx_pko_mem_queue_ptrs_s      cnf71xx;
};
typedef union cvmx_pko_mem_queue_ptrs cvmx_pko_mem_queue_ptrs_t;

/**
 * cvmx_pko_mem_queue_qos
 *
 * Notes:
 * Sets the QOS mask, per queue.  These QOS_MASK bits are logically and physically the same QOS_MASK
 * bits in PKO_MEM_QUEUE_PTRS.  This CSR address allows the QOS_MASK bits to be written during PKO
 * operation without affecting any other queue state.  The port to which queue QID is mapped is port
 * PID.  Note that the queue to port mapping must be the same as was previously programmed via the
 * PKO_MEM_QUEUE_PTRS CSR.
 * This CSR is a memory of 256 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  A read of any entry that has not been
 * previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_queue_qos {
	uint64_t u64;
	struct cvmx_pko_mem_queue_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_61_63               : 3;
	uint64_t qos_mask                     : 8;  /**< Mask to control priority across 8 QOS rounds */
	uint64_t reserved_13_52               : 40;
	uint64_t pid                          : 6;  /**< Port ID to which this queue is mapped */
	uint64_t qid                          : 7;  /**< Queue ID */
#else
	uint64_t qid                          : 7;
	uint64_t pid                          : 6;
	uint64_t reserved_13_52               : 40;
	uint64_t qos_mask                     : 8;
	uint64_t reserved_61_63               : 3;
#endif
	} s;
	struct cvmx_pko_mem_queue_qos_s       cn30xx;
	struct cvmx_pko_mem_queue_qos_s       cn31xx;
	struct cvmx_pko_mem_queue_qos_s       cn38xx;
	struct cvmx_pko_mem_queue_qos_s       cn38xxp2;
	struct cvmx_pko_mem_queue_qos_s       cn50xx;
	struct cvmx_pko_mem_queue_qos_s       cn52xx;
	struct cvmx_pko_mem_queue_qos_s       cn52xxp1;
	struct cvmx_pko_mem_queue_qos_s       cn56xx;
	struct cvmx_pko_mem_queue_qos_s       cn56xxp1;
	struct cvmx_pko_mem_queue_qos_s       cn58xx;
	struct cvmx_pko_mem_queue_qos_s       cn58xxp1;
	struct cvmx_pko_mem_queue_qos_s       cn61xx;
	struct cvmx_pko_mem_queue_qos_s       cn63xx;
	struct cvmx_pko_mem_queue_qos_s       cn63xxp1;
	struct cvmx_pko_mem_queue_qos_s       cn66xx;
	struct cvmx_pko_mem_queue_qos_s       cnf71xx;
};
typedef union cvmx_pko_mem_queue_qos cvmx_pko_mem_queue_qos_t;

/**
 * cvmx_pko_mem_throttle_int
 *
 * Notes:
 * Writing PACKET and WORD with 0 resets both counts for INT to 0 rather than add 0.
 * Otherwise, writes to this CSR add to the existing WORD/PACKET counts for the interface INT.
 *
 * PKO tracks the number of (8-byte) WORD's and PACKET's in-flight (sum total in both PKO
 * and the interface MAC) on the interface. (When PKO first selects a packet from a PKO queue, it
 * increments the counts appropriately. When the interface MAC has (largely) completed sending
 * the words/packet, PKO decrements the count appropriately.) When PKO_REG_FLAGS[ENA_THROTTLE]
 * is set and the most-significant bit of the WORD or packet count for a interface is set,
 * PKO will not transfer any packets over the interface. Software can limit the amount of
 * packet data and/or the number of packets that OCTEON can send out the chip after receiving backpressure
 * from the interface/pipe via these per-pipe throttle counts when PKO_REG_FLAGS[ENA_THROTTLE]=1.
 * For example, to limit the number of packets outstanding in the interface to N, preset PACKET for
 * the pipe to the value 0x20-N (0x20 is the smallest PACKET value with the most-significant bit set).
 *
 * This CSR is a memory of 32 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  The index to this CSR is an INTERFACE.  A read of any
 * entry that has not been previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_throttle_int {
	uint64_t u64;
	struct cvmx_pko_mem_throttle_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t word                         : 15; /**< On a write, the amount to add to the interface
                                                         throttle word count selected by INT. On a read,
                                                         returns the current value of the interface throttle
                                                         word count selected by PKO_REG_READ_IDX[IDX]. */
	uint64_t reserved_14_31               : 18;
	uint64_t packet                       : 6;  /**< On a write, the amount to add to the interface
                                                         throttle packet count selected by INT. On a read,
                                                         returns the current value of the interface throttle
                                                         packet count selected by PKO_REG_READ_IDX[IDX]. */
	uint64_t reserved_5_7                 : 3;
	uint64_t intr                         : 5;  /**< Selected interface for writes. Undefined on a read.
                                                         See PKO_MEM_IPORT_PTRS[INT] for encoding. */
#else
	uint64_t intr                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t packet                       : 6;
	uint64_t reserved_14_31               : 18;
	uint64_t word                         : 15;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_pko_mem_throttle_int_s    cn68xx;
	struct cvmx_pko_mem_throttle_int_s    cn68xxp1;
};
typedef union cvmx_pko_mem_throttle_int cvmx_pko_mem_throttle_int_t;

/**
 * cvmx_pko_mem_throttle_pipe
 *
 * Notes:
 * Writing PACKET and WORD with 0 resets both counts for PIPE to 0 rather than add 0.
 * Otherwise, writes to this CSR add to the existing WORD/PACKET counts for the PKO pipe PIPE.
 *
 * PKO tracks the number of (8-byte) WORD's and PACKET's in-flight (sum total in both PKO
 * and the interface MAC) on the pipe. (When PKO first selects a packet from a PKO queue, it
 * increments the counts appropriately. When the interface MAC has (largely) completed sending
 * the words/packet, PKO decrements the count appropriately.) When PKO_REG_FLAGS[ENA_THROTTLE]
 * is set and the most-significant bit of the WORD or packet count for a PKO pipe is set,
 * PKO will not transfer any packets over the PKO pipe. Software can limit the amount of
 * packet data and/or the number of packets that OCTEON can send out the chip after receiving backpressure
 * from the interface/pipe via these per-pipe throttle counts when PKO_REG_FLAGS[ENA_THROTTLE]=1.
 * For example, to limit the number of packets outstanding in the pipe to N, preset PACKET for
 * the pipe to the value 0x20-N (0x20 is the smallest PACKET value with the most-significant bit set).
 *
 * This CSR is a memory of 128 entries, and thus, the PKO_REG_READ_IDX CSR must be written before any
 * CSR read operations to this address can be performed.  The index to this CSR is a PIPE.  A read of any
 * entry that has not been previously written is illegal and will result in unpredictable CSR read data.
 */
union cvmx_pko_mem_throttle_pipe {
	uint64_t u64;
	struct cvmx_pko_mem_throttle_pipe_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t word                         : 15; /**< On a write, the amount to add to the pipe throttle
                                                         word count selected by PIPE. On a read, returns
                                                         the current value of the pipe throttle word count
                                                         selected by PKO_REG_READ_IDX[IDX]. */
	uint64_t reserved_14_31               : 18;
	uint64_t packet                       : 6;  /**< On a write, the amount to add to the pipe throttle
                                                         packet count selected by PIPE. On a read, returns
                                                         the current value of the pipe throttle packet count
                                                         selected by PKO_REG_READ_IDX[IDX]. */
	uint64_t reserved_7_7                 : 1;
	uint64_t pipe                         : 7;  /**< Selected PKO pipe for writes. Undefined on a read. */
#else
	uint64_t pipe                         : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t packet                       : 6;
	uint64_t reserved_14_31               : 18;
	uint64_t word                         : 15;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_pko_mem_throttle_pipe_s   cn68xx;
	struct cvmx_pko_mem_throttle_pipe_s   cn68xxp1;
};
typedef union cvmx_pko_mem_throttle_pipe cvmx_pko_mem_throttle_pipe_t;

/**
 * cvmx_pko_reg_bist_result
 *
 * Notes:
 * Access to the internal BiST results
 * Each bit is the BiST result of an individual memory (per bit, 0=pass and 1=fail).
 */
union cvmx_pko_reg_bist_result {
	uint64_t u64;
	struct cvmx_pko_reg_bist_result_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pko_reg_bist_result_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t psb2                         : 5;  /**< BiST result of the PSB   memories (0=pass, !0=fail) */
	uint64_t count                        : 1;  /**< BiST result of the COUNT memories (0=pass, !0=fail) */
	uint64_t rif                          : 1;  /**< BiST result of the RIF   memories (0=pass, !0=fail) */
	uint64_t wif                          : 1;  /**< BiST result of the WIF   memories (0=pass, !0=fail) */
	uint64_t ncb                          : 1;  /**< BiST result of the NCB   memories (0=pass, !0=fail) */
	uint64_t out                          : 1;  /**< BiST result of the OUT   memories (0=pass, !0=fail) */
	uint64_t crc                          : 1;  /**< BiST result of the CRC   memories (0=pass, !0=fail) */
	uint64_t chk                          : 1;  /**< BiST result of the CHK   memories (0=pass, !0=fail) */
	uint64_t qsb                          : 2;  /**< BiST result of the QSB   memories (0=pass, !0=fail) */
	uint64_t qcb                          : 2;  /**< BiST result of the QCB   memories (0=pass, !0=fail) */
	uint64_t pdb                          : 4;  /**< BiST result of the PDB   memories (0=pass, !0=fail) */
	uint64_t psb                          : 7;  /**< BiST result of the PSB   memories (0=pass, !0=fail) */
#else
	uint64_t psb                          : 7;
	uint64_t pdb                          : 4;
	uint64_t qcb                          : 2;
	uint64_t qsb                          : 2;
	uint64_t chk                          : 1;
	uint64_t crc                          : 1;
	uint64_t out                          : 1;
	uint64_t ncb                          : 1;
	uint64_t wif                          : 1;
	uint64_t rif                          : 1;
	uint64_t count                        : 1;
	uint64_t psb2                         : 5;
	uint64_t reserved_27_63               : 37;
#endif
	} cn30xx;
	struct cvmx_pko_reg_bist_result_cn30xx cn31xx;
	struct cvmx_pko_reg_bist_result_cn30xx cn38xx;
	struct cvmx_pko_reg_bist_result_cn30xx cn38xxp2;
	struct cvmx_pko_reg_bist_result_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t csr                          : 1;  /**< BiST result of CSR      memories (0=pass, !0=fail) */
	uint64_t iob                          : 1;  /**< BiST result of IOB      memories (0=pass, !0=fail) */
	uint64_t out_crc                      : 1;  /**< BiST result of OUT_CRC  memories (0=pass, !0=fail) */
	uint64_t out_ctl                      : 3;  /**< BiST result of OUT_CTL  memories (0=pass, !0=fail) */
	uint64_t out_sta                      : 1;  /**< BiST result of OUT_STA  memories (0=pass, !0=fail) */
	uint64_t out_wif                      : 1;  /**< BiST result of OUT_WIF  memories (0=pass, !0=fail) */
	uint64_t prt_chk                      : 3;  /**< BiST result of PRT_CHK  memories (0=pass, !0=fail) */
	uint64_t prt_nxt                      : 1;  /**< BiST result of PRT_NXT  memories (0=pass, !0=fail) */
	uint64_t prt_psb                      : 6;  /**< BiST result of PRT_PSB  memories (0=pass, !0=fail) */
	uint64_t ncb_inb                      : 2;  /**< BiST result of NCB_INB  memories (0=pass, !0=fail) */
	uint64_t prt_qcb                      : 2;  /**< BiST result of PRT_QCB  memories (0=pass, !0=fail) */
	uint64_t prt_qsb                      : 3;  /**< BiST result of PRT_QSB  memories (0=pass, !0=fail) */
	uint64_t dat_dat                      : 4;  /**< BiST result of DAT_DAT  memories (0=pass, !0=fail) */
	uint64_t dat_ptr                      : 4;  /**< BiST result of DAT_PTR  memories (0=pass, !0=fail) */
#else
	uint64_t dat_ptr                      : 4;
	uint64_t dat_dat                      : 4;
	uint64_t prt_qsb                      : 3;
	uint64_t prt_qcb                      : 2;
	uint64_t ncb_inb                      : 2;
	uint64_t prt_psb                      : 6;
	uint64_t prt_nxt                      : 1;
	uint64_t prt_chk                      : 3;
	uint64_t out_wif                      : 1;
	uint64_t out_sta                      : 1;
	uint64_t out_ctl                      : 3;
	uint64_t out_crc                      : 1;
	uint64_t iob                          : 1;
	uint64_t csr                          : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} cn50xx;
	struct cvmx_pko_reg_bist_result_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t csr                          : 1;  /**< BiST result of CSR      memories (0=pass, !0=fail) */
	uint64_t iob                          : 1;  /**< BiST result of IOB      memories (0=pass, !0=fail) */
	uint64_t out_dat                      : 1;  /**< BiST result of OUT_DAT  memories (0=pass, !0=fail) */
	uint64_t out_ctl                      : 3;  /**< BiST result of OUT_CTL  memories (0=pass, !0=fail) */
	uint64_t out_sta                      : 1;  /**< BiST result of OUT_STA  memories (0=pass, !0=fail) */
	uint64_t out_wif                      : 1;  /**< BiST result of OUT_WIF  memories (0=pass, !0=fail) */
	uint64_t prt_chk                      : 3;  /**< BiST result of PRT_CHK  memories (0=pass, !0=fail) */
	uint64_t prt_nxt                      : 1;  /**< BiST result of PRT_NXT  memories (0=pass, !0=fail) */
	uint64_t prt_psb                      : 8;  /**< BiST result of PRT_PSB  memories (0=pass, !0=fail) */
	uint64_t ncb_inb                      : 2;  /**< BiST result of NCB_INB  memories (0=pass, !0=fail) */
	uint64_t prt_qcb                      : 2;  /**< BiST result of PRT_QCB  memories (0=pass, !0=fail) */
	uint64_t prt_qsb                      : 3;  /**< BiST result of PRT_QSB  memories (0=pass, !0=fail) */
	uint64_t prt_ctl                      : 2;  /**< BiST result of PRT_CTL  memories (0=pass, !0=fail) */
	uint64_t dat_dat                      : 2;  /**< BiST result of DAT_DAT  memories (0=pass, !0=fail) */
	uint64_t dat_ptr                      : 4;  /**< BiST result of DAT_PTR  memories (0=pass, !0=fail) */
#else
	uint64_t dat_ptr                      : 4;
	uint64_t dat_dat                      : 2;
	uint64_t prt_ctl                      : 2;
	uint64_t prt_qsb                      : 3;
	uint64_t prt_qcb                      : 2;
	uint64_t ncb_inb                      : 2;
	uint64_t prt_psb                      : 8;
	uint64_t prt_nxt                      : 1;
	uint64_t prt_chk                      : 3;
	uint64_t out_wif                      : 1;
	uint64_t out_sta                      : 1;
	uint64_t out_ctl                      : 3;
	uint64_t out_dat                      : 1;
	uint64_t iob                          : 1;
	uint64_t csr                          : 1;
	uint64_t reserved_35_63               : 29;
#endif
	} cn52xx;
	struct cvmx_pko_reg_bist_result_cn52xx cn52xxp1;
	struct cvmx_pko_reg_bist_result_cn52xx cn56xx;
	struct cvmx_pko_reg_bist_result_cn52xx cn56xxp1;
	struct cvmx_pko_reg_bist_result_cn50xx cn58xx;
	struct cvmx_pko_reg_bist_result_cn50xx cn58xxp1;
	struct cvmx_pko_reg_bist_result_cn52xx cn61xx;
	struct cvmx_pko_reg_bist_result_cn52xx cn63xx;
	struct cvmx_pko_reg_bist_result_cn52xx cn63xxp1;
	struct cvmx_pko_reg_bist_result_cn52xx cn66xx;
	struct cvmx_pko_reg_bist_result_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t crc                          : 1;  /**< BiST result of CRC      memories (0=pass, !0=fail) */
	uint64_t csr                          : 1;  /**< BiST result of CSR      memories (0=pass, !0=fail) */
	uint64_t iob                          : 1;  /**< BiST result of IOB      memories (0=pass, !0=fail) */
	uint64_t out_dat                      : 1;  /**< BiST result of OUT_DAT  memories (0=pass, !0=fail) */
	uint64_t reserved_31_31               : 1;
	uint64_t out_ctl                      : 2;  /**< BiST result of OUT_CTL  memories (0=pass, !0=fail) */
	uint64_t out_sta                      : 1;  /**< BiST result of OUT_STA  memories (0=pass, !0=fail) */
	uint64_t out_wif                      : 1;  /**< BiST result of OUT_WIF  memories (0=pass, !0=fail) */
	uint64_t prt_chk                      : 3;  /**< BiST result of PRT_CHK  memories (0=pass, !0=fail) */
	uint64_t prt_nxt                      : 1;  /**< BiST result of PRT_NXT  memories (0=pass, !0=fail) */
	uint64_t prt_psb7                     : 1;  /**< BiST result of PRT_PSB  memories (0=pass, !0=fail) */
	uint64_t reserved_21_21               : 1;
	uint64_t prt_psb                      : 6;  /**< BiST result of PRT_PSB  memories (0=pass, !0=fail) */
	uint64_t ncb_inb                      : 2;  /**< BiST result of NCB_INB  memories (0=pass, !0=fail) */
	uint64_t prt_qcb                      : 2;  /**< BiST result of PRT_QCB  memories (0=pass, !0=fail) */
	uint64_t prt_qsb                      : 3;  /**< BiST result of PRT_QSB  memories (0=pass, !0=fail) */
	uint64_t prt_ctl                      : 2;  /**< BiST result of PRT_CTL  memories (0=pass, !0=fail) */
	uint64_t dat_dat                      : 2;  /**< BiST result of DAT_DAT  memories (0=pass, !0=fail) */
	uint64_t dat_ptr                      : 4;  /**< BiST result of DAT_PTR  memories (0=pass, !0=fail) */
#else
	uint64_t dat_ptr                      : 4;
	uint64_t dat_dat                      : 2;
	uint64_t prt_ctl                      : 2;
	uint64_t prt_qsb                      : 3;
	uint64_t prt_qcb                      : 2;
	uint64_t ncb_inb                      : 2;
	uint64_t prt_psb                      : 6;
	uint64_t reserved_21_21               : 1;
	uint64_t prt_psb7                     : 1;
	uint64_t prt_nxt                      : 1;
	uint64_t prt_chk                      : 3;
	uint64_t out_wif                      : 1;
	uint64_t out_sta                      : 1;
	uint64_t out_ctl                      : 2;
	uint64_t reserved_31_31               : 1;
	uint64_t out_dat                      : 1;
	uint64_t iob                          : 1;
	uint64_t csr                          : 1;
	uint64_t crc                          : 1;
	uint64_t reserved_36_63               : 28;
#endif
	} cn68xx;
	struct cvmx_pko_reg_bist_result_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t csr                          : 1;  /**< BiST result of CSR      memories (0=pass, !0=fail) */
	uint64_t iob                          : 1;  /**< BiST result of IOB      memories (0=pass, !0=fail) */
	uint64_t out_dat                      : 1;  /**< BiST result of OUT_DAT  memories (0=pass, !0=fail) */
	uint64_t reserved_31_31               : 1;
	uint64_t out_ctl                      : 2;  /**< BiST result of OUT_CTL  memories (0=pass, !0=fail) */
	uint64_t out_sta                      : 1;  /**< BiST result of OUT_STA  memories (0=pass, !0=fail) */
	uint64_t out_wif                      : 1;  /**< BiST result of OUT_WIF  memories (0=pass, !0=fail) */
	uint64_t prt_chk                      : 3;  /**< BiST result of PRT_CHK  memories (0=pass, !0=fail) */
	uint64_t prt_nxt                      : 1;  /**< BiST result of PRT_NXT  memories (0=pass, !0=fail) */
	uint64_t prt_psb7                     : 1;  /**< BiST result of PRT_PSB  memories (0=pass, !0=fail) */
	uint64_t reserved_21_21               : 1;
	uint64_t prt_psb                      : 6;  /**< BiST result of PRT_PSB  memories (0=pass, !0=fail) */
	uint64_t ncb_inb                      : 2;  /**< BiST result of NCB_INB  memories (0=pass, !0=fail) */
	uint64_t prt_qcb                      : 2;  /**< BiST result of PRT_QCB  memories (0=pass, !0=fail) */
	uint64_t prt_qsb                      : 3;  /**< BiST result of PRT_QSB  memories (0=pass, !0=fail) */
	uint64_t prt_ctl                      : 2;  /**< BiST result of PRT_CTL  memories (0=pass, !0=fail) */
	uint64_t dat_dat                      : 2;  /**< BiST result of DAT_DAT  memories (0=pass, !0=fail) */
	uint64_t dat_ptr                      : 4;  /**< BiST result of DAT_PTR  memories (0=pass, !0=fail) */
#else
	uint64_t dat_ptr                      : 4;
	uint64_t dat_dat                      : 2;
	uint64_t prt_ctl                      : 2;
	uint64_t prt_qsb                      : 3;
	uint64_t prt_qcb                      : 2;
	uint64_t ncb_inb                      : 2;
	uint64_t prt_psb                      : 6;
	uint64_t reserved_21_21               : 1;
	uint64_t prt_psb7                     : 1;
	uint64_t prt_nxt                      : 1;
	uint64_t prt_chk                      : 3;
	uint64_t out_wif                      : 1;
	uint64_t out_sta                      : 1;
	uint64_t out_ctl                      : 2;
	uint64_t reserved_31_31               : 1;
	uint64_t out_dat                      : 1;
	uint64_t iob                          : 1;
	uint64_t csr                          : 1;
	uint64_t reserved_35_63               : 29;
#endif
	} cn68xxp1;
	struct cvmx_pko_reg_bist_result_cn52xx cnf71xx;
};
typedef union cvmx_pko_reg_bist_result cvmx_pko_reg_bist_result_t;

/**
 * cvmx_pko_reg_cmd_buf
 *
 * Notes:
 * Sets the command buffer parameters
 * The size of the command buffer segments is measured in uint64s.  The pool specifies (1 of 8 free
 * lists to be used when freeing command buffer segments.
 */
union cvmx_pko_reg_cmd_buf {
	uint64_t u64;
	struct cvmx_pko_reg_cmd_buf_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t pool                         : 3;  /**< Free list used to free command buffer segments */
	uint64_t reserved_13_19               : 7;
	uint64_t size                         : 13; /**< Number of uint64s per command buffer segment */
#else
	uint64_t size                         : 13;
	uint64_t reserved_13_19               : 7;
	uint64_t pool                         : 3;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_pko_reg_cmd_buf_s         cn30xx;
	struct cvmx_pko_reg_cmd_buf_s         cn31xx;
	struct cvmx_pko_reg_cmd_buf_s         cn38xx;
	struct cvmx_pko_reg_cmd_buf_s         cn38xxp2;
	struct cvmx_pko_reg_cmd_buf_s         cn50xx;
	struct cvmx_pko_reg_cmd_buf_s         cn52xx;
	struct cvmx_pko_reg_cmd_buf_s         cn52xxp1;
	struct cvmx_pko_reg_cmd_buf_s         cn56xx;
	struct cvmx_pko_reg_cmd_buf_s         cn56xxp1;
	struct cvmx_pko_reg_cmd_buf_s         cn58xx;
	struct cvmx_pko_reg_cmd_buf_s         cn58xxp1;
	struct cvmx_pko_reg_cmd_buf_s         cn61xx;
	struct cvmx_pko_reg_cmd_buf_s         cn63xx;
	struct cvmx_pko_reg_cmd_buf_s         cn63xxp1;
	struct cvmx_pko_reg_cmd_buf_s         cn66xx;
	struct cvmx_pko_reg_cmd_buf_s         cn68xx;
	struct cvmx_pko_reg_cmd_buf_s         cn68xxp1;
	struct cvmx_pko_reg_cmd_buf_s         cnf71xx;
};
typedef union cvmx_pko_reg_cmd_buf cvmx_pko_reg_cmd_buf_t;

/**
 * cvmx_pko_reg_crc_ctl#
 *
 * Notes:
 * Controls datapath reflection when calculating CRC
 *
 */
union cvmx_pko_reg_crc_ctlx {
	uint64_t u64;
	struct cvmx_pko_reg_crc_ctlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t invres                       : 1;  /**< Invert the result */
	uint64_t refin                        : 1;  /**< Reflect the bits in each byte.
                                                          Byte order does not change.
                                                         - 0: CRC is calculated MSB to LSB
                                                         - 1: CRC is calculated MLB to MSB */
#else
	uint64_t refin                        : 1;
	uint64_t invres                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pko_reg_crc_ctlx_s        cn38xx;
	struct cvmx_pko_reg_crc_ctlx_s        cn38xxp2;
	struct cvmx_pko_reg_crc_ctlx_s        cn58xx;
	struct cvmx_pko_reg_crc_ctlx_s        cn58xxp1;
};
typedef union cvmx_pko_reg_crc_ctlx cvmx_pko_reg_crc_ctlx_t;

/**
 * cvmx_pko_reg_crc_enable
 *
 * Notes:
 * Enables CRC for the GMX ports.
 *
 */
union cvmx_pko_reg_crc_enable {
	uint64_t u64;
	struct cvmx_pko_reg_crc_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t enable                       : 32; /**< Mask for ports 31-0 to enable CRC
                                                         Mask bit==0 means CRC not enabled
                                                         Mask bit==1 means CRC     enabled
                                                         Note that CRC should be enabled only when using SPI4.2 */
#else
	uint64_t enable                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pko_reg_crc_enable_s      cn38xx;
	struct cvmx_pko_reg_crc_enable_s      cn38xxp2;
	struct cvmx_pko_reg_crc_enable_s      cn58xx;
	struct cvmx_pko_reg_crc_enable_s      cn58xxp1;
};
typedef union cvmx_pko_reg_crc_enable cvmx_pko_reg_crc_enable_t;

/**
 * cvmx_pko_reg_crc_iv#
 *
 * Notes:
 * Determines the IV used by the CRC algorithm
 * * PKO_CRC_IV
 *  PKO_CRC_IV controls the initial state of the CRC algorithm.  Octane can
 *  support a wide range of CRC algorithms and as such, the IV must be
 *  carefully constructed to meet the specific algorithm.  The code below
 *  determines the value to program into Octane based on the algorthim's IV
 *  and width.  In the case of Octane, the width should always be 32.
 *
 *  PKO_CRC_IV0 sets the IV for ports 0-15 while PKO_CRC_IV1 sets the IV for
 *  ports 16-31.
 *
 *   @verbatim
 *   unsigned octane_crc_iv(unsigned algorithm_iv, unsigned poly, unsigned w)
 *   [
 *     int i;
 *     int doit;
 *     unsigned int current_val = algorithm_iv;
 *
 *     for(i = 0; i < w; i++) [
 *       doit = current_val & 0x1;
 *
 *       if(doit) current_val ^= poly;
 *       assert(!(current_val & 0x1));
 *
 *       current_val = (current_val >> 1) | (doit << (w-1));
 *     ]
 *
 *     return current_val;
 *   ]
 *   @endverbatim
 */
union cvmx_pko_reg_crc_ivx {
	uint64_t u64;
	struct cvmx_pko_reg_crc_ivx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iv                           : 32; /**< IV used by the CRC algorithm.  Default is FCS32. */
#else
	uint64_t iv                           : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pko_reg_crc_ivx_s         cn38xx;
	struct cvmx_pko_reg_crc_ivx_s         cn38xxp2;
	struct cvmx_pko_reg_crc_ivx_s         cn58xx;
	struct cvmx_pko_reg_crc_ivx_s         cn58xxp1;
};
typedef union cvmx_pko_reg_crc_ivx cvmx_pko_reg_crc_ivx_t;

/**
 * cvmx_pko_reg_debug0
 *
 * Notes:
 * Note that this CSR is present only in chip revisions beginning with pass2.
 *
 */
union cvmx_pko_reg_debug0 {
	uint64_t u64;
	struct cvmx_pko_reg_debug0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t asserts                      : 64; /**< Various assertion checks */
#else
	uint64_t asserts                      : 64;
#endif
	} s;
	struct cvmx_pko_reg_debug0_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t asserts                      : 17; /**< Various assertion checks */
#else
	uint64_t asserts                      : 17;
	uint64_t reserved_17_63               : 47;
#endif
	} cn30xx;
	struct cvmx_pko_reg_debug0_cn30xx     cn31xx;
	struct cvmx_pko_reg_debug0_cn30xx     cn38xx;
	struct cvmx_pko_reg_debug0_cn30xx     cn38xxp2;
	struct cvmx_pko_reg_debug0_s          cn50xx;
	struct cvmx_pko_reg_debug0_s          cn52xx;
	struct cvmx_pko_reg_debug0_s          cn52xxp1;
	struct cvmx_pko_reg_debug0_s          cn56xx;
	struct cvmx_pko_reg_debug0_s          cn56xxp1;
	struct cvmx_pko_reg_debug0_s          cn58xx;
	struct cvmx_pko_reg_debug0_s          cn58xxp1;
	struct cvmx_pko_reg_debug0_s          cn61xx;
	struct cvmx_pko_reg_debug0_s          cn63xx;
	struct cvmx_pko_reg_debug0_s          cn63xxp1;
	struct cvmx_pko_reg_debug0_s          cn66xx;
	struct cvmx_pko_reg_debug0_s          cn68xx;
	struct cvmx_pko_reg_debug0_s          cn68xxp1;
	struct cvmx_pko_reg_debug0_s          cnf71xx;
};
typedef union cvmx_pko_reg_debug0 cvmx_pko_reg_debug0_t;

/**
 * cvmx_pko_reg_debug1
 */
union cvmx_pko_reg_debug1 {
	uint64_t u64;
	struct cvmx_pko_reg_debug1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t asserts                      : 64; /**< Various assertion checks */
#else
	uint64_t asserts                      : 64;
#endif
	} s;
	struct cvmx_pko_reg_debug1_s          cn50xx;
	struct cvmx_pko_reg_debug1_s          cn52xx;
	struct cvmx_pko_reg_debug1_s          cn52xxp1;
	struct cvmx_pko_reg_debug1_s          cn56xx;
	struct cvmx_pko_reg_debug1_s          cn56xxp1;
	struct cvmx_pko_reg_debug1_s          cn58xx;
	struct cvmx_pko_reg_debug1_s          cn58xxp1;
	struct cvmx_pko_reg_debug1_s          cn61xx;
	struct cvmx_pko_reg_debug1_s          cn63xx;
	struct cvmx_pko_reg_debug1_s          cn63xxp1;
	struct cvmx_pko_reg_debug1_s          cn66xx;
	struct cvmx_pko_reg_debug1_s          cn68xx;
	struct cvmx_pko_reg_debug1_s          cn68xxp1;
	struct cvmx_pko_reg_debug1_s          cnf71xx;
};
typedef union cvmx_pko_reg_debug1 cvmx_pko_reg_debug1_t;

/**
 * cvmx_pko_reg_debug2
 */
union cvmx_pko_reg_debug2 {
	uint64_t u64;
	struct cvmx_pko_reg_debug2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t asserts                      : 64; /**< Various assertion checks */
#else
	uint64_t asserts                      : 64;
#endif
	} s;
	struct cvmx_pko_reg_debug2_s          cn50xx;
	struct cvmx_pko_reg_debug2_s          cn52xx;
	struct cvmx_pko_reg_debug2_s          cn52xxp1;
	struct cvmx_pko_reg_debug2_s          cn56xx;
	struct cvmx_pko_reg_debug2_s          cn56xxp1;
	struct cvmx_pko_reg_debug2_s          cn58xx;
	struct cvmx_pko_reg_debug2_s          cn58xxp1;
	struct cvmx_pko_reg_debug2_s          cn61xx;
	struct cvmx_pko_reg_debug2_s          cn63xx;
	struct cvmx_pko_reg_debug2_s          cn63xxp1;
	struct cvmx_pko_reg_debug2_s          cn66xx;
	struct cvmx_pko_reg_debug2_s          cn68xx;
	struct cvmx_pko_reg_debug2_s          cn68xxp1;
	struct cvmx_pko_reg_debug2_s          cnf71xx;
};
typedef union cvmx_pko_reg_debug2 cvmx_pko_reg_debug2_t;

/**
 * cvmx_pko_reg_debug3
 */
union cvmx_pko_reg_debug3 {
	uint64_t u64;
	struct cvmx_pko_reg_debug3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t asserts                      : 64; /**< Various assertion checks */
#else
	uint64_t asserts                      : 64;
#endif
	} s;
	struct cvmx_pko_reg_debug3_s          cn50xx;
	struct cvmx_pko_reg_debug3_s          cn52xx;
	struct cvmx_pko_reg_debug3_s          cn52xxp1;
	struct cvmx_pko_reg_debug3_s          cn56xx;
	struct cvmx_pko_reg_debug3_s          cn56xxp1;
	struct cvmx_pko_reg_debug3_s          cn58xx;
	struct cvmx_pko_reg_debug3_s          cn58xxp1;
	struct cvmx_pko_reg_debug3_s          cn61xx;
	struct cvmx_pko_reg_debug3_s          cn63xx;
	struct cvmx_pko_reg_debug3_s          cn63xxp1;
	struct cvmx_pko_reg_debug3_s          cn66xx;
	struct cvmx_pko_reg_debug3_s          cn68xx;
	struct cvmx_pko_reg_debug3_s          cn68xxp1;
	struct cvmx_pko_reg_debug3_s          cnf71xx;
};
typedef union cvmx_pko_reg_debug3 cvmx_pko_reg_debug3_t;

/**
 * cvmx_pko_reg_debug4
 */
union cvmx_pko_reg_debug4 {
	uint64_t u64;
	struct cvmx_pko_reg_debug4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t asserts                      : 64; /**< Various assertion checks */
#else
	uint64_t asserts                      : 64;
#endif
	} s;
	struct cvmx_pko_reg_debug4_s          cn68xx;
	struct cvmx_pko_reg_debug4_s          cn68xxp1;
};
typedef union cvmx_pko_reg_debug4 cvmx_pko_reg_debug4_t;

/**
 * cvmx_pko_reg_engine_inflight
 *
 * Notes:
 * Sets the maximum number of inflight packets, per engine.  Values greater than 4 are illegal.
 * Setting an engine's value to 0 effectively stops the engine.
 */
union cvmx_pko_reg_engine_inflight {
	uint64_t u64;
	struct cvmx_pko_reg_engine_inflight_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t engine15                     : 4;  /**< Maximum number of inflight packets for engine15 */
	uint64_t engine14                     : 4;  /**< Maximum number of inflight packets for engine14 */
	uint64_t engine13                     : 4;  /**< Maximum number of inflight packets for engine13 */
	uint64_t engine12                     : 4;  /**< Maximum number of inflight packets for engine12 */
	uint64_t engine11                     : 4;  /**< Maximum number of inflight packets for engine11 */
	uint64_t engine10                     : 4;  /**< Maximum number of inflight packets for engine10 */
	uint64_t engine9                      : 4;  /**< Maximum number of inflight packets for engine9 */
	uint64_t engine8                      : 4;  /**< Maximum number of inflight packets for engine8 */
	uint64_t engine7                      : 4;  /**< Maximum number of inflight packets for engine7 */
	uint64_t engine6                      : 4;  /**< Maximum number of inflight packets for engine6 */
	uint64_t engine5                      : 4;  /**< Maximum number of inflight packets for engine5 */
	uint64_t engine4                      : 4;  /**< Maximum number of inflight packets for engine4 */
	uint64_t engine3                      : 4;  /**< Maximum number of inflight packets for engine3 */
	uint64_t engine2                      : 4;  /**< Maximum number of inflight packets for engine2 */
	uint64_t engine1                      : 4;  /**< Maximum number of inflight packets for engine1 */
	uint64_t engine0                      : 4;  /**< Maximum number of inflight packets for engine0 */
#else
	uint64_t engine0                      : 4;
	uint64_t engine1                      : 4;
	uint64_t engine2                      : 4;
	uint64_t engine3                      : 4;
	uint64_t engine4                      : 4;
	uint64_t engine5                      : 4;
	uint64_t engine6                      : 4;
	uint64_t engine7                      : 4;
	uint64_t engine8                      : 4;
	uint64_t engine9                      : 4;
	uint64_t engine10                     : 4;
	uint64_t engine11                     : 4;
	uint64_t engine12                     : 4;
	uint64_t engine13                     : 4;
	uint64_t engine14                     : 4;
	uint64_t engine15                     : 4;
#endif
	} s;
	struct cvmx_pko_reg_engine_inflight_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t engine9                      : 4;  /**< Maximum number of inflight packets for engine9 */
	uint64_t engine8                      : 4;  /**< Maximum number of inflight packets for engine8 */
	uint64_t engine7                      : 4;  /**< MBZ */
	uint64_t engine6                      : 4;  /**< MBZ */
	uint64_t engine5                      : 4;  /**< MBZ */
	uint64_t engine4                      : 4;  /**< MBZ */
	uint64_t engine3                      : 4;  /**< Maximum number of inflight packets for engine3 */
	uint64_t engine2                      : 4;  /**< Maximum number of inflight packets for engine2 */
	uint64_t engine1                      : 4;  /**< Maximum number of inflight packets for engine1 */
	uint64_t engine0                      : 4;  /**< Maximum number of inflight packets for engine0 */
#else
	uint64_t engine0                      : 4;
	uint64_t engine1                      : 4;
	uint64_t engine2                      : 4;
	uint64_t engine3                      : 4;
	uint64_t engine4                      : 4;
	uint64_t engine5                      : 4;
	uint64_t engine6                      : 4;
	uint64_t engine7                      : 4;
	uint64_t engine8                      : 4;
	uint64_t engine9                      : 4;
	uint64_t reserved_40_63               : 24;
#endif
	} cn52xx;
	struct cvmx_pko_reg_engine_inflight_cn52xx cn52xxp1;
	struct cvmx_pko_reg_engine_inflight_cn52xx cn56xx;
	struct cvmx_pko_reg_engine_inflight_cn52xx cn56xxp1;
	struct cvmx_pko_reg_engine_inflight_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t engine13                     : 4;  /**< Maximum number of inflight packets for engine13 */
	uint64_t engine12                     : 4;  /**< Maximum number of inflight packets for engine12 */
	uint64_t engine11                     : 4;  /**< Maximum number of inflight packets for engine11 */
	uint64_t engine10                     : 4;  /**< Maximum number of inflight packets for engine10 */
	uint64_t engine9                      : 4;  /**< Maximum number of inflight packets for engine9 */
	uint64_t engine8                      : 4;  /**< Maximum number of inflight packets for engine8 */
	uint64_t engine7                      : 4;  /**< Maximum number of inflight packets for engine7 */
	uint64_t engine6                      : 4;  /**< Maximum number of inflight packets for engine6 */
	uint64_t engine5                      : 4;  /**< Maximum number of inflight packets for engine5 */
	uint64_t engine4                      : 4;  /**< Maximum number of inflight packets for engine4 */
	uint64_t engine3                      : 4;  /**< Maximum number of inflight packets for engine3 */
	uint64_t engine2                      : 4;  /**< Maximum number of inflight packets for engine2 */
	uint64_t engine1                      : 4;  /**< Maximum number of inflight packets for engine1 */
	uint64_t engine0                      : 4;  /**< Maximum number of inflight packets for engine0 */
#else
	uint64_t engine0                      : 4;
	uint64_t engine1                      : 4;
	uint64_t engine2                      : 4;
	uint64_t engine3                      : 4;
	uint64_t engine4                      : 4;
	uint64_t engine5                      : 4;
	uint64_t engine6                      : 4;
	uint64_t engine7                      : 4;
	uint64_t engine8                      : 4;
	uint64_t engine9                      : 4;
	uint64_t engine10                     : 4;
	uint64_t engine11                     : 4;
	uint64_t engine12                     : 4;
	uint64_t engine13                     : 4;
	uint64_t reserved_56_63               : 8;
#endif
	} cn61xx;
	struct cvmx_pko_reg_engine_inflight_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t engine11                     : 4;  /**< Maximum number of inflight packets for engine11 */
	uint64_t engine10                     : 4;  /**< Maximum number of inflight packets for engine10 */
	uint64_t engine9                      : 4;  /**< Maximum number of inflight packets for engine9 */
	uint64_t engine8                      : 4;  /**< Maximum number of inflight packets for engine8 */
	uint64_t engine7                      : 4;  /**< MBZ */
	uint64_t engine6                      : 4;  /**< MBZ */
	uint64_t engine5                      : 4;  /**< MBZ */
	uint64_t engine4                      : 4;  /**< MBZ */
	uint64_t engine3                      : 4;  /**< Maximum number of inflight packets for engine3 */
	uint64_t engine2                      : 4;  /**< Maximum number of inflight packets for engine2 */
	uint64_t engine1                      : 4;  /**< Maximum number of inflight packets for engine1 */
	uint64_t engine0                      : 4;  /**< Maximum number of inflight packets for engine0 */
#else
	uint64_t engine0                      : 4;
	uint64_t engine1                      : 4;
	uint64_t engine2                      : 4;
	uint64_t engine3                      : 4;
	uint64_t engine4                      : 4;
	uint64_t engine5                      : 4;
	uint64_t engine6                      : 4;
	uint64_t engine7                      : 4;
	uint64_t engine8                      : 4;
	uint64_t engine9                      : 4;
	uint64_t engine10                     : 4;
	uint64_t engine11                     : 4;
	uint64_t reserved_48_63               : 16;
#endif
	} cn63xx;
	struct cvmx_pko_reg_engine_inflight_cn63xx cn63xxp1;
	struct cvmx_pko_reg_engine_inflight_cn61xx cn66xx;
	struct cvmx_pko_reg_engine_inflight_s cn68xx;
	struct cvmx_pko_reg_engine_inflight_s cn68xxp1;
	struct cvmx_pko_reg_engine_inflight_cn61xx cnf71xx;
};
typedef union cvmx_pko_reg_engine_inflight cvmx_pko_reg_engine_inflight_t;

/**
 * cvmx_pko_reg_engine_inflight1
 *
 * Notes:
 * Sets the maximum number of inflight packets, per engine.  Values greater than 8 are illegal.
 * Setting an engine's value to 0 effectively stops the engine.
 */
union cvmx_pko_reg_engine_inflight1 {
	uint64_t u64;
	struct cvmx_pko_reg_engine_inflight1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t engine19                     : 4;  /**< Maximum number of inflight packets for engine19 */
	uint64_t engine18                     : 4;  /**< Maximum number of inflight packets for engine18 */
	uint64_t engine17                     : 4;  /**< Maximum number of inflight packets for engine17 */
	uint64_t engine16                     : 4;  /**< Maximum number of inflight packets for engine16 */
#else
	uint64_t engine16                     : 4;
	uint64_t engine17                     : 4;
	uint64_t engine18                     : 4;
	uint64_t engine19                     : 4;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pko_reg_engine_inflight1_s cn68xx;
	struct cvmx_pko_reg_engine_inflight1_s cn68xxp1;
};
typedef union cvmx_pko_reg_engine_inflight1 cvmx_pko_reg_engine_inflight1_t;

/**
 * cvmx_pko_reg_engine_storage#
 *
 * Notes:
 * The PKO has 40KB of local storage, consisting of 20, 2KB chunks.  Up to 15 contiguous chunks may be mapped per engine.
 * The total of all mapped storage must not exceed 40KB.
 */
union cvmx_pko_reg_engine_storagex {
	uint64_t u64;
	struct cvmx_pko_reg_engine_storagex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t engine15                     : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 15.
                                                         ENGINE15 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine14                     : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 14.
                                                         ENGINE14 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine13                     : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 13.
                                                         ENGINE13 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine12                     : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 12.
                                                         ENGINE12 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine11                     : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 11.
                                                         ENGINE11 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine10                     : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 10.
                                                         ENGINE10 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine9                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 9.
                                                         ENGINE9 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine8                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 8.
                                                         ENGINE8 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine7                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 7.
                                                         ENGINE7 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine6                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 6.
                                                         ENGINE6 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine5                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 5.
                                                         ENGINE5 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine4                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 4.
                                                         ENGINE4 does not exist and is reserved in
                                                         PKO_REG_ENGINE_STORAGE1. */
	uint64_t engine3                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 3. */
	uint64_t engine2                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 2. */
	uint64_t engine1                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 1. */
	uint64_t engine0                      : 4;  /**< Number of contiguous 2KB chunks allocated to
                                                         engine (X * 16) + 0. */
#else
	uint64_t engine0                      : 4;
	uint64_t engine1                      : 4;
	uint64_t engine2                      : 4;
	uint64_t engine3                      : 4;
	uint64_t engine4                      : 4;
	uint64_t engine5                      : 4;
	uint64_t engine6                      : 4;
	uint64_t engine7                      : 4;
	uint64_t engine8                      : 4;
	uint64_t engine9                      : 4;
	uint64_t engine10                     : 4;
	uint64_t engine11                     : 4;
	uint64_t engine12                     : 4;
	uint64_t engine13                     : 4;
	uint64_t engine14                     : 4;
	uint64_t engine15                     : 4;
#endif
	} s;
	struct cvmx_pko_reg_engine_storagex_s cn68xx;
	struct cvmx_pko_reg_engine_storagex_s cn68xxp1;
};
typedef union cvmx_pko_reg_engine_storagex cvmx_pko_reg_engine_storagex_t;

/**
 * cvmx_pko_reg_engine_thresh
 *
 * Notes:
 * When not enabled, packet data may be sent as soon as it is written into PKO's internal buffers.
 * When enabled and the packet fits entirely in the PKO's internal buffer, none of the packet data will
 * be sent until all of it has been written into the PKO's internal buffer.  Note that a packet is
 * considered to fit entirely only if the packet's size is <= BUFFER_SIZE-8.  When enabled and the
 * packet does not fit entirely in the PKO's internal buffer, none of the packet data will be sent until
 * at least BUFFER_SIZE-256 bytes of the packet have been written into the PKO's internal buffer
 * (note that BUFFER_SIZE is a function of PKO_REG_GMX_PORT_MODE above)
 */
union cvmx_pko_reg_engine_thresh {
	uint64_t u64;
	struct cvmx_pko_reg_engine_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t mask                         : 20; /**< Mask[n]=0 disables packet send threshold for engine n
                                                         Mask[n]=1 enables  packet send threshold for engine n  $PR       NS */
#else
	uint64_t mask                         : 20;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_pko_reg_engine_thresh_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t mask                         : 10; /**< Mask[n]=0 disables packet send threshold for eng n
                                                         Mask[n]=1 enables  packet send threshold for eng n     $PR       NS
                                                         Mask[n] MBZ for n = 4-7, as engines 4-7 dont exist */
#else
	uint64_t mask                         : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} cn52xx;
	struct cvmx_pko_reg_engine_thresh_cn52xx cn52xxp1;
	struct cvmx_pko_reg_engine_thresh_cn52xx cn56xx;
	struct cvmx_pko_reg_engine_thresh_cn52xx cn56xxp1;
	struct cvmx_pko_reg_engine_thresh_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t mask                         : 14; /**< Mask[n]=0 disables packet send threshold for engine n
                                                         Mask[n]=1 enables  packet send threshold for engine n  $PR       NS */
#else
	uint64_t mask                         : 14;
	uint64_t reserved_14_63               : 50;
#endif
	} cn61xx;
	struct cvmx_pko_reg_engine_thresh_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t mask                         : 12; /**< Mask[n]=0 disables packet send threshold for engine n
                                                         Mask[n]=1 enables  packet send threshold for engine n  $PR       NS
                                                         Mask[n] MBZ for n = 4-7, as engines 4-7 dont exist */
#else
	uint64_t mask                         : 12;
	uint64_t reserved_12_63               : 52;
#endif
	} cn63xx;
	struct cvmx_pko_reg_engine_thresh_cn63xx cn63xxp1;
	struct cvmx_pko_reg_engine_thresh_cn61xx cn66xx;
	struct cvmx_pko_reg_engine_thresh_s   cn68xx;
	struct cvmx_pko_reg_engine_thresh_s   cn68xxp1;
	struct cvmx_pko_reg_engine_thresh_cn61xx cnf71xx;
};
typedef union cvmx_pko_reg_engine_thresh cvmx_pko_reg_engine_thresh_t;

/**
 * cvmx_pko_reg_error
 *
 * Notes:
 * Note that this CSR is present only in chip revisions beginning with pass2.
 *
 */
union cvmx_pko_reg_error {
	uint64_t u64;
	struct cvmx_pko_reg_error_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t loopback                     : 1;  /**< A packet was sent to an illegal loopback port */
	uint64_t currzero                     : 1;  /**< A packet data pointer has size=0 */
	uint64_t doorbell                     : 1;  /**< A doorbell count has overflowed */
	uint64_t parity                       : 1;  /**< Read parity error at port data buffer */
#else
	uint64_t parity                       : 1;
	uint64_t doorbell                     : 1;
	uint64_t currzero                     : 1;
	uint64_t loopback                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pko_reg_error_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t doorbell                     : 1;  /**< A doorbell count has overflowed */
	uint64_t parity                       : 1;  /**< Read parity error at port data buffer */
#else
	uint64_t parity                       : 1;
	uint64_t doorbell                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn30xx;
	struct cvmx_pko_reg_error_cn30xx      cn31xx;
	struct cvmx_pko_reg_error_cn30xx      cn38xx;
	struct cvmx_pko_reg_error_cn30xx      cn38xxp2;
	struct cvmx_pko_reg_error_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t currzero                     : 1;  /**< A packet data pointer has size=0 */
	uint64_t doorbell                     : 1;  /**< A doorbell count has overflowed */
	uint64_t parity                       : 1;  /**< Read parity error at port data buffer */
#else
	uint64_t parity                       : 1;
	uint64_t doorbell                     : 1;
	uint64_t currzero                     : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} cn50xx;
	struct cvmx_pko_reg_error_cn50xx      cn52xx;
	struct cvmx_pko_reg_error_cn50xx      cn52xxp1;
	struct cvmx_pko_reg_error_cn50xx      cn56xx;
	struct cvmx_pko_reg_error_cn50xx      cn56xxp1;
	struct cvmx_pko_reg_error_cn50xx      cn58xx;
	struct cvmx_pko_reg_error_cn50xx      cn58xxp1;
	struct cvmx_pko_reg_error_cn50xx      cn61xx;
	struct cvmx_pko_reg_error_cn50xx      cn63xx;
	struct cvmx_pko_reg_error_cn50xx      cn63xxp1;
	struct cvmx_pko_reg_error_cn50xx      cn66xx;
	struct cvmx_pko_reg_error_s           cn68xx;
	struct cvmx_pko_reg_error_s           cn68xxp1;
	struct cvmx_pko_reg_error_cn50xx      cnf71xx;
};
typedef union cvmx_pko_reg_error cvmx_pko_reg_error_t;

/**
 * cvmx_pko_reg_flags
 *
 * Notes:
 * When set, ENA_PKO enables the PKO picker and places the PKO in normal operation.  When set, ENA_DWB
 * enables the use of DontWriteBacks during the buffer freeing operations.  When not set, STORE_BE inverts
 * bits[2:0] of the STORE0 byte write address.  When set, RESET causes a 4-cycle reset pulse to the
 * entire box.
 */
union cvmx_pko_reg_flags {
	uint64_t u64;
	struct cvmx_pko_reg_flags_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t dis_perf3                    : 1;  /**< Set to disable inactive queue QOS skipping */
	uint64_t dis_perf2                    : 1;  /**< Set to disable inactive queue skipping */
	uint64_t dis_perf1                    : 1;  /**< Set to disable command word prefetching */
	uint64_t dis_perf0                    : 1;  /**< Set to disable read performance optimizations */
	uint64_t ena_throttle                 : 1;  /**< Set to enable the PKO picker throttle logic
                                                         When ENA_THROTTLE=1 and the most-significant
                                                         bit of any of the pipe or interface, word or
                                                         packet throttle count is set, then PKO will
                                                         not output any packets to the interface/pipe.
                                                         See PKO_MEM_THROTTLE_PIPE and
                                                         PKO_MEM_THROTTLE_INT. */
	uint64_t reset                        : 1;  /**< Reset oneshot pulse */
	uint64_t store_be                     : 1;  /**< Force STORE0 byte write address to big endian */
	uint64_t ena_dwb                      : 1;  /**< Set to enable DontWriteBacks */
	uint64_t ena_pko                      : 1;  /**< Set to enable the PKO picker */
#else
	uint64_t ena_pko                      : 1;
	uint64_t ena_dwb                      : 1;
	uint64_t store_be                     : 1;
	uint64_t reset                        : 1;
	uint64_t ena_throttle                 : 1;
	uint64_t dis_perf0                    : 1;
	uint64_t dis_perf1                    : 1;
	uint64_t dis_perf2                    : 1;
	uint64_t dis_perf3                    : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_pko_reg_flags_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t reset                        : 1;  /**< Reset oneshot pulse */
	uint64_t store_be                     : 1;  /**< Force STORE0 byte write address to big endian */
	uint64_t ena_dwb                      : 1;  /**< Set to enable DontWriteBacks */
	uint64_t ena_pko                      : 1;  /**< Set to enable the PKO picker */
#else
	uint64_t ena_pko                      : 1;
	uint64_t ena_dwb                      : 1;
	uint64_t store_be                     : 1;
	uint64_t reset                        : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} cn30xx;
	struct cvmx_pko_reg_flags_cn30xx      cn31xx;
	struct cvmx_pko_reg_flags_cn30xx      cn38xx;
	struct cvmx_pko_reg_flags_cn30xx      cn38xxp2;
	struct cvmx_pko_reg_flags_cn30xx      cn50xx;
	struct cvmx_pko_reg_flags_cn30xx      cn52xx;
	struct cvmx_pko_reg_flags_cn30xx      cn52xxp1;
	struct cvmx_pko_reg_flags_cn30xx      cn56xx;
	struct cvmx_pko_reg_flags_cn30xx      cn56xxp1;
	struct cvmx_pko_reg_flags_cn30xx      cn58xx;
	struct cvmx_pko_reg_flags_cn30xx      cn58xxp1;
	struct cvmx_pko_reg_flags_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t dis_perf3                    : 1;  /**< Set to disable inactive queue QOS skipping */
	uint64_t dis_perf2                    : 1;  /**< Set to disable inactive queue skipping */
	uint64_t reserved_4_6                 : 3;
	uint64_t reset                        : 1;  /**< Reset oneshot pulse */
	uint64_t store_be                     : 1;  /**< Force STORE0 byte write address to big endian */
	uint64_t ena_dwb                      : 1;  /**< Set to enable DontWriteBacks */
	uint64_t ena_pko                      : 1;  /**< Set to enable the PKO picker */
#else
	uint64_t ena_pko                      : 1;
	uint64_t ena_dwb                      : 1;
	uint64_t store_be                     : 1;
	uint64_t reset                        : 1;
	uint64_t reserved_4_6                 : 3;
	uint64_t dis_perf2                    : 1;
	uint64_t dis_perf3                    : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn61xx;
	struct cvmx_pko_reg_flags_cn30xx      cn63xx;
	struct cvmx_pko_reg_flags_cn30xx      cn63xxp1;
	struct cvmx_pko_reg_flags_cn61xx      cn66xx;
	struct cvmx_pko_reg_flags_s           cn68xx;
	struct cvmx_pko_reg_flags_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t dis_perf1                    : 1;  /**< Set to disable command word prefetching */
	uint64_t dis_perf0                    : 1;  /**< Set to disable read performance optimizations */
	uint64_t ena_throttle                 : 1;  /**< Set to enable the PKO picker throttle logic
                                                         When ENA_THROTTLE=1 and the most-significant
                                                         bit of any of the pipe or interface, word or
                                                         packet throttle count is set, then PKO will
                                                         not output any packets to the interface/pipe.
                                                         See PKO_MEM_THROTTLE_PIPE and
                                                         PKO_MEM_THROTTLE_INT. */
	uint64_t reset                        : 1;  /**< Reset oneshot pulse */
	uint64_t store_be                     : 1;  /**< Force STORE0 byte write address to big endian */
	uint64_t ena_dwb                      : 1;  /**< Set to enable DontWriteBacks */
	uint64_t ena_pko                      : 1;  /**< Set to enable the PKO picker */
#else
	uint64_t ena_pko                      : 1;
	uint64_t ena_dwb                      : 1;
	uint64_t store_be                     : 1;
	uint64_t reset                        : 1;
	uint64_t ena_throttle                 : 1;
	uint64_t dis_perf0                    : 1;
	uint64_t dis_perf1                    : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} cn68xxp1;
	struct cvmx_pko_reg_flags_cn61xx      cnf71xx;
};
typedef union cvmx_pko_reg_flags cvmx_pko_reg_flags_t;

/**
 * cvmx_pko_reg_gmx_port_mode
 *
 * Notes:
 * The system has a total of 4 + 4 + 4 + 4 + 4 ports and 4 + 4 + 1 + 1 + 1 + 1 engines (GM0 + GM1 + PCI + LOOP + SRIO0 + SRIO1 + SRIO2 + SRIO3).
 * This CSR sets the number of GMX0/GMX1 ports and amount of local storage per engine.
 * It has no effect on the number of ports or amount of local storage per engine for PCI, LOOP,
 * SRIO0, SRIO1, SRIO2, or SRIO3.  When all GMX ports are used (MODE0=2), each GMX engine has 2.5kB of local
 * storage.  Increasing the value of MODEn by 1 decreases the number of GMX ports by a power of 2 and
 * increases the local storage per PKO GMX engine by a power of 2.  If one of the modes is 5, then only
 * one of interfaces GM0 or GM1 is present and the storage per engine of the existing interface is
 * doubled.  Modes 0 and 1 are illegal and, if selected, are treated as mode 2.
 *
 * MODE[n] GM[n] PCI   LOOP  GM[n]                      PCI            LOOP           SRIO[n]
 *         ports ports ports storage/engine             storage/engine storage/engine storage/engine
 * 0       4     4     4     ( 2.5kB << (MODE[1-n]==5)) 2.5kB          2.5kB          2.5kB
 * 1       4     4     4     ( 2.5kB << (MODE[1-n]==5)) 2.5kB          2.5kB          2.5kB
 * 2       4     4     4     ( 2.5kB << (MODE[1-n]==5)) 2.5kB          2.5kB          2.5kB
 * 3       2     4     4     ( 5.0kB << (MODE[1-n]==5)) 2.5kB          2.5kB          2.5kB
 * 4       1     4     4     (10.0kB << (MODE[1-n]==5)) 2.5kB          2.5kB          2.5kB
 * 5       0     4     4     (   0kB                  ) 2.5kB          2.5kB          2.5kB
 * where 0 <= n <= 1
 */
union cvmx_pko_reg_gmx_port_mode {
	uint64_t u64;
	struct cvmx_pko_reg_gmx_port_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t mode1                        : 3;  /**< # of GM1 ports = 16 >> MODE0, 0 <= MODE0 <= 4 */
	uint64_t mode0                        : 3;  /**< # of GM0 ports = 16 >> MODE0, 0 <= MODE0 <= 4 */
#else
	uint64_t mode0                        : 3;
	uint64_t mode1                        : 3;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_pko_reg_gmx_port_mode_s   cn30xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn31xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn38xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn38xxp2;
	struct cvmx_pko_reg_gmx_port_mode_s   cn50xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn52xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn52xxp1;
	struct cvmx_pko_reg_gmx_port_mode_s   cn56xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn56xxp1;
	struct cvmx_pko_reg_gmx_port_mode_s   cn58xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn58xxp1;
	struct cvmx_pko_reg_gmx_port_mode_s   cn61xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn63xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cn63xxp1;
	struct cvmx_pko_reg_gmx_port_mode_s   cn66xx;
	struct cvmx_pko_reg_gmx_port_mode_s   cnf71xx;
};
typedef union cvmx_pko_reg_gmx_port_mode cvmx_pko_reg_gmx_port_mode_t;

/**
 * cvmx_pko_reg_int_mask
 *
 * Notes:
 * When a mask bit is set, the corresponding interrupt is enabled.
 *
 */
union cvmx_pko_reg_int_mask {
	uint64_t u64;
	struct cvmx_pko_reg_int_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t loopback                     : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[3] above */
	uint64_t currzero                     : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[2] above */
	uint64_t doorbell                     : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[1] above */
	uint64_t parity                       : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[0] above */
#else
	uint64_t parity                       : 1;
	uint64_t doorbell                     : 1;
	uint64_t currzero                     : 1;
	uint64_t loopback                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pko_reg_int_mask_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t doorbell                     : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[1] above */
	uint64_t parity                       : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[0] above */
#else
	uint64_t parity                       : 1;
	uint64_t doorbell                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn30xx;
	struct cvmx_pko_reg_int_mask_cn30xx   cn31xx;
	struct cvmx_pko_reg_int_mask_cn30xx   cn38xx;
	struct cvmx_pko_reg_int_mask_cn30xx   cn38xxp2;
	struct cvmx_pko_reg_int_mask_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t currzero                     : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[2] above */
	uint64_t doorbell                     : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[1] above */
	uint64_t parity                       : 1;  /**< Bit mask corresponding to PKO_REG_ERROR[0] above */
#else
	uint64_t parity                       : 1;
	uint64_t doorbell                     : 1;
	uint64_t currzero                     : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} cn50xx;
	struct cvmx_pko_reg_int_mask_cn50xx   cn52xx;
	struct cvmx_pko_reg_int_mask_cn50xx   cn52xxp1;
	struct cvmx_pko_reg_int_mask_cn50xx   cn56xx;
	struct cvmx_pko_reg_int_mask_cn50xx   cn56xxp1;
	struct cvmx_pko_reg_int_mask_cn50xx   cn58xx;
	struct cvmx_pko_reg_int_mask_cn50xx   cn58xxp1;
	struct cvmx_pko_reg_int_mask_cn50xx   cn61xx;
	struct cvmx_pko_reg_int_mask_cn50xx   cn63xx;
	struct cvmx_pko_reg_int_mask_cn50xx   cn63xxp1;
	struct cvmx_pko_reg_int_mask_cn50xx   cn66xx;
	struct cvmx_pko_reg_int_mask_s        cn68xx;
	struct cvmx_pko_reg_int_mask_s        cn68xxp1;
	struct cvmx_pko_reg_int_mask_cn50xx   cnf71xx;
};
typedef union cvmx_pko_reg_int_mask cvmx_pko_reg_int_mask_t;

/**
 * cvmx_pko_reg_loopback_bpid
 *
 * Notes:
 * None.
 *
 */
union cvmx_pko_reg_loopback_bpid {
	uint64_t u64;
	struct cvmx_pko_reg_loopback_bpid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t bpid7                        : 6;  /**< Loopback port 7 backpressure-ID */
	uint64_t reserved_52_52               : 1;
	uint64_t bpid6                        : 6;  /**< Loopback port 6 backpressure-ID */
	uint64_t reserved_45_45               : 1;
	uint64_t bpid5                        : 6;  /**< Loopback port 5 backpressure-ID */
	uint64_t reserved_38_38               : 1;
	uint64_t bpid4                        : 6;  /**< Loopback port 4 backpressure-ID */
	uint64_t reserved_31_31               : 1;
	uint64_t bpid3                        : 6;  /**< Loopback port 3 backpressure-ID */
	uint64_t reserved_24_24               : 1;
	uint64_t bpid2                        : 6;  /**< Loopback port 2 backpressure-ID */
	uint64_t reserved_17_17               : 1;
	uint64_t bpid1                        : 6;  /**< Loopback port 1 backpressure-ID */
	uint64_t reserved_10_10               : 1;
	uint64_t bpid0                        : 6;  /**< Loopback port 0 backpressure-ID */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t bpid0                        : 6;
	uint64_t reserved_10_10               : 1;
	uint64_t bpid1                        : 6;
	uint64_t reserved_17_17               : 1;
	uint64_t bpid2                        : 6;
	uint64_t reserved_24_24               : 1;
	uint64_t bpid3                        : 6;
	uint64_t reserved_31_31               : 1;
	uint64_t bpid4                        : 6;
	uint64_t reserved_38_38               : 1;
	uint64_t bpid5                        : 6;
	uint64_t reserved_45_45               : 1;
	uint64_t bpid6                        : 6;
	uint64_t reserved_52_52               : 1;
	uint64_t bpid7                        : 6;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_pko_reg_loopback_bpid_s   cn68xx;
	struct cvmx_pko_reg_loopback_bpid_s   cn68xxp1;
};
typedef union cvmx_pko_reg_loopback_bpid cvmx_pko_reg_loopback_bpid_t;

/**
 * cvmx_pko_reg_loopback_pkind
 *
 * Notes:
 * None.
 *
 */
union cvmx_pko_reg_loopback_pkind {
	uint64_t u64;
	struct cvmx_pko_reg_loopback_pkind_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t pkind7                       : 6;  /**< Loopback port 7 port-kind */
	uint64_t reserved_52_52               : 1;
	uint64_t pkind6                       : 6;  /**< Loopback port 6 port-kind */
	uint64_t reserved_45_45               : 1;
	uint64_t pkind5                       : 6;  /**< Loopback port 5 port-kind */
	uint64_t reserved_38_38               : 1;
	uint64_t pkind4                       : 6;  /**< Loopback port 4 port-kind */
	uint64_t reserved_31_31               : 1;
	uint64_t pkind3                       : 6;  /**< Loopback port 3 port-kind */
	uint64_t reserved_24_24               : 1;
	uint64_t pkind2                       : 6;  /**< Loopback port 2 port-kind */
	uint64_t reserved_17_17               : 1;
	uint64_t pkind1                       : 6;  /**< Loopback port 1 port-kind */
	uint64_t reserved_10_10               : 1;
	uint64_t pkind0                       : 6;  /**< Loopback port 0 port-kind */
	uint64_t num_ports                    : 4;  /**< Number of loopback ports, 0 <= NUM_PORTS <= 8 */
#else
	uint64_t num_ports                    : 4;
	uint64_t pkind0                       : 6;
	uint64_t reserved_10_10               : 1;
	uint64_t pkind1                       : 6;
	uint64_t reserved_17_17               : 1;
	uint64_t pkind2                       : 6;
	uint64_t reserved_24_24               : 1;
	uint64_t pkind3                       : 6;
	uint64_t reserved_31_31               : 1;
	uint64_t pkind4                       : 6;
	uint64_t reserved_38_38               : 1;
	uint64_t pkind5                       : 6;
	uint64_t reserved_45_45               : 1;
	uint64_t pkind6                       : 6;
	uint64_t reserved_52_52               : 1;
	uint64_t pkind7                       : 6;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_pko_reg_loopback_pkind_s  cn68xx;
	struct cvmx_pko_reg_loopback_pkind_s  cn68xxp1;
};
typedef union cvmx_pko_reg_loopback_pkind cvmx_pko_reg_loopback_pkind_t;

/**
 * cvmx_pko_reg_min_pkt
 *
 * Notes:
 * This CSR is used with PKO_MEM_IPORT_PTRS[MIN_PKT] to select the minimum packet size.  Packets whose
 * size in bytes < (SIZEn+1) are zero-padded to (SIZEn+1) bytes.  Note that this does not include CRC bytes.
 * SIZE0=0 is read-only and is used when no padding is desired.
 */
union cvmx_pko_reg_min_pkt {
	uint64_t u64;
	struct cvmx_pko_reg_min_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t size7                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size6                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size5                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size4                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size3                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size2                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size1                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
	uint64_t size0                        : 8;  /**< Minimum packet size-1 in bytes                                NS */
#else
	uint64_t size0                        : 8;
	uint64_t size1                        : 8;
	uint64_t size2                        : 8;
	uint64_t size3                        : 8;
	uint64_t size4                        : 8;
	uint64_t size5                        : 8;
	uint64_t size6                        : 8;
	uint64_t size7                        : 8;
#endif
	} s;
	struct cvmx_pko_reg_min_pkt_s         cn68xx;
	struct cvmx_pko_reg_min_pkt_s         cn68xxp1;
};
typedef union cvmx_pko_reg_min_pkt cvmx_pko_reg_min_pkt_t;

/**
 * cvmx_pko_reg_preempt
 */
union cvmx_pko_reg_preempt {
	uint64_t u64;
	struct cvmx_pko_reg_preempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t min_size                     : 16; /**< Threshhold for packet preemption, measured in bytes.
                                                         Only packets which have at least MIN_SIZE bytes
                                                         remaining to be read can be preempted. */
#else
	uint64_t min_size                     : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pko_reg_preempt_s         cn52xx;
	struct cvmx_pko_reg_preempt_s         cn52xxp1;
	struct cvmx_pko_reg_preempt_s         cn56xx;
	struct cvmx_pko_reg_preempt_s         cn56xxp1;
	struct cvmx_pko_reg_preempt_s         cn61xx;
	struct cvmx_pko_reg_preempt_s         cn63xx;
	struct cvmx_pko_reg_preempt_s         cn63xxp1;
	struct cvmx_pko_reg_preempt_s         cn66xx;
	struct cvmx_pko_reg_preempt_s         cn68xx;
	struct cvmx_pko_reg_preempt_s         cn68xxp1;
	struct cvmx_pko_reg_preempt_s         cnf71xx;
};
typedef union cvmx_pko_reg_preempt cvmx_pko_reg_preempt_t;

/**
 * cvmx_pko_reg_queue_mode
 *
 * Notes:
 * Sets the number of queues and amount of local storage per queue
 * The system has a total of 256 queues and (256*8) words of local command storage.  This CSR sets the
 * number of queues that are used.  Increasing the value of MODE by 1 decreases the number of queues
 * by a power of 2 and increases the local storage per queue by a power of 2.
 * MODEn queues storage/queue
 * 0     256     64B ( 8 words)
 * 1     128    128B (16 words)
 * 2      64    256B (32 words)
 */
union cvmx_pko_reg_queue_mode {
	uint64_t u64;
	struct cvmx_pko_reg_queue_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t mode                         : 2;  /**< # of queues = 256 >> MODE, 0 <= MODE <=2 */
#else
	uint64_t mode                         : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pko_reg_queue_mode_s      cn30xx;
	struct cvmx_pko_reg_queue_mode_s      cn31xx;
	struct cvmx_pko_reg_queue_mode_s      cn38xx;
	struct cvmx_pko_reg_queue_mode_s      cn38xxp2;
	struct cvmx_pko_reg_queue_mode_s      cn50xx;
	struct cvmx_pko_reg_queue_mode_s      cn52xx;
	struct cvmx_pko_reg_queue_mode_s      cn52xxp1;
	struct cvmx_pko_reg_queue_mode_s      cn56xx;
	struct cvmx_pko_reg_queue_mode_s      cn56xxp1;
	struct cvmx_pko_reg_queue_mode_s      cn58xx;
	struct cvmx_pko_reg_queue_mode_s      cn58xxp1;
	struct cvmx_pko_reg_queue_mode_s      cn61xx;
	struct cvmx_pko_reg_queue_mode_s      cn63xx;
	struct cvmx_pko_reg_queue_mode_s      cn63xxp1;
	struct cvmx_pko_reg_queue_mode_s      cn66xx;
	struct cvmx_pko_reg_queue_mode_s      cn68xx;
	struct cvmx_pko_reg_queue_mode_s      cn68xxp1;
	struct cvmx_pko_reg_queue_mode_s      cnf71xx;
};
typedef union cvmx_pko_reg_queue_mode cvmx_pko_reg_queue_mode_t;

/**
 * cvmx_pko_reg_queue_preempt
 *
 * Notes:
 * Per QID, setting both PREEMPTER=1 and PREEMPTEE=1 is illegal and sets only PREEMPTER=1.
 * This CSR is used with PKO_MEM_QUEUE_PTRS and PKO_REG_QUEUE_PTRS1.  When programming queues, the
 * programming sequence must first write PKO_REG_QUEUE_PREEMPT, then PKO_REG_QUEUE_PTRS1 and then
 * PKO_MEM_QUEUE_PTRS for each queue.  Preemption is supported only on queues that are ultimately
 * mapped to engines 0-7.  It is illegal to set preemptee or preempter for a queue that is ultimately
 * mapped to engines 8-11.
 *
 * Also, PKO_REG_ENGINE_INFLIGHT must be at least 2 for any engine on which preemption is enabled.
 *
 * See the descriptions of PKO_MEM_QUEUE_PTRS for further explanation of queue programming.
 */
union cvmx_pko_reg_queue_preempt {
	uint64_t u64;
	struct cvmx_pko_reg_queue_preempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t preemptee                    : 1;  /**< Allow this QID to be preempted.
                                                         0=cannot be preempted, 1=can be preempted */
	uint64_t preempter                    : 1;  /**< Preempts the servicing of packet on PID to
                                                         allow this QID immediate servicing.  0=do not cause
                                                         preemption, 1=cause preemption.  Per PID, at most
                                                         1 QID can have this bit set. */
#else
	uint64_t preempter                    : 1;
	uint64_t preemptee                    : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pko_reg_queue_preempt_s   cn52xx;
	struct cvmx_pko_reg_queue_preempt_s   cn52xxp1;
	struct cvmx_pko_reg_queue_preempt_s   cn56xx;
	struct cvmx_pko_reg_queue_preempt_s   cn56xxp1;
	struct cvmx_pko_reg_queue_preempt_s   cn61xx;
	struct cvmx_pko_reg_queue_preempt_s   cn63xx;
	struct cvmx_pko_reg_queue_preempt_s   cn63xxp1;
	struct cvmx_pko_reg_queue_preempt_s   cn66xx;
	struct cvmx_pko_reg_queue_preempt_s   cn68xx;
	struct cvmx_pko_reg_queue_preempt_s   cn68xxp1;
	struct cvmx_pko_reg_queue_preempt_s   cnf71xx;
};
typedef union cvmx_pko_reg_queue_preempt cvmx_pko_reg_queue_preempt_t;

/**
 * cvmx_pko_reg_queue_ptrs1
 *
 * Notes:
 * This CSR is used with PKO_MEM_QUEUE_PTRS and PKO_MEM_QUEUE_QOS to allow access to queues 128-255
 * and to allow up mapping of up to 16 queues per port.  When programming queues 128-255, the
 * programming sequence must first write PKO_REG_QUEUE_PTRS1 and then write PKO_MEM_QUEUE_PTRS or
 * PKO_MEM_QUEUE_QOS for each queue.
 * See the descriptions of PKO_MEM_QUEUE_PTRS and PKO_MEM_QUEUE_QOS for further explanation of queue
 * programming.
 */
union cvmx_pko_reg_queue_ptrs1 {
	uint64_t u64;
	struct cvmx_pko_reg_queue_ptrs1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t idx3                         : 1;  /**< [3] of Index (distance from head) in the queue array */
	uint64_t qid7                         : 1;  /**< [7] of Queue ID */
#else
	uint64_t qid7                         : 1;
	uint64_t idx3                         : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pko_reg_queue_ptrs1_s     cn50xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cn52xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cn52xxp1;
	struct cvmx_pko_reg_queue_ptrs1_s     cn56xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cn56xxp1;
	struct cvmx_pko_reg_queue_ptrs1_s     cn58xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cn58xxp1;
	struct cvmx_pko_reg_queue_ptrs1_s     cn61xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cn63xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cn63xxp1;
	struct cvmx_pko_reg_queue_ptrs1_s     cn66xx;
	struct cvmx_pko_reg_queue_ptrs1_s     cnf71xx;
};
typedef union cvmx_pko_reg_queue_ptrs1 cvmx_pko_reg_queue_ptrs1_t;

/**
 * cvmx_pko_reg_read_idx
 *
 * Notes:
 * Provides the read index during a CSR read operation to any of the CSRs that are physically stored
 * as memories.  The names of these CSRs begin with the prefix "PKO_MEM_".
 * IDX[7:0] is the read index.  INC[7:0] is an increment that is added to IDX[7:0] after any CSR read.
 * The intended use is to initially write this CSR such that IDX=0 and INC=1.  Then, the entire
 * contents of a CSR memory can be read with consecutive CSR read commands.
 */
union cvmx_pko_reg_read_idx {
	uint64_t u64;
	struct cvmx_pko_reg_read_idx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t inc                          : 8;  /**< Increment to add to current index for next index */
	uint64_t index                        : 8;  /**< Index to use for next memory CSR read */
#else
	uint64_t index                        : 8;
	uint64_t inc                          : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pko_reg_read_idx_s        cn30xx;
	struct cvmx_pko_reg_read_idx_s        cn31xx;
	struct cvmx_pko_reg_read_idx_s        cn38xx;
	struct cvmx_pko_reg_read_idx_s        cn38xxp2;
	struct cvmx_pko_reg_read_idx_s        cn50xx;
	struct cvmx_pko_reg_read_idx_s        cn52xx;
	struct cvmx_pko_reg_read_idx_s        cn52xxp1;
	struct cvmx_pko_reg_read_idx_s        cn56xx;
	struct cvmx_pko_reg_read_idx_s        cn56xxp1;
	struct cvmx_pko_reg_read_idx_s        cn58xx;
	struct cvmx_pko_reg_read_idx_s        cn58xxp1;
	struct cvmx_pko_reg_read_idx_s        cn61xx;
	struct cvmx_pko_reg_read_idx_s        cn63xx;
	struct cvmx_pko_reg_read_idx_s        cn63xxp1;
	struct cvmx_pko_reg_read_idx_s        cn66xx;
	struct cvmx_pko_reg_read_idx_s        cn68xx;
	struct cvmx_pko_reg_read_idx_s        cn68xxp1;
	struct cvmx_pko_reg_read_idx_s        cnf71xx;
};
typedef union cvmx_pko_reg_read_idx cvmx_pko_reg_read_idx_t;

/**
 * cvmx_pko_reg_throttle
 *
 * Notes:
 * This CSR is used with PKO_MEM_THROTTLE_PIPE and PKO_MEM_THROTTLE_INT.  INT_MASK corresponds to the
 * interfaces listed in the description for PKO_MEM_IPORT_PTRS[INT].  Set INT_MASK[N] to enable the
 * updating of PKO_MEM_THROTTLE_PIPE and PKO_MEM_THROTTLE_INT counts for packets destined for
 * interface N.  INT_MASK has no effect on the updates caused by CSR writes to PKO_MEM_THROTTLE_PIPE
 * and PKO_MEM_THROTTLE_INT.  Note that this does not disable the throttle logic, just the updating of
 * the interface counts.
 */
union cvmx_pko_reg_throttle {
	uint64_t u64;
	struct cvmx_pko_reg_throttle_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t int_mask                     : 32; /**< Mask to enable THROTTLE count updates per interface           NS */
#else
	uint64_t int_mask                     : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pko_reg_throttle_s        cn68xx;
	struct cvmx_pko_reg_throttle_s        cn68xxp1;
};
typedef union cvmx_pko_reg_throttle cvmx_pko_reg_throttle_t;

/**
 * cvmx_pko_reg_timestamp
 *
 * Notes:
 * None.
 *
 */
union cvmx_pko_reg_timestamp {
	uint64_t u64;
	struct cvmx_pko_reg_timestamp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t wqe_word                     : 4;  /**< Specifies the 8-byte word in the WQE to which a PTP
                                                         timestamp is written.  Values 0 and 1 are illegal. */
#else
	uint64_t wqe_word                     : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pko_reg_timestamp_s       cn61xx;
	struct cvmx_pko_reg_timestamp_s       cn63xx;
	struct cvmx_pko_reg_timestamp_s       cn63xxp1;
	struct cvmx_pko_reg_timestamp_s       cn66xx;
	struct cvmx_pko_reg_timestamp_s       cn68xx;
	struct cvmx_pko_reg_timestamp_s       cn68xxp1;
	struct cvmx_pko_reg_timestamp_s       cnf71xx;
};
typedef union cvmx_pko_reg_timestamp cvmx_pko_reg_timestamp_t;

#endif
