/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
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
 * @file
 *
 * File defining checks for different Octeon features.
 *
 * <hr>$Revision: 30468 $<hr>
 */

#ifndef __OCTEON_FEATURE_H__
#define __OCTEON_FEATURE_H__

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Errors
 */
typedef enum
{
   OCTEON_FEATURE_SUCCESS                = 0,
   OCTEON_FEATURE_MAP_OVERFLOW           = -1,
} octeon_feature_result_t;

/*
 * Octeon models are declared after the macros in octeon-model.h with the
 * suffix _FEATURE. The individual features are declared with the
 * _FEATURE_ infix.
 */
typedef enum
{
    /*
     * Checks on the critical path are moved to the top (8 positions)
     * so that the compiler generates one less insn than for the rest
     * of the checks.
     */
    OCTEON_FEATURE_PKND,        /**<  CN68XX uses port kinds for packet interface */
    OCTEON_FEATURE_CN68XX_WQE,  /**<  CN68XX has different fields in word0 - word2 */

    /*
     * Features
     */
    OCTEON_FEATURE_SAAD,        /**<  Octeon models in the CN5XXX family and higher support atomic add instructions to memory (saa/saad) */
    OCTEON_FEATURE_ZIP,         /**<  Does this Octeon support the ZIP offload engine? */
    OCTEON_FEATURE_CRYPTO,      /**<  Does this Octeon support crypto acceleration using COP2? */
    OCTEON_FEATURE_DORM_CRYPTO, /**<  Can crypto be enabled by calling cvmx_crypto_dormant_enable()? */
    OCTEON_FEATURE_PCIE,        /**<  Does this Octeon support PCI express? */
    OCTEON_FEATURE_SRIO,        /**<  Does this Octeon support SRIO */
    OCTEON_FEATURE_ILK,         /**<  Does this Octeon support Interlaken */
    OCTEON_FEATURE_KEY_MEMORY,  /**<  Some Octeon models support internal memory for storing cryptographic keys */
    OCTEON_FEATURE_LED_CONTROLLER, /**<  Octeon has a LED controller for banks of external LEDs */
    OCTEON_FEATURE_TRA,         /**<  Octeon has a trace buffer */
    OCTEON_FEATURE_MGMT_PORT,   /**<  Octeon has a management port */
    OCTEON_FEATURE_RAID,        /**<  Octeon has a raid unit */
    OCTEON_FEATURE_USB,         /**<  Octeon has a builtin USB */
    OCTEON_FEATURE_NO_WPTR,     /**<  Octeon IPD can run without using work queue entries */
    OCTEON_FEATURE_DFA,         /**<  Octeon has DFA state machines */
    OCTEON_FEATURE_MDIO_CLAUSE_45,     /**<  Octeon MDIO block supports clause 45 transactions for 10 Gig support */
    OCTEON_FEATURE_NPEI,        /**<  CN52XX and CN56XX used a block named NPEI for PCIe access. Newer chips replaced this with SLI+DPI */
    OCTEON_FEATURE_HFA,         /**<  Octeon has DFA/HFA */
    OCTEON_FEATURE_DFM,         /**<  Octeon has DFM */
    OCTEON_FEATURE_CIU2,        /**<  Octeon has CIU2 */
    OCTEON_FEATURE_DICI_MODE,   /**<  Octeon has DMA Instruction Completion Interrupt mode */
    OCTEON_FEATURE_BIT_EXTRACTOR,    /**<  Octeon has Bit Select Extractor schedulor */
    OCTEON_FEATURE_NAND,        /**<  Octeon has NAND */
    OCTEON_FEATURE_MMC,         /**<  Octeon has built-in MMC support */
    OCTEON_MAX_FEATURE
} octeon_feature_t;

/**
 * Determine if the current Octeon supports a specific feature. These
 * checks have been optimized to be fairly quick, but they should still
 * be kept out of fast path code.
 *
 * @param feature Feature to check for. This should always be a constant so the
 *                compiler can remove the switch statement through optimization.
 *
 * @return Non zero if the feature exists. Zero if the feature does not
 *         exist.
 *
 * Note: This was octeon_has_feature before the feature map and is
 * called only after model-checking is set up in octeon_feature_init().
 */
static inline int old_octeon_has_feature(octeon_feature_t feature)
{
    switch (feature)
    {
        case OCTEON_FEATURE_SAAD:
            return !OCTEON_IS_MODEL(OCTEON_CN3XXX);

        case OCTEON_FEATURE_ZIP:
            if (OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN52XX))
                return 0;
            else
                return !cvmx_fuse_read(121);

        case OCTEON_FEATURE_CRYPTO:
	    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) {
                cvmx_mio_fus_dat2_t fus_2;
                fus_2.u64 = cvmx_read_csr(CVMX_MIO_FUS_DAT2);
                if (fus_2.s.nocrypto || fus_2.s.nomul) {
                    return 0;
                } else if (!fus_2.s.dorm_crypto) {
                    return 1;
                } else {
                    cvmx_rnm_ctl_status_t st;
                    st.u64 = cvmx_read_csr(CVMX_RNM_CTL_STATUS);
                    return st.s.eer_val;
                }
            } else {
                return !cvmx_fuse_read(90);
            }

        case OCTEON_FEATURE_DORM_CRYPTO:
            if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) {
                cvmx_mio_fus_dat2_t fus_2;
                fus_2.u64 = cvmx_read_csr(CVMX_MIO_FUS_DAT2);
                return !fus_2.s.nocrypto && !fus_2.s.nomul && fus_2.s.dorm_crypto;
            } else {
                return 0;
            }

        case OCTEON_FEATURE_PCIE:
            return (OCTEON_IS_MODEL(OCTEON_CN56XX)
                    || OCTEON_IS_MODEL(OCTEON_CN52XX)
                    || OCTEON_IS_MODEL(OCTEON_CN6XXX)
                    || OCTEON_IS_MODEL(OCTEON_CNF7XXX));

	case OCTEON_FEATURE_SRIO:
	    return (OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX));

	case OCTEON_FEATURE_ILK:
	    return (OCTEON_IS_MODEL(OCTEON_CN68XX));

        case OCTEON_FEATURE_KEY_MEMORY:
            return (OCTEON_IS_MODEL(OCTEON_CN38XX)
                    || OCTEON_IS_MODEL(OCTEON_CN58XX)
                    || OCTEON_IS_MODEL(OCTEON_CN56XX)
                    || OCTEON_IS_MODEL(OCTEON_CN6XXX)
                    || OCTEON_IS_MODEL(OCTEON_CNF7XXX));

        case OCTEON_FEATURE_LED_CONTROLLER:
            return OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN56XX);

        case OCTEON_FEATURE_TRA:
            return !(OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN50XX));
        case OCTEON_FEATURE_MGMT_PORT:
            return (OCTEON_IS_MODEL(OCTEON_CN56XX)
                    || OCTEON_IS_MODEL(OCTEON_CN52XX)
                    || OCTEON_IS_MODEL(OCTEON_CN6XXX));

        case OCTEON_FEATURE_RAID:
            return OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN6XXX);

        case OCTEON_FEATURE_USB:
            return !(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX));

        case OCTEON_FEATURE_NO_WPTR:
            return ((OCTEON_IS_MODEL(OCTEON_CN56XX)
                     || OCTEON_IS_MODEL(OCTEON_CN52XX)
                     || OCTEON_IS_MODEL(OCTEON_CN6XXX)
                     || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
                    && !OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X)
                    && !OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X));

        case OCTEON_FEATURE_DFA:
            if (!OCTEON_IS_MODEL(OCTEON_CN38XX) && !OCTEON_IS_MODEL(OCTEON_CN31XX) && !OCTEON_IS_MODEL(OCTEON_CN58XX))
                return 0;
            else if (OCTEON_IS_MODEL(OCTEON_CN3020))
                return 0;
            else
                return !cvmx_fuse_read(120);

        case OCTEON_FEATURE_HFA:
            if (!OCTEON_IS_MODEL(OCTEON_CN6XXX))
                return 0;
            else
                return !cvmx_fuse_read(90);

        case OCTEON_FEATURE_DFM:
            if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
                return 0;
            else
                return !cvmx_fuse_read(90);

        case OCTEON_FEATURE_MDIO_CLAUSE_45:
            return !(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN50XX));

        case OCTEON_FEATURE_NPEI:
            return (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN52XX));

        case OCTEON_FEATURE_PKND:
            return (OCTEON_IS_MODEL(OCTEON_CN68XX));

        case OCTEON_FEATURE_CN68XX_WQE:
            return (OCTEON_IS_MODEL(OCTEON_CN68XX));

        case OCTEON_FEATURE_CIU2:
            return (OCTEON_IS_MODEL(OCTEON_CN68XX));

        case OCTEON_FEATURE_NAND:
            return (OCTEON_IS_MODEL(OCTEON_CN52XX)
                    || OCTEON_IS_MODEL(OCTEON_CN63XX)
                    || OCTEON_IS_MODEL(OCTEON_CN66XX)
                    || OCTEON_IS_MODEL(OCTEON_CN68XX));

        case OCTEON_FEATURE_DICI_MODE:
            return (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS2_X)
                    || OCTEON_IS_MODEL(OCTEON_CN61XX)
                    || OCTEON_IS_MODEL(OCTEON_CNF71XX));

        case OCTEON_FEATURE_BIT_EXTRACTOR:
            return (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS2_X)
                    || OCTEON_IS_MODEL(OCTEON_CN61XX)
                    || OCTEON_IS_MODEL(OCTEON_CNF71XX));

        case OCTEON_FEATURE_MMC:
            return (OCTEON_IS_MODEL(OCTEON_CN61XX)
		    || OCTEON_IS_MODEL(OCTEON_CNF71XX));
        default:
	    break;
    }
    return 0;
}

/*
 * bit map for octeon features
 */
#define FEATURE_MAP_SIZE	128
extern uint8_t octeon_feature_map[FEATURE_MAP_SIZE];

/*
 * Answer ``Is the bit for feature set in the bitmap?''
 * @param feature
 * @return 1 when the feature is present and 0 otherwise, -1 in case of error.
 */
#if defined(__U_BOOT__) || defined(CVMX_BUILD_FOR_LINUX_HOST) || defined(CVMX_BUILD_FOR_TOOLCHAIN)
#define octeon_has_feature old_octeon_has_feature
#else
#if defined(USE_RUNTIME_MODEL_CHECKS) || (defined(__FreeBSD__) && defined(_KERNEL))
static inline int octeon_has_feature(octeon_feature_t feature)
{
	int byte, bit;
	byte = feature >> 3;
	bit = feature & 0x7;

	if (byte >= FEATURE_MAP_SIZE)
        {
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
            printk("ERROR: octeon_feature_map: Invalid Octeon Feature 0x%x\n", feature);
#else
            printf("ERROR: octeon_feature_map: Invalid Octeon Feature 0x%x\n", feature);
#endif
            return -1;
        }

	return (octeon_feature_map[byte] & ((1 << bit))) ? 1 : 0;
}
#else
#define octeon_has_feature old_octeon_has_feature
#endif
#endif

/*
 * initialize octeon_feature_map[]
 */
extern void octeon_feature_init(void);

#ifdef	__cplusplus
}
#endif

#endif    /* __OCTEON_FEATURE_H__ */
