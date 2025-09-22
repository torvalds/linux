/*	$OpenBSD: radius.c,v 1.14 2025/06/24 00:05:42 yasuoka Exp $	*/

/*
 * Copyright (c) 2024 Internet Initiative Japan Inc.
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
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/ip_ipsp.h>

#include <endian.h>
#include <event.h>
#include <errno.h>
#include <imsg.h>
#include <limits.h>
#include <netinet/in.h>
#include <radius.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "iked.h"
#include "eap.h"
#include "ikev2.h"
#include "types.h"

void	 iked_radius_request_send(struct iked *, void *);
void	 iked_radius_fill_attributes(struct iked_sa *, RADIUS_PACKET *);
void	 iked_radius_config(struct iked_radserver_req *, const RADIUS_PACKET *,
	    int, uint32_t, uint8_t);
void	 iked_radius_acct_request(struct iked *, struct iked_sa *, uint8_t);

const struct iked_radcfgmap radius_cfgmaps[] = {
    { IKEV2_CFG_INTERNAL_IP4_ADDRESS, 0, RADIUS_TYPE_FRAMED_IP_ADDRESS },
    { IKEV2_CFG_INTERNAL_IP4_NETMASK, 0, RADIUS_TYPE_FRAMED_IP_NETMASK },
    { IKEV2_CFG_INTERNAL_IP4_DNS, RADIUS_VENDOR_MICROSOFT,
	RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER },
    { IKEV2_CFG_INTERNAL_IP4_DNS, RADIUS_VENDOR_MICROSOFT,
	RADIUS_VTYPE_MS_SECONDARY_DNS_SERVER },
    { IKEV2_CFG_INTERNAL_IP4_NBNS, RADIUS_VENDOR_MICROSOFT,
	RADIUS_VTYPE_MS_PRIMARY_NBNS_SERVER },
    { IKEV2_CFG_INTERNAL_IP4_NBNS, RADIUS_VENDOR_MICROSOFT,
	RADIUS_VTYPE_MS_SECONDARY_NBNS_SERVER },
    { 0 }
};

int
iked_radius_request(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg)
{
	struct eap_message		*eap;
	RADIUS_PACKET			*pkt;
	size_t				 len;

	eap = ibuf_data(msg->msg_eapmsg);
	len = betoh16(eap->eap_length);
	if (eap->eap_code != EAP_CODE_RESPONSE) {
		log_debug("%s: eap_code is not response %u", __func__,
		    (unsigned)eap->eap_code);
		return -1;
	}

	if (eap->eap_type == EAP_TYPE_IDENTITY) {
		if ((sa->sa_radreq = calloc(1,
		    sizeof(struct iked_radserver_req))) == NULL) {
			log_debug(
			    "%s: calloc failed for iked_radserver_req: %s",
			    __func__, strerror(errno));
			return (-1);
		}
		timer_set(env, &sa->sa_radreq->rr_timer,
		    iked_radius_request_send, sa->sa_radreq);
		sa->sa_radreq->rr_user = strdup(msg->msg_eap.eam_identity);
	}

	if ((pkt = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST))
	    == NULL) {
		log_debug("%s: radius_new_request_packet failed %s", __func__,
		    strerror(errno));
		return -1;
	}

	radius_put_string_attr(pkt, RADIUS_TYPE_USER_NAME,
	    sa->sa_radreq->rr_user);
	if (sa->sa_radreq->rr_state != NULL)
		radius_put_raw_attr(pkt, RADIUS_TYPE_STATE,
		    ibuf_data(sa->sa_radreq->rr_state),
		    ibuf_size(sa->sa_radreq->rr_state));

	if (radius_put_raw_attr_cat(pkt, RADIUS_TYPE_EAP_MESSAGE,
	    (uint8_t *)eap, len) == -1) {
		log_debug("%s: radius_put_raw_attr_cat failed %s", __func__,
		    strerror(errno));
		return -1;
	}

	iked_radius_fill_attributes(sa, pkt);

	/* save the request, it'll be needed for message authentication */
	if (sa->sa_radreq->rr_reqpkt != NULL)
		radius_delete_packet(sa->sa_radreq->rr_reqpkt);
	sa->sa_radreq->rr_reqpkt = pkt;
	sa->sa_radreq->rr_sa = sa;
	sa->sa_radreq->rr_ntry = 0;

	iked_radius_request_send(env, sa->sa_radreq);

	return 0;
}

void
iked_radius_request_free(struct iked *env, struct iked_radserver_req *req)
{
	if (req == NULL)
		return;
	timer_del(env, &req->rr_timer);
	free(req->rr_user);
	ibuf_free(req->rr_state);
	if (req->rr_reqpkt)
		radius_delete_packet(req->rr_reqpkt);
	if (req->rr_sa)
		req->rr_sa->sa_radreq = NULL;
	if (req->rr_server)
		TAILQ_REMOVE(&req->rr_server->rs_reqs, req, rr_entry);
	free(req);
}

void
iked_radius_on_event(int fd, short ev, void *ctx)
{
	struct iked			*env;
	struct iked_radserver		*server = ctx;
	struct iked_radserver_req	*req;
	const struct iked_radcfgmap	*cfgmap;
	RADIUS_PACKET			*pkt;
	int				 i, resid;
	struct ibuf			*e;
	const void			*attrval;
	size_t				 attrlen;
	uint8_t				 code;
	char				 username[256];
	u_char				 eapmsk[128];
	/* RFC 3748 defines the MSK minimum size is 64 bytes */
	size_t				 eapmsksiz = sizeof(eapmsk);

	env = server->rs_env;
	pkt = radius_recv(server->rs_sock, 0);
	if (pkt == NULL) {
		log_info("%s: receiving a RADIUS message failed: %s", __func__,
		    strerror(errno));
		return;
	}
	resid = radius_get_id(pkt);

	TAILQ_FOREACH(req, &server->rs_reqs, rr_entry) {
		if (req->rr_reqid == resid)
			break;
	}
	if (req == NULL) {
		log_debug("%s: received an unknown RADIUS message: id=%u",
		    __func__, (unsigned)resid);
		radius_delete_packet(pkt);
		return;
	}

	radius_set_request_packet(pkt, req->rr_reqpkt);
	if (radius_check_response_authenticator(pkt, server->rs_secret) != 0) {
		log_info("%s: received an invalid RADIUS message: bad "
		    "response authenticator", __func__);
		radius_delete_packet(pkt);
		return;
	}
	if (req->rr_accounting) {
		/* accounting */
		code = radius_get_code(pkt);
		switch (code) {
		case RADIUS_CODE_ACCOUNTING_RESPONSE: /* Expected */
			break;
		default:
			log_info("%s: received an invalid RADIUS message: "
			    "code %u", __func__, (unsigned)code);
		}
		radius_delete_packet(pkt);
		iked_radius_request_free(env, req);
		return;
	}

	/* authentication */
	if (radius_check_message_authenticator(pkt, server->rs_secret) != 0) {
		log_info("%s: received an invalid RADIUS message: bad "
		    "message authenticator", __func__);
		radius_delete_packet(pkt);
		return;
	}

	timer_del(env, &req->rr_timer);
	req->rr_ntry = 0;

	if (req->rr_sa == NULL)
		goto fail;

	code = radius_get_code(pkt);
	switch (code) {
	case RADIUS_CODE_ACCESS_CHALLENGE:
		if (radius_get_raw_attr_ptr(pkt, RADIUS_TYPE_STATE, &attrval,
		    &attrlen) != 0) {
			log_info("%s: received an invalid RADIUS message: no "
			    "state attribute", __func__);
			goto fail;
		}
		if (req->rr_state != NULL &&
		    ibuf_set(req->rr_state, 0, attrval, attrlen) != 0) {
			ibuf_free(req->rr_state);
			req->rr_state = NULL;
		}
		if (req->rr_state == NULL &&
		    (req->rr_state = ibuf_new(attrval, attrlen)) == NULL) {
			log_info("%s: ibuf_new() failed: %s", __func__,
			    strerror(errno));
			goto fail;
		}
		break;
	case RADIUS_CODE_ACCESS_ACCEPT:
		log_info("%s: received Access-Accept for %s",
		    SPI_SA(req->rr_sa, __func__), req->rr_user);
		/* Try to retrieve the EAP MSK from the RADIUS response */
		if (radius_get_eap_msk(pkt, eapmsk, &eapmsksiz,
		    server->rs_secret) == 0) {
			ibuf_free(req->rr_sa->sa_eapmsk);
			if ((req->rr_sa->sa_eapmsk = ibuf_new(eapmsk,
			    eapmsksiz)) == NULL) {
				log_info("%s: ibuf_new() failed: %s", __func__,
				    strerror(errno));
				goto fail;
			}
		} else
			log_debug("Could not retrieve the EAP MSK from the "
			    "RADIUS message");

		free(req->rr_sa->sa_eapid);
		/* The EAP identity might be protected (RFC 3748 7.3) */
		if (radius_get_string_attr(pkt, RADIUS_TYPE_USER_NAME,
		    username, sizeof(username)) == 0 &&
		    strcmp(username, req->rr_user) != 0) {
			/*
			 * The Access-Accept might have a User-Name.  It
			 * should be used for Accounting (RFC 2865 5.1).
			 */
			free(req->rr_user);
			req->rr_sa->sa_eapid = strdup(username);
		} else
			req->rr_sa->sa_eapid = req->rr_user;
		req->rr_user = NULL;

		if (radius_get_raw_attr_ptr(pkt, RADIUS_TYPE_CLASS, &attrval,
		    &attrlen) == 0) {
			ibuf_free(req->rr_sa->sa_eapclass);
			if ((req->rr_sa->sa_eapclass = ibuf_new(attrval,
			    attrlen)) == NULL) {
				log_info("%s: ibuf_new() failed: %s", __func__,
				    strerror(errno));
			}
		}

		sa_state(env, req->rr_sa, IKEV2_STATE_AUTH_SUCCESS);

		/* Map RADIUS attributes to cp */
		if (TAILQ_EMPTY(&env->sc_radcfgmaps)) {
			for (i = 0; radius_cfgmaps[i].cfg_type != 0; i++) {
				cfgmap = &radius_cfgmaps[i];
				iked_radius_config(req, pkt, cfgmap->cfg_type,
				    cfgmap->vendor_id, cfgmap->attr_type);
			}
		} else {
			TAILQ_FOREACH(cfgmap, &env->sc_radcfgmaps, entry)
				iked_radius_config(req, pkt, cfgmap->cfg_type,
				    cfgmap->vendor_id, cfgmap->attr_type);
		}

		TAILQ_REMOVE(&server->rs_reqs, req, rr_entry);
		req->rr_server = NULL;
		break;
	case RADIUS_CODE_ACCESS_REJECT:
		log_info("%s: received Access-Reject for %s",
		    SPI_SA(req->rr_sa, __func__), req->rr_user);
		TAILQ_REMOVE(&server->rs_reqs, req, rr_entry);
		req->rr_server = NULL;
		break;
	default:
		log_debug("%s: received an invalid RADIUS message: code %u",
		    __func__, (unsigned)code);
		break;
	}

	/* get the length first */
	if (radius_get_raw_attr_cat(pkt, RADIUS_TYPE_EAP_MESSAGE, NULL,
	    &attrlen) != 0) {
		log_info("%s: failed to retrieve the EAP message", __func__);
		goto fail;
	}
	/* allocate a buffer */
	if ((e = ibuf_new(NULL, attrlen)) == NULL) {
		log_info("%s: ibuf_new() failed: %s", __func__,
		    strerror(errno));
		goto fail;
	}
	/* copy the message to the buffer */
	if (radius_get_raw_attr_cat(pkt, RADIUS_TYPE_EAP_MESSAGE,
	    ibuf_data(e), &attrlen) != 0) {
		ibuf_free(e);
		log_info("%s: failed to retrieve the EAP message", __func__);
		goto fail;
	}
	radius_delete_packet(pkt);
	ikev2_send_ike_e(env, req->rr_sa, e, IKEV2_PAYLOAD_EAP,
	    IKEV2_EXCHANGE_IKE_AUTH, 1);
	ibuf_free(e);
	/* keep request for challenge state and config parameters */
	req->rr_reqid = -1;	/* release reqid */
	return;
 fail:
	radius_delete_packet(pkt);
	if (req->rr_server != NULL)
		TAILQ_REMOVE(&server->rs_reqs, req, rr_entry);
	req->rr_server = NULL;
	if (req->rr_sa != NULL) {
		ikev2_ike_sa_setreason(req->rr_sa, "RADIUS request failed");
		sa_free(env, req->rr_sa);
	}
}

void
iked_radius_request_send(struct iked *env, void *ctx)
{
	struct iked_radserver_req	*req = ctx, *req0;
	struct iked_radserver		*server = req->rr_server;
	const int			 timeouts[] = { 2, 4, 8 };
	uint8_t				 seq;
	int				 i, max_tries, max_failovers;
	struct sockaddr_storage		 ss;
	socklen_t			 sslen;
	struct iked_radservers		*radservers;
	struct timespec			 now;

	if (!req->rr_accounting) {
		max_tries = env->sc_radauth.max_tries;
		max_failovers = env->sc_radauth.max_failovers;
		radservers = &env->sc_radauthservers;
	} else {
		max_tries = env->sc_radacct.max_tries;
		max_failovers = env->sc_radacct.max_failovers;
		radservers = &env->sc_radacctservers;
	}

	if (req->rr_ntry > max_tries) {
		req->rr_ntry = 0;
		log_info("%s: RADIUS server %s failed", __func__,
		    print_addr(&server->rs_sockaddr));
 next_server:
		TAILQ_REMOVE(&server->rs_reqs, req, rr_entry);
		req->rr_server = NULL;
		if (req->rr_nfailover >= max_failovers ||
		    TAILQ_NEXT(server, rs_entry) == NULL) {
			log_info("%s: No more RADIUS server", __func__);
			goto fail;
		} else if (req->rr_state != NULL) {
			log_info("%s: Can't change RADIUS server: "
			    "client has a state already", __func__);
			goto fail;
		} else {
			TAILQ_REMOVE(radservers, server, rs_entry);
			TAILQ_INSERT_TAIL(radservers, server, rs_entry);
			server = TAILQ_FIRST(radservers);
			log_info("%s: RADIUS server %s is active",
			    __func__, print_addr(&server->rs_sockaddr));
		}
		req->rr_nfailover++;
	}

	if (req->rr_server != NULL &&
	    req->rr_server != TAILQ_FIRST(radservers)) {
		/* Current server is marked fail */
		if (req->rr_state != NULL || req->rr_nfailover >= max_failovers)
			goto fail; /* can't fail over */
		TAILQ_REMOVE(&server->rs_reqs, req, rr_entry);
		req->rr_server = NULL;
		req->rr_nfailover++;
	}

	if (req->rr_server == NULL) {
		/* Select a new server */
		server = TAILQ_FIRST(radservers);
		if (server == NULL) {
			log_info("%s: No RADIUS server is configured",
			    __func__);
			goto fail;
		}
		TAILQ_INSERT_TAIL(&server->rs_reqs, req, rr_entry);
		req->rr_server = server;

		/* Prepare NAS-IP-Address */
		if (server->rs_nas_ipv4.s_addr == INADDR_ANY &&
		    IN6_IS_ADDR_UNSPECIFIED(&server->rs_nas_ipv6)) {
			sslen = sizeof(ss);
			if (getsockname(server->rs_sock, (struct sockaddr *)&ss,
			    &sslen) == 0) {
				if (ss.ss_family == AF_INET)
					server->rs_nas_ipv4 =
					    ((struct sockaddr_in *)&ss)
					    ->sin_addr;
				else
					server->rs_nas_ipv6 =
					    ((struct sockaddr_in6 *)&ss)
					    ->sin6_addr;
			}
		}
	}
	if (req->rr_ntry == 0) {
		/* decide the ID */
		seq = ++server->rs_reqseq;
		for (i = 0; i <= UCHAR_MAX; i++) {
			TAILQ_FOREACH(req0, &server->rs_reqs, rr_entry) {
				if (req0->rr_reqid == -1)
					continue;
				if (req0->rr_reqid == seq)
					break;
			}
			if (req0 == NULL)
				break;
			seq++;
		}
		if (i > UCHAR_MAX) {
			log_info("%s: RADIUS server %s failed.  Too many "
			    "pending requests", __func__,
			    print_addr(&server->rs_sockaddr));
			if (TAILQ_NEXT(server, rs_entry) != NULL)
				goto next_server;
			goto fail;
		}
		req->rr_reqid = seq;
		radius_set_id(req->rr_reqpkt, req->rr_reqid);
	}

	if (server->rs_nas_ipv4.s_addr != INADDR_ANY)
		radius_put_ipv4_attr(req->rr_reqpkt, RADIUS_TYPE_NAS_IP_ADDRESS,
		    server->rs_nas_ipv4);
	else if (!IN6_IS_ADDR_UNSPECIFIED(&server->rs_nas_ipv6))
		radius_put_ipv6_attr(req->rr_reqpkt,
		    RADIUS_TYPE_NAS_IPV6_ADDRESS, &server->rs_nas_ipv6);
	/* Identifier */
	radius_put_string_attr(req->rr_reqpkt, RADIUS_TYPE_NAS_IDENTIFIER,
	    IKED_NAS_ID);

	if (req->rr_accounting) {
		if (req->rr_ntry == 0 && req->rr_nfailover == 0)
			radius_put_uint32_attr(req->rr_reqpkt,
			    RADIUS_TYPE_ACCT_DELAY_TIME, 0);
		else {
			clock_gettime(CLOCK_MONOTONIC, &now);
			timespecsub(&now, &req->rr_accttime, &now);
			radius_put_uint32_attr(req->rr_reqpkt,
			    RADIUS_TYPE_ACCT_DELAY_TIME, now.tv_sec);
		}
		radius_set_accounting_request_authenticator(req->rr_reqpkt,
		    server->rs_secret);
	} else {
		radius_put_message_authenticator(req->rr_reqpkt,
		    server->rs_secret);
	}

	if (radius_send(server->rs_sock, req->rr_reqpkt, 0) < 0)
		log_info("%s: sending a RADIUS message failed: %s", __func__,
		    strerror(errno));

	if (req->rr_ntry >= (int)nitems(timeouts))
		timer_add(env, &req->rr_timer, timeouts[nitems(timeouts) - 1]);
	else
		timer_add(env, &req->rr_timer, timeouts[req->rr_ntry]);
	req->rr_ntry++;
	return;
 fail:
	if (req->rr_server != NULL)
		TAILQ_REMOVE(&server->rs_reqs, req, rr_entry);
	req->rr_server = NULL;
	if (req->rr_sa != NULL) {
		ikev2_ike_sa_setreason(req->rr_sa, "RADIUS request failed");
		sa_free(env, req->rr_sa);
	}
}

void
iked_radius_fill_attributes(struct iked_sa *sa, RADIUS_PACKET *pkt)
{
	char	port_id[16 + 1];

	/* NAS Port Type = Virtual */
	radius_put_uint32_attr(pkt,
	    RADIUS_TYPE_NAS_PORT_TYPE, RADIUS_NAS_PORT_TYPE_VIRTUAL);
	/* Service Type =  Framed */
	radius_put_uint32_attr(pkt, RADIUS_TYPE_SERVICE_TYPE,
	    RADIUS_SERVICE_TYPE_FRAMED);
	/* Tunnel Type = EAP */
	radius_put_uint32_attr(pkt, RADIUS_TYPE_TUNNEL_TYPE,
	    RADIUS_TUNNEL_TYPE_ESP);
	/* NAS-Port-ID = ISPI */
	snprintf(port_id, sizeof(port_id), "%016llx",
	    (unsigned long long)sa->sa_hdr.sh_ispi);
	radius_put_string_attr(pkt, RADIUS_TYPE_NAS_PORT_ID, port_id);

	radius_put_string_attr(pkt, RADIUS_TYPE_CALLED_STATION_ID,
	    print_addr(&sa->sa_local.addr));
	radius_put_string_attr(pkt, RADIUS_TYPE_CALLING_STATION_ID,
	    print_addr(&sa->sa_peer.addr));
}

void
iked_radius_config(struct iked_radserver_req *req, const RADIUS_PACKET *pkt,
    int cfg_type, uint32_t vendor_id, uint8_t attr_type)
{
	unsigned int		 i;
	struct iked_sa		*sa = req->rr_sa;
	struct in_addr		 ia4;
	struct in6_addr		 ia6;
	struct sockaddr_in	*sin4;
	struct sockaddr_in6	*sin6;
	struct iked_addr	*addr;
	struct iked_cfg		*ikecfg;

	for (i = 0; i < sa->sa_policy->pol_ncfg; i++) {
		ikecfg = &sa->sa_policy->pol_cfg[i];
		if (ikecfg->cfg_type == cfg_type &&
		    ikecfg->cfg_type != IKEV2_CFG_INTERNAL_IP4_ADDRESS)
			return;	/* use config rather than radius */
	}
	switch (cfg_type) {
	case IKEV2_CFG_INTERNAL_IP4_ADDRESS:
	case IKEV2_CFG_INTERNAL_IP4_NETMASK:
	case IKEV2_CFG_INTERNAL_IP4_DNS:
	case IKEV2_CFG_INTERNAL_IP4_NBNS:
	case IKEV2_CFG_INTERNAL_IP4_DHCP:
	case IKEV2_CFG_INTERNAL_IP4_SERVER:
		if (vendor_id == 0 && radius_has_attr(pkt, attr_type))
			radius_get_ipv4_attr(pkt, attr_type, &ia4);
		else if (vendor_id != 0 && radius_has_vs_attr(pkt, vendor_id,
		    attr_type))
			radius_get_vs_ipv4_attr(pkt, vendor_id, attr_type,
			    &ia4);
		else
			break; /* no attribute contained */

		if (cfg_type == IKEV2_CFG_INTERNAL_IP4_NETMASK) {
			/*
			 * This assumes IKEV2_CFG_INTERNAL_IP4_ADDRESS is
			 * called before IKEV2_CFG_INTERNAL_IP4_NETMASK
			 */
			if (sa->sa_rad_addr == NULL) {
				/*
				 * RFC 7296, IKEV2_CFG_INTERNAL_IP4_NETMASK
				 * must be used with
				 * IKEV2_CFG_INTERNAL_IP4_ADDRESS
				 */
				break;
			}
			if (ia4.s_addr == 0) {
				log_debug("%s: netmask is wrong", __func__);
				break;
			}
			if (ia4.s_addr == htonl(0))
				sa->sa_rad_addr->addr_mask = 0;
			else
				sa->sa_rad_addr->addr_mask =
				    33 - ffs(ntohl(ia4.s_addr));
			if (sa->sa_rad_addr->addr_mask < 32)
				sa->sa_rad_addr->addr_net = 1;
		}
		if (cfg_type == IKEV2_CFG_INTERNAL_IP4_ADDRESS) {
			if ((addr = calloc(1, sizeof(*addr))) == NULL) {
				log_warn("%s: calloc", __func__);
				return;
			}
			sa->sa_rad_addr = addr;
		} else {
			req->rr_cfg[req->rr_ncfg].cfg_action = IKEV2_CP_REPLY;
			req->rr_cfg[req->rr_ncfg].cfg_type = cfg_type;
			addr = &req->rr_cfg[req->rr_ncfg].cfg.address;
			req->rr_ncfg++;
		}
		addr->addr_af = AF_INET;
		sin4 = (struct sockaddr_in *)&addr->addr;
		sin4->sin_family = AF_INET;
		sin4->sin_len = sizeof(struct sockaddr_in);
		sin4->sin_addr = ia4;
		break;
	case IKEV2_CFG_INTERNAL_IP6_ADDRESS:
	case IKEV2_CFG_INTERNAL_IP6_DNS:
	case IKEV2_CFG_INTERNAL_IP6_NBNS:
	case IKEV2_CFG_INTERNAL_IP6_DHCP:
	case IKEV2_CFG_INTERNAL_IP6_SERVER:
		if (vendor_id == 0 && radius_has_attr(pkt, attr_type))
			radius_get_ipv6_attr(pkt, attr_type, &ia6);
		else if (vendor_id != 0 && radius_has_vs_attr(pkt, vendor_id,
		    attr_type))
			radius_get_vs_ipv6_attr(pkt, vendor_id, attr_type,
			    &ia6);
		else
			break; /* no attribute contained */

		if (cfg_type == IKEV2_CFG_INTERNAL_IP6_ADDRESS) {
			if ((addr = calloc(1, sizeof(*addr))) == NULL) {
				log_warn("%s: calloc", __func__);
				return;
			}
			sa->sa_rad_addr = addr;
		} else {
			req->rr_cfg[req->rr_ncfg].cfg_action = IKEV2_CP_REPLY;
			req->rr_cfg[req->rr_ncfg].cfg_type = cfg_type;
			addr = &req->rr_cfg[req->rr_ncfg].cfg.address;
			req->rr_ncfg++;
		}
		addr->addr_af = AF_INET;
		sin6 = (struct sockaddr_in6 *)&addr->addr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_addr = ia6;
		break;
	}
	return;
}

void
iked_radius_acct_on(struct iked *env)
{
	if (TAILQ_EMPTY(&env->sc_radacctservers))
		return;
	if (env->sc_radaccton == 0) {	/* trigger once */
		iked_radius_acct_request(env, NULL,
		    RADIUS_ACCT_STATUS_TYPE_ACCT_ON);
		env->sc_radaccton = 1;
	}
}

void
iked_radius_acct_off(struct iked *env)
{
	iked_radius_acct_request(env, NULL, RADIUS_ACCT_STATUS_TYPE_ACCT_OFF);
}

void
iked_radius_acct_start(struct iked *env, struct iked_sa *sa)
{
	iked_radius_acct_request(env, sa, RADIUS_ACCT_STATUS_TYPE_START);
}

void
iked_radius_acct_stop(struct iked *env, struct iked_sa *sa)
{
	iked_radius_acct_request(env, sa, RADIUS_ACCT_STATUS_TYPE_STOP);
}

void
iked_radius_acct_request(struct iked *env, struct iked_sa *sa, uint8_t stype)
{
	struct iked_radserver_req	*req;
	RADIUS_PACKET			*pkt;
	struct iked_addr		*addr4 = NULL;
	struct iked_addr		*addr6 = NULL;
	struct in_addr			 mask4;
	char				 sa_id[IKED_ID_SIZE];
	char				 sid[16 + 1];
	struct timespec			 now;
	int				 cause;

	if (TAILQ_EMPTY(&env->sc_radacctservers))
		return;
	/*
	 * In RFC2866 5.6, "Users who are delivered service without
	 * being authenticated SHOULD NOT generate Accounting records
	 */
	if (sa != NULL && sa->sa_eapid == NULL) {
		/* fallback to IKEID for accounting */
		if (ikev2_print_id(IKESA_DSTID(sa), sa_id, sizeof(sa_id)) != -1)
			sa->sa_eapid = strdup(sa_id);
		if (sa->sa_eapid == NULL)
			return;
	}

	if ((req = calloc(1, sizeof(struct iked_radserver_req))) == NULL) {
		log_debug("%s: calloc faile for iked_radserver_req: %s",
		    __func__, strerror(errno));
		return;
	}
	req->rr_accounting = 1;
	clock_gettime(CLOCK_MONOTONIC, &now);
	req->rr_accttime = now;
	timer_set(env, &req->rr_timer, iked_radius_request_send, req);

	if ((pkt = radius_new_request_packet(RADIUS_CODE_ACCOUNTING_REQUEST))
	    == NULL) {
		log_debug("%s: radius_new_request_packet failed %s", __func__,
		    strerror(errno));
		return;
	}

	/* RFC 2866  5.1. Acct-Status-Type */
	radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_STATUS_TYPE, stype);

	if (sa == NULL) {
		/* ASSERT(stype == RADIUS_ACCT_STATUS_TYPE_ACCT_ON ||
		    stype == RADIUS_ACCT_STATUS_TYPE_ACCT_OFF) */
		req->rr_reqpkt = pkt;
		req->rr_ntry = 0;
		iked_radius_request_send(env, req);
		return;
	}

	iked_radius_fill_attributes(sa, pkt);

	radius_put_string_attr(pkt, RADIUS_TYPE_USER_NAME, sa->sa_eapid);

	/* RFC 2866  5.5. Acct-Session-Id */
	snprintf(sid, sizeof(sid), "%016llx",
	    (unsigned long long)sa->sa_hdr.sh_ispi);
	radius_put_string_attr(pkt, RADIUS_TYPE_ACCT_SESSION_ID, sid);

	/* Accounting Request must have Framed-IP-Address */
	addr4 = sa->sa_addrpool;
	if (addr4 != NULL) {
		radius_put_ipv4_attr(pkt, RADIUS_TYPE_FRAMED_IP_ADDRESS,
		    ((struct sockaddr_in *)&addr4->addr)->sin_addr);
		if (addr4->addr_mask != 0) {
			mask4.s_addr = htonl(
			    0xFFFFFFFFUL << (32 - addr4->addr_mask));
			radius_put_ipv4_attr(pkt,
			    RADIUS_TYPE_FRAMED_IP_NETMASK, mask4);
		}
	}
	addr6 = sa->sa_addrpool6;
	if (addr6 != NULL)
		radius_put_ipv6_attr(pkt, RADIUS_TYPE_FRAMED_IPV6_ADDRESS,
		    &((struct sockaddr_in6 *)&addr6->addr)->sin6_addr);

	/* RFC2866 5.6 Acct-Authentic */
	radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_AUTHENTIC,
	    (sa->sa_radreq != NULL)? RADIUS_ACCT_AUTHENTIC_RADIUS :
	    RADIUS_ACCT_AUTHENTIC_LOCAL);

	switch (stype) {
	case RADIUS_ACCT_STATUS_TYPE_START:
		if (req->rr_sa && req->rr_sa->sa_eapclass != NULL)
			radius_put_raw_attr(pkt, RADIUS_TYPE_CLASS,
			    ibuf_data(req->rr_sa->sa_eapclass),
			    ibuf_size(req->rr_sa->sa_eapclass));
		break;
	case RADIUS_ACCT_STATUS_TYPE_INTERIM_UPDATE:
	case RADIUS_ACCT_STATUS_TYPE_STOP:
		/* RFC 2866 5.7.  Acct-Session-Time */
		timespecsub(&now, &sa->sa_starttime, &now);
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_SESSION_TIME,
		    now.tv_sec);
		/* RFC 2866 5.10 Acct-Terminate-Cause */
		cause = RADIUS_TERMNATE_CAUSE_SERVICE_UNAVAIL;
		if (sa->sa_reason) {
			if (strcmp(sa->sa_reason, "received delete") == 0) {
				cause = RADIUS_TERMNATE_CAUSE_USER_REQUEST;
			} else if (strcmp(sa->sa_reason, "SA rekeyed") == 0) {
				cause = RADIUS_TERMNATE_CAUSE_SESSION_TIMEOUT;
			} else if (strncmp(sa->sa_reason, "retransmit",
			    strlen("retransmit")) == 0) {
				cause = RADIUS_TERMNATE_CAUSE_LOST_SERVICE;
			} else if (strcmp(sa->sa_reason,
			    "disconnect requested") == 0) {
				cause = RADIUS_TERMNATE_CAUSE_ADMIN_RESET;
			}
		}
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_TERMINATE_CAUSE,
		    cause);
		/* I/O statistics {Input,Output}-{Packets,Octets,Gigawords} */
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_INPUT_PACKETS,
		    sa->sa_stats.sas_ipackets);
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_OUTPUT_PACKETS,
		    sa->sa_stats.sas_opackets);
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_INPUT_OCTETS,
		    sa->sa_stats.sas_ibytes & 0xffffffffUL);
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_OUTPUT_OCTETS,
		    sa->sa_stats.sas_obytes & 0xffffffffUL);
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_INPUT_GIGAWORDS,
		    sa->sa_stats.sas_ibytes >> 32);
		radius_put_uint32_attr(pkt, RADIUS_TYPE_ACCT_OUTPUT_GIGAWORDS,
		    sa->sa_stats.sas_obytes >> 32);
		break;
	}
	req->rr_reqpkt = pkt;
	req->rr_ntry = 0;
	iked_radius_request_send(env, req);
}

void
iked_radius_dae_on_event(int fd, short ev, void *ctx)
{
	struct iked_raddae	*dae = ctx;
	struct iked		*env = dae->rd_env;
	RADIUS_PACKET		*req = NULL, *res = NULL;
	struct sockaddr_storage	 ss;
	socklen_t		 sslen;
	struct iked_radclient	*client;
	struct iked_sa		*sa = NULL;
	char			 attr[256], username[256];
	char			*endp, *reason, *nakcause = NULL;
	int			 code, n = 0;
	uint64_t		 ispi = 0;
	uint32_t		 u32, cause = 0;
	struct iked_addr	*addr4 = NULL;

	reason = "disconnect requested";

	sslen = sizeof(ss);
	req = radius_recvfrom(dae->rd_sock, 0, (struct sockaddr *)&ss, &sslen);
	if (req == NULL) {
		log_warn("%s: receiving a RADIUS message failed: %s", __func__,
		    strerror(errno));
		return;
	}
	TAILQ_FOREACH(client, &env->sc_raddaeclients, rc_entry) {
		if (sockaddr_cmp((struct sockaddr *)&client->rc_sockaddr,
		    (struct sockaddr *)&ss, -1) == 0)
			break;
	}
	if (client == NULL) {
		log_warnx("%s: received RADIUS message from %s: "
		    "unknown client", __func__, print_addr(&ss));
		goto out;
	}

	if (radius_check_accounting_request_authenticator(req,
	    client->rc_secret) != 0) {
		log_warnx("%s: received an invalid RADIUS message from %s: bad "
		    "response authenticator", __func__, print_addr(&ss));
		goto out;
	}

	if ((code = radius_get_code(req)) != RADIUS_CODE_DISCONNECT_REQUEST) {
		/* Code other than Disconnect-Request is not supported */
		if (code == RADIUS_CODE_COA_REQUEST) {
			code = RADIUS_CODE_COA_NAK;
			cause = RADIUS_ERROR_CAUSE_ADMINISTRATIVELY_PROHIBITED;
			nakcause = "Coa-Request is not supported";
			goto send;
		}
		log_warnx("%s: received an invalid RADIUS message "
		    "from %s: unknown code %d", __func__,
		    print_addr(&ss), code);
		goto out;
	}

	log_info("received Disconnect-Request from %s", print_addr(&ss));

	if (radius_get_string_attr(req, RADIUS_TYPE_NAS_IDENTIFIER, attr,
	    sizeof(attr)) == 0 && strcmp(attr, IKED_NAS_ID) != 0) {
		cause = RADIUS_ERROR_CAUSE_NAS_IDENTIFICATION_MISMATCH;
		nakcause = "NAS-Identifier is not matched";
		goto search_done;
	}

	/* prepare User-Name attribute */
	memset(username, 0, sizeof(username));
	radius_get_string_attr(req, RADIUS_TYPE_USER_NAME, username,
	    sizeof(username));

	if (radius_get_string_attr(req, RADIUS_TYPE_ACCT_SESSION_ID, attr,
	    sizeof(attr)) == 0) {
		/* the client is to disconnect a session */
		ispi = strtoull(attr, &endp, 16);
		if (attr[0] == '\0' || *endp != '\0' || errno == ERANGE ||
		    ispi == ULLONG_MAX) {
			cause = RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE;
			nakcause = "Session-Id is wrong";
			goto search_done;

		}
		RB_FOREACH(sa, iked_sas, &env->sc_sas) {
			if (sa->sa_hdr.sh_ispi == ispi)
				break;
		}
		if (sa == NULL)
			goto search_done;
		if (username[0] != '\0' && (sa->sa_eapid == NULL ||
		    strcmp(username, sa->sa_eapid) != 0)) {
			/* specified User-Name attribute is mismatched */
			cause = RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE;
			nakcause = "User-Name is not matched";
			goto search_done;
		}
		ikev2_ike_sa_setreason(sa, reason);
		ikev2_ike_sa_delete(env, sa);
		n++;
	} else if (username[0] != '\0') {
		RB_FOREACH(sa, iked_sas, &env->sc_sas) {
			if (sa->sa_eapid != NULL &&
			    strcmp(sa->sa_eapid, username) == 0) {
				ikev2_ike_sa_setreason(sa, reason);
				ikev2_ike_sa_delete(env, sa);
				n++;
			}
		}
	} else if (radius_get_uint32_attr(req, RADIUS_TYPE_FRAMED_IP_ADDRESS,
	    &u32) == 0) {
		RB_FOREACH(sa, iked_sas, &env->sc_sas) {
			addr4 = sa->sa_addrpool;
			if (addr4 != NULL) {
				if (u32 == ((struct sockaddr_in *)&addr4->addr)
				    ->sin_addr.s_addr) {
					ikev2_ike_sa_setreason(sa, reason);
					ikev2_ike_sa_delete(env, sa);
					n++;
				}
			}
		}
	}
 search_done:
	if (n > 0)
		code = RADIUS_CODE_DISCONNECT_ACK;
	else {
		if (nakcause == NULL)
			nakcause = "session not found";
		if (cause == 0)
			cause = RADIUS_ERROR_CAUSE_SESSION_NOT_FOUND;
		code = RADIUS_CODE_DISCONNECT_NAK;
	}
 send:
	res = radius_new_response_packet(code, req);
	if (res == NULL) {
		log_warn("%s: radius_new_response_packet", __func__);
		goto out;
	}
	if (cause != 0)
		radius_put_uint32_attr(res, RADIUS_TYPE_ERROR_CAUSE, cause);
	radius_set_response_authenticator(res, client->rc_secret);
	if (radius_sendto(dae->rd_sock, res, 0, (struct sockaddr *)&ss, sslen)
	    == -1)
		log_warn("%s: sendto", __func__);
	log_info("send %s for %s%s%s",
	    (code == RADIUS_CODE_DISCONNECT_ACK)? "Disconnect-ACK" :
	    (code == RADIUS_CODE_DISCONNECT_NAK)? "Disconnect-NAK" : "CoA-NAK",
	    print_addr(&ss), (nakcause)? ": " : "", (nakcause)? nakcause : "");
 out:
	radius_delete_packet(req);
	if (res != NULL)
		radius_delete_packet(res);
}
