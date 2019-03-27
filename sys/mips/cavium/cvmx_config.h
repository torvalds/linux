/***********************license start***************
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003-2008 Cavium Networks (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Cavium Networks nor the names of
 *       its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM NETWORKS MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES
 * OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR
 * PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET
 * POSSESSION OR CORRESPONDENCE TO DESCRIPTION.  THE ENTIRE RISK ARISING OUT
 * OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 *
 *
 * For any questions regarding licensing please contact marketing@caviumnetworks.com
 *
 ***********************license end**************************************/
/* $FreeBSD$ */

#ifndef	_CVMX_CONFIG_H
#define	_CVMX_CONFIG_H

#include "opt_cvmx.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/stdarg.h>

#define	asm		__asm

#define	CVMX_DONT_INCLUDE_CONFIG

/* Define to enable the use of simple executive packet output functions.
** For packet I/O setup enable the helper functions below. 
*/ 
#define CVMX_ENABLE_PKO_FUNCTIONS

/* Define to enable the use of simple executive helper functions. These
** include many hardware setup functions.  See cvmx-helper.[ch] for
** details.
*/
#define CVMX_ENABLE_HELPER_FUNCTIONS

/* CVMX_HELPER_FIRST_MBUFF_SKIP is the number of bytes to reserve before
** the beginning of the packet. If necessary, override the default  
** here.  See the IPD section of the hardware manual for MBUFF SKIP 
** details.*/ 
#define CVMX_HELPER_FIRST_MBUFF_SKIP 184

/* CVMX_HELPER_NOT_FIRST_MBUFF_SKIP is the number of bytes to reserve in each
** chained packet element. If necessary, override the default here */
#define CVMX_HELPER_NOT_FIRST_MBUFF_SKIP 0

/* CVMX_HELPER_ENABLE_BACK_PRESSURE controls whether back pressure is enabled
** for all input ports. This controls if IPD sends backpressure to all ports if
** Octeon's FPA pools don't have enough packet or work queue entries. Even when
** this is off, it is still possible to get backpressure from individual
** hardware ports. When configuring backpressure, also check
** CVMX_HELPER_DISABLE_*_BACKPRESSURE below. If necessary, override the default
** here */
#define CVMX_HELPER_ENABLE_BACK_PRESSURE 1

/* CVMX_HELPER_ENABLE_IPD controls if the IPD is enabled in the helper
**  function. Once it is enabled the hardware starts accepting packets. You
**  might want to skip the IPD enable if configuration changes are need
**  from the default helper setup. If necessary, override the default here */
#define CVMX_HELPER_ENABLE_IPD 1

/* CVMX_HELPER_INPUT_TAG_TYPE selects the type of tag that the IPD assigns
** to incoming packets. */
#define CVMX_HELPER_INPUT_TAG_TYPE CVMX_POW_TAG_TYPE_ORDERED

/* The following select which fields are used by the PIP to generate
** the tag on INPUT
** 0: don't include
** 1: include */
#define CVMX_HELPER_INPUT_TAG_IPV6_SRC_IP	0
#define CVMX_HELPER_INPUT_TAG_IPV6_DST_IP   	0
#define CVMX_HELPER_INPUT_TAG_IPV6_SRC_PORT 	0
#define CVMX_HELPER_INPUT_TAG_IPV6_DST_PORT 	0
#define CVMX_HELPER_INPUT_TAG_IPV6_NEXT_HEADER 	0
#define CVMX_HELPER_INPUT_TAG_IPV4_SRC_IP	0
#define CVMX_HELPER_INPUT_TAG_IPV4_DST_IP   	0
#define CVMX_HELPER_INPUT_TAG_IPV4_SRC_PORT 	0
#define CVMX_HELPER_INPUT_TAG_IPV4_DST_PORT 	0
#define CVMX_HELPER_INPUT_TAG_IPV4_PROTOCOL	0
#define CVMX_HELPER_INPUT_TAG_INPUT_PORT	1

/* Select skip mode for input ports */
#define CVMX_HELPER_INPUT_PORT_SKIP_MODE	CVMX_PIP_PORT_CFG_MODE_SKIPL2

/* Define the number of queues per output port */
#define CVMX_HELPER_PKO_QUEUES_PER_PORT_INTERFACE0	1
#define CVMX_HELPER_PKO_QUEUES_PER_PORT_INTERFACE1	1

/* Configure PKO to use per-core queues (PKO lockless operation). 
** Please see the related SDK documentation for PKO that illustrates 
** how to enable and configure this option. */
//#define CVMX_ENABLE_PKO_LOCKLESS_OPERATION 1
//#define CVMX_HELPER_PKO_MAX_PORTS_INTERFACE0 8
//#define CVMX_HELPER_PKO_MAX_PORTS_INTERFACE1 8

/* Force backpressure to be disabled.  This overrides all other
** backpressure configuration */
#define CVMX_HELPER_DISABLE_RGMII_BACKPRESSURE 1

/* Disable the SPI4000's processing of backpressure packets and backpressure
** generation. When this is 1, the SPI4000 will not stop sending packets when
** receiving backpressure. It will also not generate backpressure packets when
** its internal FIFOs are full. */
#define CVMX_HELPER_DISABLE_SPI4000_BACKPRESSURE 1

/* CVMX_HELPER_SPI_TIMEOUT is used to determine how long the SPI initialization
** routines wait for SPI training. You can override the value using
** executive-config.h if necessary */
#define CVMX_HELPER_SPI_TIMEOUT 10

/* Select the number of low latency memory ports (interfaces) that
** will be configured.  Valid values are 1 and 2.
*/
#define CVMX_LLM_CONFIG_NUM_PORTS 2

/* Enable the fix for PKI-100 errata ("Size field is 8 too large in WQE and next
** pointers"). If CVMX_ENABLE_LEN_M8_FIX is set to 0, the fix for this errata will 
** not be enabled. 
** 0: Fix is not enabled
** 1: Fix is enabled, if supported by hardware
*/
#define CVMX_ENABLE_LEN_M8_FIX  1

#if defined(CVMX_ENABLE_HELPER_FUNCTIONS) && !defined(CVMX_ENABLE_PKO_FUNCTIONS)
#define CVMX_ENABLE_PKO_FUNCTIONS
#endif

/* Enable debug and informational printfs */
#define CVMX_CONFIG_ENABLE_DEBUG_PRINTS 	1

/************************* Config Specific Defines ************************/
#define CVMX_LLM_NUM_PORTS 1
#define CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 1			/**< PKO queues per port for interface 0 (ports 0-15) */
#define CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 1			/**< PKO queues per port for interface 1 (ports 16-31) */
#define CVMX_PKO_MAX_PORTS_INTERFACE0 CVMX_HELPER_PKO_MAX_PORTS_INTERFACE0 /**< Limit on the number of PKO ports enabled for interface 0 */
#define CVMX_PKO_MAX_PORTS_INTERFACE1 CVMX_HELPER_PKO_MAX_PORTS_INTERFACE1 /**< Limit on the number of PKO ports enabled for interface 1 */
#define CVMX_PKO_QUEUES_PER_PORT_PCI 1				/**< PKO queues per port for PCI (ports 32-35) */
#define CVMX_PKO_QUEUES_PER_PORT_LOOP 1				/**< PKO queues per port for Loop devices (ports 36-39) */

/************************* FPA allocation *********************************/
/* Pool sizes in bytes, must be multiple of a cache line */
#define CVMX_FPA_POOL_0_SIZE (15 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_1_SIZE (1 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_2_SIZE (8 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_3_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_4_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_5_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_6_SIZE (0 * CVMX_CACHE_LINE_SIZE)
#define CVMX_FPA_POOL_7_SIZE (0 * CVMX_CACHE_LINE_SIZE)

/* Pools in use */
#define CVMX_FPA_PACKET_POOL                (0)             /**< Packet buffers */
#define CVMX_FPA_PACKET_POOL_SIZE           CVMX_FPA_POOL_0_SIZE
#define CVMX_FPA_WQE_POOL                   (1)             /**< Work queue entrys */
#define CVMX_FPA_WQE_POOL_SIZE              CVMX_FPA_POOL_1_SIZE
#define CVMX_FPA_OUTPUT_BUFFER_POOL         (2)             /**< PKO queue command buffers */
#define CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE    CVMX_FPA_POOL_2_SIZE

/*************************  FAU allocation ********************************/
#define	CVMX_FAU_REG_END                    2048

#define	CVMX_SCR_SCRATCH                    0

#endif /* !_CVMX_CONFIG_H */
