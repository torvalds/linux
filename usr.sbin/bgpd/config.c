/*	$OpenBSD: config.c,v 1.116 2025/03/26 15:28:13 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004, 2005 Henning Brauer <henning@openbsd.org>
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
#include <sys/socket.h>
#include <arpa/inet.h>

#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

int		host_ip(const char *, struct bgpd_addr *, uint8_t *);
void		free_networks(struct network_head *);
void		free_flowspecs(struct flowspec_tree *);

struct bgpd_config *
new_config(void)
{
	struct bgpd_config	*conf;

	if ((conf = calloc(1, sizeof(struct bgpd_config))) == NULL)
		fatal(NULL);

	if ((conf->filters = calloc(1, sizeof(struct filter_head))) == NULL)
		fatal(NULL);
	if ((conf->listen_addrs = calloc(1, sizeof(struct listen_addrs))) ==
	    NULL)
		fatal(NULL);
	if ((conf->mrt = calloc(1, sizeof(struct mrt_head))) == NULL)
		fatal(NULL);

	/* init the various list for later */
	RB_INIT(&conf->peers);
	TAILQ_INIT(&conf->networks);
	RB_INIT(&conf->flowspecs);
	SIMPLEQ_INIT(&conf->l3vpns);
	SIMPLEQ_INIT(&conf->prefixsets);
	SIMPLEQ_INIT(&conf->originsets);
	SIMPLEQ_INIT(&conf->rde_prefixsets);
	SIMPLEQ_INIT(&conf->rde_originsets);
	RB_INIT(&conf->roa);
	RB_INIT(&conf->aspa);
	SIMPLEQ_INIT(&conf->as_sets);
	SIMPLEQ_INIT(&conf->rtrs);

	TAILQ_INIT(conf->filters);
	TAILQ_INIT(conf->listen_addrs);
	LIST_INIT(conf->mrt);

	return (conf);
}

void
copy_config(struct bgpd_config *to, struct bgpd_config *from)
{
	to->flags = from->flags;
	to->log = from->log;
	to->default_tableid = from->default_tableid;
	to->bgpid = from->bgpid;
	to->clusterid = from->clusterid;
	to->as = from->as;
	to->short_as = from->short_as;
	to->holdtime = from->holdtime;
	to->min_holdtime = from->min_holdtime;
	to->staletime = from->staletime;
	to->connectretry = from->connectretry;
	to->fib_priority = from->fib_priority;
	to->filtered_in_locrib = from->filtered_in_locrib;
}

void
network_free(struct network *n)
{
	rtlabel_unref(n->net.rtlabel);
	filterset_free(&n->net.attrset);
	free(n);
}

void
free_networks(struct network_head *networks)
{
	struct network		*n;

	while ((n = TAILQ_FIRST(networks)) != NULL) {
		TAILQ_REMOVE(networks, n, entry);
		network_free(n);
	}
}

struct flowspec_config *
flowspec_alloc(uint8_t aid, int len)
{
	struct flowspec_config *conf;
	struct flowspec *flow;

	flow = malloc(FLOWSPEC_SIZE + len);
	if (flow == NULL)
		return NULL;
	memset(flow, 0, FLOWSPEC_SIZE);

	conf = calloc(1, sizeof(*conf));
	if (conf == NULL) {
		free(flow);
		return NULL;
	}

	conf->flow = flow;
	TAILQ_INIT(&conf->attrset);
	flow->len = len;
	flow->aid = aid;

	return conf;
}

void
flowspec_free(struct flowspec_config *f)
{
	filterset_free(&f->attrset);
	free(f->flow);
	free(f);
}

void
free_flowspecs(struct flowspec_tree *flowspecs)
{
	struct flowspec_config *f, *nf;

	RB_FOREACH_SAFE(f, flowspec_tree, flowspecs, nf) {
		RB_REMOVE(flowspec_tree, flowspecs, f);
		flowspec_free(f);
	}
}

void
free_l3vpns(struct l3vpn_head *l3vpns)
{
	struct l3vpn		*vpn;

	while ((vpn = SIMPLEQ_FIRST(l3vpns)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(l3vpns, entry);
		filterset_free(&vpn->export);
		filterset_free(&vpn->import);
		free_networks(&vpn->net_l);
		free(vpn);
	}
}

void
free_prefixsets(struct prefixset_head *psh)
{
	struct prefixset	*ps;

	while (!SIMPLEQ_EMPTY(psh)) {
		ps = SIMPLEQ_FIRST(psh);
		free_roatree(&ps->roaitems);
		free_prefixtree(&ps->psitems);
		SIMPLEQ_REMOVE_HEAD(psh, entry);
		free(ps);
	}
}

void
free_rde_prefixsets(struct rde_prefixset_head *psh)
{
	struct rde_prefixset	*ps;

	if (psh == NULL)
		return;

	while (!SIMPLEQ_EMPTY(psh)) {
		ps = SIMPLEQ_FIRST(psh);
		trie_free(&ps->th);
		SIMPLEQ_REMOVE_HEAD(psh, entry);
		free(ps);
	}
}

void
free_prefixtree(struct prefixset_tree *p)
{
	struct prefixset_item	*psi, *npsi;

	RB_FOREACH_SAFE(psi, prefixset_tree, p, npsi) {
		RB_REMOVE(prefixset_tree, p, psi);
		free(psi);
	}
}

void
free_roatree(struct roa_tree *r)
{
	struct roa	*roa, *nroa;

	RB_FOREACH_SAFE(roa, roa_tree, r, nroa) {
		RB_REMOVE(roa_tree, r, roa);
		free(roa);
	}
}

void
free_aspa(struct aspa_set *aspa)
{
	if (aspa == NULL)
		return;
	free(aspa->tas);
	free(aspa);
}

void
free_aspatree(struct aspa_tree *a)
{
	struct aspa_set	*aspa, *naspa;

	RB_FOREACH_SAFE(aspa, aspa_tree, a, naspa) {
		RB_REMOVE(aspa_tree, a, aspa);
		free_aspa(aspa);
	}
}

void
free_rtrs(struct rtr_config_head *rh)
{
	struct rtr_config	*r;

	while (!SIMPLEQ_EMPTY(rh)) {
		r = SIMPLEQ_FIRST(rh);
		SIMPLEQ_REMOVE_HEAD(rh, entry);
		free(r);
	}
}

void
free_config(struct bgpd_config *conf)
{
	struct peer		*p, *next;
	struct listen_addr	*la;
	struct mrt		*m;

	free_l3vpns(&conf->l3vpns);
	free_networks(&conf->networks);
	free_flowspecs(&conf->flowspecs);
	filterlist_free(conf->filters);
	free_prefixsets(&conf->prefixsets);
	free_prefixsets(&conf->originsets);
	free_rde_prefixsets(&conf->rde_prefixsets);
	free_rde_prefixsets(&conf->rde_originsets);
	as_sets_free(&conf->as_sets);
	free_roatree(&conf->roa);
	free_aspatree(&conf->aspa);
	free_rtrs(&conf->rtrs);

	while ((la = TAILQ_FIRST(conf->listen_addrs)) != NULL) {
		TAILQ_REMOVE(conf->listen_addrs, la, entry);
		free(la);
	}
	free(conf->listen_addrs);

	while ((m = LIST_FIRST(conf->mrt)) != NULL) {
		LIST_REMOVE(m, entry);
		free(m);
	}
	free(conf->mrt);

	RB_FOREACH_SAFE(p, peer_head, &conf->peers, next) {
		RB_REMOVE(peer_head, &conf->peers, p);
		free(p);
	}

	free(conf->csock);
	free(conf->rcsock);

	free(conf);
}

void
merge_config(struct bgpd_config *xconf, struct bgpd_config *conf)
{
	struct listen_addr	*nla, *ola, *next;
	struct peer		*p, *np, *nextp;
	struct flowspec_config	*f, *nextf, *xf;

	/*
	 * merge the freshly parsed conf into the running xconf
	 */

	/* adjust FIB priority if changed */
	/* if xconf is uninitialized we get RTP_NONE */
	if (xconf->fib_priority != conf->fib_priority) {
		kr_fib_decouple_all();
		kr_fib_prio_set(conf->fib_priority);
		kr_fib_couple_all();
	}

	/* take over the easy config changes */
	copy_config(xconf, conf);

	/* clear old control sockets and use new */
	free(xconf->csock);
	free(xconf->rcsock);
	xconf->csock = conf->csock;
	xconf->rcsock = conf->rcsock;
	/* set old one to NULL so we don't double free */
	conf->csock = NULL;
	conf->rcsock = NULL;

	/* clear all current filters and take over the new ones */
	filterlist_free(xconf->filters);
	xconf->filters = conf->filters;
	conf->filters = NULL;

	/* merge mrt config */
	mrt_mergeconfig(xconf->mrt, conf->mrt);

	/* switch the roa, first remove the old one */
	free_roatree(&xconf->roa);
	/* then move the RB tree root */
	RB_ROOT(&xconf->roa) = RB_ROOT(&conf->roa);
	RB_ROOT(&conf->roa) = NULL;

	/* switch the aspa, first remove the old one */
	free_aspatree(&xconf->aspa);
	/* then move the RB tree root */
	RB_ROOT(&xconf->aspa) = RB_ROOT(&conf->aspa);
	RB_ROOT(&conf->aspa) = NULL;

	/* switch the rtr_configs, first remove the old ones */
	free_rtrs(&xconf->rtrs);
	SIMPLEQ_CONCAT(&xconf->rtrs, &conf->rtrs);

	/* switch the prefixsets, first remove the old ones */
	free_prefixsets(&xconf->prefixsets);
	SIMPLEQ_CONCAT(&xconf->prefixsets, &conf->prefixsets);

	/* switch the originsets, first remove the old ones */
	free_prefixsets(&xconf->originsets);
	SIMPLEQ_CONCAT(&xconf->originsets, &conf->originsets);

	/* switch the as_sets, first remove the old ones */
	as_sets_free(&xconf->as_sets);
	SIMPLEQ_CONCAT(&xconf->as_sets, &conf->as_sets);

	/* switch the network statements, but first remove the old ones */
	free_networks(&xconf->networks);
	TAILQ_CONCAT(&xconf->networks, &conf->networks, entry);

	/*
	 * Merge the flowspec statements. Mark the old ones for deletion
	 * which happens when the flowspec is sent to the RDE.
	 */
	RB_FOREACH(f, flowspec_tree, &xconf->flowspecs)
		f->reconf_action = RECONF_DELETE;

	RB_FOREACH_SAFE(f, flowspec_tree, &conf->flowspecs, nextf) {
		RB_REMOVE(flowspec_tree, &conf->flowspecs, f);

		xf = RB_INSERT(flowspec_tree, &xconf->flowspecs, f);
		if (xf != NULL) {
			filterset_free(&xf->attrset);
			filterset_move(&f->attrset, &xf->attrset);
			flowspec_free(f);
			xf->reconf_action = RECONF_KEEP;
		} else
			f->reconf_action = RECONF_KEEP;
	}

	/* switch the l3vpn configs, first remove the old ones */
	free_l3vpns(&xconf->l3vpns);
	SIMPLEQ_CONCAT(&xconf->l3vpns, &conf->l3vpns);

	/*
	 * merge new listeners:
	 * -flag all existing ones as to be deleted
	 * -those that are in both new and old: flag to keep
	 * -new ones get inserted and flagged as to reinit
	 * -remove all that are still flagged for deletion
	 */

	TAILQ_FOREACH(nla, xconf->listen_addrs, entry)
		nla->reconf = RECONF_DELETE;

	/* no new listeners? preserve default ones */
	if (TAILQ_EMPTY(conf->listen_addrs))
		TAILQ_FOREACH(ola, xconf->listen_addrs, entry)
			if (ola->flags & DEFAULT_LISTENER)
				ola->reconf = RECONF_KEEP;
	/* else loop over listeners and merge configs */
	TAILQ_FOREACH_SAFE(nla, conf->listen_addrs, entry, next) {
		TAILQ_FOREACH(ola, xconf->listen_addrs, entry)
			if (!memcmp(&nla->sa, &ola->sa, sizeof(nla->sa)))
				break;

		if (ola == NULL) {
			/* new listener, copy over */
			TAILQ_REMOVE(conf->listen_addrs, nla, entry);
			TAILQ_INSERT_TAIL(xconf->listen_addrs, nla, entry);
			nla->reconf = RECONF_REINIT;
		} else		/* exists, just flag */
			ola->reconf = RECONF_KEEP;
	}
	/* finally clean up the original list and remove all stale entries */
	TAILQ_FOREACH_SAFE(nla, xconf->listen_addrs, entry, next) {
		if (nla->reconf == RECONF_DELETE) {
			TAILQ_REMOVE(xconf->listen_addrs, nla, entry);
			free(nla);
		}
	}

	/*
	 * merge peers:
	 * - need to know which peers are new, replaced and removed
	 * - walk over old peers and check if there is a corresponding new
	 *   peer if so mark it RECONF_KEEP. Mark all old peers RECONF_DELETE.
	 */
	RB_FOREACH_SAFE(p, peer_head, &xconf->peers, nextp) {
		np = getpeerbyid(conf, p->conf.id);
		if (np != NULL) {
			np->reconf_action = RECONF_KEEP;
			/* keep the auth state since parent needs it */
			np->auth_state = p->auth_state;

			RB_REMOVE(peer_head, &xconf->peers, p);
			free(p);
		} else {
			p->reconf_action = RECONF_DELETE;
		}
	}
	RB_FOREACH_SAFE(np, peer_head, &conf->peers, nextp) {
		RB_REMOVE(peer_head, &conf->peers, np);
		if (RB_INSERT(peer_head, &xconf->peers, np) != NULL)
			fatalx("%s: peer tree is corrupt", __func__);
	}

	/* conf is merged so free it */
	free_config(conf);
}

void
free_deleted_peers(struct bgpd_config *conf)
{
	struct peer *p, *nextp;

	RB_FOREACH_SAFE(p, peer_head, &conf->peers, nextp) {
		if (p->reconf_action == RECONF_DELETE) {
			/* peer no longer exists, clear pfkey state */
			pfkey_remove(&p->auth_state);
			RB_REMOVE(peer_head, &conf->peers, p);
			free(p);
		}
	}
}

uint32_t
get_bgpid(void)
{
	struct ifaddrs		*ifap, *ifa;
	uint32_t		 ip = 0, cur, localnet;

	localnet = INADDR_LOOPBACK & IN_CLASSA_NET;

	if (getifaddrs(&ifap) == -1)
		fatal("getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET)
			continue;
		cur = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
		cur = ntohl(cur);
		if ((cur & localnet) == localnet)	/* skip 127/8 */
			continue;
		if (cur > ip)
			ip = cur;
	}
	freeifaddrs(ifap);

	return (ip);
}

int
host(const char *s, struct bgpd_addr *h, uint8_t *len)
{
	int			 mask = 128;
	char			*p, *ps;
	const char		*errstr;

	if ((ps = strdup(s)) == NULL)
		fatal("%s: strdup", __func__);

	if ((p = strrchr(ps, '/')) != NULL) {
		mask = strtonum(p+1, 0, 128, &errstr);
		if (errstr) {
			log_warnx("prefixlen is %s: %s", errstr, p);
			free(ps);
			return (0);
		}
		p[0] = '\0';
	}

	memset(h, 0, sizeof(*h));

	if (host_ip(ps, h, len) == 0) {
		free(ps);
		return (0);
	}

	if (p != NULL)
		*len = mask;

	free(ps);
	return (1);
}

int
host_ip(const char *s, struct bgpd_addr *h, uint8_t *len)
{
	struct addrinfo		 hints, *res;
	int			 bits;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, NULL, &hints, &res) == 0) {
		*len = res->ai_family == AF_INET6 ? 128 : 32;
		sa2addr(res->ai_addr, h, NULL);
		freeaddrinfo(res);
	} else {	/* ie. for 10/8 parsing */
		if ((bits = inet_net_pton(AF_INET, s, &h->v4,
		    sizeof(h->v4))) == -1)
			return (0);
		*len = bits;
		h->aid = AID_INET;
	}

	return (1);
}

int
prepare_listeners(struct bgpd_config *conf)
{
	struct listen_addr	*la, *next;
	int			 opt = 1;
	int			 r = 0;

	TAILQ_FOREACH_SAFE(la, conf->listen_addrs, entry, next) {
		if (la->reconf != RECONF_REINIT)
			continue;

		if ((la->fd = socket(la->sa.ss_family,
		    SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
		    IPPROTO_TCP)) == -1) {
			if (la->flags & DEFAULT_LISTENER && (errno ==
			    EAFNOSUPPORT || errno == EPROTONOSUPPORT)) {
				TAILQ_REMOVE(conf->listen_addrs, la, entry);
				free(la);
				continue;
			} else
				fatal("socket");
		}

		opt = 1;
		if (setsockopt(la->fd, SOL_SOCKET, SO_REUSEADDR,
		    &opt, sizeof(opt)) == -1)
			fatal("setsockopt SO_REUSEADDR");

		if (bind(la->fd, (struct sockaddr *)&la->sa, la->sa_len) ==
		    -1) {
			switch (la->sa.ss_family) {
			case AF_INET:
				log_warn("cannot bind to %s:%u",
				    log_sockaddr((struct sockaddr *)&la->sa,
				    la->sa_len), ntohs(((struct sockaddr_in *)
				    &la->sa)->sin_port));
				break;
			case AF_INET6:
				log_warn("cannot bind to [%s]:%u",
				    log_sockaddr((struct sockaddr *)&la->sa,
				    la->sa_len), ntohs(((struct sockaddr_in6 *)
				    &la->sa)->sin6_port));
				break;
			default:
				log_warn("cannot bind to %s",
				    log_sockaddr((struct sockaddr *)&la->sa,
				    la->sa_len));
				break;
			}
			close(la->fd);
			TAILQ_REMOVE(conf->listen_addrs, la, entry);
			free(la);
			r = -1;
			continue;
		}
	}

	return (r);
}

void
expand_networks(struct bgpd_config *c, struct network_head *nw)
{
	struct network		*n, *m, *tmp;
	struct prefixset	*ps;
	struct prefixset_item	*psi;

	TAILQ_FOREACH_SAFE(n, nw, entry, tmp) {
		if (n->net.type == NETWORK_PREFIXSET) {
			TAILQ_REMOVE(nw, n, entry);
			if ((ps = find_prefixset(n->net.psname, &c->prefixsets))
			    == NULL)
				fatal("%s: prefixset %s not found", __func__,
				    n->net.psname);
			RB_FOREACH(psi, prefixset_tree, &ps->psitems) {
				if ((m = calloc(1, sizeof(struct network)))
				    == NULL)
					fatal(NULL);
				memcpy(&m->net.prefix, &psi->p.addr,
				    sizeof(m->net.prefix));
				m->net.prefixlen = psi->p.len;
				filterset_copy(&n->net.attrset,
				    &m->net.attrset);
				TAILQ_INSERT_TAIL(nw, m, entry);
			}
			network_free(n);
		}
	}
}

static inline int
prefixset_cmp(struct prefixset_item *a, struct prefixset_item *b)
{
	int i;

	if (a->p.addr.aid < b->p.addr.aid)
		return (-1);
	if (a->p.addr.aid > b->p.addr.aid)
		return (1);

	switch (a->p.addr.aid) {
	case AID_INET:
		i = memcmp(&a->p.addr.v4, &b->p.addr.v4,
		    sizeof(struct in_addr));
		break;
	case AID_INET6:
		i = memcmp(&a->p.addr.v6, &b->p.addr.v6,
		    sizeof(struct in6_addr));
		break;
	default:
		fatalx("%s: unknown af", __func__);
	}
	if (i > 0)
		return (1);
	if (i < 0)
		return (-1);
	if (a->p.len < b->p.len)
		return (-1);
	if (a->p.len > b->p.len)
		return (1);
	if (a->p.len_min < b->p.len_min)
		return (-1);
	if (a->p.len_min > b->p.len_min)
		return (1);
	if (a->p.len_max < b->p.len_max)
		return (-1);
	if (a->p.len_max > b->p.len_max)
		return (1);
	return (0);
}

RB_GENERATE(prefixset_tree, prefixset_item, entry, prefixset_cmp);

static inline int
roa_cmp(struct roa *a, struct roa *b)
{
	int i;

	if (a->aid < b->aid)
		return (-1);
	if (a->aid > b->aid)
		return (1);

	switch (a->aid) {
	case AID_INET:
		i = memcmp(&a->prefix.inet, &b->prefix.inet,
		    sizeof(struct in_addr));
		break;
	case AID_INET6:
		i = memcmp(&a->prefix.inet6, &b->prefix.inet6,
		    sizeof(struct in6_addr));
		break;
	default:
		fatalx("%s: unknown af", __func__);
	}
	if (i > 0)
		return (1);
	if (i < 0)
		return (-1);
	if (a->prefixlen < b->prefixlen)
		return (-1);
	if (a->prefixlen > b->prefixlen)
		return (1);

	if (a->asnum < b->asnum)
		return (-1);
	if (a->asnum > b->asnum)
		return (1);

	if (a->maxlen < b->maxlen)
		return (-1);
	if (a->maxlen > b->maxlen)
		return (1);

	return (0);
}

RB_GENERATE(roa_tree, roa, entry, roa_cmp);

static inline int
aspa_cmp(struct aspa_set *a, struct aspa_set *b)
{
	if (a->as < b->as)
		return (-1);
	if (a->as > b->as)
		return (1);
	return (0);
}

RB_GENERATE(aspa_tree, aspa_set, entry, aspa_cmp);

static inline int
flowspec_config_cmp(struct flowspec_config *a, struct flowspec_config *b)
{
	if (a->flow->aid < b->flow->aid)
		return -1;
	if (a->flow->aid > b->flow->aid)
		return 1;

	return flowspec_cmp(a->flow->data, a->flow->len,
	    b->flow->data, b->flow->len, a->flow->aid == AID_FLOWSPECv6);
}

RB_GENERATE(flowspec_tree, flowspec_config, entry, flowspec_config_cmp);
