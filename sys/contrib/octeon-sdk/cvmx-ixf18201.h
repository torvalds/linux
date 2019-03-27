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



#ifndef __CVMX_IXF18201_H__
#define __CVMX_IXF18201_H__

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Initialize the IXF18201 SPI<->XAUI MAC.
 * @return 1 on success
 *         0 on failure
 */
int cvmx_ixf18201_init(void);

/**
 * Read a 16 bit register from the IXF18201
 *
 * @param reg_addr Register address
 *
 * @return 16 bit register value
 */
uint16_t cvmx_ixf18201_read16(uint16_t reg_addr);
/**
 * Write a 16 bit IXF18201 register
 *
 * @param reg_addr Register address
 * @param data Value to write
 *
 */
void cvmx_ixf18201_write16(uint16_t reg_addr, uint16_t data);
/**
 * Write a 16 bit IXF18201 register
 *
 * @param reg_addr Register address (must be 4 byte aligned)
 *
 * @return 32 bit register value
 */
uint32_t cvmx_ixf18201_read32(uint16_t reg_addr);
/**
 * Write a 32 bit IXF18201 register
 *
 * @param reg_addr Register address (must be 4 byte aligned)
 * @param data Value to write
 *
 */
void cvmx_ixf18201_write32(uint16_t reg_addr, uint32_t data);

/**
 * Performs an MII clause 45 write using the MII block in IXF18201.
 *
 * @param mii_addr Device MII address
 * @param mmd      MMD address (block within device)
 * @param reg      Register address
 * @param val      Value to write
 */
void cvmx_ixf18201_mii_write(int mii_addr, int mmd, uint16_t reg, uint16_t val);
/**
 * Performs an MII clause 45 read using the MII block in IXF18201.
 *
 * @param mii_addr Device MII address
 * @param mmd      MMD address (block within device)
 * @param reg      Register address
 * @return  register value read from device
 */
int cvmx_ixf18201_mii_read(int mii_addr, int mmd, uint16_t reg);

#ifdef	__cplusplus
}
#endif

#endif  /*  __CVMX_IXF18201_H__ */
