/*
 * OSS compatible sequencer driver
 *
 * midi device information
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

#ifndef __SEQ_OSS_MIDI_H
#define __SEQ_OSS_MIDI_H

#include "seq_oss_device.h"
#include <sound/seq_oss_legacy.h>

typedef struct seq_oss_midi_t seq_oss_midi_t;

int snd_seq_oss_midi_lookup_ports(int client);
int snd_seq_oss_midi_check_new_port(snd_seq_port_info_t *pinfo);
int snd_seq_oss_midi_check_exit_port(int client, int port);
void snd_seq_oss_midi_clear_all(void);

void snd_seq_oss_midi_setup(seq_oss_devinfo_t *dp);
void snd_seq_oss_midi_cleanup(seq_oss_devinfo_t *dp);

int snd_seq_oss_midi_open(seq_oss_devinfo_t *dp, int dev, int file_mode);
void snd_seq_oss_midi_open_all(seq_oss_devinfo_t *dp, int file_mode);
int snd_seq_oss_midi_close(seq_oss_devinfo_t *dp, int dev);
void snd_seq_oss_midi_reset(seq_oss_devinfo_t *dp, int dev);
int snd_seq_oss_midi_putc(seq_oss_devinfo_t *dp, int dev, unsigned char c, snd_seq_event_t *ev);
int snd_seq_oss_midi_input(snd_seq_event_t *ev, int direct, void *private);
int snd_seq_oss_midi_filemode(seq_oss_devinfo_t *dp, int dev);
int snd_seq_oss_midi_make_info(seq_oss_devinfo_t *dp, int dev, struct midi_info *inf);
void snd_seq_oss_midi_get_addr(seq_oss_devinfo_t *dp, int dev, snd_seq_addr_t *addr);

#endif
