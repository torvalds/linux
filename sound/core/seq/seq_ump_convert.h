// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ALSA sequencer event conversion between UMP and legacy clients
 */
#ifndef __SEQ_UMP_CONVERT_H
#define __SEQ_UMP_CONVERT_H

#include "seq_clientmgr.h"
#include "seq_ports.h"

int snd_seq_deliver_from_ump(struct snd_seq_client *source,
			     struct snd_seq_client *dest,
			     struct snd_seq_client_port *dest_port,
			     struct snd_seq_event *event,
			     int atomic, int hop);
int snd_seq_deliver_to_ump(struct snd_seq_client *source,
			   struct snd_seq_client *dest,
			   struct snd_seq_client_port *dest_port,
			   struct snd_seq_event *event,
			   int atomic, int hop);

#endif /* __SEQ_UMP_CONVERT_H */
