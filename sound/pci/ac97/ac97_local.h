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

#define AC97_SINGLE_VALUE(reg,shift,mask,invert) ((reg) | ((shift) << 8) | ((shift) << 12) | ((mask) << 16) | ((invert) << 24))
#define AC97_PAGE_SINGLE_VALUE(reg,shift,mask,invert,page) (AC97_SINGLE_VALUE(reg,shift,mask,invert) | (1<<25) | ((page) << 26))
#define AC97_SINGLE(xname, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_info_volsw, \
  .get = snd_ac97_get_volsw, .put = snd_ac97_put_volsw, \
  .private_value =  AC97_SINGLE_VALUE(reg, shift, mask, invert) }
#define AC97_PAGE_SINGLE(xname, reg, shift, mask, invert, page)		\
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_info_volsw, \
  .get = snd_ac97_get_volsw, .put = snd_ac97_put_volsw, \
  .private_value =  AC97_PAGE_SINGLE_VALUE(reg, shift, mask, invert, page) }
#define AC97_DOUBLE(xname, reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), .info = snd_ac97_info_volsw, \
  .get = snd_ac97_get_volsw, .put = snd_ac97_put_volsw, \
  .private_value = (reg) | ((shift_left) << 8) | ((shift_right) << 12) | ((mask) << 16) | ((invert) << 24) }
#define AC97_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xmask, xtexts) \
{ .reg = xreg, .shift_l = xshift_l, .shift_r = xshift_r, \
  .mask = xmask, .texts = xtexts }
#define AC97_ENUM_SINGLE(xreg, xshift, xmask, xtexts) \
	AC97_ENUM_DOUBLE(xreg, xshift, xshift, xmask, xtexts)
#define AC97_ENUM(xname, xenum) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_info_enum_double, \
  .get = snd_ac97_get_enum_double, .put = snd_ac97_put_enum_double, \
  .private_value = (unsigned long)&xenum }

/* ac97_codec.c */
extern const char *snd_ac97_stereo_enhancements[];
extern const snd_kcontrol_new_t snd_ac97_controls_3d[];
extern const snd_kcontrol_new_t snd_ac97_controls_spdif[];
snd_kcontrol_t *snd_ac97_cnew(const snd_kcontrol_new_t *_template, ac97_t * ac97);
void snd_ac97_get_name(ac97_t *ac97, unsigned int id, char *name, int modem);
int snd_ac97_info_volsw(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo);
int snd_ac97_get_volsw(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
int snd_ac97_put_volsw(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
int snd_ac97_try_bit(ac97_t * ac97, int reg, int bit);
int snd_ac97_remove_ctl(ac97_t *ac97, const char *name, const char *suffix);
int snd_ac97_rename_ctl(ac97_t *ac97, const char *src, const char *dst, const char *suffix);
int snd_ac97_swap_ctl(ac97_t *ac97, const char *s1, const char *s2, const char *suffix);
void snd_ac97_rename_vol_ctl(ac97_t *ac97, const char *src, const char *dst);
void snd_ac97_restore_status(ac97_t *ac97);
void snd_ac97_restore_iec958(ac97_t *ac97);
int snd_ac97_info_enum_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo);
int snd_ac97_get_enum_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
int snd_ac97_put_enum_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);

int snd_ac97_update_bits_nolock(ac97_t *ac97, unsigned short reg,
				unsigned short mask, unsigned short value);

/* ac97_proc.c */
#ifdef CONFIG_PROC_FS
void snd_ac97_bus_proc_init(ac97_bus_t * ac97);
void snd_ac97_bus_proc_done(ac97_bus_t * ac97);
void snd_ac97_proc_init(ac97_t * ac97);
void snd_ac97_proc_done(ac97_t * ac97);
#else
#define snd_ac97_bus_proc_init(ac97_bus_t) do { } while (0)
#define snd_ac97_bus_proc_done(ac97_bus_t) do { } while (0)
#define snd_ac97_proc_init(ac97_t) do { } while (0)
#define snd_ac97_proc_done(ac97_t) do { } while (0)
#endif
