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

#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-spinlock.h"
#include "cvmx-flash.h"

#define MAX_NUM_FLASH_CHIPS 8   /* Maximum number of flash chips */
#define MAX_NUM_REGIONS     8   /* Maximum number of block regions per chip */
#define DEBUG 1

#define CFI_CMDSET_NONE             0
#define CFI_CMDSET_INTEL_EXTENDED   1
#define CFI_CMDSET_AMD_STANDARD     2
#define CFI_CMDSET_INTEL_STANDARD   3
#define CFI_CMDSET_AMD_EXTENDED     4
#define CFI_CMDSET_MITSU_STANDARD   256
#define CFI_CMDSET_MITSU_EXTENDED   257
#define CFI_CMDSET_SST              258

typedef struct
{
    void *              base_ptr;       /**< Memory pointer to start of flash */
    int                 is_16bit;       /**< Chip is 16bits wide in 8bit mode */
    uint16_t            vendor;         /**< Vendor ID of Chip */
    int                 size;           /**< Size of the chip in bytes */
    uint64_t            erase_timeout;  /**< Erase timeout in cycles */
    uint64_t            write_timeout;  /**< Write timeout in cycles */
    int                 num_regions;    /**< Number of block regions */
    cvmx_flash_region_t region[MAX_NUM_REGIONS];
} cvmx_flash_t;

static CVMX_SHARED cvmx_flash_t flash_info[MAX_NUM_FLASH_CHIPS];
static CVMX_SHARED cvmx_spinlock_t flash_lock = CVMX_SPINLOCK_UNLOCKED_INITIALIZER;


/**
 * @INTERNAL
 * Read a byte from flash
 *
 * @param chip_id Chip to read from
 * @param offset  Offset into the chip
 * @return Value read
 */
static uint8_t __cvmx_flash_read8(int chip_id, int offset)
{
    return *(volatile uint8_t *)(flash_info[chip_id].base_ptr + offset);
}


/**
 * @INTERNAL
 * Read a byte from flash (for commands)
 *
 * @param chip_id Chip to read from
 * @param offset  Offset into the chip
 * @return Value read
 */
static uint8_t __cvmx_flash_read_cmd(int chip_id, int offset)
{
    if (flash_info[chip_id].is_16bit)
        offset<<=1;
    return __cvmx_flash_read8(chip_id, offset);
}


/**
 * @INTERNAL
 * Read 16bits from flash (for commands)
 *
 * @param chip_id Chip to read from
 * @param offset  Offset into the chip
 * @return Value read
 */
static uint16_t __cvmx_flash_read_cmd16(int chip_id, int offset)
{
    uint16_t v = __cvmx_flash_read_cmd(chip_id, offset);
    v |= __cvmx_flash_read_cmd(chip_id, offset + 1)<<8;
    return v;
}


/**
 * @INTERNAL
 * Write a byte to flash
 *
 * @param chip_id Chip to write to
 * @param offset  Offset into the chip
 * @param data    Value to write
 */
static void __cvmx_flash_write8(int chip_id, int offset, uint8_t data)
{
    volatile uint8_t *flash_ptr = (volatile uint8_t *)flash_info[chip_id].base_ptr;
    flash_ptr[offset] = data;
}


/**
 * @INTERNAL
 * Write a byte to flash (for commands)
 *
 * @param chip_id Chip to write to
 * @param offset  Offset into the chip
 * @param data    Value to write
 */
static void __cvmx_flash_write_cmd(int chip_id, int offset, uint8_t data)
{
    volatile uint8_t *flash_ptr = (volatile uint8_t *)flash_info[chip_id].base_ptr;
    flash_ptr[offset<<flash_info[chip_id].is_16bit] = data;
}


/**
 * @INTERNAL
 * Query a address and see if a CFI flash chip is there.
 *
 * @param chip_id  Chip ID data to fill in if the chip is there
 * @param base_ptr Memory pointer to the start address to query
 * @return Zero on success, Negative on failure
 */
static int __cvmx_flash_queury_cfi(int chip_id, void *base_ptr)
{
    int region;
    cvmx_flash_t *flash = flash_info + chip_id;

    /* Set the minimum needed for the read and write primitives to work */
    flash->base_ptr = base_ptr;
    flash->is_16bit = 1;   /* FIXME: Currently assumes the chip is 16bits */

    /* Put flash in CFI query mode */
    __cvmx_flash_write_cmd(chip_id, 0x00, 0xf0); /* Reset the flash chip */
    __cvmx_flash_write_cmd(chip_id, 0x55, 0x98);

    /* Make sure we get the QRY response we should */
    if ((__cvmx_flash_read_cmd(chip_id, 0x10) != 'Q') ||
        (__cvmx_flash_read_cmd(chip_id, 0x11) != 'R') ||
        (__cvmx_flash_read_cmd(chip_id, 0x12) != 'Y'))
    {
        flash->base_ptr = NULL;
        return -1;
    }

    /* Read the 16bit vendor ID */
    flash->vendor = __cvmx_flash_read_cmd16(chip_id, 0x13);

    /* Read the write timeout. The timeout is microseconds(us) is 2^0x1f
        typically. The worst case is this value time 2^0x23 */
    flash->write_timeout = 1ull << (__cvmx_flash_read_cmd(chip_id, 0x1f) +
                                    __cvmx_flash_read_cmd(chip_id, 0x23));

    /* Read the erase timeout. The timeout is milliseconds(ms) is 2^0x21
        typically. The worst case is this value time 2^0x25 */
    flash->erase_timeout = 1ull << (__cvmx_flash_read_cmd(chip_id, 0x21) +
                                    __cvmx_flash_read_cmd(chip_id, 0x25));

    /* Get the flash size. This is 2^0x27 */
    flash->size = 1<<__cvmx_flash_read_cmd(chip_id, 0x27);

    /* Get the number of different sized block regions from 0x2c */
    flash->num_regions = __cvmx_flash_read_cmd(chip_id, 0x2c);

    int start_offset = 0;
    /* Loop through all regions get information about each */
    for (region=0; region<flash->num_regions; region++)
    {
        cvmx_flash_region_t *rgn_ptr = flash->region + region;
        rgn_ptr->start_offset = start_offset;

        /* The number of blocks in each region is a 16 bit little endian
            endian field. It is encoded at 0x2d + region*4 as (blocks-1) */
        uint16_t blocks = __cvmx_flash_read_cmd16(chip_id, 0x2d + region*4);
        rgn_ptr->num_blocks =  1u + blocks;

        /* The size of each block is a 16 bit little endian endian field. It
            is encoded at 0x2d + region*4 + 2 as (size/256). Zero is a special
            case representing 128 */
        uint16_t size = __cvmx_flash_read_cmd16(chip_id, 0x2d + region*4 + 2);
        if (size == 0)
            rgn_ptr->block_size = 128;
        else
            rgn_ptr->block_size = 256u * size;

        start_offset += rgn_ptr->block_size * rgn_ptr->num_blocks;
    }

    /* Take the chip out of CFI query mode */
    switch (flash_info[chip_id].vendor)
    {
        case CFI_CMDSET_AMD_STANDARD:
            __cvmx_flash_write_cmd(chip_id, 0x00, 0xf0);
        case CFI_CMDSET_INTEL_STANDARD:
        case CFI_CMDSET_INTEL_EXTENDED:
            __cvmx_flash_write_cmd(chip_id, 0x00, 0xff);
            break;
    }

    /* Convert the timeouts to cycles */
    flash->write_timeout *= cvmx_clock_get_rate(CVMX_CLOCK_CORE) / 1000000;
    flash->erase_timeout *= cvmx_clock_get_rate(CVMX_CLOCK_CORE) / 1000;

#if DEBUG
    /* Print the information about the chip */
    cvmx_dprintf("cvmx-flash: Base pointer:  %p\n"
           "            Vendor:        0x%04x\n"
           "            Size:          %d bytes\n"
           "            Num regions:   %d\n"
           "            Erase timeout: %llu cycles\n"
           "            Write timeout: %llu cycles\n",
           flash->base_ptr,
           (unsigned int)flash->vendor,
           flash->size,
           flash->num_regions,
           (unsigned long long)flash->erase_timeout,
           (unsigned long long)flash->write_timeout);

    for (region=0; region<flash->num_regions; region++)
    {
        cvmx_dprintf("            Region %d: offset 0x%x, %d blocks, %d bytes/block\n",
               region,
               flash->region[region].start_offset,
               flash->region[region].num_blocks,
               flash->region[region].block_size);
    }
#endif

    return 0;
}


/**
 * Initialize the flash access library
 */
void cvmx_flash_initialize(void)
{
    int boot_region;
    int chip_id = 0;

    memset(flash_info, 0, sizeof(flash_info));

    /* Loop through each boot bus chip select region */
    for (boot_region=0; boot_region<MAX_NUM_FLASH_CHIPS; boot_region++)
    {
        cvmx_mio_boot_reg_cfgx_t region_cfg;
        region_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFG0 + boot_region*8);
        /* Only try chip select regions that are enabled. This assumes the
            bootloader already setup the flash */
        if (region_cfg.s.en)
        {
            /* Convert the hardware address to a pointer. Note that the bootbus,
                unlike memory, isn't 1:1 mapped in the simple exec */
            void *base_ptr = cvmx_phys_to_ptr((region_cfg.s.base<<16) | 0xffffffff80000000ull);
            if (__cvmx_flash_queury_cfi(chip_id, base_ptr) == 0)
            {
                /* Valid CFI flash chip found */
                chip_id++;
            }
        }
    }

    if (chip_id == 0)
        cvmx_dprintf("cvmx-flash: No CFI chips found\n");
}


/**
 * Return a pointer to the flash chip
 *
 * @param chip_id Chip ID to return
 * @return NULL if the chip doesn't exist
 */
void *cvmx_flash_get_base(int chip_id)
{
    return flash_info[chip_id].base_ptr;
}


/**
 * Return the number of erasable regions on the chip
 *
 * @param chip_id Chip to return info for
 * @return Number of regions
 */
int cvmx_flash_get_num_regions(int chip_id)
{
    return flash_info[chip_id].num_regions;
}


/**
 * Return information about a flash chips region
 *
 * @param chip_id Chip to get info for
 * @param region  Region to get info for
 * @return Region information
 */
const cvmx_flash_region_t *cvmx_flash_get_region_info(int chip_id, int region)
{
    return flash_info[chip_id].region + region;
}


/**
 * Erase a block on the flash chip
 *
 * @param chip_id Chip to erase a block on
 * @param region  Region to erase a block in
 * @param block   Block number to erase
 * @return Zero on success. Negative on failure
 */
int cvmx_flash_erase_block(int chip_id, int region, int block)
{
    cvmx_spinlock_lock(&flash_lock);
#if DEBUG
    cvmx_dprintf("cvmx-flash: Erasing chip %d, region %d, block %d\n",
           chip_id, region, block);
#endif

    int offset = flash_info[chip_id].region[region].start_offset +
                block * flash_info[chip_id].region[region].block_size;

    switch (flash_info[chip_id].vendor)
    {
        case CFI_CMDSET_AMD_STANDARD:
        {
            /* Send the erase sector command sequence */
            __cvmx_flash_write_cmd(chip_id, 0x00, 0xf0); /* Reset the flash chip */
            __cvmx_flash_write_cmd(chip_id, 0x555, 0xaa);
            __cvmx_flash_write_cmd(chip_id, 0x2aa, 0x55);
            __cvmx_flash_write_cmd(chip_id, 0x555, 0x80);
            __cvmx_flash_write_cmd(chip_id, 0x555, 0xaa);
            __cvmx_flash_write_cmd(chip_id, 0x2aa, 0x55);
            __cvmx_flash_write8(chip_id, offset, 0x30);

            /* Loop checking status */
            uint8_t status = __cvmx_flash_read8(chip_id, offset);
            uint64_t start_cycle = cvmx_get_cycle();
            while (1)
            {
                /* Read the status and xor it with the old status so we can
                    find toggling bits */
                uint8_t old_status = status;
                status = __cvmx_flash_read8(chip_id, offset);
                uint8_t toggle = status ^ old_status;

                /* Check if the erase in progress bit is toggling */
                if (toggle & (1<<6))
                {
                    /* Check hardware timeout */
                    if (status & (1<<5))
                    {
                        /* Chip has signalled a timeout. Reread the status */
                        old_status = __cvmx_flash_read8(chip_id, offset);
                        status = __cvmx_flash_read8(chip_id, offset);
                        toggle = status ^ old_status;

                        /* Check if the erase in progress bit is toggling */
                        if (toggle & (1<<6))
                        {
                            cvmx_dprintf("cvmx-flash: Hardware timeout erasing block\n");
                            cvmx_spinlock_unlock(&flash_lock);
                            return -1;
                        }
                        else
                            break;  /* Not toggling, erase complete */
                    }
                }
                else
                    break;  /* Not toggling, erase complete */

                if (cvmx_get_cycle() > start_cycle + flash_info[chip_id].erase_timeout)
                {
                    cvmx_dprintf("cvmx-flash: Timeout erasing block\n");
                    cvmx_spinlock_unlock(&flash_lock);
                    return -1;
                }
            }

            __cvmx_flash_write_cmd(chip_id, 0x00, 0xf0); /* Reset the flash chip */
            cvmx_spinlock_unlock(&flash_lock);
            return 0;
        }
        case CFI_CMDSET_INTEL_STANDARD:
        case CFI_CMDSET_INTEL_EXTENDED:
        {
            /* Send the erase sector command sequence */
            __cvmx_flash_write_cmd(chip_id, 0x00, 0xff); /* Reset the flash chip */
            __cvmx_flash_write8(chip_id, offset, 0x20);
            __cvmx_flash_write8(chip_id, offset, 0xd0);

            /* Loop checking status */
            uint8_t status = __cvmx_flash_read8(chip_id, offset);
            uint64_t start_cycle = cvmx_get_cycle();
            while ((status & 0x80) == 0)
            {
                if (cvmx_get_cycle() > start_cycle + flash_info[chip_id].erase_timeout)
                {
                    cvmx_dprintf("cvmx-flash: Timeout erasing block\n");
                    cvmx_spinlock_unlock(&flash_lock);
                    return -1;
                }
                status = __cvmx_flash_read8(chip_id, offset);
            }

            /* Check the final status */
            if (status & 0x7f)
            {
                cvmx_dprintf("cvmx-flash: Hardware failure erasing block\n");
                cvmx_spinlock_unlock(&flash_lock);
                return -1;
            }

            __cvmx_flash_write_cmd(chip_id, 0x00, 0xff); /* Reset the flash chip */
            cvmx_spinlock_unlock(&flash_lock);
            return 0;
        }
    }

    cvmx_dprintf("cvmx-flash: Unsupported flash vendor\n");
    cvmx_spinlock_unlock(&flash_lock);
    return -1;
}


/**
 * Write a block on the flash chip
 *
 * @param chip_id Chip to write a block on
 * @param region  Region to write a block in
 * @param block   Block number to write
 * @param data    Data to write
 * @return Zero on success. Negative on failure
 */
int cvmx_flash_write_block(int chip_id, int region, int block, const void *data)
{
    cvmx_spinlock_lock(&flash_lock);
#if DEBUG
    cvmx_dprintf("cvmx-flash: Writing chip %d, region %d, block %d\n",
           chip_id, region, block);
#endif
    int offset = flash_info[chip_id].region[region].start_offset +
                block * flash_info[chip_id].region[region].block_size;
    int len = flash_info[chip_id].region[region].block_size;
    const uint8_t *ptr = (const uint8_t *)data;

    switch (flash_info[chip_id].vendor)
    {
        case CFI_CMDSET_AMD_STANDARD:
        {
            /* Loop through one byte at a time */
            while (len--)
            {
                /* Send the program sequence */
                __cvmx_flash_write_cmd(chip_id, 0x00, 0xf0); /* Reset the flash chip */
                __cvmx_flash_write_cmd(chip_id, 0x555, 0xaa);
                __cvmx_flash_write_cmd(chip_id, 0x2aa, 0x55);
                __cvmx_flash_write_cmd(chip_id, 0x555, 0xa0);
                __cvmx_flash_write8(chip_id, offset, *ptr);

                /* Loop polling for status */
                uint64_t start_cycle = cvmx_get_cycle();
                while (1)
                {
                    uint8_t status = __cvmx_flash_read8(chip_id, offset);
                    if (((status ^ *ptr) & (1<<7)) == 0)
                        break;  /* Data matches, this byte is done */
                    else if (status & (1<<5))
                    {
                        /* Hardware timeout, recheck status */
                        status = __cvmx_flash_read8(chip_id, offset);
                        if (((status ^ *ptr) & (1<<7)) == 0)
                            break;  /* Data matches, this byte is done */
                        else
                        {
                            cvmx_dprintf("cvmx-flash: Hardware write timeout\n");
                            cvmx_spinlock_unlock(&flash_lock);
                            return -1;
                        }
                    }

                    if (cvmx_get_cycle() > start_cycle + flash_info[chip_id].write_timeout)
                    {
                        cvmx_dprintf("cvmx-flash: Timeout writing block\n");
                        cvmx_spinlock_unlock(&flash_lock);
                        return -1;
                    }
                }

                /* Increment to the next byte */
                ptr++;
                offset++;
            }

            __cvmx_flash_write_cmd(chip_id, 0x00, 0xf0); /* Reset the flash chip */
            cvmx_spinlock_unlock(&flash_lock);
            return 0;
        }
        case CFI_CMDSET_INTEL_STANDARD:
        case CFI_CMDSET_INTEL_EXTENDED:
        {
cvmx_dprintf("%s:%d len=%d\n", __FUNCTION__, __LINE__, len);
            /* Loop through one byte at a time */
            while (len--)
            {
                /* Send the program sequence */
                __cvmx_flash_write_cmd(chip_id, 0x00, 0xff); /* Reset the flash chip */
                __cvmx_flash_write8(chip_id, offset, 0x40);
                __cvmx_flash_write8(chip_id, offset, *ptr);

                /* Loop polling for status */
                uint8_t status = __cvmx_flash_read8(chip_id, offset);
                uint64_t start_cycle = cvmx_get_cycle();
                while ((status & 0x80) == 0)
                {
                    if (cvmx_get_cycle() > start_cycle + flash_info[chip_id].write_timeout)
                    {
                        cvmx_dprintf("cvmx-flash: Timeout writing block\n");
                        cvmx_spinlock_unlock(&flash_lock);
                        return -1;
                    }
                    status = __cvmx_flash_read8(chip_id, offset);
                }

                /* Check the final status */
                if (status & 0x7f)
                {
                    cvmx_dprintf("cvmx-flash: Hardware failure erasing block\n");
                    cvmx_spinlock_unlock(&flash_lock);
                    return -1;
                }

                /* Increment to the next byte */
                ptr++;
                offset++;
            }
cvmx_dprintf("%s:%d\n", __FUNCTION__, __LINE__);

            __cvmx_flash_write_cmd(chip_id, 0x00, 0xff); /* Reset the flash chip */
            cvmx_spinlock_unlock(&flash_lock);
            return 0;
        }
    }

    cvmx_dprintf("cvmx-flash: Unsupported flash vendor\n");
    cvmx_spinlock_unlock(&flash_lock);
    return -1;
}


/**
 * Erase and write data to a flash
 *
 * @param address Memory address to write to
 * @param data    Data to write
 * @param len     Length of the data
 * @return Zero on success. Negative on failure
 */
int cvmx_flash_write(void *address, const void *data, int len)
{
    int chip_id;

    /* Find which chip controls this address. Don't allow the write to span
        multiple chips */
    for (chip_id=0; chip_id<MAX_NUM_FLASH_CHIPS; chip_id++)
    {
        if ((flash_info[chip_id].base_ptr <= address) &&
            (flash_info[chip_id].base_ptr + flash_info[chip_id].size >= address + len))
            break;
    }

    if (chip_id == MAX_NUM_FLASH_CHIPS)
    {
        cvmx_dprintf("cvmx-flash: Unable to find chip that contains address %p\n", address);
        return -1;
    }

    cvmx_flash_t *flash = flash_info + chip_id;

    /* Determine which block region we need to start writing to */
    void *region_base = flash->base_ptr;
    int region = 0;
    while (region_base + flash->region[region].num_blocks * flash->region[region].block_size <= address)
    {
        region++;
        region_base = flash->base_ptr + flash->region[region].start_offset;
    }

    /* Determine which block in the region to start at */
    int block = (address - region_base) / flash->region[region].block_size;

    /* Require all writes to start on block boundries */
    if (address != region_base + block*flash->region[region].block_size)
    {
        cvmx_dprintf("cvmx-flash: Write address not aligned on a block boundry\n");
        return -1;
    }

    /* Loop until we're out of data */
    while (len > 0)
    {
        /* Erase the current block */
        if (cvmx_flash_erase_block(chip_id, region, block))
            return -1;
        /* Write the new data */
        if (cvmx_flash_write_block(chip_id, region, block, data))
            return -1;

        /* Increment to the next block */
        data += flash->region[region].block_size;
        len -= flash->region[region].block_size;
        block++;
        if (block >= flash->region[region].num_blocks)
        {
            block = 0;
            region++;
        }
    }

    return 0;
}

