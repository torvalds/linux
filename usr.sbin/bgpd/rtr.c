/*	$OpenBSD: rtr.c,v 1.31 2025/04/14 14:50:29 claudio Exp $ */

/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/tree.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

static void	rtr_dispatch_imsg_parent(struct imsgbuf *);
static void	rtr_dispatch_imsg_rde(struct imsgbuf *);

volatile sig_atomic_t		 rtr_quit;
static struct imsgbuf		*ibuf_main;
static struct imsgbuf		*ibuf_rde;
static struct bgpd_config	*conf, *nconf;
static struct timer_head	 expire_timer;
static int			 rtr_recalc_semaphore;

static void
rtr_sighdlr(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		rtr_quit = 1;
		break;
	}
}

#define PFD_PIPE_MAIN	0
#define PFD_PIPE_RDE	1
#define PFD_PIPE_COUNT	2

#define EXPIRE_TIMEOUT	300

void
rtr_sem_acquire(int cnt)
{
	rtr_recalc_semaphore += cnt;
}

void
rtr_sem_release(int cnt)
{
	rtr_recalc_semaphore -= cnt;
	if (rtr_recalc_semaphore < 0)
		fatalx("rtr recalc semaphore underflow");
}

/*
 * Every EXPIRE_TIMEOUT seconds traverse the static roa-set table and expire
 * all elements where the expires timestamp is smaller or equal to now.
 * If any change is done recalculate the RTR table.
 */
static unsigned int
rtr_expire_roas(time_t now)
{
	struct roa *roa, *nr;
	unsigned int recalc = 0;

	RB_FOREACH_SAFE(roa, roa_tree, &conf->roa, nr) {
		if (roa->expires != 0 && roa->expires <= now) {
			recalc++;
			RB_REMOVE(roa_tree, &conf->roa, roa);
			free(roa);
		}
	}
	if (recalc != 0)
		log_info("%u roa-set entries expired", recalc);
	return recalc;
}

static unsigned int
rtr_expire_aspa(time_t now)
{
	struct aspa_set *aspa, *na;
	unsigned int recalc = 0;

	RB_FOREACH_SAFE(aspa, aspa_tree, &conf->aspa, na) {
		if (aspa->expires != 0 && aspa->expires <= now) {
			recalc++;
			RB_REMOVE(aspa_tree, &conf->aspa, aspa);
			free_aspa(aspa);
		}
	}
	if (recalc != 0)
		log_info("%u aspa-set entries expired", recalc);
	return recalc;
}

void
rtr_roa_insert(struct roa_tree *rt, struct roa *in)
{
	struct roa *roa;

	if ((roa = malloc(sizeof(*roa))) == NULL)
		fatal("roa alloc");
	memcpy(roa, in, sizeof(*roa));
	if (RB_INSERT(roa_tree, rt, roa) != NULL)
		/* just ignore duplicates */
		free(roa);
}

/*
 * Add an asnum to the aspa_set. The aspa_set is sorted by asnum.
 */
static void
aspa_set_entry(struct aspa_set *aspa, uint32_t asnum)
{
	uint32_t i, num, *newtas;

	for (i = 0; i < aspa->num; i++) {
		if (asnum < aspa->tas[i])
			break;
		if (asnum == aspa->tas[i])
			return;
	}

	num = aspa->num + 1;
	newtas = reallocarray(aspa->tas, num, sizeof(uint32_t));
	if (newtas == NULL)
		fatal("aspa_set merge");

	if (i < aspa->num) {
		memmove(newtas + i + 1, newtas + i,
		    (aspa->num - i) * sizeof(uint32_t));
	}
	newtas[i] = asnum;

	aspa->num = num;
	aspa->tas = newtas;
}

/*
 * Insert and merge an aspa_set into the aspa_tree at.
 */
void
rtr_aspa_insert(struct aspa_tree *at, struct aspa_set *mergeset)
{
	struct aspa_set *aspa, needle = { .as = mergeset->as };
	uint32_t i;

	aspa = RB_FIND(aspa_tree, at, &needle);
	if (aspa == NULL) {
		if ((aspa = calloc(1, sizeof(*aspa))) == NULL)
			fatal("aspa insert");
		aspa->as = mergeset->as;
		RB_INSERT(aspa_tree, at, aspa);
	}

	for (i = 0; i < mergeset->num; i++)
		aspa_set_entry(aspa, mergeset->tas[i]);
}

void
rtr_main(int debug, int verbose)
{
	struct passwd		*pw;
	struct pollfd		*pfd = NULL;
	void			*newp;
	size_t			 pfd_elms = 0, i;
	monotime_t		 timeout;

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	log_procinit(log_procnames[PROC_RTR]);

	if ((pw = getpwnam(BGPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("rtr engine");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	signal(SIGTERM, rtr_sighdlr);
	signal(SIGINT, rtr_sighdlr);
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

	conf = new_config();
	log_info("rtr engine ready");

	TAILQ_INIT(&expire_timer);
	timer_set(&expire_timer, Timer_Rtr_Expire, EXPIRE_TIMEOUT);

	while (rtr_quit == 0) {
		i = rtr_count();
		if (pfd_elms < PFD_PIPE_COUNT + i) {
			if ((newp = reallocarray(pfd,
			    PFD_PIPE_COUNT + i,
			    sizeof(struct pollfd))) == NULL)
				fatal("realloc pollfd");
			pfd = newp;
			pfd_elms = PFD_PIPE_COUNT + i;
		}

		/* run the expire timeout every EXPIRE_TIMEOUT seconds */
		timeout = timer_nextduein(&expire_timer);
		if (!monotime_valid(timeout))
			fatalx("roa-set expire timer no longer running");

		memset(pfd, 0, sizeof(struct pollfd) * pfd_elms);

		set_pollfd(&pfd[PFD_PIPE_MAIN], ibuf_main);
		set_pollfd(&pfd[PFD_PIPE_RDE], ibuf_rde);

		i = PFD_PIPE_COUNT;
		i += rtr_poll_events(pfd + i, pfd_elms - i, &timeout);

		timeout = monotime_sub(timeout, getmonotime());
		if (!monotime_valid(timeout))
			timeout = monotime_clear();

		if (poll(pfd, i, monotime_to_msec(timeout)) == -1) {
			if (errno == EINTR)
				continue;
			fatal("poll error");
		}

		if (handle_pollfd(&pfd[PFD_PIPE_MAIN], ibuf_main) == -1)
			fatalx("Lost connection to parent");
		else
			rtr_dispatch_imsg_parent(ibuf_main);

		if (handle_pollfd(&pfd[PFD_PIPE_RDE], ibuf_rde) == -1) {
			log_warnx("RTR: Lost connection to RDE");
			imsgbuf_clear(ibuf_rde);
			free(ibuf_rde);
			ibuf_rde = NULL;
		} else
			rtr_dispatch_imsg_rde(ibuf_rde);

		i = PFD_PIPE_COUNT;
		rtr_check_events(pfd + i, pfd_elms - i);

		if (timer_nextisdue(&expire_timer, getmonotime()) != NULL) {
			timer_set(&expire_timer, Timer_Rtr_Expire,
			    EXPIRE_TIMEOUT);
			if (rtr_expire_roas(time(NULL)) != 0)
				rtr_recalc();
			if (rtr_expire_aspa(time(NULL)) != 0)
				rtr_recalc();
		}
	}

	rtr_shutdown();

	free_config(conf);
	free(pfd);

	/* close pipes */
	if (ibuf_rde) {
		imsgbuf_clear(ibuf_rde);
		close(ibuf_rde->fd);
		free(ibuf_rde);
	}
	imsgbuf_clear(ibuf_main);
	close(ibuf_main->fd);
	free(ibuf_main);

	log_info("rtr engine exiting");
	exit(0);
}

static void
rtr_dispatch_imsg_parent(struct imsgbuf *imsgbuf)
{
	static struct aspa_set	*aspa;
	struct imsg		 imsg;
	struct bgpd_config	 tconf;
	struct roa		 roa;
	struct rtr_config_msg	 rtrconf;
	struct rtr_session	*rs;
	uint32_t		 rtrid;
	int			 n, fd;

	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			fatal("%s: imsg_get error", __func__);
		if (n == 0)
			break;

		rtrid = imsg_get_id(&imsg);
		switch (imsg_get_type(&imsg)) {
		case IMSG_SOCKET_CONN_RTR:
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("expected to receive imsg fd "
				    "but didn't receive any");
				break;
			}
			if (ibuf_rde) {
				log_warnx("Unexpected imsg ctl "
				    "connection to RDE received");
				imsgbuf_clear(ibuf_rde);
				free(ibuf_rde);
			}
			if ((ibuf_rde = malloc(sizeof(struct imsgbuf))) == NULL)
				fatal(NULL);
			if (imsgbuf_init(ibuf_rde, fd) == -1 ||
			    imsgbuf_set_maxsize(ibuf_rde, MAX_BGPD_IMSGSIZE) ==
			    -1)
				fatal(NULL);
			break;
		case IMSG_SOCKET_SETUP:
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("expected to receive imsg fd "
				    "but didn't receive any");
				break;
			}
			if ((rs = rtr_get(rtrid)) == NULL) {
				log_warnx("IMSG_SOCKET_SETUP: "
				    "unknown rtr id %d", rtrid);
				close(fd);
				break;
			}
			rtr_open(rs, fd);
			break;
		case IMSG_RECONF_CONF:
			if (imsg_get_data(&imsg, &tconf, sizeof(tconf)) == -1)
				fatal("imsg_get_data");

			nconf = new_config();
			copy_config(nconf, &tconf);
			rtr_config_prep();
			break;
		case IMSG_RECONF_ROA_ITEM:
			if (imsg_get_data(&imsg, &roa, sizeof(roa)) == -1)
				fatal("imsg_get_data");
			rtr_roa_insert(&nconf->roa, &roa);
			break;
		case IMSG_RECONF_ASPA:
			if (aspa != NULL)
				fatalx("unexpected IMSG_RECONF_ASPA");
			if ((aspa = calloc(1, sizeof(*aspa))) == NULL)
				fatal("aspa alloc");
			if (imsg_get_data(&imsg, aspa,
			    offsetof(struct aspa_set, tas)) == -1)
				fatal("imsg_get_data");
			break;
		case IMSG_RECONF_ASPA_TAS:
			if (aspa == NULL)
				fatalx("unexpected IMSG_RECONF_ASPA_TAS");
			aspa->tas = reallocarray(NULL, aspa->num,
			    sizeof(*aspa->tas));
			if (aspa->tas == NULL)
				fatal("aspa tas alloc");
			if (imsg_get_data(&imsg, aspa->tas,
			    aspa->num * sizeof(*aspa->tas)) == -1)
				fatal("imsg_get_data");
			break;
		case IMSG_RECONF_ASPA_DONE:
			if (aspa == NULL)
				fatalx("unexpected IMSG_RECONF_ASPA_DONE");
			if (RB_INSERT(aspa_tree, &nconf->aspa, aspa) != NULL) {
				log_warnx("duplicate ASPA set received");
				free_aspa(aspa);
			}
			aspa = NULL;
			break;
		case IMSG_RECONF_RTR_CONFIG:
			if (imsg_get_data(&imsg, &rtrconf,
			    sizeof(rtrconf)) == -1)
				fatal("imsg_get_data");
			rs = rtr_get(rtrid);
			if (rs == NULL)
				rtr_new(rtrid, &rtrconf);
			else
				rtr_config_keep(rs, &rtrconf);
			break;
		case IMSG_RECONF_DRAIN:
			imsg_compose(ibuf_main, IMSG_RECONF_DRAIN, 0, 0,
			    -1, NULL, 0);
			break;
		case IMSG_RECONF_DONE:
			if (nconf == NULL)
				fatalx("got IMSG_RECONF_DONE but no config");
			copy_config(conf, nconf);
			/* switch the roa, first remove the old one */
			free_roatree(&conf->roa);
			/* then move the RB tree root */
			RB_ROOT(&conf->roa) = RB_ROOT(&nconf->roa);
			RB_ROOT(&nconf->roa) = NULL;
			/* switch the aspa tree, first remove the old one */
			free_aspatree(&conf->aspa);
			/* then move the RB tree root */
			RB_ROOT(&conf->aspa) = RB_ROOT(&nconf->aspa);
			RB_ROOT(&nconf->aspa) = NULL;
			/* finally merge the rtr session */
			rtr_config_merge();
			rtr_expire_roas(time(NULL));
			rtr_expire_aspa(time(NULL));
			rtr_recalc();
			log_info("RTR engine reconfigured");
			imsg_compose(ibuf_main, IMSG_RECONF_DONE, 0, 0,
			    -1, NULL, 0);
			free_config(nconf);
			nconf = NULL;
			break;
		case IMSG_CTL_SHOW_RTR:
			if ((rs = rtr_get(rtrid)) == NULL) {
				log_warnx("IMSG_CTL_SHOW_RTR: "
				    "unknown rtr id %d", rtrid);
				break;
			}
			rtr_show(rs, imsg_get_pid(&imsg));
			break;
		case IMSG_CTL_END:
			imsg_compose(ibuf_main, IMSG_CTL_END, 0,
			    imsg_get_pid(&imsg), -1, NULL, 0);
			break;
		}
		imsg_free(&imsg);
	}
}

static void
rtr_dispatch_imsg_rde(struct imsgbuf *imsgbuf)
{
	struct imsg	imsg;
	int		n;

	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			fatal("%s: imsg_get error", __func__);
		if (n == 0)
			break;

		/* NOTHING */

		imsg_free(&imsg);
	}
}

void
rtr_imsg_compose(int type, uint32_t id, pid_t pid, void *data, size_t datalen)
{
	imsg_compose(ibuf_main, type, id, pid, -1, data, datalen);
}

/*
 * Compress aspa_set tas_aid into the bitfield used by the RDE.
 * Returns the size of tas and tas_aid bitfield required for this aspa_set.
 * At the same time tas_aid is overwritten with the bitmasks or cleared
 * if no extra aid masks are needed.
 */
static size_t
rtr_aspa_set_size(struct aspa_set *aspa)
{
	return aspa->num * sizeof(uint32_t);
}

/*
 * Merge all RPKI ROA trees into one as one big union.
 * Simply try to add all roa entries into a new RB tree.
 * This could be made a fair bit faster but for now this is good enough.
 */
void
rtr_recalc(void)
{
	struct roa_tree rt;
	struct aspa_tree at;
	struct roa *roa, *nr;
	struct aspa_set *aspa;
	struct aspa_prep ap = { 0 };

	if (rtr_recalc_semaphore > 0)
		return;

	RB_INIT(&rt);
	RB_INIT(&at);

	RB_FOREACH(roa, roa_tree, &conf->roa)
		rtr_roa_insert(&rt, roa);
	rtr_roa_merge(&rt);

	imsg_compose(ibuf_rde, IMSG_RECONF_ROA_SET, 0, 0, -1, NULL, 0);
	RB_FOREACH_SAFE(roa, roa_tree, &rt, nr) {
		imsg_compose(ibuf_rde, IMSG_RECONF_ROA_ITEM, 0, 0, -1,
		    roa, sizeof(*roa));
	}
	free_roatree(&rt);

	RB_FOREACH(aspa, aspa_tree, &conf->aspa)
		rtr_aspa_insert(&at, aspa);
	rtr_aspa_merge(&at);

	RB_FOREACH(aspa, aspa_tree, &at) {
		ap.datasize += rtr_aspa_set_size(aspa);
		ap.entries++;
	}

	imsg_compose(ibuf_rde, IMSG_RECONF_ASPA_PREP, 0, 0, -1,
	    &ap, sizeof(ap));

	/* walk tree in reverse because aspa_add_set requires that */
	RB_FOREACH_REVERSE(aspa, aspa_tree, &at) {
		struct aspa_set	as = { .as = aspa->as, .num = aspa->num };

		imsg_compose(ibuf_rde, IMSG_RECONF_ASPA, 0, 0, -1,
		    &as, offsetof(struct aspa_set, tas));
		imsg_compose(ibuf_rde, IMSG_RECONF_ASPA_TAS, 0, 0, -1,
		    aspa->tas, aspa->num * sizeof(*aspa->tas));
		imsg_compose(ibuf_rde, IMSG_RECONF_ASPA_DONE, 0, 0, -1,
		    NULL, 0);
	}

	free_aspatree(&at);

	imsg_compose(ibuf_rde, IMSG_RECONF_DONE, 0, 0, -1, NULL, 0);
}
