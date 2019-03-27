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

#define DEBUGPRINT(format, ...) printf(format, ##__VA_ARGS__)

/**
 * Given a packet data address, return a pointer to the
 * beginning of the packet buffer.
 *
 * @param packet_ptr Packet data hardware address
 * @return Packet buffer pointer
 */
static inline char *cvm_oct_get_buffer_ptr(cvmx_buf_ptr_t packet_ptr)
{
	return cvmx_phys_to_ptr(((packet_ptr.s.addr >> 7) - packet_ptr.s.back) << 7);
}


/**
 * Given an IPD/PKO port number, return the logical interface it is
 * on.
 *
 * @param ipd_port Port to check
 *
 * @return Logical interface
 */
static inline int INTERFACE(int ipd_port)
{
	if (ipd_port < 32)    /* Interface 0 or 1 for RGMII,GMII,SPI, etc */
		return ipd_port>>4;
	else if (ipd_port < 36)   /* Interface 2 for NPI */
		return 2;
	else if (ipd_port < 40)   /* Interface 3 for loopback */
		return 3;
	else if (ipd_port == 40)  /* Non existant interface for POW0 */
		return 4;
	else
		panic("Illegal ipd_port %d passed to INTERFACE\n", ipd_port);
}


/**
 * Given an IPD/PKO port number, return the port's index on a
 * logical interface.
 *
 * @param ipd_port Port to check
 *
 * @return Index into interface port list
 */
static inline int INDEX(int ipd_port)
{
	if (ipd_port < 32)
		return ipd_port & 15;
	else
		return ipd_port & 3;
}

