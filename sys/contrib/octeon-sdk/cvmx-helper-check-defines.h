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
 * Validate defines required by cvmx-helper. This header file
 * validates a number of defines required for cvmx-helper to
 * function properly. It either supplies a default or fails
 * compile if a define is incorrect.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifndef __CVMX_HELPER_CHECK_DEFINES_H__
#define __CVMX_HELPER_CHECK_DEFINES_H__

/* CVMX_HELPER_FIRST_MBUFF_SKIP is the number of bytes to reserve before
    the beginning of the packet. Override in executive-config.h */
#ifndef CVMX_HELPER_FIRST_MBUFF_SKIP
#define CVMX_HELPER_FIRST_MBUFF_SKIP 184
#warning WARNING: default CVMX_HELPER_FIRST_MBUFF_SKIP used.  Defaults deprecated, please set in executive-config.h
#endif

/* CVMX_HELPER_NOT_FIRST_MBUFF_SKIP is the number of bytes to reserve in each
    chained packet element. Override in executive-config.h */
#ifndef CVMX_HELPER_NOT_FIRST_MBUFF_SKIP
#define CVMX_HELPER_NOT_FIRST_MBUFF_SKIP 0
#warning WARNING: default CVMX_HELPER_NOT_FIRST_MBUFF_SKIP used.  Defaults deprecated, please set in executive-config.h
#endif

/* CVMX_HELPER_ENABLE_IPD controls if the IPD is enabled in the helper
    function. Once it is enabled the hardware starts accepting packets. You
    might want to skip the IPD enable if configuration changes are need
    from the default helper setup. Override in executive-config.h */
#ifndef CVMX_HELPER_ENABLE_IPD
#define CVMX_HELPER_ENABLE_IPD 1
#warning WARNING: default CVMX_HELPER_ENABLE_IPD used.  Defaults deprecated, please set in executive-config.h
#endif

/* Set default (defaults are deprecated) input tag type */
#ifndef  CVMX_HELPER_INPUT_TAG_TYPE
#define  CVMX_HELPER_INPUT_TAG_TYPE CVMX_POW_TAG_TYPE_ORDERED
#warning WARNING: default CVMX_HELPER_INPUT_TAG_TYPE used.  Defaults deprecated, please set in executive-config.h
#endif

#ifndef CVMX_HELPER_INPUT_PORT_SKIP_MODE
#define CVMX_HELPER_INPUT_PORT_SKIP_MODE	CVMX_PIP_PORT_CFG_MODE_SKIPL2
#warning WARNING: default CVMX_HELPER_INPUT_PORT_SKIP_MODE used.  Defaults deprecated, please set in executive-config.h
#endif

#if defined(CVMX_ENABLE_HELPER_FUNCTIONS) && !defined(CVMX_HELPER_INPUT_TAG_INPUT_PORT)
#error CVMX_HELPER_INPUT_TAG_* values for determining tag hash inputs must be defined in executive-config.h
#endif

#endif
