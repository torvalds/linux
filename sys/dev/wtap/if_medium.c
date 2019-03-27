/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Monthadar Al Jaberi, TerraNet AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include "if_wtapvar.h"
#include "if_medium.h"

void
init_medium(struct wtap_medium *md)
{

	DWTAP_PRINTF("%s\n", __func__);
	STAILQ_INIT(&md->md_pktbuf);
	mtx_init(&md->md_mtx, "wtap_medium mtx", NULL, MTX_DEF | MTX_RECURSE);

	/* Event handler for sending packets between wtaps */
	struct eventhandler *eh = (struct eventhandler *)
	    malloc(sizeof(struct eventhandler), M_WTAP, M_NOWAIT | M_ZERO);
	eh->tq = taskqueue_create("wtap_tx_taskq",  M_NOWAIT | M_ZERO,
	    taskqueue_thread_enqueue, &eh->tq);
	taskqueue_start_threads(&eh->tq, 1, PI_NET, "%s taskq", "wtap_medium");
	md->tx_handler = eh;
	/* Mark medium closed by default */
	md->open = 0;
}

void
deinit_medium(struct wtap_medium *md)
{

	DWTAP_PRINTF("%s\n", __func__);
	taskqueue_free(md->tx_handler->tq);
	free(md->tx_handler, M_WTAP);
}

int
medium_transmit(struct wtap_medium *md, int id, struct mbuf*m)
{

	mtx_lock(&md->md_mtx);
	if (md->open == 0){
		DWTAP_PRINTF("[%d] dropping m=%p\n", id, m);
		m_free(m);
		mtx_unlock(&md->md_mtx);
		return 0;
	}

	DWTAP_PRINTF("[%d] transmiting m=%p\n", id, m);
	struct packet *p = (struct packet *)malloc(sizeof(struct packet),
	    M_WTAP_PACKET, M_ZERO | M_NOWAIT);
	p->id = id;
	p->m = m;

	STAILQ_INSERT_TAIL(&md->md_pktbuf, p, pf_list);
	taskqueue_enqueue(md->tx_handler->tq, &md->tx_handler->proc);
	mtx_unlock(&md->md_mtx);

      return 0;
}

struct packet *
medium_get_next_packet(struct wtap_medium *md)
{
	struct packet *p;

	mtx_lock(&md->md_mtx);
	p = STAILQ_FIRST(&md->md_pktbuf);
	if (p == NULL){
		mtx_unlock(&md->md_mtx);
		return NULL;
	}

	STAILQ_REMOVE_HEAD(&md->md_pktbuf, pf_list);
	mtx_unlock(&md->md_mtx);
	return p;
}

void
medium_open(struct wtap_medium *md)
{

	mtx_lock(&md->md_mtx);
	md->open = 1;
	mtx_unlock(&md->md_mtx);
}

void
medium_close(struct wtap_medium *md)
{

	mtx_lock(&md->md_mtx);
	md->open = 0;
	mtx_unlock(&md->md_mtx);
}
