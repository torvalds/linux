// SPDX-License-Identifier: GPL-2.0

#include <error.h>
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
			      attach_type, BPF_F_ALLOW_MULTI);
	if (err) {
		log_err("Failed to attach %s BPF program", title);
		return -1;
	}

	return 0;
}

static int prog_detach(struct bpf_object *obj, int cgroup_fd, const char *title)
{
	enum bpf_attach_type attach_type;
	enum bpf_prog_type prog_type;
	struct bpf_program *prog;
	int err;

	err = libbpf_prog_type_by_name(title, &prog_type, &attach_type);
	if (err)
		return -1;

	prog = bpf_object__find_program_by_title(obj, title);
	if (!prog)
		return -1;

	err = bpf_prog_detach2(bpf_program__fd(prog), cgroup_fd,
			       attach_type);
	if (err)
		return -1;

	return 0;
}

static int run_getsockopt_test(struct bpf_object *obj, int cg_parent,
			       int cg_child, int sock_fd)
{
	socklen_t optlen;
	__u8 buf;
	int err;

	/* Set IP_TOS to the expected value (0x80). */

	buf = 0x80;
	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (err < 0) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0x80) {
		log_err("Unexpected getsockopt 0x%x != 0x80 without BPF", buf);
		err = -1;
		goto detach;
	}

	/* Attach child program and make sure it returns new value:
	 * - kernel:      -> 0x80
	 * - child:  0x80 -> 0x90
	 */

	err = prog_attach(obj, cg_child, "cgroup/getsockopt/child");
	if (err)
		goto detach;

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0x90) {
		log_err("Unexpected getsockopt 0x%x != 0x90", buf);
		err = -1;
		goto detach;
	}

	/* Attach parent program and make sure it returns new value:
	 * - kernel:      -> 0x80
	 * - child:  0x80 -> 0x90
	 * - parent: 0x90 -> 0xA0
	 */

	err = prog_attach(obj, cg_parent, "cgroup/getsockopt/parent");
	if (err)
		goto detach;

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0xA0) {
		log_err("Unexpected getsockopt 0x%x != 0xA0", buf);
		err = -1;
		goto detach;
	}

	/* Setting unexpected initial sockopt should return EPERM:
	 * - kernel: -> 0x40
	 * - child:  unexpected 0x40, EPERM
	 * - parent: unexpected 0x40, EPERM
	 */

	buf = 0x40;
	if (setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1) < 0) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!err) {
		log_err("Unexpected success from getsockopt(IP_TOS)");
		goto detach;
	}

	/* Detach child program and make sure we still get EPERM:
	 * - kernel: -> 0x40
	 * - parent: unexpected 0x40, EPERM
	 */

	err = prog_detach(obj, cg_child, "cgroup/getsockopt/child");
	if (err) {
		log_err("Failed to detach child program");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (!err) {
		log_err("Unexpected success from getsockopt(IP_TOS)");
		goto detach;
	}

	/* Set initial value to the one the parent program expects:
	 * - kernel:      -> 0x90
	 * - parent: 0x90 -> 0xA0
	 */

	buf = 0x90;
	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (err < 0) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0xA0) {
		log_err("Unexpected getsockopt 0x%x != 0xA0", buf);
		err = -1;
		goto detach;
	}

detach:
	prog_detach(obj, cg_child, "cgroup/getsockopt/child");
	prog_detach(obj, cg_parent, "cgroup/getsockopt/parent");

	return err;
}

static int run_setsockopt_test(struct bpf_object *obj, int cg_parent,
			       int cg_child, int sock_fd)
{
	socklen_t optlen;
	__u8 buf;
	int err;

	/* Set IP_TOS to the expected value (0x80). */

	buf = 0x80;
	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (err < 0) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0x80) {
		log_err("Unexpected getsockopt 0x%x != 0x80 without BPF", buf);
		err = -1;
		goto detach;
	}

	/* Attach child program and make sure it adds 0x10. */

	err = prog_attach(obj, cg_child, "cgroup/setsockopt");
	if (err)
		goto detach;

	buf = 0x80;
	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (err < 0) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0x80 + 0x10) {
		log_err("Unexpected getsockopt 0x%x != 0x80 + 0x10", buf);
		err = -1;
		goto detach;
	}

	/* Attach parent program and make sure it adds another 0x10. */

	err = prog_attach(obj, cg_parent, "cgroup/setsockopt");
	if (err)
		goto detach;

	buf = 0x80;
	err = setsockopt(sock_fd, SOL_IP, IP_TOS, &buf, 1);
	if (err < 0) {
		log_err("Failed to call setsockopt(IP_TOS)");
		goto detach;
	}

	buf = 0x00;
	optlen = 1;
	err = getsockopt(sock_fd, SOL_IP, IP_TOS, &buf, &optlen);
	if (err) {
		log_err("Failed to call getsockopt(IP_TOS)");
		goto detach;
	}

	if (buf != 0x80 + 2 * 0x10) {
		log_err("Unexpected getsockopt 0x%x != 0x80 + 2 * 0x10", buf);
		err = -1;
		goto detach;
	}

detach:
	prog_detach(obj, cg_child, "cgroup/setsockopt");
	prog_detach(obj, cg_parent, "cgroup/setsockopt");

	return err;
}

int main(int argc, char **argv)
{
	struct bpf_prog_load_attr attr = {
		.file = "./sockopt_multi.o",
	};
	int cg_parent = -1, cg_child = -1;
	struct bpf_object *obj = NULL;
	int sock_fd = -1;
	int err = -1;
	int ignored;

	if (setup_cgroup_environment()) {
		log_err("Failed to setup cgroup environment\n");
		goto out;
	}

	cg_parent = create_and_get_cgroup("/parent");
	if (cg_parent < 0) {
		log_err("Failed to create cgroup /parent\n");
		goto out;
	}

	cg_child = create_and_get_cgroup("/parent/child");
	if (cg_child < 0) {
		log_err("Failed to create cgroup /parent/child\n");
		goto out;
	}

	if (join_cgroup("/parent/child")) {
		log_err("Failed to join cgroup /parent/child\n");
		goto out;
	}

	err = bpf_prog_load_xattr(&attr, &obj, &ignored);
	if (err) {
		log_err("Failed to load BPF object");
		goto out;
	}

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		log_err("Failed to create socket");
		goto out;
	}

	if (run_getsockopt_test(obj, cg_parent, cg_child, sock_fd))
		err = -1;
	printf("test_sockopt_multi: getsockopt %s\n",
	       err ? "FAILED" : "PASSED");

	if (run_setsockopt_test(obj, cg_parent, cg_child, sock_fd))
		err = -1;
	printf("test_sockopt_multi: setsockopt %s\n",
	       err ? "FAILED" : "PASSED");

out:
	close(sock_fd);
	bpf_object__close(obj);
	close(cg_child);
	close(cg_parent);

	printf("test_sockopt_multi: %s\n", err ? "FAILED" : "PASSED");
	return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
