/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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
 */

#ifndef _USB_MBUF_H_
#define	_USB_MBUF_H_

/*
 * The following structure defines a minimum re-implementation of the
 * mbuf system in the kernel.
 */
struct usb_mbuf {
	uint8_t *cur_data_ptr;
	uint8_t *min_data_ptr;
	struct usb_mbuf *usb_nextpkt;
	struct usb_mbuf *usb_next;

	usb_size_t cur_data_len;
	usb_size_t max_data_len;
	uint8_t last_packet:1;
	uint8_t unused:7;
};

#define	USB_IF_ENQUEUE(ifq, m) do {		\
    (m)->usb_nextpkt = NULL;			\
    if ((ifq)->ifq_tail == NULL)		\
        (ifq)->ifq_head = (m);			\
    else					\
        (ifq)->ifq_tail->usb_nextpkt = (m);	\
    (ifq)->ifq_tail = (m);			\
    (ifq)->ifq_len++;				\
  } while (0)

#define	USB_IF_DEQUEUE(ifq, m) do {				\
    (m) = (ifq)->ifq_head;					\
    if (m) {							\
        if (((ifq)->ifq_head = (m)->usb_nextpkt) == NULL) {	\
	     (ifq)->ifq_tail = NULL;				\
	}							\
	(m)->usb_nextpkt = NULL;				\
	(ifq)->ifq_len--;					\
    }								\
  } while (0)

#define	USB_IF_PREPEND(ifq, m) do {		\
      (m)->usb_nextpkt = (ifq)->ifq_head;	\
      if ((ifq)->ifq_tail == NULL) {		\
	  (ifq)->ifq_tail = (m);		\
      }						\
      (ifq)->ifq_head = (m);			\
      (ifq)->ifq_len++;				\
  } while (0)

#define	USB_IF_QFULL(ifq)   ((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define	USB_IF_QLEN(ifq)    ((ifq)->ifq_len)
#define	USB_IF_POLL(ifq, m) ((m) = (ifq)->ifq_head)

#define	USB_MBUF_RESET(m) do {			\
    (m)->cur_data_ptr = (m)->min_data_ptr;	\
    (m)->cur_data_len = (m)->max_data_len;	\
    (m)->last_packet = 0;			\
  } while (0)

/* prototypes */
void   *usb_alloc_mbufs(struct malloc_type *type, struct usb_ifqueue *ifq,
	    usb_size_t block_size, uint16_t nblocks);

#endif					/* _USB_MBUF_H_ */
