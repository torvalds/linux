/*
 * aml_platform.h  --  ALSA audio platform interface for the AML Meson SoC
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _AML_PLATFORM_H
#define _AML_PLATFORM_H

#define AML_AUDIO_I2S       0
#define AML_AUDIO_PCM       1

struct aml_audio_interface
{
    unsigned int id;
    char    *name;
    struct  snd_pcm_ops *pcm_ops;

	int     (*pcm_new)(struct snd_soc_pcm_runtime *rtd);
	void    (*pcm_free)(struct snd_pcm *);
};

#endif /* _AML_PLATFORM_H */
