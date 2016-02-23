/*
 * Driver for Digigram miXart soundcards
 *
 * include file for mixer
 *
 * Copyright (c) 2003 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SOUND_MIXART_MIXER_H
#define __SOUND_MIXART_MIXER_H

/* exported */
int mixart_update_playback_stream_level(struct snd_mixart* chip, int is_aes, int idx);
int mixart_update_capture_stream_level(struct snd_mixart* chip, int is_aes);
int snd_mixart_create_mixer(struct mixart_mgr* mgr);

#endif /* __SOUND_MIXART_MIXER_H */
