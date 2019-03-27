/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Netflix, Inc.
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

#ifndef __tcp_log_dev_h__
#define	__tcp_log_dev_h__

/*
 * This is the common header for data streamed from the log device. All
 * blocks of data need to start with this header.
 */
struct tcp_log_common_header {
	uint32_t	tlch_version;	/* Version is specific to type. */
	uint32_t	tlch_type;	/* Type of entry(ies) that follow. */
	uint64_t	tlch_length;	/* Total length, including header. */
} __packed;

#define	TCP_LOG_DEV_TYPE_BBR	1	/* black box recorder */

#ifdef _KERNEL
/*
 * This is a queue entry. All queue entries need to start with this structure
 * so the common code can cast them to this structure; however, other modules
 * are free to include additional data after this structure.
 *
 * The elements are explained here:
 * tldq_queue: used by the common code to maintain this entry's position in the
 *     queue.
 * tldq_buf: should be NULL, or a pointer to a chunk of data. The data must be
 *     as long as the common header indicates.
 * tldq_xform: If tldq_buf is NULL, the code will call this to create the
 *     the tldq_buf object. The function should *not* directly modify tldq_buf,
 *     but should return the buffer (which must meet the restrictions
 *     indicated for tldq_buf).
 * tldq_dtor: This function is called to free the queue entry. If tldq_buf is
 *     not NULL, the dtor function must free that, too.
 * tldq_refcnt: used by the common code to indicate how many readers still need
 *     this data.
 */
struct tcp_log_dev_queue {
	STAILQ_ENTRY(tcp_log_dev_queue) tldq_queue;
	struct tcp_log_common_header *tldq_buf;
	struct tcp_log_common_header *(*tldq_xform)(struct tcp_log_dev_queue *entry);
	void	(*tldq_dtor)(struct tcp_log_dev_queue *entry);
	volatile u_int tldq_refcnt;
};

STAILQ_HEAD(log_queueh, tcp_log_dev_queue);

struct tcp_log_dev_info {
	STAILQ_ENTRY(tcp_log_dev_info) tldi_list;
	struct tcp_log_dev_queue *tldi_head;
	struct tcp_log_common_header *tldi_cur;
	off_t			tldi_off;
};
STAILQ_HEAD(log_infoh, tcp_log_dev_info);

#ifdef TCP_BLACKBOX
MALLOC_DECLARE(M_TCPLOGDEV);
int tcp_log_dev_add_log(struct tcp_log_dev_queue *entry);
#endif /* TCP_BLACKBOX */
#endif /* _KERNEL */
#endif /* !__tcp_log_dev_h__ */
