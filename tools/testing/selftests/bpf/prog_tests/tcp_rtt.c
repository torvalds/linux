// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"

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

	fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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

static pthread_mutex_t server_started_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t server_started = PTHREAD_COND_INITIALIZER;
static volatile bool server_done = false;

static void *server_thread(void *arg)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd = *(int *)arg;
	int client_fd;
	int err;

	err = listen(fd, 1);

	pthread_mutex_lock(&server_started_mtx);
	pthread_cond_signal(&server_started);
	pthread_mutex_unlock(&server_started_mtx);

	if (CHECK_FAIL(err < 0)) {
		perror("Failed to listed on socket");
		return ERR_PTR(err);
	}

	while (true) {
		client_fd = accept(fd, (struct sockaddr *)&addr, &len);
		if (client_fd == -1 && errno == EAGAIN) {
			usleep(50);
			continue;
		}
		break;
	}
	if (CHECK_FAIL(client_fd < 0)) {
		perror("Failed to accept client");
		return ERR_PTR(err);
	}

	while (!server_done)
		usleep(50);

	close(client_fd);

	return NULL;
}

void test_tcp_rtt(void)
{
	int server_fd, cgroup_fd;
	pthread_t tid;
	void *server_res;

	cgroup_fd = test__join_cgroup("/tcp_rtt");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	server_fd = start_server();
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;

	if (CHECK_FAIL(pthread_create(&tid, NULL, server_thread,
				      (void *)&server_fd)))
		goto close_server_fd;

	pthread_mutex_lock(&server_started_mtx);
	pthread_cond_wait(&server_started, &server_started_mtx);
	pthread_mutex_unlock(&server_started_mtx);

	CHECK_FAIL(run_test(cgroup_fd, server_fd));

	server_done = true;
	CHECK_FAIL(pthread_join(tid, &server_res));
	CHECK_FAIL(IS_ERR(server_res));

close_server_fd:
	close(server_fd);
close_cgroup_fd:
	close(cgroup_fd);
}
