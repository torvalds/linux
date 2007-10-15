/*
 *   IWFFFF - AMD InterWave (tm) - Instrument routines
 *   Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
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
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/ainstr_iw.h>
#include <sound/initval.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture IWFFFF support.");
MODULE_LICENSE("GPL");

static unsigned int snd_seq_iwffff_size(unsigned int size, unsigned int format)
{
	unsigned int result = size;
	
	if (format & IWFFFF_WAVE_16BIT)
		result <<= 1;
	if (format & IWFFFF_WAVE_STEREO)
		result <<= 1;
	return result;
}

static void snd_seq_iwffff_copy_lfo_from_stream(struct iwffff_lfo *fp,
						struct iwffff_xlfo *fx)
{
	fp->freq = le16_to_cpu(fx->freq);
	fp->depth = le16_to_cpu(fx->depth);
	fp->sweep = le16_to_cpu(fx->sweep);
	fp->shape = fx->shape;
	fp->delay = fx->delay;
}

static int snd_seq_iwffff_copy_env_from_stream(__u32 req_stype,
					       struct iwffff_layer *lp,
					       struct iwffff_env *ep,
					       struct iwffff_xenv *ex,
					       char __user **data,
					       long *len,
					       gfp_t gfp_mask)
{
	__u32 stype;
	struct iwffff_env_record *rp, *rp_last;
	struct iwffff_xenv_record rx;
	struct iwffff_env_point *pp;
	struct iwffff_xenv_point px;
	int points_size, idx;

	ep->flags = ex->flags;
	ep->mode = ex->mode;
	ep->index = ex->index;
	rp_last = NULL;
	while (1) {
		if (*len < (long)sizeof(__u32))
			return -EINVAL;
		if (copy_from_user(&stype, *data, sizeof(stype)))
			return -EFAULT;
		if (stype == IWFFFF_STRU_WAVE)
			return 0;
		if (req_stype != stype) {
			if (stype == IWFFFF_STRU_ENV_RECP ||
			    stype == IWFFFF_STRU_ENV_RECV)
				return 0;
		}
		if (*len < (long)sizeof(rx))
			return -EINVAL;
		if (copy_from_user(&rx, *data, sizeof(rx)))
			return -EFAULT;
		*data += sizeof(rx);
		*len -= sizeof(rx);
		points_size = (le16_to_cpu(rx.nattack) + le16_to_cpu(rx.nrelease)) * 2 * sizeof(__u16);
		if (points_size > *len)
			return -EINVAL;
		rp = kzalloc(sizeof(*rp) + points_size, gfp_mask);
		if (rp == NULL)
			return -ENOMEM;
		rp->nattack = le16_to_cpu(rx.nattack);
		rp->nrelease = le16_to_cpu(rx.nrelease);
		rp->sustain_offset = le16_to_cpu(rx.sustain_offset);
		rp->sustain_rate = le16_to_cpu(rx.sustain_rate);
		rp->release_rate = le16_to_cpu(rx.release_rate);
		rp->hirange = rx.hirange;
		pp = (struct iwffff_env_point *)(rp + 1);
		for (idx = 0; idx < rp->nattack + rp->nrelease; idx++) {
			if (copy_from_user(&px, *data, sizeof(px)))
				return -EFAULT;
			*data += sizeof(px);
			*len -= sizeof(px);
			pp->offset = le16_to_cpu(px.offset);
			pp->rate = le16_to_cpu(px.rate);
		}
		if (ep->record == NULL) {
			ep->record = rp;
		} else {
			rp_last = rp;
		}
		rp_last = rp;
	}
	return 0;
}

static int snd_seq_iwffff_copy_wave_from_stream(struct snd_iwffff_ops *ops,
						struct iwffff_layer *lp,
					        char __user **data,
					        long *len,
					        int atomic)
{
	struct iwffff_wave *wp, *prev;
	struct iwffff_xwave xp;
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
	wp->address.memory = le32_to_cpu(xp.offset);
	wp->size = le32_to_cpu(xp.size);
	wp->start = le32_to_cpu(xp.start);
	wp->loop_start = le32_to_cpu(xp.loop_start);
	wp->loop_end = le32_to_cpu(xp.loop_end);
	wp->loop_repeat = le16_to_cpu(xp.loop_repeat);
	wp->sample_ratio = le32_to_cpu(xp.sample_ratio);
	wp->attenuation = xp.attenuation;
	wp->low_note = xp.low_note;
	wp->high_note = xp.high_note;
	real_size = snd_seq_iwffff_size(wp->size, wp->format);
	if (!(wp->format & IWFFFF_WAVE_ROM)) {
		if ((long)real_size > *len) {
			kfree(wp);
			return -ENOMEM;
		}
	}
	if (ops->put_sample) {
		err = ops->put_sample(ops->private_data, wp,
				      *data, real_size, atomic);
		if (err < 0) {
			kfree(wp);
			return err;
		}
	}
	if (!(wp->format & IWFFFF_WAVE_ROM)) {
		*data += real_size;
		*len -= real_size;
	}
	prev = lp->wave;
	if (prev) {
		while (prev->next) prev = prev->next;
		prev->next = wp;
	} else {
		lp->wave = wp;
	}
	return 0;
}

static void snd_seq_iwffff_env_free(struct snd_iwffff_ops *ops,
				    struct iwffff_env *env,
				    int atomic)
{
	struct iwffff_env_record *rec;
	
	while ((rec = env->record) != NULL) {
		env->record = rec->next;
		kfree(rec);
	}
}
				    
static void snd_seq_iwffff_wave_free(struct snd_iwffff_ops *ops,
				     struct iwffff_wave *wave,
				     int atomic)
{
	if (ops->remove_sample)
		ops->remove_sample(ops->private_data, wave, atomic);
	kfree(wave);
}

static void snd_seq_iwffff_instr_free(struct snd_iwffff_ops *ops,
                                      struct iwffff_instrument *ip,
                                      int atomic)
{
	struct iwffff_layer *layer;
	struct iwffff_wave *wave;
	
	while ((layer = ip->layer) != NULL) {
		ip->layer = layer->next;
		snd_seq_iwffff_env_free(ops, &layer->penv, atomic);
		snd_seq_iwffff_env_free(ops, &layer->venv, atomic);
		while ((wave = layer->wave) != NULL) {
			layer->wave = wave->next;
			snd_seq_iwffff_wave_free(ops, wave, atomic);
		}
		kfree(layer);
	}
}

static int snd_seq_iwffff_put(void *private_data, struct snd_seq_kinstr *instr,
			      char __user *instr_data, long len, int atomic,
			      int cmd)
{
	struct snd_iwffff_ops *ops = private_data;
	struct iwffff_instrument *ip;
	struct iwffff_xinstrument ix;
	struct iwffff_layer *lp, *prev_lp;
	struct iwffff_xlayer lx;
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
	if (ix.stype != IWFFFF_STRU_INSTR)
		return -EINVAL;
	instr_data += sizeof(ix);
	len -= sizeof(ix);
	ip = (struct iwffff_instrument *)KINSTR_DATA(instr);
	ip->exclusion = le16_to_cpu(ix.exclusion);
	ip->layer_type = le16_to_cpu(ix.layer_type);
	ip->exclusion_group = le16_to_cpu(ix.exclusion_group);
	ip->effect1 = ix.effect1;
	ip->effect1_depth = ix.effect1_depth;
	ip->effect2 = ix.effect2;
	ip->effect2_depth = ix.effect2_depth;
	/* copy layers */
	prev_lp = NULL;
	while (len > 0) {
		if (len < (long)sizeof(struct iwffff_xlayer)) {
			snd_seq_iwffff_instr_free(ops, ip, atomic);
			return -EINVAL;
		}
		if (copy_from_user(&lx, instr_data, sizeof(lx)))
			return -EFAULT;
		instr_data += sizeof(lx);
		len -= sizeof(lx);
		if (lx.stype != IWFFFF_STRU_LAYER) {
			snd_seq_iwffff_instr_free(ops, ip, atomic);
			return -EINVAL;
		}
		lp = kzalloc(sizeof(*lp), gfp_mask);
		if (lp == NULL) {
			snd_seq_iwffff_instr_free(ops, ip, atomic);
			return -ENOMEM;
		}
		if (prev_lp) {
			prev_lp->next = lp;
		} else {
			ip->layer = lp;
		}
		prev_lp = lp;
		lp->flags = lx.flags;
		lp->velocity_mode = lx.velocity_mode;
		lp->layer_event = lx.layer_event;
		lp->low_range = lx.low_range;
		lp->high_range = lx.high_range;
		lp->pan = lx.pan;
		lp->pan_freq_scale = lx.pan_freq_scale;
		lp->attenuation = lx.attenuation;
		snd_seq_iwffff_copy_lfo_from_stream(&lp->tremolo, &lx.tremolo);
		snd_seq_iwffff_copy_lfo_from_stream(&lp->vibrato, &lx.vibrato);
		lp->freq_scale = le16_to_cpu(lx.freq_scale);
		lp->freq_center = lx.freq_center;
		err = snd_seq_iwffff_copy_env_from_stream(IWFFFF_STRU_ENV_RECP,
							  lp,
							  &lp->penv, &lx.penv,
						          &instr_data, &len,
						          gfp_mask);
		if (err < 0) {
			snd_seq_iwffff_instr_free(ops, ip, atomic);
			return err;
		}
		err = snd_seq_iwffff_copy_env_from_stream(IWFFFF_STRU_ENV_RECV,
							  lp,
							  &lp->venv, &lx.venv,
						          &instr_data, &len,
						          gfp_mask);
		if (err < 0) {
			snd_seq_iwffff_instr_free(ops, ip, atomic);
			return err;
		}
		while (len > (long)sizeof(__u32)) {
			__u32 stype;

			if (copy_from_user(&stype, instr_data, sizeof(stype)))
				return -EFAULT;
			if (stype != IWFFFF_STRU_WAVE)
				break;
			err = snd_seq_iwffff_copy_wave_from_stream(ops,
								   lp,
							    	   &instr_data,
								   &len,
								   atomic);
			if (err < 0) {
				snd_seq_iwffff_instr_free(ops, ip, atomic);
				return err;
			}
		}
	}
	return 0;
}

static void snd_seq_iwffff_copy_lfo_to_stream(struct iwffff_xlfo *fx,
					      struct iwffff_lfo *fp)
{
	fx->freq = cpu_to_le16(fp->freq);
	fx->depth = cpu_to_le16(fp->depth);
	fx->sweep = cpu_to_le16(fp->sweep);
	fp->shape = fx->shape;
	fp->delay = fx->delay;
}

static int snd_seq_iwffff_copy_env_to_stream(__u32 req_stype,
					     struct iwffff_layer *lp,
					     struct iwffff_xenv *ex,
					     struct iwffff_env *ep,
					     char __user **data,
					     long *len)
{
	struct iwffff_env_record *rp;
	struct iwffff_xenv_record rx;
	struct iwffff_env_point *pp;
	struct iwffff_xenv_point px;
	int points_size, idx;

	ex->flags = ep->flags;
	ex->mode = ep->mode;
	ex->index = ep->index;
	for (rp = ep->record; rp; rp = rp->next) {
		if (*len < (long)sizeof(rx))
			return -ENOMEM;
		memset(&rx, 0, sizeof(rx));
		rx.stype = req_stype;
		rx.nattack = cpu_to_le16(rp->nattack);
		rx.nrelease = cpu_to_le16(rp->nrelease);
		rx.sustain_offset = cpu_to_le16(rp->sustain_offset);
		rx.sustain_rate = cpu_to_le16(rp->sustain_rate);
		rx.release_rate = cpu_to_le16(rp->release_rate);
		rx.hirange = cpu_to_le16(rp->hirange);
		if (copy_to_user(*data, &rx, sizeof(rx)))
			return -EFAULT;
		*data += sizeof(rx);
		*len -= sizeof(rx);
		points_size = (rp->nattack + rp->nrelease) * 2 * sizeof(__u16);
		if (*len < points_size)
			return -ENOMEM;
		pp = (struct iwffff_env_point *)(rp + 1);
		for (idx = 0; idx < rp->nattack + rp->nrelease; idx++) {
			px.offset = cpu_to_le16(pp->offset);
			px.rate = cpu_to_le16(pp->rate);
			if (copy_to_user(*data, &px, sizeof(px)))
				return -EFAULT;
			*data += sizeof(px);
			*len -= sizeof(px);
		}
	}
	return 0;
}

static int snd_seq_iwffff_copy_wave_to_stream(struct snd_iwffff_ops *ops,
					      struct iwffff_layer *lp,
					      char __user **data,
					      long *len,
					      int atomic)
{
	struct iwffff_wave *wp;
	struct iwffff_xwave xp;
	int err;
	unsigned int real_size;
	
	for (wp = lp->wave; wp; wp = wp->next) {
		if (*len < (long)sizeof(xp))
			return -ENOMEM;
		memset(&xp, 0, sizeof(xp));
		xp.stype = IWFFFF_STRU_WAVE;
		xp.share_id[0] = cpu_to_le32(wp->share_id[0]);
		xp.share_id[1] = cpu_to_le32(wp->share_id[1]);
		xp.share_id[2] = cpu_to_le32(wp->share_id[2]);
		xp.share_id[3] = cpu_to_le32(wp->share_id[3]);
		xp.format = cpu_to_le32(wp->format);
		if (wp->format & IWFFFF_WAVE_ROM)
			xp.offset = cpu_to_le32(wp->address.memory);
		xp.size = cpu_to_le32(wp->size);
		xp.start = cpu_to_le32(wp->start);
		xp.loop_start = cpu_to_le32(wp->loop_start);
		xp.loop_end = cpu_to_le32(wp->loop_end);
		xp.loop_repeat = cpu_to_le32(wp->loop_repeat);
		xp.sample_ratio = cpu_to_le32(wp->sample_ratio);
		xp.attenuation = wp->attenuation;
		xp.low_note = wp->low_note;
		xp.high_note = wp->high_note;
		if (copy_to_user(*data, &xp, sizeof(xp)))
			return -EFAULT;
		*data += sizeof(xp);
		*len -= sizeof(xp);
		real_size = snd_seq_iwffff_size(wp->size, wp->format);
		if (!(wp->format & IWFFFF_WAVE_ROM)) {
			if (*len < (long)real_size)
				return -ENOMEM;
		}
		if (ops->get_sample) {
			err = ops->get_sample(ops->private_data, wp,
					      *data, real_size, atomic);
			if (err < 0)
				return err;
		}
		if (!(wp->format & IWFFFF_WAVE_ROM)) {
			*data += real_size;
			*len -= real_size;
		}
	}
	return 0;
}

static int snd_seq_iwffff_get(void *private_data, struct snd_seq_kinstr *instr,
			      char __user *instr_data, long len, int atomic, int cmd)
{
	struct snd_iwffff_ops *ops = private_data;
	struct iwffff_instrument *ip;
	struct iwffff_xinstrument ix;
	struct iwffff_layer *lp;
	struct iwffff_xlayer lx;
	char __user *layer_instr_data;
	int err;
	
	if (cmd != SNDRV_SEQ_INSTR_GET_CMD_FULL)
		return -EINVAL;
	if (len < (long)sizeof(ix))
		return -ENOMEM;
	memset(&ix, 0, sizeof(ix));
	ip = (struct iwffff_instrument *)KINSTR_DATA(instr);
	ix.stype = IWFFFF_STRU_INSTR;
	ix.exclusion = cpu_to_le16(ip->exclusion);
	ix.layer_type = cpu_to_le16(ip->layer_type);
	ix.exclusion_group = cpu_to_le16(ip->exclusion_group);
	ix.effect1 = cpu_to_le16(ip->effect1);
	ix.effect1_depth = cpu_to_le16(ip->effect1_depth);
	ix.effect2 = ip->effect2;
	ix.effect2_depth = ip->effect2_depth;
	if (copy_to_user(instr_data, &ix, sizeof(ix)))
		return -EFAULT;
	instr_data += sizeof(ix);
	len -= sizeof(ix);
	for (lp = ip->layer; lp; lp = lp->next) {
		if (len < (long)sizeof(lx))
			return -ENOMEM;
		memset(&lx, 0, sizeof(lx));
		lx.stype = IWFFFF_STRU_LAYER;
		lx.flags = lp->flags;
		lx.velocity_mode = lp->velocity_mode;
		lx.layer_event = lp->layer_event;
		lx.low_range = lp->low_range;
		lx.high_range = lp->high_range;
		lx.pan = lp->pan;
		lx.pan_freq_scale = lp->pan_freq_scale;
		lx.attenuation = lp->attenuation;
		snd_seq_iwffff_copy_lfo_to_stream(&lx.tremolo, &lp->tremolo);
		snd_seq_iwffff_copy_lfo_to_stream(&lx.vibrato, &lp->vibrato);
		layer_instr_data = instr_data;
		instr_data += sizeof(lx);
		len -= sizeof(lx);
		err = snd_seq_iwffff_copy_env_to_stream(IWFFFF_STRU_ENV_RECP,
							lp,
							&lx.penv, &lp->penv,
						        &instr_data, &len);
		if (err < 0)
			return err;
		err = snd_seq_iwffff_copy_env_to_stream(IWFFFF_STRU_ENV_RECV,
							lp,
							&lx.venv, &lp->venv,
						        &instr_data, &len);
		if (err < 0)
			return err;
		/* layer structure updating is now finished */
		if (copy_to_user(layer_instr_data, &lx, sizeof(lx)))
			return -EFAULT;
		err = snd_seq_iwffff_copy_wave_to_stream(ops,
							 lp,
							 &instr_data,
							 &len,
							 atomic);
		if (err < 0)
			return err;
	}
	return 0;
}

static long snd_seq_iwffff_env_size_in_stream(struct iwffff_env *ep)
{
	long result = 0;
	struct iwffff_env_record *rp;

	for (rp = ep->record; rp; rp = rp->next) {
		result += sizeof(struct iwffff_xenv_record);
		result += (rp->nattack + rp->nrelease) * 2 * sizeof(__u16);
	}
	return 0;
}

static long snd_seq_iwffff_wave_size_in_stream(struct iwffff_layer *lp)
{
	long result = 0;
	struct iwffff_wave *wp;
	
	for (wp = lp->wave; wp; wp = wp->next) {
		result += sizeof(struct iwffff_xwave);
		if (!(wp->format & IWFFFF_WAVE_ROM))
			result += wp->size;
	}
	return result;
}

static int snd_seq_iwffff_get_size(void *private_data, struct snd_seq_kinstr *instr,
				   long *size)
{
	long result;
	struct iwffff_instrument *ip;
	struct iwffff_layer *lp;

	*size = 0;
	ip = (struct iwffff_instrument *)KINSTR_DATA(instr);
	result = sizeof(struct iwffff_xinstrument);
	for (lp = ip->layer; lp; lp = lp->next) {
		result += sizeof(struct iwffff_xlayer);
		result += snd_seq_iwffff_env_size_in_stream(&lp->penv);
		result += snd_seq_iwffff_env_size_in_stream(&lp->venv);
		result += snd_seq_iwffff_wave_size_in_stream(lp);
	}
	*size = result;
	return 0;
}

static int snd_seq_iwffff_remove(void *private_data,
				 struct snd_seq_kinstr *instr,
                                 int atomic)
{
	struct snd_iwffff_ops *ops = private_data;
	struct iwffff_instrument *ip;

	ip = (struct iwffff_instrument *)KINSTR_DATA(instr);
	snd_seq_iwffff_instr_free(ops, ip, atomic);
	return 0;
}

static void snd_seq_iwffff_notify(void *private_data,
				  struct snd_seq_kinstr *instr,
                                  int what)
{
	struct snd_iwffff_ops *ops = private_data;

	if (ops->notify)
		ops->notify(ops->private_data, instr, what);
}

int snd_seq_iwffff_init(struct snd_iwffff_ops *ops,
			void *private_data,
			struct snd_seq_kinstr_ops *next)
{
	memset(ops, 0, sizeof(*ops));
	ops->private_data = private_data;
	ops->kops.private_data = ops;
	ops->kops.add_len = sizeof(struct iwffff_instrument);
	ops->kops.instr_type = SNDRV_SEQ_INSTR_ID_INTERWAVE;
	ops->kops.put = snd_seq_iwffff_put;
	ops->kops.get = snd_seq_iwffff_get;
	ops->kops.get_size = snd_seq_iwffff_get_size;
	ops->kops.remove = snd_seq_iwffff_remove;
	ops->kops.notify = snd_seq_iwffff_notify;
	ops->kops.next = next;
	return 0;
}

/*
 *  Init part
 */

static int __init alsa_ainstr_iw_init(void)
{
	return 0;
}

static void __exit alsa_ainstr_iw_exit(void)
{
}

module_init(alsa_ainstr_iw_init)
module_exit(alsa_ainstr_iw_exit)

EXPORT_SYMBOL(snd_seq_iwffff_init);
