/*
 *  FM (OPL2/3) Instrument routines
 *  Copyright (c) 2000 Uros Bizjak <uros@kss-loka.si>
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
#include <sound/core.h>
#include <sound/ainstr_fm.h>
#include <sound/initval.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Uros Bizjak <uros@kss-loka.si>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture FM Instrument support.");
MODULE_LICENSE("GPL");

static int snd_seq_fm_put(void *private_data, struct snd_seq_kinstr *instr,
			  char __user *instr_data, long len, int atomic, int cmd)
{
	struct fm_instrument *ip;
	struct fm_xinstrument ix;
	int idx;

	if (cmd != SNDRV_SEQ_INSTR_PUT_CMD_CREATE)
		return -EINVAL;
	/* copy instrument data */
	if (len < (long)sizeof(ix))
		return -EINVAL;
	if (copy_from_user(&ix, instr_data, sizeof(ix)))
		return -EFAULT;
	if (ix.stype != FM_STRU_INSTR)
		return -EINVAL;
	ip = (struct fm_instrument *)KINSTR_DATA(instr);
	ip->share_id[0] = le32_to_cpu(ix.share_id[0]);
	ip->share_id[1] = le32_to_cpu(ix.share_id[1]);
	ip->share_id[2] = le32_to_cpu(ix.share_id[2]);
	ip->share_id[3] = le32_to_cpu(ix.share_id[3]);
	ip->type = ix.type;
	for (idx = 0; idx < 4; idx++) {
		ip->op[idx].am_vib = ix.op[idx].am_vib;
		ip->op[idx].ksl_level = ix.op[idx].ksl_level;
		ip->op[idx].attack_decay = ix.op[idx].attack_decay;
		ip->op[idx].sustain_release = ix.op[idx].sustain_release;
		ip->op[idx].wave_select = ix.op[idx].wave_select;
	}
	for (idx = 0; idx < 2; idx++) {
		ip->feedback_connection[idx] = ix.feedback_connection[idx];
	}
	ip->echo_delay = ix.echo_delay;
	ip->echo_atten = ix.echo_atten;
	ip->chorus_spread = ix.chorus_spread;
	ip->trnsps = ix.trnsps;
	ip->fix_dur = ix.fix_dur;
	ip->modes = ix.modes;
	ip->fix_key = ix.fix_key;
	return 0;
}

static int snd_seq_fm_get(void *private_data, struct snd_seq_kinstr *instr,
			  char __user *instr_data, long len, int atomic,
			  int cmd)
{
	struct fm_instrument *ip;
	struct fm_xinstrument ix;
	int idx;
	
	if (cmd != SNDRV_SEQ_INSTR_GET_CMD_FULL)
		return -EINVAL;
	if (len < (long)sizeof(ix))
		return -ENOMEM;
	memset(&ix, 0, sizeof(ix));
	ip = (struct fm_instrument *)KINSTR_DATA(instr);
	ix.stype = FM_STRU_INSTR;
	ix.share_id[0] = cpu_to_le32(ip->share_id[0]);
	ix.share_id[1] = cpu_to_le32(ip->share_id[1]);
	ix.share_id[2] = cpu_to_le32(ip->share_id[2]);
	ix.share_id[3] = cpu_to_le32(ip->share_id[3]);
	ix.type = ip->type;
	for (idx = 0; idx < 4; idx++) {
		ix.op[idx].am_vib = ip->op[idx].am_vib;
		ix.op[idx].ksl_level = ip->op[idx].ksl_level;
		ix.op[idx].attack_decay = ip->op[idx].attack_decay;
		ix.op[idx].sustain_release = ip->op[idx].sustain_release;
		ix.op[idx].wave_select = ip->op[idx].wave_select;
	}
	for (idx = 0; idx < 2; idx++) {
		ix.feedback_connection[idx] = ip->feedback_connection[idx];
	}
	if (copy_to_user(instr_data, &ix, sizeof(ix)))
		return -EFAULT;
	ix.echo_delay = ip->echo_delay;
	ix.echo_atten = ip->echo_atten;
	ix.chorus_spread = ip->chorus_spread;
	ix.trnsps = ip->trnsps;
	ix.fix_dur = ip->fix_dur;
	ix.modes = ip->modes;
	ix.fix_key = ip->fix_key;
	return 0;
}

static int snd_seq_fm_get_size(void *private_data, struct snd_seq_kinstr *instr,
			       long *size)
{
	*size = sizeof(struct fm_xinstrument);
	return 0;
}

int snd_seq_fm_init(struct snd_seq_kinstr_ops *ops,
		    struct snd_seq_kinstr_ops *next)
{
	memset(ops, 0, sizeof(*ops));
	// ops->private_data = private_data;
	ops->add_len = sizeof(struct fm_instrument);
	ops->instr_type = SNDRV_SEQ_INSTR_ID_OPL2_3;
	ops->put = snd_seq_fm_put;
	ops->get = snd_seq_fm_get;
	ops->get_size = snd_seq_fm_get_size;
	// ops->remove = snd_seq_fm_remove;
	// ops->notify = snd_seq_fm_notify;
	ops->next = next;
	return 0;
}

/*
 *  Init part
 */

static int __init alsa_ainstr_fm_init(void)
{
	return 0;
}

static void __exit alsa_ainstr_fm_exit(void)
{
}

module_init(alsa_ainstr_fm_init)
module_exit(alsa_ainstr_fm_exit)

EXPORT_SYMBOL(snd_seq_fm_init);
