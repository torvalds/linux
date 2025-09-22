/* $OpenBSD: ike_aggressive.c,v 1.13 2018/01/15 09:54:48 mpi Exp $	 */
/* $EOM: ike_aggressive.c,v 1.4 2000/01/31 22:33:45 niklas Exp $	 */

/*
 * Copyright (c) 1999 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999 Angelos D. Keromytis.  All rights reserved.
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
#include "ike_aggressive.h"
#include "ike_phase_1.h"
#include "ipsec.h"
#include "ipsec_doi.h"
#include "isakmp.h"
#include "log.h"
#include "message.h"
#include "nat_traversal.h"
#include "prf.h"
#include "sa.h"
#include "transport.h"
#include "util.h"

static int	initiator_recv_SA_KE_NONCE_ID_AUTH(struct message *);
static int	initiator_send_SA_KE_NONCE_ID(struct message *);
static int	initiator_send_AUTH(struct message *);
static int	responder_recv_SA_KE_NONCE_ID(struct message *);
static int	responder_send_SA_KE_NONCE_ID_AUTH(struct message *);
static int	responder_recv_AUTH(struct message *);

int (*ike_aggressive_initiator[])(struct message *) = {
	initiator_send_SA_KE_NONCE_ID,
	initiator_recv_SA_KE_NONCE_ID_AUTH,
	initiator_send_AUTH
};

int (*ike_aggressive_responder[])(struct message *) = {
	responder_recv_SA_KE_NONCE_ID,
	responder_send_SA_KE_NONCE_ID_AUTH,
	responder_recv_AUTH
};

/* Offer a set of transforms to the responder in the MSG message.  */
static int
initiator_send_SA_KE_NONCE_ID(struct message *msg)
{
	if (ike_phase_1_initiator_send_SA(msg))
		return -1;

	if (ike_phase_1_initiator_send_KE_NONCE(msg))
		return -1;

	return ike_phase_1_send_ID(msg);
}

/* Figure out what transform the responder chose.  */
static int
initiator_recv_SA_KE_NONCE_ID_AUTH(struct message *msg)
{
	if (ike_phase_1_initiator_recv_SA(msg))
		return -1;

	if (ike_phase_1_initiator_recv_KE_NONCE(msg))
		return -1;

	return ike_phase_1_recv_ID_AUTH(msg);
}

static int
initiator_send_AUTH(struct message *msg)
{
	msg->exchange->flags |= EXCHANGE_FLAG_ENCRYPT;

	if (ike_phase_1_send_AUTH(msg))
		return -1;

	/*
	 * RFC 2407 4.6.3 says that, among others, INITIAL-CONTACT MUST NOT
	 * be sent in Aggressive Mode.  This leaves us with the choice of
	 * doing it in an informational exchange of its own with no delivery
	 * guarantee or in the first Quick Mode, or not at all.
	 * draft-jenkins-ipsec-rekeying-01.txt has some text that requires
	 * INITIAL-CONTACT in phase 1, thus contradicting what we learned
	 * above.  I will bring this up in the IPsec list.  For now we don't
	 * do INITIAL-CONTACT at all when using aggressive mode.
	 */
	return 0;
}

/*
 * Accept a set of transforms offered by the initiator and chose one we can
 * handle.  Also accept initiator's public DH value, nonce and ID.
 */
static int
responder_recv_SA_KE_NONCE_ID(struct message *msg)
{
	if (ike_phase_1_responder_recv_SA(msg))
		return -1;

	if (ike_phase_1_recv_ID(msg))
		return -1;

	return ike_phase_1_recv_KE_NONCE(msg);
}

/*
 * Reply with the transform we chose.  Send our public DH value and a nonce
 * to the initiator.
 */
static int
responder_send_SA_KE_NONCE_ID_AUTH(struct message *msg)
{
	/* Add the SA payload with the transform that was chosen.  */
	if (ike_phase_1_responder_send_SA(msg))
		return -1;

	/* XXX Should we really just use the initiator's nonce size?  */
	if (ike_phase_1_send_KE_NONCE(msg, msg->exchange->nonce_i_len))
		return -1;

	if (ike_phase_1_post_exchange_KE_NONCE(msg))
		return -1;

	return ike_phase_1_responder_send_ID_AUTH(msg);
}

/*
 * Reply with the transform we chose.  Send our public DH value and a nonce
 * to the initiator.
 */
static int
responder_recv_AUTH(struct message *msg)
{
	if (ike_phase_1_recv_AUTH(msg))
		return -1;

	/* Aggressive: Check for NAT-D payloads and contents.  */
	if (msg->exchange->flags & EXCHANGE_FLAG_NAT_T_CAP_PEER)
		(void)nat_t_exchange_check_nat_d(msg);
	return 0;
}
