/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

/*
 * This file is used to provide the initiator and target with a prettier
 * interface.  It must not be included by ICL modules, such as icl_soft.c.
 */

#ifndef ICL_WRAPPERS_H
#define	ICL_WRAPPERS_H

#include <sys/kobj.h>

#include <dev/iscsi/icl.h>
#include <icl_conn_if.h>

static inline struct icl_pdu *
icl_pdu_new(struct icl_conn *ic, int flags)
{

	return (ICL_CONN_NEW_PDU(ic, flags));
}

static inline size_t
icl_pdu_data_segment_length(const struct icl_pdu *ip)
{

	return (ICL_CONN_PDU_DATA_SEGMENT_LENGTH(ip->ip_conn, ip));
}

static inline int
icl_pdu_append_data(struct icl_pdu *ip, const void *addr, size_t len, int flags)
{

	return (ICL_CONN_PDU_APPEND_DATA(ip->ip_conn, ip, addr, len, flags));
}

static inline void
icl_pdu_get_data(struct icl_pdu *ip, size_t off, void *addr, size_t len)
{

	ICL_CONN_PDU_GET_DATA(ip->ip_conn, ip, off, addr, len);
}

static inline void
icl_pdu_queue(struct icl_pdu *ip)
{

	ICL_CONN_PDU_QUEUE(ip->ip_conn, ip);
}

static inline void
icl_pdu_free(struct icl_pdu *ip)
{

	ICL_CONN_PDU_FREE(ip->ip_conn, ip);
}

static inline void
icl_conn_free(struct icl_conn *ic)
{

	ICL_CONN_FREE(ic);
}

static inline int
icl_conn_handoff(struct icl_conn *ic, int fd)
{

	return (ICL_CONN_HANDOFF(ic, fd));
}

static inline void
icl_conn_close(struct icl_conn *ic)
{

	ICL_CONN_CLOSE(ic);
}

static inline int
icl_conn_task_setup(struct icl_conn *ic, struct icl_pdu *ip,
    struct ccb_scsiio *csio, uint32_t *task_tagp, void **prvp)
{

	return (ICL_CONN_TASK_SETUP(ic, ip, csio, task_tagp, prvp));
}

static inline void
icl_conn_task_done(struct icl_conn *ic, void *prv)
{

	ICL_CONN_TASK_DONE(ic, prv);
}

static inline int
icl_conn_transfer_setup(struct icl_conn *ic, union ctl_io *io,
    uint32_t *transfer_tagp, void **prvp)
{

	return (ICL_CONN_TRANSFER_SETUP(ic, io, transfer_tagp, prvp));
}

static inline void
icl_conn_transfer_done(struct icl_conn *ic, void *prv)
{

	ICL_CONN_TRANSFER_DONE(ic, prv);
}

/*
 * The function below is only used with ICL_KERNEL_PROXY.
 */
static inline int
icl_conn_connect(struct icl_conn *ic, int domain, int socktype,
    int protocol, struct sockaddr *from_sa, struct sockaddr *to_sa)
{

	return (ICL_CONN_CONNECT(ic, domain, socktype, protocol,
	    from_sa, to_sa));
}

#endif /* !ICL_WRAPPERS_H */
