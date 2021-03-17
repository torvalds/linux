// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA sequencer /proc interface
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <sound/core.h>

#include "seq_info.h"
#include "seq_clientmgr.h"
#include "seq_timer.h"

static struct snd_info_entry *queues_entry;
static struct snd_info_entry *clients_entry;
static struct snd_info_entry *timer_entry;


static struct snd_info_entry * __init
create_info_entry(char *name, void (*read)(struct snd_info_entry *,
					   struct snd_info_buffer *))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, name, snd_seq_root);
	if (entry == NULL)
		return NULL;
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	entry->c.text.read = read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return NULL;
	}
	return entry;
}

void snd_seq_info_done(void)
{
	snd_info_free_entry(queues_entry);
	snd_info_free_entry(clients_entry);
	snd_info_free_entry(timer_entry);
}

/* create all our /proc entries */
int __init snd_seq_info_init(void)
{
	queues_entry = create_info_entry("queues",
					 snd_seq_info_queues_read);
	clients_entry = create_info_entry("clients",
					  snd_seq_info_clients_read);
	timer_entry = create_info_entry("timer", snd_seq_info_timer_read);
	if (!queues_entry || !clients_entry || !timer_entry)
		goto error;
	return 0;

 error:
	snd_seq_info_done();
	return -ENOMEM;
}
