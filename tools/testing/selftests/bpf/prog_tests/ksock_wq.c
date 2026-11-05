// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include <unistd.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "ksock_wq.skel.h"

#define CALLBACK_WAIT_RETRIES 1000
#define CALLBACK_WAIT_US 1000

void test_ksock_wq(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct ksock_wq *skel;
	char recv_data[sizeof(skel->data->send_data)];
	u32 callback_done;
	int err, i, port, rfd;

	skel = ksock_wq__open_and_load();
	if (!ASSERT_OK_PTR(skel, "ksock_wq open and load"))
		return;

	rfd = start_server(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1000);
	if (!ASSERT_OK_FD(rfd, "receiver socket"))
		goto out;
	port = get_socket_local_port(rfd);
	if (!ASSERT_GE(port, 0, "receiver port"))
		goto out;
	skel->bss->remote_port = port;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.ksock_wq_start),
				     &opts);
	if (!ASSERT_OK(err, "run ksock_wq_start"))
		goto out;
	if (!ASSERT_OK(opts.retval, "ksock_wq_start retval"))
		goto out;

	for (i = 0; i < CALLBACK_WAIT_RETRIES; i++) {
		if (__atomic_load_n(&skel->bss->callback_done, __ATOMIC_ACQUIRE) == 2)
			break;
		usleep(CALLBACK_WAIT_US);
	}
	callback_done = __atomic_load_n(&skel->bss->callback_done,
					__ATOMIC_ACQUIRE);
	if (!ASSERT_EQ(callback_done, 2, "workqueue callbacks completed"))
		goto out;

	ASSERT_EQ(skel->bss->create_ret, -EOPNOTSUPP, "workqueue create rejected");

	if (!ASSERT_EQ(skel->bss->connect_send_ret, sizeof(recv_data),
		       "workqueue send bytes"))
		goto out;
	err = recv(rfd, recv_data, sizeof(recv_data), 0);
	if (ASSERT_EQ(err, sizeof(recv_data), "recv len"))
		ASSERT_MEMEQ(recv_data, skel->data->send_data, sizeof(recv_data),
			     "payload match");

out:
	if (rfd >= 0)
		close(rfd);
	ksock_wq__destroy(skel);
}
