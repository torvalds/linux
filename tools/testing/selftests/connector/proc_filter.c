// SPDX-License-Identifier: GPL-2.0-only

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "../kselftest.h"

#define NL_MESSAGE_SIZE (sizeof(struct nlmsghdr) + sizeof(struct cn_msg) + \
			 sizeof(struct proc_input))
#define NL_MESSAGE_SIZE_NF (sizeof(struct nlmsghdr) + sizeof(struct cn_msg) + \
			 sizeof(int))

#define MAX_EVENTS 1

volatile static int interrupted;
static int nl_sock, ret_errno, tcount;
static struct epoll_event evn;

static int filter;

#ifdef ENABLE_PRINTS
#define Printf printf
#else
#define Printf ksft_print_msg
#endif

int send_message(void *pinp)
{
	char buff[NL_MESSAGE_SIZE];
	struct nlmsghdr *hdr;
	struct cn_msg *msg;

	hdr = (struct nlmsghdr *)buff;
	if (filter)
		hdr->nlmsg_len = NL_MESSAGE_SIZE;
	else
		hdr->nlmsg_len = NL_MESSAGE_SIZE_NF;
	hdr->nlmsg_type = NLMSG_DONE;
	hdr->nlmsg_flags = 0;
	hdr->nlmsg_seq = 0;
	hdr->nlmsg_pid = getpid();

	msg = (struct cn_msg *)NLMSG_DATA(hdr);
	msg->id.idx = CN_IDX_PROC;
	msg->id.val = CN_VAL_PROC;
	msg->seq = 0;
	msg->ack = 0;
	msg->flags = 0;

	if (filter) {
		msg->len = sizeof(struct proc_input);
		((struct proc_input *)msg->data)->mcast_op =
			((struct proc_input *)pinp)->mcast_op;
		((struct proc_input *)msg->data)->event_type =
			((struct proc_input *)pinp)->event_type;
	} else {
		msg->len = sizeof(int);
		*(int *)msg->data = *(enum proc_cn_mcast_op *)pinp;
	}

	if (send(nl_sock, hdr, hdr->nlmsg_len, 0) == -1) {
		ret_errno = errno;
		perror("send failed");
		return -3;
	}
	return 0;
}

int register_proc_netlink(int *efd, void *input)
{
	struct sockaddr_nl sa_nl;
	int err = 0, epoll_fd;

	nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);

	if (nl_sock == -1) {
		ret_errno = errno;
		perror("socket failed");
		return -1;
	}

	bzero(&sa_nl, sizeof(sa_nl));
	sa_nl.nl_family = AF_NETLINK;
	sa_nl.nl_groups = CN_IDX_PROC;
	sa_nl.nl_pid    = getpid();

	if (bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl)) == -1) {
		ret_errno = errno;
		perror("bind failed");
		return -2;
	}

	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		ret_errno = errno;
		perror("epoll_create1 failed");
		return -2;
	}

	err = send_message(input);

	if (err < 0)
		return err;

	evn.events = EPOLLIN;
	evn.data.fd = nl_sock;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nl_sock, &evn) < 0) {
		ret_errno = errno;
		perror("epoll_ctl failed");
		return -3;
	}
	*efd = epoll_fd;
	return 0;
}

static void sigint(int sig)
{
	interrupted = 1;
}

int handle_packet(char *buff, int fd, struct proc_event *event)
{
	struct nlmsghdr *hdr;

	hdr = (struct nlmsghdr *)buff;

	if (hdr->nlmsg_type == NLMSG_ERROR) {
		perror("NLMSG_ERROR error\n");
		return -3;
	} else if (hdr->nlmsg_type == NLMSG_DONE) {
		event = (struct proc_event *)
			((struct cn_msg *)NLMSG_DATA(hdr))->data;
		tcount++;
		switch (event->what) {
		case PROC_EVENT_EXIT:
			Printf("Exit process %d (tgid %d) with code %d, signal %d\n",
			       event->event_data.exit.process_pid,
			       event->event_data.exit.process_tgid,
			       event->event_data.exit.exit_code,
			       event->event_data.exit.exit_signal);
			break;
		case PROC_EVENT_FORK:
			Printf("Fork process %d (tgid %d), parent %d (tgid %d)\n",
			       event->event_data.fork.child_pid,
			       event->event_data.fork.child_tgid,
			       event->event_data.fork.parent_pid,
			       event->event_data.fork.parent_tgid);
			break;
		case PROC_EVENT_EXEC:
			Printf("Exec process %d (tgid %d)\n",
			       event->event_data.exec.process_pid,
			       event->event_data.exec.process_tgid);
			break;
		case PROC_EVENT_UID:
			Printf("UID process %d (tgid %d) uid %d euid %d\n",
			       event->event_data.id.process_pid,
			       event->event_data.id.process_tgid,
			       event->event_data.id.r.ruid,
			       event->event_data.id.e.euid);
			break;
		case PROC_EVENT_GID:
			Printf("GID process %d (tgid %d) gid %d egid %d\n",
			       event->event_data.id.process_pid,
			       event->event_data.id.process_tgid,
			       event->event_data.id.r.rgid,
			       event->event_data.id.e.egid);
			break;
		case PROC_EVENT_SID:
			Printf("SID process %d (tgid %d)\n",
			       event->event_data.sid.process_pid,
			       event->event_data.sid.process_tgid);
			break;
		case PROC_EVENT_PTRACE:
			Printf("Ptrace process %d (tgid %d), Tracer %d (tgid %d)\n",
			       event->event_data.ptrace.process_pid,
			       event->event_data.ptrace.process_tgid,
			       event->event_data.ptrace.tracer_pid,
			       event->event_data.ptrace.tracer_tgid);
			break;
		case PROC_EVENT_COMM:
			Printf("Comm process %d (tgid %d) comm %s\n",
			       event->event_data.comm.process_pid,
			       event->event_data.comm.process_tgid,
			       event->event_data.comm.comm);
			break;
		case PROC_EVENT_COREDUMP:
			Printf("Coredump process %d (tgid %d) parent %d, (tgid %d)\n",
			       event->event_data.coredump.process_pid,
			       event->event_data.coredump.process_tgid,
			       event->event_data.coredump.parent_pid,
			       event->event_data.coredump.parent_tgid);
			break;
		default:
			break;
		}
	}
	return 0;
}

int handle_events(int epoll_fd, struct proc_event *pev)
{
	char buff[CONNECTOR_MAX_MSG_SIZE];
	struct epoll_event ev[MAX_EVENTS];
	int i, event_count = 0, err = 0;

	event_count = epoll_wait(epoll_fd, ev, MAX_EVENTS, -1);
	if (event_count < 0) {
		ret_errno = errno;
		if (ret_errno != EINTR)
			perror("epoll_wait failed");
		return -3;
	}
	for (i = 0; i < event_count; i++) {
		if (!(ev[i].events & EPOLLIN))
			continue;
		if (recv(ev[i].data.fd, buff, sizeof(buff), 0) == -1) {
			ret_errno = errno;
			perror("recv failed");
			return -3;
		}
		err = handle_packet(buff, ev[i].data.fd, pev);
		if (err < 0)
			return err;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int epoll_fd, err;
	struct proc_event proc_ev;
	struct proc_input input;

	signal(SIGINT, sigint);

	if (argc > 2) {
		printf("Expected 0(assume no-filter) or 1 argument(-f)\n");
		exit(KSFT_SKIP);
	}

	if (argc == 2) {
		if (strcmp(argv[1], "-f") == 0) {
			filter = 1;
		} else {
			printf("Valid option : -f (for filter feature)\n");
			exit(KSFT_SKIP);
		}
	}

	if (filter) {
		input.event_type = PROC_EVENT_NONZERO_EXIT;
		input.mcast_op = PROC_CN_MCAST_LISTEN;
		err = register_proc_netlink(&epoll_fd, (void*)&input);
	} else {
		enum proc_cn_mcast_op op = PROC_CN_MCAST_LISTEN;
		err = register_proc_netlink(&epoll_fd, (void*)&op);
	}

	if (err < 0) {
		if (err == -2)
			close(nl_sock);
		if (err == -3) {
			close(nl_sock);
			close(epoll_fd);
		}
		exit(1);
	}

	while (!interrupted) {
		err = handle_events(epoll_fd, &proc_ev);
		if (err < 0) {
			if (ret_errno == EINTR)
				continue;
			if (err == -2)
				close(nl_sock);
			if (err == -3) {
				close(nl_sock);
				close(epoll_fd);
			}
			exit(1);
		}
	}

	if (filter) {
		input.mcast_op = PROC_CN_MCAST_IGNORE;
		send_message((void*)&input);
	} else {
		enum proc_cn_mcast_op op = PROC_CN_MCAST_IGNORE;
		send_message((void*)&op);
	}

	close(epoll_fd);
	close(nl_sock);

	printf("Done total count: %d\n", tcount);
	exit(0);
}
