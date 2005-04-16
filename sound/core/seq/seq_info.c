/*
 *   ALSA sequencer /proc interface
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <sound/core.h>

#include "seq_info.h"
#include "seq_clientmgr.h"
#include "seq_timer.h"


static snd_info_entry_t *queues_entry;
static snd_info_entry_t *clients_entry;
static snd_info_entry_t *timer_entry;


static snd_info_entry_t * __init
create_info_entry(char *name, int size, void (*read)(snd_info_entry_t *, snd_info_buffer_t *))
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, name, snd_seq_root);
	if (entry == NULL)
		return NULL;
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	entry->c.text.read_size = size;
	entry->c.text.read = read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return NULL;
	}
	return entry;
}


/* create all our /proc entries */
int __init snd_seq_info_init(void)
{
	queues_entry = create_info_entry("queues", 512 + (256 * SNDRV_SEQ_MAX_QUEUES),
					 snd_seq_info_queues_read);
	clients_entry = create_info_entry("clients", 512 + (256 * SNDRV_SEQ_MAX_CLIENTS),
					  snd_seq_info_clients_read);
	timer_entry = create_info_entry("timer", 1024, snd_seq_info_timer_read);
	return 0;
}

int __exit snd_seq_info_done(void)
{
	if (queues_entry)
		snd_info_unregister(queues_entry);
	if (clients_entry)
		snd_info_unregister(clients_entry);
	if (timer_entry)
		snd_info_unregister(timer_entry);
	return 0;
}
