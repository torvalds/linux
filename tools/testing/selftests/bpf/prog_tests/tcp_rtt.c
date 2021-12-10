// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "tcp_rtt.skel.h"

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

	if (CHECK_FAIL(write(fd, &b, sizeof(b)) != 1))
		perror("Failed to send single byte");
}

static int wait_for_ack(int fd, int retries)
{
	struct tcp_info info;
	socklen_t optlen;
	int i, err;

	for (i = 0; i < retries; i++) {
		optlen = sizeof(info);
		err = getsockopt(fd, SOL_TCP, TCP_INFO, &info, &optlen);
		if (err < 0) {
			log_err("Failed to lookup TCP stats");
			return err;
		}

		if (info.tcpi_unacked == 0)
			return 0;

		usleep(10);
	}

	log_err("Did not receive ACK");
	return -1;
}

static int verify_sk(int map_fd, int client_fd, const char *msg, __u32 invoked,
		     __u32 dsack_dups, __u32 delivered, __u32 delivered_ce,
		     __u32 icsk_retransmits)
{
	int err = 0;
	struct tcp_rtt_storage val;

	if (CHECK_FAIL(bpf_map_lookup_elem(map_fd, &client_fd, &val) < 0)) {
		perror("Failed to read socket storage");
		return -1;
	}

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


static int run_test(int cgroup_fd, int server_fd)
{
	struct tcp_rtt *skel;
	int client_fd;
	int prog_fd;
	int map_fd;
	int err;

	skel = tcp_rtt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_load"))
		return -1;

	map_fd = bpf_map__fd(skel->maps.socket_storage_map);
	prog_fd = bpf_program__fd(skel->progs._sockops);

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (err) {
		log_err("Failed to attach BPF program");
		goto close_bpf_object;
	}

	client_fd = connect_to_fd(server_fd, 0);
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
	if (wait_for_ack(client_fd, 100) < 0) {
		err = -1;
		goto close_client_fd;
	}


	err += verify_sk(map_fd, client_fd, "first payload byte",
			 /*invoked=*/2,
			 /*dsack_dups=*/0,
			 /*delivered=*/2,
			 /*delivered_ce=*/0,
			 /*icsk_retransmits=*/0);

close_client_fd:
	close(client_fd);

close_bpf_object:
	tcp_rtt__destroy(skel);
	return err;
}

void test_tcp_rtt(void)
{
	int server_fd, cgroup_fd;

	cgroup_fd = test__join_cgroup("/tcp_rtt");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;

	CHECK_FAIL(run_test(cgroup_fd, server_fd));

	close(server_fd);

close_cgroup_fd:
	close(cgroup_fd);
}
