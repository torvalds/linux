/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ALSA sequencer System Client
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 */
#ifndef __SND_SEQ_SYSTEM_H
#define __SND_SEQ_SYSTEM_H

#include <sound/seq_kernel.h>


/* entry points for broadcasting system events */
void snd_seq_system_broadcast(int client, int port, int type, bool atomic);

/* normal system notification event broadcast */
#define notify_event(client, port, type) \
	snd_seq_system_broadcast(client, port, type, false)

#define snd_seq_system_client_ev_client_start(client) notify_event(client, 0, SNDRV_SEQ_EVENT_CLIENT_START)
#define snd_seq_system_client_ev_client_exit(client) notify_event(client, 0, SNDRV_SEQ_EVENT_CLIENT_EXIT)
#define snd_seq_system_client_ev_client_change(client) notify_event(client, 0, SNDRV_SEQ_EVENT_CLIENT_CHANGE)
#define snd_seq_system_client_ev_port_start(client, port) notify_event(client, port, SNDRV_SEQ_EVENT_PORT_START)
#define snd_seq_system_client_ev_port_exit(client, port) notify_event(client, port, SNDRV_SEQ_EVENT_PORT_EXIT)
#define snd_seq_system_client_ev_port_change(client, port) notify_event(client, port, SNDRV_SEQ_EVENT_PORT_CHANGE)

int snd_seq_system_notify(int client, int port, struct snd_seq_event *ev,
			  bool atomic);

/* register our internal client */
int snd_seq_system_client_init(void);

/* unregister our internal client */
void snd_seq_system_client_done(void);


#endif
