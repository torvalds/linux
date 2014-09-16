#ifndef _TLK106_PHY_H
#define _TLK106_PHY_H

#define TLK106_PHY_ID_MASK	0xFFFFFFFF

/* 
 * The Model Revision number is the Least Significat Byte, so I 
 * built support for revision 0-5 just in case 
 * 
 */

#define PHY_ID_TLK106_0		0x2000A210
#define PHY_ID_TLK106_1		0x2000A211
#define PHY_ID_TLK106_2		0x2000A212
#define PHY_ID_TLK106_3		0x2000A213
#define PHY_ID_TLK106_4		0x2000A214
#define PHY_ID_TLK106_5		0x2000A215

#endif /* _TLK106_PHY_H */
