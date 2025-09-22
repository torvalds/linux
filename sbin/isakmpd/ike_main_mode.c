/* $OpenBSD: ike_main_mode.c,v 1.19 2018/01/15 09:54:48 mpi Exp $	 */
/* $EOM: ike_main_mode.c,v 1.77 1999/04/25 22:12:34 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
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
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "attribute.h"
#include "conf.h"
#include "constants.h"
#include "crypto.h"
#include "dh.h"
#include "doi.h"
#include "exchange.h"
#include "hash.h"
#include "ike_auth.h"
#include "ike_main_mode.h"
#include "ike_phase_1.h"
#include "ipsec.h"
#include "ipsec_doi.h"
#include "isakmp.h"
#include "log.h"
#include "message.h"
#include "prf.h"
#include "sa.h"
#include "transport.h"
#include "util.h"

static int      initiator_send_ID_AUTH(struct message *);
static int      responder_send_ID_AUTH(struct message *);
static int      responder_send_KE_NONCE(struct message *);

int (*ike_main_mode_initiator[]) (struct message *) = {
	ike_phase_1_initiator_send_SA,
	ike_phase_1_initiator_recv_SA,
	ike_phase_1_initiator_send_KE_NONCE,
	ike_phase_1_initiator_recv_KE_NONCE,
	initiator_send_ID_AUTH,
	ike_phase_1_recv_ID_AUTH
};

int (*ike_main_mode_responder[]) (struct message *) = {
	ike_phase_1_responder_recv_SA,
	ike_phase_1_responder_send_SA,
	ike_phase_1_recv_KE_NONCE,
	responder_send_KE_NONCE,
	ike_phase_1_recv_ID_AUTH,
	responder_send_ID_AUTH
};

static int
initiator_send_ID_AUTH(struct message *msg)
{
	msg->exchange->flags |= EXCHANGE_FLAG_ENCRYPT;

	if (ike_phase_1_send_ID(msg))
		return -1;

	if (ike_phase_1_send_AUTH(msg))
		return -1;

	return ipsec_initial_contact(msg);
}

/* Send our public DH value and a nonce to the initiator.  */
int
responder_send_KE_NONCE(struct message *msg)
{
	/* XXX Should we really just use the initiator's nonce size?  */
	if (ike_phase_1_send_KE_NONCE(msg, msg->exchange->nonce_i_len))
		return -1;

	/*
	 * Calculate DH values & key material in parallel with the message
	 * going on a roundtrip over the wire.
	 */
	message_register_post_send(msg,
	    (void (*)(struct message *))ike_phase_1_post_exchange_KE_NONCE);

	return 0;
}

static int
responder_send_ID_AUTH(struct message *msg)
{
	msg->exchange->flags |= EXCHANGE_FLAG_ENCRYPT;

	if (ike_phase_1_responder_send_ID_AUTH(msg))
		return -1;

	return ipsec_initial_contact(msg);
}
