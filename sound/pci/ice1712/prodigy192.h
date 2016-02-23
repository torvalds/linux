#ifndef __SOUND_PRODIGY192_H
#define __SOUND_PRODIGY192_H

#define PRODIGY192_DEVICE_DESC 	       "{AudioTrak,Prodigy 192},"
#define PRODIGY192_STAC9460_ADDR	0x54

#define VT1724_SUBDEVICE_PRODIGY192VE	 0x34495345	/* PRODIGY 192 VE */
/*
 *  AudioTrak Prodigy192 GPIO definitions for MI/ODI/O card with
 *  AK4114 (SPDIF-IN)
 */
#define VT1724_PRODIGY192_CS	(1 << 8)	/* GPIO8, pin 75 */
#define VT1724_PRODIGY192_CCLK	(1 << 9)	/* GPIO9, pin 76 */
#define VT1724_PRODIGY192_CDOUT	(1 << 10)	/* GPIO10, pin 77 */
#define VT1724_PRODIGY192_CDIN	(1 << 11)	/* GPIO11, pin 86 */

extern struct snd_ice1712_card_info  snd_vt1724_prodigy192_cards[];

#endif	/* __SOUND_PRODIGY192_H */
