// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include <linux/filter.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#define CG_PATH                                "/tcp_rtt"

struct tcp_rtt_storage {
	__u32 invoked;
	__u32 dsack_dups;
	__u32 delivered;
	__u32 delivered_ce;
	__u32 icsk_retransmits;
};

static void send_byte(int fd)
{
	char b = 0x55;

	if (write(fd, &b, sizeof(b)) != 1)
		error(1, errno, "Failed to send single byte");
}

static int verify_sk(int map_fd, int client_fd, const char *msg, __u32 invoked,
		     __u32 dsack_dups, __u32 delivered, __u32 delivered_ce,
		     __u32 icsk_retransmits)
{
	int err = 0;
	struct tcp_rtt_storage val;

	if (bpf_map_lookup_elem(map_fd, &client_fd, &val) < 0)
		error(1, errno, "Failed to read socket storage");

	if (val.invoked != invoked) {
		log_err("%s: unexpected bpf_tcp_sock.invoked %d != %d",
			msg, val.invoked, invoked);
		err++;
	}

	if (val.dsack_dups != dsack_dups) {
		log_err("%s: unexpected bpf_tcp_sock.dsack_dups %d != %d",
			msg, val.dsack_dups, dsack_dups);
		err++;
	}

	if (val.delivered != delivered) {
		log_err("%s: unexpected bpf_tcp_sock.delivered %d != %d",
			msg, val.delivered, delivered);
		err++;
	}

	if (val.delivered_ce != delivered_ce) {
		log_err("%s: unexpected bpf_tcp_sock.delivered_ce %d != %d",
			msg, val.delivered_ce, delivered_ce);
		err++;
	}

	if (val.icsk_retransmits != icsk_retransmits) {
		log_err("%s: unexpected bpf_tcp_sock.icsk_retransmits %d != %d",
			msg, val.icsk_retransmits, icsk_retransmits);
		err++;
	}

	return err;
}

static int connect_to_server(int server_fd)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		goto out;
	}

	if (connect(fd, (const struct sockaddr *)&addr, len) < 0) {
		log_err("Fail to connect to server");
		goto out;
	}

	return fd;

out:
	close(fd);
	return -1;
}

static int run_test(int cgroup_fd, int server_fd)
{
	struct bpf_prog_load_attr attr = {
		.prog_type = BPF_PROG_TYPE_SOCK_OPS,
		.file = "./tcp_rtt.o",
		.expected_attach_type = BPF_CGROUP_SOCK_OPS,
	};
	struct bpf_object *obj;
	struct bpf_map *map;
	int client_fd;
	int prog_fd;
	int map_fd;
	int err;

	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	if (err) {
		log_err("Failed to load BPF object");
		return -1;
	}

	map = bpf_map__next(NULL, obj);
	map_fd = bpf_map__fd(map);

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (err) {
		log_err("Failed to attach BPF program");
		goto close_bpf_object;
	}

	client_fd = connect_to_server(server_fd);
	if (client_fd < 0) {
		err = -1;
		goto close_bpf_object;
	}

	err += verify_sk(map_fd, client_fd, "syn-ack",
			 /*invoked=*/1,
			 /*dsack_dups=*/0,
			 /*delivered=*/1,
			 /*delivered_ce=*/0,
			 /*icsk_retransmits=*/0);

	send_byte(client_fd);

	err += verify_sk(map_fd, client_fd, "first payload byte",
			 /*invoked=*/2,
			 /*dsack_dups=*/0,
			 /*delivered=*/2,
			 /*delivered_ce=*/0,
			 /*icsk_retransmits=*/0);

	close(client_fd);

close_bpf_object:
	bpf_object__close(obj);
	return err;
}

static int start_server(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create server socket");
		return -1;
	}

	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_err("Failed to bind socket");
		close(fd);
		return -1;
	}

	return fd;
}

static void *server_thread(void *arg)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd = *(int *)arg;
	int client_fd;

	if (listen(fd, 1) < 0)
		error(1, errno, "Failed to listed on socket");

	client_fd = accept(fd, (struct sockaddr *)&addr, &len);
	if (client_fd < 0)
		error(1, errno, "Failed to accept client");

	/* Wait for the next connection (that never arrives)
	 * to keep this thread alive to prevent calling
	 * close() on client_fd.
	 */
	if (accept(fd, (struct sockaddr *)&addr, &len) >= 0)
		error(1, errno, "Unexpected success in second accept");

	close(client_fd);

	return NULL;
}

int main(int args, char **argv)
{
	int server_fd, cgroup_fd;
	int err = EXIT_SUCCESS;
	pthread_t tid;

	if (setup_cgroup_environment())
		goto cleanup_obj;

	cgroup_fd = create_and_get_cgroup(CG_PATH);
	if (cgroup_fd < 0)
		goto cleanup_cgroup_env;

	if (join_cgroup(CG_PATH))
		goto cleanup_cgroup;

	server_fd = start_server();
	if (server_fd < 0) {
		err = EXIT_FAILURE;
		goto cleanup_cgroup;
	}

	pthread_create(&tid, NULL, server_thread, (void *)&server_fd);

	if (run_test(cgroup_fd, server_fd))
		err = EXIT_FAILURE;

	close(server_fd);

	printf("test_sockopt_sk: %s\n",
	       err == EXIT_SUCCESS ? "PASSED" : "FAILED");

cleanup_cgroup:
	close(cgroup_fd);
cleanup_cgroup_env:
	cleanup_cgroup_environment();
cleanup_obj:
	return err;
}
