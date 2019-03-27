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
 * PCI / PCIe packet engine related structures.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_NPI_H__
#define __CVMX_NPI_H__

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * PCI / PCIe packet instruction header format
 */
typedef union
{
    uint64_t u64;
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t            r :     1;      /**< Packet is RAW */
        uint64_t            g :     1;      /**< Gather list is used */
        uint64_t            dlengsz : 14;   /**< Data length / Gather list size */
        uint64_t            fsz     : 6;    /**< Front data size */
        uint64_t            qos     : 3;    /**< POW QoS queue */
        uint64_t            grp     : 4;    /**< POW Group */
        uint64_t            rs      : 1;    /**< Real short */
        cvmx_pow_tag_type_t tt      : 2;    /**< POW Tag type */
        uint64_t            tag     : 32;   /**< POW 32 bit tag */
#else
	uint64_t            tag     : 32;
	cvmx_pow_tag_type_t tt      : 2;
	uint64_t            rs      : 1;
	uint64_t            grp     : 4;
	uint64_t            qos     : 3;
	uint64_t            fsz     : 6;
	uint64_t            dlengsz : 14;
	uint64_t            g :     1;
	uint64_t            r :     1;
#endif
    } s;
} cvmx_npi_inst_hdr_t;

/**
 * PCI / PCIe packet data pointer formats 0-3
 */
typedef union
{
    uint64_t dptr0;
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    es      : 2;    /**< Endian swap mode */
        uint64_t    ns      : 1;    /**< No snoop */
        uint64_t    ro      : 1;    /**< Relaxed ordering */
        uint64_t    addr    : 60;   /**< PCI/PCIe address */
#else
        uint64_t    addr    : 60;
        uint64_t    ro      : 1;
        uint64_t    ns      : 1;
        uint64_t    es      : 2;
#endif
    } dptr1;
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    pm      : 2;    /**< Parse mode */
        uint64_t    sl      : 7;    /**< Skip length */
        uint64_t    addr    : 55;   /**< PCI/PCIe address */
#else
        uint64_t    addr    : 55;
        uint64_t    sl      : 7;
        uint64_t    pm      : 2;
#endif
    } dptr2;
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    es      : 2;    /**< Endian swap mode */
        uint64_t    ns      : 1;    /**< No snoop */
        uint64_t    ro      : 1;    /**< Relaxed ordering */
        uint64_t    pm      : 2;    /**< Parse mode */
        uint64_t    sl      : 7;    /**< Skip length */
        uint64_t    addr    : 51;   /**< PCI/PCIe address */
#else
        uint64_t    addr    : 51;
        uint64_t    sl      : 7;
        uint64_t    pm      : 2;
        uint64_t    ro      : 1;
        uint64_t    ns      : 1;
        uint64_t    es      : 2;
#endif
    } dptr3;
} cvmx_npi_dptr_t;

#ifdef	__cplusplus
}
#endif

#endif  /* __CVMX_NPI_H__ */
