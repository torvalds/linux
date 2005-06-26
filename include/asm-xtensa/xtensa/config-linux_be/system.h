/*
 * xtensa/config/system.h -- HAL definitions that are dependent on SYSTEM configuration
 *
 *  NOTE: The location and contents of this file are highly subject to change.
 *
 *  Source for configuration-independent binaries (which link in a
 *  configuration-specific HAL library) must NEVER include this file.
 *  The HAL itself has historically included this file in some instances,
 *  but this is not appropriate either, because the HAL is meant to be
 *  core-specific but system independent.
 */

/*
 * Copyright (c) 2003 Tensilica, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307,
 * USA.
 */


#ifndef XTENSA_CONFIG_SYSTEM_H
#define XTENSA_CONFIG_SYSTEM_H

/*#include <xtensa/hal.h>*/



/*----------------------------------------------------------------------
				DEVICE ADDRESSES
  ----------------------------------------------------------------------*/

/*
 *  Strange place to find these, but the configuration GUI
 *  allows moving these around to account for various core
 *  configurations.  Specific boards (and their BSP software)
 *  will have specific meanings for these components.
 */

/*  I/O Block areas:  */
#define XSHAL_IOBLOCK_CACHED_VADDR	0xE0000000
#define XSHAL_IOBLOCK_CACHED_PADDR	0xF0000000
#define XSHAL_IOBLOCK_CACHED_SIZE	0x0E000000

#define XSHAL_IOBLOCK_BYPASS_VADDR	0xF0000000
#define XSHAL_IOBLOCK_BYPASS_PADDR	0xF0000000
#define XSHAL_IOBLOCK_BYPASS_SIZE	0x0E000000

/*  System ROM:  */
#define XSHAL_ROM_VADDR		0xEE000000
#define XSHAL_ROM_PADDR		0xFE000000
#define XSHAL_ROM_SIZE		0x00400000
/*  Largest available area (free of vectors):  */
#define XSHAL_ROM_AVAIL_VADDR	0xEE00052C
#define XSHAL_ROM_AVAIL_VSIZE	0x003FFAD4

/*  System RAM:  */
#define XSHAL_RAM_VADDR		0xD0000000
#define XSHAL_RAM_PADDR		0x00000000
#define XSHAL_RAM_VSIZE		0x08000000
#define XSHAL_RAM_PSIZE		0x10000000
#define XSHAL_RAM_SIZE		XSHAL_RAM_PSIZE
/*  Largest available area (free of vectors):  */
#define XSHAL_RAM_AVAIL_VADDR	0xD0000370
#define XSHAL_RAM_AVAIL_VSIZE	0x07FFFC90

/*
 *  Shadow system RAM (same device as system RAM, at different address).
 *  (Emulation boards need this for the SONIC Ethernet driver
 *   when data caches are configured for writeback mode.)
 *  NOTE: on full MMU configs, this points to the BYPASS virtual address
 *  of system RAM, ie. is the same as XSHAL_RAM_* except that virtual
 *  addresses are viewed through the BYPASS static map rather than
 *  the CACHED static map.
 */
#define XSHAL_RAM_BYPASS_VADDR		0xD8000000
#define XSHAL_RAM_BYPASS_PADDR		0x00000000
#define XSHAL_RAM_BYPASS_PSIZE		0x08000000

/*  Alternate system RAM (different device than system RAM):  */
#define XSHAL_ALTRAM_VADDR		0xCEE00000
#define XSHAL_ALTRAM_PADDR		0xC0000000
#define XSHAL_ALTRAM_SIZE		0x00200000


/*----------------------------------------------------------------------
 *			DEVICE-ADDRESS DEPENDENT...
 *
 *  Values written to CACHEATTR special register (or its equivalent)
 *  to enable and disable caches in various modes.
 *----------------------------------------------------------------------*/

/*----------------------------------------------------------------------
			BACKWARD COMPATIBILITY ...
  ----------------------------------------------------------------------*/

/*
 *  NOTE:  the following two macros are DEPRECATED.  Use the latter
 *  board-specific macros instead, which are specially tuned for the
 *  particular target environments' memory maps.
 */
#define XSHAL_CACHEATTR_BYPASS		XSHAL_XT2000_CACHEATTR_BYPASS	/* disable caches in bypass mode */
#define XSHAL_CACHEATTR_DEFAULT		XSHAL_XT2000_CACHEATTR_DEFAULT	/* default setting to enable caches (no writeback!) */

/*----------------------------------------------------------------------
			ISS (Instruction Set Simulator) SPECIFIC ...
  ----------------------------------------------------------------------*/

#define XSHAL_ISS_CACHEATTR_WRITEBACK	0x1122222F	/* enable caches in write-back mode */
#define XSHAL_ISS_CACHEATTR_WRITEALLOC	0x1122222F	/* enable caches in write-allocate mode */
#define XSHAL_ISS_CACHEATTR_WRITETHRU	0x1122222F	/* enable caches in write-through mode */
#define XSHAL_ISS_CACHEATTR_BYPASS	0x2222222F	/* disable caches in bypass mode */
#define XSHAL_ISS_CACHEATTR_DEFAULT	XSHAL_ISS_CACHEATTR_WRITEBACK	/* default setting to enable caches */

/*  For Coware only:  */
#define XSHAL_COWARE_CACHEATTR_WRITEBACK	0x11222222	/* enable caches in write-back mode */
#define XSHAL_COWARE_CACHEATTR_WRITEALLOC	0x11222222	/* enable caches in write-allocate mode */
#define XSHAL_COWARE_CACHEATTR_WRITETHRU	0x11222222	/* enable caches in write-through mode */
#define XSHAL_COWARE_CACHEATTR_BYPASS		0x22222222	/* disable caches in bypass mode */
#define XSHAL_COWARE_CACHEATTR_DEFAULT		XSHAL_COWARE_CACHEATTR_WRITEBACK	/* default setting to enable caches */

/*  For BFM and other purposes:  */
#define XSHAL_ALLVALID_CACHEATTR_WRITEBACK	0x11222222	/* enable caches without any invalid regions */
#define XSHAL_ALLVALID_CACHEATTR_DEFAULT	XSHAL_ALLVALID_CACHEATTR_WRITEBACK	/* default setting for caches without any invalid regions */

#define XSHAL_ISS_PIPE_REGIONS	0
#define XSHAL_ISS_SDRAM_REGIONS	0


/*----------------------------------------------------------------------
			XT2000 BOARD SPECIFIC ...
  ----------------------------------------------------------------------*/

#define XSHAL_XT2000_CACHEATTR_WRITEBACK	0x22FFFFFF	/* enable caches in write-back mode */
#define XSHAL_XT2000_CACHEATTR_WRITEALLOC	0x22FFFFFF	/* enable caches in write-allocate mode */
#define XSHAL_XT2000_CACHEATTR_WRITETHRU	0x22FFFFFF	/* enable caches in write-through mode */
#define XSHAL_XT2000_CACHEATTR_BYPASS		0x22FFFFFF	/* disable caches in bypass mode */
#define XSHAL_XT2000_CACHEATTR_DEFAULT		XSHAL_XT2000_CACHEATTR_WRITEBACK	/* default setting to enable caches */

#define XSHAL_XT2000_PIPE_REGIONS	0x00001000	/* BusInt pipeline regions */
#define XSHAL_XT2000_SDRAM_REGIONS	0x00000005	/* BusInt SDRAM regions */


/*----------------------------------------------------------------------
				VECTOR SIZES
  ----------------------------------------------------------------------*/

/*
 *  Sizes allocated to vectors by the system (memory map) configuration.
 *  These sizes are constrained by core configuration (eg. one vector's
 *  code cannot overflow into another vector) but are dependent on the
 *  system or board (or LSP) memory map configuration.
 *
 *  Whether or not each vector happens to be in a system ROM is also
 *  a system configuration matter, sometimes useful, included here also:
 */
#define XSHAL_RESET_VECTOR_SIZE	0x000004E0
#define XSHAL_RESET_VECTOR_ISROM	1
#define XSHAL_USER_VECTOR_SIZE	0x0000001C
#define XSHAL_USER_VECTOR_ISROM	0
#define XSHAL_PROGRAMEXC_VECTOR_SIZE	XSHAL_USER_VECTOR_SIZE	/* for backward compatibility */
#define XSHAL_USEREXC_VECTOR_SIZE	XSHAL_USER_VECTOR_SIZE	/* for backward compatibility */
#define XSHAL_KERNEL_VECTOR_SIZE	0x0000001C
#define XSHAL_KERNEL_VECTOR_ISROM	0
#define XSHAL_STACKEDEXC_VECTOR_SIZE	XSHAL_KERNEL_VECTOR_SIZE	/* for backward compatibility */
#define XSHAL_KERNELEXC_VECTOR_SIZE	XSHAL_KERNEL_VECTOR_SIZE	/* for backward compatibility */
#define XSHAL_DOUBLEEXC_VECTOR_SIZE	0x000000E0
#define XSHAL_DOUBLEEXC_VECTOR_ISROM	0
#define XSHAL_WINDOW_VECTORS_SIZE	0x00000180
#define XSHAL_WINDOW_VECTORS_ISROM	0
#define XSHAL_INTLEVEL2_VECTOR_SIZE	0x0000000C
#define XSHAL_INTLEVEL2_VECTOR_ISROM	0
#define XSHAL_INTLEVEL3_VECTOR_SIZE	0x0000000C
#define XSHAL_INTLEVEL3_VECTOR_ISROM	0
#define XSHAL_INTLEVEL4_VECTOR_SIZE	0x0000000C
#define XSHAL_INTLEVEL4_VECTOR_ISROM	1
#define XSHAL_DEBUG_VECTOR_SIZE		XSHAL_INTLEVEL4_VECTOR_SIZE
#define XSHAL_DEBUG_VECTOR_ISROM	XSHAL_INTLEVEL4_VECTOR_ISROM


#endif /*XTENSA_CONFIG_SYSTEM_H*/

