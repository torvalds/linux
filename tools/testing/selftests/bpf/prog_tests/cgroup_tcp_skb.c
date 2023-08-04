// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Facebook */
#include <test_progs.h>
#include <linux/in6.h>
#include <sys/socket.h>
#include <sched.h>
#include <unistd.h>
#include "cgroup_helpers.h"
#include "testing_helpers.h"
#include "cgroup_tcp_skb.skel.h"
#include "cgroup_tcp_skb.h"

#define CGROUP_TCP_SKB_PATH "/test_cgroup_tcp_skb"

static int install_filters(int cgroup_fd,
			   struct bpf_link **egress_link,
			   struct bpf_link **ingress_link,
			   struct bpf_program *egress_prog,
			   struct bpf_program *ingress_prog,
			   struct cgroup_tcp_skb *skel)
{
	/* Prepare filters */
	skel->bss->g_sock_state = 0;
	skel->bss->g_unexpected = 0;
	*egress_link =
		bpf_program__attach_cgroup(egress_prog,
					   cgroup_fd);
	if (!ASSERT_OK_PTR(egress_link, "egress_link"))
		return -1;
	*ingress_link =
		bpf_program__attach_cgroup(ingress_prog,
					   cgroup_fd);
	if (!ASSERT_OK_PTR(ingress_link, "ingress_link"))
		return -1;

	return 0;
}

static void uninstall_filters(struct bpf_link **egress_link,
			      struct bpf_link **ingress_link)
{
	bpf_link__destroy(*egress_link);
	*egress_link = NULL;
	bpf_link__destroy(*ingress_link);
	*ingress_link = NULL;
}

static int create_client_sock_v6(void)
{
	int fd;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	return fd;
}

static int create_server_sock_v6(void)
{
	struct sockaddr_in6 addr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(0),
		.sin6_addr = IN6ADDR_LOOPBACK_INIT,
	};
	int fd, err;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	err = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0) {
		perror("bind");
		return -1;
	}

	err = listen(fd, 1);
	if (err < 0) {
		perror("listen");
		return -1;
	}

	return fd;
}

static int get_sock_port_v6(int fd)
{
	struct sockaddr_in6 addr;
	socklen_t len;
	int err;

	len = sizeof(addr);
	err = getsockname(fd, (struct sockaddr *)&addr, &len);
	if (err < 0) {
		perror("getsockname");
		return -1;
	}

	return ntohs(addr.sin6_port);
}

static int connect_client_server_v6(int client_fd, int listen_fd)
{
	struct sockaddr_in6 addr = {
		.sin6_family = AF_INET6,
		.sin6_addr = IN6ADDR_LOOPBACK_INIT,
	};
	int err, port;

	port = get_sock_port_v6(listen_fd);
	if (port < 0)
		return -1;
	addr.sin6_port = htons(port);

	err = connect(client_fd, (struct sockaddr *)&addr, sizeof(addr));
	if (err < 0) {
		perror("connect");
		return -1;
	}

	return 0;
}

/* Connect to the server in a cgroup from the outside of the cgroup. */
static int talk_to_cgroup(int *client_fd, int *listen_fd, int *service_fd,
			  struct cgroup_tcp_skb *skel)
{
	int err, cp;
	char buf[5];

	/* Create client & server socket */
	err = join_root_cgroup();
	if (!ASSERT_OK(err, "join_root_cgroup"))
		return -1;
	*client_fd = create_client_sock_v6();
	if (!ASSERT_GE(*client_fd, 0, "client_fd"))
		return -1;
	err = join_cgroup(CGROUP_TCP_SKB_PATH);
	if (!ASSERT_OK(err, "join_cgroup"))
		return -1;
	*listen_fd = create_server_sock_v6();
	if (!ASSERT_GE(*listen_fd, 0, "listen_fd"))
		return -1;
	skel->bss->g_sock_port = get_sock_port_v6(*listen_fd);

	/* Connect client to server */
	err = connect_client_server_v6(*client_fd, *listen_fd);
	if (!ASSERT_OK(err, "connect_client_server_v6"))
		return -1;
	*service_fd = accept(*listen_fd, NULL, NULL);
	if (!ASSERT_GE(*service_fd, 0, "service_fd"))
		return -1;
	err = join_root_cgroup();
	if (!ASSERT_OK(err, "join_root_cgroup"))
		return -1;
	cp = write(*client_fd, "hello", 5);
	if (!ASSERT_EQ(cp, 5, "write"))
		return -1;
	cp = read(*service_fd, buf, 5);
	if (!ASSERT_EQ(cp, 5, "read"))
		return -1;

	return 0;
}

/* Connect to the server out of a cgroup from inside the cgroup. */
static int talk_to_outside(int *client_fd, int *listen_fd, int *service_fd,
			   struct cgroup_tcp_skb *skel)

{
	int err, cp;
	char buf[5];

	/* Create client & server socket */
	err = join_root_cgroup();
	if (!ASSERT_OK(err, "join_root_cgroup"))
		return -1;
	*listen_fd = create_server_sock_v6();
	if (!ASSERT_GE(*listen_fd, 0, "listen_fd"))
		return -1;
	err = join_cgroup(CGROUP_TCP_SKB_PATH);
	if (!ASSERT_OK(err, "join_cgroup"))
		return -1;
	*client_fd = create_client_sock_v6();
	if (!ASSERT_GE(*client_fd, 0, "client_fd"))
		return -1;
	err = join_root_cgroup();
	if (!ASSERT_OK(err, "join_root_cgroup"))
		return -1;
	skel->bss->g_sock_port = get_sock_port_v6(*listen_fd);

	/* Connect client to server */
	err = connect_client_server_v6(*client_fd, *listen_fd);
	if (!ASSERT_OK(err, "connect_client_server_v6"))
		return -1;
	*service_fd = accept(*listen_fd, NULL, NULL);
	if (!ASSERT_GE(*service_fd, 0, "service_fd"))
		return -1;
	cp = write(*client_fd, "hello", 5);
	if (!ASSERT_EQ(cp, 5, "write"))
		return -1;
	cp = read(*service_fd, buf, 5);
	if (!ASSERT_EQ(cp, 5, "read"))
		return -1;

	return 0;
}

static int close_connection(int *closing_fd, int *peer_fd, int *listen_fd,
			    struct cgroup_tcp_skb *skel)
{
	__u32 saved_packet_count = 0;
	int err;
	int i;

	/* Wait for ACKs to be sent */
	saved_packet_count = skel->bss->g_packet_count;
	usleep(100000);		/* 0.1s */
	for (i = 0;
	     skel->bss->g_packet_count != saved_packet_count && i < 10;
	     i++) {
		saved_packet_count = skel->bss->g_packet_count;
		usleep(100000);	/* 0.1s */
	}
	if (!ASSERT_EQ(skel->bss->g_packet_count, saved_packet_count,
		       "packet_count"))
		return -1;

	skel->bss->g_packet_count = 0;
	saved_packet_count = 0;

	/* Half shutdown to make sure the closing socket having a chance to
	 * receive a FIN from the peer.
	 */
	err = shutdown(*closing_fd, SHUT_WR);
	if (!ASSERT_OK(err, "shutdown closing_fd"))
		return -1;

	/* Wait for FIN and the ACK of the FIN to be observed */
	for (i = 0;
	     skel->bss->g_packet_count < saved_packet_count + 2 && i < 10;
	     i++)
		usleep(100000);	/* 0.1s */
	if (!ASSERT_GE(skel->bss->g_packet_count, saved_packet_count + 2,
		       "packet_count"))
		return -1;

	saved_packet_count = skel->bss->g_packet_count;

	/* Fully shutdown the connection */
	err = close(*peer_fd);
	if (!ASSERT_OK(err, "close peer_fd"))
		return -1;
	*peer_fd = -1;

	/* Wait for FIN and the ACK of the FIN to be observed */
	for (i = 0;
	     skel->bss->g_packet_count < saved_packet_count + 2 && i < 10;
	     i++)
		usleep(100000);	/* 0.1s */
	if (!ASSERT_GE(skel->bss->g_packet_count, saved_packet_count + 2,
		       "packet_count"))
		return -1;

	err = close(*closing_fd);
	if (!ASSERT_OK(err, "close closing_fd"))
		return -1;
	*closing_fd = -1;

	close(*listen_fd);
	*listen_fd = -1;

	return 0;
}

/* This test case includes four scenarios:
 * 1. Connect to the server from outside the cgroup and close the connection
 *    from outside the cgroup.
 * 2. Connect to the server from outside the cgroup and close the connection
 *    from inside the cgroup.
 * 3. Connect to the server from inside the cgroup and close the connection
 *    from outside the cgroup.
 * 4. Connect to the server from inside the cgroup and close the connection
 *    from inside the cgroup.
 *
 * The test case is to verify that cgroup_skb/{egress,ingress} filters
 * receive expected packets including SYN, SYN/ACK, ACK, FIN, and FIN/ACK.
 */
void test_cgroup_tcp_skb(void)
{
	struct bpf_link *ingress_link = NULL;
	struct bpf_link *egress_link = NULL;
	int client_fd = -1, listen_fd = -1;
	struct cgroup_tcp_skb *skel;
	int service_fd = -1;
	int cgroup_fd = -1;
	int err;

	skel = cgroup_tcp_skb__open_and_load();
	if (!ASSERT_OK(!skel, "skel_open_load"))
		return;

	err = setup_cgroup_environment();
	if (!ASSERT_OK(err, "setup_cgroup_environment"))
		goto cleanup;

	cgroup_fd = create_and_get_cgroup(CGROUP_TCP_SKB_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "cgroup_fd"))
		goto cleanup;

	/* Scenario 1 */
	err = install_filters(cgroup_fd, &egress_link, &ingress_link,
			      skel->progs.server_egress,
			      skel->progs.server_ingress,
			      skel);
	if (!ASSERT_OK(err, "install_filters"))
		goto cleanup;

	err = talk_to_cgroup(&client_fd, &listen_fd, &service_fd, skel);
	if (!ASSERT_OK(err, "talk_to_cgroup"))
		goto cleanup;

	err = close_connection(&client_fd, &service_fd, &listen_fd, skel);
	if (!ASSERT_OK(err, "close_connection"))
		goto cleanup;

	ASSERT_EQ(skel->bss->g_unexpected, 0, "g_unexpected");
	ASSERT_EQ(skel->bss->g_sock_state, CLOSED, "g_sock_state");

	uninstall_filters(&egress_link, &ingress_link);

	/* Scenario 2 */
	err = install_filters(cgroup_fd, &egress_link, &ingress_link,
			      skel->progs.server_egress_srv,
			      skel->progs.server_ingress_srv,
			      skel);

	err = talk_to_cgroup(&client_fd, &listen_fd, &service_fd, skel);
	if (!ASSERT_OK(err, "talk_to_cgroup"))
		goto cleanup;

	err = close_connection(&service_fd, &client_fd, &listen_fd, skel);
	if (!ASSERT_OK(err, "close_connection"))
		goto cleanup;

	ASSERT_EQ(skel->bss->g_unexpected, 0, "g_unexpected");
	ASSERT_EQ(skel->bss->g_sock_state, TIME_WAIT, "g_sock_state");

	uninstall_filters(&egress_link, &ingress_link);

	/* Scenario 3 */
	err = install_filters(cgroup_fd, &egress_link, &ingress_link,
			      skel->progs.client_egress_srv,
			      skel->progs.client_ingress_srv,
			      skel);

	err = talk_to_outside(&client_fd, &listen_fd, &service_fd, skel);
	if (!ASSERT_OK(err, "talk_to_outside"))
		goto cleanup;

	err = close_connection(&service_fd, &client_fd, &listen_fd, skel);
	if (!ASSERT_OK(err, "close_connection"))
		goto cleanup;

	ASSERT_EQ(skel->bss->g_unexpected, 0, "g_unexpected");
	ASSERT_EQ(skel->bss->g_sock_state, CLOSED, "g_sock_state");

	uninstall_filters(&egress_link, &ingress_link);

	/* Scenario 4 */
	err = install_filters(cgroup_fd, &egress_link, &ingress_link,
			      skel->progs.client_egress,
			      skel->progs.client_ingress,
			      skel);

	err = talk_to_outside(&client_fd, &listen_fd, &service_fd, skel);
	if (!ASSERT_OK(err, "talk_to_outside"))
		goto cleanup;

	err = close_connection(&client_fd, &service_fd, &listen_fd, skel);
	if (!ASSERT_OK(err, "close_connection"))
		goto cleanup;

	ASSERT_EQ(skel->bss->g_unexpected, 0, "g_unexpected");
	ASSERT_EQ(skel->bss->g_sock_state, TIME_WAIT, "g_sock_state");

	uninstall_filters(&egress_link, &ingress_link);

cleanup:
	close(client_fd);
	close(listen_fd);
	close(service_fd);
	close(cgroup_fd);
	bpf_link__destroy(egress_link);
	bpf_link__destroy(ingress_link);
	cleanup_cgroup_environment();
	cgroup_tcp_skb__destroy(skel);
}
