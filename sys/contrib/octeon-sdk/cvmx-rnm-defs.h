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
 * cvmx-rnm-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon rnm.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_RNM_DEFS_H__
#define __CVMX_RNM_DEFS_H__

#define CVMX_RNM_BIST_STATUS (CVMX_ADD_IO_SEG(0x0001180040000008ull))
#define CVMX_RNM_CTL_STATUS (CVMX_ADD_IO_SEG(0x0001180040000000ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RNM_EER_DBG CVMX_RNM_EER_DBG_FUNC()
static inline uint64_t CVMX_RNM_EER_DBG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RNM_EER_DBG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180040000018ull);
}
#else
#define CVMX_RNM_EER_DBG (CVMX_ADD_IO_SEG(0x0001180040000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RNM_EER_KEY CVMX_RNM_EER_KEY_FUNC()
static inline uint64_t CVMX_RNM_EER_KEY_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RNM_EER_KEY not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180040000010ull);
}
#else
#define CVMX_RNM_EER_KEY (CVMX_ADD_IO_SEG(0x0001180040000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_RNM_SERIAL_NUM CVMX_RNM_SERIAL_NUM_FUNC()
static inline uint64_t CVMX_RNM_SERIAL_NUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_RNM_SERIAL_NUM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180040000020ull);
}
#else
#define CVMX_RNM_SERIAL_NUM (CVMX_ADD_IO_SEG(0x0001180040000020ull))
#endif

/**
 * cvmx_rnm_bist_status
 *
 * RNM_BIST_STATUS = RNM's BIST Status Register
 *
 * The RNM's Memory Bist Status register.
 */
union cvmx_rnm_bist_status {
	uint64_t u64;
	struct cvmx_rnm_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t rrc                          : 1;  /**< Status of RRC block bist. */
	uint64_t mem                          : 1;  /**< Status of MEM block bist. */
#else
	uint64_t mem                          : 1;
	uint64_t rrc                          : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_rnm_bist_status_s         cn30xx;
	struct cvmx_rnm_bist_status_s         cn31xx;
	struct cvmx_rnm_bist_status_s         cn38xx;
	struct cvmx_rnm_bist_status_s         cn38xxp2;
	struct cvmx_rnm_bist_status_s         cn50xx;
	struct cvmx_rnm_bist_status_s         cn52xx;
	struct cvmx_rnm_bist_status_s         cn52xxp1;
	struct cvmx_rnm_bist_status_s         cn56xx;
	struct cvmx_rnm_bist_status_s         cn56xxp1;
	struct cvmx_rnm_bist_status_s         cn58xx;
	struct cvmx_rnm_bist_status_s         cn58xxp1;
	struct cvmx_rnm_bist_status_s         cn61xx;
	struct cvmx_rnm_bist_status_s         cn63xx;
	struct cvmx_rnm_bist_status_s         cn63xxp1;
	struct cvmx_rnm_bist_status_s         cn66xx;
	struct cvmx_rnm_bist_status_s         cn68xx;
	struct cvmx_rnm_bist_status_s         cn68xxp1;
	struct cvmx_rnm_bist_status_s         cnf71xx;
};
typedef union cvmx_rnm_bist_status cvmx_rnm_bist_status_t;

/**
 * cvmx_rnm_ctl_status
 *
 * RNM_CTL_STATUS = RNM's Control/Status Register
 *
 * The RNM's interrupt enable register.
 */
union cvmx_rnm_ctl_status {
	uint64_t u64;
	struct cvmx_rnm_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t dis_mak                      : 1;  /**< Disable use of Master AES KEY */
	uint64_t eer_lck                      : 1;  /**< Encryption enable register locked */
	uint64_t eer_val                      : 1;  /**< Dormant encryption key match */
	uint64_t ent_sel                      : 4;  /**< ? */
	uint64_t exp_ent                      : 1;  /**< Exported entropy enable for random number generator */
	uint64_t rng_rst                      : 1;  /**< Reset RNG as core reset. */
	uint64_t rnm_rst                      : 1;  /**< Reset the RNM as core reset except for register
                                                         logic. */
	uint64_t rng_en                       : 1;  /**< Enable the output of the RNG. */
	uint64_t ent_en                       : 1;  /**< Entropy enable for random number generator. */
#else
	uint64_t ent_en                       : 1;
	uint64_t rng_en                       : 1;
	uint64_t rnm_rst                      : 1;
	uint64_t rng_rst                      : 1;
	uint64_t exp_ent                      : 1;
	uint64_t ent_sel                      : 4;
	uint64_t eer_val                      : 1;
	uint64_t eer_lck                      : 1;
	uint64_t dis_mak                      : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_rnm_ctl_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t rng_rst                      : 1;  /**< Reset RNG as core reset. */
	uint64_t rnm_rst                      : 1;  /**< Reset the RNM as core reset except for register
                                                         logic. */
	uint64_t rng_en                       : 1;  /**< Enable the output of the RNG. */
	uint64_t ent_en                       : 1;  /**< Entropy enable for random number generator. */
#else
	uint64_t ent_en                       : 1;
	uint64_t rng_en                       : 1;
	uint64_t rnm_rst                      : 1;
	uint64_t rng_rst                      : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} cn30xx;
	struct cvmx_rnm_ctl_status_cn30xx     cn31xx;
	struct cvmx_rnm_ctl_status_cn30xx     cn38xx;
	struct cvmx_rnm_ctl_status_cn30xx     cn38xxp2;
	struct cvmx_rnm_ctl_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t ent_sel                      : 4;  /**< ? */
	uint64_t exp_ent                      : 1;  /**< Exported entropy enable for random number generator */
	uint64_t rng_rst                      : 1;  /**< Reset RNG as core reset. */
	uint64_t rnm_rst                      : 1;  /**< Reset the RNM as core reset except for register
                                                         logic. */
	uint64_t rng_en                       : 1;  /**< Enable the output of the RNG. */
	uint64_t ent_en                       : 1;  /**< Entropy enable for random number generator. */
#else
	uint64_t ent_en                       : 1;
	uint64_t rng_en                       : 1;
	uint64_t rnm_rst                      : 1;
	uint64_t rng_rst                      : 1;
	uint64_t exp_ent                      : 1;
	uint64_t ent_sel                      : 4;
	uint64_t reserved_9_63                : 55;
#endif
	} cn50xx;
	struct cvmx_rnm_ctl_status_cn50xx     cn52xx;
	struct cvmx_rnm_ctl_status_cn50xx     cn52xxp1;
	struct cvmx_rnm_ctl_status_cn50xx     cn56xx;
	struct cvmx_rnm_ctl_status_cn50xx     cn56xxp1;
	struct cvmx_rnm_ctl_status_cn50xx     cn58xx;
	struct cvmx_rnm_ctl_status_cn50xx     cn58xxp1;
	struct cvmx_rnm_ctl_status_s          cn61xx;
	struct cvmx_rnm_ctl_status_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t eer_lck                      : 1;  /**< Encryption enable register locked */
	uint64_t eer_val                      : 1;  /**< Dormant encryption key match */
	uint64_t ent_sel                      : 4;  /**< ? */
	uint64_t exp_ent                      : 1;  /**< Exported entropy enable for random number generator */
	uint64_t rng_rst                      : 1;  /**< Reset RNG as core reset. */
	uint64_t rnm_rst                      : 1;  /**< Reset the RNM as core reset except for register
                                                         logic. */
	uint64_t rng_en                       : 1;  /**< Enable the output of the RNG. */
	uint64_t ent_en                       : 1;  /**< Entropy enable for random number generator. */
#else
	uint64_t ent_en                       : 1;
	uint64_t rng_en                       : 1;
	uint64_t rnm_rst                      : 1;
	uint64_t rng_rst                      : 1;
	uint64_t exp_ent                      : 1;
	uint64_t ent_sel                      : 4;
	uint64_t eer_val                      : 1;
	uint64_t eer_lck                      : 1;
	uint64_t reserved_11_63               : 53;
#endif
	} cn63xx;
	struct cvmx_rnm_ctl_status_cn63xx     cn63xxp1;
	struct cvmx_rnm_ctl_status_s          cn66xx;
	struct cvmx_rnm_ctl_status_cn63xx     cn68xx;
	struct cvmx_rnm_ctl_status_cn63xx     cn68xxp1;
	struct cvmx_rnm_ctl_status_s          cnf71xx;
};
typedef union cvmx_rnm_ctl_status cvmx_rnm_ctl_status_t;

/**
 * cvmx_rnm_eer_dbg
 *
 * RNM_EER_DBG = RNM's Encryption enable debug register
 *
 * The RNM's Encryption enable debug register
 */
union cvmx_rnm_eer_dbg {
	uint64_t u64;
	struct cvmx_rnm_eer_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dat                          : 64; /**< Dormant encryption debug info. */
#else
	uint64_t dat                          : 64;
#endif
	} s;
	struct cvmx_rnm_eer_dbg_s             cn61xx;
	struct cvmx_rnm_eer_dbg_s             cn63xx;
	struct cvmx_rnm_eer_dbg_s             cn63xxp1;
	struct cvmx_rnm_eer_dbg_s             cn66xx;
	struct cvmx_rnm_eer_dbg_s             cn68xx;
	struct cvmx_rnm_eer_dbg_s             cn68xxp1;
	struct cvmx_rnm_eer_dbg_s             cnf71xx;
};
typedef union cvmx_rnm_eer_dbg cvmx_rnm_eer_dbg_t;

/**
 * cvmx_rnm_eer_key
 *
 * RNM_EER_KEY = RNM's Encryption enable register
 *
 * The RNM's Encryption enable register
 */
union cvmx_rnm_eer_key {
	uint64_t u64;
	struct cvmx_rnm_eer_key_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t key                          : 64; /**< Dormant encryption key.  If dormant crypto is fuse
                                                         enabled, crypto can be enable by writing this
                                                         register with the correct key. */
#else
	uint64_t key                          : 64;
#endif
	} s;
	struct cvmx_rnm_eer_key_s             cn61xx;
	struct cvmx_rnm_eer_key_s             cn63xx;
	struct cvmx_rnm_eer_key_s             cn63xxp1;
	struct cvmx_rnm_eer_key_s             cn66xx;
	struct cvmx_rnm_eer_key_s             cn68xx;
	struct cvmx_rnm_eer_key_s             cn68xxp1;
	struct cvmx_rnm_eer_key_s             cnf71xx;
};
typedef union cvmx_rnm_eer_key cvmx_rnm_eer_key_t;

/**
 * cvmx_rnm_serial_num
 *
 * RNM_SERIAL_NUM = RNM's fuse serial number register
 *
 * The RNM's fuse serial number register
 *
 * Notes:
 * Added RNM_SERIAL_NUM in pass 2.0
 *
 */
union cvmx_rnm_serial_num {
	uint64_t u64;
	struct cvmx_rnm_serial_num_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dat                          : 64; /**< Dormant encryption serial number */
#else
	uint64_t dat                          : 64;
#endif
	} s;
	struct cvmx_rnm_serial_num_s          cn61xx;
	struct cvmx_rnm_serial_num_s          cn63xx;
	struct cvmx_rnm_serial_num_s          cn66xx;
	struct cvmx_rnm_serial_num_s          cn68xx;
	struct cvmx_rnm_serial_num_s          cn68xxp1;
	struct cvmx_rnm_serial_num_s          cnf71xx;
};
typedef union cvmx_rnm_serial_num cvmx_rnm_serial_num_t;

#endif
