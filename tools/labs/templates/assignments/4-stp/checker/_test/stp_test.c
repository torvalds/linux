/*
 * SO2 Transport Protocol - test suite
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <assert.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>

#include "test.h"
#include "debug.h"
#include "util.h"

#include "stp.h"
#include "stp_test.h"

#define SSA			struct sockaddr
#define BUFLEN			32

/* declared in test.h; used for printing information in test macro */
int max_points = 100;

/* values read from STP_PROC_FULL_FILENAME */
static int rx_pkts, hdr_err, csum_err, no_sock, no_buffs, tx_pkts;

enum socket_action {
	ACTION_SENDTO,
	ACTION_SENDMSG,
	ACTION_SEND,
	ACTION_SENDTO_PING_PONG,
	ACTION_SENDMSG_PING_PONG,
	ACTION_SEND_PING_PONG,
};

/*
 * Do initialization for STP test functions.
 */

static void init_test(void)
{
	system("insmod " MODULE_FILENAME);
}

/*
 * Do cleanup for STP test functions.
 */

static void cleanup_test(void)
{
	system("rmmod " MODULE_NAME);
}

/*
 * Check for successful module insertion and removal from the kernel.
 */

static void test_insmod_rmmod(void)
{
	int rc;

	rc = system("insmod " MODULE_FILENAME);
	test("test_insmod", rc == 0, 1);

	rc = system("rmmod " MODULE_NAME);
	test("test_rmmod", rc == 0, 1);

	rc = system("insmod " MODULE_FILENAME);
	test(__FUNCTION__, rc == 0, 1);

	system("rmmod " MODULE_NAME);
}

/*
 * Check /proc/net/protocols for STP protocol. Grep for line starting with
 * the string identified by STP_PROTO_NAME.
 */

static void test_proto_name_exists_after_insmod(void)
{
	int rc;

	init_test();

	rc = system("grep '^" STP_PROTO_NAME "' /proc/net/protocols > /dev/null 2>&1");
	test(__FUNCTION__, rc == 0, 2);

	cleanup_test();
}

/*
 * STP entry in /proc/net/protocols is deleted when module is removed.
 */

static void test_proto_name_inexistent_after_rmmod(void)
{
	int rc;

	init_test();
	cleanup_test();

	rc = system("grep '^" STP_PROTO_NAME "' /proc/net/protocols > /dev/null 2>&1");
	test(__FUNCTION__, rc != 0, 2);
}

/*
 * Check for proc entry for STP statistics.
 */

static void test_proc_entry_exists_after_insmod(void)
{
	int rc;

	init_test();

	rc = access(STP_PROC_FULL_FILENAME, F_OK);
	test(__FUNCTION__, rc == 0, 2);

	cleanup_test();
}

/*
 * STP statistics file in /proc/net/ is deleted when module is removed.
 */

static void test_proc_entry_inexistent_after_rmmod(void)
{
	int rc;

	init_test();
	cleanup_test();

	rc = system("file " STP_PROC_FULL_FILENAME " > /dev/null 2>&1");
	test(__FUNCTION__, rc != 0, 2);
}

/*
 * Call socket(2) with proper arguments for creating an AF_STP socket.
 */

static void test_socket(void)
{
	int s;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);
	test(__FUNCTION__, s > 0, 5);

	close(s);
	cleanup_test();
}

/*
 * Create two AF_STP sockets using socket(2).
 */

static void test_two_sockets(void)
{
	int s1, s2;

	init_test();

	s1 = socket(AF_STP, SOCK_DGRAM, 0);
	s2 = socket(AF_STP, SOCK_DGRAM, 0);
	test(__FUNCTION__, s1 > 0 && s2 > 0 && s1 != s2, 2);

	close(s1);
	close(s2);
	cleanup_test();
}

/*
 * Pass bad socket type argument to socket(2) (second argument).
 * Call should fail.
 */

static void test_socket_bad_socket_type(void)
{
	int s;

	init_test();

	s = socket(AF_STP, SOCK_STREAM, 0);
	test(__FUNCTION__, s < 0, 1);

	close(s);
	cleanup_test();
}

/*
 * Pass bad protocol argument to socket(2) (third argument).
 * Call should fail.
 */

static void test_socket_bad_protocol(void)
{
	int s;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, IPPROTO_TCP);
	test(__FUNCTION__, s < 0, 1);

	close(s);
	cleanup_test();
}

/*
 * Close open socket using close(2).
 */

static void test_close(void)
{
	int s;
	int rc;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	rc = close(s);
	test(__FUNCTION__, rc == 0, 2);

	cleanup_test();
}

/*
 * Pass closed socket descriptor to close(2). Call should fail.
 */

static void test_close_closed_socket(void)
{
	int s;
	int rc;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	close(s);
	rc = close(s);

	test(__FUNCTION__, rc < 0, 2);

	cleanup_test();
}

/*
 * Bind socket to proper address. Use "all" interface.
 */

static void test_bind(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas;
	const unsigned short port = 12345;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = 0;
	rc = bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc == 0, 5);

	close(s);
	cleanup_test();
}

/*
 * Bind socket to proper address. Use "eth0" interface.
 */

static void test_bind_eth0(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas;
	const unsigned short port = 12345;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("eth0");
	rc = bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc == 0, 2);

	close(s);
	cleanup_test();
}

/*
 * Use bind(2) on two AF_STP sockets.
 */

static void test_two_binds(void)
{
	int s1, s2;
	int rc1, rc2;
	struct sockaddr_stp sas1, sas2;
	const unsigned short port1 = 12345, port2 = 54321;

	init_test();

	s1 = socket(AF_STP, SOCK_DGRAM, 0);

	sas1.sas_family = AF_STP;
	sas1.sas_port = htons(port1);
	sas1.sas_ifindex = 0;
	rc1 = bind(s1, (struct sockaddr *) &sas1, sizeof(struct sockaddr_stp));

	s2 = socket(AF_STP, SOCK_DGRAM, 0);

	sas2.sas_family = AF_STP;
	sas2.sas_port = htons(port2);
	sas2.sas_ifindex = 0;
	rc2 = bind(s2, (struct sockaddr *) &sas2, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc1 == 0 && rc2 == 0, 2);

	close(s1); close(s2);
	cleanup_test();
}

/*
 * Pass bad address to bind(2) (second argument).
 * Call should fail.
 */

static void test_bind_bad_address(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas;
	const unsigned short port = 12345;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_INET;	/* invalid */
	sas.sas_port = htons(port);
	sas.sas_ifindex = 0;
	rc = bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc != 0, 1);

	close(s);
	cleanup_test();
}

/*
 * Use bind(2) on two AF_STP sockets using same port and "all" interface.
 * Call should fail.
 */

static void test_two_binds_same_if(void)
{
	int s1, s2;
	int rc1, rc2;
	struct sockaddr_stp sas1, sas2;
	const unsigned short port = 12345;

	init_test();

	s1 = socket(AF_STP, SOCK_DGRAM, 0);

	sas1.sas_family = AF_STP;
	sas1.sas_port = htons(port);
	sas1.sas_ifindex = 0;
	rc1 = bind(s1, (struct sockaddr *) &sas1, sizeof(struct sockaddr_stp));

	s2 = socket(AF_STP, SOCK_DGRAM, 0);

	sas2.sas_family = AF_STP;
	sas2.sas_port = htons(port);
	sas2.sas_ifindex = 0;
	rc2 = bind(s2, (struct sockaddr *) &sas2, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc1 == 0 && rc2 < 0, 2);

	close(s1); close(s2);
	cleanup_test();
}

/*
 * Use bind(2) on two AF_STP sockets using same port and same interface.
 * Call should fail.
 */

static void test_two_binds_same_if_eth0(void)
{
	int s1, s2;
	int rc1, rc2;
	struct sockaddr_stp sas1, sas2;
	const unsigned short port = 12345;

	init_test();

	s1 = socket(AF_STP, SOCK_DGRAM, 0);

	sas1.sas_family = AF_STP;
	sas1.sas_port = htons(port);
	sas1.sas_ifindex = if_nametoindex("eth0");
	rc1 = bind(s1, (struct sockaddr *) &sas1, sizeof(struct sockaddr_stp));

	s2 = socket(AF_STP, SOCK_DGRAM, 0);

	sas2.sas_family = AF_STP;
	sas2.sas_port = htons(port);
	sas2.sas_ifindex = if_nametoindex("eth0");
	rc2 = bind(s2, (struct sockaddr *) &sas2, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc1 == 0 && rc2 < 0, 2);

	close(s1); close(s2);
	cleanup_test();
}

/*
 * Use bind(2) on two AF_STP sockets using same port and "all" interface and
 * "eth0".
 * Call should fail.
 */

static void test_two_binds_same_if_all_eth0(void)
{
	int s1, s2;
	int rc1, rc2;
	struct sockaddr_stp sas1, sas2;
	const unsigned short port = 12345;

	init_test();

	s1 = socket(AF_STP, SOCK_DGRAM, 0);

	sas1.sas_family = AF_STP;
	sas1.sas_port = htons(port);
	sas1.sas_ifindex = 0;
	rc1 = bind(s1, (struct sockaddr *) &sas1, sizeof(struct sockaddr_stp));

	s2 = socket(AF_STP, SOCK_DGRAM, 0);

	sas2.sas_family = AF_STP;
	sas2.sas_port = htons(port);
	sas2.sas_ifindex = if_nametoindex("eth0");
	rc2 = bind(s2, (struct sockaddr *) &sas2, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc1 == 0 && rc2 < 0, 2);

	close(s1); close(s2);
	cleanup_test();
}

/*
 * Use bind(2) on two AF_STP sockets using same port and "eth0" interface and
 * "all".
 * Call should fail.
 */

static void test_two_binds_same_if_eth0_all(void)
{
	int s1, s2;
	int rc1, rc2;
	struct sockaddr_stp sas1, sas2;
	const unsigned short port = 12345;

	init_test();

	s1 = socket(AF_STP, SOCK_DGRAM, 0);

	sas1.sas_family = AF_STP;
	sas1.sas_port = htons(port);
	sas1.sas_ifindex = if_nametoindex("eth0");
	rc1 = bind(s1, (struct sockaddr *) &sas1, sizeof(struct sockaddr_stp));

	s2 = socket(AF_STP, SOCK_DGRAM, 0);

	sas2.sas_family = AF_STP;
	sas2.sas_port = htons(port);
	sas2.sas_ifindex = 0;
	rc2 = bind(s2, (struct sockaddr *) &sas2, sizeof(struct sockaddr_stp));

	test(__FUNCTION__, rc1 == 0 && rc2 < 0, 2);

	close(s1); close(s2);
	cleanup_test();
}

static ssize_t sendto_message(int sockfd, struct sockaddr_stp *sas,
	char *buf, size_t len)
{
	return sendto(sockfd, buf, len, 0, (SSA *) sas, sizeof(*sas));
}

static ssize_t sendmsg_message(int sockfd, struct sockaddr_stp *sas,
	char *buf, size_t len)
{
	struct iovec iov;
	struct msghdr msg;

	iov.iov_base = buf;
	iov.iov_len = len;
	msg.msg_name = sas;
	msg.msg_namelen = sizeof(*sas);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	return sendmsg(sockfd, &msg, 0);
}

static ssize_t send_message(int sockfd, char *buf, size_t len)
{
	return send(sockfd, buf, len, 0);
}

/*
 * Use recvfrom(2) to receive message. We don't care what is the source
 * address of the message.
 */

static ssize_t recvfrom_message(int sockfd, char *buf, size_t len)
{
	dprintf("ready to receive using recvfrom\n");
	return recvfrom(sockfd, buf, len, 0, NULL, NULL);
}

/*
 * Use recvmsg(2) to receive message. We don't care what is the source
 * address of the message.
 */

static ssize_t recvmsg_message(int sockfd, char *buf, size_t len)
{
	struct iovec iov;
	struct msghdr msg;

	iov.iov_base = buf;
	iov.iov_len = len;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	return recvmsg(sockfd, &msg, 0);
}

/*
 * Can not use recv(2) on datagram sockets. call recvfrom_message().
 */

static ssize_t recv_message(int sockfd, char *buf, size_t len)
{
	dprintf("ready to receive using recv\n");
	return recv(sockfd, buf, len, 0);
}

/*
 * Use sendto(2) on a socket.
 */

static void test_sendto(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 12345, remote_port = 54321;
	char bufout[BUFLEN] = DEFAULT_SENDER_MESSAGE;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));

	rc = sendto_message(s, &remote_sas, bufout, BUFLEN);

	test(__FUNCTION__, rc >= 0, 5);

	close(s);
	cleanup_test();
}

/*
 * Use sendmsg(2) on a socket.
 */

static void test_sendmsg(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 12345, remote_port = 54321;
	char bufout[BUFLEN] = DEFAULT_SENDER_MESSAGE;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));

	rc = sendmsg_message(s, &remote_sas, bufout, BUFLEN);

	test(__FUNCTION__, rc >= 0, 3);

	close(s);
	cleanup_test();
}

/*
 * Connect local socket to remote AF_STP socket.
 */

static void test_connect(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 12345, remote_port = 54321;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));

	rc = connect(s, (struct sockaddr *) &remote_sas, sizeof(remote_sas));

	test(__FUNCTION__, rc >= 0, 5);

	close(s);
	cleanup_test();
}

/*
 * Use send(2) on a connected socket.
 */

static void test_send(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 12345, remote_port = 54321;
	char bufout[BUFLEN] = DEFAULT_SENDER_MESSAGE;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));

	rc = connect(s, (SSA *) &remote_sas, sizeof(remote_sas));
	assert(rc == 0);

	rc = send_message(s, bufout, BUFLEN);

	test(__FUNCTION__, rc >= 0, 5);

	close(s);
	cleanup_test();
}

/*
 * Read values from STP_PROC_FULL_FILENAME.
 */

static int stp_proc_read_values(void)
{
	char buffer[256];
	FILE *f;

	f = fopen(STP_PROC_FULL_FILENAME, "rt");
	if (f == NULL)
		return -1;

	/* read column line */
	fgets(buffer, 256, f);

	/* read values line */
	fscanf(f, "%d %d %d %d %d %d",
		&rx_pkts, &hdr_err, &csum_err, &no_sock, &no_buffs, &tx_pkts);
	dprintf("read: %d %d %d %d %d %d\n",
		rx_pkts, hdr_err, csum_err, no_sock, no_buffs, tx_pkts);

	fclose(f);

	return 0;
}

/*
 * Send packet updates RxPkts column in STP_PROC_FULL_FILENAME.
 * Expected values are 1, 1.
 */

static void test_stat_tx(void)
{
	int s;
	int rc;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 12345, remote_port = 54321;
	char bufout[BUFLEN] = DEFAULT_SENDER_MESSAGE;

	init_test();

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));

	rc = connect(s, (SSA *) &remote_sas, sizeof(remote_sas));
	assert(rc == 0);

	send_message(s, bufout, BUFLEN);

	close(s);

	stp_proc_read_values();

	test(__FUNCTION__, tx_pkts == 1, 3);

	cleanup_test();
}

/*
 * Start sender process.
 *
 * action switches between sendto(2), sendmsg(2), send(2) and whether
 * to do ping_pong or not.
 */

static pid_t start_sender(enum socket_action action)
{
	pid_t pid;
	int s;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 12345, remote_port = 54321;
	char bufin[BUFLEN];
	char bufout[BUFLEN] = DEFAULT_SENDER_MESSAGE;
	ssize_t bytes_recv = 0, bytes_sent = 0;
	sem_t *sem;

	/* set bufin to 0 for testing purposes (it should be overwritten) */
	memset(bufin, 0, BUFLEN);

	pid = fork();
	DIE(pid < 0, "fork");

	switch (pid) {
	case 0:		/* child process */
		break;

	default:	/* parent process */
		return pid;
	}

	/* only child process (sender) is running */

	sem = sem_open(SEM_NAME_SENDER, 0);
	if (sem == SEM_FAILED)
		exit(EXIT_FAILURE);

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));
	if (action == ACTION_SEND || action == ACTION_SEND_PING_PONG) {
		int rc;

		rc = connect(s, (SSA *) &remote_sas, sizeof(remote_sas));
		assert(rc == 0);
	}

	switch (action) {
	case ACTION_SENDTO:
	case ACTION_SENDTO_PING_PONG:
		bytes_sent = sendto_message(s, &remote_sas, bufout, BUFLEN);
		dprintf("sent %s\n", bufout);
		break;

	case ACTION_SENDMSG:
	case ACTION_SENDMSG_PING_PONG:
		bytes_sent = sendmsg_message(s, &remote_sas, bufout, BUFLEN);
		dprintf("sent %s\n", bufout);
		break;

	case ACTION_SEND:
	case ACTION_SEND_PING_PONG:
		bytes_sent = send_message(s, bufout, BUFLEN);
		dprintf("sent %s\n", bufout);
		break;

	default:
		break;
	}

	switch (action) {
	case ACTION_SENDTO_PING_PONG:
		bytes_recv = recvfrom_message(s, bufin, BUFLEN);
		dprintf("received %s\n", bufin);
		break;
	case ACTION_SENDMSG_PING_PONG:
		bytes_recv = recvmsg_message(s, bufin, BUFLEN);
		dprintf("received %s\n", bufin);
		break;
	case ACTION_SEND_PING_PONG:
		bytes_recv = recv_message(s, bufin, BUFLEN);
		dprintf("received %s\n", bufin);
		break;
	default:
		break;
	}

	/* Let the parent know we're done. */
	sem_post(sem);

	/* exit with EXIT_SUCCESS in case of successful communication */
	switch (action) {
	case ACTION_SENDTO:
	case ACTION_SEND:
	case ACTION_SENDMSG:
		if (bytes_sent > 0)
			exit(EXIT_SUCCESS);
		break;

	case ACTION_SENDMSG_PING_PONG:
	case ACTION_SENDTO_PING_PONG:
	case ACTION_SEND_PING_PONG:
		dprintf("(ping_pong) bytes_sent: %d, bytes_recv: %d, strcmp: %d\n",
			bytes_sent, bytes_recv, strcmp(bufin, bufout));
		dprintf("bufin: #%s#, bufout: #%s#\n", bufin, bufout);
		if (bytes_sent > 0 && bytes_recv > 0 &&
			strcmp(bufin, DEFAULT_RECEIVER_MESSAGE) == 0)
			exit(EXIT_SUCCESS);
		break;
	}

	exit(EXIT_FAILURE);

	/* is not reached */
	return 0;
}

/*
 * Start receiver process.
 *
 * action switches between sendto(2), sendmsg(2), send(2) and whether
 * to do ping_pong or not.
 */

static pid_t start_receiver(enum socket_action action)
{
	pid_t pid;
	int s;
	struct sockaddr_stp sas, remote_sas;
	const unsigned short port = 54321, remote_port = 12345;
	char bufin[BUFLEN];
	char bufout[BUFLEN] = DEFAULT_RECEIVER_MESSAGE;
	ssize_t bytes_recv = 0, bytes_sent = 0;
	sem_t *sem;

	/* set bufin to 0 for testing purposes (it should be overwritten) */
	memset(bufin, 0, BUFLEN);

	pid = fork();
	DIE(pid < 0, "fork");

	switch (pid) {
	case 0:		/* child process */
		break;

	default:	/* parent process */
		return pid;
	}

	/* only child process (receiver) is running */

	sem = sem_open(SEM_NAME_RECEIVER, 0);
	if (sem == SEM_FAILED)
		exit(EXIT_FAILURE);

	s = socket(AF_STP, SOCK_DGRAM, 0);

	sas.sas_family = AF_STP;
	sas.sas_port = htons(port);
	sas.sas_ifindex = if_nametoindex("lo");
	bind(s, (struct sockaddr *) &sas, sizeof(struct sockaddr_stp));

	remote_sas.sas_family = AF_STP;
	remote_sas.sas_port = htons(remote_port);
	remote_sas.sas_ifindex = 0;
	memcpy(remote_sas.sas_addr, ether_aton("00:00:00:00:00:00"),
		sizeof(remote_sas.sas_addr));

	if (action == ACTION_SEND || action == ACTION_SEND_PING_PONG) {
		int rc;

		rc = connect(s, (SSA *) &remote_sas, sizeof(remote_sas));
		assert(rc == 0);
		dprintf("connected\n");
	}

	/* We're set up, let the parent know. */
	sem_post(sem);

	switch (action) {
	case ACTION_SENDTO:
	case ACTION_SENDTO_PING_PONG:
		bytes_recv = recvfrom_message(s, bufin, BUFLEN);
		dprintf("received %s\n", bufin);
		break;

	case ACTION_SENDMSG:
	case ACTION_SENDMSG_PING_PONG:
		bytes_recv = recvmsg_message(s, bufin, BUFLEN);
		dprintf("received %s\n", bufin);
		break;

	case ACTION_SEND:
	case ACTION_SEND_PING_PONG:
		bytes_recv = recv_message(s, bufin, BUFLEN);
		dprintf("received %s\n", bufin);
		break;

	default:
		break;
	}

	switch (action) {
	case ACTION_SENDTO_PING_PONG:
		bytes_sent = sendto_message(s, &remote_sas, bufout, BUFLEN);
		dprintf("sent %s\n", bufout);
		break;
	case ACTION_SENDMSG_PING_PONG:
		bytes_sent = sendmsg_message(s, &remote_sas, bufout, BUFLEN);
		dprintf("sent %s\n", bufout);
		break;
	case ACTION_SEND_PING_PONG:
		bytes_sent = send_message(s, bufout, BUFLEN);
		dprintf("sent %s\n", bufout);
		break;
	default:
		break;
	}

	/* Let the parent know we're done. */
	sem_post(sem);

	/* exit with EXIT_SUCCESS in case of successful communication */
	switch (action) {
	case ACTION_SENDTO:
	case ACTION_SEND:
	case ACTION_SENDMSG:
		if (bytes_recv > 0)
			exit(EXIT_SUCCESS);
		break;

	case ACTION_SENDMSG_PING_PONG:
	case ACTION_SENDTO_PING_PONG:
	case ACTION_SEND_PING_PONG:
		dprintf("(ping_pong) bytes_sent: %d, bytes_recv: %d\n",
				bytes_sent, bytes_recv);
		dprintf("bufin: #%s#, bufout: #%s#\n", bufin, bufout);
		if (bytes_recv > 0 && bytes_sent > 0 &&
			strcmp(bufin, DEFAULT_SENDER_MESSAGE) == 0)
			exit(EXIT_SUCCESS);
		break;
	}

	exit(EXIT_FAILURE);

	/* is not reached */
	return 0;
}

int wait_for_semaphore(sem_t *sem, unsigned int secs)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_REALTIME, &ts);
	assert(ret == 0);

	ts.tv_sec += secs;

	ret = sem_timedwait(sem, &ts);
	return ret;
}

/*
 * Wrapper call for running a sender and a receiver process.
 *
 * action switches between sendto(2), sendmsg(2), send(2) and whether
 * to do ping_pong or not.
 *
 * Returns boolean value: 1 in case of successful run, 0 otherwise.
 */

static int run_sender_receiver(enum socket_action action)
{
	pid_t pid_r = 0, pid_s = 0;
	int rc1, rc2, ret;
	int status1, status2;
	sem_t *sem_r, *sem_s;

	/* Create two named semaphores used to communicate
	 * with the child processes
	 */
	sem_r = sem_open(SEM_NAME_RECEIVER, O_CREAT, (mode_t)0644, 0);
	assert(sem_r != SEM_FAILED);
	sem_s = sem_open(SEM_NAME_SENDER, O_CREAT, (mode_t)0644, 0);
	assert(sem_s != SEM_FAILED);

	/* start the receiver */
	pid_r = start_receiver(action);
	assert(pid_r > 0);
	/* wait for it to bind */
	wait_for_semaphore(sem_r, RECV_TIMEOUT);

	/* Receiver is set up, start the sender now. */
	pid_s = start_sender(action);
	assert(pid_s > 0);

	/* Wait for both to finish. */
	rc1 = wait_for_semaphore(sem_r, SENDRECV_TIMEOUT);
	ret = waitpid(pid_r, &status1, rc1 ? WNOHANG : 0);
	assert(ret >= 0);
	kill(pid_r, SIGTERM); kill(pid_r, SIGKILL);

	rc2 = wait_for_semaphore(sem_s, SENDRECV_TIMEOUT);
	ret = waitpid(pid_s, &status2, rc2 ? WNOHANG : 0);
	assert(ret >= 0);
	kill(pid_s, SIGTERM); kill(pid_s, SIGKILL);

	sem_close(sem_r); sem_unlink(SEM_NAME_RECEIVER);
	sem_close(sem_s); sem_unlink(SEM_NAME_SENDER);

	return !rc1 && !rc2 &&
	       WIFEXITED(status1) && WEXITSTATUS(status1) == EXIT_SUCCESS &&
	       WIFEXITED(status2) && WEXITSTATUS(status2) == EXIT_SUCCESS;
}

/*
 * Send a datagram on one end and receive it on the other end.
 * Use sendto(2) and recvfrom(2).
 */

static void test_sendto_recvfrom(void)
{
	int rc;

	init_test();

	rc = run_sender_receiver(ACTION_SENDTO);

	test(__FUNCTION__, rc != 0, 10);

	cleanup_test();
}

/*
 * Send and receive packet updates RxPkts and TxPkts columns in
 * STP_PROC_FULL_FILENAME. Expected values are 1, 1.
 */

static void test_stat_tx_rx(void)
{
	init_test();

	run_sender_receiver(ACTION_SENDTO);

	stp_proc_read_values();

	test(__FUNCTION__, tx_pkts == 1 && rx_pkts == 1, 3);

	cleanup_test();
}

/*
 * Send a packet and then wait for a reply.
 */

static void test_sendto_recvfrom_ping_pong(void)
{
	int rc;

	init_test();

	rc = run_sender_receiver(ACTION_SENDTO_PING_PONG);

	test(__FUNCTION__, rc != 0, 5);

	cleanup_test();
}

/*
 * Send and receive ping pong updates RxPkts and TxPkts column in
 * STP_PROC_FULL_FILENAME. Expected values are 2, 2.
 */

static void test_stat_tx_rx_ping_pong(void)
{
	init_test();

	run_sender_receiver(ACTION_SENDTO_PING_PONG);

	stp_proc_read_values();
	stp_proc_read_values();

	test(__FUNCTION__, tx_pkts == 2 && rx_pkts == 2, 3);

	cleanup_test();
}

/*
 * Send a datagram on one end and receive it on the other end.
 * Use sendmsg(2) and recvmsg(2).
 */

static void test_sendmsg_recvmsg(void)
{
	int rc;

	init_test();

	rc = run_sender_receiver(ACTION_SENDMSG);

	test(__FUNCTION__, rc != 0, 5);

	cleanup_test();
}

/*
 * Send a packet and then wait for a reply.
 */

static void test_sendmsg_recvmsg_ping_pong(void)
{
	int rc;

	init_test();

	rc = run_sender_receiver(ACTION_SENDMSG_PING_PONG);

	test(__FUNCTION__, rc != 0, 3);

	cleanup_test();
}

/*
 * Send a packet on one end and receive it on the other end.
 * Use send(2) and recv(2).
 */

static void test_send_receive(void)
{
	int rc;

	init_test();

	rc = run_sender_receiver(ACTION_SEND);

	test(__FUNCTION__, rc != 0, 5);

	cleanup_test();
}

/*
 * Send a packet and then wait for a reply.
 */

static void test_send_receive_ping_pong(void)
{
	int rc;

	init_test();

	rc = run_sender_receiver(ACTION_SEND_PING_PONG);

	test(__FUNCTION__, rc != 0, 3);

	cleanup_test();
}

static void (*test_fun_array[])(void) = {
	NULL,
	test_insmod_rmmod,
	test_proto_name_exists_after_insmod,
	test_proto_name_inexistent_after_rmmod,
	test_proc_entry_exists_after_insmod,
	test_proc_entry_inexistent_after_rmmod,
	test_socket,
	test_two_sockets,
	test_socket_bad_socket_type,
	test_socket_bad_protocol,
	test_close,
	test_close_closed_socket,
	test_bind,
	test_bind_eth0,
	test_two_binds,
	test_bind_bad_address,
	test_two_binds_same_if,
	test_two_binds_same_if_eth0,
	test_two_binds_same_if_all_eth0,
	test_two_binds_same_if_eth0_all,
	test_sendto,
	test_sendmsg,
	test_connect,
	test_send,
	test_stat_tx,
	test_sendto_recvfrom,
	test_stat_tx_rx,
	test_sendto_recvfrom_ping_pong,
	test_stat_tx_rx_ping_pong,
	test_sendmsg_recvmsg,
	test_sendmsg_recvmsg_ping_pong,
	test_send_receive,
	test_send_receive_ping_pong,
};

/*
 * Usage message for invalid executable call.
 */

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s test_no\n\n", argv0);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	int test_idx;

	if (argc != 2)
		usage(argv[0]);

	test_idx = atoi(argv[1]);

	if (test_idx < 1 ||
		test_idx >= sizeof(test_fun_array)/sizeof(test_fun_array[0])) {
		fprintf(stderr, "Error: test index %d is out of bounds\n",
			test_idx);
		exit(EXIT_FAILURE);
	}

	srand(time(NULL));
	srand48(time(NULL));
	test_fun_array[test_idx]();

	return 0;
}
