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




#ifndef __CVMX_BOOTLOADER__
#define __CVMX_BOOTLOADER__



/**
 * @file
 *
 * Bootloader definitions that are shared with other programs
 *
 * <hr>$Revision: 70030 $<hr>
 */


/* The bootloader_header_t structure defines the header that is present
** at the start of binary u-boot images.  This header is used to locate the bootloader
** image in NAND, and also to allow verification of images for normal NOR booting.
** This structure is placed at the beginning of a bootloader binary image, and remains
** in the executable code.
*/
#define BOOTLOADER_HEADER_MAGIC 0x424f4f54  /* "BOOT" in ASCII */

#define BOOTLOADER_HEADER_COMMENT_LEN  64
#define BOOTLOADER_HEADER_VERSION_LEN  64
#define BOOTLOADER_HEADER_MAX_SIZE      0x200 /* limited by the space to the next exception handler */

#define BOOTLOADER_HEADER_CURRENT_MAJOR_REV 1
#define BOOTLOADER_HEADER_CURRENT_MINOR_REV 2
/* Revision history
* 1.1  Initial released revision. (SDK 1.9)
* 1.2  TLB based relocatable image (SDK 2.0)
*
*
*/

/* offsets to struct bootloader_header fields for assembly use */
#define GOT_ADDRESS_OFFSET       48

#define LOOKUP_STEP (64*1024)

#ifndef __ASSEMBLY__
typedef struct bootloader_header
{
    uint32_t    jump_instr; /* Jump to executable code following the
                            ** header.  This allows this header to
                            ** be (and remain) part of the executable image)
                            */
    uint32_t    nop_instr;  /* Must be 0x0 */
    uint32_t    magic; /* Magic number to identify header */
    uint32_t    hcrc;  /* CRC of all of header excluding this field */

    uint16_t    hlen;  /* Length of header in bytes */
    uint16_t    maj_rev;  /* Major revision */
    uint16_t    min_rev;  /* Minor revision */
    uint16_t    board_type;  /* Board type that the image is for */

    uint32_t    dlen;  /* Length of data (immediately following header) in bytes */
    uint32_t    dcrc;  /* CRC of data */
    uint64_t    address;  /* Mips virtual address */
    uint32_t    flags;
    uint16_t    image_type;  /* Defined in bootloader_image_t enum */
    uint16_t    resv0;       /* pad */

    uint32_t    reserved1;
    uint32_t    reserved2;
    uint32_t    reserved3;
    uint32_t    reserved4;

    char        comment_string[BOOTLOADER_HEADER_COMMENT_LEN];  /* Optional, for descriptive purposes */
    char        version_string[BOOTLOADER_HEADER_VERSION_LEN];  /* Optional, for descriptive purposes */
} __attribute__((packed)) bootloader_header_t;



/* Defines for flag field */
#define BL_HEADER_FLAG_FAILSAFE         (1)


typedef enum
{
    BL_HEADER_IMAGE_UNKNOWN = 0x0,
    BL_HEADER_IMAGE_STAGE2,  /* Binary bootloader stage2 image (NAND boot) */
    BL_HEADER_IMAGE_STAGE3,  /* Binary bootloader stage3 image (NAND boot)*/
    BL_HEADER_IMAGE_NOR,     /* Binary bootloader for NOR boot */
    BL_HEADER_IMAGE_PCIBOOT,     /* Binary bootloader for PCI boot */
    BL_HEADER_IMAGE_UBOOT_ENV,  /* Environment for u-boot */
    BL_HEADER_IMAGE_MAX,
    /* Range for customer private use.  Will not be used by Cavium Inc. */
    BL_HEADER_IMAGE_CUST_RESERVED_MIN = 0x1000,
    BL_HEADER_IMAGE_CUST_RESERVED_MAX = 0x1fff
} bootloader_image_t;

#endif /* __ASSEMBLY__ */

/* Maximum address searched for NAND boot images and environments.  This is used
** by stage1 and stage2. */
#define MAX_NAND_SEARCH_ADDR   0x400000

/* Maximum address to look for start of normal bootloader */
#define MAX_NOR_SEARCH_ADDR   0x200000

/* Defines for RAM based environment set by the host or the previous bootloader
** in a chain boot configuration. */

#define U_BOOT_RAM_ENV_ADDR     (0x1000)
#define U_BOOT_RAM_ENV_SIZE     (0x1000)
#define U_BOOT_RAM_ENV_CRC_SIZE (0x4)
#define U_BOOT_RAM_ENV_ADDR_2	(U_BOOT_RAM_ENV_ADDR + U_BOOT_RAM_ENV_SIZE)

#endif /* __CVMX_BOOTLOADER__ */
