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

typedef struct seq_oss_synth_t seq_oss_synth_t;

void snd_seq_oss_synth_init(void);
int snd_seq_oss_synth_register(snd_seq_device_t *dev);
int snd_seq_oss_synth_unregister(snd_seq_device_t *dev);
void snd_seq_oss_synth_setup(seq_oss_devinfo_t *dp);
void snd_seq_oss_synth_setup_midi(seq_oss_devinfo_t *dp);
void snd_seq_oss_synth_cleanup(seq_oss_devinfo_t *dp);

void snd_seq_oss_synth_reset(seq_oss_devinfo_t *dp, int dev);
int snd_seq_oss_synth_load_patch(seq_oss_devinfo_t *dp, int dev, int fmt, const char __user *buf, int p, int c);
int snd_seq_oss_synth_is_valid(seq_oss_devinfo_t *dp, int dev);
int snd_seq_oss_synth_sysex(seq_oss_devinfo_t *dp, int dev, unsigned char *buf, snd_seq_event_t *ev);
int snd_seq_oss_synth_addr(seq_oss_devinfo_t *dp, int dev, snd_seq_event_t *ev);
int snd_seq_oss_synth_ioctl(seq_oss_devinfo_t *dp, int dev, unsigned int cmd, unsigned long addr);
int snd_seq_oss_synth_raw_event(seq_oss_devinfo_t *dp, int dev, unsigned char *data, snd_seq_event_t *ev);

int snd_seq_oss_synth_make_info(seq_oss_devinfo_t *dp, int dev, struct synth_info *inf);

#endif
