/*	$OpenBSD: relayctl.c,v 1.63 2024/11/21 13:38:15 claudio Exp $	*/

/*
 * Copyright (c) 2007 - 2013 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
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
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/un.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <imsg.h>

#include "relayd.h"
#include "parser.h"

__dead void	 usage(void);
int		 show_summary_msg(struct imsg *, int);
int		 show_session_msg(struct imsg *);
int		 show_command_output(struct imsg *);
char		*print_rdr_status(int);
char		*print_host_status(int, int);
char		*print_table_status(int, int);
char		*print_relay_status(int);
void		 print_statistics(struct ctl_stats[PROC_MAX_INSTANCES + 1]);

struct imsgname {
	int type;
	char *name;
	void (*func)(struct imsg *);
};

struct imsgname *monitor_lookup(u_int8_t);
void		 monitor_host_status(struct imsg *);
void		 monitor_id(struct imsg *);
int		 monitor(struct imsg *);

struct imsgname imsgs[] = {
	{ IMSG_HOST_STATUS,		"host_status",	monitor_host_status },
	{ IMSG_CTL_RDR_DISABLE,		"ctl_rdr_disable",	monitor_id },
	{ IMSG_CTL_RDR_ENABLE,		"ctl_rdr_enable",	monitor_id },
	{ IMSG_CTL_TABLE_DISABLE,	"ctl_table_disable",	monitor_id },
	{ IMSG_CTL_TABLE_ENABLE,	"ctl_table_enable",	monitor_id },
	{ IMSG_CTL_HOST_DISABLE,	"ctl_host_disable",	monitor_id },
	{ IMSG_CTL_HOST_ENABLE,		"ctl_host_enable",	monitor_id },
	{ IMSG_CTL_TABLE_CHANGED,	"ctl_table_changed",	monitor_id },
	{ IMSG_CTL_PULL_RULESET,	"ctl_pull_ruleset",	monitor_id },
	{ IMSG_CTL_PUSH_RULESET,	"ctl_push_ruleset",	monitor_id },
	{ IMSG_SYNC,			"sync",			NULL },
	{ 0,				NULL,			NULL }
};
struct imsgname imsgunknown = {
	-1,				"<unknown>",		NULL
};

struct imsgbuf	*ibuf;
int error = 0;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-s socket] command [argument ...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un	 sun;
	struct parse_result	*res;
	struct imsg		 imsg;
	int			 ctl_sock;
	int			 done = 0;
	int			 n, verbose = 0;
	int			 ch;
	const char		*sockname;

	sockname = RELAYD_SOCKET;
	while ((ch = getopt(argc, argv, "s:")) != -1) {
		switch (ch) {
		case 's':
			sockname = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/* parse options */
	if ((res = parse(argc, argv)) == NULL)
		exit(1);

	/* connect to relayd control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, sockname, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path))
		errx(1, "socket `%s' too long", sockname);
 reconnect:
	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		/* Keep retrying if running in monitor mode */
		if (res->action == MONITOR &&
		    (errno == ENOENT || errno == ECONNREFUSED)) {
			usleep(100);
			goto reconnect;
		}
		err(1, "connect: %s", sockname);
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
		err(1, NULL);
	if (imsgbuf_init(ibuf, ctl_sock) == -1)
		err(1, NULL);
	done = 0;

	/* process user request */
	switch (res->action) {
	case NONE:
		usage();
		/* not reached */
	case SHOW_SUM:
	case SHOW_HOSTS:
	case SHOW_RDRS:
	case SHOW_RELAYS:
	case SHOW_ROUTERS:
		imsg_compose(ibuf, IMSG_CTL_SHOW_SUM, 0, 0, -1, NULL, 0);
		printf("%-4s\t%-8s\t%-24s\t%-7s\tStatus\n",
		    "Id", "Type", "Name", "Avlblty");
		break;
	case SHOW_SESSIONS:
		imsg_compose(ibuf, IMSG_CTL_SESSION, 0, 0, -1, NULL, 0);
		break;
	case RDR_ENABLE:
		imsg_compose(ibuf, IMSG_CTL_RDR_ENABLE, 0, 0, -1,
		    &res->id, sizeof(res->id));
		break;
	case RDR_DISABLE:
		imsg_compose(ibuf, IMSG_CTL_RDR_DISABLE, 0, 0, -1,
		    &res->id, sizeof(res->id));
		break;
	case TABLE_ENABLE:
		imsg_compose(ibuf, IMSG_CTL_TABLE_ENABLE, 0, 0, -1,
		    &res->id, sizeof(res->id));
		break;
	case TABLE_DISABLE:
		imsg_compose(ibuf, IMSG_CTL_TABLE_DISABLE, 0, 0, -1,
		    &res->id, sizeof(res->id));
		break;
	case HOST_ENABLE:
		imsg_compose(ibuf, IMSG_CTL_HOST_ENABLE, 0, 0, -1,
		    &res->id, sizeof(res->id));
		break;
	case HOST_DISABLE:
		imsg_compose(ibuf, IMSG_CTL_HOST_DISABLE, 0, 0, -1,
		    &res->id, sizeof(res->id));
		break;
	case SHUTDOWN:
		imsg_compose(ibuf, IMSG_CTL_SHUTDOWN, 0, 0, -1, NULL, 0);
		break;
	case POLL:
		imsg_compose(ibuf, IMSG_CTL_POLL, 0, 0, -1, NULL, 0);
		break;
	case LOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1,
		    res->path, strlen(res->path));
		done = 1;
		break;
	case RELOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		done = 1;
		break;
	case MONITOR:
		imsg_compose(ibuf, IMSG_CTL_NOTIFY, 0, 0, -1, NULL, 0);
		break;
	case LOG_VERBOSE:
		verbose = 2;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(ibuf, IMSG_CTL_VERBOSE, 0, 0, -1,
		    &verbose, sizeof(verbose));
		printf("logging request sent.\n");
		done = 1;
		break;
	}

	if (imsgbuf_flush(ibuf) == -1)
		err(1, "write error");

	while (!done) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				errx(1, "imsg_get error");
			if (n == 0)
				break;
			switch (res->action) {
			case SHOW_SUM:
			case SHOW_HOSTS:
			case SHOW_RDRS:
			case SHOW_RELAYS:
			case SHOW_ROUTERS:
				done = show_summary_msg(&imsg, res->action);
				break;
			case SHOW_SESSIONS:
				done = show_session_msg(&imsg);
				break;
			case RDR_DISABLE:
			case RDR_ENABLE:
			case TABLE_DISABLE:
			case TABLE_ENABLE:
			case HOST_DISABLE:
			case HOST_ENABLE:
			case POLL:
			case SHUTDOWN:
				done = show_command_output(&imsg);
				break;
			case NONE:
			case LOG_VERBOSE:
			case LOG_BRIEF:
			case RELOAD:
			case LOAD:
				break;
			case MONITOR:
				done = monitor(&imsg);
				break;
			}
			imsg_free(&imsg);
		}
	}
	close(ctl_sock);
	free(ibuf);

	return (error ? 1 : 0);
}

struct imsgname *
monitor_lookup(u_int8_t type)
{
	int i;

	for (i = 0; imsgs[i].name != NULL; i++)
		if (imsgs[i].type == type)
			return (&imsgs[i]);
	return (&imsgunknown);
}

void
monitor_host_status(struct imsg *imsg)
{
	struct ctl_status	 cs;

	memcpy(&cs, imsg->data, sizeof(cs));
	printf("\tid: %u\n", cs.id);
	printf("\tstate: ");
	switch (cs.up) {
	case HOST_UP:
		printf("up\n");
		break;
	case HOST_DOWN:
		printf("down\n");
		break;
	default:
		printf("unknown\n");
		break;
	}
}

void
monitor_id(struct imsg *imsg)
{
	struct ctl_id		 id;

	memcpy(&id, imsg->data, sizeof(id));
	printf("\tid: %u\n", id.id);
	if (strlen(id.name))
		printf("\tname: %s\n", id.name);
}

int
monitor(struct imsg *imsg)
{
	time_t			 now;
	int			 done = 0;
	struct imsgname		*imn;

	now = time(NULL);

	imn = monitor_lookup(imsg->hdr.type);
	printf("%s: imsg type %u len %u peerid %u pid %d\n", imn->name,
	    imsg->hdr.type, imsg->hdr.len, imsg->hdr.peerid, imsg->hdr.pid);
	printf("\ttimestamp: %lld, %s", (long long)now, ctime(&now));
	if (imn->type == -1)
		done = 1;
	if (imn->func != NULL)
		(*imn->func)(imsg);

	return (done);
}

int
show_summary_msg(struct imsg *imsg, int type)
{
	struct rdr		*rdr;
	struct table		*table;
	struct host		*host;
	struct relay		*rlay;
	struct router		*rt;
	struct netroute		*nr;
	struct ctl_stats	 stats[PROC_MAX_INSTANCES];
	char			 name[HOST_NAME_MAX+1];

	switch (imsg->hdr.type) {
	case IMSG_CTL_RDR:
		if (!(type == SHOW_SUM || type == SHOW_RDRS))
			break;
		rdr = imsg->data;
		printf("%-4u\t%-8s\t%-24s\t%-7s\t%s\n",
		    rdr->conf.id, "redirect", rdr->conf.name, "",
		    print_rdr_status(rdr->conf.flags));
		break;
	case IMSG_CTL_TABLE:
		if (!(type == SHOW_SUM || type == SHOW_HOSTS))
			break;
		table = imsg->data;
		printf("%-4u\t%-8s\t%-24s\t%-7s\t%s\n",
		    table->conf.id, "table", table->conf.name, "",
		    print_table_status(table->up, table->conf.flags));
		break;
	case IMSG_CTL_HOST:
		if (!(type == SHOW_SUM || type == SHOW_HOSTS))
			break;
		host = imsg->data;
		if (host->conf.parentid)
			snprintf(name, sizeof(name), "%s parent %u",
			    host->conf.name, host->conf.parentid);
		else
			strlcpy(name, host->conf.name, sizeof(name));
		printf("%-4u\t%-8s\t%-24s\t%-7s\t%s\n",
		    host->conf.id, "host", name,
		    print_availability(host->check_cnt, host->up_cnt),
		    print_host_status(host->up, host->flags));
		if (type == SHOW_HOSTS && host->check_cnt) {
			printf("\t%8s\ttotal: %lu/%lu checks",
			    "", host->up_cnt, host->check_cnt);
			if (host->retry_cnt)
				printf(", %d retries", host->retry_cnt);
			if (host->he && host->up == HOST_DOWN)
				printf(", error: %s", host_error(host->he));
			printf("\n");
		}
		break;
	case IMSG_CTL_RELAY:
		if (!(type == SHOW_SUM || type == SHOW_RELAYS))
			break;
		rlay = imsg->data;
		printf("%-4u\t%-8s\t%-24s\t%-7s\t%s\n",
		    rlay->rl_conf.id, "relay", rlay->rl_conf.name, "",
		    print_relay_status(rlay->rl_conf.flags));
		break;
	case IMSG_CTL_RDR_STATS:
		if (type != SHOW_RDRS)
			break;
		bcopy(imsg->data, &stats[0], sizeof(stats[0]));
		stats[1].id = EMPTY_ID;
		print_statistics(stats);
		break;
	case IMSG_CTL_RELAY_STATS:
		if (type != SHOW_RELAYS)
			break;
		bcopy(imsg->data, &stats, sizeof(stats));
		print_statistics(stats);
		break;
	case IMSG_CTL_ROUTER:
		if (!(type == SHOW_SUM || type == SHOW_ROUTERS))
			break;
		rt = imsg->data;
		printf("%-4u\t%-8s\t%-24s\t%-7s\t%s\n",
		    rt->rt_conf.id, "router", rt->rt_conf.name, "",
		    print_relay_status(rt->rt_conf.flags));
		if (type != SHOW_ROUTERS)
			break;
		if (rt->rt_conf.rtable)
			printf("\t%8s\trtable: %d\n", "", rt->rt_conf.rtable);
		if (strlen(rt->rt_conf.label))
			printf("\t%8s\trtlabel: %s\n", "", rt->rt_conf.label);
		break;
	case IMSG_CTL_NETROUTE:
		if (type != SHOW_ROUTERS)
			break;
		nr = imsg->data;
		(void)print_host(&nr->nr_conf.ss, name, sizeof(name));
		printf("\t%8s\troute: %s/%d\n",
		    "", name, nr->nr_conf.prefixlen);
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		errx(1, "wrong message in summary: %u", imsg->hdr.type);
		break;
	}
	return (0);
}

int
show_session_msg(struct imsg *imsg)
{
	struct rsession		*con;
	char			 a[128], b[128];
	struct timeval		 tv_now;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SESSION:
		con = imsg->data;

		(void)print_host(&con->se_in.ss, a, sizeof(a));
		(void)print_host(&con->se_out.ss, b, sizeof(b));
		printf("session %u:%u %s:%u -> %s:%u\t%s\n",
		    imsg->hdr.peerid, con->se_id,
		    a, ntohs(con->se_in.port), b, ntohs(con->se_out.port),
		    con->se_done ? "DONE" : "RUNNING");

		getmonotime(&tv_now);
		print_time(&tv_now, &con->se_tv_start, a, sizeof(a));
		print_time(&tv_now, &con->se_tv_last, b, sizeof(b));
		printf("\tage %s, idle %s, relay %u, pid %u",
		    a, b, con->se_relayid, con->se_pid);
		/* XXX grab tagname instead of tag id */
		if (con->se_tag)
			printf(", tag (id) %u", con->se_tag);
		printf("\n");
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		errx(1, "wrong message in session: %u", imsg->hdr.type);
		break;
	}
	return (0);
}

int
show_command_output(struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CTL_OK:
		printf("command succeeded\n");
		break;
	case IMSG_CTL_FAIL:
		printf("command failed\n");
		error++;
		break;
	default:
		errx(1, "wrong message in summary: %u", imsg->hdr.type);
	}
	return (1);
}

char *
print_rdr_status(int flags)
{
	if (flags & F_DISABLE) {
		return ("disabled");
	} else if (flags & F_DOWN) {
		return ("down");
	} else if (flags & F_BACKUP) {
		return ("active (using backup table)");
	} else
		return ("active");
}

char *
print_table_status(int up, int fl)
{
	static char buf[1024];

	bzero(buf, sizeof(buf));

	if (fl & F_DISABLE) {
		snprintf(buf, sizeof(buf) - 1, "disabled");
	} else if (!up) {
		snprintf(buf, sizeof(buf) - 1, "empty");
	} else
		snprintf(buf, sizeof(buf) - 1, "active (%d hosts)", up);
	return (buf);
}

char *
print_host_status(int status, int fl)
{
	if (fl & F_DISABLE)
		return ("disabled");

	switch (status) {
	case HOST_DOWN:
		return ("down");
	case HOST_UNKNOWN:
		return ("unknown");
	case HOST_UP:
		return ("up");
	default:
		errx(1, "invalid status: %d", status);
	}
}

char *
print_relay_status(int flags)
{
	if (flags & F_DISABLE) {
		return ("disabled");
	} else
		return ("active");
}

void
print_statistics(struct ctl_stats stats[PROC_MAX_INSTANCES + 1])
{
	struct ctl_stats	 crs;
	int			 i;

	bzero(&crs, sizeof(crs));
	crs.interval = stats[0].interval;
	for (i = 0; stats[i].id != EMPTY_ID; i++) {
		crs.cnt += stats[i].cnt;
		crs.last += stats[i].last;
		crs.avg += stats[i].avg;
		crs.last_hour += stats[i].last_hour;
		crs.avg_hour += stats[i].avg_hour;
		crs.last_day += stats[i].last_day;
		crs.avg_day += stats[i].avg_day;
	}
	if (crs.cnt == 0)
		return;
	printf("\t%8s\ttotal: %llu sessions\n"
	    "\t%8s\tlast: %u/%llus %u/h %u/d sessions\n"
	    "\t%8s\taverage: %u/%llus %u/h %u/d sessions\n",
	    "", crs.cnt,
	    "", crs.last, crs.interval,
	    crs.last_hour, crs.last_day,
	    "", crs.avg, crs.interval,
	    crs.avg_hour, crs.avg_day);
}
