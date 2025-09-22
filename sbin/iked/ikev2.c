/*	$OpenBSD: ikev2.c,v 1.394 2025/07/31 19:02:43 pascal Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <endian.h>
#include <errno.h>
#include <err.h>
#include <event.h>
#include <time.h>

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "iked.h"
#include "ikev2.h"
#include "eap.h"
#include "dh.h"
#include "chap_ms.h"
#include "version.h"

void	 ikev2_info(struct iked *, struct imsg *, int);
void	 ikev2_info_sa(struct iked *, struct imsg *, int, const char *,
	    struct iked_sa *);
void	 ikev2_info_csa(struct iked *, struct imsg *, int, const char *,
	    struct iked_childsa *);
void	 ikev2_info_flow(struct iked *, struct imsg *, int, const char *,
	    struct iked_flow *);
void	 ikev2_log_established(struct iked_sa *);
void	 ikev2_log_proposal(struct iked_sa *, struct iked_proposals *);
void	 ikev2_log_cert_info(const char *, struct iked_id *);

void	 ikev2_run(struct privsep *, struct privsep_proc *, void *);
void	 ikev2_shutdown(void);
int	 ikev2_dispatch_parent(int, struct privsep_proc *, struct imsg *);
int	 ikev2_dispatch_cert(int, struct privsep_proc *, struct imsg *);
int	 ikev2_dispatch_control(int, struct privsep_proc *, struct imsg *);

struct iked_sa *
	 ikev2_getimsgdata(struct iked *, struct imsg *, struct iked_sahdr *,
	    uint8_t *, uint8_t **, size_t *);

int	 ikev2_ike_auth_compatible(struct iked_sa *, uint8_t, uint8_t);
int	 ikev2_ike_auth_recv(struct iked *, struct iked_sa *,
	    struct iked_message *);
int	 ikev2_ike_auth(struct iked *, struct iked_sa *);
int	 ikev2_auth_verify(struct iked *, struct iked_sa *);

void	 ikev2_init_recv(struct iked *, struct iked_message *,
	    struct ike_header *);
void	 ikev2_init_ike_sa_timeout(struct iked *, void *);
int	 ikev2_init_ike_sa_peer(struct iked *, struct iked_policy *,
	    struct iked_addr *, struct iked_message *);
int	 ikev2_init_ike_auth(struct iked *, struct iked_sa *);
int	 ikev2_init_auth(struct iked *, struct iked_message *);
int	 ikev2_init_done(struct iked *, struct iked_sa *);

int	 ikev2_record_dstid(struct iked *, struct iked_sa *);

void	 ikev2_enable_timer(struct iked *, struct iked_sa *);
void	 ikev2_disable_timer(struct iked *, struct iked_sa *);

void	 ikev2_resp_recv(struct iked *, struct iked_message *,
	    struct ike_header *);
int	 ikev2_resp_ike_sa_init(struct iked *, struct iked_message *);
int	 ikev2_resp_ike_eap(struct iked *, struct iked_sa *,
	    struct iked_message *);
int	 ikev2_resp_ike_eap_mschap(struct iked *, struct iked_sa *,
	    struct iked_message *);
int	 ikev2_resp_ike_auth(struct iked *, struct iked_sa *);
int	 ikev2_send_auth_failed(struct iked *, struct iked_sa *);
int	 ikev2_send_error(struct iked *, struct iked_sa *,
	    struct iked_message *, uint8_t);
int	 ikev2_send_init_error(struct iked *, struct iked_message *);

int	 ikev2_handle_certreq(struct iked*, struct iked_message *);
ssize_t	 ikev2_handle_delete(struct iked *, struct iked_message *,
	    struct ibuf *, struct ikev2_payload **, uint8_t *);

int	 ikev2_send_create_child_sa(struct iked *, struct iked_sa *,
	    struct iked_spi *, uint8_t, uint16_t);
int	 ikev2_ikesa_enable(struct iked *, struct iked_sa *, struct iked_sa *);
void	 ikev2_ikesa_delete(struct iked *, struct iked_sa *, int);
int	 ikev2_nonce_cmp(struct ibuf *, struct ibuf *);
int	 ikev2_init_create_child_sa(struct iked *, struct iked_message *);
int	 ikev2_resp_create_child_sa(struct iked *, struct iked_message *);
void	 ikev2_ike_sa_rekey(struct iked *, void *);
void	 ikev2_ike_sa_rekey_timeout(struct iked *, void *);
void	 ikev2_ike_sa_rekey_schedule(struct iked *, struct iked_sa *);
void	 ikev2_ike_sa_rekey_schedule_fast(struct iked *, struct iked_sa *);
void	 ikev2_ike_sa_alive(struct iked *, void *);
void	 ikev2_ike_sa_keepalive(struct iked *, void *);

int	 ikev2_sa_negotiate_common(struct iked *, struct iked_sa *,
	    struct iked_message *, int);
int	 ikev2_sa_initiator(struct iked *, struct iked_sa *,
	    struct iked_sa *, struct iked_message *);
int	 ikev2_sa_responder(struct iked *, struct iked_sa *, struct iked_sa *,
	    struct iked_message *);
int	 ikev2_sa_initiator_dh(struct iked_sa *, struct iked_message *,
	    unsigned int, struct iked_sa *);
int	 ikev2_sa_responder_dh(struct iked_kex *, struct iked_proposals *,
	    struct iked_message *, unsigned int);
void	 ikev2_sa_cleanup_dh(struct iked_sa *);
int	 ikev2_sa_keys(struct iked *, struct iked_sa *, struct ibuf *);
int	 ikev2_sa_tag(struct iked_sa *, struct iked_id *);
int	 ikev2_set_sa_proposal(struct iked_sa *, struct iked_policy *,
	    unsigned int);

int	 ikev2_childsa_negotiate(struct iked *, struct iked_sa *,
	    struct iked_kex *, struct iked_proposals *, int, int);
int	 ikev2_childsa_delete_proposed(struct iked *, struct iked_sa *,
	    struct iked_proposals *);
int	 ikev2_valid_proposal(struct iked_proposal *,
	    struct iked_transform **, struct iked_transform **, int *);

int	 ikev2_handle_notifies(struct iked *, struct iked_message *);

ssize_t	 ikev2_add_proposals(struct iked *, struct iked_sa *, struct ibuf *,
	    struct iked_proposals *, uint8_t, int, int, int);
ssize_t	 ikev2_add_cp(struct iked *, struct iked_sa *, int, struct ibuf *);
ssize_t	 ikev2_init_add_cp(struct iked *, struct iked_sa *, struct ibuf *);
ssize_t	 ikev2_resp_add_cp(struct iked *, struct iked_sa *, struct ibuf *);
ssize_t	 ikev2_add_transform(struct ibuf *,
	    uint8_t, uint8_t, uint16_t, uint16_t);
ssize_t	 ikev2_add_ts(struct ibuf *, struct ikev2_payload **, ssize_t,
	    struct iked_sa *, int);
ssize_t	 ikev2_add_certreq(struct ibuf *, struct ikev2_payload **, ssize_t,
	    struct ibuf *, uint8_t);
ssize_t	 ikev2_add_ipcompnotify(struct iked *, struct ibuf *,
	    struct ikev2_payload **, ssize_t, struct iked_sa *, int);
ssize_t	 ikev2_add_ts_payload(struct ibuf *, unsigned int, struct iked_sa *);
ssize_t	 ikev2_add_error(struct iked *, struct ibuf *, struct iked_message *);
int	 ikev2_add_data(struct ibuf *, void *, size_t);
int	 ikev2_add_buf(struct ibuf *buf, struct ibuf *);

int	 ikev2_cp_setaddr(struct iked *, struct iked_sa *, sa_family_t);
int	 ikev2_cp_setaddr_pool(struct iked *, struct iked_sa *,
	    struct iked_cfg *, const char **, sa_family_t);
int	 ikev2_cp_fixaddr(struct iked_sa *, struct iked_addr *,
	    struct iked_addr *);
int	 ikev2_cp_fixflow(struct iked_sa *, struct iked_flow *,
	    struct iked_flow *);
int	 ikev2_cp_request_configured(struct iked_sa *);

ssize_t	 ikev2_add_sighashnotify(struct ibuf *, struct ikev2_payload **,
	    ssize_t);
ssize_t	 ikev2_add_nat_detection(struct iked *, struct ibuf *,
	    struct ikev2_payload **, struct iked_message *, ssize_t);
ssize_t	 ikev2_add_vendor_id(struct ibuf *, struct ikev2_payload **,
	    ssize_t, struct ibuf *);
ssize_t	 ikev2_add_notify(struct ibuf *, struct ikev2_payload **, ssize_t,
	    uint16_t);
ssize_t	 ikev2_add_mobike(struct ibuf *, struct ikev2_payload **, ssize_t);
ssize_t	 ikev2_add_fragmentation(struct ibuf *, struct ikev2_payload **,
	    ssize_t);
ssize_t	 ikev2_add_transport_mode(struct iked *, struct ibuf *,
	    struct ikev2_payload **, ssize_t, struct iked_sa *);
int	 ikev2_update_sa_addresses(struct iked *, struct iked_sa *);
int	 ikev2_resp_informational(struct iked *, struct iked_sa *,
	    struct iked_message *);

void	ikev2_ctl_reset_id(struct iked *, struct imsg *, unsigned int);
void	ikev2_ctl_show_sa(struct iked *, struct imsg *);
void	ikev2_ctl_show_stats(struct iked *, struct imsg *);

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	ikev2_dispatch_parent },
	{ "certstore",	PROC_CERT,	ikev2_dispatch_cert },
	{ "control",	PROC_CONTROL,	ikev2_dispatch_control }
};

void
ikev2(struct privsep *ps, struct privsep_proc *p)
{
	proc_run(ps, p, procs, nitems(procs), ikev2_run, NULL);
}

void
ikev2_run(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	/*
	 * pledge in the ikev2 process:
	 * stdio - for malloc and basic I/O including events.
	 * inet - for sendto with specified peer address.
	 * recvfd - for PFKEYv2 and the listening UDP sockets.
	 * In theory, recvfd could be dropped after getting the fds once.
	 */
	p->p_shutdown = ikev2_shutdown;
	if (pledge("stdio inet recvfd", NULL) == -1)
		fatal("pledge");
}

void
ikev2_shutdown(void)
{
	struct iked		*env = iked_env;

	ibuf_free(env->sc_certreq);
	env->sc_certreq = NULL;
	config_doreset(env, RESET_ALL);
}

int
ikev2_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct iked		*env = iked_env;
	struct iked_sa		*sa, *satmp;
	struct iked_policy	*pol, *old;

	switch (imsg->hdr.type) {
	case IMSG_CTL_RESET:
		return (config_getreset(env, imsg));
	case IMSG_CTL_COUPLE:
	case IMSG_CTL_DECOUPLE:
		return (config_getcoupled(env, imsg->hdr.type));
	case IMSG_CTL_ACTIVE:
	case IMSG_CTL_PASSIVE:
		if (config_getmode(env, imsg->hdr.type) == -1)
			return (0);	/* ignore error */
		config_enablesocket(env);
		timer_del(env, &env->sc_inittmr);
		TAILQ_FOREACH(pol, &env->sc_policies, pol_entry) {
			if (policy_generate_ts(pol) == -1)
				fatalx("%s: too many traffic selectors", __func__);
		}
		/* Find new policies for dangling SAs */
		RB_FOREACH_SAFE(sa, iked_sas, &env->sc_sas, satmp) {
			if (sa->sa_state != IKEV2_STATE_ESTABLISHED) {
				sa_state(env, sa, IKEV2_STATE_CLOSING);
				ikev2_ike_sa_setreason(sa, "reload");
				sa_free(env, sa);
				continue;
			}

			old = sa->sa_policy;
			if (policy_lookup_sa(env, sa) == -1) {
				log_info("%s: No matching Policy found, terminating SA.",
				    SPI_SA(sa, __func__));
				ikev2_ike_sa_setreason(sa, "Policy no longer exists");
				ikev2_ikesa_delete(env, sa, sa->sa_hdr.sh_initiator);
			}
			if (old != sa->sa_policy) {
				/* Cleanup old policy */
				TAILQ_REMOVE(&old->pol_sapeers, sa, sa_peer_entry);
				policy_unref(env, old);
				policy_ref(env, sa->sa_policy);
				TAILQ_INSERT_TAIL(&sa->sa_policy->pol_sapeers, sa, sa_peer_entry);
			}
		}
		if (!env->sc_passive) {
			timer_set(env, &env->sc_inittmr, ikev2_init_ike_sa,
			    NULL);
			timer_add(env, &env->sc_inittmr,
			    IKED_INITIATOR_INITIAL);
		}
		iked_radius_acct_on(env);
		return (0);
	case IMSG_UDP_SOCKET:
		return (config_getsocket(env, imsg, ikev2_msg_cb));
	case IMSG_PFKEY_SOCKET:
		return (config_getpfkey(env, imsg));
	case IMSG_CFG_POLICY:
		return (config_getpolicy(env, imsg));
	case IMSG_CFG_FLOW:
		return (config_getflow(env, imsg));
	case IMSG_CFG_USER:
		return (config_getuser(env, imsg));
	case IMSG_CFG_RADAUTH:
		return (config_getradauth(env, imsg));
	case IMSG_CFG_RADACCT:
		return (config_getradacct(env, imsg));
	case IMSG_CFG_RADSERVER:
		return (config_getradserver(env, imsg));
	case IMSG_CFG_RADCFGMAP:
		return (config_getradcfgmap(env, imsg));
	case IMSG_CFG_RADDAE:
		return (config_getraddae(env, imsg));
	case IMSG_CFG_RADDAECLIENT:
		return (config_getradclient(env, imsg));
	case IMSG_COMPILE:
		return (config_getcompile(env));
	case IMSG_CTL_STATIC:
		return (config_getstatic(env, imsg));
	default:
		break;
	}

	return (-1);
}

int
ikev2_dispatch_cert(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct iked		*env = iked_env;
	struct iked_sahdr	 sh;
	struct iked_sa		*sa;
	uint8_t			 type;
	uint8_t			*ptr;
	size_t			 len;
	struct iked_id		*id = NULL;
	int			 ignore = 0;
	int			 i;

	switch (imsg->hdr.type) {
	case IMSG_CERTREQ:
		IMSG_SIZE_CHECK(imsg, &type);

		ptr = imsg->data;
		memcpy(&type, ptr, sizeof(type));
		ptr += sizeof(type);

		ibuf_free(env->sc_certreq);
		env->sc_certreqtype = type;
		env->sc_certreq = ibuf_new(ptr,
		    IMSG_DATA_SIZE(imsg) - sizeof(type));

		log_debug("%s: updated local CERTREQ type %s length %zu",
		    __func__, print_map(type, ikev2_cert_map),
		    ibuf_length(env->sc_certreq));

		break;
	case IMSG_CERTVALID:
	case IMSG_CERTINVALID:
		/* Ignore invalid or unauthenticated SAs */
		if ((sa = ikev2_getimsgdata(env, imsg,
		    &sh, &type, &ptr, &len)) == NULL ||
		    sa->sa_state < IKEV2_STATE_EAP)
			break;

		if (sh.sh_initiator)
			id = &sa->sa_rcert;
		else
			id = &sa->sa_icert;

		id->id_type = type;
		id->id_offset = 0;
		ibuf_free(id->id_buf);
		id->id_buf = NULL;

		if (len > 0 && (id->id_buf = ibuf_new(ptr, len)) == NULL) {
			log_debug("%s: failed to get cert payload",
			    __func__);
			break;
		}

		if (imsg->hdr.type == IMSG_CERTVALID) {
			if (sa->sa_peerauth.id_type && ikev2_auth_verify(env, sa))
				break;

			log_debug("%s: peer certificate is valid", __func__);
			sa_stateflags(sa, IKED_REQ_CERTVALID);

			if (ikev2_ike_auth(env, sa) != 0)
				log_debug("%s: failed to send ike auth", __func__);
		} else {
			log_warnx("%s: peer certificate is invalid",
				SPI_SA(sa, __func__));
			ikev2_send_auth_failed(env, sa);
		}
		break;
	case IMSG_CERT:
		if ((sa = ikev2_getimsgdata(env, imsg,
		    &sh, &type, &ptr, &len)) == NULL) {
			log_debug("%s: invalid cert reply", __func__);
			break;
		}

		/*
		 * Ignore the message if we already got a valid certificate.
		 * This might happen if the peer sent multiple CERTREQs.
		 */
		if (sa->sa_stateflags & IKED_REQ_CERT ||
		    type == IKEV2_CERT_NONE)
			ignore = 1;

		log_debug("%s: cert type %s length %zu, %s", __func__,
		    print_map(type, ikev2_cert_map), len,
		    ignore ? "ignored" : "ok");

		if (ignore)
			break;

		if (sh.sh_initiator)
			id = &sa->sa_icert;
		else
			id = &sa->sa_rcert;

		id->id_type = type;
		id->id_offset = 0;
		ibuf_free(id->id_buf);
		id->id_buf = NULL;

		if (len <= 0 || (id->id_buf = ibuf_new(ptr, len)) == NULL) {
			log_debug("%s: failed to get cert payload",
			    __func__);
			break;
		}

		sa_stateflags(sa, IKED_REQ_CERT);

		if (ikev2_ike_auth(env, sa) != 0)
			log_debug("%s: failed to send ike auth", __func__);
		break;
	case IMSG_SCERT:
		if ((sa = ikev2_getimsgdata(env, imsg,
		    &sh, &type, &ptr, &len)) == NULL) {
			log_debug("%s: invalid supplemental cert reply",
			    __func__);
			break;
		}

		if (sa->sa_stateflags & IKED_REQ_CERT ||
		    type == IKEV2_CERT_NONE)
			ignore = 1;

		log_debug("%s: supplemental cert type %s length %zu, %s",
		    __func__,
		    print_map(type, ikev2_cert_map), len,
		    ignore ? "ignored" : "ok");

		if (ignore)
			break;

		for (i = 0; i < IKED_SCERT_MAX; i++) {
			id = &sa->sa_scert[i];
			if (id->id_type == IKEV2_CERT_NONE)
				break;
			id = NULL;
		}

		if (id == NULL) {
			log_debug("%s: too many supplemental cert. ignored",
			    __func__);
			break;
		}

		id->id_type = type;
		id->id_offset = 0;
		ibuf_free(id->id_buf);
		id->id_buf = NULL;

		if (len <= 0 || (id->id_buf = ibuf_new(ptr, len)) == NULL) {
			log_debug("%s: failed to get supplemental cert payload",
			    __func__);
			break;
		}

		break;
	case IMSG_AUTH:
		if ((sa = ikev2_getimsgdata(env, imsg,
		    &sh, &type, &ptr, &len)) == NULL) {
			log_debug("%s: invalid auth reply", __func__);
			break;
		}
		if (sa_stateok(sa, IKEV2_STATE_VALID)) {
			log_warnx("%s: ignoring AUTH in state %s",
			    SPI_SA(sa, __func__),
			    print_map(sa->sa_state, ikev2_state_map));
			break;
		}

		log_debug("%s: AUTH type %d len %zu", __func__, type, len);

		id = &sa->sa_localauth;
		id->id_type = type;
		id->id_offset = 0;
		ibuf_free(id->id_buf);
		id->id_buf = NULL;

		if (type != IKEV2_AUTH_NONE) {
			if (len <= 0 ||
			    (id->id_buf = ibuf_new(ptr, len)) == NULL) {
				log_debug("%s: failed to get auth payload",
				    __func__);
				break;
			}
		}

		sa_stateflags(sa, IKED_REQ_AUTH);

		if (ikev2_ike_auth(env, sa) != 0)
			log_debug("%s: failed to send ike auth", __func__);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
ikev2_dispatch_control(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct iked		*env = iked_env;

	switch (imsg->hdr.type) {
	case IMSG_CTL_RESET_ID:
		ikev2_ctl_reset_id(env, imsg, imsg->hdr.type);
		break;
	case IMSG_CTL_SHOW_SA:
		ikev2_ctl_show_sa(env, imsg);
		break;
	case IMSG_CTL_SHOW_STATS:
		ikev2_ctl_show_stats(env, imsg);
		break;
	default:
		return (-1);
	}

	return (0);
}

/* try to delete established SA if no other exchange is active */
int
ikev2_ike_sa_delete(struct iked *env, struct iked_sa *sa)
{
	if (sa->sa_state != IKEV2_STATE_ESTABLISHED)
		return (-1);
	if (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF))
		return (-1);
	ikev2_disable_timer(env, sa);
	ikev2_ike_sa_setreason(sa, "reset sa control message");
	ikev2_ikesa_delete(env, sa, 1);
	timer_add(env, &sa->sa_timer, 0);
	return (0);
}

void
ikev2_ctl_reset_id(struct iked *env, struct imsg *imsg, unsigned int type)
{
	struct iked_sa			*sa;
	char				*reset_id = NULL;
	char				 sa_id[IKED_ID_SIZE];

	if ((reset_id = get_string(imsg->data, IMSG_DATA_SIZE(imsg))) == NULL)
		return;

	log_debug("%s: %s %d", __func__, reset_id, type);
	RB_FOREACH(sa, iked_sas, &env->sc_sas) {
		if (ikev2_print_id(IKESA_DSTID(sa), sa_id, sizeof(sa_id)) == -1)
			continue;
		if (strcmp(reset_id, sa_id) != 0)
			continue;
		if (sa->sa_state == IKEV2_STATE_CLOSED)
			continue;
		if (sa->sa_state == IKEV2_STATE_ESTABLISHED)
			ikev2_disable_timer(env, sa);
		log_info("%s: IKE SA %p id %s ispi %s rspi %s", __func__,
		    sa, sa_id,
		    print_spi(sa->sa_hdr.sh_ispi, 8),
		    print_spi(sa->sa_hdr.sh_rspi, 8));
		ikev2_ike_sa_setreason(sa, "reset control message");
		ikev2_ikesa_delete(env, sa, 1);
		/* default IKED_IKE_SA_DELETE_TIMEOUT is 120s, so switch to 6s */
		timer_add(env, &sa->sa_timer, 3 * IKED_RETRANSMIT_TIMEOUT);
	}
	free(reset_id);
}

void
ikev2_ctl_show_sa(struct iked *env, struct imsg *imsg)
{
	ikev2_info(env, imsg, 0);
}

void
ikev2_ctl_show_stats(struct iked *env, struct imsg *imsg)
{
	proc_compose_imsg(&env->sc_ps, PROC_CONTROL, -1,
	    IMSG_CTL_SHOW_STATS, imsg->hdr.peerid, -1,
	    &env->sc_stats, sizeof(env->sc_stats));
}

struct iked_sa *
ikev2_getimsgdata(struct iked *env, struct imsg *imsg, struct iked_sahdr *sh,
    uint8_t *type, uint8_t **buf, size_t *size)
{
	uint8_t		*ptr;
	size_t		 len;
	struct iked_sa	*sa;

	ptr = imsg->data;
	len = IMSG_DATA_SIZE(imsg);
	if (len < sizeof(*sh))
		fatalx("ikev2_getimsgdata: length too small for sh");
	memcpy(sh, ptr, sizeof(*sh));
	len -= sizeof(*sh);
	ptr += sizeof(*sh);
	if (len < sizeof(*type))
		fatalx("ikev2_getimsgdata: length too small for type");
	memcpy(type, ptr, sizeof(*type));
	len -= sizeof(*type);
	ptr += sizeof(*type);

	sa = sa_lookup(env, sh->sh_ispi, sh->sh_rspi, sh->sh_initiator);

	log_debug("%s: imsg %d rspi %s ispi %s initiator %d sa %s"
	    " type %d data length %zd",
	    __func__, imsg->hdr.type,
	    print_spi(sh->sh_rspi, 8),
	    print_spi(sh->sh_ispi, 8),
	    sh->sh_initiator,
	    sa == NULL ? "invalid" : "valid", *type, len);

	if (sa == NULL)
		return (NULL);

	*buf = ptr;
	*size = len;

	return (sa);
}

static time_t
gettime(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec;
}

void
ikev2_recv(struct iked *env, struct iked_message *msg)
{
	struct ike_header	*hdr;
	struct iked_sa		*sa;
	struct iked_msg_retransmit *mr;
	unsigned int		 initiator, flag = 0;
	int			 r;

	hdr = ibuf_seek(msg->msg_data, msg->msg_offset, sizeof(*hdr));

	if (hdr == NULL || ibuf_size(msg->msg_data) <
	    (betoh32(hdr->ike_length) - msg->msg_offset))
		return;

	ikestat_inc(env, ikes_msg_rcvd);

	initiator = (hdr->ike_flags & IKEV2_FLAG_INITIATOR) ? 0 : 1;
	msg->msg_response = (hdr->ike_flags & IKEV2_FLAG_RESPONSE) ? 1 : 0;
	msg->msg_exchange = hdr->ike_exchange;
	msg->msg_sa = sa_lookup(env,
	    betoh64(hdr->ike_ispi), betoh64(hdr->ike_rspi),
	    initiator);
	msg->msg_msgid = betoh32(hdr->ike_msgid);
	if (policy_lookup(env, msg, NULL, NULL, 0) != 0) {
		log_debug("%s: no compatible policy found", __func__);
		ikestat_inc(env, ikes_msg_rcvd_dropped);
		return;
	}

	logit(hdr->ike_exchange == IKEV2_EXCHANGE_INFORMATIONAL ?
	    LOG_DEBUG : LOG_INFO,
	    "%srecv %s %s %u peer %s local %s, %zu bytes, policy '%s'",
	    SPI_IH(hdr),
	    print_map(hdr->ike_exchange, ikev2_exchange_map),
	    msg->msg_response ? "res" : "req",
	    msg->msg_msgid,
	    print_addr(&msg->msg_peer),
	    print_addr(&msg->msg_local),
	    ibuf_size(msg->msg_data),
	    msg->msg_policy->pol_name);
	log_debug("%s: ispi %s rspi %s", __func__,
	    print_spi(betoh64(hdr->ike_ispi), 8),
	    print_spi(betoh64(hdr->ike_rspi), 8));

	if ((sa = msg->msg_sa) == NULL)
		goto done;

	sa->sa_last_recvd = gettime();

	if (hdr->ike_exchange == IKEV2_EXCHANGE_CREATE_CHILD_SA)
		flag = IKED_REQ_CHILDSA;
	if (hdr->ike_exchange == IKEV2_EXCHANGE_INFORMATIONAL)
		flag = IKED_REQ_INF;

	if (hdr->ike_exchange != IKEV2_EXCHANGE_IKE_SA_INIT &&
	    hdr->ike_nextpayload != IKEV2_PAYLOAD_SK &&
	    hdr->ike_nextpayload != IKEV2_PAYLOAD_SKF) {
		ikestat_inc(env, ikes_msg_rcvd_dropped);
		return;
	}

	if (msg->msg_response) {
		if (msg->msg_msgid > sa->sa_reqid) {
			ikestat_inc(env, ikes_msg_rcvd_dropped);
			return;
		}
		mr = ikev2_msg_lookup(env, &sa->sa_requests, msg,
		    hdr->ike_exchange);
		if (hdr->ike_exchange != IKEV2_EXCHANGE_INFORMATIONAL &&
		    mr == NULL && sa->sa_fragments.frag_count == 0) {
			ikestat_inc(env, ikes_msg_rcvd_dropped);
			return;
		}
		if (flag) {
			if ((sa->sa_stateflags & flag) == 0) {
				ikestat_inc(env, ikes_msg_rcvd_dropped);
				return;
			}
			/*
			 * We have initiated this exchange, even if
			 * we are not the initiator of the IKE SA.
			 */
			initiator = 1;
		}
		/*
		 * There's no need to keep the request (fragments) around
		 */
		if (mr != NULL && hdr->ike_nextpayload != IKEV2_PAYLOAD_SKF)
			ikev2_msg_dispose(env, &sa->sa_requests, mr);
	} else {
		/*
		 * IKE_SA_INIT is special since it always uses the message id 0.
		 * Even when the message was rejected, and the new message has
		 * different proposals, the id will be the same.  To discern
		 * retransmits and new messages, the RFC suggests to compare the
		 * the messages.
		 */
		if (sa->sa_state == IKEV2_STATE_CLOSED && sa->sa_1stmsg &&
		    hdr->ike_exchange == IKEV2_EXCHANGE_IKE_SA_INIT &&
		    msg->msg_msgid == 0 &&
		    (ibuf_size(msg->msg_data) != ibuf_size(sa->sa_1stmsg) ||
		    memcmp(ibuf_data(msg->msg_data), ibuf_data(sa->sa_1stmsg),
		    ibuf_size(sa->sa_1stmsg)) != 0)) {
			ikev2_ike_sa_setreason(sa, NULL);
			sa_free(env, sa);
			msg->msg_sa = sa = NULL;
			goto done;
		}
		if (msg->msg_msgid < sa->sa_msgid) {
			ikestat_inc(env, ikes_msg_rcvd_dropped);
			return;
		}
		if (flag)
			initiator = 0;
		/*
		 * See if we have responded to this request before
		 * For return values 0 and -1 we have.
		 */
		if ((r = ikev2_msg_retransmit_response(env, sa, msg, hdr))
		     != -2) {
			if (r == -1) {
				log_warn("%s: failed to retransmit a "
				    "response", __func__);
				ikev2_ike_sa_setreason(sa,
				    "retransmitting response failed");
				sa_free(env, sa);
			}
			return;
		} else if (sa->sa_msgid_set && msg->msg_msgid == sa->sa_msgid) {
			/*
			 * Response is being worked on, most likely we're
			 * waiting for the CA process to get back to us
			 */
			ikestat_inc(env, ikes_msg_rcvd_busy);
			return;
		}
		sa->sa_msgid_current = msg->msg_msgid;
	}

done:
	if (initiator)
		ikev2_init_recv(env, msg, hdr);
	else
		ikev2_resp_recv(env, msg, hdr);

	if (sa != NULL && !msg->msg_response && msg->msg_valid) {
		/*
		 * If it's a valid request, make sure to update the peer's
		 * message ID and dispose of all previous responses.
		 * We need to set sa_msgid_set in order to distinguish between
		 * "last msgid was 0" and "msgid not set yet".
		 */
		sa->sa_msgid = sa->sa_msgid_current;
		sa->sa_msgid_set = 1;
		ikev2_msg_prevail(env, &sa->sa_responses, msg);
	}

	if (sa != NULL && sa->sa_state == IKEV2_STATE_CLOSED) {
		log_debug("%s: closing SA", __func__);
		ikev2_ike_sa_setreason(sa, "closed");
		sa_free(env, sa);
	}
}

int
ikev2_ike_auth_compatible(struct iked_sa *sa, uint8_t policy, uint8_t wire)
{
	if (wire == IKEV2_AUTH_SIG_ANY)		/* internal, not on wire */
		return (-1);
	if (policy == wire || policy == IKEV2_AUTH_NONE)
		return (0);
	switch (policy) {
	case IKEV2_AUTH_SIG_ANY:
		switch (wire) {
		case IKEV2_AUTH_SIG:
		case IKEV2_AUTH_RSA_SIG:
		case IKEV2_AUTH_ECDSA_256:
		case IKEV2_AUTH_ECDSA_384:
		case IKEV2_AUTH_ECDSA_521:
			return (0);
		}
		break;
	case IKEV2_AUTH_SIG:
	case IKEV2_AUTH_RSA_SIG:
	case IKEV2_AUTH_ECDSA_256:
	case IKEV2_AUTH_ECDSA_384:
	case IKEV2_AUTH_ECDSA_521:
		switch (wire) {
		/*
		 * XXX Maybe we need an indication saying:
		 * XXX Accept AUTH_SIG as long as its DSA?
		 */
		case IKEV2_AUTH_SIG:
			if (sa->sa_sigsha2)
				return (0);
		}
		break;
	}
	return (-1);
}

int
ikev2_auth_verify(struct iked *env, struct iked_sa *sa)
{
	struct iked_auth	 ikeauth;
	struct ibuf		*authmsg;
	int			 ret;

	memcpy(&ikeauth, &sa->sa_policy->pol_auth,
	    sizeof(ikeauth));

	if (sa->sa_policy->pol_auth.auth_eap &&
	    sa->sa_eapmsk != NULL) {
		/*
		 * The initiator EAP auth is a PSK derived
		 * from the EAP-specific MSK
		 */
		ikeauth.auth_method = IKEV2_AUTH_SHARED_KEY_MIC;

		/* Copy session key as PSK */
		memcpy(ikeauth.auth_data,
		    ibuf_data(sa->sa_eapmsk),
		    ibuf_size(sa->sa_eapmsk));
		ikeauth.auth_length = ibuf_size(sa->sa_eapmsk);
	}

	if (ikev2_ike_auth_compatible(sa,
	    ikeauth.auth_method, sa->sa_peerauth.id_type) < 0) {
		log_warnx("%s: unexpected auth method %s, was "
		    "expecting %s", SPI_SA(sa, __func__),
		    print_map(sa->sa_peerauth.id_type,
		    ikev2_auth_map),
		    print_map(ikeauth.auth_method,
		    ikev2_auth_map));
		ikev2_send_auth_failed(env, sa);
		explicit_bzero(&ikeauth, sizeof(ikeauth));
		return (-1);
	}
	ikeauth.auth_method = sa->sa_peerauth.id_type;

	if ((authmsg = ikev2_msg_auth(env, sa,
	    sa->sa_hdr.sh_initiator)) == NULL) {
		log_debug("%s: failed to get auth data",
		    __func__);
		ikev2_send_auth_failed(env, sa);
		explicit_bzero(&ikeauth, sizeof(ikeauth));
		return (-1);
	}

	ret = ikev2_msg_authverify(env, sa, &ikeauth,
	    ibuf_data(sa->sa_peerauth.id_buf),
	    ibuf_size(sa->sa_peerauth.id_buf),
	    authmsg);
	ibuf_free(authmsg);
	if (ret != 0) {
		log_info("%s: ikev2_msg_authverify failed",
		    SPI_SA(sa, __func__));
		ikev2_send_auth_failed(env, sa);
		explicit_bzero(&ikeauth, sizeof(ikeauth));
		return (-1);
	}
	if (sa->sa_eapmsk != NULL) {
		if ((authmsg = ikev2_msg_auth(env, sa,
		    !sa->sa_hdr.sh_initiator)) == NULL) {
			log_debug("%s: failed to get auth data",
			    __func__);
			explicit_bzero(&ikeauth, sizeof(ikeauth));
			return (-1);
		}

		/* XXX 2nd AUTH for EAP messages */
		ret = ikev2_msg_authsign(env, sa, &ikeauth, authmsg);
		ibuf_free(authmsg);
		if (ret != 0) {
			ikev2_send_auth_failed(env, sa);
			explicit_bzero(&ikeauth, sizeof(ikeauth));
			return (-1);
		}

		/* ikev2_msg_authverify verified AUTH */
		sa_stateflags(sa, IKED_REQ_AUTHVALID);
		sa_stateflags(sa, IKED_REQ_EAPVALID);
		sa_state(env, sa, IKEV2_STATE_EAP_SUCCESS);
	}

	explicit_bzero(&ikeauth, sizeof(ikeauth));
	return (0);
}

int
ikev2_ike_auth_recv(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg)
{
	struct iked_id		*id;
	struct ibuf		*authmsg, *buf;
	struct iked_policy	*old;
	uint8_t			*cert = NULL;
	size_t			 certlen = 0;
	int			 certtype = IKEV2_CERT_NONE;
	int			 i;

	/* The AUTH payload indicates if the responder wants EAP or not */
	if (msg->msg_auth.id_type != IKEV2_AUTH_NONE &&
	    !sa_stateok(sa, IKEV2_STATE_EAP))
		sa_state(env, sa, IKEV2_STATE_AUTH_REQUEST);

	if (!sa->sa_hdr.sh_initiator &&
	    !sa_stateok(sa, IKEV2_STATE_AUTH_REQUEST) &&
	    sa->sa_policy->pol_auth.auth_eap)
		sa_state(env, sa, IKEV2_STATE_EAP);

	if (sa->sa_hdr.sh_initiator)
		id = &sa->sa_rid;
	else
		id = &sa->sa_iid;

	/* try to relookup the policy based on the peerid */
	if (msg->msg_peerid.id_type && !sa->sa_hdr.sh_initiator) {
		old = sa->sa_policy;

		sa->sa_policy = NULL;
		if (policy_lookup(env, msg, &sa->sa_proposals, NULL, 0) != 0 ||
		    msg->msg_policy == NULL) {
			log_info("%s: no compatible policy found",
			    SPI_SA(sa, __func__));
			ikev2_send_auth_failed(env, sa);
			TAILQ_REMOVE(&old->pol_sapeers, sa, sa_peer_entry);
			policy_unref(env, old);
			return (-1);
		}
		if (msg->msg_policy != old) {
			/* Clean up old policy */
			TAILQ_REMOVE(&old->pol_sapeers, sa, sa_peer_entry);
			policy_unref(env, old);

			/* Update SA with new policy*/
			if (sa_new(env, sa->sa_hdr.sh_ispi,
			    sa->sa_hdr.sh_rspi, 0, msg->msg_policy) != sa) {
				log_warnx("%s: failed to update SA",
				    SPI_SA(sa, __func__));
				ikev2_send_auth_failed(env, sa);
				return (-1);
			}
		} else {
			/* restore */
			msg->msg_policy = sa->sa_policy = old;
		}
		if (ikev2_handle_certreq(env, msg) != 0)
			return (-1);
	} else if (sa->sa_hdr.sh_initiator) {
		old = sa->sa_policy;

		/* verify policy on initiator */
		sa->sa_policy = NULL;
		if (policy_lookup(env, msg, &sa->sa_proposals, &old->pol_flows,
		    old->pol_nflows) != 0 || msg->msg_policy != old) {

			/* get dstid */
			if (msg->msg_peerid.id_type) {
				memcpy(id, &msg->msg_peerid, sizeof(*id));
				bzero(&msg->msg_peerid, sizeof(msg->msg_peerid));
			}
			log_warnx("%s: policy mismatch", SPI_SA(sa, __func__));
			ikev2_send_auth_failed(env, sa);
			TAILQ_REMOVE(&old->pol_sapeers, sa, sa_peer_entry);
			policy_unref(env, old);
			return (-1);
		}
		/* restore */
		msg->msg_policy = sa->sa_policy = old;
	}

	/* AUTH payload is required for non-EAP */
	if (!msg->msg_auth.id_type &&
	    !sa->sa_policy->pol_auth.auth_eap) {
		/* get dstid */
		if (msg->msg_peerid.id_type) {
			memcpy(id, &msg->msg_peerid, sizeof(*id));
			bzero(&msg->msg_peerid, sizeof(msg->msg_peerid));
		}
		log_debug("%s: missing auth payload", SPI_SA(sa, __func__));
		ikev2_send_auth_failed(env, sa);
		return (-1);
	}

	if (msg->msg_peerid.id_type) {
		memcpy(id, &msg->msg_peerid, sizeof(*id));
		bzero(&msg->msg_peerid, sizeof(msg->msg_peerid));

		if (!sa->sa_hdr.sh_initiator) {
			if ((authmsg = ikev2_msg_auth(env, sa,
			    !sa->sa_hdr.sh_initiator)) == NULL) {
				log_debug("%s: failed to get response "
				    "auth data", __func__);
				return (-1);
			}

			ca_setauth(env, sa, authmsg, PROC_CERT);
			ibuf_free(authmsg);
		}
	}

	/* Encode all received certs as single blob */
	if (msg->msg_cert.id_type != IKEV2_CERT_BUNDLE &&
	    msg->msg_scert[0].id_type != IKEV2_CERT_NONE) {
		if ((buf = ibuf_new(NULL, 0)) == NULL)
			return (-1);
		/* begin with certificate */
		if (ca_certbundle_add(buf, &msg->msg_cert) != 0) {
			ibuf_free(buf);
			return (-1);
		}
		/* add intermediate CAs */
		for (i = 0; i < IKED_SCERT_MAX; i++) {
			if (msg->msg_scert[i].id_type == IKEV2_CERT_NONE)
				break;
			if (ca_certbundle_add(buf, &msg->msg_scert[i]) != 0) {
				ibuf_free(buf);
				return (-1);
			}
		}
		ibuf_free(msg->msg_cert.id_buf);
		msg->msg_cert.id_buf = buf;
		msg->msg_cert.id_type = IKEV2_CERT_BUNDLE;
	}

	if (!TAILQ_EMPTY(&msg->msg_proposals)) {
		if (proposals_negotiate(&sa->sa_proposals,
		    &sa->sa_policy->pol_proposals, &msg->msg_proposals,
		    0, -1) != 0) {
			log_info("%s: no proposal chosen", __func__);
			msg->msg_error = IKEV2_N_NO_PROPOSAL_CHOSEN;
			ikestat_inc(env, ikes_sa_proposals_negotiate_failures);
			return (-1);
		} else
			sa_stateflags(sa, IKED_REQ_SA);
	}

	if (msg->msg_auth.id_type) {
		memcpy(&sa->sa_peerauth, &msg->msg_auth, sizeof(sa->sa_peerauth));
		bzero(&msg->msg_auth, sizeof(msg->msg_auth));
	}

	if (msg->msg_cp) {
		if (msg->msg_cp_addr) {
			sa->sa_cp_addr = msg->msg_cp_addr;
			msg->msg_cp_addr = NULL;
		}
		if (msg->msg_cp_addr6) {
			sa->sa_cp_addr6 = msg->msg_cp_addr6;
			msg->msg_cp_addr6 = NULL;
		}
		if (msg->msg_cp_dns) {
			sa->sa_cp_dns = msg->msg_cp_dns;
			msg->msg_cp_dns = NULL;
		}
		sa->sa_cp = msg->msg_cp;
	}

	/* For EAP and PSK AUTH can be verified without the CA process*/
	if ((sa->sa_policy->pol_auth.auth_eap &&
	    sa->sa_eapmsk != NULL) ||
	    sa->sa_policy->pol_auth.auth_method == IKEV2_AUTH_SHARED_KEY_MIC)
		ikev2_auth_verify(env, sa);
	/* For CERT and Pubkey AUTH the CA process must find a matching key */
	else if (sa->sa_peerauth.id_type) {
		if (msg->msg_cert.id_type) {
			certtype = msg->msg_cert.id_type;
			cert = ibuf_data(msg->msg_cert.id_buf);
			certlen = ibuf_size(msg->msg_cert.id_buf);
		}
		sa->sa_stateflags &= ~IKED_REQ_CERTVALID;
		if (ca_setcert(env, &sa->sa_hdr, id, certtype, cert, certlen, PROC_CERT) == -1)
			return (-1);
	}

	if (sa->sa_cp == IKEV2_CP_REPLY) {
		if (sa->sa_cp_addr)
			log_info("%s: obtained lease: %s", SPI_SA(sa, __func__),
			    print_addr(&sa->sa_cp_addr->addr));
		if (sa->sa_cp_addr6)
			log_info("%s: obtained lease: %s", SPI_SA(sa, __func__),
			    print_addr(&sa->sa_cp_addr6->addr));
		if (sa->sa_cp_dns)
			log_info("%s: obtained DNS: %s", SPI_SA(sa, __func__),
			    print_addr(&sa->sa_cp_dns->addr));
	}

	return ikev2_ike_auth(env, sa);
}

int
ikev2_ike_auth(struct iked *env, struct iked_sa *sa)
{
	/* Attempt state transition */
	if (sa->sa_state == IKEV2_STATE_EAP_SUCCESS)
		sa_state(env, sa, IKEV2_STATE_EAP_VALID);
	else if (sa->sa_state == IKEV2_STATE_AUTH_SUCCESS)
		sa_state(env, sa, IKEV2_STATE_VALID);

	if (sa->sa_hdr.sh_initiator) {
		if (sa_stateok(sa, IKEV2_STATE_AUTH_SUCCESS))
			return (ikev2_init_done(env, sa));
		/* AUTH exchange is awaiting response from CA process, ignore */
		else if (sa_stateok(sa, IKEV2_STATE_AUTH_REQUEST))
			return (0);
		else
			return (ikev2_init_ike_auth(env, sa));
	}
	return (ikev2_resp_ike_auth(env, sa));
}

void
ikev2_init_recv(struct iked *env, struct iked_message *msg,
    struct ike_header *hdr)
{
	struct iked_sa		*sa;
	struct iked_policy	*pol;

	if (ikev2_msg_valid_ike_sa(env, hdr, msg) == -1) {
		log_debug("%s: unknown SA", __func__);
		return;
	}
	sa = msg->msg_sa;

	switch (hdr->ike_exchange) {
	case IKEV2_EXCHANGE_IKE_SA_INIT:
		/* Update the SPIs */
		if ((sa = sa_new(env,
		    betoh64(hdr->ike_ispi), betoh64(hdr->ike_rspi), 1,
		    NULL)) == NULL || sa != msg->msg_sa) {
			log_debug("%s: invalid new SA", __func__);
			if (sa) {
				ikev2_ike_sa_setreason(sa, "invalid new SA");
				sa_free(env, sa);
			}
			return;
		}
		break;
	case IKEV2_EXCHANGE_IKE_AUTH:
	case IKEV2_EXCHANGE_CREATE_CHILD_SA:
	case IKEV2_EXCHANGE_INFORMATIONAL:
		break;
	default:
		log_debug("%s: unsupported exchange: %s", __func__,
		    print_map(hdr->ike_exchange, ikev2_exchange_map));
		return;
	}

	if (ikev2_pld_parse(env, hdr, msg, msg->msg_offset) != 0) {
		log_debug("%s: failed to parse message", __func__);
		return;
	}

	if (sa_address(sa, &sa->sa_peer, (struct sockaddr *)&msg->msg_peer)
	    == -1 ||
	    sa_address(sa, &sa->sa_local, (struct sockaddr *)&msg->msg_local)
	    == -1) {
		ikestat_inc(env, ikes_msg_rcvd_dropped);
		return;
	}
	sa->sa_fd = msg->msg_fd;
	log_debug("%s: updated SA to peer %s local %s", __func__,
	    print_addr(&sa->sa_peer.addr), print_addr(&sa->sa_local.addr));

	if (sa->sa_fragments.frag_count != 0)
		return;

	if (!ikev2_msg_frompeer(msg))
		return;

	if (ikev2_handle_notifies(env, msg) != 0)
		return;

	if (msg->msg_nat_detected && sa->sa_natt == 0)
		ikev2_enable_natt(env, sa, msg, 1);

	switch (hdr->ike_exchange) {
	case IKEV2_EXCHANGE_IKE_SA_INIT:
		if (ibuf_length(msg->msg_cookie)) {
			pol = sa->sa_policy;
			if (ikev2_init_ike_sa_peer(env, pol,
			    &pol->pol_peer, msg) != 0)
				log_warnx("%s: failed to initiate a "
				    "IKE_SA_INIT exchange", SPI_SA(sa,
				    __func__));
			break;
		}
		if (msg->msg_flags & IKED_MSG_FLAGS_NO_PROPOSAL_CHOSEN) {
			log_info("%s: failed to negotiate IKE SA",
			    SPI_SA(sa, __func__));
			ikev2_ike_sa_setreason(sa, "no proposal chosen");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			return;
		}
		if (ikev2_handle_certreq(env, msg) != 0)
			return;

		if (ikev2_init_auth(env, msg) != 0) {
			ikev2_ike_sa_setreason(sa,
			    "failed to initiate IKE_AUTH exchange");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			return;
		}
		break;
	case IKEV2_EXCHANGE_IKE_AUTH:
		if (msg->msg_flags & IKED_MSG_FLAGS_AUTHENTICATION_FAILED) {
			log_debug("%s: AUTHENTICATION_FAILED, closing SA",
			    __func__);
			ikev2_log_cert_info(SPI_SA(sa, __func__),
			    sa->sa_hdr.sh_initiator ? &sa->sa_rcert
			    : &sa->sa_icert);
			ikev2_ike_sa_setreason(sa,
			    "authentication failed notification from peer");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			return;
		}
		if (msg->msg_flags & IKED_MSG_FLAGS_NO_PROPOSAL_CHOSEN) {
			log_info("%s: failed to negotiate IKE SA",
			    SPI_SA(sa, __func__));
			ikev2_ike_sa_setreason(sa, "no proposal chosen (IKE SA)");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			return;
		}

		(void)ikev2_ike_auth_recv(env, sa, msg);
		break;
	case IKEV2_EXCHANGE_CREATE_CHILD_SA:
		if (msg->msg_flags & IKED_MSG_FLAGS_NO_PROPOSAL_CHOSEN) {
			log_info("%s: CREATE_CHILD_SA failed",
			    SPI_SA(sa, __func__));
			ikev2_ike_sa_setreason(sa, "no proposal chosen (CHILD SA)");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			return;
		}
		(void)ikev2_init_create_child_sa(env, msg);
		break;
	case IKEV2_EXCHANGE_INFORMATIONAL:
		sa->sa_stateflags &= ~IKED_REQ_INF;
		break;
	default:
		log_debug("%s: exchange %s not implemented", __func__,
		    print_map(hdr->ike_exchange, ikev2_exchange_map));
		break;
	}
}

void
ikev2_enable_natt(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg, int udpencap)
{
	struct iked_socket	*sock;
	in_port_t		 port;

	sock = ikev2_msg_getsocket(env, sa->sa_local.addr_af, 1);
	if (sock == NULL)
		return;

	/*
	 * Update address information and use the NAT-T
	 * port and socket, if available.
	 */
	port = htons(socket_getport(
	    (struct sockaddr *)&sock->sock_addr));
	sa->sa_local.addr_port = port;
	sa->sa_peer.addr_port = port;
	(void)socket_af((struct sockaddr *)&sa->sa_local.addr, port);
	(void)socket_af((struct sockaddr *)&sa->sa_peer.addr, port);

	msg->msg_fd = sa->sa_fd = sock->sock_fd;
	msg->msg_sock = sock;
	sa->sa_natt = 1;
	if (udpencap)
		sa->sa_udpencap = 1;

	log_debug("%s: detected NAT, enabling UDP encapsulation,"
	    " updated SA to peer %s local %s", __func__,
	    print_addr(&sa->sa_peer.addr), print_addr(&sa->sa_local.addr));
}

void
ikev2_init_ike_sa(struct iked *env, void *arg)
{
	struct iked_policy	*pol;

	TAILQ_FOREACH(pol, &env->sc_policies, pol_entry) {
		if ((pol->pol_flags & IKED_POLICY_ACTIVE) == 0)
			continue;
		if (!TAILQ_EMPTY(&pol->pol_sapeers)) {
			log_debug("%s: \"%s\" is already active",
			    __func__, pol->pol_name);
			continue;
		}

		log_info("%s: initiating \"%s\"", __func__, pol->pol_name);

		if (ikev2_init_ike_sa_peer(env, pol, &pol->pol_peer, NULL))
			log_debug("%s: failed to initiate with peer %s",
			    __func__, print_addr(&pol->pol_peer.addr));
	}

	timer_set(env, &env->sc_inittmr, ikev2_init_ike_sa, NULL);
	timer_add(env, &env->sc_inittmr, IKED_INITIATOR_INTERVAL);
}

void
ikev2_init_ike_sa_timeout(struct iked *env, void *arg)
{
	struct iked_sa	 *sa = arg;

	log_debug("%s: ispi %s rspi %s", __func__,
	    print_spi(sa->sa_hdr.sh_ispi, 8),
	    print_spi(sa->sa_hdr.sh_rspi, 8));

	ikev2_ike_sa_setreason(sa, "SA_INIT timeout");
	sa_free(env, sa);
}

int
ikev2_init_ike_sa_peer(struct iked *env, struct iked_policy *pol,
    struct iked_addr *peer, struct iked_message *retry)
{
	struct sockaddr_storage		 ss;
	struct iked_message		 req;
	struct ike_header		*hdr;
	struct ikev2_payload		*pld;
	struct ikev2_keyexchange	*ke;
	struct ikev2_notify		*n;
	struct iked_sa			*sa = NULL;
	struct ibuf			*buf, *cookie = NULL, *vendor_id = NULL;
	struct dh_group			*group;
	ssize_t				 len;
	int				 ret = -1;
	struct iked_socket		*sock;
	in_port_t			 port;

	if ((sock = ikev2_msg_getsocket(env, peer->addr_af, 0)) == NULL)
		return (-1);

	if (retry != NULL) {
		sa = retry->msg_sa;
		cookie = retry->msg_cookie;
		sa_state(env, sa, IKEV2_STATE_INIT);
	}

	/* Create a new initiator SA */
	if (sa == NULL &&
	    (sa = sa_new(env, 0, 0, 1, pol)) == NULL)
		return (-1);

	/* Pick peer's DH group if asked */
	if (pol->pol_peerdh > 0 && sa->sa_dhgroup == NULL &&
	    (sa->sa_dhgroup = group_get(pol->pol_peerdh)) == NULL) {
		log_warnx("%s: invalid peer DH group %u", SPI_SA(sa, __func__),
		    pol->pol_peerdh);
		goto closeonly;
	}
	sa->sa_reqid = 0;

	if (ikev2_sa_initiator(env, sa, NULL, NULL) == -1)
		goto closeonly;

	if (pol->pol_local.addr.ss_family == AF_UNSPEC) {
		if (socket_getaddr(sock->sock_fd, &ss) == -1)
			goto closeonly;
	} else
		memcpy(&ss, &pol->pol_local.addr, pol->pol_local.addr.ss_len);

	if ((buf = ikev2_msg_init(env, &req, &peer->addr, peer->addr.ss_len,
	    &ss, ss.ss_len, 0)) == NULL)
		goto done;

	/* Inherit the port from the 1st send socket */
	port = htons(socket_getport((struct sockaddr *)&sock->sock_addr));
	(void)socket_af((struct sockaddr *)&req.msg_local, port);
	(void)socket_af((struct sockaddr *)&req.msg_peer, port);

	req.msg_fd = sock->sock_fd;
	req.msg_sa = sa;
	req.msg_sock = sock;
	req.msg_msgid = ikev2_msg_id(env, sa);

	/* IKE header */
	if ((hdr = ikev2_add_header(buf, sa, req.msg_msgid,
	    cookie == NULL ? IKEV2_PAYLOAD_SA : IKEV2_PAYLOAD_NOTIFY,
	    IKEV2_EXCHANGE_IKE_SA_INIT, 0)) == NULL)
		goto done;

	/* Reflect COOKIE */
	if (cookie) {
		if ((pld = ikev2_add_payload(buf)) == NULL)
			goto done;
		if ((n = ibuf_reserve(buf, sizeof(*n))) == NULL)
			goto done;
		n->n_protoid = IKEV2_SAPROTO_NONE;
		n->n_spisize = 0;
		n->n_type = htobe16(IKEV2_N_COOKIE);
		if (ikev2_add_buf(buf, cookie) == -1)
			goto done;
		len = sizeof(*n) + ibuf_size(cookie);

		log_debug("%s: added cookie, len %zu", __func__,
		    ibuf_size(cookie));
		print_hexbuf(cookie);

		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_SA) == -1)
			goto done;
	}

	/* SA payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if ((len = ikev2_add_proposals(env, sa, buf, &pol->pol_proposals,
	    IKEV2_SAPROTO_IKE, sa->sa_hdr.sh_initiator, 0, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_KE) == -1)
		goto done;

	/* KE payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if ((ke = ibuf_reserve(buf, sizeof(*ke))) == NULL)
		goto done;
	if ((group = sa->sa_dhgroup) == NULL) {
		log_debug("%s: invalid dh", __func__);
		goto done;
	}
	ke->kex_dhgroup = htobe16(group->id);
	if (ikev2_add_buf(buf, sa->sa_dhiexchange) == -1)
		goto done;
	len = sizeof(*ke) + ibuf_size(sa->sa_dhiexchange);

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONCE) == -1)
		goto done;

	/* NONCE payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if (ikev2_add_buf(buf, sa->sa_inonce) == -1)
		goto done;
	len = ibuf_size(sa->sa_inonce);

	if (env->sc_vendorid != 0) {
		vendor_id = ibuf_new(IKED_VENDOR_ID, strlen(IKED_VENDOR_ID));
		ibuf_add(vendor_id, IKED_VERSION, strlen(IKED_VERSION));
		if ((len = ikev2_add_vendor_id(buf, &pld, len, vendor_id))
		    == -1)
			goto done;
	}

	/* Fragmentation Notify */
	if (env->sc_frag) {
		if ((len = ikev2_add_fragmentation(buf, &pld, len))
		    == -1)
			goto done;
	}

	if (env->sc_nattmode != NATT_DISABLE) {
		if (ntohs(port) == env->sc_nattport) {
			/* Enforce NAT-T on the initiator side */
			log_debug("%s: enforcing NAT-T", __func__);
			req.msg_natt = sa->sa_natt = sa->sa_udpencap = 1;
		}
		if ((len = ikev2_add_nat_detection(env, buf, &pld, &req, len))
		    == -1)
			goto done;
	}

	if ((len = ikev2_add_sighashnotify(buf, &pld, len)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	if (ikev2_set_header(hdr, ibuf_size(buf) - sizeof(*hdr)) == -1)
		goto done;

	(void)ikev2_pld_parse(env, hdr, &req, 0);

	ibuf_free(sa->sa_1stmsg);
	if ((sa->sa_1stmsg = ibuf_dup(buf)) == NULL) {
		log_debug("%s: failed to copy 1st message", __func__);
		goto done;
	}

	if ((ret = ikev2_msg_send(env, &req)) == 0)
		sa_state(env, sa, IKEV2_STATE_SA_INIT);

	/* Setup exchange timeout. */
	timer_set(env, &sa->sa_timer, ikev2_init_ike_sa_timeout, sa);
	timer_add(env, &sa->sa_timer, IKED_IKE_SA_EXCHANGE_TIMEOUT);

 done:
	ikev2_msg_cleanup(env, &req);
 closeonly:
	if (ret == -1) {
		log_debug("%s: closing SA", __func__);
		ikev2_ike_sa_setreason(sa, "failed to send SA_INIT");
		sa_free(env, sa);
	}
	ibuf_free(vendor_id);

	return (ret);
}

int
ikev2_init_auth(struct iked *env, struct iked_message *msg)
{
	struct iked_sa			*sa = msg->msg_sa;
	struct ibuf			*authmsg;

	if (sa == NULL)
		return (-1);

	if (ikev2_sa_initiator(env, sa, NULL, msg) == -1) {
		log_info("%s: failed to get IKE keys", SPI_SA(sa, __func__));
		return (-1);
	}

	if ((authmsg = ikev2_msg_auth(env, sa,
	    !sa->sa_hdr.sh_initiator)) == NULL) {
		log_info("%s: failed to get auth data", SPI_SA(sa, __func__));
		return (-1);
	}

	if (ca_setauth(env, sa, authmsg, PROC_CERT) == -1) {
		log_info("%s: failed to get cert", SPI_SA(sa, __func__));
		ibuf_free(authmsg);
		return (-1);
	}
	ibuf_free(authmsg);

	return (ikev2_init_ike_auth(env, sa));
}

int
ikev2_init_ike_auth(struct iked *env, struct iked_sa *sa)
{
	struct iked_policy		*pol = sa->sa_policy;
	struct ikev2_payload		*pld;
	struct ikev2_cert		*cert;
	struct ikev2_auth		*auth;
	struct iked_id			*id, *certid, peerid;
	struct ibuf			*e = NULL;
	uint8_t				 firstpayload;
	int				 ret = -1;
	ssize_t				 len;
	int				 i;

	if (!sa_stateok(sa, IKEV2_STATE_SA_INIT))
		return (0);

	if (!sa->sa_localauth.id_type) {
		log_debug("%s: no local auth", __func__);
		return (0);
	}

	bzero(&peerid, sizeof(peerid));

	/* New encrypted message buffer */
	if ((e = ibuf_static()) == NULL)
		goto done;

	id = &sa->sa_iid;
	certid = &sa->sa_icert;

	/* ID payloads */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	firstpayload = IKEV2_PAYLOAD_IDi;
	if (ibuf_add_ibuf(e, id->id_buf) != 0)
		goto done;
	len = ibuf_size(id->id_buf);

	if (pol->pol_peerid.id_type) {
		if (ikev2_policy2id(&pol->pol_peerid, &peerid, 0) != 0) {
			log_debug("%s: failed to get remote id", __func__);
			goto done;
		}
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_IDr) == -1)
			goto done;
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if (ibuf_add_ibuf(e, peerid.id_buf) != 0)
			goto done;
		len = ibuf_size(peerid.id_buf);
	}

	/* CERT payload */
	if ((sa->sa_stateinit & IKED_REQ_CERT) &&
	    (certid->id_type != IKEV2_CERT_NONE)) {
		if (ikev2_next_payload(pld, len,
		    IKEV2_PAYLOAD_CERT) == -1)
			goto done;
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((cert = ibuf_reserve(e, sizeof(*cert))) == NULL)
			goto done;
		cert->cert_type = certid->id_type;
		if (ibuf_add_ibuf(e, certid->id_buf) != 0)
			goto done;
		len = ibuf_size(certid->id_buf) + sizeof(*cert);

		for (i = 0; i < IKED_SCERT_MAX; i++) {
			if (sa->sa_scert[i].id_type == IKEV2_CERT_NONE)
				break;
			if (ikev2_next_payload(pld, len,
			    IKEV2_PAYLOAD_CERT) == -1)
				goto done;
			if ((pld = ikev2_add_payload(e)) == NULL)
				goto done;
			if ((cert = ibuf_reserve(e, sizeof(*cert))) == NULL)
				goto done;
			cert->cert_type = sa->sa_scert[i].id_type;
			if (ibuf_add_ibuf(e, sa->sa_scert[i].id_buf) != 0)
				goto done;
			len = ibuf_size(sa->sa_scert[i].id_buf) + sizeof(*cert);
		}

		/* CERTREQ payload(s) */
		if ((len = ikev2_add_certreq(e, &pld,
		    len, env->sc_certreq, env->sc_certreqtype)) == -1)
			goto done;

		if (env->sc_certreqtype != pol->pol_certreqtype &&
		    (len = ikev2_add_certreq(e, &pld,
		    len, NULL, pol->pol_certreqtype)) == -1)
			goto done;
	}

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_AUTH) == -1)
		goto done;

	/* AUTH payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((auth = ibuf_reserve(e, sizeof(*auth))) == NULL)
		goto done;
	auth->auth_method = sa->sa_localauth.id_type;
	if (ibuf_add_ibuf(e, sa->sa_localauth.id_buf) != 0)
		goto done;
	len = ibuf_size(sa->sa_localauth.id_buf) + sizeof(*auth);

	/* CP payload */
	if (ikev2_cp_request_configured(sa)) {
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_CP) == -1)
			goto done;
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((len = ikev2_init_add_cp(env, sa, e)) == -1)
			goto done;
	}

	if ((pol->pol_flags & IKED_POLICY_IPCOMP) &&
	    (len = ikev2_add_ipcompnotify(env, e, &pld, len, sa, 1)) == -1)
		goto done;
	if ((pol->pol_flags & IKED_POLICY_TRANSPORT) &&
	    (len = ikev2_add_transport_mode(env, e, &pld, len, sa)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_SA) == -1)
		goto done;

	/* SA payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((len = ikev2_add_proposals(env, sa, e, &pol->pol_proposals, 0,
	    sa->sa_hdr.sh_initiator, 0, 1)) == -1)
		goto done;

	if ((len = ikev2_add_ts(e, &pld, len, sa, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	ret = ikev2_msg_send_encrypt(env, sa, &e,
	    IKEV2_EXCHANGE_IKE_AUTH, firstpayload, 0);

 done:
	ibuf_free(e);
	ibuf_free(peerid.id_buf);

	return (ret);
}

void
ikev2_enable_timer(struct iked *env, struct iked_sa *sa)
{
	sa->sa_last_recvd = gettime();
	timer_set(env, &sa->sa_timer, ikev2_ike_sa_alive, sa);
	if (env->sc_alive_timeout > 0)
		timer_add(env, &sa->sa_timer, env->sc_alive_timeout);
	timer_set(env, &sa->sa_keepalive, ikev2_ike_sa_keepalive, sa);
	if (sa->sa_usekeepalive)
		timer_add(env, &sa->sa_keepalive,
		    IKED_IKE_SA_KEEPALIVE_TIMEOUT);
	timer_set(env, &sa->sa_rekey, ikev2_ike_sa_rekey, sa);
	if (sa->sa_policy->pol_rekey)
		ikev2_ike_sa_rekey_schedule(env, sa);
}

void
ikev2_reset_alive_timer(struct iked *env)
{
	struct iked_sa			*sa;

	RB_FOREACH(sa, iked_sas, &env->sc_sas) {
		if (sa->sa_state != IKEV2_STATE_ESTABLISHED)
			continue;
		timer_del(env, &sa->sa_timer);
		if (env->sc_alive_timeout > 0)
			timer_add(env, &sa->sa_timer, env->sc_alive_timeout);
	}
}

void
ikev2_disable_timer(struct iked *env, struct iked_sa *sa)
{
	timer_del(env, &sa->sa_timer);
	timer_del(env, &sa->sa_keepalive);
	timer_del(env, &sa->sa_rekey);
}

int
ikev2_init_done(struct iked *env, struct iked_sa *sa)
{
	int		 ret;

	if (!sa_stateok(sa, IKEV2_STATE_VALID))
		return (0);	/* ignored */

	ret = ikev2_childsa_negotiate(env, sa, &sa->sa_kex, &sa->sa_proposals,
	    sa->sa_hdr.sh_initiator, 0);
	if (ret == 0)
		ret = ikev2_childsa_enable(env, sa);
	if (ret == 0) {
		sa_state(env, sa, IKEV2_STATE_ESTABLISHED);
		iked_radius_acct_start(env, sa);
		/* Delete exchange timeout. */
		timer_del(env, &sa->sa_timer);
		ikev2_enable_timer(env, sa);
		ikev2_log_established(sa);
		ikev2_record_dstid(env, sa);
		sa_configure_iface(env, sa, 1);
	}

	if (ret)
		ikev2_childsa_delete(env, sa, 0, 0, NULL, 1);
	return (ret);
}

int
ikev2_policy2id(struct iked_static_id *polid, struct iked_id *id, int srcid)
{
	struct ikev2_id		 hdr;
	struct iked_static_id	 localpid;
	char			 idstr[IKED_ID_SIZE];
	struct in_addr		 in4;
	struct in6_addr		 in6;
	X509_NAME		*name = NULL;
	uint8_t			*p;
	int			 len;

	/* Fixup the local Id if not specified */
	if (srcid && polid->id_type == 0) {
		polid = &localpid;
		bzero(polid, sizeof(*polid));

		/* Create a default local ID based on our FQDN */
		polid->id_type = IKEV2_ID_FQDN;
		if (gethostname((char *)polid->id_data,
		    sizeof(polid->id_data)) != 0)
			return (-1);
		polid->id_offset = 0;
		polid->id_length =
		    strlen((char *)polid->id_data); /* excluding NUL */
	}

	if (!polid->id_length)
		return (-1);

	/* Create an IKEv2 ID payload */
	bzero(&hdr, sizeof(hdr));
	hdr.id_type = id->id_type = polid->id_type;
	id->id_offset = sizeof(hdr);

	ibuf_free(id->id_buf);
	if ((id->id_buf = ibuf_new(&hdr, sizeof(hdr))) == NULL)
		return (-1);

	switch (id->id_type) {
	case IKEV2_ID_IPV4:
		if (inet_pton(AF_INET, (char *)polid->id_data, &in4) != 1 ||
		    ibuf_add(id->id_buf, &in4, sizeof(in4)) != 0) {
			ibuf_free(id->id_buf);
			id->id_buf = NULL;
			return (-1);
		}
		break;
	case IKEV2_ID_IPV6:
		if (inet_pton(AF_INET6, (char *)polid->id_data, &in6) != 1 ||
		    ibuf_add(id->id_buf, &in6, sizeof(in6)) != 0) {
			ibuf_free(id->id_buf);
			id->id_buf = NULL;
			return (-1);
		}
		break;
	case IKEV2_ID_ASN1_DN:
		/* policy has ID in string-format, convert to ASN1 */
		if ((name = ca_x509_name_parse(polid->id_data)) == NULL ||
		    (len = i2d_X509_NAME(name, NULL)) < 0 ||
		    (p = ibuf_reserve(id->id_buf, len)) == NULL ||
		    (i2d_X509_NAME(name, &p)) < 0) {
			if (name)
				X509_NAME_free(name);
			ibuf_free(id->id_buf);
			id->id_buf = NULL;
			return (-1);
		}
		X509_NAME_free(name);
		break;
	default:
		if (ibuf_add(id->id_buf,
		    polid->id_data, polid->id_length) != 0) {
			ibuf_free(id->id_buf);
			id->id_buf = NULL;
			return (-1);
		}
		break;
	}

	if (ikev2_print_id(id, idstr, sizeof(idstr)) == -1)
		return (-1);

	log_debug("%s: %s %s length %zu", __func__,
	    srcid ? "srcid" : "dstid",
	    idstr, ibuf_size(id->id_buf));

	return (0);
}

struct ike_header *
ikev2_add_header(struct ibuf *buf, struct iked_sa *sa,
    uint32_t msgid, uint8_t nextpayload,
    uint8_t exchange, uint8_t flags)
{
	struct ike_header	*hdr;

	if ((hdr = ibuf_reserve(buf, sizeof(*hdr))) == NULL) {
		log_debug("%s: failed to add header", __func__);
		return (NULL);
	}

	hdr->ike_ispi = htobe64(sa->sa_hdr.sh_ispi);
	hdr->ike_rspi = htobe64(sa->sa_hdr.sh_rspi);
	hdr->ike_nextpayload = nextpayload;
	hdr->ike_version = IKEV2_VERSION;
	hdr->ike_exchange = exchange;
	hdr->ike_msgid = htobe32(msgid);
	hdr->ike_length = htobe32(sizeof(*hdr));
	hdr->ike_flags = flags;

	if (sa->sa_hdr.sh_initiator)
		hdr->ike_flags |= IKEV2_FLAG_INITIATOR;

	return (hdr);
}

int
ikev2_set_header(struct ike_header *hdr, size_t length)
{
	uint32_t	 hdrlength = sizeof(*hdr) + length;

	if (hdrlength > UINT32_MAX) {
		log_debug("%s: message too long", __func__);
		return (-1);
	}

	hdr->ike_length = htobe32(sizeof(*hdr) + length);

	return (0);
}

struct ikev2_payload *
ikev2_add_payload(struct ibuf *buf)
{
	struct ikev2_payload	*pld;

	if ((pld = ibuf_reserve(buf, sizeof(*pld))) == NULL) {
		log_debug("%s: failed to add payload", __func__);
		return (NULL);
	}

	pld->pld_nextpayload = IKEV2_PAYLOAD_NONE;
	pld->pld_length = sizeof(*pld);

	return (pld);
}

ssize_t
ikev2_add_ts_payload(struct ibuf *buf, unsigned int type, struct iked_sa *sa)
{
	struct iked_policy	*pol = sa->sa_policy;
	struct ikev2_tsp	*tsp;
	struct ikev2_ts		*ts;
	struct iked_addr	*addr;
	struct iked_addr	 pooladdr;
	uint8_t			*ptr;
	size_t			 len = 0;
	uint32_t		 av[4], bv[4], mv[4];
	struct sockaddr_in	*in4;
	struct sockaddr_in6	*in6;
	struct iked_tss		*tss;
	struct iked_ts		*tsi;

	bzero(&pooladdr, sizeof(pooladdr));

	if ((tsp = ibuf_reserve(buf, sizeof(*tsp))) == NULL)
		return (-1);
	len = sizeof(*tsp);

	if (type == IKEV2_PAYLOAD_TSi) {
		if (sa->sa_hdr.sh_initiator) {
			tss = &pol->pol_tssrc;
			tsp->tsp_count = pol->pol_tssrc_count;
		} else {
			tss = &pol->pol_tsdst;
			tsp->tsp_count = pol->pol_tsdst_count;
		}
	} else if (type == IKEV2_PAYLOAD_TSr) {
		if (sa->sa_hdr.sh_initiator) {
			tss = &pol->pol_tsdst;
			tsp->tsp_count = pol->pol_tsdst_count;
		} else {
			tss = &pol->pol_tssrc;
			tsp->tsp_count = pol->pol_tssrc_count;
		}
	} else
		return (-1);

	TAILQ_FOREACH(tsi, tss, ts_entry) {
		if ((ts = ibuf_reserve(buf, sizeof(*ts))) == NULL)
			return (-1);

		addr = &tsi->ts_addr;

		/* patch remote address (if configured to 0.0.0.0) */
		if ((type == IKEV2_PAYLOAD_TSi && !sa->sa_hdr.sh_initiator) ||
		    (type == IKEV2_PAYLOAD_TSr && sa->sa_hdr.sh_initiator)) {
			if (ikev2_cp_fixaddr(sa, addr, &pooladdr) == 0)
				addr = &pooladdr;
		}

		ts->ts_protoid = tsi->ts_ipproto;

		if (addr->addr_port) {
			ts->ts_startport = addr->addr_port;
			ts->ts_endport = addr->addr_port;
		} else {
			ts->ts_startport = 0;
			ts->ts_endport = 0xffff;
		}

		switch (addr->addr_af) {
		case AF_INET:
			ts->ts_type = IKEV2_TS_IPV4_ADDR_RANGE;
			ts->ts_length = htobe16(sizeof(*ts) + 8);

			if ((ptr = ibuf_reserve(buf, 8)) == NULL)
				return (-1);

			in4 = (struct sockaddr_in *)&addr->addr;
			if (addr->addr_net) {
				/* Convert IPv4 network to address range */
				mv[0] = prefixlen2mask(addr->addr_mask);
				av[0] = in4->sin_addr.s_addr & mv[0];
				bv[0] = in4->sin_addr.s_addr | ~mv[0];
			} else
				av[0] = bv[0] = in4->sin_addr.s_addr;

			memcpy(ptr, &av[0], 4);
			memcpy(ptr + 4, &bv[0], 4);
			break;
		case AF_INET6:
			ts->ts_type = IKEV2_TS_IPV6_ADDR_RANGE;
			ts->ts_length = htobe16(sizeof(*ts) + 32);

			if ((ptr = ibuf_reserve(buf, 32)) == NULL)
				return (-1);

			in6 = (struct sockaddr_in6 *)&addr->addr;

			memcpy(&av, &in6->sin6_addr.s6_addr, 16);
			memcpy(&bv, &in6->sin6_addr.s6_addr, 16);
			if (addr->addr_net) {
				/* Convert IPv6 network to address range */
				prefixlen2mask6(addr->addr_mask, mv);
				av[0] &= mv[0];
				av[1] &= mv[1];
				av[2] &= mv[2];
				av[3] &= mv[3];
				bv[0] |= ~mv[0];
				bv[1] |= ~mv[1];
				bv[2] |= ~mv[2];
				bv[3] |= ~mv[3];
			}

			memcpy(ptr, &av, 16);
			memcpy(ptr + 16, &bv, 16);
			break;
		}

		len += betoh16(ts->ts_length);
	}

	return (len);
}

ssize_t
ikev2_add_ts(struct ibuf *e, struct ikev2_payload **pld, ssize_t len,
    struct iked_sa *sa, int reverse)
{
	if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_TSi) == -1)
		return (-1);

	/* TSi payload */
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);
	if ((len = ikev2_add_ts_payload(e, reverse ? IKEV2_PAYLOAD_TSr :
	    IKEV2_PAYLOAD_TSi, sa)) == -1)
		return (-1);

	if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_TSr) == -1)
		return (-1);

	/* TSr payload */
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);
	if ((len = ikev2_add_ts_payload(e, reverse ? IKEV2_PAYLOAD_TSi :
	    IKEV2_PAYLOAD_TSr, sa)) == -1)
		return (-1);

	return (len);
}


ssize_t
ikev2_add_certreq(struct ibuf *e, struct ikev2_payload **pld, ssize_t len,
    struct ibuf *certreq, uint8_t type)
{
	struct ikev2_cert	*cert;

	if (type == IKEV2_CERT_NONE)
		return (len);

	if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_CERTREQ) == -1)
		return (-1);

	/* CERTREQ payload */
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);

	if ((cert = ibuf_reserve(e, sizeof(*cert))) == NULL)
		return (-1);

	cert->cert_type = type;
	len = sizeof(*cert);

	if (certreq != NULL && cert->cert_type == IKEV2_CERT_X509_CERT) {
		if (ikev2_add_buf(e, certreq) == -1)
			return (-1);
		len += ibuf_size(certreq);
	}

	log_debug("%s: type %s length %zd", __func__,
	    print_map(type, ikev2_cert_map), len);

	return (len);
}

ssize_t
ikev2_add_ipcompnotify(struct iked *env, struct ibuf *e,
    struct ikev2_payload **pld, ssize_t len, struct iked_sa *sa,
    int initiator)
{
	struct iked_childsa		 csa;
	struct iked_ipcomp		*ic;
	struct ikev2_notify		*n;
	uint8_t				*ptr;
	uint16_t			 cpi;
	uint32_t			 spi;
	uint8_t				 transform;

	/* we only support deflate */
	transform = IKEV2_IPCOMP_DEFLATE;
	ic = initiator ? &sa->sa_ipcompi : &sa->sa_ipcompr;

	if (initiator) {
		bzero(&csa, sizeof(csa));
		csa.csa_saproto = IKEV2_SAPROTO_IPCOMP;
		csa.csa_ikesa = sa;
		csa.csa_local = &sa->sa_peer;
		csa.csa_peer = &sa->sa_local;
		if (pfkey_sa_init(env, &csa, &spi) == -1)
			return (-1);
		ic->ic_cpi_in = spi;
	} else {
		spi = ic->ic_cpi_in;
		/* make sure IPCOMP CPIs are not reused */
		ic->ic_transform = 0;
		ic->ic_cpi_in = 0;
		ic->ic_cpi_out = 0;
	}
	log_debug("%s: ic_cpi_in 0x%04x", __func__, spi);

	/*
	 * We get spi == 0 if the kernel does not support IPcomp,
	 * so just return the length of the current payload.
	 */
	if (spi == 0)
		return (len);
	cpi = htobe16((uint16_t)spi);
	if (*pld)
		if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
			return (-1);
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);
	len = sizeof(*n) + sizeof(cpi) + sizeof(transform);
	if ((ptr = ibuf_reserve(e, len)) == NULL)
		return (-1);
	n = (struct ikev2_notify *)ptr;
	n->n_protoid = 0;
	n->n_spisize = 0;
	n->n_type = htobe16(IKEV2_N_IPCOMP_SUPPORTED);
	ptr += sizeof(*n);
	memcpy(ptr, &cpi, sizeof(cpi));
	ptr += sizeof(cpi);
	memcpy(ptr, &transform, sizeof(transform));

	return (len);
}

ssize_t
ikev2_add_notify(struct ibuf *e, struct ikev2_payload **pld, ssize_t len,
    uint16_t notify)
{
	struct ikev2_notify		*n;

	if (*pld)
		if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
			return (-1);
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);
	len = sizeof(*n);
	if ((n = ibuf_reserve(e, len)) == NULL)
		return (-1);
	n->n_protoid = 0;
	n->n_spisize = 0;
	n->n_type = htobe16(notify);
	log_debug("%s: done", __func__);

	return (len);
}

ssize_t
ikev2_add_vendor_id(struct ibuf *e, struct ikev2_payload **pld,
    ssize_t len, struct ibuf *id)
{
	if (*pld)
		if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_VENDOR) == -1)
			return (-1);
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);
	if (ibuf_add_ibuf(e, id) == -1)
		return (-1);

	return (ibuf_size(id));
}

ssize_t
ikev2_add_mobike(struct ibuf *e, struct ikev2_payload **pld, ssize_t len)
{
	return ikev2_add_notify(e, pld, len, IKEV2_N_MOBIKE_SUPPORTED);
}

ssize_t
ikev2_add_fragmentation(struct ibuf *buf, struct ikev2_payload **pld,
    ssize_t len)
{
	return ikev2_add_notify(buf, pld, len, IKEV2_N_FRAGMENTATION_SUPPORTED);
}

ssize_t
ikev2_add_sighashnotify(struct ibuf *e, struct ikev2_payload **pld,
    ssize_t len)
{
	struct ikev2_notify		*n;
	uint8_t				*ptr;
	size_t				 i;
	uint16_t			 hash, signature_hashes[] = {
		IKEV2_SIGHASH_SHA2_256,
		IKEV2_SIGHASH_SHA2_384,
		IKEV2_SIGHASH_SHA2_512
	};

	if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
		return (-1);

	/* XXX signature_hashes are hardcoded for now */
	len = sizeof(*n) + nitems(signature_hashes) * sizeof(hash);

	/* NOTIFY payload */
	if ((*pld = ikev2_add_payload(e)) == NULL)
		return (-1);
	if ((ptr = ibuf_reserve(e, len)) == NULL)
		return (-1);

	n = (struct ikev2_notify *)ptr;
	n->n_protoid = 0;
	n->n_spisize = 0;
	n->n_type = htobe16(IKEV2_N_SIGNATURE_HASH_ALGORITHMS);
	ptr += sizeof(*n);

	for (i = 0; i < nitems(signature_hashes); i++) {
		hash = htobe16(signature_hashes[i]);
		memcpy(ptr, &hash, sizeof(hash));
		ptr += sizeof(hash);
	}

	return (len);
}

ssize_t
ikev2_add_transport_mode(struct iked *env, struct ibuf *e,
    struct ikev2_payload **pld, ssize_t len, struct iked_sa *sa)
{
	return ikev2_add_notify(e, pld, len, IKEV2_N_USE_TRANSPORT_MODE);
}

int
ikev2_next_payload(struct ikev2_payload *pld, size_t length,
    uint8_t nextpayload)
{
	size_t	 pldlength = sizeof(*pld) + length;

	if (pldlength > UINT16_MAX) {
		log_debug("%s: payload too long", __func__);
		return (-1);
	}

	log_debug("%s: length %zu nextpayload %s",
	    __func__, pldlength, print_map(nextpayload, ikev2_payload_map));

	pld->pld_length = htobe16(pldlength);
	pld->pld_nextpayload = nextpayload;

	return (0);
}

ssize_t
ikev2_nat_detection(struct iked *env, struct iked_message *msg,
    void *ptr, size_t len, unsigned int type, int frompeer)
{
	EVP_MD_CTX		*ctx;
	struct ike_header	*hdr;
	uint8_t			 md[SHA_DIGEST_LENGTH];
	unsigned int		 mdlen = sizeof(md);
	struct iked_sa		*sa = msg->msg_sa;
	struct sockaddr_in	*in4;
	struct sockaddr_in6	*in6;
	ssize_t			 ret = -1;
	struct sockaddr_storage	*src, *dst, *ss;
	uint64_t		 rspi, ispi;
	struct ibuf		*buf;
	uint32_t		 rnd;
	int			 natt_force = 0;

	if (ptr == NULL)
		return (mdlen);

	if (frompeer) {
		buf = msg->msg_parent->msg_data;
		if ((hdr = ibuf_seek(buf, 0, sizeof(*hdr))) == NULL)
			return (-1);
		ispi = hdr->ike_ispi;
		rspi = hdr->ike_rspi;
		src = &msg->msg_peer;
		dst = &msg->msg_local;
	} else {
		ispi = htobe64(sa->sa_hdr.sh_ispi);
		rspi = htobe64(sa->sa_hdr.sh_rspi);
		src = &msg->msg_local;
		dst = &msg->msg_peer;
	}

	ctx = EVP_MD_CTX_new();
	if (ctx == NULL)
		return (-1);
	EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);

	switch (type) {
	case IKEV2_N_NAT_DETECTION_SOURCE_IP:
		log_debug("%s: %s source %s %s %s", __func__,
		    frompeer ? "peer" : "local",
		    print_spi(betoh64(ispi), 8),
		    print_spi(betoh64(rspi), 8),
		    print_addr(src));
		ss = src;
		break;
	case IKEV2_N_NAT_DETECTION_DESTINATION_IP:
		log_debug("%s: %s destination %s %s %s", __func__,
		    frompeer ? "peer" : "local",
		    print_spi(betoh64(ispi), 8),
		    print_spi(betoh64(rspi), 8),
		    print_addr(dst));
		ss = dst;
		break;
	default:
		goto done;
	}

	EVP_DigestUpdate(ctx, &ispi, sizeof(ispi));
	EVP_DigestUpdate(ctx, &rspi, sizeof(rspi));

	switch (ss->ss_family) {
	case AF_INET:
		in4 = (struct sockaddr_in *)ss;
		EVP_DigestUpdate(ctx, &in4->sin_addr.s_addr,
		    sizeof(in4->sin_addr.s_addr));
		EVP_DigestUpdate(ctx, &in4->sin_port,
		    sizeof(in4->sin_port));
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)ss;
		EVP_DigestUpdate(ctx, &in6->sin6_addr.s6_addr,
		    sizeof(in6->sin6_addr.s6_addr));
		EVP_DigestUpdate(ctx, &in6->sin6_port,
		    sizeof(in6->sin6_port));
		break;
	default:
		goto done;
	}

	if (env->sc_nattmode == NATT_FORCE)
		natt_force = 1;
	else if (msg->msg_policy != NULL) {
		if (msg->msg_policy->pol_flags & IKED_POLICY_NATT_FORCE)
			natt_force = 1;
	}

	if (natt_force) {
		/* Enforce NAT-T/UDP-encapsulation by distorting the digest */
		rnd = arc4random();
		EVP_DigestUpdate(ctx, &rnd, sizeof(rnd));
	}

	EVP_DigestFinal_ex(ctx, md, &mdlen);

	if (len < mdlen)
		goto done;

	memcpy(ptr, md, mdlen);
	ret = mdlen;
 done:
	EVP_MD_CTX_free(ctx);

	return (ret);
}

ssize_t
ikev2_add_nat_detection(struct iked *env, struct ibuf *buf,
    struct ikev2_payload **pld, struct iked_message *msg, ssize_t len)
{
	struct ikev2_notify		*n;
	uint8_t			*ptr;

	/* *pld is NULL if there is no previous payload */
	if (*pld != NULL) {
		if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
			return (-1);
	}
	/* NAT-T notify payloads */
	if ((*pld = ikev2_add_payload(buf)) == NULL)
		return (-1);
	if ((n = ibuf_reserve(buf, sizeof(*n))) == NULL)
		return (-1);
	n->n_type = htobe16(IKEV2_N_NAT_DETECTION_SOURCE_IP);
	len = ikev2_nat_detection(env, msg, NULL, 0, 0, 0);
	if ((ptr = ibuf_reserve(buf, len)) == NULL)
		return (-1);
	if ((len = ikev2_nat_detection(env, msg, ptr, len,
	    betoh16(n->n_type), 0)) == -1)
		return (-1);
	len += sizeof(*n);

	if (ikev2_next_payload(*pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
		return (-1);

	if ((*pld = ikev2_add_payload(buf)) == NULL)
		return (-1);
	if ((n = ibuf_reserve(buf, sizeof(*n))) == NULL)
		return (-1);
	n->n_type = htobe16(IKEV2_N_NAT_DETECTION_DESTINATION_IP);
	len = ikev2_nat_detection(env, msg, NULL, 0, 0, 0);
	if ((ptr = ibuf_reserve(buf, len)) == NULL)
		return (-1);
	if ((len = ikev2_nat_detection(env, msg, ptr, len,
	    betoh16(n->n_type), 0)) == -1)
		return (-1);
	len += sizeof(*n);
	return (len);
}

ssize_t
ikev2_add_cp(struct iked *env, struct iked_sa *sa, int type, struct ibuf *buf)
{
	struct iked_policy	*pol = sa->sa_policy;
	struct ikev2_cp		*cp;
	struct ikev2_cfg	*cfg;
	struct iked_cfg		*ikecfg;
	unsigned int		 i, rad_ncfg = 0;
	uint32_t		 mask4;
	size_t			 len;
	struct sockaddr_in	*in4;
	struct sockaddr_in6	*in6;
	uint8_t			 prefixlen;
	int			 sent_addr4 = 0, sent_addr6 = 0;
	int			 have_mask4 = 0, sent_mask4 = 0;

	if ((cp = ibuf_reserve(buf, sizeof(*cp))) == NULL)
		return (-1);
	len = sizeof(*cp);

	switch (type) {
	case IKEV2_CP_REQUEST:
	case IKEV2_CP_REPLY:
		cp->cp_type = type;
		break;
	default:
		/* Not yet supported */
		return (-1);
	}

	if (sa->sa_radreq != NULL)
		rad_ncfg = sa->sa_radreq->rr_ncfg;

	for (i = 0; i < pol->pol_ncfg + rad_ncfg; i++) {
		if (i < pol->pol_ncfg)
			ikecfg = &pol->pol_cfg[i];
		else
			ikecfg = &sa->sa_radreq->rr_cfg[i - pol->pol_ncfg];

		if (ikecfg->cfg_action != cp->cp_type)
			continue;
		/* only return one address in case of multiple pools */
		if (type == IKEV2_CP_REPLY) {
			switch (ikecfg->cfg_type) {
			case IKEV2_CFG_INTERNAL_IP4_ADDRESS:
				if (sent_addr4)
					continue;
				break;
			case IKEV2_CFG_INTERNAL_IP6_ADDRESS:
				if (sent_addr6)
					continue;
				break;
			}
		}

		if ((cfg = ibuf_reserve(buf, sizeof(*cfg))) == NULL)
			return (-1);

		cfg->cfg_type = htobe16(ikecfg->cfg_type);
		len += sizeof(*cfg);

		switch (ikecfg->cfg_type) {
		case IKEV2_CFG_INTERNAL_IP4_ADDRESS:
		case IKEV2_CFG_INTERNAL_IP4_NETMASK:
		case IKEV2_CFG_INTERNAL_IP4_DNS:
		case IKEV2_CFG_INTERNAL_IP4_NBNS:
		case IKEV2_CFG_INTERNAL_IP4_DHCP:
		case IKEV2_CFG_INTERNAL_IP4_SERVER:
			/* 4 bytes IPv4 address */
			in4 = ((ikecfg->cfg_type ==
			    IKEV2_CFG_INTERNAL_IP4_ADDRESS) &&
			    sa->sa_addrpool &&
			    sa->sa_addrpool->addr_af == AF_INET) ?
			    (struct sockaddr_in *)&sa->sa_addrpool->addr :
			    (struct sockaddr_in *)&ikecfg->cfg.address.addr;
			/* don't include unspecified address in request */
			if (type == IKEV2_CP_REQUEST &&
			    !in4->sin_addr.s_addr)
				break;
			cfg->cfg_length = htobe16(4);
			if (ibuf_add(buf, &in4->sin_addr.s_addr, 4) == -1)
				return (-1);
			len += 4;
			if (ikecfg->cfg_type == IKEV2_CFG_INTERNAL_IP4_ADDRESS) {
				sent_addr4 = 1;
				if (sa->sa_addrpool &&
				    sa->sa_addrpool->addr_af == AF_INET &&
				    sa->sa_addrpool->addr_mask != 0)
					have_mask4 = 1;
			}
			if (ikecfg->cfg_type == IKEV2_CFG_INTERNAL_IP4_NETMASK)
				sent_mask4 = 1;
			break;
		case IKEV2_CFG_INTERNAL_IP4_SUBNET:
			/* 4 bytes IPv4 address + 4 bytes IPv4 mask + */
			in4 = (struct sockaddr_in *)&ikecfg->cfg.address.addr;
			mask4 = prefixlen2mask(ikecfg->cfg.address.addr_mask);
			cfg->cfg_length = htobe16(8);
			if (ibuf_add(buf, &in4->sin_addr.s_addr, 4) == -1)
				return (-1);
			if (ibuf_add(buf, &mask4, 4) == -1)
				return (-1);
			len += 8;
			break;
		case IKEV2_CFG_INTERNAL_IP6_DNS:
		case IKEV2_CFG_INTERNAL_IP6_NBNS:
		case IKEV2_CFG_INTERNAL_IP6_DHCP:
		case IKEV2_CFG_INTERNAL_IP6_SERVER:
			/* 16 bytes IPv6 address */
			in6 = (struct sockaddr_in6 *)&ikecfg->cfg.address.addr;
			cfg->cfg_length = htobe16(16);
			if (ibuf_add(buf, &in6->sin6_addr.s6_addr, 16) == -1)
				return (-1);
			len += 16;
			break;
		case IKEV2_CFG_INTERNAL_IP6_ADDRESS:
		case IKEV2_CFG_INTERNAL_IP6_SUBNET:
			/* 16 bytes IPv6 address + 1 byte prefix length */
			in6 = ((ikecfg->cfg_type ==
			    IKEV2_CFG_INTERNAL_IP6_ADDRESS) &&
			    sa->sa_addrpool6 &&
			    sa->sa_addrpool6->addr_af == AF_INET6) ?
			    (struct sockaddr_in6 *)&sa->sa_addrpool6->addr :
			    (struct sockaddr_in6 *)&ikecfg->cfg.address.addr;
			/* don't include unspecified address in request */
			if (type == IKEV2_CP_REQUEST &&
			   IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr))
				break;
			cfg->cfg_length = htobe16(17);
			if (ibuf_add(buf, &in6->sin6_addr.s6_addr, 16) == -1)
				return (-1);
			if (ikecfg->cfg.address.addr_net)
				prefixlen = ikecfg->cfg.address.addr_mask;
			else
				prefixlen = 128;
			if (ibuf_add(buf, &prefixlen, 1) == -1)
				return (-1);
			len += 16 + 1;
			if (ikecfg->cfg_type == IKEV2_CFG_INTERNAL_IP6_ADDRESS)
				sent_addr6 = 1;
			break;
		case IKEV2_CFG_APPLICATION_VERSION:
			/* Reply with an empty string (non-NUL terminated) */
			cfg->cfg_length = 0;
			break;
		}
	}

	/* derive netmask from pool */
	if (type == IKEV2_CP_REPLY && have_mask4 && !sent_mask4) {
		if ((cfg = ibuf_reserve(buf, sizeof(*cfg))) == NULL)
			return (-1);
		cfg->cfg_type = htobe16(IKEV2_CFG_INTERNAL_IP4_NETMASK);
		len += sizeof(*cfg);
		mask4 = prefixlen2mask(sa->sa_addrpool->addr_mask);
		cfg->cfg_length = htobe16(4);
		if (ibuf_add(buf, &mask4, 4) == -1)
			return (-1);
		len += 4;
	}

	return (len);
}

ssize_t
ikev2_init_add_cp(struct iked *env, struct iked_sa *sa, struct ibuf *buf)
{
	return (ikev2_add_cp(env, sa, IKEV2_CP_REQUEST, buf));
}

ssize_t
ikev2_resp_add_cp(struct iked *env, struct iked_sa *sa, struct ibuf *buf)
{
	int			 ret;

	switch (sa->sa_cp) {
	case IKEV2_CP_REQUEST:
		ret = ikev2_add_cp(env, sa, IKEV2_CP_REPLY, buf);
		break;
	case IKEV2_CP_REPLY:
	case IKEV2_CP_SET:
	case IKEV2_CP_ACK:
	default:
		/* Not yet supported */
		ret = -1;
	}
	return (ret);
}

ssize_t
ikev2_add_proposals(struct iked *env, struct iked_sa *sa, struct ibuf *buf,
    struct iked_proposals *proposals, uint8_t protoid, int initiator,
    int sendikespi, int skipdh)
{
	struct ikev2_sa_proposal	*sap = NULL;
	struct iked_transform		*xform;
	struct iked_proposal		*prop;
	struct iked_childsa		 csa;
	ssize_t				 length = 0, saplength, xflen;
	uint64_t			 spi64;
	uint32_t			 spi32, spi = 0;
	unsigned int			 i, xfi, nxforms;
	int				 prop_skipdh;

	TAILQ_FOREACH(prop, proposals, prop_entry) {
		if ((protoid && prop->prop_protoid != protoid) ||
		    (!protoid && prop->prop_protoid == IKEV2_SAPROTO_IKE))
			continue;

		prop_skipdh = skipdh;

		if (protoid != IKEV2_SAPROTO_IKE && initiator) {
			if (spi == 0) {
				bzero(&csa, sizeof(csa));
				csa.csa_ikesa = sa;
				csa.csa_saproto = prop->prop_protoid;
				csa.csa_local = &sa->sa_peer;
				csa.csa_peer = &sa->sa_local;

				if (pfkey_sa_init(env, &csa, &spi) == -1)
					return (-1);
			}

			prop->prop_localspi.spi = spi;
			prop->prop_localspi.spi_size = 4;
			prop->prop_localspi.spi_protoid = prop->prop_protoid;
		}

		if ((sap = ibuf_reserve(buf, sizeof(*sap))) == NULL) {
			log_debug("%s: failed to add proposal", __func__);
			return (-1);
		}

		if (sendikespi) {
			/* Special case for IKE SA rekeying */
			prop->prop_localspi.spi = initiator ?
			    sa->sa_hdr.sh_ispi : sa->sa_hdr.sh_rspi;
			prop->prop_localspi.spi_size = 8;
			prop->prop_localspi.spi_protoid = IKEV2_SAPROTO_IKE;
		}

		/*
		 * A single DH transform of type NONE is equivalent with
		 * not sending a DH transform at all.
		 * Prefer the latter for downwards compatibility.
		 */
		if (protoid != IKEV2_SAPROTO_IKE) {
			for (i = 0; i < prop->prop_nxforms; i++) {
				xform = prop->prop_xforms + i;
				if (xform->xform_type == IKEV2_XFORMTYPE_DH &&
				    xform->xform_id != IKEV2_XFORMDH_NONE)
					break;
			}
			if (i == prop->prop_nxforms)
				prop_skipdh = 1;
		}

		/*
		 * RFC 7296: 1.2. The Initial Exchanges
		 * IKE_AUTH messages do not contain KE/N payloads, thus
		 * SA payloads cannot contain groups.
		 */
		if (prop_skipdh) {
			nxforms = 0;
			for (i = 0; i < prop->prop_nxforms; i++) {
				xform = prop->prop_xforms + i;
				if (xform->xform_type == IKEV2_XFORMTYPE_DH)
					continue;
				nxforms++;
			}
		} else
			nxforms = prop->prop_nxforms;

		sap->sap_more = IKEV1_PAYLOAD_PROPOSAL;
		sap->sap_proposalnr = prop->prop_id;
		sap->sap_protoid = prop->prop_protoid;
		sap->sap_spisize = prop->prop_localspi.spi_size;
		sap->sap_transforms = nxforms;
		saplength = sizeof(*sap);

		switch (prop->prop_localspi.spi_size) {
		case 4:
			spi32 = htobe32(prop->prop_localspi.spi);
			if (ibuf_add(buf, &spi32, sizeof(spi32)) != 0)
				return (-1);
			saplength += 4;
			break;
		case 8:
			spi64 = htobe64(prop->prop_localspi.spi);
			if (ibuf_add(buf, &spi64, sizeof(spi64)) != 0)
				return (-1);
			saplength += 8;
			break;
		default:
			break;
		}

		for (i = 0, xfi = 0; i < prop->prop_nxforms; i++) {
			xform = prop->prop_xforms + i;

			if (prop_skipdh && xform->xform_type == IKEV2_XFORMTYPE_DH)
				continue;

			if ((xflen = ikev2_add_transform(buf,
			    xfi == nxforms - 1 ?
			    IKEV2_XFORM_LAST : IKEV2_XFORM_MORE,
			    xform->xform_type, xform->xform_id,
			    xform->xform_length)) == -1)
				return (-1);

			xfi++;
			saplength += xflen;
		}

		sap->sap_length = htobe16(saplength);
		length += saplength;
	}
	if (sap != NULL)
		sap->sap_more = IKEV1_PAYLOAD_NONE;

	log_debug("%s: length %zd", __func__, length);

	return (length);
}

ssize_t
ikev2_add_transform(struct ibuf *buf,
    uint8_t more, uint8_t type, uint16_t id, uint16_t length)
{
	struct ikev2_transform	*xfrm;
	struct ikev2_attribute	*attr;

	if ((xfrm = ibuf_reserve(buf, sizeof(*xfrm))) == NULL) {
		log_debug("%s: failed to add transform", __func__);
		return (-1);
	}
	xfrm->xfrm_more = more;
	xfrm->xfrm_type = type;
	xfrm->xfrm_id = htobe16(id);

	if (length) {
		xfrm->xfrm_length = htobe16(sizeof(*xfrm) + sizeof(*attr));

		if ((attr = ibuf_reserve(buf, sizeof(*attr))) == NULL) {
			log_debug("%s: failed to add attribute", __func__);
			return (-1);
		}
		attr->attr_type = htobe16(IKEV2_ATTRAF_TV |
		    IKEV2_ATTRTYPE_KEY_LENGTH);
		attr->attr_length = htobe16(length);
	} else
		xfrm->xfrm_length = htobe16(sizeof(*xfrm));

	return (betoh16(xfrm->xfrm_length));
}

int
ikev2_add_data(struct ibuf *buf, void *data, size_t length)
{
	void	*msgbuf;

	if ((msgbuf = ibuf_reserve(buf, length)) == NULL) {
		log_debug("%s: failed", __func__);
		return (-1);
	}
	memcpy(msgbuf, data, length);

	return (0);
}

int
ikev2_add_buf(struct ibuf *buf, struct ibuf *data)
{
	void	*msgbuf;

	if ((msgbuf = ibuf_reserve(buf, ibuf_size(data))) == NULL) {
		log_debug("%s: failed", __func__);
		return (-1);
	}
	memcpy(msgbuf, ibuf_data(data), ibuf_size(data));

	return (0);
}

int
ikev2_resp_informational(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg)
{
	struct ikev2_notify		*n;
	struct ikev2_payload		*pld = NULL;
	struct ibuf			*buf = NULL;
	ssize_t				 len = 0;
	int				 ret = -1;
	uint8_t				 firstpayload = IKEV2_PAYLOAD_NONE;

	if (!sa_stateok(sa, IKEV2_STATE_AUTH_REQUEST) ||
	    msg->msg_responded || msg->msg_error)
		goto done;

	if ((buf = ibuf_static()) == NULL)
		goto done;

	if ((len = ikev2_handle_delete(env, msg, buf, &pld,
	    &firstpayload)) == -1)
		goto done;

	/*
	 * Include NAT_DETECTION notification on UPDATE_SA_ADDRESSES or if
	 * the peer did include them, too (RFC 4555, 3.8).
	 */
	if (sa->sa_mobike &&
	    (msg->msg_update_sa_addresses || msg->msg_natt_rcvd)) {
		/* NAT-T notify payloads */
		len = ikev2_add_nat_detection(env, buf, &pld, msg, len);
		if (len == -1)
			goto done;
		firstpayload = IKEV2_PAYLOAD_NOTIFY;
	}
	/* Reflect COOKIE2 */
	if (msg->msg_cookie2) {
		/* *pld is NULL if there is no previous payload */
		if (pld != NULL) {
			if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
				goto done;
		}
		if ((pld = ikev2_add_payload(buf)) == NULL)
			goto done;
		if ((n = ibuf_reserve(buf, sizeof(*n))) == NULL)
			goto done;
		n->n_protoid = IKEV2_SAPROTO_IKE;
		n->n_spisize = 0;
		n->n_type = htobe16(IKEV2_N_COOKIE2);
		if (ikev2_add_buf(buf, msg->msg_cookie2) == -1)
			goto done;
		len = sizeof(*n) + ibuf_size(msg->msg_cookie2);
		log_debug("%s: added cookie2", __func__);
		if (firstpayload == IKEV2_PAYLOAD_NONE)
			firstpayload = IKEV2_PAYLOAD_NOTIFY;
	}
	/* add terminator, if there is already a payload */
	if (firstpayload != IKEV2_PAYLOAD_NONE)
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
			goto done;
	ret = ikev2_msg_send_encrypt(env, sa, &buf,
	    IKEV2_EXCHANGE_INFORMATIONAL, firstpayload, 1);
	if (ret != -1)
		msg->msg_responded = 1;
	if (msg->msg_flags & IKED_MSG_FLAGS_AUTHENTICATION_FAILED) {
		log_debug("%s: AUTHENTICATION_FAILED, closing SA",
		    __func__);
		ikev2_log_cert_info(SPI_SA(sa, __func__),
		    sa->sa_hdr.sh_initiator ? &sa->sa_rcert : &sa->sa_icert);
		ikev2_ike_sa_setreason(sa,
		    "authentication failed notification from peer");
		sa_state(env, sa, IKEV2_STATE_CLOSED);
	}
 done:
	ibuf_free(buf);
	return (ret);
}

void
ikev2_resp_recv(struct iked *env, struct iked_message *msg,
    struct ike_header *hdr)
{
	struct iked_sa		*sa;

	switch (hdr->ike_exchange) {
	case IKEV2_EXCHANGE_IKE_SA_INIT:
		if (msg->msg_sa != NULL) {
			log_debug("%s: SA already exists", __func__);
			return;
		}
		if ((msg->msg_sa = sa_new(env,
		    betoh64(hdr->ike_ispi), betoh64(hdr->ike_rspi),
		    0, msg->msg_policy)) == NULL) {
			log_debug("%s: failed to get new SA", __func__);
			return;
		}
		/* Setup exchange timeout. */
		timer_set(env, &msg->msg_sa->sa_timer,
		    ikev2_init_ike_sa_timeout, msg->msg_sa);
		timer_add(env, &msg->msg_sa->sa_timer,
		    IKED_IKE_SA_EXCHANGE_TIMEOUT);
		break;
	case IKEV2_EXCHANGE_IKE_AUTH:
		if (ikev2_msg_valid_ike_sa(env, hdr, msg) == -1)
			return;
		if (sa_stateok(msg->msg_sa, IKEV2_STATE_VALID)) {
			log_debug("%s: already authenticated", __func__);
			return;
		}
		break;
	case IKEV2_EXCHANGE_CREATE_CHILD_SA:
	case IKEV2_EXCHANGE_INFORMATIONAL:
		if (ikev2_msg_valid_ike_sa(env, hdr, msg) == -1)
			return;
		break;
	default:
		log_debug("%s: unsupported exchange: %s", __func__,
		    print_map(hdr->ike_exchange, ikev2_exchange_map));
		return;
	}

	if (ikev2_pld_parse(env, hdr, msg, msg->msg_offset) != 0) {
		log_info("%s: failed to parse message",
		    SPI_SA(msg->msg_sa, __func__));
		return;
	}

	if (!ikev2_msg_frompeer(msg))
		return;

	if (ikev2_handle_notifies(env, msg) != 0)
		return;

	if ((sa = msg->msg_sa) == NULL)
		return;

	if (sa_address(sa, &sa->sa_peer, (struct sockaddr *)&msg->msg_peer)
	    == -1 ||
	    sa_address(sa, &sa->sa_local, (struct sockaddr *)&msg->msg_local)
	    == -1) {
		ikestat_inc(env, ikes_msg_rcvd_dropped);
		return;
	}
	sa->sa_fd = msg->msg_fd;
	log_debug("%s: updated SA to peer %s local %s", __func__,
	    print_addr(&sa->sa_peer.addr), print_addr(&sa->sa_local.addr));

	if (sa->sa_fragments.frag_count != 0)
		return;

	msg->msg_valid = 1;

	if (msg->msg_natt && sa->sa_natt == 0) {
		log_debug("%s: NAT-T message received, updated SA", __func__);
		sa->sa_natt = 1;
	}

	switch (hdr->ike_exchange) {
	case IKEV2_EXCHANGE_IKE_SA_INIT:
		if (ikev2_sa_responder(env, sa, NULL, msg) != 0) {
			log_info("%s: failed to negotiate IKE SA",
			    SPI_SA(sa, __func__));
			if (msg->msg_error == 0)
				msg->msg_error = IKEV2_N_NO_PROPOSAL_CHOSEN;
			ikev2_send_init_error(env, msg);
			ikev2_ike_sa_setreason(sa, "no proposal chosen");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			return;
		}
		if (ikev2_resp_ike_sa_init(env, msg) != 0) {
			log_debug("%s: failed to send init response", __func__);
			ikev2_ike_sa_setreason(sa, "SA_INIT response failed");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			return;
		}
		break;
	case IKEV2_EXCHANGE_IKE_AUTH:
		if (!sa_stateok(sa, IKEV2_STATE_SA_INIT)) {
			log_debug("%s: state mismatch", __func__);
			ikev2_ike_sa_setreason(sa, "state mismatch IKE_AUTH");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			return;
		}

		/* Handle EAP authentication */
		if (msg->msg_eap.eam_found) {
			if (ikev2_resp_ike_eap(env, sa, msg)) {
				log_info("%s: failed eap response",
				    SPI_SA(sa, __func__));
				ikev2_ike_sa_setreason(sa, "EAP failed");
				sa_state(env, sa, IKEV2_STATE_CLOSED);
				return;
			}
			return;
		}

		if (ikev2_ike_auth_recv(env, sa, msg) != 0) {
			log_debug("%s: failed to send auth response", __func__);
			ikev2_send_error(env, sa, msg, hdr->ike_exchange);
			ikev2_ike_sa_setreason(sa, "IKE_AUTH failed");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			return;
		}
		break;
	case IKEV2_EXCHANGE_CREATE_CHILD_SA:
		if (ikev2_resp_create_child_sa(env, msg) != 0) {
			if (msg->msg_error == 0)
				msg->msg_error = IKEV2_N_NO_PROPOSAL_CHOSEN;
			ikev2_send_error(env, sa, msg, hdr->ike_exchange);
		}
		break;
	case IKEV2_EXCHANGE_INFORMATIONAL:
		if (msg->msg_update_sa_addresses)
			ikev2_update_sa_addresses(env, sa);
		(void)ikev2_resp_informational(env, sa, msg);
		break;
	default:
		break;
	}
}

ssize_t
ikev2_handle_delete(struct iked *env, struct iked_message *msg,
    struct ibuf *resp, struct ikev2_payload **pld, uint8_t *firstpayload)
{
	struct iked_childsa	**peersas = NULL;
	struct iked_sa		*sa = msg->msg_sa;
	struct ikev2_delete	*localdel;
	FILE			*spif;
	char			*spibuf = NULL;
	uint64_t		*localspi = NULL;
	uint64_t		 spi64, spi = 0;
	uint32_t		 spi32;
	uint8_t			*buf;
	size_t			 found = 0;
	int			 ret = -1;
	size_t			 i, sz, cnt, len, dummy;

	if (!msg->msg_del_protoid)
		return (0);

	if ((spif = open_memstream(&spibuf, &dummy)) == NULL) {
		log_warn("%s", __func__);
		return (0);
	}

	sz = msg->msg_del_spisize;

	switch (sz) {
	case 4:
	case 8:
		break;
	case 0:
		if (msg->msg_del_protoid != IKEV2_SAPROTO_IKE) {
			log_debug("%s: invalid SPI size", __func__);
			goto done;
		}
		ikev2_ikesa_recv_delete(env, sa);
		ret = 0;
		goto done;
	default:
		log_info("%s: error: invalid SPI size", __func__);
		goto done;
	}

	cnt = msg->msg_del_cnt;
	len = ibuf_length(msg->msg_del_buf);

	if ((len / sz) != cnt) {
		log_debug("%s: invalid payload length %zu/%zu != %zu",
		    __func__, len, sz, cnt);
		goto done;
	}

	if (((peersas = calloc(cnt, sizeof(struct iked_childsa *))) == NULL ||
	     (localspi = calloc(cnt, sizeof(uint64_t))) == NULL)) {
		log_warn("%s", __func__);
		goto done;
	}

	buf = ibuf_data(msg->msg_del_buf);
	for (i = 0; i < cnt; i++) {
		switch (sz) {
		case 4:
			memcpy(&spi32, buf + (i * sz), sizeof(spi32));
			spi = betoh32(spi32);
			break;
		case 8:
			memcpy(&spi64, buf + (i * sz), sizeof(spi64));
			spi = betoh64(spi64);
			break;
		}

		log_debug("%s: spi %s", __func__, print_spi(spi, sz));

		if (peersas == NULL || sa == NULL)
			continue;

		if ((peersas[i] = childsa_lookup(sa, spi,
		    msg->msg_del_protoid)) == NULL) {
			log_warnx("%s: CHILD SA doesn't exist for spi %s",
			    SPI_SA(sa, __func__),
			    print_spi(spi, sz));
			continue;
		}

		if (ikev2_childsa_delete(env, sa, msg->msg_del_protoid, spi,
		    &localspi[i], 0) != -1) {
			found++;
			/* append SPI to log buffer */
			if (ftello(spif) > 0)
				fputs(", ", spif);
			fputs(print_spi(spi, sz), spif);
		}

		/*
		 * Flows are left in the require mode so that it would be
		 * possible to quickly negotiate a new Child SA
		 */
	}

	if (resp == NULL) {
		ret = 0;
		goto done;
	}

	/* Response to the INFORMATIONAL with Delete payload */
	if (found) {
		if ((*pld = ikev2_add_payload(resp)) == NULL)
			goto done;
		*firstpayload = IKEV2_PAYLOAD_DELETE;

		if ((localdel = ibuf_reserve(resp, sizeof(*localdel))) == NULL)
			goto done;

		localdel->del_protoid = msg->msg_del_protoid;
		localdel->del_spisize = sz;
		localdel->del_nspi = htobe16(found);
		ret = sizeof(*localdel);

		for (i = 0; i < cnt; i++) {
			if (localspi[i] == 0)	/* happens if found < cnt */
				continue;
			switch (sz) {
			case 4:
				spi32 = htobe32(localspi[i]);
				if (ibuf_add(resp, &spi32, sizeof(spi32)) != 0)
					goto done;
				ret += sizeof(spi32);
				break;
			case 8:
				spi64 = htobe64(localspi[i]);
				if (ibuf_add(resp, &spi64, sizeof(spi64)) != 0)
					goto done;
				ret += sizeof(spi64);
				break;
			}
		}
		fflush(spif);
		if (!ferror(spif)) {
			log_info("%sdeleted %zu SPI%s: %s",
			    SPI_SA(sa, NULL), found, found == 1 ? "" : "s",
			    spibuf);
		}
	} else {
		/* XXX should we send an INVALID_SPI notification? */
		ret = 0;
	}

 done:
	free(localspi);
	free(peersas);
	fclose(spif);
	free(spibuf);

	return (ret);
}

int
ikev2_handle_notifies(struct iked *env, struct iked_message *msg)
{
	struct iked_ipcomp	*ic;
	struct iked_sa		*sa;
	struct iked_spi		 rekey;
	struct dh_group		*group;
	uint16_t		 groupid;
	unsigned int		 protoid;

	if ((sa = msg->msg_sa) == NULL)
		return (-1);

	if (msg->msg_flags & IKED_MSG_FLAGS_CHILD_SA_NOT_FOUND)
		sa->sa_stateflags &= ~IKED_REQ_CHILDSA;

	if ((msg->msg_flags & IKED_MSG_FLAGS_FRAGMENTATION) && env->sc_frag) {
		log_debug("%s: fragmentation enabled", __func__);
		sa->sa_frag = 1;
	}

	if ((msg->msg_flags & IKED_MSG_FLAGS_MOBIKE) && env->sc_mobike) {
		log_debug("%s: mobike enabled", __func__);
		sa->sa_mobike = 1;
		/* enforce natt */
		if (sa->sa_natt == 0 && sa->sa_udpencap == 0)
			ikev2_enable_natt(env, sa, msg, 0);
	}

	if ((msg->msg_flags & IKED_MSG_FLAGS_NO_ADDITIONAL_SAS)
	    && sa->sa_stateflags & IKED_REQ_CHILDSA) {
		/* This makes sense for Child SAs only atm */
		ikev2_disable_rekeying(env, sa);
		sa->sa_stateflags &= ~IKED_REQ_CHILDSA;
	}

	if (msg->msg_flags & IKED_MSG_FLAGS_INVALID_KE) {
		groupid = betoh16(msg->msg_group);
		if (group_getid(groupid) == NULL) {
			log_debug("%s: unable to select DH group %u",
			    __func__, groupid);
			ikev2_ike_sa_setreason(sa,
			    "unable to select DH group");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			return (-1);
		}
		log_debug("%s: responder selected DH group %u", __func__,
		    groupid);
		switch (msg->msg_exchange) {
		case IKEV2_EXCHANGE_IKE_SA_INIT:
			protoid = IKEV2_SAPROTO_IKE;
			if (!sa->sa_hdr.sh_initiator) {
				log_debug("%s: not an initiator", __func__);
				ikev2_ike_sa_setreason(sa,
				    "received invalid KE as responder");
				sa_state(env, sa, IKEV2_STATE_CLOSED);
				msg->msg_sa = NULL;
				return (-1);
			}
			if (config_findtransform_ext(&msg->msg_policy->pol_proposals,
			    IKEV2_XFORMTYPE_DH, groupid, protoid) == NULL) {
				log_debug("%s: DH group %u denied by policy",
				    __func__, groupid);
				ikev2_ike_sa_setreason(sa,
				    "unsupported group in INVALID_KE message");
				sa_state(env, sa, IKEV2_STATE_CLOSED);
				msg->msg_sa = NULL;
				return (-1);
			}
			ikev2_ike_sa_setreason(sa,
			    "reinitiating with new DH group");
			sa_state(env, sa, IKEV2_STATE_CLOSED);
			msg->msg_sa = NULL;
			msg->msg_policy->pol_peerdh = groupid;
			timer_set(env, &env->sc_inittmr, ikev2_init_ike_sa, NULL);
			timer_add(env, &env->sc_inittmr, IKED_INITIATOR_INITIAL);
			return (-1);
		case IKEV2_EXCHANGE_CREATE_CHILD_SA:
			if (!(sa->sa_stateflags & IKED_REQ_CHILDSA)) {
				log_debug("%s: IKED_REQ_CHILDSA missing",
				    __func__);
				return (-1);
			}
			sa->sa_stateflags &= ~IKED_REQ_CHILDSA;
			protoid = sa->sa_rekeyspi ?
			    IKEV2_SAPROTO_ESP : IKEV2_SAPROTO_IKE;
			if (config_findtransform_ext(&msg->msg_policy->pol_proposals,
			    IKEV2_XFORMTYPE_DH, groupid, protoid) == NULL) {
				log_debug("%s: DH group %u denied by policy",
				    __func__, groupid);
				ikev2_ike_sa_setreason(sa,
				    "unsupported group in INVALID_KE message");
				sa_state(env, sa, IKEV2_STATE_CLOSED);
				msg->msg_sa = NULL;
				return (-1);
			}
			if (protoid == IKEV2_SAPROTO_ESP) {
				/* CHILDSA */
				rekey.spi = sa->sa_rekeyspi;
				rekey.spi_size = 4;
				rekey.spi_protoid = protoid;
				(void)ikev2_send_create_child_sa(env, sa,
				    &rekey, rekey.spi_protoid, groupid);
			} else {
				/* IKESA */
				if ((group = group_get(groupid)) == NULL)
					return -1;
				group_free(sa->sa_dhgroup);
				sa->sa_dhgroup = group;
				timer_set(env, &sa->sa_rekey,
				    ikev2_ike_sa_rekey, sa);
				timer_add(env, &sa->sa_rekey, 0);
			}
			return (-1);
		}
	}

	if (msg->msg_flags & IKED_MSG_FLAGS_IPCOMP_SUPPORTED) {
		/* we only support deflate */
		if ((msg->msg_policy->pol_flags & IKED_POLICY_IPCOMP) &&
		    (msg->msg_transform == IKEV2_IPCOMP_DEFLATE)) {
			ic = msg->msg_response ?
			    &sa->sa_ipcompi :
			    &sa->sa_ipcompr;
			ic->ic_transform = msg->msg_transform;
			ic->ic_cpi_out = betoh16(msg->msg_cpi);
		}
	}

	if (msg->msg_nat_detected & IKED_MSG_NAT_DST_IP) {
		/* Send keepalive, since we are behind a NAT-gw */
		sa->sa_usekeepalive = 1;
	}

	/* Signature hash algorithm */
	if (msg->msg_flags & IKED_MSG_FLAGS_SIGSHA2)
		sa->sa_sigsha2 = 1;

	if (msg->msg_flags & IKED_MSG_FLAGS_USE_TRANSPORT)
		sa->sa_use_transport_mode = 1;

	if ((msg->msg_flags & IKED_MSG_FLAGS_TEMPORARY_FAILURE)
	    && sa->sa_nexti != NULL)
		sa->sa_tmpfail = 1;

	return (0);
}

int
ikev2_resp_ike_sa_init(struct iked *env, struct iked_message *msg)
{
	struct iked_message		 resp;
	struct ike_header		*hdr;
	struct ikev2_payload		*pld;
	struct ikev2_keyexchange	*ke;
	struct iked_sa			*sa = msg->msg_sa;
	struct ibuf			*buf;
	struct ibuf			*vendor_id = NULL;
	struct dh_group			*group;
	ssize_t				 len;
	int				 ret = -1;

	if (sa->sa_hdr.sh_initiator) {
		log_debug("%s: called by initiator", __func__);
		return (-1);
	}
	if (msg->msg_nat_detected && sa->sa_udpencap == 0) {
		log_debug("%s: detected NAT, enabling UDP encapsulation",
		    __func__);
		sa->sa_udpencap = 1;
	}

	if ((buf = ikev2_msg_init(env, &resp,
	    &msg->msg_peer, msg->msg_peerlen,
	    &msg->msg_local, msg->msg_locallen, 1)) == NULL)
		goto done;

	resp.msg_sa = sa;
	resp.msg_fd = msg->msg_fd;
	resp.msg_natt = msg->msg_natt;
	resp.msg_msgid = 0;
	resp.msg_policy = sa->sa_policy;

	/* IKE header */
	if ((hdr = ikev2_add_header(buf, sa, resp.msg_msgid,
	    IKEV2_PAYLOAD_SA, IKEV2_EXCHANGE_IKE_SA_INIT,
	    IKEV2_FLAG_RESPONSE)) == NULL)
		goto done;

	/* SA payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if ((len = ikev2_add_proposals(env, sa, buf, &sa->sa_proposals,
	    IKEV2_SAPROTO_IKE, sa->sa_hdr.sh_initiator, 0, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_KE) == -1)
		goto done;

	/* KE payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if ((ke = ibuf_reserve(buf, sizeof(*ke))) == NULL)
		goto done;
	if ((group = sa->sa_dhgroup) == NULL) {
		log_debug("%s: invalid dh", __func__);
		goto done;
	}
	ke->kex_dhgroup = htobe16(group->id);
	if (ikev2_add_buf(buf, sa->sa_dhrexchange) == -1)
		goto done;
	len = sizeof(*ke) + ibuf_size(sa->sa_dhrexchange);

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONCE) == -1)
		goto done;

	/* NONCE payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if (ikev2_add_buf(buf, sa->sa_rnonce) == -1)
		goto done;
	len = ibuf_size(sa->sa_rnonce);

	if (env->sc_vendorid != 0) {
		vendor_id = ibuf_new(IKED_VENDOR_ID, strlen(IKED_VENDOR_ID));
		ibuf_add(vendor_id, IKED_VERSION, strlen(IKED_VERSION));
		if ((len = ikev2_add_vendor_id(buf, &pld, len, vendor_id))
		    == -1)
			goto done;
	}

	/* Fragmentation Notify*/
	if (sa->sa_frag) {
		if ((len = ikev2_add_fragmentation(buf, &pld, len))
		    == -1)
			goto done;
	}

	if ((env->sc_nattmode != NATT_DISABLE) &&
	    msg->msg_local.ss_family != AF_UNSPEC) {
		if ((len = ikev2_add_nat_detection(env, buf, &pld, &resp, len))
		    == -1)
			goto done;
	}
	if (sa->sa_statevalid & IKED_REQ_CERT) {
		/* CERTREQ payload(s) */
		if ((len = ikev2_add_certreq(buf, &pld,
		    len, env->sc_certreq, env->sc_certreqtype)) == -1)
			goto done;

		if (env->sc_certreqtype != sa->sa_policy->pol_certreqtype &&
		    (len = ikev2_add_certreq(buf, &pld,
		    len, NULL, sa->sa_policy->pol_certreqtype)) == -1)
			goto done;
	}

	if (sa->sa_sigsha2 &&
	    (len = ikev2_add_sighashnotify(buf, &pld, len)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	if (ikev2_set_header(hdr, ibuf_size(buf) - sizeof(*hdr)) == -1)
		goto done;

	(void)ikev2_pld_parse(env, hdr, &resp, 0);

	ibuf_free(sa->sa_2ndmsg);
	if ((sa->sa_2ndmsg = ibuf_dup(buf)) == NULL) {
		log_debug("%s: failed to copy 2nd message", __func__);
		goto done;
	}

	ret = ikev2_msg_send(env, &resp);

 done:
	ibuf_free(vendor_id);
	ikev2_msg_cleanup(env, &resp);

	return (ret);
}

int
ikev2_send_auth_failed(struct iked *env, struct iked_sa *sa)
{
	char				 dstid[IKED_ID_SIZE];
	struct ikev2_notify		*n;
	struct ibuf			*buf = NULL;
	int				 ret = -1, exchange, response;

	if (ikev2_print_id(IKESA_DSTID(sa), dstid, sizeof(dstid)) == -1)
		bzero(dstid, sizeof(dstid));
	log_info("%s: authentication failed for %s",
	    SPI_SA(sa, __func__), dstid);

	/* Log certificate information */
	ikev2_log_cert_info(SPI_SA(sa, __func__),
	    sa->sa_hdr.sh_initiator ? &sa->sa_rcert : &sa->sa_icert);

	/* Notify payload */
	if ((buf = ibuf_static()) == NULL)
		goto done;
	if ((n = ibuf_reserve(buf, sizeof(*n))) == NULL)
		goto done;
	n->n_protoid = IKEV2_SAPROTO_IKE;
	n->n_spisize = 0;
	n->n_type = htobe16(IKEV2_N_AUTHENTICATION_FAILED);
	if (sa->sa_hdr.sh_initiator) {
		exchange = IKEV2_EXCHANGE_INFORMATIONAL;
		response = 0;
	} else {
		exchange = IKEV2_EXCHANGE_IKE_AUTH;
		response = 1;
	}
	ret = ikev2_send_ike_e(env, sa, buf, IKEV2_PAYLOAD_NOTIFY,
	    exchange, response);
	if (exchange == IKEV2_EXCHANGE_INFORMATIONAL)
		sa->sa_stateflags |= IKED_REQ_INF;
 done:
	ibuf_free(buf);

	/* cleanup SA after timeout */
	sa_state(env, sa, IKEV2_STATE_CLOSING);
	timer_del(env, &sa->sa_timer);
	timer_set(env, &sa->sa_timer, ikev2_ike_sa_timeout, sa);
	timer_add(env, &sa->sa_timer, IKED_IKE_SA_DELETE_TIMEOUT);
	config_free_fragments(&sa->sa_fragments);
	ikev2_ike_sa_setreason(sa, "authentication failed");

	return (ret);
}

ssize_t
ikev2_add_error(struct iked *env, struct ibuf *buf, struct iked_message *msg)
{
	struct ikev2_notify		*n;
	struct iked_spi			*rekey;
	uint16_t			 group;
	uint32_t			 spi32;
	uint64_t			 spi64;
	size_t				 len;
	uint8_t				*ptr;

	switch (msg->msg_error) {
	case IKEV2_N_CHILD_SA_NOT_FOUND:
		break;
	case IKEV2_N_NO_PROPOSAL_CHOSEN:
		ikev2_log_proposal(msg->msg_sa, &msg->msg_proposals);
		break;
	case IKEV2_N_INVALID_KE_PAYLOAD:
		break;
	default:
		return (-1);
	}
	log_info("%s: %s", SPI_SA(msg->msg_sa, __func__),
	    print_map(msg->msg_error, ikev2_n_map));
	len = sizeof(*n);
	if ((ptr = ibuf_reserve(buf, len)) == NULL)
		return (-1);
	n = (struct ikev2_notify *)ptr;
	n->n_type = htobe16(msg->msg_error);
	switch (msg->msg_error) {
	case IKEV2_N_CHILD_SA_NOT_FOUND:
		rekey = &msg->msg_rekey;
		switch (rekey->spi_size) {
		case 4:
			spi32 = htobe32(rekey->spi);
			if (ibuf_add(buf, &spi32, sizeof(spi32)) != 0)
				return (-1);
			len += sizeof(spi32);
			break;
		case 8:
			spi64 = htobe64(rekey->spi);
			if (ibuf_add(buf, &spi64, sizeof(spi64)) != 0)
				return (-1);
			len += sizeof(spi64);
			break;
		default:
			log_debug("%s: invalid SPI size %d", __func__,
			    rekey->spi_size);
			return (-1);
		}
		n->n_protoid = rekey->spi_protoid;
		n->n_spisize = rekey->spi_size;
		break;
	case IKEV2_N_INVALID_KE_PAYLOAD:
		group = htobe16(msg->msg_dhgroup);
		if (ibuf_add(buf, &group, sizeof(group)) != 0)
			return (-1);
		len += sizeof(group);
		n->n_protoid = 0;
		n->n_spisize = 0;
		break;
	default:
		n->n_protoid = 0;
		n->n_spisize = 0;
		break;
	}
	log_debug("%s: done", __func__);

	return (len);
}

int
ikev2_record_dstid(struct iked *env, struct iked_sa *sa)
{
	struct iked_sa *osa;

	osa = sa_dstid_lookup(env, sa);
	if (osa == sa)
		return (0);
	if (osa != NULL) {
		sa_dstid_remove(env, osa);
		if (env->sc_enforcesingleikesa &&
		    osa->sa_state < IKEV2_STATE_CLOSING) {
			log_info("%sreplaced by IKESA %s (identical DSTID)",
			    SPI_SA(osa, NULL),
			    print_spi(sa->sa_hdr.sh_ispi, 8));
			if (osa->sa_state == IKEV2_STATE_ESTABLISHED)
				ikev2_disable_timer(env, osa);
			ikev2_ike_sa_setreason(osa, "sa replaced");
			ikev2_ikesa_delete(env, osa, 0);
			timer_add(env, &osa->sa_timer,
			    3 * IKED_RETRANSMIT_TIMEOUT);
		}
	}
	osa = sa_dstid_insert(env, sa);
	if (osa != NULL) {
		/* XXX how can this fail */
		log_info("%s: could not replace old IKESA %s",
		    SPI_SA(sa, __func__),
		    print_spi(osa->sa_hdr.sh_ispi, 8));
		return (-1);
	}
	return (0);
}

int
ikev2_send_error(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg, uint8_t exchange)
{
	struct ibuf			*buf = NULL;
	int				 ret = -1;

	if (msg->msg_error == 0)
		return (0);
	if ((buf = ibuf_static()) == NULL)
		goto done;
	if (ikev2_add_error(env, buf, msg) == 0)
		goto done;
	ret = ikev2_send_ike_e(env, sa, buf, IKEV2_PAYLOAD_NOTIFY,
	    exchange, 1);
 done:
	ibuf_free(buf);
	return (ret);
}

/*
 * Variant of ikev2_send_error() that can be used before encryption
 * is enabled. Based on ikev2_resp_ike_sa_init() code.
 */
int
ikev2_send_init_error(struct iked *env, struct iked_message *msg)
{
	struct iked_message		 resp;
	struct ike_header		*hdr;
	struct ikev2_payload		*pld;
	struct iked_sa			*sa = msg->msg_sa;
	struct ibuf			*buf;
	ssize_t				 len = 0;
	int				 ret = -1;

	if (sa->sa_hdr.sh_initiator) {
		log_debug("%s: called by initiator", __func__);
		return (-1);
	}
	if (msg->msg_error == 0)
		return (0);

	if ((buf = ikev2_msg_init(env, &resp,
	    &msg->msg_peer, msg->msg_peerlen,
	    &msg->msg_local, msg->msg_locallen, 1)) == NULL)
		goto done;

	resp.msg_sa = sa;
	resp.msg_fd = msg->msg_fd;
	resp.msg_natt = msg->msg_natt;
	resp.msg_msgid = 0;
	resp.msg_policy = sa->sa_policy;

	/* IKE header */
	if ((hdr = ikev2_add_header(buf, sa, resp.msg_msgid,
	    IKEV2_PAYLOAD_NOTIFY, IKEV2_EXCHANGE_IKE_SA_INIT,
	    IKEV2_FLAG_RESPONSE)) == NULL)
		goto done;

	/* NOTIFY payload */
	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;
	if ((len = ikev2_add_error(env, buf, msg)) == 0)
		goto done;
	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;
	if (ikev2_set_header(hdr, ibuf_size(buf) - sizeof(*hdr)) == -1)
		goto done;

	(void)ikev2_pld_parse(env, hdr, &resp, 0);
	ret = ikev2_msg_send(env, &resp);

 done:
	ikev2_msg_cleanup(env, &resp);

	return (ret);
}

int
ikev2_handle_certreq(struct iked* env, struct iked_message *msg)
{
	struct iked_certreq	*cr;
	struct iked_sa		*sa;
	uint8_t			 crtype;
	uint8_t	more;

	if ((sa = msg->msg_sa) == NULL)
		return (-1);

	/* Ignore CERTREQ when policy uses PSK authentication */
	if (sa->sa_policy->pol_auth.auth_method == IKEV2_AUTH_SHARED_KEY_MIC)
		return (0);

	if (sa->sa_hdr.sh_initiator)
		sa->sa_stateinit |= IKED_REQ_CERT;
	else
		sa->sa_statevalid |= IKED_REQ_CERT;

	/*
	 * If we have to send a local certificate but did not receive an
	 * optional CERTREQ, use our own certreq to find a local certificate.
	 * We could alternatively extract the CA from the peer certificate
	 * to find a matching local one.
	 */
	if (SIMPLEQ_EMPTY(&msg->msg_certreqs)) {
		if (sa->sa_policy->pol_certreqtype)
			crtype = sa->sa_policy->pol_certreqtype;
		else
			crtype = env->sc_certreqtype;
		ca_setreq(env, sa, &sa->sa_policy->pol_localid,
		    crtype, 0, ibuf_data(env->sc_certreq),
		    ibuf_size(env->sc_certreq), PROC_CERT);
	} else {
		while ((cr = SIMPLEQ_FIRST(&msg->msg_certreqs))) {
			if (SIMPLEQ_NEXT(cr, cr_entry) != NULL)
				more = 1;
			else
				more = 0;

			ca_setreq(env, sa, &sa->sa_policy->pol_localid,
			    cr->cr_type, more, ibuf_data(cr->cr_data),
			    ibuf_size(cr->cr_data),
			    PROC_CERT);

			ibuf_free(cr->cr_data);
			SIMPLEQ_REMOVE_HEAD(&msg->msg_certreqs, cr_entry);
			free(cr);
		}
	}

	return (0);
}

int
ikev2_resp_ike_eap_mschap(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg)
{
	uint8_t			 successmsg[EAP_MSCHAP_SUCCESS_SZ];
	uint8_t			 ntresponse[EAP_MSCHAP_NTRESPONSE_SZ];
	struct eap_msg		*eap = &msg->msg_eap;
	struct iked_user	*usr;
	uint8_t			*pass;
	char			*name = NULL;
	size_t			 passlen;
	int			 ret;

	switch (eap->eam_state) {
	case EAP_STATE_IDENTITY:
		sa->sa_eapid = eap->eam_identity;
		eap->eam_identity = NULL;
		return (eap_challenge_request(env, sa, eap->eam_id));
	case EAP_STATE_MSCHAPV2_CHALLENGE:
		if (eap->eam_user) {
			name = eap->eam_user;
		} else if (sa->sa_eapid) {
			name = sa->sa_eapid;
		}
		if (name == NULL) {
			log_info("%s: invalid response name",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		if ((usr = user_lookup(env, name)) == NULL) {
			log_info("%s: unknown user '%s'", SPI_SA(sa, __func__),
			    name);
			return (-1);
		}

		if ((pass = string2unicode(usr->usr_pass, &passlen)) == NULL)
			return (-1);

		mschap_nt_response(ibuf_data(sa->sa_eap.id_buf),
		    eap->eam_challenge, usr->usr_name, strlen(usr->usr_name),
		    pass, passlen, ntresponse);

		if (memcmp(ntresponse, eap->eam_ntresponse,
		    sizeof(ntresponse)) != 0) {
			log_info("%s: '%s' authentication failed",
			   SPI_SA(sa, __func__), usr->usr_name);
			freezero(pass, passlen);

			/* XXX should we send an EAP failure packet? */
			return (-1);
		}

		bzero(&successmsg, sizeof(successmsg));

		mschap_auth_response(pass, passlen,
		    ntresponse, ibuf_data(sa->sa_eap.id_buf),
		    eap->eam_challenge, usr->usr_name, strlen(usr->usr_name),
		    successmsg);
		if ((sa->sa_eapmsk = ibuf_new(NULL, MSCHAP_MSK_SZ)) == NULL) {
			log_info("%s: failed to get MSK", SPI_SA(sa, __func__));
			freezero(pass, passlen);
			return (-1);
		}
		mschap_msk(pass, passlen, ntresponse,
		    ibuf_data(sa->sa_eapmsk));
		freezero(pass, passlen);

		log_info("%s: '%s' authenticated", __func__, usr->usr_name);

		ret = eap_mschap_challenge(env, sa, eap->eam_id, eap->eam_msrid,
		    successmsg, EAP_MSCHAP_SUCCESS_SZ);
		if (ret == 0)
			sa_state(env, sa, IKEV2_STATE_AUTH_SUCCESS);
		break;
	case EAP_STATE_MSCHAPV2_SUCCESS:
		return (eap_mschap_success(env, sa, eap->eam_id));
	case EAP_STATE_SUCCESS:
		if (!sa_stateok(sa, IKEV2_STATE_AUTH_SUCCESS))
			return (-1);
		return (eap_success(env, sa, msg->msg_eap.eam_id));
	default:
		log_info("%s: eap ignored.", __func__);
		break;
	}
	return 0;
}

int
ikev2_resp_ike_eap(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg)
{
	if (!sa_stateok(sa, IKEV2_STATE_EAP))
		return (-1);

	switch (sa->sa_policy->pol_auth.auth_eap) {
	case EAP_TYPE_MSCHAP_V2:
		return ikev2_resp_ike_eap_mschap(env, sa, msg);
	case EAP_TYPE_RADIUS:
		return iked_radius_request(env, sa, msg);
	}
	return -1;
}

int
ikev2_resp_ike_auth(struct iked *env, struct iked_sa *sa)
{
	struct ikev2_payload		*pld;
	struct ikev2_cert		*cert;
	struct ikev2_auth		*auth;
	struct iked_id			*id, *certid;
	struct ibuf			*e = NULL;
	uint8_t				 firstpayload;
	int				 ret = -1;
	ssize_t				 len;
	int				 i;

	if (sa == NULL)
		return (-1);

	if (sa->sa_state == IKEV2_STATE_EAP)
		return (eap_identity_request(env, sa));

	if (!sa_stateok(sa, IKEV2_STATE_VALID))
		return (0);	/* ignore */

	if (ikev2_cp_setaddr(env, sa, AF_INET) < 0 ||
	    ikev2_cp_setaddr(env, sa, AF_INET6) < 0)
		return (-1);

	if (ikev2_childsa_negotiate(env, sa, &sa->sa_kex, &sa->sa_proposals,
	    sa->sa_hdr.sh_initiator, 0) < 0)
		return (-1);

	/* New encrypted message buffer */
	if ((e = ibuf_static()) == NULL)
		goto done;

	if (!sa->sa_localauth.id_type) {
		/* Downgrade the state */
		sa_state(env, sa, IKEV2_STATE_AUTH_SUCCESS);
	}

	if (sa->sa_hdr.sh_initiator) {
		id = &sa->sa_iid;
		certid = &sa->sa_icert;
	} else {
		id = &sa->sa_rid;
		certid = &sa->sa_rcert;
	}

	if (sa->sa_state != IKEV2_STATE_EAP_VALID) {
		/* ID payload */
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		firstpayload = IKEV2_PAYLOAD_IDr;
		if (ibuf_add_ibuf(e, id->id_buf) != 0)
			goto done;
		len = ibuf_size(id->id_buf);

		/* CERT payload */
		if ((sa->sa_statevalid & IKED_REQ_CERT) &&
		    (certid->id_type != IKEV2_CERT_NONE)) {
			if (ikev2_next_payload(pld, len,
			    IKEV2_PAYLOAD_CERT) == -1)
				goto done;

			if ((pld = ikev2_add_payload(e)) == NULL)
				goto done;
			if ((cert = ibuf_reserve(e, sizeof(*cert))) == NULL)
				goto done;
			cert->cert_type = certid->id_type;
			if (ibuf_add_ibuf(e, certid->id_buf) != 0)
				goto done;
			len = ibuf_size(certid->id_buf) + sizeof(*cert);

			for (i = 0; i < IKED_SCERT_MAX; i++) {
				if (sa->sa_scert[i].id_type == IKEV2_CERT_NONE)
					break;
				if (ikev2_next_payload(pld, len,
				    IKEV2_PAYLOAD_CERT) == -1)
					goto done;
				if ((pld = ikev2_add_payload(e)) == NULL)
					goto done;
				if ((cert = ibuf_reserve(e,
				    sizeof(*cert))) == NULL)
					goto done;
				cert->cert_type = sa->sa_scert[i].id_type;
				if (ibuf_add_ibuf(e, sa->sa_scert[i].id_buf) !=
				    0)
					goto done;
				len = ibuf_size(sa->sa_scert[i].id_buf)
				    + sizeof(*cert);
			}
		}

		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_AUTH) == -1)
			goto done;
	} else
		firstpayload = IKEV2_PAYLOAD_AUTH;

	/* AUTH payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((auth = ibuf_reserve(e, sizeof(*auth))) == NULL)
		goto done;
	auth->auth_method = sa->sa_localauth.id_type;
	if (ibuf_add_ibuf(e, sa->sa_localauth.id_buf) != 0)
		goto done;
	len = ibuf_size(sa->sa_localauth.id_buf) + sizeof(*auth);

	/* CP payload */
	if (sa->sa_cp) {
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_CP) == -1)
			goto done;
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((len = ikev2_resp_add_cp(env, sa, e)) == -1)
			goto done;
	}

	if (sa->sa_ipcompr.ic_transform &&
	    (len = ikev2_add_ipcompnotify(env, e, &pld, len, sa, 0)) == -1)
		goto done;
	if (sa->sa_used_transport_mode &&
	    (len = ikev2_add_transport_mode(env, e, &pld, len, sa)) == -1)
		goto done;

	/* MOBIKE */
	if (sa->sa_mobike &&
	    (len = ikev2_add_mobike(e, &pld, len)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_SA) == -1)
		goto done;

	/* SA payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((len = ikev2_add_proposals(env, sa, e, &sa->sa_proposals, 0,
	    sa->sa_hdr.sh_initiator, 0, 1)) == -1)
		goto done;

	if ((len = ikev2_add_ts(e, &pld, len, sa, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	ret = ikev2_msg_send_encrypt(env, sa, &e,
	    IKEV2_EXCHANGE_IKE_AUTH, firstpayload, 1);
	if (ret == 0)
		ret = ikev2_childsa_enable(env, sa);
	if (ret == 0) {
		sa_state(env, sa, IKEV2_STATE_ESTABLISHED);
		iked_radius_acct_start(env, sa);
		/* Delete exchange timeout. */
		timer_del(env, &sa->sa_timer);
		ikev2_enable_timer(env, sa);
		ikev2_log_established(sa);
		ikev2_record_dstid(env, sa);
	}

 done:
	if (ret)
		ikev2_childsa_delete(env, sa, 0, 0, NULL, 1);
	ibuf_free(e);
	return (ret);
}

int
ikev2_send_ike_e(struct iked *env, struct iked_sa *sa, struct ibuf *buf,
    uint8_t firstpayload, uint8_t exchange, int response)
{
	struct ikev2_payload		*pld;
	struct ibuf			*e = NULL;
	int				 ret = -1;

	/* New encrypted message buffer */
	if ((e = ibuf_static()) == NULL)
		goto done;

	if (buf) {
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;

		if (ibuf_add_ibuf(e, buf) != 0)
			goto done;

		if (ikev2_next_payload(pld, ibuf_size(buf),
		    IKEV2_PAYLOAD_NONE) == -1)
			goto done;
	}

	ret = ikev2_msg_send_encrypt(env, sa, &e, exchange, firstpayload,
	    response);

 done:
	ibuf_free(e);

	return (ret);
}

int
ikev2_set_sa_proposal(struct iked_sa *sa, struct iked_policy *pol,
    unsigned int proto)
{
	struct iked_proposal		*prop, *copy;
	struct iked_transform		*xform;
	unsigned int			 i;

	/* create copy of the policy proposals */
	config_free_proposals(&sa->sa_proposals, proto);
	TAILQ_FOREACH(prop, &pol->pol_proposals, prop_entry) {
		if (proto != 0 && prop->prop_protoid != proto)
			continue;
		if ((copy = config_add_proposal(&sa->sa_proposals,
		    prop->prop_id, prop->prop_protoid)) == NULL)
			return (-1);
		for (i = 0; i < prop->prop_nxforms; i++) {
			xform = &prop->prop_xforms[i];
			if (config_add_transform(copy, xform->xform_type,
			    xform->xform_id, xform->xform_length,
			    xform->xform_keylength) != 0)
				return (-1);
		}
	}
	return (0);
}

int
ikev2_send_create_child_sa(struct iked *env, struct iked_sa *sa,
    struct iked_spi *rekey, uint8_t protoid, uint16_t proposed_group)
{
	struct iked_policy		*pol = sa->sa_policy;
	struct iked_childsa		*csa = NULL, *csb = NULL;
	struct iked_transform		*xform;
	struct ikev2_notify		*n;
	struct ikev2_payload		*pld = NULL;
	struct ikev2_keyexchange	*ke;
	struct dh_group			*group;
	struct ibuf			*e = NULL, *nonce = NULL;
	uint8_t				*ptr;
	uint8_t				 firstpayload;
	uint32_t			 spi;
	ssize_t				 len = 0;
	int				 initiator, ret = -1;

	if (rekey)
		log_debug("%s: rekeying %s spi %s", __func__,
		    print_map(rekey->spi_protoid, ikev2_saproto_map),
		    print_spi(rekey->spi, rekey->spi_size));
	else
		log_debug("%s: creating new CHILD SAs", __func__);

	/* XXX cannot initiate multiple concurrent CREATE_CHILD_SA exchanges */
	if (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF)) {
		log_debug("%s: another exchange already active",
		    __func__);
		return (-1);
	}

	ibuf_free(sa->sa_simult);
	sa->sa_simult = NULL;
	sa->sa_rekeyspi = 0;	/* clear rekey spi */
	initiator = sa->sa_hdr.sh_initiator ? 1 : 0;

	if (rekey &&
	    ((csa = childsa_lookup(sa, rekey->spi,
	    rekey->spi_protoid)) == NULL ||
	    (csb = csa->csa_peersa) == NULL)) {
		log_debug("%s: CHILD SA %s wasn't found", __func__,
		    print_spi(rekey->spi, rekey->spi_size));
		goto done;
	}

	/* Generate new nonce */
	if ((nonce = ibuf_random(IKED_NONCE_SIZE)) == NULL)
		goto done;

	/* Update initiator nonce */
	ibuf_free(sa->sa_inonce);
	sa->sa_inonce = nonce;

	if ((e = ibuf_static()) == NULL)
		goto done;

	if ((pol->pol_flags & IKED_POLICY_IPCOMP) &&
	    (len = ikev2_add_ipcompnotify(env, e, &pld, 0, sa, 1)) == -1)
		goto done;
	if ((pol->pol_flags & IKED_POLICY_TRANSPORT) &&
	    (len = ikev2_add_transport_mode(env, e, &pld, len, sa)) == -1)
		goto done;

	if (pld) {
		firstpayload = IKEV2_PAYLOAD_NOTIFY;
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_SA) == -1)
			goto done;
	} else
		firstpayload = IKEV2_PAYLOAD_SA;

	/* SA payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;

	/*
	 * We need to reset the sa_proposal. Otherwise it would be
	 * left over from the IKE_AUTH exchange and would not contain
	 * any DH groups (e.g. for ESP child SAs).
	 */
	if (ikev2_set_sa_proposal(sa, pol, protoid) < 0) {
		log_debug("%s: ikev2_set_sa_proposal failed", __func__);
		goto done;
	}

	if ((len = ikev2_add_proposals(env, sa, e, &sa->sa_proposals,
	    protoid, 1, 0, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONCE) == -1)
		goto done;

	/* NONCE payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if (ikev2_add_buf(e, nonce) == -1)
		goto done;
	len = ibuf_size(nonce);

	if ((xform = config_findtransform(&pol->pol_proposals, IKEV2_XFORMTYPE_DH,
	    protoid)) && xform->xform_id != IKEV2_XFORMDH_NONE) {
		log_debug("%s: enable PFS", __func__);
		ikev2_sa_cleanup_dh(sa);
		if (proposed_group) {
			if ((sa->sa_dhgroup =
			    group_get(proposed_group)) == NULL) {
				log_debug("%s: failed to get group", __func__);
				goto done;
			}
		}
		if (ikev2_sa_initiator_dh(sa, NULL, protoid, NULL) < 0) {
			log_debug("%s: failed to setup DH", __func__);
			goto done;
		}
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_KE) == -1)
			goto done;

		/* KE payload */
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((ke = ibuf_reserve(e, sizeof(*ke))) == NULL)
			goto done;
		if ((group = sa->sa_dhgroup) == NULL) {
			log_debug("%s: invalid dh", __func__);
			goto done;
		}
		ke->kex_dhgroup = htobe16(group->id);
		if (ikev2_add_buf(e, sa->sa_dhiexchange) == -1)
			goto done;
		len = sizeof(*ke) + ibuf_size(sa->sa_dhiexchange);
	}

	if ((len = ikev2_add_ts(e, &pld, len, sa, !initiator)) == -1)
		goto done;

	if (rekey) {
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NOTIFY) == -1)
			goto done;

		/* REKEY_SA notification */
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((n = ibuf_reserve(e, sizeof(*n))) == NULL)
			goto done;
		n->n_type = htobe16(IKEV2_N_REKEY_SA);
		n->n_protoid = rekey->spi_protoid;
		n->n_spisize = rekey->spi_size;
		if ((ptr = ibuf_reserve(e, rekey->spi_size)) == NULL)
			goto done;
		len = rekey->spi_size;
		spi = htobe32((uint32_t)csa->csa_peerspi);
		memcpy(ptr, &spi, rekey->spi_size);
		len += sizeof(*n);
	}

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	ret = ikev2_msg_send_encrypt(env, sa, &e,
	    IKEV2_EXCHANGE_CREATE_CHILD_SA, firstpayload, 0);
	if (ret == 0) {
		if (rekey) {
			csa->csa_rekey = 1;
			csb->csa_rekey = 1;
			/*
			 * Remember the peer spi of the rekeyed
			 * SA for ikev2_init_create_child_sa().
			 */
			sa->sa_rekeyspi = csa->csa_peerspi;
		}
		sa->sa_stateflags |= IKED_REQ_CHILDSA;
	}

done:
	ibuf_free(e);
	return (ret);
}

void
ikev2_ike_sa_rekey(struct iked *env, void *arg)
{
	struct iked_sa			*sa = arg;
	struct iked_sa			*nsa = NULL;
	struct ikev2_payload		*pld = NULL;
	struct ikev2_keyexchange	*ke;
	struct dh_group			*group;
	struct ibuf			*e = NULL, *nonce = NULL;
	ssize_t				 len = 0;
	int				 ret = -1;

	log_debug("%s: IKE SA %p ispi %s rspi %s", __func__, sa,
	    print_spi(sa->sa_hdr.sh_ispi, 8),
	    print_spi(sa->sa_hdr.sh_rspi, 8));

	if (sa->sa_nexti) {
		log_debug("%s: already rekeying", __func__);
		goto done;
	}

	if (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF)) {
		/*
		 * We cannot initiate multiple concurrent CREATE_CHILD_SA
		 * exchanges, so retry again fast.
		 */
		log_info("%s: busy, delaying rekey", SPI_SA(sa, __func__));
		ikev2_ike_sa_rekey_schedule_fast(env, sa);
		return;
	}

	/* We need to make sure the rekeying finishes in time */
	timer_set(env, &sa->sa_rekey, ikev2_ike_sa_rekey_timeout, sa);
	timer_add(env, &sa->sa_rekey, IKED_IKE_SA_REKEY_TIMEOUT);

	if ((nsa = sa_new(env, 0, 0, 1, sa->sa_policy)) == NULL) {
		log_debug("%s: failed to get new SA", __func__);
		goto done;
	}

	if (ikev2_sa_initiator(env, nsa, sa, NULL)) {
		log_debug("%s: failed to setup DH", __func__);
		goto done;
	}
	sa_state(env, nsa, IKEV2_STATE_AUTH_SUCCESS);
	nonce = nsa->sa_inonce;

	if ((e = ibuf_static()) == NULL)
		goto done;

	/* SA payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;

	/* just reuse the old IKE SA proposals */
	if ((len = ikev2_add_proposals(env, nsa, e, &sa->sa_proposals,
	    IKEV2_SAPROTO_IKE, 1, 1, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONCE) == -1)
		goto done;

	/* NONCE payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if (ikev2_add_buf(e, nonce) == -1)
		goto done;
	len = ibuf_size(nonce);

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_KE) == -1)
		goto done;

	/* KE payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((ke = ibuf_reserve(e, sizeof(*ke))) == NULL)
		goto done;
	if ((group = nsa->sa_dhgroup) == NULL) {
		log_debug("%s: invalid dh", __func__);
		goto done;
	}
	ke->kex_dhgroup = htobe16(group->id);
	if (ikev2_add_buf(e, nsa->sa_dhiexchange) == -1)
		goto done;
	len = sizeof(*ke) + ibuf_size(nsa->sa_dhiexchange);

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	ret = ikev2_msg_send_encrypt(env, sa, &e,
	    IKEV2_EXCHANGE_CREATE_CHILD_SA, IKEV2_PAYLOAD_SA, 0);
	if (ret == 0) {
		sa->sa_stateflags |= IKED_REQ_CHILDSA;
		sa->sa_nexti = nsa;
		nsa->sa_previ = sa;
		sa->sa_tmpfail = 0;
		nsa = NULL;
	}
done:
	if (nsa) {
		ikev2_ike_sa_setreason(nsa, "failed to send CREATE_CHILD_SA");
		sa_free(env, nsa);
	}
	ibuf_free(e);

	if (ret == 0)
		log_debug("%s: create child SA sent", __func__);
	else
		log_debug("%s: could not send create child SA", __func__);
	/* XXX should we try again in case of ret != 0 ? */
}

int
ikev2_nonce_cmp(struct ibuf *a, struct ibuf *b)
{
	size_t				alen, blen, len;
	int				ret;

	alen = ibuf_size(a);
	blen = ibuf_size(b);
	len = MINIMUM(alen, blen);
	ret = memcmp(ibuf_data(a), ibuf_data(b), len);
	if (ret == 0)
		ret = (alen < blen ? -1 : 1);
	return (ret);
}

int
ikev2_init_create_child_sa(struct iked *env, struct iked_message *msg)
{
	struct iked_childsa		*csa = NULL;
	struct iked_proposal		*prop;
	struct iked_sa			*sa = msg->msg_sa;
	struct iked_sa			*nsa, *dsa;
	struct iked_spi			*spi;
	struct ikev2_delete		*del;
	struct ibuf			*buf = NULL;
	struct ibuf			*ni, *nr;
	uint32_t			 spi32;
	int				 pfs = 0, ret = -1;

	if (!ikev2_msg_frompeer(msg) ||
	    (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF)) == 0)
		return (0);

	if (sa->sa_nexti != NULL && sa->sa_tmpfail) {
		sa->sa_stateflags &= ~IKED_REQ_CHILDSA;
		ikev2_ike_sa_setreason(sa->sa_nexti, "tmpfail");
		sa_free(env, sa->sa_nexti);
		sa->sa_nexti = NULL;
		timer_set(env, &sa->sa_rekey, ikev2_ike_sa_rekey, sa);
		ikev2_ike_sa_rekey_schedule_fast(env, sa);
		log_info("%s: IKESA rekey delayed", SPI_SA(sa, __func__));
		return (0);
	}

	if (msg->msg_prop == NULL ||
	    TAILQ_EMPTY(&msg->msg_proposals)) {
		log_info("%s: no proposal specified", SPI_SA(sa, __func__));
		return (-1);
	}

	if (proposals_negotiate(&sa->sa_proposals, &sa->sa_proposals,
	    &msg->msg_proposals, 1, -1) != 0) {
		log_info("%s: no proposal chosen", SPI_SA(sa, __func__));
		ikestat_inc(env, ikes_sa_proposals_negotiate_failures);
		return (-1);
	}

	TAILQ_FOREACH(prop, &sa->sa_proposals, prop_entry) {
		if (prop->prop_protoid == msg->msg_prop->prop_protoid)
			break;
	}
	if (prop == NULL) {
		log_info("%s: failed to find %s proposals", SPI_SA(sa, __func__),
		    print_map(msg->msg_prop->prop_protoid, ikev2_saproto_map));
		return (-1);
	}

	/* IKE SA rekeying */
	if (prop->prop_protoid == IKEV2_SAPROTO_IKE) {
		if (sa->sa_nexti == NULL) {
			log_info("%s: missing IKE SA for rekeying",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		/* Update the responder SPI */
		/* XXX sa_new() is just a lookup, so nsa == sa->sa_nexti */
		spi = &msg->msg_prop->prop_peerspi;
		if ((nsa = sa_new(env, sa->sa_nexti->sa_hdr.sh_ispi,
		    spi->spi, 1, NULL)) == NULL || nsa != sa->sa_nexti) {
			log_info("%s: invalid rekey SA", SPI_SA(sa, __func__));
			if (nsa) {
				ikev2_ike_sa_setreason(nsa,
				    "invalid SA for rekey");
				sa_free(env, nsa);
			}
			ikev2_ike_sa_setreason(sa->sa_nexti, "invalid SA nexti");
			sa_free(env, sa->sa_nexti);
			sa->sa_nexti = NULL;	/* reset by sa_free */
			return (-1);
		}
		if (ikev2_sa_initiator(env, nsa, sa, msg) == -1) {
			log_info("%s: failed to get IKE keys",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		sa->sa_stateflags &= ~IKED_REQ_CHILDSA;
		if (sa->sa_nextr) {
			/*
			 * Resolve simultaneous IKE SA rekeying by
			 * deleting the SA with the lowest NONCE.
			 */
			log_info("%s: resolving simultaneous IKE SA rekeying",
			    SPI_SA(sa, __func__));
			/* ni: minimum nonce of sa_nexti */
			if (ikev2_nonce_cmp(sa->sa_nexti->sa_inonce,
			    sa->sa_nexti->sa_rnonce) < 0)
				ni = sa->sa_nexti->sa_inonce;
			else
				ni = sa->sa_nexti->sa_rnonce;
			/* nr: minimum nonce of sa_nextr */
			if (ikev2_nonce_cmp(sa->sa_nextr->sa_inonce,
			    sa->sa_nextr->sa_rnonce) < 0)
				nr = sa->sa_nextr->sa_inonce;
			else
				nr = sa->sa_nextr->sa_rnonce;
			/* delete SA with minumum nonce */
			if (ikev2_nonce_cmp(ni, nr) < 0) {
				dsa = sa->sa_nexti;
				nsa = sa->sa_nextr;
			} else {
				dsa = sa->sa_nextr;
				nsa = sa->sa_nexti;
			}
			/* unlink sa_nextr */
			sa->sa_nextr->sa_prevr = NULL;
			sa->sa_nextr = NULL;
			/* Setup address, socket and NAT information */
			sa_address(dsa, &dsa->sa_peer,
			    (struct sockaddr *)&sa->sa_peer.addr);
			sa_address(dsa, &dsa->sa_local,
			    (struct sockaddr *)&sa->sa_local.addr);
			dsa->sa_fd = sa->sa_fd;
			dsa->sa_natt = sa->sa_natt;
			dsa->sa_udpencap = sa->sa_udpencap;
			ikev2_ike_sa_setreason(dsa,
			    "resolving simultaneous rekeying");
			ikev2_ikesa_delete(env, dsa, dsa->sa_hdr.sh_initiator);
		}
		/* unlink sa_nexti */
		sa->sa_nexti->sa_previ = NULL;
		sa->sa_nexti = NULL;
		return (ikev2_ikesa_enable(env, sa, nsa));
	}

	/* Child SA rekeying */
	if (sa->sa_rekeyspi &&
	    (csa = childsa_lookup(sa, sa->sa_rekeyspi, prop->prop_protoid))
	    != NULL) {
		log_info("%s: rekeying CHILD SA old %s spi %s",
		    SPI_SA(sa, __func__),
		    print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size),
		    print_spi(prop->prop_peerspi.spi,
		    prop->prop_peerspi.spi_size));
	}

	/* check KE payload for PFS */
	if (ibuf_length(msg->msg_ke)) {
		log_debug("%s: using PFS", __func__);
		if (ikev2_sa_initiator_dh(sa, msg, prop->prop_protoid, NULL) < 0) {
			log_info("%s: failed to setup DH",
			    SPI_SA(sa, __func__));
			return (ret);
		}
		if (sa->sa_dhpeer == NULL) {
			log_info("%s: no peer DH", SPI_SA(sa, __func__));
			return (ret);
		}
		pfs = 1;
		/* XXX check group against policy ? */
		/* XXX should proposals_negotiate do this? */
	}

	/* Update responder's nonce */
	if (!ibuf_length(msg->msg_nonce)) {
		log_info("%s: responder didn't send nonce",
		    SPI_SA(sa, __func__));
		return (-1);
	}
	ibuf_free(sa->sa_rnonce);
	sa->sa_rnonce = msg->msg_nonce;
	msg->msg_nonce = NULL;

	if (csa && (nr = sa->sa_simult) != NULL) {
		log_info("%s: resolving simultaneous CHILD SA rekeying",
		    SPI_SA(sa, __func__));
		/* set ni to minimum nonce for exchange initiated by us */
		if (ikev2_nonce_cmp(sa->sa_inonce, sa->sa_rnonce) < 0)
			ni = sa->sa_inonce;
		else
			ni = sa->sa_rnonce;
		/*
		 * If the exchange initated by us has smaller nonce,
		 * then we have to delete our SAs.
		 */
		if (ikev2_nonce_cmp(ni, nr) < 0) {
			ret = ikev2_childsa_delete_proposed(env, sa,
			    &sa->sa_proposals);
			goto done;
		}
	}

	if (ikev2_childsa_negotiate(env, sa, &sa->sa_kex, &sa->sa_proposals, 1,
	    pfs)) {
		log_info("%s: failed to get CHILD SAs", SPI_SA(sa, __func__));
		return (-1);
	}

	if (csa) {
		/* Child SA rekeying */

		if ((buf = ibuf_static()) == NULL)
			goto done;

		if ((del = ibuf_reserve(buf, sizeof(*del))) == NULL)
			goto done;

		del->del_protoid = prop->prop_protoid;
		del->del_spisize = sizeof(spi32);
		del->del_nspi = htobe16(1);

		spi32 = htobe32(csa->csa_spi.spi);
		if (ibuf_add(buf, &spi32, sizeof(spi32)))
			goto done;

		if (ikev2_send_ike_e(env, sa, buf, IKEV2_PAYLOAD_DELETE,
		    IKEV2_EXCHANGE_INFORMATIONAL, 0))
			goto done;

		sa->sa_stateflags |= IKED_REQ_INF;
	}

	ret = ikev2_childsa_enable(env, sa);

done:
	sa->sa_stateflags &= ~IKED_REQ_CHILDSA;

	if (ret)
		ikev2_childsa_delete(env, sa, 0, 0, NULL, 1);
	else if (csa) {
		/* delete the rekeyed SA pair */
		ikev2_childsa_delete(env, sa, csa->csa_saproto,
		    csa->csa_peerspi, NULL, 0);
	}
	ibuf_free(buf);
	return (ret);
}

int
ikev2_ikesa_enable(struct iked *env, struct iked_sa *sa, struct iked_sa *nsa)
{
	struct iked_childsa		*csa, *csatmp, *ipcomp;
	struct iked_flow		*flow, *flowtmp;
	struct iked_proposal		*prop, *proptmp;
	int				i;

	log_debug("%s: IKE SA %p ispi %s rspi %s replaced"
	    " by SA %p ispi %s rspi %s ",
	    __func__, sa,
	    print_spi(sa->sa_hdr.sh_ispi, 8),
	    print_spi(sa->sa_hdr.sh_rspi, 8),
	    nsa,
	    print_spi(nsa->sa_hdr.sh_ispi, 8),
	    print_spi(nsa->sa_hdr.sh_rspi, 8));

	/* Transfer socket and NAT information */
	nsa->sa_fd = sa->sa_fd;
	nsa->sa_natt = sa->sa_natt;
	nsa->sa_udpencap = sa->sa_udpencap;
	nsa->sa_usekeepalive = sa->sa_usekeepalive;
	nsa->sa_mobike = sa->sa_mobike;
	nsa->sa_frag = sa->sa_frag;

	/* Transfer old addresses */
	memcpy(&nsa->sa_local, &sa->sa_local, sizeof(nsa->sa_local));
	memcpy(&nsa->sa_peer, &sa->sa_peer, sizeof(nsa->sa_peer));
	memcpy(&nsa->sa_peer_loaded, &sa->sa_peer_loaded,
	    sizeof(nsa->sa_peer_loaded));

	/* Transfer all Child SAs and flows from the old IKE SA */
	TAILQ_FOREACH_SAFE(flow, &sa->sa_flows, flow_entry, flowtmp) {
		TAILQ_REMOVE(&sa->sa_flows, flow, flow_entry);
		TAILQ_INSERT_TAIL(&nsa->sa_flows, flow,
		    flow_entry);
		flow->flow_ikesa = nsa;
		flow->flow_local = &nsa->sa_local;
		flow->flow_peer = &nsa->sa_peer;
	}
	TAILQ_FOREACH_SAFE(csa, &sa->sa_childsas, csa_entry, csatmp) {
		TAILQ_REMOVE(&sa->sa_childsas, csa, csa_entry);
		TAILQ_INSERT_TAIL(&nsa->sa_childsas, csa,
		    csa_entry);
		csa->csa_ikesa = nsa;
		if (csa->csa_dir == IPSP_DIRECTION_IN) {
			csa->csa_local = &nsa->sa_peer;
			csa->csa_peer = &nsa->sa_local;
		} else {
			csa->csa_local = &nsa->sa_local;
			csa->csa_peer = &nsa->sa_peer;
		}
		if ((ipcomp = csa->csa_bundled) != NULL) {
			ipcomp->csa_ikesa = nsa;
			ipcomp->csa_local = csa->csa_local;
			ipcomp->csa_peer = csa->csa_peer;
		}
	}
	/* Transfer all non-IKE proposals */
	TAILQ_FOREACH_SAFE(prop, &sa->sa_proposals, prop_entry, proptmp) {
		if (prop->prop_protoid == IKEV2_SAPROTO_IKE)
			continue;
		TAILQ_REMOVE(&sa->sa_proposals, prop, prop_entry);
		TAILQ_INSERT_TAIL(&nsa->sa_proposals, prop,
		    prop_entry);
	}

	/* Preserve ID information */
	ibuf_free(nsa->sa_iid.id_buf);
	ibuf_free(nsa->sa_rid.id_buf);
	ibuf_free(nsa->sa_icert.id_buf);
	ibuf_free(nsa->sa_rcert.id_buf);
	if (sa->sa_hdr.sh_initiator == nsa->sa_hdr.sh_initiator) {
		nsa->sa_iid = sa->sa_iid;
		nsa->sa_rid = sa->sa_rid;
		nsa->sa_icert = sa->sa_icert;
		nsa->sa_rcert = sa->sa_rcert;
	} else {
		/* initiator and responder role swapped */
		nsa->sa_iid = sa->sa_rid;
		nsa->sa_rid = sa->sa_iid;
		nsa->sa_icert = sa->sa_rcert;
		nsa->sa_rcert = sa->sa_icert;
	}
	for (i = 0; i < IKED_SCERT_MAX; i++)
		nsa->sa_scert[i] = sa->sa_scert[i];
	/* duplicate the actual buffer */
	nsa->sa_iid.id_buf = ibuf_dup(nsa->sa_iid.id_buf);
	nsa->sa_rid.id_buf = ibuf_dup(nsa->sa_rid.id_buf);
	nsa->sa_icert.id_buf = ibuf_dup(nsa->sa_icert.id_buf);
	nsa->sa_rcert.id_buf = ibuf_dup(nsa->sa_rcert.id_buf);
	for (i = 0; i < IKED_SCERT_MAX; i++)
		nsa->sa_scert[i].id_buf = ibuf_dup(nsa->sa_scert[i].id_buf);

	/* Transfer sa_addrpool address */
	if (sa->sa_addrpool) {
		RB_REMOVE(iked_addrpool, &env->sc_addrpool, sa);
		nsa->sa_addrpool = sa->sa_addrpool;
		sa->sa_addrpool = NULL;
		RB_INSERT(iked_addrpool, &env->sc_addrpool, nsa);
	}
	if (sa->sa_addrpool6) {
		RB_REMOVE(iked_addrpool6, &env->sc_addrpool6, sa);
		nsa->sa_addrpool6 = sa->sa_addrpool6;
		sa->sa_addrpool6 = NULL;
		RB_INSERT(iked_addrpool6, &env->sc_addrpool6, nsa);
	}
	nsa->sa_cp = sa->sa_cp;
	nsa->sa_cp_addr = sa->sa_cp_addr;
	sa->sa_cp_addr = NULL;
	nsa->sa_cp_addr6 = sa->sa_cp_addr6;
	sa->sa_cp_addr6 = NULL;
	nsa->sa_cp_dns = sa->sa_cp_dns;
	sa->sa_cp_dns = NULL;
	/* Transfer other attributes */
	if (sa->sa_dstid_entry_valid) {
		sa_dstid_remove(env, sa);
		sa_dstid_insert(env, nsa);
	}
	if (sa->sa_tag) {
		nsa->sa_tag = sa->sa_tag;
		sa->sa_tag = NULL;
	}
	/* sa_eapid needs to be set on both for radius accounting */
	if (sa->sa_eapid)
		nsa->sa_eapid = strdup(sa->sa_eapid);
	if (sa->sa_eapclass)
		nsa->sa_eapclass = ibuf_dup(sa->sa_eapclass);

	log_info("%srekeyed as new IKESA %s (enc %s%s%s group %s prf %s)",
	    SPI_SA(sa, NULL), print_spi(nsa->sa_hdr.sh_ispi, 8),
	    print_xf(nsa->sa_encr->encr_id, cipher_keylength(nsa->sa_encr) -
	    nsa->sa_encr->encr_saltlength, ikeencxfs),
	    nsa->sa_encr->encr_authid ? "" : " auth ",
	    nsa->sa_encr->encr_authid ? "" : print_xf(nsa->sa_integr->hash_id,
	    hash_keylength(nsa->sa_integr), authxfs),
	    print_xf(nsa->sa_dhgroup->id, 0, groupxfs),
	    print_xf(nsa->sa_prf->hash_id, hash_keylength(sa->sa_prf), prfxfs));
	sa_state(env, nsa, IKEV2_STATE_ESTABLISHED);
	clock_gettime(CLOCK_MONOTONIC, &nsa->sa_starttime);
	iked_radius_acct_start(env, nsa);
	ikev2_enable_timer(env, nsa);

	ikestat_inc(env, ikes_sa_rekeyed);

	nsa->sa_stateflags = nsa->sa_statevalid; /* XXX */

	/* unregister DPD keep alive timer & rekey first */
	if (sa->sa_state == IKEV2_STATE_ESTABLISHED)
		ikev2_disable_timer(env, sa);

	ikev2_ike_sa_setreason(sa, "SA rekeyed");
	ikev2_ikesa_delete(env, sa, nsa->sa_hdr.sh_initiator);
	return (0);
}

void
ikev2_ikesa_delete(struct iked *env, struct iked_sa *sa, int initiator)
{
	struct ibuf			*buf = NULL;
	struct ikev2_delete		*del;

	if (initiator) {
		/* XXX: Can not have simultaneous INFORMATIONAL exchanges */
		if (sa->sa_stateflags & IKED_REQ_INF)
			goto done;
		/* Send PAYLOAD_DELETE */
		if ((buf = ibuf_static()) == NULL)
			goto done;
		if ((del = ibuf_reserve(buf, sizeof(*del))) == NULL)
			goto done;
		del->del_protoid = IKEV2_SAPROTO_IKE;
		del->del_spisize = 0;
		del->del_nspi = 0;
		if (ikev2_send_ike_e(env, sa, buf, IKEV2_PAYLOAD_DELETE,
		    IKEV2_EXCHANGE_INFORMATIONAL, 0) == -1)
			goto done;
		sa->sa_stateflags |= IKED_REQ_INF;
		log_info("%s: sent delete, closing SA", SPI_SA(sa, __func__));
done:
		ibuf_free(buf);
		sa_state(env, sa, IKEV2_STATE_CLOSED);
	} else {
		sa_state(env, sa, IKEV2_STATE_CLOSING);
	}

	/* Remove IKE-SA after timeout, e.g. if we don't get a delete */
	timer_set(env, &sa->sa_timer, ikev2_ike_sa_timeout, sa);
	timer_add(env, &sa->sa_timer, IKED_IKE_SA_DELETE_TIMEOUT);
	ikev2_ike_sa_setreason(sa, "deleting SA");
}

void
ikev2_ikesa_recv_delete(struct iked *env, struct iked_sa *sa)
{
	log_info("%s: received delete", SPI_SA(sa, __func__));
	if (sa->sa_nexti) {
		/*
		 * We initiated rekeying, but since sa_nexti is still set
		 * we have to assume that the the peer did not receive our
		 * rekey message. So remove the initiated SA and -- if
		 * sa_nextr is set -- keep the responder SA instead.
		 */
		if (sa->sa_nextr) {
			log_debug("%s: resolving simultaneous IKE SA rekeying",
			    SPI_SA(sa, __func__));
			ikev2_ikesa_enable(env, sa, sa->sa_nextr);
			/* unlink sa_nextr */
			sa->sa_nextr->sa_prevr = NULL;
			sa->sa_nextr = NULL;
		}
		ikev2_ike_sa_setreason(sa->sa_nexti,
		    "received delete (simultaneous rekeying)");
		sa_free(env, sa->sa_nexti);
		sa->sa_nexti = NULL;	/* reset by sa_free */
	}
	ikev2_ike_sa_setreason(sa, "received delete");
	if (env->sc_stickyaddress) {
		/* delay deletion if client reconnects soon */
		sa_state(env, sa, IKEV2_STATE_CLOSING);
		timer_del(env, &sa->sa_timer);
		timer_set(env, &sa->sa_timer, ikev2_ike_sa_timeout, sa);
		timer_add(env, &sa->sa_timer, 3 * IKED_RETRANSMIT_TIMEOUT);
	} else {
		sa_state(env, sa, IKEV2_STATE_CLOSED);
	}
}

int
ikev2_resp_create_child_sa(struct iked *env, struct iked_message *msg)
{
	struct iked_childsa		*csa = NULL;
	struct iked_proposal		*prop;
	struct iked_proposals		 proposals;
	struct iked_kex			*kex, *kextmp = NULL;
	struct iked_sa			*nsa = NULL, *sa = msg->msg_sa;
	struct iked_spi			*spi, *rekey = &msg->msg_rekey;
	struct iked_transform		*xform;
	struct ikev2_keyexchange	*ke;
	struct ikev2_payload		*pld = NULL;
	struct ibuf			*e = NULL, *nonce = NULL;
	uint8_t				 firstpayload;
	ssize_t				 len = 0;
	int				 initiator, protoid, rekeying = 1;
	int				 ret = -1;
	int				 pfs = 0;

	initiator = sa->sa_hdr.sh_initiator ? 1 : 0;

	if (!ikev2_msg_frompeer(msg) || msg->msg_prop == NULL)
		return (0);

	TAILQ_INIT(&proposals);

	if ((protoid = rekey->spi_protoid) == 0) {
		/*
		 * If REKEY_SA notification is not present, then it's either
		 * IKE SA rekeying or the client wants to create additional
		 * CHILD SAs
		 */
		if (msg->msg_prop->prop_protoid == IKEV2_SAPROTO_IKE) {
			protoid = rekey->spi_protoid = IKEV2_SAPROTO_IKE;
			if (sa->sa_hdr.sh_initiator)
				rekey->spi = sa->sa_hdr.sh_rspi;
			else
				rekey->spi = sa->sa_hdr.sh_ispi;
			rekey->spi_size = 8;
		} else {
			protoid = msg->msg_prop->prop_protoid;
			rekeying = 0;
		}
	}

	if (rekeying)
		log_debug("%s: rekey %s spi %s", __func__,
		    print_map(rekey->spi_protoid, ikev2_saproto_map),
		    print_spi(rekey->spi, rekey->spi_size));
	else
		log_debug("%s: creating new %s SA", __func__,
		    print_map(protoid, ikev2_saproto_map));

	if (protoid == IKEV2_SAPROTO_IKE) {
		if ((sa->sa_stateflags & IKED_REQ_CHILDSA)
		    && !(sa->sa_nexti)) {
			log_debug("%s: Ignore IKE SA rekey: waiting for Child "
			    "SA response.", __func__);
			/* Ignore, don't send error */
			msg->msg_valid = 0;
			return (0);
		}

		/* IKE SA rekeying */
		spi = &msg->msg_prop->prop_peerspi;

		if ((nsa = sa_new(env, spi->spi, 0, 0,
		    msg->msg_policy)) == NULL) {
			log_debug("%s: failed to get new SA", __func__);
			return (ret);
		}

		if (ikev2_sa_responder(env, nsa, sa, msg)) {
			log_debug("%s: failed to get IKE SA keys", __func__);
			return (ret);
		}

		sa_state(env, nsa, IKEV2_STATE_AUTH_SUCCESS);

		nonce = nsa->sa_rnonce;
		kex = &nsa->sa_kex;
	} else {
		/* Child SA creating/rekeying */

		if ((kex = kextmp = calloc(1, sizeof(*kextmp))) == NULL) {
			log_debug("%s: calloc kex", __func__);
			goto fail;
		}

		if (proposals_negotiate(&proposals,
		    &sa->sa_policy->pol_proposals, &msg->msg_proposals,
		    1, msg->msg_dhgroup) != 0) {
			log_info("%s: no proposal chosen", __func__);
			msg->msg_error = IKEV2_N_NO_PROPOSAL_CHOSEN;
			ikestat_inc(env, ikes_sa_proposals_negotiate_failures);
			goto fail;
		}

		/* Check KE payload for PFS, ignore if DH transform is NONE */
		if (((xform = config_findtransform(&proposals,
		    IKEV2_XFORMTYPE_DH, protoid)) != NULL) &&
		    xform->xform_id != IKEV2_XFORMDH_NONE &&
		    ibuf_length(msg->msg_ke)) {
			log_debug("%s: using PFS", __func__);
			if (ikev2_sa_responder_dh(kex, &proposals,
			    msg, protoid) < 0) {
				log_debug("%s: failed to setup DH", __func__);
				goto fail;
			}
			pfs = 1;
			/* XXX check group against policy ? */
		}

		/* Update peer SPI */
		TAILQ_FOREACH(prop, &proposals, prop_entry) {
			if (prop->prop_protoid == protoid)
				break;
		}
		if (prop == NULL) {
			log_debug("%s: failed to find %s proposals", __func__,
			    print_map(protoid, ikev2_saproto_map));
			goto fail;
		} else
			prop->prop_peerspi = msg->msg_prop->prop_peerspi;

		/* Set rekeying flags on Child SAs */
		if (rekeying) {
			if ((csa = childsa_lookup(sa, rekey->spi,
			    rekey->spi_protoid)) == NULL) {
				log_info("%s: CHILD SA %s wasn't found",
				    SPI_SA(sa, __func__),
				    print_spi(rekey->spi, rekey->spi_size));
				msg->msg_error = IKEV2_N_CHILD_SA_NOT_FOUND;
				goto fail;
			}
			if (!csa->csa_loaded || !csa->csa_peersa ||
			    !csa->csa_peersa->csa_loaded) {
				log_info("%s: CHILD SA %s is not loaded"
				    " or no peer SA", SPI_SA(sa, __func__),
				    print_spi(rekey->spi, rekey->spi_size));
				msg->msg_error = IKEV2_N_CHILD_SA_NOT_FOUND;
				goto fail;
			}
			csa->csa_rekey = 1;
			csa->csa_peersa->csa_rekey = 1;
		}

		/* Update initiator's nonce */
		if (!ibuf_length(msg->msg_nonce)) {
			log_debug("%s: initiator didn't send nonce", __func__);
			goto fail;
		}
		ibuf_free(kex->kex_inonce);
		kex->kex_inonce = msg->msg_nonce;
		msg->msg_nonce = NULL;

		/* Generate new responder's nonce */
		if ((nonce = ibuf_random(IKED_NONCE_SIZE)) == NULL)
			goto fail;

		/* Update responder's nonce */
		ibuf_free(kex->kex_rnonce);
		kex->kex_rnonce = nonce;

		if (ikev2_childsa_negotiate(env, sa, kex, &proposals, 0, pfs)) {
			log_debug("%s: failed to get CHILD SAs", __func__);
			goto fail;
		}

		if (rekeying && (sa->sa_stateflags & IKED_REQ_CHILDSA) &&
		    csa && (sa->sa_rekeyspi == csa->csa_peerspi)) {
			log_info("%s: simultaneous rekeying for CHILD SA %s/%s",
			    SPI_SA(sa, __func__),
			    print_spi(rekey->spi, rekey->spi_size),
			    print_spi(sa->sa_rekeyspi, rekey->spi_size));
			ibuf_free(sa->sa_simult);
			if (ikev2_nonce_cmp(kex->kex_inonce, nonce) < 0)
				sa->sa_simult = ibuf_dup(kex->kex_inonce);
			else
				sa->sa_simult = ibuf_dup(nonce);
		}
	}

	if ((e = ibuf_static()) == NULL)
		goto done;

	if (!nsa && sa->sa_ipcompr.ic_transform &&
	    (len = ikev2_add_ipcompnotify(env, e, &pld, 0, sa, 0)) == -1)
		goto done;
	if (!nsa && sa->sa_used_transport_mode &&
	    (len = ikev2_add_transport_mode(env, e, &pld, len, sa)) == -1)
		goto done;

	if (pld) {
		firstpayload = IKEV2_PAYLOAD_NOTIFY;
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_SA) == -1)
			goto done;
	} else
		firstpayload = IKEV2_PAYLOAD_SA;

	/* SA payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;

	if ((len = ikev2_add_proposals(env, nsa ? nsa : sa, e,
	    nsa ? &nsa->sa_proposals : &proposals,
	    protoid, 0, nsa ? 1 : 0, 0)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONCE) == -1)
		goto done;

	/* NONCE payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if (ikev2_add_buf(e, nonce) == -1)
		goto done;
	len = ibuf_size(nonce);

	if (protoid == IKEV2_SAPROTO_IKE || pfs) {

		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_KE) == -1)
			goto done;

		/* KE payload */
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((ke = ibuf_reserve(e, sizeof(*ke))) == NULL)
			goto done;
		if (kex->kex_dhgroup == NULL) {
			log_debug("%s: invalid dh", __func__);
			goto done;
		}
		ke->kex_dhgroup = htobe16(kex->kex_dhgroup->id);
		if (ikev2_add_buf(e, kex->kex_dhrexchange) == -1)
			goto done;
		len = sizeof(*ke) + ibuf_size(kex->kex_dhrexchange);
	}

	if (protoid != IKEV2_SAPROTO_IKE)
		if ((len = ikev2_add_ts(e, &pld, len, sa, initiator)) == -1)
			goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	if ((ret = ikev2_msg_send_encrypt(env, sa, &e,
	    IKEV2_EXCHANGE_CREATE_CHILD_SA, firstpayload, 1)) == -1)
		goto done;

	if (protoid == IKEV2_SAPROTO_IKE) {
		/*
		 * If we also have initiated rekeying for this IKE SA, then
		 * sa_nexti is already set. In this case don't enable the new SA
		 * immediately, but record it in sa_nextr, until the exchange
		 * for sa_nexti completes in ikev2_init_create_child_sa() and
		 * the 'winner' can be selected by comparing nonces.
		 */
		if (sa->sa_nexti) {
			log_info("%s: simultaneous IKE SA rekeying",
			    SPI_SA(sa, __func__));
			sa->sa_nextr = nsa;
			nsa->sa_prevr = sa;	/* backpointer */
			ret = 0;
		} else
			ret = ikev2_ikesa_enable(env, sa, nsa);
	} else
		ret = ikev2_childsa_enable(env, sa);

 done:
	if (ret && protoid != IKEV2_SAPROTO_IKE)
		ikev2_childsa_delete(env, sa, 0, 0, NULL, 1);
	ibuf_free(e);
	config_free_proposals(&proposals, 0);
	config_free_kex(kextmp);
	return (ret);

 fail:
	config_free_proposals(&proposals, 0);
	config_free_kex(kextmp);
	return (-1);
}

void
ikev2_ike_sa_setreason(struct iked_sa *sa, char *reason)
{
	/* allow update only if reason is reset to NULL */
	if (reason == NULL || sa->sa_reason == NULL)
		sa->sa_reason = reason;
}

void
ikev2_ike_sa_timeout(struct iked *env, void *arg)
{
	struct iked_sa			*sa = arg;

	log_debug("%s: closing SA", __func__);
	ikev2_ike_sa_setreason(sa, "timeout");
	sa_free(env, sa);
}

void
ikev2_ike_sa_rekey_timeout(struct iked *env, void *arg)
{
	struct iked_sa			*sa = arg;

	log_debug("%s: closing SA", __func__);
	ikev2_ike_sa_setreason(sa, "rekey timeout");
	sa_free(env, sa);
}

void
ikev2_ike_sa_rekey_schedule(struct iked *env, struct iked_sa *sa)
{
	timer_add(env, &sa->sa_rekey, (sa->sa_policy->pol_rekey * 850 +
	    arc4random_uniform(100)) / 1000);
}

/* rekey delayed, so re-try after short delay (1% of configured) */
void
ikev2_ike_sa_rekey_schedule_fast(struct iked *env, struct iked_sa *sa)
{
	int timeout = sa->sa_policy->pol_rekey / 100; /* 1% */

	if (timeout > 60)
		timeout = 60;	/* max */
	else if (timeout < 4)
		timeout = 4;	/* min */
	timer_add(env, &sa->sa_rekey, timeout);
}

void
ikev2_ike_sa_alive(struct iked *env, void *arg)
{
	struct iked_sa			*sa = arg;
	struct iked_childsa		*csa = NULL;
	uint64_t			 last_used, diff;
	int				 foundin = 0, foundout = 0;
	int				 ikeidle = 0;

	if (env->sc_alive_timeout == 0)
		return;

	/* check for incoming traffic on any child SA */
	TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
		if (!csa->csa_loaded)
			continue;
		if (pfkey_sa_last_used(env, csa, &last_used) != 0)
			continue;
		diff = (uint32_t)(gettime() - last_used);
		log_debug("%s: %s CHILD SA spi %s last used %llu second(s) ago",
		    __func__,
		    csa->csa_dir == IPSP_DIRECTION_IN ? "incoming" : "outgoing",
		    print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size), diff);
		if (diff < env->sc_alive_timeout) {
			if (csa->csa_dir == IPSP_DIRECTION_IN) {
				foundin = 1;
				break;
			} else {
				foundout = 1;
			}
		}
	}

	diff = (uint32_t)(gettime() - sa->sa_last_recvd);
	if (diff >= IKED_IKE_SA_LAST_RECVD_TIMEOUT) {
		ikeidle = 1;
		log_debug("%s: IKE SA %p ispi %s rspi %s last received %llu"
		    " second(s) ago", __func__, sa,
		    print_spi(sa->sa_hdr.sh_ispi, 8),
		    print_spi(sa->sa_hdr.sh_rspi, 8), diff);
	}

	/*
	 * send probe if any outgoing SA has been used, but no incoming
	 * SA, or if we haven't received an IKE message. but only if we
	 * are not already waiting for an answer.
	 */
	if (((!foundin && foundout) || ikeidle) &&
	    (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF)) == 0) {
		log_debug("%s: sending alive check", __func__);
		ikev2_send_ike_e(env, sa, NULL, IKEV2_PAYLOAD_NONE,
		    IKEV2_EXCHANGE_INFORMATIONAL, 0);
		sa->sa_stateflags |= IKED_REQ_INF;
		ikestat_inc(env, ikes_dpd_sent);
	}

	/* re-register */
	timer_add(env, &sa->sa_timer, env->sc_alive_timeout);
}

void
ikev2_ike_sa_keepalive(struct iked *env, void *arg)
{
	struct iked_sa			*sa = arg;
	uint8_t				 marker = 0xff;

	if (sendtofrom(sa->sa_fd, &marker, sizeof(marker), 0,
	    (struct sockaddr *)&sa->sa_peer.addr, sa->sa_peer.addr.ss_len,
	    (struct sockaddr *)&sa->sa_local.addr, sa->sa_local.addr.ss_len)
	    == -1)
		log_warn("%s: sendtofrom: peer %s local %s", __func__,
		    print_addr(&sa->sa_peer.addr),
		    print_addr(&sa->sa_local.addr));
	else
		log_debug("%s: peer %s local %s", __func__,
		    print_addr(&sa->sa_peer.addr),
		    print_addr(&sa->sa_local.addr));
	ikestat_inc(env, ikes_keepalive_sent);
	timer_add(env, &sa->sa_keepalive, IKED_IKE_SA_KEEPALIVE_TIMEOUT);
}

int
ikev2_send_informational(struct iked *env, struct iked_message *msg)
{
	struct iked_message		 resp;
	struct ike_header		*hdr;
	struct ikev2_payload		*pld;
	struct ikev2_notify		*n;
	struct iked_sa			*sa = msg->msg_sa, sah;
	struct ibuf			*buf, *e = NULL;
	int				 ret = -1;

	if (msg->msg_error == 0)
		return (0);

	if ((buf = ikev2_msg_init(env, &resp,
	    &msg->msg_peer, msg->msg_peerlen,
	    &msg->msg_local, msg->msg_locallen, 0)) == NULL)
		goto done;

	/* New encrypted message buffer */
	if ((e = ibuf_static()) == NULL)
		goto done;

	/* NOTIFY payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;

	if ((n = ibuf_reserve(e, sizeof(*n))) == NULL)
		goto done;
	n->n_protoid = IKEV2_SAPROTO_IKE;	/* XXX ESP etc. */
	n->n_spisize = 0;
	n->n_type = htobe16(msg->msg_error);

	switch (msg->msg_error) {
	case IKEV2_N_INVALID_IKE_SPI:
		break;
	case IKEV2_N_NO_PROPOSAL_CHOSEN:
		ikev2_log_proposal(msg->msg_sa, &msg->msg_proposals);
		break;
	default:
		log_warnx("%s: unsupported notification %s", SPI_SA(sa,
		    __func__), print_map(msg->msg_error, ikev2_n_map));
		goto done;
	}
	log_info("%s: %s", SPI_SA(sa, __func__),
	    print_map(msg->msg_error, ikev2_n_map));

	if (ikev2_next_payload(pld, sizeof(*n), IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	if (sa != NULL && msg->msg_e) {
		resp.msg_msgid = ikev2_msg_id(env, sa);

		/* IKE header */
		if ((hdr = ikev2_add_header(buf, sa, resp.msg_msgid,
		    IKEV2_PAYLOAD_SK, IKEV2_EXCHANGE_INFORMATIONAL,
		    0)) == NULL)
			goto done;

		if ((pld = ikev2_add_payload(buf)) == NULL)
			goto done;

		/* Encrypt message and add as an E payload */
		if ((e = ikev2_msg_encrypt(env, sa, e, buf)) == NULL) {
			log_debug("%s: encryption failed", __func__);
			goto done;
		}
		if (ibuf_add_ibuf(buf, e) != 0)
			goto done;
		if (ikev2_next_payload(pld, ibuf_size(e),
		    IKEV2_PAYLOAD_NOTIFY) == -1)
			goto done;

		if (ikev2_set_header(hdr, ibuf_size(buf) - sizeof(*hdr)) == -1)
			goto done;

		/* Add integrity checksum (HMAC) */
		if (ikev2_msg_integr(env, sa, buf) != 0) {
			log_debug("%s: integrity checksum failed", __func__);
			goto done;
		}
	} else {
		if ((hdr = ibuf_seek(msg->msg_data, 0, sizeof(*hdr))) == NULL)
			goto done;

		bzero(&sah, sizeof(sah));
		sah.sa_hdr.sh_rspi = betoh64(hdr->ike_rspi);
		sah.sa_hdr.sh_ispi = betoh64(hdr->ike_ispi);
		sah.sa_hdr.sh_initiator =
		    hdr->ike_flags & IKEV2_FLAG_INITIATOR ? 0 : 1;

		resp.msg_msgid = ikev2_msg_id(env, &sah);

		/* IKE header */
		if ((hdr = ikev2_add_header(buf, &sah, resp.msg_msgid,
		    IKEV2_PAYLOAD_NOTIFY, IKEV2_EXCHANGE_INFORMATIONAL,
		    0)) == NULL)
			goto done;
		if (ibuf_add_ibuf(buf, e) != 0)
			goto done;
		if (ikev2_set_header(hdr, ibuf_size(buf) - sizeof(*hdr)) == -1)
			goto done;
	}

	resp.msg_data = buf;
	resp.msg_fd = msg->msg_fd;
	TAILQ_INIT(&resp.msg_proposals);

	ret = ikev2_msg_send(env, &resp);

 done:
	ibuf_free(e);
	ikev2_msg_cleanup(env, &resp);

	return (ret);
}

ssize_t
ikev2_psk(struct iked_sa *sa, uint8_t *data, size_t length,
    uint8_t **pskptr)
{
	uint8_t		*psk;
	size_t		 psklen = -1;

	if (hash_setkey(sa->sa_prf, data, length) == NULL)
		return (-1);

	if ((psk = calloc(1, hash_keylength(sa->sa_prf))) == NULL)
		return (-1);

	hash_init(sa->sa_prf);
	hash_update(sa->sa_prf, IKEV2_KEYPAD, strlen(IKEV2_KEYPAD));
	hash_final(sa->sa_prf, psk, &psklen);

	*pskptr = psk;
	return (psklen);
}

int
ikev2_sa_initiator_dh(struct iked_sa *sa, struct iked_message *msg,
    unsigned int proto, struct iked_sa *osa)
{
	struct iked_policy	*pol = sa->sa_policy;
	struct iked_transform	*xform;
	struct iked_proposals	*proposals;

	proposals = osa ? &osa->sa_proposals : &pol->pol_proposals;

	if (sa->sa_dhgroup == NULL) {
		if ((xform = config_findtransform(proposals,
		    IKEV2_XFORMTYPE_DH, proto)) == NULL) {
			log_debug("%s: did not find dh transform", __func__);
			return (-1);
		}
		if ((sa->sa_dhgroup =
		    group_get(xform->xform_id)) == NULL) {
			log_debug("%s: invalid dh %d", __func__,
			    xform->xform_id);
			return (-1);
		}
	}

	if (!ibuf_length(sa->sa_dhiexchange)) {
		if (dh_create_exchange(sa->sa_dhgroup,
		    &sa->sa_dhiexchange, NULL) == -1) {
			log_debug("%s: failed to get dh exchange", __func__);
			return (-1);
		}
	}

	/* Initial message */
	if (msg == NULL)
		return (0);

	if (!ibuf_length(sa->sa_dhrexchange)) {
		if (!ibuf_length(msg->msg_ke)) {
			log_debug("%s: invalid peer dh exchange", __func__);
			return (-1);
		}
		sa->sa_dhrexchange = msg->msg_ke;
		msg->msg_ke = NULL;
	}

	/* Set a pointer to the peer exchange */
	sa->sa_dhpeer = sa->sa_dhrexchange;
	return (0);
}

int
ikev2_sa_negotiate_common(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg, int groupid)
{
	struct iked_transform	*xform;

	/* XXX we need a better way to get this */
	if (proposals_negotiate(&sa->sa_proposals,
	    &msg->msg_policy->pol_proposals, &msg->msg_proposals, 0, groupid) != 0) {
		log_info("%s: proposals_negotiate", __func__);
		ikestat_inc(env, ikes_sa_proposals_negotiate_failures);
		return (-1);
	}
	if (sa_stateok(sa, IKEV2_STATE_SA_INIT))
		sa_stateflags(sa, IKED_REQ_SA);

	if (sa->sa_encr == NULL) {
		if ((xform = config_findtransform(&sa->sa_proposals,
		    IKEV2_XFORMTYPE_ENCR, 0)) == NULL) {
			log_info("%s: did not find encr transform",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		if ((sa->sa_encr = cipher_new(xform->xform_type,
		    xform->xform_id, xform->xform_length)) == NULL) {
			log_info("%s: failed to get encr",
			    SPI_SA(sa, __func__));
			return (-1);
		}
	}

	/* For AEAD ciphers integrity is implicit */
	if (sa->sa_encr->encr_authid && sa->sa_integr == NULL) {
		if ((sa->sa_integr = hash_new(IKEV2_XFORMTYPE_INTEGR,
		    sa->sa_encr->encr_authid)) == NULL) {
			log_info("%s: failed to get AEAD integr",
			    SPI_SA(sa, __func__));
			return (-1);
		}
	}

	if (sa->sa_prf == NULL) {
		if ((xform = config_findtransform(&sa->sa_proposals,
		    IKEV2_XFORMTYPE_PRF, 0)) == NULL) {
			log_info("%s: did not find prf transform",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		if ((sa->sa_prf =
		    hash_new(xform->xform_type, xform->xform_id)) == NULL) {
			log_info("%s: failed to get prf", SPI_SA(sa, __func__));
			return (-1);
		}
	}

	if (sa->sa_integr == NULL) {
		if ((xform = config_findtransform(&sa->sa_proposals,
		    IKEV2_XFORMTYPE_INTEGR, 0)) == NULL) {
			log_info("%s: did not find integr transform",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		if ((sa->sa_integr =
		    hash_new(xform->xform_type, xform->xform_id)) == NULL) {
			log_info("%s: failed to get integr",
			    SPI_SA(sa, __func__));
			return (-1);
		}
	}

	return (0);
}

int
ikev2_sa_initiator(struct iked *env, struct iked_sa *sa,
    struct iked_sa *osa, struct iked_message *msg)
{
	if (ikev2_sa_initiator_dh(sa, msg, 0, osa) < 0)
		return (-1);

	if (!ibuf_length(sa->sa_inonce)) {
		if ((sa->sa_inonce = ibuf_random(IKED_NONCE_SIZE)) == NULL) {
			log_info("%s: failed to get local nonce",
			    SPI_SA(sa, __func__));
			return (-1);
		}
	}

	/* Initial message */
	if (msg == NULL)
		return (0);

	if (!ibuf_length(sa->sa_rnonce)) {
		if (!ibuf_length(msg->msg_nonce)) {
			log_info("%s: invalid peer nonce",
			    SPI_SA(sa, __func__));
			return (-1);
		}
		sa->sa_rnonce = msg->msg_nonce;
		msg->msg_nonce = NULL;
	}

	if (ikev2_sa_negotiate_common(env, sa, msg, -1) != 0)
		return (-1);

	ibuf_free(sa->sa_2ndmsg);
	if ((sa->sa_2ndmsg = ibuf_dup(msg->msg_data)) == NULL) {
		log_info("%s: failed to copy 2nd message",
		    SPI_SA(sa, __func__));
		return (-1);
	}

	return (ikev2_sa_keys(env, sa, osa ? osa->sa_key_d : NULL));
}

int
ikev2_sa_responder_dh(struct iked_kex *kex, struct iked_proposals *proposals,
    struct iked_message *msg, unsigned int proto)
{
	struct iked_transform	*xform;

	if (kex->kex_dhgroup == NULL) {
		if ((xform = config_findtransform(proposals,
		    IKEV2_XFORMTYPE_DH, proto)) == NULL) {
			log_info("%s: did not find dh transform",
			    SPI_SA(msg->msg_sa, __func__));
			return (-1);
		}
		if ((kex->kex_dhgroup =
		    group_get(xform->xform_id)) == NULL) {
			log_debug("%s: invalid dh %d",
			    SPI_SA(msg->msg_sa, __func__), xform->xform_id);
			return (-1);
		}
	}

	/* Look for dhgroup mismatch during an IKE SA negotiation */
	if (msg->msg_dhgroup != kex->kex_dhgroup->id) {
		log_info("%s: want dh %s, KE has %s",
		    SPI_SA(msg->msg_sa, __func__),
		    print_map(kex->kex_dhgroup->id, ikev2_xformdh_map),
		    print_map(msg->msg_dhgroup, ikev2_xformdh_map));
		msg->msg_error = IKEV2_N_INVALID_KE_PAYLOAD;
		msg->msg_dhgroup = kex->kex_dhgroup->id;
		return (-1);
	}

	if (!ibuf_length(kex->kex_dhiexchange)) {
		kex->kex_dhiexchange = msg->msg_ke;
		msg->msg_ke = NULL;
	}

	if (!ibuf_length(kex->kex_dhrexchange)) {
		if (dh_create_exchange(kex->kex_dhgroup,
		    &kex->kex_dhrexchange, kex->kex_dhiexchange) == -1) {
			log_info("%s: failed to get dh exchange",
			    SPI_SA(msg->msg_sa, __func__));
			return (-1);
		}
	}

	/* Set a pointer to the peer exchange */
	kex->kex_dhpeer = kex->kex_dhiexchange;
	return (0);
}

int
ikev2_sa_responder(struct iked *env, struct iked_sa *sa, struct iked_sa *osa,
    struct iked_message *msg)
{
	struct iked_policy	*old;

	/* re-lookup policy based on 'msg' (unless IKESA is rekeyed) */
	if (osa == NULL) {
		old = sa->sa_policy;
		sa->sa_policy = NULL;
		if (policy_lookup(env, msg, &msg->msg_proposals,
		    NULL, 0) != 0 || msg->msg_policy == NULL) {
			sa->sa_policy = old;
			log_info("%s: no proposal chosen", __func__);
			msg->msg_error = IKEV2_N_NO_PROPOSAL_CHOSEN;
			return (-1);
		}
		/* move sa to new policy */
		sa->sa_policy = msg->msg_policy;
		TAILQ_REMOVE(&old->pol_sapeers, sa, sa_peer_entry);
		TAILQ_INSERT_TAIL(&sa->sa_policy->pol_sapeers,
		    sa, sa_peer_entry);
		policy_unref(env, old);
		policy_ref(env, sa->sa_policy);
	}

	sa_state(env, sa, IKEV2_STATE_SA_INIT);

	ibuf_free(sa->sa_1stmsg);
	if ((sa->sa_1stmsg = ibuf_dup(msg->msg_data)) == NULL) {
		log_debug("%s: failed to copy 1st message", __func__);
		return (-1);
	}

	if (sa->sa_rnonce == NULL &&
	    (sa->sa_rnonce = ibuf_random(IKED_NONCE_SIZE)) == NULL) {
		log_debug("%s: failed to get local nonce", __func__);
		return (-1);
	}

	if (!ibuf_length(sa->sa_inonce) &&
	    (ibuf_length(msg->msg_nonce) < IKED_NONCE_MIN)) {
		log_debug("%s: failed to get peer nonce", __func__);
		return (-1);
	}
	sa->sa_inonce = msg->msg_nonce;
	msg->msg_nonce = NULL;

	if (ikev2_sa_negotiate_common(env, sa, msg, msg->msg_dhgroup) != 0)
		return (-1);

	if (ikev2_sa_responder_dh(&sa->sa_kex, &sa->sa_proposals, msg, 0) < 0)
		return (-1);

	return (ikev2_sa_keys(env, sa, osa ? osa->sa_key_d : NULL));
}

int
ikev2_sa_keys(struct iked *env, struct iked_sa *sa, struct ibuf *key)
{
	struct iked_hash	*prf, *integr;
	struct iked_cipher	*encr;
	struct dh_group		*group;
	struct ibuf		*ninr, *dhsecret, *skeyseed, *s, *t;
	size_t			 nonceminlen, ilen, rlen, tmplen;
	uint64_t		 ispi, rspi;
	int			 ret = -1;
	int			 isaead = 0;

	ninr = dhsecret = skeyseed = s = t = NULL;

	if ((encr = sa->sa_encr) == NULL ||
	    (prf = sa->sa_prf) == NULL ||
	    (integr = sa->sa_integr) == NULL ||
	    (group = sa->sa_dhgroup) == NULL) {
		log_info("%s: failed to get key input data",
		    SPI_SA(sa, __func__));
		return (-1);
	}

	/* For AEADs no auth keys are required (see RFC 5282) */
	isaead = !!integr->hash_isaead;

	if (prf->hash_fixedkey)
		nonceminlen = prf->hash_fixedkey;
	else
		nonceminlen = IKED_NONCE_MIN;

	/* Nonces need a minimal size and should have an even length */
	if (ibuf_length(sa->sa_inonce) < nonceminlen ||
	    (ibuf_length(sa->sa_inonce) % 2) != 0 ||
	    ibuf_length(sa->sa_rnonce) < nonceminlen ||
	    (ibuf_length(sa->sa_rnonce) % 2) != 0) {
		log_info("%s: invalid nonces", SPI_SA(sa, __func__));
		return (-1);
	}

	if (prf->hash_fixedkey) {
		/* Half of the key bits must come from Ni, and half from Nr */
		ilen = prf->hash_fixedkey / 2;
		rlen = prf->hash_fixedkey / 2;
	} else {
		/* Most PRF functions accept a variable-length key */
		ilen = ibuf_length(sa->sa_inonce);
		rlen = ibuf_length(sa->sa_rnonce);
	}

	/*
	 *  Depending on whether we're generating new keying material
	 *  or rekeying existing SA the algorithm is different. If the
	 *  "key" argument is not specified a concatenation of nonces
	 *  (Ni | Nr) is used as a PRF key, otherwise a "key" buffer
	 *  is used and PRF is performed on the concatenation of DH
	 *  exchange result and nonces (g^ir | Ni | Nr).  See sections
	 *  2.14 and 2.18 of RFC7296 for more information.
	 */

	/*
	 *  Generate g^ir
	 */
	if (dh_create_shared(group, &dhsecret, sa->sa_dhpeer) == -1) {
		log_info("%s: failed to get dh secret"
		    " group %d secret %zu exchange %zu",
		    SPI_SA(sa, __func__),
		    group->id, ibuf_length(dhsecret),
		    ibuf_length(sa->sa_dhpeer));
		goto done;
	}

	log_debug("%s: DHSECRET with %zu bytes", SPI_SA(sa, __func__),
	    ibuf_size(dhsecret));
	print_hexbuf(dhsecret);

	if (!key) {
		/*
		 * Set PRF key to generate SKEYSEED = prf(Ni | Nr, g^ir)
		 */
		if ((ninr = ibuf_new(ibuf_data(sa->sa_inonce), ilen)) == NULL ||
		    ibuf_add(ninr, ibuf_data(sa->sa_rnonce), rlen) != 0) {
			log_info("%s: failed to get nonce key buffer",
			    SPI_SA(sa, __func__));
			goto done;
		}
		key = ninr;
	} else {
		/*
		 * Set PRF key to generate SKEYSEED = prf(key, g^ir | Ni | Nr)
		 */
		if (ibuf_add(dhsecret, ibuf_data(sa->sa_inonce), ilen) != 0 ||
		    ibuf_add(dhsecret, ibuf_data(sa->sa_rnonce), rlen) != 0) {
			log_info("%s: failed to get nonce key buffer",
			    SPI_SA(sa, __func__));
			goto done;
		}
	}

	if ((hash_setkey(prf, ibuf_data(key), ibuf_size(key))) == NULL) {
		log_info("%s: failed to set prf key", SPI_SA(sa, __func__));
		goto done;
	}

	if ((skeyseed = ibuf_new(NULL, hash_keylength(prf))) == NULL) {
		log_info("%s: failed to get SKEYSEED buffer",
		    SPI_SA(sa, __func__));
		goto done;
	}

	tmplen = 0;
	hash_init(prf);
	hash_update(prf, ibuf_data(dhsecret), ibuf_size(dhsecret));
	hash_final(prf, ibuf_data(skeyseed), &tmplen);

	log_debug("%s: SKEYSEED with %zu bytes", __func__, tmplen);
	print_hex(ibuf_data(skeyseed), 0, tmplen);

	if (ibuf_setsize(skeyseed, tmplen) == -1) {
		log_info("%s: failed to set keymaterial length",
		    SPI_SA(sa, __func__));
		goto done;
	}

	/*
	 * Now generate the key material
	 *
	 * S = Ni | Nr | SPIi | SPIr
	 */

	/* S = Ni | Nr | SPIi | SPIr */
	ilen = ibuf_length(sa->sa_inonce);
	rlen = ibuf_length(sa->sa_rnonce);
	ispi = htobe64(sa->sa_hdr.sh_ispi);
	rspi = htobe64(sa->sa_hdr.sh_rspi);

	if ((s = ibuf_new(ibuf_data(sa->sa_inonce), ilen)) == NULL ||
	    ibuf_add(s, ibuf_data(sa->sa_rnonce), rlen) != 0 ||
	    ibuf_add(s, &ispi, sizeof(ispi)) != 0 ||
	    ibuf_add(s, &rspi, sizeof(rspi)) != 0) {
		log_info("%s: failed to set S buffer",
		    SPI_SA(sa, __func__));
		goto done;
	}

	log_debug("%s: S with %zu bytes", SPI_SA(sa, __func__), ibuf_size(s));
	print_hexbuf(s);

	/*
	 * Get the size of the key material we need and the number
	 * of rounds we need to run the prf+ function.
	 */
	ilen = hash_length(prf) +			/* SK_d */
	    (isaead ? 0 : hash_keylength(integr)) +	/* SK_ai */
	    (isaead ? 0 : hash_keylength(integr)) +	/* SK_ar */
	    cipher_keylength(encr) +			/* SK_ei */
	    cipher_keylength(encr) +			/* SK_er */
	    hash_keylength(prf) +			/* SK_pi */
	    hash_keylength(prf);			/* SK_pr */

	if ((t = ikev2_prfplus(prf, skeyseed, s, ilen)) == NULL) {
		log_info("%s: failed to get IKE SA key material",
		    SPI_SA(sa, __func__));
		goto done;
	}

	/* ibuf_getdata() returns a new buffer from the next read offset */
	if ((sa->sa_key_d = ibuf_getdata(t, hash_length(prf))) == NULL ||
	    (!isaead &&
	    (sa->sa_key_iauth = ibuf_getdata(t, hash_keylength(integr))) ==
	    NULL) ||
	    (!isaead &&
	    (sa->sa_key_rauth = ibuf_getdata(t, hash_keylength(integr))) ==
	    NULL) ||
	    (sa->sa_key_iencr = ibuf_getdata(t, cipher_keylength(encr))) ==
	    NULL ||
	    (sa->sa_key_rencr = ibuf_getdata(t, cipher_keylength(encr))) ==
	    NULL ||
	    (sa->sa_key_iprf = ibuf_getdata(t, hash_length(prf))) == NULL ||
	    (sa->sa_key_rprf = ibuf_getdata(t, hash_length(prf))) == NULL) {
		log_debug("%s: failed to get SA keys", SPI_SA(sa, __func__));
		goto done;
	}

	log_debug("%s: SK_d with %zu bytes", __func__, ibuf_size(sa->sa_key_d));
	print_hexbuf(sa->sa_key_d);
	if (!isaead) {
		log_debug("%s: SK_ai with %zu bytes", __func__,
		    ibuf_size(sa->sa_key_iauth));
		print_hexbuf(sa->sa_key_iauth);
		log_debug("%s: SK_ar with %zu bytes", __func__,
		    ibuf_size(sa->sa_key_rauth));
		print_hexbuf(sa->sa_key_rauth);
	}
	log_debug("%s: SK_ei with %zu bytes", __func__,
	    ibuf_size(sa->sa_key_iencr));
	print_hexbuf(sa->sa_key_iencr);
	log_debug("%s: SK_er with %zu bytes", __func__,
	    ibuf_size(sa->sa_key_rencr));
	print_hexbuf(sa->sa_key_rencr);
	log_debug("%s: SK_pi with %zu bytes", __func__,
	    ibuf_size(sa->sa_key_iprf));
	print_hexbuf(sa->sa_key_iprf);
	log_debug("%s: SK_pr with %zu bytes", __func__,
	    ibuf_size(sa->sa_key_rprf));
	print_hexbuf(sa->sa_key_rprf);

	ret = 0;

 done:
	ibuf_free(ninr);
	ibuf_free(dhsecret);
	ibuf_free(skeyseed);
	ibuf_free(s);
	ibuf_free(t);

	return (ret);
}

void
ikev2_sa_cleanup_dh(struct iked_sa *sa)
{
	ibuf_free(sa->sa_dhiexchange);
	ibuf_free(sa->sa_dhrexchange);
	group_free(sa->sa_dhgroup);
	sa->sa_dhiexchange = NULL;
	sa->sa_dhrexchange = NULL;
	sa->sa_dhgroup = NULL;
}

struct ibuf *
ikev2_prfplus(struct iked_hash *prf, struct ibuf *key, struct ibuf *seed,
    size_t keymatlen)
{
	struct ibuf	*t = NULL, *t1 = NULL, *t2 = NULL;
	size_t		 rlen, i, hashlen = 0;
	uint8_t		 pad = 0;

	/*
	 * prf+ (K, S) = T1 | T2 | T3 | T4 | ...
	 *
	 * T1 = prf (K, S | 0x01)
	 * T2 = prf (K, T1 | S | 0x02)
	 * T3 = prf (K, T2 | S | 0x03)
	 * T4 = prf (K, T3 | S | 0x04)
	 */

	if ((hash_setkey(prf, ibuf_data(key), ibuf_size(key))) == NULL) {
		log_debug("%s: failed to set prf+ key", __func__);
		goto fail;
	}

	if ((t = ibuf_new(NULL, 0)) == NULL) {
		log_debug("%s: failed to get T buffer", __func__);
		goto fail;
	}

	rlen = roundup(keymatlen, hash_length(prf)) / hash_length(prf);
	if (rlen > 255)
		fatalx("ikev2_prfplus: key material too large");

	for (i = 0; i < rlen; i++) {
		if (t1 != NULL) {
			t2 = t1;
		} else
			t2 = ibuf_new(NULL, 0);
		t1 = ibuf_new(NULL, hash_keylength(prf));

		ibuf_add_ibuf(t2, seed);
		pad = i + 1;
		ibuf_add(t2, &pad, 1);

		hash_init(prf);
		hash_update(prf, ibuf_data(t2), ibuf_size(t2));
		hash_final(prf, ibuf_data(t1), &hashlen);

		if (hashlen != hash_length(prf))
			fatalx("ikev2_prfplus: hash length mismatch");

		ibuf_free(t2);
		ibuf_add_ibuf(t, t1);

		log_debug("%s: T%d with %zu bytes", __func__,
		    pad, ibuf_size(t1));
		print_hexbuf(t1);
	}

	log_debug("%s: Tn with %zu bytes", __func__, ibuf_size(t));
	print_hexbuf(t);

	ibuf_free(t1);

	return (t);

 fail:
	ibuf_free(t1);
	ibuf_free(t);

	return (NULL);
}

int
ikev2_sa_tag(struct iked_sa *sa, struct iked_id *id)
{
	char	*format, *domain = NULL, *idrepl = NULL;
	char	 idstr[IKED_ID_SIZE];
	int	 ret = -1;
	size_t	 len;

	free(sa->sa_tag);
	sa->sa_tag = NULL;
	format = sa->sa_policy->pol_tag;

	len = IKED_TAG_SIZE;
	if ((sa->sa_tag = calloc(1, len)) == NULL) {
		log_debug("%s: calloc", __func__);
		goto fail;
	}
	if (strlcpy(sa->sa_tag, format, len) >= len) {
		log_debug("%s: tag too long", __func__);
		goto fail;
	}

	if (ikev2_print_id(id, idstr, sizeof(idstr)) == -1) {
		log_debug("%s: invalid id", __func__);
		goto fail;
	}

	/* ASN.1 DER IDs are too long, use the CN part instead */
	if ((id->id_type == IKEV2_ID_ASN1_DN) &&
	    (idrepl = strstr(idstr, "CN=")) != NULL) {
		domain = strstr(idrepl, "emailAddress=");
		idrepl[strcspn(idrepl, "/")] = '\0';
	} else
		idrepl = idstr;

	if (strstr(format, "$id") != NULL) {
		if (expand_string(sa->sa_tag, len, "$id", idrepl) != 0) {
			log_debug("%s: failed to expand tag", __func__);
			goto fail;
		}
	}

	if (strstr(format, "$eapid") != NULL && sa->sa_eapid != NULL) {
		if (expand_string(sa->sa_tag, len, "$eapid",
		    sa->sa_eapid) != 0) {
			log_debug("%s: failed to expand tag", __func__);
			goto fail;
		}
	}

	if (strstr(format, "$name") != NULL) {
		if (expand_string(sa->sa_tag, len, "$name",
		    sa->sa_policy->pol_name) != 0) {
			log_debug("%s: failed to expand tag", __func__);
			goto fail;
		}
	}

	if (strstr(format, "$domain") != NULL) {
		if (id->id_type == IKEV2_ID_FQDN)
			domain = strchr(idrepl, '.');
		else if (id->id_type == IKEV2_ID_UFQDN)
			domain = strchr(idrepl, '@');
		else if (*idstr == '/' && domain != NULL)
			domain = strchr(domain, '@');
		else
			domain = NULL;
		if (domain == NULL || strlen(domain) < 2) {
			log_debug("%s: no valid domain in ID %s",
			    __func__, idstr);
			goto fail;
		}
		domain++;
		if (expand_string(sa->sa_tag, len, "$domain", domain) != 0) {
			log_debug("%s: failed to expand tag", __func__);
			goto fail;
		}
	}

	log_debug("%s: %s (%zu)", __func__, sa->sa_tag, strlen(sa->sa_tag));

	ret = 0;
 fail:
	if (ret != 0) {
		free(sa->sa_tag);
		sa->sa_tag = NULL;
	}

	return (ret);
}

int
ikev2_childsa_delete_proposed(struct iked *env, struct iked_sa *sa,
    struct iked_proposals *proposals)
{
	struct ibuf			*buf = NULL;
	struct iked_proposal		*prop;
	struct ikev2_delete		*del;
	uint32_t			 spi32;
	uint8_t				 protoid = 0;
	int				 ret = -1, count;

	if (!sa_stateok(sa, IKEV2_STATE_VALID))
		return (-1);

	count = 0;
	TAILQ_FOREACH(prop, proposals, prop_entry) {
		if (ikev2_valid_proposal(prop, NULL, NULL, NULL) != 0)
			continue;
		protoid = prop->prop_protoid;
		count++;
	}
	if (count == 0)
		return (0);
	if ((buf = ibuf_static()) == NULL)
		return (-1);
	if ((del = ibuf_reserve(buf, sizeof(*del))) == NULL)
		goto done;
	/* XXX we assume all have the same protoid */
	del->del_protoid = protoid;
	del->del_spisize = 4;
	del->del_nspi = htobe16(count);

	TAILQ_FOREACH(prop, proposals, prop_entry) {
		if (ikev2_valid_proposal(prop, NULL, NULL, NULL) != 0)
			continue;
		spi32 = htobe32(prop->prop_localspi.spi);
		if (ibuf_add(buf, &spi32, sizeof(spi32)))
			goto done;
	}

	if (ikev2_send_ike_e(env, sa, buf, IKEV2_PAYLOAD_DELETE,
	    IKEV2_EXCHANGE_INFORMATIONAL, 0) == -1)
		goto done;
	sa->sa_stateflags |= IKED_REQ_INF;
	ret = 0;
 done:
	ibuf_free(buf);

	return (ret);
}

int
ikev2_childsa_negotiate(struct iked *env, struct iked_sa *sa,
    struct iked_kex *kex, struct iked_proposals *proposals, int initiator,
    int pfs)
{
	struct iked_proposal	*prop;
	struct iked_transform	*xform, *encrxf = NULL, *integrxf = NULL;
	struct iked_childsa	*csa = NULL, *csb = NULL;
	struct iked_childsa	*csa2 = NULL, *csb2 = NULL;
	struct iked_flow	*flow, *saflow, *flowa, *flowb;
	struct iked_ipcomp	*ic;
	struct ibuf		*keymat = NULL, *seed = NULL, *dhsecret = NULL;
	struct dh_group		*group = NULL;
	uint32_t		 spi = 0;
	unsigned int		 i;
	size_t			 ilen = 0;
	int			 esn, skip, ret = -1;

	if (!sa_stateok(sa, IKEV2_STATE_VALID))
		return (-1);

	if (ikev2_sa_tag(sa, IKESA_DSTID(sa)) == -1)
		return (-1);

	ic = initiator ? &sa->sa_ipcompi : &sa->sa_ipcompr;
	if (ic->ic_transform == 0 || ic->ic_cpi_out == 0 ||
	    (initiator && ic->ic_cpi_in == 0))
		ic = NULL;

	/* reset state */
	sa->sa_used_transport_mode = 0;

	/* We need to determine the key material length first */
	TAILQ_FOREACH(prop, proposals, prop_entry) {
		if (prop->prop_protoid == IKEV2_SAPROTO_IKE)
			continue;
		log_debug("%s: proposal %d", __func__, prop->prop_id);
		for (i = 0; i < prop->prop_nxforms; i++) {
			xform = prop->prop_xforms + i;
			xform->xform_keylength =
			    keylength_xf(prop->prop_protoid,
			    xform->xform_type, xform->xform_id);

			switch (xform->xform_type) {
			case IKEV2_XFORMTYPE_ENCR:
			case IKEV2_XFORMTYPE_INTEGR:
				if (xform->xform_length)
					xform->xform_keylength =
					    xform->xform_length;
				xform->xform_keylength +=
				    noncelength_xf(xform->xform_type,
				    xform->xform_id);
				ilen += xform->xform_keylength / 8;
				break;
			}
		}
	}

	/* double key material length for inbound/outbound */
	ilen *= 2;

	log_debug("%s: key material length %zu", __func__, ilen);

	if ((seed = ibuf_new(NULL, 0)) == NULL) {
		log_debug("%s: failed to setup IKE SA key material", __func__);
		goto done;
	}
	if (pfs) {
		log_debug("%s: using PFS", __func__);
		if (kex->kex_dhpeer == NULL ||
		    ibuf_size(kex->kex_dhpeer) == 0 ||
		    (group = kex->kex_dhgroup) == NULL) {
			log_debug("%s: no dh group for pfs", __func__);
			goto done;
		}
		if (dh_create_shared(group, &dhsecret, kex->kex_dhpeer) == -1) {
			log_debug("%s: failed to get dh secret"
			    " group %d secret %zu exchange %zu",
			    __func__, group->id, ibuf_length(dhsecret),
			    ibuf_length(kex->kex_dhpeer));
			goto done;
		}
		if (ibuf_add_ibuf(seed, dhsecret) != 0) {
			log_debug("%s: failed to set dh secret", __func__);
			goto done;
		}
	}
	if (ibuf_add_ibuf(seed, kex->kex_inonce) != 0 ||
	    ibuf_add_ibuf(seed, kex->kex_rnonce) != 0 ||
	    (keymat = ikev2_prfplus(sa->sa_prf,
	    sa->sa_key_d, seed, ilen)) == NULL) {
		log_debug("%s: failed to get IKE SA key material", __func__);
		goto done;
	}

	/* Create the new flows */
	TAILQ_FOREACH(prop, proposals, prop_entry) {
		if (ikev2_valid_proposal(prop, NULL, NULL, NULL) != 0)
			continue;

		RB_FOREACH(flow, iked_flows, &sa->sa_policy->pol_flows) {

			if ((flowa = calloc(1, sizeof(*flowa))) == NULL) {
				log_debug("%s: failed to get flow", __func__);
				goto done;
			}

			memcpy(flowa, flow, sizeof(*flow));
			flowa->flow_dir = IPSP_DIRECTION_OUT;
			flowa->flow_saproto = ic ? IKEV2_SAPROTO_IPCOMP :
			    prop->prop_protoid;
			flowa->flow_rdomain = sa->sa_policy->pol_rdomain;
			flowa->flow_local = &sa->sa_local;
			flowa->flow_peer = &sa->sa_peer;
			flowa->flow_ikesa = sa;
			if (ikev2_cp_fixflow(sa, flow, flowa) == -1) {
				flow_free(flowa);
				continue;
			}

			skip = 0;
			TAILQ_FOREACH(saflow, &sa->sa_flows, flow_entry) {
				if (flow_equal(saflow, flowa)) {
					skip = 1;
					break;
				}
			}
			if (skip) {
				flow_free(flowa);
				continue;
			}

			if ((flowb = calloc(1, sizeof(*flowb))) == NULL) {
				log_debug("%s: failed to get flow", __func__);
				flow_free(flowa);
				goto done;
			}

			memcpy(flowb, flowa, sizeof(*flow));

			flowb->flow_dir = IPSP_DIRECTION_IN;
			memcpy(&flowb->flow_src, &flow->flow_dst,
			    sizeof(flow->flow_dst));
			memcpy(&flowb->flow_dst, &flow->flow_src,
			    sizeof(flow->flow_src));
			if (ikev2_cp_fixflow(sa, flow, flowb) == -1) {
				flow_free(flowa);
				flow_free(flowb);
				continue;
			}

			TAILQ_INSERT_TAIL(&sa->sa_flows, flowa, flow_entry);
			TAILQ_INSERT_TAIL(&sa->sa_flows, flowb, flow_entry);
		}
	}

	/* create the CHILD SAs using the key material */
	TAILQ_FOREACH(prop, proposals, prop_entry) {
		if (ikev2_valid_proposal(prop, &encrxf, &integrxf, &esn) != 0)
			continue;

		spi = 0;

		if ((csa = calloc(1, sizeof(*csa))) == NULL) {
			log_debug("%s: failed to get CHILD SA", __func__);
			goto done;
		}

		csa->csa_saproto = prop->prop_protoid;
		csa->csa_ikesa = sa;
		csa->csa_spi.spi_protoid = prop->prop_protoid;
		csa->csa_esn = esn;
		csa->csa_transport = sa->sa_use_transport_mode;
		sa->sa_used_transport_mode = sa->sa_use_transport_mode;

		if (pfs && group)
			csa->csa_pfsgrpid = group->id;

		/* Set up responder's SPIs */
		if (initiator) {
			csa->csa_dir = IPSP_DIRECTION_OUT;
			csa->csa_local = &sa->sa_local;
			csa->csa_peer = &sa->sa_peer;
			csa->csa_peerspi = prop->prop_localspi.spi;
			csa->csa_spi.spi = prop->prop_peerspi.spi;
			csa->csa_spi.spi_size = prop->prop_peerspi.spi_size;
		} else {
			csa->csa_dir = IPSP_DIRECTION_IN;
			csa->csa_local = &sa->sa_peer;
			csa->csa_peer = &sa->sa_local;

			if ((ret = pfkey_sa_init(env, csa,
			    &spi)) != 0)
				goto done;
			csa->csa_allocated = 1;

			csa->csa_peerspi = prop->prop_peerspi.spi;
			csa->csa_spi.spi = prop->prop_localspi.spi = spi;
			csa->csa_spi.spi_size = 4;
		}

		if (encrxf && (csa->csa_encrkey = ibuf_getdata(keymat,
		    encrxf->xform_keylength / 8)) == NULL) {
			log_debug("%s: failed to get CHILD SA encryption key",
			    __func__);
			goto done;
		}
		if (integrxf && (csa->csa_integrkey = ibuf_getdata(keymat,
		    integrxf->xform_keylength / 8)) == NULL) {
			log_debug("%s: failed to get CHILD SA integrity key",
			    __func__);
			goto done;
		}
		if (encrxf)
			csa->csa_encrid = encrxf->xform_id;
		if (integrxf)
			csa->csa_integrid = integrxf->xform_id;

		if ((csb = calloc(1, sizeof(*csb))) == NULL) {
			log_debug("%s: failed to get CHILD SA", __func__);
			goto done;
		}

		memcpy(csb, csa, sizeof(*csb));

		/* Set up initiator's SPIs */
		csb->csa_spi.spi = csa->csa_peerspi;
		csb->csa_peerspi = csa->csa_spi.spi;
		csb->csa_allocated = csa->csa_allocated ? 0 : 1;
		csb->csa_dir = csa->csa_dir == IPSP_DIRECTION_IN ?
		    IPSP_DIRECTION_OUT : IPSP_DIRECTION_IN;
		csb->csa_local = csa->csa_peer;
		csb->csa_peer = csa->csa_local;

		if (encrxf && (csb->csa_encrkey = ibuf_getdata(keymat,
		    encrxf->xform_keylength / 8)) == NULL) {
			log_debug("%s: failed to get CHILD SA encryption key",
			    __func__);
			goto done;
		}
		if (integrxf && (csb->csa_integrkey = ibuf_getdata(keymat,
		    integrxf->xform_keylength / 8)) == NULL) {
			log_debug("%s: failed to get CHILD SA integrity key",
			    __func__);
			goto done;
		}

		if (ic && prop->prop_protoid == IKEV2_SAPROTO_ESP) {
			/* add IPCOMP SAs */
			if ((csa2 = calloc(1, sizeof(*csa2))) == NULL) {
				log_debug("%s: failed to get CHILD SA", __func__);
				goto done;
			}
			if ((csb2 = calloc(1, sizeof(*csb2))) == NULL) {
				log_debug("%s: failed to get CHILD SA", __func__);
				goto done;
			}

			csa2->csa_saproto = IKEV2_SAPROTO_IPCOMP;
			csa2->csa_ikesa = csa->csa_ikesa;
			csa2->csa_dir = csa->csa_dir;
			csa2->csa_local = csa->csa_local;
			csa2->csa_peer = csa->csa_peer;
			if (initiator) {
				csa2->csa_spi.spi = ic->ic_cpi_out;
				csa2->csa_peerspi = ic->ic_cpi_in;
				csa2->csa_allocated = 0;
				/* make sure IPCOMP CPIs are not reused */
				ic->ic_transform = 0;
				ic->ic_cpi_in = ic->ic_cpi_out = 0;
			} else {
				if ((ret = pfkey_sa_init(env, csa2,
				    &spi)) != 0)
					goto done;
				ic->ic_cpi_in = spi;
				csa2->csa_spi.spi = ic->ic_cpi_in;
				csa2->csa_peerspi = ic->ic_cpi_out;
				csa2->csa_allocated = 1;
			}
			csa2->csa_spi.spi_size = 2;

			memcpy(csb2, csa2, sizeof(*csb2));
			csb2->csa_spi.spi = csa2->csa_peerspi;
			csb2->csa_peerspi = csa2->csa_spi.spi;
			csb2->csa_allocated = csa2->csa_allocated ? 0 : 1;
			csb2->csa_dir = csa2->csa_dir == IPSP_DIRECTION_IN ?
			    IPSP_DIRECTION_OUT : IPSP_DIRECTION_IN;
			csb2->csa_local = csa2->csa_peer;
			csb2->csa_peer = csa2->csa_local;

			/* link IPComp and ESP SAs, switch ESP to transport */
			csa->csa_transport = 1;
			csa->csa_bundled = csa2;
			csa2->csa_bundled = csa;
			csb->csa_transport = 1;
			csb->csa_bundled = csb2;
			csb2->csa_bundled = csb;
			csa2 = NULL;
			csb2 = NULL;

			ic = NULL;
		}

		TAILQ_INSERT_TAIL(&sa->sa_childsas, csa, csa_entry);
		TAILQ_INSERT_TAIL(&sa->sa_childsas, csb, csa_entry);
		ikestat_add(env, ikes_csa_created, 2);

		csa->csa_peersa = csb;
		csb->csa_peersa = csa;
		csa = NULL;
		csb = NULL;
	}

	ret = 0;
 done:
	sa->sa_use_transport_mode = 0;		/* reset state after use */
	ibuf_free(dhsecret);
	ibuf_free(keymat);
	ibuf_free(seed);
	childsa_free(csa);
	childsa_free(csb);
	childsa_free(csa2);
	childsa_free(csb2);

	return (ret);
}

int
ikev2_childsa_enable(struct iked *env, struct iked_sa *sa)
{
	struct iked_childsa	*csa, *ocsa, *ipcomp;
	struct iked_flow	*flow, *oflow;
	int			 peer_changed, reload;
	FILE			*spif, *flowf;
	char			*spibuf = NULL, *flowbuf = NULL;
	char			 prenat_mask[10];
	uint16_t		 encrid = 0, integrid = 0, groupid = 0;
	size_t			 encrlen = 0, integrlen = 0, spisz, flowsz;
	int			 esn = 0;
	int			 ret = -1;

	spif = open_memstream(&spibuf, &spisz);
	if (spif == NULL) {
		log_warn("%s", __func__);
		return (ret);
	}
	flowf = open_memstream(&flowbuf, &flowsz);
	if (flowf == NULL) {
		log_warn("%s", __func__);
		fclose(spif);
		return (ret);
	}

	TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
		if (csa->csa_rekey || csa->csa_loaded)
			continue;

		ipcomp = csa->csa_bundled;
		if (ipcomp && ipcomp->csa_loaded) {
			log_info("%s: IPCOMP SA for CHILD SA spi %s"
			    " already loaded", __func__,
			    print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size));
			continue;
		}

		if (pfkey_sa_add(env, csa, NULL) != 0) {
			log_debug("%s: failed to load CHILD SA spi %s",
			    __func__, print_spi(csa->csa_spi.spi,
			    csa->csa_spi.spi_size));
			goto done;
		}
		if (ipcomp) {
			if (pfkey_sa_add(env, ipcomp, csa) != 0) {
				log_debug("%s: failed to load IPCOMP spi %s",
				    __func__, print_spi(ipcomp->csa_spi.spi,
				    ipcomp->csa_spi.spi_size));
				ipcomp = NULL;
			}
		}

		if ((ocsa = RB_FIND(iked_activesas, &env->sc_activesas, csa))
		    != NULL) {
			log_debug("%s: replaced CHILD SA %p with %p spi %s",
			    __func__, ocsa, csa, print_spi(ocsa->csa_spi.spi,
			    ocsa->csa_spi.spi_size));
			ocsa->csa_loaded = 0;
			ocsa->csa_rekey = 1;	/* prevent re-loading */
			RB_REMOVE(iked_activesas, &env->sc_activesas, ocsa);
		}

		RB_INSERT(iked_activesas, &env->sc_activesas, csa);

		log_debug("%s: loaded CHILD SA spi %s", __func__,
		    print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size));

		/* append SPI to log buffer */
		if (ftello(spif) > 0)
			fputs(", ", spif);
		fputs(print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size), spif);
		if (ipcomp)
			fprintf(spif, "(%s)", print_spi(ipcomp->csa_spi.spi,
			    ipcomp->csa_spi.spi_size));
		if (!encrid) {
			encrid = csa->csa_encrid;
			encrlen = ibuf_length(csa->csa_encrkey);
			switch (encrid) {
			case IKEV2_XFORMENCR_AES_GCM_16:
			case IKEV2_XFORMENCR_AES_GCM_12:
				encrlen -= 4;
				break;
			default:
				if (!csa->csa_integrid)
					break;
				integrid = csa->csa_integrid;
				integrlen = ibuf_length(csa->csa_integrkey);
			}
			groupid = csa->csa_pfsgrpid;
			esn = csa->csa_esn;
		}
	}

	peer_changed = (memcmp(&sa->sa_peer_loaded, &sa->sa_peer,
	    sizeof(sa->sa_peer_loaded)) != 0);

	if (!(sa->sa_policy->pol_flags & IKED_POLICY_ROUTING)) {
		TAILQ_FOREACH(flow, &sa->sa_flows, flow_entry) {
			/* re-load the flow if the peer for the flow has changed */
			reload = 0;
			if (flow->flow_loaded) {
				if (!peer_changed) {
					log_debug("%s: flow already loaded %p",
					    __func__, flow);
					continue;
				}
				RB_REMOVE(iked_flows, &env->sc_activeflows, flow);
				(void)pfkey_flow_delete(env, flow);
				flow->flow_loaded = 0; /* we did RB_REMOVE */
				reload = 1;
			}

			if (pfkey_flow_add(env, flow) != 0) {
				log_debug("%s: failed to load flow", __func__);
				goto done;
			}

			if ((oflow = RB_FIND(iked_flows, &env->sc_activeflows, flow))
			    != NULL) {
				log_debug("%s: replaced old flow %p with %p",
				    __func__, oflow, flow);
				oflow->flow_loaded = 0;
				RB_REMOVE(iked_flows, &env->sc_activeflows, oflow);
			}

			RB_INSERT(iked_flows, &env->sc_activeflows, flow);

			log_debug("%s: %sloaded flow %p", __func__,
			    reload ? "re" : "", flow);

			/* append flow to log buffer */
			if (flow->flow_dir == IPSP_DIRECTION_OUT &&
			    flow->flow_prenat.addr_af != 0)
				snprintf(prenat_mask, sizeof(prenat_mask), "%d",
				    flow->flow_prenat.addr_mask);
			else
				prenat_mask[0] = '\0';
			if (flow->flow_dir == IPSP_DIRECTION_OUT) {
				if (ftello(flowf) > 0)
					fputs(", ", flowf);
				fprintf(flowf, "%s-%s/%d%s%s%s%s%s=%s/%d(%u)%s",
				    print_map(flow->flow_saproto, ikev2_saproto_map),
				    print_addr(&flow->flow_src.addr),
				    flow->flow_src.addr_mask,
				    flow->flow_prenat.addr_af != 0 ? "[": "",
				    flow->flow_prenat.addr_af != 0 ?
				    print_addr(&flow->flow_prenat.addr) : "",
				    flow->flow_prenat.addr_af != 0 ? "/" : "",
				    flow->flow_prenat.addr_af != 0 ? prenat_mask : "",
				    flow->flow_prenat.addr_af != 0 ? "]": "",
				    print_addr(&flow->flow_dst.addr),
				    flow->flow_dst.addr_mask,
				    flow->flow_ipproto,
				    reload ? "-R" : "");
			}
		}
	}

	/* remember the current address for ikev2_update_sa_addresses()  */
	if (peer_changed) {
		memcpy(&sa->sa_peer_loaded, &sa->sa_peer,
		    sizeof(sa->sa_peer_loaded));
		log_debug("%s: remember SA peer %s", __func__,
		    print_addr(&sa->sa_peer_loaded.addr));
	}

	fflush(spif);
	if (ftello(spif) > 0 && !ferror(spif)) {
		log_info("%s: loaded SPIs: %s (enc %s%s%s%s%s%s)",
		    SPI_SA(sa, __func__), spibuf,
		    print_xf(encrid, encrlen, ipsecencxfs),
		    integrid ? " auth " : "",
		    integrid ? print_xf(integrid, integrlen, authxfs) : "",
		    groupid ? " group " : "",
		    groupid ? print_xf(groupid, 0, groupxfs) : "",
		    esn ? " esn" : "");
	}
	fflush(flowf);
	if (ftello(flowf) > 0 && !ferror(flowf)) {
		log_info("%s: loaded flows: %s", SPI_SA(sa, __func__), flowbuf);
	}

	ret = 0;
 done:
	fclose(spif);
	fclose(flowf);
	free(spibuf);
	free(flowbuf);
	return (ret);
}

int
ikev2_childsa_delete(struct iked *env, struct iked_sa *sa, uint8_t saproto,
    uint64_t spi, uint64_t *spiptr, int cleanup)
{
	struct iked_childsa	*csa, *csatmp = NULL, *ipcomp;
	uint64_t		 peerspi = 0;
	int			 found = 0;

	TAILQ_FOREACH_SAFE(csa, &sa->sa_childsas, csa_entry, csatmp) {
		if ((saproto && csa->csa_saproto != saproto) ||
		    (spi && (csa->csa_spi.spi != spi &&
			     csa->csa_peerspi != spi)) ||
		    (cleanup && csa->csa_loaded))
			continue;

		if (csa->csa_loaded)
			RB_REMOVE(iked_activesas, &env->sc_activesas, csa);

		if (pfkey_sa_delete(env, csa) != 0)
			log_info("%s: failed to delete CHILD SA spi %s",
			    SPI_SA(sa, __func__), print_spi(csa->csa_spi.spi,
			    csa->csa_spi.spi_size));
		else
			log_debug("%s: deleted CHILD SA spi %s",
			    SPI_SA(sa, __func__), print_spi(csa->csa_spi.spi,
			    csa->csa_spi.spi_size));
		found++;

		if (spi && csa->csa_spi.spi == spi)
			peerspi = csa->csa_peerspi;

		ipcomp = csa->csa_bundled;
		if (ipcomp) {
			if (ipcomp->csa_loaded) {
				if (pfkey_sa_delete(env, ipcomp) != 0)
					log_info("%s: failed to delete IPCOMP"
					    " SA spi %s", SPI_SA(sa, __func__),
					    print_spi(ipcomp->csa_spi.spi,
					    ipcomp->csa_spi.spi_size));
				else
					log_debug("%s: deleted IPCOMP SA spi %s",
					    SPI_SA(sa, __func__),
					    print_spi(ipcomp->csa_spi.spi,
					    ipcomp->csa_spi.spi_size));
			}
			childsa_free(ipcomp);
		}
		TAILQ_REMOVE(&sa->sa_childsas, csa, csa_entry);
		ikestat_inc(env, ikes_csa_removed);
		childsa_free(csa);
	}

	if (spiptr)
		*spiptr = peerspi;

	return (found ? 0 : -1);
}

int
ikev2_valid_proposal(struct iked_proposal *prop,
    struct iked_transform **exf, struct iked_transform **ixf, int *esn)
{
	struct iked_transform	*xform, *encrxf, *integrxf;
	unsigned int		 i, doesn = 0;

	switch (prop->prop_protoid) {
	case IKEV2_SAPROTO_ESP:
	case IKEV2_SAPROTO_AH:
		break;
	default:
		return (-1);
	}

	encrxf = integrxf = NULL;
	for (i = 0; i < prop->prop_nxforms; i++) {
		xform = prop->prop_xforms + i;
		if (xform->xform_type == IKEV2_XFORMTYPE_ENCR)
			encrxf = xform;
		else if (xform->xform_type == IKEV2_XFORMTYPE_INTEGR)
			integrxf = xform;
		else if (xform->xform_type == IKEV2_XFORMTYPE_ESN &&
		    xform->xform_id == IKEV2_XFORMESN_ESN)
			doesn = 1;
	}

	if (prop->prop_protoid == IKEV2_SAPROTO_IKE) {
		if (encrxf == NULL || integrxf == NULL)
			return (-1);
	} else if (prop->prop_protoid == IKEV2_SAPROTO_AH) {
		if (integrxf == NULL)
			return (-1);
	} else if (prop->prop_protoid == IKEV2_SAPROTO_ESP) {
		if (encrxf == NULL)
			return (-1);
	}

	if (exf)
		*exf = encrxf;
	if (ixf)
		*ixf = integrxf;
	if (esn)
		*esn = doesn;

	return (0);
}

/* return 0 if processed, -1 if busy */
int
ikev2_child_sa_acquire(struct iked *env, struct iked_flow *acquire)
{
	struct iked_flow	*flow;
	struct iked_sa		*sa;
	struct iked_policy	 pol, *p = NULL;

	if (env->sc_passive)
		return (0);

	/* First try to find an active flow with IKE SA */
	flow = RB_FIND(iked_flows, &env->sc_activeflows, acquire);
	if (!flow) {
		/* Otherwise try to find a matching policy */
		bzero(&pol, sizeof(pol));
		pol.pol_af = acquire->flow_peer->addr_af;
		memcpy(&pol.pol_peer, acquire->flow_peer,
		    sizeof(pol.pol_peer));

		RB_INIT(&pol.pol_flows);
		RB_INSERT(iked_flows, &pol.pol_flows, acquire);
		pol.pol_nflows = 1;

		if ((p = policy_test(env, &pol)) == NULL) {
			log_warnx("%s: flow wasn't found", __func__);
			return (0);
		}

		log_debug("%s: found matching policy '%s'", __func__,
		    p->pol_name);

		if (ikev2_init_ike_sa_peer(env, p,
		    &p->pol_peer, NULL) != 0)
			log_warnx("%s: failed to initiate a "
			    "IKE_SA_INIT exchange for policy '%s'",
			    __func__, p->pol_name);
	} else {
		log_debug("%s: found active flow", __func__);

		if ((sa = flow->flow_ikesa) == NULL) {
			log_warnx("%s: flow without SA", __func__);
			return (0);
		}
		if (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF))
			return (-1);	/* busy, retry later */
		if (ikev2_send_create_child_sa(env, sa, NULL,
		    flow->flow_saproto, 0) != 0)
			log_warnx("%s: failed to initiate a "
			    "CREATE_CHILD_SA exchange", SPI_SA(sa, __func__));
	}
	return (0);
}

void
ikev2_disable_rekeying(struct iked *env, struct iked_sa *sa)
{
	struct iked_childsa		*csa;

	TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
		csa->csa_persistent = 1;
		csa->csa_rekey = 0;
	}

	(void)ikev2_childsa_delete(env, sa, 0, 0, NULL, 1);
}

/* return 0 if processed, -1 if busy */
int
ikev2_child_sa_rekey(struct iked *env, struct iked_spi *rekey)
{
	struct iked_childsa		*csa, key;
	struct iked_sa			*sa;

	key.csa_spi = *rekey;
	csa = RB_FIND(iked_activesas, &env->sc_activesas, &key);
	if (!csa)
		return (0);

	if (csa->csa_rekey)	/* See if it's already taken care of */
		return (0);
	if ((sa = csa->csa_ikesa) == NULL) {
		log_warnx("%s: SA %s doesn't have a parent SA", __func__,
		    print_spi(rekey->spi, rekey->spi_size));
		return (0);
	}
	if (!sa_stateok(sa, IKEV2_STATE_ESTABLISHED)) {
		log_warnx("%s: not established, SPI %s", SPI_SA(sa, __func__),
		    print_spi(rekey->spi, rekey->spi_size));
		return (0);
	}
	if (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF)) {
		log_info("%s: busy, retrying, SPI %s", SPI_SA(sa, __func__),
		    print_spi(rekey->spi, rekey->spi_size));
		return (-1);	/* busy, retry later */
	}
	if (sa->sa_tmpfail) {
		log_info("%s: peer busy, retrying, SPI %s", SPI_SA(sa, __func__),
		    print_spi(rekey->spi, rekey->spi_size));
		return (-1);	/* peer is busy, retry later */
	}
	if (csa->csa_allocated)	/* Peer SPI died first, get the local one */
		rekey->spi = csa->csa_peerspi;
	if (ikev2_send_create_child_sa(env, sa, rekey, rekey->spi_protoid, 0))
		log_warnx("%s: failed to initiate a CREATE_CHILD_SA exchange",
		    SPI_SA(sa, __func__));
	return (0);
}

/* return 0 if processed, -1 if busy */
int
ikev2_child_sa_drop(struct iked *env, struct iked_spi *drop)
{
	struct ibuf			*buf = NULL;
	struct iked_childsa		*csa, key;
	struct iked_sa			*sa;
	struct ikev2_delete		*del;
	uint32_t			 spi32;

	key.csa_spi = *drop;
	csa = RB_FIND(iked_activesas, &env->sc_activesas, &key);
	if (!csa || csa->csa_rekey)
		return (0);

	sa = csa->csa_ikesa;
	if (sa && (sa->sa_stateflags & (IKED_REQ_CHILDSA|IKED_REQ_INF))) {
		/* XXXX might loop, should we add a counter? */
		log_debug("%s: parent SA busy", __func__);
		return (-1);	/* busy, retry later */
	}

	RB_REMOVE(iked_activesas, &env->sc_activesas, csa);
	csa->csa_loaded = 0;
	csa->csa_rekey = 1;	/* prevent re-loading */
	if (sa == NULL) {
		log_debug("%s: failed to find a parent SA", __func__);
		return (0);
	}

	if (csa->csa_allocated)
		spi32 = htobe32(csa->csa_spi.spi);
	else
		spi32 = htobe32(csa->csa_peerspi);

	if (ikev2_childsa_delete(env, sa, csa->csa_saproto,
	    csa->csa_peerspi, NULL, 0))
		log_debug("%s: failed to delete CHILD SA %s", __func__,
		    print_spi(csa->csa_peerspi, drop->spi_size));

	/* Send PAYLOAD_DELETE */

	if ((buf = ibuf_static()) == NULL)
		return (0);
	if ((del = ibuf_reserve(buf, sizeof(*del))) == NULL)
		goto done;
	del->del_protoid = drop->spi_protoid;
	del->del_spisize = 4;
	del->del_nspi = htobe16(1);
	if (ibuf_add(buf, &spi32, sizeof(spi32)))
		goto done;

	if (ikev2_send_ike_e(env, sa, buf, IKEV2_PAYLOAD_DELETE,
	    IKEV2_EXCHANGE_INFORMATIONAL, 0) == -1)
		goto done;

	sa->sa_stateflags |= IKED_REQ_INF;

done:
	ibuf_free(buf);
	return (0);
}

int
ikev2_print_static_id(struct iked_static_id *id, char *idstr, size_t idstrlen)
{
	struct iked_id	idp;
	int		ret = -1;

	bzero(&idp, sizeof(idp));
	if ((idp.id_buf = ibuf_new(id->id_data, id->id_length)) == NULL) {
		bzero(idstr, idstrlen);
		return (-1);
	}
	idp.id_type = id->id_type;
	idp.id_offset = id->id_offset;
	if (ikev2_print_id(&idp, idstr, idstrlen) == -1) {
		bzero(idstr, idstrlen);
		goto done;
	}
	ret = 0;
 done:
	ibuf_free(idp.id_buf);
	return (ret);
}

int
ikev2_print_id(struct iked_id *id, char *idstr, size_t idstrlen)
{
	uint8_t				*ptr;
	struct sockaddr_in		 s4 = { 0 };
	struct sockaddr_in6		 s6 = { 0 };
	char				*str;
	ssize_t				 len;
	int				 i;
	const char			*type;

	bzero(idstr, idstrlen);

	if (id->id_buf == NULL)
		return (-1);

	len = ibuf_size(id->id_buf);
	ptr = ibuf_data(id->id_buf);

	if (len <= id->id_offset)
		return (-1);

	len -= id->id_offset;
	ptr += id->id_offset;

	type = print_map(id->id_type, ikev2_id_map);

	if (strlcpy(idstr, type, idstrlen) >= idstrlen ||
	    strlcat(idstr, "/", idstrlen) >= idstrlen)
		return (-1);

	switch (id->id_type) {
	case IKEV2_ID_IPV4:
		s4.sin_family = AF_INET;
		s4.sin_len = sizeof(s4);
		memcpy(&s4.sin_addr.s_addr, ptr, len);

		if (strlcat(idstr, print_addr(&s4), idstrlen) >= idstrlen)
			return (-1);
		break;
	case IKEV2_ID_FQDN:
	case IKEV2_ID_UFQDN:
		if ((str = get_string(ptr, len)) == NULL)
			return (-1);

		if (strlcat(idstr, str, idstrlen) >= idstrlen) {
			free(str);
			return (-1);
		}
		free(str);
		break;
	case IKEV2_ID_IPV6:
		s6.sin6_family = AF_INET6;
		s6.sin6_len = sizeof(s6);
		memcpy(&s6.sin6_addr, ptr, len);

		if (strlcat(idstr, print_addr(&s6), idstrlen) >= idstrlen)
			return (-1);
		break;
	case IKEV2_ID_ASN1_DN:
		if ((str = ca_asn1_name(ptr, len)) == NULL)
			return (-1);
		if (strlcat(idstr, str, idstrlen) >= idstrlen) {
			OPENSSL_free(str);
			return (-1);
		}
		OPENSSL_free(str);
		break;
	default:
		/* XXX test */
		for (i = 0; i < len; i++) {
			char buf[3];
			snprintf(buf, sizeof(buf), "%02x", ptr[i]);
			if (strlcat(idstr, buf, idstrlen) >= idstrlen)
				break;
		}
		break;
	}

	return (0);
}

/*
 * If we have an IKEV2_CP_REQUEST for IKEV2_CFG_INTERNAL_IP4_ADDRESS and
 * if a network(pool) is configured, then select an address from that pool
 * and remember it in the sa_addrpool attribute.
 */
int
ikev2_cp_setaddr(struct iked *env, struct iked_sa *sa, sa_family_t family)
{
	struct iked_policy	*pol = sa->sa_policy;
	struct iked_cfg		*ikecfg = NULL;
	const char		*errstr = NULL;
	int			 ret, pass, passes;
	size_t			 i;
	struct sockaddr_in	*in4;

	switch (family) {
	case AF_INET:
		if (sa->sa_addrpool)
			return (0);
		break;
	case AF_INET6:
		if (sa->sa_addrpool6)
			return (0);
		break;
	default:
		return (-1);
	}
	if (pol->pol_ncfg == 0)
		return (0);
	/* default if no pool configured */
	ret = 0;

	/* handle the special addresses from RADIUS */
	if (sa->sa_rad_addr != NULL) {
		in4 = (struct sockaddr_in *)&sa->sa_rad_addr->addr;
		/* 0xFFFFFFFF allows the user to select an address (RFC 2865) */
		if (in4->sin_addr.s_addr == htonl(0xFFFFFFFF))
			;/* this is  default behavior if the user selects */
		/* 0xFFFFFFFE indicated the NAS should select (RFC 2865) */
		else if (in4->sin_addr.s_addr == htonl(0xFFFFFFFE)) {
			free(sa->sa_cp_addr);
			sa->sa_cp_addr = NULL;
		}
	}

	/* two passes if client requests from specific pool */
	passes = (sa->sa_cp_addr != NULL || sa->sa_cp_addr6 != NULL ||
	    sa->sa_rad_addr != NULL || sa->sa_rad_addr6 != NULL) ? 2 : 1;
	for (pass = 0; pass < passes; pass++) {
		/* loop over all address pool configs (addr_net) */
		for (i = 0; i < pol->pol_ncfg; i++) {
			ikecfg = &pol->pol_cfg[i];
			if (!ikecfg->cfg.address.addr_net)
				continue;
			if ((family == AF_INET && ikecfg->cfg_type ==
			    IKEV2_CFG_INTERNAL_IP4_ADDRESS) ||
			    (family == AF_INET6 && ikecfg->cfg_type ==
			    IKEV2_CFG_INTERNAL_IP6_ADDRESS)) {
				if ((ret = ikev2_cp_setaddr_pool(env, sa,
				    ikecfg, &errstr, family)) == 0)
					return (0);
			}
		}
		if (family == AF_INET) {
			free(sa->sa_cp_addr);
			sa->sa_cp_addr = NULL;
			free(sa->sa_rad_addr);
			sa->sa_rad_addr = NULL;
		} else {
			free(sa->sa_cp_addr6);
			sa->sa_cp_addr6 = NULL;
			free(sa->sa_rad_addr6);
			sa->sa_rad_addr6 = NULL;
		}
	}

	if (errstr != NULL)
		log_warnx("%s: %s", SPI_SA(sa, __func__), errstr);
	return (ret);
}

int
ikev2_cp_setaddr_pool(struct iked *env, struct iked_sa *sa,
    struct iked_cfg *ikecfg, const char **errstr, sa_family_t family)
{
	struct sockaddr_in	*in4 = NULL, *cfg4 = NULL;
	struct sockaddr_in6	*in6 = NULL, *cfg6 = NULL;
	struct iked_sa		 key;
	struct iked_sa		*osa;
	char			 idstr[IKED_ID_SIZE];
	struct iked_addr	 addr;
	uint32_t		 mask, host, lower, upper, start, nhost;
	int			 requested = 0, rad_requested = 0;

	/*
	 * failure: pool configured, but not requested.
	 * If we continue, we might end up with flows where 0.0.0.0 is NOT
	 * replaced with an address from the pool with ikev2_cp_fixaddr().
	 */
	if (sa->sa_cp != IKEV2_CP_REQUEST) {
		log_debug("%s: pool configured, but IKEV2_CP_REQUEST missing",
		    __func__);
		return (-1);
	}
	bzero(&addr, sizeof(addr));
	addr.addr_af = family;

	/* check if old IKESA for same DSTID already exists and transfer IPs */
	if (env->sc_stickyaddress &&
	    (osa = sa_dstid_lookup(env, sa)) != NULL &&
	    ((family == AF_INET && osa->sa_addrpool) ||
	    (family == AF_INET6 && osa->sa_addrpool6))) {
		/* we have to transfer both, even if we just need one */
		if (osa->sa_addrpool) {
			if (RB_REMOVE(iked_addrpool, &env->sc_addrpool, osa)
			    != osa) {
				log_info("%s: addrpool error",
				    SPI_SA(osa, __func__));
				return (-1);
			}
		}
		if (osa->sa_addrpool6) {
			if (RB_REMOVE(iked_addrpool6, &env->sc_addrpool6, osa)
			    != osa) {
				log_info("%s: addrpool6 error",
				    SPI_SA(osa, __func__));
				return (-1);
			}
		}
		sa_dstid_remove(env, osa);
		sa->sa_addrpool = osa->sa_addrpool;
		osa->sa_addrpool = NULL;
		sa->sa_addrpool6 = osa->sa_addrpool6;
		osa->sa_addrpool6 = NULL;
		if (osa->sa_state < IKEV2_STATE_CLOSING) {
			if (osa->sa_state == IKEV2_STATE_ESTABLISHED)
				ikev2_disable_timer(env, osa);
			ikev2_ike_sa_setreason(osa,
			    "address re-use (identical dstid)");
			ikev2_ikesa_delete(env, osa, 1);
			timer_add(env, &osa->sa_timer,
			    3 * IKED_RETRANSMIT_TIMEOUT);
		}
		if (sa->sa_addrpool) {
			RB_INSERT(iked_addrpool, &env->sc_addrpool, sa);
			log_info(
			    "%s: giving up assigned address %s to IKESA %s",
			    SPI_SA(osa, __func__),
			    print_addr(&sa->sa_addrpool->addr),
			    print_spi(sa->sa_hdr.sh_ispi, 8));
		}
		if (sa->sa_addrpool6) {
			RB_INSERT(iked_addrpool6, &env->sc_addrpool6, sa);
			log_info(
			    "%s: giving up assigned v6 address %s to IKESA %s",
			    SPI_SA(osa, __func__),
			    print_addr(&sa->sa_addrpool6->addr),
			    print_spi(sa->sa_hdr.sh_ispi, 8));
		}
		if (family == AF_INET && sa->sa_addrpool != NULL)
			memcpy(&addr, sa->sa_addrpool, sizeof(addr));
		else if (family == AF_INET6 && sa->sa_addrpool6 != NULL)
			memcpy(&addr, sa->sa_addrpool6, sizeof(addr));
		goto done;
	}
	switch (addr.addr_af) {
	case AF_INET:
		cfg4 = (struct sockaddr_in *)&ikecfg->cfg.address.addr;
		mask = prefixlen2mask(ikecfg->cfg.address.addr_mask);
		if (sa->sa_cp_addr != NULL || sa->sa_rad_addr != NULL) {
			if (sa->sa_rad_addr != NULL) {
				rad_requested = 1;
				memcpy(&addr, sa->sa_rad_addr, sizeof(addr));
			} else {
				requested = 1;
				memcpy(&addr, sa->sa_cp_addr, sizeof(addr));
			}
			key.sa_addrpool = &addr;
			in4 = (struct sockaddr_in *)&addr.addr;
			if ((in4->sin_addr.s_addr & mask) !=
			    (cfg4->sin_addr.s_addr & mask)) {
				*errstr = "requested addr out of range";
				return (-1);
			}
			if (RB_FIND(iked_addrpool, &env->sc_addrpool,
			    &key)) {
				*errstr = "requested addr in use";
				return (-1);
			}
			if (sa->sa_rad_addr != NULL) {
				sa->sa_addrpool = sa->sa_rad_addr;
				sa->sa_rad_addr = NULL;
			} else {
				sa->sa_addrpool = sa->sa_cp_addr;
				sa->sa_cp_addr = NULL;
			}
			free(sa->sa_cp_addr);
			free(sa->sa_rad_addr);
			RB_INSERT(iked_addrpool, &env->sc_addrpool, sa);
			goto done;
		}
		in4 = (struct sockaddr_in *)&addr.addr;
		in4->sin_family = AF_INET;
		in4->sin_len = sizeof(*in4);
		lower = ntohl(cfg4->sin_addr.s_addr & ~mask);
		key.sa_addrpool = &addr;
		break;
	case AF_INET6:
		cfg6 = (struct sockaddr_in6 *)&ikecfg->cfg.address.addr;
		in6 = (struct sockaddr_in6 *)&addr.addr;
		if (sa->sa_cp_addr6 != NULL || sa->sa_rad_addr6 != NULL) {
			/* XXX not yet supported */
		}
		in6->sin6_family = AF_INET6;
		in6->sin6_len = sizeof(*in6);
		/* truncate prefixlen to get a 32-bit space */
		mask = (ikecfg->cfg.address.addr_mask >= 96)
		    ? prefixlen2mask(ikecfg->cfg.address.addr_mask - 96)
		    : prefixlen2mask(0);
		memcpy(&lower, &cfg6->sin6_addr.s6_addr[12], sizeof(uint32_t));
		lower = ntohl(lower & ~mask);
		key.sa_addrpool6 = &addr;
		break;
	default:
		return (-1);
	}

	/* Note that start, upper and host are in HOST byte order */
	upper = ntohl(~mask);
	/* skip .0 address if possible */
	if (lower < upper && lower == 0)
		lower = 1;
	if (upper < lower)
		upper = lower;
	/* Randomly select start from [lower, upper-1] */
	start = arc4random_uniform(upper - lower) + lower;

	for (host = start;;) {
		log_debug("%s: mask %x start %x lower %x host %x upper %x",
		    __func__, mask, start, lower, host, upper);
		switch (addr.addr_af) {
		case AF_INET:
			in4->sin_addr.s_addr =
			    (cfg4->sin_addr.s_addr & mask) | htonl(host);
			break;
		case AF_INET6:
			memcpy(in6, cfg6, sizeof(*in6));
			memcpy(&nhost, &cfg6->sin6_addr.s6_addr[12],
			    sizeof(uint32_t));
			nhost = (nhost & mask) | htonl(host);
			memcpy(&in6->sin6_addr.s6_addr[12], &nhost,
			    sizeof(uint32_t));
			break;
		default:
			return (-1);
		}
		if ((addr.addr_af == AF_INET &&
		    !RB_FIND(iked_addrpool, &env->sc_addrpool, &key)) ||
		    (addr.addr_af == AF_INET6 &&
		    !RB_FIND(iked_addrpool6, &env->sc_addrpool6, &key)))
			break;
		/* try next address */
		host++;
		/* but skip broadcast and network address */
		if (host >= upper || host < lower)
			host = lower;
		if (host == start) {
			*errstr = "address pool exhausted";
			return (-1);		/* exhausted */
		}
	}

	addr.addr_mask = ikecfg->cfg.address.addr_mask;
	switch (addr.addr_af) {
	case AF_INET:
		if (!key.sa_addrpool)
			return (-1);			/* cannot happen? */
		if ((sa->sa_addrpool = calloc(1, sizeof(addr))) == NULL)
			return (-1);
		memcpy(sa->sa_addrpool, &addr, sizeof(addr));
		RB_INSERT(iked_addrpool, &env->sc_addrpool, sa);
		break;
	case AF_INET6:
		if (!key.sa_addrpool6)
			return (-1);			/* cannot happen? */
		if ((sa->sa_addrpool6 = calloc(1, sizeof(addr))) == NULL)
			return (-1);
		memcpy(sa->sa_addrpool6, &addr, sizeof(addr));
		RB_INSERT(iked_addrpool6, &env->sc_addrpool6, sa);
		break;
	default:
		return (-1);
	}
 done:
	if (ikev2_print_id(IKESA_DSTID(sa), idstr, sizeof(idstr)) == -1)
		bzero(idstr, sizeof(idstr));
	log_info("%sassigned address %s to %s%s%s", SPI_SA(sa, NULL),
	    print_addr(&addr.addr),
	    idstr, requested ? " (requested by peer)" : "",
	    rad_requested? "(requested by RADIUS)" : "");
	return (0);
}

int
ikev2_cp_request_configured(struct iked_sa *sa)
{
	struct iked_policy	*pol = sa->sa_policy;
	struct iked_cfg		*ikecfg;
	unsigned int		 i;

	for (i = 0; i < pol->pol_ncfg; i++) {
		ikecfg = &pol->pol_cfg[i];
		if (ikecfg->cfg_action == IKEV2_CP_REQUEST) {
			log_debug("%s: yes", SPI_SA(sa, __func__));
			return 1;
		}
	}
	log_debug("%s: no", SPI_SA(sa, __func__));
	return 0;
}

/*
 * if 'addr' is 'UNSPECIFIED' replace it with sa_addrpool from
 * the ip-pool or the sa_cp_addr received from peer and store the
 * result in 'patched'.
 */
int
ikev2_cp_fixaddr(struct iked_sa *sa, struct iked_addr *addr,
    struct iked_addr *patched)
{
	struct sockaddr_in	*in4;
	struct sockaddr_in6	*in6;
	struct iked_addr	*naddr;

	if (addr->addr_net)
		return (-2);
	if (sa->sa_cp == 0)
		return (-1);
	switch (addr->addr_af) {
	case AF_INET:
		in4 = (struct sockaddr_in *)&addr->addr;
		if (in4->sin_addr.s_addr)
			return (-2);
		naddr = (sa->sa_cp == IKEV2_CP_REQUEST) ?
		    sa->sa_addrpool : sa->sa_cp_addr;
		if (naddr == NULL)
			return (-1);
		memcpy(patched, naddr, sizeof(*patched));
		patched->addr_net = 0;
		patched->addr_mask = 32;
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&addr->addr;
		if (!IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr))
			return (-2);
		naddr = (sa->sa_cp == IKEV2_CP_REQUEST) ?
		    sa->sa_addrpool6 : sa->sa_cp_addr6;
		if (naddr == NULL)
			return (-1);
		memcpy(patched, naddr, sizeof(*patched));
		patched->addr_net = 0;
		patched->addr_mask = 128;
		break;
	}
	return (0);
}

/* replace unspecified address in flow with requested address */
int
ikev2_cp_fixflow(struct iked_sa *sa, struct iked_flow *flow,
    struct iked_flow *patched)
{
	switch (sa->sa_cp) {
	case IKEV2_CP_REQUEST:
		if (patched->flow_dir == IPSP_DIRECTION_IN)
			return (ikev2_cp_fixaddr(sa, &flow->flow_dst,
			    &patched->flow_src));
		else
			return (ikev2_cp_fixaddr(sa, &flow->flow_dst,
			    &patched->flow_dst));
	case IKEV2_CP_REPLY:
		if (patched->flow_dir == IPSP_DIRECTION_IN)
			return (ikev2_cp_fixaddr(sa, &flow->flow_src,
			    &patched->flow_dst));
		else
			return (ikev2_cp_fixaddr(sa, &flow->flow_src,
			    &patched->flow_src));
	default:
		return (0);
	}
}

int
ikev2_update_sa_addresses(struct iked *env, struct iked_sa *sa)
{
	struct iked_childsa		*csa, *ipcomp;
	struct iked_flow		*flow, *oflow;
	struct iked_message		*msg;
	struct iked_msg_retransmit	*mr;

	if (!sa_stateok(sa, IKEV2_STATE_ESTABLISHED))
		return -1;

	log_info("%s: old %s new %s", SPI_SA(sa, __func__),
	    print_addr(&sa->sa_peer_loaded.addr),
	    print_addr(&sa->sa_peer.addr));

	TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
		if (!csa->csa_loaded)
			continue;
		if (pfkey_sa_update_addresses(env, csa) != 0)
			log_debug("%s: failed to update sa", __func__);
		if ((ipcomp = csa->csa_bundled) != NULL &&
		    ipcomp->csa_loaded)
			if (pfkey_sa_update_addresses(env, ipcomp)
			    != 0)
				log_debug("%s: failed to update sa", __func__);
	}

	/* delete and re-add flows */
	TAILQ_FOREACH(flow, &sa->sa_flows, flow_entry) {
		if (flow->flow_loaded) {
			RB_REMOVE(iked_flows, &env->sc_activeflows, flow);
			(void)pfkey_flow_delete(env, flow);
			flow->flow_loaded = 0;
		}
		if (pfkey_flow_add(env, flow) != 0)
			log_debug("%s: failed to add flow %p", __func__, flow);
		if (!flow->flow_loaded)
			continue;
		if ((oflow = RB_FIND(iked_flows, &env->sc_activeflows, flow))
		    != NULL) {
			log_debug("%s: replaced old flow %p with %p",
			    __func__, oflow, flow);
			oflow->flow_loaded = 0;
			RB_REMOVE(iked_flows, &env->sc_activeflows, oflow);
		}
		RB_INSERT(iked_flows, &env->sc_activeflows, flow);
	}

	/* update pending requests and responses */
	TAILQ_FOREACH(mr, &sa->sa_requests, mrt_entry) {
		TAILQ_FOREACH(msg, &mr->mrt_frags, msg_entry) {
			msg->msg_local = sa->sa_local.addr;
			msg->msg_locallen = sa->sa_local.addr.ss_len;
			msg->msg_peer = sa->sa_peer.addr;
			msg->msg_peerlen = sa->sa_peer.addr.ss_len;
		}
	}
	TAILQ_FOREACH(mr, &sa->sa_responses, mrt_entry) {
		TAILQ_FOREACH(msg, &mr->mrt_frags, msg_entry) {
			msg->msg_local = sa->sa_local.addr;
			msg->msg_locallen = sa->sa_local.addr.ss_len;
			msg->msg_peer = sa->sa_peer.addr;
			msg->msg_peerlen = sa->sa_peer.addr.ss_len;
		}
	}

	/* Update sa_peer_loaded, to match in-kernel information */
	memcpy(&sa->sa_peer_loaded, &sa->sa_peer, sizeof(sa->sa_peer_loaded));

	return 0;
}

void
ikev2_info_sa(struct iked *env, struct imsg *imsg, int dolog, const char *msg,
    struct iked_sa *sa)
{
	char		 idstr[IKED_ID_SIZE];
	char		*buf;
	int		 buflen;

	if (ikev2_print_id(IKESA_DSTID(sa), idstr, sizeof(idstr)) == -1)
		bzero(idstr, sizeof(idstr));

	buflen = asprintf(&buf,
	    "%s: %p rspi %s ispi %s %s->%s<%s>[%s] %s %c%s%s nexti %p pol %p\n",
	    msg, sa,
	    print_spi(sa->sa_hdr.sh_rspi, 8),
	    print_spi(sa->sa_hdr.sh_ispi, 8),
	    print_addr(&sa->sa_local.addr),
	    print_addr(&sa->sa_peer.addr),
	    idstr,
	    sa->sa_addrpool ? print_addr(&sa->sa_addrpool->addr) : "",
	    print_map(sa->sa_state, ikev2_state_map),
	    sa->sa_hdr.sh_initiator ? 'i' : 'r',
	    sa->sa_natt ? " natt" : "",
	    sa->sa_udpencap ? " udpencap" : "",
	    sa->sa_nexti, sa->sa_policy);

	if (buflen == -1 || buf == NULL)
		return;

	if (dolog) {
		if (buflen > 1)
			buf[buflen - 1] = '\0';
		log_debug("%s", buf);
	} else
		proc_compose_imsg(&env->sc_ps, PROC_CONTROL, -1,
		    IMSG_CTL_SHOW_SA, imsg->hdr.peerid, -1,
		    buf, buflen + 1);
	free(buf);
}

void
ikev2_info_csa(struct iked *env, struct imsg *imsg, int dolog, const char *msg,
    struct iked_childsa *csa)
{
	char		*buf;
	int		 buflen;

	buflen = asprintf(&buf,
	    "%s: %p %s %s %s %s -> %s (%s%s%s%s) B=%p P=%p @%p\n", msg, csa,
	    print_map(csa->csa_saproto, ikev2_saproto_map),
	    print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size),
	    csa->csa_dir == IPSP_DIRECTION_IN ? "in" : "out",
	    print_addr(&csa->csa_local->addr),
	    print_addr(&csa->csa_peer->addr),
	    csa->csa_loaded ? "L" : "",
	    csa->csa_rekey ? "R" : "",
	    csa->csa_allocated ? "A" : "",
	    csa->csa_persistent ? "P" : "",
	    csa->csa_bundled,
	    csa->csa_peersa,
	    csa->csa_ikesa);

	if (buflen == -1 || buf == NULL)
		return;

	if (dolog) {
		if (buflen > 1)
			buf[buflen - 1] = '\0';
		log_debug("%s", buf);
	} else
		proc_compose_imsg(&env->sc_ps, PROC_CONTROL, -1,
		    IMSG_CTL_SHOW_SA, imsg->hdr.peerid, -1,
		    buf, buflen + 1);
	free(buf);
}

void
ikev2_info_flow(struct iked *env, struct imsg *imsg, int dolog, const char *msg,
    struct iked_flow *flow)
{
	char		prenat_mask[10];
	char		*buf;
	int		buflen;

	if (flow->flow_prenat.addr_af != 0)
		snprintf(prenat_mask, sizeof(prenat_mask), "%d",
		    flow->flow_prenat.addr_mask);
	else
		prenat_mask[0] = '\0';

	buflen = asprintf(&buf,
	    "%s: %p %s %s %s/%d -> %s/%d %s%s%s%s%s[%u]@%d (%s) @%p\n", msg, flow,
	    print_map(flow->flow_saproto, ikev2_saproto_map),
	    flow->flow_dir == IPSP_DIRECTION_IN ? "in" : "out",
	    print_addr(&flow->flow_src.addr),
	    flow->flow_src.addr_mask,
	    print_addr(&flow->flow_dst.addr),
	    flow->flow_dst.addr_mask,
	    flow->flow_prenat.addr_af != 0 ? "[": "",
	    flow->flow_prenat.addr_af != 0 ?
	    print_addr(&flow->flow_prenat.addr) : "",
	    flow->flow_prenat.addr_af != 0 ? "/" : "",
	    flow->flow_prenat.addr_af != 0 ? prenat_mask : "",
	    flow->flow_prenat.addr_af != 0 ? "] ": "",
	    flow->flow_ipproto,
	    flow->flow_rdomain,
	    flow->flow_loaded ? "L" : "",
	    flow->flow_ikesa);

	if (buflen == -1 || buf == NULL)
		return;

	if (dolog) {
		if (buflen > 1)
			buf[buflen - 1] = '\0';
		log_debug("%s", buf);
	} else
		proc_compose_imsg(&env->sc_ps, PROC_CONTROL, -1,
		    IMSG_CTL_SHOW_SA, imsg->hdr.peerid, -1,
		    buf, buflen + 1);
	free(buf);
}

void
ikev2_info(struct iked *env, struct imsg *imsg, int dolog)
{
	struct iked_sa			*sa;
	struct iked_childsa		*csa, *ipcomp;
	struct iked_flow		*flow;

	log_debug("%s: called", __func__);

	RB_FOREACH(sa, iked_sas, &env->sc_sas) {
		ikev2_info_sa(env, imsg, dolog, "iked_sas", sa);
		TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
			ikev2_info_csa(env, imsg, dolog, "  sa_childsas", csa);
			if ((ipcomp = csa->csa_bundled) != NULL)
				ikev2_info_csa(env, imsg, dolog, "             ",
				    ipcomp);
		}
		TAILQ_FOREACH(flow, &sa->sa_flows, flow_entry) {
			ikev2_info_flow(env, imsg, dolog, "  sa_flows", flow);
		}
	}
	RB_FOREACH(csa, iked_activesas, &env->sc_activesas) {
		ikev2_info_csa(env, imsg, dolog, "iked_activesas", csa);
		if ((ipcomp = csa->csa_bundled) != NULL)
			ikev2_info_csa(env, imsg, dolog, "              ", ipcomp);
	}
	RB_FOREACH(flow, iked_flows, &env->sc_activeflows) {
		ikev2_info_flow(env, imsg, dolog, "iked_flows", flow);
	}
	RB_FOREACH(sa, iked_dstid_sas, &env->sc_dstid_sas) {
		ikev2_info_sa(env, imsg, dolog, "iked_dstid_sas", sa);
	}
	if (dolog)
		return;
	/* Send empty reply to indicate end of information. */
	proc_compose_imsg(&env->sc_ps, PROC_CONTROL, -1, IMSG_CTL_SHOW_SA,
	    imsg->hdr.peerid, -1, NULL, 0);
}

const char *
ikev2_ikesa_info(uint64_t spi, const char *msg)
{
	static char buf[1024];
	const char *spistr;

	spistr = print_spi(spi, 8);
	if (msg)
		snprintf(buf, sizeof(buf), "spi=%s: %s", spistr, msg);
	else
		snprintf(buf, sizeof(buf), "spi=%s: ", spistr);
	return buf;
}

void
ikev2_log_established(struct iked_sa *sa)
{
	char dstid[IKED_ID_SIZE], srcid[IKED_ID_SIZE];

	clock_gettime(CLOCK_MONOTONIC, &sa->sa_starttime);

	if (ikev2_print_id(IKESA_DSTID(sa), dstid, sizeof(dstid)) == -1)
		bzero(dstid, sizeof(dstid));
	if (ikev2_print_id(IKESA_SRCID(sa), srcid, sizeof(srcid)) == -1)
		bzero(srcid, sizeof(srcid));
	log_info(
	    "%sestablished peer %s[%s] local %s[%s]%s%s%s%s policy '%s'%s"
	    " (enc %s%s%s group %s prf %s)", SPI_SA(sa, NULL),
	    print_addr(&sa->sa_peer.addr), dstid,
	    print_addr(&sa->sa_local.addr), srcid,
	    sa->sa_addrpool ? " assigned " : "",
	    sa->sa_addrpool ? print_addr(&sa->sa_addrpool->addr) : "",
	    sa->sa_addrpool6 ? " assigned " : "",
	    sa->sa_addrpool6 ? print_addr(&sa->sa_addrpool6->addr) : "",
	    sa->sa_policy ? sa->sa_policy->pol_name : "",
	    sa->sa_hdr.sh_initiator ? " as initiator" : " as responder",
	    print_xf(sa->sa_encr->encr_id, cipher_keylength(sa->sa_encr) -
	    sa->sa_encr->encr_saltlength, ikeencxfs),
	    sa->sa_encr->encr_authid ? "" : " auth ",
	    sa->sa_encr->encr_authid ? "" : print_xf(sa->sa_integr->hash_id,
	    hash_keylength(sa->sa_integr), authxfs),
	    print_xf(sa->sa_dhgroup->id, 0, groupxfs),
	    print_xf(sa->sa_prf->hash_id, hash_keylength(sa->sa_prf), prfxfs));
}

void
ikev2_log_cert_info(const char *msg, struct iked_id *certid)
{
	X509		*cert = NULL;
	BIO		*rawcert = NULL;

	if (certid->id_type != IKEV2_CERT_X509_CERT ||
	    certid->id_buf == NULL)
		return;
	if ((rawcert = BIO_new_mem_buf(ibuf_data(certid->id_buf),
	    ibuf_size(certid->id_buf))) == NULL ||
	    (cert = d2i_X509_bio(rawcert, NULL)) == NULL)
		goto out;
	ca_cert_info(msg, cert);
out:
	if (cert)
		X509_free(cert);
	if (rawcert)
		BIO_free(rawcert);
}

void
ikev2_log_proposal(struct iked_sa *sa, struct iked_proposals *proposals)
{
	struct iked_proposal	*prop;
	struct iked_transform	*xform;
	unsigned int		 i;
	char			 lenstr[20];

	TAILQ_FOREACH(prop, proposals, prop_entry) {
		for (i = 0; i < prop->prop_nxforms; i++) {
			xform = &prop->prop_xforms[i];
			if (xform->xform_keylength)
				snprintf(lenstr, sizeof(lenstr), "-%u",
				    xform->xform_keylength);
			else
				lenstr[0] = '\0';
			log_info("%s: %s #%u %s=%s%s",
			    sa ? SPI_SA(sa, __func__) : __func__,
			    print_map(prop->prop_protoid, ikev2_saproto_map),
			    prop->prop_id,
			    print_map(xform->xform_type, ikev2_xformtype_map),
			    xform->xform_map ?
			    print_map(xform->xform_id, xform->xform_map)
			    : "UNKNOWN",
			    lenstr);
		}
	}
}
