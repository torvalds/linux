// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <linux/filter.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#define CG_PATH				"/sockopt"

#define SOL_CUSTOM			0xdeadbeef

static int getsetsockopt(void)
{
	int fd, err;
	char buf[4] = {};
	socklen_t optlen;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create socket");
		return -1;
	}

	/* IP_TOS - BPF bypass */

	buf[0] = 0x08;
	err = setsockopt(fd, SOL_IP, IP_TOS, buf, 1);
	if (err) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto err;
	}

	buf[0] = 0x00;
	optlen = 1;
	err = getsockopt(fd, SOL_IP, IP_TOS, buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto err;
	}

	if (buf[0] != 0x08) {
		log_err("Unexpected getsockopt(IP_TOS) buf[0] 0x%02x != 0x08",
			buf[0]);
		goto err;
	}

	/* IP_TTL - EPERM */

	buf[0] = 1;
	err = setsockopt(fd, SOL_IP, IP_TTL, buf, 1);
	if (!err || errno != EPERM) {
		log_err("Unexpected success from setsockopt(IP_TTL)");
		goto err;
	}

	/* SOL_CUSTOM - handled by BPF */

	buf[0] = 0x01;
	err = setsockopt(fd, SOL_CUSTOM, 0, buf, 1);
	if (err) {
		log_err("Failed to call setsockopt");
		goto err;
	}

	buf[0] = 0x00;
	optlen = 4;
	err = getsockopt(fd, SOL_CUSTOM, 0, buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt");
		goto err;
	}

	if (optlen != 1) {
		log_err("Unexpected optlen %d != 1", optlen);
		goto err;
	}
	if (buf[0] != 0x01) {
		log_err("Unexpected buf[0] 0x%02x != 0x01", buf[0]);
		goto err;
	}

	/* SO_SNDBUF is overwritten */

	buf[0] = 0x01;
	buf[1] = 0x01;
	buf[2] = 0x01;
	buf[3] = 0x01;
	err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, buf, 4);
	if (err) {
		log_err("Failed to call setsockopt(SO_SNDBUF)");
		goto err;
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	optlen = 4;
	err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(SO_SNDBUF)");
		goto err;
	}

	if (*(__u32 *)buf != 0x55AA*2) {
		log_err("Unexpected getsockopt(SO_SNDBUF) 0x%x != 0x55AA*2",
			*(__u32 *)buf);
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

static int run_test(int cgroup_fd)
{
	struct bpf_prog_load_attr attr = {
		.file = "./sockopt_sk.o",
	};
	struct bpf_object *obj;
	int ignored;
	int err;

	err = bpf_prog_load_xattr(&attr, &obj, &ignored);
	if (err) {
		log_err("Failed to load BPF object");
		return -1;
	}

	err = prog_attach(obj, cgroup_fd, "cgroup/getsockopt");
	if (err)
		goto close_bpf_object;

	err = prog_attach(obj, cgroup_fd, "cgroup/setsockopt");
	if (err)
		goto close_bpf_object;

	err = getsetsockopt();

close_bpf_object:
	bpf_object__close(obj);
	return err;
}

int main(int args, char **argv)
{
	int cgroup_fd;
	int err = EXIT_SUCCESS;

	if (setup_cgroup_environment())
		goto cleanup_obj;

	cgroup_fd = create_and_get_cgroup(CG_PATH);
	if (cgroup_fd < 0)
		goto cleanup_cgroup_env;

	if (join_cgroup(CG_PATH))
		goto cleanup_cgroup;

	if (run_test(cgroup_fd))
		err = EXIT_FAILURE;

	printf("test_sockopt_sk: %s\n",
	       err == EXIT_SUCCESS ? "PASSED" : "FAILED");

cleanup_cgroup:
	close(cgroup_fd);
cleanup_cgroup_env:
	cleanup_cgroup_environment();
cleanup_obj:
	return err;
}
