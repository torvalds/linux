/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   ALSA sequencer /proc info
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 */
#ifndef __SND_SEQ_INFO_H
#define __SND_SEQ_INFO_H

#include <sound/info.h>
#include <sound/seq_kernel.h>

void snd_seq_info_clients_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer);
void snd_seq_info_timer_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer);
void snd_seq_info_queues_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer);


#ifdef CONFIG_SND_PROC_FS
int snd_seq_info_init(void);
void snd_seq_info_done(void);
#else
static inline int snd_seq_info_init(void) { return 0; }
static inline void snd_seq_info_done(void) {}
#endif

#endif
