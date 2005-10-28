/*
 *   GF1 (GUS) Patch - Instrument routines
 *   Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
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
 
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/ainstr_gf1.h>
#include <sound/initval.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture GF1 (GUS) Patch support.");
MODULE_LICENSE("GPL");

static unsigned int snd_seq_gf1_size(unsigned int size, unsigned int format)
{
	unsigned int result = size;
	
	if (format & GF1_WAVE_16BIT)
		result <<= 1;
	if (format & GF1_WAVE_STEREO)
		result <<= 1;
	return format;
}

static int snd_seq_gf1_copy_wave_from_stream(snd_gf1_ops_t *ops,
					     gf1_instrument_t *ip,
					     char __user **data,
					     long *len,
					     int atomic)
{
	gf1_wave_t *wp, *prev;
	gf1_xwave_t xp;
	int err;
	gfp_t gfp_mask;
	unsigned int real_size;
	
	gfp_mask = atomic ? GFP_ATOMIC : GFP_KERNEL;
	if (*len < (long)sizeof(xp))
		return -EINVAL;
	if (copy_from_user(&xp, *data, sizeof(xp)))
		return -EFAULT;
	*data += sizeof(xp);
	*len -= sizeof(xp);
	wp = kzalloc(sizeof(*wp), gfp_mask);
	if (wp == NULL)
		return -ENOMEM;
	wp->share_id[0] = le32_to_cpu(xp.share_id[0]);
	wp->share_id[1] = le32_to_cpu(xp.share_id[1]);
	wp->share_id[2] = le32_to_cpu(xp.share_id[2]);
	wp->share_id[3] = le32_to_cpu(xp.share_id[3]);
	wp->format = le32_to_cpu(xp.format);
	wp->size = le32_to_cpu(xp.size);
	wp->start = le32_to_cpu(xp.start);
	wp->loop_start = le32_to_cpu(xp.loop_start);
	wp->loop_end = le32_to_cpu(xp.loop_end);
	wp->loop_repeat = le16_to_cpu(xp.loop_repeat);
	wp->flags = xp.flags;
	wp->sample_rate = le32_to_cpu(xp.sample_rate);
	wp->low_frequency = le32_to_cpu(xp.low_frequency);
	wp->high_frequency = le32_to_cpu(xp.high_frequency);
	wp->root_frequency = le32_to_cpu(xp.root_frequency);
	wp->tune = le16_to_cpu(xp.tune);
	wp->balance = xp.balance;
	memcpy(wp->envelope_rate, xp.envelope_rate, 6);
	memcpy(wp->envelope_offset, xp.envelope_offset, 6);
	wp->tremolo_sweep = xp.tremolo_sweep;
	wp->tremolo_rate = xp.tremolo_rate;
	wp->tremolo_depth = xp.tremolo_depth;
	wp->vibrato_sweep = xp.vibrato_sweep;
	wp->vibrato_rate = xp.vibrato_rate;
	wp->vibrato_depth = xp.vibrato_depth;
	wp->scale_frequency = le16_to_cpu(xp.scale_frequency);
	wp->scale_factor = le16_to_cpu(xp.scale_factor);
	real_size = snd_seq_gf1_size(wp->size, wp->format);
	if ((long)real_size > *len) {
		kfree(wp);
		return -ENOMEM;
	}
	if (ops->put_sample) {
		err = ops->put_sample(ops->private_data, wp,
				      *data, real_size, atomic);
		if (err < 0) {
			kfree(wp);
			return err;
		}
	}
	*data += real_size;
	*len -= real_size;
	prev = ip->wave;
	if (prev) {
		while (prev->next) prev = prev->next;
		prev->next = wp;
	} else {
		ip->wave = wp;
	}
	return 0;
}

static void snd_seq_gf1_wave_free(snd_gf1_ops_t *ops,
				  gf1_wave_t *wave,
				  int atomic)
{
	if (ops->remove_sample)
		ops->remove_sample(ops->private_data, wave, atomic);
	kfree(wave);
}

static void snd_seq_gf1_instr_free(snd_gf1_ops_t *ops,
				   gf1_instrument_t *ip,
				   int atomic)
{
	gf1_wave_t *wave;
	
	while ((wave = ip->wave) != NULL) {
		ip->wave = wave->next;
		snd_seq_gf1_wave_free(ops, wave, atomic);
	}
}

static int snd_seq_gf1_put(void *private_data, snd_seq_kinstr_t *instr,
			   char __user *instr_data, long len, int atomic,
			   int cmd)
{
	snd_gf1_ops_t *ops = (snd_gf1_ops_t *)private_data;
	gf1_instrument_t *ip;
	gf1_xinstrument_t ix;
	int err;
	gfp_t gfp_mask;

	if (cmd != SNDRV_SEQ_INSTR_PUT_CMD_CREATE)
		return -EINVAL;
	gfp_mask = atomic ? GFP_ATOMIC : GFP_KERNEL;
	/* copy instrument data */
	if (len < (long)sizeof(ix))
		return -EINVAL;
	if (copy_from_user(&ix, instr_data, sizeof(ix)))
		return -EFAULT;
	if (ix.stype != GF1_STRU_INSTR)
		return -EINVAL;
	instr_data += sizeof(ix);
	len -= sizeof(ix);
	ip = (gf1_instrument_t *)KINSTR_DATA(instr);
	ip->exclusion = le16_to_cpu(ix.exclusion);
	ip->exclusion_group = le16_to_cpu(ix.exclusion_group);
	ip->effect1 = ix.effect1;
	ip->effect1_depth = ix.effect1_depth;
	ip->effect2 = ix.effect2;
	ip->effect2_depth = ix.effect2_depth;
	/* copy layers */
	while (len > (long)sizeof(__u32)) {
		__u32 stype;

		if (copy_from_user(&stype, instr_data, sizeof(stype)))
			return -EFAULT;
		if (stype != GF1_STRU_WAVE) {
			snd_seq_gf1_instr_free(ops, ip, atomic);
			return -EINVAL;
		}
		err = snd_seq_gf1_copy_wave_from_stream(ops,
							ip,
							&instr_data,
							&len,
							atomic);
		if (err < 0) {
			snd_seq_gf1_instr_free(ops, ip, atomic);
			return err;
		}
	}
	return 0;
}

static int snd_seq_gf1_copy_wave_to_stream(snd_gf1_ops_t *ops,
					   gf1_instrument_t *ip,
					   char __user **data,
					   long *len,
					   int atomic)
{
	gf1_wave_t *wp;
	gf1_xwave_t xp;
	int err;
	unsigned int real_size;
	
	for (wp = ip->wave; wp; wp = wp->next) {
		if (*len < (long)sizeof(xp))
			return -ENOMEM;
		memset(&xp, 0, sizeof(xp));
		xp.stype = GF1_STRU_WAVE;
		xp.share_id[0] = cpu_to_le32(wp->share_id[0]);
		xp.share_id[1] = cpu_to_le32(wp->share_id[1]);
		xp.share_id[2] = cpu_to_le32(wp->share_id[2]);
		xp.share_id[3] = cpu_to_le32(wp->share_id[3]);
		xp.format = cpu_to_le32(wp->format);
		xp.size = cpu_to_le32(wp->size);
		xp.start = cpu_to_le32(wp->start);
		xp.loop_start = cpu_to_le32(wp->loop_start);
		xp.loop_end = cpu_to_le32(wp->loop_end);
		xp.loop_repeat = cpu_to_le32(wp->loop_repeat);
		xp.flags = wp->flags;
		xp.sample_rate = cpu_to_le32(wp->sample_rate);
		xp.low_frequency = cpu_to_le32(wp->low_frequency);
		xp.high_frequency = cpu_to_le32(wp->high_frequency);
		xp.root_frequency = cpu_to_le32(wp->root_frequency);
		xp.tune = cpu_to_le16(wp->tune);
		xp.balance = wp->balance;
		memcpy(xp.envelope_rate, wp->envelope_rate, 6);
		memcpy(xp.envelope_offset, wp->envelope_offset, 6);
		xp.tremolo_sweep = wp->tremolo_sweep;
		xp.tremolo_rate = wp->tremolo_rate;
		xp.tremolo_depth = wp->tremolo_depth;
		xp.vibrato_sweep = wp->vibrato_sweep;
		xp.vibrato_rate = wp->vibrato_rate;
		xp.vibrato_depth = wp->vibrato_depth;
		xp.scale_frequency = cpu_to_le16(wp->scale_frequency);
		xp.scale_factor = cpu_to_le16(wp->scale_factor);
		if (copy_to_user(*data, &xp, sizeof(xp)))
			return -EFAULT;
		*data += sizeof(xp);
		*len -= sizeof(xp);
		real_size = snd_seq_gf1_size(wp->size, wp->format);
		if (*len < (long)real_size)
			return -ENOMEM;
		if (ops->get_sample) {
			err = ops->get_sample(ops->private_data, wp,
					      *data, real_size, atomic);
			if (err < 0)
				return err;
		}
		*data += wp->size;
		*len -= wp->size;
	}
	return 0;
}

static int snd_seq_gf1_get(void *private_data, snd_seq_kinstr_t *instr,
			   char __user *instr_data, long len, int atomic,
			   int cmd)
{
	snd_gf1_ops_t *ops = (snd_gf1_ops_t *)private_data;
	gf1_instrument_t *ip;
	gf1_xinstrument_t ix;
	
	if (cmd != SNDRV_SEQ_INSTR_GET_CMD_FULL)
		return -EINVAL;
	if (len < (long)sizeof(ix))
		return -ENOMEM;
	memset(&ix, 0, sizeof(ix));
	ip = (gf1_instrument_t *)KINSTR_DATA(instr);
	ix.stype = GF1_STRU_INSTR;
	ix.exclusion = cpu_to_le16(ip->exclusion);
	ix.exclusion_group = cpu_to_le16(ip->exclusion_group);
	ix.effect1 = cpu_to_le16(ip->effect1);
	ix.effect1_depth = cpu_to_le16(ip->effect1_depth);
	ix.effect2 = ip->effect2;
	ix.effect2_depth = ip->effect2_depth;
	if (copy_to_user(instr_data, &ix, sizeof(ix)))
		return -EFAULT;
	instr_data += sizeof(ix);
	len -= sizeof(ix);
	return snd_seq_gf1_copy_wave_to_stream(ops,
					       ip,
					       &instr_data,
					       &len,
					       atomic);
}

static int snd_seq_gf1_get_size(void *private_data, snd_seq_kinstr_t *instr,
				long *size)
{
	long result;
	gf1_instrument_t *ip;
	gf1_wave_t *wp;

	*size = 0;
	ip = (gf1_instrument_t *)KINSTR_DATA(instr);
	result = sizeof(gf1_xinstrument_t);
	for (wp = ip->wave; wp; wp = wp->next) {
		result += sizeof(gf1_xwave_t);
		result += wp->size;
	}
	*size = result;
	return 0;
}

static int snd_seq_gf1_remove(void *private_data,
			      snd_seq_kinstr_t *instr,
                              int atomic)
{
	snd_gf1_ops_t *ops = (snd_gf1_ops_t *)private_data;
	gf1_instrument_t *ip;

	ip = (gf1_instrument_t *)KINSTR_DATA(instr);
	snd_seq_gf1_instr_free(ops, ip, atomic);
	return 0;
}

static void snd_seq_gf1_notify(void *private_data,
			       snd_seq_kinstr_t *instr,
			       int what)
{
	snd_gf1_ops_t *ops = (snd_gf1_ops_t *)private_data;

	if (ops->notify)
		ops->notify(ops->private_data, instr, what);
}

int snd_seq_gf1_init(snd_gf1_ops_t *ops,
		     void *private_data,
		     snd_seq_kinstr_ops_t *next)
{
	memset(ops, 0, sizeof(*ops));
	ops->private_data = private_data;
	ops->kops.private_data = ops;
	ops->kops.add_len = sizeof(gf1_instrument_t);
	ops->kops.instr_type = SNDRV_SEQ_INSTR_ID_GUS_PATCH;
	ops->kops.put = snd_seq_gf1_put;
	ops->kops.get = snd_seq_gf1_get;
	ops->kops.get_size = snd_seq_gf1_get_size;
	ops->kops.remove = snd_seq_gf1_remove;
	ops->kops.notify = snd_seq_gf1_notify;
	ops->kops.next = next;
	return 0;
}

/*
 *  Init part
 */

static int __init alsa_ainstr_gf1_init(void)
{
	return 0;
}

static void __exit alsa_ainstr_gf1_exit(void)
{
}

module_init(alsa_ainstr_gf1_init)
module_exit(alsa_ainstr_gf1_exit)

EXPORT_SYMBOL(snd_seq_gf1_init);
