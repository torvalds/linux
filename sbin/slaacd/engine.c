/*	$OpenBSD: engine.c,v 1.99 2024/11/21 13:35:20 claudio Exp $	*/

/*
 * Copyright (c) 2017 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2004, 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip6.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>

#include <crypto/sha2.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "slaacd.h"
#include "engine.h"

#define	MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#define	MAX_RTR_SOLICITATION_DELAY	1
#define	MAX_RTR_SOLICITATION_DELAY_USEC	MAX_RTR_SOLICITATION_DELAY * 1000000
#define	RTR_SOLICITATION_INTERVAL	4
#define	MAX_RTR_SOLICITATIONS		3

/*
 * Constants for RFC 8981 temporary address extensions
 *
 * PRIV_PREFERRED_LIFETIME > (PRIV_MAX_DESYNC_FACTOR + PRIV_REGEN_ADVANCE)
 */
#define PRIV_VALID_LIFETIME	172800	/* 2 days */
#define PRIV_PREFERRED_LIFETIME	86400	/* 1 day */
#define PRIV_MAX_DESYNC_FACTOR	34560	/* PRIV_PREFERRED_LIFETIME * 0.4 */
#define	PRIV_REGEN_ADVANCE	5	/* 5 seconds */

enum if_state {
	IF_DOWN,
	IF_INIT,
	IF_BOUND,
};

enum proposal_state {
	PROPOSAL_IF_DOWN,
	PROPOSAL_NOT_CONFIGURED,
	PROPOSAL_CONFIGURED,
	PROPOSAL_NEARLY_EXPIRED,
	PROPOSAL_WITHDRAWN,
	PROPOSAL_DUPLICATED,
	PROPOSAL_STALE,
};

const char* rpref_name[] = {
	"Low",
	"Medium",
	"High",
};

struct radv_prefix {
	LIST_ENTRY(radv_prefix)	entries;
	struct in6_addr		prefix;
	uint8_t			prefix_len; /*XXX int */
	int			onlink;
	int			autonomous;
	uint32_t		vltime;
	uint32_t		pltime;
	int			dad_counter;
};

struct radv_rdns {
	LIST_ENTRY(radv_rdns)	entries;
	struct in6_addr		rdns;
};

struct radv {
	LIST_ENTRY(radv)		 entries;
	struct sockaddr_in6		 from;
	struct timespec			 when;
	struct timespec			 uptime;
	struct event			 timer;
	uint32_t			 min_lifetime;
	uint8_t				 curhoplimit;
	int				 managed;
	int				 other;
	enum rpref			 rpref;
	uint16_t			 router_lifetime; /* in seconds */
	uint32_t			 reachable_time; /* in milliseconds */
	uint32_t			 retrans_time; /* in milliseconds */
	LIST_HEAD(, radv_prefix)	 prefixes;
	uint32_t			 rdns_lifetime;
	LIST_HEAD(, radv_rdns)		 rdns_servers;
	uint32_t			 mtu;
};

struct address_proposal {
	LIST_ENTRY(address_proposal)	 entries;
	struct event			 timer;
	int64_t				 id;
	enum proposal_state		 state;
	struct timeval			 timo;
	struct timespec			 created;
	struct timespec			 when;
	struct timespec			 uptime;
	uint32_t			 if_index;
	struct ether_addr		 hw_address;
	struct sockaddr_in6		 from;
	struct sockaddr_in6		 addr;
	struct in6_addr			 mask;
	struct in6_addr			 prefix;
	int				 temporary;
	uint8_t				 prefix_len;
	uint32_t			 vltime;
	uint32_t			 pltime;
	uint32_t			 desync_factor;
	uint8_t				 soiikey[SLAACD_SOIIKEY_LEN];
	uint32_t			 mtu;
};

struct dfr_proposal {
	LIST_ENTRY(dfr_proposal)	 entries;
	struct event			 timer;
	int64_t				 id;
	enum proposal_state		 state;
	struct timeval			 timo;
	struct timespec			 when;
	struct timespec			 uptime;
	uint32_t			 if_index;
	int				 rdomain;
	struct sockaddr_in6		 addr;
	uint32_t			 router_lifetime;
	enum rpref			 rpref;
};

struct rdns_proposal {
	LIST_ENTRY(rdns_proposal)	 entries;
	struct event			 timer;
	int64_t				 id;
	enum proposal_state		 state;
	struct timeval			 timo;
	struct timespec			 when;
	struct timespec			 uptime;
	uint32_t			 if_index;
	int				 rdomain;
	struct sockaddr_in6		 from;
	int				 rdns_count;
	struct in6_addr			 rdns[MAX_RDNS_COUNT];
	uint32_t			 rdns_lifetime;
};

struct slaacd_iface {
	LIST_ENTRY(slaacd_iface)	 entries;
	enum if_state			 state;
	struct event			 timer;
	struct timeval			 timo;
	struct timespec			 last_sol;
	int				 probes;
	uint32_t			 if_index;
	uint32_t			 rdomain;
	int				 running;
	int				 autoconf;
	int				 temporary;
	int				 soii;
	struct ether_addr		 hw_address;
	struct sockaddr_in6		 ll_address;
	uint8_t				 soiikey[SLAACD_SOIIKEY_LEN];
	int				 link_state;
	uint32_t			 cur_mtu;
	LIST_HEAD(, radv)		 radvs;
	LIST_HEAD(, address_proposal)	 addr_proposals;
	LIST_HEAD(, dfr_proposal)	 dfr_proposals;
	LIST_HEAD(, rdns_proposal)	 rdns_proposals;
};

LIST_HEAD(, slaacd_iface) slaacd_interfaces;

__dead void		 engine_shutdown(void);
void			 engine_sig_handler(int sig, short, void *);
void			 engine_dispatch_frontend(int, short, void *);
void			 engine_dispatch_main(int, short, void *);
#ifndef	SMALL
void			 send_interface_info(struct slaacd_iface *, pid_t);
void			 engine_showinfo_ctl(pid_t, uint32_t);
void			 debug_log_ra(struct imsg_ra *);
int			 in6_mask2prefixlen(struct in6_addr *);
#endif	/* SMALL */
struct slaacd_iface	*get_slaacd_iface_by_id(uint32_t);
void			 remove_slaacd_iface(uint32_t);
void			 free_ra(struct radv *);
void			 iface_state_transition(struct slaacd_iface *, enum
			     if_state);
void			 addr_proposal_state_transition(struct
			     address_proposal *, enum proposal_state);
void			 dfr_proposal_state_transition(struct dfr_proposal *,
			     enum proposal_state);
void			 rdns_proposal_state_transition(struct rdns_proposal *,
			     enum proposal_state);
void			 engine_update_iface(struct imsg_ifinfo *);
void			 request_solicitation(struct slaacd_iface *);
void			 parse_ra(struct slaacd_iface *, struct imsg_ra *);
void			 gen_addr(struct slaacd_iface *, struct radv_prefix *,
			     struct address_proposal *, int);
void			 gen_address_proposal(struct slaacd_iface *, struct
			     radv *, struct radv_prefix *, int);
void			 free_address_proposal(struct address_proposal *);
void			 withdraw_addr(struct address_proposal *);
void			 configure_address(struct address_proposal *);
void			 in6_prefixlen2mask(struct in6_addr *, int len);
void			 gen_dfr_proposal(struct slaacd_iface *, struct
			     radv *);
void			 configure_dfr(struct dfr_proposal *);
void			 free_dfr_proposal(struct dfr_proposal *);
void			 withdraw_dfr(struct dfr_proposal *);
void			 update_iface_ra_rdns(struct slaacd_iface *,
			     struct radv *);
void			 gen_rdns_proposal(struct slaacd_iface *, struct
			     radv *);
void			 free_rdns_proposal(struct rdns_proposal *);
void			 withdraw_rdns(struct rdns_proposal *);
void			 compose_rdns_proposal(uint32_t, int);
void			 update_iface_ra(struct slaacd_iface *, struct radv *);
void			 update_iface_ra_dfr(struct slaacd_iface *,
    			     struct radv *);
void			 update_iface_ra_prefix(struct slaacd_iface *,
			     struct radv *, struct radv_prefix *prefix);
void			 address_proposal_timeout(int, short, void *);
void			 dfr_proposal_timeout(int, short, void *);
void			 rdns_proposal_timeout(int, short, void *);
void			 iface_timeout(int, short, void *);
struct radv		*find_ra(struct slaacd_iface *, struct sockaddr_in6 *);
struct address_proposal	*find_address_proposal_by_addr(struct slaacd_iface *,
			     struct sockaddr_in6 *);
struct dfr_proposal	*find_dfr_proposal_by_gw(struct slaacd_iface *,
			     struct sockaddr_in6 *);
struct rdns_proposal	*find_rdns_proposal_by_gw(struct slaacd_iface *,
			     struct sockaddr_in6 *);
struct radv_prefix	*find_prefix(struct radv *, struct in6_addr *, uint8_t);
int			 engine_imsg_compose_main(int, pid_t, void *, uint16_t);
uint32_t		 real_lifetime(struct timespec *, uint32_t);
void			 merge_dad_couters(struct radv *, struct radv *);

static struct imsgev	*iev_frontend;
static struct imsgev	*iev_main;
int64_t			 proposal_id;


#define	CASE(x) case x : return #x

#ifndef SMALL
static const char*
if_state_name(enum if_state ifs)
{
	switch (ifs) {
	CASE(IF_DOWN);
	CASE(IF_INIT);
	CASE(IF_BOUND);
	}
}

static const char*
proposal_state_name(enum proposal_state ps)
{
	switch (ps) {
	CASE(PROPOSAL_IF_DOWN);
	CASE(PROPOSAL_NOT_CONFIGURED);
	CASE(PROPOSAL_CONFIGURED);
	CASE(PROPOSAL_NEARLY_EXPIRED);
	CASE(PROPOSAL_WITHDRAWN);
	CASE(PROPOSAL_DUPLICATED);
	CASE(PROPOSAL_STALE);
	}
}
#endif

void
engine_sig_handler(int sig, short event, void *arg)
{
	/*
	 * Normal signal handler rules don't apply because libevent
	 * decouples for us.
	 */

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		engine_shutdown();
	default:
		fatalx("unexpected signal");
	}
}

void
engine(int debug, int verbose)
{
	struct event		 ev_sigint, ev_sigterm;
	struct passwd		*pw;

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if ((pw = getpwnam(SLAACD_USER)) == NULL)
		fatal("getpwnam");

	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	if (unveil("/", "") == -1)
		fatal("unveil /");
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	setproctitle("%s", "engine");
	log_procinit("engine");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	event_init();

	/* Setup signal handler(s). */
	signal_set(&ev_sigint, SIGINT, engine_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, engine_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Setup pipe and event handler to the main process. */
	if ((iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);

	if (imsgbuf_init(&iev_main->ibuf, 3) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_main->ibuf);
	iev_main->handler = engine_dispatch_main;

	/* Setup event handlers. */
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	LIST_INIT(&slaacd_interfaces);

	event_dispatch();

	engine_shutdown();
}

__dead void
engine_shutdown(void)
{
	/* Close pipes. */
	imsgbuf_clear(&iev_frontend->ibuf);
	close(iev_frontend->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	free(iev_frontend);
	free(iev_main);

	log_info("engine exiting");
	exit(0);
}

int
engine_imsg_compose_frontend(int type, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_frontend, type, 0, pid, -1,
	    data, datalen));
}

int
engine_imsg_compose_main(int type, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1,
	    data, datalen));
}

void
engine_dispatch_frontend(int fd, short event, void *bula)
{
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct imsg			 imsg;
	struct slaacd_iface		*iface;
	struct imsg_ra			 ra;
	struct address_proposal		*addr_proposal = NULL;
	struct dfr_proposal		*dfr_proposal = NULL;
	struct imsg_del_addr		 del_addr;
	struct imsg_del_route		 del_route;
	struct imsg_dup_addr		 dup_addr;
	ssize_t				 n;
	int				 shut = 0;
#ifndef	SMALL
	int				 verbose;
#endif	/* SMALL */
	uint32_t			 if_index, type;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* Connection closed. */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get error", __func__);
		if (n == 0)	/* No more messages. */
			break;

		type = imsg_get_type(&imsg);

		switch (type) {
#ifndef	SMALL
		case IMSG_CTL_LOG_VERBOSE:
			if (imsg_get_data(&imsg, &verbose,
			    sizeof(verbose)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			log_setverbose(verbose);
			break;
		case IMSG_CTL_SHOW_INTERFACE_INFO:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			engine_showinfo_ctl(imsg_get_pid(&imsg), if_index);
			break;
#endif	/* SMALL */
		case IMSG_REMOVE_IF:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			remove_slaacd_iface(if_index);
			break;
		case IMSG_RA:
			if (imsg_get_data(&imsg, &ra, sizeof(ra)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_slaacd_iface_by_id(ra.if_index);

			/*
			 * Ignore unsolicitated router advertisements
			 * if we think the interface is still down.
			 * Otherwise we confuse the state machine.
			 */
			if (iface != NULL && iface->state != IF_DOWN)
				parse_ra(iface, &ra);
			break;
		case IMSG_CTL_SEND_SOLICITATION:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_slaacd_iface_by_id(if_index);
			if (iface == NULL)
				log_warnx("requested to send solicitation on "
				    "non-autoconf interface: %u", if_index);
			else {
				iface->last_sol.tv_sec = 0; /* no rate limit */
				request_solicitation(iface);
			}
			break;
		case IMSG_DEL_ADDRESS:
			if (imsg_get_data(&imsg, &del_addr,
			    sizeof(del_addr)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_slaacd_iface_by_id(del_addr.if_index);
			if (iface == NULL) {
				log_debug("IMSG_DEL_ADDRESS: unknown interface"
				    ", ignoring");
				break;
			}

			addr_proposal = find_address_proposal_by_addr(iface,
			    &del_addr.addr);
			/*
			 * If it's in state PROPOSAL_WITHDRAWN we just
			 * deleted it ourself but want to keep it around
			 * so we can renew it
			 */
			if (addr_proposal && addr_proposal->state !=
			    PROPOSAL_WITHDRAWN)
				free_address_proposal(addr_proposal);
			break;
		case IMSG_DEL_ROUTE:
			if (imsg_get_data(&imsg, &del_route,
			    sizeof(del_route)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_slaacd_iface_by_id(del_route.if_index);
			if (iface == NULL) {
				log_debug("IMSG_DEL_ROUTE: unknown interface"
				    ", ignoring");
				break;
			}

			dfr_proposal = find_dfr_proposal_by_gw(iface,
			    &del_route.gw);

			if (dfr_proposal) {
				dfr_proposal->state = PROPOSAL_WITHDRAWN;
				free_dfr_proposal(dfr_proposal);
			}
			break;
		case IMSG_DUP_ADDRESS:
			if (imsg_get_data(&imsg, &dup_addr,
			    sizeof(dup_addr)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_slaacd_iface_by_id(dup_addr.if_index);
			if (iface == NULL) {
				log_debug("IMSG_DUP_ADDRESS: unknown interface"
				    ", ignoring");
				break;
			}

			addr_proposal = find_address_proposal_by_addr(iface,
			    &dup_addr.addr);

			if (addr_proposal)
				addr_proposal_state_transition(addr_proposal,
				    PROPOSAL_DUPLICATED);
			break;
		case IMSG_REPROPOSE_RDNS:
			LIST_FOREACH (iface, &slaacd_interfaces, entries)
				compose_rdns_proposal(iface->if_index,
				    iface->rdomain);
			break;
		default:
			log_debug("%s: unexpected imsg %d", __func__, type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* This pipe is dead. Remove its event handler. */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
engine_dispatch_main(int fd, short event, void *bula)
{
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg_ifinfo	 imsg_ifinfo;
	ssize_t			 n;
	uint32_t		 type;
	int			 shut = 0;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* Connection closed. */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get error", __func__);
		if (n == 0)	/* No more messages. */
			break;

		type = imsg_get_type(&imsg);

		switch (type) {
		case IMSG_SOCKET_IPC:
			/*
			 * Setup pipe and event handler to the frontend
			 * process.
			 */
			if (iev_frontend)
				fatalx("%s: received unexpected imsg fd "
				    "to engine", __func__);

			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg fd to "
				   "engine but didn't receive any", __func__);

			iev_frontend = malloc(sizeof(struct imsgev));
			if (iev_frontend == NULL)
				fatal(NULL);

			if (imsgbuf_init(&iev_frontend->ibuf, fd) == -1)
				fatal(NULL);
			iev_frontend->handler = engine_dispatch_frontend;
			iev_frontend->events = EV_READ;

			event_set(&iev_frontend->ev, iev_frontend->ibuf.fd,
			iev_frontend->events, iev_frontend->handler,
			    iev_frontend);
			event_add(&iev_frontend->ev, NULL);

			if (pledge("stdio", NULL) == -1)
				fatal("pledge");
			break;
		case IMSG_UPDATE_IF:
			if (imsg_get_data(&imsg, &imsg_ifinfo,
			    sizeof(imsg_ifinfo)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			engine_update_iface(&imsg_ifinfo);
			break;
		default:
			log_debug("%s: unexpected imsg %d", __func__, type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* This pipe is dead. Remove its event handler. */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

#ifndef	SMALL
void
send_interface_info(struct slaacd_iface *iface, pid_t pid)
{
	struct ctl_engine_info			 cei;
	struct ctl_engine_info_ra		 cei_ra;
	struct ctl_engine_info_ra_prefix	 cei_ra_prefix;
	struct ctl_engine_info_ra_rdns		 cei_ra_rdns;
	struct ctl_engine_info_address_proposal	 cei_addr_proposal;
	struct ctl_engine_info_dfr_proposal	 cei_dfr_proposal;
	struct ctl_engine_info_rdns_proposal	 cei_rdns_proposal;
	struct radv				*ra;
	struct radv_prefix			*prefix;
	struct radv_rdns			*rdns;
	struct address_proposal			*addr_proposal;
	struct dfr_proposal			*dfr_proposal;
	struct rdns_proposal			*rdns_proposal;

	memset(&cei, 0, sizeof(cei));
	cei.if_index = iface->if_index;
	cei.running = iface->running;
	cei.autoconf = iface->autoconf;
	cei.temporary = iface->temporary;
	cei.soii = iface->soii;
	memcpy(&cei.hw_address, &iface->hw_address, sizeof(struct ether_addr));
	memcpy(&cei.ll_address, &iface->ll_address,
	    sizeof(struct sockaddr_in6));
	engine_imsg_compose_frontend(IMSG_CTL_SHOW_INTERFACE_INFO, pid, &cei,
	    sizeof(cei));
	LIST_FOREACH(ra, &iface->radvs, entries) {
		memset(&cei_ra, 0, sizeof(cei_ra));
		memcpy(&cei_ra.from, &ra->from, sizeof(cei_ra.from));
		memcpy(&cei_ra.when, &ra->when, sizeof(cei_ra.when));
		memcpy(&cei_ra.uptime, &ra->uptime, sizeof(cei_ra.uptime));
		cei_ra.curhoplimit = ra->curhoplimit;
		cei_ra.managed = ra->managed;
		cei_ra.other = ra->other;
		if (strlcpy(cei_ra.rpref, rpref_name[ra->rpref], sizeof(
		    cei_ra.rpref)) >= sizeof(cei_ra.rpref))
			log_warnx("truncated router preference");
		cei_ra.router_lifetime = ra->router_lifetime;
		cei_ra.reachable_time = ra->reachable_time;
		cei_ra.retrans_time = ra->retrans_time;
		cei_ra.mtu = ra->mtu;
		engine_imsg_compose_frontend(IMSG_CTL_SHOW_INTERFACE_INFO_RA,
		    pid, &cei_ra, sizeof(cei_ra));

		LIST_FOREACH(prefix, &ra->prefixes, entries) {
			memset(&cei_ra_prefix, 0, sizeof(cei_ra_prefix));

			cei_ra_prefix.prefix = prefix->prefix;
			cei_ra_prefix.prefix_len = prefix->prefix_len;
			cei_ra_prefix.onlink = prefix->onlink;
			cei_ra_prefix.autonomous = prefix->autonomous;
			cei_ra_prefix.vltime = prefix->vltime;
			cei_ra_prefix.pltime = prefix->pltime;
			engine_imsg_compose_frontend(
			    IMSG_CTL_SHOW_INTERFACE_INFO_RA_PREFIX, pid,
			    &cei_ra_prefix, sizeof(cei_ra_prefix));
		}

		LIST_FOREACH(rdns, &ra->rdns_servers, entries) {
			memset(&cei_ra_rdns, 0, sizeof(cei_ra_rdns));
			memcpy(&cei_ra_rdns.rdns, &rdns->rdns,
			    sizeof(cei_ra_rdns.rdns));
			cei_ra_rdns.lifetime = ra->rdns_lifetime;
			engine_imsg_compose_frontend(
			    IMSG_CTL_SHOW_INTERFACE_INFO_RA_RDNS, pid,
			    &cei_ra_rdns, sizeof(cei_ra_rdns));
		}
	}

	if (!LIST_EMPTY(&iface->addr_proposals))
		engine_imsg_compose_frontend(
		    IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSALS, pid, NULL, 0);

	LIST_FOREACH(addr_proposal, &iface->addr_proposals, entries) {
		memset(&cei_addr_proposal, 0, sizeof(cei_addr_proposal));
		cei_addr_proposal.id = addr_proposal->id;
		if (strlcpy(cei_addr_proposal.state,
		    proposal_state_name(addr_proposal->state),
		    sizeof(cei_addr_proposal.state)) >=
		    sizeof(cei_addr_proposal.state))
			log_warnx("truncated state name");
		cei_addr_proposal.next_timeout = addr_proposal->timo.tv_sec;
		cei_addr_proposal.when = addr_proposal->when;
		cei_addr_proposal.uptime = addr_proposal->uptime;
		memcpy(&cei_addr_proposal.addr, &addr_proposal->addr, sizeof(
		    cei_addr_proposal.addr));
		memcpy(&cei_addr_proposal.prefix, &addr_proposal->prefix,
		    sizeof(cei_addr_proposal.prefix));
		cei_addr_proposal.prefix_len = addr_proposal->prefix_len;
		cei_addr_proposal.temporary = addr_proposal->temporary;
		cei_addr_proposal.vltime = addr_proposal->vltime;
		cei_addr_proposal.pltime = addr_proposal->pltime;

		engine_imsg_compose_frontend(
		    IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSAL, pid,
			    &cei_addr_proposal, sizeof(cei_addr_proposal));
	}

	if (!LIST_EMPTY(&iface->dfr_proposals))
		engine_imsg_compose_frontend(
		    IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSALS, pid, NULL, 0);

	LIST_FOREACH(dfr_proposal, &iface->dfr_proposals, entries) {
		memset(&cei_dfr_proposal, 0, sizeof(cei_dfr_proposal));
		cei_dfr_proposal.id = dfr_proposal->id;
		if (strlcpy(cei_dfr_proposal.state,
		    proposal_state_name(dfr_proposal->state),
		    sizeof(cei_dfr_proposal.state)) >=
		    sizeof(cei_dfr_proposal.state))
			log_warnx("truncated state name");
		cei_dfr_proposal.next_timeout = dfr_proposal->timo.tv_sec;
		cei_dfr_proposal.when = dfr_proposal->when;
		cei_dfr_proposal.uptime = dfr_proposal->uptime;
		memcpy(&cei_dfr_proposal.addr, &dfr_proposal->addr, sizeof(
		    cei_dfr_proposal.addr));
		cei_dfr_proposal.router_lifetime =
		    dfr_proposal->router_lifetime;
		if (strlcpy(cei_dfr_proposal.rpref,
		    rpref_name[dfr_proposal->rpref],
		    sizeof(cei_dfr_proposal.rpref)) >=
		    sizeof(cei_dfr_proposal.rpref))
			log_warnx("truncated router preference");
		engine_imsg_compose_frontend(
		    IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSAL, pid,
			    &cei_dfr_proposal, sizeof(cei_dfr_proposal));
	}

	if (!LIST_EMPTY(&iface->rdns_proposals))
		engine_imsg_compose_frontend(
		    IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSALS, pid, NULL, 0);

	LIST_FOREACH(rdns_proposal, &iface->rdns_proposals, entries) {
		memset(&cei_rdns_proposal, 0, sizeof(cei_rdns_proposal));
		cei_rdns_proposal.id = rdns_proposal->id;
		if (strlcpy(cei_rdns_proposal.state,
		    proposal_state_name(rdns_proposal->state),
		    sizeof(cei_rdns_proposal.state)) >=
		    sizeof(cei_rdns_proposal.state))
			log_warnx("truncated state name");
		cei_rdns_proposal.next_timeout = rdns_proposal->timo.tv_sec;
		cei_rdns_proposal.when = rdns_proposal->when;
		cei_rdns_proposal.uptime = rdns_proposal->uptime;
		memcpy(&cei_rdns_proposal.from, &rdns_proposal->from, sizeof(
		    cei_rdns_proposal.from));
		cei_rdns_proposal.rdns_count = rdns_proposal->rdns_count;
		memcpy(&cei_rdns_proposal.rdns,
		    &rdns_proposal->rdns, sizeof(cei_rdns_proposal.rdns));
		cei_rdns_proposal.rdns_lifetime =
		    rdns_proposal->rdns_lifetime;
		engine_imsg_compose_frontend(
		    IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSAL, pid,
			    &cei_rdns_proposal, sizeof(cei_rdns_proposal));
	}
}

void
engine_showinfo_ctl(pid_t pid, uint32_t if_index)
{
	struct slaacd_iface			*iface;

	if (if_index == 0) {
		LIST_FOREACH (iface, &slaacd_interfaces, entries)
			send_interface_info(iface, pid);
	} else {
		if ((iface = get_slaacd_iface_by_id(if_index)) != NULL)
			send_interface_info(iface, pid);
	}
	engine_imsg_compose_frontend(IMSG_CTL_END, pid, NULL, 0);
}

#endif	/* SMALL */

struct slaacd_iface*
get_slaacd_iface_by_id(uint32_t if_index)
{
	struct slaacd_iface	*iface;
	LIST_FOREACH (iface, &slaacd_interfaces, entries) {
		if (iface->if_index == if_index)
			return (iface);
	}

	return (NULL);
}

void
remove_slaacd_iface(uint32_t if_index)
{
	struct slaacd_iface	*iface;
	struct radv		*ra;
	struct address_proposal	*addr_proposal;
	struct dfr_proposal	*dfr_proposal;
	struct rdns_proposal	*rdns_proposal;

	iface = get_slaacd_iface_by_id(if_index);

	if (iface == NULL)
		return;

	LIST_REMOVE(iface, entries);
	while(!LIST_EMPTY(&iface->radvs)) {
		ra = LIST_FIRST(&iface->radvs);
		LIST_REMOVE(ra, entries);
		free_ra(ra);
	}
	while(!LIST_EMPTY(&iface->addr_proposals)) {
		addr_proposal = LIST_FIRST(&iface->addr_proposals);
		free_address_proposal(addr_proposal);
	}
	while(!LIST_EMPTY(&iface->dfr_proposals)) {
		dfr_proposal = LIST_FIRST(&iface->dfr_proposals);
		free_dfr_proposal(dfr_proposal);
	}
	while(!LIST_EMPTY(&iface->rdns_proposals)) {
		rdns_proposal = LIST_FIRST(&iface->rdns_proposals);
		free_rdns_proposal(rdns_proposal);
	}
	compose_rdns_proposal(iface->if_index, iface->rdomain);
	evtimer_del(&iface->timer);
	free(iface);
}

void
free_ra(struct radv *ra)
{
	struct radv_prefix	*prefix;
	struct radv_rdns	*rdns;

	if (ra == NULL)
		return;

	evtimer_del(&ra->timer);

	while (!LIST_EMPTY(&ra->prefixes)) {
		prefix = LIST_FIRST(&ra->prefixes);
		LIST_REMOVE(prefix, entries);
		free(prefix);
	}

	while (!LIST_EMPTY(&ra->rdns_servers)) {
		rdns = LIST_FIRST(&ra->rdns_servers);
		LIST_REMOVE(rdns, entries);
		free(rdns);
	}

	free(ra);
}

void
iface_state_transition(struct slaacd_iface *iface, enum if_state new_state)
{
	enum if_state		 old_state = iface->state;
	struct address_proposal	*addr_proposal;
	struct dfr_proposal	*dfr_proposal;
	struct rdns_proposal	*rdns_proposal;

	iface->state = new_state;

	switch (new_state) {
	case IF_DOWN:
		if (old_state != IF_DOWN) {
			LIST_FOREACH (addr_proposal, &iface->addr_proposals,
			    entries)
				addr_proposal_state_transition(addr_proposal,
				    PROPOSAL_IF_DOWN);
			LIST_FOREACH (dfr_proposal, &iface->dfr_proposals,
			    entries)
				dfr_proposal_state_transition(dfr_proposal,
					PROPOSAL_IF_DOWN);
			LIST_FOREACH (rdns_proposal, &iface->rdns_proposals,
			    entries)
				rdns_proposal_state_transition(rdns_proposal,
				    PROPOSAL_IF_DOWN);
		}

		/* nothing else to do until interface comes back up */
		iface->timo.tv_sec = -1;
		break;
	case IF_INIT:
		switch (old_state) {
		case IF_INIT:
			iface->probes++;
			break;
		case IF_DOWN:
			LIST_FOREACH (addr_proposal, &iface->addr_proposals,
			    entries)
				addr_proposal_state_transition(addr_proposal,
				    PROPOSAL_WITHDRAWN);
			LIST_FOREACH (dfr_proposal, &iface->dfr_proposals,
			    entries)
				dfr_proposal_state_transition(dfr_proposal,
				    PROPOSAL_WITHDRAWN);
			LIST_FOREACH (rdns_proposal, &iface->rdns_proposals,
			    entries)
				rdns_proposal_state_transition(rdns_proposal,
				    PROPOSAL_WITHDRAWN);
		default:
			iface->probes = 0;
		}
		if (iface->probes < MAX_RTR_SOLICITATIONS) {
			iface->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
			request_solicitation(iface);
		} else
			/* no router available, stop probing */
			iface->timo.tv_sec = -1;
		break;
	case IF_BOUND:
		iface->timo.tv_sec = -1;
		break;
	}

	if (log_getverbose()) {
		char	 ifnamebuf[IF_NAMESIZE], *if_name;
		if_name = if_indextoname(iface->if_index, ifnamebuf);
		log_debug("%s[%s] %s -> %s, timo: %lld", __func__,
		    if_name == NULL ? "?" : if_name, if_state_name(old_state),
		    if_state_name(new_state), iface->timo.tv_sec);
	}

	if (iface->timo.tv_sec == -1) {
		if (evtimer_pending(&iface->timer, NULL))
			evtimer_del(&iface->timer);
	} else
		evtimer_add(&iface->timer, &iface->timo);
}

void addr_proposal_state_transition(struct address_proposal *addr_proposal,
    enum proposal_state new_state)
{
	enum proposal_state	 old_state = addr_proposal->state;
	struct slaacd_iface	*iface;
	uint32_t		 lifetime;

	addr_proposal->state = new_state;

	if ((iface = get_slaacd_iface_by_id(addr_proposal->if_index)) == NULL)
		return;

	switch (addr_proposal->state) {
	case PROPOSAL_IF_DOWN:
		if (old_state == PROPOSAL_IF_DOWN) {
			withdraw_addr(addr_proposal);
			addr_proposal->timo.tv_sec = -1;
		} else {
			addr_proposal->timo.tv_sec =
			    real_lifetime(&addr_proposal->uptime,
				addr_proposal->vltime);
		}
		break;
	case PROPOSAL_NOT_CONFIGURED:
		break;
	case PROPOSAL_CONFIGURED:
		lifetime = real_lifetime(&addr_proposal->uptime,
		    addr_proposal->pltime);
		if (lifetime == 0)
			lifetime = real_lifetime(&addr_proposal->uptime,
			    addr_proposal->vltime);
		if (lifetime > MAX_RTR_SOLICITATIONS *
		    (RTR_SOLICITATION_INTERVAL + 1))
			addr_proposal->timo.tv_sec = lifetime -
			    MAX_RTR_SOLICITATIONS * RTR_SOLICITATION_INTERVAL;
		else
			addr_proposal->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
		break;
	case PROPOSAL_NEARLY_EXPIRED:
		lifetime = real_lifetime(&addr_proposal->uptime,
		    addr_proposal->pltime);
		if (lifetime == 0)
			lifetime = real_lifetime(&addr_proposal->uptime,
			    addr_proposal->vltime);
		if (lifetime > MAX_RTR_SOLICITATIONS *
		    (RTR_SOLICITATION_INTERVAL + 1))
			addr_proposal->timo.tv_sec = lifetime -
			    MAX_RTR_SOLICITATIONS * RTR_SOLICITATION_INTERVAL;
		else
			addr_proposal->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
		request_solicitation(iface);
		break;
	case PROPOSAL_WITHDRAWN:
		withdraw_addr(addr_proposal);
		addr_proposal->timo.tv_sec = MAX_RTR_SOLICITATIONS *
		    RTR_SOLICITATION_INTERVAL;
		break;
	case PROPOSAL_DUPLICATED:
		addr_proposal->timo.tv_sec = 0;
		break;
	case PROPOSAL_STALE:
		addr_proposal->timo.tv_sec = 0; /* remove immediately */
		break;
	}

	if (log_getverbose()) {
		char	 ifnamebuf[IF_NAMESIZE], *if_name;
		if_name = if_indextoname(addr_proposal->if_index, ifnamebuf);
		log_debug("%s[%s] %s -> %s, timo: %lld", __func__,
		    if_name == NULL ? "?" : if_name,
		    proposal_state_name(old_state),
		    proposal_state_name(new_state), addr_proposal->timo.tv_sec);
	}

	if (addr_proposal->timo.tv_sec == -1) {
		if (evtimer_pending(&addr_proposal->timer, NULL))
			evtimer_del(&addr_proposal->timer);
	} else
		evtimer_add(&addr_proposal->timer, &addr_proposal->timo);
}

void dfr_proposal_state_transition(struct dfr_proposal *dfr_proposal,
    enum proposal_state new_state)
{
	enum proposal_state	 old_state = dfr_proposal->state;
	struct slaacd_iface	*iface;
	uint32_t		 lifetime;

	dfr_proposal->state = new_state;

	if ((iface = get_slaacd_iface_by_id(dfr_proposal->if_index)) == NULL)
		return;

	switch (dfr_proposal->state) {
	case PROPOSAL_IF_DOWN:
		if (old_state == PROPOSAL_IF_DOWN) {
			withdraw_dfr(dfr_proposal);
			dfr_proposal->timo.tv_sec = -1;
		} else {
			dfr_proposal->timo.tv_sec =
			    real_lifetime(&dfr_proposal->uptime,
				dfr_proposal->router_lifetime);
		}
		break;
	case PROPOSAL_NOT_CONFIGURED:
		break;
	case PROPOSAL_CONFIGURED:
		lifetime = real_lifetime(&dfr_proposal->uptime,
		    dfr_proposal->router_lifetime);
		if (lifetime > MAX_RTR_SOLICITATIONS *
		    (RTR_SOLICITATION_INTERVAL + 1))
			dfr_proposal->timo.tv_sec = lifetime -
			    MAX_RTR_SOLICITATIONS * RTR_SOLICITATION_INTERVAL;
		else
			dfr_proposal->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
		break;
	case PROPOSAL_NEARLY_EXPIRED:
		lifetime = real_lifetime(&dfr_proposal->uptime,
		    dfr_proposal->router_lifetime);
		if (lifetime > MAX_RTR_SOLICITATIONS *
		    (RTR_SOLICITATION_INTERVAL + 1))
			dfr_proposal->timo.tv_sec = lifetime -
			    MAX_RTR_SOLICITATIONS * RTR_SOLICITATION_INTERVAL;
		else
			dfr_proposal->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
		request_solicitation(iface);
		break;
	case PROPOSAL_WITHDRAWN:
		withdraw_dfr(dfr_proposal);
		dfr_proposal->timo.tv_sec = MAX_RTR_SOLICITATIONS *
		    RTR_SOLICITATION_INTERVAL;
		break;
	case PROPOSAL_STALE:
		dfr_proposal->timo.tv_sec = 0; /* remove immediately */
		break;
	case PROPOSAL_DUPLICATED:
		fatalx("invalid dfr state: PROPOSAL_DUPLICATED");
		break;
	}

	if (log_getverbose()) {
		char	 ifnamebuf[IF_NAMESIZE], *if_name;

		if_name = if_indextoname(dfr_proposal->if_index, ifnamebuf);
		log_debug("%s[%s] %s -> %s, timo: %lld", __func__,
		    if_name == NULL ? "?" : if_name,
		    proposal_state_name(old_state),
		    proposal_state_name(new_state), dfr_proposal->timo.tv_sec);
	}

	if (dfr_proposal->timo.tv_sec == -1) {
		if (evtimer_pending(&dfr_proposal->timer, NULL))
			evtimer_del(&dfr_proposal->timer);
	} else
		evtimer_add(&dfr_proposal->timer, &dfr_proposal->timo);

}

void rdns_proposal_state_transition(struct rdns_proposal *rdns_proposal,
    enum proposal_state new_state)
{
	enum proposal_state	 old_state = rdns_proposal->state;
	struct slaacd_iface	*iface;
	uint32_t		 lifetime;

	rdns_proposal->state = new_state;

	if ((iface = get_slaacd_iface_by_id(rdns_proposal->if_index)) == NULL)
		return;

	switch (rdns_proposal->state) {
	case PROPOSAL_IF_DOWN:
		if (old_state == PROPOSAL_IF_DOWN) {
			withdraw_rdns(rdns_proposal);
			rdns_proposal->timo.tv_sec = -1;
		} else {
			rdns_proposal->timo.tv_sec =
			    real_lifetime(&rdns_proposal->uptime,
				rdns_proposal->rdns_lifetime);
		}
		break;
	case PROPOSAL_NOT_CONFIGURED:
		break;
	case PROPOSAL_CONFIGURED:
		lifetime = real_lifetime(&rdns_proposal->uptime,
		    rdns_proposal->rdns_lifetime);
		if (lifetime > MAX_RTR_SOLICITATIONS *
		    (RTR_SOLICITATION_INTERVAL + 1))
			rdns_proposal->timo.tv_sec = lifetime -
			    MAX_RTR_SOLICITATIONS * RTR_SOLICITATION_INTERVAL;
		else
			rdns_proposal->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
		break;
	case PROPOSAL_NEARLY_EXPIRED:
		lifetime = real_lifetime(&rdns_proposal->uptime,
		    rdns_proposal->rdns_lifetime);
		if (lifetime > MAX_RTR_SOLICITATIONS *
		    (RTR_SOLICITATION_INTERVAL + 1))
			rdns_proposal->timo.tv_sec = lifetime -
			    MAX_RTR_SOLICITATIONS * RTR_SOLICITATION_INTERVAL;
		else
			rdns_proposal->timo.tv_sec = RTR_SOLICITATION_INTERVAL;
		request_solicitation(iface);
		break;
	case PROPOSAL_WITHDRAWN:
		withdraw_rdns(rdns_proposal);
		rdns_proposal->timo.tv_sec = MAX_RTR_SOLICITATIONS *
		    RTR_SOLICITATION_INTERVAL;
		break;
	case PROPOSAL_STALE:
		rdns_proposal->timo.tv_sec = 0; /* remove immediately */
		break;
	case PROPOSAL_DUPLICATED:
		fatalx("invalid rdns state: PROPOSAL_DUPLICATED");
		break;
	}

	if (log_getverbose()) {
		char	 ifnamebuf[IF_NAMESIZE], *if_name;

		if_name = if_indextoname(rdns_proposal->if_index, ifnamebuf);
		log_debug("%s[%s] %s -> %s, timo: %lld", __func__,
		    if_name == NULL ? "?" : if_name,
		    proposal_state_name(old_state),
		    proposal_state_name(new_state), rdns_proposal->timo.tv_sec);
	}

	if (rdns_proposal->timo.tv_sec == -1) {
		if (evtimer_pending(&rdns_proposal->timer, NULL))
			evtimer_del(&rdns_proposal->timer);
	} else
		evtimer_add(&rdns_proposal->timer, &rdns_proposal->timo);
}

void
request_solicitation(struct slaacd_iface *iface)
{
	struct timespec	now, diff, sol_delay = {RTR_SOLICITATION_INTERVAL, 0};

	clock_gettime(CLOCK_MONOTONIC, &now);
	timespecsub(&now, &iface->last_sol, &diff);
	if (timespeccmp(&diff, &sol_delay, <)) {
		log_debug("last solicitation less than %d seconds ago",
		    RTR_SOLICITATION_INTERVAL);
		return;
	}

	iface->last_sol = now;
	engine_imsg_compose_frontend(IMSG_CTL_SEND_SOLICITATION, 0,
	    &iface->if_index, sizeof(iface->if_index));
}

void
engine_update_iface(struct imsg_ifinfo *imsg_ifinfo)
{
	struct slaacd_iface	*iface;
	int			 need_refresh = 0;

	iface = get_slaacd_iface_by_id(imsg_ifinfo->if_index);
	if (iface == NULL) {
		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		iface->state = IF_DOWN;
		iface->timo.tv_usec = arc4random_uniform(1000000);
		evtimer_set(&iface->timer, iface_timeout, iface);
		iface->if_index = imsg_ifinfo->if_index;
		iface->rdomain = imsg_ifinfo->rdomain;
		iface->running = imsg_ifinfo->running;
		iface->link_state = imsg_ifinfo->link_state;
		iface->autoconf = imsg_ifinfo->autoconf;
		iface->temporary = imsg_ifinfo->temporary;
		iface->soii = imsg_ifinfo->soii;
		memcpy(&iface->hw_address, &imsg_ifinfo->hw_address,
		    sizeof(struct ether_addr));
		memcpy(&iface->ll_address, &imsg_ifinfo->ll_address,
		    sizeof(struct sockaddr_in6));
		memcpy(iface->soiikey, imsg_ifinfo->soiikey,
		    sizeof(iface->soiikey));
		LIST_INIT(&iface->radvs);
		LIST_INSERT_HEAD(&slaacd_interfaces, iface, entries);
		LIST_INIT(&iface->addr_proposals);
		LIST_INIT(&iface->dfr_proposals);
		LIST_INIT(&iface->rdns_proposals);
		need_refresh = 1;
	} else {
		memcpy(&iface->ll_address, &imsg_ifinfo->ll_address,
		    sizeof(struct sockaddr_in6));

		if (iface->autoconf != imsg_ifinfo->autoconf) {
			iface->autoconf = imsg_ifinfo->autoconf;
			need_refresh = 1;
		}

		if (iface->temporary != imsg_ifinfo->temporary) {
			iface->temporary = imsg_ifinfo->temporary;
			need_refresh = 1;
		}

		if (iface->soii != imsg_ifinfo->soii) {
			iface->soii = imsg_ifinfo->soii;
			need_refresh = 1;
		}

		if (memcmp(&iface->hw_address, &imsg_ifinfo->hw_address,
		    sizeof(struct ether_addr)) != 0) {
			memcpy(&iface->hw_address, &imsg_ifinfo->hw_address,
			    sizeof(struct ether_addr));
			need_refresh = 1;
		}

		if (memcmp(iface->soiikey, imsg_ifinfo->soiikey,
		    sizeof(iface->soiikey)) != 0) {
			memcpy(iface->soiikey, imsg_ifinfo->soiikey,
			    sizeof(iface->soiikey));
			need_refresh = 1;
		}

		if (imsg_ifinfo->running != iface->running) {
			iface->running = imsg_ifinfo->running;
			need_refresh = 1;
		}
		if (imsg_ifinfo->link_state != iface->link_state) {
			iface->link_state = imsg_ifinfo->link_state;
			need_refresh = 1;
		}
	}

	if (!need_refresh)
		return;

	if (iface->running && LINK_STATE_IS_UP(iface->link_state))
		iface_state_transition(iface, IF_INIT);

	else
		iface_state_transition(iface, IF_DOWN);
}

void
parse_ra(struct slaacd_iface *iface, struct imsg_ra *ra)
{
	struct icmp6_hdr	*icmp6_hdr;
	struct nd_router_advert	*nd_ra;
	struct radv		*radv;
	struct radv_prefix	*prefix;
	struct radv_rdns	*rdns;
	ssize_t			 len = ra->len;
	const char		*hbuf;
	uint8_t			*p;

#ifndef	SMALL
	if (log_getverbose() > 1)
		debug_log_ra(ra);
#endif	/* SMALL */

	hbuf = sin6_to_str(&ra->from);
	if ((size_t)len < sizeof(struct icmp6_hdr)) {
		log_warnx("received too short message (%ld) from %s", len,
		    hbuf);
		return;
	}

	p = ra->packet;
	icmp6_hdr = (struct icmp6_hdr *)p;
	if (icmp6_hdr->icmp6_type != ND_ROUTER_ADVERT)
		return;

	if (!IN6_IS_ADDR_LINKLOCAL(&ra->from.sin6_addr)) {
		log_debug("RA from non link local address %s", hbuf);
		return;
	}

	if ((size_t)len < sizeof(struct nd_router_advert)) {
		log_warnx("received too short message (%ld) from %s", len,
		    hbuf);
		return;
	}

	if ((radv = calloc(1, sizeof(*radv))) == NULL)
		fatal("calloc");

	LIST_INIT(&radv->prefixes);
	LIST_INIT(&radv->rdns_servers);

	radv->min_lifetime = UINT32_MAX;

	nd_ra = (struct nd_router_advert *)p;
	len -= sizeof(struct nd_router_advert);
	p += sizeof(struct nd_router_advert);

	log_debug("ICMPv6 type(%d), code(%d) from %s of length %ld",
	    nd_ra->nd_ra_type, nd_ra->nd_ra_code, hbuf, len);

	if (nd_ra->nd_ra_code != 0) {
		log_warnx("invalid ICMPv6 code (%d) from %s", nd_ra->nd_ra_code,
		    hbuf);
		goto err;
	}

	memcpy(&radv->from, &ra->from, sizeof(ra->from));

	if (clock_gettime(CLOCK_REALTIME, &radv->when))
		fatal("clock_gettime");
	if (clock_gettime(CLOCK_MONOTONIC, &radv->uptime))
		fatal("clock_gettime");

	radv->curhoplimit = nd_ra->nd_ra_curhoplimit;
	radv->managed = nd_ra->nd_ra_flags_reserved & ND_RA_FLAG_MANAGED;
	radv->other = nd_ra->nd_ra_flags_reserved & ND_RA_FLAG_OTHER;

	switch (nd_ra->nd_ra_flags_reserved & ND_RA_FLAG_RTPREF_MASK) {
	case ND_RA_FLAG_RTPREF_HIGH:
		radv->rpref=HIGH;
		break;
	case ND_RA_FLAG_RTPREF_LOW:
		radv->rpref=LOW;
		break;
	case ND_RA_FLAG_RTPREF_MEDIUM:
		/* fallthrough */
	default:
		radv->rpref=MEDIUM;
		break;
	}
	radv->router_lifetime = ntohs(nd_ra->nd_ra_router_lifetime);
	if (radv->router_lifetime != 0)
		radv->min_lifetime = radv->router_lifetime;
	radv->reachable_time = ntohl(nd_ra->nd_ra_reachable);
	radv->retrans_time = ntohl(nd_ra->nd_ra_retransmit);

	while ((size_t)len >= sizeof(struct nd_opt_hdr)) {
		struct nd_opt_hdr *nd_opt_hdr = (struct nd_opt_hdr *)p;
		struct nd_opt_prefix_info *prf;
		struct nd_opt_rdnss *rdnss;
		struct nd_opt_mtu *mtu;
		struct in6_addr *in6;
		int i;

		len -= sizeof(struct nd_opt_hdr);
		p += sizeof(struct nd_opt_hdr);

		if (nd_opt_hdr->nd_opt_len * 8 - 2 > len) {
			log_warnx("invalid option len: %u > %ld",
			    nd_opt_hdr->nd_opt_len, len);
			goto err;
		}

		switch (nd_opt_hdr->nd_opt_type) {
		case ND_OPT_PREFIX_INFORMATION:
			if (nd_opt_hdr->nd_opt_len != 4) {
				log_warnx("invalid ND_OPT_PREFIX_INFORMATION: "
				   "len != 4");
				goto err;
			}

			if ((prefix = calloc(1, sizeof(*prefix))) == NULL)
				fatal("calloc");

			prf = (struct nd_opt_prefix_info*) nd_opt_hdr;
			prefix->prefix = prf->nd_opt_pi_prefix;
			prefix->prefix_len = prf->nd_opt_pi_prefix_len;
			prefix->onlink = prf->nd_opt_pi_flags_reserved &
			    ND_OPT_PI_FLAG_ONLINK;
			prefix->autonomous = prf->nd_opt_pi_flags_reserved &
			    ND_OPT_PI_FLAG_AUTO;
			prefix->vltime = ntohl(prf->nd_opt_pi_valid_time);
			prefix->pltime = ntohl(prf->nd_opt_pi_preferred_time);
			if (radv->min_lifetime > prefix->pltime)
				radv->min_lifetime = prefix->pltime;

			LIST_INSERT_HEAD(&radv->prefixes, prefix, entries);

			break;

		case ND_OPT_RDNSS:
			if (nd_opt_hdr->nd_opt_len  < 3) {
				log_warnx("invalid ND_OPT_RDNSS: len < 24");
				goto err;
			}

			if ((nd_opt_hdr->nd_opt_len - 1) % 2 != 0) {
				log_warnx("invalid ND_OPT_RDNSS: length with"
				    "out header is not multiply of 16: %d",
				    (nd_opt_hdr->nd_opt_len - 1) * 8);
				goto err;
			}

			rdnss = (struct nd_opt_rdnss*) nd_opt_hdr;

			radv->rdns_lifetime = ntohl(
			    rdnss->nd_opt_rdnss_lifetime);
			if (radv->min_lifetime > radv->rdns_lifetime)
				radv->min_lifetime = radv->rdns_lifetime;

			in6 = (struct in6_addr*) (p + 6);
			for (i=0; i < (nd_opt_hdr->nd_opt_len - 1)/2; i++,
			    in6++) {
				if ((rdns = calloc(1, sizeof(*rdns))) == NULL)
					fatal("calloc");
				memcpy(&rdns->rdns, in6, sizeof(rdns->rdns));
				LIST_INSERT_HEAD(&radv->rdns_servers, rdns,
				    entries);
			}
			break;
		case ND_OPT_MTU:
			if (nd_opt_hdr->nd_opt_len != 1) {
				log_warnx("invalid ND_OPT_MTU: len != 1");
				goto err;
			}
			mtu = (struct nd_opt_mtu*) nd_opt_hdr;
			radv->mtu = ntohl(mtu->nd_opt_mtu_mtu);

			/* path MTU cannot be less than IPV6_MMTU */
			if (radv->mtu < IPV6_MMTU) {
				radv->mtu = 0;
				log_warnx("invalid advertised MTU");
			}

			break;
		case ND_OPT_DNSSL:
		case ND_OPT_REDIRECTED_HEADER:
		case ND_OPT_SOURCE_LINKADDR:
		case ND_OPT_TARGET_LINKADDR:
		case ND_OPT_ROUTE_INFO:
#if 0
			log_debug("\tOption: %u (len: %u) not implemented",
			    nd_opt_hdr->nd_opt_type, nd_opt_hdr->nd_opt_len *
			    8);
#endif
			break;
		default:
			log_debug("\t\tUNKNOWN: %d", nd_opt_hdr->nd_opt_type);
			break;

		}
		len -= nd_opt_hdr->nd_opt_len * 8 - 2;
		p += nd_opt_hdr->nd_opt_len * 8 - 2;
	}
	update_iface_ra(iface, radv);
	return;

err:
	free_ra(radv);
}

void
gen_addr(struct slaacd_iface *iface, struct radv_prefix *prefix, struct
    address_proposal *addr_proposal, int temporary)
{
	SHA2_CTX ctx;
	struct in6_addr	iid;
	int i;
	u_int8_t digest[SHA512_DIGEST_LENGTH];

	memset(&iid, 0, sizeof(iid));

	/* from in6_ifadd() in nd6_rtr.c */
	/* XXX from in6.h, guarded by #ifdef _KERNEL   XXX nonstandard */
#define s6_addr32 __u6_addr.__u6_addr32

	in6_prefixlen2mask(&addr_proposal->mask, addr_proposal->prefix_len);

	memset(&addr_proposal->addr, 0, sizeof(addr_proposal->addr));

	addr_proposal->addr.sin6_family = AF_INET6;
	addr_proposal->addr.sin6_len = sizeof(addr_proposal->addr);

	memcpy(&addr_proposal->addr.sin6_addr, &prefix->prefix,
	    sizeof(addr_proposal->addr.sin6_addr));

	for (i = 0; i < 4; i++)
		addr_proposal->addr.sin6_addr.s6_addr32[i] &=
		    addr_proposal->mask.s6_addr32[i];

	if (temporary) {
		arc4random_buf(&iid.s6_addr, sizeof(iid.s6_addr));
	} else if (iface->soii) {
		SHA512Init(&ctx);
		SHA512Update(&ctx, &prefix->prefix,
		    sizeof(prefix->prefix));
		SHA512Update(&ctx, &iface->hw_address,
		    sizeof(iface->hw_address));
		SHA512Update(&ctx, &prefix->dad_counter,
		    sizeof(prefix->dad_counter));
		SHA512Update(&ctx, addr_proposal->soiikey,
		    sizeof(addr_proposal->soiikey));
		SHA512Final(digest, &ctx);

		memcpy(&iid.s6_addr, digest + (sizeof(digest) -
		    sizeof(iid.s6_addr)), sizeof(iid.s6_addr));
	} else {
		/* This is safe, because we have a 64 prefix len */
		memcpy(&iid.s6_addr, &iface->ll_address.sin6_addr,
		    sizeof(iid.s6_addr));
	}

	for (i = 0; i < 4; i++)
		addr_proposal->addr.sin6_addr.s6_addr32[i] |=
		    (iid.s6_addr32[i] & ~addr_proposal->mask.s6_addr32[i]);
#undef s6_addr32
}

/* from sys/netinet6/in6.c */
void
in6_prefixlen2mask(struct in6_addr *maskp, int len)
{
	u_char maskarray[8] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};
	int bytelen, bitlen, i;

	if (0 > len || len > 128)
		fatalx("%s: invalid prefix length(%d)\n", __func__, len);

	bzero(maskp, sizeof(*maskp));
	bytelen = len / 8;
	bitlen = len % 8;
	for (i = 0; i < bytelen; i++)
		maskp->s6_addr[i] = 0xff;
	/* len == 128 is ok because bitlen == 0 then */
	if (bitlen)
		maskp->s6_addr[bytelen] = maskarray[bitlen - 1];
}

#ifndef	SMALL
/* from kame via ifconfig, where it's called prefix() */
int
in6_mask2prefixlen(struct in6_addr *in6)
{
	u_char *nam = (u_char *)in6;
	int byte, bit, plen = 0, size = sizeof(struct in6_addr);

	for (byte = 0; byte < size; byte++, plen += 8)
		if (nam[byte] != 0xff)
			break;
	if (byte == size)
		return (plen);
	for (bit = 7; bit != 0; bit--, plen++)
		if (!(nam[byte] & (1 << bit)))
			break;
	for (; bit != 0; bit--)
		if (nam[byte] & (1 << bit))
			return (0);
	byte++;
	for (; byte < size; byte++)
		if (nam[byte])
			return (0);
	return (plen);
}

void
debug_log_ra(struct imsg_ra *ra)
{
	struct nd_router_advert	*nd_ra;
	ssize_t			 len = ra->len;
	char			 ntopbuf[INET6_ADDRSTRLEN];
	const char		*hbuf;
	uint8_t			*p;

	hbuf = sin6_to_str(&ra->from);

	if (!IN6_IS_ADDR_LINKLOCAL(&ra->from.sin6_addr)) {
		log_warnx("RA from non link local address %s", hbuf);
		return;
	}

	if ((size_t)len < sizeof(struct nd_router_advert)) {
		log_warnx("received too short message (%ld) from %s", len,
		    hbuf);
		return;
	}

	p = ra->packet;
	nd_ra = (struct nd_router_advert *)p;
	len -= sizeof(struct nd_router_advert);
	p += sizeof(struct nd_router_advert);

	log_debug("ICMPv6 type(%d), code(%d) from %s of length %ld",
	    nd_ra->nd_ra_type, nd_ra->nd_ra_code, hbuf, len);

	if (nd_ra->nd_ra_type != ND_ROUTER_ADVERT) {
		log_warnx("invalid ICMPv6 type (%d) from %s", nd_ra->nd_ra_type,
		    hbuf);
		return;
	}

	if (nd_ra->nd_ra_code != 0) {
		log_warnx("invalid ICMPv6 code (%d) from %s", nd_ra->nd_ra_code,
		    hbuf);
		return;
	}

	log_debug("---");
	log_debug("RA from %s", hbuf);
	log_debug("\tCur Hop Limit: %u", nd_ra->nd_ra_curhoplimit);
	log_debug("\tManaged address configuration: %d",
	    (nd_ra->nd_ra_flags_reserved & ND_RA_FLAG_MANAGED) ? 1 : 0);
	log_debug("\tOther configuration: %d",
	    (nd_ra->nd_ra_flags_reserved & ND_RA_FLAG_OTHER) ? 1 : 0);
	switch (nd_ra->nd_ra_flags_reserved & ND_RA_FLAG_RTPREF_MASK) {
	case ND_RA_FLAG_RTPREF_HIGH:
		log_debug("\tRouter Preference: high");
		break;
	case ND_RA_FLAG_RTPREF_MEDIUM:
		log_debug("\tRouter Preference: medium");
		break;
	case ND_RA_FLAG_RTPREF_LOW:
		log_debug("\tRouter Preference: low");
		break;
	case ND_RA_FLAG_RTPREF_RSV:
		log_debug("\tRouter Preference: reserved");
		break;
	}
	log_debug("\tRouter Lifetime: %hds",
	    ntohs(nd_ra->nd_ra_router_lifetime));
	log_debug("\tReachable Time: %ums", ntohl(nd_ra->nd_ra_reachable));
	log_debug("\tRetrans Timer: %ums", ntohl(nd_ra->nd_ra_retransmit));

	while ((size_t)len >= sizeof(struct nd_opt_hdr)) {
		struct nd_opt_hdr *nd_opt_hdr = (struct nd_opt_hdr *)p;
		struct nd_opt_mtu *mtu;
		struct nd_opt_prefix_info *prf;
		struct nd_opt_rdnss *rdnss;
		struct in6_addr *in6;
		int i;

		len -= sizeof(struct nd_opt_hdr);
		p += sizeof(struct nd_opt_hdr);
		if (nd_opt_hdr->nd_opt_len * 8 - 2 > len) {
			log_warnx("invalid option len: %u > %ld",
			    nd_opt_hdr->nd_opt_len, len);
			return;
		}
		log_debug("\tOption: %u (len: %u)", nd_opt_hdr->nd_opt_type,
		    nd_opt_hdr->nd_opt_len * 8);
		switch (nd_opt_hdr->nd_opt_type) {
		case ND_OPT_SOURCE_LINKADDR:
			if (nd_opt_hdr->nd_opt_len == 1)
				log_debug("\t\tND_OPT_SOURCE_LINKADDR: "
				    "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
				    p[0], p[1], p[2], p[3], p[4], p[5], p[6],
				    p[7]);
			else
				log_debug("\t\tND_OPT_SOURCE_LINKADDR");
			break;
		case ND_OPT_TARGET_LINKADDR:
			if (nd_opt_hdr->nd_opt_len == 1)
				log_debug("\t\tND_OPT_TARGET_LINKADDR: "
				    "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
				    p[0], p[1], p[2], p[3], p[4], p[5], p[6],
				    p[7]);
			else
				log_debug("\t\tND_OPT_TARGET_LINKADDR");
			break;
		case ND_OPT_PREFIX_INFORMATION:
			if (nd_opt_hdr->nd_opt_len != 4) {
				log_warnx("invalid ND_OPT_PREFIX_INFORMATION: "
				   "len != 4");
				return;
			}
			prf = (struct nd_opt_prefix_info*) nd_opt_hdr;

			log_debug("\t\tND_OPT_PREFIX_INFORMATION: %s/%u",
			    inet_ntop(AF_INET6, &prf->nd_opt_pi_prefix,
			    ntopbuf, INET6_ADDRSTRLEN),
			    prf->nd_opt_pi_prefix_len);
			log_debug("\t\t\tOn-link: %d",
			    prf->nd_opt_pi_flags_reserved &
			    ND_OPT_PI_FLAG_ONLINK ? 1:0);
			log_debug("\t\t\tAutonomous address-configuration: %d",
			    prf->nd_opt_pi_flags_reserved &
			    ND_OPT_PI_FLAG_AUTO ? 1 : 0);
			log_debug("\t\t\tvltime: %u",
			    ntohl(prf->nd_opt_pi_valid_time));
			log_debug("\t\t\tpltime: %u",
			    ntohl(prf->nd_opt_pi_preferred_time));
			break;
		case ND_OPT_REDIRECTED_HEADER:
			log_debug("\t\tND_OPT_REDIRECTED_HEADER");
			break;
		case ND_OPT_MTU:
			if (nd_opt_hdr->nd_opt_len != 1) {
				log_warnx("invalid ND_OPT_MTU: len != 1");
				return;
			}
			mtu = (struct nd_opt_mtu*) nd_opt_hdr;
			log_debug("\t\tND_OPT_MTU: %u",
			    ntohl(mtu->nd_opt_mtu_mtu));
			break;
		case ND_OPT_ROUTE_INFO:
			log_debug("\t\tND_OPT_ROUTE_INFO");
			break;
		case ND_OPT_RDNSS:
			if (nd_opt_hdr->nd_opt_len  < 3) {
				log_warnx("invalid ND_OPT_RDNSS: len < 24");
				return;
			}
			if ((nd_opt_hdr->nd_opt_len - 1) % 2 != 0) {
				log_warnx("invalid ND_OPT_RDNSS: length with"
				    "out header is not multiply of 16: %d",
				    (nd_opt_hdr->nd_opt_len - 1) * 8);
				return;
			}
			rdnss = (struct nd_opt_rdnss*) nd_opt_hdr;
			log_debug("\t\tND_OPT_RDNSS: lifetime: %u", ntohl(
			    rdnss->nd_opt_rdnss_lifetime));
			in6 = (struct in6_addr*) (p + 6);
			for (i=0; i < (nd_opt_hdr->nd_opt_len - 1)/2; i++,
			    in6++) {
				log_debug("\t\t\t%s", inet_ntop(AF_INET6, in6,
				    ntopbuf, INET6_ADDRSTRLEN));
			}
			break;
		default:
			log_debug("\t\tUNKNOWN: %d", nd_opt_hdr->nd_opt_type);
			break;

		}
		len -= nd_opt_hdr->nd_opt_len * 8 - 2;
		p += nd_opt_hdr->nd_opt_len * 8 - 2;
	}
}
#endif	/* SMALL */

void update_iface_ra(struct slaacd_iface *iface, struct radv *ra)
{
	struct radv		*old_ra;
	struct radv_prefix	*prefix;

	if ((old_ra = find_ra(iface, &ra->from)) == NULL)
		LIST_INSERT_HEAD(&iface->radvs, ra, entries);
	else {
		LIST_REPLACE(old_ra, ra, entries);
		merge_dad_couters(old_ra, ra);
		free_ra(old_ra);
	}

	update_iface_ra_dfr(iface, ra);

	LIST_FOREACH(prefix, &ra->prefixes, entries) {
		if (!prefix->autonomous || prefix->vltime == 0 ||
		    prefix->pltime > prefix->vltime ||
		    IN6_IS_ADDR_LINKLOCAL(&prefix->prefix))
			continue;
		update_iface_ra_prefix(iface, ra, prefix);
	}

	update_iface_ra_rdns(iface, ra);
}

void
update_iface_ra_dfr(struct slaacd_iface *iface, struct radv *ra)
{
	struct dfr_proposal	*dfr_proposal;

	dfr_proposal = find_dfr_proposal_by_gw(iface, &ra->from);

	if (ra->router_lifetime == 0) {
		free_dfr_proposal(dfr_proposal);
		return;
	}

	if (!dfr_proposal) {
		/* new proposal */
		gen_dfr_proposal(iface, ra);
		return;
	}

	dfr_proposal->when = ra->when;
	dfr_proposal->uptime = ra->uptime;
	dfr_proposal->router_lifetime = ra->router_lifetime;

	log_debug("%s, dfr state: %s, rl: %d", __func__,
	    proposal_state_name(dfr_proposal->state),
	    real_lifetime(&dfr_proposal->uptime,
	    dfr_proposal->router_lifetime));

	switch (dfr_proposal->state) {
	case PROPOSAL_CONFIGURED:
	case PROPOSAL_NEARLY_EXPIRED:
		/* routes do not expire in the kernel, update timeout */
		dfr_proposal_state_transition(dfr_proposal,
		    PROPOSAL_CONFIGURED);
		break;
	case PROPOSAL_IF_DOWN:
	case PROPOSAL_WITHDRAWN:
		log_debug("updating dfr");
		configure_dfr(dfr_proposal);
		break;
	default:
		log_debug("%s: iface %d: %s", __func__, iface->if_index,
		    sin6_to_str(&dfr_proposal->addr));
		break;
	}
}

void
update_iface_ra_prefix(struct slaacd_iface *iface, struct radv *ra,
    struct radv_prefix *prefix)
{
	struct address_proposal	*addr_proposal;
	uint32_t		 pltime, vltime;
	int			 found, found_temporary, duplicate_found;

	found = found_temporary = duplicate_found = 0;

	if (!!iface->autoconf != !!iface->temporary) {
		struct address_proposal	*tmp;
		/*
		 * If only the autoconf or temporary flag is set, check if we
		 * have the "other kind" of address configured and delete it.
		 */
		LIST_FOREACH_SAFE (addr_proposal, &iface->addr_proposals,
		    entries, tmp) {
			if ((!addr_proposal->temporary && !iface->autoconf) ||
			    (addr_proposal->temporary && !iface->temporary))
				free_address_proposal(addr_proposal);
		}
	}

	LIST_FOREACH(addr_proposal, &iface->addr_proposals, entries) {
		if (prefix->prefix_len == addr_proposal-> prefix_len &&
		    memcmp(&prefix->prefix, &addr_proposal->prefix,
		    sizeof(struct in6_addr)) != 0)
			continue;

		if (memcmp(&addr_proposal->hw_address,
		    &iface->hw_address,
		    sizeof(addr_proposal->hw_address)) != 0)
			continue;

		if (memcmp(&addr_proposal->soiikey, &iface->soiikey,
		    sizeof(addr_proposal->soiikey)) != 0)
			continue;

		if (addr_proposal->state == PROPOSAL_DUPLICATED) {
			duplicate_found = 1;
			continue;
		}

		vltime = prefix->vltime;

		if (addr_proposal->temporary) {
			struct timespec	now;
			int64_t		ltime, mtime;

			if (clock_gettime(CLOCK_MONOTONIC, &now))
				fatal("clock_gettime");

			mtime = addr_proposal->created.tv_sec +
			    PRIV_PREFERRED_LIFETIME -
			    addr_proposal->desync_factor;

			ltime = MINIMUM(mtime, now.tv_sec + prefix->pltime) -
			    now.tv_sec;

			pltime = ltime > 0 ? ltime : 0;

			ltime = MINIMUM(addr_proposal->created.tv_sec +
			    PRIV_VALID_LIFETIME, now.tv_sec + vltime) -
			    now.tv_sec;
			vltime = ltime > 0 ? ltime : 0;

			if ((mtime - now.tv_sec) > PRIV_REGEN_ADVANCE)
				found_temporary = 1;
		} else {
			pltime = prefix->pltime;
			found = 1;
		}

		addr_proposal->from = ra->from;
		addr_proposal->when = ra->when;
		addr_proposal->uptime = ra->uptime;

		addr_proposal->vltime = vltime;
		addr_proposal->pltime = pltime;

		if (ra->mtu == iface->cur_mtu)
			addr_proposal->mtu = 0;
		else {
			addr_proposal->mtu = ra->mtu;
			iface->cur_mtu = ra->mtu;
		}

		log_debug("%s, addr state: %s", __func__,
		    proposal_state_name(addr_proposal->state));

		switch (addr_proposal->state) {
		case PROPOSAL_CONFIGURED:
		case PROPOSAL_NEARLY_EXPIRED:
		case PROPOSAL_IF_DOWN:
		case PROPOSAL_WITHDRAWN:
			log_debug("updating address");
			configure_address(addr_proposal);
			break;
		default:
			log_debug("%s: iface %d: %s", __func__, iface->if_index,
			    sin6_to_str(&addr_proposal->addr));
			break;
		}
	}

	if (!found && iface->autoconf && duplicate_found && iface->soii) {
		prefix->dad_counter++;
		log_debug("%s dad_counter: %d", __func__, prefix->dad_counter);
		gen_address_proposal(iface, ra, prefix, 0);
	} else if (!found  && iface->autoconf && (iface->soii ||
	    prefix->prefix_len <= 64))
		/* new proposal */
		gen_address_proposal(iface, ra, prefix, 0);

	/* temporary addresses do not depend on eui64 */
	if (!found_temporary && iface->temporary) {
		if (prefix->pltime >= PRIV_REGEN_ADVANCE) {
			/* new temporary proposal */
			gen_address_proposal(iface, ra, prefix, 1);
		} else if (prefix->pltime > 0) {
			log_warnx("%s: pltime from %s is too small: %d < %d; "
			    "not generating temporary address", __func__,
			    sin6_to_str(&ra->from), prefix->pltime,
			    PRIV_REGEN_ADVANCE);
		}
	}
}

void
update_iface_ra_rdns(struct slaacd_iface *iface, struct radv *ra)
{
	struct rdns_proposal	*rdns_proposal;
	struct radv_rdns	*radv_rdns;
	struct in6_addr		 rdns[MAX_RDNS_COUNT];
	int			 rdns_count;

	rdns_proposal = find_rdns_proposal_by_gw(iface, &ra->from);

	if (!rdns_proposal) {
		/* new proposal */
		if (!LIST_EMPTY(&ra->rdns_servers))
			gen_rdns_proposal(iface, ra);
		return;
	}

	rdns_count = 0;
	memset(&rdns, 0, sizeof(rdns));
	LIST_FOREACH(radv_rdns, &ra->rdns_servers, entries) {
		memcpy(&rdns[rdns_count++],
		    &radv_rdns->rdns, sizeof(struct in6_addr));
		if (rdns_proposal->rdns_count == MAX_RDNS_COUNT)
			break;
	}

	if (rdns_count == 0) {
		free_rdns_proposal(rdns_proposal);
		return;
	}

	if (rdns_proposal->rdns_count != rdns_count ||
	    memcmp(&rdns_proposal->rdns, &rdns, sizeof(rdns)) != 0) {
		memcpy(&rdns_proposal->rdns, &rdns, sizeof(rdns));
		rdns_proposal->rdns_count = rdns_count;
		rdns_proposal->state = PROPOSAL_NOT_CONFIGURED;
	}
	rdns_proposal->when = ra->when;
	rdns_proposal->uptime = ra->uptime;
	rdns_proposal->rdns_lifetime = ra->rdns_lifetime;

	log_debug("%s, rdns state: %s, rl: %d", __func__,
	    proposal_state_name(rdns_proposal->state),
	    real_lifetime(&rdns_proposal->uptime,
	    rdns_proposal->rdns_lifetime));

	switch (rdns_proposal->state) {
	case PROPOSAL_CONFIGURED:
	case PROPOSAL_NEARLY_EXPIRED:
		/* rdns are not expired by the kernel, update timeout */
		rdns_proposal_state_transition(rdns_proposal,
		    PROPOSAL_CONFIGURED);
		break;
	case PROPOSAL_IF_DOWN:
	case PROPOSAL_WITHDRAWN:
	case PROPOSAL_NOT_CONFIGURED:
		log_debug("updating rdns");
		rdns_proposal_state_transition(rdns_proposal,
		    PROPOSAL_CONFIGURED);
		compose_rdns_proposal(rdns_proposal->if_index,
		    rdns_proposal->rdomain);
		break;
	default:
		log_debug("%s: iface %d: %s", __func__, iface->if_index,
		    sin6_to_str(&rdns_proposal->from));
		break;
	}
}


void
configure_address(struct address_proposal *addr_proposal)
{
	struct imsg_configure_address	 address;
	struct slaacd_iface		*iface;

	log_debug("%s: %d", __func__, addr_proposal->if_index);

	address.if_index = addr_proposal->if_index;
	memcpy(&address.addr, &addr_proposal->addr, sizeof(address.addr));
	memcpy(&address.gw, &addr_proposal->from, sizeof(address.gw));
	memcpy(&address.mask, &addr_proposal->mask, sizeof(address.mask));
	address.vltime = addr_proposal->vltime;
	address.pltime = addr_proposal->pltime;
	address.temporary = addr_proposal->temporary;
	address.mtu = addr_proposal->mtu;

	engine_imsg_compose_main(IMSG_CONFIGURE_ADDRESS, 0, &address,
	    sizeof(address));

	if ((iface = get_slaacd_iface_by_id(addr_proposal->if_index)) != NULL)
		iface_state_transition(iface, IF_BOUND);
	addr_proposal_state_transition(addr_proposal, PROPOSAL_CONFIGURED);
}

void
gen_address_proposal(struct slaacd_iface *iface, struct radv *ra, struct
    radv_prefix *prefix, int temporary)
{
	struct address_proposal	*addr_proposal;
	const char		*hbuf;

	if ((addr_proposal = calloc(1, sizeof(*addr_proposal))) == NULL)
		fatal("calloc");
	addr_proposal->id = ++proposal_id;
	evtimer_set(&addr_proposal->timer, address_proposal_timeout,
	    addr_proposal);
	addr_proposal->timo.tv_sec = 1;
	addr_proposal->timo.tv_usec = arc4random_uniform(1000000);
	addr_proposal->state = PROPOSAL_NOT_CONFIGURED;
	if (clock_gettime(CLOCK_MONOTONIC, &addr_proposal->created))
		fatal("clock_gettime");
	addr_proposal->when = ra->when;
	addr_proposal->uptime = ra->uptime;
	addr_proposal->if_index = iface->if_index;
	memcpy(&addr_proposal->from, &ra->from,
	    sizeof(addr_proposal->from));
	memcpy(&addr_proposal->hw_address, &iface->hw_address,
	    sizeof(addr_proposal->hw_address));
	memcpy(&addr_proposal->soiikey, &iface->soiikey,
	    sizeof(addr_proposal->soiikey));
	addr_proposal->temporary = temporary;
	memcpy(&addr_proposal->prefix, &prefix->prefix,
	    sizeof(addr_proposal->prefix));
	addr_proposal->prefix_len = prefix->prefix_len;

	if (temporary) {
		addr_proposal->vltime = MINIMUM(prefix->vltime,
		    PRIV_VALID_LIFETIME);
		addr_proposal->desync_factor =
		    arc4random_uniform(PRIV_MAX_DESYNC_FACTOR);

		addr_proposal->pltime = MINIMUM(prefix->pltime,
		    PRIV_PREFERRED_LIFETIME - addr_proposal->desync_factor);
	} else {
		addr_proposal->vltime = prefix->vltime;
		addr_proposal->pltime = prefix->pltime;
	}

	if (ra->mtu == iface->cur_mtu)
		addr_proposal->mtu = 0;
	else {
		addr_proposal->mtu = ra->mtu;
		iface->cur_mtu = ra->mtu;
	}

	gen_addr(iface, prefix, addr_proposal, temporary);

	LIST_INSERT_HEAD(&iface->addr_proposals, addr_proposal, entries);
	configure_address(addr_proposal);

	hbuf = sin6_to_str(&addr_proposal->addr);
	log_debug("%s: iface %d: %s", __func__, iface->if_index, hbuf);
}

void
free_address_proposal(struct address_proposal *addr_proposal)
{
	if (addr_proposal == NULL)
		return;

	LIST_REMOVE(addr_proposal, entries);
	evtimer_del(&addr_proposal->timer);
	switch (addr_proposal->state) {
	case PROPOSAL_CONFIGURED:
	case PROPOSAL_NEARLY_EXPIRED:
	case PROPOSAL_STALE:
		withdraw_addr(addr_proposal);
		break;
	default:
		break;
	}
	free(addr_proposal);
}

void
withdraw_addr(struct address_proposal *addr_proposal)
{
	struct imsg_configure_address	address;

	log_debug("%s: %d", __func__, addr_proposal->if_index);
	memset(&address, 0, sizeof(address));
	address.if_index = addr_proposal->if_index;
	memcpy(&address.addr, &addr_proposal->addr, sizeof(address.addr));

	engine_imsg_compose_main(IMSG_WITHDRAW_ADDRESS, 0, &address,
	    sizeof(address));
}

void
gen_dfr_proposal(struct slaacd_iface *iface, struct radv *ra)
{
	struct dfr_proposal	*dfr_proposal;
	const char		*hbuf;

	if ((dfr_proposal = calloc(1, sizeof(*dfr_proposal))) == NULL)
		fatal("calloc");
	dfr_proposal->id = ++proposal_id;
	evtimer_set(&dfr_proposal->timer, dfr_proposal_timeout,
	    dfr_proposal);
	dfr_proposal->timo.tv_sec = 1;
	dfr_proposal->timo.tv_usec = arc4random_uniform(1000000);
	dfr_proposal->state = PROPOSAL_NOT_CONFIGURED;
	dfr_proposal->when = ra->when;
	dfr_proposal->uptime = ra->uptime;
	dfr_proposal->if_index = iface->if_index;
	dfr_proposal->rdomain = iface->rdomain;
	memcpy(&dfr_proposal->addr, &ra->from,
	    sizeof(dfr_proposal->addr));
	dfr_proposal->router_lifetime = ra->router_lifetime;
	dfr_proposal->rpref = ra->rpref;

	LIST_INSERT_HEAD(&iface->dfr_proposals, dfr_proposal, entries);
	configure_dfr(dfr_proposal);

	hbuf = sin6_to_str(&dfr_proposal->addr);
	log_debug("%s: iface %d: %s", __func__, iface->if_index, hbuf);
}

void
configure_dfr(struct dfr_proposal *dfr_proposal)
{
	struct imsg_configure_dfr	 dfr;

	log_debug("%s: %d", __func__, dfr_proposal->if_index);

	dfr.if_index = dfr_proposal->if_index;
	dfr.rdomain = dfr_proposal->rdomain;
	memcpy(&dfr.addr, &dfr_proposal->addr, sizeof(dfr.addr));
	dfr.router_lifetime = dfr_proposal->router_lifetime;

	engine_imsg_compose_main(IMSG_CONFIGURE_DFR, 0, &dfr, sizeof(dfr));

	dfr_proposal_state_transition(dfr_proposal, PROPOSAL_CONFIGURED);
}

void
withdraw_dfr(struct dfr_proposal *dfr_proposal)
{
	struct imsg_configure_dfr	 dfr;

	log_debug("%s: %d", __func__, dfr_proposal->if_index);

	dfr.if_index = dfr_proposal->if_index;
	dfr.rdomain = dfr_proposal->rdomain;
	memcpy(&dfr.addr, &dfr_proposal->addr, sizeof(dfr.addr));
	dfr.router_lifetime = dfr_proposal->router_lifetime;

	engine_imsg_compose_main(IMSG_WITHDRAW_DFR, 0, &dfr, sizeof(dfr));
}

void
free_dfr_proposal(struct dfr_proposal *dfr_proposal)
{
	if (dfr_proposal == NULL)
		return;

	LIST_REMOVE(dfr_proposal, entries);
	evtimer_del(&dfr_proposal->timer);
	switch (dfr_proposal->state) {
	case PROPOSAL_CONFIGURED:
	case PROPOSAL_NEARLY_EXPIRED:
	case PROPOSAL_STALE:
		withdraw_dfr(dfr_proposal);
		break;
	default:
		break;
	}
	free(dfr_proposal);
}

void
gen_rdns_proposal(struct slaacd_iface *iface, struct radv *ra)
{
	struct rdns_proposal	*rdns_proposal;
	struct radv_rdns	*rdns;
	const char		*hbuf;

	if ((rdns_proposal = calloc(1, sizeof(*rdns_proposal))) == NULL)
		fatal("calloc");
	rdns_proposal->id = ++proposal_id;
	evtimer_set(&rdns_proposal->timer, rdns_proposal_timeout,
	    rdns_proposal);
	rdns_proposal->timo.tv_sec = 1;
	rdns_proposal->timo.tv_usec = arc4random_uniform(1000000);
	rdns_proposal->state = PROPOSAL_NOT_CONFIGURED;
	rdns_proposal->when = ra->when;
	rdns_proposal->uptime = ra->uptime;
	rdns_proposal->if_index = iface->if_index;
	rdns_proposal->rdomain = iface->rdomain;
	memcpy(&rdns_proposal->from, &ra->from,
	    sizeof(rdns_proposal->from));
	rdns_proposal->rdns_lifetime = ra->rdns_lifetime;
	LIST_FOREACH(rdns, &ra->rdns_servers, entries) {
		memcpy(&rdns_proposal->rdns[rdns_proposal->rdns_count++],
		    &rdns->rdns, sizeof(struct in6_addr));
		if (rdns_proposal->rdns_count == MAX_RDNS_COUNT)
			break;
	}

	LIST_INSERT_HEAD(&iface->rdns_proposals, rdns_proposal, entries);
	compose_rdns_proposal(iface->if_index, iface->rdomain);

	hbuf = sin6_to_str(&rdns_proposal->from);
	log_debug("%s: iface %d: %s", __func__, iface->if_index, hbuf);
}

void
compose_rdns_proposal(uint32_t if_index, int rdomain)
{
	struct imsg_propose_rdns rdns;
	struct slaacd_iface	*iface;
	struct rdns_proposal	*rdns_proposal;
	int			 i;

	memset(&rdns, 0, sizeof(rdns));
	rdns.if_index = if_index;
	rdns.rdomain = rdomain;

	if ((iface = get_slaacd_iface_by_id(if_index)) != NULL) {
		LIST_FOREACH(rdns_proposal, &iface->rdns_proposals, entries) {
			if (rdns_proposal->state == PROPOSAL_WITHDRAWN ||
			    rdns_proposal->state == PROPOSAL_STALE)
				continue;
			rdns_proposal_state_transition(rdns_proposal,
			    PROPOSAL_CONFIGURED);
			for (i = 0; i < rdns_proposal->rdns_count &&
				 rdns.rdns_count < MAX_RDNS_COUNT; i++) {
				rdns.rdns[rdns.rdns_count++] =
				    rdns_proposal->rdns[i];
			}
		}
	}

	engine_imsg_compose_main(IMSG_PROPOSE_RDNS, 0, &rdns, sizeof(rdns));
}

void
free_rdns_proposal(struct rdns_proposal *rdns_proposal)
{
	if (rdns_proposal == NULL)
		return;

	LIST_REMOVE(rdns_proposal, entries);
	evtimer_del(&rdns_proposal->timer);
	switch (rdns_proposal->state) {
	case PROPOSAL_CONFIGURED:
	case PROPOSAL_NEARLY_EXPIRED:
	case PROPOSAL_STALE:
		withdraw_rdns(rdns_proposal);
		break;
	default:
		break;
	}
	free(rdns_proposal);
}

void
withdraw_rdns(struct rdns_proposal *rdns_proposal)
{
	log_debug("%s: %d", __func__, rdns_proposal->if_index);

	rdns_proposal->state = PROPOSAL_WITHDRAWN;

	/* we have to re-propose all rdns servers, minus one */
	compose_rdns_proposal(rdns_proposal->if_index, rdns_proposal->rdomain);
}

void
address_proposal_timeout(int fd, short events, void *arg)
{
	struct address_proposal	*addr_proposal;
	struct slaacd_iface	*iface = NULL;
	struct radv		*ra = NULL;
	struct radv_prefix	*prefix = NULL;
	const char		*hbuf;

	addr_proposal = (struct address_proposal *)arg;

	hbuf = sin6_to_str(&addr_proposal->addr);
	log_debug("%s: iface %d: %s [%s], priv: %s", __func__,
	    addr_proposal->if_index, hbuf,
	    proposal_state_name(addr_proposal->state),
	    addr_proposal->temporary ? "y" : "n");

	switch (addr_proposal->state) {
	case PROPOSAL_IF_DOWN:
		addr_proposal_state_transition(addr_proposal, PROPOSAL_STALE);
		break;
	case PROPOSAL_CONFIGURED:
		addr_proposal_state_transition(addr_proposal,
		    PROPOSAL_NEARLY_EXPIRED);
		break;
	case PROPOSAL_NEARLY_EXPIRED:
		if (real_lifetime(&addr_proposal->uptime,
		    addr_proposal->vltime) > 0)
			addr_proposal_state_transition(addr_proposal,
			    PROPOSAL_NEARLY_EXPIRED);
		else
			addr_proposal_state_transition(addr_proposal,
			    PROPOSAL_STALE);
		break;
	case PROPOSAL_DUPLICATED:
		iface = get_slaacd_iface_by_id(addr_proposal->if_index);
		if (iface != NULL)
			ra = find_ra(iface, &addr_proposal->from);
		if (ra != NULL)
			prefix = find_prefix(ra, &addr_proposal->prefix,
			    addr_proposal->prefix_len);
		if (prefix != NULL) {
			if (!addr_proposal->temporary) {
				prefix->dad_counter++;
				gen_address_proposal(iface, ra, prefix, 0);
			} else
				gen_address_proposal(iface, ra, prefix, 1);
		}
		addr_proposal_state_transition(addr_proposal, PROPOSAL_STALE);
		break;
	case PROPOSAL_STALE:
		free_address_proposal(addr_proposal);
		addr_proposal = NULL;
		break;
	case PROPOSAL_WITHDRAWN:
		free_address_proposal(addr_proposal);
		addr_proposal = NULL;
		break;
	default:
		log_debug("%s: unhandled state: %s", __func__,
		    proposal_state_name(addr_proposal->state));
	}
}

void
dfr_proposal_timeout(int fd, short events, void *arg)
{
	struct dfr_proposal	*dfr_proposal;
	const char		*hbuf;

	dfr_proposal = (struct dfr_proposal *)arg;

	hbuf = sin6_to_str(&dfr_proposal->addr);
	log_debug("%s: iface %d: %s [%s]", __func__, dfr_proposal->if_index,
	    hbuf, proposal_state_name(dfr_proposal->state));

	switch (dfr_proposal->state) {
	case PROPOSAL_IF_DOWN:
		dfr_proposal_state_transition(dfr_proposal, PROPOSAL_STALE);
		break;
	case PROPOSAL_CONFIGURED:
		dfr_proposal_state_transition(dfr_proposal,
		    PROPOSAL_NEARLY_EXPIRED);
		break;
	case PROPOSAL_NEARLY_EXPIRED:
		if (real_lifetime(&dfr_proposal->uptime,
		    dfr_proposal->router_lifetime) > 0)
			dfr_proposal_state_transition(dfr_proposal,
			    PROPOSAL_NEARLY_EXPIRED);
		else
			dfr_proposal_state_transition(dfr_proposal,
			    PROPOSAL_STALE);
		break;
	case PROPOSAL_STALE:
		free_dfr_proposal(dfr_proposal);
		dfr_proposal = NULL;
		break;
	case PROPOSAL_WITHDRAWN:
		free_dfr_proposal(dfr_proposal);
		dfr_proposal = NULL;
		break;

	default:
		log_debug("%s: unhandled state: %s", __func__,
		    proposal_state_name(dfr_proposal->state));
	}
}

void
rdns_proposal_timeout(int fd, short events, void *arg)
{
	struct rdns_proposal	*rdns_proposal;
	const char		*hbuf;

	rdns_proposal = (struct rdns_proposal *)arg;

	hbuf = sin6_to_str(&rdns_proposal->from);
	log_debug("%s: iface %d: %s [%s]", __func__, rdns_proposal->if_index,
	    hbuf, proposal_state_name(rdns_proposal->state));

	switch (rdns_proposal->state) {
	case PROPOSAL_IF_DOWN:
		rdns_proposal_state_transition(rdns_proposal, PROPOSAL_STALE);
		break;
	case PROPOSAL_CONFIGURED:
		rdns_proposal_state_transition(rdns_proposal,
		    PROPOSAL_NEARLY_EXPIRED);
		break;
	case PROPOSAL_NEARLY_EXPIRED:
		if (real_lifetime(&rdns_proposal->uptime,
		    rdns_proposal->rdns_lifetime) > 0)
			rdns_proposal_state_transition(rdns_proposal,
			    PROPOSAL_NEARLY_EXPIRED);
		else
			rdns_proposal_state_transition(rdns_proposal,
			    PROPOSAL_STALE);
		break;
	case PROPOSAL_STALE:
		free_rdns_proposal(rdns_proposal);
		rdns_proposal = NULL;
		break;
	case PROPOSAL_WITHDRAWN:
		free_rdns_proposal(rdns_proposal);
		rdns_proposal = NULL;
		break;

	default:
		log_debug("%s: unhandled state: %s", __func__,
		    proposal_state_name(rdns_proposal->state));
	}
}

void
iface_timeout(int fd, short events, void *arg)
{
	struct slaacd_iface	*iface = (struct slaacd_iface *)arg;

	log_debug("%s[%d]: %s", __func__, iface->if_index,
	    if_state_name(iface->state));

	switch (iface->state) {
	case IF_DOWN:
		fatalx("%s: timeout in wrong state IF_DOWN", __func__);
		break;
	case IF_INIT:
		iface_state_transition(iface, IF_INIT);
		break;
	default:
		break;
	}
}

struct radv*
find_ra(struct slaacd_iface *iface, struct sockaddr_in6 *from)
{
	struct radv	*ra;

	LIST_FOREACH (ra, &iface->radvs, entries) {
		if (memcmp(&ra->from.sin6_addr, &from->sin6_addr,
		    sizeof(from->sin6_addr)) == 0)
			return (ra);
	}

	return (NULL);
}

struct address_proposal*
find_address_proposal_by_addr(struct slaacd_iface *iface, struct sockaddr_in6
    *addr)
{
	struct address_proposal	*addr_proposal;

	LIST_FOREACH (addr_proposal, &iface->addr_proposals, entries) {
		if (memcmp(&addr_proposal->addr, addr, sizeof(*addr)) == 0)
			return (addr_proposal);
	}

	return (NULL);
}

struct dfr_proposal*
find_dfr_proposal_by_gw(struct slaacd_iface *iface, struct sockaddr_in6
    *addr)
{
	struct dfr_proposal	*dfr_proposal;

	LIST_FOREACH (dfr_proposal, &iface->dfr_proposals, entries) {
		if (memcmp(&dfr_proposal->addr, addr, sizeof(*addr)) == 0)
			return (dfr_proposal);
	}

	return (NULL);
}

struct rdns_proposal*
find_rdns_proposal_by_gw(struct slaacd_iface *iface, struct sockaddr_in6
    *from)
{
	struct rdns_proposal	*rdns_proposal;

	LIST_FOREACH (rdns_proposal, &iface->rdns_proposals, entries) {
		if (memcmp(&rdns_proposal->from, from, sizeof(*from)) == 0)
			return (rdns_proposal);
	}

	return (NULL);
}

struct radv_prefix *
find_prefix(struct radv *ra, struct in6_addr *prefix, uint8_t prefix_len)
{
	struct radv_prefix	*result;


	LIST_FOREACH(result, &ra->prefixes, entries) {
		if (memcmp(&result->prefix, prefix,
		    sizeof(result->prefix)) == 0 && result->prefix_len ==
		    prefix_len)
			return (result);
	}
	return (NULL);
}

uint32_t
real_lifetime(struct timespec *received_uptime, uint32_t ltime)
{
	struct timespec	 now, diff;
	int64_t		 remaining;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		fatal("clock_gettime");

	timespecsub(&now, received_uptime, &diff);

	remaining = ((int64_t)ltime) - diff.tv_sec;

	if (remaining < 0)
		remaining = 0;

	return (remaining);
}

void
merge_dad_couters(struct radv *old_ra, struct radv *new_ra)
{

	struct radv_prefix	*old_prefix, *new_prefix;

	LIST_FOREACH(old_prefix, &old_ra->prefixes, entries) {
		if (!old_prefix->dad_counter)
			continue;
		if ((new_prefix = find_prefix(new_ra, &old_prefix->prefix,
		    old_prefix->prefix_len)) != NULL)
			new_prefix->dad_counter = old_prefix->dad_counter;
	}
}
