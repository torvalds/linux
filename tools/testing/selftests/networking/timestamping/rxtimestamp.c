#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <asm/types.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct options {
	int so_timestamp;
	int so_timestampns;
	int so_timestamping;
};

struct tstamps {
	bool tstamp;
	bool tstampns;
	bool swtstamp;
	bool hwtstamp;
};

struct socket_type {
	char *friendly_name;
	int type;
	int protocol;
	bool enabled;
};

struct test_case {
	struct options sockopt;
	struct tstamps expected;
	bool enabled;
};

struct sof_flag {
	int mask;
	char *name;
};

static struct sof_flag sof_flags[] = {
#define SOF_FLAG(f) { f, #f }
	SOF_FLAG(SOF_TIMESTAMPING_SOFTWARE),
	SOF_FLAG(SOF_TIMESTAMPING_RX_SOFTWARE),
	SOF_FLAG(SOF_TIMESTAMPING_RX_HARDWARE),
};

static struct socket_type socket_types[] = {
	{ "ip",		SOCK_RAW,	IPPROTO_EGP },
	{ "udp",	SOCK_DGRAM,	IPPROTO_UDP },
	{ "tcp",	SOCK_STREAM,	IPPROTO_TCP },
};

static struct test_case test_cases[] = {
	{ {}, {} },
	{
		{ so_timestamp: 1 },
		{ tstamp: true }
	},
	{
		{ so_timestampns: 1 },
		{ tstampns: true }
	},
	{
		{ so_timestamp: 1, so_timestampns: 1 },
		{ tstampns: true }
	},
	{
		{ so_timestamping: SOF_TIMESTAMPING_RX_SOFTWARE },
		{}
	},
	{
		/* Loopback device does not support hw timestamps. */
		{ so_timestamping: SOF_TIMESTAMPING_RX_HARDWARE },
		{}
	},
	{
		{ so_timestamping: SOF_TIMESTAMPING_SOFTWARE },
		{}
	},
	{
		{ so_timestamping: SOF_TIMESTAMPING_RX_SOFTWARE
			| SOF_TIMESTAMPING_RX_HARDWARE },
		{}
	},
	{
		{ so_timestamping: SOF_TIMESTAMPING_SOFTWARE
			| SOF_TIMESTAMPING_RX_SOFTWARE },
		{ swtstamp: true }
	},
	{
		{ so_timestamp: 1, so_timestamping: SOF_TIMESTAMPING_SOFTWARE
			| SOF_TIMESTAMPING_RX_SOFTWARE },
		{ tstamp: true, swtstamp: true }
	},
};

static struct option long_options[] = {
	{ "list_tests", no_argument, 0, 'l' },
	{ "test_num", required_argument, 0, 'n' },
	{ "op_size", required_argument, 0, 's' },
	{ "tcp", no_argument, 0, 't' },
	{ "udp", no_argument, 0, 'u' },
	{ "ip", no_argument, 0, 'i' },
	{ NULL, 0, NULL, 0 },
};

static int next_port = 19999;
static int op_size = 10 * 1024;

void print_test_case(struct test_case *t)
{
	int f = 0;

	printf("sockopts {");
	if (t->sockopt.so_timestamp)
		printf(" SO_TIMESTAMP ");
	if (t->sockopt.so_timestampns)
		printf(" SO_TIMESTAMPNS ");
	if (t->sockopt.so_timestamping) {
		printf(" SO_TIMESTAMPING: {");
		for (f = 0; f < ARRAY_SIZE(sof_flags); f++)
			if (t->sockopt.so_timestamping & sof_flags[f].mask)
				printf(" %s |", sof_flags[f].name);
		printf("}");
	}
	printf("} expected cmsgs: {");
	if (t->expected.tstamp)
		printf(" SCM_TIMESTAMP ");
	if (t->expected.tstampns)
		printf(" SCM_TIMESTAMPNS ");
	if (t->expected.swtstamp || t->expected.hwtstamp) {
		printf(" SCM_TIMESTAMPING {");
		if (t->expected.swtstamp)
			printf("0");
		if (t->expected.swtstamp && t->expected.hwtstamp)
			printf(",");
		if (t->expected.hwtstamp)
			printf("2");
		printf("}");
	}
	printf("}\n");
}

void do_send(int src)
{
	int r;
	char *buf = malloc(op_size);

	memset(buf, 'z', op_size);
	r = write(src, buf, op_size);
	if (r < 0)
		error(1, errno, "Failed to sendmsg");

	free(buf);
}

bool do_recv(int rcv, int read_size, struct tstamps expected)
{
	const int CMSG_SIZE = 1024;

	struct scm_timestamping *ts;
	struct tstamps actual = {};
	char cmsg_buf[CMSG_SIZE];
	struct iovec recv_iov;
	struct cmsghdr *cmsg;
	bool failed = false;
	struct msghdr hdr;
	int flags = 0;
	int r;

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_iov = &recv_iov;
	hdr.msg_iovlen = 1;
	recv_iov.iov_base = malloc(read_size);
	recv_iov.iov_len = read_size;

	hdr.msg_control = cmsg_buf;
	hdr.msg_controllen = sizeof(cmsg_buf);

	r = recvmsg(rcv, &hdr, flags);
	if (r < 0)
		error(1, errno, "Failed to recvmsg");
	if (r != read_size)
		error(1, 0, "Only received %d bytes of payload.", r);

	if (hdr.msg_flags & (MSG_TRUNC | MSG_CTRUNC))
		error(1, 0, "Message was truncated.");

	for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET)
			error(1, 0, "Unexpected cmsg_level %d",
			      cmsg->cmsg_level);
		switch (cmsg->cmsg_type) {
		case SCM_TIMESTAMP:
			actual.tstamp = true;
			break;
		case SCM_TIMESTAMPNS:
			actual.tstampns = true;
			break;
		case SCM_TIMESTAMPING:
			ts = (struct scm_timestamping *)CMSG_DATA(cmsg);
			actual.swtstamp = !!ts->ts[0].tv_sec;
			if (ts->ts[1].tv_sec != 0)
				error(0, 0, "ts[1] should not be set.");
			actual.hwtstamp = !!ts->ts[2].tv_sec;
			break;
		default:
			error(1, 0, "Unexpected cmsg_type %d", cmsg->cmsg_type);
		}
	}

#define VALIDATE(field) \
	do { \
		if (expected.field != actual.field) { \
			if (expected.field) \
				error(0, 0, "Expected " #field " to be set."); \
			else \
				error(0, 0, \
				      "Expected " #field " to not be set."); \
			failed = true; \
		} \
	} while (0)

	VALIDATE(tstamp);
	VALIDATE(tstampns);
	VALIDATE(swtstamp);
	VALIDATE(hwtstamp);
#undef VALIDATE

	free(recv_iov.iov_base);

	return failed;
}

void config_so_flags(int rcv, struct options o)
{
	int on = 1;

	if (setsockopt(rcv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		error(1, errno, "Failed to enable SO_REUSEADDR");

	if (o.so_timestamp &&
	    setsockopt(rcv, SOL_SOCKET, SO_TIMESTAMP,
		       &o.so_timestamp, sizeof(o.so_timestamp)) < 0)
		error(1, errno, "Failed to enable SO_TIMESTAMP");

	if (o.so_timestampns &&
	    setsockopt(rcv, SOL_SOCKET, SO_TIMESTAMPNS,
		       &o.so_timestampns, sizeof(o.so_timestampns)) < 0)
		error(1, errno, "Failed to enable SO_TIMESTAMPNS");

	if (o.so_timestamping &&
	    setsockopt(rcv, SOL_SOCKET, SO_TIMESTAMPING,
		       &o.so_timestamping, sizeof(o.so_timestamping)) < 0)
		error(1, errno, "Failed to set SO_TIMESTAMPING");
}

bool run_test_case(struct socket_type s, struct test_case t)
{
	int port = (s.type == SOCK_RAW) ? 0 : next_port++;
	int read_size = op_size;
	struct sockaddr_in addr;
	bool failed = false;
	int src, dst, rcv;

	src = socket(AF_INET, s.type, s.protocol);
	if (src < 0)
		error(1, errno, "Failed to open src socket");

	dst = socket(AF_INET, s.type, s.protocol);
	if (dst < 0)
		error(1, errno, "Failed to open dst socket");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (bind(dst, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		error(1, errno, "Failed to bind to port %d", port);

	if (s.type == SOCK_STREAM && (listen(dst, 1) < 0))
		error(1, errno, "Failed to listen");

	if (connect(src, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		error(1, errno, "Failed to connect");

	if (s.type == SOCK_STREAM) {
		rcv = accept(dst, NULL, NULL);
		if (rcv < 0)
			error(1, errno, "Failed to accept");
		close(dst);
	} else {
		rcv = dst;
	}

	config_so_flags(rcv, t.sockopt);
	usleep(20000); /* setsockopt for SO_TIMESTAMPING is asynchronous */
	do_send(src);

	if (s.type == SOCK_RAW)
		read_size += 20;  /* for IP header */
	failed = do_recv(rcv, read_size, t.expected);

	close(rcv);
	close(src);

	return failed;
}

int main(int argc, char **argv)
{
	bool all_protocols = true;
	bool all_tests = true;
	int arg_index = 0;
	int failures = 0;
	int s, t, opt;

	while ((opt = getopt_long(argc, argv, "", long_options,
				  &arg_index)) != -1) {
		switch (opt) {
		case 'l':
			for (t = 0; t < ARRAY_SIZE(test_cases); t++) {
				printf("%d\t", t);
				print_test_case(&test_cases[t]);
			}
			return 0;
		case 'n':
			t = atoi(optarg);
			if (t >= ARRAY_SIZE(test_cases))
				error(1, 0, "Invalid test case: %d", t);
			all_tests = false;
			test_cases[t].enabled = true;
			break;
		case 's':
			op_size = atoi(optarg);
			break;
		case 't':
			all_protocols = false;
			socket_types[2].enabled = true;
			break;
		case 'u':
			all_protocols = false;
			socket_types[1].enabled = true;
			break;
		case 'i':
			all_protocols = false;
			socket_types[0].enabled = true;
			break;
		default:
			error(1, 0, "Failed to parse parameters.");
		}
	}

	for (s = 0; s < ARRAY_SIZE(socket_types); s++) {
		if (!all_protocols && !socket_types[s].enabled)
			continue;

		printf("Testing %s...\n", socket_types[s].friendly_name);
		for (t = 0; t < ARRAY_SIZE(test_cases); t++) {
			if (!all_tests && !test_cases[t].enabled)
				continue;

			printf("Starting testcase %d...\n", t);
			if (run_test_case(socket_types[s], test_cases[t])) {
				failures++;
				printf("FAILURE in test case ");
				print_test_case(&test_cases[t]);
			}
		}
	}
	if (!failures)
		printf("PASSED.\n");
	return failures;
}
