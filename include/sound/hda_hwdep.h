/*
 * HWDEP Interface for HD-audio codec
 *
 * Copyright (c) 2007 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SOUND_HDA_HWDEP_H
#define __SOUND_HDA_HWDEP_H

#define HDA_HWDEP_VERSION	((1 << 16) | (0 << 8) | (0 << 0)) /* 1.0.0 */

/* verb */
#define HDA_REG_NID_SHIFT	24
#define HDA_REG_VERB_SHIFT	8
#define HDA_REG_VAL_SHIFT	0
#define HDA_VERB(nid,verb,param)	((nid)<<24 | (verb)<<8 | (param))

struct hda_verb_ioctl {
	u32 verb;	/* HDA_VERB() */
	u32 res;	/* response */
};

/*
 * ioctls
 */
#define HDA_IOCTL_PVERSION		_IOR('H', 0x10, int)
#define HDA_IOCTL_VERB_WRITE		_IOWR('H', 0x11, struct hda_verb_ioctl)
#define HDA_IOCTL_GET_WCAP		_IOWR('H', 0x12, struct hda_verb_ioctl)

#endif
