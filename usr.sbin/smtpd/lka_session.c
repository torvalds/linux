/*	$OpenBSD: lka_session.c,v 1.100 2024/02/02 23:33:42 gilles Exp $	*/

/*
 * Copyright (c) 2011 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

#define	EXPAND_DEPTH	10

#define	F_WAITING	0x01

struct lka_session {
	uint64_t		 id; /* given by smtp */

	TAILQ_HEAD(, envelope)	 deliverylist;
	struct expand		 expand;

	int			 flags;
	int			 error;
	const char		*errormsg;
	struct envelope		 envelope;
	struct xnodes		 nodes;
	/* waiting for fwdrq */
	struct rule		*rule;
	struct expandnode	*node;
};

static void lka_expand(struct lka_session *, struct rule *,
    struct expandnode *);
static void lka_submit(struct lka_session *, struct rule *,
    struct expandnode *);
static void lka_resume(struct lka_session *);

static int		init;
static struct tree	sessions;

void
lka_session(uint64_t id, struct envelope *envelope)
{
	struct lka_session	*lks;
	struct expandnode	 xn;

	if (init == 0) {
		init = 1;
		tree_init(&sessions);
	}

	lks = xcalloc(1, sizeof(*lks));
	lks->id = id;
	RB_INIT(&lks->expand.tree);
	TAILQ_INIT(&lks->deliverylist);
	tree_xset(&sessions, lks->id, lks);

	lks->envelope = *envelope;

	TAILQ_INIT(&lks->nodes);
	memset(&xn, 0, sizeof xn);
	xn.type = EXPAND_ADDRESS;
	xn.u.mailaddr = lks->envelope.rcpt;
	lks->expand.parent = NULL;
	lks->expand.rule = NULL;
	lks->expand.queue = &lks->nodes;
	expand_insert(&lks->expand, &xn);
	lka_resume(lks);
}

void
lka_session_forward_reply(struct forward_req *fwreq, int fd)
{
	struct lka_session     *lks;
	struct dispatcher      *dsp;
	struct rule	       *rule;
	struct expandnode      *xn;
	int			ret;

	lks = tree_xget(&sessions, fwreq->id);
	xn = lks->node;
	rule = lks->rule;

	lks->flags &= ~F_WAITING;

	switch (fwreq->status) {
	case 0:
		/* permanent failure while lookup ~/.forward */
		log_trace(TRACE_EXPAND, "expand: ~/.forward failed for user %s",
		    fwreq->user);
		lks->error = LKA_PERMFAIL;
		break;
	case 1:
		if (fd == -1) {
			dsp = dict_get(env->sc_dispatchers, lks->rule->dispatcher);
			if (dsp->u.local.forward_only) {
				log_trace(TRACE_EXPAND, "expand: no .forward "
				    "for user %s on forward-only rule", fwreq->user);
				lks->error = LKA_TEMPFAIL;
			}
			else if (dsp->u.local.expand_only) {
				log_trace(TRACE_EXPAND, "expand: no .forward "
				    "for user %s and no default action on rule", fwreq->user);
				lks->error = LKA_PERMFAIL;
			}
			else {
				log_trace(TRACE_EXPAND, "expand: no .forward for "
				    "user %s, just deliver", fwreq->user);
				lka_submit(lks, rule, xn);
			}
		}
		else {
			dsp = dict_get(env->sc_dispatchers, rule->dispatcher);

			/* expand for the current user and rule */
			lks->expand.rule = rule;
			lks->expand.parent = xn;

			/* forwards_get() will close the descriptor no matter what */
			ret = forwards_get(fd, &lks->expand);
			if (ret == -1) {
				log_trace(TRACE_EXPAND, "expand: temporary "
				    "forward error for user %s", fwreq->user);
				lks->error = LKA_TEMPFAIL;
			}
			else if (ret == 0) {
				if (dsp->u.local.forward_only) {
					log_trace(TRACE_EXPAND, "expand: empty .forward "
					    "for user %s on forward-only rule", fwreq->user);
					lks->error = LKA_TEMPFAIL;
				}
				else if (dsp->u.local.expand_only) {
					log_trace(TRACE_EXPAND, "expand: empty .forward "
					    "for user %s and no default action on rule", fwreq->user);
					lks->error = LKA_PERMFAIL;
				}
				else {
					log_trace(TRACE_EXPAND, "expand: empty .forward "
					    "for user %s, just deliver", fwreq->user);
					lka_submit(lks, rule, xn);
				}
			}
		}
		break;
	default:
		/* temporary failure while looking up ~/.forward */
		lks->error = LKA_TEMPFAIL;
	}

	if (lks->error == LKA_TEMPFAIL && lks->errormsg == NULL)
		lks->errormsg = "424 4.2.4 Mailing list expansion problem";
	if (lks->error == LKA_PERMFAIL && lks->errormsg == NULL)
		lks->errormsg = "524 5.2.4 Mailing list expansion problem";

	lka_resume(lks);
}

static void
lka_resume(struct lka_session *lks)
{
	struct envelope		*ep;
	struct expandnode	*xn;

	if (lks->error)
		goto error;

	/* pop next node and expand it */
	while ((xn = TAILQ_FIRST(&lks->nodes))) {
		TAILQ_REMOVE(&lks->nodes, xn, tq_entry);
		lka_expand(lks, xn->rule, xn);
		if (lks->flags & F_WAITING)
			return;
		if (lks->error)
			goto error;
	}

	/* delivery list is empty, reject */
	if (TAILQ_FIRST(&lks->deliverylist) == NULL) {
		log_trace(TRACE_EXPAND, "expand: lka_done: expanded to empty "
		    "delivery list");
		lks->error = LKA_PERMFAIL;
		lks->errormsg = "524 5.2.4 Mailing list expansion problem";
	}
    error:
	if (lks->error) {
		m_create(p_dispatcher, IMSG_SMTP_EXPAND_RCPT, 0, 0, -1);
		m_add_id(p_dispatcher, lks->id);
		m_add_int(p_dispatcher, lks->error);

		if (lks->errormsg)
			m_add_string(p_dispatcher, lks->errormsg);
		else {
			if (lks->error == LKA_PERMFAIL)
				m_add_string(p_dispatcher, "550 Invalid recipient");
			else if (lks->error == LKA_TEMPFAIL)
				m_add_string(p_dispatcher, "451 Temporary failure");
		}

		m_close(p_dispatcher);
		while ((ep = TAILQ_FIRST(&lks->deliverylist)) != NULL) {
			TAILQ_REMOVE(&lks->deliverylist, ep, entry);
			free(ep);
		}
	}
	else {
		/* Process the delivery list and submit envelopes to queue */
		while ((ep = TAILQ_FIRST(&lks->deliverylist)) != NULL) {
			TAILQ_REMOVE(&lks->deliverylist, ep, entry);
			m_create(p_queue, IMSG_LKA_ENVELOPE_SUBMIT, 0, 0, -1);
			m_add_id(p_queue, lks->id);
			m_add_envelope(p_queue, ep);
			m_close(p_queue);
			free(ep);
		}

		m_create(p_queue, IMSG_LKA_ENVELOPE_COMMIT, 0, 0, -1);
		m_add_id(p_queue, lks->id);
		m_close(p_queue);
	}

	expand_clear(&lks->expand);
	tree_xpop(&sessions, lks->id);
	free(lks);
}

static void
lka_expand(struct lka_session *lks, struct rule *rule, struct expandnode *xn)
{
	struct forward_req	fwreq;
	struct envelope		ep;
	struct expandnode	node;
	struct mailaddr		maddr;
	struct dispatcher      *dsp;
	struct table	       *userbase;
	int			r;
	union lookup		lk;
	char		       *tag;
	const char	       *srs_decoded;

	if (xn->depth >= EXPAND_DEPTH) {
		log_trace(TRACE_EXPAND, "expand: lka_expand: node too deep.");
		lks->error = LKA_PERMFAIL;
		lks->errormsg = "524 5.2.4 Mailing list expansion problem";
		return;
	}

	switch (xn->type) {
	case EXPAND_INVALID:
	case EXPAND_INCLUDE:
		fatalx("lka_expand: unexpected type");
		break;

	case EXPAND_ADDRESS:

		log_trace(TRACE_EXPAND, "expand: lka_expand: address: %s@%s "
		    "[depth=%d]",
		    xn->u.mailaddr.user, xn->u.mailaddr.domain, xn->depth);


		ep = lks->envelope;
		ep.dest = xn->u.mailaddr;
		if (xn->parent) /* nodes with parent are forward addresses */
			ep.flags |= EF_INTERNAL;

		/* handle SRS */
		if (env->sc_srs_key != NULL &&
		    ep.sender.user[0] == '\0' &&
		    (strncasecmp(ep.dest.user, "SRS0=", 5) == 0 ||
			strncasecmp(ep.dest.user, "SRS1=", 5) == 0)) {
			srs_decoded = srs_decode(mailaddr_to_text(&ep.dest));
			if (srs_decoded &&
			    text_to_mailaddr(&ep.dest, srs_decoded)) {
				/* flag envelope internal and override dest */
				ep.flags |= EF_INTERNAL;
				xn->u.mailaddr = ep.dest;
				lks->envelope = ep;
			}
			else {
				log_warn("SRS failed to decode: %s",
				    mailaddr_to_text(&ep.dest));
			}
		}

		/* Pass the node through the ruleset */
		rule = ruleset_match(&ep);
		if (rule == NULL || rule->reject) {
			lks->error = (errno == EAGAIN) ?
			    LKA_TEMPFAIL : LKA_PERMFAIL;
			break;
		}

		dsp = dict_xget(env->sc_dispatchers, rule->dispatcher);
		if (dsp->type == DISPATCHER_REMOTE) {
			lka_submit(lks, rule, xn);
		}
		else if (dsp->u.local.table_virtual) {
			/* expand */
			lks->expand.rule = rule;
			lks->expand.parent = xn;

			/* temporary replace the mailaddr with a copy where
			 * we eventually strip the '+'-part before lookup.
			 */
			maddr = xn->u.mailaddr;
			xlowercase(maddr.user, xn->u.mailaddr.user,
			    sizeof maddr.user);
			r = aliases_virtual_get(&lks->expand, &maddr);
			if (r == -1) {
				lks->error = LKA_TEMPFAIL;
				log_trace(TRACE_EXPAND, "expand: lka_expand: "
				    "error in virtual alias lookup");
			}
			else if (r == 0) {
				lks->error = LKA_PERMFAIL;
				log_trace(TRACE_EXPAND, "expand: lka_expand: "
				    "no aliases for virtual");
			}
			if (lks->error == LKA_TEMPFAIL && lks->errormsg == NULL)
				lks->errormsg = "424 4.2.4 Mailing list expansion problem";
			if (lks->error == LKA_PERMFAIL && lks->errormsg == NULL)
				lks->errormsg = "524 5.2.4 Mailing list expansion problem";
		}
		else {
			lks->expand.rule = rule;
			lks->expand.parent = xn;
			xn->rule = rule;

			memset(&node, 0, sizeof node);
			node.type = EXPAND_USERNAME;
			xlowercase(node.u.user, xn->u.mailaddr.user,
			    sizeof node.u.user);
			expand_insert(&lks->expand, &node);
		}
		break;

	case EXPAND_USERNAME:
		log_trace(TRACE_EXPAND, "expand: lka_expand: username: %s "
		    "[depth=%d, sameuser=%d]",
		    xn->u.user, xn->depth, xn->sameuser);

		/* expand aliases with the given rule */
		dsp = dict_xget(env->sc_dispatchers, rule->dispatcher);

		lks->expand.rule = rule;
		lks->expand.parent = xn;

		if (!xn->sameuser &&
		    (dsp->u.local.table_alias || dsp->u.local.table_virtual)) {
			if (dsp->u.local.table_alias)
				r = aliases_get(&lks->expand, xn->u.user);
			if (dsp->u.local.table_virtual)
				r = aliases_virtual_get(&lks->expand, &xn->u.mailaddr);
			if (r == -1) {
				log_trace(TRACE_EXPAND, "expand: lka_expand: "
				    "error in alias lookup");
				lks->error = LKA_TEMPFAIL;
				if (lks->errormsg == NULL)
					lks->errormsg = "424 4.2.4 Mailing list expansion problem";
			}
			if (r)
				break;
		}

		/* gilles+hackers@ -> gilles@ */
		if ((tag = strchr(xn->u.user, *env->sc_subaddressing_delim)) != NULL) {
			*tag++ = '\0';
			(void)strlcpy(xn->subaddress, tag, sizeof xn->subaddress);
		}

		userbase = table_find(env, dsp->u.local.table_userbase);
		r = table_lookup(userbase, K_USERINFO, xn->u.user, &lk);
		if (r == -1) {
			log_trace(TRACE_EXPAND, "expand: lka_expand: "
			    "backend error while searching user");
			lks->error = LKA_TEMPFAIL;
			break;
		}
		if (r == 0) {
			log_trace(TRACE_EXPAND, "expand: lka_expand: "
			    "user-part does not match system user");
			lks->error = LKA_PERMFAIL;
			break;
		}
		xn->realuser = 1;
		xn->realuser_uid = lk.userinfo.uid;

		if (xn->sameuser && xn->parent->forwarded) {
			log_trace(TRACE_EXPAND, "expand: lka_expand: same "
			    "user, submitting");
			lka_submit(lks, rule, xn);
			break;
		}


		/* when alternate delivery user is provided,
		 * skip other users forward files.
		 */
		if (dsp->u.local.user) {
			if (strcmp(dsp->u.local.user, xn->u.user) != 0) {
				log_trace(TRACE_EXPAND, "expand: lka_expand: "
				    "alternate delivery user mismatch recipient "
				    "user, skip .forward, submitting");
				lka_submit(lks, rule, xn);
				break;
			}
		}

		/* no aliases found, query forward file */
		lks->rule = rule;
		lks->node = xn;
		xn->forwarded = 1;

		memset(&fwreq, 0, sizeof(fwreq));
		fwreq.id = lks->id;
		(void)strlcpy(fwreq.user, lk.userinfo.username, sizeof(fwreq.user));
		(void)strlcpy(fwreq.directory, lk.userinfo.directory, sizeof(fwreq.directory));
		fwreq.uid = lk.userinfo.uid;
		fwreq.gid = lk.userinfo.gid;

		m_compose(p_parent, IMSG_LKA_OPEN_FORWARD, 0, 0, -1,
		    &fwreq, sizeof(fwreq));
		lks->flags |= F_WAITING;
		break;

	case EXPAND_FILENAME:
		if (xn->parent->realuser && xn->parent->realuser_uid == 0) {
			log_trace(TRACE_EXPAND, "expand: filename not allowed in root's forward");
			lks->error = LKA_TEMPFAIL;
			break;
		}

		dsp = dict_xget(env->sc_dispatchers, rule->dispatcher);
		if (dsp->u.local.forward_only) {
			log_trace(TRACE_EXPAND, "expand: filename matched on forward-only rule");
			lks->error = LKA_TEMPFAIL;
			break;
		}
		log_trace(TRACE_EXPAND, "expand: lka_expand: filename: %s "
		    "[depth=%d]", xn->u.buffer, xn->depth);
		lka_submit(lks, rule, xn);
		break;

	case EXPAND_ERROR:
		dsp = dict_xget(env->sc_dispatchers, rule->dispatcher);
		if (dsp->u.local.forward_only) {
			log_trace(TRACE_EXPAND, "expand: error matched on forward-only rule");
			lks->error = LKA_TEMPFAIL;
			break;
		}
		log_trace(TRACE_EXPAND, "expand: lka_expand: error: %s "
		    "[depth=%d]", xn->u.buffer, xn->depth);
		if (xn->u.buffer[0] == '4')
			lks->error = LKA_TEMPFAIL;
		else if (xn->u.buffer[0] == '5')
			lks->error = LKA_PERMFAIL;
		lks->errormsg = xn->u.buffer;
		break;

	case EXPAND_FILTER:
		if (xn->parent->realuser && xn->parent->realuser_uid == 0) {
			log_trace(TRACE_EXPAND, "expand: filter not allowed in root's forward");
			lks->error = LKA_TEMPFAIL;
			break;
		}

		dsp = dict_xget(env->sc_dispatchers, rule->dispatcher);
		if (dsp->u.local.forward_only) {
			log_trace(TRACE_EXPAND, "expand: filter matched on forward-only rule");
			lks->error = LKA_TEMPFAIL;
			break;
		}
		log_trace(TRACE_EXPAND, "expand: lka_expand: filter: %s "
		    "[depth=%d]", xn->u.buffer, xn->depth);
		lka_submit(lks, rule, xn);
		break;
	}
}

static struct expandnode *
lka_find_ancestor(struct expandnode *xn, enum expand_type type)
{
	while (xn && (xn->type != type))
		xn = xn->parent;
	if (xn == NULL) {
		log_warnx("warn: lka_find_ancestor: no ancestors of type %d",
		    type);
		fatalx(NULL);
	}
	return (xn);
}

static void
lka_submit(struct lka_session *lks, struct rule *rule, struct expandnode *xn)
{
	struct envelope		*ep;
	struct dispatcher	*dsp;
	const char		*user;
	const char		*format;

	ep = xmemdup(&lks->envelope, sizeof *ep);
	(void)strlcpy(ep->dispatcher, rule->dispatcher, sizeof ep->dispatcher);

	dsp = dict_xget(env->sc_dispatchers, ep->dispatcher);

	switch (dsp->type) {
	case DISPATCHER_REMOTE:
		if (xn->type != EXPAND_ADDRESS)
			fatalx("lka_deliver: expect address");
		ep->type = D_MTA;
		ep->dest = xn->u.mailaddr;
		break;

	case DISPATCHER_BOUNCE:
	case DISPATCHER_LOCAL:
		if (xn->type != EXPAND_USERNAME &&
		    xn->type != EXPAND_FILENAME &&
		    xn->type != EXPAND_FILTER)
			fatalx("lka_deliver: wrong type: %d", xn->type);

		ep->type = D_MDA;
		ep->dest = lka_find_ancestor(xn, EXPAND_ADDRESS)->u.mailaddr;
		if (xn->type == EXPAND_USERNAME) {
			(void)strlcpy(ep->mda_user, xn->u.user, sizeof(ep->mda_user));
			(void)strlcpy(ep->mda_subaddress, xn->subaddress, sizeof(ep->mda_subaddress));
		}
		else {
			user = !xn->parent->realuser ?
			    SMTPD_USER :
			    xn->parent->u.user;
			(void)strlcpy(ep->mda_user, user, sizeof (ep->mda_user));

			/* this battle needs to be fought ... */
			if (xn->type == EXPAND_FILTER &&
			    strcmp(ep->mda_user, SMTPD_USER) == 0)
				log_warnx("commands executed from aliases "
				    "run with %s privileges", SMTPD_USER);

			format = "%s";
			if (xn->type == EXPAND_FILENAME)
				format = "/usr/libexec/mail.mboxfile -f %%{mbox.from} %s";
			(void)snprintf(ep->mda_exec, sizeof(ep->mda_exec),
			    format, xn->u.buffer);
		}
		break;
	}

	TAILQ_INSERT_TAIL(&lks->deliverylist, ep, entry);
}
