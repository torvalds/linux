/*
 *      linux/drivers/video/maxinefb.h
 *
 *      DECstation 5000/xx onboard framebuffer support, Copyright (C) 1999 by
 *      Michael Engel <engel@unix-ag.org> and Karsten Merker <merker@guug.de>
 *      This file is subject to the terms and conditions of the GNU General
 *      Public License.  See the file COPYING in the main directory of this
 *      archive for more details.
 */

#include <asm/addrspace.h>

/*
 * IMS332 video controller register base address
 */
#define MAXINEFB_IMS332_ADDRESS		KSEG1ADDR(0x1c140000)

/*
 * Begin of DECstation 5000/xx onboard framebuffer memory, default resolution
 * is 1024x768x8
 */
#define DS5000_xx_ONBOARD_FBMEM_START	KSEG1ADDR(0x0a000000)

/*
 *      The IMS 332 video controller used in the DECstation 5000/xx series
 *      uses 32 bits wide registers; the following defines declare the
 *      register numbers, to get the real offset, these have to be multiplied
 *      by four.
 */

#define IMS332_REG_CURSOR_RAM           0x200	/* hardware cursor bitmap */

/*
 * The color palette entries have the form 0x00BBGGRR
 */
#define IMS332_REG_COLOR_PALETTE        0x100	/* color palette, 256 entries */
#define IMS332_REG_CURSOR_COLOR_PALETTE	0x0a1	/* cursor color palette, */
						/* 3 entries             */
