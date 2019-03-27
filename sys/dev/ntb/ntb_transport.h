/*-
 * Copyright (c) 2016 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

struct ntb_transport_qp;

extern devclass_t ntb_transport_devclass;

enum ntb_link_event {
	NTB_LINK_DOWN = 0,
	NTB_LINK_UP,
};

struct ntb_queue_handlers {
	void (*rx_handler)(struct ntb_transport_qp *qp, void *qp_data,
	    void *data, int len);
	void (*tx_handler)(struct ntb_transport_qp *qp, void *qp_data,
	    void *data, int len);
	void (*event_handler)(void *data, enum ntb_link_event status);
};

int ntb_transport_queue_count(device_t dev);
struct ntb_transport_qp *
ntb_transport_create_queue(device_t dev, int q,
    const struct ntb_queue_handlers *handlers, void *data);
void ntb_transport_free_queue(struct ntb_transport_qp *qp);
unsigned char ntb_transport_qp_num(struct ntb_transport_qp *qp);
unsigned int ntb_transport_max_size(struct ntb_transport_qp *qp);
int ntb_transport_rx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len);
int ntb_transport_tx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len);
void *ntb_transport_rx_remove(struct ntb_transport_qp *qp, unsigned int *len);
void ntb_transport_link_up(struct ntb_transport_qp *qp);
void ntb_transport_link_down(struct ntb_transport_qp *qp);
bool ntb_transport_link_query(struct ntb_transport_qp *qp);
uint64_t ntb_transport_link_speed(struct ntb_transport_qp *qp);
unsigned int ntb_transport_tx_free_entry(struct ntb_transport_qp *qp);
