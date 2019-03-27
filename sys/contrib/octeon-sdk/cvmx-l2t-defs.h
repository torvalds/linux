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
 * cvmx-l2t-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon l2t.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_L2T_DEFS_H__
#define __CVMX_L2T_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_L2T_ERR CVMX_L2T_ERR_FUNC()
static inline uint64_t CVMX_L2T_ERR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)))
		cvmx_warn("CVMX_L2T_ERR not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180080000008ull);
}
#else
#define CVMX_L2T_ERR (CVMX_ADD_IO_SEG(0x0001180080000008ull))
#endif

/**
 * cvmx_l2t_err
 *
 * L2T_ERR = L2 Tag Errors
 *
 * Description: L2 Tag ECC SEC/DED Errors and Interrupt Enable
 */
union cvmx_l2t_err {
	uint64_t u64;
	struct cvmx_l2t_err_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t fadru                        : 1;  /**< Failing L2 Tag Upper Address Bit (Index[10])
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADRU contains the upper(MSB bit) cacheline index
                                                         into the L2 Tag Store. */
	uint64_t lck_intena2                  : 1;  /**< L2 Tag Lock Error2 Interrupt Enable bit */
	uint64_t lckerr2                      : 1;  /**< HW detected a case where a Rd/Wr Miss from PP#n
                                                         could not find an available/unlocked set (for
                                                         replacement).
                                                         Most likely, this is a result of SW mixing SET
                                                         PARTITIONING with ADDRESS LOCKING. If SW allows
                                                         another PP to LOCKDOWN all SETs available to PP#n,
                                                         then a Rd/Wr Miss from PP#n will be unable
                                                         to determine a 'valid' replacement set (since LOCKED
                                                         addresses should NEVER be replaced).
                                                         If such an event occurs, the HW will select the smallest
                                                         available SET(specified by UMSK'x)' as the replacement
                                                         set, and the address is unlocked. */
	uint64_t lck_intena                   : 1;  /**< L2 Tag Lock Error Interrupt Enable bit */
	uint64_t lckerr                       : 1;  /**< SW attempted to LOCK DOWN the last available set of
                                                         the INDEX (which is ignored by HW - but reported to SW).
                                                         The LDD(L1 load-miss) for the LOCK operation is completed
                                                         successfully, however the address is NOT locked.
                                                         NOTE: 'Available' sets takes the L2C_SPAR*[UMSK*]
                                                         into account. For example, if diagnostic PPx has
                                                         UMSKx defined to only use SETs [1:0], and SET1 had
                                                         been previously LOCKED, then an attempt to LOCK the
                                                         last available SET0 would result in a LCKERR. (This
                                                         is to ensure that at least 1 SET at each INDEX is
                                                         not LOCKED for general use by other PPs). */
	uint64_t fset                         : 3;  /**< Failing L2 Tag Hit Set# (1-of-8)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set and
                                                         (FSYN != 0), the FSET specifies the failing hit-set.
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit-set
                                                         is specified by the L2C_DBG[SET]. */
	uint64_t fadr                         : 10; /**< Failing L2 Tag Address (10-bit Index)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADR contains the lower 10bit cacheline index
                                                         into the L2 Tag Store. */
	uint64_t fsyn                         : 6;  /**< When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the contents of this register contain the 6-bit
                                                         syndrome for the hit set only.
                                                         If (FSYN = 0), the SBE or DBE reported was for one of
                                                         the "non-hit" sets at the failing index(FADR).
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit set
                                                         is specified by the L2C_DBG[SET].
                                                         If (FSYN != 0), the SBE or DBE reported was for the
                                                         hit set at the failing index(FADR) and failing
                                                         set(FSET).
                                                         SW NOTE: To determine which "non-hit" set was in error,
                                                         SW can use the L2C_DBG[L2T] debug feature to explicitly
                                                         read the other sets at the failing index(FADR). When
                                                         (FSYN !=0), then the FSET contains the failing hit-set.
                                                         NOTE: A DED Error will always overwrite a SEC Error
                                                         SYNDROME and FADR). */
	uint64_t ded_err                      : 1;  /**< L2T Double Bit Error detected (DED)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for double bit errors(DBEs).
                                                         This bit is set if ANY of the 8 sets contains a DBE.
                                                         DBEs also generated an interrupt(if enabled). */
	uint64_t sec_err                      : 1;  /**< L2T Single Bit Error corrected (SEC)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for single bit errors(SBEs).
                                                         This bit is set if ANY of the 8 sets contains an SBE.
                                                         SBEs are auto corrected in HW and generate an
                                                         interrupt(if enabled). */
	uint64_t ded_intena                   : 1;  /**< L2 Tag ECC Double Error Detect(DED) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on double bit (uncorrectable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Tag ECC Single Error Correct(SEC) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on single bit (correctable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Tag ECC Enable
                                                         When set, enables 6-bit SEC/DED codeword for 19-bit
                                                         L2 Tag Arrays [V,D,L,TAG[33:18]] */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t fsyn                         : 6;
	uint64_t fadr                         : 10;
	uint64_t fset                         : 3;
	uint64_t lckerr                       : 1;
	uint64_t lck_intena                   : 1;
	uint64_t lckerr2                      : 1;
	uint64_t lck_intena2                  : 1;
	uint64_t fadru                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_l2t_err_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t lck_intena2                  : 1;  /**< L2 Tag Lock Error2 Interrupt Enable bit */
	uint64_t lckerr2                      : 1;  /**< HW detected a case where a Rd/Wr Miss from PP#n
                                                         could not find an available/unlocked set (for
                                                         replacement).
                                                         Most likely, this is a result of SW mixing SET
                                                         PARTITIONING with ADDRESS LOCKING. If SW allows
                                                         another PP to LOCKDOWN all SETs available to PP#n,
                                                         then a Rd/Wr Miss from PP#n will be unable
                                                         to determine a 'valid' replacement set (since LOCKED
                                                         addresses should NEVER be replaced).
                                                         If such an event occurs, the HW will select the smallest
                                                         available SET(specified by UMSK'x)' as the replacement
                                                         set, and the address is unlocked. */
	uint64_t lck_intena                   : 1;  /**< L2 Tag Lock Error Interrupt Enable bit */
	uint64_t lckerr                       : 1;  /**< SW attempted to LOCK DOWN the last available set of
                                                         the INDEX (which is ignored by HW - but reported to SW).
                                                         The LDD(L1 load-miss) for the LOCK operation is
                                                         completed successfully, however the address is NOT
                                                         locked.
                                                         NOTE: 'Available' sets takes the L2C_SPAR*[UMSK*]
                                                         into account. For example, if diagnostic PPx has
                                                         UMSKx defined to only use SETs [1:0], and SET1 had
                                                         been previously LOCKED, then an attempt to LOCK the
                                                         last available SET0 would result in a LCKERR. (This
                                                         is to ensure that at least 1 SET at each INDEX is
                                                         not LOCKED for general use by other PPs). */
	uint64_t reserved_23_23               : 1;
	uint64_t fset                         : 2;  /**< Failing L2 Tag Hit Set# (1-of-4)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set and
                                                         (FSYN != 0), the FSET specifies the failing hit-set.
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit-set
                                                         is specified by the L2C_DBG[SET]. */
	uint64_t reserved_19_20               : 2;
	uint64_t fadr                         : 8;  /**< Failing L2 Tag Store Index (8-bit)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADR contains the 8bit cacheline index into the
                                                         L2 Tag Store. */
	uint64_t fsyn                         : 6;  /**< When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the contents of this register contain the 6-bit
                                                         syndrome for the hit set only.
                                                         If (FSYN = 0), the SBE or DBE reported was for one of
                                                         the "non-hit" sets at the failing index(FADR).
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit set
                                                         is specified by the L2C_DBG[SET].
                                                         If (FSYN != 0), the SBE or DBE reported was for the
                                                         hit set at the failing index(FADR) and failing
                                                         set(FSET).
                                                         SW NOTE: To determine which "non-hit" set was in error,
                                                         SW can use the L2C_DBG[L2T] debug feature to explicitly
                                                         read the other sets at the failing index(FADR). When
                                                         (FSYN !=0), then the FSET contains the failing hit-set.
                                                         NOTE: A DED Error will always overwrite a SEC Error
                                                         SYNDROME and FADR). */
	uint64_t ded_err                      : 1;  /**< L2T Double Bit Error detected (DED)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for double bit errors(DBEs).
                                                         This bit is set if ANY of the 8 sets contains a DBE.
                                                         DBEs also generated an interrupt(if enabled). */
	uint64_t sec_err                      : 1;  /**< L2T Single Bit Error corrected (SEC)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for single bit errors(SBEs).
                                                         This bit is set if ANY of the 8 sets contains an SBE.
                                                         SBEs are auto corrected in HW and generate an
                                                         interrupt(if enabled). */
	uint64_t ded_intena                   : 1;  /**< L2 Tag ECC Double Error Detect(DED) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on double bit (uncorrectable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Tag ECC Single Error Correct(SEC) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on single bit (correctable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Tag ECC Enable
                                                         When set, enables 6-bit SEC/DED codeword for 22-bit
                                                         L2 Tag Arrays [V,D,L,TAG[33:15]] */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t fsyn                         : 6;
	uint64_t fadr                         : 8;
	uint64_t reserved_19_20               : 2;
	uint64_t fset                         : 2;
	uint64_t reserved_23_23               : 1;
	uint64_t lckerr                       : 1;
	uint64_t lck_intena                   : 1;
	uint64_t lckerr2                      : 1;
	uint64_t lck_intena2                  : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn30xx;
	struct cvmx_l2t_err_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t lck_intena2                  : 1;  /**< L2 Tag Lock Error2 Interrupt Enable bit */
	uint64_t lckerr2                      : 1;  /**< HW detected a case where a Rd/Wr Miss from PP#n
                                                         could not find an available/unlocked set (for
                                                         replacement).
                                                         Most likely, this is a result of SW mixing SET
                                                         PARTITIONING with ADDRESS LOCKING. If SW allows
                                                         another PP to LOCKDOWN all SETs available to PP#n,
                                                         then a Rd/Wr Miss from PP#n will be unable
                                                         to determine a 'valid' replacement set (since LOCKED
                                                         addresses should NEVER be replaced).
                                                         If such an event occurs, the HW will select the smallest
                                                         available SET(specified by UMSK'x)' as the replacement
                                                         set, and the address is unlocked. */
	uint64_t lck_intena                   : 1;  /**< L2 Tag Lock Error Interrupt Enable bit */
	uint64_t lckerr                       : 1;  /**< SW attempted to LOCK DOWN the last available set of
                                                         the INDEX (which is ignored by HW - but reported to SW).
                                                         The LDD(L1 load-miss) for the LOCK operation is completed
                                                         successfully, however the address is NOT locked.
                                                         NOTE: 'Available' sets takes the L2C_SPAR*[UMSK*]
                                                         into account. For example, if diagnostic PPx has
                                                         UMSKx defined to only use SETs [1:0], and SET1 had
                                                         been previously LOCKED, then an attempt to LOCK the
                                                         last available SET0 would result in a LCKERR. (This
                                                         is to ensure that at least 1 SET at each INDEX is
                                                         not LOCKED for general use by other PPs). */
	uint64_t reserved_23_23               : 1;
	uint64_t fset                         : 2;  /**< Failing L2 Tag Hit Set# (1-of-4)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set and
                                                         (FSYN != 0), the FSET specifies the failing hit-set.
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit-set
                                                         is specified by the L2C_DBG[SET]. */
	uint64_t reserved_20_20               : 1;
	uint64_t fadr                         : 9;  /**< Failing L2 Tag Address (9-bit Index)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADR contains the 9-bit cacheline index into the
                                                         L2 Tag Store. */
	uint64_t fsyn                         : 6;  /**< When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the contents of this register contain the 6-bit
                                                         syndrome for the hit set only.
                                                         If (FSYN = 0), the SBE or DBE reported was for one of
                                                         the "non-hit" sets at the failing index(FADR).
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit set
                                                         is specified by the L2C_DBG[SET].
                                                         If (FSYN != 0), the SBE or DBE reported was for the
                                                         hit set at the failing index(FADR) and failing
                                                         set(FSET).
                                                         SW NOTE: To determine which "non-hit" set was in error,
                                                         SW can use the L2C_DBG[L2T] debug feature to explicitly
                                                         read the other sets at the failing index(FADR). When
                                                         (FSYN !=0), then the FSET contains the failing hit-set.
                                                         NOTE: A DED Error will always overwrite a SEC Error
                                                         SYNDROME and FADR). */
	uint64_t ded_err                      : 1;  /**< L2T Double Bit Error detected (DED)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for double bit errors(DBEs).
                                                         This bit is set if ANY of the 8 sets contains a DBE.
                                                         DBEs also generated an interrupt(if enabled). */
	uint64_t sec_err                      : 1;  /**< L2T Single Bit Error corrected (SEC)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for single bit errors(SBEs).
                                                         This bit is set if ANY of the 8 sets contains an SBE.
                                                         SBEs are auto corrected in HW and generate an
                                                         interrupt(if enabled). */
	uint64_t ded_intena                   : 1;  /**< L2 Tag ECC Double Error Detect(DED) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on double bit (uncorrectable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Tag ECC Single Error Correct(SEC) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on single bit (correctable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Tag ECC Enable
                                                         When set, enables 6-bit SEC/DED codeword for 21-bit
                                                         L2 Tag Arrays [V,D,L,TAG[33:16]] */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t fsyn                         : 6;
	uint64_t fadr                         : 9;
	uint64_t reserved_20_20               : 1;
	uint64_t fset                         : 2;
	uint64_t reserved_23_23               : 1;
	uint64_t lckerr                       : 1;
	uint64_t lck_intena                   : 1;
	uint64_t lckerr2                      : 1;
	uint64_t lck_intena2                  : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn31xx;
	struct cvmx_l2t_err_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t lck_intena2                  : 1;  /**< L2 Tag Lock Error2 Interrupt Enable bit */
	uint64_t lckerr2                      : 1;  /**< HW detected a case where a Rd/Wr Miss from PP#n
                                                         could not find an available/unlocked set (for
                                                         replacement).
                                                         Most likely, this is a result of SW mixing SET
                                                         PARTITIONING with ADDRESS LOCKING. If SW allows
                                                         another PP to LOCKDOWN all SETs available to PP#n,
                                                         then a Rd/Wr Miss from PP#n will be unable
                                                         to determine a 'valid' replacement set (since LOCKED
                                                         addresses should NEVER be replaced).
                                                         If such an event occurs, the HW will select the smallest
                                                         available SET(specified by UMSK'x)' as the replacement
                                                         set, and the address is unlocked. */
	uint64_t lck_intena                   : 1;  /**< L2 Tag Lock Error Interrupt Enable bit */
	uint64_t lckerr                       : 1;  /**< SW attempted to LOCK DOWN the last available set of
                                                         the INDEX (which is ignored by HW - but reported to SW).
                                                         The LDD(L1 load-miss) for the LOCK operation is completed
                                                         successfully, however the address is NOT locked.
                                                         NOTE: 'Available' sets takes the L2C_SPAR*[UMSK*]
                                                         into account. For example, if diagnostic PPx has
                                                         UMSKx defined to only use SETs [1:0], and SET1 had
                                                         been previously LOCKED, then an attempt to LOCK the
                                                         last available SET0 would result in a LCKERR. (This
                                                         is to ensure that at least 1 SET at each INDEX is
                                                         not LOCKED for general use by other PPs). */
	uint64_t fset                         : 3;  /**< Failing L2 Tag Hit Set# (1-of-8)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set and
                                                         (FSYN != 0), the FSET specifies the failing hit-set.
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit-set
                                                         is specified by the L2C_DBG[SET]. */
	uint64_t fadr                         : 10; /**< Failing L2 Tag Address (10-bit Index)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADR contains the 10bit cacheline index into the
                                                         L2 Tag Store. */
	uint64_t fsyn                         : 6;  /**< When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the contents of this register contain the 6-bit
                                                         syndrome for the hit set only.
                                                         If (FSYN = 0), the SBE or DBE reported was for one of
                                                         the "non-hit" sets at the failing index(FADR).
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit set
                                                         is specified by the L2C_DBG[SET].
                                                         If (FSYN != 0), the SBE or DBE reported was for the
                                                         hit set at the failing index(FADR) and failing
                                                         set(FSET).
                                                         SW NOTE: To determine which "non-hit" set was in error,
                                                         SW can use the L2C_DBG[L2T] debug feature to explicitly
                                                         read the other sets at the failing index(FADR). When
                                                         (FSYN !=0), then the FSET contains the failing hit-set.
                                                         NOTE: A DED Error will always overwrite a SEC Error
                                                         SYNDROME and FADR). */
	uint64_t ded_err                      : 1;  /**< L2T Double Bit Error detected (DED)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for double bit errors(DBEs).
                                                         This bit is set if ANY of the 8 sets contains a DBE.
                                                         DBEs also generated an interrupt(if enabled). */
	uint64_t sec_err                      : 1;  /**< L2T Single Bit Error corrected (SEC)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for single bit errors(SBEs).
                                                         This bit is set if ANY of the 8 sets contains an SBE.
                                                         SBEs are auto corrected in HW and generate an
                                                         interrupt(if enabled). */
	uint64_t ded_intena                   : 1;  /**< L2 Tag ECC Double Error Detect(DED) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on double bit (uncorrectable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Tag ECC Single Error Correct(SEC) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on single bit (correctable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Tag ECC Enable
                                                         When set, enables 6-bit SEC/DED codeword for 20-bit
                                                         L2 Tag Arrays [V,D,L,TAG[33:17]] */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t fsyn                         : 6;
	uint64_t fadr                         : 10;
	uint64_t fset                         : 3;
	uint64_t lckerr                       : 1;
	uint64_t lck_intena                   : 1;
	uint64_t lckerr2                      : 1;
	uint64_t lck_intena2                  : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn38xx;
	struct cvmx_l2t_err_cn38xx            cn38xxp2;
	struct cvmx_l2t_err_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t lck_intena2                  : 1;  /**< L2 Tag Lock Error2 Interrupt Enable bit */
	uint64_t lckerr2                      : 1;  /**< HW detected a case where a Rd/Wr Miss from PP#n
                                                         could not find an available/unlocked set (for
                                                         replacement).
                                                         Most likely, this is a result of SW mixing SET
                                                         PARTITIONING with ADDRESS LOCKING. If SW allows
                                                         another PP to LOCKDOWN all SETs available to PP#n,
                                                         then a Rd/Wr Miss from PP#n will be unable
                                                         to determine a 'valid' replacement set (since LOCKED
                                                         addresses should NEVER be replaced).
                                                         If such an event occurs, the HW will select the smallest
                                                         available SET(specified by UMSK'x)' as the replacement
                                                         set, and the address is unlocked. */
	uint64_t lck_intena                   : 1;  /**< L2 Tag Lock Error Interrupt Enable bit */
	uint64_t lckerr                       : 1;  /**< SW attempted to LOCK DOWN the last available set of
                                                         the INDEX (which is ignored by HW - but reported to SW).
                                                         The LDD(L1 load-miss) for the LOCK operation is completed
                                                         successfully, however the address is NOT locked.
                                                         NOTE: 'Available' sets takes the L2C_SPAR*[UMSK*]
                                                         into account. For example, if diagnostic PPx has
                                                         UMSKx defined to only use SETs [1:0], and SET1 had
                                                         been previously LOCKED, then an attempt to LOCK the
                                                         last available SET0 would result in a LCKERR. (This
                                                         is to ensure that at least 1 SET at each INDEX is
                                                         not LOCKED for general use by other PPs). */
	uint64_t fset                         : 3;  /**< Failing L2 Tag Hit Set# (1-of-8)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set and
                                                         (FSYN != 0), the FSET specifies the failing hit-set.
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit-set
                                                         is specified by the L2C_DBG[SET]. */
	uint64_t reserved_18_20               : 3;
	uint64_t fadr                         : 7;  /**< Failing L2 Tag Address (7-bit Index)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADR contains the lower 7bit cacheline index
                                                         into the L2 Tag Store. */
	uint64_t fsyn                         : 6;  /**< When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the contents of this register contain the 6-bit
                                                         syndrome for the hit set only.
                                                         If (FSYN = 0), the SBE or DBE reported was for one of
                                                         the "non-hit" sets at the failing index(FADR).
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit set
                                                         is specified by the L2C_DBG[SET].
                                                         If (FSYN != 0), the SBE or DBE reported was for the
                                                         hit set at the failing index(FADR) and failing
                                                         set(FSET).
                                                         SW NOTE: To determine which "non-hit" set was in error,
                                                         SW can use the L2C_DBG[L2T] debug feature to explicitly
                                                         read the other sets at the failing index(FADR). When
                                                         (FSYN !=0), then the FSET contains the failing hit-set.
                                                         NOTE: A DED Error will always overwrite a SEC Error
                                                         SYNDROME and FADR). */
	uint64_t ded_err                      : 1;  /**< L2T Double Bit Error detected (DED)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for double bit errors(DBEs).
                                                         This bit is set if ANY of the 8 sets contains a DBE.
                                                         DBEs also generated an interrupt(if enabled). */
	uint64_t sec_err                      : 1;  /**< L2T Single Bit Error corrected (SEC)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for single bit errors(SBEs).
                                                         This bit is set if ANY of the 8 sets contains an SBE.
                                                         SBEs are auto corrected in HW and generate an
                                                         interrupt(if enabled). */
	uint64_t ded_intena                   : 1;  /**< L2 Tag ECC Double Error Detect(DED) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on double bit (uncorrectable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Tag ECC Single Error Correct(SEC) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on single bit (correctable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Tag ECC Enable
                                                         When set, enables 6-bit SEC/DED codeword for 23-bit
                                                         L2 Tag Arrays [V,D,L,TAG[33:14]] */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t fsyn                         : 6;
	uint64_t fadr                         : 7;
	uint64_t reserved_18_20               : 3;
	uint64_t fset                         : 3;
	uint64_t lckerr                       : 1;
	uint64_t lck_intena                   : 1;
	uint64_t lckerr2                      : 1;
	uint64_t lck_intena2                  : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn50xx;
	struct cvmx_l2t_err_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t lck_intena2                  : 1;  /**< L2 Tag Lock Error2 Interrupt Enable bit */
	uint64_t lckerr2                      : 1;  /**< HW detected a case where a Rd/Wr Miss from PP#n
                                                         could not find an available/unlocked set (for
                                                         replacement).
                                                         Most likely, this is a result of SW mixing SET
                                                         PARTITIONING with ADDRESS LOCKING. If SW allows
                                                         another PP to LOCKDOWN all SETs available to PP#n,
                                                         then a Rd/Wr Miss from PP#n will be unable
                                                         to determine a 'valid' replacement set (since LOCKED
                                                         addresses should NEVER be replaced).
                                                         If such an event occurs, the HW will select the smallest
                                                         available SET(specified by UMSK'x)' as the replacement
                                                         set, and the address is unlocked. */
	uint64_t lck_intena                   : 1;  /**< L2 Tag Lock Error Interrupt Enable bit */
	uint64_t lckerr                       : 1;  /**< SW attempted to LOCK DOWN the last available set of
                                                         the INDEX (which is ignored by HW - but reported to SW).
                                                         The LDD(L1 load-miss) for the LOCK operation is completed
                                                         successfully, however the address is NOT locked.
                                                         NOTE: 'Available' sets takes the L2C_SPAR*[UMSK*]
                                                         into account. For example, if diagnostic PPx has
                                                         UMSKx defined to only use SETs [1:0], and SET1 had
                                                         been previously LOCKED, then an attempt to LOCK the
                                                         last available SET0 would result in a LCKERR. (This
                                                         is to ensure that at least 1 SET at each INDEX is
                                                         not LOCKED for general use by other PPs). */
	uint64_t fset                         : 3;  /**< Failing L2 Tag Hit Set# (1-of-8)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set and
                                                         (FSYN != 0), the FSET specifies the failing hit-set.
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit-set
                                                         is specified by the L2C_DBG[SET]. */
	uint64_t reserved_20_20               : 1;
	uint64_t fadr                         : 9;  /**< Failing L2 Tag Address (9-bit Index)
                                                         When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the FADR contains the lower 9bit cacheline index
                                                         into the L2 Tag Store. */
	uint64_t fsyn                         : 6;  /**< When L2T_ERR[SEC_ERR] or L2T_ERR[DED_ERR] are set,
                                                         the contents of this register contain the 6-bit
                                                         syndrome for the hit set only.
                                                         If (FSYN = 0), the SBE or DBE reported was for one of
                                                         the "non-hit" sets at the failing index(FADR).
                                                         NOTE: During a force-hit (L2T/L2D/L2T=1), the hit set
                                                         is specified by the L2C_DBG[SET].
                                                         If (FSYN != 0), the SBE or DBE reported was for the
                                                         hit set at the failing index(FADR) and failing
                                                         set(FSET).
                                                         SW NOTE: To determine which "non-hit" set was in error,
                                                         SW can use the L2C_DBG[L2T] debug feature to explicitly
                                                         read the other sets at the failing index(FADR). When
                                                         (FSYN !=0), then the FSET contains the failing hit-set.
                                                         NOTE: A DED Error will always overwrite a SEC Error
                                                         SYNDROME and FADR). */
	uint64_t ded_err                      : 1;  /**< L2T Double Bit Error detected (DED)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for double bit errors(DBEs).
                                                         This bit is set if ANY of the 8 sets contains a DBE.
                                                         DBEs also generated an interrupt(if enabled). */
	uint64_t sec_err                      : 1;  /**< L2T Single Bit Error corrected (SEC)
                                                         During every L2 Tag Probe, all 8 sets Tag's (at a
                                                         given index) are checked for single bit errors(SBEs).
                                                         This bit is set if ANY of the 8 sets contains an SBE.
                                                         SBEs are auto corrected in HW and generate an
                                                         interrupt(if enabled). */
	uint64_t ded_intena                   : 1;  /**< L2 Tag ECC Double Error Detect(DED) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on double bit (uncorrectable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t sec_intena                   : 1;  /**< L2 Tag ECC Single Error Correct(SEC) Interrupt
                                                         Enable bit. When set, allows interrupts to be
                                                         reported on single bit (correctable) errors from
                                                         the L2 Tag Arrays. */
	uint64_t ecc_ena                      : 1;  /**< L2 Tag ECC Enable
                                                         When set, enables 6-bit SEC/DED codeword for 21-bit
                                                         L2 Tag Arrays [V,D,L,TAG[33:16]] */
#else
	uint64_t ecc_ena                      : 1;
	uint64_t sec_intena                   : 1;
	uint64_t ded_intena                   : 1;
	uint64_t sec_err                      : 1;
	uint64_t ded_err                      : 1;
	uint64_t fsyn                         : 6;
	uint64_t fadr                         : 9;
	uint64_t reserved_20_20               : 1;
	uint64_t fset                         : 3;
	uint64_t lckerr                       : 1;
	uint64_t lck_intena                   : 1;
	uint64_t lckerr2                      : 1;
	uint64_t lck_intena2                  : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn52xx;
	struct cvmx_l2t_err_cn52xx            cn52xxp1;
	struct cvmx_l2t_err_s                 cn56xx;
	struct cvmx_l2t_err_s                 cn56xxp1;
	struct cvmx_l2t_err_s                 cn58xx;
	struct cvmx_l2t_err_s                 cn58xxp1;
};
typedef union cvmx_l2t_err cvmx_l2t_err_t;

#endif
