/*	$OpenBSD: radiusd_eap2mschap.c,v 1.4 2024/09/15 05:31:23 yasuoka Exp $	*/

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
#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <event.h>
#include <radius.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "radiusd.h"
#include "radiusd_module.h"
#include "radius_subr.h"
#include "log.h"

#define EAP_TIMEOUT			60

#include "eap2mschap_local.h"

int
main(int argc, char *argv[])
{
	struct module_handlers handlers = {
		.start = eap2mschap_start,
		.config_set = eap2mschap_config_set,
		.stop = eap2mschap_stop,
		.access_request = eap2mschap_access_request,
		.next_response = eap2mschap_next_response
	};
	struct eap2mschap	eap2mschap;

	eap2mschap_init(&eap2mschap);
	if ((eap2mschap.base = module_create(STDIN_FILENO, &eap2mschap,
	    &handlers)) == NULL)
		err(1, "module_create");

	module_drop_privilege(eap2mschap.base, 0);
	setproctitle("[main]");

	module_load(eap2mschap.base);
	event_init();
	log_init(0);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	module_start(eap2mschap.base);
	event_loop(0);

	module_destroy(eap2mschap.base);

	event_loop(0);
	event_base_free(NULL);

	exit(EXIT_SUCCESS);
}

void
eap2mschap_init(struct eap2mschap *self)
{
	memset(self, 0, sizeof(struct eap2mschap));
	RB_INIT(&self->eapt);
	TAILQ_INIT(&self->reqq);
}

void
eap2mschap_start(void *ctx)
{
	struct eap2mschap	*self = ctx;

	if (self->chap_name[0] == '\0')
		strlcpy(self->chap_name, "radiusd", sizeof(self->chap_name));

	module_send_message(self->base, IMSG_OK, NULL);

	evtimer_set(&self->ev_eapt, eap2mschap_on_eapt, self);
}

void
eap2mschap_config_set(void *ctx, const char *name, int argc,
    char * const * argv)
{
	struct eap2mschap	*self = ctx;
	const char		*errmsg = "none";

	if (strcmp(name, "chap-name") == 0) {
		SYNTAX_ASSERT(argc == 1,
		    "specify 1 argument for `chap-name'");
		if (strlcpy(self->chap_name, argv[0], sizeof(self->chap_name))
		    >= sizeof(self->chap_name)) {
			module_send_message(self->base, IMSG_NG,
			    "chap-name is too long");
			return;
		}
	} else if (strcmp(name, "_debug") == 0)
		log_init(1);
	else if (strncmp(name, "_", 1) == 0)
		/* ignore all internal messages */;
	else {
		module_send_message(self->base, IMSG_NG,
		    "Unknown config parameter `%s'", name);
		return;
	}

	module_send_message(self->base, IMSG_OK, NULL);
	return;
 syntax_error:
	module_send_message(self->base, IMSG_NG, "%s", errmsg);
}

void
eap2mschap_stop(void *ctx)
{
	struct eap2mschap	*self = ctx;
	struct access_req	*req, *reqt;

	evtimer_del(&self->ev_eapt);

	RB_FOREACH_SAFE(req, access_reqt, &self->eapt, reqt) {
		RB_REMOVE(access_reqt, &self->eapt, req);
		access_request_free(req);
	}
	TAILQ_FOREACH_SAFE(req, &self->reqq, next, reqt) {
		TAILQ_REMOVE(&self->reqq, req, next);
		access_request_free(req);
	}
}

void
eap2mschap_access_request(void *ctx, u_int q_id, const u_char *reqpkt,
    size_t reqpktlen)
{
	struct eap2mschap	*self = ctx;
	struct access_req	*req = NULL;
	RADIUS_PACKET		*pkt;

	if ((pkt = radius_convert_packet(reqpkt, reqpktlen)) == NULL) {
		log_warn("%s: radius_convert_packet() failed", __func__);
		goto on_fail;
	}

	if (radius_has_attr(pkt, RADIUS_TYPE_EAP_MESSAGE)) {
		if ((req = eap_recv(self, q_id, pkt)) == NULL)
			return;
		TAILQ_INSERT_TAIL(&self->reqq, req, next);
		radius_delete_packet(pkt);
		return;
	}
	if (pkt != NULL)
		radius_delete_packet(pkt);
	module_accsreq_next(self->base, q_id, reqpkt, reqpktlen);
	return;
 on_fail:
	if (pkt != NULL)
		radius_delete_packet(pkt);
	module_accsreq_aborted(self->base, q_id);
}

void
eap2mschap_next_response(void *ctx, u_int q_id, const u_char *respkt,
    size_t respktlen)
{
	struct eap2mschap	*self = ctx;
	struct access_req	*req = NULL;
	RADIUS_PACKET		*pkt = NULL;

	TAILQ_FOREACH(req, &self->reqq, next) {
		if (req->q_id == q_id)
			break;
	}
	if (req == NULL) {
		module_accsreq_answer(self->base, q_id, respkt, respktlen);
		return;
	}
	TAILQ_REMOVE(&self->reqq, req, next);
	if ((pkt = radius_convert_packet(respkt, respktlen)) == NULL) {
		log_warn("%s: q=%u radius_convert_packet() failed", __func__,
		    q_id);
		goto on_fail;
	}
	eap_resp_mschap(self, req, pkt);
	return;
 on_fail:
	if (pkt != NULL)
		radius_delete_packet(pkt);
	module_accsreq_aborted(self->base, q_id);
}

void
eap2mschap_on_eapt(int fd, short ev, void *ctx)
{
	struct eap2mschap	*self = ctx;
	time_t			 currtime;
	struct access_req	*req, *reqt;

	currtime = monotime();
	RB_FOREACH_SAFE(req, access_reqt, &self->eapt, reqt) {
		if (currtime - req->eap_time > EAP_TIMEOUT) {
			RB_REMOVE(access_reqt, &self->eapt, req);
			access_request_free(req);
		}
	}
	TAILQ_FOREACH_SAFE(req, &self->reqq, next, reqt) {
		if (currtime - req->eap_time > EAP_TIMEOUT) {
			TAILQ_REMOVE(&self->reqq, req, next);
			access_request_free(req);
		}
	}

	eap2mschap_reset_eaptimer(self);
}

void
eap2mschap_reset_eaptimer(struct eap2mschap *self)
{
	struct timeval	 tv = { 4, 0 };

	if ((!RB_EMPTY(&self->eapt) || !TAILQ_EMPTY(&self->reqq)) &&
	    evtimer_pending(&self->ev_eapt, NULL) == 0)
		evtimer_add(&self->ev_eapt, &tv);
}

struct access_req *
access_request_new(struct eap2mschap *self, u_int q_id)
{
	struct access_req	*req = NULL;

	if ((req = calloc(1, sizeof(struct access_req))) == NULL) {
		log_warn("%s: Out of memory", __func__);
		return (NULL);
	}
	req->eap2mschap = self;
	req->q_id = q_id;

	EAP2MSCHAP_DBG("%s(%p)", __func__, req);
	return (req);
}

void
access_request_free(struct access_req *req)
{
	EAP2MSCHAP_DBG("%s(%p)", __func__, req);
	free(req->username);
	if (req->pkt != NULL)
		radius_delete_packet(req->pkt);
	free(req);
}

int
access_request_compar(struct access_req *a, struct access_req *b)
{
	return (memcmp(a->state, b->state, sizeof(a->state)));
}

RB_GENERATE_STATIC(access_reqt, access_req, tree, access_request_compar);

/***********************************************************************
 * EAP related functions
 * Specfication: RFC 3748 [MS-CHAP]
 ***********************************************************************/
struct access_req *
eap_recv(struct eap2mschap *self, u_int q_id, RADIUS_PACKET *pkt)
{
	char			 buf[512], buf2[80];
	size_t			 msgsiz = 0;
	struct eap		*eap;
	int			 namesiz;
	struct access_req	*req = NULL;
	char			 state[16];
	size_t			 statesiz;
	struct access_req	 key;

	/*
	 * Check the message authenticator.  OK if it exists since the check
	 * is done by radiusd(8).
	 */
	if (!radius_has_attr(pkt, RADIUS_TYPE_MESSAGE_AUTHENTICATOR)) {
		log_warnx("q=%u Received EAP message but has no message "
		    "authenticator", q_id);
		goto fail;
	}

	if (radius_get_raw_attr_cat(pkt, RADIUS_TYPE_EAP_MESSAGE, NULL,
	    &msgsiz) != 0) {
		log_warnx("q=%u Received EAP message is too big %zu", q_id,
		    msgsiz);
		goto fail;
	}
	msgsiz = sizeof(buf);
	if (radius_get_raw_attr_cat(pkt, RADIUS_TYPE_EAP_MESSAGE, buf,
	    &msgsiz) != 0) {
		log_warnx("%s: radius_get_raw_attr_cat() failed", __func__);
		goto fail;
	}

	eap = (struct eap *)buf;
	if (msgsiz < offsetof(struct eap, value[1]) ||
	    ntohs(eap->length) > msgsiz) {
		log_warnx("q=%u Received EAP message has wrong in size: "
		    "received length %zu eap.length=%u", q_id, msgsiz,
		    ntohs(eap->length));
		goto fail;
	}

	EAP2MSCHAP_DBG("q=%u Received EAP code=%d type=%d", q_id,
	    (int)eap->code, (int)eap->value[0]);

	if (eap->code != EAP_CODE_RESPONSE) {
		log_warnx("q=%u Received EAP message has unexpected code %u",
		    q_id, (unsigned)eap->code);
		goto fail;
	}

	if (eap->value[0] == EAP_TYPE_IDENTITY) {
		/*
		 * Handle EAP-Indentity
		 */
		struct eap_mschap_challenge	*chall;
		RADIUS_PACKET			*radres = NULL;

		if ((req = access_request_new(self, q_id)) == NULL)
			goto fail;
		req->eap_time = monotime();
		arc4random_buf(req->state, sizeof(req->state));
		arc4random_buf(req->chall, sizeof(req->chall));

		namesiz = ntohs(eap->length) - offsetof(struct eap, value[1]);
		log_info("q=%u EAP state=%s EAP-Identity %.*s ",
		    q_id, hex_string(req->state, sizeof(req->state),
		    buf2, sizeof(buf2)), namesiz, eap->value + 1);
		namesiz = strlen(self->chap_name);

		/*
		 * Start MS-CHAP-V2
		 */
		msgsiz = offsetof(struct eap_mschap_challenge,
		    chap_name[namesiz]);
		chall = (struct eap_mschap_challenge *)buf;
		chall->eap.code = EAP_CODE_REQUEST;
		chall->eap.id = ++req->eap_id;
		chall->eap.length = htons(msgsiz);
		chall->eap_type = EAP_TYPE_MSCHAPV2;
		chall->chap.code = CHAP_CHALLENGE;
		chall->chap.id = ++req->chap_id;
		chall->chap.length = htons(msgsiz -
		    offsetof(struct eap_mschap_challenge, chap));
		chall->challsiz = sizeof(chall->chall);
		memcpy(chall->chall, req->chall, sizeof(chall->chall));
		memcpy(chall->chap_name, self->chap_name, namesiz);

		if ((radres = radius_new_response_packet(
		    RADIUS_CODE_ACCESS_CHALLENGE, pkt)) == NULL) {
			log_warn("%s: radius_new_response_packet() failed",
			    __func__);
			goto fail;
		}
		radius_put_raw_attr(radres, RADIUS_TYPE_EAP_MESSAGE, buf,
		    msgsiz);
		radius_put_raw_attr(radres, RADIUS_TYPE_STATE, req->state,
		    sizeof(req->state));
		radius_put_uint32_attr(radres, RADIUS_TYPE_SESSION_TIMEOUT,
		    EAP_TIMEOUT);
		radius_put_message_authenticator(radres, "");	/* dummy */

		req->eap_chap_status = EAP_CHAP_CHALLENGE_SENT;
		module_accsreq_answer(self->base, req->q_id,
		    radius_get_data(radres), radius_get_length(radres));

		radius_delete_packet(pkt);
		radius_delete_packet(radres);
		RB_INSERT(access_reqt, &self->eapt, req);
		eap2mschap_reset_eaptimer(self);

		return (NULL);
	}
	/* Other than EAP-Identity */
	statesiz = sizeof(state);
	if (radius_get_raw_attr(pkt, RADIUS_TYPE_STATE, state, &statesiz) != 0)
	{
		log_info("q=%u received EAP message (type=%d) doesn't have a "
		    "proper state attribute", q_id, eap->value[0]);
		goto fail;
	}

	memcpy(key.state, state, statesiz);
	if ((req = RB_FIND(access_reqt, &self->eapt, &key)) == NULL) {
		log_info("q=%u received EAP message (type=%d) no context for "
		    "the state=%s", q_id, eap->value[0], hex_string(state,
		    statesiz, buf2, sizeof(buf2)));
		goto fail;
	}
	req->eap_time = monotime();
	req->q_id = q_id;
	switch (eap->value[0]) {
	case EAP_TYPE_NAK:
		log_info("q=%u EAP state=%s NAK received", q_id,
		    hex_string(state, statesiz, buf2, sizeof(buf2)));
		eap_send_reject(req, pkt, q_id);
		goto fail;
	case EAP_TYPE_MSCHAPV2:
		if (msgsiz < offsetof(struct eap, value[1])) {
			log_warnx("q=%u EAP state=%s Received message has "
			    "wrong in size for EAP-MS-CHAPV2: received length "
			    "%zu eap.length=%u", q_id,
			    hex_string(state, statesiz, buf2, sizeof(buf2)),
			    msgsiz, ntohs(eap->length));
			goto fail;
		}
		req = eap_recv_mschap(self, req, pkt, (struct eap_chap *)eap);

		break;
	default:
		log_warnx("q=%u EAP state=%s EAP unknown type=%u receieved.",
		    q_id, hex_string(state, statesiz, buf2, sizeof(buf2)),
		    eap->value[0]);
		goto fail;
	}

	return (req);
 fail:
	radius_delete_packet(pkt);
	return (NULL);
}

struct access_req *
eap_recv_mschap(struct eap2mschap *self, struct access_req *req,
    RADIUS_PACKET *pkt, struct eap_chap *chap)
{
	size_t		 eapsiz;
	char		 buf[80];

	EAP2MSCHAP_DBG("%s(%p)", __func__, req);

	eapsiz = ntohs(chap->eap.length);
	switch (chap->chap.code) {
	case CHAP_RESPONSE:
	    {
		struct eap_mschap_response	*resp;
		struct radius_ms_chap2_response	 rr;
		size_t				 namelen;
		bool				 reset_username = false;

		if (req->eap_chap_status != EAP_CHAP_CHALLENGE_SENT)
			goto failmsg;
		resp = (struct eap_mschap_response *)chap;
		if (eapsiz < sizeof(struct eap_mschap_response) ||
		    htons(resp->chap.length) <
		    sizeof(struct eap_mschap_response) -
		    offsetof(struct eap_mschap_response, chap)) {
			log_warnx("q=%u EAP state=%s Received EAP message has "
			    "wrong in size: received length %zu eap.length=%u "
			    "chap.length=%u valuesize=%u", req->q_id,
			    hex_string(req->state, sizeof(req->state), buf,
			    sizeof(buf)), eapsiz, ntohs(resp->eap.length),
			    ntohs(resp->chap.length), resp->chap.value[9]);
			goto fail;
		}
		log_info("q=%u EAP state=%s Received "
		    "CHAP-Response", req->q_id, hex_string(req->state,
		    sizeof(req->state), buf, sizeof(buf)));

		/* Unknown identity in EAP and got the username in CHAP */
		namelen = ntohs(resp->chap.length) -
		    (offsetof(struct eap_mschap_response, chap_name[0]) -
		    offsetof(struct eap_mschap_response, chap));
		if ((req->username == NULL || req->username[0] == '\0') &&
		    namelen > 0) {
			free(req->username);
			if ((req->username = strndup(resp->chap_name, namelen))
			    == NULL) {
				log_warn("%s: strndup", __func__);
				goto fail;
			}
			log_info("q=%u EAP state=%s username=%s", req->q_id,
			    hex_string(req->state, sizeof(req->state), buf,
			    sizeof(buf)), req->username);
			reset_username = true;
		}

		rr.ident = resp->chap.id;
		rr.flags = resp->flags;
		memcpy(rr.peerchall, resp->peerchall, sizeof(rr.peerchall));
		memcpy(rr.reserved, resp->reserved, sizeof(rr.reserved));
		memcpy(rr.ntresponse, resp->ntresponse, sizeof(rr.ntresponse));

		radius_del_attr_all(pkt, RADIUS_TYPE_EAP_MESSAGE);
		radius_del_attr_all(pkt, RADIUS_TYPE_STATE);

		if (reset_username) {
			radius_del_attr_all(pkt, RADIUS_TYPE_USER_NAME);
			radius_put_string_attr(pkt, RADIUS_TYPE_USER_NAME,
			    req->username);
		}
		radius_put_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP_CHALLENGE, req->chall,
		    sizeof(req->chall));
		radius_put_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_RESPONSE, &rr, sizeof(rr));
		req->eap_chap_status = EAP_CHAP_CHALLENGE_SENT;
		RB_REMOVE(access_reqt, &self->eapt, req);
		module_accsreq_next(self->base, req->q_id, radius_get_data(pkt),
		    radius_get_length(pkt));
		return (req);
	    }
	case CHAP_SUCCESS:
	    {
		struct eap		 eapres;
		RADIUS_PACKET		*radres = NULL;
		unsigned int		 i;
		uint8_t			 attr[256];
		size_t			 attrlen;

		/* Receiving Success-Reponse */
		if (chap->eap.code != EAP_CODE_RESPONSE) {
			log_info("q=%u EAP state=%s Received "
			    "CHAP-Success but EAP code is wrong %u", req->q_id,
			    hex_string(req->state, sizeof(req->state), buf,
			    sizeof(buf)), chap->eap.code);
			goto fail;
		}
		if (req->eap_chap_status == EAP_CHAP_SUCCESS_REQUEST_SENT)
			eapres.id = ++req->eap_id;
		else if (req->eap_chap_status != EAP_CHAP_SUCCESS)
			goto failmsg;

		req->eap_chap_status = EAP_CHAP_SUCCESS;
		eapres.code = EAP_CODE_SUCCESS;
		eapres.length = htons(sizeof(struct eap));

		if ((radres = radius_new_response_packet(
		    RADIUS_CODE_ACCESS_ACCEPT, pkt)) == NULL) {
			log_warn("%s: radius_new_response_packet failed",
			    __func__);
			goto fail;
		}

		radius_put_raw_attr(radres, RADIUS_TYPE_EAP_MESSAGE, &eapres,
		    sizeof(struct eap));
		radius_put_raw_attr(radres, RADIUS_TYPE_STATE, req->state,
		    sizeof(req->state));
		/* notice authenticated username */
		radius_put_string_attr(radres, RADIUS_TYPE_USER_NAME,
		    req->username);
		radius_put_message_authenticator(radres, "");	/* dummy */

		/* restore attributes */
		for (i = 0; i < nitems(preserve_attrs); i++) {
			attrlen = sizeof(attr);
			if (preserve_attrs[i].vendor == 0) {
				if (radius_get_raw_attr(req->pkt,
				    preserve_attrs[i].type, &attr, &attrlen)
				    == 0)
					radius_put_raw_attr(radres,
					    preserve_attrs[i].type, &attr,
					    attrlen);
			} else {
				if (radius_get_vs_raw_attr(req->pkt,
				    preserve_attrs[i].vendor,
				    preserve_attrs[i].type, &attr, &attrlen)
				    == 0)
					radius_put_vs_raw_attr(radres,
					    preserve_attrs[i].vendor,
					    preserve_attrs[i].type, &attr,
					    attrlen);
			}
		}

		module_accsreq_answer(self->base, req->q_id,
		    radius_get_data(radres), radius_get_length(radres));

		radius_delete_packet(pkt);
		radius_delete_packet(radres);

		return (NULL);
	    }
		break;
	}
 failmsg:
	log_warnx(
	    "q=%u EAP state=%s Can't handle the received EAP-CHAP message "
	    "(chap.code=%d) in EAP CHAP state=%s", req->q_id, hex_string(
	    req->state, sizeof(req->state), buf, sizeof(buf)), chap->chap.code,
	    eap_chap_status_string(req->eap_chap_status));
 fail:
	radius_delete_packet(pkt);
	return (NULL);
}

void
eap_resp_mschap(struct eap2mschap *self, struct access_req *req,
    RADIUS_PACKET *pkt)
{
	bool			 accept = false;
	int			 id, code;
	char			 resp[256 + 1], buf[80];
	size_t			 respsiz = 0, eapsiz;
	struct {
		struct eap_chap	 chap;
		char		 space[256];
	}			 eap;

	code = radius_get_code(pkt);
	id = radius_get_id(pkt);
	EAP2MSCHAP_DBG("id=%d code=%d", id, code);
	switch (code) {
	case RADIUS_CODE_ACCESS_ACCEPT:
	case RADIUS_CODE_ACCESS_REJECT:
	    {
		RADIUS_PACKET		*respkt;

		respsiz = sizeof(resp);
		if (code == RADIUS_CODE_ACCESS_ACCEPT) {
			accept = true;
			if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
			    RADIUS_VTYPE_MS_CHAP2_SUCCESS, &resp, &respsiz)
			    != 0) {
				log_warnx("q=%u EAP state=%s no "
				    "MS-CHAP2-Success attribute", req->q_id,
				    hex_string(req->state, sizeof(req->state),
				    buf, sizeof(buf)));
				goto fail;
			}
		} else {
			if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
			    RADIUS_VTYPE_MS_CHAP_ERROR, &resp, &respsiz)
			    != 0) {
				resp[0] = ++req->chap_id;
				snprintf(resp + 1, sizeof(resp) - 1,
				    "E=691 R=0 V=3");
				respsiz = 1 + strlen(resp + 1);
			}
		}

		/* Send EAP-CHAP "Success-Request" or "Failure-Request" */
		if ((respkt = radius_new_request_packet(accept
		    ? RADIUS_CODE_ACCESS_CHALLENGE
		    : RADIUS_CODE_ACCESS_REJECT)) == NULL) {
			log_warn("%s: radius_new_request_packet", __func__);
			goto fail;
		}
		radius_set_id(respkt, id);

		eapsiz  = offsetof(struct eap_chap, chap.value[respsiz - 1]);
		eap.chap.eap.code = EAP_CODE_REQUEST;
		eap.chap.eap.id = ++req->eap_id;
		eap.chap.eap.length = htons(eapsiz);
		eap.chap.eap_type = EAP_TYPE_MSCHAPV2;
		eap.chap.chap.id = resp[0];
		eap.chap.chap.length = htons(
		    offsetof(struct chap, value[respsiz - 1]));
		memcpy(eap.chap.chap.value, resp + 1, respsiz - 1);
		if (accept)
			eap.chap.chap.code = CHAP_SUCCESS;
		else
			eap.chap.chap.code = CHAP_FAILURE;

		radius_put_raw_attr(respkt, RADIUS_TYPE_STATE, req->state,
		    sizeof(req->state));
		radius_put_raw_attr(respkt, RADIUS_TYPE_EAP_MESSAGE, &eap,
		    eapsiz);

		module_accsreq_answer(req->eap2mschap->base, req->q_id,
		    radius_get_data(respkt), radius_get_length(respkt));
		radius_delete_packet(respkt);
		if (accept)
			req->eap_chap_status = EAP_CHAP_SUCCESS_REQUEST_SENT;
		else
			req->eap_chap_status = EAP_CHAP_FAILURE_REQUEST_SENT;

		RB_INSERT(access_reqt, &req->eap2mschap->eapt, req);
		eap2mschap_reset_eaptimer(self);
		req->pkt = pkt;
		pkt = NULL;
		break;
	    }
	default:
		log_warnx("q=%u Received unknown RADIUS packet code=%d",
		    req->q_id, code);
		goto fail;
	}
	return;
 fail:
	if (pkt != NULL)
		radius_delete_packet(pkt);
	module_accsreq_aborted(self->base, req->q_id);
	access_request_free(req);
	return;
}

void
eap_send_reject(struct access_req *req, RADIUS_PACKET *reqp, u_int q_id)
{
	RADIUS_PACKET		*resp;
	struct {
		uint8_t		 code;
		uint8_t		 id;
		uint16_t	 length;
	} __packed		 eap;

	resp = radius_new_response_packet(RADIUS_CODE_ACCESS_REJECT, reqp);
	if (resp == NULL) {
		log_warn("%s: radius_new_response_packet() failed", __func__);
		module_accsreq_aborted(req->eap2mschap->base, q_id);
		return;
	}
	memset(&eap, 0, sizeof(eap));	/* just in case */
	eap.code = EAP_CODE_REQUEST;
	eap.id = ++req->eap_id;
	eap.length = htons(sizeof(eap));
	radius_put_raw_attr(resp, RADIUS_TYPE_EAP_MESSAGE, &eap,
	    ntohs(eap.length));
	module_accsreq_answer(req->eap2mschap->base, q_id,
	    radius_get_data(resp), radius_get_length(resp));
	radius_delete_packet(resp);
}

const char *
eap_chap_status_string(enum eap_chap_status status)
{
	switch (status) {
	case EAP_CHAP_NONE:		return "None";
	case EAP_CHAP_CHALLENGE_SENT:	return "Challenge-Sent";
	case EAP_CHAP_SUCCESS_REQUEST_SENT:
					return "Success-Request-Sent";
	case EAP_CHAP_FAILURE_REQUEST_SENT:
					return "Failure-Request-Sent";
	case EAP_CHAP_CHANGE_PASSWORD_SENT:
					return "Change-Password-Sent";
	case EAP_CHAP_SUCCESS:		return "Success";
	case EAP_CHAP_FAILED:		return "Failed";
	}
	return "Error";
}

/***********************************************************************
 * Miscellaneous functions
 ***********************************************************************/
const char *
hex_string(const char *bytes, size_t byteslen, char *buf, size_t bufsiz)
{
	const char	 hexstr[] = "0123456789abcdef";
	unsigned	 i, j;

	for (i = 0, j = 0; i < byteslen && j + 2 < bufsiz; i++, j += 2) {
		buf[j]     = hexstr[(bytes[i] & 0xf0) >> 4];
		buf[j + 1] = hexstr[bytes[i] & 0xf];
	}

	if (i < byteslen)
		return (NULL);
	buf[j] = '\0';
	return (buf);
}

time_t
monotime(void)
{
	struct timespec		ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		fatal("clock_gettime(CLOCK_MONOTONIC,) failed");

	return (ts.tv_sec);
}
