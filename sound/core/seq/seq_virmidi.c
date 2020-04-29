// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Virtual Raw MIDI client on Sequencer
 *
 *  Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>,
 *                        Jaroslav Kysela <perex@perex.cz>
 */

/*
 * Virtual Raw MIDI client
 *
 * The virtual rawmidi client is a sequencer client which associate
 * a rawmidi device file.  The created rawmidi device file can be
 * accessed as a normal raw midi, but its MIDI source and destination
 * are arbitrary.  For example, a user-client software synth connected
 * to this port can be used as a normal midi device as well.
 *
 * The virtual rawmidi device accepts also multiple opens.  Each file
 * has its own input buffer, so that no conflict would occur.  The drain
 * of input/output buffer acts only to the local buffer.
 *
 */

#include <linux/init.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/minors.h>
#include <sound/seq_kernel.h>
#include <sound/seq_midi_event.h>
#include <sound/seq_virmidi.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Virtual Raw MIDI client on Sequencer");
MODULE_LICENSE("GPL");

/*
 * initialize an event record
 */
static void snd_virmidi_init_event(struct snd_virmidi *vmidi,
				   struct snd_seq_event *ev)
{
	memset(ev, 0, sizeof(*ev));
	ev->source.port = vmidi->port;
	switch (vmidi->seq_mode) {
	case SNDRV_VIRMIDI_SEQ_DISPATCH:
		ev->dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
		break;
	case SNDRV_VIRMIDI_SEQ_ATTACH:
		/* FIXME: source and destination are same - not good.. */
		ev->dest.client = vmidi->client;
		ev->dest.port = vmidi->port;
		break;
	}
	ev->type = SNDRV_SEQ_EVENT_NONE;
}

/*
 * decode input event and put to read buffer of each opened file
 */
static int snd_virmidi_dev_receive_event(struct snd_virmidi_dev *rdev,
					 struct snd_seq_event *ev,
					 bool atomic)
{
	struct snd_virmidi *vmidi;
	unsigned char msg[4];
	int len;

	if (atomic)
		read_lock(&rdev->filelist_lock);
	else
		down_read(&rdev->filelist_sem);
	list_for_each_entry(vmidi, &rdev->filelist, list) {
		if (!READ_ONCE(vmidi->trigger))
			continue;
		if (ev->type == SNDRV_SEQ_EVENT_SYSEX) {
			if ((ev->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARIABLE)
				continue;
			snd_seq_dump_var_event(ev, (snd_seq_dump_func_t)snd_rawmidi_receive, vmidi->substream);
			snd_midi_event_reset_decode(vmidi->parser);
		} else {
			len = snd_midi_event_decode(vmidi->parser, msg, sizeof(msg), ev);
			if (len > 0)
				snd_rawmidi_receive(vmidi->substream, msg, len);
		}
	}
	if (atomic)
		read_unlock(&rdev->filelist_lock);
	else
		up_read(&rdev->filelist_sem);

	return 0;
}

/*
 * event handler of virmidi port
 */
static int snd_virmidi_event_input(struct snd_seq_event *ev, int direct,
				   void *private_data, int atomic, int hop)
{
	struct snd_virmidi_dev *rdev;

	rdev = private_data;
	if (!(rdev->flags & SNDRV_VIRMIDI_USE))
		return 0; /* ignored */
	return snd_virmidi_dev_receive_event(rdev, ev, atomic);
}

/*
 * trigger rawmidi stream for input
 */
static void snd_virmidi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_virmidi *vmidi = substream->runtime->private_data;

	WRITE_ONCE(vmidi->trigger, !!up);
}

/* process rawmidi bytes and send events;
 * we need no lock here for vmidi->event since it's handled only in this work
 */
static void snd_vmidi_output_work(struct work_struct *work)
{
	struct snd_virmidi *vmidi;
	struct snd_rawmidi_substream *substream;
	unsigned char input;
	int ret;

	vmidi = container_of(work, struct snd_virmidi, output_work);
	substream = vmidi->substream;

	/* discard the outputs in dispatch mode unless subscribed */
	if (vmidi->seq_mode == SNDRV_VIRMIDI_SEQ_DISPATCH &&
	    !(vmidi->rdev->flags & SNDRV_VIRMIDI_SUBSCRIBE)) {
		snd_rawmidi_proceed(substream);
		return;
	}

	while (READ_ONCE(vmidi->trigger)) {
		if (snd_rawmidi_transmit(substream, &input, 1) != 1)
			break;
		if (!snd_midi_event_encode_byte(vmidi->parser, input,
						&vmidi->event))
			continue;
		if (vmidi->event.type != SNDRV_SEQ_EVENT_NONE) {
			ret = snd_seq_kernel_client_dispatch(vmidi->client,
							     &vmidi->event,
							     false, 0);
			vmidi->event.type = SNDRV_SEQ_EVENT_NONE;
			if (ret < 0)
				break;
		}
		/* rawmidi input might be huge, allow to have a break */
		cond_resched();
	}
}

/*
 * trigger rawmidi stream for output
 */
static void snd_virmidi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_virmidi *vmidi = substream->runtime->private_data;

	WRITE_ONCE(vmidi->trigger, !!up);
	if (up)
		queue_work(system_highpri_wq, &vmidi->output_work);
}

/*
 * open rawmidi handle for input
 */
static int snd_virmidi_input_open(struct snd_rawmidi_substream *substream)
{
	struct snd_virmidi_dev *rdev = substream->rmidi->private_data;
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	struct snd_virmidi *vmidi;

	vmidi = kzalloc(sizeof(*vmidi), GFP_KERNEL);
	if (vmidi == NULL)
		return -ENOMEM;
	vmidi->substream = substream;
	if (snd_midi_event_new(0, &vmidi->parser) < 0) {
		kfree(vmidi);
		return -ENOMEM;
	}
	vmidi->seq_mode = rdev->seq_mode;
	vmidi->client = rdev->client;
	vmidi->port = rdev->port;	
	runtime->private_data = vmidi;
	down_write(&rdev->filelist_sem);
	write_lock_irq(&rdev->filelist_lock);
	list_add_tail(&vmidi->list, &rdev->filelist);
	write_unlock_irq(&rdev->filelist_lock);
	up_write(&rdev->filelist_sem);
	vmidi->rdev = rdev;
	return 0;
}

/*
 * open rawmidi handle for output
 */
static int snd_virmidi_output_open(struct snd_rawmidi_substream *substream)
{
	struct snd_virmidi_dev *rdev = substream->rmidi->private_data;
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	struct snd_virmidi *vmidi;

	vmidi = kzalloc(sizeof(*vmidi), GFP_KERNEL);
	if (vmidi == NULL)
		return -ENOMEM;
	vmidi->substream = substream;
	if (snd_midi_event_new(MAX_MIDI_EVENT_BUF, &vmidi->parser) < 0) {
		kfree(vmidi);
		return -ENOMEM;
	}
	vmidi->seq_mode = rdev->seq_mode;
	vmidi->client = rdev->client;
	vmidi->port = rdev->port;
	snd_virmidi_init_event(vmidi, &vmidi->event);
	vmidi->rdev = rdev;
	INIT_WORK(&vmidi->output_work, snd_vmidi_output_work);
	runtime->private_data = vmidi;
	return 0;
}

/*
 * close rawmidi handle for input
 */
static int snd_virmidi_input_close(struct snd_rawmidi_substream *substream)
{
	struct snd_virmidi_dev *rdev = substream->rmidi->private_data;
	struct snd_virmidi *vmidi = substream->runtime->private_data;

	down_write(&rdev->filelist_sem);
	write_lock_irq(&rdev->filelist_lock);
	list_del(&vmidi->list);
	write_unlock_irq(&rdev->filelist_lock);
	up_write(&rdev->filelist_sem);
	snd_midi_event_free(vmidi->parser);
	substream->runtime->private_data = NULL;
	kfree(vmidi);
	return 0;
}

/*
 * close rawmidi handle for output
 */
static int snd_virmidi_output_close(struct snd_rawmidi_substream *substream)
{
	struct snd_virmidi *vmidi = substream->runtime->private_data;

	WRITE_ONCE(vmidi->trigger, false); /* to be sure */
	cancel_work_sync(&vmidi->output_work);
	snd_midi_event_free(vmidi->parser);
	substream->runtime->private_data = NULL;
	kfree(vmidi);
	return 0;
}

/*
 * subscribe callback - allow output to rawmidi device
 */
static int snd_virmidi_subscribe(void *private_data,
				 struct snd_seq_port_subscribe *info)
{
	struct snd_virmidi_dev *rdev;

	rdev = private_data;
	if (!try_module_get(rdev->card->module))
		return -EFAULT;
	rdev->flags |= SNDRV_VIRMIDI_SUBSCRIBE;
	return 0;
}

/*
 * unsubscribe callback - disallow output to rawmidi device
 */
static int snd_virmidi_unsubscribe(void *private_data,
				   struct snd_seq_port_subscribe *info)
{
	struct snd_virmidi_dev *rdev;

	rdev = private_data;
	rdev->flags &= ~SNDRV_VIRMIDI_SUBSCRIBE;
	module_put(rdev->card->module);
	return 0;
}


/*
 * use callback - allow input to rawmidi device
 */
static int snd_virmidi_use(void *private_data,
			   struct snd_seq_port_subscribe *info)
{
	struct snd_virmidi_dev *rdev;

	rdev = private_data;
	if (!try_module_get(rdev->card->module))
		return -EFAULT;
	rdev->flags |= SNDRV_VIRMIDI_USE;
	return 0;
}

/*
 * unuse callback - disallow input to rawmidi device
 */
static int snd_virmidi_unuse(void *private_data,
			     struct snd_seq_port_subscribe *info)
{
	struct snd_virmidi_dev *rdev;

	rdev = private_data;
	rdev->flags &= ~SNDRV_VIRMIDI_USE;
	module_put(rdev->card->module);
	return 0;
}


/*
 *  Register functions
 */

static const struct snd_rawmidi_ops snd_virmidi_input_ops = {
	.open = snd_virmidi_input_open,
	.close = snd_virmidi_input_close,
	.trigger = snd_virmidi_input_trigger,
};

static const struct snd_rawmidi_ops snd_virmidi_output_ops = {
	.open = snd_virmidi_output_open,
	.close = snd_virmidi_output_close,
	.trigger = snd_virmidi_output_trigger,
};

/*
 * create a sequencer client and a port
 */
static int snd_virmidi_dev_attach_seq(struct snd_virmidi_dev *rdev)
{
	int client;
	struct snd_seq_port_callback pcallbacks;
	struct snd_seq_port_info *pinfo;
	int err;

	if (rdev->client >= 0)
		return 0;

	pinfo = kzalloc(sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo) {
		err = -ENOMEM;
		goto __error;
	}

	client = snd_seq_create_kernel_client(rdev->card, rdev->device,
					      "%s %d-%d", rdev->rmidi->name,
					      rdev->card->number,
					      rdev->device);
	if (client < 0) {
		err = client;
		goto __error;
	}
	rdev->client = client;

	/* create a port */
	pinfo->addr.client = client;
	sprintf(pinfo->name, "VirMIDI %d-%d", rdev->card->number, rdev->device);
	/* set all capabilities */
	pinfo->capability |= SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SYNC_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
	pinfo->capability |= SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_SYNC_READ | SNDRV_SEQ_PORT_CAP_SUBS_READ;
	pinfo->capability |= SNDRV_SEQ_PORT_CAP_DUPLEX;
	pinfo->type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC
		| SNDRV_SEQ_PORT_TYPE_SOFTWARE
		| SNDRV_SEQ_PORT_TYPE_PORT;
	pinfo->midi_channels = 16;
	memset(&pcallbacks, 0, sizeof(pcallbacks));
	pcallbacks.owner = THIS_MODULE;
	pcallbacks.private_data = rdev;
	pcallbacks.subscribe = snd_virmidi_subscribe;
	pcallbacks.unsubscribe = snd_virmidi_unsubscribe;
	pcallbacks.use = snd_virmidi_use;
	pcallbacks.unuse = snd_virmidi_unuse;
	pcallbacks.event_input = snd_virmidi_event_input;
	pinfo->kernel = &pcallbacks;
	err = snd_seq_kernel_client_ctl(client, SNDRV_SEQ_IOCTL_CREATE_PORT, pinfo);
	if (err < 0) {
		snd_seq_delete_kernel_client(client);
		rdev->client = -1;
		goto __error;
	}

	rdev->port = pinfo->addr.port;
	err = 0; /* success */

 __error:
	kfree(pinfo);
	return err;
}


/*
 * release the sequencer client
 */
static void snd_virmidi_dev_detach_seq(struct snd_virmidi_dev *rdev)
{
	if (rdev->client >= 0) {
		snd_seq_delete_kernel_client(rdev->client);
		rdev->client = -1;
	}
}

/*
 * register the device
 */
static int snd_virmidi_dev_register(struct snd_rawmidi *rmidi)
{
	struct snd_virmidi_dev *rdev = rmidi->private_data;
	int err;

	switch (rdev->seq_mode) {
	case SNDRV_VIRMIDI_SEQ_DISPATCH:
		err = snd_virmidi_dev_attach_seq(rdev);
		if (err < 0)
			return err;
		break;
	case SNDRV_VIRMIDI_SEQ_ATTACH:
		if (rdev->client == 0)
			return -EINVAL;
		/* should check presence of port more strictly.. */
		break;
	default:
		pr_err("ALSA: seq_virmidi: seq_mode is not set: %d\n", rdev->seq_mode);
		return -EINVAL;
	}
	return 0;
}


/*
 * unregister the device
 */
static int snd_virmidi_dev_unregister(struct snd_rawmidi *rmidi)
{
	struct snd_virmidi_dev *rdev = rmidi->private_data;

	if (rdev->seq_mode == SNDRV_VIRMIDI_SEQ_DISPATCH)
		snd_virmidi_dev_detach_seq(rdev);
	return 0;
}

/*
 *
 */
static const struct snd_rawmidi_global_ops snd_virmidi_global_ops = {
	.dev_register = snd_virmidi_dev_register,
	.dev_unregister = snd_virmidi_dev_unregister,
};

/*
 * free device
 */
static void snd_virmidi_free(struct snd_rawmidi *rmidi)
{
	struct snd_virmidi_dev *rdev = rmidi->private_data;
	kfree(rdev);
}

/*
 * create a new device
 *
 */
/* exported */
int snd_virmidi_new(struct snd_card *card, int device, struct snd_rawmidi **rrmidi)
{
	struct snd_rawmidi *rmidi;
	struct snd_virmidi_dev *rdev;
	int err;
	
	*rrmidi = NULL;
	if ((err = snd_rawmidi_new(card, "VirMidi", device,
				   16,	/* may be configurable */
				   16,	/* may be configurable */
				   &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, rmidi->id);
	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (rdev == NULL) {
		snd_device_free(card, rmidi);
		return -ENOMEM;
	}
	rdev->card = card;
	rdev->rmidi = rmidi;
	rdev->device = device;
	rdev->client = -1;
	init_rwsem(&rdev->filelist_sem);
	rwlock_init(&rdev->filelist_lock);
	INIT_LIST_HEAD(&rdev->filelist);
	rdev->seq_mode = SNDRV_VIRMIDI_SEQ_DISPATCH;
	rmidi->private_data = rdev;
	rmidi->private_free = snd_virmidi_free;
	rmidi->ops = &snd_virmidi_global_ops;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_virmidi_input_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_virmidi_output_ops);
	rmidi->info_flags = SNDRV_RAWMIDI_INFO_INPUT |
			    SNDRV_RAWMIDI_INFO_OUTPUT |
			    SNDRV_RAWMIDI_INFO_DUPLEX;
	*rrmidi = rmidi;
	return 0;
}
EXPORT_SYMBOL(snd_virmidi_new);
