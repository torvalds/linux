/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
 *
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
 *
 */

int patch_yamaha_ymf753(struct snd_ac97 * ac97);
int patch_wolfson00(struct snd_ac97 * ac97);
int patch_wolfson03(struct snd_ac97 * ac97);
int patch_wolfson04(struct snd_ac97 * ac97);
int patch_wolfson05(struct snd_ac97 * ac97);
int patch_wolfson11(struct snd_ac97 * ac97);
int patch_wolfson13(struct snd_ac97 * ac97);
int patch_tritech_tr28028(struct snd_ac97 * ac97);
int patch_sigmatel_stac9700(struct snd_ac97 * ac97);
int patch_sigmatel_stac9708(struct snd_ac97 * ac97);
int patch_sigmatel_stac9721(struct snd_ac97 * ac97);
int patch_sigmatel_stac9744(struct snd_ac97 * ac97);
int patch_sigmatel_stac9756(struct snd_ac97 * ac97);
int patch_sigmatel_stac9758(struct snd_ac97 * ac97);
int patch_cirrus_cs4299(struct snd_ac97 * ac97);
int patch_cirrus_spdif(struct snd_ac97 * ac97);
int patch_conexant(struct snd_ac97 * ac97);
int patch_cx20551(struct snd_ac97 * ac97);
int patch_ad1819(struct snd_ac97 * ac97);
int patch_ad1881(struct snd_ac97 * ac97);
int patch_ad1885(struct snd_ac97 * ac97);
int patch_ad1886(struct snd_ac97 * ac97);
int patch_ad1888(struct snd_ac97 * ac97);
int patch_ad1980(struct snd_ac97 * ac97);
int patch_ad1981a(struct snd_ac97 * ac97);
int patch_ad1981b(struct snd_ac97 * ac97);
int patch_ad1985(struct snd_ac97 * ac97);
int patch_ad1986(struct snd_ac97 * ac97);
int patch_alc650(struct snd_ac97 * ac97);
int patch_alc655(struct snd_ac97 * ac97);
int patch_alc850(struct snd_ac97 * ac97);
int patch_cm9738(struct snd_ac97 * ac97);
int patch_cm9739(struct snd_ac97 * ac97);
int patch_cm9761(struct snd_ac97 * ac97);
int patch_cm9780(struct snd_ac97 * ac97);
int patch_vt1616(struct snd_ac97 * ac97);
int patch_vt1617a(struct snd_ac97 * ac97);
int patch_it2646(struct snd_ac97 * ac97);
int patch_ucb1400(struct snd_ac97 * ac97);
int mpatch_si3036(struct snd_ac97 * ac97);
int patch_lm4550(struct snd_ac97 * ac97);
