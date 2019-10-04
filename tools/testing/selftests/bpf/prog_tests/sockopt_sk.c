// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"

#define SOL_CUSTOM			0xdeadbeef

static int getsetsockopt(void)
{
	int fd, err;
	union {
		char u8[4];
		__u32 u32;
		char cc[16]; /* TCP_CA_NAME_MAX */
	} buf = {};
	socklen_t optlen;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create socket");
		return -1;
	}

	/* IP_TOS - BPF bypass */

	buf.u8[0] = 0x08;
	err = setsockopt(fd, SOL_IP, IP_TOS, &buf, 1);
	if (err) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto err;
	}

	buf.u8[0] = 0x00;
	optlen = 1;
	err = getsockopt(fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto err;
	}

	if (buf.u8[0] != 0x08) {
		log_err("Unexpected getsockopt(IP_TOS) buf[0] 0x%02x != 0x08",
			buf.u8[0]);
		goto err;
	}

	/* IP_TTL - EPERM */

	buf.u8[0] = 1;
	err = setsockopt(fd, SOL_IP, IP_TTL, &buf, 1);
	if (!err || errno != EPERM) {
		log_err("Unexpected success from setsockopt(IP_TTL)");
		goto err;
	}

	/* SOL_CUSTOM - handled by BPF */

	buf.u8[0] = 0x01;
	err = setsockopt(fd, SOL_CUSTOM, 0, &buf, 1);
	if (err) {
		log_err("Failed to call setsockopt");
		goto err;
	}

	buf.u32 = 0x00;
	optlen = 4;
	err = getsockopt(fd, SOL_CUSTOM, 0, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt");
		goto err;
	}

	if (optlen != 1) {
		log_err("Unexpected optlen %d != 1", optlen);
		goto err;
	}
	if (buf.u8[0] != 0x01) {
		log_err("Unexpected buf[0] 0x%02x != 0x01", buf.u8[0]);
		goto err;
	}

	/* SO_SNDBUF is overwritten */

	buf.u32 = 0x01010101;
	err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, 4);
	if (err) {
		log_err("Failed to call setsockopt(SO_SNDBUF)");
		goto err;
	}

	buf.u32 = 0x00;
	optlen = 4;
	err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(SO_SNDBUF)");
		goto err;
	}

	if (buf.u32 != 0x55AA*2) {
		log_err("Unexpected getsockopt(SO_SNDBUF) 0x%x != 0x55AA*2",
			buf.u32);
		goto err;
	}

	/* TCP_CONGESTION can extend the string */

	strcpy(buf.cc, "nv");
	err = setsockopt(fd, SOL_TCP, TCP_CONGESTION, &buf, strlen("nv"));
	if (err) {
		log_err("Failed to call setsockopt(TCP_CONGESTION)");
		goto err;
	}


	optlen = sizeof(buf.cc);
	err = getsockopt(fd, SOL_TCP, TCP_CONGESTION, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(TCP_CONGESTION)");
		goto err;
	}

	if (strcmp(buf.cc, "cubic") != 0) {
		log_err("Unexpected getsockopt(TCP_CONGESTION) %s != %s",
			buf.cc, "cubic");
		goto err;
	}

	close(fd);
	return 0;
err:
	close(fd);
	return -1;
}

static int prog_attach(struct bpf_object *obj, int cgroup_fd, const char *title)
{
	enum bpf_attach_type attach_type;
	enum bpf_prog_type prog_type;
	struct bpf_program *prog;
	int err;

	err = libbpf_prog_type_by_name(title, &prog_type, &attach_type);
	if (err) {
		log_err("Failed to deduct types for %s BPF program", title);
		return -1;
	}

	prog = bpf_object__find_program_by_title(obj, title);
	if (!prog) {
		log_err("Failed to find %s BPF program", title);
		return -1;
	}

	err = bpf_prog_attach(bpf_program__fd(prog), cgroup_fd,
			      attach_type, 0);
	if (err) {
		log_err("Failed to attach %s BPF program", title);
		return -1;
	}

	return 0;
}

static void run_test(int cgroup_fd)
{
	struct bpf_prog_load_attr attr = {
		.file = "./sockopt_sk.o",
	};
	struct bpf_object *obj;
	int ignored;
	int err;

	err = bpf_prog_load_xattr(&attr, &obj, &ignored);
	if (CHECK_FAIL(err))
		return;

	err = prog_attach(obj, cgroup_fd, "cgroup/getsockopt");
	if (CHECK_FAIL(err))
		goto close_bpf_object;

	err = prog_attach(obj, cgroup_fd, "cgroup/setsockopt");
	if (CHECK_FAIL(err))
		goto close_bpf_object;

	CHECK_FAIL(getsetsockopt());

close_bpf_object:
	bpf_object__close(obj);
}

void test_sockopt_sk(void)
{
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/sockopt_sk");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	run_test(cgroup_fd);
	close(cgroup_fd);
}
