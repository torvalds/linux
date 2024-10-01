// SPDX-License-Identifier: GPL-2.0
/* Test IPV6_FLOWINFO_MGR */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <error.h>
#include <errno.h>
#include <limits.h>
#include <linux/in6.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

	fd = socket(PF_INET6, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket");

	run_tests(fd);

	if (close(fd))
		error(1, errno, "close");

	return 0;
}
