// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#define _GNU_SOURCE
#include <test_progs.h>
#include <network_helpers.h>
#include <ctype.h>

#define CMD_OUT_BUF_SIZE 1023

#define SYS(cmd) ({ \
	if (!ASSERT_OK(system(cmd), (cmd))) \
		goto out; \
})

#define SYS_OUT(cmd, ...) ({ \
	char buf[1024]; \
	snprintf(buf, sizeof(buf), (cmd), ##__VA_ARGS__); \
	FILE *f = popen(buf, "r"); \
	if (!ASSERT_OK_PTR(f, buf)) \
		goto out; \
	f; \
})

/* out must be at least `size * 4 + 1` bytes long */
static void escape_str(char *out, const char *in, size_t size)
{
	static const char *hex = "0123456789ABCDEF";
	size_t i;

	for (i = 0; i < size; i++) {
		if (isprint(in[i]) && in[i] != '\\' && in[i] != '\'') {
			*out++ = in[i];
		} else {
			*out++ = '\\';
			*out++ = 'x';
			*out++ = hex[(in[i] >> 4) & 0xf];
			*out++ = hex[in[i] & 0xf];
		}
	}
	*out++ = '\0';
}

static bool expect_str(char *buf, size_t size, const char *str, const char *name)
{
	static char escbuf_expected[CMD_OUT_BUF_SIZE * 4];
	static char escbuf_actual[CMD_OUT_BUF_SIZE * 4];
	static int duration = 0;
	bool ok;

	ok = size == strlen(str) && !memcmp(buf, str, size);

	if (!ok) {
		escape_str(escbuf_expected, str, strlen(str));
		escape_str(escbuf_actual, buf, size);
	}
	CHECK(!ok, name, "unexpected %s: actual '%s' != expected '%s'\n",
	      name, escbuf_actual, escbuf_expected);

	return ok;
}

static void test_synproxy(bool xdp)
{
	int server_fd = -1, client_fd = -1, accept_fd = -1;
	char *prog_id = NULL, *prog_id_end;
	struct nstoken *ns = NULL;
	FILE *ctrl_file = NULL;
	char buf[CMD_OUT_BUF_SIZE];
	size_t size;

	SYS("ip netns add synproxy");

	SYS("ip link add tmp0 type veth peer name tmp1");
	SYS("ip link set tmp1 netns synproxy");
	SYS("ip link set tmp0 up");
	SYS("ip addr replace 198.18.0.1/24 dev tmp0");

	/* When checksum offload is enabled, the XDP program sees wrong
	 * checksums and drops packets.
	 */
	SYS("ethtool -K tmp0 tx off");
	if (xdp)
		/* Workaround required for veth. */
		SYS("ip link set tmp0 xdp object xdp_dummy.o section xdp 2> /dev/null");

	ns = open_netns("synproxy");
	if (!ASSERT_OK_PTR(ns, "setns"))
		goto out;

	SYS("ip link set lo up");
	SYS("ip link set tmp1 up");
	SYS("ip addr replace 198.18.0.2/24 dev tmp1");
	SYS("sysctl -w net.ipv4.tcp_syncookies=2");
	SYS("sysctl -w net.ipv4.tcp_timestamps=1");
	SYS("sysctl -w net.netfilter.nf_conntrack_tcp_loose=0");
	SYS("iptables -t raw -I PREROUTING \
	    -i tmp1 -p tcp -m tcp --syn --dport 8080 -j CT --notrack");
	SYS("iptables -t filter -A INPUT \
	    -i tmp1 -p tcp -m tcp --dport 8080 -m state --state INVALID,UNTRACKED \
	    -j SYNPROXY --sack-perm --timestamp --wscale 7 --mss 1460");
	SYS("iptables -t filter -A INPUT \
	    -i tmp1 -m state --state INVALID -j DROP");

	ctrl_file = SYS_OUT("./xdp_synproxy --iface tmp1 --ports 8080 \
			    --single --mss4 1460 --mss6 1440 \
			    --wscale 7 --ttl 64%s", xdp ? "" : " --tc");
	size = fread(buf, 1, sizeof(buf), ctrl_file);
	pclose(ctrl_file);
	if (!expect_str(buf, size, "Total SYNACKs generated: 0\n",
			"initial SYNACKs"))
		goto out;

	if (!xdp) {
		ctrl_file = SYS_OUT("tc filter show dev tmp1 ingress");
		size = fread(buf, 1, sizeof(buf), ctrl_file);
		pclose(ctrl_file);
		prog_id = memmem(buf, size, " id ", 4);
		if (!ASSERT_OK_PTR(prog_id, "find prog id"))
			goto out;
		prog_id += 4;
		if (!ASSERT_LT(prog_id, buf + size, "find prog id begin"))
			goto out;
		prog_id_end = prog_id;
		while (prog_id_end < buf + size && *prog_id_end >= '0' &&
		       *prog_id_end <= '9')
			prog_id_end++;
		if (!ASSERT_LT(prog_id_end, buf + size, "find prog id end"))
			goto out;
		*prog_id_end = '\0';
	}

	server_fd = start_server(AF_INET, SOCK_STREAM, "198.18.0.2", 8080, 0);
	if (!ASSERT_GE(server_fd, 0, "start_server"))
		goto out;

	close_netns(ns);
	ns = NULL;

	client_fd = connect_to_fd(server_fd, 10000);
	if (!ASSERT_GE(client_fd, 0, "connect_to_fd"))
		goto out;

	accept_fd = accept(server_fd, NULL, NULL);
	if (!ASSERT_GE(accept_fd, 0, "accept"))
		goto out;

	ns = open_netns("synproxy");
	if (!ASSERT_OK_PTR(ns, "setns"))
		goto out;

	if (xdp)
		ctrl_file = SYS_OUT("./xdp_synproxy --iface tmp1 --single");
	else
		ctrl_file = SYS_OUT("./xdp_synproxy --prog %s --single",
				    prog_id);
	size = fread(buf, 1, sizeof(buf), ctrl_file);
	pclose(ctrl_file);
	if (!expect_str(buf, size, "Total SYNACKs generated: 1\n",
			"SYNACKs after connection"))
		goto out;

out:
	if (accept_fd >= 0)
		close(accept_fd);
	if (client_fd >= 0)
		close(client_fd);
	if (server_fd >= 0)
		close(server_fd);
	if (ns)
		close_netns(ns);

	system("ip link del tmp0");
	system("ip netns del synproxy");
}

void test_xdp_synproxy(void)
{
	if (test__start_subtest("xdp"))
		test_synproxy(true);
	if (test__start_subtest("tc"))
		test_synproxy(false);
}
