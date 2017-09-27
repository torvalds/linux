/*
 * OSS compatible sequencer driver
 *
 * synth device information
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SEQ_OSS_SYNTH_H
#define __SEQ_OSS_SYNTH_H

#include "seq_oss_device.h"
#include <sound/seq_oss_legacy.h>
#include <sound/seq_device.h>

void snd_seq_oss_synth_init(void);
int snd_seq_oss_synth_probe(struct device *dev);
int snd_seq_oss_synth_remove(struct device *dev);
void snd_seq_oss_synth_setup(struct seq_oss_devinfo *dp);
void snd_seq_oss_synth_setup_midi(struct seq_oss_devinfo *dp);
void snd_seq_oss_synth_cleanup(struct seq_oss_devinfo *dp);

void snd_seq_oss_synth_reset(struct seq_oss_devinfo *dp, int dev);
int snd_seq_oss_synth_load_patch(struct seq_oss_devinfo *dp, int dev, int fmt,
				 const char __user *buf, int p, int c);
int snd_seq_oss_synth_is_valid(struct seq_oss_devinfo *dp, int dev);
int snd_seq_oss_synth_sysex(struct seq_oss_devinfo *dp, int dev, unsigned char *buf,
			    struct snd_seq_event *ev);
int snd_seq_oss_synth_addr(struct seq_oss_devinfo *dp, int dev, struct snd_seq_event *ev);
int snd_seq_oss_synth_ioctl(struct seq_oss_devinfo *dp, int dev, unsigned int cmd,
			    unsigned long addr);
int snd_seq_oss_synth_raw_event(struct seq_oss_devinfo *dp, int dev,
				unsigned char *data, struct snd_seq_event *ev);

int snd_seq_oss_synth_make_info(struct seq_oss_devinfo *dp, int dev, struct synth_info *inf);

#endif
