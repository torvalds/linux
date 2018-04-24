// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <linux/filter.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"

#define CG_PATH	"/foo"
#define CONNECT4_PROG_PATH	"./connect4_prog.o"
#define CONNECT6_PROG_PATH	"./connect6_prog.o"

#define SERV4_IP		"192.168.1.254"
#define SERV4_REWRITE_IP	"127.0.0.1"
#define SERV4_PORT		4040
#define SERV4_REWRITE_PORT	4444

#define SERV6_IP		"face:b00c:1234:5678::abcd"
#define SERV6_REWRITE_IP	"::1"
#define SERV6_PORT		6060
#define SERV6_REWRITE_PORT	6666

#define INET_NTOP_BUF	40

typedef int (*load_fn)(enum bpf_attach_type, const char *comment);
typedef int (*info_fn)(int, struct sockaddr *, socklen_t *);

struct program {
	enum bpf_attach_type type;
	load_fn	loadfn;
	int fd;
	const char *name;
	enum bpf_attach_type invalid_type;
};

char bpf_log_buf[BPF_LOG_BUF_SIZE];

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

static int load_insns(enum bpf_attach_type attach_type,
		      const struct bpf_insn *insns, size_t insns_cnt,
		      const char *comment)
{
	struct bpf_load_program_attr load_attr;
	int ret;

	memset(&load_attr, 0, sizeof(struct bpf_load_program_attr));
	load_attr.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR;
	load_attr.expected_attach_type = attach_type;
	load_attr.insns = insns;
	load_attr.insns_cnt = insns_cnt;
	load_attr.license = "GPL";

	ret = bpf_load_program_xattr(&load_attr, bpf_log_buf, BPF_LOG_BUF_SIZE);
	if (ret < 0 && comment) {
		log_err(">>> Loading %s program error.\n"
			">>> Output from verifier:\n%s\n-------\n",
			comment, bpf_log_buf);
	}

	return ret;
}

/* [1] These testing programs try to read different context fields, including
 * narrow loads of different sizes from user_ip4 and user_ip6, and write to
 * those allowed to be overridden.
 *
 * [2] BPF_LD_IMM64 & BPF_JMP_REG are used below whenever there is a need to
 * compare a register with unsigned 32bit integer. BPF_JMP_IMM can't be used
 * in such cases since it accepts only _signed_ 32bit integer as IMM
 * argument. Also note that BPF_LD_IMM64 contains 2 instructions what matters
 * to count jumps properly.
 */

static int bind4_prog_load(enum bpf_attach_type attach_type,
			   const char *comment)
{
	union {
		uint8_t u4_addr8[4];
		uint16_t u4_addr16[2];
		uint32_t u4_addr32;
	} ip4;
	struct sockaddr_in addr4_rw;

	if (inet_pton(AF_INET, SERV4_IP, (void *)&ip4) != 1) {
		log_err("Invalid IPv4: %s", SERV4_IP);
		return -1;
	}

	if (mk_sockaddr(AF_INET, SERV4_REWRITE_IP, SERV4_REWRITE_PORT,
			(struct sockaddr *)&addr4_rw, sizeof(addr4_rw)) == -1)
		return -1;

	/* See [1]. */
	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

		/* if (sk.family == AF_INET && */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, family)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, AF_INET, 16),

		/*     (sk.type == SOCK_DGRAM || sk.type == SOCK_STREAM) && */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, type)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, SOCK_DGRAM, 1),
		BPF_JMP_A(1),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, SOCK_STREAM, 12),

		/*     1st_byte_of_user_ip4 == expected && */
		BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, user_ip4)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, ip4.u4_addr8[0], 10),

		/*     1st_half_of_user_ip4 == expected && */
		BPF_LDX_MEM(BPF_H, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, user_ip4)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, ip4.u4_addr16[0], 8),

		/*     whole_user_ip4 == expected) { */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, user_ip4)),
		BPF_LD_IMM64(BPF_REG_8, ip4.u4_addr32), /* See [2]. */
		BPF_JMP_REG(BPF_JNE, BPF_REG_7, BPF_REG_8, 4),

		/*      user_ip4 = addr4_rw.sin_addr */
		BPF_MOV32_IMM(BPF_REG_7, addr4_rw.sin_addr.s_addr),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_ip4)),

		/*      user_port = addr4_rw.sin_port */
		BPF_MOV32_IMM(BPF_REG_7, addr4_rw.sin_port),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_port)),
		/* } */

		/* return 1 */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	return load_insns(attach_type, insns,
			  sizeof(insns) / sizeof(struct bpf_insn), comment);
}

static int bind6_prog_load(enum bpf_attach_type attach_type,
			   const char *comment)
{
	struct sockaddr_in6 addr6_rw;
	struct in6_addr ip6;

	if (inet_pton(AF_INET6, SERV6_IP, (void *)&ip6) != 1) {
		log_err("Invalid IPv6: %s", SERV6_IP);
		return -1;
	}

	if (mk_sockaddr(AF_INET6, SERV6_REWRITE_IP, SERV6_REWRITE_PORT,
			(struct sockaddr *)&addr6_rw, sizeof(addr6_rw)) == -1)
		return -1;

	/* See [1]. */
	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),

		/* if (sk.family == AF_INET6 && */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, family)),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, AF_INET6, 18),

		/*            5th_byte_of_user_ip6 == expected && */
		BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, user_ip6[1])),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, ip6.s6_addr[4], 16),

		/*            3rd_half_of_user_ip6 == expected && */
		BPF_LDX_MEM(BPF_H, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, user_ip6[1])),
		BPF_JMP_IMM(BPF_JNE, BPF_REG_7, ip6.s6_addr16[2], 14),

		/*            last_word_of_user_ip6 == expected) { */
		BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_6,
			    offsetof(struct bpf_sock_addr, user_ip6[3])),
		BPF_LD_IMM64(BPF_REG_8, ip6.s6_addr32[3]),  /* See [2]. */
		BPF_JMP_REG(BPF_JNE, BPF_REG_7, BPF_REG_8, 10),


#define STORE_IPV6_WORD(N)						       \
		BPF_MOV32_IMM(BPF_REG_7, addr6_rw.sin6_addr.s6_addr32[N]),     \
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,		       \
			    offsetof(struct bpf_sock_addr, user_ip6[N]))

		/*      user_ip6 = addr6_rw.sin6_addr */
		STORE_IPV6_WORD(0),
		STORE_IPV6_WORD(1),
		STORE_IPV6_WORD(2),
		STORE_IPV6_WORD(3),

		/*      user_port = addr6_rw.sin6_port */
		BPF_MOV32_IMM(BPF_REG_7, addr6_rw.sin6_port),
		BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_7,
			    offsetof(struct bpf_sock_addr, user_port)),

		/* } */

		/* return 1 */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	return load_insns(attach_type, insns,
			  sizeof(insns) / sizeof(struct bpf_insn), comment);
}

static int connect_prog_load_path(const char *path,
				  enum bpf_attach_type attach_type,
				  const char *comment)
{
	struct bpf_prog_load_attr attr;
	struct bpf_object *obj;
	int prog_fd;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = path;
	attr.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR;
	attr.expected_attach_type = attach_type;

	if (bpf_prog_load_xattr(&attr, &obj, &prog_fd)) {
		if (comment)
			log_err(">>> Loading %s program at %s error.\n",
				comment, path);
		return -1;
	}

	return prog_fd;
}

static int connect4_prog_load(enum bpf_attach_type attach_type,
			      const char *comment)
{
	return connect_prog_load_path(CONNECT4_PROG_PATH, attach_type, comment);
}

static int connect6_prog_load(enum bpf_attach_type attach_type,
			      const char *comment)
{
	return connect_prog_load_path(CONNECT6_PROG_PATH, attach_type, comment);
}

static void print_ip_port(int sockfd, info_fn fn, const char *fmt)
{
	char addr_buf[INET_NTOP_BUF];
	struct sockaddr_storage addr;
	struct sockaddr_in6 *addr6;
	struct sockaddr_in *addr4;
	socklen_t addr_len;
	unsigned short port;
	void *nip;

	addr_len = sizeof(struct sockaddr_storage);
	memset(&addr, 0, addr_len);

	if (fn(sockfd, (struct sockaddr *)&addr, (socklen_t *)&addr_len) == 0) {
		if (addr.ss_family == AF_INET) {
			addr4 = (struct sockaddr_in *)&addr;
			nip = (void *)&addr4->sin_addr;
			port = ntohs(addr4->sin_port);
		} else if (addr.ss_family == AF_INET6) {
			addr6 = (struct sockaddr_in6 *)&addr;
			nip = (void *)&addr6->sin6_addr;
			port = ntohs(addr6->sin6_port);
		} else {
			return;
		}
		const char *addr_str =
			inet_ntop(addr.ss_family, nip, addr_buf, INET_NTOP_BUF);
		printf(fmt, addr_str ? addr_str : "??", port);
	}
}

static void print_local_ip_port(int sockfd, const char *fmt)
{
	print_ip_port(sockfd, getsockname, fmt);
}

static void print_remote_ip_port(int sockfd, const char *fmt)
{
	print_ip_port(sockfd, getpeername, fmt);
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

	print_local_ip_port(fd, "\t   Actual: bind(%s, %d)\n");

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
	int fd;

	domain = addr->ss_family;

	if (domain != AF_INET && domain != AF_INET6) {
		log_err("Unsupported address family");
		return -1;
	}

	fd = socket(domain, type, 0);
	if (fd == -1) {
		log_err("Failed to creating client socket");
		return -1;
	}

	if (connect(fd, (const struct sockaddr *)addr, addr_len) == -1) {
		log_err("Fail to connect to server");
		goto err;
	}

	print_remote_ip_port(fd, "\t   Actual: connect(%s, %d)");
	print_local_ip_port(fd, " from (%s, %d)\n");

	return 0;
err:
	close(fd);
	return -1;
}

static void print_test_case_num(int domain, int type)
{
	static int test_num;

	printf("Test case #%d (%s/%s):\n", ++test_num,
	       (domain == AF_INET ? "IPv4" :
		domain == AF_INET6 ? "IPv6" :
		"unknown_domain"),
	       (type == SOCK_STREAM ? "TCP" :
		type == SOCK_DGRAM ? "UDP" :
		"unknown_type"));
}

static int run_test_case(int domain, int type, const char *ip,
			 unsigned short port)
{
	struct sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int servfd = -1;
	int err = 0;

	print_test_case_num(domain, type);

	if (mk_sockaddr(domain, ip, port, (struct sockaddr *)&addr,
			addr_len) == -1)
		return -1;

	printf("\tRequested: bind(%s, %d) ..\n", ip, port);
	servfd = start_server(type, &addr, addr_len);
	if (servfd == -1)
		goto err;

	printf("\tRequested: connect(%s, %d) from (*, *) ..\n", ip, port);
	if (connect_to_server(type, &addr, addr_len))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(servfd);
	return err;
}

static void close_progs_fds(struct program *progs, size_t prog_cnt)
{
	size_t i;

	for (i = 0; i < prog_cnt; ++i) {
		close(progs[i].fd);
		progs[i].fd = -1;
	}
}

static int load_and_attach_progs(int cgfd, struct program *progs,
				 size_t prog_cnt)
{
	size_t i;

	for (i = 0; i < prog_cnt; ++i) {
		printf("Load %s with invalid type (can pollute stderr) ",
		       progs[i].name);
		fflush(stdout);
		progs[i].fd = progs[i].loadfn(progs[i].invalid_type, NULL);
		if (progs[i].fd != -1) {
			log_err("Load with invalid type accepted for %s",
				progs[i].name);
			goto err;
		}
		printf("... REJECTED\n");

		printf("Load %s with valid type", progs[i].name);
		progs[i].fd = progs[i].loadfn(progs[i].type, progs[i].name);
		if (progs[i].fd == -1) {
			log_err("Failed to load program %s", progs[i].name);
			goto err;
		}
		printf(" ... OK\n");

		printf("Attach %s with invalid type", progs[i].name);
		if (bpf_prog_attach(progs[i].fd, cgfd, progs[i].invalid_type,
				    BPF_F_ALLOW_OVERRIDE) != -1) {
			log_err("Attach with invalid type accepted for %s",
				progs[i].name);
			goto err;
		}
		printf(" ... REJECTED\n");

		printf("Attach %s with valid type", progs[i].name);
		if (bpf_prog_attach(progs[i].fd, cgfd, progs[i].type,
				    BPF_F_ALLOW_OVERRIDE) == -1) {
			log_err("Failed to attach program %s", progs[i].name);
			goto err;
		}
		printf(" ... OK\n");
	}

	return 0;
err:
	close_progs_fds(progs, prog_cnt);
	return -1;
}

static int run_domain_test(int domain, int cgfd, struct program *progs,
			   size_t prog_cnt, const char *ip, unsigned short port)
{
	int err = 0;

	if (load_and_attach_progs(cgfd, progs, prog_cnt) == -1)
		goto err;

	if (run_test_case(domain, SOCK_STREAM, ip, port) == -1)
		goto err;

	if (run_test_case(domain, SOCK_DGRAM, ip, port) == -1)
		goto err;

	goto out;
err:
	err = -1;
out:
	close_progs_fds(progs, prog_cnt);
	return err;
}

static int run_test(void)
{
	size_t inet6_prog_cnt;
	size_t inet_prog_cnt;
	int cgfd = -1;
	int err = 0;

	struct program inet6_progs[] = {
		{BPF_CGROUP_INET6_BIND, bind6_prog_load, -1, "bind6",
		 BPF_CGROUP_INET4_BIND},
		{BPF_CGROUP_INET6_CONNECT, connect6_prog_load, -1, "connect6",
		 BPF_CGROUP_INET4_CONNECT},
	};
	inet6_prog_cnt = sizeof(inet6_progs) / sizeof(struct program);

	struct program inet_progs[] = {
		{BPF_CGROUP_INET4_BIND, bind4_prog_load, -1, "bind4",
		 BPF_CGROUP_INET6_BIND},
		{BPF_CGROUP_INET4_CONNECT, connect4_prog_load, -1, "connect4",
		 BPF_CGROUP_INET6_CONNECT},
	};
	inet_prog_cnt = sizeof(inet_progs) / sizeof(struct program);

	if (setup_cgroup_environment())
		goto err;

	cgfd = create_and_get_cgroup(CG_PATH);
	if (!cgfd)
		goto err;

	if (join_cgroup(CG_PATH))
		goto err;

	if (run_domain_test(AF_INET, cgfd, inet_progs, inet_prog_cnt, SERV4_IP,
			    SERV4_PORT) == -1)
		goto err;

	if (run_domain_test(AF_INET6, cgfd, inet6_progs, inet6_prog_cnt,
			    SERV6_IP, SERV6_PORT) == -1)
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	printf(err ? "### FAIL\n" : "### SUCCESS\n");
	return err;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr,
			"%s has to be run via %s.sh. Skip direct run.\n",
			argv[0], argv[0]);
		exit(0);
	}
	return run_test();
}
