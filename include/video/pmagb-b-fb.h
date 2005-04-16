/*
 *      linux/drivers/video/pmagb-b-fb.h
 *
 *      TurboChannel PMAGB-B framebuffer card support,
 *      Copyright (C) 1999, 2000, 2001 by
 *      Michael Engel <engel@unix-ag.org> and 
 *      Karsten Merker <merker@linuxtag.org>
 *      This file is subject to the terms and conditions of the GNU General
 *      Public License.  See the file COPYING in the main directory of this
 *      archive for more details.
 */


/*
 * Bt459 RAM DAC register base offset (rel. to TC slot base address)
 */
#define PMAGB_B_BT459_OFFSET			0x001C0000

/*
 * Begin of PMAGB-B framebuffer memory, resolution is configurable:
 * 1024x864x8 or 1280x1024x8, settable by jumper on the card
 */
#define PMAGB_B_ONBOARD_FBMEM_OFFSET	0x00201000

/*
 * Bt459 register offsets, byte-wide registers
 */

#define BT459_ADR_LOW			BT459_OFFSET + 0x00	/* addr. low */
#define BT459_ADR_HIGH			BT459_OFFSET + 0x04	/* addr. high */
#define BT459_DATA			BT459_OFFSET + 0x08	/* r/w data */
#define BT459_CMAP			BT459_OFFSET + 0x0C	/* color map */
