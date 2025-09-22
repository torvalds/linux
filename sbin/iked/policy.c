/*	$OpenBSD: policy.c,v 1.99 2024/07/13 12:22:46 yasuoka Exp $	*/

/*
 * Copyright (c) 2020-2021 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2001 Daniel Hartmeier
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/tree.h>

#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <event.h>

#include "iked.h"
#include "ikev2.h"

static __inline int
	 sa_cmp(struct iked_sa *, struct iked_sa *);
static __inline int
	 sa_dstid_cmp(struct iked_sa *, struct iked_sa *);
static __inline int
	 user_cmp(struct iked_user *, struct iked_user *);
static __inline int
	 childsa_cmp(struct iked_childsa *, struct iked_childsa *);
static __inline int
	 flow_cmp(struct iked_flow *, struct iked_flow *);
static __inline int
	 addr_cmp(struct iked_addr *, struct iked_addr *, int);
static __inline int
	 ts_insert_unique(struct iked_addr *, struct iked_tss *, int);

static int	policy_test_flows(struct iked_policy *, struct iked_policy *);
static int	proposals_match(struct iked_proposal *, struct iked_proposal *,
		    struct iked_transform **, int, int);

void
policy_init(struct iked *env)
{
	TAILQ_INIT(&env->sc_policies);
	TAILQ_INIT(&env->sc_ocsp);
	TAILQ_INIT(&env->sc_radauthservers);
	TAILQ_INIT(&env->sc_radacctservers);
	TAILQ_INIT(&env->sc_radcfgmaps);
	TAILQ_INIT(&env->sc_raddaes);
	TAILQ_INIT(&env->sc_raddaeclients);
	RB_INIT(&env->sc_users);
	RB_INIT(&env->sc_sas);
	RB_INIT(&env->sc_dstid_sas);
	RB_INIT(&env->sc_activesas);
	RB_INIT(&env->sc_activeflows);
}

/*
 * Lookup an iked policy matching the IKE_AUTH message msg
 * and store a pointer to the found policy in msg.  If no policy
 * matches a pointer to the default policy is stored in msg.
 * If 'proposals' is not NULL policy_lookup only returns policies
 * compatible with 'proposals'.
 *
 * Returns 0 on success and -1 if no matching policy was
 * found and no default exists.
 */
int
policy_lookup(struct iked *env, struct iked_message *msg,
    struct iked_proposals *proposals, struct iked_flows *flows,
    int nflows)
{
	struct iked_policy	 pol;
	char			*s, idstr[IKED_ID_SIZE];


	if (msg->msg_sa != NULL && msg->msg_sa->sa_policy != NULL) {
		/* Existing SA with policy */
		msg->msg_policy = msg->msg_sa->sa_policy;
		return (0);
	}

	bzero(&pol, sizeof(pol));
	if (proposals != NULL)
		pol.pol_proposals = *proposals;
	pol.pol_af = msg->msg_peer.ss_family;
	if (flows)
		pol.pol_flows = *flows;
	pol.pol_nflows = nflows;
	if (msg->msg_flags & IKED_MSG_FLAGS_USE_TRANSPORT)
		pol.pol_flags |= IKED_POLICY_TRANSPORT;
	memcpy(&pol.pol_peer.addr, &msg->msg_peer, sizeof(msg->msg_peer));
	memcpy(&pol.pol_local.addr, &msg->msg_local, sizeof(msg->msg_local));
	if (msg->msg_peerid.id_type &&
	    ikev2_print_id(&msg->msg_peerid, idstr, IKED_ID_SIZE) == 0 &&
	    (s = strchr(idstr, '/')) != NULL) {
		pol.pol_peerid.id_type = msg->msg_peerid.id_type;
		pol.pol_peerid.id_length = strlen(s+1);
		strlcpy(pol.pol_peerid.id_data, s+1,
		    sizeof(pol.pol_peerid.id_data));
		log_debug("%s: peerid '%s'", __func__, s+1);
	}
	if (msg->msg_localid.id_type &&
	    ikev2_print_id(&msg->msg_localid, idstr, IKED_ID_SIZE) == 0 &&
	    (s = strchr(idstr, '/')) != NULL) {
		pol.pol_localid.id_type = msg->msg_localid.id_type;
		pol.pol_localid.id_length = strlen(s+1);
		strlcpy(pol.pol_localid.id_data, s+1,
		    sizeof(pol.pol_localid.id_data));
		log_debug("%s: localid '%s'", __func__, s+1);
	}

	/* Try to find a matching policy for this message */
	if ((msg->msg_policy = policy_test(env, &pol)) != NULL) {
		log_debug("%s: setting policy '%s'", __func__,
		    msg->msg_policy->pol_name);
		return (0);
	}

	/* No matching policy found, try the default */
	if ((msg->msg_policy = env->sc_defaultcon) != NULL)
		return (0);

	/* No policy found */
	return (-1);
}

/*
 * Lookup an iked policy matching the SA sa and store a pointer
 * to the found policy in SA.
 *
 * Returns 0 on success and -1 if no matching policy was
 * found
 */
int
policy_lookup_sa(struct iked *env, struct iked_sa *sa)
{
	struct iked_policy	 pol, *pol_found;
	struct iked_id		*lid, *pid;
	char			*s, idstr[IKED_ID_SIZE];

	/*
	 * The SA should never be without policy. In the case of
	 * 'ikectl reload' the policy is no longer in sc_policies
	 * but is kept alive by the reference from the sa.
	 */
	if (sa->sa_policy == NULL) {
		log_warn("%s: missing SA policy.", SPI_SA(sa, __func__));
		return (-1);
	}

	bzero(&pol, sizeof(pol));
	pol.pol_proposals = sa->sa_proposals;
	pol.pol_af = sa->sa_peer.addr_af;
	if (sa->sa_used_transport_mode)
		pol.pol_flags |= IKED_POLICY_TRANSPORT;
	memcpy(&pol.pol_peer.addr, &sa->sa_peer.addr, sizeof(sa->sa_peer.addr));
	memcpy(&pol.pol_local.addr, &sa->sa_local.addr, sizeof(sa->sa_local.addr));
	pol.pol_flows = sa->sa_policy->pol_flows;
	pol.pol_nflows = sa->sa_policy->pol_nflows;

	if (sa->sa_hdr.sh_initiator) {
		lid = &sa->sa_iid;
		pid = &sa->sa_rid;
	} else {
		lid = &sa->sa_rid;
		pid = &sa->sa_iid;
	}

	if (pid->id_type &&
	    ikev2_print_id(pid, idstr, IKED_ID_SIZE) == 0 &&
	    (s = strchr(idstr, '/')) != NULL) {
		pol.pol_peerid.id_type = pid->id_type;
		pol.pol_peerid.id_length = strlen(s+1);
		strlcpy(pol.pol_peerid.id_data, s+1,
		    sizeof(pol.pol_peerid.id_data));
		log_debug("%s: peerid '%s'", __func__, s+1);
	}

	if (lid->id_type &&
	    ikev2_print_id(lid, idstr, IKED_ID_SIZE) == 0 &&
	    (s = strchr(idstr, '/')) != NULL) {
		pol.pol_localid.id_type = lid->id_type;
		pol.pol_localid.id_length = strlen(s+1);
		strlcpy(pol.pol_localid.id_data, s+1,
		    sizeof(pol.pol_localid.id_data));
		log_debug("%s: localid '%s'", __func__, s+1);
	}

	/* Try to find a matching policy for this message */
	if ((pol_found = policy_test(env, &pol)) != NULL) {
		log_debug("%s: found policy '%s'", SPI_SA(sa, __func__),
		    pol_found->pol_name);
		sa->sa_policy = pol_found;
		return (0);
	}

	/* No policy found */
	return (-1);
}

/*
 * Find a policy matching the query policy key in the global env.
 * If multiple matching policies are found the policy with the highest
 * priority is selected.
 *
 * Returns a pointer to a matching policy, or NULL if no policy matches.
 */
struct iked_policy *
policy_test(struct iked *env, struct iked_policy *key)
{
	struct iked_policy	*p = NULL, *pol = NULL;

	p = TAILQ_FIRST(&env->sc_policies);
	while (p != NULL) {
		if (p->pol_flags & IKED_POLICY_SKIP)
			p = p->pol_skip[IKED_SKIP_FLAGS];
		else if (key->pol_af && p->pol_af &&
		    key->pol_af != p->pol_af)
			p = p->pol_skip[IKED_SKIP_AF];
		else if (sockaddr_cmp((struct sockaddr *)&key->pol_peer.addr,
		    (struct sockaddr *)&p->pol_peer.addr,
		    p->pol_peer.addr_mask) != 0)
			p = p->pol_skip[IKED_SKIP_DST_ADDR];
		else if (sockaddr_cmp((struct sockaddr *)&key->pol_local.addr,
		    (struct sockaddr *)&p->pol_local.addr,
		    p->pol_local.addr_mask) != 0)
			p = p->pol_skip[IKED_SKIP_SRC_ADDR];
		else {
			/*
			 * Check if flows are requested and if they
			 * are compatible.
			 */
			if (key->pol_nflows && policy_test_flows(key, p)) {
				p = TAILQ_NEXT(p, pol_entry);
				continue;
			}
			/* make sure the peer ID matches */
			if (key->pol_peerid.id_type &&
			    p->pol_peerid.id_type &&
			    (key->pol_peerid.id_type != p->pol_peerid.id_type ||
			    memcmp(key->pol_peerid.id_data,
			    p->pol_peerid.id_data,
			    sizeof(key->pol_peerid.id_data)) != 0)) {
				p = TAILQ_NEXT(p, pol_entry);
				continue;
			}

			/* make sure the local ID matches */
			if (key->pol_localid.id_type &&
			    p->pol_localid.id_type &&
			    (key->pol_localid.id_type != p->pol_localid.id_type ||
			    memcmp(key->pol_localid.id_data,
			    p->pol_localid.id_data,
			    sizeof(key->pol_localid.id_data)) != 0)) {
				log_info("%s: localid mismatch", __func__);
				p = TAILQ_NEXT(p, pol_entry);
				continue;
			}

			/* check transport mode */
			if ((key->pol_flags & IKED_POLICY_TRANSPORT) &&
			    !(p->pol_flags & IKED_POLICY_TRANSPORT)) {
				p = TAILQ_NEXT(p, pol_entry);
				continue;
			}

			/* Make sure the proposals are compatible */
			if (TAILQ_FIRST(&key->pol_proposals) &&
			    proposals_negotiate(NULL, &p->pol_proposals,
			    &key->pol_proposals, 0, -1) == -1) {
				p = TAILQ_NEXT(p, pol_entry);
				continue;
			}

			/* Policy matched */
			pol = p;

			if (pol->pol_flags & IKED_POLICY_QUICK)
				break;

			/* Continue to find last matching policy */
			p = TAILQ_NEXT(p, pol_entry);
		}
	}

	return (pol);
}

static int
policy_test_flows(struct iked_policy *key, struct iked_policy *p)
{
	struct iked_flow	*f;

	for (f = RB_MIN(iked_flows, &key->pol_flows); f != NULL;
	    f = RB_NEXT(iked_flows, &key->pol_flows, f))
		if (RB_FIND(iked_flows, &p->pol_flows, f) == NULL)
			return (-1);

	return (0);
}

#define	IKED_SET_SKIP_STEPS(i)						\
	do {								\
		while (head[i] != cur) {				\
			head[i]->pol_skip[i] = cur;			\
			head[i] = TAILQ_NEXT(head[i], pol_entry);	\
		}							\
	} while (0)

/* This code is derived from pf_calc_skip_steps() from pf.c */
void
policy_calc_skip_steps(struct iked_policies *policies)
{
	struct iked_policy	*head[IKED_SKIP_COUNT], *cur, *prev;
	int			 i;

	cur = TAILQ_FIRST(policies);
	prev = cur;
	for (i = 0; i < IKED_SKIP_COUNT; ++i)
		head[i] = cur;
	while (cur != NULL) {
		if (cur->pol_flags & IKED_POLICY_SKIP)
			IKED_SET_SKIP_STEPS(IKED_SKIP_FLAGS);
		if (cur->pol_af != AF_UNSPEC &&
		    prev->pol_af != AF_UNSPEC &&
		    cur->pol_af != prev->pol_af)
			IKED_SET_SKIP_STEPS(IKED_SKIP_AF);
		if (IKED_ADDR_NEQ(&cur->pol_peer, &prev->pol_peer))
			IKED_SET_SKIP_STEPS(IKED_SKIP_DST_ADDR);
		if (IKED_ADDR_NEQ(&cur->pol_local, &prev->pol_local))
			IKED_SET_SKIP_STEPS(IKED_SKIP_SRC_ADDR);

		prev = cur;
		cur = TAILQ_NEXT(cur, pol_entry);
	}
	for (i = 0; i < IKED_SKIP_COUNT; ++i)
		IKED_SET_SKIP_STEPS(i);
}

void
policy_ref(struct iked *env, struct iked_policy *pol)
{
	if (pol->pol_flags & IKED_POLICY_REFCNT)
		pol->pol_refcnt++;
}

void
policy_unref(struct iked *env, struct iked_policy *pol)
{
	if (pol == NULL || (pol->pol_flags & IKED_POLICY_REFCNT) == 0)
		return;
	if (--(pol->pol_refcnt) <= 0)
		config_free_policy(env, pol);
	else {
		struct iked_sa		*tmp;
		int			 count = 0;

		TAILQ_FOREACH(tmp, &pol->pol_sapeers, sa_peer_entry)
			count++;
		if (count != pol->pol_refcnt)
			log_warnx("%s: ERROR pol %p pol_refcnt %d != count %d",
			    __func__, pol, pol->pol_refcnt, count);
	}
}

void
sa_state(struct iked *env, struct iked_sa *sa, int state)
{
	const char		*a;
	const char		*b;
	int			 ostate = sa->sa_state;

	a = print_map(ostate, ikev2_state_map);
	b = print_map(state, ikev2_state_map);

	sa->sa_state = state;
	if (ostate != IKEV2_STATE_INIT &&
	    !sa_stateok(sa, state)) {
		log_debug("%s: cannot switch: %s -> %s",
		    SPI_SA(sa, __func__), a, b);
		sa->sa_state = ostate;
	} else if (ostate != sa->sa_state) {
		switch (state) {
		case IKEV2_STATE_ESTABLISHED:
		case IKEV2_STATE_CLOSED:
			log_debug("%s: %s -> %s from %s to %s policy '%s'",
			    SPI_SA(sa, __func__), a, b,
			    print_addr(&sa->sa_peer.addr),
			    print_addr(&sa->sa_local.addr),
			    sa->sa_policy ? sa->sa_policy->pol_name :
			    "<unknown>");
			break;
		default:
			log_debug("%s: %s -> %s",
			    SPI_SA(sa, __func__), a, b);
			break;
		}
	}

	if (ostate != sa->sa_state) {
		switch (sa->sa_state) {
		case IKEV2_STATE_ESTABLISHED:
			ikestat_inc(env, ikes_sa_established_total);
			ikestat_inc(env, ikes_sa_established_current);
			break;
		case IKEV2_STATE_CLOSED:
		case IKEV2_STATE_CLOSING:
			switch (ostate) {
			case IKEV2_STATE_ESTABLISHED:
				ikestat_dec(env, ikes_sa_established_current);
				break;
			case IKEV2_STATE_CLOSED:
			case IKEV2_STATE_CLOSING:
				break;
			default:
				ikestat_inc(env, ikes_sa_established_failures);
				break;
			}
			break;
		}
	}
}

void
sa_stateflags(struct iked_sa *sa, unsigned int flags)
{
	unsigned int	require;

	if (sa->sa_state > IKEV2_STATE_SA_INIT)
		require = sa->sa_statevalid;
	else
		require = sa->sa_stateinit;

	log_debug("%s: 0x%04x -> 0x%04x %s (required 0x%04x %s)", __func__,
	    sa->sa_stateflags, sa->sa_stateflags | flags,
	    print_bits(sa->sa_stateflags | flags, IKED_REQ_BITS), require,
	    print_bits(require, IKED_REQ_BITS));

	sa->sa_stateflags |= flags;
}

int
sa_stateok(const struct iked_sa *sa, int state)
{
	unsigned int	 require;

	if (sa->sa_state < state)
		return (0);

	if (state == IKEV2_STATE_SA_INIT)
		require = sa->sa_stateinit;
	else
		require = sa->sa_statevalid;

	if (state == IKEV2_STATE_SA_INIT ||
	    state == IKEV2_STATE_VALID ||
	    state == IKEV2_STATE_EAP_VALID) {
		log_debug("%s: %s flags 0x%04x, require 0x%04x %s", __func__,
		    print_map(state, ikev2_state_map),
		    (sa->sa_stateflags & require), require,
		    print_bits(require, IKED_REQ_BITS));

		if ((sa->sa_stateflags & require) != require)
			return (0);	/* not ready, ignore */
	}
	return (1);
}

struct iked_sa *
sa_new(struct iked *env, uint64_t ispi, uint64_t rspi,
    unsigned int initiator, struct iked_policy *pol)
{
	struct iked_sa	*sa;
	struct iked_sa	*old;
	struct iked_id	*localid;
	unsigned int	 diff;

	if ((ispi == 0 && rspi == 0) ||
	    (sa = sa_lookup(env, ispi, rspi, initiator)) == NULL) {
		/* Create new SA */
		if (!initiator && ispi == 0) {
			log_debug("%s: cannot create responder IKE SA w/o ispi",
			    __func__);
			return (NULL);
		}
		sa = config_new_sa(env, initiator);
		if (sa == NULL) {
			log_debug("%s: failed to allocate IKE SA", __func__);
			return (NULL);
		}
		if (!initiator)
			sa->sa_hdr.sh_ispi = ispi;
		old = RB_INSERT(iked_sas, &env->sc_sas, sa);
		if (old && old != sa) {
			log_warnx("%s: duplicate IKE SA", __func__);
			config_free_sa(env, sa);
			return (NULL);
		}
	}
	/* Update rspi in the initator case */
	if (initiator && sa->sa_hdr.sh_rspi == 0 && rspi)
		sa->sa_hdr.sh_rspi = rspi;

	if (pol == NULL && sa->sa_policy == NULL)
		fatalx("%s: sa %p no policy", __func__, sa);
	else if (sa->sa_policy == NULL) {
		policy_ref(env, pol);
		sa->sa_policy = pol;
		TAILQ_INSERT_TAIL(&pol->pol_sapeers, sa, sa_peer_entry);
	} else
		pol = sa->sa_policy;

	sa->sa_statevalid = IKED_REQ_AUTH|IKED_REQ_AUTHVALID|IKED_REQ_SA;
	if (pol != NULL && pol->pol_auth.auth_eap) {
		sa->sa_statevalid |= IKED_REQ_CERT|IKED_REQ_EAPVALID;
	} else if (pol != NULL && pol->pol_auth.auth_method !=
	    IKEV2_AUTH_SHARED_KEY_MIC) {
		sa->sa_statevalid |= IKED_REQ_CERTVALID|IKED_REQ_CERT;
	}

	if (initiator) {
		localid = &sa->sa_iid;
		diff = IKED_REQ_CERTVALID|IKED_REQ_AUTHVALID|IKED_REQ_SA|
		    IKED_REQ_EAPVALID;
		sa->sa_stateinit = sa->sa_statevalid & ~diff;
		sa->sa_statevalid = sa->sa_statevalid & diff;
	} else
		localid = &sa->sa_rid;

	if (pol != NULL &&
	    ikev2_policy2id(&pol->pol_localid, localid, 1) != 0) {
		log_debug("%s: failed to get local id", __func__);
		ikev2_ike_sa_setreason(sa, "failed to get local id");
		sa_free(env, sa);
		return (NULL);
	}

	return (sa);
}

int
policy_generate_ts(struct iked_policy *pol)
{
	struct iked_flow	*flow;

	/* Generate list of traffic selectors from flows */
	RB_FOREACH(flow, iked_flows, &pol->pol_flows) {
		if (ts_insert_unique(&flow->flow_src, &pol->pol_tssrc,
		    flow->flow_ipproto) == 1)
			pol->pol_tssrc_count++;
		if (ts_insert_unique(&flow->flow_dst, &pol->pol_tsdst,
		    flow->flow_ipproto) == 1)
			pol->pol_tsdst_count++;
	}
	if (pol->pol_tssrc_count > IKEV2_MAXNUM_TSS ||
	    pol->pol_tsdst_count > IKEV2_MAXNUM_TSS)
		return (-1);

	return (0);
}

int
ts_insert_unique(struct iked_addr *addr, struct iked_tss *tss, int ipproto)
{
	struct iked_ts		*ts;

	/* Remove duplicates */
	TAILQ_FOREACH(ts, tss, ts_entry) {
		if (addr_cmp(addr, &ts->ts_addr, 1) == 0)
			return (0);
	}

	if ((ts = calloc(1, sizeof(*ts))) == NULL)
		return (-1);

	ts->ts_ipproto = ipproto;
	ts->ts_addr = *addr;

	TAILQ_INSERT_TAIL(tss, ts, ts_entry);
	return (1);
}

void
sa_free(struct iked *env, struct iked_sa *sa)
{
	struct iked_sa	*osa;

	if (sa->sa_reason)
		log_info("%s: %s", SPI_SA(sa, __func__), sa->sa_reason);
	else
		log_debug("%s: ispi %s rspi %s", SPI_SA(sa, __func__),
		    print_spi(sa->sa_hdr.sh_ispi, 8),
		    print_spi(sa->sa_hdr.sh_rspi, 8));

	/* IKE rekeying running? (old sa freed before new sa) */
	if (sa->sa_nexti) {
		RB_REMOVE(iked_sas, &env->sc_sas, sa->sa_nexti);
		if (sa->sa_nexti->sa_dstid_entry_valid) {
			log_info("%s: nexti established? %s",
			    SPI_SA(sa, __func__), SPI_SA(sa->sa_nexti, NULL));
			sa_dstid_remove(env, sa->sa_nexti);
		}
		config_free_sa(env, sa->sa_nexti);
	}
	if (sa->sa_nextr) {
		RB_REMOVE(iked_sas, &env->sc_sas, sa->sa_nextr);
		if (sa->sa_nextr->sa_dstid_entry_valid) {
			log_info("%s: nextr established? %s",
			    SPI_SA(sa, __func__), SPI_SA(sa->sa_nextr, NULL));
			sa_dstid_remove(env, sa->sa_nextr);
		}
		config_free_sa(env, sa->sa_nextr);
	}
	/* reset matching backpointers (new sa freed before old sa) */
	if ((osa = sa->sa_previ) != NULL) {
		if (osa->sa_nexti == sa) {
			log_debug("%s: resetting: sa %p == osa->sa_nexti %p"
			    " (osa %p)",
			    SPI_SA(sa, __func__), osa, sa, osa->sa_nexti);
			osa->sa_nexti = NULL;
		} else {
			log_info("%s: inconsistent: sa %p != osa->sa_nexti %p"
			    " (osa %p)",
			    SPI_SA(sa, __func__), osa, sa, osa->sa_nexti);
		}
	}
	if ((osa = sa->sa_prevr) != NULL) {
		if (osa->sa_nextr == sa) {
			log_debug("%s: resetting: sa %p == osa->sa_nextr %p"
			    " (osa %p)",
			    SPI_SA(sa, __func__), osa, sa, osa->sa_nextr);
			osa->sa_nextr = NULL;
		} else {
			log_info("%s: inconsistent: sa %p != osa->sa_nextr %p"
			    " (osa %p)",
			    SPI_SA(sa, __func__), osa, sa, osa->sa_nextr);
		}
	}
	RB_REMOVE(iked_sas, &env->sc_sas, sa);
	if (sa->sa_dstid_entry_valid)
		sa_dstid_remove(env, sa);
	config_free_sa(env, sa);
}

void
sa_free_flows(struct iked *env, struct iked_saflows *head)
{
	struct iked_flow	*flow, *flowtmp;

	TAILQ_FOREACH_SAFE(flow, head, flow_entry, flowtmp) {
		log_debug("%s: free %p", __func__, flow);

		if (flow->flow_loaded)
			RB_REMOVE(iked_flows, &env->sc_activeflows, flow);
		TAILQ_REMOVE(head, flow, flow_entry);
		(void)pfkey_flow_delete(env, flow);
		flow_free(flow);
	}
}


int
sa_address(struct iked_sa *sa, struct iked_addr *addr, struct sockaddr *peer)
{
	bzero(addr, sizeof(*addr));
	addr->addr_af = peer->sa_family;
	addr->addr_port = htons(socket_getport(peer));
	memcpy(&addr->addr, peer, peer->sa_len);
	if (socket_af((struct sockaddr *)&addr->addr, addr->addr_port) == -1) {
		log_debug("%s: invalid address", __func__);
		return (-1);
	}
	return (0);
}

int
sa_configure_iface(struct iked *env, struct iked_sa *sa, int add)
{
	struct iked_flow	*saflow;
	struct sockaddr		*caddr;
	int			 rdomain;

	if (sa->sa_policy == NULL || sa->sa_policy->pol_iface == 0)
		return (0);

	if (sa->sa_cp_dns) {
		if (vroute_setdns(env, add,
		    (struct sockaddr *)&sa->sa_cp_dns->addr,
		    sa->sa_policy->pol_iface) != 0)
			return (-1);
	}

	if (!sa->sa_cp_addr && !sa->sa_cp_addr6)
		return (0);

	if (sa->sa_cp_addr) {
		if (vroute_setaddr(env, add,
		    (struct sockaddr *)&sa->sa_cp_addr->addr,
		    sa->sa_cp_addr->addr_mask, sa->sa_policy->pol_iface) != 0)
			return (-1);
	}
	if (sa->sa_cp_addr6) {
		if (vroute_setaddr(env, add,
		    (struct sockaddr *)&sa->sa_cp_addr6->addr,
		    sa->sa_cp_addr6->addr_mask, sa->sa_policy->pol_iface) != 0)
			return (-1);
	}

	if (add) {
		/* Add direct route to peer */
		if (vroute_setcloneroute(env, getrtable(),
		    (struct sockaddr *)&sa->sa_peer.addr, 0, NULL))
			return (-1);
	} else {
		if (vroute_setdelroute(env, getrtable(),
		    (struct sockaddr *)&sa->sa_peer.addr,
		    0, NULL))
			return (-1);
	}

	TAILQ_FOREACH(saflow, &sa->sa_flows, flow_entry) {
		rdomain = saflow->flow_rdomain == -1 ?
		    getrtable() : saflow->flow_rdomain;

		switch(saflow->flow_src.addr_af) {
		case AF_INET:
			if (sa->sa_cp_addr == NULL)
				continue;
			caddr = (struct sockaddr *)&sa->sa_cp_addr->addr;
			break;
		case AF_INET6:
			if (sa->sa_cp_addr6 == NULL)
				continue;
			caddr = (struct sockaddr *)&sa->sa_cp_addr6->addr;
			break;
		default:
			return (-1);
		}
		if (sockaddr_cmp((struct sockaddr *)&saflow->flow_src.addr,
		    caddr, -1) != 0)
			continue;

		if (add) {
			if (vroute_setaddroute(env, rdomain,
			    (struct sockaddr *)&saflow->flow_dst.addr,
			    saflow->flow_dst.addr_mask, caddr))
				return (-1);
		} else {
			if (vroute_setdelroute(env, rdomain,
			    (struct sockaddr *)&saflow->flow_dst.addr,
			    saflow->flow_dst.addr_mask, caddr))
				return (-1);
		}
	}

	return (0);
}

void
childsa_free(struct iked_childsa *csa)
{
	struct iked_childsa *csb;

	if (csa == NULL)
		return;

	if (csa->csa_loaded)
		log_info("%s: CHILD SA spi %s is still loaded",
		    csa->csa_ikesa ? SPI_SA(csa->csa_ikesa, __func__) :
		    __func__,
		    print_spi(csa->csa_spi.spi, csa->csa_spi.spi_size));
	if ((csb = csa->csa_bundled) != NULL)
		csb->csa_bundled = NULL;
	if ((csb = csa->csa_peersa) != NULL)
		csb->csa_peersa = NULL;
	ibuf_free(csa->csa_encrkey);
	ibuf_free(csa->csa_integrkey);
	free(csa);
}

struct iked_childsa *
childsa_lookup(struct iked_sa *sa, uint64_t spi, uint8_t protoid)
{
	struct iked_childsa	*csa;

	if (sa == NULL || spi == 0 || protoid == 0)
		return (NULL);

	TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
		if (csa->csa_spi.spi_protoid == protoid &&
		    (csa->csa_spi.spi == spi))
			break;
	}
	return (csa);
}

void
flow_free(struct iked_flow *flow)
{
	free(flow);
}

struct iked_sa *
sa_lookup(struct iked *env, uint64_t ispi, uint64_t rspi,
    unsigned int initiator)
{
	struct iked_sa	*sa, key;

	key.sa_hdr.sh_ispi = ispi;
	key.sa_hdr.sh_initiator = initiator;

	if ((sa = RB_FIND(iked_sas, &env->sc_sas, &key)) != NULL) {
		gettimeofday(&sa->sa_timeused, NULL);

		/* Validate if SPIr matches */
		if ((sa->sa_hdr.sh_rspi != 0) &&
		    (rspi != 0) &&
		    (sa->sa_hdr.sh_rspi != rspi))
			return (NULL);
	}

	return (sa);
}

static __inline int
sa_cmp(struct iked_sa *a, struct iked_sa *b)
{
	if (a->sa_hdr.sh_initiator > b->sa_hdr.sh_initiator)
		return (-1);
	if (a->sa_hdr.sh_initiator < b->sa_hdr.sh_initiator)
		return (1);

	if (a->sa_hdr.sh_ispi > b->sa_hdr.sh_ispi)
		return (-1);
	if (a->sa_hdr.sh_ispi < b->sa_hdr.sh_ispi)
		return (1);

	return (0);
}

static struct iked_id *
sa_dstid_checked(struct iked_sa *sa)
{
	struct iked_id *id;

	id = IKESA_DSTID(sa);
	if (id == NULL || id->id_buf == NULL ||
	    ibuf_data(id->id_buf) == NULL)
		return (NULL);
	if (ibuf_size(id->id_buf) <= id->id_offset)
		return (NULL);
	return (id);
}

struct iked_sa *
sa_dstid_lookup(struct iked *env, struct iked_sa *key)
{
	struct iked_sa *sa;

	if (sa_dstid_checked(key) == NULL)
		fatalx("%s: no id for key %p", __func__, key);
	sa = RB_FIND(iked_dstid_sas, &env->sc_dstid_sas, key);
	if (sa != NULL && !sa->sa_dstid_entry_valid)
		fatalx("%s: sa %p not estab (key %p)", __func__, sa, key);
	return (sa);
}

struct iked_sa *
sa_dstid_insert(struct iked *env, struct iked_sa *sa)
{
	struct iked_sa *osa;

	if (sa->sa_dstid_entry_valid)
		fatalx("%s: sa %p is estab", __func__, sa);
	if (sa_dstid_checked(sa) == NULL)
		fatalx("%s: no id for sa %p", __func__, sa);
	osa = RB_FIND(iked_dstid_sas, &env->sc_dstid_sas, sa);
	if (osa == NULL) {
		osa = RB_INSERT(iked_dstid_sas, &env->sc_dstid_sas, sa);
		if (osa && osa != sa) {
			log_warnx("%s: duplicate IKE SA", SPI_SA(sa, __func__));
			return (osa);
		}
		sa->sa_dstid_entry_valid = 1;
		return (NULL);
	}
	if (!osa->sa_dstid_entry_valid)
		fatalx("%s: osa %p not estab (sa %p)", __func__, osa, sa);
	return (osa);
}

void
sa_dstid_remove(struct iked *env, struct iked_sa *sa)
{
	if (!sa->sa_dstid_entry_valid)
		fatalx("%s: sa %p is not estab", __func__, sa);
	if (sa_dstid_checked(sa) == NULL)
		fatalx("%s: no id for sa %p", __func__, sa);
	RB_REMOVE(iked_dstid_sas, &env->sc_dstid_sas, sa);
	sa->sa_dstid_entry_valid = 0;
}

static __inline int
sa_dstid_cmp(struct iked_sa *a, struct iked_sa *b)
{
	struct iked_id		*aid = NULL, *bid = NULL;
	size_t			 alen, blen;
	uint8_t			*aptr, *bptr;

	aid = sa_dstid_checked(a);
	bid = sa_dstid_checked(b);
	if (aid == NULL || bid == NULL)
		fatalx("corrupt IDs");
	if (aid->id_type > bid->id_type)
		return (-1);
	else if (aid->id_type < bid->id_type)
		return (1);
	alen = ibuf_size(aid->id_buf);
	blen = ibuf_size(bid->id_buf);
	aptr = ibuf_data(aid->id_buf);
	bptr = ibuf_data(bid->id_buf);
	if (aptr == NULL || bptr == NULL)
		fatalx("corrupt ID bufs");
	if (alen <= aid->id_offset || blen <= bid->id_offset)
		fatalx("corrupt ID lens");
	aptr += aid->id_offset;
	alen -= aid->id_offset;
	bptr += bid->id_offset;
	blen -= bid->id_offset;
	if (alen > blen)
		return (-1);
	if (alen < blen)
		return (1);
	return (memcmp(aptr, bptr, alen));
}

static __inline int
sa_addrpool_cmp(struct iked_sa *a, struct iked_sa *b)
{
	return (sockaddr_cmp((struct sockaddr *)&a->sa_addrpool->addr,
	    (struct sockaddr *)&b->sa_addrpool->addr, -1));
}

static __inline int
sa_addrpool6_cmp(struct iked_sa *a, struct iked_sa *b)
{
	return (sockaddr_cmp((struct sockaddr *)&a->sa_addrpool6->addr,
	    (struct sockaddr *)&b->sa_addrpool6->addr, -1));
}

struct iked_user *
user_lookup(struct iked *env, const char *user)
{
	struct iked_user	 key;

	if (strlcpy(key.usr_name, user,
	    sizeof(key.usr_name)) >= sizeof(key.usr_name))
		return (NULL);

	return (RB_FIND(iked_users, &env->sc_users, &key));
}

static __inline int
user_cmp(struct iked_user *a, struct iked_user *b)
{
	return (strcmp(a->usr_name, b->usr_name));
}

/*
 * Find a matching subset of the proposal lists 'local' and 'peer'.
 * The resulting proposal is stored in 'result' if 'result' is not NULL.
 * The 'rekey' parameter indicates a CREATE_CHILD_SA exchange where
 * an extra group is necessary for PFS. For the initial IKE_AUTH exchange
 * the ESP SA proposal never includes an explicit DH group.
 *
 * Return 0 if a matching subset was found and -1 if no subset was found
 * or an error occured.
 */
int
proposals_negotiate(struct iked_proposals *result, struct iked_proposals *local,
    struct iked_proposals *peer, int rekey, int groupid)
{
	struct iked_proposal	*ppeer = NULL, *plocal, *prop, vpeer, vlocal;
	struct iked_transform	 chosen[IKEV2_XFORMTYPE_MAX];
	struct iked_transform	*valid[IKEV2_XFORMTYPE_MAX];
	struct iked_transform	*match[IKEV2_XFORMTYPE_MAX];
	unsigned int		 i, score, chosen_score = 0;
	uint8_t			 protoid = 0;

	bzero(valid, sizeof(valid));
	bzero(&vlocal, sizeof(vlocal));
	bzero(&vpeer, sizeof(vpeer));

	if (TAILQ_EMPTY(peer)) {
		log_debug("%s: peer did not send %s proposals", __func__,
		    print_map(protoid, ikev2_saproto_map));
		return (-1);
	}

	TAILQ_FOREACH(plocal, local, prop_entry) {
		TAILQ_FOREACH(ppeer, peer, prop_entry) {
			if (ppeer->prop_protoid != plocal->prop_protoid)
				continue;
			bzero(match, sizeof(match));
			score = proposals_match(plocal, ppeer, match,
			    rekey, groupid);
			log_debug("%s: score %d", __func__, score);
			if (score && (!chosen_score || score < chosen_score)) {
				chosen_score = score;
				for (i = 0; i < IKEV2_XFORMTYPE_MAX; i++) {
					if ((valid[i] = match[i]))
						memcpy(&chosen[i], match[i],
						    sizeof(chosen[0]));
				}
				memcpy(&vpeer, ppeer, sizeof(vpeer));
				memcpy(&vlocal, plocal, sizeof(vlocal));
			}
		}
		if (chosen_score != 0)
			break;
	}

	if (chosen_score == 0)
		return (-1);
	else if (result == NULL)
		return (0);

	(void)config_free_proposals(result, vpeer.prop_protoid);
	prop = config_add_proposal(result, vpeer.prop_id, vpeer.prop_protoid);

	if (vpeer.prop_localspi.spi_size) {
		prop->prop_localspi.spi_size = vpeer.prop_localspi.spi_size;
		prop->prop_peerspi = vpeer.prop_peerspi;
	}
	if (vlocal.prop_localspi.spi_size) {
		prop->prop_localspi.spi_size = vlocal.prop_localspi.spi_size;
		prop->prop_localspi.spi = vlocal.prop_localspi.spi;
	}

	for (i = 0; i < IKEV2_XFORMTYPE_MAX; i++) {
		if (valid[i] == NULL)
			continue;
		print_debug("%s: score %d: %s %s", __func__,
		    chosen[i].xform_score, print_map(i, ikev2_xformtype_map),
		    print_map(chosen[i].xform_id, chosen[i].xform_map));
		if (chosen[i].xform_length)
			print_debug(" %d", chosen[i].xform_length);
		print_debug("\n");

		if (config_add_transform(prop, chosen[i].xform_type,
		    chosen[i].xform_id, chosen[i].xform_length,
		    chosen[i].xform_keylength) != 0)
			break;
	}

	return (0);
}

static int
proposals_match(struct iked_proposal *local, struct iked_proposal *peer,
    struct iked_transform **xforms, int rekey, int dhgroup)
{
	struct iked_transform	*tpeer, *tlocal;
	unsigned int		 i, j, type, score, requiredh = 0, nodh = 0, noauth = 0;
	unsigned int		 dhforced = 0;
	uint8_t			 protoid = peer->prop_protoid;
	uint8_t			 peerxfs[IKEV2_XFORMTYPE_MAX];

	bzero(peerxfs, sizeof(peerxfs));

	for (i = 0; i < peer->prop_nxforms; i++) {
		tpeer = peer->prop_xforms + i;
		/* If any of the ENC transforms is an AEAD, ignore auth */
		if (tpeer->xform_type == IKEV2_XFORMTYPE_ENCR &&
		    encxf_noauth(tpeer->xform_id))
			noauth = 1;
	}

	for (i = 0; i < peer->prop_nxforms; i++) {
		tpeer = peer->prop_xforms + i;
		if (tpeer->xform_type >= IKEV2_XFORMTYPE_MAX)
			continue;
		if (noauth && tpeer->xform_type == IKEV2_XFORMTYPE_INTEGR)
			return (0);

		/*
		 * Record all transform types from the peer's proposal,
		 * because if we want this proposal we have to select
		 * a transform for each proposed transform type.
		 */
		peerxfs[tpeer->xform_type] = 1;

		for (j = 0; j < local->prop_nxforms; j++) {
			tlocal = local->prop_xforms + j;

			/*
			 * We require a DH group for ESP if there is any
			 * local proposal with DH enabled.
			 */
			if (rekey && requiredh == 0 &&
			    protoid == IKEV2_SAPROTO_ESP &&
			    tlocal->xform_type == IKEV2_XFORMTYPE_DH &&
			    tlocal->xform_id != IKEV2_XFORMDH_NONE)
				requiredh = 1;

			/*
			 * If none is an explicit option, don't require
			 * DH group. Overrides requiredh = 1.
			 */
			if (rekey && nodh == 0 &&
			    protoid == IKEV2_SAPROTO_ESP &&
			    tlocal->xform_type == IKEV2_XFORMTYPE_DH &&
			    tlocal->xform_id == IKEV2_XFORMDH_NONE)
				nodh = 1;

			/* Compare peer and local proposals */
			if (tpeer->xform_type != tlocal->xform_type ||
			    tpeer->xform_id != tlocal->xform_id ||
			    tpeer->xform_length != tlocal->xform_length)
				continue;
			type = tpeer->xform_type;

			if (nodh == 0 && dhgroup >= 0 &&
			    type == IKEV2_XFORMTYPE_DH) {
				if (dhforced)
					continue;
				/* reset xform, so this xform w/matching group is enforced */
				if (tlocal->xform_id == dhgroup) {
					xforms[type] = NULL;
					dhforced = 1;
				}
			}

			if (xforms[type] == NULL || tlocal->xform_score <
			    xforms[type]->xform_score) {
				xforms[type] = tlocal;
			} else
				continue;

			print_debug("%s: xform %d <-> %d (%d): %s %s "
			    "(keylength %d <-> %d)", __func__,
			    peer->prop_id, local->prop_id, tlocal->xform_score,
			    print_map(type, ikev2_xformtype_map),
			    print_map(tpeer->xform_id, tpeer->xform_map),
			    tpeer->xform_keylength, tlocal->xform_keylength);
			if (tpeer->xform_length)
				print_debug(" %d", tpeer->xform_length);
			print_debug("\n");
		}
	}

	for (i = score = 0; i < IKEV2_XFORMTYPE_MAX; i++) {
		if (protoid == IKEV2_SAPROTO_IKE && xforms[i] == NULL &&
		    (i == IKEV2_XFORMTYPE_ENCR || i == IKEV2_XFORMTYPE_PRF ||
		    (!noauth && i == IKEV2_XFORMTYPE_INTEGR) ||
		    i == IKEV2_XFORMTYPE_DH)) {
			score = 0;
			break;
		} else if (protoid == IKEV2_SAPROTO_AH && xforms[i] == NULL &&
		    (i == IKEV2_XFORMTYPE_INTEGR || i == IKEV2_XFORMTYPE_ESN)) {
			score = 0;
			break;
		} else if (protoid == IKEV2_SAPROTO_ESP && xforms[i] == NULL &&
		    (i == IKEV2_XFORMTYPE_ENCR || i == IKEV2_XFORMTYPE_ESN ||
		    (requiredh && !nodh && i == IKEV2_XFORMTYPE_DH))) {
			score = 0;
			break;
		} else if (peerxfs[i] && xforms[i] == NULL) {
			score = 0;
			break;
		} else if (xforms[i] == NULL)
			continue;

		score += xforms[i]->xform_score;
	}

	return (score);
}

static __inline int
childsa_cmp(struct iked_childsa *a, struct iked_childsa *b)
{
	if (a->csa_spi.spi > b->csa_spi.spi)
		return (1);
	if (a->csa_spi.spi < b->csa_spi.spi)
		return (-1);
	return (0);
}

static __inline int
addr_cmp(struct iked_addr *a, struct iked_addr *b, int useports)
{
	int		diff = 0;

	diff = sockaddr_cmp((struct sockaddr *)&a->addr,
	    (struct sockaddr *)&b->addr, 128);
	if (!diff)
		diff = (int)a->addr_mask - (int)b->addr_mask;
	if (!diff && useports)
		diff = a->addr_port - b->addr_port;

	return (diff);
}

static __inline int
flow_cmp(struct iked_flow *a, struct iked_flow *b)
{
	int		diff = 0;

	if (!diff)
		diff = a->flow_rdomain - b->flow_rdomain;
	if (!diff)
		diff = (int)a->flow_ipproto - (int)b->flow_ipproto;
	if (!diff)
		diff = (int)a->flow_saproto - (int)b->flow_saproto;
	if (!diff)
		diff = (int)a->flow_dir - (int)b->flow_dir;
	if (!diff)
		diff = addr_cmp(&a->flow_dst, &b->flow_dst, 1);
	if (!diff)
		diff = addr_cmp(&a->flow_src, &b->flow_src, 1);
	if (!diff)
		diff = addr_cmp(&a->flow_prenat, &b->flow_prenat, 0);

	return (diff);
}

int
flow_equal(struct iked_flow *a, struct iked_flow *b)
{
	return (flow_cmp(a, b) == 0);
}

RB_GENERATE(iked_sas, iked_sa, sa_entry, sa_cmp);
RB_GENERATE(iked_dstid_sas, iked_sa, sa_dstid_entry, sa_dstid_cmp);
RB_GENERATE(iked_addrpool, iked_sa, sa_addrpool_entry, sa_addrpool_cmp);
RB_GENERATE(iked_addrpool6, iked_sa, sa_addrpool6_entry, sa_addrpool6_cmp);
RB_GENERATE(iked_users, iked_user, usr_entry, user_cmp);
RB_GENERATE(iked_activesas, iked_childsa, csa_node, childsa_cmp);
RB_GENERATE(iked_flows, iked_flow, flow_node, flow_cmp);
