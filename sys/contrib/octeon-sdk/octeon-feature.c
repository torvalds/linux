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
 * <hr>$Revision: 1 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/init.h>

#include <asm/octeon/octeon.h>
#else
#include "cvmx.h"
#endif

CVMX_SHARED uint8_t octeon_feature_map[FEATURE_MAP_SIZE] __attribute__((aligned(128)));
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(octeon_feature_map);
#else
#define __init
#endif

/*
 * Set the bit in octeon_feature_map for feature.
 *
 * @param feature
 * @return 0 for success and nonzero for error.
 */
static int __init octeon_feature_set(octeon_feature_t feature)
{
    int bit, byte;

    byte = feature >> 3;
    bit = feature & 0x7;
    octeon_feature_map[byte] |= (((uint8_t)1) << bit);

    return 0;
}

void __init octeon_feature_init(void)
{
    octeon_feature_result_t val;

    /*
     * Check feature map size
     */
    if (OCTEON_MAX_FEATURE > (FEATURE_MAP_SIZE * 8 - 1))
    {
        val = OCTEON_FEATURE_MAP_OVERFLOW;
        goto feature_check;
    }

    /*
     * Feature settings
     */
#define OCTEON_FEATURE_SET(feature_x)		\
	if (old_octeon_has_feature(feature_x))	\
	    octeon_feature_set(feature_x)

    OCTEON_FEATURE_SET(OCTEON_FEATURE_SAAD);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_ZIP);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_CRYPTO);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_DORM_CRYPTO);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_PCIE);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_SRIO);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_ILK);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_KEY_MEMORY);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_LED_CONTROLLER);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_TRA);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_MGMT_PORT);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_RAID);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_USB);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_NO_WPTR);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_DFA);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_MDIO_CLAUSE_45);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_NPEI);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_PKND);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_CN68XX_WQE);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_HFA);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_DFM);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_CIU2);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_DICI_MODE);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_BIT_EXTRACTOR);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_NAND);
    OCTEON_FEATURE_SET(OCTEON_FEATURE_MMC);

    val = OCTEON_FEATURE_SUCCESS;

feature_check:

    if (val != OCTEON_FEATURE_SUCCESS)
    {
	cvmx_dprintf("octeon_feature_init(): ");
	switch (val)
	{
	case OCTEON_FEATURE_MAP_OVERFLOW:
	    cvmx_dprintf("feature map overflow.\n");
	    break;
	default:
	    cvmx_dprintf("unknown error %d.\n", val);
	    break;
	}
#if !defined(CVMX_BUILD_FOR_LINUX_KERNEL) && !defined(__U_BOOT__) && !defined(CVMX_BUILD_FOR_TOOLCHAIN) && !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)
	exit (1);
#endif
    }
}
