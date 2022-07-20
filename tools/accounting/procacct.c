// SPDX-License-Identifier: GPL-2.0
/* procacct.c
 *
 * Demonstrator of fetching resource data on task exit, as a way
 * to accumulate accurate program resource usage statistics, without
 * prior identification of the programs. For that, the fields for
 * device and inode of the program executable binary file are also
 * extracted in addition to the command string.
 *
 * The TGID together with the PID and the AGROUP flag allow
 * identification of threads in a process and single-threaded processes.
 * The ac_tgetime field gives proper whole-process walltime.
 *
 * Written (changed) by Thomas Orgis, University of Hamburg in 2022
 *
 * This is a cheap derivation (inheriting the style) of getdelays.c:
 *
 * Utility to get per-pid and per-tgid delay accounting statistics
 * Also illustrates usage of the taskstats interface
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2005
 * Copyright (C) Balbir Singh, IBM Corp. 2006
 * Copyright (c) Jay Lan, SGI. 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>

#include <linux/genetlink.h>
#include <linux/acct.h>
#include <linux/taskstats.h>
#include <linux/kdev_t.h>

/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */
#define GENLMSG_DATA(glh)	((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)	(NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)		((void *)((char *)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)	(len - NLA_HDRLEN)

#define err(code, fmt, arg...)			\
	do {					\
		fprintf(stderr, fmt, ##arg);	\
		exit(code);			\
	} while (0)

int rcvbufsz;
char name[100];
int dbg;
int print_delays;
int print_io_accounting;
int print_task_context_switch_counts;

#define PRINTF(fmt, arg...) {			\
		if (dbg) {			\
			printf(fmt, ##arg);	\
		}				\
	}

/* Maximum size of response requested or message sent */
#define MAX_MSG_SIZE	1024
/* Maximum number of cpus expected to be specified in a cpumask */
#define MAX_CPUS	32

struct msgtemplate {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[MAX_MSG_SIZE];
};

char cpumask[100+6*MAX_CPUS];

static void usage(void)
{
	fprintf(stderr, "procacct [-v] [-w logfile] [-r bufsize] [-m cpumask]\n");
	fprintf(stderr, "  -v: debug on\n");
}

/*
 * Create a raw netlink socket and bind
 */
static int create_nl_socket(int protocol)
{
	int fd;
	struct sockaddr_nl local;

	fd = socket(AF_NETLINK, SOCK_RAW, protocol);
	if (fd < 0)
		return -1;

	if (rcvbufsz)
		if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
				&rcvbufsz, sizeof(rcvbufsz)) < 0) {
			fprintf(stderr, "Unable to set socket rcv buf size to %d\n",
				rcvbufsz);
			goto error;
		}

	memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;

	if (bind(fd, (struct sockaddr *) &local, sizeof(local)) < 0)
		goto error;

	return fd;
error:
	close(fd);
	return -1;
}


static int send_cmd(int sd, __u16 nlmsg_type, __u32 nlmsg_pid,
	     __u8 genl_cmd, __u16 nla_type,
	     void *nla_data, int nla_len)
{
	struct nlattr *na;
	struct sockaddr_nl nladdr;
	int r, buflen;
	char *buf;

	struct msgtemplate msg;

	msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	msg.n.nlmsg_type = nlmsg_type;
	msg.n.nlmsg_flags = NLM_F_REQUEST;
	msg.n.nlmsg_seq = 0;
	msg.n.nlmsg_pid = nlmsg_pid;
	msg.g.cmd = genl_cmd;
	msg.g.version = 0x1;
	na = (struct nlattr *) GENLMSG_DATA(&msg);
	na->nla_type = nla_type;
	na->nla_len = nla_len + 1 + NLA_HDRLEN;
	memcpy(NLA_DATA(na), nla_data, nla_len);
	msg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	buf = (char *) &msg;
	buflen = msg.n.nlmsg_len;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	while ((r = sendto(sd, buf, buflen, 0, (struct sockaddr *) &nladdr,
			   sizeof(nladdr))) < buflen) {
		if (r > 0) {
			buf += r;
			buflen -= r;
		} else if (errno != EAGAIN)
			return -1;
	}
	return 0;
}


/*
 * Probe the controller in genetlink to find the family id
 * for the TASKSTATS family
 */
static int get_family_id(int sd)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[256];
	} ans;

	int id = 0, rc;
	struct nlattr *na;
	int rep_len;

	strcpy(name, TASKSTATS_GENL_NAME);
	rc = send_cmd(sd, GENL_ID_CTRL, getpid(), CTRL_CMD_GETFAMILY,
			CTRL_ATTR_FAMILY_NAME, (void *)name,
			strlen(TASKSTATS_GENL_NAME)+1);
	if (rc < 0)
		return 0;	/* sendto() failure? */

	rep_len = recv(sd, &ans, sizeof(ans), 0);
	if (ans.n.nlmsg_type == NLMSG_ERROR ||
	    (rep_len < 0) || !NLMSG_OK((&ans.n), rep_len))
		return 0;

	na = (struct nlattr *) GENLMSG_DATA(&ans);
	na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
	if (na->nla_type == CTRL_ATTR_FAMILY_ID)
		id = *(__u16 *) NLA_DATA(na);

	return id;
}

#define average_ms(t, c) (t / 1000000ULL / (c ? c : 1))

static void print_procacct(struct taskstats *t)
{
	/* First letter: T is a mere thread, G the last in a group, U  unknown. */
	printf(
		"%c pid=%lu tgid=%lu uid=%lu wall=%llu gwall=%llu cpu=%llu vmpeak=%llu rsspeak=%llu dev=%lu:%lu inode=%llu comm=%s\n"
	,	t->version >= 12 ? (t->ac_flag & AGROUP ? 'P' : 'T') : '?'
	,	(unsigned long)t->ac_pid
	,	(unsigned long)(t->version >= 12 ? t->ac_tgid : 0)
	,	(unsigned long)t->ac_uid
	,	(unsigned long long)t->ac_etime
	,	(unsigned long long)(t->version >= 12 ? t->ac_tgetime : 0)
	,	(unsigned long long)(t->ac_utime+t->ac_stime)
	,	(unsigned long long)t->hiwater_vm
	,	(unsigned long long)t->hiwater_rss
	,	(unsigned long)(t->version >= 12 ? MAJOR(t->ac_exe_dev) : 0)
	,	(unsigned long)(t->version >= 12 ? MINOR(t->ac_exe_dev) : 0)
	,	(unsigned long long)(t->version >= 12 ? t->ac_exe_inode : 0)
	,	t->ac_comm
	);
}

void handle_aggr(int mother, struct nlattr *na, int fd)
{
	int aggr_len = NLA_PAYLOAD(na->nla_len);
	int len2 = 0;
	pid_t rtid = 0;

	na = (struct nlattr *) NLA_DATA(na);
	while (len2 < aggr_len) {
		switch (na->nla_type) {
		case TASKSTATS_TYPE_PID:
			rtid = *(int *) NLA_DATA(na);
			PRINTF("PID\t%d\n", rtid);
			break;
		case TASKSTATS_TYPE_TGID:
			rtid = *(int *) NLA_DATA(na);
			PRINTF("TGID\t%d\n", rtid);
			break;
		case TASKSTATS_TYPE_STATS:
			if (mother == TASKSTATS_TYPE_AGGR_PID)
				print_procacct((struct taskstats *) NLA_DATA(na));
			if (fd) {
				if (write(fd, NLA_DATA(na), na->nla_len) < 0)
					err(1, "write error\n");
			}
			break;
		case TASKSTATS_TYPE_NULL:
			break;
		default:
			fprintf(stderr, "Unknown nested nla_type %d\n",
				na->nla_type);
			break;
		}
		len2 += NLA_ALIGN(na->nla_len);
		na = (struct nlattr *)((char *)na +
						 NLA_ALIGN(na->nla_len));
	}
}

int main(int argc, char *argv[])
{
	int c, rc, rep_len, aggr_len, len2;
	int cmd_type = TASKSTATS_CMD_ATTR_UNSPEC;
	__u16 id;
	__u32 mypid;

	struct nlattr *na;
	int nl_sd = -1;
	int len = 0;
	pid_t tid = 0;

	int fd = 0;
	int write_file = 0;
	int maskset = 0;
	char *logfile = NULL;
	int containerset = 0;
	char *containerpath = NULL;
	int cfd = 0;
	int forking = 0;
	sigset_t sigset;

	struct msgtemplate msg;

	while (!forking) {
		c = getopt(argc, argv, "m:vr:");
		if (c < 0)
			break;

		switch (c) {
		case 'w':
			logfile = strdup(optarg);
			printf("write to file %s\n", logfile);
			write_file = 1;
			break;
		case 'r':
			rcvbufsz = atoi(optarg);
			printf("receive buf size %d\n", rcvbufsz);
			if (rcvbufsz < 0)
				err(1, "Invalid rcv buf size\n");
			break;
		case 'm':
			strncpy(cpumask, optarg, sizeof(cpumask));
			cpumask[sizeof(cpumask) - 1] = '\0';
			maskset = 1;
			break;
		case 'v':
			printf("debug on\n");
			dbg = 1;
			break;
		default:
			usage();
			exit(-1);
		}
	}
	if (!maskset) {
		maskset = 1;
		strncpy(cpumask, "1", sizeof(cpumask));
		cpumask[sizeof(cpumask) - 1] = '\0';
	}
	printf("cpumask %s maskset %d\n", cpumask, maskset);

	if (write_file) {
		fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			perror("Cannot open output file\n");
			exit(1);
		}
	}

	nl_sd = create_nl_socket(NETLINK_GENERIC);
	if (nl_sd < 0)
		err(1, "error creating Netlink socket\n");

	mypid = getpid();
	id = get_family_id(nl_sd);
	if (!id) {
		fprintf(stderr, "Error getting family id, errno %d\n", errno);
		goto err;
	}
	PRINTF("family id %d\n", id);

	if (maskset) {
		rc = send_cmd(nl_sd, id, mypid, TASKSTATS_CMD_GET,
			      TASKSTATS_CMD_ATTR_REGISTER_CPUMASK,
			      &cpumask, strlen(cpumask) + 1);
		PRINTF("Sent register cpumask, retval %d\n", rc);
		if (rc < 0) {
			fprintf(stderr, "error sending register cpumask\n");
			goto err;
		}
	}

	do {
		rep_len = recv(nl_sd, &msg, sizeof(msg), 0);
		PRINTF("received %d bytes\n", rep_len);

		if (rep_len < 0) {
			fprintf(stderr, "nonfatal reply error: errno %d\n",
				errno);
			continue;
		}
		if (msg.n.nlmsg_type == NLMSG_ERROR ||
		    !NLMSG_OK((&msg.n), rep_len)) {
			struct nlmsgerr *err = NLMSG_DATA(&msg);

			fprintf(stderr, "fatal reply error,  errno %d\n",
				err->error);
			goto done;
		}

		PRINTF("nlmsghdr size=%zu, nlmsg_len=%d, rep_len=%d\n",
		       sizeof(struct nlmsghdr), msg.n.nlmsg_len, rep_len);


		rep_len = GENLMSG_PAYLOAD(&msg.n);

		na = (struct nlattr *) GENLMSG_DATA(&msg);
		len = 0;
		while (len < rep_len) {
			len += NLA_ALIGN(na->nla_len);
			int mother = na->nla_type;

			PRINTF("mother=%i\n", mother);
			switch (na->nla_type) {
			case TASKSTATS_TYPE_AGGR_PID:
			case TASKSTATS_TYPE_AGGR_TGID:
				/* For nested attributes, na follows */
				handle_aggr(mother, na, fd);
				break;
			default:
				fprintf(stderr, "Unexpected nla_type %d\n",
					na->nla_type);
			case TASKSTATS_TYPE_NULL:
				break;
			}
			na = (struct nlattr *) (GENLMSG_DATA(&msg) + len);
		}
	} while (1);
done:
	if (maskset) {
		rc = send_cmd(nl_sd, id, mypid, TASKSTATS_CMD_GET,
			      TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK,
			      &cpumask, strlen(cpumask) + 1);
		printf("Sent deregister mask, retval %d\n", rc);
		if (rc < 0)
			err(rc, "error sending deregister cpumask\n");
	}
err:
	close(nl_sd);
	if (fd)
		close(fd);
	if (cfd)
		close(cfd);
	return 0;
}
