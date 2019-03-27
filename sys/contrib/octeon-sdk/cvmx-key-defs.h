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
 * cvmx-key-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon key.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_KEY_DEFS_H__
#define __CVMX_KEY_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_KEY_BIST_REG CVMX_KEY_BIST_REG_FUNC()
static inline uint64_t CVMX_KEY_BIST_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_KEY_BIST_REG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180020000018ull);
}
#else
#define CVMX_KEY_BIST_REG (CVMX_ADD_IO_SEG(0x0001180020000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_KEY_CTL_STATUS CVMX_KEY_CTL_STATUS_FUNC()
static inline uint64_t CVMX_KEY_CTL_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_KEY_CTL_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180020000010ull);
}
#else
#define CVMX_KEY_CTL_STATUS (CVMX_ADD_IO_SEG(0x0001180020000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_KEY_INT_ENB CVMX_KEY_INT_ENB_FUNC()
static inline uint64_t CVMX_KEY_INT_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_KEY_INT_ENB not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180020000008ull);
}
#else
#define CVMX_KEY_INT_ENB (CVMX_ADD_IO_SEG(0x0001180020000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_KEY_INT_SUM CVMX_KEY_INT_SUM_FUNC()
static inline uint64_t CVMX_KEY_INT_SUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_KEY_INT_SUM not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180020000000ull);
}
#else
#define CVMX_KEY_INT_SUM (CVMX_ADD_IO_SEG(0x0001180020000000ull))
#endif

/**
 * cvmx_key_bist_reg
 *
 * KEY_BIST_REG = KEY's BIST Status Register
 *
 * The KEY's BIST status for memories.
 */
union cvmx_key_bist_reg {
	uint64_t u64;
	struct cvmx_key_bist_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t rrc                          : 1;  /**< RRC bist status. */
	uint64_t mem1                         : 1;  /**< MEM - 1 bist status. */
	uint64_t mem0                         : 1;  /**< MEM - 0 bist status. */
#else
	uint64_t mem0                         : 1;
	uint64_t mem1                         : 1;
	uint64_t rrc                          : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_key_bist_reg_s            cn38xx;
	struct cvmx_key_bist_reg_s            cn38xxp2;
	struct cvmx_key_bist_reg_s            cn56xx;
	struct cvmx_key_bist_reg_s            cn56xxp1;
	struct cvmx_key_bist_reg_s            cn58xx;
	struct cvmx_key_bist_reg_s            cn58xxp1;
	struct cvmx_key_bist_reg_s            cn61xx;
	struct cvmx_key_bist_reg_s            cn63xx;
	struct cvmx_key_bist_reg_s            cn63xxp1;
	struct cvmx_key_bist_reg_s            cn66xx;
	struct cvmx_key_bist_reg_s            cn68xx;
	struct cvmx_key_bist_reg_s            cn68xxp1;
	struct cvmx_key_bist_reg_s            cnf71xx;
};
typedef union cvmx_key_bist_reg cvmx_key_bist_reg_t;

/**
 * cvmx_key_ctl_status
 *
 * KEY_CTL_STATUS = KEY's Control/Status Register
 *
 * The KEY's interrupt enable register.
 */
union cvmx_key_ctl_status {
	uint64_t u64;
	struct cvmx_key_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t mem1_err                     : 7;  /**< Causes a flip of the ECC bit associated 38:32
                                                         respective to bit 13:7 of this field, for FPF
                                                         FIFO 1. */
	uint64_t mem0_err                     : 7;  /**< Causes a flip of the ECC bit associated 38:32
                                                         respective to bit 6:0 of this field, for FPF
                                                         FIFO 0. */
#else
	uint64_t mem0_err                     : 7;
	uint64_t mem1_err                     : 7;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_key_ctl_status_s          cn38xx;
	struct cvmx_key_ctl_status_s          cn38xxp2;
	struct cvmx_key_ctl_status_s          cn56xx;
	struct cvmx_key_ctl_status_s          cn56xxp1;
	struct cvmx_key_ctl_status_s          cn58xx;
	struct cvmx_key_ctl_status_s          cn58xxp1;
	struct cvmx_key_ctl_status_s          cn61xx;
	struct cvmx_key_ctl_status_s          cn63xx;
	struct cvmx_key_ctl_status_s          cn63xxp1;
	struct cvmx_key_ctl_status_s          cn66xx;
	struct cvmx_key_ctl_status_s          cn68xx;
	struct cvmx_key_ctl_status_s          cn68xxp1;
	struct cvmx_key_ctl_status_s          cnf71xx;
};
typedef union cvmx_key_ctl_status cvmx_key_ctl_status_t;

/**
 * cvmx_key_int_enb
 *
 * KEY_INT_ENB = KEY's Interrupt Enable
 *
 * The KEY's interrupt enable register.
 */
union cvmx_key_int_enb {
	uint64_t u64;
	struct cvmx_key_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t ked1_dbe                     : 1;  /**< When set (1) and bit 3 of the KEY_INT_SUM
                                                         register is asserted the KEY will assert an
                                                         interrupt. */
	uint64_t ked1_sbe                     : 1;  /**< When set (1) and bit 2 of the KEY_INT_SUM
                                                         register is asserted the KEY will assert an
                                                         interrupt. */
	uint64_t ked0_dbe                     : 1;  /**< When set (1) and bit 1 of the KEY_INT_SUM
                                                         register is asserted the KEY will assert an
                                                         interrupt. */
	uint64_t ked0_sbe                     : 1;  /**< When set (1) and bit 0 of the KEY_INT_SUM
                                                         register is asserted the KEY will assert an
                                                         interrupt. */
#else
	uint64_t ked0_sbe                     : 1;
	uint64_t ked0_dbe                     : 1;
	uint64_t ked1_sbe                     : 1;
	uint64_t ked1_dbe                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_key_int_enb_s             cn38xx;
	struct cvmx_key_int_enb_s             cn38xxp2;
	struct cvmx_key_int_enb_s             cn56xx;
	struct cvmx_key_int_enb_s             cn56xxp1;
	struct cvmx_key_int_enb_s             cn58xx;
	struct cvmx_key_int_enb_s             cn58xxp1;
	struct cvmx_key_int_enb_s             cn61xx;
	struct cvmx_key_int_enb_s             cn63xx;
	struct cvmx_key_int_enb_s             cn63xxp1;
	struct cvmx_key_int_enb_s             cn66xx;
	struct cvmx_key_int_enb_s             cn68xx;
	struct cvmx_key_int_enb_s             cn68xxp1;
	struct cvmx_key_int_enb_s             cnf71xx;
};
typedef union cvmx_key_int_enb cvmx_key_int_enb_t;

/**
 * cvmx_key_int_sum
 *
 * KEY_INT_SUM = KEY's Interrupt Summary Register
 *
 * Contains the diffrent interrupt summary bits of the KEY.
 */
union cvmx_key_int_sum {
	uint64_t u64;
	struct cvmx_key_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t ked1_dbe                     : 1;
	uint64_t ked1_sbe                     : 1;
	uint64_t ked0_dbe                     : 1;
	uint64_t ked0_sbe                     : 1;
#else
	uint64_t ked0_sbe                     : 1;
	uint64_t ked0_dbe                     : 1;
	uint64_t ked1_sbe                     : 1;
	uint64_t ked1_dbe                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_key_int_sum_s             cn38xx;
	struct cvmx_key_int_sum_s             cn38xxp2;
	struct cvmx_key_int_sum_s             cn56xx;
	struct cvmx_key_int_sum_s             cn56xxp1;
	struct cvmx_key_int_sum_s             cn58xx;
	struct cvmx_key_int_sum_s             cn58xxp1;
	struct cvmx_key_int_sum_s             cn61xx;
	struct cvmx_key_int_sum_s             cn63xx;
	struct cvmx_key_int_sum_s             cn63xxp1;
	struct cvmx_key_int_sum_s             cn66xx;
	struct cvmx_key_int_sum_s             cn68xx;
	struct cvmx_key_int_sum_s             cn68xxp1;
	struct cvmx_key_int_sum_s             cnf71xx;
};
typedef union cvmx_key_int_sum cvmx_key_int_sum_t;

#endif
