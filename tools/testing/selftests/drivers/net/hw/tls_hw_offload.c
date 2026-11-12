// SPDX-License-Identifier: GPL-2.0
/*
 * TLS Hardware Offload Two-Node Test
 *
 * Tests kTLS hardware offload between two physical nodes using
 * hardcoded keys. Supports TLS 1.2 and AES-GCM-128/256.
 *
 * Both ends must run this helper: there is no handshake.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <linux/tls.h>

#ifndef SOL_TLS
#define SOL_TLS 282
#endif

#define TLS_RECORD_TYPE_HANDSHAKE		22
#define TLS_RECORD_TYPE_ALERT			21
#define TLS_RECORD_TYPE_DATA			23

#define MIN_BUF_SIZE   16

/* Initial key material */
static struct tls12_crypto_info_aes_gcm_128 tls_info_key0_128 = {
	.info = {
		.version = TLS_1_2_VERSION,
		.cipher_type = TLS_CIPHER_AES_GCM_128,
	},
	.iv = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 },
	.key = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10 },
	.salt = { 0x01, 0x02, 0x03, 0x04 },
	.rec_seq = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static struct tls12_crypto_info_aes_gcm_256 tls_info_key0_256 = {
	.info = {
		.version = TLS_1_2_VERSION,
		.cipher_type = TLS_CIPHER_AES_GCM_256,
	},
	.iv = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 },
	.key = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
		 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20 },
	.salt = { 0x01, 0x02, 0x03, 0x04 },
	.rec_seq = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static int test_record_type;
static int num_iterations = 100;
static int cipher_type = TLS_CIPHER_AES_GCM_128;
static int tls_version = TLS_1_2_VERSION;
static int server_port = 4433;
static char *server_ip;
/* Address family to force: AF_UNSPEC (any), AF_INET (-4), AF_INET6 (-6). */
static int force_family = AF_UNSPEC;

static int send_size = 16384;
static int random_size_max;
/* Per-iteration progress output (-V). Off by default: the
 * harness runs the server in the background and captures its stdout in a
 * pipe that is not drained until the server exits, so unbounded per-record
 * chatter (thousands of lines for the large echo variants) would fill the
 * pipe, block printf(), stall the echo loop and surface as a bogus recv
 * timeout on the client. With -V off a run prints
 * fixed setup/summary lines, comfortably inside a 64 KiB pipe.
 */
static int verbose;

/* Upper bound on a helper's lifetime, enforced by SIGALRM. The harness
 * waits for both helpers with the default 20s cmd()/bkg() timeout and
 * cannot signal the one on the remote, so each must exit on its own first.
 * A normal run takes a few seconds; the socket timeouts catch most stalls
 * sooner and with a better error.
 */
#define HELPER_TIMEOUT_S	15

static const char *cipher_name(int cipher)
{
	switch (cipher) {
	case TLS_CIPHER_AES_GCM_128: return "AES-GCM-128";
	case TLS_CIPHER_AES_GCM_256: return "AES-GCM-256";
	default: return "unknown";
	}
}

static const char *version_name(int version)
{
	switch (version) {
	case TLS_1_2_VERSION: return "TLS 1.2";
	default: return "unknown";
	}
}

static int setup_tls_ulp(int fd)
{
	int ret;

	ret = setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	if (ret < 0) {
		printf("SETUP ERROR: TCP_ULP failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

/* Echo mode drives both directions from a single thread: the
 * client pushes a whole payload with one blocking send() and only reads the
 * echo afterwards, while the server blocks in send() mid-echo. If a payload
 * exceeds the peer's receive window the two sides deadlock - client stuck in
 * send(), server stuck echoing, neither draining the other. Size the socket
 * buffers so a full payload always fits in the peer's window (the forward
 * send() then completes without needing the peer to read concurrently); the
 * send/recv timeouts armed by set_io_timeouts() turn any residual stall into a
 * loud EAGAIN instead of a hang.
 */
static void configure_echo_socket(int fd, int payload)
{
	int want = payload;

	if (want < MIN_BUF_SIZE)
		want = MIN_BUF_SIZE;

	/* SO_*BUFFORCE bypasses the rmem_max/wmem_max sysctl caps (needs
	 * CAP_NET_ADMIN); fall back to the best-effort, cap-limited option
	 * when unprivileged - the timeouts below still turn any resulting
	 * stall into a loud failure rather than a hang.
	 */
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &want, sizeof(want)) < 0)
		setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &want, sizeof(want));
	if (setsockopt(fd, SOL_SOCKET, SO_SNDBUFFORCE, &want, sizeof(want)) < 0)
		setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &want, sizeof(want));
}

/* Arm send/recv timeouts so any unexpected stall fails loudly with EAGAIN
 * instead of running into HELPER_TIMEOUT_S.
 */
static void set_io_timeouts(int fd)
{
	struct timeval tv = { .tv_sec = 8, .tv_usec = 0 };

	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* Send the whole buffer, looping over short counts. A blocking SOCK_STREAM
 * send() may return fewer bytes than requested (e.g. when SO_SNDTIMEO fires
 * after partial progress) without setting errno, so a short count is not an
 * error - only a negative return is.
 */
static int send_all(int fd, const char *buf, ssize_t len)
{
	ssize_t sent = 0;
	ssize_t ret;

	while (sent < len) {
		ret = send(fd, buf + sent, len - sent, 0);
		if (ret <= 0) {
			/* 0 for a non-empty request would spin forever. */
			printf("FAIL: send failed: %s\n",
			       ret ? strerror(errno) : "no progress");
			return -1;
		}
		sent += ret;
	}
	return 0;
}

static int recv_tls_message(int fd, char *buf, size_t buflen, int *record_type)
{
	char cmsg_buf[CMSG_SPACE(sizeof(unsigned char))];
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	struct iovec iov;
	int ret;

	iov.iov_base = buf;
	iov.iov_len = buflen;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);

	ret = recvmsg(fd, &msg, 0);
	if (ret <= 0)
		return ret;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg && cmsg->cmsg_level == SOL_TLS &&
	    cmsg->cmsg_type == TLS_GET_RECORD_TYPE)
		*record_type = *((unsigned char *)CMSG_DATA(cmsg));

	return ret;
}

static int install_tls_key(int fd, int direction, int cipher)
{
	const char *dir = direction == TLS_TX ? "TX" : "RX";
	int ret;

	if (verbose)
		printf("Installing TLS_%s %s...\n", dir, cipher_name(cipher));

	if (cipher == TLS_CIPHER_AES_GCM_256) {
		struct tls12_crypto_info_aes_gcm_256 key;

		memcpy(&key, &tls_info_key0_256, sizeof(key));
		key.info.version = tls_version;
		ret = setsockopt(fd, SOL_TLS, direction, &key, sizeof(key));
	} else {
		struct tls12_crypto_info_aes_gcm_128 key;

		memcpy(&key, &tls_info_key0_128, sizeof(key));
		key.info.version = tls_version;
		ret = setsockopt(fd, SOL_TLS, direction, &key, sizeof(key));
	}

	if (ret < 0) {
		printf("SETUP ERROR: TLS_%s %s failed: %s\n",
		       dir, cipher_name(cipher), strerror(errno));
		return -1;
	}
	printf("TLS_%s %s installed\n", dir, cipher_name(cipher));
	return 0;
}

/* Open a TCP connection to server_ip:server_port, switch to the TLS
 * ULP, and install initial TX/RX keys. Works over IPv4 or
 * IPv6: getaddrinfo() resolves server_ip (honouring any -4/-6 forced
 * family and %zone scope IDs in link-local addresses). Returns the fd on
 * success, -1 on error (with the fd already closed).
 */
static int client_connect_tls(void)
{
	struct addrinfo hints = {0}, *res, *rp;
	char port_str[16];
	int csk = -1;
	int err = 0;
	int ret;

	hints.ai_family = force_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	snprintf(port_str, sizeof(port_str), "%d", server_port);

	ret = getaddrinfo(server_ip, port_str, &hints, &res);
	if (ret) {
		printf("SETUP ERROR: getaddrinfo(%s): %s\n", server_ip,
		       gai_strerror(ret));
		return -1;
	}

	printf("Connecting to %s:%d...\n", server_ip, server_port);
	for (rp = res; rp; rp = rp->ai_next) {
		csk = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (csk < 0) {
			err = errno;
			continue;
		}
		if (connect(csk, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		err = errno;
		close(csk);
		csk = -1;
	}
	freeaddrinfo(res);

	if (csk < 0) {
		printf("SETUP ERROR: connect to %s:%d failed: %s\n",
		       server_ip, server_port, strerror(err));
		return -1;
	}
	printf("Connected!\n");

	if (setup_tls_ulp(csk) < 0)
		goto err;

	if (install_tls_key(csk, TLS_TX, cipher_type) < 0 ||
	    install_tls_key(csk, TLS_RX, cipher_type) < 0)
		goto err;

	set_io_timeouts(csk);
	configure_echo_socket(csk, random_size_max > 0 ?
				    random_size_max : send_size);

	return csk;
err:
	close(csk);
	return -1;
}

/* Drain `len` echoed bytes from the server and verify they match the
 * payload we just sent.
 */
static int client_recv_echo(int fd, const char *sent, char *echo_buf,
			    ssize_t len)
{
	ssize_t total = 0;
	ssize_t n;

	while (total < len) {
		n = recv(fd, echo_buf + total, len - total, 0);
		if (n < 0) {
			printf("FAIL: Echo recv failed: %s\n", strerror(errno));
			return -1;
		}
		if (n == 0) {
			printf("FAIL: Connection closed during echo\n");
			return -1;
		}
		total += n;
	}

	if (memcmp(sent, echo_buf, len) != 0) {
		printf("FAIL: Echo data mismatch!\n");
		return -1;
	}
	if (verbose)
		printf("Received echo %zd bytes (ok)\n", total);
	return 0;
}

static int do_client(void)
{
	char *buf = NULL, *echo_buf = NULL;
	int max_size;
	int csk = -1, i = 1;
	int test_result = -1;
	ssize_t n, total = 0;

	max_size = random_size_max > 0 ? random_size_max : send_size;
	if (max_size < MIN_BUF_SIZE)
		max_size = MIN_BUF_SIZE;
	buf = malloc(max_size);
	echo_buf = malloc(max_size);
	if (!buf || !echo_buf) {
		printf("SETUP ERROR: failed to allocate buffers\n");
		goto out;
	}

	csk = client_connect_tls();
	if (csk < 0)
		goto out;

	printf("TLS setup complete.\n");

	if (random_size_max > 0)
		printf("Sending %d messages of random size (1..%d bytes)...\n",
		       num_iterations, random_size_max);
	else
		printf("Sending %d messages of %d bytes...\n",
		       num_iterations, send_size);

	for (i = 1; i <= num_iterations; i++) {
		int this_size;
		int j;

		if (random_size_max > 0)
			this_size = (rand() % random_size_max) + 1;
		else
			this_size = send_size;

		for (j = 0; j < this_size; j++)
			buf[j] = rand() & 0xFF;

		if (send_all(csk, buf, this_size) < 0)
			goto out;
		n = this_size;
		total += n;

		if (verbose)
			printf("Sent %zd bytes (iteration %d)\n", n, i);
		if (client_recv_echo(csk, buf, echo_buf, n) < 0)
			goto out;
	}

	test_result = 0;
out:
	printf("Sent %d/%d iterations, %zd bytes\n", i - 1, num_iterations,
	       total);
	if (csk >= 0)
		close(csk);
	free(buf);
	free(echo_buf);
	return test_result;
}

/* Bind/listen on server_port, accept one client, switch to the TLS ULP
 * and install initial keys.
 * Returns the connected fd on success and writes the listener fd to
 * *lsk_out so the caller can close it. Returns -1 on error, with all
 * intermediate fds already closed and *lsk_out left at -1.
 */
static int server_accept_tls(int *lsk_out)
{
	struct addrinfo hints = {0}, *res, *rp;
	int lsk = -1, csk, one = 1;
	char port_str[16];
	int ret;

	*lsk_out = -1;

	/* AI_PASSIVE gives a wildcard bind address for the chosen family
	 * (0.0.0.0 / ::). The family is forced by -4/-6; when unspecified,
	 * bind the first entry that works.
	 */
	hints.ai_family = force_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	snprintf(port_str, sizeof(port_str), "%d", server_port);

	ret = getaddrinfo(NULL, port_str, &hints, &res);
	if (ret) {
		printf("SETUP ERROR: getaddrinfo(port %d): %s\n", server_port,
		       gai_strerror(ret));
		return -1;
	}

	for (rp = res; rp; rp = rp->ai_next) {
		lsk = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (lsk < 0)
			continue;
		setsockopt(lsk, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (bind(lsk, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(lsk);
		lsk = -1;
	}
	freeaddrinfo(res);

	if (lsk < 0) {
		printf("SETUP ERROR: failed to bind port %d: %s\n",
		       server_port, strerror(errno));
		return -1;
	}

	if (listen(lsk, 1) < 0) {
		printf("SETUP ERROR: listen failed: %s\n", strerror(errno));
		close(lsk);
		return -1;
	}

	printf("Server listening on port %d\n", server_port);
	printf("Waiting for client connection...\n");

	/* Bound accept() so a client that never connects (a deploy or connect
	 * failure on the peer) does not block the server forever. accept()
	 * honours SO_RCVTIMEO on the listening socket; the client connects
	 * right after wait_port_listen(), so 10s is generous.
	 */
	{
		struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };

		setsockopt(lsk, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	csk = accept(lsk, NULL, NULL);
	if (csk < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			printf("SETUP ERROR: accept timed out, no client\n");
		else
			printf("SETUP ERROR: accept failed: %s\n",
			       strerror(errno));
		close(lsk);
		return -1;
	}
	printf("Client connected!\n");

	if (setup_tls_ulp(csk) < 0)
		goto err;

	if (install_tls_key(csk, TLS_TX, cipher_type) < 0 ||
	    install_tls_key(csk, TLS_RX, cipher_type) < 0)
		goto err;

	set_io_timeouts(csk);
	configure_echo_socket(csk, random_size_max > 0 ?
				    random_size_max : send_size);

	*lsk_out = lsk;
	return csk;
err:
	close(csk);
	close(lsk);
	return -1;
}

static int do_server(void)
{
	int lsk = -1, csk = -1;
	ssize_t n, total = 0;
	int test_result = -1;
	int recv_count = 0;
	char *buf = NULL;
	int record_type = 0;
	int buf_size;

	buf_size = send_size;
	if (buf_size < MIN_BUF_SIZE)
		buf_size = MIN_BUF_SIZE;
	buf = malloc(buf_size);
	if (!buf) {
		printf("SETUP ERROR: failed to allocate buffer\n");
		goto out;
	}

	csk = server_accept_tls(&lsk);
	if (csk < 0)
		goto out;

	printf("TLS %s setup complete. Receiving...\n",
	       cipher_name(cipher_type));

	/* Echo everything back; the client verifies each echo. */
	while (1) {
		record_type = 0;
		n = recv_tls_message(csk, buf, buf_size, &record_type);
		if (n == 0) {
			printf("Connection closed by client\n");
			break;
		}
		if (n < 0) {
			printf("FAIL: recv failed: %s\n", strerror(errno));
			goto out;
		}
		if (record_type != TLS_RECORD_TYPE_DATA) {
			printf("FAIL: expected data record, got type %d\n",
			       record_type);
			goto out;
		}

		total += n;
		recv_count++;
		if (verbose)
			printf("Received %zd bytes (total: %zd, count: %d)\n",
			       n, total, recv_count);

		if (send_all(csk, buf, n) < 0)
			goto out;
		if (verbose)
			printf("Echoed %zd bytes back to client\n", n);
	}

	test_result = 0;
out:
	printf("Connection closed. Total received: %zd bytes in %d reads\n",
	       total, recv_count);

	if (csk >= 0)
		close(csk);
	if (lsk >= 0)
		close(lsk);
	free(buf);
	return test_result;
}

static int send_record_type(int fd)
{
	char control[CMSG_SPACE(sizeof(unsigned char))] = {};
	struct iovec iov = { .iov_base = "BBBB", .iov_len = 4 };
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	char ack[2];

	if (send(fd, "AAAA", 4, MSG_MORE) != 4) {
		printf("FAIL: send MSG_MORE failed: %s\n", strerror(errno));
		return -1;
	}
	cmsg->cmsg_level = SOL_TLS;
	cmsg->cmsg_type = TLS_SET_RECORD_TYPE;
	cmsg->cmsg_len = CMSG_LEN(sizeof(unsigned char));
	*CMSG_DATA(cmsg) = test_record_type;
	if (sendmsg(fd, &msg, 0) != 4) {
		printf("FAIL: sendmsg record type failed: %s\n",
		       strerror(errno));
		return -1;
	}
	return client_recv_echo(fd, "OK", ack, sizeof(ack));
}

static int expect_record(int fd, const char *data, unsigned char type)
{
	char control[CMSG_SPACE(sizeof(unsigned char))] = {};
	char buf[16];
	struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};
	struct cmsghdr *cmsg;
	int ret;

	ret = recvmsg(fd, &msg, 0);
	if (ret != 4 || memcmp(buf, data, 4) ||
	    (msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) ||
	    !(msg.msg_flags & MSG_EOR)) {
		printf("FAIL: expected record '%s', got %d bytes, flags %#x\n",
		       data, ret, msg.msg_flags);
		return -1;
	}
	cmsg = CMSG_FIRSTHDR(&msg);
	if (!cmsg || cmsg->cmsg_level != SOL_TLS ||
	    cmsg->cmsg_type != TLS_GET_RECORD_TYPE ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(unsigned char)) ||
	    *CMSG_DATA(cmsg) != type) {
		printf("FAIL: expected record type %u\n", type);
		return -1;
	}
	return 0;
}

static int recv_record_type(int fd)
{
	char buf[16];
	int ret;

	if (expect_record(fd, "AAAA", TLS_RECORD_TYPE_DATA) ||
	    expect_record(fd, "BBBB", test_record_type) ||
	    send_all(fd, "OK", 2))
		return -1;
	ret = recv(fd, buf, sizeof(buf), 0);
	if (ret) {
		printf("FAIL: expected EOF after records, got %d\n", ret);
		return -1;
	}
	return 0;
}

static int test_pending_record(int is_server)
{
	char *buf = NULL, *echo_buf = NULL;
	int lsk = -1, csk = -1;
	int test_result = -1;
	int i;

	buf = malloc(send_size);
	echo_buf = malloc(send_size);
	if (!buf || !echo_buf) {
		printf("SETUP ERROR: failed to allocate buffers\n");
		goto out;
	}
	memset(buf, 0xa5, send_size);

	csk = is_server ? server_accept_tls(&lsk) : client_connect_tls();
	if (csk < 0)
		goto out;

	/* Exercise the offload data path before the record-type transition. */
	for (i = 0; i < num_iterations; i++) {
		if (!is_server && send_all(csk, buf, send_size) < 0)
			goto out;
		if (client_recv_echo(csk, buf, echo_buf, send_size) < 0)
			goto out;
		if (is_server && send_all(csk, buf, send_size) < 0)
			goto out;
	}

	test_result = is_server ? recv_record_type(csk) : send_record_type(csk);
out:
	if (csk >= 0)
		close(csk);
	if (lsk >= 0)
		close(lsk);
	free(buf);
	free(echo_buf);
	return test_result;
}

static int parse_int_arg(const char *arg, int min, int max,
			 const char *name, int *out)
{
	char *endp;
	long val;

	errno = 0;
	val = strtol(arg, &endp, 10);
	if (errno || endp == arg || *endp != '\0' || val < min || val > max) {
		if (max == INT_MAX)
			printf("ERROR: Invalid %s '%s'. Must be >= %d.\n",
			       name, arg, min);
		else
			printf("ERROR: Invalid %s '%s'. Must be %d..%d.\n",
			       name, arg, min, max);
		return -1;
	}
	*out = (int)val;
	return 0;
}

static int parse_cipher_option(const char *arg)
{
	if (strcmp(arg, "128") == 0) {
		cipher_type = TLS_CIPHER_AES_GCM_128;
		return 0;
	} else if (strcmp(arg, "256") == 0) {
		cipher_type = TLS_CIPHER_AES_GCM_256;
		return 0;
	}
	printf("ERROR: Invalid cipher '%s'. Must be 128 or 256.\n", arg);
	return -1;
}

static int parse_version_option(const char *arg)
{
	if (strcmp(arg, "1.2") == 0) {
		tls_version = TLS_1_2_VERSION;
		return 0;
	}
	printf("ERROR: Invalid TLS version '%s'. Must be 1.2.\n", arg);
	return -1;
}

static void print_usage(const char *prog)
{
	printf("TLS Hardware Offload Two-Node Test\n\n");
	printf("Usage:\n");
	printf("  %s server [OPTIONS]\n", prog);
	printf("  %s client -s <ip> [OPTIONS]\n", prog);
	printf("\nOptions:\n");
	printf("  -s <ip>       Server IPv4/IPv6 address (client, required)\n");
	printf("  -p <port>     Server port (default: 4433)\n");
	printf("  -4            Force IPv4 (default: auto/either)\n");
	printf("  -6            Force IPv6 (default: auto/either)\n");
	printf("  -b <size>     Send buffer size in bytes (default: 16384)\n");
	printf("  -r <max>      Use random send buffer sizes (1..<max>)\n");
	printf("  -v <version>  TLS version: 1.2 (default: 1.2)\n");
	printf("  -c <cipher>   Cipher: 128 or 256 (default: 128)\n");
	printf("  -n <N>        Send/echo iterations (default: 100)\n");
	printf("  -t <type>     Record type test: 21=alert, 22=handshake\n");
	printf("  -V            Verbose: log every iteration\n");
	printf("  -h            Show this help message\n");
	printf("\nExample:\n");
	printf("  Node A: %s server\n", prog);
	printf("  Node B: %s client -s 192.168.20.2\n", prog);
	printf("\nIPv6 Example:\n");
	printf("  Node A: %s server -6\n", prog);
	printf("  Node B: %s client -6 -s fd00::2\n", prog);
}

int main(int argc, char *argv[])
{
	int send_size_set = 0;
	int is_server;
	int opt;

	/* When the peer aborts a TLS connection (e.g. tls_err_abort() on a
	 * failed decrypt), a send() here would raise SIGPIPE and kill us by
	 * signal, so the harness sees only a bare non-zero exit with no
	 * "FAIL:" line. Ignore it and let send()/sendmsg() return EPIPE, which
	 * send_all()/send_record_type() report.
	 */
	signal(SIGPIPE, SIG_IGN);

	/* Line-buffer stdout so that output printed before a SIGALRM death
	 * is not lost in the stdio buffer.
	 */
	setvbuf(stdout, NULL, _IOLBF, 0);
	alarm(HELPER_TIMEOUT_S);

	if (argc < 2 ||
	    (strcmp(argv[1], "server") && strcmp(argv[1], "client"))) {
		print_usage(argv[0]);
		return 1;
	}
	is_server = !strcmp(argv[1], "server");

	optind = 2; /* skip subcommand */
	while ((opt = getopt(argc, argv, "s:p:b:r:c:v:n:t:V46h")) != -1) {
		switch (opt) {
		case 's':
			server_ip = optarg;
			break;
		case 'V':
			verbose = 1;
			break;
		case '4':
			if (force_family == AF_INET6) {
				printf("ERROR: -4 and -6 are exclusive\n");
				return 1;
			}
			force_family = AF_INET;
			break;
		case '6':
			if (force_family == AF_INET) {
				printf("ERROR: -4 and -6 are exclusive\n");
				return 1;
			}
			force_family = AF_INET6;
			break;
		case 'p':
			if (parse_int_arg(optarg, 1, 65535, "port",
					  &server_port) < 0)
				return 1;
			break;
		case 'b':
			if (parse_int_arg(optarg, 1, INT_MAX, "buffer size",
					  &send_size) < 0)
				return 1;
			send_size_set = 1;
			break;
		case 'r':
			if (parse_int_arg(optarg, 1, INT_MAX, "random size",
					  &random_size_max) < 0)
				return 1;
			break;
		case 'c':
			if (parse_cipher_option(optarg) < 0)
				return 1;
			break;
		case 'v':
			if (parse_version_option(optarg) < 0)
				return 1;
			break;
		case 't':
			if (parse_int_arg(optarg, TLS_RECORD_TYPE_ALERT,
					  TLS_RECORD_TYPE_HANDSHAKE,
					  "record type", &test_record_type) < 0)
				return 1;
			break;
		case 'n':
			/* do_client() counts i = 1..num_iterations with an
			 * inclusive int bound, so INT_MAX itself would
			 * overflow the counter instead of terminating.
			 */
			if (parse_int_arg(optarg, 1, INT_MAX - 1,
					  "iteration count",
					  &num_iterations) < 0)
				return 1;
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	if (send_size_set && random_size_max > 0) {
		printf("ERROR: -b and -r are mutually exclusive\n");
		return 1;
	}

	if (test_record_type && random_size_max) {
		printf("ERROR: -t cannot be combined with -r\n");
		return 1;
	}

	if (is_server) {
		if (server_ip) {
			printf("warning: -s is ignored in server mode\n");
			server_ip = NULL;
		}
		if (random_size_max > 0) {
			printf("warning: -r is ignored in server mode\n");
			random_size_max = 0;
		}
	} else {
		if (!server_ip) {
			printf("ERROR: Client requires -s <ip> option\n");
			return 1;
		}
	}

	printf("TLS Version: %s\n", version_name(tls_version));
	printf("Cipher: %s\n", cipher_name(cipher_type));
	printf("Address family: %s\n",
	       force_family == AF_INET ? "IPv4" :
	       force_family == AF_INET6 ? "IPv6" : "auto");
	if (random_size_max > 0)
		printf("Buffer size: random (1..%d)\n", random_size_max);
	else
		printf("Buffer size: %d\n", send_size);

	srand(time(NULL));

	if (test_record_type)
		return test_pending_record(is_server) ? 1 : 0;

	if (is_server)
		return do_server() ? 1 : 0;

	return do_client() ? 1 : 0;
}
