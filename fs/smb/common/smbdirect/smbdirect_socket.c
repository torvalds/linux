// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_socket_prepare_create(struct smbdirect_socket *sc,
					    const struct smbdirect_socket_parameters *sp,
					    struct workqueue_struct *workqueue)
{
	smbdirect_socket_init(sc);

	/*
	 * Make a copy of the callers parameters
	 * from here we only work on the copy
	 */
	sc->parameters = *sp;

	/*
	 * Remember the callers workqueue
	 */
	sc->workqueue = workqueue;
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_socket_set_logging(struct smbdirect_socket *sc,
					 void *private_ptr,
					 bool (*needed)(struct smbdirect_socket *sc,
							void *private_ptr,
							unsigned int lvl,
							unsigned int cls),
					 void (*vaprintf)(struct smbdirect_socket *sc,
							  const char *func,
							  unsigned int line,
							  void *private_ptr,
							  unsigned int lvl,
							  unsigned int cls,
							  struct va_format *vaf))
{
	sc->logging.private_ptr = private_ptr;
	sc->logging.needed = needed;
	sc->logging.vaprintf = vaprintf;
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_socket_wake_up_all(struct smbdirect_socket *sc)
{
	/*
	 * Wake up all waiters in all wait queues
	 * in order to notice the broken connection.
	 */
	wake_up_all(&sc->status_wait);
	wake_up_all(&sc->send_io.bcredits.wait_queue);
	wake_up_all(&sc->send_io.lcredits.wait_queue);
	wake_up_all(&sc->send_io.credits.wait_queue);
	wake_up_all(&sc->send_io.pending.dec_wait_queue);
	wake_up_all(&sc->send_io.pending.zero_wait_queue);
	wake_up_all(&sc->recv_io.reassembly.wait_queue);
	wake_up_all(&sc->rw_io.credits.wait_queue);
	wake_up_all(&sc->mr_io.ready.wait_queue);
	wake_up_all(&sc->mr_io.cleanup.wait_queue);
}
