// SPDX-License-Identifier: GPL-2.0
/* Test IPV6_FLOWINFO_MGR */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/in6.h>
#include <net/if.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* uapi/glibc weirdness may leave this undefined */
#ifndef IPV6_FLOWLABEL_MGR
#define IPV6_FLOWLABEL_MGR	32
#endif
#ifndef IPV6_FLOWINFO_SEND
#define IPV6_FLOWINFO_SEND	33
#endif

/* from net/ipv6/ip6_flowlabel.c */
#define FL_MIN_LINGER		6

#define explain(x)							\
	do { if (cfg_verbose) fprintf(stderr, "       " x "\n"); } while (0)

#define __expect(x)							\
	do {								\
		if (!(x))						\
			fprintf(stderr, "[OK]   " #x "\n");		\
		else							\
			error(1, 0, "[ERR]  " #x " (line %d)", __LINE__); \
	} while (0)

#define expect_pass(x)	__expect(x)
#define expect_fail(x)	__expect(!(x))

#define expect_fail_errno(x, e)						\
	do {								\
		int __exp = (e);					\
		int __ret = (x);					\
		int __err = errno;					\
		if (__ret && __err == __exp)				\
			fprintf(stderr, "[OK]   " #x "\n");		\
		else if (!__ret)					\
			error(1, 0, "[ERR]  " #x			\
			      " (line %d): unexpectedly succeeded",	\
			      __LINE__);				\
		else							\
			error(1, 0, "[ERR]  " #x			\
			      " (line %d): expected errno %d, got %d",	\
			      __LINE__, __exp, __err);			\
	} while (0)

static bool cfg_long_running;
static bool cfg_verbose;

static int flowlabel_get(int fd, uint32_t label, uint8_t share, uint16_t flags)
{
	struct in6_flowlabel_req req = {
		.flr_action = IPV6_FL_A_GET,
		.flr_label = htonl(label),
		.flr_flags = flags,
		.flr_share = share,
	};

	/* do not pass IPV6_ADDR_ANY or IPV6_ADDR_MAPPED */
	req.flr_dst.s6_addr[0] = 0xfd;
	req.flr_dst.s6_addr[15] = 0x1;

	return setsockopt(fd, SOL_IPV6, IPV6_FLOWLABEL_MGR, &req, sizeof(req));
}

static int flowlabel_put(int fd, uint32_t label)
{
	struct in6_flowlabel_req req = {
		.flr_action = IPV6_FL_A_PUT,
		.flr_label = htonl(label),
	};

	return setsockopt(fd, SOL_IPV6, IPV6_FLOWLABEL_MGR, &req, sizeof(req));
}

static int flowlabel_renew(int fd, uint32_t label, uint8_t share,
			   uint16_t linger)
{
	struct in6_flowlabel_req req = {
		.flr_action = IPV6_FL_A_RENEW,
		.flr_label = htonl(label),
		.flr_share = share,
		.flr_linger = linger,
	};

	return setsockopt(fd, SOL_IPV6, IPV6_FLOWLABEL_MGR, &req, sizeof(req));
}

static struct sockaddr_in6 loopback_addr(void)
{
	struct sockaddr_in6 addr = {
		.sin6_family	= AF_INET6,
		.sin6_addr	= IN6ADDR_LOOPBACK_INIT,
		.sin6_port	= htons(8888),
	};

	return addr;
}

static int tcp_listen(void)
{
	struct sockaddr_in6 addr = loopback_addr();
	const int one = 1;
	int fd;

	fd = socket(PF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		error(1, errno, "socket listener");
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
		error(1, errno, "setsockopt SO_REUSEADDR");
	if (bind(fd, (void *)&addr, sizeof(addr)))
		error(1, errno, "bind");
	if (listen(fd, 1))
		error(1, errno, "listen");

	return fd;
}

static void tcp_connect(int listener, uint32_t flowlabel,
			int *client, int *accepted)
{
	struct sockaddr_in6 addr = loopback_addr();
	const int one = 1;
	int cfd, afd;

	cfd = socket(PF_INET6, SOCK_STREAM, 0);
	if (cfd == -1)
		error(1, errno, "socket client");

	if (flowlabel_get(cfd, flowlabel, IPV6_FL_S_EXCL, IPV6_FL_F_CREATE))
		error(1, errno, "flowlabel_get");
	if (setsockopt(cfd, SOL_IPV6, IPV6_FLOWINFO_SEND, &one, sizeof(one)))
		error(1, errno, "setsockopt flowinfo_send");
	addr.sin6_flowinfo = htonl(flowlabel);

	if (connect(cfd, (void *)&addr, sizeof(addr)))
		error(1, errno, "connect");

	afd = accept(listener, NULL, NULL);
	if (afd == -1)
		error(1, errno, "accept");

	if (flowlabel_put(cfd, flowlabel))
		error(1, errno, "flowlabel_put");

	*client = cfd;
	*accepted = afd;
}

static bool disable_flowlabel_consistency(void)
{
	int fd;

	fd = open("/proc/sys/net/ipv6/flowlabel_consistency", O_WRONLY);
	if (fd == -1)
		return false;

	if (write(fd, "0", 1) != 1) {
		close(fd);
		return false;
	}
	close(fd);

	return true;
}

static void run_tests(int fd)
{
	int wstatus;
	pid_t pid;

	explain("cannot get non-existent label");
	expect_fail(flowlabel_get(fd, 1, IPV6_FL_S_ANY, 0));

	explain("cannot put non-existent label");
	expect_fail(flowlabel_put(fd, 1));

	explain("cannot create label greater than 20 bits");
	expect_fail(flowlabel_get(fd, 0x1FFFFF, IPV6_FL_S_ANY,
				  IPV6_FL_F_CREATE));

	explain("create a new label (FL_F_CREATE)");
	expect_pass(flowlabel_get(fd, 1, IPV6_FL_S_ANY, IPV6_FL_F_CREATE));
	explain("can get the label (without FL_F_CREATE)");
	expect_pass(flowlabel_get(fd, 1, IPV6_FL_S_ANY, 0));
	explain("can get it again with create flag set, too");
	expect_pass(flowlabel_get(fd, 1, IPV6_FL_S_ANY, IPV6_FL_F_CREATE));
	explain("cannot get it again with the exclusive (FL_FL_EXCL) flag");
	expect_fail(flowlabel_get(fd, 1, IPV6_FL_S_ANY,
				  IPV6_FL_F_CREATE | IPV6_FL_F_EXCL));
	explain("can now put exactly three references");
	expect_pass(flowlabel_put(fd, 1));
	expect_pass(flowlabel_put(fd, 1));
	expect_pass(flowlabel_put(fd, 1));
	expect_fail(flowlabel_put(fd, 1));

	explain("create a new exclusive label (FL_S_EXCL)");
	expect_pass(flowlabel_get(fd, 2, IPV6_FL_S_EXCL, IPV6_FL_F_CREATE));
	explain("cannot get it again in non-exclusive mode");
	expect_fail(flowlabel_get(fd, 2, IPV6_FL_S_ANY,  IPV6_FL_F_CREATE));
	explain("cannot get it again in exclusive mode either");
	expect_fail(flowlabel_get(fd, 2, IPV6_FL_S_EXCL, IPV6_FL_F_CREATE));
	expect_pass(flowlabel_put(fd, 2));

	if (cfg_long_running) {
		explain("cannot reuse the label, due to linger");
		expect_fail(flowlabel_get(fd, 2, IPV6_FL_S_ANY,
					  IPV6_FL_F_CREATE));
		explain("after sleep, can reuse");
		sleep(FL_MIN_LINGER * 2 + 1);
		expect_pass(flowlabel_get(fd, 2, IPV6_FL_S_ANY,
					  IPV6_FL_F_CREATE));
	}

	explain("create a new user-private label (FL_S_USER)");
	expect_pass(flowlabel_get(fd, 3, IPV6_FL_S_USER, IPV6_FL_F_CREATE));
	explain("cannot get it again in non-exclusive mode");
	expect_fail(flowlabel_get(fd, 3, IPV6_FL_S_ANY, 0));
	explain("cannot get it again in exclusive mode");
	expect_fail(flowlabel_get(fd, 3, IPV6_FL_S_EXCL, 0));
	explain("can get it again in user mode");
	expect_pass(flowlabel_get(fd, 3, IPV6_FL_S_USER, 0));
	explain("child process can get it too, but not after setuid(nobody)");
	pid = fork();
	if (pid == -1)
		error(1, errno, "fork");
	if (!pid) {
		expect_pass(flowlabel_get(fd, 3, IPV6_FL_S_USER, 0));
		if (setuid(USHRT_MAX))
			fprintf(stderr, "[INFO] skip setuid child test\n");
		else
			expect_fail(flowlabel_get(fd, 3, IPV6_FL_S_USER, 0));
		exit(0);
	}
	if (wait(&wstatus) == -1)
		error(1, errno, "wait");
	if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
		error(1, errno, "wait: unexpected child result");

	explain("create a new process-private label (FL_S_PROCESS)");
	expect_pass(flowlabel_get(fd, 4, IPV6_FL_S_PROCESS, IPV6_FL_F_CREATE));
	explain("can get it again");
	expect_pass(flowlabel_get(fd, 4, IPV6_FL_S_PROCESS, 0));
	explain("child process cannot can get it");
	pid = fork();
	if (pid == -1)
		error(1, errno, "fork");
	if (!pid) {
		expect_fail(flowlabel_get(fd, 4, IPV6_FL_S_PROCESS, 0));
		exit(0);
	}
	if (wait(&wstatus) == -1)
		error(1, errno, "wait");
	if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
		error(1, errno, "wait: unexpected child result");

	explain("It is not possible to renew a label that does not exist");
	expect_fail_errno(flowlabel_renew(fd, 5, IPV6_FL_S_EXCL,
					  2 * (FL_MIN_LINGER * 2 + 1)),
			  ESRCH);

	explain("Create a label for basic renew validation");
	expect_pass(flowlabel_get(fd, 5, IPV6_FL_S_EXCL, IPV6_FL_F_CREATE));
	explain("renew does not error for an existing, valid label");
	expect_pass(flowlabel_renew(fd, 5, IPV6_FL_S_EXCL,
				    2 * (FL_MIN_LINGER * 2 + 1)));

	if (cfg_long_running) {
		explain("create a new label with FL_MIN_LINGER linger time");
		expect_pass(flowlabel_get(fd, 6, IPV6_FL_S_EXCL,
					  IPV6_FL_F_CREATE));
		explain("renew the label to extend linger, then put it");
		expect_pass(flowlabel_renew(fd, 6, IPV6_FL_S_EXCL,
					    2 * (FL_MIN_LINGER * 2 + 1)));
		expect_pass(flowlabel_put(fd, 6));
		sleep(FL_MIN_LINGER * 2 + 1);
		explain("cannot create: new linger time not over yet");
		expect_fail_errno(flowlabel_get(fd, 6, IPV6_FL_S_ANY,
						IPV6_FL_F_CREATE),
				  EPERM);
	}

	{
		struct in6_flowlabel_req freq = {
			.flr_action = IPV6_FL_A_GET,
			.flr_flags = IPV6_FL_F_REMOTE,
		};
		int remote_listener = tcp_listen();
		socklen_t freq_len = sizeof(freq);
		int remote_cfd, remote_afd;

		explain("Prepare TCP SYN for REMOTE flag validation");
		tcp_connect(remote_listener, 7, &remote_cfd, &remote_afd);

		explain("Query for label sent by client with IPV6_FL_F_REMOTE");
		expect_pass(getsockopt(remote_afd, SOL_IPV6, IPV6_FLOWLABEL_MGR,
				       &freq, &freq_len));
		if (ntohl(freq.flr_label) != 7)
			error(1, 0, "unexpected remote flowlabel %u",
			      ntohl(freq.flr_label));

		close(remote_afd);
		close(remote_cfd);
		close(remote_listener);
	}

	if (!disable_flowlabel_consistency()) {
		fprintf(stderr,
			"[INFO] skip REFLECT: cannot disable net.ipv6.flowlabel_consistency\n");
	} else {
		struct in6_flowlabel_req reflect_query = {
			.flr_action = IPV6_FL_A_GET,
		};
		struct in6_flowlabel_req reflect_off = {
			.flr_action = IPV6_FL_A_PUT,
			.flr_flags = IPV6_FL_F_REFLECT,
		};
		struct in6_flowlabel_req reflect_on = {
			.flr_action = IPV6_FL_A_GET,
			.flr_flags = IPV6_FL_F_REFLECT,
		};
		socklen_t reflect_query_len = sizeof(reflect_query);
		int reflect_listener = tcp_listen();
		int reflect_cfd, reflect_afd;

		explain("Enable REFLECT on listener before client connects");
		expect_pass(setsockopt(reflect_listener, SOL_IPV6,
				       IPV6_FLOWLABEL_MGR, &reflect_on,
				       sizeof(reflect_on)));

		tcp_connect(reflect_listener, 8, &reflect_cfd, &reflect_afd);

		explain("accepted socket's label should be reflected");
		expect_pass(getsockopt(reflect_afd, SOL_IPV6,
				       IPV6_FLOWLABEL_MGR, &reflect_query,
				       &reflect_query_len));
		if (ntohl(reflect_query.flr_label) != 8)
			error(1, 0, "unexpected reflected flowlabel %u",
			      ntohl(reflect_query.flr_label));

		explain("PUT+REFLECT disables reflection on accepted socket");
		expect_pass(setsockopt(reflect_afd, SOL_IPV6,
				       IPV6_FLOWLABEL_MGR, &reflect_off,
				       sizeof(reflect_off)));
		explain("cannot disable reflection twice");
		expect_fail(setsockopt(reflect_afd, SOL_IPV6,
				       IPV6_FLOWLABEL_MGR, &reflect_off,
				       sizeof(reflect_off)));

		close(reflect_afd);
		close(reflect_cfd);
		close(reflect_listener);
	}
}

static void setup(void)
{
	struct ifreq ifr = {
		.ifr_name = "lo"
	};
	int ctl;

	if (unshare(CLONE_NEWNET))
		error(1, errno, "unshare");

	ctl = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (ctl == -1)
		error(1, errno, "socket");

	if (ioctl(ctl, SIOCGIFFLAGS, &ifr))
		error(1, errno, "ioctl SIOCGIFFLAGS");
	ifr.ifr_flags |= IFF_UP;
	if (ioctl(ctl, SIOCSIFFLAGS, &ifr))
		error(1, errno, "ioctl: bring lo up");

	if (close(ctl))
		error(1, errno, "close");
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "lv")) != -1) {
		switch (c) {
		case 'l':
			cfg_long_running = true;
			break;
		case 'v':
			cfg_verbose = true;
			break;
		default:
			error(1, 0, "%s: parse error", argv[0]);
		}
	}
}

int main(int argc, char **argv)
{
	int fd;

	parse_opts(argc, argv);
	setup();

	fd = socket(PF_INET6, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket");

	run_tests(fd);

	if (close(fd))
		error(1, errno, "close");

	return 0;
}
