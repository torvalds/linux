/*	$OpenBSD: rde.c,v 1.656 2025/06/04 09:12:34 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2016 Job Snijders <job@instituut.net>
 * Copyright (c) 2016 Peter Hessler <phessler@openbsd.org>
 * Copyright (c) 2018 Sebastian Benoit <benno@openbsd.org>
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
#include <sys/time.h>
#include <sys/resource.h>

#include <errno.h>
#include <pwd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "rde.h"
#include "log.h"

#define PFD_PIPE_MAIN		0
#define PFD_PIPE_SESSION	1
#define PFD_PIPE_SESSION_CTL	2
#define PFD_PIPE_ROA		3
#define PFD_PIPE_COUNT		4

void		 rde_sighdlr(int);
void		 rde_dispatch_imsg_session(struct imsgbuf *);
void		 rde_dispatch_imsg_parent(struct imsgbuf *);
void		 rde_dispatch_imsg_rtr(struct imsgbuf *);
void		 rde_dispatch_imsg_peer(struct rde_peer *, void *);
void		 rde_update_dispatch(struct rde_peer *, struct ibuf *);
int		 rde_update_update(struct rde_peer *, uint32_t,
		    struct filterstate *, struct bgpd_addr *, uint8_t);
void		 rde_update_withdraw(struct rde_peer *, uint32_t,
		    struct bgpd_addr *, uint8_t);
int		 rde_attr_parse(struct ibuf *, struct rde_peer *,
		    struct filterstate *, struct ibuf *, struct ibuf *);
int		 rde_attr_add(struct filterstate *, struct ibuf *);
uint8_t		 rde_attr_missing(struct rde_aspath *, int, uint16_t);
int		 rde_get_mp_nexthop(struct ibuf *, uint8_t,
		    struct rde_peer *, struct filterstate *);
void		 rde_as4byte_fixup(struct rde_peer *, struct rde_aspath *);
uint8_t		 rde_aspa_validity(struct rde_peer *, struct rde_aspath *,
		    uint8_t);
void		 rde_reflector(struct rde_peer *, struct rde_aspath *);

void		 rde_dump_ctx_new(struct ctl_show_rib_request *, pid_t,
		    enum imsg_type);
void		 rde_dump_ctx_throttle(pid_t, int);
void		 rde_dump_ctx_terminate(pid_t);
void		 rde_dump_mrt_new(struct mrt *, pid_t, int);

int		 rde_l3vpn_import(struct rde_community *, struct l3vpn *);
static void	 rde_commit_pftable(void);
void		 rde_reload_done(void);
static void	 rde_softreconfig_in_done(void *, uint8_t);
static void	 rde_softreconfig_out_done(void *, uint8_t);
static void	 rde_softreconfig_done(void);
static void	 rde_softreconfig_out(struct rib_entry *, void *);
static void	 rde_softreconfig_in(struct rib_entry *, void *);
static void	 rde_softreconfig_sync_reeval(struct rib_entry *, void *);
static void	 rde_softreconfig_sync_fib(struct rib_entry *, void *);
static void	 rde_softreconfig_sync_done(void *, uint8_t);
static void	 rde_rpki_reload(void);
static int	 rde_roa_reload(void);
static int	 rde_aspa_reload(void);
int		 rde_update_queue_pending(void);
void		 rde_update_queue_runner(uint8_t);
struct rde_prefixset *rde_find_prefixset(char *, struct rde_prefixset_head *);
void		 rde_mark_prefixsets_dirty(struct rde_prefixset_head *,
		    struct rde_prefixset_head *);
uint8_t		 rde_roa_validity(struct rde_prefixset *,
		    struct bgpd_addr *, uint8_t, uint32_t);

static void	 rde_peer_recv_eor(struct rde_peer *, uint8_t);
static void	 rde_peer_send_eor(struct rde_peer *, uint8_t);

void		 network_add(struct network_config *, struct filterstate *);
void		 network_delete(struct network_config *);
static void	 network_dump_upcall(struct rib_entry *, void *);
static void	 network_flush_upcall(struct rib_entry *, void *);

void		 flowspec_add(struct flowspec *, struct filterstate *,
		    struct filter_set_head *);
void		 flowspec_delete(struct flowspec *);
static void	 flowspec_flush_upcall(struct rib_entry *, void *);
static void	 flowspec_dump_upcall(struct rib_entry *, void *);
static void	 flowspec_dump_done(void *, uint8_t);

void		 rde_shutdown(void);
static int	 ovs_match(struct prefix *, uint32_t);
static int	 avs_match(struct prefix *, uint32_t);

static struct imsgbuf		*ibuf_se;
static struct imsgbuf		*ibuf_se_ctl;
static struct imsgbuf		*ibuf_rtr;
static struct imsgbuf		*ibuf_main;
static struct bgpd_config	*conf, *nconf;
static struct rde_prefixset	 rde_roa, roa_new;
static struct rde_aspa		*rde_aspa, *aspa_new;
static uint8_t			 rde_aspa_generation;

volatile sig_atomic_t	 rde_quit = 0;
struct filter_head	*out_rules, *out_rules_tmp;
struct rde_memstats	 rdemem;
int			 softreconfig;
static int		 rde_eval_all;

extern struct peer_tree	 peertable;
extern struct rde_peer	*peerself;

struct rde_dump_ctx {
	LIST_ENTRY(rde_dump_ctx)	entry;
	struct ctl_show_rib_request	req;
	uint32_t			peerid;
	uint8_t				throttled;
};

LIST_HEAD(, rde_dump_ctx) rde_dump_h = LIST_HEAD_INITIALIZER(rde_dump_h);

struct rde_mrt_ctx {
	LIST_ENTRY(rde_mrt_ctx)	entry;
	struct mrt		mrt;
};

LIST_HEAD(, rde_mrt_ctx) rde_mrts = LIST_HEAD_INITIALIZER(rde_mrts);
u_int rde_mrt_cnt;

void
rde_sighdlr(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		rde_quit = 1;
		break;
	}
}

void
rde_main(int debug, int verbose)
{
	struct passwd		*pw;
	struct pollfd		*pfd = NULL;
	struct rde_mrt_ctx	*mctx, *xmctx;
	void			*newp;
	u_int			 pfd_elms = 0, i, j;
	int			 timeout;
	uint8_t			 aid;

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	log_procinit(log_procnames[PROC_RDE]);

	if ((pw = getpwnam(BGPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("route decision engine");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	signal(SIGTERM, rde_sighdlr);
	signal(SIGINT, rde_sighdlr);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);

	if ((ibuf_main = malloc(sizeof(struct imsgbuf))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(ibuf_main, 3) == -1 ||
	    imsgbuf_set_maxsize(ibuf_main, MAX_BGPD_IMSGSIZE) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(ibuf_main);

	/* initialize the RIB structures */
	if ((out_rules = calloc(1, sizeof(struct filter_head))) == NULL)
		fatal(NULL);
	TAILQ_INIT(out_rules);

	pt_init();
	peer_init(out_rules);

	/* make sure the default RIBs are setup */
	rib_new("Adj-RIB-In", 0, F_RIB_NOFIB | F_RIB_NOEVALUATE);

	conf = new_config();
	log_info("route decision engine ready");

	while (rde_quit == 0) {
		if (pfd_elms < PFD_PIPE_COUNT + rde_mrt_cnt) {
			if ((newp = reallocarray(pfd,
			    PFD_PIPE_COUNT + rde_mrt_cnt,
			    sizeof(struct pollfd))) == NULL) {
				/* panic for now  */
				log_warn("could not resize pfd from %u -> %u"
				    " entries", pfd_elms, PFD_PIPE_COUNT +
				    rde_mrt_cnt);
				fatalx("exiting");
			}
			pfd = newp;
			pfd_elms = PFD_PIPE_COUNT + rde_mrt_cnt;
		}
		timeout = -1;
		memset(pfd, 0, sizeof(struct pollfd) * pfd_elms);

		set_pollfd(&pfd[PFD_PIPE_MAIN], ibuf_main);
		set_pollfd(&pfd[PFD_PIPE_SESSION], ibuf_se);
		set_pollfd(&pfd[PFD_PIPE_SESSION_CTL], ibuf_se_ctl);
		set_pollfd(&pfd[PFD_PIPE_ROA], ibuf_rtr);

		i = PFD_PIPE_COUNT;

		LIST_FOREACH_SAFE(mctx, &rde_mrts, entry, xmctx) {
			if (i >= pfd_elms)
				fatalx("poll pfd too small");
			if (msgbuf_queuelen(mctx->mrt.wbuf) > 0) {
				pfd[i].fd = mctx->mrt.fd;
				pfd[i].events = POLLOUT;
				i++;
			} else if (mctx->mrt.state == MRT_STATE_REMOVE) {
				mrt_clean(&mctx->mrt);
				LIST_REMOVE(mctx, entry);
				free(mctx);
				rde_mrt_cnt--;
			}
		}

		if (peer_work_pending() || rde_update_queue_pending() ||
		    nexthop_pending() || rib_dump_pending())
			timeout = 0;

		if (poll(pfd, i, timeout) == -1) {
			if (errno == EINTR)
				continue;
			fatal("poll error");
		}

		if (handle_pollfd(&pfd[PFD_PIPE_MAIN], ibuf_main) == -1)
			fatalx("Lost connection to parent");
		else
			rde_dispatch_imsg_parent(ibuf_main);

		if (handle_pollfd(&pfd[PFD_PIPE_SESSION], ibuf_se) == -1) {
			log_warnx("RDE: Lost connection to SE");
			imsgbuf_clear(ibuf_se);
			free(ibuf_se);
			ibuf_se = NULL;
		} else
			rde_dispatch_imsg_session(ibuf_se);

		if (handle_pollfd(&pfd[PFD_PIPE_SESSION_CTL], ibuf_se_ctl) ==
		    -1) {
			log_warnx("RDE: Lost connection to SE control");
			imsgbuf_clear(ibuf_se_ctl);
			free(ibuf_se_ctl);
			ibuf_se_ctl = NULL;
		} else
			rde_dispatch_imsg_session(ibuf_se_ctl);

		if (handle_pollfd(&pfd[PFD_PIPE_ROA], ibuf_rtr) == -1) {
			log_warnx("RDE: Lost connection to ROA");
			imsgbuf_clear(ibuf_rtr);
			free(ibuf_rtr);
			ibuf_rtr = NULL;
		} else
			rde_dispatch_imsg_rtr(ibuf_rtr);

		for (j = PFD_PIPE_COUNT, mctx = LIST_FIRST(&rde_mrts);
		    j < i && mctx != NULL; j++) {
			if (pfd[j].fd == mctx->mrt.fd &&
			    pfd[j].revents & POLLOUT)
				mrt_write(&mctx->mrt);
			mctx = LIST_NEXT(mctx, entry);
		}

		peer_foreach(rde_dispatch_imsg_peer, NULL);
		peer_reaper(NULL);
		rib_dump_runner();
		nexthop_runner();
		if (ibuf_se && imsgbuf_queuelen(ibuf_se) < SESS_MSG_HIGH_MARK) {
			for (aid = AID_MIN; aid < AID_MAX; aid++)
				rde_update_queue_runner(aid);
		}
		/* commit pftable once per poll loop */
		rde_commit_pftable();
	}

	/* do not clean up on shutdown on production, it takes ages. */
	if (debug)
		rde_shutdown();

	free_config(conf);
	free(pfd);

	/* close pipes */
	if (ibuf_se) {
		imsgbuf_clear(ibuf_se);
		close(ibuf_se->fd);
		free(ibuf_se);
	}
	if (ibuf_se_ctl) {
		imsgbuf_clear(ibuf_se_ctl);
		close(ibuf_se_ctl->fd);
		free(ibuf_se_ctl);
	}
	if (ibuf_rtr) {
		imsgbuf_clear(ibuf_rtr);
		close(ibuf_rtr->fd);
		free(ibuf_rtr);
	}
	imsgbuf_clear(ibuf_main);
	close(ibuf_main->fd);
	free(ibuf_main);

	while ((mctx = LIST_FIRST(&rde_mrts)) != NULL) {
		mrt_clean(&mctx->mrt);
		LIST_REMOVE(mctx, entry);
		free(mctx);
	}

	log_info("route decision engine exiting");
	exit(0);
}

struct network_config	netconf_s, netconf_p;
struct filterstate	netconf_state;
struct filter_set_head	session_set = TAILQ_HEAD_INITIALIZER(session_set);
struct filter_set_head	parent_set = TAILQ_HEAD_INITIALIZER(parent_set);

void
rde_dispatch_imsg_session(struct imsgbuf *imsgbuf)
{
	static struct flowspec	*curflow;
	struct imsg		 imsg;
	struct ibuf		 ibuf;
	struct rde_peer_stats	 stats;
	struct ctl_show_set	 cset;
	struct ctl_show_rib	 csr;
	struct ctl_show_rib_request	req;
	struct session_up	 sup;
	struct peer_config	 pconf;
	struct rde_peer		*peer;
	struct rde_aspath	*asp;
	struct filter_set	*s;
	struct as_set		*aset;
	struct rde_prefixset	*pset;
	ssize_t			 n;
	uint32_t		 peerid;
	pid_t			 pid;
	int			 verbose;
	uint8_t			 aid;

	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			fatal("rde_dispatch_imsg_session: imsg_get error");
		if (n == 0)
			break;

		peerid = imsg_get_id(&imsg);
		pid = imsg_get_pid(&imsg);
		switch (imsg_get_type(&imsg)) {
		case IMSG_UPDATE:
		case IMSG_REFRESH:
			if ((peer = peer_get(peerid)) == NULL) {
				log_warnx("rde_dispatch: unknown peer id %d",
				    peerid);
				break;
			}
			if (peer_is_up(peer))
				peer_imsg_push(peer, &imsg);
			break;
		case IMSG_SESSION_ADD:
			if (imsg_get_data(&imsg, &pconf, sizeof(pconf)) == -1)
				fatalx("incorrect size of session request");
			peer = peer_add(peerid, &pconf, out_rules);
			/* make sure rde_eval_all is on if needed. */
			if (peer->conf.flags & PEERFLAG_EVALUATE_ALL)
				rde_eval_all = 1;
			break;
		case IMSG_SESSION_UP:
			if ((peer = peer_get(peerid)) == NULL) {
				log_warnx("%s: unknown peer id %d",
				    "IMSG_SESSION_UP", peerid);
				break;
			}
			if (imsg_get_data(&imsg, &sup, sizeof(sup)) == -1)
				fatalx("incorrect size of session request");
			peer_up(peer, &sup);
			/* make sure rde_eval_all is on if needed. */
			if (peer_has_add_path(peer, AID_UNSPEC, CAPA_AP_SEND))
				rde_eval_all = 1;
			break;
		case IMSG_SESSION_DOWN:
			if ((peer = peer_get(peerid)) == NULL) {
				log_warnx("%s: unknown peer id %d",
				    "IMSG_SESSION_DOWN", peerid);
				break;
			}
			peer_down(peer);
			break;
		case IMSG_SESSION_DELETE:
			/* silently ignore deletes for unknown peers */
			if ((peer = peer_get(peerid)) == NULL)
				break;
			peer_delete(peer);
			break;
		case IMSG_SESSION_STALE:
		case IMSG_SESSION_NOGRACE:
		case IMSG_SESSION_FLUSH:
		case IMSG_SESSION_RESTARTED:
			if ((peer = peer_get(peerid)) == NULL) {
				log_warnx("%s: unknown peer id %d",
				    "graceful restart", peerid);
				break;
			}
			if (imsg_get_data(&imsg, &aid, sizeof(aid)) == -1) {
				log_warnx("%s: wrong imsg len", __func__);
				break;
			}
			if (aid < AID_MIN || aid >= AID_MAX) {
				log_warnx("%s: bad AID", __func__);
				break;
			}

			switch (imsg_get_type(&imsg)) {
			case IMSG_SESSION_STALE:
				peer_stale(peer, aid, 0);
				break;
			case IMSG_SESSION_NOGRACE:
				peer_stale(peer, aid, 1);
				break;
			case IMSG_SESSION_FLUSH:
				peer_flush(peer, aid, peer->staletime[aid]);
				break;
			case IMSG_SESSION_RESTARTED:
				if (monotime_valid(peer->staletime[aid]))
					peer_flush(peer, aid,
					    peer->staletime[aid]);
				break;
			}
			break;
		case IMSG_NETWORK_ADD:
			if (imsg_get_data(&imsg, &netconf_s,
			    sizeof(netconf_s)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			TAILQ_INIT(&netconf_s.attrset);
			rde_filterstate_init(&netconf_state);
			asp = &netconf_state.aspath;
			asp->aspath = aspath_get(NULL, 0);
			asp->origin = ORIGIN_IGP;
			asp->flags = F_ATTR_ORIGIN | F_ATTR_ASPATH |
			    F_ATTR_LOCALPREF | F_PREFIX_ANNOUNCED |
			    F_ANN_DYNAMIC;
			break;
		case IMSG_NETWORK_ASPATH:
			if (imsg_get_ibuf(&imsg, &ibuf) == -1) {
				log_warnx("rde_dispatch: bad imsg");
				memset(&netconf_s, 0, sizeof(netconf_s));
				break;
			}
			if (ibuf_get(&ibuf, &csr, sizeof(csr)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				memset(&netconf_s, 0, sizeof(netconf_s));
				break;
			}
			asp = &netconf_state.aspath;
			asp->lpref = csr.local_pref;
			asp->med = csr.med;
			asp->weight = csr.weight;
			asp->flags = csr.flags;
			asp->origin = csr.origin;
			asp->flags |= F_PREFIX_ANNOUNCED | F_ANN_DYNAMIC;
			aspath_put(asp->aspath);
			asp->aspath = aspath_get(ibuf_data(&ibuf),
			    ibuf_size(&ibuf));
			break;
		case IMSG_NETWORK_ATTR:
			/* parse optional path attributes */
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    rde_attr_add(&netconf_state, &ibuf) == -1) {
				log_warnx("rde_dispatch: bad network "
				    "attribute");
				rde_filterstate_clean(&netconf_state);
				memset(&netconf_s, 0, sizeof(netconf_s));
				break;
			}
			break;
		case IMSG_NETWORK_DONE:
			TAILQ_CONCAT(&netconf_s.attrset, &session_set, entry);
			switch (netconf_s.prefix.aid) {
			case AID_INET:
				if (netconf_s.prefixlen > 32)
					goto badnet;
				network_add(&netconf_s, &netconf_state);
				break;
			case AID_INET6:
				if (netconf_s.prefixlen > 128)
					goto badnet;
				network_add(&netconf_s, &netconf_state);
				break;
			case 0:
				/* something failed beforehand */
				break;
			default:
badnet:
				log_warnx("request to insert invalid network");
				break;
			}
			rde_filterstate_clean(&netconf_state);
			break;
		case IMSG_NETWORK_REMOVE:
			if (imsg_get_data(&imsg, &netconf_s,
			    sizeof(netconf_s)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			TAILQ_INIT(&netconf_s.attrset);

			switch (netconf_s.prefix.aid) {
			case AID_INET:
				if (netconf_s.prefixlen > 32)
					goto badnetdel;
				network_delete(&netconf_s);
				break;
			case AID_INET6:
				if (netconf_s.prefixlen > 128)
					goto badnetdel;
				network_delete(&netconf_s);
				break;
			default:
badnetdel:
				log_warnx("request to remove invalid network");
				break;
			}
			break;
		case IMSG_NETWORK_FLUSH:
			if (rib_dump_new(RIB_ADJ_IN, AID_UNSPEC,
			    RDE_RUNNER_ROUNDS, NULL, network_flush_upcall,
			    NULL, NULL) == -1)
				log_warn("rde_dispatch: IMSG_NETWORK_FLUSH");
			break;
		case IMSG_FLOWSPEC_ADD:
			if (curflow != NULL) {
				log_warnx("rde_dispatch: "
				    "unexpected flowspec add");
				break;
			}
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_size(&ibuf) <= FLOWSPEC_SIZE) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			curflow = malloc(ibuf_size(&ibuf));
			if (curflow == NULL)
				fatal(NULL);
			memcpy(curflow, ibuf_data(&ibuf), ibuf_size(&ibuf));
			if (curflow->len + FLOWSPEC_SIZE != ibuf_size(&ibuf)) {
				free(curflow);
				curflow = NULL;
				log_warnx("rde_dispatch: wrong flowspec len");
				break;
			}
			rde_filterstate_init(&netconf_state);
			asp = &netconf_state.aspath;
			asp->aspath = aspath_get(NULL, 0);
			asp->origin = ORIGIN_IGP;
			asp->flags = F_ATTR_ORIGIN | F_ATTR_ASPATH |
			    F_ATTR_LOCALPREF | F_PREFIX_ANNOUNCED |
			    F_ANN_DYNAMIC;
			break;
		case IMSG_FLOWSPEC_DONE:
			if (curflow == NULL) {
				log_warnx("rde_dispatch: "
				    "unexpected flowspec done");
				break;
			}

			if (flowspec_valid(curflow->data, curflow->len,
			    curflow->aid == AID_FLOWSPECv6) == -1)
				log_warnx("invalid flowspec update received "
				    "from bgpctl");
			else
				flowspec_add(curflow, &netconf_state,
				    &session_set);

			rde_filterstate_clean(&netconf_state);
			filterset_free(&session_set);
			free(curflow);
			curflow = NULL;
			break;
		case IMSG_FLOWSPEC_REMOVE:
			if (curflow != NULL) {
				log_warnx("rde_dispatch: "
				    "unexpected flowspec remove");
				break;
			}
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_size(&ibuf) <= FLOWSPEC_SIZE) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			curflow = malloc(ibuf_size(&ibuf));
			if (curflow == NULL)
				fatal(NULL);
			memcpy(curflow, ibuf_data(&ibuf), ibuf_size(&ibuf));
			if (curflow->len + FLOWSPEC_SIZE != ibuf_size(&ibuf)) {
				free(curflow);
				curflow = NULL;
				log_warnx("rde_dispatch: wrong flowspec len");
				break;
			}

			if (flowspec_valid(curflow->data, curflow->len,
			    curflow->aid == AID_FLOWSPECv6) == -1)
				log_warnx("invalid flowspec withdraw received "
				    "from bgpctl");
			else
				flowspec_delete(curflow);

			free(curflow);
			curflow = NULL;
			break;
		case IMSG_FLOWSPEC_FLUSH:
			prefix_flowspec_dump(AID_UNSPEC, NULL,
			    flowspec_flush_upcall, NULL);
			break;
		case IMSG_FILTER_SET:
			if ((s = malloc(sizeof(struct filter_set))) == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, s, sizeof(struct filter_set))
			    == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				free(s);
				break;
			}
			if (s->type == ACTION_SET_NEXTHOP) {
				s->action.nh_ref =
				    nexthop_get(&s->action.nexthop);
				s->type = ACTION_SET_NEXTHOP_REF;
			}
			TAILQ_INSERT_TAIL(&session_set, s, entry);
			break;
		case IMSG_CTL_SHOW_NETWORK:
		case IMSG_CTL_SHOW_RIB:
		case IMSG_CTL_SHOW_RIB_PREFIX:
			if (imsg_get_data(&imsg, &req, sizeof(req)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			rde_dump_ctx_new(&req, pid, imsg_get_type(&imsg));
			break;
		case IMSG_CTL_SHOW_FLOWSPEC:
			if (imsg_get_data(&imsg, &req, sizeof(req)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			prefix_flowspec_dump(req.aid, &pid,
			    flowspec_dump_upcall, flowspec_dump_done);
			break;
		case IMSG_CTL_SHOW_NEIGHBOR:
			peer = peer_get(peerid);
			if (peer != NULL)
				memcpy(&stats, &peer->stats, sizeof(stats));
			else
				memset(&stats, 0, sizeof(stats));
			imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_NEIGHBOR,
			    peerid, pid, -1, &stats, sizeof(stats));
			break;
		case IMSG_CTL_SHOW_RIB_MEM:
			imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_RIB_MEM, 0,
			    pid, -1, &rdemem, sizeof(rdemem));
			break;
		case IMSG_CTL_SHOW_SET:
			/* first roa set */
			pset = &rde_roa;
			memset(&cset, 0, sizeof(cset));
			cset.type = ROA_SET;
			strlcpy(cset.name, "RPKI ROA", sizeof(cset.name));
			cset.lastchange = pset->lastchange;
			cset.v4_cnt = pset->th.v4_cnt;
			cset.v6_cnt = pset->th.v6_cnt;
			imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_SET, 0,
			    pid, -1, &cset, sizeof(cset));

			/* then aspa set */
			memset(&cset, 0, sizeof(cset));
			cset.type = ASPA_SET;
			strlcpy(cset.name, "RPKI ASPA", sizeof(cset.name));
			aspa_table_stats(rde_aspa, &cset);
			imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_SET, 0,
			    pid, -1, &cset, sizeof(cset));

			SIMPLEQ_FOREACH(aset, &conf->as_sets, entry) {
				memset(&cset, 0, sizeof(cset));
				cset.type = ASNUM_SET;
				strlcpy(cset.name, aset->name,
				    sizeof(cset.name));
				cset.lastchange = aset->lastchange;
				cset.as_cnt = set_nmemb(aset->set);
				imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_SET, 0,
				    pid, -1, &cset, sizeof(cset));
			}
			SIMPLEQ_FOREACH(pset, &conf->rde_prefixsets, entry) {
				memset(&cset, 0, sizeof(cset));
				cset.type = PREFIX_SET;
				strlcpy(cset.name, pset->name,
				    sizeof(cset.name));
				cset.lastchange = pset->lastchange;
				cset.v4_cnt = pset->th.v4_cnt;
				cset.v6_cnt = pset->th.v6_cnt;
				imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_SET, 0,
				    pid, -1, &cset, sizeof(cset));
			}
			SIMPLEQ_FOREACH(pset, &conf->rde_originsets, entry) {
				memset(&cset, 0, sizeof(cset));
				cset.type = ORIGIN_SET;
				strlcpy(cset.name, pset->name,
				    sizeof(cset.name));
				cset.lastchange = pset->lastchange;
				cset.v4_cnt = pset->th.v4_cnt;
				cset.v6_cnt = pset->th.v6_cnt;
				imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_SET, 0,
				    pid, -1, &cset, sizeof(cset));
			}
			imsg_compose(ibuf_se_ctl, IMSG_CTL_END, 0, pid,
			    -1, NULL, 0);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by SE */
			if (imsg_get_data(&imsg, &verbose, sizeof(verbose)) ==
			    -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			log_setverbose(verbose);
			break;
		case IMSG_CTL_END:
			imsg_compose(ibuf_se_ctl, IMSG_CTL_END, 0, pid,
			    -1, NULL, 0);
			break;
		case IMSG_CTL_TERMINATE:
			rde_dump_ctx_terminate(pid);
			break;
		case IMSG_XON:
			if (peerid) {
				peer = peer_get(peerid);
				if (peer)
					peer->throttled = 0;
			} else {
				rde_dump_ctx_throttle(pid, 0);
			}
			break;
		case IMSG_XOFF:
			if (peerid) {
				peer = peer_get(peerid);
				if (peer)
					peer->throttled = 1;
			} else {
				rde_dump_ctx_throttle(pid, 1);
			}
			break;
		case IMSG_RECONF_DRAIN:
			imsg_compose(ibuf_se, IMSG_RECONF_DRAIN, 0, 0,
			    -1, NULL, 0);
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}
}

void
rde_dispatch_imsg_parent(struct imsgbuf *imsgbuf)
{
	static struct rde_prefixset	*last_prefixset;
	static struct as_set	*last_as_set;
	static struct l3vpn	*vpn;
	static struct flowspec	*curflow;
	struct imsg		 imsg;
	struct ibuf		 ibuf;
	struct bgpd_config	 tconf;
	struct filterstate	 state;
	struct kroute_nexthop	 knext;
	struct mrt		 xmrt;
	struct prefixset_item	 psi;
	struct rde_rib		 rr;
	struct roa		 roa;
	char			 name[SET_NAME_LEN];
	struct imsgbuf		*i;
	struct filter_head	*nr;
	struct filter_rule	*r;
	struct filter_set	*s;
	struct rib		*rib;
	struct rde_prefixset	*ps;
	struct rde_aspath	*asp;
	size_t			 nmemb;
	int			 n, fd, rv;
	uint16_t		 rid;

	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			fatal("rde_dispatch_imsg_parent: imsg_get error");
		if (n == 0)
			break;

		switch (imsg_get_type(&imsg)) {
		case IMSG_SOCKET_CONN:
		case IMSG_SOCKET_CONN_CTL:
		case IMSG_SOCKET_CONN_RTR:
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("expected to receive imsg fd "
				    "but didn't receive any");
				break;
			}
			if ((i = malloc(sizeof(struct imsgbuf))) == NULL)
				fatal(NULL);
			if (imsgbuf_init(i, fd) == -1 ||
			    imsgbuf_set_maxsize(i, MAX_BGPD_IMSGSIZE) == -1)
				fatal(NULL);
			switch (imsg_get_type(&imsg)) {
			case IMSG_SOCKET_CONN:
				if (ibuf_se) {
					log_warnx("Unexpected imsg connection "
					    "to SE received");
					imsgbuf_clear(ibuf_se);
					free(ibuf_se);
				}
				ibuf_se = i;
				break;
			case IMSG_SOCKET_CONN_CTL:
				if (ibuf_se_ctl) {
					log_warnx("Unexpected imsg ctl "
					    "connection to SE received");
					imsgbuf_clear(ibuf_se_ctl);
					free(ibuf_se_ctl);
				}
				ibuf_se_ctl = i;
				break;
			case IMSG_SOCKET_CONN_RTR:
				if (ibuf_rtr) {
					log_warnx("Unexpected imsg ctl "
					    "connection to ROA received");
					imsgbuf_clear(ibuf_rtr);
					free(ibuf_rtr);
				}
				ibuf_rtr = i;
				break;
			}
			break;
		case IMSG_NETWORK_ADD:
			if (imsg_get_data(&imsg, &netconf_p,
			    sizeof(netconf_p)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			TAILQ_INIT(&netconf_p.attrset);
			break;
		case IMSG_NETWORK_DONE:
			TAILQ_CONCAT(&netconf_p.attrset, &parent_set, entry);

			rde_filterstate_init(&state);
			asp = &state.aspath;
			asp->aspath = aspath_get(NULL, 0);
			asp->origin = ORIGIN_IGP;
			asp->flags = F_ATTR_ORIGIN | F_ATTR_ASPATH |
			    F_ATTR_LOCALPREF | F_PREFIX_ANNOUNCED;

			network_add(&netconf_p, &state);
			rde_filterstate_clean(&state);
			break;
		case IMSG_NETWORK_REMOVE:
			if (imsg_get_data(&imsg, &netconf_p,
			    sizeof(netconf_p)) == -1) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			TAILQ_INIT(&netconf_p.attrset);
			network_delete(&netconf_p);
			break;
		case IMSG_FLOWSPEC_ADD:
			if (curflow != NULL) {
				log_warnx("rde_dispatch: "
				    "unexpected flowspec add");
				break;
			}
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_size(&ibuf) <= FLOWSPEC_SIZE) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			curflow = malloc(ibuf_size(&ibuf));
			if (curflow == NULL)
				fatal(NULL);
			memcpy(curflow, ibuf_data(&ibuf), ibuf_size(&ibuf));
			if (curflow->len + FLOWSPEC_SIZE != ibuf_size(&ibuf)) {
				free(curflow);
				curflow = NULL;
				log_warnx("rde_dispatch: wrong flowspec len");
				break;
			}
			break;
		case IMSG_FLOWSPEC_DONE:
			if (curflow == NULL) {
				log_warnx("rde_dispatch: "
				    "unexpected flowspec done");
				break;
			}

			rde_filterstate_init(&state);
			asp = &state.aspath;
			asp->aspath = aspath_get(NULL, 0);
			asp->origin = ORIGIN_IGP;
			asp->flags = F_ATTR_ORIGIN | F_ATTR_ASPATH |
			    F_ATTR_LOCALPREF | F_PREFIX_ANNOUNCED;

			if (flowspec_valid(curflow->data, curflow->len,
			    curflow->aid == AID_FLOWSPECv6) == -1)
				log_warnx("invalid flowspec update received "
				    "from parent");
			else
				flowspec_add(curflow, &state, &parent_set);

			rde_filterstate_clean(&state);
			filterset_free(&parent_set);
			free(curflow);
			curflow = NULL;
			break;
		case IMSG_FLOWSPEC_REMOVE:
			if (curflow != NULL) {
				log_warnx("rde_dispatch: "
				    "unexpected flowspec remove");
				break;
			}
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_size(&ibuf) <= FLOWSPEC_SIZE) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			curflow = malloc(ibuf_size(&ibuf));
			if (curflow == NULL)
				fatal(NULL);
			memcpy(curflow, ibuf_data(&ibuf), ibuf_size(&ibuf));
			if (curflow->len + FLOWSPEC_SIZE != ibuf_size(&ibuf)) {
				free(curflow);
				curflow = NULL;
				log_warnx("rde_dispatch: wrong flowspec len");
				break;
			}

			if (flowspec_valid(curflow->data, curflow->len,
			    curflow->aid == AID_FLOWSPECv6) == -1)
				log_warnx("invalid flowspec withdraw received "
				    "from parent");
			else
				flowspec_delete(curflow);

			free(curflow);
			curflow = NULL;
			break;
		case IMSG_RECONF_CONF:
			if (imsg_get_data(&imsg, &tconf, sizeof(tconf)) == -1)
				fatalx("IMSG_RECONF_CONF bad len");
			out_rules_tmp = calloc(1, sizeof(struct filter_head));
			if (out_rules_tmp == NULL)
				fatal(NULL);
			TAILQ_INIT(out_rules_tmp);
			nconf = new_config();
			copy_config(nconf, &tconf);

			for (rid = 0; rid < rib_size; rid++) {
				if ((rib = rib_byid(rid)) == NULL)
					continue;
				rib->state = RECONF_DELETE;
				rib->fibstate = RECONF_NONE;
			}
			break;
		case IMSG_RECONF_RIB:
			if (imsg_get_data(&imsg, &rr, sizeof(rr)) == -1)
				fatalx("IMSG_RECONF_RIB bad len");
			rib = rib_byid(rib_find(rr.name));
			if (rib == NULL) {
				rib = rib_new(rr.name, rr.rtableid, rr.flags);
			} else if (rib->flags == rr.flags &&
			    rib->rtableid == rr.rtableid) {
				/* no change to rib apart from filters */
				rib->state = RECONF_KEEP;
			} else {
				/* reload rib because something changed */
				rib->flags_tmp = rr.flags;
				rib->rtableid_tmp = rr.rtableid;
				rib->state = RECONF_RELOAD;
			}
			break;
		case IMSG_RECONF_FILTER:
			if ((r = malloc(sizeof(struct filter_rule))) == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, r, sizeof(*r)) == -1)
				fatalx("IMSG_RECONF_FILTER bad len");
			if (r->match.prefixset.name[0] != '\0') {
				r->match.prefixset.ps =
				    rde_find_prefixset(r->match.prefixset.name,
					&nconf->rde_prefixsets);
				if (r->match.prefixset.ps == NULL)
					log_warnx("%s: no prefixset for %s",
					    __func__, r->match.prefixset.name);
			}
			if (r->match.originset.name[0] != '\0') {
				r->match.originset.ps =
				    rde_find_prefixset(r->match.originset.name,
					&nconf->rde_originsets);
				if (r->match.originset.ps == NULL)
					log_warnx("%s: no origin-set for %s",
					    __func__, r->match.originset.name);
			}
			if (r->match.as.flags & AS_FLAG_AS_SET_NAME) {
				struct as_set * aset;

				aset = as_sets_lookup(&nconf->as_sets,
				    r->match.as.name);
				if (aset == NULL) {
					log_warnx("%s: no as-set for %s",
					    __func__, r->match.as.name);
				} else {
					r->match.as.flags = AS_FLAG_AS_SET;
					r->match.as.aset = aset;
				}
			}
			TAILQ_INIT(&r->set);
			TAILQ_CONCAT(&r->set, &parent_set, entry);
			if ((rib = rib_byid(rib_find(r->rib))) == NULL) {
				log_warnx("IMSG_RECONF_FILTER: filter rule "
				    "for nonexistent rib %s", r->rib);
				filterset_free(&r->set);
				free(r);
				break;
			}
			r->peer.ribid = rib->id;
			if (r->dir == DIR_IN) {
				nr = rib->in_rules_tmp;
				if (nr == NULL) {
					nr = calloc(1,
					    sizeof(struct filter_head));
					if (nr == NULL)
						fatal(NULL);
					TAILQ_INIT(nr);
					rib->in_rules_tmp = nr;
				}
				TAILQ_INSERT_TAIL(nr, r, entry);
			} else {
				TAILQ_INSERT_TAIL(out_rules_tmp, r, entry);
			}
			break;
		case IMSG_RECONF_PREFIX_SET:
		case IMSG_RECONF_ORIGIN_SET:
			ps = calloc(1, sizeof(struct rde_prefixset));
			if (ps == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, ps->name, sizeof(ps->name)) ==
			    -1)
				fatalx("IMSG_RECONF_PREFIX_SET bad len");
			if (imsg_get_type(&imsg) == IMSG_RECONF_ORIGIN_SET) {
				SIMPLEQ_INSERT_TAIL(&nconf->rde_originsets, ps,
				    entry);
			} else {
				SIMPLEQ_INSERT_TAIL(&nconf->rde_prefixsets, ps,
				    entry);
			}
			last_prefixset = ps;
			break;
		case IMSG_RECONF_ROA_ITEM:
			if (imsg_get_data(&imsg, &roa, sizeof(roa)) == -1)
				fatalx("IMSG_RECONF_ROA_ITEM bad len");
			rv = trie_roa_add(&last_prefixset->th, &roa);
			break;
		case IMSG_RECONF_PREFIX_SET_ITEM:
			if (imsg_get_data(&imsg, &psi, sizeof(psi)) == -1)
				fatalx("IMSG_RECONF_PREFIX_SET_ITEM bad len");
			if (last_prefixset == NULL)
				fatalx("King Bula has no prefixset");
			rv = trie_add(&last_prefixset->th,
			    &psi.p.addr, psi.p.len,
			    psi.p.len_min, psi.p.len_max);
			if (rv == -1)
				log_warnx("trie_add(%s) %s/%u failed",
				    last_prefixset->name, log_addr(&psi.p.addr),
				    psi.p.len);
			break;
		case IMSG_RECONF_AS_SET:
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_get(&ibuf, &nmemb, sizeof(nmemb)) == -1 ||
			    ibuf_get(&ibuf, name, sizeof(name)) == -1)
				fatalx("IMSG_RECONF_AS_SET bad len");
			if (as_sets_lookup(&nconf->as_sets, name) != NULL)
				fatalx("duplicate as-set %s", name);
			last_as_set = as_sets_new(&nconf->as_sets, name, nmemb,
			    sizeof(uint32_t));
			break;
		case IMSG_RECONF_AS_SET_ITEMS:
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_size(&ibuf) == 0 ||
			    ibuf_size(&ibuf) % sizeof(uint32_t) != 0)
				fatalx("IMSG_RECONF_AS_SET_ITEMS bad len");
			nmemb = ibuf_size(&ibuf) / sizeof(uint32_t);
			if (set_add(last_as_set->set, ibuf_data(&ibuf),
			    nmemb) != 0)
				fatal(NULL);
			break;
		case IMSG_RECONF_AS_SET_DONE:
			set_prep(last_as_set->set);
			last_as_set = NULL;
			break;
		case IMSG_RECONF_VPN:
			if ((vpn = malloc(sizeof(*vpn))) == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, vpn, sizeof(*vpn)) == -1)
				fatalx("IMSG_RECONF_VPN bad len");
			TAILQ_INIT(&vpn->import);
			TAILQ_INIT(&vpn->export);
			TAILQ_INIT(&vpn->net_l);
			SIMPLEQ_INSERT_TAIL(&nconf->l3vpns, vpn, entry);
			break;
		case IMSG_RECONF_VPN_EXPORT:
			if (vpn == NULL) {
				log_warnx("rde_dispatch_imsg_parent: "
				    "IMSG_RECONF_VPN_EXPORT unexpected");
				break;
			}
			TAILQ_CONCAT(&vpn->export, &parent_set, entry);
			break;
		case IMSG_RECONF_VPN_IMPORT:
			if (vpn == NULL) {
				log_warnx("rde_dispatch_imsg_parent: "
				    "IMSG_RECONF_VPN_IMPORT unexpected");
				break;
			}
			TAILQ_CONCAT(&vpn->import, &parent_set, entry);
			break;
		case IMSG_RECONF_VPN_DONE:
			break;
		case IMSG_RECONF_DRAIN:
			imsg_compose(ibuf_main, IMSG_RECONF_DRAIN, 0, 0,
			    -1, NULL, 0);
			break;
		case IMSG_RECONF_DONE:
			if (nconf == NULL)
				fatalx("got IMSG_RECONF_DONE but no config");
			last_prefixset = NULL;

			rde_reload_done();
			break;
		case IMSG_NEXTHOP_UPDATE:
			if (imsg_get_data(&imsg, &knext, sizeof(knext)) == -1)
				fatalx("IMSG_NEXTHOP_UPDATE bad len");
			nexthop_update(&knext);
			break;
		case IMSG_FILTER_SET:
			if ((s = malloc(sizeof(*s))) == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, s, sizeof(*s)) == -1)
				fatalx("IMSG_FILTER_SET bad len");
			if (s->type == ACTION_SET_NEXTHOP) {
				s->action.nh_ref =
				    nexthop_get(&s->action.nexthop);
				s->type = ACTION_SET_NEXTHOP_REF;
			}
			TAILQ_INSERT_TAIL(&parent_set, s, entry);
			break;
		case IMSG_MRT_OPEN:
		case IMSG_MRT_REOPEN:
			if (imsg_get_data(&imsg, &xmrt, sizeof(xmrt)) == -1) {
				log_warnx("wrong imsg len");
				break;
			}
			if ((fd = imsg_get_fd(&imsg)) == -1)
				log_warnx("expected to receive fd for mrt dump "
				    "but didn't receive any");
			else if (xmrt.type == MRT_TABLE_DUMP ||
			    xmrt.type == MRT_TABLE_DUMP_MP ||
			    xmrt.type == MRT_TABLE_DUMP_V2) {
				rde_dump_mrt_new(&xmrt, imsg_get_pid(&imsg),
				    fd);
			} else
				close(fd);
			break;
		case IMSG_MRT_CLOSE:
			/* ignore end message because a dump is atomic */
			break;
		default:
			fatalx("unhandled IMSG %u", imsg_get_type(&imsg));
		}
		imsg_free(&imsg);
	}
}

void
rde_dispatch_imsg_rtr(struct imsgbuf *imsgbuf)
{
	static struct aspa_set	*aspa;
	struct imsg		 imsg;
	struct roa		 roa;
	struct aspa_prep	 ap;
	int			 n;

	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			fatal("rde_dispatch_imsg_parent: imsg_get error");
		if (n == 0)
			break;

		switch (imsg_get_type(&imsg)) {
		case IMSG_RECONF_ROA_SET:
			/* start of update */
			trie_free(&roa_new.th);	/* clear new roa */
			break;
		case IMSG_RECONF_ROA_ITEM:
			if (imsg_get_data(&imsg, &roa, sizeof(roa)) == -1)
				fatalx("IMSG_RECONF_ROA_ITEM bad len");
			if (trie_roa_add(&roa_new.th, &roa) != 0) {
#if defined(__GNUC__) && __GNUC__ < 4
				struct bgpd_addr p = {
					.aid = roa.aid
				};
				p.v6 = roa.prefix.inet6;
#else
				struct bgpd_addr p = {
					.aid = roa.aid,
					.v6 = roa.prefix.inet6
				};
#endif
				log_warnx("trie_roa_add %s/%u failed",
				    log_addr(&p), roa.prefixlen);
			}
			break;
		case IMSG_RECONF_ASPA_PREP:
			if (imsg_get_data(&imsg, &ap, sizeof(ap)) == -1)
				fatalx("IMSG_RECONF_ASPA_PREP bad len");
			if (aspa_new)
				fatalx("unexpected IMSG_RECONF_ASPA_PREP");
			aspa_new = aspa_table_prep(ap.entries, ap.datasize);
			break;
		case IMSG_RECONF_ASPA:
			if (aspa_new == NULL)
				fatalx("unexpected IMSG_RECONF_ASPA");
			if (aspa != NULL)
				fatalx("IMSG_RECONF_ASPA already sent");
			if ((aspa = calloc(1, sizeof(*aspa))) == NULL)
				fatal("IMSG_RECONF_ASPA");
			if (imsg_get_data(&imsg, aspa,
			    offsetof(struct aspa_set, tas)) == -1)
				fatal("IMSG_RECONF_ASPA bad len");
			break;
		case IMSG_RECONF_ASPA_TAS:
			if (aspa == NULL)
				fatalx("unexpected IMSG_RECONF_ASPA_TAS");
			aspa->tas = reallocarray(NULL, aspa->num,
			    sizeof(uint32_t));
			if (aspa->tas == NULL)
				fatal("IMSG_RECONF_ASPA_TAS");
			if (imsg_get_data(&imsg, aspa->tas,
			    aspa->num * sizeof(uint32_t)) == -1)
				fatal("IMSG_RECONF_ASPA_TAS bad len");
			break;
		case IMSG_RECONF_ASPA_DONE:
			if (aspa_new == NULL)
				fatalx("unexpected IMSG_RECONF_ASPA");
			aspa_add_set(aspa_new, aspa->as, aspa->tas,
			    aspa->num);
			free_aspa(aspa);
			aspa = NULL;
			break;
		case IMSG_RECONF_DONE:
			/* end of update */
			if (rde_roa_reload() + rde_aspa_reload() != 0)
				rde_rpki_reload();
			break;
		}
		imsg_free(&imsg);
	}
}

void
rde_dispatch_imsg_peer(struct rde_peer *peer, void *bula)
{
	struct route_refresh rr;
	struct imsg imsg;
	struct ibuf ibuf;

	if (!peer_is_up(peer)) {
		peer_imsg_flush(peer);
		return;
	}

	if (!peer_imsg_pop(peer, &imsg))
		return;

	switch (imsg_get_type(&imsg)) {
	case IMSG_UPDATE:
		if (imsg_get_ibuf(&imsg, &ibuf) == -1)
			log_warn("update: bad imsg");
		else
			rde_update_dispatch(peer, &ibuf);
		break;
	case IMSG_REFRESH:
		if (imsg_get_data(&imsg, &rr, sizeof(rr)) == -1) {
			log_warnx("route refresh: wrong imsg len");
			break;
		}
		if (rr.aid < AID_MIN || rr.aid >= AID_MAX) {
			log_peer_warnx(&peer->conf,
			    "route refresh: bad AID %d", rr.aid);
			break;
		}
		if (peer->capa.mp[rr.aid] == 0) {
			log_peer_warnx(&peer->conf,
			    "route refresh: AID %s not negotiated",
			    aid2str(rr.aid));
			break;
		}
		switch (rr.subtype) {
		case ROUTE_REFRESH_REQUEST:
			peer_blast(peer, rr.aid);
			break;
		case ROUTE_REFRESH_BEGIN_RR:
			/* check if graceful restart EOR was received */
			if ((peer->recv_eor & (1 << rr.aid)) == 0) {
				log_peer_warnx(&peer->conf,
				    "received %s BoRR before EoR",
				    aid2str(rr.aid));
				break;
			}
			peer_begin_rrefresh(peer, rr.aid);
			break;
		case ROUTE_REFRESH_END_RR:
			if ((peer->recv_eor & (1 << rr.aid)) != 0 &&
			    monotime_valid(peer->staletime[rr.aid]))
				peer_flush(peer, rr.aid,
				    peer->staletime[rr.aid]);
			else
				log_peer_warnx(&peer->conf,
				    "received unexpected %s EoRR",
				    aid2str(rr.aid));
			break;
		default:
			log_peer_warnx(&peer->conf,
			    "route refresh: bad subtype %d", rr.subtype);
			break;
		}
		break;
	default:
		log_warnx("%s: unhandled imsg type %d", __func__,
		    imsg_get_type(&imsg));
		break;
	}

	imsg_free(&imsg);
}

/* handle routing updates from the session engine. */
void
rde_update_dispatch(struct rde_peer *peer, struct ibuf *buf)
{
	struct filterstate	 state;
	struct bgpd_addr	 prefix;
	struct ibuf		 wdbuf, attrbuf, nlribuf, reachbuf, unreachbuf;
	uint16_t		 afi, len;
	uint8_t			 aid, prefixlen, safi, subtype;
	uint32_t		 fas, pathid;

	if (ibuf_get_n16(buf, &len) == -1 ||
	    ibuf_get_ibuf(buf, len, &wdbuf) == -1 ||
	    ibuf_get_n16(buf, &len) == -1 ||
	    ibuf_get_ibuf(buf, len, &attrbuf) == -1 ||
	    ibuf_get_ibuf(buf, ibuf_size(buf), &nlribuf) == -1) {
		rde_update_err(peer, ERR_UPDATE, ERR_UPD_ATTRLIST, NULL);
		return;
	}

	if (ibuf_size(&attrbuf) == 0) {
		/* 0 = no NLRI information in this message */
		if (ibuf_size(&nlribuf) != 0) {
			/* crap at end of update which should not be there */
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_ATTRLIST,
			    NULL);
			return;
		}
		if (ibuf_size(&wdbuf) == 0) {
			/* EoR marker */
			rde_peer_recv_eor(peer, AID_INET);
			return;
		}
	}

	ibuf_from_buffer(&reachbuf, NULL, 0);
	ibuf_from_buffer(&unreachbuf, NULL, 0);
	rde_filterstate_init(&state);
	if (ibuf_size(&attrbuf) != 0) {
		/* parse path attributes */
		while (ibuf_size(&attrbuf) > 0) {
			if (rde_attr_parse(&attrbuf, peer, &state, &reachbuf,
			    &unreachbuf) == -1)
				goto done;
		}

		/* check for missing but necessary attributes */
		if ((subtype = rde_attr_missing(&state.aspath, peer->conf.ebgp,
		    ibuf_size(&nlribuf)))) {
			struct ibuf sbuf;
			ibuf_from_buffer(&sbuf, &subtype, sizeof(subtype));
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_MISSNG_WK_ATTR,
			    &sbuf);
			goto done;
		}

		rde_as4byte_fixup(peer, &state.aspath);

		/* enforce remote AS if requested */
		if (state.aspath.flags & F_ATTR_ASPATH &&
		    peer->conf.enforce_as == ENFORCE_AS_ON) {
			fas = aspath_neighbor(state.aspath.aspath);
			if (peer->conf.remote_as != fas) {
				log_peer_warnx(&peer->conf, "bad path, "
				    "starting with %s expected %u, "
				    "enforce neighbor-as enabled",
				    log_as(fas), peer->conf.remote_as);
				rde_update_err(peer, ERR_UPDATE, ERR_UPD_ASPATH,
				    NULL);
				goto done;
			}
		}

		/* aspath needs to be loop free. This is not a hard error. */
		if (state.aspath.flags & F_ATTR_ASPATH &&
		    peer->conf.ebgp &&
		    peer->conf.enforce_local_as == ENFORCE_AS_ON &&
		    !aspath_loopfree(state.aspath.aspath, peer->conf.local_as))
			state.aspath.flags |= F_ATTR_LOOP;

		rde_reflector(peer, &state.aspath);

		/* Cache aspa lookup for all updates from ebgp sessions. */
		if (state.aspath.flags & F_ATTR_ASPATH && peer->conf.ebgp) {
			aspa_validation(rde_aspa, state.aspath.aspath,
			    &state.aspath.aspa_state);
			state.aspath.aspa_generation = rde_aspa_generation;
		}
	}

	/* withdraw prefix */
	if (ibuf_size(&wdbuf) > 0) {
		if (peer->capa.mp[AID_INET] == 0) {
			log_peer_warnx(&peer->conf,
			    "bad withdraw, %s disabled", aid2str(AID_INET));
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_NETWORK,
			    NULL);
			goto done;
		}
	}
	while (ibuf_size(&wdbuf) > 0) {
		if (peer_has_add_path(peer, AID_INET, CAPA_AP_RECV)) {
			if (ibuf_get_n32(&wdbuf, &pathid) == -1) {
				log_peer_warnx(&peer->conf,
				    "bad withdraw prefix");
				rde_update_err(peer, ERR_UPDATE,
				    ERR_UPD_NETWORK, NULL);
				goto done;
			}
		} else
			pathid = 0;

		if (nlri_get_prefix(&wdbuf, &prefix, &prefixlen) == -1) {
			/*
			 * the RFC does not mention what we should do in
			 * this case. Let's do the same as in the NLRI case.
			 */
			log_peer_warnx(&peer->conf, "bad withdraw prefix");
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_NETWORK,
			    NULL);
			goto done;
		}

		rde_update_withdraw(peer, pathid, &prefix, prefixlen);
	}

	/* withdraw MP_UNREACH_NLRI if available */
	if (ibuf_size(&unreachbuf) != 0) {
		if (ibuf_get_n16(&unreachbuf, &afi) == -1 ||
		    ibuf_get_n8(&unreachbuf, &safi) == -1 ||
		    afi2aid(afi, safi, &aid) == -1) {
			log_peer_warnx(&peer->conf,
			    "bad AFI/SAFI pair in withdraw");
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_OPTATTR,
			    &unreachbuf);
			goto done;
		}

		if (peer->capa.mp[aid] == 0) {
			log_peer_warnx(&peer->conf,
			    "bad withdraw, %s disabled", aid2str(aid));
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_OPTATTR,
			    &unreachbuf);
			goto done;
		}

		if ((state.aspath.flags & ~F_ATTR_MP_UNREACH) == 0 &&
		    ibuf_size(&unreachbuf) == 0) {
			/* EoR marker */
			rde_peer_recv_eor(peer, aid);
		}

		while (ibuf_size(&unreachbuf) > 0) {
			if (peer_has_add_path(peer, aid, CAPA_AP_RECV)) {
				if (ibuf_get_n32(&unreachbuf,
				    &pathid) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad %s withdraw prefix",
					    aid2str(aid));
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &unreachbuf);
					goto done;
				}
			} else
				pathid = 0;

			switch (aid) {
			case AID_INET:
				log_peer_warnx(&peer->conf,
				    "bad MP withdraw for %s", aid2str(aid));
				rde_update_err(peer, ERR_UPDATE,
				    ERR_UPD_OPTATTR, &unreachbuf);
				goto done;
			case AID_INET6:
				if (nlri_get_prefix6(&unreachbuf,
				    &prefix, &prefixlen) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad IPv6 withdraw prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &unreachbuf);
					goto done;
				}
				break;
			case AID_VPN_IPv4:
				if (nlri_get_vpn4(&unreachbuf,
				    &prefix, &prefixlen, 1) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad VPNv4 withdraw prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &unreachbuf);
					goto done;
				}
				break;
			case AID_VPN_IPv6:
				if (nlri_get_vpn6(&unreachbuf,
				    &prefix, &prefixlen, 1) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad VPNv6 withdraw prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &unreachbuf);
					goto done;
				}
				break;
			case AID_EVPN:
				if (nlri_get_evpn(&unreachbuf,
				    &prefix, &prefixlen) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad EVPN withdraw prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &unreachbuf);
					goto done;
				}
				break;
			case AID_FLOWSPECv4:
			case AID_FLOWSPECv6:
				/* ignore flowspec for now */
			default:
				/* ignore unsupported multiprotocol AF */
				if (ibuf_skip(&unreachbuf,
				    ibuf_size(&unreachbuf)) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad withdraw prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &unreachbuf);
					goto done;
				}
				continue;
			}

			rde_update_withdraw(peer, pathid, &prefix, prefixlen);
		}

		if ((state.aspath.flags & ~F_ATTR_MP_UNREACH) == 0)
			goto done;
	}

	/* parse nlri prefix */
	if (ibuf_size(&nlribuf) > 0) {
		if (peer->capa.mp[AID_INET] == 0) {
			log_peer_warnx(&peer->conf,
			    "bad update, %s disabled", aid2str(AID_INET));
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_NETWORK,
			    NULL);
			goto done;
		}

		/* inject open policy OTC attribute if needed */
		if ((state.aspath.flags & F_ATTR_OTC) == 0) {
			uint32_t tmp;
			switch (peer->role) {
			case ROLE_CUSTOMER:
			case ROLE_RS_CLIENT:
			case ROLE_PEER:
				tmp = htonl(peer->conf.remote_as);
				if (attr_optadd(&state.aspath,
				    ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_OTC,
				    &tmp, sizeof(tmp)) == -1) {
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_ATTRLIST, NULL);
					goto done;
				}
				state.aspath.flags |= F_ATTR_OTC;
				break;
			default:
				break;
			}
		}
	}
	while (ibuf_size(&nlribuf) > 0) {
		if (peer_has_add_path(peer, AID_INET, CAPA_AP_RECV)) {
			if (ibuf_get_n32(&nlribuf, &pathid) == -1) {
				log_peer_warnx(&peer->conf,
				    "bad nlri prefix");
				rde_update_err(peer, ERR_UPDATE,
				    ERR_UPD_NETWORK, NULL);
				goto done;
			}
		} else
			pathid = 0;

		if (nlri_get_prefix(&nlribuf, &prefix, &prefixlen) == -1) {
			log_peer_warnx(&peer->conf, "bad nlri prefix");
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_NETWORK,
			    NULL);
			goto done;
		}

		if (rde_update_update(peer, pathid, &state,
		    &prefix, prefixlen) == -1)
			goto done;
	}

	/* add MP_REACH_NLRI if available */
	if (ibuf_size(&reachbuf) != 0) {
		if (ibuf_get_n16(&reachbuf, &afi) == -1 ||
		    ibuf_get_n8(&reachbuf, &safi) == -1 ||
		    afi2aid(afi, safi, &aid) == -1) {
			log_peer_warnx(&peer->conf,
			    "bad AFI/SAFI pair in update");
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_OPTATTR,
			    &reachbuf);
			goto done;
		}

		if (peer->capa.mp[aid] == 0) {
			log_peer_warnx(&peer->conf,
			    "bad update, %s disabled", aid2str(aid));
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_OPTATTR,
			    &reachbuf);
			goto done;
		}

		if (aid == AID_INET6 || aid == AID_INET) {
			/* inject open policy OTC attribute if needed */
			if ((state.aspath.flags & F_ATTR_OTC) == 0) {
				uint32_t tmp;
				switch (peer->role) {
				case ROLE_CUSTOMER:
				case ROLE_RS_CLIENT:
				case ROLE_PEER:
					tmp = htonl(peer->conf.remote_as);
					if (attr_optadd(&state.aspath,
					    ATTR_OPTIONAL|ATTR_TRANSITIVE,
					    ATTR_OTC, &tmp,
					    sizeof(tmp)) == -1) {
						rde_update_err(peer, ERR_UPDATE,
						    ERR_UPD_ATTRLIST, NULL);
						goto done;
					}
					state.aspath.flags |= F_ATTR_OTC;
					break;
				default:
					break;
				}
			}
		} else {
			/* Only IPv4 and IPv6 unicast do OTC handling */
			state.aspath.flags &= ~F_ATTR_OTC_LEAK;
		}

		/* unlock the previously locked nexthop, it is no longer used */
		nexthop_unref(state.nexthop);
		state.nexthop = NULL;
		if (rde_get_mp_nexthop(&reachbuf, aid, peer, &state) == -1) {
			log_peer_warnx(&peer->conf, "bad nlri nexthop");
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_OPTATTR,
			    &reachbuf);
			goto done;
		}

		while (ibuf_size(&reachbuf) > 0) {
			if (peer_has_add_path(peer, aid, CAPA_AP_RECV)) {
				if (ibuf_get_n32(&reachbuf, &pathid) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad %s nlri prefix", aid2str(aid));
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
			} else
				pathid = 0;

			switch (aid) {
			case AID_INET:
				/*
				 * rde_get_mp_nexthop already enforces that
				 * this is only used for RFC 8950.
				 */
				if (nlri_get_prefix(&reachbuf,
				    &prefix, &prefixlen) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad IPv4 MP nlri prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
				break;
			case AID_INET6:
				if (nlri_get_prefix6(&reachbuf,
				    &prefix, &prefixlen) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad IPv6 nlri prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
				break;
			case AID_VPN_IPv4:
				if (nlri_get_vpn4(&reachbuf,
				    &prefix, &prefixlen, 0) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad VPNv4 nlri prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
				break;
			case AID_VPN_IPv6:
				if (nlri_get_vpn6(&reachbuf,
				    &prefix, &prefixlen, 0) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad VPNv6 nlri prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
				break;
			case AID_EVPN:
				if (nlri_get_evpn(&reachbuf,
				    &prefix, &prefixlen) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad EVPN nlri prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
				break;
			case AID_FLOWSPECv4:
			case AID_FLOWSPECv6:
				/* ignore flowspec for now */
			default:
				/* ignore unsupported multiprotocol AF */
				if (ibuf_skip(&reachbuf,
				    ibuf_size(&reachbuf)) == -1) {
					log_peer_warnx(&peer->conf,
					    "bad nlri prefix");
					rde_update_err(peer, ERR_UPDATE,
					    ERR_UPD_OPTATTR, &reachbuf);
					goto done;
				}
				continue;
			}

			if (rde_update_update(peer, pathid, &state,
			    &prefix, prefixlen) == -1)
				goto done;
		}
	}

done:
	rde_filterstate_clean(&state);
}

/*
 * Check if path_id is already in use.
 */
static int
pathid_conflict(struct rib_entry *re, uint32_t pathid)
{
	struct prefix *p;

	if (re == NULL)
		return 0;

	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib)
		if (p->path_id_tx == pathid)
			return 1;
	return 0;
}

/*
 * Assign a send side path_id to all paths.
 */
static uint32_t
pathid_assign(struct rde_peer *peer, uint32_t path_id,
    struct bgpd_addr *prefix, uint8_t prefixlen)
{
	struct rib_entry *re;
	uint32_t path_id_tx;

	/* If peer has no add-path use the per peer path_id */
	if (!peer_has_add_path(peer, prefix->aid, CAPA_AP_RECV))
		return peer->path_id_tx;

	/* peer uses add-path, therefore new path_ids need to be assigned */
	re = rib_get_addr(rib_byid(RIB_ADJ_IN), prefix, prefixlen);
	if (re != NULL) {
		struct prefix *p;

		p = prefix_bypeer(re, peer, path_id);
		if (p != NULL)
			return p->path_id_tx;
	}

	/*
	 * Assign new local path_id, must be an odd number.
	 * Even numbers are used by the per peer path_id_tx.
	 */
	do {
		path_id_tx = arc4random() | 1;
	} while (pathid_conflict(re, path_id_tx));

	return path_id_tx;
}

int
rde_update_update(struct rde_peer *peer, uint32_t path_id,
    struct filterstate *in, struct bgpd_addr *prefix, uint8_t prefixlen)
{
	struct filterstate	 state;
	enum filter_actions	 action;
	uint32_t		 path_id_tx;
	uint16_t		 i;
	uint8_t			 roa_state, aspa_state;
	const char		*wmsg = "filtered, withdraw";

	peer->stats.prefix_rcvd_update++;

	roa_state = rde_roa_validity(&rde_roa, prefix, prefixlen,
	    aspath_origin(in->aspath.aspath));
	aspa_state = rde_aspa_validity(peer, &in->aspath, prefix->aid);
	rde_filterstate_set_vstate(in, roa_state, aspa_state);

	path_id_tx = pathid_assign(peer, path_id, prefix, prefixlen);
	/* add original path to the Adj-RIB-In */
	if (prefix_update(rib_byid(RIB_ADJ_IN), peer, path_id, path_id_tx,
	    in, 0, prefix, prefixlen) == 1)
		peer->stats.prefix_cnt++;

	/* max prefix checker */
	if (peer->conf.max_prefix &&
	    peer->stats.prefix_cnt > peer->conf.max_prefix) {
		log_peer_warnx(&peer->conf, "prefix limit reached (>%u/%u)",
		    peer->stats.prefix_cnt, peer->conf.max_prefix);
		rde_update_err(peer, ERR_CEASE, ERR_CEASE_MAX_PREFIX, NULL);
		return (-1);
	}

	if (in->aspath.flags & F_ATTR_PARSE_ERR)
		wmsg = "path invalid, withdraw";

	for (i = RIB_LOC_START; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		rde_filterstate_copy(&state, in);
		/* input filter */
		action = rde_filter(rib->in_rules, peer, peer, prefix,
		    prefixlen, &state);

		if (action == ACTION_ALLOW) {
			rde_update_log("update", i, peer,
			    &state.nexthop->exit_nexthop, prefix,
			    prefixlen);
			prefix_update(rib, peer, path_id, path_id_tx, &state,
			    0, prefix, prefixlen);
		} else if (conf->filtered_in_locrib && i == RIB_LOC_START) {
			rde_update_log(wmsg, i, peer, NULL, prefix, prefixlen);
			prefix_update(rib, peer, path_id, path_id_tx, &state,
			    1, prefix, prefixlen);
		} else {
			if (prefix_withdraw(rib, peer, path_id, prefix,
			    prefixlen))
				rde_update_log(wmsg, i, peer,
				    NULL, prefix, prefixlen);
		}

		rde_filterstate_clean(&state);
	}
	return (0);
}

void
rde_update_withdraw(struct rde_peer *peer, uint32_t path_id,
    struct bgpd_addr *prefix, uint8_t prefixlen)
{
	uint16_t i;

	for (i = RIB_LOC_START; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		if (prefix_withdraw(rib, peer, path_id, prefix, prefixlen))
			rde_update_log("withdraw", i, peer, NULL, prefix,
			    prefixlen);
	}

	/* remove original path form the Adj-RIB-In */
	if (prefix_withdraw(rib_byid(RIB_ADJ_IN), peer, path_id,
	    prefix, prefixlen))
		peer->stats.prefix_cnt--;

	peer->stats.prefix_rcvd_withdraw++;
}

/*
 * BGP UPDATE parser functions
 */

/* attribute parser specific macros */
#define CHECK_FLAGS(s, t, m)	\
	(((s) & ~(ATTR_DEFMASK | (m))) == (t))

int
rde_attr_parse(struct ibuf *buf, struct rde_peer *peer,
    struct filterstate *state, struct ibuf *reach, struct ibuf *unreach)
{
	struct bgpd_addr nexthop;
	struct rde_aspath *a = &state->aspath;
	struct ibuf	 attrbuf, tmpbuf, *npath = NULL;
	size_t		 alen, hlen;
	uint32_t	 tmp32, zero = 0;
	int		 error;
	uint8_t		 flags, type;

	ibuf_from_ibuf(&attrbuf, buf);
	if (ibuf_get_n8(&attrbuf, &flags) == -1 ||
	    ibuf_get_n8(&attrbuf, &type) == -1)
		goto bad_list;

	if (flags & ATTR_EXTLEN) {
		uint16_t attr_len;
		if (ibuf_get_n16(&attrbuf, &attr_len) == -1)
			goto bad_list;
		alen = attr_len;
		hlen = 4;
	} else {
		uint8_t attr_len;
		if (ibuf_get_n8(&attrbuf, &attr_len) == -1)
			goto bad_list;
		alen = attr_len;
		hlen = 3;
	}

	if (ibuf_truncate(&attrbuf, alen) == -1)
		goto bad_list;
	/* consume the attribute in buf before moving forward */
	if (ibuf_skip(buf, hlen + alen) == -1)
		goto bad_list;

	switch (type) {
	case ATTR_UNDEF:
		/* ignore and drop path attributes with a type code of 0 */
		break;
	case ATTR_ORIGIN:
		if (!CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 1)
			goto bad_len;
		if (a->flags & F_ATTR_ORIGIN)
			goto bad_list;
		if (ibuf_get_n8(&attrbuf, &a->origin) == -1)
			goto bad_len;
		if (a->origin > ORIGIN_INCOMPLETE) {
			/*
			 * mark update as bad and withdraw all routes as per
			 * RFC 7606
			 */
			a->flags |= F_ATTR_PARSE_ERR;
			log_peer_warnx(&peer->conf, "bad ORIGIN %u, "
			    "path invalidated and prefix withdrawn",
			    a->origin);
			return (-1);
		}
		a->flags |= F_ATTR_ORIGIN;
		break;
	case ATTR_ASPATH:
		if (!CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0))
			goto bad_flags;
		if (a->flags & F_ATTR_ASPATH)
			goto bad_list;
		error = aspath_verify(&attrbuf, peer_has_as4byte(peer),
		    peer_permit_as_set(peer));
		if (error != 0 && error != AS_ERR_SOFT) {
			log_peer_warnx(&peer->conf, "bad ASPATH, %s",
			    log_aspath_error(error));
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_ASPATH,
			    NULL);
			return (-1);
		}
		if (peer_has_as4byte(peer)) {
			ibuf_from_ibuf(&tmpbuf, &attrbuf);
		} else {
			if ((npath = aspath_inflate(&attrbuf)) == NULL)
				fatal("aspath_inflate");
			ibuf_from_ibuf(&tmpbuf, npath);
		}
		if (error == AS_ERR_SOFT) {
			char *str;

			/*
			 * soft errors like unexpected segment types are
			 * not considered fatal and the path is just
			 * marked invalid.
			 */
			a->flags |= F_ATTR_PARSE_ERR;

			aspath_asprint(&str, &tmpbuf);
			log_peer_warnx(&peer->conf, "bad ASPATH %s, "
			    "path invalidated and prefix withdrawn",
			    str ? str : "(bad aspath)");
			free(str);
		}
		a->flags |= F_ATTR_ASPATH;
		a->aspath = aspath_get(ibuf_data(&tmpbuf), ibuf_size(&tmpbuf));
		ibuf_free(npath);
		break;
	case ATTR_NEXTHOP:
		if (!CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 4)
			goto bad_len;
		if (a->flags & F_ATTR_NEXTHOP)
			goto bad_list;
		a->flags |= F_ATTR_NEXTHOP;

		memset(&nexthop, 0, sizeof(nexthop));
		nexthop.aid = AID_INET;
		if (ibuf_get_h32(&attrbuf, &nexthop.v4.s_addr) == -1)
			goto bad_len;
		/*
		 * Check if the nexthop is a valid IP address. We consider
		 * multicast addresses as invalid.
		 */
		tmp32 = ntohl(nexthop.v4.s_addr);
		if (IN_MULTICAST(tmp32)) {
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_NEXTHOP,
			    &attrbuf);
			return (-1);
		}
		nexthop_unref(state->nexthop);	/* just to be sure */
		state->nexthop = nexthop_get(&nexthop);
		break;
	case ATTR_MED:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 4)
			goto bad_len;
		if (a->flags & F_ATTR_MED)
			goto bad_list;
		if (ibuf_get_n32(&attrbuf, &a->med) == -1)
			goto bad_len;
		a->flags |= F_ATTR_MED;
		break;
	case ATTR_LOCALPREF:
		if (!CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 4)
			goto bad_len;
		if (peer->conf.ebgp) {
			/* ignore local-pref attr on non ibgp peers */
			break;
		}
		if (a->flags & F_ATTR_LOCALPREF)
			goto bad_list;
		if (ibuf_get_n32(&attrbuf, &a->lpref) == -1)
			goto bad_len;
		a->flags |= F_ATTR_LOCALPREF;
		break;
	case ATTR_ATOMIC_AGGREGATE:
		if (!CHECK_FLAGS(flags, ATTR_WELL_KNOWN, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 0)
			goto bad_len;
		goto optattr;
	case ATTR_AGGREGATOR:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if ((!peer_has_as4byte(peer) && ibuf_size(&attrbuf) != 6) ||
		    (peer_has_as4byte(peer) && ibuf_size(&attrbuf) != 8)) {
			/*
			 * ignore attribute in case of error as per
			 * RFC 7606
			 */
			log_peer_warnx(&peer->conf, "bad AGGREGATOR, "
			    "attribute discarded");
			break;
		}
		if (!peer_has_as4byte(peer)) {
			/* need to inflate aggregator AS to 4-byte */
			u_char	t[8];
			t[0] = t[1] = 0;
			if (ibuf_get(&attrbuf, &t[2], 6) == -1)
				goto bad_list;
			if (memcmp(t, &zero, sizeof(uint32_t)) == 0) {
				/* As per RFC7606 use "attribute discard". */
				log_peer_warnx(&peer->conf, "bad AGGREGATOR, "
				    "AS 0 not allowed, attribute discarded");
				break;
			}
			if (attr_optadd(a, flags, type, t, sizeof(t)) == -1)
				goto bad_list;
			break;
		}
		/* 4-byte ready server take the default route */
		ibuf_from_ibuf(&tmpbuf, &attrbuf);
		if (ibuf_get_n32(&tmpbuf, &tmp32) == -1)
			goto bad_len;
		if (tmp32 == 0) {
			/* As per RFC7606 use "attribute discard" here. */
			char *pfmt = log_fmt_peer(&peer->conf);
			log_debug("%s: bad AGGREGATOR, "
			    "AS 0 not allowed, attribute discarded", pfmt);
			free(pfmt);
			break;
		}
		goto optattr;
	case ATTR_COMMUNITIES:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if (community_add(&state->communities, flags,
		    &attrbuf) == -1) {
			/*
			 * mark update as bad and withdraw all routes as per
			 * RFC 7606
			 */
			a->flags |= F_ATTR_PARSE_ERR;
			log_peer_warnx(&peer->conf, "bad COMMUNITIES, "
			    "path invalidated and prefix withdrawn");
		}
		break;
	case ATTR_LARGE_COMMUNITIES:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if (community_large_add(&state->communities, flags,
		    &attrbuf) == -1) {
			/*
			 * mark update as bad and withdraw all routes as per
			 * RFC 7606
			 */
			a->flags |= F_ATTR_PARSE_ERR;
			log_peer_warnx(&peer->conf, "bad LARGE COMMUNITIES, "
			    "path invalidated and prefix withdrawn");
		}
		break;
	case ATTR_EXT_COMMUNITIES:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if (community_ext_add(&state->communities, flags,
		    peer->conf.ebgp, &attrbuf) == -1) {
			/*
			 * mark update as bad and withdraw all routes as per
			 * RFC 7606
			 */
			a->flags |= F_ATTR_PARSE_ERR;
			log_peer_warnx(&peer->conf, "bad EXT_COMMUNITIES, "
			    "path invalidated and prefix withdrawn");
		}
		break;
	case ATTR_ORIGINATOR_ID:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 4)
			goto bad_len;
		goto optattr;
	case ATTR_CLUSTER_LIST:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL, 0))
			goto bad_flags;
		if (peer->conf.ebgp) {
			/* As per RFC7606 use "attribute discard" here. */
			log_peer_warnx(&peer->conf, "bad CLUSTER_LIST, "
			    "received from external peer, attribute discarded");
			break;
		}
		if (ibuf_size(&attrbuf) % 4 != 0 || ibuf_size(&attrbuf) == 0) {
			/*
			 * mark update as bad and withdraw all routes as per
			 * RFC 7606
			 */
			a->flags |= F_ATTR_PARSE_ERR;
			log_peer_warnx(&peer->conf, "bad CLUSTER_LIST, "
			    "path invalidated and prefix withdrawn");
			break;
		}
		goto optattr;
	case ATTR_MP_REACH_NLRI:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) < 5)
			goto bad_len;
		/* the validity is checked in rde_update_dispatch() */
		if (a->flags & F_ATTR_MP_REACH)
			goto bad_list;
		a->flags |= F_ATTR_MP_REACH;

		*reach = attrbuf;
		break;
	case ATTR_MP_UNREACH_NLRI:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL, 0))
			goto bad_flags;
		if (ibuf_size(&attrbuf) < 3)
			goto bad_len;
		/* the validity is checked in rde_update_dispatch() */
		if (a->flags & F_ATTR_MP_UNREACH)
			goto bad_list;
		a->flags |= F_ATTR_MP_UNREACH;

		*unreach = attrbuf;
		break;
	case ATTR_AS4_AGGREGATOR:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 8) {
			/* see ATTR_AGGREGATOR ... */
			log_peer_warnx(&peer->conf, "bad AS4_AGGREGATOR, "
			    "attribute discarded");
			break;
		}
		ibuf_from_ibuf(&tmpbuf, &attrbuf);
		if (ibuf_get_n32(&tmpbuf, &tmp32) == -1)
			goto bad_len;
		if (tmp32 == 0) {
			/* As per RFC6793 use "attribute discard" here. */
			log_peer_warnx(&peer->conf, "bad AS4_AGGREGATOR, "
			    "AS 0 not allowed, attribute discarded");
			break;
		}
		a->flags |= F_ATTR_AS4BYTE_NEW;
		goto optattr;
	case ATTR_AS4_PATH:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if ((error = aspath_verify(&attrbuf, 1,
		    peer_permit_as_set(peer))) != 0) {
			/* As per RFC6793 use "attribute discard" here. */
			log_peer_warnx(&peer->conf, "bad AS4_PATH, "
			    "attribute discarded");
			break;
		}
		a->flags |= F_ATTR_AS4BYTE_NEW;
		goto optattr;
	case ATTR_OTC:
		if (!CHECK_FLAGS(flags, ATTR_OPTIONAL|ATTR_TRANSITIVE,
		    ATTR_PARTIAL))
			goto bad_flags;
		if (ibuf_size(&attrbuf) != 4) {
			/* treat-as-withdraw */
			a->flags |= F_ATTR_PARSE_ERR;
			log_peer_warnx(&peer->conf, "bad OTC, "
			    "path invalidated and prefix withdrawn");
			break;
		}
		switch (peer->role) {
		case ROLE_PROVIDER:
		case ROLE_RS:
			a->flags |= F_ATTR_OTC_LEAK;
			break;
		case ROLE_PEER:
			if (ibuf_get_n32(&attrbuf, &tmp32) == -1)
				goto bad_len;
			if (tmp32 != peer->conf.remote_as)
				a->flags |= F_ATTR_OTC_LEAK;
			break;
		default:
			break;
		}
		a->flags |= F_ATTR_OTC;
		goto optattr;
	default:
		if ((flags & ATTR_OPTIONAL) == 0) {
			rde_update_err(peer, ERR_UPDATE, ERR_UPD_UNKNWN_WK_ATTR,
			    &attrbuf);
			return (-1);
		}
 optattr:
		if (attr_optadd(a, flags, type, ibuf_data(&attrbuf),
		    ibuf_size(&attrbuf)) == -1)
			goto bad_list;
		break;
	}

	return (0);

 bad_len:
	rde_update_err(peer, ERR_UPDATE, ERR_UPD_ATTRLEN, &attrbuf);
	return (-1);
 bad_flags:
	rde_update_err(peer, ERR_UPDATE, ERR_UPD_ATTRFLAGS, &attrbuf);
	return (-1);
 bad_list:
	rde_update_err(peer, ERR_UPDATE, ERR_UPD_ATTRLIST, NULL);
	return (-1);
}

#undef CHECK_FLAGS

int
rde_attr_add(struct filterstate *state, struct ibuf *buf)
{
	uint16_t	 attr_len;
	uint8_t		 flags;
	uint8_t		 type;
	uint8_t		 tmp8;

	if (ibuf_get_n8(buf, &flags) == -1 ||
	    ibuf_get_n8(buf, &type) == -1)
		return (-1);

	if (flags & ATTR_EXTLEN) {
		if (ibuf_get_n16(buf, &attr_len) == -1)
			return (-1);
	} else {
		if (ibuf_get_n8(buf, &tmp8) == -1)
			return (-1);
		attr_len = tmp8;
	}

	if (ibuf_size(buf) != attr_len)
		return (-1);

	switch (type) {
	case ATTR_COMMUNITIES:
		return community_add(&state->communities, flags, buf);
	case ATTR_LARGE_COMMUNITIES:
		return community_large_add(&state->communities, flags, buf);
	case ATTR_EXT_COMMUNITIES:
		return community_ext_add(&state->communities, flags, 0, buf);
	}

	if (attr_optadd(&state->aspath, flags, type, ibuf_data(buf),
	    attr_len) == -1)
		return (-1);
	return (0);
}

uint8_t
rde_attr_missing(struct rde_aspath *a, int ebgp, uint16_t nlrilen)
{
	/* ATTR_MP_UNREACH_NLRI may be sent alone */
	if (nlrilen == 0 && a->flags & F_ATTR_MP_UNREACH &&
	    (a->flags & F_ATTR_MP_REACH) == 0)
		return (0);

	if ((a->flags & F_ATTR_ORIGIN) == 0)
		return (ATTR_ORIGIN);
	if ((a->flags & F_ATTR_ASPATH) == 0)
		return (ATTR_ASPATH);
	if ((a->flags & F_ATTR_MP_REACH) == 0 &&
	    (a->flags & F_ATTR_NEXTHOP) == 0)
		return (ATTR_NEXTHOP);
	if (!ebgp)
		if ((a->flags & F_ATTR_LOCALPREF) == 0)
			return (ATTR_LOCALPREF);
	return (0);
}

int
rde_get_mp_nexthop(struct ibuf *buf, uint8_t aid,
    struct rde_peer *peer, struct filterstate *state)
{
	struct bgpd_addr	nexthop;
	struct ibuf		nhbuf;
	uint8_t			nhlen;

	if (ibuf_get_n8(buf, &nhlen) == -1)
		return (-1);
	if (ibuf_get_ibuf(buf, nhlen, &nhbuf) == -1)
		return (-1);
	/* ignore reserved (old SNPA) field as per RFC4760 */
	if (ibuf_skip(buf, 1) == -1)
		return (-1);

	if (aid == AID_INET && peer_has_ext_nexthop(peer, AID_INET) &&
	    (nhlen == 16 || nhlen == 32))
		aid = AID_INET6;
	if (aid == AID_VPN_IPv4 && peer_has_ext_nexthop(peer, AID_VPN_IPv4) &&
	    (nhlen == 24 || nhlen == 48))
		aid = AID_VPN_IPv6;

	memset(&nexthop, 0, sizeof(nexthop));
	switch (aid) {
	case AID_INET:
		log_peer_warnx(&peer->conf, "bad multiprotocol nexthop, "
		    "IPv4 unexpected");
		return (-1);
	case AID_INET6:
		/*
		 * RFC2545 describes that there may be a link-local
		 * address carried in nexthop. Yikes!
		 * This is not only silly, it is wrong and we just ignore
		 * this link-local nexthop. The bgpd session doesn't run
		 * over the link-local address so why should all other
		 * traffic.
		 */
		if (nhlen != 16 && nhlen != 32) {
			log_peer_warnx(&peer->conf, "bad %s nexthop, "
			    "bad size %d", aid2str(aid), nhlen);
			return (-1);
		}
		if (ibuf_get(&nhbuf, &nexthop.v6, sizeof(nexthop.v6)) == -1)
			return (-1);
		nexthop.aid = AID_INET6;
		if (IN6_IS_ADDR_LINKLOCAL(&nexthop.v6)) {
			if (peer->local_if_scope != 0) {
				nexthop.scope_id = peer->local_if_scope;
			} else {
				log_peer_warnx(&peer->conf,
				    "unexpected link-local nexthop: %s",
				    log_addr(&nexthop));
				return (-1);
			}
		}
		break;
	case AID_VPN_IPv4:
		/*
		 * Neither RFC4364 nor RFC3107 specify the format of the
		 * nexthop in an explicit way. The quality of RFC went down
		 * the toilet the larger the number got.
		 * RFC4364 is very confusing about VPN-IPv4 address and the
		 * VPN-IPv4 prefix that carries also a MPLS label.
		 * So the nexthop is a 12-byte address with a 64bit RD and
		 * an IPv4 address following. In the nexthop case the RD can
		 * be ignored.
		 * Since the nexthop has to be in the main IPv4 table just
		 * create an AID_INET nexthop. So we don't need to handle
		 * AID_VPN_IPv4 in nexthop and kroute.
		 */
		if (nhlen != 12) {
			log_peer_warnx(&peer->conf, "bad %s nexthop, "
			    "bad size %d", aid2str(aid), nhlen);
			return (-1);
		}
		if (ibuf_skip(&nhbuf, sizeof(uint64_t)) == -1 ||
		    ibuf_get(&nhbuf, &nexthop.v4, sizeof(nexthop.v4)) == -1)
			return (-1);
		nexthop.aid = AID_INET;
		break;
	case AID_VPN_IPv6:
		if (nhlen != 24 && nhlen != 48) {
			log_peer_warnx(&peer->conf, "bad %s nexthop, "
			    "bad size %d", aid2str(aid), nhlen);
			return (-1);
		}
		if (ibuf_skip(&nhbuf, sizeof(uint64_t)) == -1 ||
		    ibuf_get(&nhbuf, &nexthop.v6, sizeof(nexthop.v6)) == -1)
			return (-1);
		nexthop.aid = AID_INET6;
		if (IN6_IS_ADDR_LINKLOCAL(&nexthop.v6)) {
			if (peer->local_if_scope != 0) {
				nexthop.scope_id = peer->local_if_scope;
			} else {
				log_peer_warnx(&peer->conf,
				    "unexpected link-local nexthop: %s",
				    log_addr(&nexthop));
				return (-1);
			}
		}
		break;
	case AID_EVPN:
		switch (nhlen) {
		case 4:
			if (ibuf_get_h32(&nhbuf, &nexthop.v4.s_addr) == -1)
				return (-1);
			nexthop.aid = AID_INET;
			break;
		case 16:
		case 32:
			if (ibuf_get(&nhbuf, &nexthop.v6,
			    sizeof(nexthop.v6)) == -1)
				return (-1);
			nexthop.aid = AID_INET6;
			if (IN6_IS_ADDR_LINKLOCAL(&nexthop.v6)) {
				if (peer->local_if_scope != 0) {
					nexthop.scope_id = peer->local_if_scope;
				} else {
					log_peer_warnx(&peer->conf,
					    "unexpected link-local nexthop: %s",
					    log_addr(&nexthop));
					return (-1);
				}
			}
			break;
		default:
			log_peer_warnx(&peer->conf, "bad %s nexthop, "
			    "bad size %d", aid2str(aid), nhlen);
			return (-1);
		}
		break;
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		/* nexthop must be 0 and ignored for flowspec */
		if (nhlen != 0) {
			log_peer_warnx(&peer->conf, "bad %s nexthop, "
			    "bad size %d", aid2str(aid), nhlen);
			return (-1);
		}
		return (0);
	default:
		log_peer_warnx(&peer->conf, "bad multiprotocol nexthop, "
		    "bad AID");
		return (-1);
	}

	state->nexthop = nexthop_get(&nexthop);

	return (0);
}

void
rde_update_err(struct rde_peer *peer, uint8_t error, uint8_t suberr,
    struct ibuf *opt)
{
	struct ibuf *wbuf;
	size_t size = 0;

	if (opt != NULL) {
		ibuf_rewind(opt);
		size = ibuf_size(opt);
	}
	if ((wbuf = imsg_create(ibuf_se, IMSG_UPDATE_ERR, peer->conf.id, 0,
	    size + sizeof(error) + sizeof(suberr))) == NULL)
		fatal("%s %d imsg_create error", __func__, __LINE__);
	if (imsg_add(wbuf, &error, sizeof(error)) == -1 ||
	    imsg_add(wbuf, &suberr, sizeof(suberr)) == -1)
		fatal("%s %d imsg_add error", __func__, __LINE__);
	if (opt != NULL)
		if (ibuf_add_ibuf(wbuf, opt) == -1)
			fatal("%s %d ibuf_add_ibuf error", __func__, __LINE__);
	imsg_close(ibuf_se, wbuf);
	peer->state = PEER_ERR;
}

void
rde_update_log(const char *message, uint16_t rid,
    const struct rde_peer *peer, const struct bgpd_addr *next,
    const struct bgpd_addr *prefix, uint8_t prefixlen)
{
	char		*l = NULL;
	char		*n = NULL;
	char		*p = NULL;

	if (!((conf->log & BGPD_LOG_UPDATES) ||
	    (peer->flags & PEERFLAG_LOG_UPDATES)))
		return;

	if (next != NULL)
		if (asprintf(&n, " via %s", log_addr(next)) == -1)
			n = NULL;
	if (asprintf(&p, "%s/%u", log_addr(prefix), prefixlen) == -1)
		p = NULL;
	l = log_fmt_peer(&peer->conf);
	log_info("Rib %s: %s AS%s: %s %s%s", rib_byid(rid)->name,
	    l, log_as(peer->conf.remote_as), message,
	    p ? p : "out of memory", n ? n : "");

	free(l);
	free(n);
	free(p);
}

/*
 * 4-Byte ASN helper function.
 * Two scenarios need to be considered:
 * - NEW session with NEW attributes present -> just remove the attributes
 * - OLD session with NEW attributes present -> try to merge them
 */
void
rde_as4byte_fixup(struct rde_peer *peer, struct rde_aspath *a)
{
	struct attr	*nasp, *naggr, *oaggr;
	uint32_t	 as;

	/*
	 * if either ATTR_AS4_AGGREGATOR or ATTR_AS4_PATH is present
	 * try to fixup the attributes.
	 * Do not fixup if F_ATTR_PARSE_ERR is set.
	 */
	if (!(a->flags & F_ATTR_AS4BYTE_NEW) || a->flags & F_ATTR_PARSE_ERR)
		return;

	/* first get the attributes */
	nasp = attr_optget(a, ATTR_AS4_PATH);
	naggr = attr_optget(a, ATTR_AS4_AGGREGATOR);

	if (peer_has_as4byte(peer)) {
		/* NEW session using 4-byte ASNs */
		if (nasp) {
			log_peer_warnx(&peer->conf, "uses 4-byte ASN "
			    "but sent AS4_PATH attribute.");
			attr_free(a, nasp);
		}
		if (naggr) {
			log_peer_warnx(&peer->conf, "uses 4-byte ASN "
			    "but sent AS4_AGGREGATOR attribute.");
			attr_free(a, naggr);
		}
		return;
	}
	/* OLD session using 2-byte ASNs */
	/* try to merge the new attributes into the old ones */
	if ((oaggr = attr_optget(a, ATTR_AGGREGATOR))) {
		memcpy(&as, oaggr->data, sizeof(as));
		if (ntohl(as) != AS_TRANS) {
			/* per RFC ignore AS4_PATH and AS4_AGGREGATOR */
			if (nasp)
				attr_free(a, nasp);
			if (naggr)
				attr_free(a, naggr);
			return;
		}
		if (naggr) {
			/* switch over to new AGGREGATOR */
			attr_free(a, oaggr);
			if (attr_optadd(a, ATTR_OPTIONAL | ATTR_TRANSITIVE,
			    ATTR_AGGREGATOR, naggr->data, naggr->len) == -1)
				fatalx("attr_optadd failed but impossible");
		}
	}
	/* there is no need for AS4_AGGREGATOR any more */
	if (naggr)
		attr_free(a, naggr);

	/* merge AS4_PATH with ASPATH */
	if (nasp)
		aspath_merge(a, nasp);
}


uint8_t
rde_aspa_validity(struct rde_peer *peer, struct rde_aspath *asp, uint8_t aid)
{
	if (!peer->conf.ebgp)	/* ASPA is only performed on ebgp sessions */
		return ASPA_NEVER_KNOWN;
	if (aid != AID_INET && aid != AID_INET6) /* skip uncovered aids */
		return ASPA_NEVER_KNOWN;

#ifdef MAYBE
	/*
	 * By default enforce neighbor-as is set for all ebgp sessions.
	 * So if a admin disables this check should we really "reenable"
	 * it here in such a dubious way?
	 * This just fails the ASPA validation for these paths so maybe
	 * this can be helpful. But it is not transparent to the admin.
	 */

	/* skip neighbor-as check for transparent RS sessions */
	if (peer->role != ROLE_RS_CLIENT &&
	    peer->conf.enforce_as != ENFORCE_AS_ON) {
		uint32_t fas;

		fas = aspath_neighbor(asp->aspath);
		if (peer->conf.remote_as != fas)
			return ASPA_INVALID;
	}
#endif

	/* if no role is set, the outcome is unknown */
	if (peer->role == ROLE_NONE)
		return ASPA_UNKNOWN;

	if (peer->role == ROLE_CUSTOMER)
		return asp->aspa_state.downup;
	else
		return asp->aspa_state.onlyup;
}

/*
 * route reflector helper function
 */
void
rde_reflector(struct rde_peer *peer, struct rde_aspath *asp)
{
	struct attr	*a;
	uint8_t		*p;
	uint16_t	 len;
	uint32_t	 id;

	/* do not consider updates with parse errors */
	if (asp->flags & F_ATTR_PARSE_ERR)
		return;

	/* check for originator id if eq router_id drop */
	if ((a = attr_optget(asp, ATTR_ORIGINATOR_ID)) != NULL) {
		id = htonl(conf->bgpid);
		if (memcmp(&id, a->data, sizeof(id)) == 0) {
			/* this is coming from myself */
			asp->flags |= F_ATTR_LOOP;
			return;
		}
	} else if (conf->flags & BGPD_FLAG_REFLECTOR) {
		if (peer->conf.ebgp)
			id = htonl(conf->bgpid);
		else
			id = htonl(peer->remote_bgpid);
		if (attr_optadd(asp, ATTR_OPTIONAL, ATTR_ORIGINATOR_ID,
		    &id, sizeof(id)) == -1)
			fatalx("attr_optadd failed but impossible");
	}

	/* check for own id in the cluster list */
	if (conf->flags & BGPD_FLAG_REFLECTOR) {
		id = htonl(conf->clusterid);
		if ((a = attr_optget(asp, ATTR_CLUSTER_LIST)) != NULL) {
			for (len = 0; len < a->len; len += sizeof(id))
				/* check if coming from my cluster */
				if (memcmp(&id, a->data + len,
				    sizeof(id)) == 0) {
					asp->flags |= F_ATTR_LOOP;
					return;
				}

			/* prepend own clusterid by replacing attribute */
			len = a->len + sizeof(id);
			if (len < a->len)
				fatalx("rde_reflector: cluster-list overflow");
			if ((p = malloc(len)) == NULL)
				fatal("rde_reflector");
			memcpy(p, &id, sizeof(id));
			memcpy(p + sizeof(id), a->data, a->len);
			attr_free(asp, a);
			if (attr_optadd(asp, ATTR_OPTIONAL, ATTR_CLUSTER_LIST,
			    p, len) == -1)
				fatalx("attr_optadd failed but impossible");
			free(p);
		} else if (attr_optadd(asp, ATTR_OPTIONAL, ATTR_CLUSTER_LIST,
		    &id, sizeof(id)) == -1)
			fatalx("attr_optadd failed but impossible");
	}
}

/*
 * control specific functions
 */
static void
rde_dump_rib_as(struct prefix *p, struct rde_aspath *asp, pid_t pid, int flags,
    int adjout)
{
	struct ctl_show_rib	 rib;
	struct ibuf		*wbuf;
	struct attr		*a;
	struct nexthop		*nexthop;
	struct rib_entry	*re;
	struct prefix		*xp;
	struct rde_peer		*peer;
	monotime_t		 staletime;
	size_t			 aslen;
	uint8_t			 l;

	nexthop = prefix_nexthop(p);
	peer = prefix_peer(p);
	memset(&rib, 0, sizeof(rib));
	rib.lastchange = p->lastchange;
	rib.local_pref = asp->lpref;
	rib.med = asp->med;
	rib.weight = asp->weight;
	strlcpy(rib.descr, peer->conf.descr, sizeof(rib.descr));
	memcpy(&rib.remote_addr, &peer->remote_addr,
	    sizeof(rib.remote_addr));
	rib.remote_id = peer->remote_bgpid;
	if (nexthop != NULL) {
		rib.exit_nexthop = nexthop->exit_nexthop;
		rib.true_nexthop = nexthop->true_nexthop;
	} else {
		/* announced network can have a NULL nexthop */
		rib.exit_nexthop.aid = p->pt->aid;
		rib.true_nexthop.aid = p->pt->aid;
	}
	pt_getaddr(p->pt, &rib.prefix);
	rib.prefixlen = p->pt->prefixlen;
	rib.origin = asp->origin;
	rib.roa_validation_state = prefix_roa_vstate(p);
	rib.aspa_validation_state = prefix_aspa_vstate(p);
	rib.dmetric = p->dmetric;
	rib.flags = 0;
	if (!adjout && prefix_eligible(p)) {
		re = prefix_re(p);
		TAILQ_FOREACH(xp, &re->prefix_h, entry.list.rib) {
			switch (xp->dmetric) {
			case PREFIX_DMETRIC_BEST:
				if (xp == p)
					rib.flags |= F_PREF_BEST;
				break;
			case PREFIX_DMETRIC_ECMP:
				if (xp == p)
					rib.flags |= F_PREF_ECMP;
				break;
			case PREFIX_DMETRIC_AS_WIDE:
				if (xp == p)
					rib.flags |= F_PREF_AS_WIDE;
				break;
			default:
				xp = NULL;	/* stop loop */
				break;
			}
			if (xp == NULL || xp == p)
				break;
		}
	}
	if (!peer->conf.ebgp)
		rib.flags |= F_PREF_INTERNAL;
	if (asp->flags & F_PREFIX_ANNOUNCED)
		rib.flags |= F_PREF_ANNOUNCE;
	if (prefix_eligible(p))
		rib.flags |= F_PREF_ELIGIBLE;
	if (prefix_filtered(p))
		rib.flags |= F_PREF_FILTERED;
	/* otc loop includes parse err so skip the latter if the first is set */
	if (asp->flags & F_ATTR_OTC_LEAK)
		rib.flags |= F_PREF_OTC_LEAK;
	else if (asp->flags & F_ATTR_PARSE_ERR)
		rib.flags |= F_PREF_INVALID;
	staletime = peer->staletime[p->pt->aid];
	if (monotime_valid(staletime) &&
	    monotime_cmp(p->lastchange, staletime) <= 0)
		rib.flags |= F_PREF_STALE;
	if (!adjout) {
		if (peer_has_add_path(peer, p->pt->aid, CAPA_AP_RECV)) {
			rib.path_id = p->path_id;
			rib.flags |= F_PREF_PATH_ID;
		}
	} else {
		if (peer_has_add_path(peer, p->pt->aid, CAPA_AP_SEND)) {
			rib.path_id = p->path_id_tx;
			rib.flags |= F_PREF_PATH_ID;
		}
	}
	aslen = aspath_length(asp->aspath);

	if ((wbuf = imsg_create(ibuf_se_ctl, IMSG_CTL_SHOW_RIB, 0, pid,
	    sizeof(rib) + aslen)) == NULL)
		return;
	if (imsg_add(wbuf, &rib, sizeof(rib)) == -1 ||
	    imsg_add(wbuf, aspath_dump(asp->aspath), aslen) == -1)
		return;
	imsg_close(ibuf_se_ctl, wbuf);

	if (flags & F_CTL_DETAIL) {
		struct rde_community *comm = prefix_communities(p);
		size_t len = comm->nentries * sizeof(struct community);
		if (comm->nentries > 0) {
			if (imsg_compose(ibuf_se_ctl,
			    IMSG_CTL_SHOW_RIB_COMMUNITIES, 0, pid, -1,
			    comm->communities, len) == -1)
				return;
		}
		for (l = 0; l < asp->others_len; l++) {
			if ((a = asp->others[l]) == NULL)
				break;
			if ((wbuf = imsg_create(ibuf_se_ctl,
			    IMSG_CTL_SHOW_RIB_ATTR, 0, pid, 0)) == NULL)
				return;
			if (attr_writebuf(wbuf, a->flags, a->type, a->data,
			    a->len) == -1) {
				ibuf_free(wbuf);
				return;
			}
			imsg_close(ibuf_se_ctl, wbuf);
		}
	}
}

int
rde_match_peer(struct rde_peer *p, struct ctl_neighbor *n)
{
	char *s;

	if (n && n->addr.aid) {
		if (memcmp(&p->conf.remote_addr, &n->addr,
		    sizeof(p->conf.remote_addr)))
			return 0;
	} else if (n && n->descr[0]) {
		s = n->is_group ? p->conf.group : p->conf.descr;
		/* cannot trust n->descr to be properly terminated */
		if (strncmp(s, n->descr, sizeof(n->descr)))
			return 0;
	}
	return 1;
}

static void
rde_dump_filter(struct prefix *p, struct ctl_show_rib_request *req, int adjout)
{
	struct rde_aspath	*asp;

	if (!rde_match_peer(prefix_peer(p), &req->neighbor))
		return;

	asp = prefix_aspath(p);
	if ((req->flags & F_CTL_BEST) && p->dmetric != PREFIX_DMETRIC_BEST)
		return;
	if ((req->flags & F_CTL_INVALID) &&
	    (asp->flags & F_ATTR_PARSE_ERR) == 0)
		return;
	if ((req->flags & F_CTL_FILTERED) && !prefix_filtered(p))
		return;
	if ((req->flags & F_CTL_INELIGIBLE) && prefix_eligible(p))
		return;
	if ((req->flags & F_CTL_LEAKED) &&
	    (asp->flags & F_ATTR_OTC_LEAK) == 0)
		return;
	if ((req->flags & F_CTL_HAS_PATHID)) {
		/* Match against the transmit path id if adjout is used.  */
		if (adjout) {
			if (req->path_id != p->path_id_tx)
				return;
		} else {
			if (req->path_id != p->path_id)
				return;
		}
	}
	if (req->as.type != AS_UNDEF &&
	    !aspath_match(asp->aspath, &req->as, 0))
		return;
	if (req->community.flags != 0) {
		if (!community_match(prefix_communities(p), &req->community,
		    NULL))
			return;
	}
	if (!ovs_match(p, req->flags))
		return;
	if (!avs_match(p, req->flags))
		return;
	rde_dump_rib_as(p, asp, req->pid, req->flags, adjout);
}

static void
rde_dump_upcall(struct rib_entry *re, void *ptr)
{
	struct rde_dump_ctx	*ctx = ptr;
	struct prefix		*p;

	if (re == NULL)
		return;
	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib)
		rde_dump_filter(p, &ctx->req, 0);
}

static void
rde_dump_adjout_upcall(struct prefix *p, void *ptr)
{
	struct rde_dump_ctx	*ctx = ptr;

	if ((p->flags & PREFIX_FLAG_ADJOUT) == 0)
		fatalx("%s: prefix without PREFIX_FLAG_ADJOUT hit", __func__);
	if (p->flags & (PREFIX_FLAG_WITHDRAW | PREFIX_FLAG_DEAD))
		return;
	rde_dump_filter(p, &ctx->req, 1);
}

static int
rde_dump_throttled(void *arg)
{
	struct rde_dump_ctx	*ctx = arg;

	return (ctx->throttled != 0);
}

static void
rde_dump_done(void *arg, uint8_t aid)
{
	struct rde_dump_ctx	*ctx = arg;
	struct rde_peer		*peer;
	u_int			 error;

	if (ctx->req.flags & F_CTL_ADJ_OUT) {
		peer = peer_match(&ctx->req.neighbor, ctx->peerid);
		if (peer == NULL)
			goto done;
		ctx->peerid = peer->conf.id;
		switch (ctx->req.type) {
		case IMSG_CTL_SHOW_RIB:
			if (prefix_dump_new(peer, ctx->req.aid,
			    CTL_MSG_HIGH_MARK, ctx, rde_dump_adjout_upcall,
			    rde_dump_done, rde_dump_throttled) == -1)
				goto nomem;
			break;
		case IMSG_CTL_SHOW_RIB_PREFIX:
			if (prefix_dump_subtree(peer, &ctx->req.prefix,
			    ctx->req.prefixlen, CTL_MSG_HIGH_MARK, ctx,
			    rde_dump_adjout_upcall, rde_dump_done,
			    rde_dump_throttled) == -1)
				goto nomem;
			break;
		default:
			fatalx("%s: unsupported imsg type", __func__);
		}
		return;
	}
done:
	imsg_compose(ibuf_se_ctl, IMSG_CTL_END, 0, ctx->req.pid, -1, NULL, 0);
	LIST_REMOVE(ctx, entry);
	free(ctx);
	return;

nomem:
	log_warn(__func__);
	error = CTL_RES_NOMEM;
	imsg_compose(ibuf_se_ctl, IMSG_CTL_RESULT, 0, ctx->req.pid, -1, &error,
	    sizeof(error));
	return;
}

void
rde_dump_ctx_new(struct ctl_show_rib_request *req, pid_t pid,
    enum imsg_type type)
{
	struct rde_dump_ctx	*ctx;
	struct rib_entry	*re;
	struct prefix		*p;
	u_int			 error;
	uint8_t			 hostplen, plen;
	uint16_t		 rid;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL) {
 nomem:
		log_warn(__func__);
		error = CTL_RES_NOMEM;
		imsg_compose(ibuf_se_ctl, IMSG_CTL_RESULT, 0, pid, -1, &error,
		    sizeof(error));
		free(ctx);
		return;
	}

	if (strcmp(req->rib, "Adj-RIB-Out") == 0)
		req->flags |= F_CTL_ADJ_OUT;

	memcpy(&ctx->req, req, sizeof(struct ctl_show_rib_request));
	ctx->req.pid = pid;
	ctx->req.type = type;

	if (req->flags & (F_CTL_ADJ_IN | F_CTL_INVALID)) {
		rid = RIB_ADJ_IN;
	} else if (req->flags & F_CTL_ADJ_OUT) {
		struct rde_peer *peer;

		peer = peer_match(&req->neighbor, 0);
		if (peer == NULL) {
			error = CTL_RES_NOSUCHPEER;
			imsg_compose(ibuf_se_ctl, IMSG_CTL_RESULT, 0, pid, -1,
			    &error, sizeof(error));
			free(ctx);
			return;
		}
		ctx->peerid = peer->conf.id;
		switch (ctx->req.type) {
		case IMSG_CTL_SHOW_RIB:
			if (prefix_dump_new(peer, ctx->req.aid,
			    CTL_MSG_HIGH_MARK, ctx, rde_dump_adjout_upcall,
			    rde_dump_done, rde_dump_throttled) == -1)
				goto nomem;
			break;
		case IMSG_CTL_SHOW_RIB_PREFIX:
			if (req->flags & F_LONGER) {
				if (prefix_dump_subtree(peer, &req->prefix,
				    req->prefixlen, CTL_MSG_HIGH_MARK, ctx,
				    rde_dump_adjout_upcall,
				    rde_dump_done, rde_dump_throttled) == -1)
					goto nomem;
				break;
			}
			switch (req->prefix.aid) {
			case AID_INET:
			case AID_VPN_IPv4:
				hostplen = 32;
				break;
			case AID_INET6:
			case AID_VPN_IPv6:
				hostplen = 128;
				break;
			default:
				fatalx("%s: unknown af", __func__);
			}

			do {
				if (req->flags & F_SHORTER) {
					for (plen = 0; plen <= req->prefixlen;
					    plen++) {
						p = prefix_adjout_lookup(peer,
						    &req->prefix, plen);
						/* dump all matching paths */
						while (p != NULL) {
							rde_dump_adjout_upcall(
							    p, ctx);
							p = prefix_adjout_next(
							    peer, p);
						}
					}
					p = NULL;
				} else if (req->prefixlen == hostplen) {
					p = prefix_adjout_match(peer,
					    &req->prefix);
				} else {
					p = prefix_adjout_lookup(peer,
					    &req->prefix, req->prefixlen);
				}
				/* dump all matching paths */
				while (p != NULL) {
					rde_dump_adjout_upcall(p, ctx);
					p = prefix_adjout_next(peer, p);
				}
			} while ((peer = peer_match(&req->neighbor,
			    peer->conf.id)));

			imsg_compose(ibuf_se_ctl, IMSG_CTL_END, 0, ctx->req.pid,
			    -1, NULL, 0);
			free(ctx);
			return;
		default:
			fatalx("%s: unsupported imsg type", __func__);
		}

		LIST_INSERT_HEAD(&rde_dump_h, ctx, entry);
		return;
	} else if ((rid = rib_find(req->rib)) == RIB_NOTFOUND) {
		log_warnx("%s: no such rib %s", __func__, req->rib);
		error = CTL_RES_NOSUCHRIB;
		imsg_compose(ibuf_se_ctl, IMSG_CTL_RESULT, 0, pid, -1, &error,
		    sizeof(error));
		free(ctx);
		return;
	}

	switch (ctx->req.type) {
	case IMSG_CTL_SHOW_NETWORK:
		if (rib_dump_new(rid, ctx->req.aid, CTL_MSG_HIGH_MARK, ctx,
		    network_dump_upcall, rde_dump_done,
		    rde_dump_throttled) == -1)
			goto nomem;
		break;
	case IMSG_CTL_SHOW_RIB:
		if (rib_dump_new(rid, ctx->req.aid, CTL_MSG_HIGH_MARK, ctx,
		    rde_dump_upcall, rde_dump_done, rde_dump_throttled) == -1)
			goto nomem;
		break;
	case IMSG_CTL_SHOW_RIB_PREFIX:
		if (req->flags & F_LONGER) {
			if (rib_dump_subtree(rid, &req->prefix, req->prefixlen,
			    CTL_MSG_HIGH_MARK, ctx, rde_dump_upcall,
			    rde_dump_done, rde_dump_throttled) == -1)
				goto nomem;
			break;
		}
		switch (req->prefix.aid) {
		case AID_INET:
		case AID_VPN_IPv4:
			hostplen = 32;
			break;
		case AID_INET6:
		case AID_VPN_IPv6:
			hostplen = 128;
			break;
		default:
			fatalx("%s: unknown af", __func__);
		}

		if (req->flags & F_SHORTER) {
			for (plen = 0; plen <= req->prefixlen; plen++) {
				re = rib_get_addr(rib_byid(rid), &req->prefix,
				    plen);
				rde_dump_upcall(re, ctx);
			}
		} else if (req->prefixlen == hostplen) {
			re = rib_match(rib_byid(rid), &req->prefix);
			rde_dump_upcall(re, ctx);
		} else {
			re = rib_get_addr(rib_byid(rid), &req->prefix,
			    req->prefixlen);
			rde_dump_upcall(re, ctx);
		}
		imsg_compose(ibuf_se_ctl, IMSG_CTL_END, 0, ctx->req.pid,
		    -1, NULL, 0);
		free(ctx);
		return;
	default:
		fatalx("%s: unsupported imsg type", __func__);
	}
	LIST_INSERT_HEAD(&rde_dump_h, ctx, entry);
}

void
rde_dump_ctx_throttle(pid_t pid, int throttle)
{
	struct rde_dump_ctx	*ctx;

	LIST_FOREACH(ctx, &rde_dump_h, entry) {
		if (ctx->req.pid == pid) {
			ctx->throttled = throttle;
			return;
		}
	}
}

void
rde_dump_ctx_terminate(pid_t pid)
{
	struct rde_dump_ctx	*ctx;

	LIST_FOREACH(ctx, &rde_dump_h, entry) {
		if (ctx->req.pid == pid) {
			rib_dump_terminate(ctx);
			return;
		}
	}
}

static int
rde_mrt_throttled(void *arg)
{
	struct mrt	*mrt = arg;

	return (msgbuf_queuelen(mrt->wbuf) > SESS_MSG_LOW_MARK);
}

static void
rde_mrt_done(void *ptr, uint8_t aid)
{
	mrt_done(ptr);
}

void
rde_dump_mrt_new(struct mrt *mrt, pid_t pid, int fd)
{
	struct rde_mrt_ctx *ctx;
	uint16_t rid;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL) {
		log_warn("rde_dump_mrt_new");
		return;
	}
	memcpy(&ctx->mrt, mrt, sizeof(struct mrt));
	if ((ctx->mrt.wbuf = msgbuf_new()) == NULL) {
		log_warn("rde_dump_mrt_new");
		free(ctx);
		return;
	}
	ctx->mrt.fd = fd;
	ctx->mrt.state = MRT_STATE_RUNNING;
	rid = rib_find(ctx->mrt.rib);
	if (rid == RIB_NOTFOUND) {
		log_warnx("non existing RIB %s for mrt dump", ctx->mrt.rib);
		free(ctx);
		return;
	}

	if (ctx->mrt.type == MRT_TABLE_DUMP_V2)
		mrt_dump_v2_hdr(&ctx->mrt, conf);

	if (rib_dump_new(rid, AID_UNSPEC, CTL_MSG_HIGH_MARK, &ctx->mrt,
	    mrt_dump_upcall, rde_mrt_done, rde_mrt_throttled) == -1)
		fatal("%s: rib_dump_new", __func__);

	LIST_INSERT_HEAD(&rde_mrts, ctx, entry);
	rde_mrt_cnt++;
}

/*
 * kroute specific functions
 */
int
rde_l3vpn_import(struct rde_community *comm, struct l3vpn *rd)
{
	struct filter_set	*s;

	TAILQ_FOREACH(s, &rd->import, entry) {
		if (community_match(comm, &s->action.community, 0))
			return (1);
	}
	return (0);
}

void
rde_send_kroute_flush(struct rib *rib)
{
	if (imsg_compose(ibuf_main, IMSG_KROUTE_FLUSH, rib->rtableid, 0, -1,
	    NULL, 0) == -1)
		fatal("%s %d imsg_compose error", __func__, __LINE__);
}

void
rde_send_kroute(struct rib *rib, struct prefix *new, struct prefix *old)
{
	struct kroute_full	 kf;
	struct prefix		*p;
	struct l3vpn		*vpn;
	enum imsg_type		 type;

	/*
	 * Make sure that self announce prefixes are not committed to the
	 * FIB. If both prefixes are unreachable no update is needed.
	 */
	if ((old == NULL || prefix_aspath(old)->flags & F_PREFIX_ANNOUNCED) &&
	    (new == NULL || prefix_aspath(new)->flags & F_PREFIX_ANNOUNCED))
		return;

	if (new == NULL || prefix_aspath(new)->flags & F_PREFIX_ANNOUNCED) {
		type = IMSG_KROUTE_DELETE;
		p = old;
	} else {
		type = IMSG_KROUTE_CHANGE;
		p = new;
	}

	memset(&kf, 0, sizeof(kf));
	pt_getaddr(p->pt, &kf.prefix);
	kf.prefixlen = p->pt->prefixlen;
	if (type == IMSG_KROUTE_CHANGE) {
		if (prefix_nhflags(p) == NEXTHOP_REJECT)
			kf.flags |= F_REJECT;
		if (prefix_nhflags(p) == NEXTHOP_BLACKHOLE)
			kf.flags |= F_BLACKHOLE;
		kf.nexthop = prefix_nexthop(p)->exit_nexthop;
		strlcpy(kf.label, rtlabel_id2name(prefix_aspath(p)->rtlabelid),
		    sizeof(kf.label));
	}

	switch (kf.prefix.aid) {
	case AID_VPN_IPv4:
		/* XXX FIB can not handle non-IPv4 nexthop */
		if (kf.nexthop.aid != AID_INET)
			type = IMSG_KROUTE_DELETE;
		/* FALLTHROUGH */
	case AID_VPN_IPv6:
		if (!(rib->flags & F_RIB_LOCAL))
			/* not Loc-RIB, no update for VPNs */
			break;

		SIMPLEQ_FOREACH(vpn, &conf->l3vpns, entry) {
			if (!rde_l3vpn_import(prefix_communities(p), vpn))
				continue;
			/* XXX not ideal but this will change */
			kf.ifindex = if_nametoindex(vpn->ifmpe);
			if (imsg_compose(ibuf_main, type, vpn->rtableid, 0, -1,
			    &kf, sizeof(kf)) == -1)
				fatal("%s %d imsg_compose error", __func__,
				    __LINE__);
		}
		break;
	case AID_INET:
		/* XXX FIB can not handle non-IPv4 nexthop */
		if (kf.nexthop.aid != AID_INET)
			type = IMSG_KROUTE_DELETE;
		/* FALLTHROUGH */
	default:
		if (imsg_compose(ibuf_main, type, rib->rtableid, 0, -1,
		    &kf, sizeof(kf)) == -1)
			fatal("%s %d imsg_compose error", __func__, __LINE__);
		break;
	}
}

/*
 * update specific functions
 */
int
rde_evaluate_all(void)
{
	return rde_eval_all;
}

/* flush Adj-RIB-Out by withdrawing all prefixes */
static void
rde_up_flush_upcall(struct prefix *p, void *ptr)
{
	prefix_adjout_withdraw(p);
}

int
rde_update_queue_pending(void)
{
	struct rde_peer *peer;
	uint8_t aid;

	if (ibuf_se && imsgbuf_queuelen(ibuf_se) >= SESS_MSG_HIGH_MARK)
		return 0;

	RB_FOREACH(peer, peer_tree, &peertable) {
		if (peer->conf.id == 0)
			continue;
		if (!peer_is_up(peer))
			continue;
		if (peer->throttled)
			continue;
		for (aid = AID_MIN; aid < AID_MAX; aid++) {
			if (!RB_EMPTY(&peer->updates[aid]) ||
			    !RB_EMPTY(&peer->withdraws[aid]))
				return 1;
		}
	}
	return 0;
}

void
rde_update_queue_runner(uint8_t aid)
{
	struct rde_peer		*peer;
	int			 sent, max = RDE_RUNNER_ROUNDS;

	/* first withdraws ... */
	do {
		sent = 0;
		RB_FOREACH(peer, peer_tree, &peertable) {
			if (peer->conf.id == 0)
				continue;
			if (!peer_is_up(peer))
				continue;
			if (peer->throttled)
				continue;
			if (RB_EMPTY(&peer->withdraws[aid]))
				continue;

			up_dump_withdraws(ibuf_se, peer, aid);
			sent++;
		}
		max -= sent;
	} while (sent != 0 && max > 0);

	/* ... then updates */
	max = RDE_RUNNER_ROUNDS;
	do {
		sent = 0;
		RB_FOREACH(peer, peer_tree, &peertable) {
			if (peer->conf.id == 0)
				continue;
			if (!peer_is_up(peer))
				continue;
			if (peer->throttled)
				continue;
			if (RB_EMPTY(&peer->updates[aid]))
				continue;

			if (up_is_eor(peer, aid)) {
				int sent_eor = peer->sent_eor & (1 << aid);
				if (peer->capa.grestart.restart && !sent_eor)
					rde_peer_send_eor(peer, aid);
				if (peer->capa.enhanced_rr && sent_eor)
					rde_peer_send_rrefresh(peer, aid,
					    ROUTE_REFRESH_END_RR);
				continue;
			}

			up_dump_update(ibuf_se, peer, aid);
			sent++;
		}
		max -= sent;
	} while (sent != 0 && max > 0);
}

/*
 * pf table specific functions
 */
struct rde_pftable_node {
	RB_ENTRY(rde_pftable_node)	 entry;
	struct pt_entry			*prefix;
	int				 refcnt;
	uint16_t			 id;
};
RB_HEAD(rde_pftable_tree, rde_pftable_node);

static inline int
rde_pftable_cmp(struct rde_pftable_node *a, struct rde_pftable_node *b)
{
	if (a->prefix > b->prefix)
		return 1;
	if (a->prefix < b->prefix)
		return -1;
	return (a->id - b->id);
}

RB_GENERATE_STATIC(rde_pftable_tree, rde_pftable_node, entry, rde_pftable_cmp);

struct rde_pftable_tree pftable_tree = RB_INITIALIZER(&pftable_tree);
int need_commit;

static void
rde_pftable_send(uint16_t id, struct pt_entry *pt, int del)
{
	struct pftable_msg pfm;

	if (id == 0)
		return;

	/* do not run while cleaning up */
	if (rde_quit)
		return;

	memset(&pfm, 0, sizeof(pfm));
	strlcpy(pfm.pftable, pftable_id2name(id), sizeof(pfm.pftable));
	pt_getaddr(pt, &pfm.addr);
	pfm.len = pt->prefixlen;

	if (imsg_compose(ibuf_main,
	    del ? IMSG_PFTABLE_REMOVE : IMSG_PFTABLE_ADD,
	    0, 0, -1, &pfm, sizeof(pfm)) == -1)
		fatal("%s %d imsg_compose error", __func__, __LINE__);

	need_commit = 1;
}

void
rde_pftable_add(uint16_t id, struct prefix *p)
{
	struct rde_pftable_node *pfn, node;

	memset(&node, 0, sizeof(node));
	node.prefix = p->pt;
	node.id = id;

	pfn = RB_FIND(rde_pftable_tree, &pftable_tree, &node);
	if (pfn == NULL) {
		if ((pfn = calloc(1, sizeof(*pfn))) == NULL)
			fatal("%s", __func__);
		pfn->prefix = pt_ref(p->pt);
		pfn->id = id;

		if (RB_INSERT(rde_pftable_tree, &pftable_tree, pfn) != NULL)
			fatalx("%s: tree corrupt", __func__);

		rde_pftable_send(id, p->pt, 0);
	}
	pfn->refcnt++;
}

void
rde_pftable_del(uint16_t id, struct prefix *p)
{
	struct rde_pftable_node *pfn, node;

	memset(&node, 0, sizeof(node));
	node.prefix = p->pt;
	node.id = id;

	pfn = RB_FIND(rde_pftable_tree, &pftable_tree, &node);
	if (pfn == NULL)
		return;

	if (--pfn->refcnt <= 0) {
		rde_pftable_send(id, p->pt, 1);

		if (RB_REMOVE(rde_pftable_tree, &pftable_tree, pfn) == NULL)
			fatalx("%s: tree corrupt", __func__);

		pt_unref(pfn->prefix);
		free(pfn);
	}
}

void
rde_commit_pftable(void)
{
	/* do not run while cleaning up */
	if (rde_quit)
		return;

	if (!need_commit)
		return;

	if (imsg_compose(ibuf_main, IMSG_PFTABLE_COMMIT, 0, 0, -1, NULL, 0) ==
	    -1)
		fatal("%s %d imsg_compose error", __func__, __LINE__);

	need_commit = 0;
}

/*
 * nexthop specific functions
 */
void
rde_send_nexthop(struct bgpd_addr *next, int insert)
{
	int			 type;

	if (insert)
		type = IMSG_NEXTHOP_ADD;
	else
		type = IMSG_NEXTHOP_REMOVE;

	if (imsg_compose(ibuf_main, type, 0, 0, -1, next,
	    sizeof(struct bgpd_addr)) == -1)
		fatal("%s %d imsg_compose error", __func__, __LINE__);
}

/*
 * soft reconfig specific functions
 */
void
rde_reload_done(void)
{
	struct rde_peer		*peer;
	struct filter_head	*fh;
	struct rde_prefixset_head prefixsets_old;
	struct rde_prefixset_head originsets_old;
	struct as_set_head	 as_sets_old;
	uint16_t		 rid;
	int			 reload = 0, force_locrib = 0;

	softreconfig = 0;

	SIMPLEQ_INIT(&prefixsets_old);
	SIMPLEQ_INIT(&originsets_old);
	SIMPLEQ_INIT(&as_sets_old);
	SIMPLEQ_CONCAT(&prefixsets_old, &conf->rde_prefixsets);
	SIMPLEQ_CONCAT(&originsets_old, &conf->rde_originsets);
	SIMPLEQ_CONCAT(&as_sets_old, &conf->as_sets);

	/* run softreconfig in if filter mode changed */
	if (conf->filtered_in_locrib != nconf->filtered_in_locrib) {
		log_debug("filter mode changed, reloading Loc-Rib");
		force_locrib = 1;
	}

	/* merge the main config */
	copy_config(conf, nconf);

	/* need to copy the sets and roa table and clear them in nconf */
	SIMPLEQ_CONCAT(&conf->rde_prefixsets, &nconf->rde_prefixsets);
	SIMPLEQ_CONCAT(&conf->rde_originsets, &nconf->rde_originsets);
	SIMPLEQ_CONCAT(&conf->as_sets, &nconf->as_sets);

	/* apply new set of l3vpn, sync will be done later */
	free_l3vpns(&conf->l3vpns);
	SIMPLEQ_CONCAT(&conf->l3vpns, &nconf->l3vpns);
	/* XXX WHERE IS THE SYNC ??? */

	free_config(nconf);
	nconf = NULL;

	/* sync peerself with conf */
	peerself->remote_bgpid = conf->bgpid;
	peerself->conf.local_as = conf->as;
	peerself->conf.remote_as = conf->as;
	peerself->conf.remote_addr.aid = AID_INET;
	peerself->conf.remote_addr.v4.s_addr = htonl(conf->bgpid);
	peerself->conf.remote_masklen = 32;
	peerself->short_as = conf->short_as;

	rde_mark_prefixsets_dirty(&prefixsets_old, &conf->rde_prefixsets);
	rde_mark_prefixsets_dirty(&originsets_old, &conf->rde_originsets);
	as_sets_mark_dirty(&as_sets_old, &conf->as_sets);


	/* make sure that rde_eval_all is correctly set after a config change */
	rde_eval_all = 0;

	/* Make the new outbound filter rules the active one. */
	filterlist_free(out_rules);
	out_rules = out_rules_tmp;
	out_rules_tmp = NULL;

	/* check if filter changed */
	RB_FOREACH(peer, peer_tree, &peertable) {
		if (peer->conf.id == 0)	/* ignore peerself */
			continue;
		peer->reconf_out = 0;
		peer->reconf_rib = 0;

		/* max prefix checker */
		if (peer->conf.max_prefix &&
		    peer->stats.prefix_cnt > peer->conf.max_prefix) {
			log_peer_warnx(&peer->conf,
			    "prefix limit reached (>%u/%u)",
			    peer->stats.prefix_cnt, peer->conf.max_prefix);
			rde_update_err(peer, ERR_CEASE, ERR_CEASE_MAX_PREFIX,
			    NULL);
		}
		/* max prefix checker outbound */
		if (peer->conf.max_out_prefix &&
		    peer->stats.prefix_out_cnt > peer->conf.max_out_prefix) {
			log_peer_warnx(&peer->conf,
			    "outbound prefix limit reached (>%u/%u)",
			    peer->stats.prefix_out_cnt,
			    peer->conf.max_out_prefix);
			rde_update_err(peer, ERR_CEASE,
			    ERR_CEASE_MAX_SENT_PREFIX, NULL);
		}

		if (peer->export_type != peer->conf.export_type) {
			log_peer_info(&peer->conf, "export type change, "
			    "reloading");
			peer->reconf_rib = 1;
		}
		if ((peer->flags & PEERFLAG_EVALUATE_ALL) !=
		    (peer->conf.flags & PEERFLAG_EVALUATE_ALL)) {
			log_peer_info(&peer->conf, "rde evaluate change, "
			    "reloading");
			peer->reconf_rib = 1;
		}
		if ((peer->flags & PEERFLAG_TRANS_AS) !=
		    (peer->conf.flags & PEERFLAG_TRANS_AS)) {
			log_peer_info(&peer->conf, "transparent-as change, "
			    "reloading");
			peer->reconf_rib = 1;
		}
		if (peer->loc_rib_id != rib_find(peer->conf.rib)) {
			log_peer_info(&peer->conf, "rib change, reloading");
			peer->loc_rib_id = rib_find(peer->conf.rib);
			if (peer->loc_rib_id == RIB_NOTFOUND)
				fatalx("King Bula's peer met an unknown RIB");
			peer->reconf_rib = 1;
		}
		/*
		 * Update add-path settings but only if the session is
		 * running with add-path and the config uses add-path
		 * as well.
		 */
		if (peer_has_add_path(peer, AID_UNSPEC, CAPA_AP_SEND)) {
			if (peer->conf.eval.mode != ADDPATH_EVAL_NONE &&
			    memcmp(&peer->eval, &peer->conf.eval,
			    sizeof(peer->eval)) != 0) {
				log_peer_info(&peer->conf,
				    "addpath eval change, reloading");
				peer->reconf_out = 1;
				peer->eval = peer->conf.eval;
			}
			/* add-path send needs rde_eval_all */
			rde_eval_all = 1;
		}
		if (peer->role != peer->conf.role) {
			if (reload == 0)
				log_debug("peer role change: "
				    "reloading Adj-RIB-In");
			peer->role = peer->conf.role;
			reload++;
		}
		peer->export_type = peer->conf.export_type;
		peer->flags = peer->conf.flags;
		if (peer->flags & PEERFLAG_EVALUATE_ALL)
			rde_eval_all = 1;

		if (peer->reconf_rib) {
			if (prefix_dump_new(peer, AID_UNSPEC,
			    RDE_RUNNER_ROUNDS, NULL, rde_up_flush_upcall,
			    rde_softreconfig_in_done, NULL) == -1)
				fatal("%s: prefix_dump_new", __func__);
			log_peer_info(&peer->conf, "flushing Adj-RIB-Out");
			softreconfig++;	/* account for the running flush */
			continue;
		}

		/* reapply outbound filters for this peer */
		fh = peer_apply_out_filter(peer, out_rules);

		if (!rde_filter_equal(peer->out_rules, fh)) {
			char *p = log_fmt_peer(&peer->conf);
			log_debug("out filter change: reloading peer %s", p);
			free(p);
			peer->reconf_out = 1;
		}
		filterlist_free(fh);
	}

	/* bring ribs in sync */
	for (rid = RIB_LOC_START; rid < rib_size; rid++) {
		struct rib *rib = rib_byid(rid);
		if (rib == NULL)
			continue;
		rde_filter_calc_skip_steps(rib->in_rules_tmp);

		/* flip rules, make new active */
		fh = rib->in_rules;
		rib->in_rules = rib->in_rules_tmp;
		rib->in_rules_tmp = fh;

		switch (rib->state) {
		case RECONF_DELETE:
			rib_free(rib);
			break;
		case RECONF_RELOAD:
			if (rib_update(rib)) {
				RB_FOREACH(peer, peer_tree, &peertable) {
					/* ignore peerself */
					if (peer->conf.id == 0)
						continue;
					/* skip peers using a different rib */
					if (peer->loc_rib_id != rib->id)
						continue;
					/* peer rib is already being flushed */
					if (peer->reconf_rib)
						continue;

					if (prefix_dump_new(peer, AID_UNSPEC,
					    RDE_RUNNER_ROUNDS, NULL,
					    rde_up_flush_upcall,
					    rde_softreconfig_in_done,
					    NULL) == -1)
						fatal("%s: prefix_dump_new",
						    __func__);

					log_peer_info(&peer->conf,
					    "flushing Adj-RIB-Out");
					/* account for the running flush */
					softreconfig++;
				}
			}

			rib->state = RECONF_KEEP;
			/* FALLTHROUGH */
		case RECONF_KEEP:
			if (!(force_locrib && rid == RIB_LOC_START) &&
			    rde_filter_equal(rib->in_rules, rib->in_rules_tmp))
				/* rib is in sync */
				break;
			log_debug("filter change: reloading RIB %s",
			    rib->name);
			rib->state = RECONF_RELOAD;
			reload++;
			break;
		case RECONF_REINIT:
			/* new rib */
			rib->state = RECONF_RELOAD;
			reload++;
			break;
		case RECONF_NONE:
			break;
		}
		filterlist_free(rib->in_rules_tmp);
		rib->in_rules_tmp = NULL;
	}

	/* old filters removed, free all sets */
	free_rde_prefixsets(&prefixsets_old);
	free_rde_prefixsets(&originsets_old);
	as_sets_free(&as_sets_old);

	log_info("RDE reconfigured");

	softreconfig++;
	if (reload > 0) {
		if (rib_dump_new(RIB_ADJ_IN, AID_UNSPEC, RDE_RUNNER_ROUNDS,
		    NULL, rde_softreconfig_in, rde_softreconfig_in_done,
		    NULL) == -1)
			fatal("%s: rib_dump_new", __func__);
		log_info("running softreconfig in");
	} else {
		rde_softreconfig_in_done((void *)1, AID_UNSPEC);
	}
}

static void
rde_softreconfig_in_done(void *arg, uint8_t dummy)
{
	struct rde_peer	*peer;
	uint16_t	 i;

	softreconfig--;
	/* one guy done but other dumps are still running */
	if (softreconfig > 0)
		return;

	if (arg == NULL)
		log_info("softreconfig in done");

	/* now do the Adj-RIB-Out sync and a possible FIB sync */
	softreconfig = 0;
	for (i = 0; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		rib->state = RECONF_NONE;
		if (rib->fibstate == RECONF_RELOAD) {
			if (rib_dump_new(i, AID_UNSPEC, RDE_RUNNER_ROUNDS,
			    rib, rde_softreconfig_sync_fib,
			    rde_softreconfig_sync_done, NULL) == -1)
				fatal("%s: rib_dump_new", __func__);
			softreconfig++;
			log_info("starting fib sync for rib %s",
			    rib->name);
		} else if (rib->fibstate == RECONF_REINIT) {
			if (rib_dump_new(i, AID_UNSPEC, RDE_RUNNER_ROUNDS,
			    rib, rde_softreconfig_sync_reeval,
			    rde_softreconfig_sync_done, NULL) == -1)
				fatal("%s: rib_dump_new", __func__);
			softreconfig++;
			log_info("starting re-evaluation of rib %s",
			    rib->name);
		}
	}

	RB_FOREACH(peer, peer_tree, &peertable) {
		uint8_t aid;

		if (peer->reconf_out) {
			if (peer->export_type == EXPORT_NONE) {
				/* nothing to do here */
				peer->reconf_out = 0;
			} else if (peer->export_type == EXPORT_DEFAULT_ROUTE) {
				/* just resend the default route */
				for (aid = AID_MIN; aid < AID_MAX; aid++) {
					if (peer->capa.mp[aid])
						up_generate_default(peer, aid);
				}
				peer->reconf_out = 0;
			} else
				rib_byid(peer->loc_rib_id)->state =
				    RECONF_RELOAD;
		} else if (peer->reconf_rib) {
			/* dump the full table to neighbors that changed rib */
			for (aid = AID_MIN; aid < AID_MAX; aid++) {
				if (peer->capa.mp[aid])
					peer_dump(peer, aid);
			}
		}
	}

	for (i = 0; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		if (rib->state == RECONF_RELOAD) {
			if (rib_dump_new(i, AID_UNSPEC, RDE_RUNNER_ROUNDS,
			    rib, rde_softreconfig_out,
			    rde_softreconfig_out_done, NULL) == -1)
				fatal("%s: rib_dump_new", __func__);
			softreconfig++;
			log_info("starting softreconfig out for rib %s",
			    rib->name);
		}
	}

	/* if nothing to do move to last stage */
	if (softreconfig == 0)
		rde_softreconfig_done();
}

static void
rde_softreconfig_out_done(void *arg, uint8_t aid)
{
	struct rib	*rib = arg;

	/* this RIB dump is done */
	log_info("softreconfig out done for %s", rib->name);

	/* check if other dumps are still running */
	if (--softreconfig == 0)
		rde_softreconfig_done();
}

static void
rde_softreconfig_done(void)
{
	uint16_t	i;

	for (i = 0; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		rib->state = RECONF_NONE;
	}

	log_info("RDE soft reconfiguration done");
	imsg_compose(ibuf_main, IMSG_RECONF_DONE, 0, 0,
	    -1, NULL, 0);
}

static void
rde_softreconfig_in(struct rib_entry *re, void *bula)
{
	struct filterstate	 state;
	struct rib		*rib;
	struct prefix		*p;
	struct pt_entry		*pt;
	struct rde_peer		*peer;
	struct rde_aspath	*asp;
	enum filter_actions	 action;
	struct bgpd_addr	 prefix;
	uint16_t		 i;
	uint8_t			 aspa_vstate;

	pt = re->prefix;
	pt_getaddr(pt, &prefix);
	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib) {
		asp = prefix_aspath(p);
		peer = prefix_peer(p);

		/* possible role change update ASPA validation state */
		if (prefix_aspa_vstate(p) == ASPA_NEVER_KNOWN)
			aspa_vstate = ASPA_NEVER_KNOWN;
		else
			aspa_vstate = rde_aspa_validity(peer, asp, pt->aid);
		prefix_set_vstate(p, prefix_roa_vstate(p), aspa_vstate);

		/* skip announced networks, they are never filtered */
		if (asp->flags & F_PREFIX_ANNOUNCED)
			continue;

		for (i = RIB_LOC_START; i < rib_size; i++) {
			rib = rib_byid(i);
			if (rib == NULL)
				continue;

			if (rib->state != RECONF_RELOAD)
				continue;

			rde_filterstate_prep(&state, p);
			action = rde_filter(rib->in_rules, peer, peer, &prefix,
			    pt->prefixlen, &state);

			if (action == ACTION_ALLOW) {
				/* update Local-RIB */
				prefix_update(rib, peer, p->path_id,
				    p->path_id_tx, &state, 0,
				    &prefix, pt->prefixlen);
			} else if (conf->filtered_in_locrib &&
			    i == RIB_LOC_START) {
				prefix_update(rib, peer, p->path_id,
				    p->path_id_tx, &state, 1,
				    &prefix, pt->prefixlen);
			} else {
				/* remove from Local-RIB */
				prefix_withdraw(rib, peer, p->path_id, &prefix,
				    pt->prefixlen);
			}

			rde_filterstate_clean(&state);
		}
	}
}

static void
rde_softreconfig_out(struct rib_entry *re, void *arg)
{
	if (prefix_best(re) == NULL)
		/* no valid path for prefix */
		return;

	rde_generate_updates(re, NULL, NULL, EVAL_RECONF);
}

static void
rde_softreconfig_sync_reeval(struct rib_entry *re, void *arg)
{
	struct prefix_queue	prefixes = TAILQ_HEAD_INITIALIZER(prefixes);
	struct prefix		*p, *next;
	struct rib		*rib = arg;

	if (rib->flags & F_RIB_NOEVALUATE) {
		/*
		 * evaluation process is turned off
		 * all dependent adj-rib-out were already flushed
		 * unlink nexthop if it was linked
		 */
		TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib) {
			if (p->flags & PREFIX_NEXTHOP_LINKED)
				nexthop_unlink(p);
			p->dmetric = PREFIX_DMETRIC_INVALID;
		}
		return;
	}

	/* evaluation process is turned on, so evaluate all prefixes again */
	TAILQ_CONCAT(&prefixes, &re->prefix_h, entry.list.rib);

	/*
	 * TODO: this code works but is not optimal. prefix_evaluate()
	 * does a lot of extra work in the worst case. Would be better
	 * to resort the list once and then call rde_generate_updates()
	 * and rde_send_kroute() once.
	 */
	TAILQ_FOREACH_SAFE(p, &prefixes, entry.list.rib, next) {
		/* need to re-link the nexthop if not already linked */
		TAILQ_REMOVE(&prefixes, p, entry.list.rib);
		if ((p->flags & PREFIX_NEXTHOP_LINKED) == 0)
			nexthop_link(p);
		prefix_evaluate(re, p, NULL);
	}
}

static void
rde_softreconfig_sync_fib(struct rib_entry *re, void *bula)
{
	struct prefix *p;

	if ((p = prefix_best(re)) != NULL)
		rde_send_kroute(re_rib(re), p, NULL);
}

static void
rde_softreconfig_sync_done(void *arg, uint8_t aid)
{
	struct rib *rib = arg;

	/* this RIB dump is done */
	if (rib->fibstate == RECONF_RELOAD)
		log_info("fib sync done for %s", rib->name);
	else
		log_info("re-evaluation done for %s", rib->name);
	rib->fibstate = RECONF_NONE;

	/* check if other dumps are still running */
	if (--softreconfig == 0)
		rde_softreconfig_done();
}

/*
 * ROA specific functions. The roa set is updated independent of the config
 * so this runs outside of the softreconfig handlers.
 */
static void
rde_rpki_softreload(struct rib_entry *re, void *bula)
{
	struct filterstate	 state;
	struct rib		*rib;
	struct prefix		*p;
	struct pt_entry		*pt;
	struct rde_peer		*peer;
	struct rde_aspath	*asp;
	enum filter_actions	 action;
	struct bgpd_addr	 prefix;
	uint8_t			 roa_vstate, aspa_vstate;
	uint16_t		 i;

	pt = re->prefix;
	pt_getaddr(pt, &prefix);
	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib) {
		asp = prefix_aspath(p);
		peer = prefix_peer(p);

		/* ROA validation state update */
		roa_vstate = rde_roa_validity(&rde_roa,
		    &prefix, pt->prefixlen, aspath_origin(asp->aspath));

		/* ASPA validation state update (if needed) */
		if (prefix_aspa_vstate(p) == ASPA_NEVER_KNOWN) {
			aspa_vstate = ASPA_NEVER_KNOWN;
		} else {
			if (asp->aspa_generation != rde_aspa_generation) {
				asp->aspa_generation = rde_aspa_generation;
				aspa_validation(rde_aspa, asp->aspath,
				    &asp->aspa_state);
			}
			aspa_vstate = rde_aspa_validity(peer, asp, pt->aid);
		}

		if (roa_vstate == prefix_roa_vstate(p) &&
		    aspa_vstate == prefix_aspa_vstate(p))
			continue;

		prefix_set_vstate(p, roa_vstate, aspa_vstate);
		/* skip announced networks, they are never filtered */
		if (asp->flags & F_PREFIX_ANNOUNCED)
			continue;

		for (i = RIB_LOC_START; i < rib_size; i++) {
			rib = rib_byid(i);
			if (rib == NULL)
				continue;

			rde_filterstate_prep(&state, p);
			action = rde_filter(rib->in_rules, peer, peer, &prefix,
			    pt->prefixlen, &state);

			if (action == ACTION_ALLOW) {
				/* update Local-RIB */
				prefix_update(rib, peer, p->path_id,
				    p->path_id_tx, &state, 0,
				    &prefix, pt->prefixlen);
			} else if (conf->filtered_in_locrib &&
			    i == RIB_LOC_START) {
				prefix_update(rib, peer, p->path_id,
				    p->path_id_tx, &state, 1,
				    &prefix, pt->prefixlen);
			} else {
				/* remove from Local-RIB */
				prefix_withdraw(rib, peer, p->path_id, &prefix,
				    pt->prefixlen);
			}

			rde_filterstate_clean(&state);
		}
	}
}

static int rpki_update_pending;

static void
rde_rpki_softreload_done(void *arg, uint8_t aid)
{
	/* the roa update is done */
	log_info("RPKI softreload done");
	rpki_update_pending = 0;
}

static void
rde_rpki_reload(void)
{
	if (rpki_update_pending) {
		log_info("RPKI softreload skipped, old still running");
		return;
	}

	rpki_update_pending = 1;
	if (rib_dump_new(RIB_ADJ_IN, AID_UNSPEC, RDE_RUNNER_ROUNDS,
	    rib_byid(RIB_ADJ_IN), rde_rpki_softreload,
	    rde_rpki_softreload_done, NULL) == -1)
		fatal("%s: rib_dump_new", __func__);
}

static int
rde_roa_reload(void)
{
	struct rde_prefixset roa_old;

	if (rpki_update_pending) {
		trie_free(&roa_new.th);	/* can't use new roa table */
		return 1;		/* force call to rde_rpki_reload */
	}

	roa_old = rde_roa;
	rde_roa = roa_new;
	memset(&roa_new, 0, sizeof(roa_new));

	/* check if roa changed */
	if (trie_equal(&rde_roa.th, &roa_old.th)) {
		rde_roa.lastchange = roa_old.lastchange;
		trie_free(&roa_old.th);	/* old roa no longer needed */
		return 0;
	}

	rde_roa.lastchange = getmonotime();
	trie_free(&roa_old.th);		/* old roa no longer needed */

	log_debug("ROA change: reloading Adj-RIB-In");
	return 1;
}

static int
rde_aspa_reload(void)
{
	struct rde_aspa *aspa_old;

	if (rpki_update_pending) {
		aspa_table_free(aspa_new);	/* can't use new aspa table */
		aspa_new = NULL;
		return 1;			/* rpki_client_relaod warns */
	}

	aspa_old = rde_aspa;
	rde_aspa = aspa_new;
	aspa_new = NULL;

	/* check if aspa changed */
	if (aspa_table_equal(rde_aspa, aspa_old)) {
		aspa_table_unchanged(rde_aspa, aspa_old);
		aspa_table_free(aspa_old);	/* old aspa no longer needed */
		return 0;
	}

	aspa_table_free(aspa_old);		/* old aspa no longer needed */
	log_debug("ASPA change: reloading Adj-RIB-In");
	rde_aspa_generation++;
	return 1;
}

/*
 * generic helper function
 */
uint32_t
rde_local_as(void)
{
	return (conf->as);
}

int
rde_decisionflags(void)
{
	return (conf->flags & BGPD_FLAG_DECISION_MASK);
}

/* End-of-RIB marker, RFC 4724 */
static void
rde_peer_recv_eor(struct rde_peer *peer, uint8_t aid)
{
	peer->stats.prefix_rcvd_eor++;
	peer->recv_eor |= 1 << aid;

	/*
	 * First notify SE to avert a possible race with the restart timeout.
	 * If the timeout fires before this imsg is processed by the SE it will
	 * result in the same operation since the timeout issues a FLUSH which
	 * does the same as the RESTARTED action (flushing stale routes).
	 * The logic in the SE is so that only one of FLUSH or RESTARTED will
	 * be sent back to the RDE and so peer_flush is only called once.
	 */
	if (imsg_compose(ibuf_se, IMSG_SESSION_RESTARTED, peer->conf.id,
	    0, -1, &aid, sizeof(aid)) == -1)
		fatal("imsg_compose error while receiving EoR");

	log_peer_info(&peer->conf, "received %s EOR marker",
	    aid2str(aid));
}

static void
rde_peer_send_eor(struct rde_peer *peer, uint8_t aid)
{
	uint16_t	afi;
	uint8_t		safi;

	peer->stats.prefix_sent_eor++;
	peer->sent_eor |= 1 << aid;

	if (aid == AID_INET) {
		u_char null[4];

		memset(&null, 0, 4);
		if (imsg_compose(ibuf_se, IMSG_UPDATE, peer->conf.id,
		    0, -1, &null, 4) == -1)
			fatal("imsg_compose error while sending EoR");
	} else {
		uint16_t	i;
		u_char		buf[10];

		if (aid2afi(aid, &afi, &safi) == -1)
			fatalx("peer_send_eor: bad AID");

		i = 0;	/* v4 withdrawn len */
		memcpy(&buf[0], &i, sizeof(i));
		i = htons(6);	/* path attr len */
		memcpy(&buf[2], &i, sizeof(i));
		buf[4] = ATTR_OPTIONAL;
		buf[5] = ATTR_MP_UNREACH_NLRI;
		buf[6] = 3;	/* withdrawn len */
		i = htons(afi);
		memcpy(&buf[7], &i, sizeof(i));
		buf[9] = safi;

		if (imsg_compose(ibuf_se, IMSG_UPDATE, peer->conf.id,
		    0, -1, &buf, 10) == -1)
			fatal("%s %d imsg_compose error", __func__, __LINE__);
	}

	log_peer_info(&peer->conf, "sending %s EOR marker",
	    aid2str(aid));
}

void
rde_peer_send_rrefresh(struct rde_peer *peer, uint8_t aid, uint8_t subtype)
{
	struct route_refresh rr;

	/* not strickly needed, the SE checks as well */
	if (peer->capa.enhanced_rr == 0)
		return;

	switch (subtype) {
	case ROUTE_REFRESH_END_RR:
	case ROUTE_REFRESH_BEGIN_RR:
		break;
	default:
		fatalx("%s unexpected subtype %d", __func__, subtype);
	}

	rr.aid = aid;
	rr.subtype = subtype;

	if (imsg_compose(ibuf_se, IMSG_REFRESH, peer->conf.id, 0, -1,
	    &rr, sizeof(rr)) == -1)
		fatal("%s %d imsg_compose error", __func__, __LINE__);

	log_peer_info(&peer->conf, "sending %s %s marker",
	    aid2str(aid), subtype == ROUTE_REFRESH_END_RR ? "EoRR" : "BoRR");
}

/*
 * network announcement stuff
 */
void
network_add(struct network_config *nc, struct filterstate *state)
{
	struct l3vpn		*vpn;
	struct filter_set_head	*vpnset = NULL;
	struct in_addr		 prefix4;
	struct in6_addr		 prefix6;
	uint32_t		 path_id_tx;
	uint16_t		 i;
	uint8_t			 vstate;

	if (nc->rd != 0) {
		SIMPLEQ_FOREACH(vpn, &conf->l3vpns, entry) {
			if (vpn->rd != nc->rd)
				continue;
			switch (nc->prefix.aid) {
			case AID_INET:
				prefix4 = nc->prefix.v4;
				memset(&nc->prefix, 0, sizeof(nc->prefix));
				nc->prefix.aid = AID_VPN_IPv4;
				nc->prefix.rd = vpn->rd;
				nc->prefix.v4 = prefix4;
				nc->prefix.labellen = 3;
				nc->prefix.labelstack[0] =
				    (vpn->label >> 12) & 0xff;
				nc->prefix.labelstack[1] =
				    (vpn->label >> 4) & 0xff;
				nc->prefix.labelstack[2] =
				    (vpn->label << 4) & 0xf0;
				nc->prefix.labelstack[2] |= BGP_MPLS_BOS;
				vpnset = &vpn->export;
				break;
			case AID_INET6:
				prefix6 = nc->prefix.v6;
				memset(&nc->prefix, 0, sizeof(nc->prefix));
				nc->prefix.aid = AID_VPN_IPv6;
				nc->prefix.rd = vpn->rd;
				nc->prefix.v6 = prefix6;
				nc->prefix.labellen = 3;
				nc->prefix.labelstack[0] =
				    (vpn->label >> 12) & 0xff;
				nc->prefix.labelstack[1] =
				    (vpn->label >> 4) & 0xff;
				nc->prefix.labelstack[2] =
				    (vpn->label << 4) & 0xf0;
				nc->prefix.labelstack[2] |= BGP_MPLS_BOS;
				vpnset = &vpn->export;
				break;
			default:
				log_warnx("unable to VPNize prefix");
				filterset_free(&nc->attrset);
				return;
			}
			break;
		}
		if (vpn == NULL) {
			log_warnx("network_add: "
			    "prefix %s/%u in non-existing l3vpn %s",
			    log_addr(&nc->prefix), nc->prefixlen,
			    log_rd(nc->rd));
			return;
		}
	}

	rde_apply_set(&nc->attrset, peerself, peerself, state, nc->prefix.aid);
	if (vpnset)
		rde_apply_set(vpnset, peerself, peerself, state,
		    nc->prefix.aid);

	vstate = rde_roa_validity(&rde_roa, &nc->prefix, nc->prefixlen,
	    aspath_origin(state->aspath.aspath));
	rde_filterstate_set_vstate(state, vstate, ASPA_NEVER_KNOWN);

	path_id_tx = pathid_assign(peerself, 0, &nc->prefix, nc->prefixlen);
	if (prefix_update(rib_byid(RIB_ADJ_IN), peerself, 0, path_id_tx,
	    state, 0, &nc->prefix, nc->prefixlen) == 1)
		peerself->stats.prefix_cnt++;
	for (i = RIB_LOC_START; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		rde_update_log("announce", i, peerself,
		    state->nexthop ? &state->nexthop->exit_nexthop : NULL,
		    &nc->prefix, nc->prefixlen);
		prefix_update(rib, peerself, 0, path_id_tx, state, 0,
		    &nc->prefix, nc->prefixlen);
	}
	filterset_free(&nc->attrset);
}

void
network_delete(struct network_config *nc)
{
	struct l3vpn	*vpn;
	struct in_addr	 prefix4;
	struct in6_addr	 prefix6;
	uint32_t	 i;

	if (nc->rd) {
		SIMPLEQ_FOREACH(vpn, &conf->l3vpns, entry) {
			if (vpn->rd != nc->rd)
				continue;
			switch (nc->prefix.aid) {
			case AID_INET:
				prefix4 = nc->prefix.v4;
				memset(&nc->prefix, 0, sizeof(nc->prefix));
				nc->prefix.aid = AID_VPN_IPv4;
				nc->prefix.rd = vpn->rd;
				nc->prefix.v4 = prefix4;
				nc->prefix.labellen = 3;
				nc->prefix.labelstack[0] =
				    (vpn->label >> 12) & 0xff;
				nc->prefix.labelstack[1] =
				    (vpn->label >> 4) & 0xff;
				nc->prefix.labelstack[2] =
				    (vpn->label << 4) & 0xf0;
				nc->prefix.labelstack[2] |= BGP_MPLS_BOS;
				break;
			case AID_INET6:
				prefix6 = nc->prefix.v6;
				memset(&nc->prefix, 0, sizeof(nc->prefix));
				nc->prefix.aid = AID_VPN_IPv6;
				nc->prefix.rd = vpn->rd;
				nc->prefix.v6 = prefix6;
				nc->prefix.labellen = 3;
				nc->prefix.labelstack[0] =
				    (vpn->label >> 12) & 0xff;
				nc->prefix.labelstack[1] =
				    (vpn->label >> 4) & 0xff;
				nc->prefix.labelstack[2] =
				    (vpn->label << 4) & 0xf0;
				nc->prefix.labelstack[2] |= BGP_MPLS_BOS;
				break;
			default:
				log_warnx("unable to VPNize prefix");
				return;
			}
		}
	}

	for (i = RIB_LOC_START; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		if (prefix_withdraw(rib, peerself, 0, &nc->prefix,
		    nc->prefixlen))
			rde_update_log("withdraw announce", i, peerself,
			    NULL, &nc->prefix, nc->prefixlen);
	}
	if (prefix_withdraw(rib_byid(RIB_ADJ_IN), peerself, 0, &nc->prefix,
	    nc->prefixlen))
		peerself->stats.prefix_cnt--;
}

static void
network_dump_upcall(struct rib_entry *re, void *ptr)
{
	struct prefix		*p;
	struct rde_aspath	*asp;
	struct kroute_full	 kf;
	struct bgpd_addr	 addr;
	struct rde_dump_ctx	*ctx = ptr;

	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib) {
		asp = prefix_aspath(p);
		if (!(asp->flags & F_PREFIX_ANNOUNCED))
			continue;
		pt_getaddr(p->pt, &addr);

		memset(&kf, 0, sizeof(kf));
		kf.prefix = addr;
		kf.prefixlen = p->pt->prefixlen;
		if (prefix_nhvalid(p) && prefix_nexthop(p) != NULL)
			kf.nexthop = prefix_nexthop(p)->true_nexthop;
		else
			kf.nexthop.aid = kf.prefix.aid;
		if ((asp->flags & F_ANN_DYNAMIC) == 0)
			kf.flags = F_STATIC;
		if (imsg_compose(ibuf_se_ctl, IMSG_CTL_SHOW_NETWORK, 0,
		    ctx->req.pid, -1, &kf, sizeof(kf)) == -1)
			log_warnx("%s: imsg_compose error", __func__);
	}
}

static void
network_flush_upcall(struct rib_entry *re, void *ptr)
{
	struct bgpd_addr addr;
	struct prefix *p;
	uint32_t i;
	uint8_t prefixlen;

	p = prefix_bypeer(re, peerself, 0);
	if (p == NULL)
		return;
	if ((prefix_aspath(p)->flags & F_ANN_DYNAMIC) != F_ANN_DYNAMIC)
		return;

	pt_getaddr(re->prefix, &addr);
	prefixlen = re->prefix->prefixlen;

	for (i = RIB_LOC_START; i < rib_size; i++) {
		struct rib *rib = rib_byid(i);
		if (rib == NULL)
			continue;
		if (prefix_withdraw(rib, peerself, 0, &addr, prefixlen) == 1)
			rde_update_log("flush announce", i, peerself,
			    NULL, &addr, prefixlen);
	}

	if (prefix_withdraw(rib_byid(RIB_ADJ_IN), peerself, 0, &addr,
	    prefixlen) == 1)
		peerself->stats.prefix_cnt--;
}

/*
 * flowspec announcement stuff
 */
void
flowspec_add(struct flowspec *f, struct filterstate *state,
    struct filter_set_head *attrset)
{
	struct pt_entry *pte;
	uint32_t path_id_tx;

	rde_apply_set(attrset, peerself, peerself, state, f->aid);
	rde_filterstate_set_vstate(state, ROA_NOTFOUND, ASPA_NEVER_KNOWN);
	path_id_tx = peerself->path_id_tx; /* XXX should use pathid_assign() */

	pte = pt_get_flow(f);
	if (pte == NULL)
		pte = pt_add_flow(f);

	if (prefix_flowspec_update(peerself, state, pte, path_id_tx) == 1)
		peerself->stats.prefix_cnt++;
}

void
flowspec_delete(struct flowspec *f)
{
	struct pt_entry *pte;

	pte = pt_get_flow(f);
	if (pte == NULL)
		return;

	if (prefix_flowspec_withdraw(peerself, pte) == 1)
		peerself->stats.prefix_cnt--;
}

static void
flowspec_flush_upcall(struct rib_entry *re, void *ptr)
{
	struct prefix *p;

	p = prefix_bypeer(re, peerself, 0);
	if (p == NULL)
		return;
	if ((prefix_aspath(p)->flags & F_ANN_DYNAMIC) != F_ANN_DYNAMIC)
		return;
	if (prefix_flowspec_withdraw(peerself, re->prefix) == 1)
		peerself->stats.prefix_cnt--;
}

static void
flowspec_dump_upcall(struct rib_entry *re, void *ptr)
{
	pid_t *pid = ptr;
	struct prefix		*p;
	struct rde_aspath	*asp;
	struct rde_community	*comm;
	struct flowspec		ff;
	struct ibuf		*ibuf;
	uint8_t			*flow;
	int			len;

	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib) {
		asp = prefix_aspath(p);
		if (!(asp->flags & F_PREFIX_ANNOUNCED))
			continue;
		comm = prefix_communities(p);

		len = pt_getflowspec(p->pt, &flow);

		memset(&ff, 0, sizeof(ff));
		ff.aid = p->pt->aid;
		ff.len = len;
		if ((asp->flags & F_ANN_DYNAMIC) == 0)
			ff.flags = F_STATIC;
		if ((ibuf = imsg_create(ibuf_se_ctl, IMSG_CTL_SHOW_FLOWSPEC, 0,
		    *pid, FLOWSPEC_SIZE + len)) == NULL)
				continue;
		if (imsg_add(ibuf, &ff, FLOWSPEC_SIZE) == -1 ||
		    imsg_add(ibuf, flow, len) == -1)
			continue;
		imsg_close(ibuf_se_ctl, ibuf);
		if (comm->nentries > 0) {
			if (imsg_compose(ibuf_se_ctl,
			    IMSG_CTL_SHOW_RIB_COMMUNITIES, 0, *pid, -1,
			    comm->communities,
			    comm->nentries * sizeof(struct community)) == -1)
				continue;
		}
	}
}

static void
flowspec_dump_done(void *ptr, uint8_t aid)
{
	pid_t *pid = ptr;

	imsg_compose(ibuf_se_ctl, IMSG_CTL_END, 0, *pid, -1, NULL, 0);
}


/* clean up */
void
rde_shutdown(void)
{
	/*
	 * the decision process is turned off if rde_quit = 1 and
	 * rde_shutdown depends on this.
	 */

	/* First all peers go down */
	peer_shutdown();

	/* free filters */
	filterlist_free(out_rules);
	filterlist_free(out_rules_tmp);

	/* kill the VPN configs */
	free_l3vpns(&conf->l3vpns);

	/* now check everything */
	rib_shutdown();
	nexthop_shutdown();
	path_shutdown();
	attr_shutdown();
	pt_shutdown();
}

struct rde_prefixset *
rde_find_prefixset(char *name, struct rde_prefixset_head *p)
{
	struct rde_prefixset *ps;

	SIMPLEQ_FOREACH(ps, p, entry) {
		if (!strcmp(ps->name, name))
			return (ps);
	}
	return (NULL);
}

void
rde_mark_prefixsets_dirty(struct rde_prefixset_head *psold,
    struct rde_prefixset_head *psnew)
{
	struct rde_prefixset *new, *old;

	SIMPLEQ_FOREACH(new, psnew, entry) {
		if ((psold == NULL) ||
		    (old = rde_find_prefixset(new->name, psold)) == NULL) {
			new->dirty = 1;
			new->lastchange = getmonotime();
		} else {
			if (trie_equal(&new->th, &old->th) == 0) {
				new->dirty = 1;
				new->lastchange = getmonotime();
			} else
				new->lastchange = old->lastchange;
		}
	}
}

uint8_t
rde_roa_validity(struct rde_prefixset *ps, struct bgpd_addr *prefix,
    uint8_t plen, uint32_t as)
{
	int r;

	r = trie_roa_check(&ps->th, prefix, plen, as);
	return (r & ROA_MASK);
}

static int
ovs_match(struct prefix *p, uint32_t flag)
{
	if (flag & (F_CTL_OVS_VALID|F_CTL_OVS_INVALID|F_CTL_OVS_NOTFOUND)) {
		switch (prefix_roa_vstate(p)) {
		case ROA_VALID:
			if (!(flag & F_CTL_OVS_VALID))
				return 0;
			break;
		case ROA_INVALID:
			if (!(flag & F_CTL_OVS_INVALID))
				return 0;
			break;
		case ROA_NOTFOUND:
			if (!(flag & F_CTL_OVS_NOTFOUND))
				return 0;
			break;
		default:
			break;
		}
	}

	return 1;
}

static int
avs_match(struct prefix *p, uint32_t flag)
{
	if (flag & (F_CTL_AVS_VALID|F_CTL_AVS_INVALID|F_CTL_AVS_UNKNOWN)) {
		switch (prefix_aspa_vstate(p) & ASPA_MASK) {
		case ASPA_VALID:
			if (!(flag & F_CTL_AVS_VALID))
				return 0;
			break;
		case ASPA_INVALID:
			if (!(flag & F_CTL_AVS_INVALID))
				return 0;
			break;
		case ASPA_UNKNOWN:
			if (!(flag & F_CTL_AVS_UNKNOWN))
				return 0;
			break;
		default:
			break;
		}
	}

	return 1;
}
