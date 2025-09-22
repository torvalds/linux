/*	$OpenBSD: if_ieatt.h,v 1.2 1997/11/07 08:06:58 niklas Exp $	*/
/*	$NetBSD: if_ieatt.h,v 1.2 1994/10/27 04:17:40 cgd Exp $	*/

/*
 * definitions for AT&T StarLAN 10 etc...
 */

#define IEATT_RESET 	0	/* any write here resets the 586 */
#define IEATT_ATTN 	1	/* any write here sends a Chan attn */
#define IEATT_REVISION	6	/* read here to figure out this board */
#define IEATT_ATTRIB	7	/* more information about this board */

#define SL_BOARD(x) ((x) & 0x0f)
#define SL_REV(x) ((x) >> 4)

#define SL1_BOARD	0
#define SL10_BOARD	1
#define EN100_BOARD	2
#define SLFIBER_BOARD	3

#define SL_ATTR_WIDTH	0x04	/* bus width: clear -> 8-bit */
#define SL_ATTR_SPEED	0x08	/* medium speed: clear -> 10 Mbps */
#define SL_ATTR_CODING	0x10	/* encoding: clear -> Manchester */
#define SL_ATTR_HBW	0x20	/* host bus width: clear -> 16-bit */
#define SL_ATTR_TYPE	0x40	/* medium type: clear -> Ethernet */
#define SL_ATTR_BOOTROM	0x80	/* set -> boot ROM present */
