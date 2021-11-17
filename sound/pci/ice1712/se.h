/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_SE_H
#define __SOUND_SE_H

/* ID */
#define SE_DEVICE_DESC	\
		"{ONKYO INC,SE-90PCI},"\
		"{ONKYO INC,SE-200PCI},"

#define VT1724_SUBDEVICE_SE90PCI	0xb161000
#define VT1724_SUBDEVICE_SE200PCI	0xb160100

/* entry struct */
extern struct snd_ice1712_card_info snd_vt1724_se_cards[];

#endif /* __SOUND_SE_H */
