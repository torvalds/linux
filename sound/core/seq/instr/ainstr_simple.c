/*
 *   Simple (MOD player) - Instrument routines
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
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/ainstr_simple.h>
#include <sound/initval.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture Simple Instrument support.");
MODULE_LICENSE("GPL");

static unsigned int snd_seq_simple_size(unsigned int size, unsigned int format)
{
	unsigned int result = size;
	
	if (format & SIMPLE_WAVE_16BIT)
		result <<= 1;
	if (format & SIMPLE_WAVE_STEREO)
		result <<= 1;
	return result;
}

static void snd_seq_simple_instr_free(struct snd_simple_ops *ops,
				      struct simple_instrument *ip,
				      int atomic)
{
	if (ops->remove_sample)
		ops->remove_sample(ops->private_data, ip, atomic);
}

static int snd_seq_simple_put(void *private_data, struct snd_seq_kinstr *instr,
			      char __user *instr_data, long len,
			      int atomic, int cmd)
{
	struct snd_simple_ops *ops = private_data;
	struct simple_instrument *ip;
	struct simple_xinstrument ix;
	int err;
	gfp_t gfp_mask;
	unsigned int real_size;

	if (cmd != SNDRV_SEQ_INSTR_PUT_CMD_CREATE)
		return -EINVAL;
	gfp_mask = atomic ? GFP_ATOMIC : GFP_KERNEL;
	/* copy instrument data */
	if (len < (long)sizeof(ix))
		return -EINVAL;
	if (copy_from_user(&ix, instr_data, sizeof(ix)))
		return -EFAULT;
	if (ix.stype != SIMPLE_STRU_INSTR)
		return -EINVAL;
	instr_data += sizeof(ix);
	len -= sizeof(ix);
	ip = (struct simple_instrument *)KINSTR_DATA(instr);
	ip->share_id[0] = le32_to_cpu(ix.share_id[0]);
	ip->share_id[1] = le32_to_cpu(ix.share_id[1]);
	ip->share_id[2] = le32_to_cpu(ix.share_id[2]);
	ip->share_id[3] = le32_to_cpu(ix.share_id[3]);
	ip->format = le32_to_cpu(ix.format);
	ip->size = le32_to_cpu(ix.size);
	ip->start = le32_to_cpu(ix.start);
	ip->loop_start = le32_to_cpu(ix.loop_start);
	ip->loop_end = le32_to_cpu(ix.loop_end);
	ip->loop_repeat = le16_to_cpu(ix.loop_repeat);
	ip->effect1 = ix.effect1;
	ip->effect1_depth = ix.effect1_depth;
	ip->effect2 = ix.effect2;
	ip->effect2_depth = ix.effect2_depth;
	real_size = snd_seq_simple_size(ip->size, ip->format);
	if (len < (long)real_size)
		return -EINVAL;
	if (ops->put_sample) {
		err = ops->put_sample(ops->private_data, ip,
				      instr_data, real_size, atomic);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_seq_simple_get(void *private_data, struct snd_seq_kinstr *instr,
			      char __user *instr_data, long len,
			      int atomic, int cmd)
{
	struct snd_simple_ops *ops = private_data;
	struct simple_instrument *ip;
	struct simple_xinstrument ix;
	int err;
	unsigned int real_size;
	
	if (cmd != SNDRV_SEQ_INSTR_GET_CMD_FULL)
		return -EINVAL;
	if (len < (long)sizeof(ix))
		return -ENOMEM;
	memset(&ix, 0, sizeof(ix));
	ip = (struct simple_instrument *)KINSTR_DATA(instr);
	ix.stype = SIMPLE_STRU_INSTR;
	ix.share_id[0] = cpu_to_le32(ip->share_id[0]);
	ix.share_id[1] = cpu_to_le32(ip->share_id[1]);
	ix.share_id[2] = cpu_to_le32(ip->share_id[2]);
	ix.share_id[3] = cpu_to_le32(ip->share_id[3]);
	ix.format = cpu_to_le32(ip->format);
	ix.size = cpu_to_le32(ip->size);
	ix.start = cpu_to_le32(ip->start);
	ix.loop_start = cpu_to_le32(ip->loop_start);
	ix.loop_end = cpu_to_le32(ip->loop_end);
	ix.loop_repeat = cpu_to_le32(ip->loop_repeat);
	ix.effect1 = cpu_to_le16(ip->effect1);
	ix.effect1_depth = cpu_to_le16(ip->effect1_depth);
	ix.effect2 = ip->effect2;
	ix.effect2_depth = ip->effect2_depth;
	if (copy_to_user(instr_data, &ix, sizeof(ix)))
		return -EFAULT;
	instr_data += sizeof(ix);
	len -= sizeof(ix);
	real_size = snd_seq_simple_size(ip->size, ip->format);
	if (len < (long)real_size)
		return -ENOMEM;
	if (ops->get_sample) {
		err = ops->get_sample(ops->private_data, ip,
				      instr_data, real_size, atomic);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_seq_simple_get_size(void *private_data, struct snd_seq_kinstr *instr,
				   long *size)
{
	struct simple_instrument *ip;

	ip = (struct simple_instrument *)KINSTR_DATA(instr);
	*size = sizeof(struct simple_xinstrument) + snd_seq_simple_size(ip->size, ip->format);
	return 0;
}

static int snd_seq_simple_remove(void *private_data,
			         struct snd_seq_kinstr *instr,
                                 int atomic)
{
	struct snd_simple_ops *ops = private_data;
	struct simple_instrument *ip;

	ip = (struct simple_instrument *)KINSTR_DATA(instr);
	snd_seq_simple_instr_free(ops, ip, atomic);
	return 0;
}

static void snd_seq_simple_notify(void *private_data,
			          struct snd_seq_kinstr *instr,
                                  int what)
{
	struct snd_simple_ops *ops = private_data;

	if (ops->notify)
		ops->notify(ops->private_data, instr, what);
}

int snd_seq_simple_init(struct snd_simple_ops *ops,
		        void *private_data,
		        struct snd_seq_kinstr_ops *next)
{
	memset(ops, 0, sizeof(*ops));
	ops->private_data = private_data;
	ops->kops.private_data = ops;
	ops->kops.add_len = sizeof(struct simple_instrument);
	ops->kops.instr_type = SNDRV_SEQ_INSTR_ID_SIMPLE;
	ops->kops.put = snd_seq_simple_put;
	ops->kops.get = snd_seq_simple_get;
	ops->kops.get_size = snd_seq_simple_get_size;
	ops->kops.remove = snd_seq_simple_remove;
	ops->kops.notify = snd_seq_simple_notify;
	ops->kops.next = next;
	return 0;
}

/*
 *  Init part
 */

static int __init alsa_ainstr_simple_init(void)
{
	return 0;
}

static void __exit alsa_ainstr_simple_exit(void)
{
}

module_init(alsa_ainstr_simple_init)
module_exit(alsa_ainstr_simple_exit)

EXPORT_SYMBOL(snd_seq_simple_init);
