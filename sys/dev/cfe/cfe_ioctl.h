/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2000, 2001, 2002, 2003
 * Broadcom Corporation. All rights reserved.
 *
 * This software is furnished under license and may be used and 
 * copied only in accordance with the following terms and 
 * conditions.  Subject to these conditions, you may download, 
 * copy, install, use, modify and distribute modified or unmodified 
 * copies of this software in source and/or binary form.  No title 
 * or ownership is transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce and
 *    retain this copyright notice and list of conditions as they appear in
 *    the source file.
 *
 * 2) No right is granted to use any trade name, trademark, or logo of
 *    Broadcom Corporation.  The "Broadcom Corporation" name may not be
 *    used to endorse or promote products derived from this software
 *    without the prior written permission of Broadcom Corporation.
 *
 * 3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR IMPLIED
 *    WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED WARRANTIES OF
 *    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 *    NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM BE LIABLE
 *    FOR ANY DAMAGES WHATSOEVER, AND IN PARTICULAR, BROADCOM SHALL NOT BE
 *    LIABLE FOR DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *    OR OTHERWISE), EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*  *********************************************************************
    *  Broadcom Common Firmware Environment (CFE)
    *  
    *  IOCTL definitions			File: cfe_ioctl.h
    *  
    *  IOCTL function numbers and I/O data structures.
    *  
    *  Author:  Mitch Lichtenberg
    *  
    ********************************************************************* */


/*  *********************************************************************
    *  NVFAM and FLASH stuff
    ********************************************************************* */

#define IOCTL_NVRAM_GETINFO	1	/* return nvram_info_t */
#define IOCTL_NVRAM_ERASE	2	/* erase sector containing nvram_info_t area */
#define IOCTL_FLASH_ERASE_SECTOR 3	/* erase an arbitrary sector */
#define IOCTL_FLASH_ERASE_ALL   4	/* Erase the entire flash */
#define IOCTL_FLASH_WRITE_ALL	5	/* write entire flash */
#define IOCTL_FLASH_GETINFO	6	/* get flash device info */
#define IOCTL_FLASH_GETSECTORS	7	/* get sector information */
#define IOCTL_FLASH_ERASE_RANGE 8	/* erase range of bytes */
#define IOCTL_NVRAM_UNLOCK	9	/* allow r/w beyond logical end of device */
#define IOCTL_FLASH_PROTECT_RANGE 10	/* Protect a group of sectors */
#define IOCTL_FLASH_UNPROTECT_RANGE 11	/* unprotect a group of sectors */
#define IOCTL_FLASH_DATA_WIDTH_MODE	12 	/* switch flash and gen bus to support 8 or 16-bit mode I/Os */
#define IOCTL_FLASH_BURST_MODE	13	/* configure gen bus for burst mode */

typedef struct flash_range_s {
    unsigned int range_base;
    unsigned int range_length;
} flash_range_t;

typedef struct flash_info_s {
    unsigned long long flash_base;	/* flash physical base address */
    unsigned int flash_size;		/* available device size in bytes */
    unsigned int flash_type;		/* type, from FLASH_TYPE below */
    unsigned int flash_flags;		/* Various flags (FLASH_FLAG_xxx) */
} flash_info_t;

typedef struct flash_sector_s {
    int flash_sector_idx;
    int flash_sector_status;
    unsigned int flash_sector_offset;
    unsigned int flash_sector_size;
} flash_sector_t;

#define FLASH_SECTOR_OK		0
#define FLASH_SECTOR_INVALID	-1

#define FLASH_TYPE_UNKNOWN	0	/* not sure what kind of flash */
#define FLASH_TYPE_SRAM		1	/* not flash: it's SRAM */
#define FLASH_TYPE_ROM		2	/* not flash: it's ROM */
#define FLASH_TYPE_FLASH	3	/* it's flash memory of some sort */

#define FLASH_FLAG_NOERASE	1	/* Byte-range writes supported,
					   Erasing is not necessary */

typedef struct nvram_info_s {
    int nvram_offset;			/* offset of environment area */
    int nvram_size;			/* size of environment area */
    int nvram_eraseflg;			/* true if we need to erase first */
} nvram_info_t;

/*  *********************************************************************
    *  Ethernet stuff
    ********************************************************************* */

#define IOCTL_ETHER_GETHWADDR	1	/* Get hardware address (6bytes) */
#define IOCTL_ETHER_SETHWADDR   2	/* Set hardware address (6bytes) */
#define IOCTL_ETHER_GETSPEED    3	/* Get Speed and Media (int) */
#define IOCTL_ETHER_SETSPEED    4	/* Set Speed and Media (int) */
#define IOCTL_ETHER_GETLINK	5	/* get link status (int) */
#define IOCTL_ETHER_GETLOOPBACK	7	/* get loopback state */
#define IOCTL_ETHER_SETLOOPBACK	8	/* set loopback state */
#define IOCTL_ETHER_SETPACKETFIFO 9	/* set packet fifo mode (int) */
#define IOCTL_ETHER_SETSTROBESIG 10	/* set strobe signal (int) */

#define ETHER_LOOPBACK_OFF	0	/* no loopback */
#define ETHER_LOOPBACK_INT	1	/* Internal loopback */
#define ETHER_LOOPBACK_EXT	2	/* External loopback (through PHY) */

#define ETHER_SPEED_AUTO	0	/* Auto detect */
#define ETHER_SPEED_UNKNOWN	0	/* Speed not known (on link status) */
#define ETHER_SPEED_10HDX	1	/* 10MB hdx and fdx */
#define ETHER_SPEED_10FDX	2
#define ETHER_SPEED_100HDX	3	/* 100MB hdx and fdx */
#define ETHER_SPEED_100FDX	4
#define ETHER_SPEED_1000HDX	5	/* 1000MB hdx and fdx */
#define ETHER_SPEED_1000FDX	6

#define ETHER_FIFO_8		0	/* 8-bit packet fifo mode */
#define ETHER_FIFO_16		1	/* 16-bit packet fifo mode */
#define ETHER_ETHER		2	/* Standard ethernet mode */

#define ETHER_STROBE_GMII	0	/* GMII style strobe signal */
#define ETHER_STROBE_ENCODED	1	/* Encoded */
#define ETHER_STROBE_SOP	2	/* SOP flagged. Only in 8-bit mode*/
#define ETHER_STROBE_EOP	3	/* EOP flagged. Only in 8-bit mode*/

/*  *********************************************************************
    *  Serial Ports
    ********************************************************************* */

#define IOCTL_SERIAL_SETSPEED	1	/* get baud rate (int) */
#define IOCTL_SERIAL_GETSPEED	2	/* set baud rate (int) */
#define IOCTL_SERIAL_SETFLOW	3	/* Set Flow Control */
#define IOCTL_SERIAL_GETFLOW	4	/* Get Flow Control */

#define SERIAL_FLOW_NONE	0	/* no flow control */
#define SERIAL_FLOW_SOFTWARE	1	/* software flow control (not impl) */
#define SERIAL_FLOW_HARDWARE	2	/* hardware flow control */

/*  *********************************************************************
    *  Block device stuff
    ********************************************************************* */

#define IOCTL_BLOCK_GETBLOCKSIZE 1	/* get block size (int) */
#define IOCTL_BLOCK_GETTOTALBLOCKS 2	/* get total bocks (long long) */
#define IOCTL_BLOCK_GETDEVTYPE 3	/* get device type (struct) */

typedef struct blockdev_info_s {
    unsigned long long blkdev_totalblocks;
    unsigned int blkdev_blocksize;
    unsigned int blkdev_devtype;
} blockdev_info_t;

#define BLOCK_DEVTYPE_DISK	0
#define BLOCK_DEVTYPE_CDROM	1
