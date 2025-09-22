/*	$OpenBSD: gcu_reg.h,v 1.1 2009/11/25 13:28:13 dms Exp $	*/

/*
 *   Copyright(c) 2007,2008,2009 Intel Corporation. All rights reserved.
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without 
 *   modification, are permitted provided that the following conditions 
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright 
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright 
 *       notice, this list of conditions and the following disclaimer in 
 *       the documentation and/or other materials provided with the 
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its 
 *       contributors may be used to endorse or promote products derived 
 *       from this software without specific prior written permission.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 *  version: Embedded.B.1.0.3-146
 */

/* 
 * gcu_reg.h
 * Macros and constants related to the registers available on the GCU
 */

#ifndef GCU_REG_H
#define GCU_REG_H

/* Register Offsets within memory map register space */
#define MDIO_STATUS_REG    0x00000010UL
#define MDIO_COMMAND_REG   0x00000014UL

/* MDIO_STATUS_REG fields */
#define MDIO_STATUS_STATUS_MASK     0x80000000UL  /* bit 31 = 1 on error */
#define MDIO_STATUS_READ_DATA_MASK  0x0000FFFFUL

/* MDIO_COMMAND_REG fields */
#define MDIO_COMMAND_GO_MASK         0x80000000UL /* bit 31 = 1 during read or
                                                   * write, 0 on completion */
#define MDIO_COMMAND_OPER_MASK       0x04000000UL /* bit = 1 is  a write */
#define MDIO_COMMAND_PHY_ADDR_MASK   0x03E00000UL
#define MDIO_COMMAND_PHY_REG_MASK    0x001F0000UL
#define MDIO_COMMAND_WRITE_DATA_MASK 0x0000FFFFUL

#define MDIO_COMMAND_GO_OFFSET         31
#define MDIO_COMMAND_OPER_OFFSET       26
#define MDIO_COMMAND_PHY_ADDR_OFFSET   21
#define MDIO_COMMAND_PHY_REG_OFFSET    16
#define MDIO_COMMAND_WRITE_DATA_OFFSET 0

#define MDIO_COMMAND_PHY_ADDR_MAX      2  /* total phys supported by GCU */
#define MDIO_COMMAND_PHY_REG_MAX       31 /* total registers available on 
                                           * the M88 Phy used on truxton */

#endif /* ifndef GCU_REG_H */

