// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <linux/filter.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"
#include "bpf_rlimit.h"
#include "bpf_util.h"

#ifndef ENOTSUPP
# define ENOTSUPP 524
#endif

#define CG_PATH	"/foo"
#define CONNECT4_PROG_PATH	"./connect4_prog.o"
#define CONNECT6_PROG_PATH	"./connect6_prog.o"
#define SENDMSG4_PROG_PATH	"./sendmsg4_prog.o"
#define SENDMSG6_PROG_PATH	"./sendmsg6_prog.o"
#define BIND4_PROG_PATH		"./bind4_prog.o"
#define BIND6_PROG_PATH		"./bind6_prog.o"

#define SERV4_IP		"192.168.1.254"
#define SERV4_REWRITE_IP	"127.0.0.1"
#define SRC4_IP			"172.16.0.1"
#define SRC4_REWRITE_IP		"127.0.0.4"
#define SERV4_PORT		4040
#define SERV4_REWRITE_PORT	4444

#define SERV6_IP		"face:b00c:1234:5678::abcd"
#define SERV6_REWRITE_IP	"::1"
#define SERV6_V4MAPPED_IP	"::ffff:192.168.0.4"
#define SRC6_IP			"::1"
#define SRC6_REWRITE_IP		"::6"
#define WILDCARD6_IP		"::"
#define SERV6_PORT		6060
#define SERV6_REWRITE_PORT	6666

#define INET_NTOP_BUF	40

struct sock_addr_test;

typedef int (*load_fn)(const struct sock_addr_test *test);
typedef int (*info_fn)(int, struct sockaddr *, socklen_t *);

char bpf_log_buf[BPF_LOG_BUF_SIZE];

struct sock_addr_test {
	const char *descr;
	/* BPF prog properties */
	load_fn loadfn;
	enum bpf_attach_type expected_attach_type;
	enum bpf_attach_type attach_type;
	/* Socket properties */
	int domain;
	int type;
	/* IP:port pairs for BPF prog to override */
	const char *requested_ip;
	unsigned short requested_port;
	const char *expected_ip;
	unsigned short expected_port;
	const char *expected_src_ip;
	/* Expected test result */
	enum {
		LOAD_REJECT,
		ATTACH_REJECT,
		ATTACH_OKAY,
		SYSCALL_EPERM,
		SYSCALL_ENOTSUPP,
		SUCCESS,
	} expected_result;
};

static int bind4_prog_load(const struct sock_addr_test *test);
static int bind6_prog_load(const struct sock_addr_test *test);
static int connect4_prog_load(const struct sock_addr_test *test);
static int connect6_prog_load(const struct sock_addr_test *test);
static int sendmsg_allow_prog_load(const struct sock_addr_test *test);
static int sendmsg_deny_prog_load(const struct sock_addr_test *test);
static int recvmsg_allow_prog_load(const struct sock_addr_test *test);
static int recvmsg_deny_prog_load(const struct sock_addr_test *test);
static int sendmsg4_rw_asm_prog_load(const struct sock_addr_test *test);
static int recvmsg4_rw_asm_prog_load(const struct sock_addr_test *test);
static int sendmsg4_rw_c_prog_load(const struct sock_addr_test *test);
static int sendmsg6_rw_asm_prog_load(const struct sock_addr_test *test);
static int recvmsg6_rw_asm_prog_load(const struct sock_addr_test *test);
static int sendmsg6_rw_c_prog_load(const struct sock_addr_test *test);
static int sendmsg6_rw_v4mapped_prog_load(const struct sock_addr_test *test);
static int sendmsg6_rw_wildcard_prog_load(const struct sock_addr_test *test);

static struct sock_addr_test tests[] = {
	/* bind */
	{
		"bind4: load prog with wrong expected attach type",
		bind4_prog_load,
		BPF_CGROUP_INET6_BIND,
		BPF_CGROUP_INET4_BIND,
		AF_INET,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"bind4: attach prog with wrong attach type",
		bind4_prog_load,
		BPF_CGROUP_INET4_BIND,
		BPF_CGROUP_INET6_BIND,
		AF_INET,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_REJECT,
	},
	{
		"bind4: rewrite IP & TCP port in",
		bind4_prog_load,
		BPF_CGROUP_INET4_BIND,
		BPF_CGROUP_INET4_BIND,
		AF_INET,
		SOCK_STREAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		NULL,
		SUCCESS,
	},
	{
		"bind4: rewrite IP & UDP port in",
		bind4_prog_load,
		BPF_CGROUP_INET4_BIND,
		BPF_CGROUP_INET4_BIND,
		AF_INET,
		SOCK_DGRAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		NULL,
		SUCCESS,
	},
	{
		"bind6: load prog with wrong expected attach type",
		bind6_prog_load,
		BPF_CGROUP_INET4_BIND,
		BPF_CGROUP_INET6_BIND,
		AF_INET6,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"bind6: attach prog with wrong attach type",
		bind6_prog_load,
		BPF_CGROUP_INET6_BIND,
		BPF_CGROUP_INET4_BIND,
		AF_INET,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_REJECT,
	},
	{
		"bind6: rewrite IP & TCP port in",
		bind6_prog_load,
		BPF_CGROUP_INET6_BIND,
		BPF_CGROUP_INET6_BIND,
		AF_INET6,
		SOCK_STREAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		NULL,
		SUCCESS,
	},
	{
		"bind6: rewrite IP & UDP port in",
		bind6_prog_load,
		BPF_CGROUP_INET6_BIND,
		BPF_CGROUP_INET6_BIND,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		NULL,
		SUCCESS,
	},

	/* connect */
	{
		"connect4: load prog with wrong expected attach type",
		connect4_prog_load,
		BPF_CGROUP_INET6_CONNECT,
		BPF_CGROUP_INET4_CONNECT,
		AF_INET,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"connect4: attach prog with wrong attach type",
		connect4_prog_load,
		BPF_CGROUP_INET4_CONNECT,
		BPF_CGROUP_INET6_CONNECT,
		AF_INET,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_REJECT,
	},
	{
		"connect4: rewrite IP & TCP port",
		connect4_prog_load,
		BPF_CGROUP_INET4_CONNECT,
		BPF_CGROUP_INET4_CONNECT,
		AF_INET,
		SOCK_STREAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SRC4_REWRITE_IP,
		SUCCESS,
	},
	{
		"connect4: rewrite IP & UDP port",
		connect4_prog_load,
		BPF_CGROUP_INET4_CONNECT,
		BPF_CGROUP_INET4_CONNECT,
		AF_INET,
		SOCK_DGRAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SRC4_REWRITE_IP,
		SUCCESS,
	},
	{
		"connect6: load prog with wrong expected attach type",
		connect6_prog_load,
		BPF_CGROUP_INET4_CONNECT,
		BPF_CGROUP_INET6_CONNECT,
		AF_INET6,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"connect6: attach prog with wrong attach type",
		connect6_prog_load,
		BPF_CGROUP_INET6_CONNECT,
		BPF_CGROUP_INET4_CONNECT,
		AF_INET,
		SOCK_STREAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_REJECT,
	},
	{
		"connect6: rewrite IP & TCP port",
		connect6_prog_load,
		BPF_CGROUP_INET6_CONNECT,
		BPF_CGROUP_INET6_CONNECT,
		AF_INET6,
		SOCK_STREAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SUCCESS,
	},
	{
		"connect6: rewrite IP & UDP port",
		connect6_prog_load,
		BPF_CGROUP_INET6_CONNECT,
		BPF_CGROUP_INET6_CONNECT,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SUCCESS,
	},

	/* sendmsg */
	{
		"sendmsg4: load prog with wrong expected attach type",
		sendmsg4_rw_asm_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP4_SENDMSG,
		AF_INET,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"sendmsg4: attach prog with wrong attach type",
		sendmsg4_rw_asm_prog_load,
		BPF_CGROUP_UDP4_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_REJECT,
	},
	{
		"sendmsg4: rewrite IP & port (asm)",
		sendmsg4_rw_asm_prog_load,
		BPF_CGROUP_UDP4_SENDMSG,
		BPF_CGROUP_UDP4_SENDMSG,
		AF_INET,
		SOCK_DGRAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SRC4_REWRITE_IP,
		SUCCESS,
	},
	{
		"sendmsg4: rewrite IP & port (C)",
		sendmsg4_rw_c_prog_load,
		BPF_CGROUP_UDP4_SENDMSG,
		BPF_CGROUP_UDP4_SENDMSG,
		AF_INET,
		SOCK_DGRAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SRC4_REWRITE_IP,
		SUCCESS,
	},
	{
		"sendmsg4: deny call",
		sendmsg_deny_prog_load,
		BPF_CGROUP_UDP4_SENDMSG,
		BPF_CGROUP_UDP4_SENDMSG,
		AF_INET,
		SOCK_DGRAM,
		SERV4_IP,
		SERV4_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SRC4_REWRITE_IP,
		SYSCALL_EPERM,
	},
	{
		"sendmsg6: load prog with wrong expected attach type",
		sendmsg6_rw_asm_prog_load,
		BPF_CGROUP_UDP4_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"sendmsg6: attach prog with wrong attach type",
		sendmsg6_rw_asm_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP4_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_REJECT,
	},
	{
		"sendmsg6: rewrite IP & port (asm)",
		sendmsg6_rw_asm_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SUCCESS,
	},
	{
		"sendmsg6: rewrite IP & port (C)",
		sendmsg6_rw_c_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SUCCESS,
	},
	{
		"sendmsg6: IPv4-mapped IPv6",
		sendmsg6_rw_v4mapped_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SYSCALL_ENOTSUPP,
	},
	{
		"sendmsg6: set dst IP = [::] (BSD'ism)",
		sendmsg6_rw_wildcard_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SUCCESS,
	},
	{
		"sendmsg6: preserve dst IP = [::] (BSD'ism)",
		sendmsg_allow_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		WILDCARD6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_PORT,
		SRC6_IP,
		SUCCESS,
	},
	{
		"sendmsg6: deny call",
		sendmsg_deny_prog_load,
		BPF_CGROUP_UDP6_SENDMSG,
		BPF_CGROUP_UDP6_SENDMSG,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_IP,
		SERV6_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SRC6_REWRITE_IP,
		SYSCALL_EPERM,
	},

	/* recvmsg */
	{
		"recvmsg4: return code ok",
		recvmsg_allow_prog_load,
		BPF_CGROUP_UDP4_RECVMSG,
		BPF_CGROUP_UDP4_RECVMSG,
		AF_INET,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_OKAY,
	},
	{
		"recvmsg4: return code !ok",
		recvmsg_deny_prog_load,
		BPF_CGROUP_UDP4_RECVMSG,
		BPF_CGROUP_UDP4_RECVMSG,
		AF_INET,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"recvmsg6: return code ok",
		recvmsg_allow_prog_load,
		BPF_CGROUP_UDP6_RECVMSG,
		BPF_CGROUP_UDP6_RECVMSG,
		AF_INET6,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		ATTACH_OKAY,
	},
	{
		"recvmsg6: return code !ok",
		recvmsg_deny_prog_load,
		BPF_CGROUP_UDP6_RECVMSG,
		BPF_CGROUP_UDP6_RECVMSG,
		AF_INET6,
		SOCK_DGRAM,
		NULL,
		0,
		NULL,
		0,
		NULL,
		LOAD_REJECT,
	},
	{
		"recvmsg4: rewrite IP & port (asm)",
		recvmsg4_rw_asm_prog_load,
		BPF_CGROUP_UDP4_RECVMSG,
		BPF_CGROUP_UDP4_RECVMSG,
		AF_INET,
		SOCK_DGRAM,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SERV4_REWRITE_IP,
		SERV4_REWRITE_PORT,
		SERV4_IP,
		SUCCESS,
	},
	{
		"recvmsg6: rewrite IP & port (asm)",
		recvmsg6_rw_asm_prog_load,
		BPF_CGROUP_UDP6_RECVMSG,
		BPF_CGROUP_UDP6_RECVMSG,
		AF_INET6,
		SOCK_DGRAM,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SERV6_REWRITE_IP,
		SERV6_REWRITE_PORT,
		SERV6_IP,
		SUCCESS,
	},
};

static int mk_sockaddr(int domain, const char *ip, unsigned short port,
		       struct sockaddr *addr, socklen_t addr_len)
{
	struct sockaddr_in6 *addr6;
	struct sockaddr_in *addr4;

	if (domain != AF_INET && domain != AF_INET6) {
		log_err("Unsupported address family");
		return -1;
	}

	memset(addr, 0, addr_len);

	if (domain == AF_INET) {
		if (addr_len < sizeof(struct sockaddr_in))
			return -1;
		addr4 = (struct sockaddr_in *)addr;
		addr4->sin_family = domain;
		addr4->sin_port = htons(port);
		if (inet_pton(domain, ip, (void *)&addr4->sin_addr) != 1) {
			log_err("Invalid IPv4: %s", ip);
			return -1;
		}
	} else if (domain == AF_INET6) {
		if (addr_len < sizeof(struct sockaddr_in6))
			return -1;
		addr6 = (struct sockaddr_in6 *)addr;
		addr6->sin6_family = domain;
		addr6->sin6_port = htons(port);
		if (inet_pton(domain, ip, (void *)&addr6->sin6_addr) != 1) {
			log_err("Invalid IPv6: %s", ip);
			return -1;
		}
	}

	return 0;
}

static int load_insns(const struct sock_addr_test *test,
		      const struct bpf_insn *insns, size_t insns_cnt)
{
	struct bpf_load_program_attr load_attr;
	int ret;

	memset(&load_attr, 0, sizeof(struct bpf_load_program_attr));
	load_attr.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR;
	load_attr.expected_attach_type = test->expected_attach_type;
	load_attr.insns = insns;
	load_attr.insns_cnt = insns_cnt;
	load_attr.license = "GPL";

	ret = bpf_load_program_xattr(&load_attr, bpf_log_buf, BPF_LOG_BUF_SIZE);
	if (ret < 0 && test->expected_result != LOAD_REJECT) {
		log_err(">>> Loading program error.\n"
			">>> Verifier output:\n%s\n-------\n", bpf_log_buf);
	}

	return ret;
}

static int load_path(const struct sock_addr_test *test, const char *path)
{
	struct bpf_prog_load_attr attr;
	struct bpf_object *obj;
	int prog_fd;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = path;
	attr.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR;
	attr.expected_attach_type = test->expected_attach_type;
	attr.prog_flags = BPF_F_TEST_RND_HI32;

	if (bpf_prog_load_xattr(&attr, &obj, &prog_fd)) {
		if (test->expected_result != LOAD_REJECT)
			log_err(">>> Loading program (%s) error.\n", path);
		return -1;
	}

	return prog_fd;
}

static int bind4_prog_load(const struct sock_addr_test *test)
{
	return load_path(test, BIND4_PROG_PATH);
}

static int bind6_prog_load(const struct sock_addr_test *test)
{
	return load_path(test, BIND6_PROG_PATH);
}

static int connect4_prog_load(const struct sock_addr_test *test)
{
	return load_path(test, CONNECT4_PROG_PATH);
}

static int connect6_prog_load(const struct sock_addr_test *test)
{
	return load_path(test, CONNECT6_PROG_PATH);
}

static int xmsg_ret_only_prog_load(const struct sock_addr_test *test,
				   int32_t rc)
{
	struct bpf_insn insns[] = {
		/* return rc */
		BPF_MOV64_IMM(BPF_REG_0, rc),
		BPF_EXIT_INSN(),
	};
	return load_insns(test, insns, sizeof(insns) / sizeof(struct bpf_insn));
}

static int sendmsg_allow_prog_load(const struct sock_addr_test *test)
{
	return xmsg_ret_only_prog_load(test, /*rc*/ 1);
}

static int sendmsg_deny_prog_load(const struct sock_addr_test *test)
{
	return xmsg_ret_only_prog_load(test, /*rc*/ 0);
}

static int recvmsg_allow_prog_load(const struct sock_addr_test *test)
{
	return xmsg_ret_only_prog_load(test, /*rc*/ 1);
}

static int recvmsg_deny_prog_load(const struct sock_addr_test *test)
{
	return xmsg_ret_only_prog_load(test, /*rc*/ 0);
}

static int sendmsg4_rw_asm_prog_load(const struct sock_addr_test *test)
{
	struct sockaddr_in dst4_rw_addr;
	struct in_addr src4_rw_ip;

	if (inet_pton(AF_INET, SRC4_REWRITE_IP, (void *)&src4_rw_ip) != 1) {
		log_err("Invalid IPv4: %s", SRC4_REWRITE_IP);
		return -1;
	}

	if (mk_sockaddr(AF_INET, SERV4_REWRITE_IP, SERV4_REWRITE_PORT,
			(struct sockaddr *)&dst4_rw_addr,
			sizeof(dst4_rw_addr)) == -1)
		return -1;

	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

		/* if (sk.family == AF_INET && */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, family)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, AF_INET, 8),

		/*     sk.type == SOCK_DGRAM)  { */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, type)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, SOCK_DGRAM, 6),

		/*      msg_src_ip4 = src4_rw_ip */
		BPF_MOV32_IMM(BPF_REG_7, src4_rw_ip.s_addr),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, msg_src_ip4)),

		/*      user_ip4 = dst4_rw_addr.sin_addr */
		BPF_MOV32_IMM(BPF_REG_7, dst4_rw_addr.sin_addr.s_addr),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_ip4)),

		/*      user_port = dst4_rw_addr.sin_port */
		BPF_MOV32_IMM(BPF_REG_7, dst4_rw_addr.sin_port),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_port)),
		/* } */

		/* return 1 */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	return load_insns(test, insns, sizeof(insns) / sizeof(struct bpf_insn));
}

static int recvmsg4_rw_asm_prog_load(const struct sock_addr_test *test)
{
	struct sockaddr_in src4_rw_addr;

	if (mk_sockaddr(AF_INET, SERV4_IP, SERV4_PORT,
			(struct sockaddr *)&src4_rw_addr,
			sizeof(src4_rw_addr)) == -1)
		return -1;

	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

		/* if (sk.family == AF_INET && */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, family)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, AF_INET, 6),

		/*     sk.type == SOCK_DGRAM)  { */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, type)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, SOCK_DGRAM, 4),

		/*      user_ip4 = src4_rw_addr.sin_addr */
		BPF_MOV32_IMM(BPF_REG_7, src4_rw_addr.sin_addr.s_addr),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_ip4)),

		/*      user_port = src4_rw_addr.sin_port */
		BPF_MOV32_IMM(BPF_REG_7, src4_rw_addr.sin_port),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_port)),
		/* } */

		/* return 1 */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	return load_insns(test, insns, sizeof(insns) / sizeof(struct bpf_insn));
}

static int sendmsg4_rw_c_prog_load(const struct sock_addr_test *test)
{
	return load_path(test, SENDMSG4_PROG_PATH);
}

static int sendmsg6_rw_dst_asm_prog_load(const struct sock_addr_test *test,
					 const char *rw_dst_ip)
{
	struct sockaddr_in6 dst6_rw_addr;
	struct in6_addr src6_rw_ip;

	if (inet_pton(AF_INET6, SRC6_REWRITE_IP, (void *)&src6_rw_ip) != 1) {
		log_err("Invalid IPv6: %s", SRC6_REWRITE_IP);
		return -1;
	}

	if (mk_sockaddr(AF_INET6, rw_dst_ip, SERV6_REWRITE_PORT,
			(struct sockaddr *)&dst6_rw_addr,
			sizeof(dst6_rw_addr)) == -1)
		return -1;

	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

		/* if (sk.family == AF_INET6) { */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, family)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, AF_INET6, 18),

#define STORE_IPV6_WORD_N(DST, SRC, N)					       \
		BPF_MOV32_IMM(BPF_REG_7, SRC[N]),			       \
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,		       \
			    offsetof(struct bpf_sock_addr, DST[N]))

#define STORE_IPV6(DST, SRC)						       \
		STORE_IPV6_WORD_N(DST, SRC, 0),				       \
		STORE_IPV6_WORD_N(DST, SRC, 1),				       \
		STORE_IPV6_WORD_N(DST, SRC, 2),				       \
		STORE_IPV6_WORD_N(DST, SRC, 3)

		STORE_IPV6(msg_src_ip6, src6_rw_ip.s6_addr32),
		STORE_IPV6(user_ip6, dst6_rw_addr.sin6_addr.s6_addr32),

		/*      user_port = dst6_rw_addr.sin6_port */
		BPF_MOV32_IMM(BPF_REG_7, dst6_rw_addr.sin6_port),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_port)),

		/* } */

		/* return 1 */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	return load_insns(test, insns, sizeof(insns) / sizeof(struct bpf_insn));
}

static int sendmsg6_rw_asm_prog_load(const struct sock_addr_test *test)
{
	return sendmsg6_rw_dst_asm_prog_load(test, SERV6_REWRITE_IP);
}

static int recvmsg6_rw_asm_prog_load(const struct sock_addr_test *test)
{
	struct sockaddr_in6 src6_rw_addr;

	if (mk_sockaddr(AF_INET6, SERV6_IP, SERV6_PORT,
			(struct sockaddr *)&src6_rw_addr,
			sizeof(src6_rw_addr)) == -1)
		return -1;

	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

		/* if (sk.family == AF_INET6) { */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, family)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, AF_INET6, 10),

		STORE_IPV6(user_ip6, src6_rw_addr.sin6_addr.s6_addr32),

		/*      user_port = dst6_rw_addr.sin6_port */
		BPF_MOV32_IMM(BPF_REG_7, src6_rw_addr.sin6_port),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_port)),
		/* } */

		/* return 1 */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	return load_insns(test, insns, sizeof(insns) / sizeof(struct bpf_insn));
}

static int sendmsg6_rw_v4mapped_prog_load(const struct sock_addr_test *test)
{
	return sendmsg6_rw_dst_asm_prog_load(test, SERV6_V4MAPPED_IP);
}

static int sendmsg6_rw_wildcard_prog_load(const struct sock_addr_test *test)
{
	return sendmsg6_rw_dst_asm_prog_load(test, WILDCARD6_IP);
}

static int sendmsg6_rw_c_prog_load(const struct sock_addr_test *test)
{
	return load_path(test, SENDMSG6_PROG_PATH);
}

static int cmp_addr(const struct sockaddr_storage *addr1,
		    const struct sockaddr_storage *addr2, int cmp_port)
{
	const struct sockaddr_in *four1, *four2;
	const struct sockaddr_in6 *six1, *six2;

	if (addr1->ss_family != addr2->ss_family)
		return -1;

	if (addr1->ss_family == AF_INET) {
		four1 = (const struct sockaddr_in *)addr1;
		four2 = (const struct sockaddr_in *)addr2;
		return !((four1->sin_port == four2->sin_port || !cmp_port) &&
			 four1->sin_addr.s_addr == four2->sin_addr.s_addr);
	} else if (addr1->ss_family == AF_INET6) {
		six1 = (const struct sockaddr_in6 *)addr1;
		six2 = (const struct sockaddr_in6 *)addr2;
		return !((six1->sin6_port == six2->sin6_port || !cmp_port) &&
			 !memcmp(&six1->sin6_addr, &six2->sin6_addr,
				 sizeof(struct in6_addr)));
	}

	return -1;
}

static int cmp_sock_addr(info_fn fn, int sock1,
			 const struct sockaddr_storage *addr2, int cmp_port)
{
	struct sockaddr_storage addr1;
	socklen_t len1 = sizeof(addr1);

	memset(&addr1, 0, len1);
	if (fn(sock1, (struct sockaddr *)&addr1, (socklen_t *)&len1) != 0)
		return -1;

	return cmp_addr(&addr1, addr2, cmp_port);
}

static int cmp_local_ip(int sock1, const struct sockaddr_storage *addr2)
{
	return cmp_sock_addr(getsockname, sock1, addr2, /*cmp_port*/ 0);
}

static int cmp_local_addr(int sock1, const struct sockaddr_storage *addr2)
{
	return cmp_sock_addr(getsockname, sock1, addr2, /*cmp_port*/ 1);
}

static int cmp_peer_addr(int sock1, const struct sockaddr_storage *addr2)
{
	return cmp_sock_addr(getpeername, sock1, addr2, /*cmp_port*/ 1);
}

static int start_server(int type, const struct sockaddr_storage *addr,
			socklen_t addr_len)
{
	int fd;

	fd = socket(addr->ss_family, type, 0);
	if (fd == -1) {
		log_err("Failed to create server socket");
		goto out;
	}

	if (bind(fd, (const struct sockaddr *)addr, addr_len) == -1) {
		log_err("Failed to bind server socket");
		goto close_out;
	}

	if (type == SOCK_STREAM) {
		if (listen(fd, 128) == -1) {
			log_err("Failed to listen on server socket");
			goto close_out;
		}
	}

	goto out;
close_out:
	close(fd);
	fd = -1;
out:
	return fd;
}

static int connect_to_server(int type, const struct sockaddr_storage *addr,
			     socklen_t addr_len)
{
	int domain;
	int fd = -1;

	domain = addr->ss_family;

	if (domain != AF_INET && domain != AF_INET6) {
		log_err("Unsupported address family");
		goto err;
	}

	fd = socket(domain, type, 0);
	if (fd == -1) {
		log_err("Failed to create client socket");
		goto err;
	}

	if (connect(fd, (const struct sockaddr *)addr, addr_len) == -1) {
		log_err("Fail to connect to server");
		goto err;
	}

	goto out;
err:
	close(fd);
	fd = -1;
out:
	return fd;
}

int init_pktinfo(int domain, struct cmsghdr *cmsg)
{
	struct in6_pktinfo *pktinfo6;
	struct in_pktinfo *pktinfo4;

	if (domain == AF_INET) {
		cmsg->cmsg_level = SOL_IP;
		cmsg->cmsg_type = IP_PKTINFO;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
		pktinfo4 = (struct in_pktinfo *)CMSG_DATA(cmsg);
		memset(pktinfo4, 0, sizeof(struct in_pktinfo));
		if (inet_pton(domain, SRC4_IP,
			      (void *)&pktinfo4->ipi_spec_dst) != 1)
			return -1;
	} else if (domain == AF_INET6) {
		cmsg->cmsg_level = SOL_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		pktinfo6 = (struct in6_pktinfo *)CMSG_DATA(cmsg);
		memset(pktinfo6, 0, sizeof(struct in6_pktinfo));
		if (inet_pton(domain, SRC6_IP,
			      (void *)&pktinfo6->ipi6_addr) != 1)
			return -1;
	} else {
		return -1;
	}

	return 0;
}

static int sendmsg_to_server(int type, const struct sockaddr_storage *addr,
			     socklen_t addr_len, int set_cmsg, int flags,
			     int *syscall_err)
{
	union {
		char buf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
		struct cmsghdr align;
	} control6;
	union {
		char buf[CMSG_SPACE(sizeof(struct in_pktinfo))];
		struct cmsghdr align;
	} control4;
	struct msghdr hdr;
	struct iovec iov;
	char data = 'a';
	int domain;
	int fd = -1;

	domain = addr->ss_family;

	if (domain != AF_INET && domain != AF_INET6) {
		log_err("Unsupported address family");
		goto err;
	}

	fd = socket(domain, type, 0);
	if (fd == -1) {
		log_err("Failed to create client socket");
		goto err;
	}

	memset(&iov, 0, sizeof(iov));
	iov.iov_base = &data;
	iov.iov_len = sizeof(data);

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = (void *)addr;
	hdr.msg_namelen = addr_len;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;

	if (set_cmsg) {
		if (domain == AF_INET) {
			hdr.msg_control = &control4;
			hdr.msg_controllen = sizeof(control4.buf);
		} else if (domain == AF_INET6) {
			hdr.msg_control = &control6;
			hdr.msg_controllen = sizeof(control6.buf);
		}
		if (init_pktinfo(domain, CMSG_FIRSTHDR(&hdr))) {
			log_err("Fail to init pktinfo");
			goto err;
		}
	}

	if (sendmsg(fd, &hdr, flags) != sizeof(data)) {
		log_err("Fail to send message to server");
		*syscall_err = errno;
		goto err;
	}

	goto out;
err:
	close(fd);
	fd = -1;
out:
	return fd;
}

static int fastconnect_to_server(const struct sockaddr_storage *addr,
				 socklen_t addr_len)
{
	int sendmsg_err;

	return sendmsg_to_server(SOCK_STREAM, addr, addr_len, /*set_cmsg*/0,
				 MSG_FASTOPEN, &sendmsg_err);
}

static int recvmsg_from_client(int sockfd, struct sockaddr_storage *src_addr)
{
	struct timeval tv;
	struct msghdr hdr;
	struct iovec iov;
	char data[64];
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(sockfd, &rfds);

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	if (select(sockfd + 1, &rfds, NULL, NULL, &tv) <= 0 ||
	    !FD_ISSET(sockfd, &rfds))
		return -1;

	memset(&iov, 0, sizeof(iov));
	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = src_addr;
	hdr.msg_namelen = sizeof(struct sockaddr_storage);
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;

	return recvmsg(sockfd, &hdr, 0);
}

static int init_addrs(const struct sock_addr_test *test,
		      struct sockaddr_storage *requested_addr,
		      struct sockaddr_storage *expected_addr,
		      struct sockaddr_storage *expected_src_addr)
{
	socklen_t addr_len = sizeof(struct sockaddr_storage);

	if (mk_sockaddr(test->domain, test->expected_ip, test->expected_port,
			(struct sockaddr *)expected_addr, addr_len) == -1)
		goto err;

	if (mk_sockaddr(test->domain, test->requested_ip, test->requested_port,
			(struct sockaddr *)requested_addr, addr_len) == -1)
		goto err;

	if (test->expected_src_ip &&
	    mk_sockaddr(test->domain, test->expected_src_ip, 0,
			(struct sockaddr *)expected_src_addr, addr_len) == -1)
		goto err;

	return 0;
err:
	return -1;
}

static int run_bind_test_case(const struct sock_addr_test *test)
{
	socklen_t addr_len = sizeof(struct sockaddr_storage);
	struct sockaddr_storage requested_addr;
	struct sockaddr_storage expected_addr;
	int clientfd = -1;
	int servfd = -1;
	int err = 0;

	if (init_addrs(test, &requested_addr, &expected_addr, NULL))
		goto err;

	servfd = start_server(test->type, &requested_addr, addr_len);
	if (servfd == -1)
		goto err;

	if (cmp_local_addr(servfd, &expected_addr))
		goto err;

	/* Try to connect to server just in case */
	clientfd = connect_to_server(test->type, &expected_addr, addr_len);
	if (clientfd == -1)
		goto err;

	goto out;
err:
	err = -1;
out:
	close(clientfd);
	close(servfd);
	return err;
}

static int run_connect_test_case(const struct sock_addr_test *test)
{
	socklen_t addr_len = sizeof(struct sockaddr_storage);
	struct sockaddr_storage expected_src_addr;
	struct sockaddr_storage requested_addr;
	struct sockaddr_storage expected_addr;
	int clientfd = -1;
	int servfd = -1;
	int err = 0;

	if (init_addrs(test, &requested_addr, &expected_addr,
		       &expected_src_addr))
		goto err;

	/* Prepare server to connect to */
	servfd = start_server(test->type, &expected_addr, addr_len);
	if (servfd == -1)
		goto err;

	clientfd = connect_to_server(test->type, &requested_addr, addr_len);
	if (clientfd == -1)
		goto err;

	/* Make sure src and dst addrs were overridden properly */
	if (cmp_peer_addr(clientfd, &expected_addr))
		goto err;

	if (cmp_local_ip(clientfd, &expected_src_addr))
		goto err;

	if (test->type == SOCK_STREAM) {
		/* Test TCP Fast Open scenario */
		clientfd = fastconnect_to_server(&requested_addr, addr_len);
		if (clientfd == -1)
			goto err;

		/* Make sure src and dst addrs were overridden properly */
		if (cmp_peer_addr(clientfd, &expected_addr))
			goto err;

		if (cmp_local_ip(clientfd, &expected_src_addr))
			goto err;
	}

	goto out;
err:
	err = -1;
out:
	close(clientfd);
	close(servfd);
	return err;
}

static int run_xmsg_test_case(const struct sock_addr_test *test, int max_cmsg)
{
	socklen_t addr_len = sizeof(struct sockaddr_storage);
	struct sockaddr_storage expected_addr;
	struct sockaddr_storage server_addr;
	struct sockaddr_storage sendmsg_addr;
	struct sockaddr_storage recvmsg_addr;
	int clientfd = -1;
	int servfd = -1;
	int set_cmsg;
	int err = 0;

	if (test->type != SOCK_DGRAM)
		goto err;

	if (init_addrs(test, &sendmsg_addr, &server_addr, &expected_addr))
		goto err;

	/* Prepare server to sendmsg to */
	servfd = start_server(test->type, &server_addr, addr_len);
	if (servfd == -1)
		goto err;

	for (set_cmsg = 0; set_cmsg <= max_cmsg; ++set_cmsg) {
		if (clientfd >= 0)
			close(clientfd);

		clientfd = sendmsg_to_server(test->type, &sendmsg_addr,
					     addr_len, set_cmsg, /*flags*/0,
					     &err);
		if (err)
			goto out;
		else if (clientfd == -1)
			goto err;

		/* Try to receive message on server instead of using
		 * getpeername(2) on client socket, to check that client's
		 * destination address was rewritten properly, since
		 * getpeername(2) doesn't work with unconnected datagram
		 * sockets.
		 *
		 * Get source address from recvmsg(2) as well to make sure
		 * source was rewritten properly: getsockname(2) can't be used
		 * since socket is unconnected and source defined for one
		 * specific packet may differ from the one used by default and
		 * returned by getsockname(2).
		 */
		if (recvmsg_from_client(servfd, &recvmsg_addr) == -1)
			goto err;

		if (cmp_addr(&recvmsg_addr, &expected_addr, /*cmp_port*/0))
			goto err;
	}

	goto out;
err:
	err = -1;
out:
	close(clientfd);
	close(servfd);
	return err;
}

static int run_test_case(int cgfd, const struct sock_addr_test *test)
{
	int progfd = -1;
	int err = 0;

	printf("Test case: %s .. ", test->descr);

	progfd = test->loadfn(test);
	if (test->expected_result == LOAD_REJECT && progfd < 0)
		goto out;
	else if (test->expected_result == LOAD_REJECT || progfd < 0)
		goto err;

	err = bpf_prog_attach(progfd, cgfd, test->attach_type,
			      BPF_F_ALLOW_OVERRIDE);
	if (test->expected_result == ATTACH_REJECT && err) {
		err = 0; /* error was expected, reset it */
		goto out;
	} else if (test->expected_result == ATTACH_REJECT || err) {
		goto err;
	} else if (test->expected_result == ATTACH_OKAY) {
		err = 0;
		goto out;
	}

	switch (test->attach_type) {
	case BPF_CGROUP_INET4_BIND:
	case BPF_CGROUP_INET6_BIND:
		err = run_bind_test_case(test);
		break;
	case BPF_CGROUP_INET4_CONNECT:
	case BPF_CGROUP_INET6_CONNECT:
		err = run_connect_test_case(test);
		break;
	case BPF_CGROUP_UDP4_SENDMSG:
	case BPF_CGROUP_UDP6_SENDMSG:
		err = run_xmsg_test_case(test, 1);
		break;
	case BPF_CGROUP_UDP4_RECVMSG:
	case BPF_CGROUP_UDP6_RECVMSG:
		err = run_xmsg_test_case(test, 0);
		break;
	default:
		goto err;
	}

	if (test->expected_result == SYSCALL_EPERM && err == EPERM) {
		err = 0; /* error was expected, reset it */
		goto out;
	}

	if (test->expected_result == SYSCALL_ENOTSUPP && err == ENOTSUPP) {
		err = 0; /* error was expected, reset it */
		goto out;
	}

	if (err || test->expected_result != SUCCESS)
		goto err;

	goto out;
err:
	err = -1;
out:
	/* Detaching w/o checking return code: best effort attempt. */
	if (progfd != -1)
		bpf_prog_detach(cgfd, test->attach_type);
	close(progfd);
	printf("[%s]\n", err ? "FAIL" : "PASS");
	return err;
}

static int run_tests(int cgfd)
{
	int passes = 0;
	int fails = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		if (run_test_case(cgfd, &tests[i]))
			++fails;
		else
			++passes;
	}
	printf("Summary: %d PASSED, %d FAILED\n", passes, fails);
	return fails ? -1 : 0;
}

int main(int argc, char **argv)
{
	int cgfd = -1;
	int err = 0;

	if (argc < 2) {
		fprintf(stderr,
			"%s has to be run via %s.sh. Skip direct run.\n",
			argv[0], argv[0]);
		exit(err);
	}

	cgfd = cgroup_setup_and_join(CG_PATH);
	if (cgfd < 0)
		goto err;

	if (run_tests(cgfd))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	return err;
}
