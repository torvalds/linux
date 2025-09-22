/*	$OpenBSD: task.c,v 1.10 2014/05/10 11:28:02 claudio Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <scsi/iscsi.h>

#include <errno.h>
#include <event.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "iscsid.h"
#include "log.h"

/*
 * Task handling, PDU are attached to tasks and task are scheduled across
 * all connections of a session.
 */

void
task_init(struct task *t, struct session *s, int immediate, void *carg,
    void (*c)(struct connection *, void *, struct pdu *),
    void (*f)(void *))
{
	TAILQ_INIT(&t->sendq);
	TAILQ_INIT(&t->recvq);
	t->callback = c;
	t->failback = f;
	t->callarg = carg;
	/* skip reserved and maybe bad ITT values */
	if (s->itt == 0xffffffff || s->itt == 0)
		s->itt = 1;
	t->itt = s->itt++; /* XXX we could do better here */
	t->cmdseqnum = s->cmdseqnum;
	if (!immediate)
		s->cmdseqnum++;
}

void
taskq_cleanup(struct taskq *tq)
{
	struct task *t;

	while ((t = TAILQ_FIRST(tq))) {
		TAILQ_REMOVE(tq, t, entry);
		if (t->failback)
			t->failback(t->callarg);
		conn_task_cleanup(NULL, t);
		free(t);
	}
}

void
task_pdu_add(struct task *t, struct pdu *p)
{
	struct iscsi_pdu *ipdu;

	/* fixup the pdu by setting the itt and seqnum if needed */
	ipdu = pdu_getbuf(p, NULL, PDU_HEADER);
	ipdu->itt = ntohl(t->itt);
	switch (ISCSI_PDU_OPCODE(ipdu->opcode)) {
	case ISCSI_OP_I_NOP:
	case ISCSI_OP_SCSI_REQUEST:
	case ISCSI_OP_TASK_REQUEST:
	case ISCSI_OP_LOGIN_REQUEST:
	case ISCSI_OP_TEXT_REQUEST:
	case ISCSI_OP_LOGOUT_REQUEST:
		ipdu->cmdsn = ntohl(t->cmdseqnum);
		break;
	}

	TAILQ_INSERT_TAIL(&t->sendq, p, entry);
}

void
task_pdu_cb(struct connection *c, struct pdu *p)
{
	struct task *t;
	struct iscsi_pdu *ipdu;
	u_int32_t itt;

	ipdu = pdu_getbuf(p, NULL, PDU_HEADER);
	switch (ISCSI_PDU_OPCODE(ipdu->opcode)) {
	case ISCSI_OP_T_NOP:
		itt = ntohl(ipdu->itt);
		if (itt == 0xffffffff) {
			/* target issued a ping, must answer back immediately */
			c->expstatsn = ntohl(ipdu->cmdsn) + 1;
			initiator_nop_in_imm(c, p);
			break;
		}
		/* FALLTHROUGH */
	case ISCSI_OP_LOGIN_RESPONSE:
	case ISCSI_OP_TEXT_RESPONSE:
	case ISCSI_OP_LOGOUT_RESPONSE:
	case ISCSI_OP_SCSI_RESPONSE:
	case ISCSI_OP_R2T:
	case ISCSI_OP_DATA_IN:
		itt = ntohl(ipdu->itt);
		c->expstatsn = ntohl(ipdu->cmdsn) + 1;

		/* XXX for now search the task on the connection queue
		   later on this should be moved to a per session RB tree but
		   now I do the quick ugly thing. */
		TAILQ_FOREACH(t, &c->tasks, entry) {
			if (itt == t->itt)
				break;
		}
		if (t)
			t->callback(c, t->callarg, p);
		else {
			log_debug("no task for PDU found");
			log_pdu(p, 1);
			pdu_free(p);
		}
		break;
	default:
		log_warnx("not handled yet. fix me");
		log_pdu(p, 1);
		pdu_free(p);
	}
}
