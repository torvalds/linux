/*
 * NET		Generic infrastructure for Network protocols.
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * 		From code originally in include/net/tcp.h
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <net/request_sock.h>

int reqsk_queue_alloc(struct request_sock_queue *queue,
		      const int nr_table_entries)
{
	const int lopt_size = sizeof(struct listen_sock) +
			      nr_table_entries * sizeof(struct request_sock *);
	struct listen_sock *lopt = kmalloc(lopt_size, GFP_KERNEL);

	if (lopt == NULL)
		return -ENOMEM;

	memset(lopt, 0, lopt_size);

	for (lopt->max_qlen_log = 6;
	     (1 << lopt->max_qlen_log) < sysctl_max_syn_backlog;
	     lopt->max_qlen_log++);

	get_random_bytes(&lopt->hash_rnd, sizeof(lopt->hash_rnd));
	rwlock_init(&queue->syn_wait_lock);
	queue->rskq_accept_head = queue->rskq_accept_head = NULL;

	write_lock_bh(&queue->syn_wait_lock);
	queue->listen_opt = lopt;
	write_unlock_bh(&queue->syn_wait_lock);

	return 0;
}

EXPORT_SYMBOL(reqsk_queue_alloc);
