// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ynl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <linux/genetlink.h>
#include <linux/netlink.h>

#include "netdev-user.h"

/* The below ifdef blob is required because:
 *
 * - sys/epoll.h does not (yet) have the ioctl definitions included. So,
 *   systems with older glibcs will not have them available. However,
 *   sys/epoll.h does include the type definition for epoll_data, which is
 *   needed by the user program (e.g. epoll_event.data.fd)
 *
 * - linux/eventpoll.h does not define the epoll_data type, it is simply an
 *   opaque __u64. It does, however, include the ioctl definition.
 *
 * Including both headers is impossible (types would be redefined), so I've
 * opted instead to take sys/epoll.h, and include the blob below.
 *
 * Someday, when glibc is globally up to date, the blob below can be removed.
 */
#if !defined(EPOLL_IOC_TYPE)
struct epoll_params {
	uint32_t busy_poll_usecs;
	uint16_t busy_poll_budget;
	uint8_t prefer_busy_poll;

	/* pad the struct to a multiple of 64bits */
	uint8_t __pad;
};

#define EPOLL_IOC_TYPE 0x8A
#define EPIOCSPARAMS _IOW(EPOLL_IOC_TYPE, 0x01, struct epoll_params)
#define EPIOCGPARAMS _IOR(EPOLL_IOC_TYPE, 0x02, struct epoll_params)
#endif

static uint16_t cfg_port = 8000;
static struct in_addr cfg_bind_addr = { .s_addr = INADDR_ANY };
static char *cfg_outfile;
static int cfg_max_events = 8;
static uint32_t cfg_ifindex;

/* busy poll params */
static uint32_t cfg_busy_poll_usecs;
static uint16_t cfg_busy_poll_budget;
static uint8_t cfg_prefer_busy_poll;

/* IRQ params */
static uint32_t cfg_defer_hard_irqs;
static uint64_t cfg_gro_flush_timeout;
static uint64_t cfg_irq_suspend_timeout;

static void usage(const char *filepath)
{
	error(1, 0,
	      "Usage: %s -p<port> -b<addr> -m<max_events> -u<busy_poll_usecs> -P<prefer_busy_poll> -g<busy_poll_budget> -o<outfile> -d<defer_hard_irqs> -r<gro_flush_timeout> -s<irq_suspend_timeout> -i<ifindex>",
	      filepath);
}

static void parse_opts(int argc, char **argv)
{
	unsigned long long tmp;
	int ret;
	int c;

	if (argc <= 1)
		usage(argv[0]);

	while ((c = getopt(argc, argv, "p:m:b:u:P:g:o:d:r:s:i:")) != -1) {
		/* most options take integer values, except o and b, so reduce
		 * code duplication a bit for the common case by calling
		 * strtoull here and leave bounds checking and casting per
		 * option below.
		 */
		if (c != 'o' && c != 'b')
			tmp = strtoull(optarg, NULL, 0);

		switch (c) {
		case 'u':
			if (tmp == ULLONG_MAX || tmp > UINT32_MAX)
				error(1, ERANGE, "busy_poll_usecs too large");

			cfg_busy_poll_usecs = (uint32_t)tmp;
			break;
		case 'P':
			if (tmp == ULLONG_MAX || tmp > 1)
				error(1, ERANGE,
				      "prefer busy poll should be 0 or 1");

			cfg_prefer_busy_poll = (uint8_t)tmp;
			break;
		case 'g':
			if (tmp == ULLONG_MAX || tmp > UINT16_MAX)
				error(1, ERANGE,
				      "busy poll budget must be [0, UINT16_MAX]");

			cfg_busy_poll_budget = (uint16_t)tmp;
			break;
		case 'p':
			if (tmp == ULLONG_MAX || tmp > UINT16_MAX)
				error(1, ERANGE, "port must be <= 65535");

			cfg_port = (uint16_t)tmp;
			break;
		case 'b':
			ret = inet_aton(optarg, &cfg_bind_addr);
			if (ret == 0)
				error(1, errno,
				      "bind address %s invalid", optarg);
			break;
		case 'o':
			cfg_outfile = strdup(optarg);
			if (!cfg_outfile)
				error(1, 0, "outfile invalid");
			break;
		case 'm':
			if (tmp == ULLONG_MAX || tmp > INT_MAX)
				error(1, ERANGE,
				      "max events must be > 0 and <= INT_MAX");

			cfg_max_events = (int)tmp;
			break;
		case 'd':
			if (tmp == ULLONG_MAX || tmp > INT32_MAX)
				error(1, ERANGE,
				      "defer_hard_irqs must be <= INT32_MAX");

			cfg_defer_hard_irqs = (uint32_t)tmp;
			break;
		case 'r':
			if (tmp == ULLONG_MAX || tmp > UINT64_MAX)
				error(1, ERANGE,
				      "gro_flush_timeout must be < UINT64_MAX");

			cfg_gro_flush_timeout = (uint64_t)tmp;
			break;
		case 's':
			if (tmp == ULLONG_MAX || tmp > UINT64_MAX)
				error(1, ERANGE,
				      "irq_suspend_timeout must be < ULLONG_MAX");

			cfg_irq_suspend_timeout = (uint64_t)tmp;
			break;
		case 'i':
			if (tmp == ULLONG_MAX || tmp > INT_MAX)
				error(1, ERANGE,
				      "ifindex must be <= INT_MAX");

			cfg_ifindex = (int)tmp;
			break;
		}
	}

	if (!cfg_ifindex)
		usage(argv[0]);

	if (optind != argc)
		usage(argv[0]);
}

static void epoll_ctl_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev;

	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
		error(1, errno, "epoll_ctl add fd: %d", fd);
}

static void setnonblock(int sockfd)
{
	int flags;

	flags = fcntl(sockfd, F_GETFL, 0);

	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
		error(1, errno, "unable to set socket to nonblocking mode");
}

static void write_chunk(int fd, char *buf, ssize_t buflen)
{
	ssize_t remaining = buflen;
	char *buf_offset = buf;
	ssize_t writelen = 0;
	ssize_t write_result;

	while (writelen < buflen) {
		write_result = write(fd, buf_offset, remaining);
		if (write_result == -1)
			error(1, errno, "unable to write data to outfile");

		writelen += write_result;
		remaining -= write_result;
		buf_offset += write_result;
	}
}

static void setup_queue(void)
{
	struct netdev_napi_get_list *napi_list = NULL;
	struct netdev_napi_get_req_dump *req = NULL;
	struct netdev_napi_set_req *set_req = NULL;
	struct ynl_sock *ys;
	struct ynl_error yerr;
	uint32_t napi_id = 0;

	ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!ys)
		error(1, 0, "YNL: %s", yerr.msg);

	req = netdev_napi_get_req_dump_alloc();
	netdev_napi_get_req_dump_set_ifindex(req, cfg_ifindex);
	napi_list = netdev_napi_get_dump(ys, req);

	/* assume there is 1 NAPI configured and take the first */
	if (napi_list->obj._present.id)
		napi_id = napi_list->obj.id;
	else
		error(1, 0, "napi ID not present?");

	set_req = netdev_napi_set_req_alloc();
	netdev_napi_set_req_set_id(set_req, napi_id);
	netdev_napi_set_req_set_defer_hard_irqs(set_req, cfg_defer_hard_irqs);
	netdev_napi_set_req_set_gro_flush_timeout(set_req,
						  cfg_gro_flush_timeout);
	netdev_napi_set_req_set_irq_suspend_timeout(set_req,
						    cfg_irq_suspend_timeout);

	if (netdev_napi_set(ys, set_req))
		error(1, 0, "can't set NAPI params: %s\n", yerr.msg);

	netdev_napi_get_list_free(napi_list);
	netdev_napi_get_req_dump_free(req);
	netdev_napi_set_req_free(set_req);
	ynl_sock_destroy(ys);
}

static void run_poller(void)
{
	struct epoll_event events[cfg_max_events];
	struct epoll_params epoll_params = {0};
	struct sockaddr_in server_addr;
	int i, epfd, nfds;
	ssize_t readlen;
	int outfile_fd;
	char buf[1024];
	int sockfd;
	int conn;
	int val;

	outfile_fd = open(cfg_outfile, O_WRONLY | O_CREAT, 0644);
	if (outfile_fd == -1)
		error(1, errno, "unable to open outfile: %s", cfg_outfile);

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd == -1)
		error(1, errno, "unable to create listen socket");

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(cfg_port);
	server_addr.sin_addr = cfg_bind_addr;

	/* these values are range checked during parse_opts, so casting is safe
	 * here
	 */
	epoll_params.busy_poll_usecs = cfg_busy_poll_usecs;
	epoll_params.busy_poll_budget = cfg_busy_poll_budget;
	epoll_params.prefer_busy_poll = cfg_prefer_busy_poll;
	epoll_params.__pad = 0;

	val = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)))
		error(1, errno, "poller setsockopt reuseaddr");

	setnonblock(sockfd);

	if (bind(sockfd, (struct sockaddr *)&server_addr,
		 sizeof(struct sockaddr_in)))
		error(0, errno, "poller bind to port: %d\n", cfg_port);

	if (listen(sockfd, 1))
		error(1, errno, "poller listen");

	epfd = epoll_create1(0);
	if (ioctl(epfd, EPIOCSPARAMS, &epoll_params) == -1)
		error(1, errno, "unable to set busy poll params");

	epoll_ctl_add(epfd, sockfd, EPOLLIN | EPOLLOUT | EPOLLET);

	for (;;) {
		nfds = epoll_wait(epfd, events, cfg_max_events, -1);
		for (i = 0; i < nfds; i++) {
			if (events[i].data.fd == sockfd) {
				conn = accept(sockfd, NULL, NULL);
				if (conn == -1)
					error(1, errno,
					      "accepting incoming connection failed");

				setnonblock(conn);
				epoll_ctl_add(epfd, conn,
					      EPOLLIN | EPOLLET | EPOLLRDHUP |
					      EPOLLHUP);
			} else if (events[i].events & EPOLLIN) {
				for (;;) {
					readlen = read(events[i].data.fd, buf,
						       sizeof(buf));
					if (readlen > 0)
						write_chunk(outfile_fd, buf,
							    readlen);
					else
						break;
				}
			} else {
				/* spurious event ? */
			}
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				epoll_ctl(epfd, EPOLL_CTL_DEL,
					  events[i].data.fd, NULL);
				close(events[i].data.fd);
				close(outfile_fd);
				return;
			}
		}
	}
}

int main(int argc, char *argv[])
{
	parse_opts(argc, argv);
	setup_queue();
	run_poller();

	if (cfg_outfile)
		free(cfg_outfile);

	return 0;
}
