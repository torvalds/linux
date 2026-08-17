// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Christian Brauner */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <test_progs.h>

#include "sock_read_xattr.skel.h"

static const char xattr_value[] = "bpf_sock_value";
static const char xattr_name[] = "user.bpf_test";

static void test_read_sock_xattr(void)
{
	struct sockaddr_in addr = {};
	struct sock_read_xattr *skel = NULL;
	struct bpf_link *link = NULL;
	int sock_fd = -1, err;

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (!ASSERT_OK_FD(sock_fd, "socket"))
		return;

	err = fsetxattr(sock_fd, xattr_name, xattr_value, sizeof(xattr_value), 0);
	if (!ASSERT_OK(err, "fsetxattr"))
		goto out;

	skel = sock_read_xattr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "sock_read_xattr__open_and_load"))
		goto out;

	skel->bss->monitored_pid = sys_gettid();

	/* Only attach the functional program; the verifier-only programs
	 * above are not pid-gated and would clobber the shared globals.
	 */
	link = bpf_program__attach(skel->progs.read_sock_xattr);
	if (!ASSERT_OK_PTR(link, "attach read_sock_xattr"))
		goto out;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(1234);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	/* Only the lsm/socket_connect hook matters; the connect may fail. */
	connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));

	ASSERT_EQ(skel->data->read_ret, sizeof(xattr_value), "read_ret");
	ASSERT_STREQ(skel->bss->value, xattr_value, "value");

out:
	bpf_link__destroy(link);
	if (sock_fd >= 0)
		close(sock_fd);
	sock_read_xattr__destroy(skel);
}

void test_sock_xattr(void)
{
	RUN_TESTS(sock_read_xattr);

	if (test__start_subtest("read_sock_xattr"))
		test_read_sock_xattr();
}
