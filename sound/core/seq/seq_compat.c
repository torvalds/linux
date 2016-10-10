/*
 *   32bit -> 64bit ioctl wrapper for sequencer API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
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

/* This file included from seq.c */

#include <linux/compat.h>
#include <linux/slab.h>

struct snd_seq_port_info32 {
	struct snd_seq_addr addr;	/* client/port numbers */
	char name[64];			/* port name */

	u32 capability;	/* port capability bits */
	u32 type;		/* port type bits */
	s32 midi_channels;		/* channels per MIDI port */
	s32 midi_voices;		/* voices per MIDI port */
	s32 synth_voices;		/* voices per SYNTH port */

	s32 read_use;			/* R/O: subscribers for output (from this port) */
	s32 write_use;			/* R/O: subscribers for input (to this port) */

	u32 kernel;			/* reserved for kernel use (must be NULL) */
	u32 flags;		/* misc. conditioning */
	unsigned char time_queue;	/* queue # for timestamping */
	char reserved[59];		/* for future use */
};

static int snd_seq_call_port_info_ioctl(struct snd_seq_client *client, unsigned int cmd,
					struct snd_seq_port_info32 __user *data32)
{
	int err = -EFAULT;
	struct snd_seq_port_info *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (copy_from_user(data, data32, sizeof(*data32)) ||
	    get_user(data->flags, &data32->flags) ||
	    get_user(data->time_queue, &data32->time_queue))
		goto error;
	data->kernel = NULL;

	err = snd_seq_kernel_client_ctl(client->number, cmd, &data);
	if (err < 0)
		goto error;

	if (copy_to_user(data32, data, sizeof(*data32)) ||
	    put_user(data->flags, &data32->flags) ||
	    put_user(data->time_queue, &data32->time_queue))
		err = -EFAULT;

 error:
	kfree(data);
	return err;
}



/*
 */

enum {
	SNDRV_SEQ_IOCTL_CREATE_PORT32 = _IOWR('S', 0x20, struct snd_seq_port_info32),
	SNDRV_SEQ_IOCTL_DELETE_PORT32 = _IOW ('S', 0x21, struct snd_seq_port_info32),
	SNDRV_SEQ_IOCTL_GET_PORT_INFO32 = _IOWR('S', 0x22, struct snd_seq_port_info32),
	SNDRV_SEQ_IOCTL_SET_PORT_INFO32 = _IOW ('S', 0x23, struct snd_seq_port_info32),
	SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT32 = _IOWR('S', 0x52, struct snd_seq_port_info32),
};

static long snd_seq_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_seq_client *client = file->private_data;
	void __user *argp = compat_ptr(arg);

	if (snd_BUG_ON(!client))
		return -ENXIO;

	switch (cmd) {
	case SNDRV_SEQ_IOCTL_PVERSION:
	case SNDRV_SEQ_IOCTL_CLIENT_ID:
	case SNDRV_SEQ_IOCTL_SYSTEM_INFO:
	case SNDRV_SEQ_IOCTL_GET_CLIENT_INFO:
	case SNDRV_SEQ_IOCTL_SET_CLIENT_INFO:
	case SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT:
	case SNDRV_SEQ_IOCTL_UNSUBSCRIBE_PORT:
	case SNDRV_SEQ_IOCTL_CREATE_QUEUE:
	case SNDRV_SEQ_IOCTL_DELETE_QUEUE:
	case SNDRV_SEQ_IOCTL_GET_QUEUE_INFO:
	case SNDRV_SEQ_IOCTL_SET_QUEUE_INFO:
	case SNDRV_SEQ_IOCTL_GET_NAMED_QUEUE:
	case SNDRV_SEQ_IOCTL_GET_QUEUE_STATUS:
	case SNDRV_SEQ_IOCTL_GET_QUEUE_TEMPO:
	case SNDRV_SEQ_IOCTL_SET_QUEUE_TEMPO:
	case SNDRV_SEQ_IOCTL_GET_QUEUE_TIMER:
	case SNDRV_SEQ_IOCTL_SET_QUEUE_TIMER:
	case SNDRV_SEQ_IOCTL_GET_QUEUE_CLIENT:
	case SNDRV_SEQ_IOCTL_SET_QUEUE_CLIENT:
	case SNDRV_SEQ_IOCTL_GET_CLIENT_POOL:
	case SNDRV_SEQ_IOCTL_SET_CLIENT_POOL:
	case SNDRV_SEQ_IOCTL_REMOVE_EVENTS:
	case SNDRV_SEQ_IOCTL_QUERY_SUBS:
	case SNDRV_SEQ_IOCTL_GET_SUBSCRIPTION:
	case SNDRV_SEQ_IOCTL_QUERY_NEXT_CLIENT:
	case SNDRV_SEQ_IOCTL_RUNNING_MODE:
		return snd_seq_ioctl(file, cmd, arg);
	case SNDRV_SEQ_IOCTL_CREATE_PORT32:
		return snd_seq_call_port_info_ioctl(client, SNDRV_SEQ_IOCTL_CREATE_PORT, argp);
	case SNDRV_SEQ_IOCTL_DELETE_PORT32:
		return snd_seq_call_port_info_ioctl(client, SNDRV_SEQ_IOCTL_DELETE_PORT, argp);
	case SNDRV_SEQ_IOCTL_GET_PORT_INFO32:
		return snd_seq_call_port_info_ioctl(client, SNDRV_SEQ_IOCTL_GET_PORT_INFO, argp);
	case SNDRV_SEQ_IOCTL_SET_PORT_INFO32:
		return snd_seq_call_port_info_ioctl(client, SNDRV_SEQ_IOCTL_SET_PORT_INFO, argp);
	case SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT32:
		return snd_seq_call_port_info_ioctl(client, SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT, argp);
	}
	return -ENOIOCTLCMD;
}
