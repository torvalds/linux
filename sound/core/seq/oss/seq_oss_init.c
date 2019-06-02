// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OSS compatible sequencer driver
 *
 * open/close and reset interface
 *
 * Copyright (C) 1998-1999 Takashi Iwai <tiwai@suse.de>
 */

#include "seq_oss_device.h"
#include "seq_oss_synth.h"
#include "seq_oss_midi.h"
#include "seq_oss_writeq.h"
#include "seq_oss_readq.h"
#include "seq_oss_timer.h"
#include "seq_oss_event.h"
#include <linux/init.h>
#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/*
 * common variables
 */
static int maxqlen = SNDRV_SEQ_OSS_MAX_QLEN;
module_param(maxqlen, int, 0444);
MODULE_PARM_DESC(maxqlen, "maximum queue length");

static int system_client = -1; /* ALSA sequencer client number */
static int system_port = -1;

static int num_clients;
static struct seq_oss_devinfo *client_table[SNDRV_SEQ_OSS_MAX_CLIENTS];


/*
 * prototypes
 */
static int receive_announce(struct snd_seq_event *ev, int direct, void *private, int atomic, int hop);
static int translate_mode(struct file *file);
static int create_port(struct seq_oss_devinfo *dp);
static int delete_port(struct seq_oss_devinfo *dp);
static int alloc_seq_queue(struct seq_oss_devinfo *dp);
static int delete_seq_queue(int queue);
static void free_devinfo(void *private);

#define call_ctl(type,rec) snd_seq_kernel_client_ctl(system_client, type, rec)


/* call snd_seq_oss_midi_lookup_ports() asynchronously */
static void async_call_lookup_ports(struct work_struct *work)
{
	snd_seq_oss_midi_lookup_ports(system_client);
}

static DECLARE_WORK(async_lookup_work, async_call_lookup_ports);

/*
 * create sequencer client for OSS sequencer
 */
int __init
snd_seq_oss_create_client(void)
{
	int rc;
	struct snd_seq_port_info *port;
	struct snd_seq_port_callback port_callback;

	port = kmalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		rc = -ENOMEM;
		goto __error;
	}

	/* create ALSA client */
	rc = snd_seq_create_kernel_client(NULL, SNDRV_SEQ_CLIENT_OSS,
					  "OSS sequencer");
	if (rc < 0)
		goto __error;

	system_client = rc;

	/* create annoucement receiver port */
	memset(port, 0, sizeof(*port));
	strcpy(port->name, "Receiver");
	port->addr.client = system_client;
	port->capability = SNDRV_SEQ_PORT_CAP_WRITE; /* receive only */
	port->type = 0;

	memset(&port_callback, 0, sizeof(port_callback));
	/* don't set port_callback.owner here. otherwise the module counter
	 * is incremented and we can no longer release the module..
	 */
	port_callback.event_input = receive_announce;
	port->kernel = &port_callback;
	
	call_ctl(SNDRV_SEQ_IOCTL_CREATE_PORT, port);
	if ((system_port = port->addr.port) >= 0) {
		struct snd_seq_port_subscribe subs;

		memset(&subs, 0, sizeof(subs));
		subs.sender.client = SNDRV_SEQ_CLIENT_SYSTEM;
		subs.sender.port = SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE;
		subs.dest.client = system_client;
		subs.dest.port = system_port;
		call_ctl(SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT, &subs);
	}
	rc = 0;

	/* look up midi devices */
	schedule_work(&async_lookup_work);

 __error:
	kfree(port);
	return rc;
}


/*
 * receive annoucement from system port, and check the midi device
 */
static int
receive_announce(struct snd_seq_event *ev, int direct, void *private, int atomic, int hop)
{
	struct snd_seq_port_info pinfo;

	if (atomic)
		return 0; /* it must not happen */

	switch (ev->type) {
	case SNDRV_SEQ_EVENT_PORT_START:
	case SNDRV_SEQ_EVENT_PORT_CHANGE:
		if (ev->data.addr.client == system_client)
			break; /* ignore myself */
		memset(&pinfo, 0, sizeof(pinfo));
		pinfo.addr = ev->data.addr;
		if (call_ctl(SNDRV_SEQ_IOCTL_GET_PORT_INFO, &pinfo) >= 0)
			snd_seq_oss_midi_check_new_port(&pinfo);
		break;

	case SNDRV_SEQ_EVENT_PORT_EXIT:
		if (ev->data.addr.client == system_client)
			break; /* ignore myself */
		snd_seq_oss_midi_check_exit_port(ev->data.addr.client,
						ev->data.addr.port);
		break;
	}
	return 0;
}


/*
 * delete OSS sequencer client
 */
int
snd_seq_oss_delete_client(void)
{
	cancel_work_sync(&async_lookup_work);
	if (system_client >= 0)
		snd_seq_delete_kernel_client(system_client);

	snd_seq_oss_midi_clear_all();

	return 0;
}


/*
 * open sequencer device
 */
int
snd_seq_oss_open(struct file *file, int level)
{
	int i, rc;
	struct seq_oss_devinfo *dp;

	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->cseq = system_client;
	dp->port = -1;
	dp->queue = -1;

	for (i = 0; i < SNDRV_SEQ_OSS_MAX_CLIENTS; i++) {
		if (client_table[i] == NULL)
			break;
	}

	dp->index = i;
	if (i >= SNDRV_SEQ_OSS_MAX_CLIENTS) {
		pr_debug("ALSA: seq_oss: too many applications\n");
		rc = -ENOMEM;
		goto _error;
	}

	/* look up synth and midi devices */
	snd_seq_oss_synth_setup(dp);
	snd_seq_oss_midi_setup(dp);

	if (dp->synth_opened == 0 && dp->max_mididev == 0) {
		/* pr_err("ALSA: seq_oss: no device found\n"); */
		rc = -ENODEV;
		goto _error;
	}

	/* create port */
	rc = create_port(dp);
	if (rc < 0) {
		pr_err("ALSA: seq_oss: can't create port\n");
		goto _error;
	}

	/* allocate queue */
	rc = alloc_seq_queue(dp);
	if (rc < 0)
		goto _error;

	/* set address */
	dp->addr.client = dp->cseq;
	dp->addr.port = dp->port;
	/*dp->addr.queue = dp->queue;*/
	/*dp->addr.channel = 0;*/

	dp->seq_mode = level;

	/* set up file mode */
	dp->file_mode = translate_mode(file);

	/* initialize read queue */
	if (is_read_mode(dp->file_mode)) {
		dp->readq = snd_seq_oss_readq_new(dp, maxqlen);
		if (!dp->readq) {
			rc = -ENOMEM;
			goto _error;
		}
	}

	/* initialize write queue */
	if (is_write_mode(dp->file_mode)) {
		dp->writeq = snd_seq_oss_writeq_new(dp, maxqlen);
		if (!dp->writeq) {
			rc = -ENOMEM;
			goto _error;
		}
	}

	/* initialize timer */
	dp->timer = snd_seq_oss_timer_new(dp);
	if (!dp->timer) {
		pr_err("ALSA: seq_oss: can't alloc timer\n");
		rc = -ENOMEM;
		goto _error;
	}

	/* set private data pointer */
	file->private_data = dp;

	/* set up for mode2 */
	if (level == SNDRV_SEQ_OSS_MODE_MUSIC)
		snd_seq_oss_synth_setup_midi(dp);
	else if (is_read_mode(dp->file_mode))
		snd_seq_oss_midi_open_all(dp, SNDRV_SEQ_OSS_FILE_READ);

	client_table[dp->index] = dp;
	num_clients++;

	return 0;

 _error:
	snd_seq_oss_synth_cleanup(dp);
	snd_seq_oss_midi_cleanup(dp);
	delete_seq_queue(dp->queue);
	delete_port(dp);

	return rc;
}

/*
 * translate file flags to private mode
 */
static int
translate_mode(struct file *file)
{
	int file_mode = 0;
	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		file_mode |= SNDRV_SEQ_OSS_FILE_WRITE;
	if ((file->f_flags & O_ACCMODE) != O_WRONLY)
		file_mode |= SNDRV_SEQ_OSS_FILE_READ;
	if (file->f_flags & O_NONBLOCK)
		file_mode |= SNDRV_SEQ_OSS_FILE_NONBLOCK;
	return file_mode;
}


/*
 * create sequencer port
 */
static int
create_port(struct seq_oss_devinfo *dp)
{
	int rc;
	struct snd_seq_port_info port;
	struct snd_seq_port_callback callback;

	memset(&port, 0, sizeof(port));
	port.addr.client = dp->cseq;
	sprintf(port.name, "Sequencer-%d", dp->index);
	port.capability = SNDRV_SEQ_PORT_CAP_READ|SNDRV_SEQ_PORT_CAP_WRITE; /* no subscription */
	port.type = SNDRV_SEQ_PORT_TYPE_SPECIFIC;
	port.midi_channels = 128;
	port.synth_voices = 128;

	memset(&callback, 0, sizeof(callback));
	callback.owner = THIS_MODULE;
	callback.private_data = dp;
	callback.event_input = snd_seq_oss_event_input;
	callback.private_free = free_devinfo;
	port.kernel = &callback;

	rc = call_ctl(SNDRV_SEQ_IOCTL_CREATE_PORT, &port);
	if (rc < 0)
		return rc;

	dp->port = port.addr.port;

	return 0;
}

/*
 * delete ALSA port
 */
static int
delete_port(struct seq_oss_devinfo *dp)
{
	if (dp->port < 0) {
		kfree(dp);
		return 0;
	}

	return snd_seq_event_port_detach(dp->cseq, dp->port);
}

/*
 * allocate a queue
 */
static int
alloc_seq_queue(struct seq_oss_devinfo *dp)
{
	struct snd_seq_queue_info qinfo;
	int rc;

	memset(&qinfo, 0, sizeof(qinfo));
	qinfo.owner = system_client;
	qinfo.locked = 1;
	strcpy(qinfo.name, "OSS Sequencer Emulation");
	if ((rc = call_ctl(SNDRV_SEQ_IOCTL_CREATE_QUEUE, &qinfo)) < 0)
		return rc;
	dp->queue = qinfo.queue;
	return 0;
}

/*
 * release queue
 */
static int
delete_seq_queue(int queue)
{
	struct snd_seq_queue_info qinfo;
	int rc;

	if (queue < 0)
		return 0;
	memset(&qinfo, 0, sizeof(qinfo));
	qinfo.queue = queue;
	rc = call_ctl(SNDRV_SEQ_IOCTL_DELETE_QUEUE, &qinfo);
	if (rc < 0)
		pr_err("ALSA: seq_oss: unable to delete queue %d (%d)\n", queue, rc);
	return rc;
}


/*
 * free device informations - private_free callback of port
 */
static void
free_devinfo(void *private)
{
	struct seq_oss_devinfo *dp = (struct seq_oss_devinfo *)private;

	snd_seq_oss_timer_delete(dp->timer);
		
	snd_seq_oss_writeq_delete(dp->writeq);

	snd_seq_oss_readq_delete(dp->readq);
	
	kfree(dp);
}


/*
 * close sequencer device
 */
void
snd_seq_oss_release(struct seq_oss_devinfo *dp)
{
	int queue;

	client_table[dp->index] = NULL;
	num_clients--;

	snd_seq_oss_reset(dp);

	snd_seq_oss_synth_cleanup(dp);
	snd_seq_oss_midi_cleanup(dp);

	/* clear slot */
	queue = dp->queue;
	if (dp->port >= 0)
		delete_port(dp);
	delete_seq_queue(queue);
}


/*
 * reset sequencer devices
 */
void
snd_seq_oss_reset(struct seq_oss_devinfo *dp)
{
	int i;

	/* reset all synth devices */
	for (i = 0; i < dp->max_synthdev; i++)
		snd_seq_oss_synth_reset(dp, i);

	/* reset all midi devices */
	if (dp->seq_mode != SNDRV_SEQ_OSS_MODE_MUSIC) {
		for (i = 0; i < dp->max_mididev; i++)
			snd_seq_oss_midi_reset(dp, i);
	}

	/* remove queues */
	if (dp->readq)
		snd_seq_oss_readq_clear(dp->readq);
	if (dp->writeq)
		snd_seq_oss_writeq_clear(dp->writeq);

	/* reset timer */
	snd_seq_oss_timer_stop(dp->timer);
}

#ifdef CONFIG_SND_PROC_FS
/*
 * misc. functions for proc interface
 */
char *
enabled_str(int bool)
{
	return bool ? "enabled" : "disabled";
}

static char *
filemode_str(int val)
{
	static char *str[] = {
		"none", "read", "write", "read/write",
	};
	return str[val & SNDRV_SEQ_OSS_FILE_ACMODE];
}


/*
 * proc interface
 */
void
snd_seq_oss_system_info_read(struct snd_info_buffer *buf)
{
	int i;
	struct seq_oss_devinfo *dp;

	snd_iprintf(buf, "ALSA client number %d\n", system_client);
	snd_iprintf(buf, "ALSA receiver port %d\n", system_port);

	snd_iprintf(buf, "\nNumber of applications: %d\n", num_clients);
	for (i = 0; i < num_clients; i++) {
		snd_iprintf(buf, "\nApplication %d: ", i);
		if ((dp = client_table[i]) == NULL) {
			snd_iprintf(buf, "*empty*\n");
			continue;
		}
		snd_iprintf(buf, "port %d : queue %d\n", dp->port, dp->queue);
		snd_iprintf(buf, "  sequencer mode = %s : file open mode = %s\n",
			    (dp->seq_mode ? "music" : "synth"),
			    filemode_str(dp->file_mode));
		if (dp->seq_mode)
			snd_iprintf(buf, "  timer tempo = %d, timebase = %d\n",
				    dp->timer->oss_tempo, dp->timer->oss_timebase);
		snd_iprintf(buf, "  max queue length %d\n", maxqlen);
		if (is_read_mode(dp->file_mode) && dp->readq)
			snd_seq_oss_readq_info_read(dp->readq, buf);
	}
}
#endif /* CONFIG_SND_PROC_FS */
