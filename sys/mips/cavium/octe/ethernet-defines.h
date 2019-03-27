/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/
/* $FreeBSD$ */

/*
 * A few defines are used to control the operation of this driver:
 *  CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS
 *      This kernel config option allows the user to control the number of
 *      packet and work queue buffers allocated by the driver. If this is zero,
 *      the driver uses the default from below.
 */

#define INTERRUPT_LIMIT             1000   /* Max interrupts per second per core */
/*#define INTERRUPT_LIMIT             0     *//* Don't limit the number of interrupts */
#define USE_RED                     1	/* Enable Random Early Dropping under load */
#define USE_10MBPS_PREAMBLE_WORKAROUND 1    /* Allow SW based preamble removal at 10Mbps to workaround PHYs giving us bad preambles */
#define DONT_WRITEBACK(x)           (x) /* Use this to have all FPA frees also tell the L2 not to write data to memory */
/*#define DONT_WRITEBACK(x)         0   *//* Use this to not have FPA frees control L2 */

#define MAX_RX_PACKETS 1024 /* Maximum number of packets to process per interrupt. */
#define MAX_OUT_QUEUE_DEPTH 1000

#define FAU_NUM_PACKET_BUFFERS_TO_FREE (CVMX_FAU_REG_END - sizeof(uint32_t))
#define TOTAL_NUMBER_OF_PORTS       (CVMX_PIP_NUM_INPUT_PORTS+1)

