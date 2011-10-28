/*
 * sound/soc/codec/wl1273.h
 *
 * ALSA SoC WL1273 codec driver
 *
 * Copyright (C) Nokia Corporation
 * Author: Matti Aaltonen <matti.j.aaltonen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1273_CODEC_H__
#define __WL1273_CODEC_H__

int wl1273_get_format(struct snd_soc_codec *codec, unsigned int *fmt);

#endif	/* End of __WL1273_CODEC_H__ */
