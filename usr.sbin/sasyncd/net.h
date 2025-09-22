/*	$OpenBSD: net.h,v 1.5 2006/06/02 20:09:43 mcbride Exp $	*/

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


struct qmsg;
struct syncpeer {
	LIST_ENTRY(syncpeer)	link;

	char		*name;		/* FQDN or an IP, from conf */
	struct sockaddr	*sa;
	int		 socket;
	enum RUNSTATE	 runstate;

	SIMPLEQ_HEAD(, qmsg)	msgs;
};

/* Control message types. */
enum CTLTYPE { RESERVED = 0, CTL_STATE, CTL_ERROR, CTL_ACK,
    CTL_ENDSNAP, CTL_UNKNOWN };
#define CTLTYPES { \
	"RESERVED", "CTL_STATE", "CTL_ERROR", "CTL_ACK", \
	"CTL_ENDSNAP", "CTL_UNKNOWN" \
};

/* net.c */
void	net_connect(void);
void	net_disconnect_peer(struct syncpeer *);

/* net_ctl.c */
void	net_ctl_handle_msg(struct syncpeer *, u_int8_t *, u_int32_t);
int	net_ctl_send_ack(struct syncpeer *, enum CTLTYPE, u_int32_t);
int	net_ctl_send_error(struct syncpeer *, enum CTLTYPE);
int	net_ctl_send_endsnap(struct syncpeer *);
int	net_ctl_send_state(struct syncpeer *);

