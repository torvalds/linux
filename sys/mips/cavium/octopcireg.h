/***********************license start************************************
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005-2007 Cavium Networks (support@cavium.com). All rights
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

#ifndef	_CAVIUM_OCTOPCIREG_H_
#define	_CAVIUM_OCTOPCIREG_H_

/**
 * This is the bit decoding used for the Octeon PCI controller addresses for config space
 */
typedef union
{
    uint64_t    u64;
    uint64_t *  u64_ptr;
    uint32_t *  u32_ptr;
    uint16_t *  u16_ptr;
    uint8_t *   u8_ptr;
    struct
    {
        uint64_t    upper       : 2;
        uint64_t    reserved    : 13;
        uint64_t    io          : 1;
        uint64_t    did         : 5;
        uint64_t    subdid      : 3;
        uint64_t    reserved2   : 4;
        uint64_t    endian_swap : 2;
        uint64_t    reserved3   : 10;
        uint64_t    bus         : 8;
        uint64_t    dev         : 5;
        uint64_t    func        : 3;
        uint64_t    reg         : 8;
    } s;
} octeon_pci_config_space_address_t;

typedef union
{
    uint64_t    u64;
    uint32_t *  u32_ptr;
    uint16_t *  u16_ptr;
    uint8_t *   u8_ptr;
    struct
    {
        uint64_t    upper       : 2;
        uint64_t    reserved    : 13;
        uint64_t    io          : 1;
        uint64_t    did         : 5;
        uint64_t    subdid      : 3;
        uint64_t    reserved2   : 4;
        uint64_t    endian_swap : 2;
        uint64_t    res1        : 1;
        uint64_t    port        : 1;
        uint64_t    addr        : 32;
    } s;
} octeon_pci_io_space_address_t;


#define CVMX_OCT_SUBDID_PCI_CFG     1
#define CVMX_OCT_SUBDID_PCI_IO      2
#define CVMX_OCT_SUBDID_PCI_MEM1    3
#define CVMX_OCT_SUBDID_PCI_MEM2    4
#define CVMX_OCT_SUBDID_PCI_MEM3    5
#define CVMX_OCT_SUBDID_PCI_MEM4    6

#define	CVMX_OCT_PCI_IO_BASE	0x00004000
#define	CVMX_OCT_PCI_IO_SIZE	0x08000000

#define	CVMX_OCT_PCI_MEM1_BASE	0xf0000000
#define	CVMX_OCT_PCI_MEM1_SIZE	0x0f000000

#endif /* !_CAVIUM_OCTOPCIREG_H_ */
