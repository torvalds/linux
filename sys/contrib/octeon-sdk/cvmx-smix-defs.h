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
 * cvmx-smix-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon smix.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SMIX_DEFS_H__
#define __CVMX_SMIX_DEFS_H__

static inline uint64_t CVMX_SMIX_CLK(unsigned long offset)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if ((offset == 0))
				return CVMX_ADD_IO_SEG(0x0001180000001818ull) + ((offset) & 0) * 256;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((offset <= 1))
				return CVMX_ADD_IO_SEG(0x0001180000001818ull) + ((offset) & 1) * 256;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((offset <= 3))
				return CVMX_ADD_IO_SEG(0x0001180000003818ull) + ((offset) & 3) * 128;
			break;
	}
	cvmx_warn("CVMX_SMIX_CLK (offset = %lu) not supported on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001818ull) + ((offset) & 1) * 256;
}
static inline uint64_t CVMX_SMIX_CMD(unsigned long offset)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if ((offset == 0))
				return CVMX_ADD_IO_SEG(0x0001180000001800ull) + ((offset) & 0) * 256;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((offset <= 1))
				return CVMX_ADD_IO_SEG(0x0001180000001800ull) + ((offset) & 1) * 256;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((offset <= 3))
				return CVMX_ADD_IO_SEG(0x0001180000003800ull) + ((offset) & 3) * 128;
			break;
	}
	cvmx_warn("CVMX_SMIX_CMD (offset = %lu) not supported on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001800ull) + ((offset) & 1) * 256;
}
static inline uint64_t CVMX_SMIX_EN(unsigned long offset)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if ((offset == 0))
				return CVMX_ADD_IO_SEG(0x0001180000001820ull) + ((offset) & 0) * 256;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((offset <= 1))
				return CVMX_ADD_IO_SEG(0x0001180000001820ull) + ((offset) & 1) * 256;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((offset <= 3))
				return CVMX_ADD_IO_SEG(0x0001180000003820ull) + ((offset) & 3) * 128;
			break;
	}
	cvmx_warn("CVMX_SMIX_EN (offset = %lu) not supported on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001820ull) + ((offset) & 1) * 256;
}
static inline uint64_t CVMX_SMIX_RD_DAT(unsigned long offset)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if ((offset == 0))
				return CVMX_ADD_IO_SEG(0x0001180000001810ull) + ((offset) & 0) * 256;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((offset <= 1))
				return CVMX_ADD_IO_SEG(0x0001180000001810ull) + ((offset) & 1) * 256;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((offset <= 3))
				return CVMX_ADD_IO_SEG(0x0001180000003810ull) + ((offset) & 3) * 128;
			break;
	}
	cvmx_warn("CVMX_SMIX_RD_DAT (offset = %lu) not supported on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001810ull) + ((offset) & 1) * 256;
}
static inline uint64_t CVMX_SMIX_WR_DAT(unsigned long offset)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if ((offset == 0))
				return CVMX_ADD_IO_SEG(0x0001180000001808ull) + ((offset) & 0) * 256;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((offset <= 1))
				return CVMX_ADD_IO_SEG(0x0001180000001808ull) + ((offset) & 1) * 256;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((offset <= 3))
				return CVMX_ADD_IO_SEG(0x0001180000003808ull) + ((offset) & 3) * 128;
			break;
	}
	cvmx_warn("CVMX_SMIX_WR_DAT (offset = %lu) not supported on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001808ull) + ((offset) & 1) * 256;
}

/**
 * cvmx_smi#_clk
 *
 * SMI_CLK = Clock Control Register
 *
 */
union cvmx_smix_clk {
	uint64_t u64;
	struct cvmx_smix_clk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t mode                         : 1;  /**< IEEE operating mode
                                                         0=Clause 22 complient
                                                         1=Clause 45 complient */
	uint64_t reserved_21_23               : 3;
	uint64_t sample_hi                    : 5;  /**< When to sample read data (extended bits) */
	uint64_t sample_mode                  : 1;  /**< Read Data sampling mode
                                                         According to the 802.3 spec, on reads, the STA
                                                         transitions MDC and the PHY drives MDIO with
                                                         some delay relative to that edge.  This is edge1.
                                                         The STA then samples MDIO on the next rising edge
                                                         of MDC.  This is edge2. Octeon can sample the
                                                         read data relative to either edge.
                                                          0=[SAMPLE_HI,SAMPLE] specify the sample time
                                                            relative to edge2
                                                          1=[SAMPLE_HI,SAMPLE] specify the sample time
                                                            relative to edge1 */
	uint64_t reserved_14_14               : 1;
	uint64_t clk_idle                     : 1;  /**< Do not toggle MDC on idle cycles */
	uint64_t preamble                     : 1;  /**< Send PREAMBLE on SMI transacton
                                                         PREAMBLE must be set 1 when MODE=1 in order
                                                         for the receiving PHY to correctly frame the
                                                         transaction. */
	uint64_t sample                       : 4;  /**< When to sample read data
                                                         (number of eclks after the rising edge of mdc)
                                                         ( [SAMPLE_HI,SAMPLE] > 1 )
                                                         ( [SAMPLE_HI, SAMPLE] + 3 <= 2*PHASE ) */
	uint64_t phase                        : 8;  /**< MDC Clock Phase
                                                         (number of eclks that make up an mdc phase)
                                                         (PHASE > 2) */
#else
	uint64_t phase                        : 8;
	uint64_t sample                       : 4;
	uint64_t preamble                     : 1;
	uint64_t clk_idle                     : 1;
	uint64_t reserved_14_14               : 1;
	uint64_t sample_mode                  : 1;
	uint64_t sample_hi                    : 5;
	uint64_t reserved_21_23               : 3;
	uint64_t mode                         : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_smix_clk_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t sample_hi                    : 5;  /**< When to sample read data (extended bits) */
	uint64_t sample_mode                  : 1;  /**< Read Data sampling mode
                                                         According to the 802.3 spec, on reads, the STA
                                                         transitions MDC and the PHY drives MDIO with
                                                         some delay relative to that edge.  This is edge1.
                                                         The STA then samples MDIO on the next rising edge
                                                         of MDC.  This is edge2. Octeon can sample the
                                                         read data relative to either edge.
                                                          0=[SAMPLE_HI,SAMPLE] specify the sample time
                                                            relative to edge2
                                                          1=[SAMPLE_HI,SAMPLE] specify the sample time
                                                            relative to edge1 */
	uint64_t reserved_14_14               : 1;
	uint64_t clk_idle                     : 1;  /**< Do not toggle MDC on idle cycles */
	uint64_t preamble                     : 1;  /**< Send PREAMBLE on SMI transacton */
	uint64_t sample                       : 4;  /**< When to sample read data
                                                         (number of eclks after the rising edge of mdc)
                                                         ( [SAMPLE_HI,SAMPLE] > 1 )
                                                         ( [SAMPLE_HI, SAMPLE] + 3 <= 2*PHASE ) */
	uint64_t phase                        : 8;  /**< MDC Clock Phase
                                                         (number of eclks that make up an mdc phase)
                                                         (PHASE > 2) */
#else
	uint64_t phase                        : 8;
	uint64_t sample                       : 4;
	uint64_t preamble                     : 1;
	uint64_t clk_idle                     : 1;
	uint64_t reserved_14_14               : 1;
	uint64_t sample_mode                  : 1;
	uint64_t sample_hi                    : 5;
	uint64_t reserved_21_63               : 43;
#endif
	} cn30xx;
	struct cvmx_smix_clk_cn30xx           cn31xx;
	struct cvmx_smix_clk_cn30xx           cn38xx;
	struct cvmx_smix_clk_cn30xx           cn38xxp2;
	struct cvmx_smix_clk_s                cn50xx;
	struct cvmx_smix_clk_s                cn52xx;
	struct cvmx_smix_clk_s                cn52xxp1;
	struct cvmx_smix_clk_s                cn56xx;
	struct cvmx_smix_clk_s                cn56xxp1;
	struct cvmx_smix_clk_cn30xx           cn58xx;
	struct cvmx_smix_clk_cn30xx           cn58xxp1;
	struct cvmx_smix_clk_s                cn61xx;
	struct cvmx_smix_clk_s                cn63xx;
	struct cvmx_smix_clk_s                cn63xxp1;
	struct cvmx_smix_clk_s                cn66xx;
	struct cvmx_smix_clk_s                cn68xx;
	struct cvmx_smix_clk_s                cn68xxp1;
	struct cvmx_smix_clk_s                cnf71xx;
};
typedef union cvmx_smix_clk cvmx_smix_clk_t;

/**
 * cvmx_smi#_cmd
 *
 * SMI_CMD = Force a Read/Write command to the PHY
 *
 *
 * Notes:
 * Writes to this register will create SMI xactions.  Software will poll on (depending on the xaction type).
 *
 */
union cvmx_smix_cmd {
	uint64_t u64;
	struct cvmx_smix_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t phy_op                       : 2;  /**< PHY Opcode depending on SMI_CLK[MODE]
                                                         SMI_CLK[MODE] == 0 (<=1Gbs / Clause 22)
                                                          x0=write
                                                          x1=read
                                                         SMI_CLK[MODE] == 1 (>1Gbs / Clause 45)
                                                          00=address
                                                          01=write
                                                          11=read
                                                          10=post-read-increment-address */
	uint64_t reserved_13_15               : 3;
	uint64_t phy_adr                      : 5;  /**< PHY Address */
	uint64_t reserved_5_7                 : 3;
	uint64_t reg_adr                      : 5;  /**< PHY Register Offset */
#else
	uint64_t reg_adr                      : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t phy_adr                      : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t phy_op                       : 2;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_smix_cmd_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t phy_op                       : 1;  /**< PHY Opcode
                                                         0=write
                                                         1=read */
	uint64_t reserved_13_15               : 3;
	uint64_t phy_adr                      : 5;  /**< PHY Address */
	uint64_t reserved_5_7                 : 3;
	uint64_t reg_adr                      : 5;  /**< PHY Register Offset */
#else
	uint64_t reg_adr                      : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t phy_adr                      : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t phy_op                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn30xx;
	struct cvmx_smix_cmd_cn30xx           cn31xx;
	struct cvmx_smix_cmd_cn30xx           cn38xx;
	struct cvmx_smix_cmd_cn30xx           cn38xxp2;
	struct cvmx_smix_cmd_s                cn50xx;
	struct cvmx_smix_cmd_s                cn52xx;
	struct cvmx_smix_cmd_s                cn52xxp1;
	struct cvmx_smix_cmd_s                cn56xx;
	struct cvmx_smix_cmd_s                cn56xxp1;
	struct cvmx_smix_cmd_cn30xx           cn58xx;
	struct cvmx_smix_cmd_cn30xx           cn58xxp1;
	struct cvmx_smix_cmd_s                cn61xx;
	struct cvmx_smix_cmd_s                cn63xx;
	struct cvmx_smix_cmd_s                cn63xxp1;
	struct cvmx_smix_cmd_s                cn66xx;
	struct cvmx_smix_cmd_s                cn68xx;
	struct cvmx_smix_cmd_s                cn68xxp1;
	struct cvmx_smix_cmd_s                cnf71xx;
};
typedef union cvmx_smix_cmd cvmx_smix_cmd_t;

/**
 * cvmx_smi#_en
 *
 * SMI_EN = Enable the SMI interface
 *
 */
union cvmx_smix_en {
	uint64_t u64;
	struct cvmx_smix_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t en                           : 1;  /**< Interface enable
                                                         0=SMI Interface is down / no transactions, no MDC
                                                         1=SMI Interface is up */
#else
	uint64_t en                           : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_smix_en_s                 cn30xx;
	struct cvmx_smix_en_s                 cn31xx;
	struct cvmx_smix_en_s                 cn38xx;
	struct cvmx_smix_en_s                 cn38xxp2;
	struct cvmx_smix_en_s                 cn50xx;
	struct cvmx_smix_en_s                 cn52xx;
	struct cvmx_smix_en_s                 cn52xxp1;
	struct cvmx_smix_en_s                 cn56xx;
	struct cvmx_smix_en_s                 cn56xxp1;
	struct cvmx_smix_en_s                 cn58xx;
	struct cvmx_smix_en_s                 cn58xxp1;
	struct cvmx_smix_en_s                 cn61xx;
	struct cvmx_smix_en_s                 cn63xx;
	struct cvmx_smix_en_s                 cn63xxp1;
	struct cvmx_smix_en_s                 cn66xx;
	struct cvmx_smix_en_s                 cn68xx;
	struct cvmx_smix_en_s                 cn68xxp1;
	struct cvmx_smix_en_s                 cnf71xx;
};
typedef union cvmx_smix_en cvmx_smix_en_t;

/**
 * cvmx_smi#_rd_dat
 *
 * SMI_RD_DAT = SMI Read Data
 *
 *
 * Notes:
 * VAL will assert when the read xaction completes.  A read to this register
 * will clear VAL.  PENDING indicates that an SMI RD transaction is in flight.
 */
union cvmx_smix_rd_dat {
	uint64_t u64;
	struct cvmx_smix_rd_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t pending                      : 1;  /**< Read Xaction Pending */
	uint64_t val                          : 1;  /**< Read Data Valid */
	uint64_t dat                          : 16; /**< Read Data */
#else
	uint64_t dat                          : 16;
	uint64_t val                          : 1;
	uint64_t pending                      : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_smix_rd_dat_s             cn30xx;
	struct cvmx_smix_rd_dat_s             cn31xx;
	struct cvmx_smix_rd_dat_s             cn38xx;
	struct cvmx_smix_rd_dat_s             cn38xxp2;
	struct cvmx_smix_rd_dat_s             cn50xx;
	struct cvmx_smix_rd_dat_s             cn52xx;
	struct cvmx_smix_rd_dat_s             cn52xxp1;
	struct cvmx_smix_rd_dat_s             cn56xx;
	struct cvmx_smix_rd_dat_s             cn56xxp1;
	struct cvmx_smix_rd_dat_s             cn58xx;
	struct cvmx_smix_rd_dat_s             cn58xxp1;
	struct cvmx_smix_rd_dat_s             cn61xx;
	struct cvmx_smix_rd_dat_s             cn63xx;
	struct cvmx_smix_rd_dat_s             cn63xxp1;
	struct cvmx_smix_rd_dat_s             cn66xx;
	struct cvmx_smix_rd_dat_s             cn68xx;
	struct cvmx_smix_rd_dat_s             cn68xxp1;
	struct cvmx_smix_rd_dat_s             cnf71xx;
};
typedef union cvmx_smix_rd_dat cvmx_smix_rd_dat_t;

/**
 * cvmx_smi#_wr_dat
 *
 * SMI_WR_DAT = SMI Write Data
 *
 *
 * Notes:
 * VAL will assert when the write xaction completes.  A read to this register
 * will clear VAL.  PENDING indicates that an SMI WR transaction is in flight.
 */
union cvmx_smix_wr_dat {
	uint64_t u64;
	struct cvmx_smix_wr_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t pending                      : 1;  /**< Write Xaction Pending */
	uint64_t val                          : 1;  /**< Write Data Valid */
	uint64_t dat                          : 16; /**< Write Data */
#else
	uint64_t dat                          : 16;
	uint64_t val                          : 1;
	uint64_t pending                      : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_smix_wr_dat_s             cn30xx;
	struct cvmx_smix_wr_dat_s             cn31xx;
	struct cvmx_smix_wr_dat_s             cn38xx;
	struct cvmx_smix_wr_dat_s             cn38xxp2;
	struct cvmx_smix_wr_dat_s             cn50xx;
	struct cvmx_smix_wr_dat_s             cn52xx;
	struct cvmx_smix_wr_dat_s             cn52xxp1;
	struct cvmx_smix_wr_dat_s             cn56xx;
	struct cvmx_smix_wr_dat_s             cn56xxp1;
	struct cvmx_smix_wr_dat_s             cn58xx;
	struct cvmx_smix_wr_dat_s             cn58xxp1;
	struct cvmx_smix_wr_dat_s             cn61xx;
	struct cvmx_smix_wr_dat_s             cn63xx;
	struct cvmx_smix_wr_dat_s             cn63xxp1;
	struct cvmx_smix_wr_dat_s             cn66xx;
	struct cvmx_smix_wr_dat_s             cn68xx;
	struct cvmx_smix_wr_dat_s             cn68xxp1;
	struct cvmx_smix_wr_dat_s             cnf71xx;
};
typedef union cvmx_smix_wr_dat cvmx_smix_wr_dat_t;

#endif
