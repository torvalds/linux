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
 * cvmx-mixx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon mixx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_MIXX_DEFS_H__
#define __CVMX_MIXX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_BIST(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_BIST(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100078ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_BIST(offset) (CVMX_ADD_IO_SEG(0x0001070000100078ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_CTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_CTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100020ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_CTL(offset) (CVMX_ADD_IO_SEG(0x0001070000100020ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_INTENA(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_INTENA(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100050ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_INTENA(offset) (CVMX_ADD_IO_SEG(0x0001070000100050ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_IRCNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_IRCNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100030ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_IRCNT(offset) (CVMX_ADD_IO_SEG(0x0001070000100030ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_IRHWM(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_IRHWM(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100028ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_IRHWM(offset) (CVMX_ADD_IO_SEG(0x0001070000100028ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_IRING1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_IRING1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100010ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_IRING1(offset) (CVMX_ADD_IO_SEG(0x0001070000100010ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_IRING2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_IRING2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100018ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_IRING2(offset) (CVMX_ADD_IO_SEG(0x0001070000100018ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_ISR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_ISR(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100048ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_ISR(offset) (CVMX_ADD_IO_SEG(0x0001070000100048ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_ORCNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_ORCNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100040ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_ORCNT(offset) (CVMX_ADD_IO_SEG(0x0001070000100040ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_ORHWM(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_ORHWM(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100038ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_ORHWM(offset) (CVMX_ADD_IO_SEG(0x0001070000100038ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_ORING1(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_ORING1(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100000ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_ORING1(offset) (CVMX_ADD_IO_SEG(0x0001070000100000ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_ORING2(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_ORING2(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100008ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_ORING2(offset) (CVMX_ADD_IO_SEG(0x0001070000100008ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_REMCNT(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_REMCNT(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100058ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_REMCNT(offset) (CVMX_ADD_IO_SEG(0x0001070000100058ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_TSCTL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_TSCTL(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100068ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_TSCTL(offset) (CVMX_ADD_IO_SEG(0x0001070000100068ull) + ((offset) & 1) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MIXX_TSTAMP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset == 0)))))
		cvmx_warn("CVMX_MIXX_TSTAMP(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000100060ull) + ((offset) & 1) * 2048;
}
#else
#define CVMX_MIXX_TSTAMP(offset) (CVMX_ADD_IO_SEG(0x0001070000100060ull) + ((offset) & 1) * 2048)
#endif

/**
 * cvmx_mix#_bist
 *
 * MIX_BIST = MIX BIST Register
 *
 * Description:
 *  NOTE: To read the MIX_BIST register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_bist {
	uint64_t u64;
	struct cvmx_mixx_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t opfdat                       : 1;  /**< Bist Results for AGO OPF Buffer RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mrgdat                       : 1;  /**< Bist Results for AGI MRG Buffer RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mrqdat                       : 1;  /**< Bist Results for NBR CSR RdReq RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ipfdat                       : 1;  /**< Bist Results for MIX Inbound Packet RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t irfdat                       : 1;  /**< Bist Results for MIX I-Ring Entry RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t orfdat                       : 1;  /**< Bist Results for MIX O-Ring Entry RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t orfdat                       : 1;
	uint64_t irfdat                       : 1;
	uint64_t ipfdat                       : 1;
	uint64_t mrqdat                       : 1;
	uint64_t mrgdat                       : 1;
	uint64_t opfdat                       : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_mixx_bist_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mrqdat                       : 1;  /**< Bist Results for NBR CSR RdReq RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t ipfdat                       : 1;  /**< Bist Results for MIX Inbound Packet RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t irfdat                       : 1;  /**< Bist Results for MIX I-Ring Entry RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t orfdat                       : 1;  /**< Bist Results for MIX O-Ring Entry RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t orfdat                       : 1;
	uint64_t irfdat                       : 1;
	uint64_t ipfdat                       : 1;
	uint64_t mrqdat                       : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} cn52xx;
	struct cvmx_mixx_bist_cn52xx          cn52xxp1;
	struct cvmx_mixx_bist_cn52xx          cn56xx;
	struct cvmx_mixx_bist_cn52xx          cn56xxp1;
	struct cvmx_mixx_bist_s               cn61xx;
	struct cvmx_mixx_bist_s               cn63xx;
	struct cvmx_mixx_bist_s               cn63xxp1;
	struct cvmx_mixx_bist_s               cn66xx;
	struct cvmx_mixx_bist_s               cn68xx;
	struct cvmx_mixx_bist_s               cn68xxp1;
};
typedef union cvmx_mixx_bist cvmx_mixx_bist_t;

/**
 * cvmx_mix#_ctl
 *
 * MIX_CTL = MIX Control Register
 *
 * Description:
 *  NOTE: To write to the MIX_CTL register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_CTL register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_ctl {
	uint64_t u64;
	struct cvmx_mixx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t ts_thresh                    : 4;  /**< TimeStamp Interrupt Threshold
                                                         When the \#of pending Timestamp interrupts (MIX_TSCTL[TSCNT]
                                                         is greater than MIX_CTL[TS_THRESH], then a programmable
                                                         TimeStamp Interrupt is issued (see MIX_INTR[TS]
                                                         MIX_INTENA[TSENA]).
                                                         SWNOTE: For o63, since the implementation only supports
                                                         4 oustanding timestamp interrupts, this field should
                                                         only be programmed from [0..3]. */
	uint64_t crc_strip                    : 1;  /**< HW CRC Strip Enable
                                                         When enabled, the last 4 bytes(CRC) of the ingress packet
                                                         are not included in cumulative packet byte length.
                                                         In other words, the cumulative LEN field for all
                                                         I-Ring Buffer Entries associated with a given ingress
                                                         packet will be 4 bytes less (so that the final 4B HW CRC
                                                         packet data is not processed by software). */
	uint64_t busy                         : 1;  /**< MIX Busy Status bit
                                                         MIX will assert busy status any time there are:
                                                           1) L2/DRAM reads in-flight (NCB-arb to read
                                                              response)
                                                           2) L2/DRAM writes in-flight (NCB-arb to write
                                                              data is sent.
                                                           3) L2/DRAM write commits in-flight (NCB-arb to write
                                                              commit response).
                                                         NOTE: After MIX_CTL[EN]=0, the MIX will eventually
                                                         complete any "inflight" transactions, at which point the
                                                         BUSY will de-assert. */
	uint64_t en                           : 1;  /**< MIX Enable bit
                                                         When EN=0, MIX will no longer arbitrate for
                                                         any new L2/DRAM read/write requests on the NCB Bus.
                                                         MIX will complete any requests that are currently
                                                         pended for the NCB Bus. */
	uint64_t reset                        : 1;  /**< MIX Soft Reset
                                                          When SW writes a '1' to MIX_CTL[RESET], the
                                                          MII-MIX/AGL logic will execute a soft reset.
                                                          NOTE: During a soft reset, CSR accesses are not effected.
                                                          However, the values of the CSR fields will be effected by
                                                          soft reset (except MIX_CTL[RESET] itself).
                                                          NOTE: After power-on, the MII-AGL/MIX are held in reset
                                                          until the MIX_CTL[RESET] is written to zero. SW MUST also
                                                          perform a MIX_CTL CSR read after this write to ensure the
                                                          soft reset de-assertion has had sufficient time to propagate
                                                          to all MIO-MIX internal logic before any subsequent MIX CSR
                                                          accesses are issued.
                                                          The intended "soft reset" sequence is: (please also
                                                          refer to HRM Section 12.6.2 on MIX/AGL Block Reset).
                                                             1) Write MIX_CTL[EN]=0
                                                                [To prevent any NEW transactions from being started]
                                                             2) Wait for MIX_CTL[BUSY]=0
                                                                [To indicate that all inflight transactions have
                                                                 completed]
                                                             3) Write MIX_CTL[RESET]=1, followed by a MIX_CTL CSR read
                                                                and wait for the result.
                                                             4) Re-Initialize the MIX/AGL just as would be done
                                                                for a hard reset.
                                                         NOTE: Once the MII has been soft-reset, please refer to HRM Section
                                                         12.6.1 MIX/AGL BringUp Sequence to complete the MIX/AGL
                                                         re-initialization sequence. */
	uint64_t lendian                      : 1;  /**< Packet Little Endian Mode
                                                         (0: Big Endian Mode/1: Little Endian Mode)
                                                         When the mode is set, MIX will byte-swap packet data
                                                         loads/stores at the MIX/NCB boundary. */
	uint64_t nbtarb                       : 1;  /**< MIX CB-Request Arbitration Mode.
                                                         When set to zero, the arbiter is fixed priority with
                                                         the following priority scheme:
                                                             Highest Priority: I-Ring Packet Write Request
                                                                               O-Ring Packet Read Request
                                                                               I-Ring Entry Write Request
                                                                               I-Ring Entry Read Request
                                                                               O-Ring Entry Read Request
                                                         When set to one, the arbiter is round robin. */
	uint64_t mrq_hwm                      : 2;  /**< MIX CB-Request FIFO Programmable High Water Mark.
                                                         The MRQ contains 16 CB-Requests which are CSR Rd/Wr
                                                         Requests. If the MRQ backs up with "HWM" entries,
                                                         then new CB-Requests are 'stalled'.
                                                            [0]: HWM = 11
                                                            [1]: HWM = 10
                                                            [2]: HWM = 9
                                                            [3]: HWM = 8
                                                         NOTE: This must only be written at power-on/boot time. */
#else
	uint64_t mrq_hwm                      : 2;
	uint64_t nbtarb                       : 1;
	uint64_t lendian                      : 1;
	uint64_t reset                        : 1;
	uint64_t en                           : 1;
	uint64_t busy                         : 1;
	uint64_t crc_strip                    : 1;
	uint64_t ts_thresh                    : 4;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_mixx_ctl_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t crc_strip                    : 1;  /**< HW CRC Strip Enable
                                                         When enabled, the last 4 bytes(CRC) of the ingress packet
                                                         are not included in cumulative packet byte length.
                                                         In other words, the cumulative LEN field for all
                                                         I-Ring Buffer Entries associated with a given ingress
                                                         packet will be 4 bytes less (so that the final 4B HW CRC
                                                         packet data is not processed by software). */
	uint64_t busy                         : 1;  /**< MIX Busy Status bit
                                                         MIX will assert busy status any time there are:
                                                           1) L2/DRAM reads in-flight (NCB-arb to read
                                                              response)
                                                           2) L2/DRAM writes in-flight (NCB-arb to write
                                                              data is sent.
                                                           3) L2/DRAM write commits in-flight (NCB-arb to write
                                                              commit response).
                                                         NOTE: After MIX_CTL[EN]=0, the MIX will eventually
                                                         complete any "inflight" transactions, at which point the
                                                         BUSY will de-assert. */
	uint64_t en                           : 1;  /**< MIX Enable bit
                                                         When EN=0, MIX will no longer arbitrate for
                                                         any new L2/DRAM read/write requests on the NCB Bus.
                                                         MIX will complete any requests that are currently
                                                         pended for the NCB Bus. */
	uint64_t reset                        : 1;  /**< MIX Soft Reset
                                                          When SW writes a '1' to MIX_CTL[RESET], the
                                                          MII-MIX/AGL logic will execute a soft reset.
                                                          NOTE: During a soft reset, CSR accesses are not effected.
                                                          However, the values of the CSR fields will be effected by
                                                          soft reset (except MIX_CTL[RESET] itself).
                                                          NOTE: After power-on, the MII-AGL/MIX are held in reset
                                                          until the MIX_CTL[RESET] is written to zero. SW MUST also
                                                          perform a MIX_CTL CSR read after this write to ensure the
                                                          soft reset de-assertion has had sufficient time to propagate
                                                          to all MIO-MIX internal logic before any subsequent MIX CSR
                                                          accesses are issued.
                                                          The intended "soft reset" sequence is: (please also
                                                          refer to HRM Section 12.6.2 on MIX/AGL Block Reset).
                                                             1) Write MIX_CTL[EN]=0
                                                                [To prevent any NEW transactions from being started]
                                                             2) Wait for MIX_CTL[BUSY]=0
                                                                [To indicate that all inflight transactions have
                                                                 completed]
                                                             3) Write MIX_CTL[RESET]=1, followed by a MIX_CTL CSR read
                                                                and wait for the result.
                                                             4) Re-Initialize the MIX/AGL just as would be done
                                                                for a hard reset.
                                                         NOTE: Once the MII has been soft-reset, please refer to HRM Section
                                                         12.6.1 MIX/AGL BringUp Sequence to complete the MIX/AGL
                                                         re-initialization sequence. */
	uint64_t lendian                      : 1;  /**< Packet Little Endian Mode
                                                         (0: Big Endian Mode/1: Little Endian Mode)
                                                         When the mode is set, MIX will byte-swap packet data
                                                         loads/stores at the MIX/NCB boundary. */
	uint64_t nbtarb                       : 1;  /**< MIX CB-Request Arbitration Mode.
                                                         When set to zero, the arbiter is fixed priority with
                                                         the following priority scheme:
                                                             Highest Priority: I-Ring Packet Write Request
                                                                               O-Ring Packet Read Request
                                                                               I-Ring Entry Write Request
                                                                               I-Ring Entry Read Request
                                                                               O-Ring Entry Read Request
                                                         When set to one, the arbiter is round robin. */
	uint64_t mrq_hwm                      : 2;  /**< MIX CB-Request FIFO Programmable High Water Mark.
                                                         The MRQ contains 16 CB-Requests which are CSR Rd/Wr
                                                         Requests. If the MRQ backs up with "HWM" entries,
                                                         then new CB-Requests are 'stalled'.
                                                            [0]: HWM = 11
                                                            [1]: HWM = 10
                                                            [2]: HWM = 9
                                                            [3]: HWM = 8
                                                         NOTE: This must only be written at power-on/boot time. */
#else
	uint64_t mrq_hwm                      : 2;
	uint64_t nbtarb                       : 1;
	uint64_t lendian                      : 1;
	uint64_t reset                        : 1;
	uint64_t en                           : 1;
	uint64_t busy                         : 1;
	uint64_t crc_strip                    : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} cn52xx;
	struct cvmx_mixx_ctl_cn52xx           cn52xxp1;
	struct cvmx_mixx_ctl_cn52xx           cn56xx;
	struct cvmx_mixx_ctl_cn52xx           cn56xxp1;
	struct cvmx_mixx_ctl_s                cn61xx;
	struct cvmx_mixx_ctl_s                cn63xx;
	struct cvmx_mixx_ctl_s                cn63xxp1;
	struct cvmx_mixx_ctl_s                cn66xx;
	struct cvmx_mixx_ctl_s                cn68xx;
	struct cvmx_mixx_ctl_s                cn68xxp1;
};
typedef union cvmx_mixx_ctl cvmx_mixx_ctl_t;

/**
 * cvmx_mix#_intena
 *
 * MIX_INTENA = MIX Local Interrupt Enable Mask Register
 *
 * Description:
 *  NOTE: To write to the MIX_INTENA register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_INTENA register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_intena {
	uint64_t u64;
	struct cvmx_mixx_intena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t tsena                        : 1;  /**< TimeStamp Interrupt Enable
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Outbound Ring with Timestamp
                                                         event (see: MIX_ISR[TS]). */
	uint64_t orunena                      : 1;  /**< ORCNT UnderFlow Detected Enable
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an ORCNT underflow condition
                                                         MIX_ISR[ORUN]. */
	uint64_t irunena                      : 1;  /**< IRCNT UnderFlow Interrupt Enable
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an IRCNT underflow condition
                                                         MIX_ISR[IRUN]. */
	uint64_t data_drpena                  : 1;  /**< Data was dropped due to RX FIFO full Interrupt
                                                         enable. If both the global interrupt mask bits
                                                         (CIU2_EN_xx_yy_PKT[MII]) and the local interrupt mask
                                                         bit(DATA_DRPENA) is set, than an interrupt is
                                                         reported for this event. */
	uint64_t ithena                       : 1;  /**< Inbound Ring Threshold Exceeded Interrupt Enable
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Inbound Ring Threshold
                                                         Exceeded event(IRTHRESH). */
	uint64_t othena                       : 1;  /**< Outbound Ring Threshold Exceeded Interrupt Enable
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Outbound Ring Threshold
                                                         Exceeded event(ORTHRESH). */
	uint64_t ivfena                       : 1;  /**< Inbound DoorBell(IDBELL) Overflow Detected
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Inbound Doorbell Overflow
                                                         event(IDBOVF). */
	uint64_t ovfena                       : 1;  /**< Outbound DoorBell(ODBELL) Overflow Interrupt Enable
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Outbound Doorbell Overflow
                                                         event(ODBOVF). */
#else
	uint64_t ovfena                       : 1;
	uint64_t ivfena                       : 1;
	uint64_t othena                       : 1;
	uint64_t ithena                       : 1;
	uint64_t data_drpena                  : 1;
	uint64_t irunena                      : 1;
	uint64_t orunena                      : 1;
	uint64_t tsena                        : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_mixx_intena_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t orunena                      : 1;  /**< ORCNT UnderFlow Detected
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an ORCNT underflow condition
                                                         MIX_ISR[ORUN]. */
	uint64_t irunena                      : 1;  /**< IRCNT UnderFlow Interrupt Enable
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an IRCNT underflow condition
                                                         MIX_ISR[IRUN]. */
	uint64_t data_drpena                  : 1;  /**< Data was dropped due to RX FIFO full Interrupt
                                                         enable. If both the global interrupt mask bits
                                                         (CIU_INTx_EN*[MII]) and the local interrupt mask
                                                         bit(DATA_DRPENA) is set, than an interrupt is
                                                         reported for this event. */
	uint64_t ithena                       : 1;  /**< Inbound Ring Threshold Exceeded Interrupt Enable
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Inbound Ring Threshold
                                                         Exceeded event(IRTHRESH). */
	uint64_t othena                       : 1;  /**< Outbound Ring Threshold Exceeded Interrupt Enable
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Outbound Ring Threshold
                                                         Exceeded event(ORTHRESH). */
	uint64_t ivfena                       : 1;  /**< Inbound DoorBell(IDBELL) Overflow Detected
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Inbound Doorbell Overflow
                                                         event(IDBOVF). */
	uint64_t ovfena                       : 1;  /**< Outbound DoorBell(ODBELL) Overflow Interrupt Enable
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Outbound Doorbell Overflow
                                                         event(ODBOVF). */
#else
	uint64_t ovfena                       : 1;
	uint64_t ivfena                       : 1;
	uint64_t othena                       : 1;
	uint64_t ithena                       : 1;
	uint64_t data_drpena                  : 1;
	uint64_t irunena                      : 1;
	uint64_t orunena                      : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} cn52xx;
	struct cvmx_mixx_intena_cn52xx        cn52xxp1;
	struct cvmx_mixx_intena_cn52xx        cn56xx;
	struct cvmx_mixx_intena_cn52xx        cn56xxp1;
	struct cvmx_mixx_intena_s             cn61xx;
	struct cvmx_mixx_intena_s             cn63xx;
	struct cvmx_mixx_intena_s             cn63xxp1;
	struct cvmx_mixx_intena_s             cn66xx;
	struct cvmx_mixx_intena_s             cn68xx;
	struct cvmx_mixx_intena_s             cn68xxp1;
};
typedef union cvmx_mixx_intena cvmx_mixx_intena_t;

/**
 * cvmx_mix#_ircnt
 *
 * MIX_IRCNT = MIX I-Ring Pending Packet Counter
 *
 * Description:
 *  NOTE: To write to the MIX_IRCNT register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_IRCNT register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_ircnt {
	uint64_t u64;
	struct cvmx_mixx_ircnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t ircnt                        : 20; /**< Pending \# of I-Ring Packets.
                                                         Whenever HW writes a completion code of Done, Trunc,
                                                         CRCErr or Err, it increments the IRCNT (to indicate
                                                         to SW the \# of pending Input packets in system memory).
                                                         NOTE: The HW guarantees that the completion code write
                                                         is always visible in system memory BEFORE it increments
                                                         the IRCNT.
                                                         Reads of IRCNT return the current inbound packet count.
                                                         Writes of IRCNT decrement the count by the value
                                                         written.
                                                         This register is used to generate interrupts to alert
                                                         SW of pending inbound MIX packets in system memory.
                                                         NOTE: In the case of inbound packets that span multiple
                                                         I-Ring entries, SW must keep track of the \# of I-Ring Entries
                                                         associated with a given inbound packet to reclaim the
                                                         proper \# of I-Ring Entries for re-use. */
#else
	uint64_t ircnt                        : 20;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_mixx_ircnt_s              cn52xx;
	struct cvmx_mixx_ircnt_s              cn52xxp1;
	struct cvmx_mixx_ircnt_s              cn56xx;
	struct cvmx_mixx_ircnt_s              cn56xxp1;
	struct cvmx_mixx_ircnt_s              cn61xx;
	struct cvmx_mixx_ircnt_s              cn63xx;
	struct cvmx_mixx_ircnt_s              cn63xxp1;
	struct cvmx_mixx_ircnt_s              cn66xx;
	struct cvmx_mixx_ircnt_s              cn68xx;
	struct cvmx_mixx_ircnt_s              cn68xxp1;
};
typedef union cvmx_mixx_ircnt cvmx_mixx_ircnt_t;

/**
 * cvmx_mix#_irhwm
 *
 * MIX_IRHWM = MIX I-Ring High-Water Mark Threshold Register
 *
 * Description:
 *  NOTE: To write to the MIX_IHWM register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_IHWM register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_irhwm {
	uint64_t u64;
	struct cvmx_mixx_irhwm_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t ibplwm                       : 20; /**< I-Ring BackPressure Low Water Mark Threshold.
                                                         When the \#of available I-Ring Entries (IDBELL)
                                                         is less than IBPLWM, the AGL-MAC will:
                                                           a) In full-duplex mode: send periodic PAUSE packets.
                                                           b) In half-duplex mode: Force collisions.
                                                         This programmable mechanism is provided as a means
                                                         to backpressure input traffic 'early' enough (so
                                                         that packets are not 'dropped' by OCTEON). */
	uint64_t irhwm                        : 20; /**< I-Ring Entry High Water Mark Threshold.
                                                         Used to determine when the \# of Inbound packets
                                                         in system memory(MIX_IRCNT[IRCNT]) exceeds this IRHWM
                                                         threshold.
                                                         NOTE: The power-on value of the CIU2_EN_xx_yy_PKT[MII]
                                                         interrupt enable bits is zero and must be enabled
                                                         to allow interrupts to be reported. */
#else
	uint64_t irhwm                        : 20;
	uint64_t ibplwm                       : 20;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_mixx_irhwm_s              cn52xx;
	struct cvmx_mixx_irhwm_s              cn52xxp1;
	struct cvmx_mixx_irhwm_s              cn56xx;
	struct cvmx_mixx_irhwm_s              cn56xxp1;
	struct cvmx_mixx_irhwm_s              cn61xx;
	struct cvmx_mixx_irhwm_s              cn63xx;
	struct cvmx_mixx_irhwm_s              cn63xxp1;
	struct cvmx_mixx_irhwm_s              cn66xx;
	struct cvmx_mixx_irhwm_s              cn68xx;
	struct cvmx_mixx_irhwm_s              cn68xxp1;
};
typedef union cvmx_mixx_irhwm cvmx_mixx_irhwm_t;

/**
 * cvmx_mix#_iring1
 *
 * MIX_IRING1 = MIX Inbound Ring Register \#1
 *
 * Description:
 *  NOTE: To write to the MIX_IRING1 register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_IRING1 register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_iring1 {
	uint64_t u64;
	struct cvmx_mixx_iring1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t isize                        : 20; /**< Represents the Inbound Ring Buffer's Size(in 8B
                                                         words). The ring can be as large as 1M entries.
                                                         NOTE: This CSR MUST BE setup written by SW poweron
                                                         (when IDBELL/IRCNT=0). */
	uint64_t ibase                        : 37; /**< Represents the 8B-aligned base address of the first
                                                         Inbound Ring entry in system memory.
                                                         NOTE: SW MUST ONLY write to this register during
                                                         power-on/boot code. */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t ibase                        : 37;
	uint64_t isize                        : 20;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_mixx_iring1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t isize                        : 20; /**< Represents the Inbound Ring Buffer's Size(in 8B
                                                         words). The ring can be as large as 1M entries.
                                                         NOTE: This CSR MUST BE setup written by SW poweron
                                                         (when IDBELL/IRCNT=0). */
	uint64_t reserved_36_39               : 4;
	uint64_t ibase                        : 33; /**< Represents the 8B-aligned base address of the first
                                                         Inbound Ring entry in system memory.
                                                         NOTE: SW MUST ONLY write to this register during
                                                         power-on/boot code. */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t ibase                        : 33;
	uint64_t reserved_36_39               : 4;
	uint64_t isize                        : 20;
	uint64_t reserved_60_63               : 4;
#endif
	} cn52xx;
	struct cvmx_mixx_iring1_cn52xx        cn52xxp1;
	struct cvmx_mixx_iring1_cn52xx        cn56xx;
	struct cvmx_mixx_iring1_cn52xx        cn56xxp1;
	struct cvmx_mixx_iring1_s             cn61xx;
	struct cvmx_mixx_iring1_s             cn63xx;
	struct cvmx_mixx_iring1_s             cn63xxp1;
	struct cvmx_mixx_iring1_s             cn66xx;
	struct cvmx_mixx_iring1_s             cn68xx;
	struct cvmx_mixx_iring1_s             cn68xxp1;
};
typedef union cvmx_mixx_iring1 cvmx_mixx_iring1_t;

/**
 * cvmx_mix#_iring2
 *
 * MIX_IRING2 = MIX Inbound Ring Register \#2
 *
 * Description:
 *  NOTE: To write to the MIX_IRING2 register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_IRING2 register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_iring2 {
	uint64_t u64;
	struct cvmx_mixx_iring2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t itlptr                       : 20; /**< The Inbound Ring Tail Pointer selects the I-Ring
                                                         Entry that the HW will process next. After the HW
                                                         completes receiving an inbound packet, it increments
                                                         the I-Ring Tail Pointer. [NOTE: The I-Ring Tail
                                                         Pointer HW increment is always modulo ISIZE.
                                                         NOTE: This field is 'read-only' to SW. */
	uint64_t reserved_20_31               : 12;
	uint64_t idbell                       : 20; /**< Represents the cumulative total of pending
                                                         Inbound Ring Buffer Entries. Each I-Ring
                                                         Buffer Entry contains 1) an L2/DRAM byte pointer
                                                         along with a 2) a Byte Length.
                                                         After SW inserts a new entry into the I-Ring Buffer,
                                                         it "rings the doorbell for the inbound ring". When
                                                         the MIX HW receives the doorbell ring, it advances
                                                         the doorbell count for the I-Ring.
                                                         SW must never cause the doorbell count for the
                                                         I-Ring to exceed the size of the I-ring(ISIZE).
                                                         A read of the CSR indicates the current doorbell
                                                         count. */
#else
	uint64_t idbell                       : 20;
	uint64_t reserved_20_31               : 12;
	uint64_t itlptr                       : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
	struct cvmx_mixx_iring2_s             cn52xx;
	struct cvmx_mixx_iring2_s             cn52xxp1;
	struct cvmx_mixx_iring2_s             cn56xx;
	struct cvmx_mixx_iring2_s             cn56xxp1;
	struct cvmx_mixx_iring2_s             cn61xx;
	struct cvmx_mixx_iring2_s             cn63xx;
	struct cvmx_mixx_iring2_s             cn63xxp1;
	struct cvmx_mixx_iring2_s             cn66xx;
	struct cvmx_mixx_iring2_s             cn68xx;
	struct cvmx_mixx_iring2_s             cn68xxp1;
};
typedef union cvmx_mixx_iring2 cvmx_mixx_iring2_t;

/**
 * cvmx_mix#_isr
 *
 * MIX_ISR = MIX Interrupt/Status Register
 *
 * Description:
 *  NOTE: To write to the MIX_ISR register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_ISR register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_isr {
	uint64_t u64;
	struct cvmx_mixx_isr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t ts                           : 1;  /**< TimeStamp Interrupt
                                                         When the \#of pending Timestamp Interrupts (MIX_TSCTL[TSCNT])
                                                         is greater than the TimeStamp Interrupt Threshold
                                                         (MIX_CTL[TS_THRESH]) value this interrupt bit is set.
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and this local interrupt mask bit is set, than an
                                                         interrupt is reported for an Outbound Ring with Timestamp
                                                         event (see: MIX_INTENA[TSENA]). */
	uint64_t orun                         : 1;  /**< ORCNT UnderFlow Detected
                                                         If SW writes a larger value than what is currently
                                                         in the MIX_ORCNT[ORCNT], then HW will report the
                                                         underflow condition.
                                                         NOTE: The MIX_ORCNT[IOCNT] will clamp to to zero.
                                                         NOTE: If an ORUN underflow condition is detected,
                                                         the integrity of the MIX/AGL HW state has
                                                         been compromised. To recover, SW must issue a
                                                         software reset sequence (see: MIX_CTL[RESET] */
	uint64_t irun                         : 1;  /**< IRCNT UnderFlow Detected
                                                         If SW writes a larger value than what is currently
                                                         in the MIX_IRCNT[IRCNT], then HW will report the
                                                         underflow condition.
                                                         NOTE: The MIX_IRCNT[IRCNT] will clamp to to zero.
                                                         NOTE: If an IRUN underflow condition is detected,
                                                         the integrity of the MIX/AGL HW state has
                                                         been compromised. To recover, SW must issue a
                                                         software reset sequence (see: MIX_CTL[RESET] */
	uint64_t data_drp                     : 1;  /**< Data was dropped due to RX FIFO full
                                                         If this does occur, the DATA_DRP is set and the
                                                         CIU2_RAW_PKT[MII] bit is set.
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and the local interrupt mask bit(DATA_DRPENA) is set, than an
                                                         interrupt is reported for this event. */
	uint64_t irthresh                     : 1;  /**< Inbound Ring Packet Threshold Exceeded
                                                         When the pending \#inbound packets in system
                                                         memory(IRCNT) has exceeded a programmable threshold
                                                         (IRHWM), then this bit is set. If this does occur,
                                                         the IRTHRESH is set and the CIU2_RAW_PKT[MII] bit
                                                         is set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and the local interrupt mask bit(ITHENA) is set, than an
                                                         interrupt is reported for this event. */
	uint64_t orthresh                     : 1;  /**< Outbound Ring Packet Threshold Exceeded
                                                         When the pending \#outbound packets in system
                                                         memory(ORCNT) has exceeded a programmable threshold
                                                         (ORHWM), then this bit is set. If this does occur,
                                                         the ORTHRESH is set and the CIU2_RAW_PKT[MII] bit
                                                         is set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and the local interrupt mask bit(OTHENA) is set, than an
                                                         interrupt is reported for this event. */
	uint64_t idblovf                      : 1;  /**< Inbound DoorBell(IDBELL) Overflow Detected
                                                         If SW attempts to write to the MIX_IRING2[IDBELL]
                                                         with a value greater than the remaining \#of
                                                         I-Ring Buffer Entries (MIX_REMCNT[IREMCNT]), then
                                                         the following occurs:
                                                         1) The  MIX_IRING2[IDBELL] write is IGNORED
                                                         2) The ODBLOVF is set and the CIU2_RAW_PKT[MII]
                                                            bit is set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and the local interrupt mask bit(IVFENA) is set, than an
                                                         interrupt is reported for this event.
                                                         SW should keep track of the \#I-Ring Entries in use
                                                         (ie: cumulative \# of IDBELL writes),  and ensure that
                                                         future IDBELL writes don't exceed the size of the
                                                         I-Ring Buffer (MIX_IRING2[ISIZE]).
                                                         SW must reclaim I-Ring Entries by keeping track of the
                                                         \#IRing-Entries, and writing to the MIX_IRCNT[IRCNT].
                                                         NOTE: The MIX_IRCNT[IRCNT] register represents the
                                                         total \#packets(not IRing Entries) and SW must further
                                                         keep track of the \# of I-Ring Entries associated with
                                                         each packet as they are processed.
                                                         NOTE: There is no recovery from an IDBLOVF Interrupt.
                                                         If it occurs, it's an indication that SW has
                                                         overwritten the I-Ring buffer, and the only recourse
                                                         is a HW reset. */
	uint64_t odblovf                      : 1;  /**< Outbound DoorBell(ODBELL) Overflow Detected
                                                         If SW attempts to write to the MIX_ORING2[ODBELL]
                                                         with a value greater than the remaining \#of
                                                         O-Ring Buffer Entries (MIX_REMCNT[OREMCNT]), then
                                                         the following occurs:
                                                         1) The  MIX_ORING2[ODBELL] write is IGNORED
                                                         2) The ODBLOVF is set and the CIU2_RAW_PKT[MII]
                                                            bit is set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU2_EN_xx_yy_PKT[MII])
                                                         and the local interrupt mask bit(OVFENA) is set, than an
                                                         interrupt is reported for this event.
                                                         SW should keep track of the \#I-Ring Entries in use
                                                         (ie: cumulative \# of ODBELL writes),  and ensure that
                                                         future ODBELL writes don't exceed the size of the
                                                         O-Ring Buffer (MIX_ORING2[OSIZE]).
                                                         SW must reclaim O-Ring Entries by writing to the
                                                         MIX_ORCNT[ORCNT]. .
                                                         NOTE: There is no recovery from an ODBLOVF Interrupt.
                                                         If it occurs, it's an indication that SW has
                                                         overwritten the O-Ring buffer, and the only recourse
                                                         is a HW reset. */
#else
	uint64_t odblovf                      : 1;
	uint64_t idblovf                      : 1;
	uint64_t orthresh                     : 1;
	uint64_t irthresh                     : 1;
	uint64_t data_drp                     : 1;
	uint64_t irun                         : 1;
	uint64_t orun                         : 1;
	uint64_t ts                           : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_mixx_isr_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t orun                         : 1;  /**< ORCNT UnderFlow Detected
                                                         If SW writes a larger value than what is currently
                                                         in the MIX_ORCNT[ORCNT], then HW will report the
                                                         underflow condition.
                                                         NOTE: The MIX_ORCNT[IOCNT] will clamp to to zero.
                                                         NOTE: If an ORUN underflow condition is detected,
                                                         the integrity of the MIX/AGL HW state has
                                                         been compromised. To recover, SW must issue a
                                                         software reset sequence (see: MIX_CTL[RESET] */
	uint64_t irun                         : 1;  /**< IRCNT UnderFlow Detected
                                                         If SW writes a larger value than what is currently
                                                         in the MIX_IRCNT[IRCNT], then HW will report the
                                                         underflow condition.
                                                         NOTE: The MIX_IRCNT[IRCNT] will clamp to to zero.
                                                         NOTE: If an IRUN underflow condition is detected,
                                                         the integrity of the MIX/AGL HW state has
                                                         been compromised. To recover, SW must issue a
                                                         software reset sequence (see: MIX_CTL[RESET] */
	uint64_t data_drp                     : 1;  /**< Data was dropped due to RX FIFO full
                                                         If this does occur, the DATA_DRP is set and the
                                                         CIU_INTx_SUM0,4[MII] bits are set.
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and the local interrupt mask bit(DATA_DRPENA) is set, than an
                                                         interrupt is reported for this event. */
	uint64_t irthresh                     : 1;  /**< Inbound Ring Packet Threshold Exceeded
                                                         When the pending \#inbound packets in system
                                                         memory(IRCNT) has exceeded a programmable threshold
                                                         (IRHWM), then this bit is set. If this does occur,
                                                         the IRTHRESH is set and the CIU_INTx_SUM0,4[MII] bits
                                                         are set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and the local interrupt mask bit(ITHENA) is set, than an
                                                         interrupt is reported for this event. */
	uint64_t orthresh                     : 1;  /**< Outbound Ring Packet Threshold Exceeded
                                                         When the pending \#outbound packets in system
                                                         memory(ORCNT) has exceeded a programmable threshold
                                                         (ORHWM), then this bit is set. If this does occur,
                                                         the ORTHRESH is set and the CIU_INTx_SUM0,4[MII] bits
                                                         are set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and the local interrupt mask bit(OTHENA) is set, than an
                                                         interrupt is reported for this event. */
	uint64_t idblovf                      : 1;  /**< Inbound DoorBell(IDBELL) Overflow Detected
                                                         If SW attempts to write to the MIX_IRING2[IDBELL]
                                                         with a value greater than the remaining \#of
                                                         I-Ring Buffer Entries (MIX_REMCNT[IREMCNT]), then
                                                         the following occurs:
                                                         1) The  MIX_IRING2[IDBELL] write is IGNORED
                                                         2) The ODBLOVF is set and the CIU_INTx_SUM0,4[MII]
                                                            bits are set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and the local interrupt mask bit(IVFENA) is set, than an
                                                         interrupt is reported for this event.
                                                         SW should keep track of the \#I-Ring Entries in use
                                                         (ie: cumulative \# of IDBELL writes),  and ensure that
                                                         future IDBELL writes don't exceed the size of the
                                                         I-Ring Buffer (MIX_IRING2[ISIZE]).
                                                         SW must reclaim I-Ring Entries by keeping track of the
                                                         \#IRing-Entries, and writing to the MIX_IRCNT[IRCNT].
                                                         NOTE: The MIX_IRCNT[IRCNT] register represents the
                                                         total \#packets(not IRing Entries) and SW must further
                                                         keep track of the \# of I-Ring Entries associated with
                                                         each packet as they are processed.
                                                         NOTE: There is no recovery from an IDBLOVF Interrupt.
                                                         If it occurs, it's an indication that SW has
                                                         overwritten the I-Ring buffer, and the only recourse
                                                         is a HW reset. */
	uint64_t odblovf                      : 1;  /**< Outbound DoorBell(ODBELL) Overflow Detected
                                                         If SW attempts to write to the MIX_ORING2[ODBELL]
                                                         with a value greater than the remaining \#of
                                                         O-Ring Buffer Entries (MIX_REMCNT[OREMCNT]), then
                                                         the following occurs:
                                                         1) The  MIX_ORING2[ODBELL] write is IGNORED
                                                         2) The ODBLOVF is set and the CIU_INTx_SUM0,4[MII]
                                                            bits are set if ((MIX_ISR & MIX_INTENA) != 0)).
                                                         If both the global interrupt mask bits (CIU_INTx_EN*[MII])
                                                         and the local interrupt mask bit(OVFENA) is set, than an
                                                         interrupt is reported for this event.
                                                         SW should keep track of the \#I-Ring Entries in use
                                                         (ie: cumulative \# of ODBELL writes),  and ensure that
                                                         future ODBELL writes don't exceed the size of the
                                                         O-Ring Buffer (MIX_ORING2[OSIZE]).
                                                         SW must reclaim O-Ring Entries by writing to the
                                                         MIX_ORCNT[ORCNT]. .
                                                         NOTE: There is no recovery from an ODBLOVF Interrupt.
                                                         If it occurs, it's an indication that SW has
                                                         overwritten the O-Ring buffer, and the only recourse
                                                         is a HW reset. */
#else
	uint64_t odblovf                      : 1;
	uint64_t idblovf                      : 1;
	uint64_t orthresh                     : 1;
	uint64_t irthresh                     : 1;
	uint64_t data_drp                     : 1;
	uint64_t irun                         : 1;
	uint64_t orun                         : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} cn52xx;
	struct cvmx_mixx_isr_cn52xx           cn52xxp1;
	struct cvmx_mixx_isr_cn52xx           cn56xx;
	struct cvmx_mixx_isr_cn52xx           cn56xxp1;
	struct cvmx_mixx_isr_s                cn61xx;
	struct cvmx_mixx_isr_s                cn63xx;
	struct cvmx_mixx_isr_s                cn63xxp1;
	struct cvmx_mixx_isr_s                cn66xx;
	struct cvmx_mixx_isr_s                cn68xx;
	struct cvmx_mixx_isr_s                cn68xxp1;
};
typedef union cvmx_mixx_isr cvmx_mixx_isr_t;

/**
 * cvmx_mix#_orcnt
 *
 * MIX_ORCNT = MIX O-Ring Packets Sent Counter
 *
 * Description:
 *  NOTE: To write to the MIX_ORCNT register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_ORCNT register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_orcnt {
	uint64_t u64;
	struct cvmx_mixx_orcnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t orcnt                        : 20; /**< Pending \# of O-Ring Packets.
                                                         Whenever HW removes a packet from the O-Ring, it
                                                         increments the ORCNT (to indicate to SW the \# of
                                                         Output packets in system memory that can be reclaimed).
                                                         Reads of ORCNT return the current count.
                                                         Writes of ORCNT decrement the count by the value
                                                         written.
                                                         This register is used to generate interrupts to alert
                                                         SW of pending outbound MIX packets that have been
                                                         removed from system memory. (see MIX_ISR[ORTHRESH]
                                                         description for more details).
                                                         NOTE: For outbound packets, the \# of O-Ring Packets
                                                         is equal to the \# of O-Ring Entries. */
#else
	uint64_t orcnt                        : 20;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_mixx_orcnt_s              cn52xx;
	struct cvmx_mixx_orcnt_s              cn52xxp1;
	struct cvmx_mixx_orcnt_s              cn56xx;
	struct cvmx_mixx_orcnt_s              cn56xxp1;
	struct cvmx_mixx_orcnt_s              cn61xx;
	struct cvmx_mixx_orcnt_s              cn63xx;
	struct cvmx_mixx_orcnt_s              cn63xxp1;
	struct cvmx_mixx_orcnt_s              cn66xx;
	struct cvmx_mixx_orcnt_s              cn68xx;
	struct cvmx_mixx_orcnt_s              cn68xxp1;
};
typedef union cvmx_mixx_orcnt cvmx_mixx_orcnt_t;

/**
 * cvmx_mix#_orhwm
 *
 * MIX_ORHWM = MIX O-Ring High-Water Mark Threshold Register
 *
 * Description:
 *  NOTE: To write to the MIX_ORHWM register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_ORHWM register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_orhwm {
	uint64_t u64;
	struct cvmx_mixx_orhwm_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t orhwm                        : 20; /**< O-Ring Entry High Water Mark Threshold.
                                                         Used to determine when the \# of Outbound packets
                                                         in system memory that can be reclaimed
                                                         (MIX_ORCNT[ORCNT]) exceeds this ORHWM threshold.
                                                         NOTE: The power-on value of the CIU2_EN_xx_yy_PKT[MII]
                                                         interrupt enable bits is zero and must be enabled
                                                         to allow interrupts to be reported. */
#else
	uint64_t orhwm                        : 20;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_mixx_orhwm_s              cn52xx;
	struct cvmx_mixx_orhwm_s              cn52xxp1;
	struct cvmx_mixx_orhwm_s              cn56xx;
	struct cvmx_mixx_orhwm_s              cn56xxp1;
	struct cvmx_mixx_orhwm_s              cn61xx;
	struct cvmx_mixx_orhwm_s              cn63xx;
	struct cvmx_mixx_orhwm_s              cn63xxp1;
	struct cvmx_mixx_orhwm_s              cn66xx;
	struct cvmx_mixx_orhwm_s              cn68xx;
	struct cvmx_mixx_orhwm_s              cn68xxp1;
};
typedef union cvmx_mixx_orhwm cvmx_mixx_orhwm_t;

/**
 * cvmx_mix#_oring1
 *
 * MIX_ORING1 = MIX Outbound Ring Register \#1
 *
 * Description:
 *  NOTE: To write to the MIX_ORING1 register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_ORING1 register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_oring1 {
	uint64_t u64;
	struct cvmx_mixx_oring1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t osize                        : 20; /**< Represents the Outbound Ring Buffer's Size(in 8B
                                                         words). The ring can be as large as 1M entries.
                                                         NOTE: This CSR MUST BE setup written by SW poweron
                                                         (when ODBELL/ORCNT=0). */
	uint64_t obase                        : 37; /**< Represents the 8B-aligned base address of the first
                                                         Outbound Ring(O-Ring) Entry in system memory.
                                                         NOTE: SW MUST ONLY write to this register during
                                                         power-on/boot code. */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t obase                        : 37;
	uint64_t osize                        : 20;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_mixx_oring1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t osize                        : 20; /**< Represents the Outbound Ring Buffer's Size(in 8B
                                                         words). The ring can be as large as 1M entries.
                                                         NOTE: This CSR MUST BE setup written by SW poweron
                                                         (when ODBELL/ORCNT=0). */
	uint64_t reserved_36_39               : 4;
	uint64_t obase                        : 33; /**< Represents the 8B-aligned base address of the first
                                                         Outbound Ring(O-Ring) Entry in system memory.
                                                         NOTE: SW MUST ONLY write to this register during
                                                         power-on/boot code. */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t obase                        : 33;
	uint64_t reserved_36_39               : 4;
	uint64_t osize                        : 20;
	uint64_t reserved_60_63               : 4;
#endif
	} cn52xx;
	struct cvmx_mixx_oring1_cn52xx        cn52xxp1;
	struct cvmx_mixx_oring1_cn52xx        cn56xx;
	struct cvmx_mixx_oring1_cn52xx        cn56xxp1;
	struct cvmx_mixx_oring1_s             cn61xx;
	struct cvmx_mixx_oring1_s             cn63xx;
	struct cvmx_mixx_oring1_s             cn63xxp1;
	struct cvmx_mixx_oring1_s             cn66xx;
	struct cvmx_mixx_oring1_s             cn68xx;
	struct cvmx_mixx_oring1_s             cn68xxp1;
};
typedef union cvmx_mixx_oring1 cvmx_mixx_oring1_t;

/**
 * cvmx_mix#_oring2
 *
 * MIX_ORING2 = MIX Outbound Ring Register \#2
 *
 * Description:
 *  NOTE: To write to the MIX_ORING2 register, a device would issue an IOBST directed at the MIO.
 *        To read the MIX_ORING2 register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_oring2 {
	uint64_t u64;
	struct cvmx_mixx_oring2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t otlptr                       : 20; /**< The Outbound Ring Tail Pointer selects the O-Ring
                                                         Entry that the HW will process next. After the HW
                                                         completes sending an outbound packet, it increments
                                                         the O-Ring Tail Pointer. [NOTE: The O-Ring Tail
                                                         Pointer HW increment is always modulo
                                                         MIX_ORING2[OSIZE].
                                                         NOTE: This field is 'read-only' to SW. */
	uint64_t reserved_20_31               : 12;
	uint64_t odbell                       : 20; /**< Represents the cumulative total of pending
                                                         Outbound Ring(O-Ring) Buffer Entries. Each O-Ring
                                                         Buffer Entry contains 1) an L2/DRAM byte pointer
                                                         along with a 2) a Byte Length.
                                                         After SW inserts new entries into the O-Ring Buffer,
                                                         it "rings the doorbell with the count of the newly
                                                         inserted entries". When the MIX HW receives the
                                                         doorbell ring, it increments the current doorbell
                                                         count by the CSR write value.
                                                         SW must never cause the doorbell count for the
                                                         O-Ring to exceed the size of the ring(OSIZE).
                                                         A read of the CSR indicates the current doorbell
                                                         count. */
#else
	uint64_t odbell                       : 20;
	uint64_t reserved_20_31               : 12;
	uint64_t otlptr                       : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
	struct cvmx_mixx_oring2_s             cn52xx;
	struct cvmx_mixx_oring2_s             cn52xxp1;
	struct cvmx_mixx_oring2_s             cn56xx;
	struct cvmx_mixx_oring2_s             cn56xxp1;
	struct cvmx_mixx_oring2_s             cn61xx;
	struct cvmx_mixx_oring2_s             cn63xx;
	struct cvmx_mixx_oring2_s             cn63xxp1;
	struct cvmx_mixx_oring2_s             cn66xx;
	struct cvmx_mixx_oring2_s             cn68xx;
	struct cvmx_mixx_oring2_s             cn68xxp1;
};
typedef union cvmx_mixx_oring2 cvmx_mixx_oring2_t;

/**
 * cvmx_mix#_remcnt
 *
 * MIX_REMCNT = MIX Ring Buffer Remainder Counts (useful for HW debug only)
 *
 * Description:
 *  NOTE: To read the MIX_REMCNT register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_remcnt {
	uint64_t u64;
	struct cvmx_mixx_remcnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t iremcnt                      : 20; /**< Remaining I-Ring Buffer Count
                                                         Reflects the \# of unused/remaining I-Ring Entries
                                                         that HW  currently detects in the I-Ring Buffer.
                                                         HW uses this value to detect I-Ring Doorbell overflows.
                                                         (see: MIX_ISR[IDBLOVF])
                                                         When SW writes the MIX_IRING1[ISIZE], the IREMCNT
                                                         is loaded with MIX_IRING2[ISIZE] value. (NOTE: ISIZE should only
                                                         be written at power-on, when it's known that there are
                                                         no I-Ring Entries currently in use by HW).
                                                         When SW writes to the IDBELL register, the IREMCNT
                                                         is decremented by the CSR write value.
                                                         When HW issues an IRing Write Request(onto NCB Bus),
                                                         the IREMCNT is incremented by 1. */
	uint64_t reserved_20_31               : 12;
	uint64_t oremcnt                      : 20; /**< Remaining O-Ring Buffer Count
                                                         Reflects the \# of unused/remaining O-Ring Entries
                                                         that HW  currently detects in the O-Ring Buffer.
                                                         HW uses this value to detect O-Ring Doorbell overflows.
                                                         (see: MIX_ISR[ODBLOVF])
                                                         When SW writes the MIX_IRING1[OSIZE], the OREMCNT
                                                         is loaded with MIX_ORING2[OSIZE] value. (NOTE: OSIZE should only
                                                         be written at power-on, when it's known that there are
                                                         no O-Ring Entries currently in use by HW).
                                                         When SW writes to the ODBELL register, the OREMCNT
                                                         is decremented by the CSR write value.
                                                         When SW writes to MIX_[OREMCNT], the OREMCNT is decremented
                                                         by the CSR write value. */
#else
	uint64_t oremcnt                      : 20;
	uint64_t reserved_20_31               : 12;
	uint64_t iremcnt                      : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
	struct cvmx_mixx_remcnt_s             cn52xx;
	struct cvmx_mixx_remcnt_s             cn52xxp1;
	struct cvmx_mixx_remcnt_s             cn56xx;
	struct cvmx_mixx_remcnt_s             cn56xxp1;
	struct cvmx_mixx_remcnt_s             cn61xx;
	struct cvmx_mixx_remcnt_s             cn63xx;
	struct cvmx_mixx_remcnt_s             cn63xxp1;
	struct cvmx_mixx_remcnt_s             cn66xx;
	struct cvmx_mixx_remcnt_s             cn68xx;
	struct cvmx_mixx_remcnt_s             cn68xxp1;
};
typedef union cvmx_mixx_remcnt cvmx_mixx_remcnt_t;

/**
 * cvmx_mix#_tsctl
 *
 * MIX_TSCTL = MIX TimeStamp Control Register
 *
 * Description:
 *  NOTE: To read the MIX_TSCTL register, a device would issue an IOBLD64 directed at the MIO.
 *
 * Notes:
 * SW can read the MIX_TSCTL register to determine the \#pending timestamp interrupts(TSCNT)
 * as well as the \#outstanding timestamp requests in flight(TSTOT), as well as the \#of available
 * timestamp entries (TSAVL) in the timestamp fifo.
 * A write to the MIX_TSCTL register will advance the MIX*_TSTAMP fifo head ptr by 1, and
 * also decrements the MIX*_TSCTL[TSCNT] and MIX*_TSCTL[TSTOT] pending count(s) by 1.
 * For example, if SW reads MIX*_TSCTL[TSCNT]=2 (2 pending timestamp interrupts), it would immediately
 * issue this sequence:
 *      1) MIX*_TSTAMP[TSTAMP] read followed by MIX*_TSCTL write
 *            [gets timestamp value/pops timestamp fifo and decrements pending count(s) by 1]
 *      2) MIX*_TSTAMP[TSTAMP] read followed by MIX*_TSCTL write
 *            [gets timestamp value/pops timestamp fifo and decrements pending count(s) by 1]
 *
 * SWNOTE: A MIX_TSCTL write when MIX_TSCTL[TSCNT]=0 (ie: TimeStamp Fifo empty), then the write is ignored.
 */
union cvmx_mixx_tsctl {
	uint64_t u64;
	struct cvmx_mixx_tsctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t tsavl                        : 5;  /**< # of MIX TimeStamp Entries Available for use
                                                         For o63: TSAVL MAX=4 (implementation
                                                         depth of timestamp fifo)
                                                         TSAVL = [IMPLEMENTATION_DEPTH=4(MAX) - TSCNT] */
	uint64_t reserved_13_15               : 3;
	uint64_t tstot                        : 5;  /**< # of pending MIX TimeStamp Requests in-flight
                                                         For o63: TSTOT must never exceed MAX=4 (implementation
                                                         depth of timestamp fifo) */
	uint64_t reserved_5_7                 : 3;
	uint64_t tscnt                        : 5;  /**< # of pending MIX TimeStamp Interrupts
                                                         For o63: TSCNT must never exceed MAX=4 (implementation
                                                         depth of timestamp fifo) */
#else
	uint64_t tscnt                        : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t tstot                        : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t tsavl                        : 5;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_mixx_tsctl_s              cn61xx;
	struct cvmx_mixx_tsctl_s              cn63xx;
	struct cvmx_mixx_tsctl_s              cn63xxp1;
	struct cvmx_mixx_tsctl_s              cn66xx;
	struct cvmx_mixx_tsctl_s              cn68xx;
	struct cvmx_mixx_tsctl_s              cn68xxp1;
};
typedef union cvmx_mixx_tsctl cvmx_mixx_tsctl_t;

/**
 * cvmx_mix#_tstamp
 *
 * MIX_TSTAMP = MIX TimeStamp Register
 *
 * Description:
 *  NOTE: To read the MIX_TSTAMP register, a device would issue an IOBLD64 directed at the MIO.
 */
union cvmx_mixx_tstamp {
	uint64_t u64;
	struct cvmx_mixx_tstamp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t tstamp                       : 64; /**< MIX TimeStamp Value
                                                          When SW sets up an ORING Entry with [47]=1(TSTAMP),
                                                          The packet is tagged with a specal SOP w/TSTAMP flag
                                                          as it is sent to the AGL.
                                                          Later the AGL will send "sample" strobe(s) to capture
                                                          a global 64bit timestamp value followed by a "commit"
                                                          strobe which writes the last sampled value into the
                                                          outbound Timestamp fifo (max depth=4) and increments
                                                          the MIX_TSCTL[TSCNT] register to indicate the total
                                                          \#of pending Timestamp interrupts.
                                                          If the \#pending Timestamp interrupts (MIX_TSCTL[TSCNT])
                                                          is greater than the MIX_CTL[TS_THRESH] value, then
                                                          a programmable interrupt is also triggered (see:
                                                          MIX_ISR[TS] MIX_INTENA[TSENA]).
                                                          SW will then read the MIX*_TSTAMP[TSTAMP]
                                                          register value, and MUST THEN write the MIX_TSCTL
                                                          register, which will decrement MIX_TSCTL[TSCNT] register,
                                                          to indicate that a single timestamp interrupt has
                                                          been serviced.
                                                          NOTE: The MIO-MIX HW tracks upto MAX=4 outstanding
                                                          timestamped outbound packets at a time. All subsequent
                                                          ORING Entries w/SOP-TSTAMP will be stalled until
                                                          SW can service the 4 outstanding interrupts.
                                                          SW can read the MIX_TSCTL register to determine the
                                                          \#pending timestamp interrupts(TSCNT) as well as the
                                                          \#outstanding timestamp requests in flight(TSTOT), as
                                                          well as the \#of available timestamp entries (TSAVL).
                                                         SW NOTE: A MIX_TSTAMP read when MIX_TSCTL[TSCNT]=0, will
                                                         result in a return value of all zeroes. SW should only
                                                         read this register when MIX_ISR[TS]=1 (or when
                                                         MIX_TSCTL[TSCNT] != 0) to retrieve the timestamp value
                                                         recorded by HW. If SW reads the TSTAMP when HW has not
                                                         recorded a valid timestamp, then an  all zeroes value is
                                                         returned. */
#else
	uint64_t tstamp                       : 64;
#endif
	} s;
	struct cvmx_mixx_tstamp_s             cn61xx;
	struct cvmx_mixx_tstamp_s             cn63xx;
	struct cvmx_mixx_tstamp_s             cn63xxp1;
	struct cvmx_mixx_tstamp_s             cn66xx;
	struct cvmx_mixx_tstamp_s             cn68xx;
	struct cvmx_mixx_tstamp_s             cn68xxp1;
};
typedef union cvmx_mixx_tstamp cvmx_mixx_tstamp_t;

#endif
