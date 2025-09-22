/*	$OpenBSD: pfkeyv2_parsemessage.c,v 1.64 2025/05/14 14:32:15 mvs Exp $	*/

/*
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Craig Metz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>

#if NPF > 0
#include <net/if.h>
#include <net/pfvar.h>
#endif

#ifdef ENCDEBUG
#define DPRINTF(fmt, args...)						\
	do {								\
		if (atomic_load_int(&encdebug))				\
			printf("%s: " fmt "\n", __func__, ## args);	\
	} while (0)
#else
#define DPRINTF(fmt, args...)						\
	do { } while (0)
#endif

#define BITMAP_SA                      (1LL << SADB_EXT_SA)
#define BITMAP_LIFETIME_CURRENT        (1LL << SADB_EXT_LIFETIME_CURRENT)
#define BITMAP_LIFETIME_HARD           (1LL << SADB_EXT_LIFETIME_HARD)
#define BITMAP_LIFETIME_SOFT           (1LL << SADB_EXT_LIFETIME_SOFT)
#define BITMAP_ADDRESS_SRC             (1LL << SADB_EXT_ADDRESS_SRC)
#define BITMAP_ADDRESS_DST             (1LL << SADB_EXT_ADDRESS_DST)
#define BITMAP_ADDRESS_PROXY           (1LL << SADB_EXT_ADDRESS_PROXY)
#define BITMAP_KEY_AUTH                (1LL << SADB_EXT_KEY_AUTH)
#define BITMAP_KEY_ENCRYPT             (1LL << SADB_EXT_KEY_ENCRYPT)
#define BITMAP_IDENTITY_SRC            (1LL << SADB_EXT_IDENTITY_SRC)
#define BITMAP_IDENTITY_DST            (1LL << SADB_EXT_IDENTITY_DST)
#define BITMAP_SENSITIVITY             (1LL << SADB_EXT_SENSITIVITY)
#define BITMAP_PROPOSAL                (1LL << SADB_EXT_PROPOSAL)
#define BITMAP_SUPPORTED_AUTH          (1LL << SADB_EXT_SUPPORTED_AUTH)
#define BITMAP_SUPPORTED_ENCRYPT       (1LL << SADB_EXT_SUPPORTED_ENCRYPT)
#define BITMAP_SPIRANGE                (1LL << SADB_EXT_SPIRANGE)
#define BITMAP_LIFETIME (BITMAP_LIFETIME_CURRENT | BITMAP_LIFETIME_HARD | BITMAP_LIFETIME_SOFT)
#define BITMAP_ADDRESS (BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST)
#define BITMAP_KEY      (BITMAP_KEY_AUTH | BITMAP_KEY_ENCRYPT)
#define BITMAP_IDENTITY (BITMAP_IDENTITY_SRC | BITMAP_IDENTITY_DST)
#define BITMAP_MSG                     1
#define BITMAP_X_SRC_MASK              (1LL << SADB_X_EXT_SRC_MASK)
#define BITMAP_X_DST_MASK              (1LL << SADB_X_EXT_DST_MASK)
#define BITMAP_X_PROTOCOL              (1LL << SADB_X_EXT_PROTOCOL)
#define BITMAP_X_SRC_FLOW              (1LL << SADB_X_EXT_SRC_FLOW)
#define BITMAP_X_DST_FLOW              (1LL << SADB_X_EXT_DST_FLOW)
#define BITMAP_X_FLOW_TYPE             (1LL << SADB_X_EXT_FLOW_TYPE)
#define BITMAP_X_SA2                   (1LL << SADB_X_EXT_SA2)
#define BITMAP_X_DST2                  (1LL << SADB_X_EXT_DST2)
#define BITMAP_X_POLICY                (1LL << SADB_X_EXT_POLICY)
#define BITMAP_X_FLOW                  (BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_PROTOCOL | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE)
#define BITMAP_X_SUPPORTED_COMP        (1LL << SADB_X_EXT_SUPPORTED_COMP)
#define BITMAP_X_UDPENCAP              (1LL << SADB_X_EXT_UDPENCAP)
#define BITMAP_X_LIFETIME_LASTUSE      (1LL << SADB_X_EXT_LIFETIME_LASTUSE)
#define BITMAP_X_TAG                   (1LL << SADB_X_EXT_TAG)
#define BITMAP_X_TAP                   (1LL << SADB_X_EXT_TAP)
#define BITMAP_X_SATYPE2               (1LL << SADB_X_EXT_SATYPE2)
#define BITMAP_X_RDOMAIN               (1LL << SADB_X_EXT_RDOMAIN)
#define BITMAP_X_COUNTER               (1LL << SADB_X_EXT_COUNTER)
#define BITMAP_X_MTU                   (1LL << SADB_X_EXT_MTU)
#define BITMAP_X_REPLAY                (1LL << SADB_X_EXT_REPLAY)
#define BITMAP_X_IFACE                 (1LL << SADB_X_EXT_IFACE)

uint64_t sadb_exts_allowed_in[SADB_MAX+1] =
{
	/* RESERVED */
	~0,
	/* GETSPI */
	BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_SPIRANGE,
	/* UPDATE */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS | BITMAP_ADDRESS_PROXY | BITMAP_KEY | BITMAP_IDENTITY | BITMAP_X_FLOW | BITMAP_X_UDPENCAP | BITMAP_X_TAG | BITMAP_X_TAP | BITMAP_X_RDOMAIN | BITMAP_X_COUNTER | BITMAP_X_REPLAY | BITMAP_X_IFACE,
	/* ADD */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS | BITMAP_KEY | BITMAP_IDENTITY | BITMAP_X_FLOW | BITMAP_X_UDPENCAP | BITMAP_X_LIFETIME_LASTUSE | BITMAP_X_TAG | BITMAP_X_TAP | BITMAP_X_RDOMAIN | BITMAP_X_COUNTER | BITMAP_X_REPLAY | BITMAP_X_IFACE,
	/* DELETE */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_X_RDOMAIN,
	/* GET */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_X_RDOMAIN,
	/* ACQUIRE */
	BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_IDENTITY | BITMAP_PROPOSAL,
	/* REGISTER */
	0,
	/* EXPIRE */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST,
	/* FLUSH */
	0,
	/* DUMP */
	0,
	/* X_PROMISC */
	0,
	/* X_ADDFLOW */
	BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_IDENTITY_SRC | BITMAP_IDENTITY_DST | BITMAP_X_FLOW | BITMAP_X_RDOMAIN,
	/* X_DELFLOW */
	BITMAP_X_FLOW | BITMAP_X_RDOMAIN,
	/* X_GRPSPIS */
	BITMAP_SA | BITMAP_X_SA2 | BITMAP_X_DST2 | BITMAP_ADDRESS_DST | BITMAP_X_SATYPE2 | BITMAP_X_RDOMAIN,
	/* X_ASKPOLICY */
	BITMAP_X_POLICY,
};

uint64_t sadb_exts_required_in[SADB_MAX+1] =
{
	/* RESERVED */
	0,
	/* GETSPI */
	BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_SPIRANGE,
	/* UPDATE */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST,
	/* ADD */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* DELETE */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* GET */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* ACQUIRE */
	0,
	/* REGISTER */
	0,
	/* EXPIRE */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST,
	/* FLUSH */
	0,
	/* DUMP */
	0,
	/* X_PROMISC */
	0,
	/* X_ADDFLOW */
	BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE,
	/* X_DELFLOW */
	BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE,
	/* X_GRPSPIS */
	BITMAP_SA | BITMAP_X_SA2 | BITMAP_X_DST2 | BITMAP_ADDRESS_DST | BITMAP_X_SATYPE2,
	/* X_ASKPOLICY */
	BITMAP_X_POLICY,
};

const uint64_t sadb_exts_allowed_out[SADB_MAX+1] =
{
	/* RESERVED */
	~0,
	/* GETSPI */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST,
	/* UPDATE */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS | BITMAP_ADDRESS_PROXY | BITMAP_IDENTITY | BITMAP_X_FLOW | BITMAP_X_UDPENCAP | BITMAP_X_TAG | BITMAP_X_TAP | BITMAP_X_RDOMAIN | BITMAP_X_IFACE,
	/* ADD */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS | BITMAP_IDENTITY | BITMAP_X_FLOW | BITMAP_X_UDPENCAP | BITMAP_X_TAG | BITMAP_X_TAP | BITMAP_X_RDOMAIN | BITMAP_X_IFACE,
	/* DELETE */
	BITMAP_SA | BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_X_RDOMAIN,
	/* GET */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS | BITMAP_KEY | BITMAP_IDENTITY | BITMAP_X_UDPENCAP | BITMAP_X_LIFETIME_LASTUSE | BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_PROTOCOL | BITMAP_X_FLOW_TYPE | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_TAG | BITMAP_X_TAP | BITMAP_X_COUNTER | BITMAP_X_RDOMAIN | BITMAP_X_MTU | BITMAP_X_REPLAY | BITMAP_X_IFACE,
	/* ACQUIRE */
	BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_IDENTITY | BITMAP_PROPOSAL,
	/* REGISTER */
	BITMAP_SUPPORTED_AUTH | BITMAP_SUPPORTED_ENCRYPT | BITMAP_X_SUPPORTED_COMP,
	/* EXPIRE */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS,
	/* FLUSH */
	0,
	/* DUMP */
	BITMAP_SA | BITMAP_LIFETIME | BITMAP_ADDRESS | BITMAP_IDENTITY,
	/* X_PROMISC */
	0,
	/* X_ADDFLOW */
	BITMAP_ADDRESS_SRC | BITMAP_ADDRESS_DST | BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_PROTOCOL | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE | BITMAP_IDENTITY_SRC | BITMAP_IDENTITY_DST | BITMAP_X_RDOMAIN,
	/* X_DELFLOW */
	BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_PROTOCOL | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE | BITMAP_X_RDOMAIN,
	/* X_GRPSPIS */
	BITMAP_SA | BITMAP_X_SA2 | BITMAP_X_DST2 | BITMAP_ADDRESS_DST | BITMAP_X_SATYPE2 | BITMAP_X_RDOMAIN,
	/* X_ASKPOLICY */
	BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_FLOW_TYPE | BITMAP_X_POLICY,
};

const uint64_t sadb_exts_required_out[SADB_MAX+1] =
{
	/* RESERVED */
	0,
	/* GETSPI */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* UPDATE */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* ADD */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* DELETE */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* GET */
	BITMAP_SA | BITMAP_LIFETIME_CURRENT | BITMAP_ADDRESS_DST,
	/* ACQUIRE */
	0,
	/* REGISTER */
	BITMAP_SUPPORTED_AUTH | BITMAP_SUPPORTED_ENCRYPT | BITMAP_X_SUPPORTED_COMP,
	/* EXPIRE */
	BITMAP_SA | BITMAP_ADDRESS_DST,
	/* FLUSH */
	0,
	/* DUMP */
	0,
	/* X_PROMISC */
	0,
	/* X_ADDFLOW */
	BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE,
	/* X_DELFLOW */
	BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_FLOW_TYPE,
	/* X_GRPSPIS */
	BITMAP_SA | BITMAP_X_SA2 | BITMAP_X_DST2 | BITMAP_ADDRESS_DST | BITMAP_X_SATYPE2,
	/* X_REPPOLICY */
	BITMAP_X_SRC_FLOW | BITMAP_X_DST_FLOW | BITMAP_X_SRC_MASK | BITMAP_X_DST_MASK | BITMAP_X_FLOW_TYPE,
};

int
pfkeyv2_parsemessage(void *p, int len, void **headers)
{
	struct sadb_ext *sadb_ext;
	int i, left = len;
	uint64_t allow, seen = 1;
	struct sadb_msg *sadb_msg = (struct sadb_msg *) p;

	bzero(headers, (SADB_EXT_MAX + 1) * sizeof(void *));

	if (left < sizeof(struct sadb_msg)) {
		DPRINTF("message too short");
		return (EINVAL);
	}

	headers[0] = p;

	if (sadb_msg->sadb_msg_len * sizeof(uint64_t) != left) {
		DPRINTF("length not a multiple of 64");
		return (EINVAL);
	}

	p += sizeof(struct sadb_msg);
	left -= sizeof(struct sadb_msg);

	if (sadb_msg->sadb_msg_reserved) {
		DPRINTF("message header reserved field set");
		return (EINVAL);
	}

	if (sadb_msg->sadb_msg_type > SADB_MAX) {
		DPRINTF("message type > %d", SADB_MAX);
		return (EINVAL);
	}

	if (!sadb_msg->sadb_msg_type) {
		DPRINTF("message type unset");
		return (EINVAL);
	}

	if (sadb_msg->sadb_msg_pid != curproc->p_p->ps_pid) {
		DPRINTF("bad PID value");
		return (EINVAL);
	}

	if (sadb_msg->sadb_msg_errno) {
		DPRINTF("errno set");
		return (EINVAL);
	}

	allow = sadb_exts_allowed_in[sadb_msg->sadb_msg_type];

	while (left > 0) {
		sadb_ext = (struct sadb_ext *)p;
		if (left < sizeof(struct sadb_ext)) {
			DPRINTF("extension header too short");
			return (EINVAL);
		}

		i = sadb_ext->sadb_ext_len * sizeof(uint64_t);
		if (left < i) {
			DPRINTF("extension header exceeds message length");
			return (EINVAL);
		}

		if (sadb_ext->sadb_ext_type > SADB_EXT_MAX) {
			DPRINTF("unknown extension header %d",
			    sadb_ext->sadb_ext_type);
			return (EINVAL);
		}

		if (!sadb_ext->sadb_ext_type) {
			DPRINTF("unset extension header");
			return (EINVAL);
		}

		if (!(allow & (1LL << sadb_ext->sadb_ext_type))) {
			DPRINTF("extension header %d not permitted on message "
			    "type %d",
			    sadb_ext->sadb_ext_type, sadb_msg->sadb_msg_type);
			return (EINVAL);
		}

		if (headers[sadb_ext->sadb_ext_type]) {
			DPRINTF("duplicate extension header %d",
			    sadb_ext->sadb_ext_type);
			return (EINVAL);
		}

		seen |= (1LL << sadb_ext->sadb_ext_type);

		switch (sadb_ext->sadb_ext_type) {
		case SADB_EXT_SA:
		case SADB_X_EXT_SA2:
		{
			struct sadb_sa *sadb_sa = (struct sadb_sa *)p;

			if (i != sizeof(struct sadb_sa)) {
				DPRINTF("bad header length for SA extension "
				    "header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_sa->sadb_sa_state > SADB_SASTATE_MAX) {
				DPRINTF("unknown SA state %d in SA extension "
				    "header %d",
				    sadb_sa->sadb_sa_state,
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_sa->sadb_sa_state == SADB_SASTATE_DEAD) {
				DPRINTF("cannot set SA state to dead, "
				    "SA extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_sa->sadb_sa_encrypt > SADB_EALG_MAX) {
				DPRINTF("unknown encryption algorithm %d "
				    "in SA extension header %d",
				    sadb_sa->sadb_sa_encrypt,
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_sa->sadb_sa_auth > SADB_AALG_MAX) {
				DPRINTF("unknown authentication algorithm %d "
				    "in SA extension header %d",
				    sadb_sa->sadb_sa_auth,
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_sa->sadb_sa_replay > 64) {
				DPRINTF("unsupported replay window size %d "
				    "in SA extension header %d",
				    sadb_sa->sadb_sa_replay,
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}
		}
		break;
		case SADB_X_EXT_PROTOCOL:
		case SADB_X_EXT_FLOW_TYPE:
		case SADB_X_EXT_SATYPE2:
			if (i != sizeof(struct sadb_protocol)) {
				DPRINTF("bad PROTOCOL/FLOW/SATYPE2 header "
				    "length in extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}
			break;
		case SADB_X_EXT_POLICY:
			if (i != sizeof(struct sadb_x_policy)) {
				DPRINTF("bad POLICY header length");
				return (EINVAL);
			}
			break;
		case SADB_EXT_LIFETIME_CURRENT:
		case SADB_EXT_LIFETIME_HARD:
		case SADB_EXT_LIFETIME_SOFT:
		case SADB_X_EXT_LIFETIME_LASTUSE:
			if (i != sizeof(struct sadb_lifetime)) {
				DPRINTF("bad header length for LIFETIME "
				    "extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}
			break;
		case SADB_EXT_ADDRESS_SRC:
		case SADB_EXT_ADDRESS_DST:
		case SADB_EXT_ADDRESS_PROXY:
		case SADB_X_EXT_SRC_MASK:
		case SADB_X_EXT_DST_MASK:
		case SADB_X_EXT_SRC_FLOW:
		case SADB_X_EXT_DST_FLOW:
		case SADB_X_EXT_DST2:
		{
			struct sadb_address *sadb_address =
			    (struct sadb_address *)p;
			struct sockaddr *sa = (struct sockaddr *)(p +
			    sizeof(struct sadb_address));

			if (i < sizeof(struct sadb_address) +
			    sizeof(struct sockaddr)) {
				DPRINTF("bad ADDRESS extension header %d "
				    "length",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_address->sadb_address_reserved) {
				DPRINTF("ADDRESS extension header %d reserved "
				    "field set",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}
			if (sa->sa_len &&
			    (i != sizeof(struct sadb_address) +
			    PADUP(sa->sa_len))) {
				DPRINTF("bad sockaddr length field in ADDRESS "
				    "extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			switch (sa->sa_family) {
			case AF_INET:
				if (sizeof(struct sadb_address) +
				    PADUP(sizeof(struct sockaddr_in)) != i) {
					DPRINTF("invalid ADDRESS extension "
					    "header %d length",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}

				if (sa->sa_len != sizeof(struct sockaddr_in)) {
					DPRINTF("bad sockaddr_in length in "
					    "ADDRESS extension header %d",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}

				/* Only check the right pieces */
				switch (sadb_ext->sadb_ext_type)
				{
				case SADB_X_EXT_SRC_MASK:
				case SADB_X_EXT_DST_MASK:
				case SADB_X_EXT_SRC_FLOW:
				case SADB_X_EXT_DST_FLOW:
					break;

				default:
					if (((struct sockaddr_in *)sa)->sin_port) {
						DPRINTF("port field set in "
						    "sockaddr_in of ADDRESS "
						    "extension header %d",
						    sadb_ext->sadb_ext_type);
						return (EINVAL);
					}
					break;
				}

				{
					char zero[sizeof(((struct sockaddr_in *)sa)->sin_zero)];
					bzero(zero, sizeof(zero));

					if (bcmp(&((struct sockaddr_in *)sa)->sin_zero, zero, sizeof(zero))) {
						DPRINTF("reserved sockaddr_in "
						    "field non-zero'ed in "
						    "ADDRESS extension header "
						    "%d",
						    sadb_ext->sadb_ext_type);
						return (EINVAL);
					}
				}
				break;
#ifdef INET6
			case AF_INET6:
				if (i != sizeof(struct sadb_address) +
				    PADUP(sizeof(struct sockaddr_in6))) {
					DPRINTF("invalid sockaddr_in6 length "
					    "in ADDRESS extension header %d",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}

				if (sa->sa_len !=
				    sizeof(struct sockaddr_in6)) {
					DPRINTF("bad sockaddr_in6 length in "
					    "ADDRESS extension header %d",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}

				if (((struct sockaddr_in6 *)sa)->sin6_flowinfo) {
					DPRINTF("flowinfo field set in "
					    "sockaddr_in6 of ADDRESS "
					    "extension header %d",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}

				/* Only check the right pieces */
				switch (sadb_ext->sadb_ext_type)
				{
				case SADB_X_EXT_SRC_MASK:
				case SADB_X_EXT_DST_MASK:
				case SADB_X_EXT_SRC_FLOW:
				case SADB_X_EXT_DST_FLOW:
					break;

				default:
					if (((struct sockaddr_in6 *)sa)->sin6_port) {
						DPRINTF("port field set in "
						    "sockaddr_in6 of ADDRESS "
						    "extension header %d",
						    sadb_ext->sadb_ext_type);
						return (EINVAL);
					}
					break;
				}
				break;
#endif /* INET6 */
			default:
				if (sadb_msg->sadb_msg_satype ==
				    SADB_X_SATYPE_TCPSIGNATURE &&
				    sa->sa_family == 0)
					break;
				DPRINTF("unknown address family %d in ADDRESS "
				    "extension header %d",
				    sa->sa_family, sadb_ext->sadb_ext_type);
				return (EINVAL);
			}
		}
		break;
		case SADB_EXT_KEY_AUTH:
		case SADB_EXT_KEY_ENCRYPT:
		{
			struct sadb_key *sadb_key = (struct sadb_key *)p;

			if (i < sizeof(struct sadb_key)) {
				DPRINTF("bad header length in KEY extension "
				    "header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (!sadb_key->sadb_key_bits) {
				DPRINTF("key length unset in KEY extension "
				    "header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (((sadb_key->sadb_key_bits + 63) / 64) * sizeof(uint64_t) != i - sizeof(struct sadb_key)) {
				DPRINTF("invalid key length in KEY extension "
				    "header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_key->sadb_key_reserved) {
				DPRINTF("reserved field set in KEY extension "
				    "header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}
		}
		break;
		case SADB_EXT_IDENTITY_SRC:
		case SADB_EXT_IDENTITY_DST:
		{
			struct sadb_ident *sadb_ident = (struct sadb_ident *)p;

			if (i < sizeof(struct sadb_ident)) {
				DPRINTF("bad header length of IDENTITY "
				    "extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_ident->sadb_ident_type > SADB_IDENTTYPE_MAX) {
				DPRINTF("unknown identity type %d in IDENTITY "
				    "extension header %d",
				    sadb_ident->sadb_ident_type,
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_ident->sadb_ident_reserved) {
				DPRINTF("reserved field set in IDENTITY "
				    "extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (i > sizeof(struct sadb_ident)) {
				char *c =
				    (char *)(p + sizeof(struct sadb_ident));
				int j;

				if (*(char *)(p + i - 1)) {
					DPRINTF("non NUL-terminated identity "
					    "in IDENTITY extension header %d",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}

				j = PADUP(strlen(c) + 1) +
				    sizeof(struct sadb_ident);

				if (i != j) {
					DPRINTF("actual identity length does "
					    "not match expected length in "
					    "identity extension header %d",
					    sadb_ext->sadb_ext_type);
					return (EINVAL);
				}
			}
		}
		break;
		case SADB_EXT_SENSITIVITY:
		{
			struct sadb_sens *sadb_sens = (struct sadb_sens *)p;

			if (i < sizeof(struct sadb_sens)) {
				DPRINTF("bad header length for SENSITIVITY "
				    "extension header");
				return (EINVAL);
			}

			if (i != (sadb_sens->sadb_sens_sens_len +
			    sadb_sens->sadb_sens_integ_len) *
			    sizeof(uint64_t) +
			    sizeof(struct sadb_sens)) {
				DPRINTF("bad payload length for SENSITIVITY "
				    "extension header");
				return (EINVAL);
			}
		}
		break;
		case SADB_EXT_PROPOSAL:
		{
			struct sadb_prop *sadb_prop = (struct sadb_prop *)p;

			if (i < sizeof(struct sadb_prop)) {
				DPRINTF("bad PROPOSAL header length");
				return (EINVAL);
			}

			if (sadb_prop->sadb_prop_reserved) {
				DPRINTF("reserved fieldset in PROPOSAL "
				    "extension header");
				return (EINVAL);
			}

			if ((i - sizeof(struct sadb_prop)) %
			    sizeof(struct sadb_comb)) {
				DPRINTF("bad proposal length");
				return (EINVAL);
			}

			{
				struct sadb_comb *sadb_comb =
				    (struct sadb_comb *)(p +
					sizeof(struct sadb_prop));
				int j;

				for (j = 0;
				    j < (i - sizeof(struct sadb_prop))/
				    sizeof(struct sadb_comb);
				    j++) {
					if (sadb_comb->sadb_comb_auth >
					    SADB_AALG_MAX) {
						DPRINTF("unknown "
						    "authentication algorithm "
						    "%d in PROPOSAL",
						    sadb_comb->sadb_comb_auth);
						return (EINVAL);
					}

					if (sadb_comb->sadb_comb_encrypt >
					    SADB_EALG_MAX) {
						DPRINTF("unknown encryption "
						    "algorithm %d in PROPOSAL",
						    sadb_comb->
						    sadb_comb_encrypt);
						return (EINVAL);
					}

					if (sadb_comb->sadb_comb_reserved) {
						DPRINTF("reserved field set "
						    "in COMB header");
						return (EINVAL);
					}
				}
			}
		}
		break;
		case SADB_EXT_SUPPORTED_AUTH:
		case SADB_EXT_SUPPORTED_ENCRYPT:
		case SADB_X_EXT_SUPPORTED_COMP:
		{
			struct sadb_supported *sadb_supported =
			    (struct sadb_supported *)p;
			int j;

			if (i < sizeof(struct sadb_supported)) {
				DPRINTF("bad header length for SUPPORTED "					    "extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			if (sadb_supported->sadb_supported_reserved) {
				DPRINTF("reserved field set in SUPPORTED "
				    "extension header %d",
				    sadb_ext->sadb_ext_type);
				return (EINVAL);
			}

			{
				struct sadb_alg *sadb_alg =
				    (struct sadb_alg *)(p +
					sizeof(struct sadb_supported));
				int max_alg;

				max_alg = sadb_ext->sadb_ext_type ==
				    SADB_EXT_SUPPORTED_AUTH ?
				    SADB_AALG_MAX : SADB_EXT_SUPPORTED_ENCRYPT ?
				    SADB_EALG_MAX : SADB_X_CALG_MAX;

				for (j = 0;
				    j < sadb_supported->sadb_supported_len - 1;
				    j++) {
					if (sadb_alg->sadb_alg_id > max_alg) {
						DPRINTF("unknown algorithm %d "
						    "in SUPPORTED extension "
						    "header %d",
						    sadb_alg->sadb_alg_id,
						    sadb_ext->sadb_ext_type);
						return (EINVAL);
					}

					if (sadb_alg->sadb_alg_reserved) {
						DPRINTF("reserved field set "
						    "in supported algorithms "
						    "header inside SUPPORTED "
						    "extension header %d",
						    sadb_ext->sadb_ext_type);
						return (EINVAL);
					}

					sadb_alg++;
				}
			}
		}
		break;
		case SADB_EXT_SPIRANGE:
		{
			struct sadb_spirange *sadb_spirange =
			    (struct sadb_spirange *)p;

			if (i != sizeof(struct sadb_spirange)) {
				DPRINTF("bad header length of SPIRANGE "
				    "extension header");
				return (EINVAL);
			}

			if (sadb_spirange->sadb_spirange_min >
			    sadb_spirange->sadb_spirange_max) {
				DPRINTF("bad SPI range");
				return (EINVAL);
			}
		}
		break;
		case SADB_X_EXT_UDPENCAP:
			if (i != sizeof(struct sadb_x_udpencap)) {
				DPRINTF("bad UDPENCAP header length");
				return (EINVAL);
			}
			break;
		case SADB_X_EXT_RDOMAIN:
			if (i != sizeof(struct sadb_x_rdomain)) {
				DPRINTF("bad RDOMAIN header length");
				return (EINVAL);
			}
			break;
		case SADB_X_EXT_REPLAY:
			if (i != sizeof(struct sadb_x_replay)) {
				DPRINTF("bad REPLAY header length");
				return (EINVAL);
			}
			break;
		case SADB_X_EXT_COUNTER:
			if (i != sizeof(struct sadb_x_counter)) {
				DPRINTF("bad COUNTER header length");
				return (EINVAL);
			}
			break;

#if NPF > 0
		case SADB_X_EXT_TAG:
			if (i < sizeof(struct sadb_x_tag)) {
				DPRINTF("TAG extension header too small");
				return (EINVAL);
			}
			if (i > (sizeof(struct sadb_x_tag) +
			    PF_TAG_NAME_SIZE)) {
				DPRINTF("TAG extension header too long");
				return (EINVAL);
			}
			break;
		case SADB_X_EXT_TAP:
			if (i < sizeof(struct sadb_x_tap)) {
				DPRINTF("TAP extension header too small");
				return (EINVAL);
			}
			if (i > sizeof(struct sadb_x_tap)) {
				DPRINTF("TAP extension header too long");
				return (EINVAL);
			}
			break;
#endif
		case SADB_X_EXT_IFACE:
			if (i != sizeof(struct sadb_x_iface)) {
				DPRINTF("bad IFACE header length");
				return (EINVAL);
			}
			break;
		default:
			DPRINTF("unknown extension header type %d",
			    sadb_ext->sadb_ext_type);
			return (EINVAL);
		}

		headers[sadb_ext->sadb_ext_type] = p;
		p += i;
		left -= i;
	}

	if (left) {
		DPRINTF("message too long");
		return (EINVAL);
	}

	{
		uint64_t required;

		required = sadb_exts_required_in[sadb_msg->sadb_msg_type];

		if ((seen & required) != required) {
			DPRINTF("required fields missing");
			return (EINVAL);
		}
	}

	switch (((struct sadb_msg *)headers[0])->sadb_msg_type) {
	case SADB_UPDATE:
		if (((struct sadb_sa *)headers[SADB_EXT_SA])->sadb_sa_state !=
		    SADB_SASTATE_MATURE) {
			DPRINTF("updating non-mature SA prohibited");
			return (EINVAL);
		}
		break;
	case SADB_ADD:
		if (((struct sadb_sa *)headers[SADB_EXT_SA])->sadb_sa_state !=
		    SADB_SASTATE_MATURE) {
			DPRINTF("adding non-mature SA prohibited");
			return (EINVAL);
		}
		break;
	}

	return (0);
}
