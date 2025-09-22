/*	$OpenBSD: net_ctl.c,v 1.11 2016/07/18 21:22:09 benno Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Multicom Security AB.
 */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sasyncd.h"
#include "net.h"

struct ctlmsg {
	u_int32_t	type;
	u_int32_t	data;
	u_int32_t	data2;
};

int snapcount = 0;

static int
net_ctl_check_state(struct syncpeer *p, enum RUNSTATE nstate)
{
	if (nstate < INIT || nstate > FAIL) {
		log_msg(0, "net_ctl: got bad state %d from peer \"%s\"",
		    nstate, p->name);
		net_ctl_send_error(p, CTL_STATE);
		return -1;
	}
	if (cfgstate.runstate == MASTER && nstate == MASTER) {
		log_msg(0, "net_ctl: got bad state MASTER from peer \"%s\"",
		    p->name);
		net_ctl_send_error(p, CTL_STATE);
		return -1;
	}
	if (p->runstate != nstate) {
		p->runstate = nstate;
		log_msg(1, "net_ctl: peer \"%s\" state change to %s", p->name,
		    carp_state_name(nstate));
	}
	return 0;
}

void
net_ctl_handle_msg(struct syncpeer *p, u_int8_t *msg, u_int32_t msglen)
{
	struct ctlmsg	*ctl = (struct ctlmsg *)msg;
	enum RUNSTATE	 nstate;
	enum CTLTYPE	 ctype;
	static char	*ct, *ctltype[] = CTLTYPES;

	if (msglen < sizeof *ctl) {
		log_msg(0, "net_ctl: got bad control message from peer \"%s\"",
		    p->name);
		net_ctl_send_error(p, CTL_UNKNOWN);
		return;
	}

	switch (ntohl(ctl->type)) {
	case CTL_ENDSNAP:
		log_msg(2, "net_ctl: got CTL_ENDSNAP from peer \"%s\"",
		    p->name);

		/* XXX More sophistication required to handle multiple peers. */
		if (carp_demoted) {
			snapcount++;
			if (snapcount >= cfgstate.peercnt)
				monitor_carpundemote(NULL);
		}
		break;

	case CTL_STATE:
		log_msg(2, "net_ctl: got CTL_STATE from peer \"%s\"", p->name);
		nstate = (enum RUNSTATE)ntohl(ctl->data);
		if (net_ctl_check_state(p, nstate) == 0)
			net_ctl_send_ack(p, CTL_STATE, cfgstate.runstate);
		break;

	case CTL_ERROR:
		log_msg(1, "net_ctl: got ERROR from peer \"%s\"", p->name);

		switch (ntohl(ctl->data)) {
		case RESERVED: /* PFKEY -- nothing to do here for now */
			break;

		case CTL_STATE:
			nstate = cfgstate.runstate;
			carp_check_state();
			if (nstate != cfgstate.runstate)
				net_ctl_send_state(p);
			break;

		case CTL_UNKNOWN:
		default:
			break;
		}
		break;

	case CTL_ACK:
		ctype = (enum CTLTYPE)ntohl(ctl->data);
		if (ctype < RESERVED || ctype > CTL_UNKNOWN)
			ct = "<unknown>";
		else
			ct = ctltype[ctype];
		log_msg(2, "net_ctl: got %s ACK from peer \"%s\"", ct,
		    p->name);
		if (ctype == CTL_STATE) {
			nstate = (enum RUNSTATE)ntohl(ctl->data2);
			net_ctl_check_state(p, nstate);
		}
	break;

	case CTL_UNKNOWN:
	default:
		log_msg(1, "net_ctl: got unknown msg type %u from peer \"%s\"",
		    ntohl(ctl->type), p->name);
		break;
	}
}

static int
net_ctl_send(struct syncpeer *p, u_int32_t type, u_int32_t d, u_int32_t d2)
{
	struct ctlmsg	*m = malloc(sizeof *m);

	if (!m) {
		log_err("malloc(%zu)", sizeof *m);
		return -1;
	}

	memset(m, 0, sizeof *m);
	m->type = htonl(type);
	m->data = htonl(d);
	m->data2 = htonl(d2);

	return net_queue(p, MSG_SYNCCTL, (u_int8_t *)m, sizeof *m);
}

int
net_ctl_send_ack(struct syncpeer *p, enum CTLTYPE prevtype, u_int32_t code)
{
	return net_ctl_send(p, CTL_ACK, (u_int32_t)prevtype, code);
}

int
net_ctl_send_state(struct syncpeer *p)
{
	return net_ctl_send(p, CTL_STATE, (u_int32_t)cfgstate.runstate, 0);
}

int
net_ctl_send_error(struct syncpeer *p, enum CTLTYPE prevtype)
{
	return net_ctl_send(p, CTL_ERROR, (u_int32_t)prevtype, 0);
}

int
net_ctl_send_endsnap(struct syncpeer *p)
{
	return net_ctl_send(p, CTL_ENDSNAP, 0, 0);
}

/* After a CARP tracker state change, send an state ctl msg to all peers. */
void
net_ctl_update_state(void)
{
	struct syncpeer *p;

	/* We may have new peers available.  */
	net_connect();

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		if (p->socket == -1)
			continue;
		log_msg(2, "net_ctl: sending my state %s to peer \"%s\"",
		    carp_state_name(cfgstate.runstate), p->name);
		net_ctl_send_state(p);
	}
}
