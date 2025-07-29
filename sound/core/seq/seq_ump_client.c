// SPDX-License-Identifier: GPL-2.0-or-later
/* ALSA sequencer binding for UMP device */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <sound/core.h>
#include <sound/ump.h>
#include <sound/seq_kernel.h>
#include <sound/seq_device.h>
#include "seq_clientmgr.h"
#include "seq_system.h"

struct seq_ump_client;
struct seq_ump_group;

enum {
	STR_IN = SNDRV_RAWMIDI_STREAM_INPUT,
	STR_OUT = SNDRV_RAWMIDI_STREAM_OUTPUT
};

/* context for UMP input parsing, per EP */
struct seq_ump_input_buffer {
	unsigned char len;		/* total length in words */
	unsigned char pending;		/* pending words */
	unsigned char type;		/* parsed UMP packet type */
	unsigned char group;		/* parsed UMP packet group */
	u32 buf[4];			/* incoming UMP packet */
};

/* sequencer client, per UMP EP (rawmidi) */
struct seq_ump_client {
	struct snd_ump_endpoint *ump;	/* assigned endpoint */
	int seq_client;			/* sequencer client id */
	int opened[2];			/* current opens for each direction */
	struct snd_rawmidi_file out_rfile; /* rawmidi for output */
	struct seq_ump_input_buffer input; /* input parser context */
	void *ump_info[SNDRV_UMP_MAX_BLOCKS + 1]; /* shadow of seq client ump_info */
	struct work_struct group_notify_work; /* FB change notification */
};

/* number of 32bit words for each UMP message type */
static unsigned char ump_packet_words[0x10] = {
	1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4
};

/* conversion between UMP group and seq port;
 * assume the port number is equal with UMP group number (1-based)
 */
static unsigned char ump_group_to_seq_port(unsigned char group)
{
	return group + 1;
}

/* process the incoming rawmidi stream */
static void seq_ump_input_receive(struct snd_ump_endpoint *ump,
				  const u32 *val, int words)
{
	struct seq_ump_client *client = ump->seq_client;
	struct snd_seq_ump_event ev = {};

	if (!client->opened[STR_IN])
		return;

	if (ump_is_groupless_msg(ump_message_type(*val)))
		ev.source.port = 0; /* UMP EP port */
	else
		ev.source.port = ump_group_to_seq_port(ump_message_group(*val));
	ev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
	ev.flags = SNDRV_SEQ_EVENT_UMP;
	memcpy(ev.ump, val, words << 2);
	snd_seq_kernel_client_dispatch(client->seq_client,
				       (struct snd_seq_event *)&ev,
				       true, 0);
}

/* process an input sequencer event; only deal with UMP types */
static int seq_ump_process_event(struct snd_seq_event *ev, int direct,
				 void *private_data, int atomic, int hop)
{
	struct seq_ump_client *client = private_data;
	struct snd_rawmidi_substream *substream;
	struct snd_seq_ump_event *ump_ev;
	unsigned char type;
	int len;

	substream = client->out_rfile.output;
	if (!substream)
		return -ENODEV;
	if (!snd_seq_ev_is_ump(ev))
		return 0; /* invalid event, skip */
	ump_ev = (struct snd_seq_ump_event *)ev;
	type = ump_message_type(ump_ev->ump[0]);
	len = ump_packet_words[type];
	if (len > 4)
		return 0; // invalid - skip
	snd_rawmidi_kernel_write(substream, ev->data.raw8.d, len << 2);
	return 0;
}

/* open the rawmidi */
static int seq_ump_client_open(struct seq_ump_client *client, int dir)
{
	struct snd_ump_endpoint *ump = client->ump;
	int err;

	guard(mutex)(&ump->open_mutex);
	if (dir == STR_OUT && !client->opened[dir]) {
		err = snd_rawmidi_kernel_open(&ump->core, 0,
					      SNDRV_RAWMIDI_LFLG_OUTPUT |
					      SNDRV_RAWMIDI_LFLG_APPEND,
					      &client->out_rfile);
		if (err < 0)
			return err;
	}
	client->opened[dir]++;
	return 0;
}

/* close the rawmidi */
static int seq_ump_client_close(struct seq_ump_client *client, int dir)
{
	struct snd_ump_endpoint *ump = client->ump;

	guard(mutex)(&ump->open_mutex);
	if (!--client->opened[dir])
		if (dir == STR_OUT)
			snd_rawmidi_kernel_release(&client->out_rfile);
	return 0;
}

/* sequencer subscription ops for each client */
static int seq_ump_subscribe(void *pdata, struct snd_seq_port_subscribe *info)
{
	struct seq_ump_client *client = pdata;

	return seq_ump_client_open(client, STR_IN);
}

static int seq_ump_unsubscribe(void *pdata, struct snd_seq_port_subscribe *info)
{
	struct seq_ump_client *client = pdata;

	return seq_ump_client_close(client, STR_IN);
}

static int seq_ump_use(void *pdata, struct snd_seq_port_subscribe *info)
{
	struct seq_ump_client *client = pdata;

	return seq_ump_client_open(client, STR_OUT);
}

static int seq_ump_unuse(void *pdata, struct snd_seq_port_subscribe *info)
{
	struct seq_ump_client *client = pdata;

	return seq_ump_client_close(client, STR_OUT);
}

/* fill port_info from the given UMP EP and group info */
static void fill_port_info(struct snd_seq_port_info *port,
			   struct seq_ump_client *client,
			   struct snd_ump_group *group)
{
	unsigned int rawmidi_info = client->ump->core.info_flags;

	port->addr.client = client->seq_client;
	port->addr.port = ump_group_to_seq_port(group->group);
	port->capability = 0;
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_OUTPUT)
		port->capability |= SNDRV_SEQ_PORT_CAP_WRITE |
			SNDRV_SEQ_PORT_CAP_SYNC_WRITE |
			SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_INPUT)
		port->capability |= SNDRV_SEQ_PORT_CAP_READ |
			SNDRV_SEQ_PORT_CAP_SYNC_READ |
			SNDRV_SEQ_PORT_CAP_SUBS_READ;
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_DUPLEX)
		port->capability |= SNDRV_SEQ_PORT_CAP_DUPLEX;
	if (group->dir_bits & (1 << STR_IN))
		port->direction |= SNDRV_SEQ_PORT_DIR_INPUT;
	if (group->dir_bits & (1 << STR_OUT))
		port->direction |= SNDRV_SEQ_PORT_DIR_OUTPUT;
	port->ump_group = group->group + 1;
	if (!group->active)
		port->capability |= SNDRV_SEQ_PORT_CAP_INACTIVE;
	if (group->is_midi1)
		port->flags |= SNDRV_SEQ_PORT_FLG_IS_MIDI1;
	port->type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
		SNDRV_SEQ_PORT_TYPE_MIDI_UMP |
		SNDRV_SEQ_PORT_TYPE_HARDWARE |
		SNDRV_SEQ_PORT_TYPE_PORT;
	port->midi_channels = 16;
	if (*group->name)
		snprintf(port->name, sizeof(port->name), "Group %d (%.53s)",
			 group->group + 1, group->name);
	else
		sprintf(port->name, "Group %d", group->group + 1);
}

/* skip non-existing group for static blocks */
static bool skip_group(struct seq_ump_client *client, struct snd_ump_group *group)
{
	return !group->valid &&
		(client->ump->info.flags & SNDRV_UMP_EP_INFO_STATIC_BLOCKS);
}

/* create a new sequencer port per UMP group */
static int seq_ump_group_init(struct seq_ump_client *client, int group_index)
{
	struct snd_ump_group *group = &client->ump->groups[group_index];
	struct snd_seq_port_info *port __free(kfree) = NULL;
	struct snd_seq_port_callback pcallbacks;

	if (skip_group(client, group))
		return 0;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	fill_port_info(port, client, group);
	port->flags |= SNDRV_SEQ_PORT_FLG_GIVEN_PORT;
	memset(&pcallbacks, 0, sizeof(pcallbacks));
	pcallbacks.owner = THIS_MODULE;
	pcallbacks.private_data = client;
	pcallbacks.subscribe = seq_ump_subscribe;
	pcallbacks.unsubscribe = seq_ump_unsubscribe;
	pcallbacks.use = seq_ump_use;
	pcallbacks.unuse = seq_ump_unuse;
	pcallbacks.event_input = seq_ump_process_event;
	port->kernel = &pcallbacks;
	return snd_seq_kernel_client_ctl(client->seq_client,
					 SNDRV_SEQ_IOCTL_CREATE_PORT,
					 port);
}

/* update the sequencer ports; called from notify_fb_change callback */
static void update_port_infos(struct seq_ump_client *client)
{
	struct snd_seq_port_info *old __free(kfree) = NULL;
	struct snd_seq_port_info *new __free(kfree) = NULL;
	int i, err;

	old = kzalloc(sizeof(*old), GFP_KERNEL);
	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!old || !new)
		return;

	for (i = 0; i < SNDRV_UMP_MAX_GROUPS; i++) {
		if (skip_group(client, &client->ump->groups[i]))
			continue;

		old->addr.client = client->seq_client;
		old->addr.port = ump_group_to_seq_port(i);
		err = snd_seq_kernel_client_ctl(client->seq_client,
						SNDRV_SEQ_IOCTL_GET_PORT_INFO,
						old);
		if (err < 0)
			continue;
		fill_port_info(new, client, &client->ump->groups[i]);
		if (old->capability == new->capability &&
		    !strcmp(old->name, new->name))
			continue;
		err = snd_seq_kernel_client_ctl(client->seq_client,
						SNDRV_SEQ_IOCTL_SET_PORT_INFO,
						new);
		if (err < 0)
			continue;
	}
}

/* create a UMP Endpoint port */
static int create_ump_endpoint_port(struct seq_ump_client *client)
{
	struct snd_seq_port_info *port __free(kfree) = NULL;
	struct snd_seq_port_callback pcallbacks;
	unsigned int rawmidi_info = client->ump->core.info_flags;
	int err;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->addr.client = client->seq_client;
	port->addr.port = 0; /* fixed */
	port->flags = SNDRV_SEQ_PORT_FLG_GIVEN_PORT;
	port->capability = SNDRV_SEQ_PORT_CAP_UMP_ENDPOINT;
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_INPUT) {
		port->capability |= SNDRV_SEQ_PORT_CAP_READ |
			SNDRV_SEQ_PORT_CAP_SYNC_READ |
			SNDRV_SEQ_PORT_CAP_SUBS_READ;
		port->direction |= SNDRV_SEQ_PORT_DIR_INPUT;
	}
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_OUTPUT) {
		port->capability |= SNDRV_SEQ_PORT_CAP_WRITE |
			SNDRV_SEQ_PORT_CAP_SYNC_WRITE |
			SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
		port->direction |= SNDRV_SEQ_PORT_DIR_OUTPUT;
	}
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_DUPLEX)
		port->capability |= SNDRV_SEQ_PORT_CAP_DUPLEX;
	port->ump_group = 0; /* no associated group, no conversion */
	port->type = SNDRV_SEQ_PORT_TYPE_MIDI_UMP |
		SNDRV_SEQ_PORT_TYPE_HARDWARE |
		SNDRV_SEQ_PORT_TYPE_PORT;
	port->midi_channels = 16;
	strscpy(port->name, "MIDI 2.0");
	memset(&pcallbacks, 0, sizeof(pcallbacks));
	pcallbacks.owner = THIS_MODULE;
	pcallbacks.private_data = client;
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_INPUT) {
		pcallbacks.subscribe = seq_ump_subscribe;
		pcallbacks.unsubscribe = seq_ump_unsubscribe;
	}
	if (rawmidi_info & SNDRV_RAWMIDI_INFO_OUTPUT) {
		pcallbacks.use = seq_ump_use;
		pcallbacks.unuse = seq_ump_unuse;
		pcallbacks.event_input = seq_ump_process_event;
	}
	port->kernel = &pcallbacks;
	err = snd_seq_kernel_client_ctl(client->seq_client,
					SNDRV_SEQ_IOCTL_CREATE_PORT,
					port);
	return err;
}

/* release the client resources */
static void seq_ump_client_free(struct seq_ump_client *client)
{
	cancel_work_sync(&client->group_notify_work);

	if (client->seq_client >= 0)
		snd_seq_delete_kernel_client(client->seq_client);

	client->ump->seq_ops = NULL;
	client->ump->seq_client = NULL;

	kfree(client);
}

/* update the MIDI version for the given client */
static void setup_client_midi_version(struct seq_ump_client *client)
{
	struct snd_seq_client *cptr;

	cptr = snd_seq_kernel_client_get(client->seq_client);
	if (!cptr)
		return;
	if (client->ump->info.protocol & SNDRV_UMP_EP_INFO_PROTO_MIDI2)
		cptr->midi_version = SNDRV_SEQ_CLIENT_UMP_MIDI_2_0;
	else
		cptr->midi_version = SNDRV_SEQ_CLIENT_UMP_MIDI_1_0;
	snd_seq_kernel_client_put(cptr);
}

/* set up client's group_filter bitmap */
static void setup_client_group_filter(struct seq_ump_client *client)
{
	struct snd_seq_client *cptr;
	unsigned int filter;
	int p;

	cptr = snd_seq_kernel_client_get(client->seq_client);
	if (!cptr)
		return;
	filter = ~(1U << 0); /* always allow groupless messages */
	for (p = 0; p < SNDRV_UMP_MAX_GROUPS; p++) {
		if (client->ump->groups[p].active)
			filter &= ~(1U << (p + 1));
	}
	cptr->group_filter = filter;
	snd_seq_kernel_client_put(cptr);
}

/* UMP group change notification */
static void handle_group_notify(struct work_struct *work)
{
	struct seq_ump_client *client =
		container_of(work, struct seq_ump_client, group_notify_work);

	update_port_infos(client);
	setup_client_group_filter(client);
}

/* UMP EP change notification */
static int seq_ump_notify_ep_change(struct snd_ump_endpoint *ump)
{
	struct seq_ump_client *client = ump->seq_client;
	struct snd_seq_client *cptr;
	int client_id;

	if (!client)
		return -ENODEV;
	client_id = client->seq_client;
	cptr = snd_seq_kernel_client_get(client_id);
	if (!cptr)
		return -ENODEV;

	snd_seq_system_ump_notify(client_id, 0, SNDRV_SEQ_EVENT_UMP_EP_CHANGE,
				  true);

	/* update sequencer client name if needed */
	if (*ump->core.name && strcmp(ump->core.name, cptr->name)) {
		strscpy(cptr->name, ump->core.name, sizeof(cptr->name));
		snd_seq_system_client_ev_client_change(client_id);
	}

	snd_seq_kernel_client_put(cptr);
	return 0;
}

/* UMP FB change notification */
static int seq_ump_notify_fb_change(struct snd_ump_endpoint *ump,
				    struct snd_ump_block *fb)
{
	struct seq_ump_client *client = ump->seq_client;

	if (!client)
		return -ENODEV;
	schedule_work(&client->group_notify_work);
	snd_seq_system_ump_notify(client->seq_client, fb->info.block_id,
				  SNDRV_SEQ_EVENT_UMP_BLOCK_CHANGE,
				  true);
	return 0;
}

/* UMP protocol change notification; just update the midi_version field */
static int seq_ump_switch_protocol(struct snd_ump_endpoint *ump)
{
	struct seq_ump_client *client = ump->seq_client;

	if (!client)
		return -ENODEV;
	setup_client_midi_version(client);
	snd_seq_system_ump_notify(client->seq_client, 0,
				  SNDRV_SEQ_EVENT_UMP_EP_CHANGE,
				  true);
	return 0;
}

static const struct snd_seq_ump_ops seq_ump_ops = {
	.input_receive = seq_ump_input_receive,
	.notify_ep_change = seq_ump_notify_ep_change,
	.notify_fb_change = seq_ump_notify_fb_change,
	.switch_protocol = seq_ump_switch_protocol,
};

/* create a sequencer client and ports for the given UMP endpoint */
static int snd_seq_ump_probe(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct snd_ump_endpoint *ump = dev->private_data;
	struct snd_card *card = dev->card;
	struct seq_ump_client *client;
	struct snd_ump_block *fb;
	struct snd_seq_client *cptr;
	int p, err;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	INIT_WORK(&client->group_notify_work, handle_group_notify);
	client->ump = ump;

	client->seq_client =
		snd_seq_create_kernel_client(card, ump->core.device,
					     ump->core.name);
	if (client->seq_client < 0) {
		err = client->seq_client;
		goto error;
	}

	client->ump_info[0] = &ump->info;
	list_for_each_entry(fb, &ump->block_list, list)
		client->ump_info[fb->info.block_id + 1] = &fb->info;

	setup_client_midi_version(client);

	for (p = 0; p < SNDRV_UMP_MAX_GROUPS; p++) {
		err = seq_ump_group_init(client, p);
		if (err < 0)
			goto error;
	}

	setup_client_group_filter(client);

	err = create_ump_endpoint_port(client);
	if (err < 0)
		goto error;

	cptr = snd_seq_kernel_client_get(client->seq_client);
	if (!cptr) {
		err = -EINVAL;
		goto error;
	}
	cptr->ump_info = client->ump_info;
	snd_seq_kernel_client_put(cptr);

	ump->seq_client = client;
	ump->seq_ops = &seq_ump_ops;
	return 0;

 error:
	seq_ump_client_free(client);
	return err;
}

/* remove a sequencer client */
static int snd_seq_ump_remove(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct snd_ump_endpoint *ump = dev->private_data;

	if (ump->seq_client)
		seq_ump_client_free(ump->seq_client);
	return 0;
}

static struct snd_seq_driver seq_ump_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.probe = snd_seq_ump_probe,
		.remove = snd_seq_ump_remove,
	},
	.id = SNDRV_SEQ_DEV_ID_UMP,
	.argsize = 0,
};

module_snd_seq_driver(seq_ump_driver);

MODULE_DESCRIPTION("ALSA sequencer client for UMP rawmidi");
MODULE_LICENSE("GPL");
