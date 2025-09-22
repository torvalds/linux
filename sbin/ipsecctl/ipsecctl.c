/*	$OpenBSD: ipsecctl.c,v 1.88 2024/02/06 05:39:28 yasuoka Exp $	*/
/*
 * Copyright (c) 2004, 2005 Hans-Joerg Hoexer <hshoexer@openbsd.org>
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

#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <net/pfkeyv2.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ipsecctl.h"
#include "pfkey.h"

int		 ipsecctl_rules(char *, int);
FILE		*ipsecctl_fopen(const char *, const char *);
int		 ipsecctl_commit(int, struct ipsecctl *);
int		 ipsecctl_add_rule(struct ipsecctl *, struct ipsec_rule *);
void		 ipsecctl_free_rule(struct ipsec_rule *);
int		 ipsecctl_merge_rules(struct ipsec_rule *, struct ipsec_rule *);
int		 ipsecctl_cmp_ident(struct ipsec_rule *, struct ipsec_rule *);
int		 ipsecctl_rule_matchsrc(struct ipsec_rule *,
		     struct ipsec_addr_wrap *);
int		 ipsecctl_rule_matchdst(struct ipsec_rule *,
		     struct ipsec_addr_wrap *);
void		 ipsecctl_print_addr(struct ipsec_addr_wrap *);
void		 ipsecctl_print_proto(u_int8_t);
void		 ipsecctl_print_port(u_int16_t, const char *);
void		 ipsecctl_print_key(struct ipsec_key *);
void		 ipsecctl_print_flow(struct ipsec_rule *, int);
void		 ipsecctl_print_sa(struct ipsec_rule *, int);
void		 ipsecctl_print_sabundle(struct ipsec_rule *, int);
int		 ipsecctl_flush(int);
char		*ipsecctl_get_rules(struct ipsecctl *, size_t *);
void		 ipsecctl_parse_rules(struct ipsecctl *, char *, size_t);
void		 ipsecctl_print_title(char *);
void		 ipsecctl_show(int);
int		 ipsecctl_monitor(int);
void		 usage(void);
const char	*ipsecctl_lookup_option(char *, const char **);
static int	 unmask(struct ipsec_addr *);
int		 sacompare(const void *, const void *);

const char	*showopt;
char		*isakmpd_fifo = "/var/run/isakmpd.fifo";

int		 first_title = 1;

static const char *showopt_list[] = {
	"flow", "sa", "all", NULL
};

static const char *direction[] = {"?", "in", "out"};
static const char *flowtype[] = {"?", "use", "acquire", "require", "deny",
    "bypass", "dontacq"};
static const char *satype[] = {"?", "esp", "ah", "ipcomp", "tcpmd5", "ipip"};
static const char *tmode[] = {"?", "transport", "tunnel"};
static const char *auth[] = {"?", "psk", "rsa"};

struct sad {
	struct sadb_msg	*sad_msg;
	u_int32_t	 sad_spi;
};

int
sacompare(const void *va, const void *vb)
{
	const struct sad *a = va, *b = vb;

	if (a->sad_spi < b->sad_spi)
		return (-1);
	if (a->sad_spi > b->sad_spi)
		return (1);
	return (0);
}

int
ipsecctl_rules(char *filename, int opts)
{
	struct ipsecctl		 ipsec;
	struct ipsec_rule	*rp;
	int			 action, error = 0;

	bzero(&ipsec, sizeof(ipsec));
	ipsec.opts = opts;
	TAILQ_INIT(&ipsec.rule_queue);
	TAILQ_INIT(&ipsec.bundle_queue);

	if (parse_rules(filename, &ipsec) < 0) {
		warnx("Syntax error in config file: ipsec rules not loaded");
		error = 1;
	} else {
		if (opts & IPSECCTL_OPT_DELETE)
			action = ACTION_DELETE;
		else
			action = ACTION_ADD;

		if ((opts & IPSECCTL_OPT_NOACTION) == 0)
			error = ipsecctl_commit(action, &ipsec);

	}

	/* This also frees the rules in ipsec.bundle_queue. */
	while ((rp = TAILQ_FIRST(&ipsec.rule_queue))) {
		TAILQ_REMOVE(&ipsec.rule_queue, rp, rule_entry);
		ipsecctl_free_rule(rp);
	}

	return (error);
}

FILE *
ipsecctl_fopen(const char *name, const char *mode)
{
	struct stat	 st;
	FILE		*fp;

	fp = fopen(name, mode);
	if (fp == NULL)
		return (NULL);

	if (fstat(fileno(fp), &st)) {
		fclose(fp);
		return (NULL);
	}
	if (S_ISDIR(st.st_mode)) {
		fclose(fp);
		errno = EISDIR;
		return (NULL);
	}
	return (fp);
}

int
ipsecctl_commit(int action, struct ipsecctl *ipsec)
{
	struct ipsec_rule	*rp;
	int			 ret = 0;

	if (pfkey_init() == -1)
		errx(1, "ipsecctl_commit: failed to open PF_KEY socket");

	TAILQ_FOREACH(rp, &ipsec->rule_queue, rule_entry) {
		if (rp->type & RULE_IKE) {
			if (ike_ipsec_establish(action, rp, isakmpd_fifo) ==
			    -1) {
				warnx("failed to %s ike rule %d",
				    action == ACTION_DELETE ? "delete" : "add",
				    rp->nr);
				ret = 2;
			}
		} else {
			if (pfkey_ipsec_establish(action, rp) == -1) {
				warnx("failed to %s rule %d",
				    action == ACTION_DELETE ? "delete" : "add",
				    rp->nr);
				ret = 2;
			}
		}
	}

	return (ret);
}

int
ipsecctl_add_rule(struct ipsecctl *ipsec, struct ipsec_rule *r)
{
	TAILQ_INSERT_TAIL(&ipsec->rule_queue, r, rule_entry);

	if ((ipsec->opts & IPSECCTL_OPT_VERBOSE) && !(ipsec->opts &
	    IPSECCTL_OPT_SHOW))
		ipsecctl_print_rule(r, ipsec->opts);

	return (0);
}

void
ipsecctl_free_rule(struct ipsec_rule *rp)
{
	if (rp->src) {
		free(rp->src->name);
		free(rp->src);
	}
	if (rp->dst) {
		free(rp->dst->name);
		free(rp->dst);
	}
	if (rp->dst2) {
		free(rp->dst2->name);
		free(rp->dst2);
	}
	if (rp->local) {
		free(rp->local->name);
		free(rp->local);
	}
	if (rp->peer) {
		free(rp->peer->name);
		free(rp->peer);
	}
	if (rp->auth) {
		free(rp->auth->srcid);
		free(rp->auth->dstid);
		free(rp->auth);
	}
	if (rp->ikeauth) {
		free(rp->ikeauth->string);
		free(rp->ikeauth);
	}
	free(rp->xfs);
	free(rp->p1xfs);
	free(rp->p2xfs);
	free(rp->p1life);
	free(rp->p2life);
	if (rp->authkey) {
		free(rp->authkey->data);
		free(rp->authkey);
	}
	if (rp->enckey) {
		free(rp->enckey->data);
		free(rp->enckey);
	}
	free(rp->p1name);
	free(rp->p2name);
	free(rp->p2lid);
	free(rp->p2nid);
	free(rp->p2rid);
	free(rp);
}

/*
 * Merge two flow rules if they match.
 *
 * Return 0 if ``from'' has been merged into ``to'', -1 otherwise.
 */
int
ipsecctl_merge_rules(struct ipsec_rule *to, struct ipsec_rule *from)
{
	int match = 0;

	assert((to->type & RULE_FLOW) && (from->type & RULE_FLOW));

	if ((to->satype != from->satype) ||
	    (to->direction != from->direction) ||
	    (to->sport != from->sport) ||
	    (to->dport != from->dport) ||
	    (to->proto != from->proto))
		return (-1);

	if (to->local != NULL || from->local != NULL) {
		if ((to->local == NULL) || (from->local == NULL) ||
		    memcmp(to->local, from->local, sizeof(*to->local)))
			return (-1);
	}

	if (to->peer != NULL || from->peer != NULL) {
		if ((to->peer == NULL) || (from->peer == NULL) ||
		    memcmp(to->peer, from->peer, sizeof(*to->peer)))
			return (-1);
	}

	if (ipsecctl_cmp_ident(to, from))
		return (-1);

	if (ipsecctl_rule_matchsrc(to, from->src)) {
		free(from->src->name);
		free(from->src);
		from->src = NULL;
		match = 1;
	}
	if (ipsecctl_rule_matchdst(to, from->dst)) {
		free(from->dst->name);
		free(from->dst);
		from->dst = NULL;
		match = 1;
	}

	if (!match)
		return (-1);

	TAILQ_INSERT_TAIL(&to->collapsed_rules, from, bundle_entry);

	return (0);
}

/*
 * Return 0 if ``r1'' and ``r2'' IDENTITY match, -1 otherwise.
 */
int
ipsecctl_cmp_ident(struct ipsec_rule *r1, struct ipsec_rule *r2)
{
	if ((r1->auth == NULL) && (r2->auth == NULL))
		return (0) ;

	if ((r1->auth == NULL) || (r2->auth == NULL))
		return (-1);

	if (r1->auth->type != r2->auth->type)
		return (-1);

	if (r1->auth->srcid != NULL) {
		if (r2->auth->srcid == NULL)
			return (-1);

		if (strcmp(r1->auth->srcid, r2->auth->srcid))
			return (-1);
	}

	if (r1->auth->dstid) {
		if (r2->auth->dstid == NULL)
			return (-1);

		if (strcmp(r1->auth->dstid, r2->auth->dstid))
			return (-1);
	}

	return (0);
}


/*
 * Return 0 if ``r'' or its merged entries contain ``src'', -1 otherwise.
 */
int
ipsecctl_rule_matchsrc(struct ipsec_rule *r, struct ipsec_addr_wrap *src)
{
	struct ipsec_rule *r2;

	if (memcmp(r->src, src, sizeof(*r->src)) == 0)
		return (-1);

	TAILQ_FOREACH(r2, &r->collapsed_rules, bundle_entry) {
		if (r2->src == NULL)
			continue;
		if (memcmp(r2->src, src, sizeof(*r->src)) == 0)
			return (-1);
	}

	return (0);
}

/*
 * Return 0 if ``r'' or its merged entries contain ``dst'', -1 otherwise.
 */
int
ipsecctl_rule_matchdst(struct ipsec_rule *r, struct ipsec_addr_wrap *dst)
{
	struct ipsec_rule *r2;

	if (memcmp(r->dst, dst, sizeof(*r->dst)) == 0)
		return (-1);

	TAILQ_FOREACH(r2, &r->collapsed_rules, bundle_entry) {
		if (r2->dst == NULL)
			continue;
		if (memcmp(r2->dst, dst, sizeof(*r->dst)) == 0)
			return (-1);
	}

	return (0);
}

void
ipsecctl_print_addr(struct ipsec_addr_wrap *ipa)
{
	int		bits;
	char		buf[NI_MAXHOST];

	if (ipa == NULL) {
		printf("?");
		return;
	}
	if (inet_ntop(ipa->af, &ipa->address, buf, sizeof(buf)) == NULL)
		printf("?");
	else
		printf("%s", buf);

	bits = unmask(&ipa->mask);
	if (bits != (ipa->af == AF_INET ? 32 : 128))
		printf("/%d", bits);
}

void
ipsecctl_print_proto(u_int8_t proto)
{
	struct protoent *p;

	if ((p = getprotobynumber(proto)) != NULL)
		printf("%s", p->p_name);
	else
		printf("%u", proto);
}

void
ipsecctl_print_port(u_int16_t port, const char *proto)
{
	struct servent *s;

	if ((s = getservbyport(port, proto)) != NULL)
		printf("%s", s->s_name);
	else
		printf("%u", ntohs(port));
}

void
ipsecctl_print_key(struct ipsec_key *key)
{
	int	i;

	for (i = 0; i < (int)key->len; i++)
		printf("%02x", key->data[i]);
}

void
ipsecctl_print_flow(struct ipsec_rule *r, int opts)
{
	struct ipsec_rule *r2;

	printf("flow %s %s", satype[r->satype], direction[r->direction]);

	if (r->proto) {
		printf(" proto ");
		ipsecctl_print_proto(r->proto);
	}
	printf(" from ");
	if (opts & IPSECCTL_OPT_COLLAPSE) {
		printf("{ ");
		ipsecctl_print_addr(r->src);
		TAILQ_FOREACH(r2, &r->collapsed_rules, bundle_entry) {
			if (r2->src == NULL)
				continue;
			printf(", ");
			ipsecctl_print_addr(r2->src);
		}
		printf(" }");
	} else
		ipsecctl_print_addr(r->src);
	if (r->sport) {
		printf(" port ");
		ipsecctl_print_port(r->sport,
		    r->proto == IPPROTO_TCP ? "tcp" : "udp");
	}
	printf(" to ");
	if (opts & IPSECCTL_OPT_COLLAPSE) {
		printf("{ ");
		ipsecctl_print_addr(r->dst);
		TAILQ_FOREACH(r2, &r->collapsed_rules, bundle_entry) {
			if (r2->dst == NULL)
				continue;
			printf(", ");
			ipsecctl_print_addr(r2->dst);
		}
		printf(" }");
	} else
		ipsecctl_print_addr(r->dst);
	if (r->dport) {
		printf(" port ");
		ipsecctl_print_port(r->dport,
		    r->proto == IPPROTO_TCP ? "tcp" : "udp");
	}
	if (r->local) {
		printf(" local ");
		ipsecctl_print_addr(r->local);
	}
	if (r->peer) {
		printf(" peer ");
		ipsecctl_print_addr(r->peer);
	}
	if (r->auth) {
		if (r->auth->srcid)
			printf(" srcid %s", r->auth->srcid);
		if (r->auth->dstid)
			printf(" dstid %s", r->auth->dstid);
		if (r->auth->type > 0)
			printf(" %s", auth[r->auth->type]);
	}
	printf(" type %s", flowtype[r->flowtype]);
	printf("\n");
}

void
ipsecctl_print_sa(struct ipsec_rule *r, int opts)
{
	printf("%s ", satype[r->satype]);
	/* tunnel/transport is only meaningful for esp/ah/ipcomp */
	if (r->satype != IPSEC_TCPMD5 && r->satype != IPSEC_IPIP)
		printf("%s ", tmode[r->tmode]);
	printf("from ");
	ipsecctl_print_addr(r->src);
	printf(" to ");
	ipsecctl_print_addr(r->dst);
	printf(" spi 0x%08x", r->spi);

	if (r->satype != IPSEC_TCPMD5) {
		if (r->xfs && r->xfs->authxf)
			printf(" auth %s", r->xfs->authxf->name);
		if (r->xfs && r->xfs->encxf)
			printf(" enc %s", r->xfs->encxf->name);
		if (r->xfs && r->xfs->compxf)
			printf(" comp %s", r->xfs->compxf->name);
	}
	if (r->authkey && (opts & IPSECCTL_OPT_SHOWKEY)) {
		if (r->satype == IPSEC_TCPMD5)
			printf(" ");
		else
			printf(" \\\n\t");
		printf("authkey 0x");
		ipsecctl_print_key(r->authkey);
	}
	if (r->enckey && (opts & IPSECCTL_OPT_SHOWKEY)) {
		if (r->satype == IPSEC_TCPMD5)
			printf(" ");
		else
			printf(" \\\n\t");
		printf("enckey 0x");
		ipsecctl_print_key(r->enckey);
	}
	printf("\n");
}

void
ipsecctl_print_sabundle(struct ipsec_rule *r, int opts)
{
	printf("[bundle %s to ", satype[r->proto]);
	ipsecctl_print_addr(r->dst);
	printf(" spi 0x%08x with %s to ", r->spi, satype[r->proto2]);
	ipsecctl_print_addr(r->dst2);
	printf(" spi 0x%08x", r->spi2);

	printf("]\n");
}

void
ipsecctl_print_rule(struct ipsec_rule *r, int opts)
{
	struct ipsec_rule *r2;

	if (opts & IPSECCTL_OPT_VERBOSE2) {
		printf("@%d", r->nr);
		if (opts & IPSECCTL_OPT_COLLAPSE) {
			TAILQ_FOREACH(r2, &r->collapsed_rules, bundle_entry) {
				printf(",%d", r2->nr);
			}
		}
		printf(" ");
	}

	if (r->type & RULE_FLOW)
		ipsecctl_print_flow(r, opts);
	if (r->type & RULE_SA)
		ipsecctl_print_sa(r, opts);
	if (r->type & RULE_IKE)
		ike_print_config(r, opts);
	if (r->type & RULE_BUNDLE)
		ipsecctl_print_sabundle(r, opts);
}

int
ipsecctl_flush(int opts)
{
	if (opts & IPSECCTL_OPT_NOACTION)
		return (0);

	if (pfkey_init() == -1)
		errx(1, "ipsecctl_flush: failed to open PF_KEY socket");

	if (pfkey_ipsec_flush() == -1)
		errx(1, "ipsecctl_flush: failed to flush");

	return (0);
}

char *
ipsecctl_get_rules(struct ipsecctl *ipsec, size_t *need)
{
	int		 mib[4];
	char		*buf;

	mib[0] = CTL_NET;
	mib[1] = PF_KEY;
	mib[2] = PF_KEY_V2;
	mib[3] = NET_KEY_SPD_DUMP;

	if (sysctl(mib, 4, NULL, need, NULL, 0) == -1)
		err(1, "ipsecctl_get_rules: sysctl");
	if (*need == 0)
		return NULL;
	if ((buf = malloc(*need)) == NULL)
		err(1, "ipsecctl_get_rules: malloc");
	if (sysctl(mib, 4, buf, need, NULL, 0) == -1)
		err(1, "ipsecctl_get_rules: sysctl");

	return buf;
}

void
ipsecctl_parse_rules(struct ipsecctl *ipsec, char *buf, size_t need)
{
	struct sadb_msg *msg;
	struct ipsec_rule *rule, *last = NULL;
	char		*lim, *next;

	lim = buf + need;
	for (next = buf; next < lim; next += msg->sadb_msg_len *
	    PFKEYV2_CHUNK) {
		msg = (struct sadb_msg *)next;
		if (msg->sadb_msg_len == 0)
			break;

		rule = calloc(1, sizeof(struct ipsec_rule));
		if (rule == NULL)
			err(1, "ipsecctl_parse_rules: calloc");
		rule->nr = ipsec->rule_nr++;
		rule->type |= RULE_FLOW;
		TAILQ_INIT(&rule->collapsed_rules);

		if (pfkey_parse(msg, rule))
			errx(1, "ipsecctl_parse_rules: "
			    "failed to parse PF_KEY message");

		/*
		 * Try to collapse ``rule'' with the last enqueued rule.
		 *
		 * Note that comparing only the last entry works only if
		 * the dump is sorted.
		 */
		if ((ipsec->opts & IPSECCTL_OPT_COLLAPSE) && (last != NULL) &&
		    (ipsecctl_merge_rules(last, rule) == 0))
			continue;

		ipsecctl_add_rule(ipsec, rule);
		last = rule;
	}

	free(buf);
}

void
ipsecctl_print_title(char *title)
{
	if (!first_title)
		printf("\n");
	first_title = 0;
	printf("%s\n", title);
}

void
ipsecctl_show(int opts)
{
	struct ipsecctl ipsec;
	struct ipsec_rule *rp;
	struct sadb_msg *msg;
	struct sad	*sad;
	int		 mib[5], sacount, i;
	size_t		 need = 0, rlen;
	char		*sbuf = NULL, *rbuf = NULL, *lim, *next;

	if (opts & IPSECCTL_OPT_SHOWFLOWS) {
		bzero(&ipsec, sizeof(ipsec));
		ipsec.opts = opts;
		TAILQ_INIT(&ipsec.rule_queue);
		rbuf = ipsecctl_get_rules(&ipsec, &rlen);
	}

	if (opts & IPSECCTL_OPT_SHOWSAS) {
		mib[0] = CTL_NET;
		mib[1] = PF_KEY;
		mib[2] = PF_KEY_V2;
		mib[3] = NET_KEY_SADB_DUMP;
		mib[4] = SADB_SATYPE_UNSPEC;

		/* When the SAD is empty we get ENOENT, no need to err(). */
		if (sysctl(mib, 5, NULL, &need, NULL, 0) == -1 &&
		    errno != ENOENT)
			err(1, "ipsecctl_show: sysctl");
		if (need > 0) {
			if ((sbuf = malloc(need)) == NULL)
				err(1, "ipsecctl_show: malloc");
			if (sysctl(mib, 5, sbuf, &need, NULL, 0) == -1)
				err(1, "ipsecctl_show: sysctl");
		}
	}

	if (pledge("stdio dns", NULL) == -1)
		err(1, "pledge");

	if (rbuf != NULL) {
		ipsecctl_parse_rules(&ipsec, rbuf, rlen);

		if (opts & IPSECCTL_OPT_SHOWALL)
			ipsecctl_print_title("FLOWS:");

		if (TAILQ_FIRST(&ipsec.rule_queue) != NULL) {
			while ((rp = TAILQ_FIRST(&ipsec.rule_queue))) {
				TAILQ_REMOVE(&ipsec.rule_queue, rp, rule_entry);

				ipsecctl_print_rule(rp, ipsec.opts);

				free(rp->src->name);
				free(rp->src);
				free(rp->dst->name);
				free(rp->dst);
				if (rp->local) {
					free(rp->local->name);
					free(rp->local);
				}
				if (rp->peer) {
					free(rp->peer->name);
					free(rp->peer);
				}
				if (rp->auth) {
					free(rp->auth->srcid);
					free(rp->auth->dstid);
					free(rp->auth);
				}
				free(rp);
			}
		}
	} else if (opts & IPSECCTL_OPT_SHOWALL) {
		ipsecctl_print_title("FLOWS:");
		if (opts & IPSECCTL_OPT_SHOWALL)
			printf("No flows\n");
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (sbuf != NULL) {
		if (opts & IPSECCTL_OPT_SHOWALL)
			ipsecctl_print_title("SAD:");

		sacount = 0;
		lim = sbuf + need;
		for (next = sbuf; next < lim;
		    next += msg->sadb_msg_len * PFKEYV2_CHUNK) {
			msg = (struct sadb_msg *)next;
			if (msg->sadb_msg_len == 0)
				break;
			sacount++;
		}
		if ((sad = calloc(sacount, sizeof(*sad))) == NULL)
			err(1, "ipsecctl_show: calloc");
		i = 0;
		for (next = sbuf; next < lim;
		    next += msg->sadb_msg_len * PFKEYV2_CHUNK) {
			msg = (struct sadb_msg *)next;
			if (msg->sadb_msg_len == 0)
				break;
			sad[i].sad_spi = pfkey_get_spi(msg);
			sad[i].sad_msg = msg;
			i++;
		}
		qsort(sad, sacount, sizeof(*sad), sacompare);
		for (i = 0; i < sacount; i++)
			pfkey_print_sa(sad[i].sad_msg, opts);
		free(sad);
		free(sbuf);
	} else if (opts & IPSECCTL_OPT_SHOWALL) {
		ipsecctl_print_title("SAD:");
		printf("No entries\n");
	}
}

int
ipsecctl_monitor(int opts)
{
	return (pfkey_monitor(opts));
}

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-cdFkmnv] [-D macro=value] [-f file]"
	    " [-i fifo] [-s modifier]\n", __progname);
	exit(1);
}

const char *
ipsecctl_lookup_option(char *cmd, const char **list)
{
	if (cmd != NULL && *cmd)
		for (; *list; list++)
			if (!strncmp(cmd, *list, strlen(cmd)))
				return (*list);
	return (NULL);
}

int
main(int argc, char *argv[])
{
	int		 error = 0;
	int		 ch;
	int		 opts = 0;
	char		*rulesopt = NULL;

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "cD:df:Fi:kmnvs:")) != -1) {
		switch (ch) {
		case 'c':
			opts |= IPSECCTL_OPT_COLLAPSE;
			break;

		case 'D':
			if (cmdline_symset(optarg) < 0)
				warnx("could not parse macro definition %s",
				    optarg);
			break;

		case 'd':
			opts |= IPSECCTL_OPT_DELETE;
			break;

		case 'f':
			rulesopt = optarg;
			break;

		case 'F':
			opts |= IPSECCTL_OPT_FLUSH;
			break;

		case 'i':
			isakmpd_fifo = optarg;
			break;

		case 'k':
			opts |= IPSECCTL_OPT_SHOWKEY;
			break;

		case 'm':
			opts |= IPSECCTL_OPT_MONITOR;
			break;

		case 'n':
			opts |= IPSECCTL_OPT_NOACTION;
			break;

		case 'v':
			if (opts & IPSECCTL_OPT_VERBOSE)
				opts |= IPSECCTL_OPT_VERBOSE2;
			opts |= IPSECCTL_OPT_VERBOSE;
			break;

		case 's':
			showopt = ipsecctl_lookup_option(optarg, showopt_list);
			if (showopt == NULL) {
				warnx("Unknown show modifier '%s'", optarg);
				usage();
				/* NOTREACHED */
			}
			opts |= IPSECCTL_OPT_SHOW;
			break;

		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (argc != optind) {
		warnx("unknown command line argument: %s ...", argv[optind]);
		usage();
		/* NOTREACHED */
	}
	if (opts & IPSECCTL_OPT_FLUSH)
		if (ipsecctl_flush(opts))
			error = 1;

	if (rulesopt != NULL)
		if (ipsecctl_rules(rulesopt, opts))
			error = 1;

	if (showopt != NULL) {
		switch (*showopt) {
		case 'f':
			opts |= IPSECCTL_OPT_SHOWFLOWS;
			break;
		case 's':
			opts |= IPSECCTL_OPT_SHOWSAS;
			break;
		case 'a':
			opts |= IPSECCTL_OPT_SHOWFLOWS;
			opts |= IPSECCTL_OPT_SHOWSAS;
			opts |= IPSECCTL_OPT_SHOWALL;
			break;
		}
		ipsecctl_show(opts);
	}

	if (opts & IPSECCTL_OPT_MONITOR)
		if (ipsecctl_monitor(opts))
			error = 1;

	exit(error);
}

static int
unmask(struct ipsec_addr *ipa)
{
	int		i = 31, j = 0, b = 0;
	u_int32_t	tmp;

	while (j < 4 && ipa->addr32[j] == 0xffffffff) {
		b += 32;
		j++;
	}
	if (j < 4) {
		tmp = ntohl(ipa->addr32[j]);
		for (i = 31; tmp & (1 << i); --i)
			b++;
	}
	return (b);
}
