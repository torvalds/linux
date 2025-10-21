// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"
#include <linux/seq_file.h>

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_legacy_debug_proc_show(struct smbdirect_socket *sc,
							unsigned int rdma_readwrite_threshold,
							struct seq_file *m)
{
	const struct smbdirect_socket_parameters *sp;

	if (!sc)
		return;
	sp = &sc->parameters;

	seq_puts(m, "\n");
	seq_printf(m, "SMBDirect protocol version: 0x%x ",
		   SMBDIRECT_V1);
	seq_printf(m, "transport status: %s (%u)",
		   smbdirect_socket_status_string(sc->status),
		   sc->status);

	seq_puts(m, "\n");
	seq_printf(m, "Conn receive_credit_max: %u ",
		   sp->recv_credit_max);
	seq_printf(m, "send_credit_target: %u max_send_size: %u",
		   sp->send_credit_target,
		   sp->max_send_size);

	seq_puts(m, "\n");
	seq_printf(m, "Conn max_fragmented_recv_size: %u ",
		   sp->max_fragmented_recv_size);
	seq_printf(m, "max_fragmented_send_size: %u max_receive_size:%u",
		   sp->max_fragmented_send_size,
		   sp->max_recv_size);

	seq_puts(m, "\n");
	seq_printf(m, "Conn keep_alive_interval: %u ",
		   sp->keepalive_interval_msec * 1000);
	seq_printf(m, "max_readwrite_size: %u rdma_readwrite_threshold: %u",
		   sp->max_read_write_size,
		   rdma_readwrite_threshold);

	seq_puts(m, "\n");
	seq_printf(m, "Debug count_get_receive_buffer: %llu ",
		   sc->statistics.get_receive_buffer);
	seq_printf(m, "count_put_receive_buffer: %llu count_send_empty: %llu",
		   sc->statistics.put_receive_buffer,
		   sc->statistics.send_empty);

	seq_puts(m, "\n");
	seq_printf(m, "Read Queue count_enqueue_reassembly_queue: %llu ",
		   sc->statistics.enqueue_reassembly_queue);
	seq_printf(m, "count_dequeue_reassembly_queue: %llu ",
		   sc->statistics.dequeue_reassembly_queue);
	seq_printf(m, "reassembly_data_length: %u ",
		   sc->recv_io.reassembly.data_length);
	seq_printf(m, "reassembly_queue_length: %u",
		   sc->recv_io.reassembly.queue_length);

	seq_puts(m, "\n");
	seq_printf(m, "Current Credits send_credits: %u ",
		   atomic_read(&sc->send_io.credits.count));
	seq_printf(m, "receive_credits: %u receive_credit_target: %u",
		   atomic_read(&sc->recv_io.credits.count),
		   sc->recv_io.credits.target);

	seq_puts(m, "\n");
	seq_printf(m, "Pending send_pending: %u ",
		   atomic_read(&sc->send_io.pending.count));

	seq_puts(m, "\n");
	seq_printf(m, "MR responder_resources: %u ",
		   sp->responder_resources);
	seq_printf(m, "max_frmr_depth: %u mr_type: 0x%x",
		   sp->max_frmr_depth,
		   sc->mr_io.type);

	seq_puts(m, "\n");
	seq_printf(m, "MR mr_ready_count: %u mr_used_count: %u",
		   atomic_read(&sc->mr_io.ready.count),
		   atomic_read(&sc->mr_io.used.count));
}
