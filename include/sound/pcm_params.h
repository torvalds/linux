#ifndef __SOUND_PCM_PARAMS_H
#define __SOUND_PCM_PARAMS_H

/*
 *  PCM params helpers
 *  Copyright (c) by Abramo Bagnara <abramo@alsa-project.org>
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

#include <sound/pcm.h>

int snd_pcm_hw_param_first(struct snd_pcm_substream *pcm, 
			   struct snd_pcm_hw_params *params,
			   snd_pcm_hw_param_t var, int *dir);
int snd_pcm_hw_param_last(struct snd_pcm_substream *pcm, 
			  struct snd_pcm_hw_params *params,
			  snd_pcm_hw_param_t var, int *dir);
int snd_pcm_hw_param_value(const struct snd_pcm_hw_params *params,
			   snd_pcm_hw_param_t var, int *dir);

#define SNDRV_MASK_BITS	64	/* we use so far 64bits only */
#define SNDRV_MASK_SIZE	(SNDRV_MASK_BITS / 32)
#define MASK_OFS(i)	((i) >> 5)
#define MASK_BIT(i)	(1U << ((i) & 31))

static inline size_t snd_mask_sizeof(void)
{
	return sizeof(struct snd_mask);
}

static inline void snd_mask_none(struct snd_mask *mask)
{
	memset(mask, 0, sizeof(*mask));
}

static inline void snd_mask_any(struct snd_mask *mask)
{
	memset(mask, 0xff, SNDRV_MASK_SIZE * sizeof(u_int32_t));
}

static inline int snd_mask_empty(const struct snd_mask *mask)
{
	int i;
	for (i = 0; i < SNDRV_MASK_SIZE; i++)
		if (mask->bits[i])
			return 0;
	return 1;
}

static inline unsigned int snd_mask_min(const struct snd_mask *mask)
{
	int i;
	for (i = 0; i < SNDRV_MASK_SIZE; i++) {
		if (mask->bits[i])
			return __ffs(mask->bits[i]) + (i << 5);
	}
	return 0;
}

static inline unsigned int snd_mask_max(const struct snd_mask *mask)
{
	int i;
	for (i = SNDRV_MASK_SIZE - 1; i >= 0; i--) {
		if (mask->bits[i])
			return __fls(mask->bits[i]) + (i << 5);
	}
	return 0;
}

static inline void snd_mask_set(struct snd_mask *mask, unsigned int val)
{
	mask->bits[MASK_OFS(val)] |= MASK_BIT(val);
}

static inline void snd_mask_reset(struct snd_mask *mask, unsigned int val)
{
	mask->bits[MASK_OFS(val)] &= ~MASK_BIT(val);
}

static inline void snd_mask_set_range(struct snd_mask *mask,
				      unsigned int from, unsigned int to)
{
	unsigned int i;
	for (i = from; i <= to; i++)
		mask->bits[MASK_OFS(i)] |= MASK_BIT(i);
}

static inline void snd_mask_reset_range(struct snd_mask *mask,
					unsigned int from, unsigned int to)
{
	unsigned int i;
	for (i = from; i <= to; i++)
		mask->bits[MASK_OFS(i)] &= ~MASK_BIT(i);
}

static inline void snd_mask_leave(struct snd_mask *mask, unsigned int val)
{
	unsigned int v;
	v = mask->bits[MASK_OFS(val)] & MASK_BIT(val);
	snd_mask_none(mask);
	mask->bits[MASK_OFS(val)] = v;
}

static inline void snd_mask_intersect(struct snd_mask *mask,
				      const struct snd_mask *v)
{
	int i;
	for (i = 0; i < SNDRV_MASK_SIZE; i++)
		mask->bits[i] &= v->bits[i];
}

static inline int snd_mask_eq(const struct snd_mask *mask,
			      const struct snd_mask *v)
{
	return ! memcmp(mask, v, SNDRV_MASK_SIZE * sizeof(u_int32_t));
}

static inline void snd_mask_copy(struct snd_mask *mask,
				 const struct snd_mask *v)
{
	*mask = *v;
}

static inline int snd_mask_test(const struct snd_mask *mask, unsigned int val)
{
	return mask->bits[MASK_OFS(val)] & MASK_BIT(val);
}

static inline int snd_mask_single(const struct snd_mask *mask)
{
	int i, c = 0;
	for (i = 0; i < SNDRV_MASK_SIZE; i++) {
		if (! mask->bits[i])
			continue;
		if (mask->bits[i] & (mask->bits[i] - 1))
			return 0;
		if (c)
			return 0;
		c++;
	}
	return 1;
}

static inline int snd_mask_refine(struct snd_mask *mask,
				  const struct snd_mask *v)
{
	struct snd_mask old;
	snd_mask_copy(&old, mask);
	snd_mask_intersect(mask, v);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return !snd_mask_eq(mask, &old);
}

static inline int snd_mask_refine_first(struct snd_mask *mask)
{
	if (snd_mask_single(mask))
		return 0;
	snd_mask_leave(mask, snd_mask_min(mask));
	return 1;
}

static inline int snd_mask_refine_last(struct snd_mask *mask)
{
	if (snd_mask_single(mask))
		return 0;
	snd_mask_leave(mask, snd_mask_max(mask));
	return 1;
}

static inline int snd_mask_refine_min(struct snd_mask *mask, unsigned int val)
{
	if (snd_mask_min(mask) >= val)
		return 0;
	snd_mask_reset_range(mask, 0, val - 1);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return 1;
}

static inline int snd_mask_refine_max(struct snd_mask *mask, unsigned int val)
{
	if (snd_mask_max(mask) <= val)
		return 0;
	snd_mask_reset_range(mask, val + 1, SNDRV_MASK_BITS);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return 1;
}

static inline int snd_mask_refine_set(struct snd_mask *mask, unsigned int val)
{
	int changed;
	changed = !snd_mask_single(mask);
	snd_mask_leave(mask, val);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return changed;
}

static inline int snd_mask_value(const struct snd_mask *mask)
{
	return snd_mask_min(mask);
}

static inline void snd_interval_any(struct snd_interval *i)
{
	i->min = 0;
	i->openmin = 0;
	i->max = UINT_MAX;
	i->openmax = 0;
	i->integer = 0;
	i->empty = 0;
}

static inline void snd_interval_none(struct snd_interval *i)
{
	i->empty = 1;
}

static inline int snd_interval_checkempty(const struct snd_interval *i)
{
	return (i->min > i->max ||
		(i->min == i->max && (i->openmin || i->openmax)));
}

static inline int snd_interval_empty(const struct snd_interval *i)
{
	return i->empty;
}

static inline int snd_interval_single(const struct snd_interval *i)
{
	return (i->min == i->max || 
		(i->min + 1 == i->max && i->openmax));
}

static inline int snd_interval_value(const struct snd_interval *i)
{
	return i->min;
}

static inline int snd_interval_min(const struct snd_interval *i)
{
	return i->min;
}

static inline int snd_interval_max(const struct snd_interval *i)
{
	unsigned int v;
	v = i->max;
	if (i->openmax)
		v--;
	return v;
}

static inline int snd_interval_test(const struct snd_interval *i, unsigned int val)
{
	return !((i->min > val || (i->min == val && i->openmin) ||
		  i->max < val || (i->max == val && i->openmax)));
}

static inline void snd_interval_copy(struct snd_interval *d, const struct snd_interval *s)
{
	*d = *s;
}

static inline int snd_interval_setinteger(struct snd_interval *i)
{
	if (i->integer)
		return 0;
	if (i->openmin && i->openmax && i->min == i->max)
		return -EINVAL;
	i->integer = 1;
	return 1;
}

static inline int snd_interval_eq(const struct snd_interval *i1, const struct snd_interval *i2)
{
	if (i1->empty)
		return i2->empty;
	if (i2->empty)
		return i1->empty;
	return i1->min == i2->min && i1->openmin == i2->openmin &&
		i1->max == i2->max && i1->openmax == i2->openmax;
}

/**
 * params_access - get the access type from the hw params
 * @p: hw params
 */
static inline snd_pcm_access_t params_access(const struct snd_pcm_hw_params *p)
{
	return (__force snd_pcm_access_t)snd_mask_min(hw_param_mask_c(p,
		SNDRV_PCM_HW_PARAM_ACCESS));
}

/**
 * params_format - get the sample format from the hw params
 * @p: hw params
 */
static inline snd_pcm_format_t params_format(const struct snd_pcm_hw_params *p)
{
	return (__force snd_pcm_format_t)snd_mask_min(hw_param_mask_c(p,
		SNDRV_PCM_HW_PARAM_FORMAT));
}

/**
 * params_subformat - get the sample subformat from the hw params
 * @p: hw params
 */
static inline snd_pcm_subformat_t
params_subformat(const struct snd_pcm_hw_params *p)
{
	return (__force snd_pcm_subformat_t)snd_mask_min(hw_param_mask_c(p,
		SNDRV_PCM_HW_PARAM_SUBFORMAT));
}

/**
 * params_period_bytes - get the period size (in bytes) from the hw params
 * @p: hw params
 */
static inline unsigned int
params_period_bytes(const struct snd_pcm_hw_params *p)
{
	return hw_param_interval_c(p, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->min;
}

/**
 * params_width - get the number of bits of the sample format from the hw params
 * @p: hw params
 *
 * This function returns the number of bits per sample that the selected sample
 * format of the hw params has.
 */
static inline int params_width(const struct snd_pcm_hw_params *p)
{
	return snd_pcm_format_width(params_format(p));
}

/*
 * params_physical_width - get the storage size of the sample format from the hw params
 * @p: hw params
 *
 * This functions returns the number of bits per sample that the selected sample
 * format of the hw params takes up in memory. This will be equal or larger than
 * params_width().
 */
static inline int params_physical_width(const struct snd_pcm_hw_params *p)
{
	return snd_pcm_format_physical_width(params_format(p));
}

#endif /* __SOUND_PCM_PARAMS_H */
