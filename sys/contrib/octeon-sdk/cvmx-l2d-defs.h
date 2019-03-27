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
 * cvmx-l2d-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon l2d.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_L2D_DEFS_H__
#define __CVMX_L2D_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_BST0 CVMX_L2D_BST0_FUNC()
static inline uint64_t CVMX_L2D_BST0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_BST0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000780ull);
}
#else
#define CVMX_L2D_BST0 (CVMX_ADD_IO_SEG(0x0001180080000780ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_BST1 CVMX_L2D_BST1_FUNC()
static inline uint64_t CVMX_L2D_BST1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_BST1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000788ull);
}
#else
#define CVMX_L2D_BST1 (CVMX_ADD_IO_SEG(0x0001180080000788ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_BST2 CVMX_L2D_BST2_FUNC()
static inline uint64_t CVMX_L2D_BST2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_BST2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000790ull);
}
#else
#define CVMX_L2D_BST2 (CVMX_ADD_IO_SEG(0x0001180080000790ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_BST3 CVMX_L2D_BST3_FUNC()
static inline uint64_t CVMX_L2D_BST3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_BST3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000798ull);
}
#else
#define CVMX_L2D_BST3 (CVMX_ADD_IO_SEG(0x0001180080000798ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_ERR CVMX_L2D_ERR_FUNC()
static inline uint64_t CVMX_L2D_ERR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_ERR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000010ull);
}
#else
#define CVMX_L2D_ERR (CVMX_ADD_IO_SEG(0x0001180080000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FADR CVMX_L2D_FADR_FUNC()
static inline uint64_t CVMX_L2D_FADR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FADR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000018ull);
}
#else
#define CVMX_L2D_FADR (CVMX_ADD_IO_SEG(0x0001180080000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FSYN0 CVMX_L2D_FSYN0_FUNC()
static inline uint64_t CVMX_L2D_FSYN0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FSYN0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000020ull);
}
#else
#define CVMX_L2D_FSYN0 (CVMX_ADD_IO_SEG(0x0001180080000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FSYN1 CVMX_L2D_FSYN1_FUNC()
static inline uint64_t CVMX_L2D_FSYN1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FSYN1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000028ull);
}
#else
#define CVMX_L2D_FSYN1 (CVMX_ADD_IO_SEG(0x0001180080000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FUS0 CVMX_L2D_FUS0_FUNC()
static inline uint64_t CVMX_L2D_FUS0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FUS0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800800007A0ull);
}
#else
#define CVMX_L2D_FUS0 (CVMX_ADD_IO_SEG(0x00011800800007A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FUS1 CVMX_L2D_FUS1_FUNC()
static inline uint64_t CVMX_L2D_FUS1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FUS1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800800007A8ull);
}
#else
#define CVMX_L2D_FUS1 (CVMX_ADD_IO_SEG(0x00011800800007A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FUS2 CVMX_L2D_FUS2_FUNC()
static inline uint64_t CVMX_L2D_FUS2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FUS2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800800007B0ull);
}
#else
#define CVMX_L2D_FUS2 (CVMX_ADD_IO_SEG(0x00011800800007B0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2D_FUS3 CVMX_L2D_FUS3_FUNC()
static inline uint64_t CVMX_L2D_FUS3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2D_FUS3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800800007B8ull);
}
#else
#define CVMX_L2D_FUS3 (CVMX_ADD_IO_SEG(0x00011800800007B8ull))
#endif

/**
 * cvmx_l2d_bst0
 *
 * L2D_BST0 = L2C Data Store QUAD0 BIST Status Register
 *
 */
union cvmx_l2d_bst0 {
	uint64_t u64;
	struct cvmx_l2d_bst0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t ftl                          : 1;  /**< L2C Data Store Fatal Defect(across all QUADs)
                                                         2 or more columns were detected bad across all
                                                         QUADs[0-3]. Please refer to individual quad failures
                                                         for bad column = 0x7e to determine which QUAD was in
                                                         error. */
	uint64_t q0stat                       : 34; /**< Bist Results for QUAD0
                                                         Failure \#1 Status
                                                           [16:14] bad bank
                                                           [13:7] bad high column
                                                           [6:0] bad low column
                                                         Failure \#2 Status
                                                           [33:31] bad bank
                                                           [30:24] bad high column
                                                           [23:17] bad low column
                                                         NOTES: For bad high/low column reporting:
                                                            0x7f:   No failure
                                                            0x7e:   Fatal Defect: 2 or more bad columns
                                                            0-0x45: Bad column
                                                         NOTE: If there are less than 2 failures then the
                                                            bad bank will be 0x7. */
#else
	uint64_t q0stat                       : 34;
	uint64_t ftl                          : 1;
	uint64_t reserved_35_63               : 29;
#endif
	} s;
	struct cvmx_l2d_bst0_s                cn30xx;
	struct cvmx_l2d_bst0_s                cn31xx;
	struct cvmx_l2d_bst0_s                cn38xx;
	struct cvmx_l2d_bst0_s                cn38xxp2;
	struct cvmx_l2d_bst0_s                cn50xx;
	struct cvmx_l2d_bst0_s                cn52xx;
	struct cvmx_l2d_bst0_s                cn52xxp1;
	struct cvmx_l2d_bst0_s                cn56xx;
	struct cvmx_l2d_bst0_s                cn56xxp1;
	struct cvmx_l2d_bst0_s                cn58xx;
	struct cvmx_l2d_bst0_s                cn58xxp1;
};
typedef union cvmx_l2d_bst0 cvmx_l2d_bst0_t;

/**
 * cvmx_l2d_bst1
 *
 * L2D_BST1 = L2C Data Store QUAD1 BIST Status Register
 *
 */
union cvmx_l2d_bst1 {
	uint64_t u64;
	struct cvmx_l2d_bst1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t q1stat                       : 34; /**< Bist Results for QUAD1
                                                         Failure \#1 Status
                                                            [16:14] bad bank
                                                            [13:7] bad high column
                                                            [6:0] bad low column
                                                          Failure \#2 Status
                                                            [33:31] bad bank
                                                            [30:24] bad high column
                                                            [23:17] bad low column
                                                          NOTES: For bad high/low column reporting:
                                                             0x7f:   No failure
                                                             0x7e:   Fatal Defect: 2 or more bad columns
                                                             0-0x45: Bad column
                                                          NOTE: If there are less than 2 failures then the
                                                             bad bank will be 0x7. */
#else
	uint64_t q1stat                       : 34;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_l2d_bst1_s                cn30xx;
	struct cvmx_l2d_bst1_s                cn31xx;
	struct cvmx_l2d_bst1_s                cn38xx;
	struct cvmx_l2d_bst1_s                cn38xxp2;
	struct cvmx_l2d_bst1_s                cn50xx;
	struct cvmx_l2d_bst1_s                cn52xx;
	struct cvmx_l2d_bst1_s                cn52xxp1;
	struct cvmx_l2d_bst1_s                cn56xx;
	struct cvmx_l2d_bst1_s                cn56xxp1;
	struct cvmx_l2d_bst1_s                cn58xx;
	struct cvmx_l2d_bst1_s                cn58xxp1;
};
typedef union cvmx_l2d_bst1 cvmx_l2d_bst1_t;

/**
 * cvmx_l2d_bst2
 *
 * L2D_BST2 = L2C Data Store QUAD2 BIST Status Register
 *
 */
union cvmx_l2d_bst2 {
	uint64_t u64;
	struct cvmx_l2d_bst2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t q2stat                       : 34; /**< Bist Results for QUAD2
                                                         Failure \#1 Status
                                                            [16:14] bad bank
                                                            [13:7] bad high column
                                                            [6:0] bad low column
                                                          Failure \#2 Status
                                                            [33:31] bad bank
                                                            [30:24] bad high column
                                                            [23:17] bad low column
                                                          NOTES: For bad high/low column reporting:
                                                             0x7f:   No failure
                                                             0x7e:   Fatal Defect: 2 or more bad columns
                                                             0-0x45: Bad column
                                                          NOTE: If there are less than 2 failures then the
                                                             bad bank will be 0x7. */
#else
	uint64_t q2stat                       : 34;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_l2d_bst2_s                cn30xx;
	struct cvmx_l2d_bst2_s                cn31xx;
	struct cvmx_l2d_bst2_s                cn38xx;
	struct cvmx_l2d_bst2_s                cn38xxp2;
	struct cvmx_l2d_bst2_s                cn50xx;
	struct cvmx_l2d_bst2_s                cn52xx;
	struct cvmx_l2d_bst2_s                cn52xxp1;
	struct cvmx_l2d_bst2_s                cn56xx;
	struct cvmx_l2d_bst2_s                cn56xxp1;
	struct cvmx_l2d_bst2_s                cn58xx;
	struct cvmx_l2d_bst2_s                cn58xxp1;
};
typedef union cvmx_l2d_bst2 cvmx_l2d_bst2_t;

/**
 * cvmx_l2d_bst3
 *
 * L2D_BST3 = L2C Data Store QUAD3 BIST Status Register
 *
 */
union cvmx_l2d_bst3 {
	uint64_t u64;
	struct cvmx_l2d_bst3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t q3stat                       : 34; /**< Bist Results for QUAD3
                                                         Failure \#1 Status
                                                            [16:14] bad bank
                                                            [13:7] bad high column
                                                            [6:0] bad low column
                                                          Failure \#2 Status
                                                            [33:31] bad bank
                                                            [30:24] bad high column
                                                            [23:17] bad low column
                                                          NOTES: For bad high/low column reporting:
                                                             0x7f:   No failure
                                                             0x7e:   Fatal Defect: 2 or more bad columns
                                                             0-0x45: Bad column
                                                          NOTE: If there are less than 2 failures then the
                                                             bad bank will be 0x7. */
#else
	uint64_t q3stat                       : 34;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_l2d_bst3_s                cn30xx;
	struct cvmx_l2d_bst3_s                cn31xx;
	struct cvmx_l2d_bst3_s                cn38xx;
	struct cvmx_l2d_bst3_s                cn38xxp2;
	struct cvmx_l2d_bst3_s                cn50xx;
	struct cvmx_l2d_bst3_s                cn52xx;
	struct cvmx_l2d_bst3_s                cn52xxp1;
	struct cvmx_l2d_bst3_s                cn56xx;
	struct cvmx_l2d_bst3_s                cn56xxp1;
	struct cvmx_l2d_bst3_s                cn58xx;
	struct cvmx_l2d_bst3_s                cn58xxp1;
};
typedef union cvmx_l2d_bst3 cvmx_l2d_bst3_t;

/**
 * cvmx_l2d_err
 *
 * L2D_ERR = L2 Data Errors
 *
 * Description: L2 Data ECC SEC/DED Errors and Interrupt Enable
 */
union cvmx_l2d_err {
	uint64_t u64;
	struct cvmx_l2d_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t bmhclsel                     : 1;  /**< L2 Bit Map Half CacheLine ECC Selector

                                                          When L2C_DBG[L2T]=1/L2D_ERR[ECC_ENA]=0, the BMHCLSEL selects
                                                          which half cacheline to conditionally latch into
                                                          the L2D_FSYN0/L2D_FSYN1 registers when an LDD command
                                                          is detected from the diagnostic PP (see L2C_DBG[PPNUM]).
                                                         - 0: OW[0-3] ECC (from first 1/2 cacheline) is selected to
                                                             be conditionally latched into the L2D_FSYN0/1 CSRs.
                                                         - 1: OW[4-7] ECC (from last 1/2 cacheline) is selected to
                                                             be conditionally latched into
                                                             the L2D_FSYN0/1 CSRs. */
	uint64_t ded_err                      : 1;  /**< L2D Double Error detected (DED) */
	uint64_t sec_err                      : 1;  /**< L2D Single Error corrected (SEC) */
	uint64_t ded_intena                   : 1;  /**< L2 Data ECC Double Error Detect(DED) Interrupt Enable bit
                                                         When set, allows interrupts to be reported on double bit
                                                         (uncorrectable) errors from the L2 Data Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Data ECC Single Error Correct(SEC) Interrupt Enable bit
                                                         When set, allows interrupts to be reported on single bit
                                                         (correctable) errors from the L2 Data Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Data ECC Enable
                                                         When set, enables 10-bit SEC/DED codeword for 128bit L2
                                                         Data Arrays. */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t bmhclsel                     : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_l2d_err_s                 cn30xx;
	struct cvmx_l2d_err_s                 cn31xx;
	struct cvmx_l2d_err_s                 cn38xx;
	struct cvmx_l2d_err_s                 cn38xxp2;
	struct cvmx_l2d_err_s                 cn50xx;
	struct cvmx_l2d_err_s                 cn52xx;
	struct cvmx_l2d_err_s                 cn52xxp1;
	struct cvmx_l2d_err_s                 cn56xx;
	struct cvmx_l2d_err_s                 cn56xxp1;
	struct cvmx_l2d_err_s                 cn58xx;
	struct cvmx_l2d_err_s                 cn58xxp1;
};
typedef union cvmx_l2d_err cvmx_l2d_err_t;

/**
 * cvmx_l2d_fadr
 *
 * L2D_FADR = L2 Failing Address
 *
 * Description: L2 Data ECC SEC/DED Failing Address
 *
 * Notes:
 * When L2D_SEC_ERR or L2D_DED_ERR are set, this field contains the failing L2 Data store index.
 * (A DED Error will always overwrite a SEC Error SYNDROME and FADR).
 */
union cvmx_l2d_fadr {
	uint64_t u64;
	struct cvmx_l2d_fadr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t fadru                        : 1;  /**< Failing L2 Data Store Upper Index bit(MSB) */
	uint64_t fowmsk                       : 4;  /**< Failing OW Mask (which one of 4 OWs contained SEC/DED
                                                         error) */
	uint64_t fset                         : 3;  /**< Failing SET# */
	uint64_t fadr                         : 11; /**< Failing L2 Data Store Lower Index bits
                                                         (NOTE: L2 Data Store Index is for each 1/2 cacheline)
                                                            [FADRU, FADR[10:1]]: cacheline index[17:7]
                                                            FADR[0]: 1/2 cacheline index
                                                         NOTE: FADR[1] is used to select between upper/lower 1MB
                                                         physical L2 Data Store banks. */
#else
	uint64_t fadr                         : 11;
	uint64_t fset                         : 3;
	uint64_t fowmsk                       : 4;
	uint64_t fadru                        : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_l2d_fadr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t fowmsk                       : 4;  /**< Failing OW Mask (which one of 4 OWs contained SEC/DED
                                                         error) */
	uint64_t reserved_13_13               : 1;
	uint64_t fset                         : 2;  /**< Failing SET# */
	uint64_t reserved_9_10                : 2;
	uint64_t fadr                         : 9;  /**< Failing L2 Data Store Index(1of512 = 1/2 CL address) */
#else
	uint64_t fadr                         : 9;
	uint64_t reserved_9_10                : 2;
	uint64_t fset                         : 2;
	uint64_t reserved_13_13               : 1;
	uint64_t fowmsk                       : 4;
	uint64_t reserved_18_63               : 46;
#endif
	} cn30xx;
	struct cvmx_l2d_fadr_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t fowmsk                       : 4;  /**< Failing OW Mask (which one of 4 OWs contained SEC/DED
                                                         error) */
	uint64_t reserved_13_13               : 1;
	uint64_t fset                         : 2;  /**< Failing SET# */
	uint64_t reserved_10_10               : 1;
	uint64_t fadr                         : 10; /**< Failing L2 Data Store Index
                                                         (1 of 1024 = half cacheline indices) */
#else
	uint64_t fadr                         : 10;
	uint64_t reserved_10_10               : 1;
	uint64_t fset                         : 2;
	uint64_t reserved_13_13               : 1;
	uint64_t fowmsk                       : 4;
	uint64_t reserved_18_63               : 46;
#endif
	} cn31xx;
	struct cvmx_l2d_fadr_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t fowmsk                       : 4;  /**< Failing OW Mask (which one of 4 OWs contained SEC/DED
                                                         error) */
	uint64_t fset                         : 3;  /**< Failing SET# */
	uint64_t fadr                         : 11; /**< Failing L2 Data Store Index (1of2K = 1/2 CL address) */
#else
	uint64_t fadr                         : 11;
	uint64_t fset                         : 3;
	uint64_t fowmsk                       : 4;
	uint64_t reserved_18_63               : 46;
#endif
	} cn38xx;
	struct cvmx_l2d_fadr_cn38xx           cn38xxp2;
	struct cvmx_l2d_fadr_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t fowmsk                       : 4;  /**< Failing OW Mask (which one of 4 OWs contained SEC/DED
                                                         error) */
	uint64_t fset                         : 3;  /**< Failing SET# */
	uint64_t reserved_8_10                : 3;
	uint64_t fadr                         : 8;  /**< Failing L2 Data Store Lower Index bits
                                                         (NOTE: L2 Data Store Index is for each 1/2 cacheline)
                                                            FADR[7:1]: cacheline index[13:7]
                                                            FADR[0]: 1/2 cacheline index */
#else
	uint64_t fadr                         : 8;
	uint64_t reserved_8_10                : 3;
	uint64_t fset                         : 3;
	uint64_t fowmsk                       : 4;
	uint64_t reserved_18_63               : 46;
#endif
	} cn50xx;
	struct cvmx_l2d_fadr_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t fowmsk                       : 4;  /**< Failing OW Mask (which one of 4 OWs contained SEC/DED
                                                         error) */
	uint64_t fset                         : 3;  /**< Failing SET# */
	uint64_t reserved_10_10               : 1;
	uint64_t fadr                         : 10; /**< Failing L2 Data Store Lower Index bits
                                                         (NOTE: L2 Data Store Index is for each 1/2 cacheline)
                                                            FADR[9:1]: cacheline index[15:7]
                                                            FADR[0]: 1/2 cacheline index */
#else
	uint64_t fadr                         : 10;
	uint64_t reserved_10_10               : 1;
	uint64_t fset                         : 3;
	uint64_t fowmsk                       : 4;
	uint64_t reserved_18_63               : 46;
#endif
	} cn52xx;
	struct cvmx_l2d_fadr_cn52xx           cn52xxp1;
	struct cvmx_l2d_fadr_s                cn56xx;
	struct cvmx_l2d_fadr_s                cn56xxp1;
	struct cvmx_l2d_fadr_s                cn58xx;
	struct cvmx_l2d_fadr_s                cn58xxp1;
};
typedef union cvmx_l2d_fadr cvmx_l2d_fadr_t;

/**
 * cvmx_l2d_fsyn0
 *
 * L2D_FSYN0 = L2 Failing Syndrome [OW0,4 / OW1,5]
 *
 * Description: L2 Data ECC SEC/DED Failing Syndrome for lower cache line
 *
 * Notes:
 * When L2D_SEC_ERR or L2D_DED_ERR are set, this field contains the failing L2 Data ECC 10b syndrome.
 * (A DED Error will always overwrite a SEC Error SYNDROME and FADR).
 */
union cvmx_l2d_fsyn0 {
	uint64_t u64;
	struct cvmx_l2d_fsyn0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t fsyn_ow1                     : 10; /**< Failing L2 Data Store SYNDROME OW[1,5]
                                                         When L2D_ERR[ECC_ENA]=1 and either L2D_ERR[SEC_ERR]
                                                         or L2D_ERR[DED_ERR] are set, this field represents
                                                         the failing OWECC syndrome for the half cacheline
                                                         indexed by L2D_FADR[FADR].
                                                         NOTE: The L2D_FADR[FOWMSK] further qualifies which
                                                         OW lane(1of4) detected the error.
                                                         When L2C_DBG[L2T]=1 and L2D_ERR[ECC_ENA]=0, an LDD
                                                         command from the diagnostic PP will conditionally latch
                                                         the raw OWECC for the selected half cacheline.
                                                         (see: L2D_ERR[BMHCLSEL] */
	uint64_t fsyn_ow0                     : 10; /**< Failing L2 Data Store SYNDROME OW[0,4]
                                                         When L2D_ERR[ECC_ENA]=1 and either L2D_ERR[SEC_ERR]
                                                         or L2D_ERR[DED_ERR] are set, this field represents
                                                         the failing OWECC syndrome for the half cacheline
                                                         indexed by L2D_FADR[FADR].
                                                         NOTE: The L2D_FADR[FOWMSK] further qualifies which
                                                         OW lane(1of4) detected the error.
                                                         When L2C_DBG[L2T]=1 and L2D_ERR[ECC_ENA]=0, an LDD
                                                         (L1 load-miss) from the diagnostic PP will conditionally
                                                         latch the raw OWECC for the selected half cacheline.
                                                         (see: L2D_ERR[BMHCLSEL] */
#else
	uint64_t fsyn_ow0                     : 10;
	uint64_t fsyn_ow1                     : 10;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_l2d_fsyn0_s               cn30xx;
	struct cvmx_l2d_fsyn0_s               cn31xx;
	struct cvmx_l2d_fsyn0_s               cn38xx;
	struct cvmx_l2d_fsyn0_s               cn38xxp2;
	struct cvmx_l2d_fsyn0_s               cn50xx;
	struct cvmx_l2d_fsyn0_s               cn52xx;
	struct cvmx_l2d_fsyn0_s               cn52xxp1;
	struct cvmx_l2d_fsyn0_s               cn56xx;
	struct cvmx_l2d_fsyn0_s               cn56xxp1;
	struct cvmx_l2d_fsyn0_s               cn58xx;
	struct cvmx_l2d_fsyn0_s               cn58xxp1;
};
typedef union cvmx_l2d_fsyn0 cvmx_l2d_fsyn0_t;

/**
 * cvmx_l2d_fsyn1
 *
 * L2D_FSYN1 = L2 Failing Syndrome [OW2,6 / OW3,7]
 *
 * Description: L2 Data ECC SEC/DED Failing Syndrome for upper cache line
 *
 * Notes:
 * When L2D_SEC_ERR or L2D_DED_ERR are set, this field contains the failing L2 Data ECC 10b syndrome.
 * (A DED Error will always overwrite a SEC Error SYNDROME and FADR).
 */
union cvmx_l2d_fsyn1 {
	uint64_t u64;
	struct cvmx_l2d_fsyn1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t fsyn_ow3                     : 10; /**< Failing L2 Data Store SYNDROME OW[3,7] */
	uint64_t fsyn_ow2                     : 10; /**< Failing L2 Data Store SYNDROME OW[2,5] */
#else
	uint64_t fsyn_ow2                     : 10;
	uint64_t fsyn_ow3                     : 10;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_l2d_fsyn1_s               cn30xx;
	struct cvmx_l2d_fsyn1_s               cn31xx;
	struct cvmx_l2d_fsyn1_s               cn38xx;
	struct cvmx_l2d_fsyn1_s               cn38xxp2;
	struct cvmx_l2d_fsyn1_s               cn50xx;
	struct cvmx_l2d_fsyn1_s               cn52xx;
	struct cvmx_l2d_fsyn1_s               cn52xxp1;
	struct cvmx_l2d_fsyn1_s               cn56xx;
	struct cvmx_l2d_fsyn1_s               cn56xxp1;
	struct cvmx_l2d_fsyn1_s               cn58xx;
	struct cvmx_l2d_fsyn1_s               cn58xxp1;
};
typedef union cvmx_l2d_fsyn1 cvmx_l2d_fsyn1_t;

/**
 * cvmx_l2d_fus0
 *
 * L2D_FUS0 = L2C Data Store QUAD0 Fuse Register
 *
 */
union cvmx_l2d_fus0 {
	uint64_t u64;
	struct cvmx_l2d_fus0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t q0fus                        : 34; /**< Fuse Register for QUAD0
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuse are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q0fus                        : 34;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_l2d_fus0_s                cn30xx;
	struct cvmx_l2d_fus0_s                cn31xx;
	struct cvmx_l2d_fus0_s                cn38xx;
	struct cvmx_l2d_fus0_s                cn38xxp2;
	struct cvmx_l2d_fus0_s                cn50xx;
	struct cvmx_l2d_fus0_s                cn52xx;
	struct cvmx_l2d_fus0_s                cn52xxp1;
	struct cvmx_l2d_fus0_s                cn56xx;
	struct cvmx_l2d_fus0_s                cn56xxp1;
	struct cvmx_l2d_fus0_s                cn58xx;
	struct cvmx_l2d_fus0_s                cn58xxp1;
};
typedef union cvmx_l2d_fus0 cvmx_l2d_fus0_t;

/**
 * cvmx_l2d_fus1
 *
 * L2D_FUS1 = L2C Data Store QUAD1 Fuse Register
 *
 */
union cvmx_l2d_fus1 {
	uint64_t u64;
	struct cvmx_l2d_fus1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t q1fus                        : 34; /**< Fuse Register for QUAD1
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuse are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q1fus                        : 34;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_l2d_fus1_s                cn30xx;
	struct cvmx_l2d_fus1_s                cn31xx;
	struct cvmx_l2d_fus1_s                cn38xx;
	struct cvmx_l2d_fus1_s                cn38xxp2;
	struct cvmx_l2d_fus1_s                cn50xx;
	struct cvmx_l2d_fus1_s                cn52xx;
	struct cvmx_l2d_fus1_s                cn52xxp1;
	struct cvmx_l2d_fus1_s                cn56xx;
	struct cvmx_l2d_fus1_s                cn56xxp1;
	struct cvmx_l2d_fus1_s                cn58xx;
	struct cvmx_l2d_fus1_s                cn58xxp1;
};
typedef union cvmx_l2d_fus1 cvmx_l2d_fus1_t;

/**
 * cvmx_l2d_fus2
 *
 * L2D_FUS2 = L2C Data Store QUAD2 Fuse Register
 *
 */
union cvmx_l2d_fus2 {
	uint64_t u64;
	struct cvmx_l2d_fus2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t q2fus                        : 34; /**< Fuse Register for QUAD2
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuse are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q2fus                        : 34;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_l2d_fus2_s                cn30xx;
	struct cvmx_l2d_fus2_s                cn31xx;
	struct cvmx_l2d_fus2_s                cn38xx;
	struct cvmx_l2d_fus2_s                cn38xxp2;
	struct cvmx_l2d_fus2_s                cn50xx;
	struct cvmx_l2d_fus2_s                cn52xx;
	struct cvmx_l2d_fus2_s                cn52xxp1;
	struct cvmx_l2d_fus2_s                cn56xx;
	struct cvmx_l2d_fus2_s                cn56xxp1;
	struct cvmx_l2d_fus2_s                cn58xx;
	struct cvmx_l2d_fus2_s                cn58xxp1;
};
typedef union cvmx_l2d_fus2 cvmx_l2d_fus2_t;

/**
 * cvmx_l2d_fus3
 *
 * L2D_FUS3 = L2C Data Store QUAD3 Fuse Register
 *
 */
union cvmx_l2d_fus3 {
	uint64_t u64;
	struct cvmx_l2d_fus3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t ema_ctl                      : 3;  /**< L2 Data Store EMA Control
                                                         These bits are used to 'observe' the EMA[1:0] inputs
                                                         for the L2 Data Store RAMs which are controlled by
                                                         either FUSES[141:140] or by MIO_FUSE_EMA[EMA] CSR.
                                                         From poweron (dc_ok), the EMA_CTL are driven from
                                                         FUSE[141:140]. However after the 1st CSR write to the
                                                         MIO_FUSE_EMA[EMA] bits, the EMA_CTL will source
                                                         from the MIO_FUSE_EMA[EMA] register permanently
                                                         (until dc_ok). */
	uint64_t reserved_34_36               : 3;
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t reserved_34_36               : 3;
	uint64_t ema_ctl                      : 3;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_l2d_fus3_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t crip_64k                     : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1. */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:15] UNUSED
                                                             [14]    bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:32] UNUSED
                                                             [31]    bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_64k                     : 1;
	uint64_t reserved_35_63               : 29;
#endif
	} cn30xx;
	struct cvmx_l2d_fus3_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_35_63               : 29;
	uint64_t crip_128k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1. */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:15] UNUSED
                                                             [14]    bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:32] UNUSED
                                                             [31]    bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_128k                    : 1;
	uint64_t reserved_35_63               : 29;
#endif
	} cn31xx;
	struct cvmx_l2d_fus3_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t crip_256k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1.
                                                         *** NOTE: Pass2 Addition */
	uint64_t crip_512k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1.
                                                         *** NOTE: Pass2 Addition */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_512k                    : 1;
	uint64_t crip_256k                    : 1;
	uint64_t reserved_36_63               : 28;
#endif
	} cn38xx;
	struct cvmx_l2d_fus3_cn38xx           cn38xxp2;
	struct cvmx_l2d_fus3_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t ema_ctl                      : 3;  /**< L2 Data Store EMA Control
                                                         These bits are used to 'observe' the EMA[2:0] inputs
                                                         for the L2 Data Store RAMs which are controlled by
                                                         either FUSES[142:140] or by MIO_FUSE_EMA[EMA] CSR.
                                                         From poweron (dc_ok), the EMA_CTL are driven from
                                                         FUSE[141:140]. However after the 1st CSR write to the
                                                         MIO_FUSE_EMA[EMA] bits, the EMA_CTL will source
                                                         from the MIO_FUSE_EMA[EMA] register permanently
                                                         (until dc_ok). */
	uint64_t reserved_36_36               : 1;
	uint64_t crip_32k                     : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1. */
	uint64_t crip_64k                     : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1. */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] UNUSED (5020 uses single physical bank per quad)
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] UNUSED (5020 uses single physical bank per quad)
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_64k                     : 1;
	uint64_t crip_32k                     : 1;
	uint64_t reserved_36_36               : 1;
	uint64_t ema_ctl                      : 3;
	uint64_t reserved_40_63               : 24;
#endif
	} cn50xx;
	struct cvmx_l2d_fus3_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t ema_ctl                      : 3;  /**< L2 Data Store EMA Control
                                                         These bits are used to 'observe' the EMA[2:0] inputs
                                                         for the L2 Data Store RAMs which are controlled by
                                                         either FUSES[142:140] or by MIO_FUSE_EMA[EMA] CSR.
                                                         From poweron (dc_ok), the EMA_CTL are driven from
                                                         FUSE[141:140]. However after the 1st CSR write to the
                                                         MIO_FUSE_EMA[EMA] bits, the EMA_CTL will source
                                                         from the MIO_FUSE_EMA[EMA] register permanently
                                                         (until dc_ok). */
	uint64_t reserved_36_36               : 1;
	uint64_t crip_128k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1. */
	uint64_t crip_256k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1. */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] UNUSED (5020 uses single physical bank per quad)
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] UNUSED (5020 uses single physical bank per quad)
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_256k                    : 1;
	uint64_t crip_128k                    : 1;
	uint64_t reserved_36_36               : 1;
	uint64_t ema_ctl                      : 3;
	uint64_t reserved_40_63               : 24;
#endif
	} cn52xx;
	struct cvmx_l2d_fus3_cn52xx           cn52xxp1;
	struct cvmx_l2d_fus3_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t ema_ctl                      : 3;  /**< L2 Data Store EMA Control
                                                         These bits are used to 'observe' the EMA[2:0] inputs
                                                         for the L2 Data Store RAMs which are controlled by
                                                         either FUSES[142:140] or by MIO_FUSE_EMA[EMA] CSR.
                                                         From poweron (dc_ok), the EMA_CTL are driven from
                                                         FUSE[141:140]. However after the 1st CSR write to the
                                                         MIO_FUSE_EMA[EMA] bits, the EMA_CTL will source
                                                         from the MIO_FUSE_EMA[EMA] register permanently
                                                         (until dc_ok). */
	uint64_t reserved_36_36               : 1;
	uint64_t crip_512k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1.
                                                         *** NOTE: Pass2 Addition */
	uint64_t crip_1024k                   : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1.
                                                         *** NOTE: Pass2 Addition */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_1024k                   : 1;
	uint64_t crip_512k                    : 1;
	uint64_t reserved_36_36               : 1;
	uint64_t ema_ctl                      : 3;
	uint64_t reserved_40_63               : 24;
#endif
	} cn56xx;
	struct cvmx_l2d_fus3_cn56xx           cn56xxp1;
	struct cvmx_l2d_fus3_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t ema_ctl                      : 2;  /**< L2 Data Store EMA Control
                                                         These bits are used to 'observe' the EMA[1:0] inputs
                                                         for the L2 Data Store RAMs which are controlled by
                                                         either FUSES[141:140] or by MIO_FUSE_EMA[EMA] CSR.
                                                         From poweron (dc_ok), the EMA_CTL are driven from
                                                         FUSE[141:140]. However after the 1st CSR write to the
                                                         MIO_FUSE_EMA[EMA] bits, the EMA_CTL will source
                                                         from the MIO_FUSE_EMA[EMA] register permanently
                                                         (until dc_ok). */
	uint64_t reserved_36_36               : 1;
	uint64_t crip_512k                    : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1.
                                                         *** NOTE: Pass2 Addition */
	uint64_t crip_1024k                   : 1;  /**< This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         If the FUSE is not-blown, then this bit should read
                                                         as 0. If the FUSE is blown, then this bit should read
                                                         as 1.
                                                         *** NOTE: Pass2 Addition */
	uint64_t q3fus                        : 34; /**< Fuse Register for QUAD3
                                                         This is purely for debug and not needed in the general
                                                         manufacturing flow.
                                                         Note that the fuses are complementary (Assigning a
                                                         fuse to 1 will read as a zero). This means the case
                                                         where no fuses are blown result in these csr's showing
                                                         all ones.
                                                          Failure \#1 Fuse Mapping
                                                             [16:14] bad bank
                                                             [13:7] bad high column
                                                             [6:0] bad low column
                                                           Failure \#2 Fuse Mapping
                                                             [33:31] bad bank
                                                             [30:24] bad high column
                                                             [23:17] bad low column */
#else
	uint64_t q3fus                        : 34;
	uint64_t crip_1024k                   : 1;
	uint64_t crip_512k                    : 1;
	uint64_t reserved_36_36               : 1;
	uint64_t ema_ctl                      : 2;
	uint64_t reserved_39_63               : 25;
#endif
	} cn58xx;
	struct cvmx_l2d_fus3_cn58xx           cn58xxp1;
};
typedef union cvmx_l2d_fus3 cvmx_l2d_fus3_t;

#endif
