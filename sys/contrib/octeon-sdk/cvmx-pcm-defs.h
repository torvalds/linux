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
 * cvmx-pcm-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pcm.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PCM_DEFS_H__
#define __CVMX_PCM_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCM_CLKX_CFG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PCM_CLKX_CFG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010000ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_PCM_CLKX_CFG(offset) (CVMX_ADD_IO_SEG(0x0001070000010000ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCM_CLKX_DBG(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PCM_CLKX_DBG(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010038ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_PCM_CLKX_DBG(offset) (CVMX_ADD_IO_SEG(0x0001070000010038ull) + ((offset) & 1) * 16384)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PCM_CLKX_GEN(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PCM_CLKX_GEN(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000010008ull) + ((offset) & 1) * 16384;
}
#else
#define CVMX_PCM_CLKX_GEN(offset) (CVMX_ADD_IO_SEG(0x0001070000010008ull) + ((offset) & 1) * 16384)
#endif

/**
 * cvmx_pcm_clk#_cfg
 */
union cvmx_pcm_clkx_cfg {
	uint64_t u64;
	struct cvmx_pcm_clkx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fsyncgood                    : 1;  /**< FSYNC status                                      |         NS
                                                         If 1, the last frame had a correctly positioned
                                                               fsync pulse
                                                         If 0, none/extra fsync pulse seen on most recent
                                                               frame
                                                         NOTE: this is intended for startup. the FSYNCEXTRA
                                                         and FSYNCMISSING interrupts are intended for
                                                         detecting loss of sync during normal operation. */
	uint64_t reserved_48_62               : 15;
	uint64_t fsyncsamp                    : 16; /**< Number of ECLKs from internal BCLK edge to        |          NS
                                                         sample FSYNC
                                                         NOTE: used to sync to the start of a frame and to
                                                         check for FSYNC errors. */
	uint64_t reserved_26_31               : 6;
	uint64_t fsynclen                     : 5;  /**< Number of 1/2 BCLKs FSYNC is asserted for         |          NS
                                                         NOTE: only used when GEN==1 */
	uint64_t fsyncloc                     : 5;  /**< FSYNC location, in 1/2 BCLKS before timeslot 0,   |          NS
                                                         bit 0.
                                                         NOTE: also used to detect framing errors and
                                                         therefore must have a correct value even if GEN==0 */
	uint64_t numslots                     : 10; /**< Number of 8-bit slots in a frame                  |          NS
                                                         NOTE: this, along with EXTRABIT and Fbclk
                                                         determines FSYNC frequency when GEN == 1
                                                         NOTE: also used to detect framing errors and
                                                         therefore must have a correct value even if GEN==0 */
	uint64_t extrabit                     : 1;  /**< If 0, no frame bit                                |          NS
                                                         If 1, add one extra bit time for frame bit
                                                         NOTE: if GEN == 1, then FSYNC will be delayed one
                                                         extra bit time.
                                                         NOTE: also used to detect framing errors and
                                                         therefore must have a correct value even if GEN==0
                                                         NOTE: the extra bit comes from the LSB/MSB of the
                                                         first byte of the frame in the transmit memory
                                                         region.  LSB vs MSB is determined from the setting
                                                         of PCMn_TDM_CFG[LSBFIRST]. */
	uint64_t bitlen                       : 2;  /**< Number of BCLKs in a bit time.                    |          NS
                                                         0 : 1 BCLK
                                                         1 : 2 BCLKs
                                                         2 : 4 BCLKs
                                                         3 : operation undefined */
	uint64_t bclkpol                      : 1;  /**< If 0, BCLK rise edge is start of bit time         |          NS
                                                         If 1, BCLK fall edge is start of bit time
                                                         NOTE: also used to detect framing errors and
                                                         therefore must have a correct value even if GEN==0 */
	uint64_t fsyncpol                     : 1;  /**< If 0, FSYNC idles low, asserts high               |          NS
                                                         If 1, FSYNC idles high, asserts low
                                                         NOTE: also used to detect framing errors and
                                                         therefore must have a correct value even if GEN==0 */
	uint64_t ena                          : 1;  /**< If 0, Clock receiving logic is doing nothing      |          NS
                                                         1, Clock receiving logic is looking for sync */
#else
	uint64_t ena                          : 1;
	uint64_t fsyncpol                     : 1;
	uint64_t bclkpol                      : 1;
	uint64_t bitlen                       : 2;
	uint64_t extrabit                     : 1;
	uint64_t numslots                     : 10;
	uint64_t fsyncloc                     : 5;
	uint64_t fsynclen                     : 5;
	uint64_t reserved_26_31               : 6;
	uint64_t fsyncsamp                    : 16;
	uint64_t reserved_48_62               : 15;
	uint64_t fsyncgood                    : 1;
#endif
	} s;
	struct cvmx_pcm_clkx_cfg_s            cn30xx;
	struct cvmx_pcm_clkx_cfg_s            cn31xx;
	struct cvmx_pcm_clkx_cfg_s            cn50xx;
	struct cvmx_pcm_clkx_cfg_s            cn61xx;
	struct cvmx_pcm_clkx_cfg_s            cnf71xx;
};
typedef union cvmx_pcm_clkx_cfg cvmx_pcm_clkx_cfg_t;

/**
 * cvmx_pcm_clk#_dbg
 */
union cvmx_pcm_clkx_dbg {
	uint64_t u64;
	struct cvmx_pcm_clkx_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t debuginfo                    : 64; /**< Miscellaneous debug information                   |           NS */
#else
	uint64_t debuginfo                    : 64;
#endif
	} s;
	struct cvmx_pcm_clkx_dbg_s            cn30xx;
	struct cvmx_pcm_clkx_dbg_s            cn31xx;
	struct cvmx_pcm_clkx_dbg_s            cn50xx;
	struct cvmx_pcm_clkx_dbg_s            cn61xx;
	struct cvmx_pcm_clkx_dbg_s            cnf71xx;
};
typedef union cvmx_pcm_clkx_dbg cvmx_pcm_clkx_dbg_t;

/**
 * cvmx_pcm_clk#_gen
 */
union cvmx_pcm_clkx_gen {
	uint64_t u64;
	struct cvmx_pcm_clkx_gen_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t deltasamp                    : 16; /**< Signed number of ECLKs to move sampled BCLK edge   |          NS
                                                         NOTE: the complete number of ECLKs to move is:
                                                                   NUMSAMP + 2 + 1 + DELTASAMP
                                                               NUMSAMP to compensate for sampling delay
                                                               + 2 to compensate for dual-rank synchronizer
                                                               + 1 for uncertainity
                                                               + DELTASAMP to CMA/debugging */
	uint64_t numsamp                      : 16; /**< Number of ECLK samples to detect BCLK change when  |          NS
                                                         receiving clock. */
	uint64_t n                            : 32; /**< Determines BCLK frequency when generating clock    |          NS
                                                         NOTE: Fbclk = Feclk * N / 2^32
                                                               N = (Fbclk / Feclk) * 2^32
                                                         NOTE: writing N == 0 stops the clock generator, and
                                                               causes bclk and fsync to be RECEIVED */
#else
	uint64_t n                            : 32;
	uint64_t numsamp                      : 16;
	uint64_t deltasamp                    : 16;
#endif
	} s;
	struct cvmx_pcm_clkx_gen_s            cn30xx;
	struct cvmx_pcm_clkx_gen_s            cn31xx;
	struct cvmx_pcm_clkx_gen_s            cn50xx;
	struct cvmx_pcm_clkx_gen_s            cn61xx;
	struct cvmx_pcm_clkx_gen_s            cnf71xx;
};
typedef union cvmx_pcm_clkx_gen cvmx_pcm_clkx_gen_t;

#endif
