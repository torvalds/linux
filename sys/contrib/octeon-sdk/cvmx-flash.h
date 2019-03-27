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
 * This file provides bootbus flash operations
 *
 * <hr>$Revision: 70030 $<hr>
 *
 *
 */


#ifndef __CVMX_FLASH_H__
#define __CVMX_FLASH_H__

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct
{
    int start_offset;
    int block_size;
    int num_blocks;
} cvmx_flash_region_t;

/**
 * Initialize the flash access library
 */
void cvmx_flash_initialize(void);

/**
 * Return a pointer to the flash chip
 *
 * @param chip_id Chip ID to return
 * @return NULL if the chip doesn't exist
 */
void *cvmx_flash_get_base(int chip_id);

/**
 * Return the number of erasable regions on the chip
 *
 * @param chip_id Chip to return info for
 * @return Number of regions
 */
int cvmx_flash_get_num_regions(int chip_id);

/**
 * Return information about a flash chips region
 *
 * @param chip_id Chip to get info for
 * @param region  Region to get info for
 * @return Region information
 */
const cvmx_flash_region_t *cvmx_flash_get_region_info(int chip_id, int region);

/**
 * Erase a block on the flash chip
 *
 * @param chip_id Chip to erase a block on
 * @param region  Region to erase a block in
 * @param block   Block number to erase
 * @return Zero on success. Negative on failure
 */
int cvmx_flash_erase_block(int chip_id, int region, int block);

/**
 * Write a block on the flash chip
 *
 * @param chip_id Chip to write a block on
 * @param region  Region to write a block in
 * @param block   Block number to write
 * @param data    Data to write
 * @return Zero on success. Negative on failure
 */
int cvmx_flash_write_block(int chip_id, int region, int block, const void *data);

/**
 * Erase and write data to a flash
 *
 * @param address Memory address to write to
 * @param data    Data to write
 * @param len     Length of the data
 * @return Zero on success. Negative on failure
 */
int cvmx_flash_write(void *address, const void *data, int len);

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_FLASH_H__ */
